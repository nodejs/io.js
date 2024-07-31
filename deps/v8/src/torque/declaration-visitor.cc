// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/declaration-visitor.h"

#include "src/torque/ast.h"
#include "src/torque/kythe-data.h"
#include "src/torque/server-data.h"
#include "src/torque/type-inference.h"
#include "src/torque/type-visitor.h"

namespace v8 {
namespace internal {
namespace torque {

Namespace* GetOrCreateNamespace(const std::string& name) {
  std::vector<Namespace*> existing_namespaces = FilterDeclarables<Namespace>(
      Declarations::TryLookupShallow(QualifiedName(name)));
  if (existing_namespaces.empty()) {
    return Declarations::DeclareNamespace(name);
  }
  DCHECK_EQ(1, existing_namespaces.size());
  return existing_namespaces.front();
}

void PredeclarationVisitor::Predeclare(Declaration* decl) {
  CurrentSourcePosition::Scope scope(decl->pos);
  switch (decl->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return Predeclare(name::cast(decl));
    AST_TYPE_DECLARATION_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    case AstNode::Kind::kNamespaceDeclaration:
      return Predeclare(NamespaceDeclaration::cast(decl));
    case AstNode::Kind::kGenericCallableDeclaration:
      return Predeclare(GenericCallableDeclaration::cast(decl));
    case AstNode::Kind::kGenericTypeDeclaration:
      return Predeclare(GenericTypeDeclaration::cast(decl));

    default:
      // Only processes type declaration nodes, namespaces and generics.
      break;
  }
}

void DeclarationVisitor::Visit(Declaration* decl) {
  CurrentSourcePosition::Scope scope(decl->pos);
  switch (decl->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return Visit(name::cast(decl));
    AST_DECLARATION_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNIMPLEMENTED();
  }
}

Builtin* DeclarationVisitor::CreateBuiltin(BuiltinDeclaration* decl,
                                           std::string external_name,
                                           std::string readable_name,
                                           Signature signature,
                                           base::Optional<Statement*> body) {
  const bool javascript = decl->javascript_linkage;
  const bool varargs = decl->parameters.has_varargs;
  Builtin::Kind kind = !javascript ? Builtin::kStub
                                   : varargs ? Builtin::kVarArgsJavaScript
                                             : Builtin::kFixedArgsJavaScript;
  bool has_custom_interface_descriptor = false;
  if (decl->kind == AstNode::Kind::kTorqueBuiltinDeclaration) {
    has_custom_interface_descriptor =
        static_cast<TorqueBuiltinDeclaration*>(decl)
            ->has_custom_interface_descriptor;
  }

  if (varargs && !javascript) {
    Error("Rest parameters require ", decl->name,
          " to be a JavaScript builtin");
  }

  if (javascript) {
    if (!signature.return_type->IsSubtypeOf(TypeOracle::GetJSAnyType())) {
      Error("Return type of JavaScript-linkage builtins has to be JSAny.")
          .Position(decl->return_type->pos);
    }
    for (size_t i = signature.implicit_count;
         i < signature.parameter_types.types.size(); ++i) {
      const Type* parameter_type = signature.parameter_types.types[i];
      if (!TypeOracle::GetJSAnyType()->IsSubtypeOf(parameter_type)) {
        Error(
            "Parameters of JavaScript-linkage builtins have to be a supertype "
            "of JSAny.")
            .Position(decl->parameters.types[i]->pos);
      }
    }
  }

  for (size_t i = 0; i < signature.types().size(); ++i) {
    const Type* parameter_type = signature.types()[i];
    if (parameter_type->StructSupertype()) {
      Error("Builtin do not support structs as arguments, but argument ",
            signature.parameter_names[i], " has type ", *signature.types()[i],
            ".");
    }
    if (parameter_type->IsFloat32() || parameter_type->IsFloat64()) {
      if (!has_custom_interface_descriptor) {
        Error("Builtin ", external_name,
              " needs a custom interface descriptor, "
              "because it uses type ",
              *parameter_type, " for argument ", signature.parameter_names[i],
              ". One reason being "
              "that the default descriptor defines xmm0 to be the first "
              "floating point argument register, which is current used as "
              "scratch on ia32 and cannot be allocated.");
      }
    }
  }

  if (signature.return_type->StructSupertype() && javascript) {
    Error(
        "Builtins with JS linkage cannot return structs, but the return type "
        "is ",
        *signature.return_type, ".");
  }

  if (signature.return_type == TypeOracle::GetVoidType()) {
    Error("Builtins cannot have return type void.");
  }

  Builtin::Flags flags = Builtin::Flag::kNone;
  if (has_custom_interface_descriptor)
    flags |= Builtin::Flag::kCustomInterfaceDescriptor;
  Builtin* builtin = Declarations::CreateBuiltin(
      std::move(external_name), std::move(readable_name), kind, flags,
      std::move(signature), body);
  // TODO(v8:12261): Recheck this.
  // builtin->SetIdentifierPosition(decl->name->pos);
  return builtin;
}

void DeclarationVisitor::Visit(ExternalBuiltinDeclaration* decl) {
  Builtin* builtin =
      CreateBuiltin(decl, decl->name->value, decl->name->value,
                    TypeVisitor::MakeSignature(decl), base::nullopt);
  builtin->SetIdentifierPosition(decl->name->pos);
  Declarations::Declare(decl->name->value, builtin);
}

void DeclarationVisitor::Visit(ExternalRuntimeDeclaration* decl) {
  Signature signature = TypeVisitor::MakeSignature(decl);
  if (signature.parameter_types.types.empty()) {
    ReportError(
        "Missing parameters for runtime function, at least the context "
        "parameter is required.");
  }
  if (!(signature.parameter_types.types[0] == TypeOracle::GetContextType() ||
        signature.parameter_types.types[0] == TypeOracle::GetNoContextType())) {
    ReportError(
        "first parameter to runtime functions has to be the context and have "
        "type Context or NoContext, but found type ",
        *signature.parameter_types.types[0]);
  }
  if (!(signature.return_type->IsSubtypeOf(TypeOracle::GetStrongTaggedType()) ||
        signature.return_type == TypeOracle::GetVoidType() ||
        signature.return_type == TypeOracle::GetNeverType())) {
    ReportError(
        "runtime functions can only return strong tagged values, but "
        "found type ",
        *signature.return_type);
  }
  for (const Type* parameter_type : signature.parameter_types.types) {
    if (!parameter_type->IsSubtypeOf(TypeOracle::GetStrongTaggedType())) {
      ReportError(
          "runtime functions can only take strong tagged parameters, but "
          "found type ",
          *parameter_type);
    }
  }

  RuntimeFunction* function =
      Declarations::DeclareRuntimeFunction(decl->name->value, signature);
  function->SetIdentifierPosition(decl->name->pos);
  function->SetPosition(decl->pos);
  if (GlobalContext::collect_kythe_data()) {
    KytheData::AddFunctionDefinition(function);
  }
}

void DeclarationVisitor::Visit(ExternalMacroDeclaration* decl) {
  Macro* macro = Declarations::DeclareMacro(
      decl->name->value, true, decl->external_assembler_name,
      TypeVisitor::MakeSignature(decl), base::nullopt, decl->op);
  macro->SetIdentifierPosition(decl->name->pos);
  macro->SetPosition(decl->pos);
  if (GlobalContext::collect_kythe_data()) {
    KytheData::AddFunctionDefinition(macro);
  }
}

void DeclarationVisitor::Visit(TorqueBuiltinDeclaration* decl) {
  auto builtin = CreateBuiltin(decl, decl->name->value, decl->name->value,
                               TypeVisitor::MakeSignature(decl), decl->body);
  builtin->SetIdentifierPosition(decl->name->pos);
  builtin->SetPosition(decl->pos);
  Declarations::Declare(decl->name->value, builtin);
}

void DeclarationVisitor::Visit(TorqueMacroDeclaration* decl) {
  Macro* macro = Declarations::DeclareMacro(
      decl->name->value, decl->export_to_csa, base::nullopt,
      TypeVisitor::MakeSignature(decl), decl->body, decl->op);
  macro->SetIdentifierPosition(decl->name->pos);
  macro->SetPosition(decl->pos);
  if (GlobalContext::collect_kythe_data()) {
    KytheData::AddFunctionDefinition(macro);
  }
}

void DeclarationVisitor::Visit(IntrinsicDeclaration* decl) {
  Declarations::DeclareIntrinsic(decl->name->value,
                                 TypeVisitor::MakeSignature(decl));
}

void DeclarationVisitor::Visit(ConstDeclaration* decl) {
  auto constant = Declarations::DeclareNamespaceConstant(
      decl->name, TypeVisitor::ComputeType(decl->type), decl->expression);
  if (GlobalContext::collect_kythe_data()) {
    KytheData::AddConstantDefinition(constant);
  }
}

void DeclarationVisitor::Visit(SpecializationDeclaration* decl) {
  std::vector<GenericCallable*> generic_list =
      Declarations::LookupGeneric(decl->name->value);
  // Find the matching generic specialization based on the concrete parameter
  // list.
  GenericCallable* matching_generic = nullptr;
  Signature signature_with_types = TypeVisitor::MakeSignature(decl);
  for (GenericCallable* generic : generic_list) {
    // This argument inference is just to trigger constraint checking on the
    // generic arguments.
    TypeArgumentInference inference = generic->InferSpecializationTypes(
        TypeVisitor::ComputeTypeVector(decl->generic_parameters), {});
    if (inference.HasFailed()) {
      continue;
    }
    Signature generic_signature_with_types =
        MakeSpecializedSignature(SpecializationKey<GenericCallable>{
            generic, TypeVisitor::ComputeTypeVector(decl->generic_parameters)});
    if (signature_with_types.HasSameTypesAs(generic_signature_with_types,
                                            ParameterMode::kIgnoreImplicit)) {
      if (matching_generic != nullptr) {
        std::stringstream stream;
        stream << "specialization of " << decl->name
               << " is ambigous, it matches more than one generic declaration ("
               << *matching_generic << " and " << *generic << ")";
        ReportError(stream.str());
      }
      matching_generic = generic;
    }
  }

  if (matching_generic == nullptr) {
    std::stringstream stream;
    if (generic_list.empty()) {
      stream << "no generic defined with the name " << decl->name;
      ReportError(stream.str());
    }
    stream << "specialization of " << decl->name
           << " doesn't match any generic declaration\n";
    stream << "specialization signature:";
    stream << "\n  " << signature_with_types;
    stream << "\ncandidates are:";
    for (GenericCallable* generic : generic_list) {
      stream << "\n  "
             << MakeSpecializedSignature(SpecializationKey<GenericCallable>{
                    generic,
                    TypeVisitor::ComputeTypeVector(decl->generic_parameters)});
    }
    ReportError(stream.str());
  }

  if (GlobalContext::collect_language_server_data()) {
    LanguageServerData::AddDefinition(decl->name->pos,
                                      matching_generic->IdentifierPosition());
  }

  CallableDeclaration* generic_declaration = matching_generic->declaration();

  Specialize(SpecializationKey<GenericCallable>{matching_generic,
                                                TypeVisitor::ComputeTypeVector(
                                                    decl->generic_parameters)},
             generic_declaration, decl, decl->body, decl->pos);
}

void DeclarationVisitor::Visit(ExternConstDeclaration* decl) {
  const Type* type = TypeVisitor::ComputeType(decl->type);
  if (!type->IsConstexpr()) {
    std::stringstream stream;
    stream << "extern constants must have constexpr type, but found: \""
           << *type << "\"\n";
    ReportError(stream.str());
  }

  ExternConstant* constant =
      Declarations::DeclareExternConstant(decl->name, type, decl->literal);
  if (GlobalContext::collect_kythe_data()) {
    KytheData::AddConstantDefinition(constant);
  }
}

void DeclarationVisitor::Visit(CppIncludeDeclaration* decl) {
  GlobalContext::AddCppInclude(decl->include_path);
}

void DeclarationVisitor::DeclareSpecializedTypes(
    const SpecializationKey<GenericCallable>& key) {
  size_t i = 0;
  const std::size_t generic_parameter_count =
      key.generic->generic_parameters().size();
  if (generic_parameter_count != key.specialized_types.size()) {
    std::stringstream stream;
    stream << "Wrong generic argument count for specialization of \""
           << key.generic->name() << "\", expected: " << generic_parameter_count
           << ", actual: " << key.specialized_types.size();
    ReportError(stream.str());
  }

  for (auto type : key.specialized_types) {
    Identifier* generic_type_name = key.generic->generic_parameters()[i++].name;
    TypeAlias* alias = Declarations::DeclareType(generic_type_name, type);
    alias->SetIsUserDefined(false);
  }
}

Signature DeclarationVisitor::MakeSpecializedSignature(
    const SpecializationKey<GenericCallable>& key) {
  CurrentScope::Scope generic_scope(key.generic->ParentScope());
  // Create a temporary fake-namespace just to temporarily declare the
  // specialization aliases for the generic types to create a signature.
  Namespace tmp_namespace("_tmp");
  CurrentScope::Scope tmp_namespace_scope(&tmp_namespace);
  DeclareSpecializedTypes(key);
  return TypeVisitor::MakeSignature(key.generic->declaration());
}

Callable* DeclarationVisitor::SpecializeImplicit(
    const SpecializationKey<GenericCallable>& key) {
  base::Optional<Statement*> body = key.generic->CallableBody();
  if (!body && IntrinsicDeclaration::DynamicCast(key.generic->declaration()) ==
                   nullptr) {
    ReportError("missing specialization of ", key.generic->name(),
                " with types <", key.specialized_types, "> declared at ",
                key.generic->Position());
  }
  SpecializationRequester requester{CurrentSourcePosition::Get(),
                                    CurrentScope::Get(), ""};
  CurrentScope::Scope generic_scope(key.generic->ParentScope());
  Callable* result = Specialize(key, key.generic->declaration(), base::nullopt,
                                body, CurrentSourcePosition::Get());
  result->SetIsUserDefined(false);
  requester.name = result->ReadableName();
  result->SetSpecializationRequester(requester);
  CurrentScope::Scope callable_scope(result);
  DeclareSpecializedTypes(key);
  return result;
}

Callable* DeclarationVisitor::Specialize(
    const SpecializationKey<GenericCallable>& key,
    CallableDeclaration* declaration,
    base::Optional<const SpecializationDeclaration*> explicit_specialization,
    base::Optional<Statement*> body, SourcePosition position) {
  CurrentSourcePosition::Scope pos_scope(position);
  size_t generic_parameter_count = key.generic->generic_parameters().size();
  if (generic_parameter_count != key.specialized_types.size()) {
    std::stringstream stream;
    stream << "number of template parameters ("
           << std::to_string(key.specialized_types.size())
           << ") to intantiation of generic " << declaration->name
           << " doesnt match the generic's declaration ("
           << std::to_string(generic_parameter_count) << ")";
    ReportError(stream.str());
  }
  if (key.generic->GetSpecialization(key.specialized_types)) {
    ReportError("cannot redeclare specialization of ", key.generic->name(),
                " with types <", key.specialized_types, ">");
  }

  Signature type_signature =
      explicit_specialization
          ? TypeVisitor::MakeSignature(*explicit_specialization)
          : MakeSpecializedSignature(key);

  std::string generated_name = Declarations::GetGeneratedCallableName(
      declaration->name->value, key.specialized_types);
  std::stringstream readable_name;
  readable_name << declaration->name->value << "<";
  bool first = true;
  for (const Type* t : key.specialized_types) {
    if (!first) readable_name << ", ";
    readable_name << *t;
    first = false;
  }
  readable_name << ">";
  Callable* callable;
  if (MacroDeclaration::DynamicCast(declaration) != nullptr) {
    callable =
        Declarations::CreateTorqueMacro(generated_name, readable_name.str(),
                                        false, type_signature, *body, true);
  } else if (IntrinsicDeclaration::DynamicCast(declaration) != nullptr) {
    callable =
        Declarations::CreateIntrinsic(declaration->name->value, type_signature);
  } else {
    BuiltinDeclaration* builtin = BuiltinDeclaration::cast(declaration);
    callable =
        CreateBuiltin(builtin, GlobalContext::MakeUniqueName(generated_name),
                      readable_name.str(), type_signature, *body);
  }
  key.generic->AddSpecialization(key.specialized_types, callable);
  return callable;
}

void PredeclarationVisitor::ResolvePredeclarations() {
  const auto& all_declarables = GlobalContext::AllDeclarables();
  for (size_t i = 0; i < all_declarables.size(); ++i) {
    Declarable* declarable = all_declarables[i].get();
    if (const TypeAlias* alias = TypeAlias::DynamicCast(declarable)) {
      CurrentScope::Scope scope_activator(alias->ParentScope());
      CurrentSourcePosition::Scope position_activator(alias->Position());
      alias->Resolve();
    }
  }
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
