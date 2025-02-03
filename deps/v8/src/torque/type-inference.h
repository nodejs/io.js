// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_TYPE_INFERENCE_H_
#define V8_TORQUE_TYPE_INFERENCE_H_

#include <optional>
#include <string>
#include <unordered_map>

#include "src/torque/ast.h"
#include "src/torque/declarations.h"
#include "src/torque/types.h"

namespace v8::internal::torque {

// Type argument inference computes a potential instantiation of a generic
// callable given some concrete argument types. As an example, consider the
// generic macro
//
//   macro Pick<T: type>(x: T, y: T): T
//
// along with a given call site, such as
//
//   Pick(1, 2);
//
// The inference proceeds by matching the term argument types (`constexpr
// int31`, in case of `1` and `2`) against the formal parameter types (`T` in
// both cases). During this matching we discover that `T` must equal `constexpr
// int31`.
//
// The inference will not perform any comprehensive type checking of its own,
// but *does* fail if type parameters cannot be soundly instantiated given the
// call site. For instance, for the following call site
//
//   const aSmi: Smi = ...;
//   Pick(1, aSmi);  // inference fails
//
// inference would fail, since `constexpr int31` is distinct from `Smi`. To
// allow for implicit conversions to be tried in a separate step after type
// argument inference, a number of type arguments may be given explicitly:
//
//   Pick<Smi>(1, aSmi);  // inference succeeds (doing nothing)
//
// In the above case the inference simply ignores inconsistent constraints on
// `T`. Similarly, we ignore all constraints arising from formal parameters
// that are function- or union-typed.
//
// Finally, note that term parameters are passed as type expressions, since
// we have no way of expressing a reference to type parameter as a Type. These
// type expressions are resolved during matching, so TypeArgumentInference
// should be instantiated in the appropriate scope.
class TypeArgumentInference {
 public:
  TypeArgumentInference(
      const GenericParameters& type_parameters,
      const TypeVector& explicit_type_arguments,
      const std::vector<TypeExpression*>& term_parameters,
      const std::vector<std::optional<const Type*>>& term_argument_types);

  bool HasFailed() const { return failure_reason_.has_value(); }
  const std::string& GetFailureReason() { return *failure_reason_; }
  TypeVector GetResult() const;
  void Fail(std::string reason) { failure_reason_ = {reason}; }

 private:
  void Match(TypeExpression* parameter, const Type* argument_type);
  void MatchGeneric(BasicTypeExpression* parameter, const Type* argument_type);

  size_t num_explicit_;
  std::unordered_map<std::string, size_t> type_parameter_from_name_;
  std::vector<std::optional<const Type*>> inferred_;
  std::optional<std::string> failure_reason_;
};

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_TYPE_INFERENCE_H_
