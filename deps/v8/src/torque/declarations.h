// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_DECLARATIONS_H_
#define V8_TORQUE_DECLARATIONS_H_

#include <memory>
#include <optional>
#include <string>

#include "src/torque/declarable.h"
#include "src/torque/utils.h"

namespace v8::internal::torque {

static constexpr const char* const kFromConstexprMacroName = "FromConstexpr";
static constexpr const char* kMacroEndLabelName = "__macro_end";
static constexpr const char* kBreakLabelName = "__break";
static constexpr const char* kContinueLabelName = "__continue";
static constexpr const char* kCatchLabelName = "__catch";
static constexpr const char* kNextCaseLabelName = "__NextCase";

template <class T>
std::vector<T*> FilterDeclarables(const std::vector<Declarable*> list) {
  std::vector<T*> result;
  for (Declarable* declarable : list) {
    if (T* t = T::DynamicCast(declarable)) {
      result.push_back(t);
    }
  }
  return result;
}

inline std::string UnwrapTNodeTypeName(const std::string& generates) {
  if (generates.length() < 7 || generates.substr(0, 6) != "TNode<" ||
      generates.substr(generates.length() - 1, 1) != ">") {
    ReportError("generated type \"", generates,
                "\" should be of the form \"TNode<...>\"");
  }
  return generates.substr(6, generates.length() - 7);
}

class Declarations {
 public:
  static std::vector<Declarable*> TryLookup(const QualifiedName& name) {
    return CurrentScope::Get()->Lookup(name);
  }

  static std::vector<Declarable*> TryLookupShallow(const QualifiedName& name) {
    return CurrentScope::Get()->LookupShallow(name);
  }

  template <class T>
  static std::vector<T*> TryLookup(const QualifiedName& name) {
    return FilterDeclarables<T>(TryLookup(name));
  }

  static std::vector<Declarable*> Lookup(const QualifiedName& name) {
    std::vector<Declarable*> d = TryLookup(name);
    if (d.empty()) {
      ReportError("cannot find \"", name, "\"");
    }
    return d;
  }

  static std::vector<Declarable*> LookupGlobalScope(const QualifiedName& name);

  static const TypeAlias* LookupTypeAlias(const QualifiedName& name);
  static const Type* LookupType(const QualifiedName& name);
  static const Type* LookupType(const Identifier* identifier);
  static std::optional<const Type*> TryLookupType(const QualifiedName& name);
  static const Type* LookupGlobalType(const QualifiedName& name);

  static Builtin* FindSomeInternalBuiltinWithType(
      const BuiltinPointerType* type);

  static Value* LookupValue(const QualifiedName& name);

  static Macro* TryLookupMacro(const std::string& name,
                               const TypeVector& types);
  static std::optional<Builtin*> TryLookupBuiltin(const QualifiedName& name);

  static std::vector<GenericCallable*> LookupGeneric(const std::string& name);
  static GenericCallable* LookupUniqueGeneric(const QualifiedName& name);

  static GenericType* LookupUniqueGenericType(const QualifiedName& name);
  static GenericType* LookupGlobalUniqueGenericType(const std::string& name);
  static std::optional<GenericType*> TryLookupGenericType(
      const QualifiedName& name);

  static Namespace* DeclareNamespace(const std::string& name);
  static TypeAlias* DeclareType(const Identifier* name, const Type* type);

  static TypeAlias* PredeclareTypeAlias(const Identifier* name,
                                        TypeDeclaration* type,
                                        bool redeclaration);
  static TorqueMacro* CreateTorqueMacro(std::string external_name,
                                        std::string readable_name,
                                        bool exported_to_csa,
                                        Signature signature,
                                        std::optional<Statement*> body,
                                        bool is_user_defined);
  static ExternMacro* CreateExternMacro(std::string name,
                                        std::string external_assembler_name,
                                        Signature signature);
  static Macro* DeclareMacro(const std::string& name, bool accessible_from_csa,
                             std::optional<std::string> external_assembler_name,
                             const Signature& signature,
                             std::optional<Statement*> body,
                             std::optional<std::string> op = {},
                             bool is_user_defined = true);

  static Method* CreateMethod(AggregateType* class_type,
                              const std::string& name, Signature signature,
                              Statement* body);

  static Intrinsic* CreateIntrinsic(const std::string& name,
                                    const Signature& signature);

  static Intrinsic* DeclareIntrinsic(const std::string& name,
                                     const Signature& signature);

  static Builtin* CreateBuiltin(std::string external_name,
                                std::string readable_name, Builtin::Kind kind,
                                Builtin::Flags flags, Signature signature,
                                std::optional<Statement*> body);
  static Builtin* DeclareBuiltin(const std::string& name, Builtin::Kind kind,
                                 Builtin::Flags flags,
                                 const Signature& signature,
                                 std::optional<Statement*> body);

  static RuntimeFunction* DeclareRuntimeFunction(const std::string& name,
                                                 const Signature& signature);

  static ExternConstant* DeclareExternConstant(Identifier* name,
                                               const Type* type,
                                               std::string value);
  static NamespaceConstant* DeclareNamespaceConstant(Identifier* name,
                                                     const Type* type,
                                                     Expression* body);

  static GenericCallable* DeclareGenericCallable(
      const std::string& name, GenericCallableDeclaration* ast_node);
  static GenericType* DeclareGenericType(const std::string& name,
                                         GenericTypeDeclaration* ast_node);

  template <class T>
  static T* Declare(const std::string& name, T* d) {
    CurrentScope::Get()->AddDeclarable(name, d);
    return d;
  }
  template <class T>
  static T* Declare(const std::string& name, std::unique_ptr<T> d) {
    return CurrentScope::Get()->AddDeclarable(name,
                                              RegisterDeclarable(std::move(d)));
  }
  static Macro* DeclareOperator(const std::string& name, Macro* m);

  static std::string GetGeneratedCallableName(
      const std::string& name, const TypeVector& specialized_types);
};

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_DECLARATIONS_H_
