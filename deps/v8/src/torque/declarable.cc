// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/declarable.h"

#include <fstream>
#include <iostream>

#include "src/torque/ast.h"
#include "src/torque/global-context.h"
#include "src/torque/type-inference.h"
#include "src/torque/type-visitor.h"

namespace v8 {
namespace internal {
namespace torque {

QualifiedName QualifiedName::Parse(std::string qualified_name) {
  std::vector<std::string> qualifications;
  while (true) {
    size_t namespace_delimiter_index = qualified_name.find("::");
    if (namespace_delimiter_index == std::string::npos) break;
    qualifications.push_back(
        qualified_name.substr(0, namespace_delimiter_index));
    qualified_name = qualified_name.substr(namespace_delimiter_index + 2);
  }
  return QualifiedName(qualifications, qualified_name);
}

std::ostream& operator<<(std::ostream& os, const QualifiedName& name) {
  for (const std::string& qualifier : name.namespace_qualification) {
    os << qualifier << "::";
  }
  return os << name.name;
}

std::ostream& operator<<(std::ostream& os, const Callable& m) {
  os << "callable " << m.ReadableName() << "(";
  if (m.signature().implicit_count != 0) {
    os << "implicit ";
    TypeVector implicit_parameter_types(
        m.signature().parameter_types.types.begin(),
        m.signature().parameter_types.types.begin() +
            m.signature().implicit_count);
    os << implicit_parameter_types << ")(";
    TypeVector explicit_parameter_types(
        m.signature().parameter_types.types.begin() +
            m.signature().implicit_count,
        m.signature().parameter_types.types.end());
    os << explicit_parameter_types;
  } else {
    os << m.signature().parameter_types;
  }
  os << "): " << *m.signature().return_type;
  return os;
}

std::ostream& operator<<(std::ostream& os, const Builtin& b) {
  os << "builtin " << *b.signature().return_type << " " << b.ReadableName()
     << b.signature().parameter_types;
  return os;
}

std::ostream& operator<<(std::ostream& os, const RuntimeFunction& b) {
  os << "runtime function " << *b.signature().return_type << " "
     << b.ReadableName() << b.signature().parameter_types;
  return os;
}

std::ostream& operator<<(std::ostream& os, const GenericCallable& g) {
  os << "generic " << g.name() << "<";
  PrintCommaSeparatedList(os, g.generic_parameters(),
                          [](const GenericParameter& identifier) {
                            return identifier.name->value;
                          });
  os << ">";

  return os;
}

SpecializationRequester::SpecializationRequester(SourcePosition position,
                                                 Scope* s, std::string name)
    : position(position), name(std::move(name)) {
  // Skip scopes that are not related to template specializations, they might be
  // stack-allocated and not live for long enough.
  while (s && s->GetSpecializationRequester().IsNone()) s = s->ParentScope();
  this->scope = s;
}

std::vector<Declarable*> Scope::Lookup(const QualifiedName& name) {
  if (!name.namespace_qualification.empty() &&
      name.namespace_qualification[0].empty()) {
    return GlobalContext::GetDefaultNamespace()->Lookup(
        name.DropFirstNamespaceQualification());
  }
  std::vector<Declarable*> result;
  if (ParentScope()) {
    result = ParentScope()->Lookup(name);
  }
  for (Declarable* declarable : LookupShallow(name)) {
    result.push_back(declarable);
  }
  return result;
}

base::Optional<std::string> TypeConstraint::IsViolated(const Type* type) const {
  if (upper_bound && !type->IsSubtypeOf(*upper_bound)) {
    if (type->IsTopType()) {
      return TopType::cast(type)->reason();
    } else {
      return {
          ToString("expected ", *type, " to be a subtype of ", **upper_bound)};
    }
  }
  return base::nullopt;
}

base::Optional<std::string> FindConstraintViolation(
    const std::vector<const Type*>& types,
    const std::vector<TypeConstraint>& constraints) {
  DCHECK_EQ(constraints.size(), types.size());
  for (size_t i = 0; i < types.size(); ++i) {
    if (auto violation = constraints[i].IsViolated(types[i])) {
      return {"Could not instantiate generic, " + *violation + "."};
    }
  }
  return base::nullopt;
}

std::vector<TypeConstraint> ComputeConstraints(
    Scope* scope, const GenericParameters& parameters) {
  CurrentScope::Scope scope_scope(scope);
  std::vector<TypeConstraint> result;
  for (const GenericParameter& parameter : parameters) {
    if (parameter.constraint) {
      result.push_back(TypeConstraint::SubtypeConstraint(
          TypeVisitor::ComputeType(*parameter.constraint)));
    } else {
      result.push_back(TypeConstraint::Unconstrained());
    }
  }
  return result;
}

TypeArgumentInference GenericCallable::InferSpecializationTypes(
    const TypeVector& explicit_specialization_types,
    const std::vector<base::Optional<const Type*>>& arguments) {
  const std::vector<TypeExpression*>& parameters =
      declaration()->parameters.types;
  CurrentScope::Scope generic_scope(ParentScope());
  TypeArgumentInference inference(generic_parameters(),
                                  explicit_specialization_types, parameters,
                                  arguments);
  if (!inference.HasFailed()) {
    if (auto violation =
            FindConstraintViolation(inference.GetResult(), Constraints())) {
      inference.Fail(*violation);
    }
  }
  return inference;
}

base::Optional<Statement*> GenericCallable::CallableBody() {
  if (auto* macro_decl = TorqueMacroDeclaration::DynamicCast(declaration())) {
    return macro_decl->body;
  } else if (auto* builtin_decl =
                 TorqueBuiltinDeclaration::DynamicCast(declaration())) {
    return builtin_decl->body;
  } else {
    return base::nullopt;
  }
}

bool Namespace::IsDefaultNamespace() const {
  return this == GlobalContext::GetDefaultNamespace();
}

bool Namespace::IsTestNamespace() const { return name() == kTestNamespaceName; }

const Type* TypeAlias::Resolve() const {
  if (!type_) {
    CurrentScope::Scope scope_activator(ParentScope());
    CurrentSourcePosition::Scope position_activator(Position());
    TypeDeclaration* decl = *delayed_;
    if (being_resolved_) {
      std::stringstream s;
      s << "Cannot create type " << decl->name->value
        << " due to circular dependencies.";
      ReportError(s.str());
    }
    type_ = TypeVisitor::ComputeType(decl);
  }
  return *type_;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
