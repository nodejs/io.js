// Copyright 2020 The Abseil Authors.
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
// File: statusor.h
// -----------------------------------------------------------------------------
//
// An `absl::StatusOr<T>` represents a union of an `absl::Status` object
// and an object of type `T`. The `absl::StatusOr<T>` will either contain an
// object of type `T` (indicating a successful operation), or an error (of type
// `absl::Status`) explaining why such a value is not present.
//
// In general, check the success of an operation returning an
// `absl::StatusOr<T>` like you would an `absl::Status` by using the `ok()`
// member function.
//
// Example:
//
//   StatusOr<Foo> result = Calculation();
//   if (result.ok()) {
//     result->DoSomethingCool();
//   } else {
//     LOG(ERROR) << result.status();
//   }
#ifndef ABSL_STATUS_STATUSOR_H_
#define ABSL_STATUS_STATUSOR_H_

#include <exception>
#include <initializer_list>
#include <new>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/call_once.h"
#include "absl/meta/type_traits.h"
#include "absl/status/internal/statusor_internal.h"
#include "absl/status/status.h"
#include "absl/strings/has_absl_stringify.h"
#include "absl/strings/has_ostream_operator.h"
#include "absl/strings/str_format.h"
#include "absl/types/variant.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// BadStatusOrAccess
//
// This class defines the type of object to throw (if exceptions are enabled),
// when accessing the value of an `absl::StatusOr<T>` object that does not
// contain a value. This behavior is analogous to that of
// `std::bad_optional_access` in the case of accessing an invalid
// `std::optional` value.
//
// Example:
//
// try {
//   absl::StatusOr<int> v = FetchInt();
//   DoWork(v.value());  // Accessing value() when not "OK" may throw
// } catch (absl::BadStatusOrAccess& ex) {
//   LOG(ERROR) << ex.status();
// }
class BadStatusOrAccess : public std::exception {
 public:
  explicit BadStatusOrAccess(absl::Status status);
  ~BadStatusOrAccess() override = default;

  BadStatusOrAccess(const BadStatusOrAccess& other);
  BadStatusOrAccess& operator=(const BadStatusOrAccess& other);
  BadStatusOrAccess(BadStatusOrAccess&& other);
  BadStatusOrAccess& operator=(BadStatusOrAccess&& other);

  // BadStatusOrAccess::what()
  //
  // Returns the associated explanatory string of the `absl::StatusOr<T>`
  // object's error code. This function contains information about the failing
  // status, but its exact formatting may change and should not be depended on.
  //
  // The pointer of this string is guaranteed to be valid until any non-const
  // function is invoked on the exception object.
  absl::Nonnull<const char*> what() const noexcept override;

  // BadStatusOrAccess::status()
  //
  // Returns the associated `absl::Status` of the `absl::StatusOr<T>` object's
  // error.
  const absl::Status& status() const;

 private:
  void InitWhat() const;

  absl::Status status_;
  mutable absl::once_flag init_what_;
  mutable std::string what_;
};

// Returned StatusOr objects may not be ignored.
template <typename T>
#if ABSL_HAVE_CPP_ATTRIBUTE(nodiscard)
// TODO(b/176172494): ABSL_MUST_USE_RESULT should expand to the more strict
// [[nodiscard]]. For now, just use [[nodiscard]] directly when it is available.
class [[nodiscard]] StatusOr;
#else
class ABSL_MUST_USE_RESULT StatusOr;
#endif  // ABSL_HAVE_CPP_ATTRIBUTE(nodiscard)

