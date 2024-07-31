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
// File: memory.h
// -----------------------------------------------------------------------------
//
// This header file contains utility functions for managing the creation and
// conversion of smart pointers. This file is an extension to the C++
// standard <memory> library header file.

#ifndef ABSL_MEMORY_MEMORY_H_
#define ABSL_MEMORY_MEMORY_H_

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/macros.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// -----------------------------------------------------------------------------
// Function Template: WrapUnique()
// -----------------------------------------------------------------------------
//
// Adopts ownership from a raw pointer and transfers it to the returned
// `std::unique_ptr`, whose type is deduced. Because of this deduction, *do not*
// specify the template type `T` when calling `WrapUnique`.
//
// Example:
//   X* NewX(int, int);
//   auto x = WrapUnique(NewX(1, 2));  // 'x' is std::unique_ptr<X>.
//
// Do not call WrapUnique with an explicit type, as in
// `WrapUnique<X>(NewX(1, 2))`.  The purpose of WrapUnique is to automatically
// deduce the pointer type. If you wish to make the type explicit, just use
// `std::unique_ptr` directly.
//
//   auto x = std::unique_ptr<X>(NewX(1, 2));
//                  - or -
//   std::unique_ptr<X> x(NewX(1, 2));
//
// While `absl::WrapUnique` is useful for capturing the output of a raw
// pointer factory, prefer 'absl::make_unique<T>(args...)' over
// 'absl::WrapUnique(new T(args...))'.
//
//   auto x = WrapUnique(new X(1, 2));  // works, but nonideal.
//   auto x = make_unique<X>(1, 2);     // safer, standard, avoids raw 'new'.
//
// Note that `absl::WrapUnique(p)` is valid only if `delete p` is a valid
// expression. In particular, `absl::WrapUnique()` cannot wrap pointers to
// arrays, functions or void, and it must not be used to capture pointers
// obtained from array-new expressions (even though that would compile!).
template <typename T>
std::unique_ptr<T> WrapUnique(T* ptr) {
  static_assert(!std::is_array<T>::value, "array types are unsupported");
  static_assert(std::is_object<T>::value, "non-object types are unsupported");
  return std::unique_ptr<T>(ptr);
}

// -----------------------------------------------------------------------------
// Function Template: make_unique<T>()
// -----------------------------------------------------------------------------
//
// Creates a `std::unique_ptr<>`, while avoiding issues creating temporaries
// during the construction process. `absl::make_unique<>` also avoids redundant
// type declarations, by avoiding the need to explicitly use the `new` operator.
//
// https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique
//
// For more background on why `std::unique_ptr<T>(new T(a,b))` is problematic,
// see Herb Sutter's explanation on
// (Exception-Safe Function Calls)[https://herbsutter.com/gotw/_102/].
// (In general, reviewers should treat `new T(a,b)` with scrutiny.)
//
// Historical note: Abseil once provided a C++11 compatible implementation of
// the C++14's `std::make_unique`. Now that C++11 support has been sunsetted,
// `absl::make_unique` simply uses the STL-provided implementation. New code
// should use `std::make_unique`.
using std::make_unique;

// -----------------------------------------------------------------------------
// Function Template: RawPtr()
// -----------------------------------------------------------------------------
//
// Extracts the raw pointer from a pointer-like value `ptr`. `absl::RawPtr` is
// useful within templates that need to handle a complement of raw pointers,
// `std::nullptr_t`, and smart pointers.
template <typename T>
auto RawPtr(T&& ptr) -> decltype(std::addressof(*ptr)) {
  // ptr is a forwarding reference to support Ts with non-const operators.
  return (ptr != nullptr) ? std::addressof(*ptr) : nullptr;
}
inline std::nullptr_t RawPtr(std::nullptr_t) { return nullptr; }

// -----------------------------------------------------------------------------
// Function Template: ShareUniquePtr()
// -----------------------------------------------------------------------------
//
// Adopts a `std::unique_ptr` rvalue and returns a `std::shared_ptr` of deduced
// type. Ownership (if any) of the held value is transferred to the returned
// shared pointer.
//
// Example:
//
//     auto up = absl::make_unique<int>(10);
//     auto sp = absl::ShareUniquePtr(std::move(up));  // shared_ptr<int>
//     CHECK_EQ(*sp, 10);
//     CHECK(up == nullptr);
//
// Note that this conversion is correct even when T is an array type, and more
// generally it works for *any* deleter of the `unique_ptr` (single-object
// deleter, array deleter, or any custom deleter), since the deleter is adopted
// by the shared pointer as well. The deleter is copied (unless it is a
// reference).
//
// Implements the resolution of [LWG 2415](http://wg21.link/lwg2415), by which a
// null shared pointer does not attempt to call the deleter.
template <typename T, typename D>
std::shared_ptr<T> ShareUniquePtr(std::unique_ptr<T, D>&& ptr) {
  return ptr ? std::shared_ptr<T>(std::move(ptr)) : std::shared_ptr<T>();
}

