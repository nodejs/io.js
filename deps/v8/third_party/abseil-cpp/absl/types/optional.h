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
// -----------------------------------------------------------------------------
// optional.h
// -----------------------------------------------------------------------------
//
// This header file defines the `absl::optional` type for holding a value which
// may or may not be present. This type is useful for providing value semantics
// for operations that may either wish to return or hold "something-or-nothing".
//
// Example:
//
//   // A common way to signal operation failure is to provide an output
//   // parameter and a bool return type:
//   bool AcquireResource(const Input&, Resource * out);
//
//   // Providing an absl::optional return type provides a cleaner API:
//   absl::optional<Resource> AcquireResource(const Input&);
//
// `absl::optional` is a C++11 compatible version of the C++17 `std::optional`
// abstraction and is designed to be a drop-in replacement for code compliant
// with C++17.
#ifndef ABSL_TYPES_OPTIONAL_H_
#define ABSL_TYPES_OPTIONAL_H_

#include "absl/base/config.h"   // TODO(calabrese) IWYU removal?
#include "absl/utility/utility.h"

#ifdef ABSL_USES_STD_OPTIONAL

#include <optional>  // IWYU pragma: export

namespace absl {
ABSL_NAMESPACE_BEGIN
using std::bad_optional_access;
using std::optional;
using std::make_optional;
using std::nullopt_t;
using std::nullopt;
ABSL_NAMESPACE_END
}  // namespace absl

#else  // ABSL_USES_STD_OPTIONAL

#include <cassert>
#include <functional>
#include <initializer_list>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/internal/inline_variable.h"
#include "absl/meta/type_traits.h"
#include "absl/types/bad_optional_access.h"
#include "absl/types/internal/optional.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// nullopt_t
//
// Class type for `absl::nullopt` used to indicate an `absl::optional<T>` type
// that does not contain a value.
struct nullopt_t {
  // It must not be default-constructible to avoid ambiguity for opt = {}.
  explicit constexpr nullopt_t(optional_internal::init_t) noexcept {}
};

// nullopt
//
// A tag constant of type `absl::nullopt_t` used to indicate an empty
// `absl::optional` in certain functions, such as construction or assignment.
ABSL_INTERNAL_INLINE_CONSTEXPR(nullopt_t, nullopt,
                               nullopt_t(optional_internal::init_t()));