// absl::StatusOr<T>
//
// The `absl::StatusOr<T>` class template is a union of an `absl::Status` object
// and an object of type `T`. The `absl::StatusOr<T>` models an object that is
// either a usable object, or an error (of type `absl::Status`) explaining why
// such an object is not present. An `absl::StatusOr<T>` is typically the return
// value of a function which may fail.
//
// An `absl::StatusOr<T>` can never hold an "OK" status (an
// `absl::StatusCode::kOk` value); instead, the presence of an object of type
// `T` indicates success. Instead of checking for a `kOk` value, use the
// `absl::StatusOr<T>::ok()` member function. (It is for this reason, and code
// readability, that using the `ok()` function is preferred for `absl::Status`
// as well.)
//
// Example:
//
//   StatusOr<Foo> result = DoBigCalculationThatCouldFail();
//   if (result.ok()) {
//     result->DoSomethingCool();
//   } else {
//     LOG(ERROR) << result.status();
//   }
//
// Accessing the object held by an `absl::StatusOr<T>` should be performed via
// `operator*` or `operator->`, after a call to `ok()` confirms that the
// `absl::StatusOr<T>` holds an object of type `T`:
//
// Example:
//
//   absl::StatusOr<int> i = GetCount();
//   if (i.ok()) {
//     updated_total += *i;
//   }
//
// NOTE: using `absl::StatusOr<T>::value()` when no valid value is present will
// throw an exception if exceptions are enabled or terminate the process when
// exceptions are not enabled.
//
// Example:
//
//   StatusOr<Foo> result = DoBigCalculationThatCouldFail();
//   const Foo& foo = result.value();    // Crash/exception if no value present
//   foo.DoSomethingCool();
//
// A `absl::StatusOr<T*>` can be constructed from a null pointer like any other
// pointer value, and the result will be that `ok()` returns `true` and
// `value()` returns `nullptr`. Checking the value of pointer in an
// `absl::StatusOr<T*>` generally requires a bit more care, to ensure both that
// a value is present and that value is not null:
//
//  StatusOr<std::unique_ptr<Foo>> result = FooFactory::MakeNewFoo(arg);
//  if (!result.ok()) {
//    LOG(ERROR) << result.status();
//  } else if (*result == nullptr) {
//    LOG(ERROR) << "Unexpected null pointer";
//  } else {
//    (*result)->DoSomethingCool();
//  }
//
// Example factory implementation returning StatusOr<T>:
//
//  StatusOr<Foo> FooFactory::MakeFoo(int arg) {
//    if (arg <= 0) {
//      return absl::Status(absl::StatusCode::kInvalidArgument,
//                          "Arg must be positive");
//    }
//    return Foo(arg);
//  }
template <typename T>
class StatusOr : private internal_statusor::StatusOrData<T>,
                 private internal_statusor::CopyCtorBase<T>,
                 private internal_statusor::MoveCtorBase<T>,
                 private internal_statusor::CopyAssignBase<T>,
                 private internal_statusor::MoveAssignBase<T> {
  template <typename U>
  friend class StatusOr;

  typedef internal_statusor::StatusOrData<T> Base;

 public:
  // StatusOr<T>::value_type
  //
  // This instance data provides a generic `value_type` member for use within
  // generic programming. This usage is analogous to that of
  // `optional::value_type` in the case of `std::optional`.
  typedef T value_type;

  // Constructors

  // Constructs a new `absl::StatusOr` with an `absl::StatusCode::kUnknown`
  // status. This constructor is marked 'explicit' to prevent usages in return
  // values such as 'return {};', under the misconception that
  // `absl::StatusOr<std::vector<int>>` will be initialized with an empty
  // vector, instead of an `absl::StatusCode::kUnknown` error code.
  explicit StatusOr();

  // `StatusOr<T>` is copy constructible if `T` is copy constructible.
  StatusOr(const StatusOr&) = default;
  // `StatusOr<T>` is copy assignable if `T` is copy constructible and copy
  // assignable.
  StatusOr& operator=(const StatusOr&) = default;

  // `StatusOr<T>` is move constructible if `T` is move constructible.
  StatusOr(StatusOr&&) = default;
  // `StatusOr<T>` is moveAssignable if `T` is move constructible and move
  // assignable.
  StatusOr& operator=(StatusOr&&) = default;

  // Converting Constructors

  // Constructs a new `absl::StatusOr<T>` from an `absl::StatusOr<U>`, when `T`
  // is constructible from `U`. To avoid ambiguity, these constructors are
  // disabled if `T` is also constructible from `StatusOr<U>.`. This constructor
  // is explicit if and only if the corresponding construction of `T` from `U`
  // is explicit. (This constructor inherits its explicitness from the
  // underlying constructor.)
  template <
      typename U,
      absl::enable_if_t<
          absl::conjunction<
              absl::negation<std::is_same<T, U>>,
              std::is_constructible<T, const U&>,
              std::is_convertible<const U&, T>,
              absl::negation<
                  internal_statusor::IsConstructibleOrConvertibleFromStatusOr<
                      T, U>>>::value,
          int> = 0>
  StatusOr(const StatusOr<U>& other)  // NOLINT
      : Base(static_cast<const typename StatusOr<U>::Base&>(other)) {}
  template <
      typename U,
      absl::enable_if_t<
          absl::conjunction<
              absl::negation<std::is_same<T, U>>,
              std::is_constructible<T, const U&>,
              absl::negation<std::is_convertible<const U&, T>>,
              absl::negation<
                  internal_statusor::IsConstructibleOrConvertibleFromStatusOr<
                      T, U>>>::value,
          int> = 0>
  explicit StatusOr(const StatusOr<U>& other)
      : Base(static_cast<const typename StatusOr<U>::Base&>(other)) {}

  template <
      typename U,
      absl::enable_if_t<
          absl::conjunction<
              absl::negation<std::is_same<T, U>>, std::is_constructible<T, U&&>,
              std::is_convertible<U&&, T>,
              absl::negation<
                  internal_statusor::IsConstructibleOrConvertibleFromStatusOr<
                      T, U>>>::value,
          int> = 0>
  StatusOr(StatusOr<U>&& other)  // NOLINT
      : Base(static_cast<typename StatusOr<U>::Base&&>(other)) {}
  template <
      typename U,
      absl::enable_if_t<
          absl::conjunction<
              absl::negation<std::is_same<T, U>>, std::is_constructible<T, U&&>,
              absl::negation<std::is_convertible<U&&, T>>,
              absl::negation<
                  internal_statusor::IsConstructibleOrConvertibleFromStatusOr<
                      T, U>>>::value,
          int> = 0>
  explicit StatusOr(StatusOr<U>&& other)
      : Base(static_cast<typename StatusOr<U>::Base&&>(other)) {}

  // Converting Assignment Operators

  // Creates an `absl::StatusOr<T>` through assignment from an
  // `absl::StatusOr<U>` when:
  //
  //   * Both `absl::StatusOr<T>` and `absl::StatusOr<U>` are OK by assigning
  //     `U` to `T` directly.
  //   * `absl::StatusOr<T>` is OK and `absl::StatusOr<U>` contains an error
  //      code by destroying `absl::StatusOr<T>`'s value and assigning from
  //      `absl::StatusOr<U>'
  //   * `absl::StatusOr<T>` contains an error code and `absl::StatusOr<U>` is
  //      OK by directly initializing `T` from `U`.
  //   * Both `absl::StatusOr<T>` and `absl::StatusOr<U>` contain an error
  //     code by assigning the `Status` in `absl::StatusOr<U>` to
  //     `absl::StatusOr<T>`
  //
  // These overloads only apply if `absl::StatusOr<T>` is constructible and
  // assignable from `absl::StatusOr<U>` and `StatusOr<T>` cannot be directly
  // assigned from `StatusOr<U>`.
  template <
      typename U,
      absl::enable_if_t<
          absl::conjunction<
              absl::negation<std::is_same<T, U>>,
              std::is_constructible<T, const U&>,
              std::is_assignable<T, const U&>,
              absl::negation<
                  internal_statusor::
                      IsConstructibleOrConvertibleOrAssignableFromStatusOr<
                          T, U>>>::value,
          int> = 0>
  StatusOr& operator=(const StatusOr<U>& other) {
    this->Assign(other);
    return *this;
  }
  template <
      typename U,
      absl::enable_if_t<
          absl::conjunction<
              absl::negation<std::is_same<T, U>>, std::is_constructible<T, U&&>,
              std::is_assignable<T, U&&>,
              absl::negation<
                  internal_statusor::
                      IsConstructibleOrConvertibleOrAssignableFromStatusOr<
                          T, U>>>::value,
          int> = 0>
  StatusOr& operator=(StatusOr<U>&& other) {
    this->Assign(std::move(other));
    return *this;
  }

  // Constructs a new `absl::StatusOr<T>` with a non-ok status. After calling
  // this constructor, `this->ok()` will be `false` and calls to `value()` will
  // crash, or produce an exception if exceptions are enabled.
  //
  // The constructor also takes any type `U` that is convertible to
  // `absl::Status`. This constructor is explicit if an only if `U` is not of
  // type `absl::Status` and the conversion from `U` to `Status` is explicit.
  //
  // REQUIRES: !Status(std::forward<U>(v)).ok(). This requirement is DCHECKed.
  // In optimized builds, passing absl::OkStatus() here will have the effect
  // of passing absl::StatusCode::kInternal as a fallback.
  template <
      typename U = absl::Status,
      absl::enable_if_t<
          absl::conjunction<
              std::is_convertible<U&&, absl::Status>,
              std::is_constructible<absl::Status, U&&>,
              absl::negation<std::is_same<absl::decay_t<U>, absl::StatusOr<T>>>,
              absl::negation<std::is_same<absl::decay_t<U>, T>>,
              absl::negation<std::is_same<absl::decay_t<U>, absl::in_place_t>>,
              absl::negation<internal_statusor::HasConversionOperatorToStatusOr<
                  T, U&&>>>::value,
          int> = 0>
  StatusOr(U&& v) : Base(std::forward<U>(v)) {}

  template <
      typename U = absl::Status,
      absl::enable_if_t<
          absl::conjunction<
              absl::negation<std::is_convertible<U&&, absl::Status>>,
              std::is_constructible<absl::Status, U&&>,
              absl::negation<std::is_same<absl::decay_t<U>, absl::StatusOr<T>>>,
              absl::negation<std::is_same<absl::decay_t<U>, T>>,
              absl::negation<std::is_same<absl::decay_t<U>, absl::in_place_t>>,
              absl::negation<internal_statusor::HasConversionOperatorToStatusOr<
                  T, U&&>>>::value,
          int> = 0>
  explicit StatusOr(U&& v) : Base(std::forward<U>(v)) {}

  template <
      typename U = absl::Status,
      absl::enable_if_t<
          absl::conjunction<
              std::is_convertible<U&&, absl::Status>,
              std::is_constructible<absl::Status, U&&>,
              absl::negation<std::is_same<absl::decay_t<U>, absl::StatusOr<T>>>,
              absl::negation<std::is_same<absl::decay_t<U>, T>>,
              absl::negation<std::is_same<absl::decay_t<U>, absl::in_place_t>>,
              absl::negation<internal_statusor::HasConversionOperatorToStatusOr<
                  T, U&&>>>::value,
          int> = 0>
  StatusOr& operator=(U&& v) {
    this->AssignStatus(std::forward<U>(v));
    return *this;
  }

  // Perfect-forwarding value assignment operator.

  // If `*this` contains a `T` value before the call, the contained value is
  // assigned from `std::forward<U>(v)`; Otherwise, it is directly-initialized
  // from `std::forward<U>(v)`.
  // This function does not participate in overload unless:
  // 1. `std::is_constructible_v<T, U>` is true,
  // 2. `std::is_assignable_v<T&, U>` is true.
  // 3. `std::is_same_v<StatusOr<T>, std::remove_cvref_t<U>>` is false.
  // 4. Assigning `U` to `T` is not ambiguous:
  //  If `U` is `StatusOr<V>` and `T` is constructible and assignable from
  //  both `StatusOr<V>` and `V`, the assignment is considered bug-prone and
  //  ambiguous thus will fail to compile. For example:
  //    StatusOr<bool> s1 = true;  // s1.ok() && *s1 == true
  //    StatusOr<bool> s2 = false;  // s2.ok() && *s2 == false
  //    s1 = s2;  // ambiguous, `s1 = *s2` or `s1 = bool(s2)`?
  template <
      typename U = T,
      typename = typename std::enable_if<absl::conjunction<
          std::is_constructible<T, U&&>, std::is_assignable<T&, U&&>,
          absl::disjunction<
              std::is_same<absl::remove_cvref_t<U>, T>,
              absl::conjunction<
                  absl::negation<std::is_convertible<U&&, absl::Status>>,
                  absl::negation<internal_statusor::
                                     HasConversionOperatorToStatusOr<T, U&&>>>>,
          internal_statusor::IsForwardingAssignmentValid<T, U&&>>::value>::type>
  StatusOr& operator=(U&& v) {
    this->Assign(std::forward<U>(v));
    return *this;
  }

  // Constructs the inner value `T` in-place using the provided args, using the
  // `T(args...)` constructor.
  template <typename... Args>
  explicit StatusOr(absl::in_place_t, Args&&... args);
  template <typename U, typename... Args>
  explicit StatusOr(absl::in_place_t, std::initializer_list<U> ilist,
                    Args&&... args);

  // Constructs the inner value `T` in-place using the provided args, using the
  // `T(U)` (direct-initialization) constructor. This constructor is only valid
  // if `T` can be constructed from a `U`. Can accept move or copy constructors.
  //
  // This constructor is explicit if `U` is not convertible to `T`. To avoid
  // ambiguity, this constructor is disabled if `U` is a `StatusOr<J>`, where
  // `J` is convertible to `T`.
  template <
      typename U = T,
      absl::enable_if_t<
          absl::conjunction<
              internal_statusor::IsDirectInitializationValid<T, U&&>,
              std::is_constructible<T, U&&>, std::is_convertible<U&&, T>,
              absl::disjunction<
                  std::is_same<absl::remove_cvref_t<U>, T>,
                  absl::conjunction<
                      absl::negation<std::is_convertible<U&&, absl::Status>>,
                      absl::negation<
                          internal_statusor::HasConversionOperatorToStatusOr<
                              T, U&&>>>>>::value,
          int> = 0>
  StatusOr(U&& u)  // NOLINT
      : StatusOr(absl::in_place, std::forward<U>(u)) {}

  template <
      typename U = T,
      absl::enable_if_t<
          absl::conjunction<
              internal_statusor::IsDirectInitializationValid<T, U&&>,
              absl::disjunction<
                  std::is_same<absl::remove_cvref_t<U>, T>,
                  absl::conjunction<
                      absl::negation<std::is_constructible<absl::Status, U&&>>,
                      absl::negation<
                          internal_statusor::HasConversionOperatorToStatusOr<
                              T, U&&>>>>,
              std::is_constructible<T, U&&>,
              absl::negation<std::is_convertible<U&&, T>>>::value,
          int> = 0>
  explicit StatusOr(U&& u)  // NOLINT
      : StatusOr(absl::in_place, std::forward<U>(u)) {}

  // StatusOr<T>::ok()
  //
  // Returns whether or not this `absl::StatusOr<T>` holds a `T` value. This
  // member function is analogous to `absl::Status::ok()` and should be used
  // similarly to check the status of return values.
  //
  // Example:
  //
  // StatusOr<Foo> result = DoBigCalculationThatCouldFail();
  // if (result.ok()) {
  //    // Handle result
  // else {
  //    // Handle error
  // }
  ABSL_MUST_USE_RESULT bool ok() const { return this->status_.ok(); }

  // StatusOr<T>::status()
  //
  // Returns a reference to the current `absl::Status` contained within the
  // `absl::StatusOr<T>`. If `absl::StatusOr<T>` contains a `T`, then this
  // function returns `absl::OkStatus()`.
  const Status& status() const&;
  Status status() &&;

  // StatusOr<T>::value()
  //
  // Returns a reference to the held value if `this->ok()`. Otherwise, throws
  // `absl::BadStatusOrAccess` if exceptions are enabled, or is guaranteed to
  // terminate the process if exceptions are disabled.
  //
  // If you have already checked the status using `this->ok()`, you probably
  // want to use `operator*()` or `operator->()` to access the value instead of
  // `value`.
  //
  // Note: for value types that are cheap to copy, prefer simple code:
  //
  //   T value = statusor.value();
  //
  // Otherwise, if the value type is expensive to copy, but can be left
  // in the StatusOr, simply assign to a reference:
  //
  //   T& value = statusor.value();  // or `const T&`
  //
  // Otherwise, if the value type supports an efficient move, it can be
  // used as follows:
  //
  //   T value = std::move(statusor).value();
  //
  // The `std::move` on statusor instead of on the whole expression enables
  // warnings about possible uses of the statusor object after the move.
  const T& value() const& ABSL_ATTRIBUTE_LIFETIME_BOUND;
  T& value() & ABSL_ATTRIBUTE_LIFETIME_BOUND;
  const T&& value() const&& ABSL_ATTRIBUTE_LIFETIME_BOUND;
  T&& value() && ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // StatusOr<T>:: operator*()
  //
  // Returns a reference to the current value.
  //
  // REQUIRES: `this->ok() == true`, otherwise the behavior is undefined.
  //
  // Use `this->ok()` to verify that there is a current value within the
  // `absl::StatusOr<T>`. Alternatively, see the `value()` member function for a
  // similar API that guarantees crashing or throwing an exception if there is
  // no current value.
  const T& operator*() const& ABSL_ATTRIBUTE_LIFETIME_BOUND;
  T& operator*() & ABSL_ATTRIBUTE_LIFETIME_BOUND;
  const T&& operator*() const&& ABSL_ATTRIBUTE_LIFETIME_BOUND;
  T&& operator*() && ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // StatusOr<T>::operator->()
  //
  // Returns a pointer to the current value.
  //
  // REQUIRES: `this->ok() == true`, otherwise the behavior is undefined.
  //
  // Use `this->ok()` to verify that there is a current value.
  const T* operator->() const ABSL_ATTRIBUTE_LIFETIME_BOUND;
  T* operator->() ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // StatusOr<T>::value_or()
  //
  // Returns the current value if `this->ok() == true`. Otherwise constructs a
  // value using the provided `default_value`.
  //
  // Unlike `value`, this function returns by value, copying the current value
  // if necessary. If the value type supports an efficient move, it can be used
  // as follows:
  //
  //   T value = std::move(statusor).value_or(def);
  //
  // Unlike with `value`, calling `std::move()` on the result of `value_or` will
  // still trigger a copy.
  template <typename U>
  T value_or(U&& default_value) const&;
  template <typename U>
  T value_or(U&& default_value) &&;

  // StatusOr<T>::IgnoreError()
  //
  // Ignores any errors. This method does nothing except potentially suppress
  // complaints from any tools that are checking that errors are not dropped on
  // the floor.
  void IgnoreError() const;

  // StatusOr<T>::emplace()
  //
  // Reconstructs the inner value T in-place using the provided args, using the
  // T(args...) constructor. Returns reference to the reconstructed `T`.
  template <typename... Args>
  T& emplace(Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ok()) {
      this->Clear();
      this->MakeValue(std::forward<Args>(args)...);
    } else {
      this->MakeValue(std::forward<Args>(args)...);
      this->status_ = absl::OkStatus();
    }
    return this->data_;
  }

  template <
      typename U, typename... Args,
      absl::enable_if_t<
          std::is_constructible<T, std::initializer_list<U>&, Args&&...>::value,
          int> = 0>
  T& emplace(std::initializer_list<U> ilist,
             Args&&... args) ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (ok()) {
      this->Clear();
      this->MakeValue(ilist, std::forward<Args>(args)...);
    } else {
      this->MakeValue(ilist, std::forward<Args>(args)...);
      this->status_ = absl::OkStatus();
    }
    return this->data_;
  }

  // StatusOr<T>::AssignStatus()
  //
  // Sets the status of `absl::StatusOr<T>` to the given non-ok status value.
  //
  // NOTE: We recommend using the constructor and `operator=` where possible.
  // This method is intended for use in generic programming, to enable setting
  // the status of a `StatusOr<T>` when `T` may be `Status`. In that case, the
  // constructor and `operator=` would assign into the inner value of type
  // `Status`, rather than status of the `StatusOr` (b/280392796).
  //
  // REQUIRES: !Status(std::forward<U>(v)).ok(). This requirement is DCHECKed.
  // In optimized builds, passing absl::OkStatus() here will have the effect
  // of passing absl::StatusCode::kInternal as a fallback.
  using internal_statusor::StatusOrData<T>::AssignStatus;

 private:
  using internal_statusor::StatusOrData<T>::Assign;
  template <typename U>
  void Assign(const absl::StatusOr<U>& other);
  template <typename U>
  void Assign(absl::StatusOr<U>&& other);
};