// -----------------------------------------------------------------------------
// Function Template: WeakenPtr()
// -----------------------------------------------------------------------------
//
// Creates a weak pointer associated with a given shared pointer. The returned
// value is a `std::weak_ptr` of deduced type.
//
// Example:
//
//    auto sp = std::make_shared<int>(10);
//    auto wp = absl::WeakenPtr(sp);
//    CHECK_EQ(sp.get(), wp.lock().get());
//    sp.reset();
//    CHECK(wp.lock() == nullptr);
//
template <typename T>
std::weak_ptr<T> WeakenPtr(const std::shared_ptr<T>& ptr) {
  return std::weak_ptr<T>(ptr);
}

// -----------------------------------------------------------------------------
// Class Template: pointer_traits
// -----------------------------------------------------------------------------
//
// Historical note: Abseil once provided an implementation of
// `std::pointer_traits` for platforms that had not yet provided it. Those
// platforms are no longer supported. New code should simply use
// `std::pointer_traits`.
using std::pointer_traits;

// -----------------------------------------------------------------------------
// Class Template: allocator_traits
// -----------------------------------------------------------------------------
//
// Historical note: Abseil once provided an implementation of
// `std::allocator_traits` for platforms that had not yet provided it. Those
// platforms are no longer supported. New code should simply use
// `std::allocator_traits`.
using std::allocator_traits;

namespace memory_internal {

// ExtractOr<E, O, D>::type evaluates to E<O> if possible. Otherwise, D.
template <template <typename> class Extract, typename Obj, typename Default,
          typename>
struct ExtractOr {
  using type = Default;
};

template <template <typename> class Extract, typename Obj, typename Default>
struct ExtractOr<Extract, Obj, Default, void_t<Extract<Obj>>> {
  using type = Extract<Obj>;
};

template <template <typename> class Extract, typename Obj, typename Default>
using ExtractOrT = typename ExtractOr<Extract, Obj, Default, void>::type;

// This template alias transforms Alloc::is_nothrow into a metafunction with
// Alloc as a parameter so it can be used with ExtractOrT<>.
template <typename Alloc>
using GetIsNothrow = typename Alloc::is_nothrow;

}  // namespace memory_internal

// ABSL_ALLOCATOR_NOTHROW is a build time configuration macro for user to
// specify whether the default allocation function can throw or never throws.
// If the allocation function never throws, user should define it to a non-zero
// value (e.g. via `-DABSL_ALLOCATOR_NOTHROW`).
// If the allocation function can throw, user should leave it undefined or
// define it to zero.
//
// allocator_is_nothrow<Alloc> is a traits class that derives from
// Alloc::is_nothrow if present, otherwise std::false_type. It's specialized
// for Alloc = std::allocator<T> for any type T according to the state of
// ABSL_ALLOCATOR_NOTHROW.
//
// default_allocator_is_nothrow is a class that derives from std::true_type
// when the default allocator (global operator new) never throws, and
// std::false_type when it can throw. It is a convenience shorthand for writing
// allocator_is_nothrow<std::allocator<T>> (T can be any type).
// NOTE: allocator_is_nothrow<std::allocator<T>> is guaranteed to derive from
// the same type for all T, because users should specialize neither
// allocator_is_nothrow nor std::allocator.
template <typename Alloc>
struct allocator_is_nothrow
    : memory_internal::ExtractOrT<memory_internal::GetIsNothrow, Alloc,
                                  std::false_type> {};

#if defined(ABSL_ALLOCATOR_NOTHROW) && ABSL_ALLOCATOR_NOTHROW
template <typename T>
struct allocator_is_nothrow<std::allocator<T>> : std::true_type {};
struct default_allocator_is_nothrow : std::true_type {};
#else
struct default_allocator_is_nothrow : std::false_type {};
#endif

namespace memory_internal {
template <typename Allocator, typename Iterator, typename... Args>
void ConstructRange(Allocator& alloc, Iterator first, Iterator last,
                    const Args&... args) {
  for (Iterator cur = first; cur != last; ++cur) {
    ABSL_INTERNAL_TRY {
      std::allocator_traits<Allocator>::construct(alloc, std::addressof(*cur),
                                                  args...);
    }
    ABSL_INTERNAL_CATCH_ANY {
      while (cur != first) {
        --cur;
        std::allocator_traits<Allocator>::destroy(alloc, std::addressof(*cur));
      }
      ABSL_INTERNAL_RETHROW;
    }
  }
}

template <typename Allocator, typename Iterator, typename InputIterator>
void CopyRange(Allocator& alloc, Iterator destination, InputIterator first,
               InputIterator last) {
  for (Iterator cur = destination; first != last;
       static_cast<void>(++cur), static_cast<void>(++first)) {
    ABSL_INTERNAL_TRY {
      std::allocator_traits<Allocator>::construct(alloc, std::addressof(*cur),
                                                  *first);
    }
    ABSL_INTERNAL_CATCH_ANY {
      while (cur != destination) {
        --cur;
        std::allocator_traits<Allocator>::destroy(alloc, std::addressof(*cur));
      }
      ABSL_INTERNAL_RETHROW;
    }
  }
}
}  // namespace memory_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_MEMORY_MEMORY_H_