// -----------------------------------------------------------------------------
// absl::optional
// -----------------------------------------------------------------------------
//
// A value of type `absl::optional<T>` holds either a value of `T` or an
// "empty" value.  When it holds a value of `T`, it stores it as a direct
// sub-object, so `sizeof(optional<T>)` is approximately
// `sizeof(T) + sizeof(bool)`.
//
// This implementation is based on the specification in the latest draft of the
// C++17 `std::optional` specification as of May 2017, section 20.6.
//
// Differences between `absl::optional<T>` and `std::optional<T>` include:
//
//    * `constexpr` is not used for non-const member functions.
//      (dependency on some differences between C++11 and C++14.)
//    * `absl::nullopt` and `absl::in_place` are not declared `constexpr`. We
//      need the inline variable support in C++17 for external linkage.
//    * Throws `absl::bad_optional_access` instead of
//      `std::bad_optional_access`.
//    * `make_optional()` cannot be declared `constexpr` due to the absence of
//      guaranteed copy elision.
//    * The move constructor's `noexcept` specification is stronger, i.e. if the
//      default allocator is non-throwing (via setting
//      `ABSL_ALLOCATOR_NOTHROW`), it evaluates to `noexcept(true)`, because
//      we assume
//       a) move constructors should only throw due to allocation failure and
//       b) if T's move constructor allocates, it uses the same allocation
//          function as the default allocator.
//
template <typename T>
class optional : private optional_internal::optional_data<T>,
                 private optional_internal::optional_ctor_base<
                     optional_internal::ctor_copy_traits<T>::traits>,
                 private optional_internal::optional_assign_base<
                     optional_internal::assign_copy_traits<T>::traits> {
  using data_base = optional_internal::optional_data<T>;

 public:
  typedef T value_type;

  // Constructors

  // Constructs an `optional` holding an empty value, NOT a default constructed
  // `T`.
  constexpr optional() noexcept = default;

  // Constructs an `optional` initialized with `nullopt` to hold an empty value.
  constexpr optional(nullopt_t) noexcept {}  // NOLINT(runtime/explicit)

  // Copy constructor, standard semantics
  optional(const optional&) = default;

  // Move constructor, standard semantics
  optional(optional&&) = default;

  // Constructs a non-empty `optional` direct-initialized value of type `T` from
  // the arguments `std::forward<Args>(args)...`  within the `optional`.
  // (The `in_place_t` is a tag used to indicate that the contained object
  // should be constructed in-place.)
  template <typename InPlaceT, typename... Args,
            absl::enable_if_t<absl::conjunction<
                std::is_same<InPlaceT, in_place_t>,
                std::is_constructible<T, Args&&...> >::value>* = nullptr>
  constexpr explicit optional(InPlaceT, Args&&... args)
      : data_base(in_place_t(), std::forward<Args>(args)...) {}

  // Constructs a non-empty `optional` direct-initialized value of type `T` from
  // the arguments of an initializer_list and `std::forward<Args>(args)...`.
  // (The `in_place_t` is a tag used to indicate that the contained object
  // should be constructed in-place.)
  template <typename U, typename... Args,
            typename = typename std::enable_if<std::is_constructible<
                T, std::initializer_list<U>&, Args&&...>::value>::type>
  constexpr explicit optional(in_place_t, std::initializer_list<U> il,
                              Args&&... args)
      : data_base(in_place_t(), il, std::forward<Args>(args)...) {}

  // Value constructor (implicit)
  template <
      typename U = T,
      typename std::enable_if<
          absl::conjunction<absl::negation<std::is_same<
                                in_place_t, typename std::decay<U>::type> >,
                            absl::negation<std::is_same<
                                optional<T>, typename std::decay<U>::type> >,
                            std::is_convertible<U&&, T>,
                            std::is_constructible<T, U&&> >::value,
          bool>::type = false>
  constexpr optional(U&& v) : data_base(in_place_t(), std::forward<U>(v)) {}

  // Value constructor (explicit)
  template <
      typename U = T,
      typename std::enable_if<
          absl::conjunction<absl::negation<std::is_same<
                                in_place_t, typename std::decay<U>::type> >,
                            absl::negation<std::is_same<
                                optional<T>, typename std::decay<U>::type> >,
                            absl::negation<std::is_convertible<U&&, T> >,
                            std::is_constructible<T, U&&> >::value,
          bool>::type = false>
  explicit constexpr optional(U&& v)
      : data_base(in_place_t(), std::forward<U>(v)) {}

  // Converting copy constructor (implicit)
  template <typename U,
            typename std::enable_if<
                absl::conjunction<
                    absl::negation<std::is_same<T, U> >,
                    std::is_constructible<T, const U&>,
                    absl::negation<
                        optional_internal::
                            is_constructible_convertible_from_optional<T, U> >,
                    std::is_convertible<const U&, T> >::value,
                bool>::type = false>
  optional(const optional<U>& rhs) {
    if (rhs) {
      this->construct(*rhs);
    }
  }

  // Converting copy constructor (explicit)
  template <typename U,
            typename std::enable_if<
                absl::conjunction<
                    absl::negation<std::is_same<T, U>>,
                    std::is_constructible<T, const U&>,
                    absl::negation<
                        optional_internal::
                            is_constructible_convertible_from_optional<T, U>>,
                    absl::negation<std::is_convertible<const U&, T>>>::value,
                bool>::type = false>
  explicit optional(const optional<U>& rhs) {
    if (rhs) {
      this->construct(*rhs);
    }
  }

  // Converting move constructor (implicit)
  template <typename U,
            typename std::enable_if<
                absl::conjunction<
                    absl::negation<std::is_same<T, U> >,
                    std::is_constructible<T, U&&>,
                    absl::negation<
                        optional_internal::
                            is_constructible_convertible_from_optional<T, U> >,
                    std::is_convertible<U&&, T> >::value,
                bool>::type = false>
  optional(optional<U>&& rhs) {
    if (rhs) {
      this->construct(std::move(*rhs));
    }
  }

  // Converting move constructor (explicit)
  template <
      typename U,
      typename std::enable_if<
          absl::conjunction<
              absl::negation<std::is_same<T, U>>, std::is_constructible<T, U&&>,
              absl::negation<
                  optional_internal::is_constructible_convertible_from_optional<
                      T, U>>,
              absl::negation<std::is_convertible<U&&, T>>>::value,
          bool>::type = false>
  explicit optional(optional<U>&& rhs) {
    if (rhs) {
      this->construct(std::move(*rhs));
    }
  }

  // Destructor. Trivial if `T` is trivially destructible.
  ~optional() = default;

  // Assignment Operators

  // Assignment from `nullopt`
  //
  // Example:
  //
  //   struct S { int value; };
  //   optional<S> opt = absl::nullopt;  // Could also use opt = { };
  optional& operator=(nullopt_t) noexcept {
    this->destruct();
    return *this;
  }

  // Copy assignment operator, standard semantics
  optional& operator=(const optional& src) = default;

  // Move assignment operator, standard semantics
  optional& operator=(optional&& src) = default;

  // Value assignment operators
  template <typename U = T,
            int&...,  // Workaround an internal compiler error in GCC 5 to 10.
            typename = typename std::enable_if<absl::conjunction<
                absl::negation<
                    std::is_same<optional<T>, typename std::decay<U>::type> >,
                absl::negation<absl::conjunction<
                    std::is_scalar<T>,
                    std::is_same<T, typename std::decay<U>::type> > >,
                std::is_constructible<T, U>,
                std::is_assignable<T&, U> >::value>::type>
  optional& operator=(U&& v) {
    this->assign(std::forward<U>(v));
    return *this;
  }

  template <
      typename U,
      int&...,  // Workaround an internal compiler error in GCC 5 to 10.
      typename = typename std::enable_if<absl::conjunction<
          absl::negation<std::is_same<T, U> >,
          std::is_constructible<T, const U&>, std::is_assignable<T&, const U&>,
          absl::negation<
              optional_internal::
                  is_constructible_convertible_assignable_from_optional<
                      T, U> > >::value>::type>
  optional& operator=(const optional<U>& rhs) {
    if (rhs) {
      this->assign(*rhs);
    } else {
      this->destruct();
    }
    return *this;
  }

  template <typename U,
            int&...,  // Workaround an internal compiler error in GCC 5 to 10.
            typename = typename std::enable_if<absl::conjunction<
                absl::negation<std::is_same<T, U> >,
                std::is_constructible<T, U>, std::is_assignable<T&, U>,
                absl::negation<
                    optional_internal::
                        is_constructible_convertible_assignable_from_optional<
                            T, U> > >::value>::type>
  optional& operator=(optional<U>&& rhs) {
    if (rhs) {
      this->assign(std::move(*rhs));
    } else {
      this->destruct();
    }
    return *this;
  }

  // Modifiers

  // optional::reset()
  //
  // Destroys the inner `T` value of an `absl::optional` if one is present.
  ABSL_ATTRIBUTE_REINITIALIZES void reset() noexcept { this->destruct(); }

  // optional::emplace()
  //
  // (Re)constructs the underlying `T` in-place with the given forwarded
  // arguments.
  //
  // Example:
  //
  //   optional<Foo> opt;
  //   opt.emplace(arg1,arg2,arg3);  // Constructs Foo(arg1,arg2,arg3)
  //
  // If the optional is non-empty, and the `args` refer to subobjects of the
  // current object, then behaviour is undefined, because the current object
  // will be destructed before the new object is constructed with `args`.
  template <typename... Args,
            typename = typename std::enable_if<
                std::is_constructible<T, Args&&...>::value>::type>
  T& emplace(Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    this->destruct();
    this->construct(std::forward<Args>(args)...);
    return reference();
  }

  // Emplace reconstruction overload for an initializer list and the given
  // forwarded arguments.
  //
  // Example:
  //
  //   struct Foo {
  //     Foo(std::initializer_list<int>);
  //   };
  //
  //   optional<Foo> opt;
  //   opt.emplace({1,2,3});  // Constructs Foo({1,2,3})
  template <typename U, typename... Args,
            typename = typename std::enable_if<std::is_constructible<
                T, std::initializer_list<U>&, Args&&...>::value>::type>
  T& emplace(std::initializer_list<U> il,
             Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    this->destruct();
    this->construct(il, std::forward<Args>(args)...);
    return reference();
  }

  // Swaps

  // Swap, standard semantics
  void swap(optional& rhs) noexcept(
      std::is_nothrow_move_constructible<T>::value&&
          type_traits_internal::IsNothrowSwappable<T>::value) {
    if (*this) {
      if (rhs) {
        type_traits_internal::Swap(**this, *rhs);
      } else {
        rhs.construct(std::move(**this));
        this->destruct();
      }
    } else {
      if (rhs) {
        this->construct(std::move(*rhs));
        rhs.destruct();
      } else {
        // No effect (swap(disengaged, disengaged)).
      }
    }
  }

  // Observers

  // optional::operator->()
  //
  // Accesses the underlying `T` value's member `m` of an `optional`. If the
  // `optional` is empty, behavior is undefined.
  //
  // If you need myOpt->foo in constexpr, use (*myOpt).foo instead.
  absl::Nonnull<const T*> operator->() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(this->engaged_);
    return std::addressof(this->data_);
  }
  absl::Nonnull<T*> operator->() ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(this->engaged_);
    return std::addressof(this->data_);
  }

  // optional::operator*()
  //
  // Accesses the underlying `T` value of an `optional`. If the `optional` is
  // empty, behavior is undefined.
  constexpr const T& operator*() const& ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return ABSL_HARDENING_ASSERT(this->engaged_), reference();
  }
  T& operator*() & ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(this->engaged_);
    return reference();
  }
  constexpr const T&& operator*() const&& ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return ABSL_HARDENING_ASSERT(this->engaged_), std::move(reference());
  }
  T&& operator*() && ABSL_ATTRIBUTE_LIFETIME_BOUND {
    ABSL_HARDENING_ASSERT(this->engaged_);
    return std::move(reference());
  }

  // optional::operator bool()
  //
  // Returns false if and only if the `optional` is empty.
  //
  //   if (opt) {
  //     // do something with *opt or opt->;
  //   } else {
  //     // opt is empty.
  //   }
  //
  constexpr explicit operator bool() const noexcept { return this->engaged_; }

  // optional::has_value()
  //
  // Determines whether the `optional` contains a value. Returns `false` if and
  // only if `*this` is empty.
  constexpr bool has_value() const noexcept { return this->engaged_; }