// operator==()
//
// This operator checks the equality of two `absl::StatusOr<T>` objects.
template <typename T>
bool operator==(const StatusOr<T>& lhs, const StatusOr<T>& rhs) {
  if (lhs.ok() && rhs.ok()) return *lhs == *rhs;
  return lhs.status() == rhs.status();
}

// operator!=()
//
// This operator checks the inequality of two `absl::StatusOr<T>` objects.
template <typename T>
bool operator!=(const StatusOr<T>& lhs, const StatusOr<T>& rhs) {
  return !(lhs == rhs);
}

// Prints the `value` or the status in brackets to `os`.
//
// Requires `T` supports `operator<<`.  Do not rely on the output format which
// may change without notice.
template <typename T, typename std::enable_if<
                          absl::HasOstreamOperator<T>::value, int>::type = 0>
std::ostream& operator<<(std::ostream& os, const StatusOr<T>& status_or) {
  if (status_or.ok()) {
    os << status_or.value();
  } else {
    os << internal_statusor::StringifyRandom::OpenBrackets()
       << status_or.status()
       << internal_statusor::StringifyRandom::CloseBrackets();
  }
  return os;
}

// As above, but supports `StrCat`, `StrFormat`, etc.
//
// Requires `T` has `AbslStringify`.  Do not rely on the output format which
// may change without notice.
template <
    typename Sink, typename T,
    typename std::enable_if<absl::HasAbslStringify<T>::value, int>::type = 0>
