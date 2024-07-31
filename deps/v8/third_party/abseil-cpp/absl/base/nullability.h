// Copyright 2023 The Abseil Authors.
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
// File: nullability.h
// -----------------------------------------------------------------------------
//
// This header file defines a set of "templated annotations" for designating the
// expected nullability of pointers. These annotations allow you to designate
// pointers in one of three classification states:
//
//  * "Non-null" (for pointers annotated `Nonnull<T>`), indicating that it is
//    invalid for the given pointer to ever be null.
//  * "Nullable" (for pointers annotated `Nullable<T>`), indicating that it is
//    valid for the given pointer to be null.
//  * "Unknown" (for pointers annotated `NullabilityUnknown<T>`), indicating
//    that the given pointer has not been yet classified as either nullable or
//    non-null. This is the default state of unannotated pointers.
//
// NOTE: unannotated pointers implicitly bear the annotation
// `NullabilityUnknown<T>`; you should rarely, if ever, see this annotation used
// in the codebase explicitly.
//
// -----------------------------------------------------------------------------
// Nullability and Contracts
// -----------------------------------------------------------------------------
//
// These nullability annotations allow you to more clearly specify contracts on
// software components by narrowing the *preconditions*, *postconditions*, and
// *invariants* of pointer state(s) in any given interface. It then depends on
// context who is responsible for fulfilling the annotation's requirements.
//
// For example, a function may receive a pointer argument. Designating that
// pointer argument as "non-null" tightens the precondition of the contract of
// that function. It is then the responsibility of anyone calling such a
// function to ensure that the passed pointer is not null.
//
// Similarly, a function may have a pointer as a return value. Designating that
// return value as "non-null" tightens the postcondition of the contract of that
// function. In this case, however, it is the responsibility of the function
// itself to ensure that the returned pointer is not null.
//
// Clearly defining these contracts allows providers (and consumers) of such
// pointers to have more confidence in their null state. If a function declares
// a return value as "non-null", for example, the caller should not need to
// check whether the returned value is `nullptr`; it can simply assume the
// pointer is valid.
//
// Of course most interfaces already have expectations on the nullability state
// of pointers, and these expectations are, in effect, a contract; often,
// however, those contracts are either poorly or partially specified, assumed,
// or misunderstood. These nullability annotations are designed to allow you to
// formalize those contracts within the codebase.
//
// -----------------------------------------------------------------------------
// Using Nullability Annotations
// -----------------------------------------------------------------------------
//
// It is important to note that these annotations are not distinct strong
// *types*. They are alias templates defined to be equal to the underlying
// pointer type. A pointer annotated `Nonnull<T*>`, for example, is simply a
// pointer of type `T*`. Each annotation acts as a form of documentation about
// the contract for the given pointer. Each annotation requires providers or
// consumers of these pointers across API boundaries to take appropriate steps
// when setting or using these pointers:
//
// * "Non-null" pointers should never be null. It is the responsibility of the
//   provider of this pointer to ensure that the pointer may never be set to
//   null. Consumers of such pointers can treat such pointers as non-null.
// * "Nullable" pointers may or may not be null. Consumers of such pointers
//   should precede any usage of that pointer (e.g. a dereference operation)
//   with a a `nullptr` check.
// * "Unknown" pointers may be either "non-null" or "nullable" but have not been
//   definitively determined to be in either classification state. Providers of
//   such pointers across API boundaries should determine --  over time -- to
//   annotate the pointer in either of the above two states. Consumers of such
//   pointers across an API boundary should continue to treat such pointers as
//   they currently do.
//
// Example:
//
// // PaySalary() requires the passed pointer to an `Employee` to be non-null.
// void PaySalary(absl::Nonnull<Employee *> e) {
//   pay(e->salary);  // OK to dereference
// }
//
// // CompleteTransaction() guarantees the returned pointer to an `Account` to
// // be non-null.
// absl::Nonnull<Account *> balance CompleteTransaction(double fee) {
// ...
// }
//
// // Note that specifying a nullability annotation does not prevent someone
// // from violating the contract:
//
// Nullable<Employee *> find(Map& employees, std::string_view name);
//
// void g(Map& employees) {
//   Employee *e = find(employees, "Pat");
//   // `e` can now be null.
//   PaySalary(e); // Violates contract, but compiles!
// }
//
// Nullability annotations, in other words, are useful for defining and
// narrowing contracts; *enforcement* of those contracts depends on use and any
// additional (static or dynamic analysis) tooling.
//
// NOTE: The "unknown" annotation state indicates that a pointer's contract has
// not yet been positively identified. The unknown state therefore acts as a
// form of documentation of your technical debt, and a codebase that adopts
// nullability annotations should aspire to annotate every pointer as either
// "non-null" or "nullable".
//
// -----------------------------------------------------------------------------
// Applicability of Nullability Annotations
// -----------------------------------------------------------------------------
//
// By default, nullability annotations are applicable to raw and smart
// pointers. User-defined types can indicate compatibility with nullability
// annotations by providing an `absl_nullability_compatible` nested type. The
// actual definition of this inner type is not relevant as it is used merely as
// a marker. It is common to use a using declaration of
// `absl_nullability_compatible` set to void.
//
// // Example:
// struct MyPtr {
//   using absl_nullability_compatible = void;
//   ...
// };
//
// DISCLAIMER:
// ===========================================================================
// These nullability annotations are primarily a human readable signal about the
// intended contract of the pointer. They are not *types* and do not currently
// provide any correctness guarantees. For example, a pointer annotated as
// `Nonnull<T*>` is *not guaranteed* to be non-null, and the compiler won't
// alert or prevent assignment of a `Nullable<T*>` to a `Nonnull<T*>`.
// ===========================================================================
#ifndef ABSL_BASE_NULLABILITY_H_
#define ABSL_BASE_NULLABILITY_H_