// Suppress bogus warning on MSVC: MSVC complains call to reference() after
// throw_bad_optional_access() is unreachable.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702)
#endif  // _MSC_VER
  // optional::value()
  //
  // Returns a reference to an `optional`s underlying value. The constness
  // and lvalue/rvalue-ness of the `optional` is preserved to the view of
  // the `T` sub-object. Throws `absl::bad_optional_access` when the `optional`
  // is empty.
  constexpr const T& value() const& ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return static_cast<bool>(*this)
               ? reference()
               : (optional_internal::throw_bad_optional_access(), reference());
  }
  T& value() & ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return static_cast<bool>(*this)
               ? reference()
               : (optional_internal::throw_bad_optional_access(), reference());
  }
  T&& value() && ABSL_ATTRIBUTE_LIFETIME_BOUND {  // NOLINT(build/c++11)
    return std::move(
        static_cast<bool>(*this)
            ? reference()
            : (optional_internal::throw_bad_optional_access(), reference()));
  }
  constexpr const T&& value()
      const&& ABSL_ATTRIBUTE_LIFETIME_BOUND {  // NOLINT(build/c++11)
    return std::move(
        static_cast<bool>(*this)
            ? reference()
            : (optional_internal::throw_bad_optional_access(), reference()));
  }
#ifdef _MSC_VER
#pragma warning(pop)
#endif  // _MSC_VER

  // optional::value_or()
  //
  // Returns either the value of `T` or a passed default `v` if the `optional`
  // is empty.
  template <typename U>
  constexpr T value_or(U&& v) const& {
    static_assert(std::is_copy_constructible<value_type>::value,
                  "optional<T>::value_or: T must be copy constructible");
    static_assert(std::is_convertible<U&&, value_type>::value,
                  "optional<T>::value_or: U must be convertible to T");
    return static_cast<bool>(*this) ? **this
                                    : static_cast<T>(std::forward<U>(v));
  }
  template <typename U>
  T value_or(U&& v) && {  // NOLINT(build/c++11)
    static_assert(std::is_move_constructible<value_type>::value,
                  "optional<T>::value_or: T must be move constructible");
    static_assert(std::is_convertible<U&&, value_type>::value,
                  "optional<T>::value_or: U must be convertible to T");
    return static_cast<bool>(*this) ? std::move(**this)
                                    : static_cast<T>(std::forward<U>(v));
  }

 private:
  // Private accessors for internal storage viewed as reference to T.
  constexpr const T& reference() const { return this->data_; }
  T& reference() { return this->data_; }

  // T constraint checks.  You can't have an optional of nullopt_t, in_place_t
  // or a reference.
  static_assert(
      !std::is_same<nullopt_t, typename std::remove_cv<T>::type>::value,
      "optional<nullopt_t> is not allowed.");
  static_assert(
      !std::is_same<in_place_t, typename std::remove_cv<T>::type>::value,
      "optional<in_place_t> is not allowed.");
  static_assert(!std::is_reference<T>::value,
                "optional<reference> is not allowed.");
};

