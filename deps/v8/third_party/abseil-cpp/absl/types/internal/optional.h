// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#ifndef ABSL_TYPES_INTERNAL_OPTIONAL_H_
#define ABSL_TYPES_INTERNAL_OPTIONAL_H_

#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/internal/inline_variable.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// Forward declaration
template <typename T>
class optional;

namespace optional_internal {

// This tag type is used as a constructor parameter type for `nullopt_t`.
struct init_t {
  explicit init_t() = default;
};

struct empty_struct {};

// This class stores the data in optional<T>.
// It is specialized based on whether T is trivially destructible.
// This is the specialization for non trivially destructible type.
template <typename T, bool unused = std::is_trivially_destructible<T>::value>
class optional_data_dtor_base {
  struct dummy_type {
    static_assert(sizeof(T) % sizeof(empty_struct) == 0, "");
    // Use an array to avoid GCC 6 placement-new warning.
    empty_struct data[sizeof(T) / sizeof(empty_struct)];
  };

 protected:
  // Whether there is data or not.
  bool engaged_;
  // Data storage
  union {
    T data_;
    dummy_type dummy_;
  };

  void destruct() noexcept {
    if (engaged_) {
      // `data_` must be initialized if `engaged_` is true.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
      data_.~T();
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic pop
#endif
      engaged_ = false;
    }
  }

  // dummy_ must be initialized for constexpr constructor.
  constexpr optional_data_dtor_base() noexcept : engaged_(false), dummy_{{}} {}

  template <typename... Args>
  constexpr explicit optional_data_dtor_base(in_place_t, Args&&... args)
      : engaged_(true), data_(std::forward<Args>(args)...) {}

  ~optional_data_dtor_base() { destruct(); }
};

// Specialization for trivially destructible type.
template <typename T>
class optional_data_dtor_base<T, true> {
  struct dummy_type {
    static_assert(sizeof(T) % sizeof(empty_struct) == 0, "");
    // Use array to avoid GCC 6 placement-new warning.
    empty_struct data[sizeof(T) / sizeof(empty_struct)];
  };

 protected:
  // Whether there is data or not.
  bool engaged_;
  // Data storage
  union {
    T data_;
    dummy_type dummy_;
  };
  void destruct() noexcept { engaged_ = false; }

  // dummy_ must be initialized for constexpr constructor.
  constexpr optional_data_dtor_base() noexcept : engaged_(false), dummy_{{}} {}

  template <typename... Args>
  constexpr explicit optional_data_dtor_base(in_place_t, Args&&... args)
      : engaged_(true), data_(std::forward<Args>(args)...) {}
};

template <typename T>
class optional_data_base : public optional_data_dtor_base<T> {
 protected:
  using base = optional_data_dtor_base<T>;
  using base::base;

  template <typename... Args>
  void construct(Args&&... args) {
    // Use dummy_'s address to work around casting cv-qualified T* to void*.
    ::new (static_cast<void*>(&this->dummy_)) T(std::forward<Args>(args)...);
    this->engaged_ = true;
  }

  template <typename U>
  void assign(U&& u) {
    if (this->engaged_) {
      this->data_ = std::forward<U>(u);
    } else {
      construct(std::forward<U>(u));
    }
  }
};

// TODO(absl-team): Add another class using
// std::is_trivially_move_constructible trait when available to match
// http://cplusplus.github.io/LWG/lwg-defects.html#2900, for types that
// have trivial move but nontrivial copy.
// Also, we should be checking is_trivially_copyable here, which is not
// supported now, so we use is_trivially_* traits instead.
template <typename T,
          bool unused = absl::is_trivially_copy_constructible<T>::value&&
              absl::is_trivially_copy_assignable<typename std::remove_cv<
                  T>::type>::value&& std::is_trivially_destructible<T>::value>
class optional_data;

// Trivially copyable types
template <typename T>
class optional_data<T, true> : public optional_data_base<T> {
 protected:
  using optional_data_base<T>::optional_data_base;
};

template <typename T>
class optional_data<T, false> : public optional_data_base<T> {
 protected:
  using optional_data_base<T>::optional_data_base;

  optional_data() = default;

  optional_data(const optional_data& rhs) : optional_data_base<T>() {
    if (rhs.engaged_) {
      this->construct(rhs.data_);
    }
  }

  optional_data(optional_data&& rhs) noexcept(
      absl::default_allocator_is_nothrow::value ||
      std::is_nothrow_move_constructible<T>::value)
      : optional_data_base<T>() {
    if (rhs.engaged_) {
      this->construct(std::move(rhs.data_));
    }
  }

  optional_data& operator=(const optional_data& rhs) {
    if (rhs.engaged_) {
      this->assign(rhs.data_);
    } else {
      this->destruct();
    }
    return *this;
  }

