// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/type-visitor.h"

#include <optional>

#include "src/common/globals.h"
#include "src/torque/declarable.h"
#include "src/torque/global-context.h"
#include "src/torque/kythe-data.h"
#include "src/torque/server-data.h"
#include "src/torque/type-inference.h"
#include "src/torque/type-oracle.h"

namespace v8::internal::torque {

const Type* TypeVisitor::ComputeType(TypeDeclaration* decl,
                                     MaybeSpecializationKey specialized_from,
                                     Scope* specialization_requester) {
  SourcePosition requester_position = CurrentSourcePosition::Get();
  CurrentSourcePosition::Scope scope(decl->pos);
  Scope* current_scope = CurrentScope::Get();
  if (specialized_from) {
    current_scope = TypeOracle::CreateGenericTypeInstantiationNamespace();
    current_scope->SetSpecializationRequester(
        {requester_position, specialization_requester,
         Type::ComputeName(decl->name->value, specialized_from)});
  }
  CurrentScope::Scope new_current_scope_scope(current_scope);
  if (specialized_from) {
    auto& params = specialized_from->generic->generic_parameters();
    auto arg_types_iterator = specialized_from->specialized_types.begin();
    for (auto param : params) {
      TypeAlias* alias =
          Declarations::DeclareType(param.name, *arg_types_iterator);
      alias->SetIsUserDefined(false);
      arg_types_iterator++;
    }
  }

  switch (decl->kind) {
#define ENUM_ITEM(name)        \
  case AstNode::Kind::k##name: \
    return ComputeType(name::cast(decl), specialized_from);
    AST_TYPE_DECLARATION_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
    default:
      UNIMPLEMENTED();
  }
}

const Type* TypeVisitor::ComputeType(TypeAliasDeclaration* decl,
                                     MaybeSpecializationKey specialized_from) {
  const Type* type = ComputeType(decl->type);
  type->AddAlias(decl->name->value);
  return type;
}

namespace {
std::string ComputeGeneratesType(std::optional<std::string> opt_gen,
                                 bool enforce_tnode_type) {
  if (!opt_gen) return "";
  const std::string& generates = *opt_gen;
  if (enforce_tnode_type) {
    return UnwrapTNodeTypeName(generates);
  }
  return generates;
}
}  // namespace

const AbstractType* TypeVisitor::ComputeType(
    AbstractTypeDeclaration* decl, MaybeSpecializationKey specialized_from) {
  std::string generates =
      ComputeGeneratesType(decl->generates, !decl->IsConstexpr());

  const Type* parent_type = nullptr;
  if (decl->extends) {
    parent_type = TypeVisitor::ComputeType(*decl->extends);
    if (parent_type->IsUnionType()) {
      // UnionType::IsSupertypeOf requires that types can only extend from non-
      // union types in order to work correctly.
      ReportError("type \"", decl->name->value,
                  "\" cannot extend a type union");
    }
  }

  if (decl->IsConstexpr() && decl->IsTransient()) {
    ReportError("cannot declare a transient type that is also constexpr");
  }

  const Type* non_constexpr_version = nullptr;
  if (decl->IsConstexpr()) {
    QualifiedName non_constexpr_name{GetNonConstexprName(decl->name->value)};
    if (auto type = Declarations::TryLookupType(non_constexpr_name)) {
      non_constexpr_version = *type;
    }
  }

  return TypeOracle::GetAbstractType(parent_type, decl->name->value,
                                     decl->flags, generates,
                                     non_constexpr_version, specialized_from);
}

void DeclareMethods(AggregateType* container_type,
                    const std::vector<Declaration*>& methods) {
  for (auto declaration : methods) {
    CurrentSourcePosition::Scope pos_scope(declaration->pos);
    TorqueMacroDeclaration* method =
        TorqueMacroDeclaration::DynamicCast(declaration);
    Signature signature = TypeVisitor::MakeSignature(method);
    signature.parameter_names.insert(
        signature.parameter_names.begin() + signature.implicit_count,
        MakeNode<Identifier>(kThisParameterName));
    Statement* body = *(method->body);
    const std::string& method_name(method->name->value);
    signature.parameter_types.types.insert(
        signature.parameter_types.types.begin() + signature.implicit_count,
        container_type);
    Method* m = Declarations::CreateMethod(container_type, method_name,
                                           signature, body);
    m->SetPosition(method->pos);
    m->SetIdentifierPosition(method->name->pos);
  }
}

const BitFieldStructType* TypeVisitor::ComputeType(
    BitFieldStructDeclaration* decl, MaybeSpecializationKey specialized_from) {
  CurrentSourcePosition::Scope position_scope(decl->pos);
  if (specialized_from.has_value()) {
    ReportError("Bitfield struct specialization is not supported");
  }
  const Type* parent = TypeVisitor::ComputeType(decl->parent);
  if (!IsAnyUnsignedInteger(parent)) {
    ReportError(
        "Bitfield struct must extend from an unsigned integer type, not ",
        parent->ToString());
  }
  auto opt_size = SizeOf(parent);
  if (!opt_size.has_value()) {
    ReportError("Cannot determine size of bitfield struct ", decl->name->value,
                " because of unsized parent type ", parent->ToString());
  }
  const size_t size = 8 * std::get<0>(*opt_size);  // Convert bytes to bits.
  BitFieldStructType* type = TypeOracle::GetBitFieldStructType(parent, decl);

  // Iterate through all of the declared fields, checking their validity and
  // registering them on the newly-constructed BitFieldStructType instance.
  int offset = 0;
  for (const auto& field : decl->fields) {
    CurrentSourcePosition::Scope field_position_scope(
        field.name_and_type.type->pos);
    const Type* field_type = TypeVisitor::ComputeType(field.name_and_type.type);
    if (!IsAllowedAsBitField(field_type)) {
      ReportError("Type not allowed as bitfield: ",
                  field.name_and_type.name->value);
    }

    // Compute the maximum number of bits that could be used for a field of this
    // type. Booleans are a special case, not included in SizeOf, because their
    // runtime size is 32 bits but they should only occupy 1 bit as a bitfield.
    size_t field_type_size = 0;
    if (field_type->IsSubtypeOf(TypeOracle::GetBoolType())) {
      field_type_size = 1;
    } else {
      auto opt_field_type_size = SizeOf(field_type);
      if (!opt_field_type_size.has_value()) {
        ReportError("Size unknown for type ", field_type->ToString());
      }
      field_type_size = 8 * std::get<0>(*opt_field_type_size);
    }

    if (field.num_bits < 1 ||
        static_cast<size_t>(field.num_bits) > field_type_size) {
      ReportError("Invalid number of bits for ",
                  field.name_and_type.name->value);
    }
    type->RegisterField({field.name_and_type.name->pos,
                         {field.name_and_type.name->value, field_type},
                         offset,
                         field.num_bits});
    offset += field.num_bits;
    if (static_cast<size_t>(offset) > size) {
      ReportError("Too many total bits in ", decl->name->value);
    }
  }

  return type;
}

const StructType* TypeVisitor::ComputeType(
    StructDeclaration* decl, MaybeSpecializationKey specialized_from) {
  StructType* struct_type = TypeOracle::GetStructType(decl, specialized_from);
  CurrentScope::Scope struct_namespace_scope(struct_type->nspace());
  CurrentSourcePosition::Scope decl_position_activator(decl->pos);

  ResidueClass offset = 0;
  for (auto& field : decl->fields) {
    CurrentSourcePosition::Scope position_activator(
        field.name_and_type.type->pos);
    const Type* field_type = TypeVisitor::ComputeType(field.name_and_type.type);
    if (field_type->IsConstexpr()) {
      ReportError("struct field \"", field.name_and_type.name->value,
                  "\" carries constexpr type \"", *field_type, "\"");
    }
    Field f{field.name_and_type.name->pos,
            struct_type,
            std::nullopt,
            {field.name_and_type.name->value, field_type},
            offset.SingleValue(),
            false,
            field.const_qualified,
            FieldSynchronization::kNone,
            FieldSynchronization::kNone};
    auto optional_size = SizeOf(f.name_and_type.type);
    struct_type->RegisterField(f);
    // Offsets are assigned based on an assumption of no space between members.
    // This might lead to invalid alignment in some cases, but most structs are
    // never actually packed in memory together (they just represent a batch of
    // CSA TNode values that should be passed around together). For any struct
    // that is used as a class field, we verify its offsets when setting up the
    // class type.
    if (optional_size.has_value()) {
      size_t field_size = 0;
      std::tie(field_size, std::ignore) = *optional_size;
      offset += field_size;
    } else {
      // Structs may contain fields that aren't representable in packed form. If
      // so, the offset of subsequent fields are marked as invalid.
      offset = ResidueClass::Unknown();
    }
  }
  return struct_type;
}

const ClassType* TypeVisitor::ComputeType(
    ClassDeclaration* decl, MaybeSpecializationKey specialized_from) {
  // TODO(sigurds): Remove this hack by introducing a declarable for classes.
  const TypeAlias* alias =
      Declarations::LookupTypeAlias(QualifiedName(decl->name->value));
  DCHECK_EQ(*alias->delayed_, decl);
  ClassFlags flags = decl->flags;
  bool is_shape = flags & ClassFlag::kIsShape;
  std::string generates = decl->name->value;
  const Type* super_type = TypeVisitor::ComputeType(decl->super);
  if (is_shape) {
    if (!(flags & ClassFlag::kExtern)) {
      ReportError("Shapes must be extern, add \"extern\" to the declaration.");
    }
    if (flags & ClassFlag::kUndefinedLayout) {
      ReportError("Shapes need to define their layout.");
    }
    const ClassType* super_class = ClassType::DynamicCast(super_type);
    if (!super_class ||
        !super_class->IsSubtypeOf(TypeOracle::GetJSObjectType())) {
      Error("Shapes need to extend a subclass of ",
            *TypeOracle::GetJSObjectType())
          .Throw();
    }
    // Shapes use their super class in CSA code since they have incomplete
    // support for type-checks on the C++ side.
    generates = super_class->name();
  }
  if (super_type != TypeOracle::GetStrongTaggedType()) {
    const ClassType* super_class = ClassType::DynamicCast(super_type);
    if (!super_class) {
      ReportError(
          "class \"", decl->name->value,
          "\" must extend either StrongTagged or an already declared class");
    }
    if (super_class->HasUndefinedLayout() &&
        !(flags & ClassFlag::kUndefinedLayout)) {
      Error("Class \"", decl->name->value,
            "\" defines its layout but extends a class which does not")
          .Position(decl->pos);
    }
    if ((flags & ClassFlag::kExport) &&
        !(super_class->ShouldExport() || super_class->IsExtern())) {
      Error("cannot export class ", decl->name,
            " because superclass is neither @export or extern");
    }
  }
  if ((flags & ClassFlag::kGenerateBodyDescriptor ||
       flags & ClassFlag::kExport) &&
      flags & ClassFlag::kUndefinedLayout) {
    Error("Class \"", decl->name->value,
          "\" requires a layout but doesn't have one");
  }
  if (flags & ClassFlag::kGenerateUniqueMap) {
    if (!(flags & ClassFlag::kExtern)) {
      Error("No need to specify ", ANNOTATION_GENERATE_UNIQUE_MAP,
            ", non-extern classes always have a unique map.");
    }
    if (flags & ClassFlag::kAbstract) {
      Error(ANNOTATION_ABSTRACT, " and ", ANNOTATION_GENERATE_UNIQUE_MAP,
            " shouldn't be used together, because abstract classes are never "
            "instantiated.");
    }
  }
  if ((flags & ClassFlag::kGenerateFactoryFunction) &&
      (flags & ClassFlag::kAbstract)) {
    Error(ANNOTATION_ABSTRACT, " and ", ANNOTATION_GENERATE_FACTORY_FUNCTION,
          " shouldn't be used together, because abstract classes are never "
          "instantiated.");
  }
  if (flags & ClassFlag::kExtern) {
    if (decl->generates) {
      bool enforce_tnode_type = true;
      std::string explicit_generates =
          ComputeGeneratesType(decl->generates, enforce_tnode_type);
      if (explicit_generates == generates) {
        Lint("Unnecessary 'generates' clause for class ", decl->name->value);
      }
      generates = explicit_generates;
    }
    if (flags & ClassFlag::kExport) {
      Error("cannot export a class that is marked extern");
    }
  } else {
    if (decl->generates) {
      ReportError("Only extern classes can specify a generated type.");
    }
    if (super_type != TypeOracle::GetStrongTaggedType()) {
      if (flags & ClassFlag::kUndefinedLayout) {
        Error("non-external classes must have defined layouts");
      }
    }
  }
  if (!(flags & ClassFlag::kExtern) &&
      (flags & ClassFlag::kHasSameInstanceTypeAsParent)) {
    Error("non-extern Torque-defined classes must have unique instance types");
  }
  if ((flags & ClassFlag::kHasSameInstanceTypeAsParent) &&
      !(flags & ClassFlag::kDoNotGenerateCast || flags & ClassFlag::kIsShape)) {
    Error(
        "classes that inherit their instance type must be annotated with "
        "@doNotGenerateCast");
  }

  return TypeOracle::GetClassType(super_type, decl->name->value, flags,
                                  generates, decl, alias);
}

const Type* TypeVisitor::ComputeType(TypeExpression* type_expression) {
  if (auto* basic = BasicTypeExpression::DynamicCast(type_expression)) {
    QualifiedName qualified_name{basic->namespace_qualification,
                                 basic->name->value};
    auto& args = basic->generic_arguments;
    const Type* type;
    SourcePosition pos = SourcePosition::Invalid();

    if (args.empty()) {
      auto* alias = Declarations::LookupTypeAlias(qualified_name);
      type = alias->type();
      pos = alias->GetDeclarationPosition();
      if (GlobalContext::collect_kythe_data()) {
        if (alias->IsUserDefined()) {
          KytheData::AddTypeUse(basic->name->pos, alias);
        }
      }
    } else {
      auto* generic_type =
          Declarations::LookupUniqueGenericType(qualified_name);
      type = TypeOracle::GetGenericTypeInstance(generic_type,
                                                ComputeTypeVector(args));
      pos = generic_type->declaration()->name->pos;
      if (GlobalContext::collect_kythe_data()) {
        KytheData::AddTypeUse(basic->name->pos, generic_type);
      }
    }

    if (GlobalContext::collect_language_server_data()) {
      LanguageServerData::AddDefinition(type_expression->pos, pos);
    }
    return type;

  } else if (auto* union_type =
                 UnionTypeExpression::DynamicCast(type_expression)) {
    return TypeOracle::GetUnionType(ComputeType(union_type->a),
                                    ComputeType(union_type->b));
  } else if (auto* function_type_exp =
                 FunctionTypeExpression::DynamicCast(type_expression)) {
    TypeVector argument_types;
    for (TypeExpression* type_exp : function_type_exp->parameters) {
      argument_types.push_back(ComputeType(type_exp));
    }
    return TypeOracle::GetBuiltinPointerType(
        argument_types, ComputeType(function_type_exp->return_type));
  } else {
    auto* precomputed = PrecomputedTypeExpression::cast(type_expression);
    return precomputed->type;
  }
}

Signature TypeVisitor::MakeSignature(const CallableDeclaration* declaration) {
  LabelDeclarationVector definition_vector;
  for (const auto& label : declaration->labels) {
    LabelDeclaration def = {label.name, ComputeTypeVector(label.types)};
    definition_vector.push_back(def);
  }
  std::optional<std::string> arguments_variable;
  if (declaration->parameters.has_varargs)
    arguments_variable = declaration->parameters.arguments_variable;
  Signature result{declaration->parameters.names,
                   arguments_variable,
                   {ComputeTypeVector(declaration->parameters.types),
                    declaration->parameters.has_varargs},
                   declaration->parameters.implicit_count,
                   ComputeType(declaration->return_type),
                   definition_vector,
                   declaration->transitioning};
  return result;
}

void TypeVisitor::VisitClassFieldsAndMethods(
    ClassType* class_type, const ClassDeclaration* class_declaration) {
  const ClassType* super_class = class_type->GetSuperClass();
  ResidueClass class_offset = 0;
  size_t header_size = 0;
  if (super_class) {
    class_offset = super_class->size();
    header_size = super_class->header_size();
  }

  for (const ClassFieldExpression& field_expression :
       class_declaration->fields) {
    CurrentSourcePosition::Scope position_activator(
        field_expression.name_and_type.type->pos);
    const Type* field_type = ComputeType(field_expression.name_and_type.type);
    if (class_type->IsShape()) {
      if (!field_type->IsSubtypeOf(TypeOracle::GetObjectType())) {
        ReportError(
            "in-object properties only support subtypes of Object, but "
            "found type ",
            *field_type);
      }
      if (field_expression.custom_weak_marking) {
        ReportError("in-object properties cannot use @customWeakMarking");
      }
    }
    std::optional<ClassFieldIndexInfo> array_length = field_expression.index;
    const Field& field = class_type->RegisterField(
        {field_expression.name_and_type.name->pos,
         class_type,
         array_length,
         {field_expression.name_and_type.name->value, field_type},
         class_offset.SingleValue(),
         field_expression.custom_weak_marking,
         field_expression.const_qualified,
         field_expression.read_synchronization,
         field_expression.write_synchronization});
    ResidueClass field_size = std::get<0>(field.GetFieldSizeInformation());
    if (field.index) {
      // Validate that a value at any index in a packed array is aligned
      // correctly, since it is possible to define a struct whose size is not a
      // multiple of its alignment.
      field.ValidateAlignment(class_offset +
                              field_size * ResidueClass::Unknown());

      if (auto literal =
              IntegerLiteralExpression::DynamicCast(field.index->expr)) {
        if (auto value = literal->value.TryTo<size_t>()) {
          field_size *= *value;
        } else {
          Error("Not a valid field index").Position(field.pos);
        }
      } else {
        field_size *= ResidueClass::Unknown();
      }
    }
    field.ValidateAlignment(class_offset);
    class_offset += field_size;
    // In-object properties are not considered part of the header.
    if (class_offset.SingleValue() && !class_type->IsShape()) {
      header_size = *class_offset.SingleValue();
    }
    if (!field.index && !class_offset.SingleValue()) {
      Error("Indexed fields have to be at the end of the object")
          .Position(field.pos);
    }
  }
  DCHECK_GT(header_size, 0);
  class_type->header_size_ = header_size;
  class_type->size_ = class_offset;
  class_type->GenerateAccessors();
  DeclareMethods(class_type, class_declaration->methods);
}

void TypeVisitor::VisitStructMethods(
    StructType* struct_type, const StructDeclaration* struct_declaration) {
  DeclareMethods(struct_type, struct_declaration->methods);
}

const Type* TypeVisitor::ComputeTypeForStructExpression(
    TypeExpression* type_expression,
    const std::vector<const Type*>& term_argument_types) {
  auto* basic = BasicTypeExpression::DynamicCast(type_expression);
  if (!basic) {
    ReportError("expected basic type expression referring to struct");
  }

  QualifiedName qualified_name{basic->namespace_qualification,
                               basic->name->value};
  std::optional<GenericType*> maybe_generic_type =
      Declarations::TryLookupGenericType(qualified_name);

  StructDeclaration* decl =
      maybe_generic_type
          ? StructDeclaration::DynamicCast((*maybe_generic_type)->declaration())
          : nullptr;

  // Compute types of non-generic structs as usual
  if (!(maybe_generic_type && decl)) {
    const Type* type = ComputeType(type_expression);
    if (!type->IsStructType() && !type->IsBitFieldStructType()) {
      ReportError(*type,
                  " is not a struct or bitfield struct, but used like one");
    }
    return type;
  }

  auto generic_type = *maybe_generic_type;
  auto explicit_type_arguments = ComputeTypeVector(basic->generic_arguments);

  std::vector<TypeExpression*> term_parameters;
  auto& fields = decl->fields;
  term_parameters.reserve(fields.size());
  for (auto& field : fields) {
    term_parameters.push_back(field.name_and_type.type);
  }

  CurrentScope::Scope generic_scope(generic_type->ParentScope());
  TypeArgumentInference inference(
      generic_type->generic_parameters(), explicit_type_arguments,
      term_parameters,
      TransformVector<std::optional<const Type*>>(term_argument_types));

  if (inference.HasFailed()) {
    ReportError("failed to infer type arguments for struct ", basic->name,
                " initialization: ", inference.GetFailureReason());
  }
  if (GlobalContext::collect_language_server_data()) {
    LanguageServerData::AddDefinition(type_expression->pos,
                                      generic_type->declaration()->name->pos);
  }
  return StructType::cast(
      TypeOracle::GetGenericTypeInstance(generic_type, inference.GetResult()));
}

}  // namespace v8::internal::torque
