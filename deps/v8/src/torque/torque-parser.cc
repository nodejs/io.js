// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/torque-parser.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "include/v8config.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/torque/ast.h"
#include "src/torque/constants.h"
#include "src/torque/declarations.h"
#include "src/torque/earley-parser.h"
#include "src/torque/global-context.h"
#include "src/torque/utils.h"

namespace v8::internal::torque {

using TypeList = std::vector<TypeExpression*>;

struct ExpressionWithSource {
  Expression* expression;
  std::string source;
};

struct TypeswitchCase {
  SourcePosition pos;
  std::optional<Identifier*> name;
  TypeExpression* type;
  Statement* block;
};

struct EnumEntry {
  Identifier* name;
  std::optional<TypeExpression*> type;
  std::optional<std::string> alias_entry;
};

class BuildFlags : public base::ContextualClass<BuildFlags> {
 public:
  BuildFlags() {
    build_flags_["V8_EXTERNAL_CODE_SPACE"] = V8_EXTERNAL_CODE_SPACE_BOOL;
    build_flags_["TAGGED_SIZE_8_BYTES"] = TargetArchitecture::TaggedSize() == 8;
    build_flags_["V8_ENABLE_EXPERIMENTAL_UNDEFINED_DOUBLE"] =
        V8_EXPERIMENTAL_UNDEFINED_DOUBLE_BOOL;
#ifdef V8_INTL_SUPPORT
    build_flags_["V8_INTL_SUPPORT"] = true;
#else
    build_flags_["V8_INTL_SUPPORT"] = false;
#endif
    build_flags_["V8_ENABLE_SWISS_NAME_DICTIONARY"] =
        V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL;
#ifdef V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
    build_flags_["V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS"] = true;
#else
    build_flags_["V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS"] = false;
#endif
#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
    build_flags_["V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA"] = true;
#else
    build_flags_["V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA"] = false;
#endif
    build_flags_["TRUE_FOR_TESTING"] = true;
    build_flags_["FALSE_FOR_TESTING"] = false;
#ifdef V8_SCRIPTORMODULE_LEGACY_LIFETIME
    build_flags_["V8_SCRIPTORMODULE_LEGACY_LIFETIME"] = true;
#else
    build_flags_["V8_SCRIPTORMODULE_LEGACY_LIFETIME"] = false;
#endif
#ifdef V8_ENABLE_WEBASSEMBLY
    build_flags_["V8_ENABLE_WEBASSEMBLY"] = true;
    build_flags_["WASM_CODE_POINTER_NEEDS_PADDING"] =
        TargetArchitecture::TaggedSize() == 8;
#else
    build_flags_["V8_ENABLE_WEBASSEMBLY"] = false;
#endif
    build_flags_["V8_ENABLE_SANDBOX"] = V8_ENABLE_SANDBOX_BOOL;
    build_flags_["V8_ENABLE_LEAPTIERING"] = V8_ENABLE_LEAPTIERING_BOOL;
    build_flags_["V8_ENABLE_LEAPTIERING_TAGGED_SIZE_8_BYTES"] =
        V8_ENABLE_LEAPTIERING_BOOL && build_flags_["TAGGED_SIZE_8_BYTES"];
    build_flags_["DEBUG"] = DEBUG_BOOL;
#ifdef V8_ENABLE_DRUMBRAKE
    build_flags_["V8_ENABLE_DRUMBRAKE"] = true;
#else
    build_flags_["V8_ENABLE_DRUMBRAKE"] = false;
#endif
  }
  static bool GetFlag(const std::string& name, const char* production) {
    auto it = Get().build_flags_.find(name);
    if (it == Get().build_flags_.end()) {
      ReportError("Unknown flag used in ", production, ": ", name,
                  ". Please add it to the list in BuildFlags.");
    }
    return it->second;
  }