  optional_data& operator=(optional_data&& rhs) noexcept(
      std::is_nothrow_move_assignable<T>::value&&
          std::is_nothrow_move_constructible<T>::value) {
    if (rhs.engaged_) {
      this->assign(std::move(rhs.data_));
    } else {
      this->destruct();
    }
    return *this;
  }
};

// Ordered by level of restriction, from low to high.
// Copyable implies movable.
enum class copy_traits { copyable = 0, movable = 1, non_movable = 2 };

// Base class for enabling/disabling copy/move constructor.
template <copy_traits>
class optional_ctor_base;

template <>
class optional_ctor_base<copy_traits::copyable> {
 public:
  constexpr optional_ctor_base() = default;
  optional_ctor_base(const optional_ctor_base&) = default;
  optional_ctor_base(optional_ctor_base&&) = default;
  optional_ctor_base& operator=(const optional_ctor_base&) = default;
  optional_ctor_base& operator=(optional_ctor_base&&) = default;
};

template <>
class optional_ctor_base<copy_traits::movable> {
 public:
  constexpr optional_ctor_base() = default;
  optional_ctor_base(const optional_ctor_base&) = delete;
  optional_ctor_base(optional_ctor_base&&) = default;
  optional_ctor_base& operator=(const optional_ctor_base&) = default;
  optional_ctor_base& operator=(optional_ctor_base&&) = default;
};

template <>
class optional_ctor_base<copy_traits::non_movable> {
 public:
  constexpr optional_ctor_base() = default;
  optional_ctor_base(const optional_ctor_base&) = delete;
  optional_ctor_base(optional_ctor_base&&) = delete;
  optional_ctor_base& operator=(const optional_ctor_base&) = default;
  optional_ctor_base& operator=(optional_ctor_base&&) = default;
};

// Base class for enabling/disabling copy/move assignment.
template <copy_traits>
class optional_assign_base;

template <>
class optional_assign_base<copy_traits::copyable> {
 public:
  constexpr optional_assign_base() = default;
  optional_assign_base(const optional_assign_base&) = default;
  optional_assign_base(optional_assign_base&&) = default;
  optional_assign_base& operator=(const optional_assign_base&) = default;
  optional_assign_base& operator=(optional_assign_base&&) = default;
};

template <>
class optional_assign_base<copy_traits::movable> {
 public:
  constexpr optional_assign_base() = default;
  optional_assign_base(const optional_assign_base&) = default;
  optional_assign_base(optional_assign_base&&) = default;
  optional_assign_base& operator=(const optional_assign_base&) = delete;
  optional_assign_base& operator=(optional_assign_base&&) = default;
};

template <>
class optional_assign_base<copy_traits::non_movable> {
 public:
  constexpr optional_assign_base() = default;
  optional_assign_base(const optional_assign_base&) = default;
  optional_assign_base(optional_assign_base&&) = default;
  optional_assign_base& operator=(const optional_assign_base&) = delete;
  optional_assign_base& operator=(optional_assign_base&&) = delete;
};

template <typename T>
struct ctor_copy_traits {
  static constexpr copy_traits traits =
      std::is_copy_constructible<T>::value
          ? copy_traits::copyable
          : std::is_move_constructible<T>::value ? copy_traits::movable
                                                 : copy_traits::non_movable;
};

template <typename T>
struct assign_copy_traits {
  static constexpr copy_traits traits =
      absl::is_copy_assignable<T>::value && std::is_copy_constructible<T>::value
          ? copy_traits::copyable
          : absl::is_move_assignable<T>::value &&
                    std::is_move_constructible<T>::value
                ? copy_traits::movable
                : copy_traits::non_movable;
};

// Whether T is constructible or convertible from optional<U>.
template <typename T, typename U>
struct is_constructible_convertible_from_optional
    : std::integral_constant<
          bool, std::is_constructible<T, optional<U>&>::value ||
                    std::is_constructible<T, optional<U>&&>::value ||
                    std::is_constructible<T, const optional<U>&>::value ||
                    std::is_constructible<T, const optional<U>&&>::value ||
                    std::is_convertible<optional<U>&, T>::value ||
                    std::is_convertible<optional<U>&&, T>::value ||
                    std::is_convertible<const optional<U>&, T>::value ||
                    std::is_convertible<const optional<U>&&, T>::value> {};

// Whether T is constructible or convertible or assignable from optional<U>.
template <typename T, typename U>
struct is_constructible_convertible_assignable_from_optional
    : std::integral_constant<
          bool, is_constructible_convertible_from_optional<T, U>::value ||
                    std::is_assignable<T&, optional<U>&>::value ||
                    std::is_assignable<T&, optional<U>&&>::value ||
                    std::is_assignable<T&, const optional<U>&>::value ||
                    std::is_assignable<T&, const optional<U>&&>::value> {};

// Helper function used by [optional.relops], [optional.comp_with_t],
// for checking whether an expression is convertible to bool.
bool convertible_to_bool(bool);

// Base class for std::hash<absl::optional<T>>:
// If std::hash<std::remove_const_t<T>> is enabled, it provides operator() to
// compute the hash; Otherwise, it is disabled.
// Reference N4659 23.14.15 [unord.hash].
template <typename T, typename = size_t>
struct optional_hash_base {
  optional_hash_base() = delete;
  optional_hash_base(const optional_hash_base&) = delete;
  optional_hash_base(optional_hash_base&&) = delete;
  optional_hash_base& operator=(const optional_hash_base&) = delete;
  optional_hash_base& operator=(optional_hash_base&&) = delete;
};

template <typename T>
struct optional_hash_base<T, decltype(std::hash<absl::remove_const_t<T> >()(
                                 std::declval<absl::remove_const_t<T> >()))> {
  using argument_type = absl::optional<T>;
  using result_type = size_t;
  size_t operator()(const absl::optional<T>& opt) const {
    absl::type_traits_internal::AssertHashEnabled<absl::remove_const_t<T>>();
    if (opt) {
      return std::hash<absl::remove_const_t<T> >()(*opt);
    } else {
      return static_cast<size_t>(0x297814aaad196e6dULL);
    }
  }
};

}  // namespace optional_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_INTERNAL_OPTIONAL_H_