// Non-member functions

// swap()
//
// Performs a swap between two `absl::optional` objects, using standard
// semantics.
template <typename T, typename std::enable_if<
                          std::is_move_constructible<T>::value &&
                              type_traits_internal::IsSwappable<T>::value,
                          bool>::type = false>
void swap(optional<T>& a, optional<T>& b) noexcept(noexcept(a.swap(b))) {
  a.swap(b);
}

// make_optional()
//
// Creates a non-empty `optional<T>` where the type of `T` is deduced. An
// `absl::optional` can also be explicitly instantiated with
// `make_optional<T>(v)`.
//
// Note: `make_optional()` constructions may be declared `constexpr` for
// trivially copyable types `T`. Non-trivial types require copy elision
// support in C++17 for `make_optional` to support `constexpr` on such
// non-trivial types.
//
// Example:
//
//   constexpr absl::optional<int> opt = absl::make_optional(1);
//   static_assert(opt.value() == 1, "");
template <typename T>
constexpr optional<typename std::decay<T>::type> make_optional(T&& v) {
  return optional<typename std::decay<T>::type>(std::forward<T>(v));
}

template <typename T, typename... Args>
constexpr optional<T> make_optional(Args&&... args) {
  return optional<T>(in_place_t(), std::forward<Args>(args)...);
}