#include "absl/base/internal/nullability_impl.h"

namespace absl {

// absl::Nonnull
//
// The indicated pointer is never null. It is the responsibility of the provider
// of this pointer across an API boundary to ensure that the pointer is never be
// set to null. Consumers of this pointer across an API boundary may safely
// dereference the pointer.
//
// Example:
//
// // `employee` is designated as not null.
// void PaySalary(absl::Nonnull<Employee *> employee) {
//   pay(*employee);  // OK to dereference
// }
template <typename T>
using Nonnull = nullability_internal::NonnullImpl<T>;

// absl::Nullable
//
// The indicated pointer may, by design, be either null or non-null. Consumers
// of this pointer across an API boundary should perform a `nullptr` check
// before performing any operation using the pointer.
//
// Example:
//
// // `employee` may  be null.
// void PaySalary(absl::Nullable<Employee *> employee) {
//   if (employee != nullptr) {
//     Pay(*employee);  // OK to dereference
//   }
// }
template <typename T>
using Nullable = nullability_internal::NullableImpl<T>;

// absl::NullabilityUnknown (default)
//
// The indicated pointer has not yet been determined to be definitively
// "non-null" or "nullable." Providers of such pointers across API boundaries
// should, over time, annotate such pointers as either "non-null" or "nullable."
// Consumers of these pointers across an API boundary should treat such pointers
// with the same caution they treat currently unannotated pointers. Most
// existing code will have "unknown"  pointers, which should eventually be
// migrated into one of the above two nullability states: `Nonnull<T>` or
//  `Nullable<T>`.
//
// NOTE: Because this annotation is the global default state, pointers without
// any annotation are assumed to have "unknown" semantics. This assumption is
// designed to minimize churn and reduce clutter within the codebase.
//
// Example:
//
// // `employee`s nullability state is unknown.
// void PaySalary(absl::NullabilityUnknown<Employee *> employee) {
//   Pay(*employee); // Potentially dangerous. API provider should investigate.
// }
//
// Note that a pointer without an annotation, by default, is assumed to have the
// annotation `NullabilityUnknown`.
//
// // `employee`s nullability state is unknown.
// void PaySalary(Employee* employee) {
//   Pay(*employee); // Potentially dangerous. API provider should investigate.
// }
template <typename T>
using NullabilityUnknown = nullability_internal::NullabilityUnknownImpl<T>;

}  // namespace absl

#endif  // ABSL_BASE_NULLABILITY_H_