void AbslStringify(Sink& sink, const StatusOr<T>& status_or) {
  if (status_or.ok()) {
    absl::Format(&sink, "%v", status_or.value());
  } else {
    absl::Format(&sink, "%s%v%s",
                 internal_statusor::StringifyRandom::OpenBrackets(),
                 status_or.status(),
                 internal_statusor::StringifyRandom::CloseBrackets());
  }
}

//------------------------------------------------------------------------------
// Implementation details for StatusOr<T>
//------------------------------------------------------------------------------

// TODO(sbenza): avoid the string here completely.
template <typename T>
StatusOr<T>::StatusOr() : Base(Status(absl::StatusCode::kUnknown, "")) {}

template <typename T>
template <typename U>
inline void StatusOr<T>::Assign(const StatusOr<U>& other) {
  if (other.ok()) {
    this->Assign(*other);
  } else {
    this->AssignStatus(other.status());
  }
}

template <typename T>
template <typename U>
inline void StatusOr<T>::Assign(StatusOr<U>&& other) {
  if (other.ok()) {
    this->Assign(*std::move(other));
  } else {
    this->AssignStatus(std::move(other).status());
  }
}
template <typename T>
template <typename... Args>
StatusOr<T>::StatusOr(absl::in_place_t, Args&&... args)
    : Base(absl::in_place, std::forward<Args>(args)...) {}

