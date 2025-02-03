// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_TYPE_VISITOR_H_
#define V8_TORQUE_TYPE_VISITOR_H_

#include <optional>

#include "src/torque/ast.h"
#include "src/torque/types.h"

namespace v8::internal::torque {

class Scope;

class TypeVisitor {
 public:
  static TypeVector ComputeTypeVector(const std::vector<TypeExpression*>& v) {
    TypeVector result;
    for (TypeExpression* t : v) {
      result.push_back(ComputeType(t));
    }
    return result;
  }

  static const Type* ComputeType(TypeExpression* type_expression);
  static void VisitClassFieldsAndMethods(
      ClassType* class_type, const ClassDeclaration* class_declaration);
  static void VisitStructMethods(StructType* struct_type,
                                 const StructDeclaration* struct_declaration);
  static Signature MakeSignature(const CallableDeclaration* declaration);
  // Can return either StructType or BitFieldStructType, since they can both be
  // used in struct expressions like `MyStruct{ a: 0, b: foo }`
  static const Type* ComputeTypeForStructExpression(
      TypeExpression* type_expression,
      const std::vector<const Type*>& term_argument_types);

 private:
  friend class TypeAlias;
  friend class TypeOracle;
  static const Type* ComputeType(
      TypeDeclaration* decl,
      MaybeSpecializationKey specialized_from = std::nullopt,
      Scope* specialization_requester = nullptr);
  static const AbstractType* ComputeType(
      AbstractTypeDeclaration* decl, MaybeSpecializationKey specialized_from);
  static const Type* ComputeType(TypeAliasDeclaration* decl,
                                 MaybeSpecializationKey specialized_from);
  static const BitFieldStructType* ComputeType(
      BitFieldStructDeclaration* decl, MaybeSpecializationKey specialized_from);
  static const StructType* ComputeType(StructDeclaration* decl,
                                       MaybeSpecializationKey specialized_from);
  static const ClassType* ComputeType(ClassDeclaration* decl,
                                      MaybeSpecializationKey specialized_from);
};

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_TYPE_VISITOR_H_