template <typename T, typename U, typename... Args>
constexpr optional<T> make_optional(std::initializer_list<U> il,
                                    Args&&... args) {
  return optional<T>(in_place_t(), il, std::forward<Args>(args)...);
}

// Relational operators [optional.relops]

// Empty optionals are considered equal to each other and less than non-empty
// optionals. Supports relations between optional<T> and optional<U>, between
// optional<T> and U, and between optional<T> and nullopt.
//
// Note: We're careful to support T having non-bool relationals.

// Requires: The expression, e.g. "*x == *y" shall be well-formed and its result
// shall be convertible to bool.
// The C++17 (N4606) "Returns:" statements are translated into
// code in an obvious way here, and the original text retained as function docs.
// Returns: If bool(x) != bool(y), false; otherwise if bool(x) == false, true;
// otherwise *x == *y.
template <typename T, typename U>
constexpr auto operator==(const optional<T>& x, const optional<U>& y)
    -> decltype(optional_internal::convertible_to_bool(*x == *y)) {
  return static_cast<bool>(x) != static_cast<bool>(y)
             ? false
             : static_cast<bool>(x) == false ? true
                                             : static_cast<bool>(*x == *y);
}

// Returns: If bool(x) != bool(y), true; otherwise, if bool(x) == false, false;
// otherwise *x != *y.
template <typename T, typename U>
constexpr auto operator!=(const optional<T>& x, const optional<U>& y)
    -> decltype(optional_internal::convertible_to_bool(*x != *y)) {
  return static_cast<bool>(x) != static_cast<bool>(y)
             ? true
             : static_cast<bool>(x) == false ? false
                                             : static_cast<bool>(*x != *y);
}
// Returns: If !y, false; otherwise, if !x, true; otherwise *x < *y.
template <typename T, typename U>
constexpr auto operator<(const optional<T>& x, const optional<U>& y)
    -> decltype(optional_internal::convertible_to_bool(*x < *y)) {
  return !y ? false : !x ? true : static_cast<bool>(*x < *y);
}
// Returns: If !x, false; otherwise, if !y, true; otherwise *x > *y.
template <typename T, typename U>
constexpr auto operator>(const optional<T>& x, const optional<U>& y)
    -> decltype(optional_internal::convertible_to_bool(*x > *y)) {
  return !x ? false : !y ? true : static_cast<bool>(*x > *y);
}
// Returns: If !x, true; otherwise, if !y, false; otherwise *x <= *y.
template <typename T, typename U>
constexpr auto operator<=(const optional<T>& x, const optional<U>& y)
    -> decltype(optional_internal::convertible_to_bool(*x <= *y)) {
  return !x ? true : !y ? false : static_cast<bool>(*x <= *y);
}
// Returns: If !y, true; otherwise, if !x, false; otherwise *x >= *y.
template <typename T, typename U>
constexpr auto operator>=(const optional<T>& x, const optional<U>& y)
    -> decltype(optional_internal::convertible_to_bool(*x >= *y)) {
  return !y ? true : !x ? false : static_cast<bool>(*x >= *y);
}