 private:
  std::unordered_map<std::string, bool> build_flags_;
};

template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<std::string>::id =
    ParseResultTypeId::kStdString;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<bool>::id =
    ParseResultTypeId::kBool;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<int32_t>::id =
    ParseResultTypeId::kInt32;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<double>::id =
    ParseResultTypeId::kDouble;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<IntegerLiteral>::id = ParseResultTypeId::kIntegerLiteral;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<std::string>>::id =
        ParseResultTypeId::kStdVectorOfString;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<Declaration*>::id =
    ParseResultTypeId::kDeclarationPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<TypeExpression*>::id =
        ParseResultTypeId::kTypeExpressionPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<TypeExpression*>>::id =
        ParseResultTypeId::kOptionalTypeExpressionPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<TryHandler*>::id =
    ParseResultTypeId::kTryHandlerPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<Expression*>::id =
    ParseResultTypeId::kExpressionPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<Identifier*>::id =
    ParseResultTypeId::kIdentifierPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<Identifier*>>::id =
        ParseResultTypeId::kOptionalIdentifierPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<Statement*>::id =
    ParseResultTypeId::kStatementPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<NameAndTypeExpression>::id =
        ParseResultTypeId::kNameAndTypeExpression;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<EnumEntry>::id =
    ParseResultTypeId::kEnumEntry;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<EnumEntry>>::id =
        ParseResultTypeId::kStdVectorOfEnumEntry;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<NameAndExpression>::id =
        ParseResultTypeId::kNameAndExpression;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<Annotation>::id =
    ParseResultTypeId::kAnnotation;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<Annotation>>::id =
        ParseResultTypeId::kVectorOfAnnotation;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<AnnotationParameter>::id =
        ParseResultTypeId::kAnnotationParameter;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<AnnotationParameter>>::id =
        ParseResultTypeId::kOptionalAnnotationParameter;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<ClassFieldExpression>::id =
        ParseResultTypeId::kClassFieldExpression;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<StructFieldExpression>::id =
        ParseResultTypeId::kStructFieldExpression;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<BitFieldDeclaration>::id =
        ParseResultTypeId::kBitFieldDeclaration;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<NameAndTypeExpression>>::id =
        ParseResultTypeId::kStdVectorOfNameAndTypeExpression;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<ImplicitParameters>::id =
        ParseResultTypeId::kImplicitParameters;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<ImplicitParameters>>::id =
        ParseResultTypeId::kOptionalImplicitParameters;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<NameAndExpression>>::id =
        ParseResultTypeId::kStdVectorOfNameAndExpression;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<ClassFieldExpression>>::id =
        ParseResultTypeId::kStdVectorOfClassFieldExpression;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<StructFieldExpression>>::id =
        ParseResultTypeId::kStdVectorOfStructFieldExpression;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<BitFieldDeclaration>>::id =
        ParseResultTypeId::kStdVectorOfBitFieldDeclaration;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<IncrementDecrementOperator>::id =
        ParseResultTypeId::kIncrementDecrementOperator;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<std::string>>::id =
        ParseResultTypeId::kOptionalStdString;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<Statement*>>::id =
        ParseResultTypeId::kStdVectorOfStatementPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<Declaration*>>::id =
        ParseResultTypeId::kStdVectorOfDeclarationPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<std::vector<Declaration*>>>::id =
        ParseResultTypeId::kStdVectorOfStdVectorOfDeclarationPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<Expression*>>::id =
        ParseResultTypeId::kStdVectorOfExpressionPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<ExpressionWithSource>::id =
        ParseResultTypeId::kExpressionWithSource;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<ParameterList>::id =
    ParseResultTypeId::kParameterList;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<TypeList>::id =
    ParseResultTypeId::kTypeList;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<TypeList>>::id =
        ParseResultTypeId::kOptionalTypeList;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId ParseResultHolder<LabelAndTypes>::id =
    ParseResultTypeId::kLabelAndTypes;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<LabelAndTypes>>::id =
        ParseResultTypeId::kStdVectorOfLabelAndTypes;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<TryHandler*>>::id =
        ParseResultTypeId::kStdVectorOfTryHandlerPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<Statement*>>::id =
        ParseResultTypeId::kOptionalStatementPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<Expression*>>::id =
        ParseResultTypeId::kOptionalExpressionPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<TypeswitchCase>::id = ParseResultTypeId::kTypeswitchCase;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<TypeswitchCase>>::id =
        ParseResultTypeId::kStdVectorOfTypeswitchCase;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::vector<Identifier*>>::id =
        ParseResultTypeId::kStdVectorOfIdentifierPtr;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<std::optional<ClassBody*>>::id =
        ParseResultTypeId::kOptionalClassBody;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<GenericParameter>::id =
        ParseResultTypeId::kGenericParameter;
template <>
V8_EXPORT_PRIVATE const ParseResultTypeId
    ParseResultHolder<GenericParameters>::id =
        ParseResultTypeId::kGenericParameters;

namespace {

bool ProcessIfAnnotation(ParseResultIterator* child_results);
class AnnotationSet;
bool ProcessIfAnnotation(const AnnotationSet& annotations);

std::optional<ParseResult> AddGlobalDeclarations(
    ParseResultIterator* child_results) {
  auto declarations = child_results->NextAs<std::vector<Declaration*>>();
  for (Declaration* declaration : declarations) {
    CurrentAst::Get().declarations().push_back(declaration);
  }
  return std::nullopt;
}

void NamingConventionError(const std::string& type, const std::string& name,
                           const std::string& convention,
                           SourcePosition pos = CurrentSourcePosition::Get()) {
  Lint(type, " \"", name, "\" does not follow \"", convention,
       "\" naming convention.")
      .Position(pos);
}

void NamingConventionError(const std::string& type, const Identifier* name,
                           const std::string& convention) {
  NamingConventionError(type, name->value, convention, name->pos);
}

void LintGenericParameters(const GenericParameters& parameters) {
  for (auto parameter : parameters) {
    if (!IsUpperCamelCase(parameter.name->value)) {
      NamingConventionError("Generic parameter", parameter.name,
                            "UpperCamelCase");
    }
  }
}

std::optional<ParseResult> ConcatList(ParseResultIterator* child_results) {
  auto list_of_lists =
      child_results->NextAs<std::vector<std::vector<Declaration*>>>();
  std::vector<Declaration*> result;
  for (auto& list : list_of_lists) {
    result.insert(result.end(), list.begin(), list.end());
  }
  return ParseResult{std::move(result)};
}

void CheckNotDeferredStatement(Statement* statement) {
  CurrentSourcePosition::Scope source_position(statement->pos);
  if (BlockStatement* block = BlockStatement::DynamicCast(statement)) {
    if (block->deferred) {
      Lint(
          "cannot use deferred with a statement block here, it will have no "
          "effect");
    }
  }
}

TypeExpression* AddConstexpr(TypeExpression* type) {
  BasicTypeExpression* basic = BasicTypeExpression::DynamicCast(type);
  if (!basic) Error("Unsupported extends clause.").Throw();
  return MakeNode<BasicTypeExpression>(
      basic->namespace_qualification,
      MakeNode<Identifier>(CONSTEXPR_TYPE_PREFIX + basic->name->value),
      basic->generic_arguments);
}

Expression* MakeCall(IdentifierExpression* callee,
                     std::optional<Expression*> target,
                     std::vector<Expression*> arguments,
                     const std::vector<Statement*>& otherwise) {
  std::vector<Identifier*> labels;

  // All IdentifierExpressions are treated as label names and can be directly
  // used as labels identifiers. All other statements in a call's otherwise
  // must create intermediate Labels for the otherwise's statement code.
  size_t label_id_count = 0;
  std::vector<TryHandler*> temp_labels;
  for (auto* statement : otherwise) {
    if (auto* e = ExpressionStatement::DynamicCast(statement)) {
      if (auto* id = IdentifierExpression::DynamicCast(e->expression)) {
        if (!id->generic_arguments.empty()) {
          ReportError("An otherwise label cannot have generic parameters");
        }
        labels.push_back(id->name);
        continue;
      }
    }
    auto label_name = std::string("__label") + std::to_string(label_id_count++);
    auto label_id = MakeNode<Identifier>(label_name);
    label_id->pos = SourcePosition::Invalid();
    labels.push_back(label_id);
    auto* handler =
        MakeNode<TryHandler>(TryHandler::HandlerKind::kLabel, label_id,
                             ParameterList::Empty(), statement);
    temp_labels.push_back(handler);
  }

  // Create nested try-label expression for all of the temporary Labels that
  // were created.
  Expression* result = nullptr;
  if (target) {
    result = MakeNode<CallMethodExpression>(
        *target, callee, std::move(arguments), std::move(labels));
  } else {
    result = MakeNode<CallExpression>(callee, std::move(arguments),
                                      std::move(labels));
  }

  for (auto* label : temp_labels) {
    result = MakeNode<TryLabelExpression>(result, label);
  }
  return result;
}

Expression* MakeCall(Identifier* callee,
                     const std::vector<TypeExpression*>& generic_arguments,
                     const std::vector<Expression*>& arguments,
                     const std::vector<Statement*>& otherwise) {
  return MakeCall(MakeNode<IdentifierExpression>(callee, generic_arguments),
                  std::nullopt, arguments, otherwise);
}

std::optional<ParseResult> MakeCall(ParseResultIterator* child_results) {
  auto callee = child_results->NextAs<Expression*>();
  auto args = child_results->NextAs<std::vector<Expression*>>();
  auto otherwise = child_results->NextAs<std::vector<Statement*>>();
  IdentifierExpression* target = IdentifierExpression::cast(callee);
  return ParseResult{
      MakeCall(target, std::nullopt, std::move(args), otherwise)};
}

std::optional<ParseResult> MakeMethodCall(ParseResultIterator* child_results) {
  auto this_arg = child_results->NextAs<Expression*>();
  auto callee = child_results->NextAs<Identifier*>();
  auto args = child_results->NextAs<std::vector<Expression*>>();
  auto otherwise = child_results->NextAs<std::vector<Statement*>>();
  return ParseResult{MakeCall(MakeNode<IdentifierExpression>(callee), this_arg,
                              std::move(args), otherwise)};
}

std::optional<ParseResult> MakeNewExpression(
    ParseResultIterator* child_results) {
  bool pretenured = child_results->NextAs<bool>();
  bool clear_padding = child_results->NextAs<bool>();

  auto type = child_results->NextAs<TypeExpression*>();
  auto initializers = child_results->NextAs<std::vector<NameAndExpression>>();

  Expression* result = MakeNode<NewExpression>(type, std::move(initializers),
                                               pretenured, clear_padding);
  return ParseResult{result};
}

std::optional<ParseResult> MakeBinaryOperator(
    ParseResultIterator* child_results) {
  auto left = child_results->NextAs<Expression*>();
  auto op = child_results->NextAs<Identifier*>();
  auto right = child_results->NextAs<Expression*>();
  return ParseResult{MakeCall(op, TypeList{},
                              std::vector<Expression*>{left, right},
                              std::vector<Statement*>{})};
}

std::optional<ParseResult> MakeIntrinsicCallExpression(
    ParseResultIterator* child_results) {
  auto callee = child_results->NextAs<Identifier*>();
  auto generic_arguments =
      child_results->NextAs<std::vector<TypeExpression*>>();
  auto args = child_results->NextAs<std::vector<Expression*>>();
  Expression* result = MakeNode<IntrinsicCallExpression>(
      callee, std::move(generic_arguments), std::move(args));
  return ParseResult{result};
}

std::optional<ParseResult> MakeUnaryOperator(
    ParseResultIterator* child_results) {
  auto op = child_results->NextAs<Identifier*>();
  auto e = child_results->NextAs<Expression*>();
  return ParseResult{MakeCall(op, TypeList{}, std::vector<Expression*>{e},
                              std::vector<Statement*>{})};
}

std::optional<ParseResult> MakeSpreadExpression(
    ParseResultIterator* child_results) {
  auto spreadee = child_results->NextAs<Expression*>();
  Expression* result = MakeNode<SpreadExpression>(spreadee);
  return ParseResult{result};
}

std::optional<ParseResult> MakeImplicitParameterList(
    ParseResultIterator* child_results) {
  auto kind = child_results->NextAs<Identifier*>();
  auto parameters = child_results->NextAs<std::vector<NameAndTypeExpression>>();
  return ParseResult{ImplicitParameters{kind, std::move(parameters)}};
}

void AddParameter(ParameterList* parameter_list,
                  const NameAndTypeExpression& param) {
  if (!IsLowerCamelCase(param.name->value)) {
    NamingConventionError("Parameter", param.name, "lowerCamelCase");
  }
  parameter_list->names.push_back(param.name);
  parameter_list->types.push_back(param.type);
}

template <bool has_varargs, bool has_explicit_parameter_names>
std::optional<ParseResult> MakeParameterList(
    ParseResultIterator* child_results) {
  auto implicit_params =
      child_results->NextAs<std::optional<ImplicitParameters>>();
  ParameterList result;
  result.has_varargs = has_varargs;
  result.implicit_count = 0;
  result.implicit_kind = ImplicitKind::kNoImplicit;
  if (implicit_params) {
    result.implicit_count = implicit_params->parameters.size();
    if (implicit_params->kind->value == "implicit") {
      result.implicit_kind = ImplicitKind::kImplicit;
    } else {
      DCHECK_EQ(implicit_params->kind->value, "js-implicit");
      result.implicit_kind = ImplicitKind::kJSImplicit;
    }
    result.implicit_kind_pos = implicit_params->kind->pos;
    for (NameAndTypeExpression& implicit_param : implicit_params->parameters) {
      AddParameter(&result, implicit_param);
    }
  }
  if (has_explicit_parameter_names) {
    auto explicit_params =
        child_results->NextAs<std::vector<NameAndTypeExpression>>();
    std::string arguments_variable = "";
    if (has_varargs) {
      arguments_variable = child_results->NextAs<std::string>();
    }
    for (NameAndTypeExpression& param : explicit_params) {
      AddParameter(&result, param);
    }
    result.arguments_variable = arguments_variable;
  } else {
    auto explicit_types = child_results->NextAs<TypeList>();
    for (auto* explicit_type : explicit_types) {
      result.types.push_back(explicit_type);
    }
  }
  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeAssertStatement(
    ParseResultIterator* child_results) {
  auto kind_string = child_results->NextAs<Identifier*>()->value;
  auto expr_with_source = child_results->NextAs<ExpressionWithSource>();
  AssertStatement::AssertKind kind;
  if (kind_string == "dcheck") {
    kind = AssertStatement::AssertKind::kDcheck;
  } else if (kind_string == "check") {
    kind = AssertStatement::AssertKind::kCheck;
  } else if (kind_string == "sbxcheck") {
#ifdef V8_ENABLE_SANDBOX
    kind = AssertStatement::AssertKind::kSbxCheck;
#else
    kind = AssertStatement::AssertKind::kDcheck;
#endif  // V8_ENABLE_SANDBOX
  } else if (kind_string == "static_assert") {
    kind = AssertStatement::AssertKind::kStaticAssert;
  } else {
    UNREACHABLE();
  }
  Statement* result = MakeNode<AssertStatement>(
      kind, expr_with_source.expression, expr_with_source.source);
  return ParseResult{result};
}

std::optional<ParseResult> MakeDebugStatement(
    ParseResultIterator* child_results) {
  auto kind = child_results->NextAs<Identifier*>()->value;
  DCHECK(kind == "unreachable" || kind == "debug");
  Statement* result = MakeNode<DebugStatement>(
      kind == "unreachable" ? DebugStatement::Kind::kUnreachable
                            : DebugStatement::Kind::kDebug);
  return ParseResult{result};
}

std::optional<ParseResult> DeprecatedMakeVoidType(
    ParseResultIterator* child_results) {
  Error("Default void return types are deprecated. Add `: void`.");
  TypeExpression* result = MakeNode<BasicTypeExpression>(
      std::vector<std::string>{}, MakeNode<Identifier>("void"),
      std::vector<TypeExpression*>{});
  return ParseResult{result};
}

std::optional<ParseResult> MakeExternalMacro(
    ParseResultIterator* child_results) {
  bool enabled = ProcessIfAnnotation(child_results);
  auto transitioning = child_results->NextAs<bool>();
  auto operator_name = child_results->NextAs<std::optional<std::string>>();
  auto external_assembler_name =
      child_results->NextAs<std::optional<std::string>>();
  auto name = child_results->NextAs<Identifier*>();
  auto generic_parameters = child_results->NextAs<GenericParameters>();
  LintGenericParameters(generic_parameters);

  auto args = child_results->NextAs<ParameterList>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  auto labels = child_results->NextAs<LabelAndTypesVector>();

  std::vector<Declaration*> result = {};
  if (enabled) {
    result = {MakeNode<ExternalMacroDeclaration>(
        transitioning,
        external_assembler_name ? *external_assembler_name
                                : "CodeStubAssembler",
        name, operator_name, args, return_type, std::move(labels))};
  }
  if (!generic_parameters.empty()) {
    Error("External builtins cannot be generic.");
  }
  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeIntrinsicDeclaration(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<Identifier*>();
  auto generic_parameters = child_results->NextAs<GenericParameters>();
  LintGenericParameters(generic_parameters);

  auto args = child_results->NextAs<ParameterList>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  auto body = child_results->NextAs<std::optional<Statement*>>();
  CallableDeclaration* declaration;
  if (body) {
    declaration = MakeNode<TorqueMacroDeclaration>(
        false, name, std::optional<std::string>{}, args, return_type,
        LabelAndTypesVector{}, false, body);
  } else {
    declaration = MakeNode<IntrinsicDeclaration>(name, args, return_type);
  }
  Declaration* result = declaration;
  if (!generic_parameters.empty()) {
    result = MakeNode<GenericCallableDeclaration>(std::move(generic_parameters),
                                                  declaration);
  }
  return ParseResult{result};
}

namespace {
bool HasAnnotation(ParseResultIterator* child_results, const char* annotation,
                   const char* declaration) {
  auto annotations = child_results->NextAs<std::vector<Annotation>>();
  if (!annotations.empty()) {
    if (annotations.size() > 1 || annotations[0].name->value != annotation) {
      Error(declaration, " declarations only support a single ", annotation,
            " annotation");
    }
    return true;
  }
  return false;
}

bool HasExportAnnotation(ParseResultIterator* child_results,
                         const char* declaration) {
  return HasAnnotation(child_results, ANNOTATION_EXPORT, declaration);
}
}  // namespace

class AnnotationSet {
 public:
  AnnotationSet(ParseResultIterator* iter,
                const std::set<std::string>& allowed_without_param,
                const std::set<std::string>& allowed_with_param) {
    auto list = iter->NextAs<std::vector<Annotation>>();
    for (const Annotation& a : list) {
      if (a.param.has_value()) {
        if (allowed_with_param.find(a.name->value) ==
            allowed_with_param.end()) {
          const char* error_message =
              allowed_without_param.find(a.name->value) ==
                      allowed_without_param.end()
                  ? " is not allowed here"
                  : " cannot have parameter here";
          Lint("Annotation ", a.name->value, error_message)
              .Position(a.name->pos);
        }
        if (!map_.insert({a.name->value, {*a.param, a.name->pos}}).second) {
          Lint("Duplicate annotation ", a.name->value).Position(a.name->pos);
        }
      } else {
        if (allowed_without_param.find(a.name->value) ==
            allowed_without_param.end()) {
          const char* error_message =
              allowed_with_param.find(a.name->value) == allowed_with_param.end()
                  ? " is not allowed here"
                  : " requires a parameter here";
          Lint("Annotation ", a.name->value, error_message)
              .Position(a.name->pos);
        }
        if (!set_.insert(a.name->value).second) {
          Lint("Duplicate annotation ", a.name->value).Position(a.name->pos);
        }
      }
    }
  }

  bool Contains(const std::string& s) const {
    return set_.find(s) != set_.end();
  }
  std::optional<std::string> GetStringParam(const std::string& s) const {
    auto it = map_.find(s);
    if (it == map_.end()) {
      return {};
    }
    if (it->second.first.is_int) {
      Error("Annotation ", s, " requires a string parameter but has an int")
          .Position(it->second.second);
    }
    return it->second.first.string_value;
  }
  std::optional<int32_t> GetIntParam(const std::string& s) const {
    auto it = map_.find(s);
    if (it == map_.end()) {
      return {};
    }
    if (!it->second.first.is_int) {
      Error("Annotation ", s, " requires an int parameter but has a string")
          .Position(it->second.second);
    }
    return it->second.first.int_value;
  }

 private:
  std::set<std::string> set_;
  std::map<std::string, std::pair<AnnotationParameter, SourcePosition>> map_;
};

std::optional<ParseResult> MakeTorqueMacroDeclaration(
    ParseResultIterator* child_results) {
  AnnotationSet annotations(child_results, {ANNOTATION_EXPORT},
                            {ANNOTATION_IF, ANNOTATION_IFNOT});
  bool enabled = ProcessIfAnnotation(annotations);
  bool export_to_csa = annotations.Contains(ANNOTATION_EXPORT);
  auto transitioning = child_results->NextAs<bool>();
  auto operator_name = child_results->NextAs<std::optional<std::string>>();
  auto name = child_results->NextAs<Identifier*>();
  if (!IsUpperCamelCase(name->value)) {
    NamingConventionError("Macro", name, "UpperCamelCase");
  }

  auto generic_parameters = child_results->NextAs<GenericParameters>();
  LintGenericParameters(generic_parameters);

  auto args = child_results->NextAs<ParameterList>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  auto labels = child_results->NextAs<LabelAndTypesVector>();
  auto body = child_results->NextAs<std::optional<Statement*>>();
  std::vector<Declaration*> result = {};
  if (enabled) {
    CallableDeclaration* declaration = MakeNode<TorqueMacroDeclaration>(
        transitioning, name, operator_name, args, return_type,
        std::move(labels), export_to_csa, body);
    result = {declaration};
    if (generic_parameters.empty()) {
      if (!body) ReportError("A non-generic declaration needs a body.");
    } else {
      if (export_to_csa) ReportError("Cannot export generics to CSA.");
      result = {MakeNode<GenericCallableDeclaration>(
          std::move(generic_parameters), declaration)};
    }
  }
  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeConstDeclaration(
    ParseResultIterator* child_results) {
  bool enabled = ProcessIfAnnotation(child_results);
  auto name = child_results->NextAs<Identifier*>();
  if (!IsValidNamespaceConstName(name->value)) {
    NamingConventionError("Constant", name, "kUpperCamelCase");
  }

  auto type = child_results->NextAs<TypeExpression*>();
  auto expression = child_results->NextAs<Expression*>();
  std::vector<Declaration*> result = {};
  if (enabled) result = {MakeNode<ConstDeclaration>(name, type, expression)};
  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeExternConstDeclaration(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<Identifier*>();
  auto type = child_results->NextAs<TypeExpression*>();
  auto literal = child_results->NextAs<std::string>();
  Declaration* result =
      MakeNode<ExternConstDeclaration>(name, type, std::move(literal));
  return ParseResult{result};
}

std::optional<ParseResult> MakeTypeAliasDeclaration(
    ParseResultIterator* child_results) {
  bool enabled = ProcessIfAnnotation(child_results);
  auto name = child_results->NextAs<Identifier*>();
  auto type = child_results->NextAs<TypeExpression*>();
  std::vector<Declaration*> result = {};
  if (enabled) result = {MakeNode<TypeAliasDeclaration>(name, type)};
  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeAbstractTypeDeclaration(
    ParseResultIterator* child_results) {
  bool use_parent_type_checker = HasAnnotation(
      child_results, ANNOTATION_USE_PARENT_TYPE_CHECKER, "abstract type");
  auto transient = child_results->NextAs<bool>();
  auto name = child_results->NextAs<Identifier*>();
  if (!IsValidTypeName(name->value)) {
    NamingConventionError("Type", name, "UpperCamelCase");
  }
  auto generic_parameters = child_results->NextAs<GenericParameters>();
  auto extends = child_results->NextAs<std::optional<TypeExpression*>>();
  auto generates = child_results->NextAs<std::optional<std::string>>();
  AbstractTypeFlags flags(AbstractTypeFlag::kNone);
  if (transient) flags |= AbstractTypeFlag::kTransient;
  if (use_parent_type_checker) flags |= AbstractTypeFlag::kUseParentTypeChecker;
  TypeDeclaration* type_decl = MakeNode<AbstractTypeDeclaration>(
      name, flags, extends, std::move(generates));
  Declaration* decl = type_decl;
  if (!generic_parameters.empty()) {
    decl = MakeNode<GenericTypeDeclaration>(generic_parameters, type_decl);
  }

  auto constexpr_generates =
      child_results->NextAs<std::optional<std::string>>();
  std::vector<Declaration*> result{decl};

  if (constexpr_generates) {
    // Create a AbstractTypeDeclaration for the associated constexpr type.
    Identifier* constexpr_name =
        MakeNode<Identifier>(CONSTEXPR_TYPE_PREFIX + name->value);
    constexpr_name->pos = name->pos;

    std::optional<TypeExpression*> constexpr_extends;
    if (extends) {
      constexpr_extends = AddConstexpr(*extends);
    }
    TypeDeclaration* constexpr_decl = MakeNode<AbstractTypeDeclaration>(
        constexpr_name, flags | AbstractTypeFlag::kConstexpr, constexpr_extends,
        constexpr_generates);
    constexpr_decl->pos = name->pos;
    decl = constexpr_decl;
    if (!generic_parameters.empty()) {
      decl = MakeNode<GenericTypeDeclaration>(std::move(generic_parameters),
                                              constexpr_decl);
    }
    result.push_back(decl);
  }

  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeMethodDeclaration(
    ParseResultIterator* child_results) {
  auto transitioning = child_results->NextAs<bool>();
  auto operator_name = child_results->NextAs<std::optional<std::string>>();
  auto name = child_results->NextAs<Identifier*>();
  if (!IsUpperCamelCase(name->value)) {
    NamingConventionError("Method", name, "UpperCamelCase");
  }

  auto args = child_results->NextAs<ParameterList>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  auto labels = child_results->NextAs<LabelAndTypesVector>();
  auto body = child_results->NextAs<Statement*>();
  Declaration* result = MakeNode<TorqueMacroDeclaration>(
      transitioning, name, operator_name, args, return_type, std::move(labels),
      false, body);
  return ParseResult{result};
}

bool ProcessIfAnnotation(const AnnotationSet& annotations) {
  if (std::optional<std::string> condition =
          annotations.GetStringParam(ANNOTATION_IF)) {
    if (!BuildFlags::GetFlag(*condition, ANNOTATION_IF)) return false;
  }
  if (std::optional<std::string> condition =
          annotations.GetStringParam(ANNOTATION_IFNOT)) {
    if (BuildFlags::GetFlag(*condition, ANNOTATION_IFNOT)) return false;
  }
  return true;
}

bool ProcessIfAnnotation(ParseResultIterator* child_results) {
  AnnotationSet annotations(child_results, {},
                            {ANNOTATION_IF, ANNOTATION_IFNOT});
  return ProcessIfAnnotation(annotations);
}

std::optional<ParseResult> YieldInt32(ParseResultIterator* child_results) {
  std::string value = child_results->matched_input().ToString();
  size_t num_chars_converted = 0;
  int result = 0;
  try {
    result = std::stoi(value, &num_chars_converted, 0);
  } catch (const std::invalid_argument&) {
    Error("Expected an integer");
    return ParseResult{result};
  } catch (const std::out_of_range&) {
    Error("Integer out of 32-bit range");
    return ParseResult{result};
  }
  // Tokenizer shouldn't have included extra trailing characters.
  DCHECK_EQ(num_chars_converted, value.size());
  return ParseResult{result};
}

std::optional<ParseResult> YieldDouble(ParseResultIterator* child_results) {
  std::string value = child_results->matched_input().ToString();
  size_t num_chars_converted = 0;
  double result = 0;
  try {
    result = std::stod(value, &num_chars_converted);
  } catch (const std::out_of_range&) {
    Error("double literal out-of-range");
    return ParseResult{result};
  }
  // Tokenizer shouldn't have included extra trailing characters.
  DCHECK_EQ(num_chars_converted, value.size());
  return ParseResult{result};
}

std::optional<ParseResult> YieldIntegerLiteral(
    ParseResultIterator* child_results) {
  std::string value = child_results->matched_input().ToString();
  // Consume a leading minus.
  bool negative = false;
  if (!value.empty() && value[0] == '-') {
    negative = true;
    value = value.substr(1);
  }
  uint64_t absolute_value;
  try {
    size_t parsed = 0;
    absolute_value = std::stoull(value, &parsed, 0);
    DCHECK_EQ(parsed, value.size());
  } catch (const std::invalid_argument&) {
    Error("integer literal could not be parsed").Throw();
  } catch (const std::out_of_range&) {
    Error("integer literal value out of range").Throw();
  }
  return ParseResult(IntegerLiteral(negative, absolute_value));
}

std::optional<ParseResult> MakeStringAnnotationParameter(
    ParseResultIterator* child_results) {
  std::string value = child_results->NextAs<std::string>();
  AnnotationParameter result{value, 0, false};
  return ParseResult{result};
}

std::optional<ParseResult> MakeIntAnnotationParameter(
    ParseResultIterator* child_results) {
  int32_t value = child_results->NextAs<int32_t>();
  AnnotationParameter result{"", value, true};
  return ParseResult{result};
}

int GetAnnotationValue(const AnnotationSet& annotations, const char* name,
                       int default_value) {
  auto opt_value = annotations.GetIntParam(name);
  return opt_value.has_value() ? *opt_value : default_value;
}

std::optional<ParseResult> MakeTorqueBuiltinDeclaration(
    ParseResultIterator* child_results) {
  AnnotationSet annotations(child_results,
                            {ANNOTATION_CUSTOM_INTERFACE_DESCRIPTOR},
                            {ANNOTATION_IF, ANNOTATION_INCREMENT_USE_COUNTER});
  const bool has_custom_interface_descriptor =
      annotations.Contains(ANNOTATION_CUSTOM_INTERFACE_DESCRIPTOR);
  std::optional<std::string> use_counter_name =
      annotations.GetStringParam(ANNOTATION_INCREMENT_USE_COUNTER);
  auto transitioning = child_results->NextAs<bool>();
  auto javascript_linkage = child_results->NextAs<bool>();
  auto name = child_results->NextAs<Identifier*>();
  if (!IsUpperCamelCase(name->value)) {
    NamingConventionError("Builtin", name, "UpperCamelCase");
  }

  auto generic_parameters = child_results->NextAs<GenericParameters>();
  LintGenericParameters(generic_parameters);

  auto args = child_results->NextAs<ParameterList>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  auto body = child_results->NextAs<std::optional<Statement*>>();
  CallableDeclaration* declaration = MakeNode<TorqueBuiltinDeclaration>(
      transitioning, javascript_linkage, name, args, return_type,
      has_custom_interface_descriptor, use_counter_name, body);
  Declaration* result = declaration;
  if (generic_parameters.empty()) {
    if (!body) ReportError("A non-generic declaration needs a body.");
  } else {
    result = MakeNode<GenericCallableDeclaration>(std::move(generic_parameters),
                                                  declaration);
  }
  if (use_counter_name && !body) {
    ReportError("@incrementUseCounter needs a body.");
  }
  std::vector<Declaration*> results;
  if (std::optional<std::string> condition =
          annotations.GetStringParam(ANNOTATION_IF)) {
    if (!BuildFlags::GetFlag(*condition, ANNOTATION_IF)) {
      return ParseResult{std::move(results)};
    }
  }
  results.push_back(result);
  return ParseResult{std::move(results)};
}

InstanceTypeConstraints MakeInstanceTypeConstraints(
    const AnnotationSet& annotations) {
  InstanceTypeConstraints result;
  result.value =
      GetAnnotationValue(annotations, ANNOTATION_INSTANCE_TYPE_VALUE, -1);
  result.num_flags_bits = GetAnnotationValue(
      annotations, ANNOTATION_RESERVE_BITS_IN_INSTANCE_TYPE, -1);
  return result;
}

std::optional<ParseResult> MakeClassBody(ParseResultIterator* child_results) {
  auto methods = child_results->NextAs<std::vector<Declaration*>>();
  auto fields = child_results->NextAs<std::vector<ClassFieldExpression>>();
  std::optional<ClassBody*> result =
      MakeNode<ClassBody>(std::move(methods), std::move(fields));
  return ParseResult(result);
}

std::optional<ParseResult> MakeClassDeclaration(
    ParseResultIterator* child_results) {
  AnnotationSet annotations(
      child_results,
      {ANNOTATION_ABSTRACT, ANNOTATION_HAS_SAME_INSTANCE_TYPE_AS_PARENT,
       ANNOTATION_DO_NOT_GENERATE_CPP_CLASS, ANNOTATION_CUSTOM_CPP_CLASS,
       ANNOTATION_CUSTOM_MAP, ANNOTATION_GENERATE_BODY_DESCRIPTOR,
       ANNOTATION_EXPORT, ANNOTATION_DO_NOT_GENERATE_CAST,
       ANNOTATION_GENERATE_UNIQUE_MAP, ANNOTATION_GENERATE_FACTORY_FUNCTION,
       ANNOTATION_HIGHEST_INSTANCE_TYPE_WITHIN_PARENT,
       ANNOTATION_LOWEST_INSTANCE_TYPE_WITHIN_PARENT,
       ANNOTATION_CPP_OBJECT_DEFINITION,
       ANNOTATION_CPP_OBJECT_LAYOUT_DEFINITION},
      {ANNOTATION_RESERVE_BITS_IN_INSTANCE_TYPE,
       ANNOTATION_INSTANCE_TYPE_VALUE});
  ClassFlags flags = ClassFlag::kNone;
  if (annotations.Contains(ANNOTATION_ABSTRACT)) {
    flags |= ClassFlag::kAbstract;
  }
  if (annotations.Contains(ANNOTATION_HAS_SAME_INSTANCE_TYPE_AS_PARENT)) {
    flags |= ClassFlag::kHasSameInstanceTypeAsParent;
  }
  bool do_not_generate_cpp_class =
      annotations.Contains(ANNOTATION_DO_NOT_GENERATE_CPP_CLASS);
  if (annotations.Contains(ANNOTATION_CUSTOM_CPP_CLASS)) {
    Error(
        "@customCppClass is deprecated. Use 'extern' instead. "
        "@generateBodyDescriptor, @generateUniqueMap, and "
        "@generateFactoryFunction accomplish most of what '@export "
        "@customCppClass' used to.");
  }
  if (annotations.Contains(ANNOTATION_CUSTOM_MAP)) {
    Error(
        "@customMap is deprecated. Generating a unique map is opt-in now using "
        "@generateUniqueMap.");
  }
  if (annotations.Contains(ANNOTATION_DO_NOT_GENERATE_CAST)) {
    flags |= ClassFlag::kDoNotGenerateCast;
  }
  if (annotations.Contains(ANNOTATION_GENERATE_BODY_DESCRIPTOR)) {
    flags |= ClassFlag::kGenerateBodyDescriptor;
  }
  if (annotations.Contains(ANNOTATION_GENERATE_UNIQUE_MAP)) {
    flags |= ClassFlag::kGenerateUniqueMap;
  }
  if (annotations.Contains(ANNOTATION_GENERATE_FACTORY_FUNCTION)) {
    flags |= ClassFlag::kGenerateFactoryFunction;
  }
  if (annotations.Contains(ANNOTATION_EXPORT)) {
    flags |= ClassFlag::kExport;
  }
  if (annotations.Contains(ANNOTATION_HIGHEST_INSTANCE_TYPE_WITHIN_PARENT)) {
    flags |= ClassFlag::kHighestInstanceTypeWithinParent;
  }
  if (annotations.Contains(ANNOTATION_LOWEST_INSTANCE_TYPE_WITHIN_PARENT)) {
    flags |= ClassFlag::kLowestInstanceTypeWithinParent;
  }
  if (annotations.Contains(ANNOTATION_CPP_OBJECT_DEFINITION)) {
    flags |= ClassFlag::kCppObjectDefinition;
  }
  if (annotations.Contains(ANNOTATION_CPP_OBJECT_LAYOUT_DEFINITION)) {
    flags |= ClassFlag::kCppObjectLayoutDefinition;
  }

  auto is_extern = child_results->NextAs<bool>();
  if (is_extern) flags |= ClassFlag::kExtern;
  auto transient = child_results->NextAs<bool>();
  if (transient) flags |= ClassFlag::kTransient;
  std::string kind = child_results->NextAs<Identifier*>()->value;
  if (kind == "shape") {
    flags |= ClassFlag::kIsShape;
    flags |= ClassFlag::kTransient;
    flags |= ClassFlag::kHasSameInstanceTypeAsParent;
    flags |= ClassFlag::kDoNotGenerateCast;
  } else {
    DCHECK_EQ(kind, "class");
  }
  auto name = child_results->NextAs<Identifier*>();
  if (!IsValidTypeName(name->value)) {
    NamingConventionError("Type", name, "UpperCamelCase");
  }
  auto extends = child_results->NextAs<TypeExpression*>();
  if (!BasicTypeExpression::DynamicCast(extends)) {
    ReportError("Expected type name in extends clause.");
  }
  auto generates = child_results->NextAs<std::optional<std::string>>();
  auto body = child_results->NextAs<std::optional<ClassBody*>>();
  std::vector<Declaration*> methods;
  std::vector<ClassFieldExpression> fields_raw;
  if (body.has_value()) {
    methods = (*body)->methods;
    fields_raw = (*body)->fields;
  } else {
    flags |= ClassFlag::kUndefinedLayout;
  }

  if (is_extern && body.has_value()) {
    if (!do_not_generate_cpp_class) {
      flags |= ClassFlag::kGenerateCppClassDefinitions;
    }
  } else if (do_not_generate_cpp_class) {
    Lint("Annotation @doNotGenerateCppClass has no effect");
  }

  // Filter to only include fields that should be present based on decoration.
  std::vector<ClassFieldExpression> fields;
  std::copy_if(
      fields_raw.begin(), fields_raw.end(), std::back_inserter(fields),
      [](const ClassFieldExpression& exp) {
        for (const ConditionalAnnotation& condition : exp.conditions) {
          if (condition.type == ConditionalAnnotationType::kPositive
                  ? !BuildFlags::GetFlag(condition.condition, ANNOTATION_IF)
                  : BuildFlags::GetFlag(condition.condition,
                                        ANNOTATION_IFNOT)) {
            return false;
          }
        }
        return true;
      });

  std::vector<Declaration*> result;

  result.push_back(MakeNode<ClassDeclaration>(
      name, flags, extends, generates, std::move(methods), std::move(fields),
      MakeInstanceTypeConstraints(annotations)));

  Identifier* constexpr_name =
      MakeNode<Identifier>(CONSTEXPR_TYPE_PREFIX + name->value);
  constexpr_name->pos = name->pos;
  TypeExpression* constexpr_extends = AddConstexpr(extends);
  AbstractTypeFlags abstract_type_flags(AbstractTypeFlag::kConstexpr);
  if (transient) abstract_type_flags |= AbstractTypeFlag::kTransient;
  TypeDeclaration* constexpr_decl = MakeNode<AbstractTypeDeclaration>(
      constexpr_name, abstract_type_flags, constexpr_extends,
      generates ? UnwrapTNodeTypeName(*generates) : name->value);
  constexpr_decl->pos = name->pos;
  result.push_back(constexpr_decl);

  if ((flags & ClassFlag::kDoNotGenerateCast) == 0 &&
      (flags & ClassFlag::kIsShape) == 0) {
    ParameterList parameters;
    parameters.names.push_back(MakeNode<Identifier>("obj"));
    parameters.types.push_back(MakeNode<BasicTypeExpression>(
        std::vector<std::string>{}, MakeNode<Identifier>("HeapObject"),
        std::vector<TypeExpression*>{}));
    LabelAndTypesVector labels;
    labels.push_back(LabelAndTypes{MakeNode<Identifier>("CastError"),
                                   std::vector<TypeExpression*>{}});

    TypeExpression* class_type = MakeNode<BasicTypeExpression>(
        std::vector<std::string>{}, name, std::vector<TypeExpression*>{});

    std::vector<std::string> namespace_qualification{
        TORQUE_INTERNAL_NAMESPACE_STRING};

    IdentifierExpression* internal_downcast_target =
        MakeNode<IdentifierExpression>(
            std::move(namespace_qualification),
            MakeNode<Identifier>("DownCastForTorqueClass"),
            std::vector<TypeExpression*>{class_type});
    IdentifierExpression* internal_downcast_otherwise =
        MakeNode<IdentifierExpression>(std::vector<std::string>{},
                                       MakeNode<Identifier>("CastError"));

    Expression* argument = MakeNode<IdentifierExpression>(
        std::vector<std::string>{}, MakeNode<Identifier>("obj"));

    auto value = MakeCall(internal_downcast_target, std::nullopt,
                          std::vector<Expression*>{argument},
                          std::vector<Statement*>{MakeNode<ExpressionStatement>(
                              internal_downcast_otherwise)});

    auto cast_body = MakeNode<ReturnStatement>(value);

    std::vector<TypeExpression*> generic_parameters;
    generic_parameters.push_back(MakeNode<BasicTypeExpression>(
        std::vector<std::string>{}, name, std::vector<TypeExpression*>{}));

    Declaration* specialization = MakeNode<SpecializationDeclaration>(
        false, MakeNode<Identifier>("Cast"), std::move(generic_parameters),
        std::move(parameters), class_type, std::move(labels), cast_body);
    result.push_back(specialization);
  }

  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeNamespaceDeclaration(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<std::string>();
  if (!IsSnakeCase(name)) {
    NamingConventionError("Namespace", name, "snake_case");
  }
  auto declarations = child_results->NextAs<std::vector<Declaration*>>();
  Declaration* result =
      MakeNode<NamespaceDeclaration>(std::move(name), std::move(declarations));
  return ParseResult{result};
}

std::optional<ParseResult> MakeSpecializationDeclaration(
    ParseResultIterator* child_results) {
  bool enabled = ProcessIfAnnotation(child_results);
  auto transitioning = child_results->NextAs<bool>();
  auto name = child_results->NextAs<Identifier*>();
  auto generic_parameters =
      child_results->NextAs<std::vector<TypeExpression*>>();
  auto parameters = child_results->NextAs<ParameterList>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  auto labels = child_results->NextAs<LabelAndTypesVector>();
  auto body = child_results->NextAs<Statement*>();
  CheckNotDeferredStatement(body);
  std::vector<Declaration*> result = {};
  if (enabled) {
    result = {MakeNode<SpecializationDeclaration>(
        transitioning, std::move(name), std::move(generic_parameters),
        std::move(parameters), return_type, std::move(labels), body)};
  }
  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeStructDeclaration(
    ParseResultIterator* child_results) {
  bool is_export = HasExportAnnotation(child_results, "Struct");

  StructFlags flags = StructFlag::kNone;
  if (is_export) flags |= StructFlag::kExport;

  auto name = child_results->NextAs<Identifier*>();
  if (!IsValidTypeName(name->value)) {
    NamingConventionError("Struct", name, "UpperCamelCase");
  }
  auto generic_parameters = child_results->NextAs<GenericParameters>();
  LintGenericParameters(generic_parameters);
  auto methods = child_results->NextAs<std::vector<Declaration*>>();
  auto fields = child_results->NextAs<std::vector<StructFieldExpression>>();
  TypeDeclaration* struct_decl = MakeNode<StructDeclaration>(
      flags, name, std::move(methods), std::move(fields));
  Declaration* result = struct_decl;
  if (!generic_parameters.empty()) {
    result = MakeNode<GenericTypeDeclaration>(std::move(generic_parameters),
                                              struct_decl);
  }
  return ParseResult{result};
}

std::optional<ParseResult> MakeBitFieldStructDeclaration(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<Identifier*>();
  if (!IsValidTypeName(name->value)) {
    NamingConventionError("Bitfield struct", name, "UpperCamelCase");
  }
  auto extends = child_results->NextAs<TypeExpression*>();
  auto fields = child_results->NextAs<std::vector<BitFieldDeclaration>>();
  Declaration* decl =
      MakeNode<BitFieldStructDeclaration>(name, extends, std::move(fields));
  return ParseResult{decl};
}

std::optional<ParseResult> MakeCppIncludeDeclaration(
    ParseResultIterator* child_results) {
  auto include_path = child_results->NextAs<std::string>();
  Declaration* result =
      MakeNode<CppIncludeDeclaration>(std::move(include_path));
  return ParseResult{result};
}

std::optional<ParseResult> ProcessTorqueImportDeclaration(
    ParseResultIterator* child_results) {
  auto import_path = child_results->NextAs<std::string>();
  if (!SourceFileMap::FileRelativeToV8RootExists(import_path)) {
    Error("File '", import_path, "' not found.");
  }

  auto import_id = SourceFileMap::GetSourceId(import_path);
  if (!import_id.IsValid()) {
    // TODO(szuend): Instead of reporting and error. Queue the file up
    //               for compilation.
    Error("File '", import_path, "'is not part of the source set.").Throw();
  }

  CurrentAst::Get().DeclareImportForCurrentFile(import_id);

  return std::nullopt;
}

std::optional<ParseResult> MakeExternalBuiltin(
    ParseResultIterator* child_results) {
  auto transitioning = child_results->NextAs<bool>();
  auto js_linkage = child_results->NextAs<bool>();
  auto name = child_results->NextAs<Identifier*>();
  auto generic_parameters = child_results->NextAs<GenericParameters>();
  LintGenericParameters(generic_parameters);

  auto args = child_results->NextAs<ParameterList>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  Declaration* result = MakeNode<ExternalBuiltinDeclaration>(
      transitioning, js_linkage, name, args, return_type);
  if (!generic_parameters.empty()) {
    Error("External builtins cannot be generic.");
  }
  return ParseResult{result};
}

std::optional<ParseResult> MakeExternalRuntime(
    ParseResultIterator* child_results) {
  auto transitioning = child_results->NextAs<bool>();
  auto name = child_results->NextAs<Identifier*>();
  auto args = child_results->NextAs<ParameterList>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  Declaration* result = MakeNode<ExternalRuntimeDeclaration>(
      transitioning, name, args, return_type);
  return ParseResult{result};
}

std::optional<ParseResult> StringLiteralUnquoteAction(
    ParseResultIterator* child_results) {
  return ParseResult{
      StringLiteralUnquote(child_results->NextAs<std::string>())};
}

std::optional<ParseResult> MakeBasicTypeExpression(
    ParseResultIterator* child_results) {
  auto namespace_qualification =
      child_results->NextAs<std::vector<std::string>>();
  auto is_constexpr = child_results->NextAs<bool>();
  auto name = child_results->NextAs<std::string>();
  auto generic_arguments =
      child_results->NextAs<std::vector<TypeExpression*>>();
  TypeExpression* result = MakeNode<BasicTypeExpression>(
      std::move(namespace_qualification),
      MakeNode<Identifier>(is_constexpr ? GetConstexprName(name)
                                        : std::move(name)),
      std::move(generic_arguments));
  return ParseResult{result};
}

std::optional<ParseResult> MakeFunctionTypeExpression(
    ParseResultIterator* child_results) {
  auto parameters = child_results->NextAs<std::vector<TypeExpression*>>();
  auto return_type = child_results->NextAs<TypeExpression*>();
  TypeExpression* result =
      MakeNode<FunctionTypeExpression>(std::move(parameters), return_type);
  return ParseResult{result};
}

std::optional<ParseResult> MakeReferenceTypeExpression(
    ParseResultIterator* child_results) {
  auto is_const = child_results->NextAs<bool>();
  auto referenced_type = child_results->NextAs<TypeExpression*>();
  std::vector<std::string> namespace_qualification{
      TORQUE_INTERNAL_NAMESPACE_STRING};
  std::vector<TypeExpression*> generic_arguments{referenced_type};
  TypeExpression* result = MakeNode<BasicTypeExpression>(
      std::move(namespace_qualification),
      MakeNode<Identifier>(is_const ? CONST_REFERENCE_TYPE_STRING
                                    : MUTABLE_REFERENCE_TYPE_STRING),
      std::move(generic_arguments));
  return ParseResult{result};
}

std::optional<ParseResult> MakeUnionTypeExpression(
    ParseResultIterator* child_results) {
  auto a = child_results->NextAs<TypeExpression*>();
  auto b = child_results->NextAs<TypeExpression*>();
  TypeExpression* result = MakeNode<UnionTypeExpression>(a, b);
  return ParseResult{result};
}

std::optional<ParseResult> MakeGenericParameter(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<Identifier*>();
  auto constraint = child_results->NextAs<std::optional<TypeExpression*>>();
  return ParseResult{GenericParameter{name, constraint}};
}

std::optional<ParseResult> MakeExpressionStatement(
    ParseResultIterator* child_results) {
  auto expression = child_results->NextAs<Expression*>();
  Statement* result = MakeNode<ExpressionStatement>(expression);
  return ParseResult{result};
}

std::optional<ParseResult> MakeIfStatement(ParseResultIterator* child_results) {
  auto is_constexpr = child_results->NextAs<bool>();
  auto condition = child_results->NextAs<Expression*>();
  auto if_true = child_results->NextAs<Statement*>();
  auto if_false = child_results->NextAs<std::optional<Statement*>>();

  if (if_false && !(BlockStatement::DynamicCast(if_true) &&
                    (BlockStatement::DynamicCast(*if_false) ||
                     IfStatement::DynamicCast(*if_false)))) {
    ReportError("if-else statements require curly braces");
  }

  if (is_constexpr) {
    CheckNotDeferredStatement(if_true);
    if (if_false) CheckNotDeferredStatement(*if_false);
  }

  Statement* result =
      MakeNode<IfStatement>(is_constexpr, condition, if_true, if_false);
  return ParseResult{result};
}

std::optional<ParseResult> MakeEnumDeclaration(
    ParseResultIterator* child_results) {
  const bool is_extern = child_results->NextAs<bool>();
  auto name_identifier = child_results->NextAs<Identifier*>();
  auto name = name_identifier->value;
  auto base_type_expression =
      child_results->NextAs<std::optional<TypeExpression*>>();
  auto constexpr_generates_opt =
      child_results->NextAs<std::optional<std::string>>();
  auto entries = child_results->NextAs<std::vector<EnumEntry>>();
  const bool is_open = child_results->NextAs<bool>();
  CurrentSourcePosition::Scope current_source_position(
      child_results->matched_input().pos);

  if (!is_extern) {
    ReportError("non-extern enums are not supported yet");
  }

  if (!IsValidTypeName(name)) {
    NamingConventionError("Type", name, "UpperCamelCase");
  }

  if (constexpr_generates_opt && *constexpr_generates_opt == name) {
    Lint("Unnecessary 'constexpr' clause for enum ", name);
  }
  auto constexpr_generates =
      constexpr_generates_opt ? *constexpr_generates_opt : name;
  const bool generate_nonconstexpr = base_type_expression.has_value();

  std::vector<Declaration*> result;
  // Build non-constexpr types.
  if (generate_nonconstexpr) {
    DCHECK(base_type_expression.has_value());

    if (is_open) {
      // For open enumerations, we define an abstract type and inherit all
      // entries' types from that:
      //   type Enum extends Base;
      //   namespace Enum {
      //     type kEntry0 extends Enum;
      //     ...
      //     type kEntryN extends Enum;
      //   }
      auto type_decl = MakeNode<AbstractTypeDeclaration>(
          name_identifier, AbstractTypeFlag::kNone, base_type_expression,
          std::nullopt);

      TypeExpression* name_type_expression =
          MakeNode<BasicTypeExpression>(name_identifier);
      name_type_expression->pos = name_identifier->pos;

      std::vector<Declaration*> entry_decls;
      entry_decls.reserve(entries.size());
      for (const auto& entry : entries) {
        entry_decls.push_back(MakeNode<AbstractTypeDeclaration>(
            entry.name, AbstractTypeFlag::kNone,
            entry.type.value_or(name_type_expression), std::nullopt));
      }

      result.push_back(type_decl);
      result.push_back(
          MakeNode<NamespaceDeclaration>(name, std::move(entry_decls)));
    } else {
      // For closed enumerations, we define abstract types for all entries and
      // define the enumeration as a union of those:
      //   namespace Enum {
      //     type kEntry0 extends Base;
      //     ...
      //     type kEntryN extends Base;
      //   }
      //   type Enum = Enum::kEntry0 | ... | Enum::kEntryN;
      TypeExpression* union_type = nullptr;
      std::vector<Declaration*> entry_decls;
      for (const auto& entry : entries) {
        entry_decls.push_back(MakeNode<AbstractTypeDeclaration>(
            entry.name, AbstractTypeFlag::kNone,
            entry.type.value_or(*base_type_expression), std::nullopt));

        auto entry_type = MakeNode<BasicTypeExpression>(
            std::vector<std::string>{name}, entry.name,
            std::vector<TypeExpression*>{});
        if (union_type) {
          union_type = MakeNode<UnionTypeExpression>(union_type, entry_type);
        } else {
          union_type = entry_type;
        }
      }

      result.push_back(
          MakeNode<NamespaceDeclaration>(name, std::move(entry_decls)));
      result.push_back(
          MakeNode<TypeAliasDeclaration>(name_identifier, union_type));
    }
  }

  // Build constexpr types.
  {
    // The constexpr entries inherit from an abstract enumeration type:
    //   type constexpr Enum extends constexpr Base;
    //   namespace Enum {
    //     type constexpr kEntry0 extends constexpr Enum;
    //     ...
    //     type constexpr kEntry1 extends constexpr Enum;
    //   }
    Identifier* constexpr_type_identifier =
        MakeNode<Identifier>(std::string(CONSTEXPR_TYPE_PREFIX) + name);
    TypeExpression* constexpr_type_expression = MakeNode<BasicTypeExpression>(
        MakeNode<Identifier>(std::string(CONSTEXPR_TYPE_PREFIX) + name));
    std::optional<TypeExpression*> base_constexpr_type_expression =
        std::nullopt;
    if (base_type_expression) {
      base_constexpr_type_expression = AddConstexpr(*base_type_expression);
    }
    result.push_back(MakeNode<AbstractTypeDeclaration>(
        constexpr_type_identifier, AbstractTypeFlag::kConstexpr,
        base_constexpr_type_expression, constexpr_generates));

    TypeExpression* type_expr = nullptr;
    Identifier* fromconstexpr_identifier = nullptr;
    Identifier* fromconstexpr_parameter_identifier = nullptr;
    Statement* fromconstexpr_body = nullptr;
    if (generate_nonconstexpr) {
      DCHECK(base_type_expression.has_value());
      type_expr = MakeNode<BasicTypeExpression>(std::vector<std::string>{},
                                                MakeNode<Identifier>(name),
                                                std::vector<TypeExpression*>{});

      // return %RawDownCast<Enum>(%FromConstexpr<Base>(o)))
      fromconstexpr_identifier = MakeNode<Identifier>("FromConstexpr");
      fromconstexpr_parameter_identifier = MakeNode<Identifier>("o");
      fromconstexpr_body =
          MakeNode<ReturnStatement>(MakeNode<IntrinsicCallExpression>(
              MakeNode<Identifier>("%RawDownCast"),
              std::vector<TypeExpression*>{type_expr},
              std::vector<Expression*>{MakeNode<IntrinsicCallExpression>(
                  MakeNode<Identifier>("%FromConstexpr"),
                  std::vector<TypeExpression*>{*base_type_expression},
                  std::vector<Expression*>{MakeNode<IdentifierExpression>(
                      std::vector<std::string>{},
                      fromconstexpr_parameter_identifier)})}));
    }

    EnumDescription enum_description{CurrentSourcePosition::Get(), name,
                                     constexpr_generates, is_open};
    std::vector<Declaration*> entry_decls;
    for (const auto& entry : entries) {
      const std::string entry_name = entry.name->value;
      const std::string entry_constexpr_type =
          CONSTEXPR_TYPE_PREFIX + entry_name;
      std::string alias_entry;
      if (entry.alias_entry) {
        alias_entry = constexpr_generates + "::" + *entry.alias_entry;
      }
      enum_description.entries.emplace_back(
          constexpr_generates + "::" + entry_name, alias_entry);

      entry_decls.push_back(MakeNode<AbstractTypeDeclaration>(
          MakeNode<Identifier>(entry_constexpr_type),
          AbstractTypeFlag::kConstexpr, constexpr_type_expression,
          constexpr_generates));

      bool generate_typed_constant = entry.type.has_value();
      if (generate_typed_constant) {
        // namespace Enum {
        //   const constexpr_constant_kEntry0: constexpr kEntry0 constexpr
        //   'Enum::kEntry0'; const kEntry0 = %RawDownCast<T,
        //   Base>(FromConstexpr<Enum>(constexpr_constant_kEntry0));
        // }
        if (!generate_nonconstexpr) {
          Error(
              "Enum constants with custom types require an enum with an "
              "extends clause.")
              .Position((*entry.type)->pos);
        }
        Identifier* constexpr_constant_name =
            MakeNode<Identifier>("constexpr constant " + entry_name);
        entry_decls.push_back(MakeNode<ExternConstDeclaration>(
            constexpr_constant_name,
            MakeNode<BasicTypeExpression>(
                std::vector<std::string>{},
                MakeNode<Identifier>(entry_constexpr_type),
                std::vector<TypeExpression*>{}),
            constexpr_generates + "::" + entry_name));
        entry_decls.push_back(MakeNode<ConstDeclaration>(
            entry.name, *entry.type,
            MakeNode<IntrinsicCallExpression>(
                MakeNode<Identifier>("%RawDownCast"),
                std::vector<TypeExpression*>{*entry.type,
                                             *base_type_expression},
                std::vector<Expression*>{MakeCall(
                    MakeNode<Identifier>("FromConstexpr"), {type_expr},
                    {MakeNode<IdentifierExpression>(std::vector<std::string>{},
                                                    constexpr_constant_name)},
                    {})})));
      } else {
        // namespace Enum {
        //   const kEntry0: constexpr kEntry0 constexpr 'Enum::kEntry0';
        // }
        entry_decls.push_back(MakeNode<ExternConstDeclaration>(
            entry.name,
            MakeNode<BasicTypeExpression>(
                std::vector<std::string>{},
                MakeNode<Identifier>(entry_constexpr_type),
                std::vector<TypeExpression*>{}),
            constexpr_generates + "::" + entry_name));
      }

      // FromConstexpr<Enum, Enum::constexpr kEntry0>(
      //   : Enum::constexpr kEntry0): Enum
      if (generate_nonconstexpr) {
        TypeExpression* entry_constexpr_type_expr =
            MakeNode<BasicTypeExpression>(
                std::vector<std::string>{name},
                MakeNode<Identifier>(entry_constexpr_type),
                std::vector<TypeExpression*>{});

        ParameterList parameters;
        parameters.names.push_back(fromconstexpr_parameter_identifier);
        parameters.types.push_back(entry_constexpr_type_expr);
        result.push_back(MakeNode<SpecializationDeclaration>(
            false, fromconstexpr_identifier,
            std::vector<TypeExpression*>{type_expr, entry_constexpr_type_expr},
            std::move(parameters), type_expr, LabelAndTypesVector{},
            fromconstexpr_body));
      }
    }

    result.push_back(
        MakeNode<NamespaceDeclaration>(name, std::move(entry_decls)));
    CurrentAst::Get().AddEnumDescription(std::move(enum_description));
  }

  return ParseResult{std::move(result)};
}

std::optional<ParseResult> MakeTypeswitchStatement(
    ParseResultIterator* child_results) {
  auto expression = child_results->NextAs<Expression*>();
  auto cases = child_results->NextAs<std::vector<TypeswitchCase>>();
  CurrentSourcePosition::Scope matched_input_current_source_position(
      child_results->matched_input().pos);

  // typeswitch (expression) case (x1 : T1) {
  //   ...b1
  // } case (x2 : T2) {
  //   ...b2
  // } case (x3 : T3) {
  //   ...b3
  // }
  //
  // desugars to
  //
  // {
  //   const _value = expression;
  //   try {
  //     const x1 : T1 = cast<T1>(_value) otherwise _NextCase;
  //     ...b1
  //   } label _NextCase {
  //     try {
  //       const x2 : T2 = cast<T2>(%assume_impossible<T1>(_value));
  //       ...b2
  //     } label _NextCase {
  //       const x3 : T3 = %assume_impossible<T1|T2>(_value);
  //       ...b3
  //     }
  //   }
  // }

  BlockStatement* current_block = MakeNode<BlockStatement>();
  Statement* result = current_block;
  {
    CurrentSourcePosition::Scope current_source_position(expression->pos);
    current_block->statements.push_back(MakeNode<VarDeclarationStatement>(
        true, MakeNode<Identifier>("__value"), std::nullopt, expression));
  }

  TypeExpression* accumulated_types;
  for (size_t i = 0; i < cases.size(); ++i) {
    CurrentSourcePosition::Scope current_source_position(cases[i].pos);
    Expression* value =
        MakeNode<IdentifierExpression>(MakeNode<Identifier>("__value"));
    if (i >= 1) {
      value =
          MakeNode<AssumeTypeImpossibleExpression>(accumulated_types, value);
    }
    BlockStatement* case_block;
    if (i < cases.size() - 1) {
      value = MakeCall(MakeNode<Identifier>("Cast"),
                       std::vector<TypeExpression*>{cases[i].type},
                       std::vector<Expression*>{value},
                       std::vector<Statement*>{MakeNode<ExpressionStatement>(
                           MakeNode<IdentifierExpression>(
                               MakeNode<Identifier>(kNextCaseLabelName)))});
      case_block = MakeNode<BlockStatement>();
    } else {
      case_block = current_block;
    }
    Identifier* name =
        cases[i].name ? *cases[i].name : MakeNode<Identifier>("__case_value");
    if (cases[i].name) name = *cases[i].name;
    case_block->statements.push_back(
        MakeNode<VarDeclarationStatement>(true, name, cases[i].type, value));
    case_block->statements.push_back(cases[i].block);
    if (i < cases.size() - 1) {
      BlockStatement* next_block = MakeNode<BlockStatement>();
      current_block->statements.push_back(
          MakeNode<ExpressionStatement>(MakeNode<TryLabelExpression>(
              MakeNode<StatementExpression>(case_block),
              MakeNode<TryHandler>(TryHandler::HandlerKind::kLabel,
                                   MakeNode<Identifier>(kNextCaseLabelName),
                                   ParameterList::Empty(), next_block))));
      current_block = next_block;
    }
    accumulated_types =
        i > 0 ? MakeNode<UnionTypeExpression>(accumulated_types, cases[i].type)
              : cases[i].type;
  }
  return ParseResult{result};
}

std::optional<ParseResult> MakeTypeswitchCase(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<std::optional<Identifier*>>();
  auto type = child_results->NextAs<TypeExpression*>();
  auto block = child_results->NextAs<Statement*>();
  return ParseResult{
      TypeswitchCase{child_results->matched_input().pos, name, type, block}};
}

std::optional<ParseResult> MakeWhileStatement(
    ParseResultIterator* child_results) {
  auto condition = child_results->NextAs<Expression*>();
  auto body = child_results->NextAs<Statement*>();
  Statement* result = MakeNode<WhileStatement>(condition, body);
  CheckNotDeferredStatement(result);
  return ParseResult{result};
}

std::optional<ParseResult> MakeReturnStatement(
    ParseResultIterator* child_results) {
  auto value = child_results->NextAs<std::optional<Expression*>>();
  Statement* result = MakeNode<ReturnStatement>(value);
  return ParseResult{result};
}

std::optional<ParseResult> MakeTailCallStatement(
    ParseResultIterator* child_results) {
  auto value = child_results->NextAs<Expression*>();
  Statement* result = MakeNode<TailCallStatement>(CallExpression::cast(value));
  return ParseResult{result};
}

std::optional<ParseResult> MakeVarDeclarationStatement(
    ParseResultIterator* child_results) {
  auto kind = child_results->NextAs<Identifier*>();
  bool const_qualified = kind->value == "const";
  if (!const_qualified) DCHECK_EQ("let", kind->value);
  auto name = child_results->NextAs<Identifier*>();
  if (!IsLowerCamelCase(name->value)) {
    NamingConventionError("Variable", name, "lowerCamelCase");
  }

  auto type = child_results->NextAs<std::optional<TypeExpression*>>();
  std::optional<Expression*> initializer;
  if (child_results->HasNext())
    initializer = child_results->NextAs<Expression*>();
  if (!initializer && !type) {
    ReportError("Declaration is missing a type.");
  }
  Statement* result = MakeNode<VarDeclarationStatement>(const_qualified, name,
                                                        type, initializer);
  return ParseResult{result};
}

std::optional<ParseResult> MakeBreakStatement(
    ParseResultIterator* child_results) {
  Statement* result = MakeNode<BreakStatement>();
  return ParseResult{result};
}

std::optional<ParseResult> MakeContinueStatement(
    ParseResultIterator* child_results) {
  Statement* result = MakeNode<ContinueStatement>();
  return ParseResult{result};
}

std::optional<ParseResult> MakeGotoStatement(
    ParseResultIterator* child_results) {
  auto label = child_results->NextAs<Identifier*>();
  auto arguments = child_results->NextAs<std::vector<Expression*>>();
  Statement* result = MakeNode<GotoStatement>(label, std::move(arguments));
  return ParseResult{result};
}

std::optional<ParseResult> MakeBlockStatement(
    ParseResultIterator* child_results) {
  auto deferred = child_results->NextAs<bool>();
  auto statements = child_results->NextAs<std::vector<Statement*>>();
  for (Statement* statement : statements) {
    CheckNotDeferredStatement(statement);
  }
  Statement* result = MakeNode<BlockStatement>(deferred, std::move(statements));
  return ParseResult{result};
}

std::optional<ParseResult> MakeTryLabelExpression(
    ParseResultIterator* child_results) {
  auto try_block = child_results->NextAs<Statement*>();
  CheckNotDeferredStatement(try_block);
  Statement* result = try_block;
  auto handlers = child_results->NextAs<std::vector<TryHandler*>>();
  if (handlers.empty()) {
    Error("Try blocks without catch or label don't make sense.");
  }
  for (size_t i = 0; i < handlers.size(); ++i) {
    if (i != 0 &&
        handlers[i]->handler_kind == TryHandler::HandlerKind::kCatch) {
      Error(
          "A catch handler always has to be first, before any label handler, "
          "to avoid ambiguity about whether it catches exceptions from "
          "preceding handlers or not.")
          .Position(handlers[i]->pos);
    }
    result = MakeNode<ExpressionStatement>(MakeNode<TryLabelExpression>(
        MakeNode<StatementExpression>(result), handlers[i]));
  }
  return ParseResult{result};
}

std::optional<ParseResult> MakeForLoopStatement(
    ParseResultIterator* child_results) {
  auto var_decl = child_results->NextAs<std::optional<Statement*>>();
  auto test = child_results->NextAs<std::optional<Expression*>>();
  auto action = child_results->NextAs<std::optional<Expression*>>();
  std::optional<Statement*> action_stmt;
  if (action) action_stmt = MakeNode<ExpressionStatement>(*action);
  auto body = child_results->NextAs<Statement*>();
  CheckNotDeferredStatement(body);
  Statement* result =
      MakeNode<ForLoopStatement>(var_decl, test, action_stmt, body);
  return ParseResult{result};
}

std::optional<ParseResult> MakeLabelBlock(ParseResultIterator* child_results) {
  auto label = child_results->NextAs<Identifier*>();
  if (!IsUpperCamelCase(label->value)) {
    NamingConventionError("Label", label, "UpperCamelCase");
  }
  auto parameters = child_results->NextAs<ParameterList>();
  auto body = child_results->NextAs<Statement*>();
  TryHandler* result = MakeNode<TryHandler>(TryHandler::HandlerKind::kLabel,
                                            label, std::move(parameters), body);
  return ParseResult{result};
}

std::optional<ParseResult> MakeCatchBlock(ParseResultIterator* child_results) {
  auto parameter_names = child_results->NextAs<std::vector<std::string>>();
  auto body = child_results->NextAs<Statement*>();
  for (const std::string& variable : parameter_names) {
    if (!IsLowerCamelCase(variable)) {
      NamingConventionError("Exception", variable, "lowerCamelCase");
    }
  }
  if (parameter_names.size() != 2) {
    ReportError(
        "A catch clause needs to have exactly two parameters: The exception "
        "and the message. How about: \"catch (exception, message) { ...\".");
  }
  ParameterList parameters;

  parameters.names.push_back(MakeNode<Identifier>(parameter_names[0]));
  parameters.types.push_back(MakeNode<BasicTypeExpression>(
      std::vector<std::string>{}, MakeNode<Identifier>("JSAny"),
      std::vector<TypeExpression*>{}));
  parameters.names.push_back(MakeNode<Identifier>(parameter_names[1]));
  parameters.types.push_back(MakeNode<UnionTypeExpression>(
      MakeNode<BasicTypeExpression>(std::vector<std::string>{},
                                    MakeNode<Identifier>("JSMessageObject"),
                                    std::vector<TypeExpression*>{}),
      MakeNode<BasicTypeExpression>(std::vector<std::string>{},
                                    MakeNode<Identifier>("TheHole"),
                                    std::vector<TypeExpression*>{})));
  parameters.has_varargs = false;
  TryHandler* result = MakeNode<TryHandler>(
      TryHandler::HandlerKind::kCatch, MakeNode<Identifier>(kCatchLabelName),
      std::move(parameters), body);
  return ParseResult{result};
}

std::optional<ParseResult> MakeExpressionWithSource(
    ParseResultIterator* child_results) {
  auto e = child_results->NextAs<Expression*>();
  return ParseResult{
      ExpressionWithSource{e, child_results->matched_input().ToString()}};
}

std::optional<ParseResult> MakeIdentifier(ParseResultIterator* child_results) {
  auto name = child_results->NextAs<std::string>();
  Identifier* result = MakeNode<Identifier>(std::move(name));
  return ParseResult{result};
}

std::optional<ParseResult> MakeIdentifierFromMatchedInput(
    ParseResultIterator* child_results) {
  return ParseResult{
      MakeNode<Identifier>(child_results->matched_input().ToString())};
}

std::optional<ParseResult> MakeRightShiftIdentifier(
    ParseResultIterator* child_results) {
  std::string str = child_results->matched_input().ToString();
  for (auto character : str) {
    if (character != '>') {
      ReportError("right-shift operators may not contain any whitespace");
    }
  }
  return ParseResult{MakeNode<Identifier>(str)};
}

std::optional<ParseResult> MakeNamespaceQualification(
    ParseResultIterator* child_results) {
  bool global_namespace = child_results->NextAs<bool>();
  auto namespace_qualification =
      child_results->NextAs<std::vector<std::string>>();
  if (global_namespace) {
    namespace_qualification.insert(namespace_qualification.begin(), "");
  }
  return ParseResult(std::move(namespace_qualification));
}

std::optional<ParseResult> MakeIdentifierExpression(
    ParseResultIterator* child_results) {
  auto namespace_qualification =
      child_results->NextAs<std::vector<std::string>>();
  auto name = child_results->NextAs<Identifier*>();
  auto generic_arguments =
      child_results->NextAs<std::vector<TypeExpression*>>();
  Expression* result = MakeNode<IdentifierExpression>(
      std::move(namespace_qualification), name, std::move(generic_arguments));
  return ParseResult{result};
}

std::optional<ParseResult> MakeFieldAccessExpression(
    ParseResultIterator* child_results) {
  auto object = child_results->NextAs<Expression*>();
  auto field = child_results->NextAs<Identifier*>();
  Expression* result = MakeNode<FieldAccessExpression>(object, field);
  return ParseResult{result};
}

std::optional<ParseResult> MakeReferenceFieldAccessExpression(
    ParseResultIterator* child_results) {
  auto object = child_results->NextAs<Expression*>();
  auto field = child_results->NextAs<Identifier*>();
  // `a->b` is equivalent to `(*a).b`.
  Expression* deref = MakeNode<DereferenceExpression>(object);
  Expression* result = MakeNode<FieldAccessExpression>(deref, field);
  return ParseResult{result};
}

std::optional<ParseResult> MakeElementAccessExpression(
    ParseResultIterator* child_results) {
  auto object = child_results->NextAs<Expression*>();
  auto field = child_results->NextAs<Expression*>();
  Expression* result = MakeNode<ElementAccessExpression>(object, field);
  return ParseResult{result};
}

std::optional<ParseResult> MakeDereferenceExpression(
    ParseResultIterator* child_results) {
  auto reference = child_results->NextAs<Expression*>();
  Expression* result = MakeNode<DereferenceExpression>(reference);
  return ParseResult{result};
}

std::optional<ParseResult> MakeStructExpression(
    ParseResultIterator* child_results) {
  auto type = child_results->NextAs<TypeExpression*>();
  auto initializers = child_results->NextAs<std::vector<NameAndExpression>>();
  Expression* result =
      MakeNode<StructExpression>(type, std::move(initializers));
  return ParseResult{result};
}

std::optional<ParseResult> MakeAssignmentExpression(
    ParseResultIterator* child_results) {
  auto location = child_results->NextAs<Expression*>();
  auto op = child_results->NextAs<std::optional<std::string>>();
  auto value = child_results->NextAs<Expression*>();
  Expression* result =
      MakeNode<AssignmentExpression>(location, std::move(op), value);
  return ParseResult{result};
}

std::optional<ParseResult> MakeFloatingPointLiteralExpression(
    ParseResultIterator* child_results) {
  auto value = child_results->NextAs<double>();
  Expression* result = MakeNode<FloatingPointLiteralExpression>(value);
  return ParseResult{result};
}

std::optional<ParseResult> MakeIntegerLiteralExpression(
    ParseResultIterator* child_results) {
  auto value = child_results->NextAs<IntegerLiteral>();
  Expression* result = MakeNode<IntegerLiteralExpression>(std::move(value));
  return ParseResult{result};
}

std::optional<ParseResult> MakeStringLiteralExpression(
    ParseResultIterator* child_results) {
  auto literal = child_results->NextAs<std::string>();
  Expression* result = MakeNode<StringLiteralExpression>(std::move(literal));
  return ParseResult{result};
}

std::optional<ParseResult> MakeIncrementDecrementExpressionPostfix(
    ParseResultIterator* child_results) {
  auto location = child_results->NextAs<Expression*>();
  auto op = child_results->NextAs<IncrementDecrementOperator>();
  Expression* result =
      MakeNode<IncrementDecrementExpression>(location, op, true);
  return ParseResult{result};
}

std::optional<ParseResult> MakeIncrementDecrementExpressionPrefix(
    ParseResultIterator* child_results) {
  auto op = child_results->NextAs<IncrementDecrementOperator>();
  auto location = child_results->NextAs<Expression*>();
  Expression* result =
      MakeNode<IncrementDecrementExpression>(location, op, false);
  return ParseResult{result};
}

std::optional<ParseResult> MakeLogicalOrExpression(
    ParseResultIterator* child_results) {
  auto left = child_results->NextAs<Expression*>();
  auto right = child_results->NextAs<Expression*>();
  Expression* result = MakeNode<LogicalOrExpression>(left, right);
  return ParseResult{result};
}

std::optional<ParseResult> MakeLogicalAndExpression(
    ParseResultIterator* child_results) {
  auto left = child_results->NextAs<Expression*>();
  auto right = child_results->NextAs<Expression*>();
  Expression* result = MakeNode<LogicalAndExpression>(left, right);
  return ParseResult{result};
}

std::optional<ParseResult> MakeConditionalExpression(
    ParseResultIterator* child_results) {
  auto condition = child_results->NextAs<Expression*>();
  auto if_true = child_results->NextAs<Expression*>();
  auto if_false = child_results->NextAs<Expression*>();
  Expression* result =
      MakeNode<ConditionalExpression>(condition, if_true, if_false);
  return ParseResult{result};
}

std::optional<ParseResult> MakeLabelAndTypes(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<Identifier*>();
  if (!IsUpperCamelCase(name->value)) {
    NamingConventionError("Label", name, "UpperCamelCase");
  }
  auto types = child_results->NextAs<std::vector<TypeExpression*>>();
  return ParseResult{LabelAndTypes{name, std::move(types)}};
}

std::optional<ParseResult> MakeNameAndType(ParseResultIterator* child_results) {
  auto name = child_results->NextAs<Identifier*>();
  auto type = child_results->NextAs<TypeExpression*>();
  return ParseResult{NameAndTypeExpression{name, type}};
}

std::optional<ParseResult> MakeEnumEntry(ParseResultIterator* child_results) {
  AnnotationSet annotations(child_results, {}, {ANNOTATION_SAME_ENUM_VALUE_AS});
  std::vector<ConditionalAnnotation> conditions;
  std::optional<std::string> alias_entry =
      annotations.GetStringParam(ANNOTATION_SAME_ENUM_VALUE_AS);

  auto name = child_results->NextAs<Identifier*>();
  auto type = child_results->NextAs<std::optional<TypeExpression*>>();
  return ParseResult{EnumEntry{name, type, alias_entry}};
}

std::optional<ParseResult> MakeNameAndExpression(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<Identifier*>();
  auto expression = child_results->NextAs<Expression*>();
  return ParseResult{NameAndExpression{name, expression}};
}

std::optional<ParseResult> MakeNameAndExpressionFromExpression(
    ParseResultIterator* child_results) {
  auto expression = child_results->NextAs<Expression*>();
  if (auto* id = IdentifierExpression::DynamicCast(expression)) {
    if (!id->generic_arguments.empty() ||
        !id->namespace_qualification.empty()) {
      ReportError("expected a plain identifier without qualification");
    }
    return ParseResult{NameAndExpression{id->name, id}};
  }
  ReportError("Constructor parameters need to be named.");
}

std::optional<ParseResult> MakeAnnotation(ParseResultIterator* child_results) {
  return ParseResult{
      Annotation{child_results->NextAs<Identifier*>(),
                 child_results->NextAs<std::optional<AnnotationParameter>>()}};
}

std::optional<ParseResult> MakeClassField(ParseResultIterator* child_results) {
  AnnotationSet annotations(
      child_results,
      {ANNOTATION_CPP_RELAXED_STORE, ANNOTATION_CPP_RELAXED_LOAD,
       ANNOTATION_CPP_RELEASE_STORE, ANNOTATION_CPP_ACQUIRE_LOAD,
       ANNOTATION_CUSTOM_WEAK_MARKING},
      {ANNOTATION_IF, ANNOTATION_IFNOT});
  FieldSynchronization synchronization = FieldSynchronization::kNone;
  if (annotations.Contains(ANNOTATION_CPP_RELEASE_STORE)) {
    synchronization = FieldSynchronization::kAcquireRelease;
  } else if (annotations.Contains(ANNOTATION_CPP_RELAXED_STORE)) {
    synchronization = FieldSynchronization::kRelaxed;
  }
  {
    FieldSynchronization read_synchronization = FieldSynchronization::kNone;
    if (annotations.Contains(ANNOTATION_CPP_ACQUIRE_LOAD)) {
      read_synchronization = FieldSynchronization::kAcquireRelease;
    } else if (annotations.Contains(ANNOTATION_CPP_RELAXED_LOAD)) {
      read_synchronization = FieldSynchronization::kRelaxed;
    }
    if (read_synchronization != synchronization) {
      Error("Incompatible read/write synchronization annotations for a field.");
    }
  }
  std::vector<ConditionalAnnotation> conditions;
  std::optional<std::string> if_condition =
      annotations.GetStringParam(ANNOTATION_IF);
  std::optional<std::string> ifnot_condition =
      annotations.GetStringParam(ANNOTATION_IFNOT);
  if (if_condition.has_value()) {
    conditions.push_back({*if_condition, ConditionalAnnotationType::kPositive});
  }
  if (ifnot_condition.has_value()) {
    conditions.push_back(
        {*ifnot_condition, ConditionalAnnotationType::kNegative});
  }
  bool custom_weak_marking =
      annotations.Contains(ANNOTATION_CUSTOM_WEAK_MARKING);
  auto deprecated_weak = child_results->NextAs<bool>();
  if (deprecated_weak) {
    Error(
        "The keyword 'weak' is deprecated. For a field that can contain a "
        "normal weak pointer, use type Weak<T>. For a field that should be "
        "marked in some custom way, use @customWeakMarking.");
    custom_weak_marking = true;
  }
  auto const_qualified = child_results->NextAs<bool>();
  auto name = child_results->NextAs<Identifier*>();
  auto optional = child_results->NextAs<bool>();
  auto index = child_results->NextAs<std::optional<Expression*>>();
  if (optional && !index) {
    Error(
        "Fields using optional specifier must also provide an expression "
        "indicating the condition for whether the field is present");
  }
  std::optional<ClassFieldIndexInfo> index_info;
  if (index) {
    if (optional) {
      // Internally, an optional field is just an indexed field where the count
      // is zero or one.
      index = MakeNode<ConditionalExpression>(
          *index,
          MakeCall(
              MakeNode<Identifier>("FromConstexpr"),
              {MakeNode<BasicTypeExpression>(std::vector<std::string>{},
                                             MakeNode<Identifier>("intptr"),
                                             std::vector<TypeExpression*>{})},
              {MakeNode<IntegerLiteralExpression>(IntegerLiteral(1))}, {}),
          MakeCall(
              MakeNode<Identifier>("FromConstexpr"),
              {MakeNode<BasicTypeExpression>(std::vector<std::string>{},
                                             MakeNode<Identifier>("intptr"),
                                             std::vector<TypeExpression*>{})},
              {MakeNode<IntegerLiteralExpression>(IntegerLiteral(0))}, {}));
    }
    index_info = ClassFieldIndexInfo{*index, optional};
  }
  auto type = child_results->NextAs<TypeExpression*>();
  return ParseResult{ClassFieldExpression{{name, type},
                                          index_info,
                                          std::move(conditions),
                                          custom_weak_marking,
                                          const_qualified,
                                          synchronization}};
}

std::optional<ParseResult> MakeStructField(ParseResultIterator* child_results) {
  auto const_qualified = child_results->NextAs<bool>();
  auto name = child_results->NextAs<Identifier*>();
  auto type = child_results->NextAs<TypeExpression*>();
  return ParseResult{StructFieldExpression{{name, type}, const_qualified}};
}

std::optional<ParseResult> MakeBitFieldDeclaration(
    ParseResultIterator* child_results) {
  auto name = child_results->NextAs<Identifier*>();
  auto type = child_results->NextAs<TypeExpression*>();
  auto num_bits = child_results->NextAs<int32_t>();
  return ParseResult{BitFieldDeclaration{{name, type}, num_bits}};
}

std::optional<ParseResult> ExtractAssignmentOperator(
    ParseResultIterator* child_results) {
  auto op = child_results->NextAs<Identifier*>();
  std::optional<std::string> result =
      std::string(op->value.begin(), op->value.end() - 1);
  return ParseResult(std::move(result));
}

struct TorqueGrammar : Grammar {
  static bool MatchWhitespace(InputPosition* pos) {
    while (true) {
      if (MatchChar(std::isspace, pos)) continue;
      if (MatchString("//", pos)) {
        while (MatchChar([](char c) { return c != '\n'; }, pos)) {
        }
        continue;
      }
      if (MatchString("/*", pos)) {
        while (!MatchString("*/", pos)) ++*pos;
        continue;
      }
      return true;
    }
  }

  static bool MatchIdentifier(InputPosition* pos) {
    InputPosition current = *pos;
    MatchString("_", &current);
    if (!MatchChar(std::isalpha, &current)) return false;
    while (MatchChar(std::isalnum, &current) || MatchString("_", &current)) {
    }
    *pos = current;
    return true;
  }

  static bool MatchAnnotation(InputPosition* pos) {
    InputPosition current = *pos;
    if (!MatchString("@", &current)) return false;
    if (!MatchIdentifier(&current)) return false;
    *pos = current;
    return true;
  }

  static bool MatchIntrinsicName(InputPosition* pos) {
    InputPosition current = *pos;
    if (!MatchString("%", &current)) return false;
    if (!MatchIdentifier(&current)) return false;
    *pos = current;
    return true;
  }

  static bool MatchStringLiteral(InputPosition* pos) {
    InputPosition current = *pos;
    if (MatchString("\"", &current)) {
      while (
          (MatchString("\\", &current) && MatchAnyChar(&current)) ||
          MatchChar([](char c) { return c != '"' && c != '\n'; }, &current)) {
      }
      if (MatchString("\"", &current)) {
        *pos = current;
        return true;
      }
    }
    current = *pos;
    if (MatchString("'", &current)) {
      while (
          (MatchString("\\", &current) && MatchAnyChar(&current)) ||
          MatchChar([](char c) { return c != '\'' && c != '\n'; }, &current)) {
      }
      if (MatchString("'", &current)) {
        *pos = current;
        return true;
      }
    }
    return false;
  }

  static bool MatchHexLiteral(InputPosition* pos) {
    InputPosition current = *pos;
    MatchString("-", &current);
    if (MatchString("0x", &current) && MatchChar(std::isxdigit, &current)) {
      while (MatchChar(std::isxdigit, &current)) {
      }
      *pos = current;
      return true;
    }
    return false;
  }

  static bool MatchIntegerLiteral(InputPosition* pos) {
    InputPosition current = *pos;
    bool found_digit = false;
    MatchString("-", &current);
    while (MatchChar(std::isdigit, &current)) found_digit = true;
    if (found_digit) {
      *pos = current;
      return true;
    }
    return false;
  }

  static bool MatchFloatingPointLiteral(InputPosition* pos) {
    InputPosition current = *pos;
    bool found_digit = false;
    MatchString("-", &current);
    while (MatchChar(std::isdigit, &current)) found_digit = true;
    if (!MatchString(".", &current)) return false;
    while (MatchChar(std::isdigit, &current)) found_digit = true;
    if (!found_digit) return false;
    *pos = current;
    if ((MatchString("e", &current) || MatchString("E", &current)) &&
        (MatchString("+", &current) || MatchString("-", &current) || true) &&
        MatchChar(std::isdigit, &current)) {
      while (MatchChar(std::isdigit, &current)) {
      }
      *pos = current;
      return true;
    }
    return true;
  }

  template <class T, bool first>
  static std::optional<ParseResult> MakeExtendedVectorIfAnnotation(
      ParseResultIterator* child_results) {
    std::vector<T> l = {};
    if (!first) l = child_results->NextAs<std::vector<T>>();
    bool enabled = ProcessIfAnnotation(child_results);
    T x = child_results->NextAs<T>();

    if (enabled) l.push_back(std::move(x));
    return ParseResult{std::move(l)};
  }

  template <class T>
  Symbol* NonemptyListAllowIfAnnotation(Symbol* element,
                                        std::optional<Symbol*> separator = {}) {
    Symbol* list = NewSymbol();
    *list = {
        Rule({annotations, element}, MakeExtendedVectorIfAnnotation<T, true>),
        separator ? Rule({list, annotations, *separator, element},
                         MakeExtendedVectorIfAnnotation<T, false>)
                  : Rule({list, annotations, element},
                         MakeExtendedVectorIfAnnotation<T, false>)};
    return list;
  }

  template <class T>
  Symbol* ListAllowIfAnnotation(Symbol* element,
                                std::optional<Symbol*> separator = {}) {
    return TryOrDefault<std::vector<T>>(
        NonemptyListAllowIfAnnotation<T>(element, separator));
  }

  TorqueGrammar() : Grammar(&file) { SetWhitespace(MatchWhitespace); }

  // Result: Expression*
  Symbol* expression = &assignmentExpression;

  // Result: std::string
  Symbol identifier = {Rule({Pattern(MatchIdentifier)}, YieldMatchedInput),
                       Rule({Token("runtime")}, YieldMatchedInput)};

  // Result: Identifier*
  Symbol name = {Rule({&identifier}, MakeIdentifier)};

  // Result: Identifier*
  Symbol annotationName = {
      Rule({Pattern(MatchAnnotation)}, MakeIdentifierFromMatchedInput)};

  // Result: std::string
  Symbol intrinsicName = {
      Rule({Pattern(MatchIntrinsicName)}, MakeIdentifierFromMatchedInput)};

  // Result: std::string
  Symbol stringLiteral = {
      Rule({Pattern(MatchStringLiteral)}, YieldMatchedInput)};

  // Result: std::string
  Symbol externalString = {Rule({&stringLiteral}, StringLiteralUnquoteAction)};

  // Result: IntegerLiteral
  Symbol integerLiteral = {
      Rule({Pattern(MatchIntegerLiteral)}, YieldIntegerLiteral),
      Rule({Pattern(MatchHexLiteral)}, YieldIntegerLiteral)};

  // Result: double
  Symbol floatingPointLiteral = {
      Rule({Pattern(MatchFloatingPointLiteral)}, YieldDouble)};

  // Result: int32_t
  Symbol int32Literal = {Rule({Pattern(MatchIntegerLiteral)}, YieldInt32),
                         Rule({Pattern(MatchHexLiteral)}, YieldInt32)};

  // Result: AnnotationParameter
  Symbol annotationParameter = {
      Rule({&identifier}, MakeStringAnnotationParameter),
      Rule({&int32Literal}, MakeIntAnnotationParameter),
      Rule({&externalString}, MakeStringAnnotationParameter)};

  // Result: AnnotationParameter
  Symbol annotationParameters = {
      Rule({Token("("), &annotationParameter, Token(")")})};

  // Result: Annotation
  Symbol annotation = {Rule(
      {&annotationName, Optional<AnnotationParameter>(&annotationParameters)},
      MakeAnnotation)};

  // Result: std::vector<Annotation>
  Symbol* annotations = List<Annotation>(&annotation);

  // Result: std::vector<std::string>
  Symbol namespaceQualification = {
      Rule({CheckIf(Token("::")),
            List<std::string>(Sequence({&identifier, Token("::")}))},
           MakeNamespaceQualification)};

  // Result: TypeList
  Symbol* typeList = List<TypeExpression*>(&type, Token(","));

  // Result: TypeExpression*
  Symbol simpleType = {
      Rule({Token("("), &type, Token(")")}),
      Rule({&namespaceQualification, CheckIf(Token("constexpr")), &identifier,
            TryOrDefault<std::vector<TypeExpression*>>(
                &genericSpecializationTypeList)},
           MakeBasicTypeExpression),
      Rule({Token("builtin"), Token("("), typeList, Token(")"), Token("=>"),
            &simpleType},
           MakeFunctionTypeExpression),
      Rule({CheckIf(Token("const")), Token("&"), &simpleType},
           MakeReferenceTypeExpression)};

  // Result: TypeExpression*
  Symbol type = {Rule({&simpleType}), Rule({&type, Token("|"), &simpleType},
                                           MakeUnionTypeExpression)};

  // Result: GenericParameter
  Symbol genericParameter = {
      Rule({&name, Token(":"), Token("type"),
            Optional<TypeExpression*>(Sequence({Token("extends"), &type}))},
           MakeGenericParameter)};

  // Result: GenericParameters
  Symbol genericParameters = {
      Rule({Token("<"), List<GenericParameter>(&genericParameter, Token(",")),
            Token(">")})};

  // Result: TypeList
  Symbol genericSpecializationTypeList = {
      Rule({Token("<"), typeList, Token(">")})};

  // Result: std::optional<GenericParameters>
  Symbol* optionalGenericParameters = Optional<TypeList>(&genericParameters);

  Symbol implicitParameterList{
      Rule({Token("("), OneOf({"implicit", "js-implicit"}),
            List<NameAndTypeExpression>(&nameAndType, Token(",")), Token(")")},
           MakeImplicitParameterList)};

  Symbol* optionalImplicitParameterList{
      Optional<ImplicitParameters>(&implicitParameterList)};

  // Result: ParameterList
  Symbol typeListMaybeVarArgs = {
      Rule({optionalImplicitParameterList, Token("("),
            List<TypeExpression*>(Sequence({&type, Token(",")})), Token("..."),
            Token(")")},
           MakeParameterList<true, false>),
      Rule({optionalImplicitParameterList, Token("("), typeList, Token(")")},
           MakeParameterList<false, false>)};

  // Result: LabelAndTypes
  Symbol labelParameter = {Rule(
      {&name,
       TryOrDefault<TypeList>(Sequence({Token("("), typeList, Token(")")}))},
      MakeLabelAndTypes)};

  // Result: TypeExpression*
  Symbol returnType = {Rule({Token(":"), &type}),
                       Rule({}, DeprecatedMakeVoidType)};

  // Result: LabelAndTypesVector
  Symbol* optionalLabelList{TryOrDefault<LabelAndTypesVector>(
      Sequence({Token("labels"),
                NonemptyList<LabelAndTypes>(&labelParameter, Token(","))}))};

  // Result: std::vector<Statement*>
  Symbol* optionalOtherwise{TryOrDefault<std::vector<Statement*>>(
      Sequence({Token("otherwise"),
                NonemptyList<Statement*>(&atomarStatement, Token(","))}))};

  // Result: NameAndTypeExpression
  Symbol nameAndType = {Rule({&name, Token(":"), &type}, MakeNameAndType)};

  // Result: std::optional<Expression*>
  Symbol* optionalArraySpecifier =
      Optional<Expression*>(Sequence({Token("["), expression, Token("]")}));

  // Result: ClassFieldExpression
  Symbol classField = {
      Rule({annotations, CheckIf(Token("weak")), CheckIf(Token("const")), &name,
            CheckIf(Token("?")), optionalArraySpecifier, Token(":"), &type,
            Token(";")},
           MakeClassField)};

  // Result: StructFieldExpression
  Symbol structField = {
      Rule({CheckIf(Token("const")), &name, Token(":"), &type, Token(";")},
           MakeStructField)};

  // Result: BitFieldDeclaration
  Symbol bitFieldDeclaration = {Rule({&name, Token(":"), &type, Token(":"),
                                      &int32Literal, Token("bit"), Token(";")},
                                     MakeBitFieldDeclaration)};

  // Result: ParameterList
  Symbol parameterListNoVararg = {
      Rule({optionalImplicitParameterList, Token("("),
            List<NameAndTypeExpression>(&nameAndType, Token(",")), Token(")")},
           MakeParameterList<false, true>)};

  // Result: ParameterList
  Symbol parameterListAllowVararg = {
      Rule({&parameterListNoVararg}),
      Rule({optionalImplicitParameterList, Token("("),
            List<NameAndTypeExpression>(Sequence({&nameAndType, Token(",")})),
            Token("..."), &identifier, Token(")")},
           MakeParameterList<true, true>)};

  // Result: Identifier*
  Symbol* OneOf(const std::vector<std::string>& alternatives) {
    Symbol* result = NewSymbol();
    for (const std::string& s : alternatives) {
      result->AddRule(Rule({Token(s)}, MakeIdentifierFromMatchedInput));
    }
    return result;
  }

  // Result: Expression*
  Symbol* BinaryOperator(Symbol* nextLevel, Symbol* op) {
    Symbol* result = NewSymbol();
    *result = {Rule({nextLevel}),
               Rule({result, op, nextLevel}, MakeBinaryOperator)};
    return result;
  }

  // Result: IncrementDecrementOperator
  Symbol incrementDecrementOperator = {
      Rule({Token("++")},
           YieldIntegralConstant<IncrementDecrementOperator,
                                 IncrementDecrementOperator::kIncrement>),
      Rule({Token("--")},
           YieldIntegralConstant<IncrementDecrementOperator,
                                 IncrementDecrementOperator::kDecrement>)};

  // Result: Expression*
  Symbol identifierExpression = {
      Rule({&namespaceQualification, &name,
            TryOrDefault<TypeList>(&genericSpecializationTypeList)},
           MakeIdentifierExpression),
  };

  // Result: std::vector<Expression*>
  Symbol argumentList = {Rule(
      {Token("("), List<Expression*>(expression, Token(",")), Token(")")})};

  // Result: Expression*
  Symbol callExpression = {Rule(
      {&identifierExpression, &argumentList, optionalOtherwise}, MakeCall)};

  // Result: Expression*
  Symbol callMethodExpression = {Rule(
      {&primaryExpression, Token("."), &name, &argumentList, optionalOtherwise},
      MakeMethodCall)};

  // Result: NameAndExpression
  Symbol namedExpression = {
      Rule({&name, Token(":"), expression}, MakeNameAndExpression),
      Rule({expression}, MakeNameAndExpressionFromExpression)};

  // Result: std::vector<NameAndExpression>
  Symbol initializerList = {
      Rule({Token("{"), List<NameAndExpression>(&namedExpression, Token(",")),
            Token("}")})};

  // Result: Expression*
  Symbol intrinsicCallExpression = {Rule(
      {&intrinsicName, TryOrDefault<TypeList>(&genericSpecializationTypeList),
       &argumentList},
      MakeIntrinsicCallExpression)};

  // Result: Expression*
  Symbol newExpression = {
      Rule({Token("new"),
            CheckIf(Sequence({Token("("), Token("Pretenured"), Token(")")})),
            CheckIf(Sequence({Token("("), Token("ClearPadding"), Token(")")})),
            &simpleType, &initializerList},
           MakeNewExpression)};

  // Result: Expression*
  Symbol primaryExpression = {
      Rule({&callExpression}),
      Rule({&callMethodExpression}),
      Rule({&intrinsicCallExpression}),
      Rule({&identifierExpression}),
      Rule({&primaryExpression, Token("."), &name}, MakeFieldAccessExpression),
      Rule({&primaryExpression, Token("->"), &name},
           MakeReferenceFieldAccessExpression),
      Rule({&primaryExpression, Token("["), expression, Token("]")},
           MakeElementAccessExpression),
      Rule({&integerLiteral}, MakeIntegerLiteralExpression),
      Rule({&floatingPointLiteral}, MakeFloatingPointLiteralExpression),
      Rule({&stringLiteral}, MakeStringLiteralExpression),
      Rule({&simpleType, &initializerList}, MakeStructExpression),
      Rule({&newExpression}),
      Rule({Token("("), expression, Token(")")})};

  // Result: Expression*
  Symbol unaryExpression = {
      Rule({&primaryExpression}),
      Rule({OneOf({"+", "-", "!", "~", "&"}), &unaryExpression},
           MakeUnaryOperator),
      Rule({Token("*"), &unaryExpression}, MakeDereferenceExpression),
      Rule({Token("..."), &unaryExpression}, MakeSpreadExpression),
      Rule({&incrementDecrementOperator, &unaryExpression},
           MakeIncrementDecrementExpressionPrefix),
      Rule({&unaryExpression, &incrementDecrementOperator},
           MakeIncrementDecrementExpressionPostfix)};

  // Result: Expression*
  Symbol* multiplicativeExpression =
      BinaryOperator(&unaryExpression, OneOf({"*", "/", "%"}));

  // Result: Expression*
  Symbol* additiveExpression =
      BinaryOperator(multiplicativeExpression, OneOf({"+", "-"}));

  // Result: Identifier*
  Symbol shiftOperator = {
      Rule({Token("<<")}, MakeIdentifierFromMatchedInput),
      Rule({Token(">"), Token(">")}, MakeRightShiftIdentifier),
      Rule({Token(">"), Token(">"), Token(">")}, MakeRightShiftIdentifier)};

  // Result: Expression*
  Symbol* shiftExpression = BinaryOperator(additiveExpression, &shiftOperator);

  // Do not allow expressions like a < b > c because this is never
  // useful and ambiguous with template parameters.
  // Result: Expression*
  Symbol relationalExpression = {
      Rule({shiftExpression}),
      Rule({shiftExpression, OneOf({"<", ">", "<=", ">="}), shiftExpression},
           MakeBinaryOperator)};

  // Result: Expression*
  Symbol* equalityExpression =
      BinaryOperator(&relationalExpression, OneOf({"==", "!="}));

  // Result: Expression*
  Symbol* bitwiseExpression =
      BinaryOperator(equalityExpression, OneOf({"&", "|"}));

  // Result: Expression*
  Symbol logicalAndExpression = {
      Rule({bitwiseExpression}),
      Rule({&logicalAndExpression, Token("&&"), bitwiseExpression},
           MakeLogicalAndExpression)};

  // Result: Expression*
  Symbol logicalOrExpression = {
      Rule({&logicalAndExpression}),
      Rule({&logicalOrExpression, Token("||"), &logicalAndExpression},
           MakeLogicalOrExpression)};

  // Result: Expression*
  Symbol conditionalExpression = {
      Rule({&logicalOrExpression}),
      Rule({&logicalOrExpression, Token("?"), expression, Token(":"),
            &conditionalExpression},
           MakeConditionalExpression)};

  // Result: std::optional<std::string>
  Symbol assignmentOperator = {
      Rule({Token("=")}, YieldDefaultValue<std::optional<std::string>>),
      Rule({OneOf({"*=", "/=", "%=", "+=", "-=", "<<=", ">>=", ">>>=", "&=",
                   "^=", "|="})},
           ExtractAssignmentOperator)};

  // Result: Expression*
  Symbol assignmentExpression = {
      Rule({&conditionalExpression}),
      Rule({&conditionalExpression, &assignmentOperator, &assignmentExpression},
           MakeAssignmentExpression)};

  // Result: Statement*
  Symbol block = {
      Rule({CheckIf(Token("deferred")), Token("{"),
            ListAllowIfAnnotation<Statement*>(&statement), Token("}")},
           MakeBlockStatement)};

  // Result: TryHandler*
  Symbol tryHandler = {
      Rule({Token("label"), &name,
            TryOrDefault<ParameterList>(&parameterListNoVararg), &block},
           MakeLabelBlock),
      Rule({Token("catch"), Token("("),
            List<std::string>(&identifier, Token(",")), Token(")"), &block},
           MakeCatchBlock)};

  // Result: ExpressionWithSource
  Symbol expressionWithSource = {Rule({expression}, MakeExpressionWithSource)};

  Symbol* optionalTypeSpecifier =
      Optional<TypeExpression*>(Sequence({Token(":"), &type}));

  // Result: EnumEntry
  Symbol enumEntry = {
      Rule({annotations, &name, optionalTypeSpecifier}, MakeEnumEntry)};

  // Result: Statement*
  Symbol varDeclaration = {
      Rule({OneOf({"let", "const"}), &name, optionalTypeSpecifier},
           MakeVarDeclarationStatement)};

  // Result: Statement*
  Symbol varDeclarationWithInitialization = {
      Rule({OneOf({"let", "const"}), &name, optionalTypeSpecifier, Token("="),
            expression},
           MakeVarDeclarationStatement)};

  // Result: Statement*
  Symbol atomarStatement = {
      Rule({expression}, MakeExpressionStatement),
      Rule({Token("return"), Optional<Expression*>(expression)},
           MakeReturnStatement),
      Rule({Token("tail"), &callExpression}, MakeTailCallStatement),
      Rule({Token("break")}, MakeBreakStatement),
      Rule({Token("continue")}, MakeContinueStatement),
      Rule({Token("goto"), &name,
            TryOrDefault<std::vector<Expression*>>(&argumentList)},
           MakeGotoStatement),
      Rule({OneOf({"debug", "unreachable"})}, MakeDebugStatement)};

  // Result: Statement*
  Symbol statement = {
      Rule({&block}),
      Rule({&atomarStatement, Token(";")}),
      Rule({&varDeclaration, Token(";")}),
      Rule({&varDeclarationWithInitialization, Token(";")}),
      Rule({Token("if"), CheckIf(Token("constexpr")), Token("("), expression,
            Token(")"), &statement,
            Optional<Statement*>(Sequence({Token("else"), &statement}))},
           MakeIfStatement),
      Rule(
          {
              Token("typeswitch"),
              Token("("),
              expression,
              Token(")"),
              Token("{"),
              NonemptyListAllowIfAnnotation<TypeswitchCase>(&typeswitchCase),
              Token("}"),
          },
          MakeTypeswitchStatement),
      Rule({Token("try"), &block, List<TryHandler*>(&tryHandler)},
           MakeTryLabelExpression),
      Rule({OneOf({"dcheck", "check", "sbxcheck", "static_assert"}), Token("("),
            &expressionWithSource, Token(")"), Token(";")},
           MakeAssertStatement),
      Rule({Token("while"), Token("("), expression, Token(")"), &statement},
           MakeWhileStatement),
      Rule({Token("for"), Token("("),
            Optional<Statement*>(&varDeclarationWithInitialization), Token(";"),
            Optional<Expression*>(expression), Token(";"),
            Optional<Expression*>(expression), Token(")"), &statement},
           MakeForLoopStatement)};

  // Result: TypeswitchCase
  Symbol typeswitchCase = {
      Rule({Token("case"), Token("("),
            Optional<Identifier*>(Sequence({&name, Token(":")})), &type,
            Token(")"), Token(":"), &block},
           MakeTypeswitchCase)};

  // Result: std::optional<Statement*>
  Symbol optionalBody = {
      Rule({&block}, CastParseResult<Statement*, std::optional<Statement*>>),
      Rule({Token(";")}, YieldDefaultValue<std::optional<Statement*>>)};

  // Result: Declaration*
  Symbol method = {Rule(
      {CheckIf(Token("transitioning")),
       Optional<std::string>(Sequence({Token("operator"), &externalString})),
       Token("macro"), &name, &parameterListNoVararg, &returnType,
       optionalLabelList, &block},
      MakeMethodDeclaration)};

  // Result: std::optional<ClassBody*>
  Symbol optionalClassBody = {
      Rule({Token("{"), List<Declaration*>(&method),
            List<ClassFieldExpression>(&classField), Token("}")},
           MakeClassBody),
      Rule({Token(";")}, YieldDefaultValue<std::optional<ClassBody*>>)};

  // Result: std::vector<Declaration*>
  Symbol declaration = {
      Rule({annotations, Token("const"), &name, Token(":"), &type, Token("="),
            expression, Token(";")},
           MakeConstDeclaration),
      Rule({Token("const"), &name, Token(":"), &type, Token("generates"),
            &externalString, Token(";")},
           AsSingletonVector<Declaration*, MakeExternConstDeclaration>()),
      Rule({annotations, CheckIf(Token("extern")), CheckIf(Token("transient")),
            OneOf({"class", "shape"}), &name, Token("extends"), &type,
            Optional<std::string>(
                Sequence({Token("generates"), &externalString})),
            &optionalClassBody},
           MakeClassDeclaration),
      Rule({annotations, Token("struct"), &name,
            TryOrDefault<GenericParameters>(&genericParameters), Token("{"),
            ListAllowIfAnnotation<Declaration*>(&method),
            ListAllowIfAnnotation<StructFieldExpression>(&structField),
            Token("}")},
           AsSingletonVector<Declaration*, MakeStructDeclaration>()),
      Rule({Token("bitfield"), Token("struct"), &name, Token("extends"), &type,
            Token("{"),
            ListAllowIfAnnotation<BitFieldDeclaration>(&bitFieldDeclaration),
            Token("}")},
           AsSingletonVector<Declaration*, MakeBitFieldStructDeclaration>()),
      Rule({annotations, CheckIf(Token("transient")), Token("type"), &name,
            TryOrDefault<GenericParameters>(&genericParameters),
            Optional<TypeExpression*>(Sequence({Token("extends"), &type})),
            Optional<std::string>(
                Sequence({Token("generates"), &externalString})),
            Optional<std::string>(
                Sequence({Token("constexpr"), &externalString})),
            Token(";")},
           MakeAbstractTypeDeclaration),
      Rule({annotations, Token("type"), &name, Token("="), &type, Token(";")},
           MakeTypeAliasDeclaration),
      Rule({Token("intrinsic"), &intrinsicName,
            TryOrDefault<GenericParameters>(&genericParameters),
            &parameterListNoVararg, &returnType, &optionalBody},
           AsSingletonVector<Declaration*, MakeIntrinsicDeclaration>()),
      Rule({annotations, Token("extern"), CheckIf(Token("transitioning")),
            Optional<std::string>(
                Sequence({Token("operator"), &externalString})),
            Token("macro"),
            Optional<std::string>(Sequence({&identifier, Token("::")})), &name,
            TryOrDefault<GenericParameters>(&genericParameters),
            &typeListMaybeVarArgs, &returnType, optionalLabelList, Token(";")},
           MakeExternalMacro),
      Rule({Token("extern"), CheckIf(Token("transitioning")),
            CheckIf(Token("javascript")), Token("builtin"), &name,
            TryOrDefault<GenericParameters>(&genericParameters),
            &typeListMaybeVarArgs, &returnType, Token(";")},
           AsSingletonVector<Declaration*, MakeExternalBuiltin>()),
      Rule({Token("extern"), CheckIf(Token("transitioning")), Token("runtime"),
            &name, &typeListMaybeVarArgs, &returnType, Token(";")},
           AsSingletonVector<Declaration*, MakeExternalRuntime>()),
      Rule({annotations, CheckIf(Token("transitioning")),
            Optional<std::string>(
                Sequence({Token("operator"), &externalString})),
            Token("macro"), &name,
            TryOrDefault<GenericParameters>(&genericParameters),
            &parameterListNoVararg, &returnType, optionalLabelList,
            &optionalBody},
           MakeTorqueMacroDeclaration),
      Rule({annotations, CheckIf(Token("transitioning")),
            CheckIf(Token("javascript")), Token("builtin"), &name,
            TryOrDefault<GenericParameters>(&genericParameters),
            &parameterListAllowVararg, &returnType, &optionalBody},
           MakeTorqueBuiltinDeclaration),
      Rule({annotations, CheckIf(Token("transitioning")), &name,
            &genericSpecializationTypeList, &parameterListAllowVararg,
            &returnType, optionalLabelList, &block},
           MakeSpecializationDeclaration),
      Rule({Token("#include"), &externalString},
           AsSingletonVector<Declaration*, MakeCppIncludeDeclaration>()),
      Rule({CheckIf(Token("extern")), Token("enum"), &name,
            Optional<TypeExpression*>(Sequence({Token("extends"), &type})),
            Optional<std::string>(
                Sequence({Token("constexpr"), &externalString})),
            Token("{"),
            NonemptyListAllowIfAnnotation<EnumEntry>(&enumEntry, Token(",")),
            CheckIf(Sequence({Token(","), Token("...")})), Token("}")},
           MakeEnumDeclaration),
      Rule({Token("namespace"), &identifier, Token("{"), &declarationList,
            Token("}")},
           AsSingletonVector<Declaration*, MakeNamespaceDeclaration>())};

  // Result: std::vector<Declaration*>
  Symbol declarationList = {
      Rule({List<std::vector<Declaration*>>(&declaration)}, ConcatList)};

  Symbol file = {Rule({&file, Token("import"), &externalString},
                      ProcessTorqueImportDeclaration),
                 Rule({&file, &declaration}, AddGlobalDeclarations), Rule({})};
};

}  // namespace

void ParseTorque(const std::string& input) {
  BuildFlags::Scope build_flags_scope;
  TorqueGrammar().Parse(input);
}

}  // namespace v8::internal::torque