template <typename T>
template <typename U, typename... Args>
StatusOr<T>::StatusOr(absl::in_place_t, std::initializer_list<U> ilist,
                      Args&&... args)
    : Base(absl::in_place, ilist, std::forward<Args>(args)...) {}

template <typename T>
const Status& StatusOr<T>::status() const& {
  return this->status_;
}
template <typename T>
Status StatusOr<T>::status() && {
  return ok() ? OkStatus() : std::move(this->status_);
}

template <typename T>
const T& StatusOr<T>::value() const& {
  if (!this->ok()) internal_statusor::ThrowBadStatusOrAccess(this->status_);
  return this->data_;
}

template <typename T>
T& StatusOr<T>::value() & {
  if (!this->ok()) internal_statusor::ThrowBadStatusOrAccess(this->status_);
  return this->data_;
}

template <typename T>
const T&& StatusOr<T>::value() const&& {
  if (!this->ok()) {
    internal_statusor::ThrowBadStatusOrAccess(std::move(this->status_));
  }
  return std::move(this->data_);
}

template <typename T>
T&& StatusOr<T>::value() && {
  if (!this->ok()) {
    internal_statusor::ThrowBadStatusOrAccess(std::move(this->status_));
  }
  return std::move(this->data_);
}

template <typename T>
const T& StatusOr<T>::operator*() const& {
  this->EnsureOk();
  return this->data_;
}

template <typename T>
T& StatusOr<T>::operator*() & {
  this->EnsureOk();
  return this->data_;
}

template <typename T>
const T&& StatusOr<T>::operator*() const&& {
  this->EnsureOk();
  return std::move(this->data_);
}

template <typename T>
T&& StatusOr<T>::operator*() && {
  this->EnsureOk();
  return std::move(this->data_);
}

template <typename T>
absl::Nonnull<const T*> StatusOr<T>::operator->() const {
  this->EnsureOk();
  return &this->data_;
}

template <typename T>
absl::Nonnull<T*> StatusOr<T>::operator->() {
  this->EnsureOk();
  return &this->data_;
}

template <typename T>
template <typename U>
T StatusOr<T>::value_or(U&& default_value) const& {
  if (ok()) {
    return this->data_;
  }
  return std::forward<U>(default_value);
}

template <typename T>
template <typename U>
T StatusOr<T>::value_or(U&& default_value) && {
  if (ok()) {
    return std::move(this->data_);
  }
  return std::forward<U>(default_value);
}

template <typename T>
void StatusOr<T>::IgnoreError() const {
  // no-op
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STATUS_STATUSOR_H_