// Comparison with nullopt [optional.nullops]
// The C++17 (N4606) "Returns:" statements are used directly here.
template <typename T>
constexpr bool operator==(const optional<T>& x, nullopt_t) noexcept {
  return !x;
}
template <typename T>
constexpr bool operator==(nullopt_t, const optional<T>& x) noexcept {
  return !x;
}
template <typename T>
constexpr bool operator!=(const optional<T>& x, nullopt_t) noexcept {
  return static_cast<bool>(x);
}
template <typename T>
constexpr bool operator!=(nullopt_t, const optional<T>& x) noexcept {
  return static_cast<bool>(x);
}
template <typename T>
constexpr bool operator<(const optional<T>&, nullopt_t) noexcept {
  return false;
}
template <typename T>
constexpr bool operator<(nullopt_t, const optional<T>& x) noexcept {
  return static_cast<bool>(x);
}
template <typename T>
constexpr bool operator<=(const optional<T>& x, nullopt_t) noexcept {
  return !x;
}
template <typename T>
constexpr bool operator<=(nullopt_t, const optional<T>&) noexcept {
  return true;
}
template <typename T>
constexpr bool operator>(const optional<T>& x, nullopt_t) noexcept {
  return static_cast<bool>(x);
}
template <typename T>
constexpr bool operator>(nullopt_t, const optional<T>&) noexcept {
  return false;
}
template <typename T>
constexpr bool operator>=(const optional<T>&, nullopt_t) noexcept {
  return true;
}
template <typename T>
constexpr bool operator>=(nullopt_t, const optional<T>& x) noexcept {
  return !x;
}

// Comparison with T [optional.comp_with_t]

// Requires: The expression, e.g. "*x == v" shall be well-formed and its result
// shall be convertible to bool.
// The C++17 (N4606) "Equivalent to:" statements are used directly here.
template <typename T, typename U>
constexpr auto operator==(const optional<T>& x, const U& v)
    -> decltype(optional_internal::convertible_to_bool(*x == v)) {
  return static_cast<bool>(x) ? static_cast<bool>(*x == v) : false;
}
template <typename T, typename U>
constexpr auto operator==(const U& v, const optional<T>& x)
    -> decltype(optional_internal::convertible_to_bool(v == *x)) {
  return static_cast<bool>(x) ? static_cast<bool>(v == *x) : false;
}
template <typename T, typename U>
constexpr auto operator!=(const optional<T>& x, const U& v)
    -> decltype(optional_internal::convertible_to_bool(*x != v)) {
  return static_cast<bool>(x) ? static_cast<bool>(*x != v) : true;
}
template <typename T, typename U>
constexpr auto operator!=(const U& v, const optional<T>& x)
    -> decltype(optional_internal::convertible_to_bool(v != *x)) {
  return static_cast<bool>(x) ? static_cast<bool>(v != *x) : true;
}
template <typename T, typename U>
constexpr auto operator<(const optional<T>& x, const U& v)
    -> decltype(optional_internal::convertible_to_bool(*x < v)) {
  return static_cast<bool>(x) ? static_cast<bool>(*x < v) : true;
}
template <typename T, typename U>
constexpr auto operator<(const U& v, const optional<T>& x)
    -> decltype(optional_internal::convertible_to_bool(v < *x)) {
  return static_cast<bool>(x) ? static_cast<bool>(v < *x) : false;
}
template <typename T, typename U>
constexpr auto operator<=(const optional<T>& x, const U& v)
    -> decltype(optional_internal::convertible_to_bool(*x <= v)) {
  return static_cast<bool>(x) ? static_cast<bool>(*x <= v) : true;
}
template <typename T, typename U>
constexpr auto operator<=(const U& v, const optional<T>& x)
    -> decltype(optional_internal::convertible_to_bool(v <= *x)) {
  return static_cast<bool>(x) ? static_cast<bool>(v <= *x) : false;
}
template <typename T, typename U>
constexpr auto operator>(const optional<T>& x, const U& v)
    -> decltype(optional_internal::convertible_to_bool(*x > v)) {
  return static_cast<bool>(x) ? static_cast<bool>(*x > v) : false;
}
template <typename T, typename U>
constexpr auto operator>(const U& v, const optional<T>& x)
    -> decltype(optional_internal::convertible_to_bool(v > *x)) {
  return static_cast<bool>(x) ? static_cast<bool>(v > *x) : true;
}
template <typename T, typename U>
constexpr auto operator>=(const optional<T>& x, const U& v)
    -> decltype(optional_internal::convertible_to_bool(*x >= v)) {
  return static_cast<bool>(x) ? static_cast<bool>(*x >= v) : false;
}
template <typename T, typename U>
constexpr auto operator>=(const U& v, const optional<T>& x)
    -> decltype(optional_internal::convertible_to_bool(v >= *x)) {
  return static_cast<bool>(x) ? static_cast<bool>(v >= *x) : true;
}

ABSL_NAMESPACE_END
}  // namespace absl

namespace std {

// std::hash specialization for absl::optional.
template <typename T>
struct hash<absl::optional<T> >
    : absl::optional_internal::optional_hash_base<T> {};

}  // namespace std

#undef ABSL_MSVC_CONSTEXPR_BUG_IN_UNION_LIKE_CLASS

#endif  // ABSL_USES_STD_OPTIONAL

#endif  // ABSL_TYPES_OPTIONAL_H_
