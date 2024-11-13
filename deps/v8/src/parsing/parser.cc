// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/parser.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "src/ast/ast-function-literal-id-reindexer.h"
#include "src/ast/ast-traversal-visitor.h"
#include "src/ast/ast.h"
#include "src/ast/source-range-ast-visitor.h"
#include "src/base/ieee754.h"
#include "src/base/overflowing-math.h"
#include "src/base/platform/platform.h"
#include "src/codegen/bailout-reason.h"
#include "src/common/globals.h"
#include "src/common/message-template.h"
#include "src/compiler-dispatcher/lazy-compile-dispatcher.h"
#include "src/heap/parked-scope.h"
#include "src/logging/counters.h"
#include "src/logging/log.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/scope-info.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/rewriter.h"
#include "src/runtime/runtime.h"
#include "src/strings/char-predicates-inl.h"
#include "src/strings/string-stream.h"
#include "src/strings/unicode-inl.h"
#include "src/tracing/trace-event.h"
#include "src/zone/zone-list-inl.h"

namespace v8::internal {

FunctionLiteral* Parser::DefaultConstructor(const AstRawString* name,
                                            bool call_super, int pos) {
  int expected_property_count = 0;
  const int parameter_count = 0;

  FunctionKind kind = call_super ? FunctionKind::kDefaultDerivedConstructor
                                 : FunctionKind::kDefaultBaseConstructor;
  DeclarationScope* function_scope = NewFunctionScope(kind);
  SetLanguageMode(function_scope, LanguageMode::kStrict);
  // Set start and end position to the same value
  function_scope->set_start_position(pos);
  function_scope->set_end_position(pos);
  ScopedPtrList<Statement> body(pointer_buffer());

  {
    FunctionState function_state(&function_state_, &scope_, function_scope);

    // ES#sec-runtime-semantics-classdefinitionevaluation
    //
    // 14.a
    //  ...
    //  iv. If F.[[ConstructorKind]] is DERIVED, then
    //    1. NOTE: This branch behaves similarly to constructor(...args) {
    //       super(...args); }. The most notable distinction is that while the
    //       aforementioned ECMAScript source text observably calls the
    //       @@iterator method on %Array.prototype%, this function does not.
    //    2. Let func be ! F.[[GetPrototypeOf]]().
    //    3. If IsConstructor(func) is false, throw a TypeError exception.
    //    4. Let result be ? Construct(func, args, NewTarget).
    //  ...
    if (call_super) {
      SuperCallReference* super_call_ref = NewSuperCallReference(pos);
      Expression* call =
          factory()->NewSuperCallForwardArgs(super_call_ref, pos);
      body.Add(factory()->NewReturnStatement(call, pos));
    }

    expected_property_count = function_state.expected_property_count();
  }

  FunctionLiteral* function_literal = factory()->NewFunctionLiteral(
      name, function_scope, body, expected_property_count, parameter_count,
      parameter_count, FunctionLiteral::kNoDuplicateParameters,
      FunctionSyntaxKind::kAnonymousExpression, default_eager_compile_hint(),
      pos, true, GetNextInfoId());
  return function_literal;
}

FunctionLiteral* Parser::MakeAutoAccessorGetter(VariableProxy* name_proxy,
                                                const AstRawString* name,
                                                bool is_static, int pos) {
  ScopedPtrList<Statement> body(pointer_buffer());
  DeclarationScope* function_scope =
      NewFunctionScope(is_static ? FunctionKind::kGetterFunction
                                 : FunctionKind::kStaticGetterFunction);
  SetLanguageMode(function_scope, LanguageMode::kStrict);
  function_scope->set_start_position(pos);
  function_scope->set_end_position(pos);
  {
    FunctionState function_state(&function_state_, &scope_, function_scope);
    body.Add(factory()->NewAutoAccessorGetterBody(name_proxy, pos));
  }
  // TODO(42202709): Enable lazy compilation by adding custom handling in
  //                 `Parser::DoParseFunction`.
  FunctionLiteral* getter = factory()->NewFunctionLiteral(
      nullptr, function_scope, body, 0, 0, 0,
      FunctionLiteral::kNoDuplicateParameters,
      FunctionSyntaxKind::kAccessorOrMethod,
      FunctionLiteral::kShouldEagerCompile, pos, true, GetNextInfoId());
  const AstRawString* prefix =
      name ? ast_value_factory()->get_space_string() : nullptr;
  SetFunctionName(getter, name, prefix);
  return getter;
}

FunctionLiteral* Parser::MakeAutoAccessorSetter(VariableProxy* name_proxy,
                                                const AstRawString* name,
                                                bool is_static, int pos) {
  ScopedPtrList<Statement> body(pointer_buffer());
  DeclarationScope* function_scope =
      NewFunctionScope(is_static ? FunctionKind::kSetterFunction
                                 : FunctionKind::kStaticSetterFunction);
  SetLanguageMode(function_scope, LanguageMode::kStrict);
  function_scope->set_start_position(pos);
  function_scope->set_end_position(pos);
  function_scope->DeclareParameter(ast_value_factory()->empty_string(),
                                   VariableMode::kTemporary, false, false,
                                   ast_value_factory(), kNoSourcePosition);
  {
    FunctionState function_state(&function_state_, &scope_, function_scope);
    body.Add(factory()->NewAutoAccessorSetterBody(name_proxy, pos));
  }
  // TODO(42202709): Enable lazy compilation by adding custom handling in
  //                 `Parser::DoParseFunction`.
  FunctionLiteral* setter = factory()->NewFunctionLiteral(
      nullptr, function_scope, body, 0, 1, 0,
      FunctionLiteral::kNoDuplicateParameters,
      FunctionSyntaxKind::kAccessorOrMethod,
      FunctionLiteral::kShouldEagerCompile, pos, true, GetNextInfoId());
  const AstRawString* prefix =
      name ? ast_value_factory()->set_space_string() : nullptr;
  SetFunctionName(setter, name, prefix);
  return setter;
}

AutoAccessorInfo* Parser::NewAutoAccessorInfo(ClassScope* scope,
                                              ClassInfo* class_info,
                                              const AstRawString* name,
                                              bool is_static, int pos) {
  VariableProxy* accessor_storage_name_proxy =
      CreateSyntheticContextVariableProxy(
          scope, class_info,
          AutoAccessorVariableName(ast_value_factory(),
                                   class_info->autoaccessor_count++),
          is_static);
  // The property value position will match the beginning of the "accessor"
  // keyword, which can be the same as the start of the parent class scope, use
  // the position of the next two characters to distinguish them.
  FunctionLiteral* getter = MakeAutoAccessorGetter(accessor_storage_name_proxy,
                                                   name, is_static, pos + 1);
  FunctionLiteral* setter = MakeAutoAccessorSetter(accessor_storage_name_proxy,
                                                   name, is_static, pos + 2);
  return factory()->NewAutoAccessorInfo(getter, setter,
                                        accessor_storage_name_proxy);
}

ClassLiteralProperty* Parser::NewClassLiteralPropertyWithAccessorInfo(
    ClassScope* scope, ClassInfo* class_info, const AstRawString* name,
    Expression* key, Expression* value, bool is_static, bool is_computed_name,
    bool is_private, int pos) {
  AutoAccessorInfo* accessor_info =
      NewAutoAccessorInfo(scope, class_info, name, is_static, pos);
  return factory()->NewClassLiteralProperty(
      key, value, accessor_info, is_static, is_computed_name, is_private);
}

void Parser::ReportUnexpectedTokenAt(Scanner::Location location,
                                     Token::Value token,
                                     MessageTemplate message) {
  const char* arg = nullptr;
  switch (token) {
    case Token::kEos:
      message = MessageTemplate::kUnexpectedEOS;
      break;
    case Token::kSmi:
    case Token::kNumber:
    case Token::kBigInt:
      message = MessageTemplate::kUnexpectedTokenNumber;
      break;
    case Token::kString:
      message = MessageTemplate::kUnexpectedTokenString;
      break;
    case Token::kPrivateName:
    case Token::kIdentifier:
      message = MessageTemplate::kUnexpectedTokenIdentifier;
      // Use ReportMessageAt with the AstRawString parameter; skip the
      // ReportMessageAt below.
      ReportMessageAt(location, message, GetIdentifier());
      return;
    case Token::kAwait:
    case Token::kEnum:
      message = MessageTemplate::kUnexpectedReserved;
      break;
    case Token::kLet:
    case Token::kStatic:
    case Token::kYield:
    case Token::kFutureStrictReservedWord:
      message = is_strict(language_mode())
                    ? MessageTemplate::kUnexpectedStrictReserved
                    : MessageTemplate::kUnexpectedTokenIdentifier;
      arg = Token::String(token);
      break;
    case Token::kTemplateSpan:
    case Token::kTemplateTail:
      message = MessageTemplate::kUnexpectedTemplateString;
      break;
    case Token::kEscapedStrictReservedWord:
    case Token::kEscapedKeyword:
      message = MessageTemplate::kInvalidEscapedReservedWord;
      break;
    case Token::kIllegal:
      if (scanner()->has_error()) {
        message = scanner()->error();
        location = scanner()->error_location();
      } else {
        message = MessageTemplate::kInvalidOrUnexpectedToken;
      }
      break;
    case Token::kRegExpLiteral:
      message = MessageTemplate::kUnexpectedTokenRegExp;
      break;
    default:
      const char* name = Token::String(token);
      DCHECK_NOT_NULL(name);
      arg = name;
      break;
  }
  ReportMessageAt(location, message, arg);
}

// ----------------------------------------------------------------------------
// Implementation of Parser

bool Parser::ShortcutLiteralBinaryExpression(Expression** x, Expression* y,
                                             Token::Value op, int pos) {
  // Constant fold numeric operations.
  if ((*x)->IsNumberLiteral() && y->IsNumberLiteral()) {
    double x_val = (*x)->AsLiteral()->AsNumber();
    double y_val = y->AsLiteral()->AsNumber();
    switch (op) {
      case Token::kAdd:
        *x = factory()->NewNumberLiteral(x_val + y_val, pos);
        return true;
      case Token::kSub:
        *x = factory()->NewNumberLiteral(x_val - y_val, pos);
        return true;
      case Token::kMul:
        *x = factory()->NewNumberLiteral(x_val * y_val, pos);
        return true;
      case Token::kDiv:
        *x = factory()->NewNumberLiteral(base::Divide(x_val, y_val), pos);
        return true;
      case Token::kMod:
        *x = factory()->NewNumberLiteral(Modulo(x_val, y_val), pos);
        return true;
      case Token::kBitOr: {
        int value = DoubleToInt32(x_val) | DoubleToInt32(y_val);
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::kBitAnd: {
        int value = DoubleToInt32(x_val) & DoubleToInt32(y_val);
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::kBitXor: {
        int value = DoubleToInt32(x_val) ^ DoubleToInt32(y_val);
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::kShl: {
        int value =
            base::ShlWithWraparound(DoubleToInt32(x_val), DoubleToInt32(y_val));
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::kShr: {
        uint32_t shift = DoubleToInt32(y_val) & 0x1F;
        uint32_t value = DoubleToUint32(x_val) >> shift;
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::kSar: {
        uint32_t shift = DoubleToInt32(y_val) & 0x1F;
        int value = ArithmeticShiftRight(DoubleToInt32(x_val), shift);
        *x = factory()->NewNumberLiteral(value, pos);
        return true;
      }
      case Token::kExp:
        *x = factory()->NewNumberLiteral(base::ieee754::pow(x_val, y_val), pos);
        return true;
      default:
        break;
    }
  }

  // Constant fold string concatenation.
  if (op == Token::kAdd) {
    // Only consider string concatenation of two strings.
    // TODO(leszeks): We could also eagerly convert other literals to string if
    // one side of the addition is a string.
    if ((*x)->IsStringLiteral() && y->IsStringLiteral()) {
      const AstRawString* x_val = (*x)->AsLiteral()->AsRawString();
      const AstRawString* y_val = y->AsLiteral()->AsRawString();

      int new_length = x_val->length() + y_val->length();
      // Copy the strings into a new AstRawString.
      // TODO(leszeks): We could avoid the string copy here by using an
      // AstConsString, but then we'd need to make StringLiteral be able to hold
      // different kinds of AstString, and we'd need to figure out if we want to
      // later internalize the concatenated string (right now AstRawStrings are
      // internalized but AstConsString is not).
      const AstRawString* new_val;
      if (x_val->is_one_byte() && y_val->is_one_byte()) {
        // TODO(leszeks): The data will be copied again by the AstValueFactory.
        // We could avoid the multiple copies by using a stack vector here, or
        // by making GetOneByteString do the string concatenation as part of its
        // string table lookup.
        base::Vector<uint8_t> new_data =
            zone()->AllocateVector<uint8_t>(new_length);
        CopyChars(new_data.data(), x_val->raw_data(), x_val->length());
        CopyChars(new_data.data() + x_val->length(), y_val->raw_data(),
                  y_val->length());
        new_val = ast_value_factory()->GetOneByteString(new_data);
      } else {
        base::Vector<uint16_t> new_data =
            zone()->AllocateVector<uint16_t>(new_length);
        if (x_val->is_one_byte()) {
          CopyChars(new_data.data(), x_val->raw_data(), x_val->length());
        } else {
          CopyChars(new_data.data(),
                    reinterpret_cast<const uint16_t*>(x_val->raw_data()),
                    x_val->length());
        }
        if (y_val->is_one_byte()) {
          CopyChars(new_data.data() + x_val->length(), y_val->raw_data(),
                    y_val->length());
        } else {
          CopyChars(new_data.data() + x_val->length(),
                    reinterpret_cast<const uint16_t*>(y_val->raw_data()),
                    y_val->length());
        }
        new_val = ast_value_factory()->GetTwoByteString(new_data);
      }

      *x = factory()->NewStringLiteral(new_val, (*x)->position());
      return true;
    }
  }
  return false;
}

bool Parser::CollapseConditionalChain(Expression** x, Expression* cond,
                                      Expression* then_expression,
                                      Expression* else_expression, int pos,
                                      const SourceRange& then_range) {
  if (*x && (*x)->IsConditionalChain()) {
    ConditionalChain* conditional_chain = (*x)->AsConditionalChain();
    if (then_expression != nullptr) {
      conditional_chain->AddChainEntry(cond, then_expression, pos);
      AppendConditionalChainSourceRange(conditional_chain, then_range);
    }
    if (else_expression != nullptr) {
      conditional_chain->set_else_expression(else_expression);
      DCHECK_GT(conditional_chain->conditional_chain_length(), 1);
    }
    return true;
  }
  return false;
}

void Parser::AppendConditionalChainElse(Expression** x,
                                        const SourceRange& else_range) {
  if (*x && (*x)->IsConditionalChain()) {
    ConditionalChain* conditional_chain = (*x)->AsConditionalChain();
    AppendConditionalChainElseSourceRange(conditional_chain, else_range);
  }
}

bool Parser::CollapseNaryExpression(Expression** x, Expression* y,
                                    Token::Value op, int pos,
                                    const SourceRange& range) {
  // Filter out unsupported ops.
  if (!Token::IsBinaryOp(op) || op == Token::kExp) return false;

  // Convert *x into an nary operation with the given op, returning false if
  // this is not possible.
  NaryOperation* nary = nullptr;
  if ((*x)->IsBinaryOperation()) {
    BinaryOperation* binop = (*x)->AsBinaryOperation();
    if (binop->op() != op) return false;

    nary = factory()->NewNaryOperation(op, binop->left(), 2);
    nary->AddSubsequent(binop->right(), binop->position());
    ConvertBinaryToNaryOperationSourceRange(binop, nary);
    *x = nary;
  } else if ((*x)->IsNaryOperation()) {
    nary = (*x)->AsNaryOperation();
    if (nary->op() != op) return false;
  } else {
    return false;
  }

  // Append our current expression to the nary operation.
  // TODO(leszeks): Do some literal collapsing here if we're appending Smi or
  // String literals.
  nary->AddSubsequent(y, pos);
  nary->clear_parenthesized();
  AppendNaryOperationSourceRange(nary, range);

  return true;
}

const AstRawString* Parser::GetBigIntAsSymbol() {
  base::Vector<const uint8_t> literal = scanner()->BigIntLiteral();
  if (literal[0] != '0' || literal.length() == 1) {
    return ast_value_factory()->GetOneByteString(literal);
  }
  std::unique_ptr<char[]> decimal =
      BigIntLiteralToDecimal(local_isolate_, literal);
  return ast_value_factory()->GetOneByteString(decimal.get());
}

Expression* Parser::BuildUnaryExpression(Expression* expression,
                                         Token::Value op, int pos) {
  DCHECK_NOT_NULL(expression);
  const Literal* literal = expression->AsLiteral();
  if (literal != nullptr) {
    if (op == Token::kNot) {
      // Convert the literal to a boolean condition and negate it.
      return factory()->NewBooleanLiteral(literal->ToBooleanIsFalse(), pos);
    } else if (literal->IsNumberLiteral()) {
      // Compute some expressions involving only number literals.
      double value = literal->AsNumber();
      switch (op) {
        case Token::kAdd:
          return expression;
        case Token::kSub:
          return factory()->NewNumberLiteral(-value, pos);
        case Token::kBitNot:
          return factory()->NewNumberLiteral(~DoubleToInt32(value), pos);
        default:
          break;
      }
    }
  }
  return factory()->NewUnaryOperation(op, expression, pos);
}

Expression* Parser::NewThrowError(Runtime::FunctionId id,
                                  MessageTemplate message,
                                  const AstRawString* arg, int pos) {
  ScopedPtrList<Expression> args(pointer_buffer());
  args.Add(factory()->NewSmiLiteral(static_cast<int>(message), pos));
  args.Add(factory()->NewStringLiteral(arg, pos));
  CallRuntime* call_constructor = factory()->NewCallRuntime(id, args, pos);
  return factory()->NewThrow(call_constructor, pos);
}

Expression* Parser::NewSuperPropertyReference(int pos) {
  const AstRawString* home_object_name;
  if (IsStatic(scope()->GetReceiverScope()->function_kind())) {
    home_object_name = ast_value_factory_->dot_static_home_object_string();
  } else {
    home_object_name = ast_value_factory_->dot_home_object_string();
  }

  VariableProxy* proxy = NewUnresolved(home_object_name, pos);
  proxy->set_is_home_object();
  return factory()->NewSuperPropertyReference(proxy, pos);
}

SuperCallReference* Parser::NewSuperCallReference(int pos) {
  VariableProxy* new_target_proxy =
      NewUnresolved(ast_value_factory()->new_target_string(), pos);
  VariableProxy* this_function_proxy =
      NewUnresolved(ast_value_factory()->this_function_string(), pos);
  return factory()->NewSuperCallReference(new_target_proxy, this_function_proxy,
                                          pos);
}

Expression* Parser::NewTargetExpression(int pos) {
  auto proxy = NewUnresolved(ast_value_factory()->new_target_string(), pos);
  proxy->set_is_new_target();
  return proxy;
}

Expression* Parser::ImportMetaExpression(int pos) {
  ScopedPtrList<Expression> args(pointer_buffer());
  if (!has_module_in_scope_chain()) {
    DCHECK(IsParsingWhileDebugging());
    // When debugging, we permit import.meta invocations -- however, they will
    // never produce a non-undefined result outside of a module.
    return factory()->NewUndefinedLiteral(pos);
  }
  return factory()->NewCallRuntime(Runtime::kInlineGetImportMetaObject, args,
                                   pos);
}

Expression* Parser::ExpressionFromLiteral(Token::Value token, int pos) {
  switch (token) {
    case Token::kNullLiteral:
      return factory()->NewNullLiteral(pos);
    case Token::kTrueLiteral:
      return factory()->NewBooleanLiteral(true, pos);
    case Token::kFalseLiteral:
      return factory()->NewBooleanLiteral(false, pos);
    case Token::kSmi: {
      uint32_t value = scanner()->smi_value();
      return factory()->NewSmiLiteral(value, pos);
    }
    case Token::kNumber: {
      double value = scanner()->DoubleValue();
      return factory()->NewNumberLiteral(value, pos);
    }
    case Token::kBigInt:
      return factory()->NewBigIntLiteral(
          AstBigInt(scanner()->CurrentLiteralAsCString(zone())), pos);
    case Token::kString: {
      return factory()->NewStringLiteral(GetSymbol(), pos);
    }
    default:
      DCHECK(false);
  }
  return FailureExpression();
}

Expression* Parser::NewV8Intrinsic(const AstRawString* name,
                                   const ScopedPtrList<Expression>& args,
                                   int pos) {
  if (ParsingExtension()) {
    // The extension structures are only accessible while parsing the
    // very first time, not when reparsing because of lazy compilation.
    GetClosureScope()->ForceEagerCompilation();
  }

  if (!name->is_one_byte()) {
    // There are no two-byte named intrinsics.
    ReportMessage(MessageTemplate::kNotDefined, name);
    return FailureExpression();
  }

  const Runtime::Function* function =
      Runtime::FunctionForName(name->raw_data(), name->length());

  // Be more permissive when fuzzing. Intrinsics are not supported.
  if (v8_flags.fuzzing) {
    return NewV8RuntimeFunctionForFuzzing(function, args, pos);
  }

  if (function == nullptr) {
    ReportMessage(MessageTemplate::kNotDefined, name);
    return FailureExpression();
  }

  // Check that the expected number of arguments are being passed.
  if (function->nargs != -1 && function->nargs != args.length()) {
    ReportMessage(MessageTemplate::kRuntimeWrongNumArgs);
    return FailureExpression();
  }

  return factory()->NewCallRuntime(function, args, pos);
}

// More permissive runtime-function creation on fuzzers.
Expression* Parser::NewV8RuntimeFunctionForFuzzing(
    const Runtime::Function* function, const ScopedPtrList<Expression>& args,
    int pos) {
  CHECK(v8_flags.fuzzing);

  // Intrinsics are not supported for fuzzing. Only allow runtime functions
  // marked as fuzzing-safe. Also prevent later errors due to too few arguments
  // and just ignore this call.
  if (function == nullptr ||
      !Runtime::IsEnabledForFuzzing(function->function_id) ||
      function->nargs > args.length()) {
    return factory()->NewUndefinedLiteral(kNoSourcePosition);
  }

  // Flexible number of arguments permitted.
  if (function->nargs == -1) {
    return factory()->NewCallRuntime(function, args, pos);
  }

  // Otherwise ignore superfluous arguments.
  ScopedPtrList<Expression> permissive_args(pointer_buffer());
  for (int i = 0; i < function->nargs; i++) {
    permissive_args.Add(args.at(i));
  }
  return factory()->NewCallRuntime(function, permissive_args, pos);
}

Parser::Parser(LocalIsolate* local_isolate, ParseInfo* info)
    : ParserBase<Parser>(info->zone(), &scanner_, info->stack_limit(),
                         info->ast_value_factory(),
                         info->pending_error_handler(),
                         info->runtime_call_stats(), info->v8_file_logger(),
                         info->flags(), true),
      local_isolate_(local_isolate),
      info_(info),
      scanner_(info->character_stream(), flags()),
      preparser_zone_(info->zone()->allocator(), "pre-parser-zone"),
      reusable_preparser_(nullptr),
      mode_(PARSE_EAGERLY),  // Lazy mode must be set explicitly.
      source_range_map_(info->source_range_map()),
      total_preparse_skipped_(0),
      consumed_preparse_data_(info->consumed_preparse_data()),
      preparse_data_buffer_(),
      parameters_end_pos_(info->parameters_end_pos()) {
  // Even though we were passed ParseInfo, we should not store it in
  // Parser - this makes sure that Isolate is not accidentally accessed via
  // ParseInfo during background parsing.
  DCHECK_NOT_NULL(info->character_stream());
  // Determine if functions can be lazily compiled. This is necessary to
  // allow some of our builtin JS files to be lazily compiled. These
  // builtins cannot be handled lazily by the parser, since we have to know
  // if a function uses the special natives syntax, which is something the
  // parser records.
  // If the debugger requests compilation for break points, we cannot be
  // aggressive about lazy compilation, because it might trigger compilation
  // of functions without an outer context when setting a breakpoint through
  // Debug::FindSharedFunctionInfoInScript
  // We also compile eagerly for kProduceExhaustiveCodeCache.
  bool can_compile_lazily = flags().allow_lazy_compile() && !flags().is_eager();

  set_default_eager_compile_hint(can_compile_lazily
                                     ? FunctionLiteral::kShouldLazyCompile
                                     : FunctionLiteral::kShouldEagerCompile);
  allow_lazy_ = flags().allow_lazy_compile() && flags().allow_lazy_parsing() &&
                info->extension() == nullptr && can_compile_lazily;
  for (int feature = 0; feature < v8::Isolate::kUseCounterFeatureCount;
       ++feature) {
    use_counts_[feature] = 0;
  }
}

void Parser::InitializeEmptyScopeChain(ParseInfo* info) {
  DCHECK_NULL(original_scope_);
  DCHECK_NULL(info->script_scope());
  DeclarationScope* script_scope =
      NewScriptScope(flags().is_repl_mode() ? REPLMode::kYes : REPLMode::kNo);
  info->set_script_scope(script_scope);
  original_scope_ = script_scope;
}

template <typename IsolateT>
void Parser::DeserializeScopeChain(
    IsolateT* isolate, ParseInfo* info,
    MaybeHandle<ScopeInfo> maybe_outer_scope_info,
    Scope::DeserializationMode mode) {
  InitializeEmptyScopeChain(info);
  Handle<ScopeInfo> outer_scope_info;
  if (maybe_outer_scope_info.ToHandle(&outer_scope_info)) {
    DCHECK_EQ(ThreadId::Current(), isolate->thread_id());
    original_scope_ = Scope::DeserializeScopeChain(
        isolate, zone(), *outer_scope_info, info->script_scope(),
        ast_value_factory(), mode, info);
    if (flags().is_eval() || IsArrowFunction(flags().function_kind()) ||
        flags().function_kind() ==
            FunctionKind::kClassStaticInitializerFunction) {
      original_scope_->GetReceiverScope()->DeserializeReceiver(
          ast_value_factory());
    }
    if (info->has_module_in_scope_chain()) {
      set_has_module_in_scope_chain();
    }
  }
}

template void Parser::DeserializeScopeChain(
    Isolate* isolate, ParseInfo* info,
    MaybeHandle<ScopeInfo> maybe_outer_scope_info,
    Scope::DeserializationMode mode);
template void Parser::DeserializeScopeChain(
    LocalIsolate* isolate, ParseInfo* info,
    MaybeHandle<ScopeInfo> maybe_outer_scope_info,
    Scope::DeserializationMode mode);

namespace {

void MaybeProcessSourceRanges(ParseInfo* parse_info, Expression* root,
                              uintptr_t stack_limit_) {
  if (parse_info->source_range_map() != nullptr) {
    SourceRangeAstVisitor visitor(stack_limit_, root,
                                  parse_info->source_range_map());
    visitor.Run();
  }
}

}  // namespace

void Parser::ParseProgram(Isolate* isolate, DirectHandle<Script> script,
                          ParseInfo* info,
                          MaybeHandle<ScopeInfo> maybe_outer_scope_info) {
  DCHECK_EQ(script->id(), flags().script_id());

  // It's OK to use the Isolate & counters here, since this function is only
  // called in the main thread.
  DCHECK(parsing_on_main_thread_);
  RCS_SCOPE(runtime_call_stats_, flags().is_eval()
                                     ? RuntimeCallCounterId::kParseEval
                                     : RuntimeCallCounterId::kParseProgram);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.ParseProgram");
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(v8_flags.log_function_events)) timer.Start();

  // Initialize parser state.
  DeserializeScopeChain(isolate, info, maybe_outer_scope_info,
                        Scope::DeserializationMode::kIncludingVariables);

  DCHECK_EQ(script->is_wrapped(), info->is_wrapped_as_function());
  if (script->is_wrapped()) {
    maybe_wrapped_arguments_ = handle(script->wrapped_arguments(), isolate);
  }

  scanner_.Initialize();
  FunctionLiteral* result = DoParseProgram(isolate, info);
  HandleSourceURLComments(isolate, script);
  if (result == nullptr) return;
  MaybeProcessSourceRanges(info, result, stack_limit_);
  PostProcessParseResult(isolate, info, result);

  if (V8_UNLIKELY(v8_flags.log_function_events)) {
    double ms = timer.Elapsed().InMillisecondsF();
    const char* event_name = "parse-eval";
    int start = -1;
    int end = -1;
    if (!flags().is_eval()) {
      event_name = "parse-script";
      start = 0;
      end = Cast<String>(script->source())->length();
    }
    LOG(isolate,
        FunctionEvent(event_name, flags().script_id(), ms, start, end, "", 0));
  }
}

FunctionLiteral* Parser::DoParseProgram(Isolate* isolate, ParseInfo* info) {
  // Note that this function can be called from the main thread or from a
  // background thread. We should not access anything Isolate / heap dependent
  // via ParseInfo, and also not pass it forward. If not on the main thread
  // isolate will be nullptr.
  DCHECK_EQ(parsing_on_main_thread_, isolate != nullptr);
  DCHECK_NULL(scope_);

  ParsingModeScope mode(this, allow_lazy_ ? PARSE_LAZILY : PARSE_EAGERLY);
  ResetInfoId();

  FunctionLiteral* result = nullptr;
  {
    Scope* outer = original_scope_;
    DCHECK_NOT_NULL(outer);
    if (flags().is_eval()) {
      outer = NewEvalScope(outer);
    } else if (flags().is_module()) {
      DCHECK_EQ(outer, info->script_scope());
      outer = NewModuleScope(info->script_scope());
    }

    DeclarationScope* scope = outer->AsDeclarationScope();
    scope->set_start_position(0);

    FunctionState function_state(&function_state_, &scope_, scope);
    ScopedPtrList<Statement> body(pointer_buffer());
    int beg_pos = scanner()->location().beg_pos;
    if (flags().is_module()) {
      DCHECK(flags().is_module());

      PrepareGeneratorVariables();
      Expression* initial_yield = BuildInitialYield(
          kNoSourcePosition, FunctionKind::kGeneratorFunction);
      body.Add(
          factory()->NewExpressionStatement(initial_yield, kNoSourcePosition));
      ParseModuleItemList(&body);
      // Modules will always have an initial yield. If there are any
      // additional suspends, they are awaits, and we treat the module as a
      // ModuleWithTopLevelAwait.
      if (function_state.suspend_count() > 1) {
        scope->set_module_has_toplevel_await();
      }
      if (!has_error() &&
          !module()->Validate(this->scope()->AsModuleScope(),
                              pending_error_handler(), zone())) {
        scanner()->set_parser_error();
      }
    } else if (info->is_wrapped_as_function()) {
      DCHECK(parsing_on_main_thread_);
      ParseWrapped(isolate, info, &body, scope, zone());
    } else if (flags().is_repl_mode()) {
      ParseREPLProgram(info, &body, scope);
    } else {
      // Don't count the mode in the use counters--give the program a chance
      // to enable script-wide strict mode below.
      this->scope()->SetLanguageMode(info->language_mode());
      ParseStatementList(&body, Token::kEos);
    }

    // The parser will peek but not consume kEos.  Our scope logically goes all
    // the way to the kEos, though.
    scope->set_end_position(peek_position());

    if (is_strict(language_mode())) {
      CheckStrictOctalLiteral(beg_pos, end_position());
    }
    if (is_sloppy(language_mode())) {
      // TODO(littledan): Function bindings on the global object that modify
      // pre-existing bindings should be made writable, enumerable and
      // nonconfigurable if possible, whereas this code will leave attributes
      // unchanged if the property already exists.
      InsertSloppyBlockFunctionVarBindings(scope);
    }
    // Internalize the ast strings in the case of eval so we can check for
    // conflicting var declarations with outer scope-info-backed scopes.
    if (flags().is_eval()) {
      DCHECK(parsing_on_main_thread_);
      DCHECK(!isolate->main_thread_local_heap()->IsParked());
      info->ast_value_factory()->Internalize(isolate);
    }
    CheckConflictingVarDeclarations(scope);

    if (flags().parse_restriction() == ONLY_SINGLE_FUNCTION_LITERAL) {
      if (body.length() != 1 || !body.at(0)->IsExpressionStatement() ||
          !body.at(0)
               ->AsExpressionStatement()
               ->expression()
               ->IsFunctionLiteral()) {
        ReportMessage(MessageTemplate::kSingleFunctionLiteral);
      }
    }

    int parameter_count = 0;
    result = factory()->NewScriptOrEvalFunctionLiteral(
        scope, body, function_state.expected_property_count(), parameter_count);
    result->set_suspend_count(function_state.suspend_count());
  }

  info->set_max_info_id(GetLastInfoId());

  if (has_error()) return nullptr;

  RecordFunctionLiteralSourceRange(result);

  return result;
}

template <typename IsolateT>
void Parser::PostProcessParseResult(IsolateT* isolate, ParseInfo* info,
                                    FunctionLiteral* literal) {
  DCHECK_NOT_NULL(literal);

  info->set_literal(literal);
  info->set_language_mode(literal->language_mode());
  if (info->flags().is_eval()) {
    info->set_allow_eval_cache(allow_eval_cache());
  }

  info->ast_value_factory()->Internalize(isolate);

  {
    RCS_SCOPE(info->runtime_call_stats(), RuntimeCallCounterId::kCompileAnalyse,
              RuntimeCallStats::kThreadSpecific);
    if (!Rewriter::Rewrite(info) || !DeclarationScope::Analyze(info)) {
      // Null out the literal to indicate that something failed.
      info->set_literal(nullptr);
      return;
    }
  }
}

template void Parser::PostProcessParseResult(Isolate* isolate, ParseInfo* info,
                                             FunctionLiteral* literal);
template void Parser::PostProcessParseResult(LocalIsolate* isolate,
                                             ParseInfo* info,
                                             FunctionLiteral* literal);

ZonePtrList<const AstRawString>* Parser::PrepareWrappedArguments(
    Isolate* isolate, ParseInfo* info, Zone* zone) {
  DCHECK(parsing_on_main_thread_);
  DCHECK_NOT_NULL(isolate);
  DirectHandle<FixedArray> arguments =
      maybe_wrapped_arguments_.ToHandleChecked();
  int arguments_length = arguments->length();
  ZonePtrList<const AstRawString>* arguments_for_wrapped_function =
      zone->New<ZonePtrList<const AstRawString>>(arguments_length, zone);
  for (int i = 0; i < arguments_length; i++) {
    const AstRawString* argument_string = ast_value_factory()->GetString(
        Cast<String>(arguments->get(i)),
        SharedStringAccessGuardIfNeeded(isolate));
    arguments_for_wrapped_function->Add(argument_string, zone);
  }
  return arguments_for_wrapped_function;
}

void Parser::ParseWrapped(Isolate* isolate, ParseInfo* info,
                          ScopedPtrList<Statement>* body,
                          DeclarationScope* outer_scope, Zone* zone) {
  DCHECK(parsing_on_main_thread_);
  DCHECK(info->is_wrapped_as_function());
  ParsingModeScope parsing_mode(this, PARSE_EAGERLY);

  // Set function and block state for the outer eval scope.
  DCHECK(outer_scope->is_eval_scope());
  FunctionState function_state(&function_state_, &scope_, outer_scope);

  const AstRawString* function_name = nullptr;
  Scanner::Location location(0, 0);

  ZonePtrList<const AstRawString>* arguments_for_wrapped_function =
      PrepareWrappedArguments(isolate, info, zone);

  FunctionLiteral* function_literal =
      ParseFunctionLiteral(function_name, location, kSkipFunctionNameCheck,
                           FunctionKind::kNormalFunction, kNoSourcePosition,
                           FunctionSyntaxKind::kWrapped, LanguageMode::kSloppy,
                           arguments_for_wrapped_function);

  Statement* return_statement =
      factory()->NewReturnStatement(function_literal, kNoSourcePosition);
  body->Add(return_statement);
}

void Parser::ParseREPLProgram(ParseInfo* info, ScopedPtrList<Statement>* body,
                              DeclarationScope* scope) {
  // REPL scripts are handled nearly the same way as the body of an async
  // function. The difference is the value used to resolve the async
  // promise.
  // For a REPL script this is the completion value of the
  // script instead of the expression of some "return" statement. The
  // completion value of the script is obtained by manually invoking
  // the {Rewriter} which will return a VariableProxy referencing the
  // result.
  DCHECK(flags().is_repl_mode());
  this->scope()->SetLanguageMode(info->language_mode());
  PrepareGeneratorVariables();

  BlockT block = impl()->NullBlock();
  {
    StatementListT statements(pointer_buffer());
    ParseStatementList(&statements, Token::kEos);
    block = factory()->NewBlock(true, statements);
  }

  if (has_error()) return;

  std::optional<VariableProxy*> maybe_result =
      Rewriter::RewriteBody(info, scope, block->statements());
  Expression* result_value =
      (maybe_result && *maybe_result)
          ? static_cast<Expression*>(*maybe_result)
          : factory()->NewUndefinedLiteral(kNoSourcePosition);
  Expression* wrapped_result_value = WrapREPLResult(result_value);
  block->statements()->Add(factory()->NewAsyncReturnStatement(
                               wrapped_result_value, kNoSourcePosition),
                           zone());
  body->Add(block);
}

Expression* Parser::WrapREPLResult(Expression* value) {
  // REPL scripts additionally wrap the ".result" variable in an
  // object literal:
  //
  //     return %_AsyncFunctionResolve(
  //               .generator_object, {__proto__: null, .repl_result: .result});
  //
  // Should ".result" be a resolved promise itself, the async return
  // would chain the promises and return the resolve value instead of
  // the promise.

  Literal* property_name = factory()->NewStringLiteral(
      ast_value_factory()->dot_repl_result_string(), kNoSourcePosition);
  ObjectLiteralProperty* property =
      factory()->NewObjectLiteralProperty(property_name, value, true);

  Literal* proto_name = factory()->NewStringLiteral(
      ast_value_factory()->proto_string(), kNoSourcePosition);
  ObjectLiteralProperty* prototype = factory()->NewObjectLiteralProperty(
      proto_name, factory()->NewNullLiteral(kNoSourcePosition), false);

  ScopedPtrList<ObjectLiteralProperty> properties(pointer_buffer());
  properties.Add(property);
  properties.Add(prototype);
  return factory()->NewObjectLiteral(properties, false, kNoSourcePosition,
                                     false);
}

void Parser::ParseFunction(Isolate* isolate, ParseInfo* info,
                           DirectHandle<SharedFunctionInfo> shared_info) {
  // It's OK to use the Isolate & counters here, since this function is only
  // called in the main thread.
  DCHECK(parsing_on_main_thread_);
  RCS_SCOPE(runtime_call_stats_, RuntimeCallCounterId::kParseFunction);
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.ParseFunction");
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(v8_flags.log_function_events)) timer.Start();

  MaybeHandle<ScopeInfo> maybe_outer_scope_info;
  if (shared_info->HasOuterScopeInfo()) {
    maybe_outer_scope_info = handle(shared_info->GetOuterScopeInfo(), isolate);
  }
  int start_position = shared_info->StartPosition();
  int end_position = shared_info->EndPosition();

  DeserializeScopeChain(isolate, info, maybe_outer_scope_info,
                        Scope::DeserializationMode::kIncludingVariables);
  DCHECK_EQ(factory()->zone(), info->zone());

  DirectHandle<Script> script(Cast<Script>(shared_info->script()), isolate);
  if (shared_info->is_wrapped()) {
    maybe_wrapped_arguments_ = handle(script->wrapped_arguments(), isolate);
  }

  int function_literal_id = shared_info->function_literal_id();

  // Initialize parser state.
  info->set_function_name(ast_value_factory()->GetString(
      shared_info->Name(), SharedStringAccessGuardIfNeeded(isolate)));
  scanner_.Initialize();

  FunctionKind function_kind = flags().function_kind();
  FunctionLiteral* result;
  if (V8_UNLIKELY(IsClassMembersInitializerFunction(function_kind))) {
    // Reparsing of class member initializer functions has to be handled
    // specially because they require reparsing of the whole class body,
    // function start/end positions correspond to the class literal body
    // positions.
    result = ParseClassForMemberInitialization(
        function_kind, start_position, function_literal_id, end_position,
        info->function_name());
    info->set_max_info_id(GetLastInfoId());
  } else if (V8_UNLIKELY(shared_info->private_name_lookup_skips_outer_class() &&
                         original_scope_->is_class_scope())) {
    // If the function skips the outer class and the outer scope is a class, the
    // function is in heritage position. Otherwise the function scope's skip bit
    // will be correctly inherited from the outer scope.
    ClassScope::HeritageParsingScope heritage(original_scope_->AsClassScope());
    result = DoParseFunction(isolate, info, start_position, end_position,
                             function_literal_id, info->function_name());
  } else {
    result = DoParseFunction(isolate, info, start_position, end_position,
                             function_literal_id, info->function_name());
  }
  if (result == nullptr) return;
  MaybeProcessSourceRanges(info, result, stack_limit_);
  PostProcessParseResult(isolate, info, result);
  if (V8_UNLIKELY(v8_flags.log_function_events)) {
    double ms = timer.Elapsed().InMillisecondsF();
    // We should already be internalized by now, so the debug name will be
    // available.
    DeclarationScope* function_scope = result->scope();
    std::unique_ptr<char[]> function_name = shared_info->DebugNameCStr();
    LOG(isolate,
        FunctionEvent("parse-function", flags().script_id(), ms,
                      function_scope->start_position(),
                      function_scope->end_position(), function_name.get(),
                      strlen(function_name.get())));
  }
}

FunctionLiteral* Parser::DoParseFunction(Isolate* isolate, ParseInfo* info,
                                         int start_position, int end_position,
                                         int function_literal_id,
                                         const AstRawString* raw_name) {
  DCHECK_EQ(parsing_on_main_thread_, isolate != nullptr);
  DCHECK_NOT_NULL(raw_name);
  DCHECK_NULL(scope_);

  DCHECK(ast_value_factory());
  fni_.PushEnclosingName(raw_name);

  ResetInfoId();
  DCHECK_LT(0, function_literal_id);
  SkipInfos(function_literal_id - 1);

  ParsingModeScope parsing_mode(this, PARSE_EAGERLY);

  // Place holder for the result.
  FunctionLiteral* result = nullptr;

  {
    // Parse the function literal.
    Scope* outer = original_scope_;
    DeclarationScope* outer_function = outer->GetClosureScope();
    DCHECK(outer);
    FunctionState function_state(&function_state_, &scope_, outer_function);
    BlockState block_state(&scope_, outer);
    DCHECK(is_sloppy(outer->language_mode()) ||
           is_strict(info->language_mode()));
    FunctionKind kind = flags().function_kind();
    DCHECK_IMPLIES(IsConciseMethod(kind) || IsAccessorFunction(kind),
                   flags().function_syntax_kind() ==
                       FunctionSyntaxKind::kAccessorOrMethod);

    if (IsArrowFunction(kind)) {
      if (IsAsyncFunction(kind)) {
        DCHECK(!scanner()->HasLineTerminatorAfterNext());
        if (!Check(Token::kAsync)) {
          CHECK(stack_overflow());
          return nullptr;
        }
        if (!(peek_any_identifier() || peek() == Token::kLeftParen)) {
          CHECK(stack_overflow());
          return nullptr;
        }
      }

      CHECK_EQ(function_literal_id, GetNextInfoId());

      // TODO(adamk): We should construct this scope from the ScopeInfo.
      DeclarationScope* scope = NewFunctionScope(kind);
      scope->set_has_checked_syntax(true);

      // This bit only needs to be explicitly set because we're
      // not passing the ScopeInfo to the Scope constructor.
      SetLanguageMode(scope, info->language_mode());

      scope->set_start_position(start_position);
      ParserFormalParameters formals(scope);
      {
        ParameterDeclarationParsingScope formals_scope(this);
        // Parsing patterns as variable reference expression creates
        // NewUnresolved references in current scope. Enter arrow function
        // scope for formal parameter parsing.
        BlockState inner_block_state(&scope_, scope);
        if (Check(Token::kLeftParen)) {
          // '(' StrictFormalParameters ')'
          ParseFormalParameterList(&formals);
          Expect(Token::kRightParen);
        } else {
          // BindingIdentifier
          ParameterParsingScope parameter_parsing_scope(impl(), &formals);
          ParseFormalParameter(&formals);
          DeclareFormalParameters(&formals);
        }
        formals.duplicate_loc = formals_scope.duplicate_location();
      }

      // It doesn't really matter what value we pass here for
      // could_be_immediately_invoked since we already introduced an eager
      // compilation scope above.
      bool could_be_immediately_invoked = false;
      Expression* expression = ParseArrowFunctionLiteral(
          formals, function_literal_id, could_be_immediately_invoked);
      // Scanning must end at the same position that was recorded
      // previously. If not, parsing has been interrupted due to a stack
      // overflow, at which point the partially parsed arrow function
      // concise body happens to be a valid expression. This is a problem
      // only for arrow functions with single expression bodies, since there
      // is no end token such as "}" for normal functions.
      if (scanner()->location().end_pos == end_position) {
        // The pre-parser saw an arrow function here, so the full parser
        // must produce a FunctionLiteral.
        DCHECK(expression->IsFunctionLiteral());
        result = expression->AsFunctionLiteral();
      }
    } else if (IsDefaultConstructor(kind)) {
      DCHECK_EQ(scope(), outer);
      result = DefaultConstructor(raw_name, IsDerivedConstructor(kind),
                                  start_position);
    } else {
      ZonePtrList<const AstRawString>* arguments_for_wrapped_function =
          info->is_wrapped_as_function()
              ? PrepareWrappedArguments(isolate, info, zone())
              : nullptr;
      result = ParseFunctionLiteral(
          raw_name, Scanner::Location::invalid(), kSkipFunctionNameCheck, kind,
          kNoSourcePosition, flags().function_syntax_kind(),
          info->language_mode(), arguments_for_wrapped_function);
    }

    if (has_error()) return nullptr;
    result->set_requires_instance_members_initializer(
        flags().requires_instance_members_initializer());
    result->set_class_scope_has_private_brand(
        flags().class_scope_has_private_brand());
    result->set_has_static_private_methods_or_accessors(
        flags().has_static_private_methods_or_accessors());
  }

  info->set_max_info_id(GetLastInfoId());

  DCHECK_IMPLIES(result, function_literal_id == result->function_literal_id());
  return result;
}

FunctionLiteral* Parser::ParseClassForMemberInitialization(
    FunctionKind initalizer_kind, int initializer_pos, int initializer_id,
    int initializer_end_pos, const AstRawString* class_name) {
  // When the function is a class members initializer function, we record the
  // source range of the entire class body as its positions in its SFI, so at
  // this point the scanner should be rewound to the position of the class
  // token.
  DCHECK_EQ(peek_position(), initializer_pos);
  // Insert a FunctionState with the closest outer Declaration scope
  DeclarationScope* nearest_decl_scope = original_scope_->GetDeclarationScope();
  DCHECK_NOT_NULL(nearest_decl_scope);
  FunctionState function_state(&function_state_, &scope_, nearest_decl_scope);

  // We will reindex the function literals later.
  ResetInfoId();
  SkipInfos(initializer_id - 1);

  // We preparse the class members that are not fields with initializers
  // in order to collect the function literal ids.
  ParsingModeScope mode(this, PARSE_LAZILY);

  ExpressionParsingScope no_expression_scope(impl());

  // Reparse the whole class body to build member initializer functions.
  FunctionLiteral* initializer;
  {
    bool is_anonymous = IsEmptyIdentifier(class_name);
    BlockState block_state(&scope_, original_scope_);
    RaiseLanguageMode(LanguageMode::kStrict);

    BlockState object_literal_scope_state(&object_literal_scope_, nullptr);

    ClassInfo class_info(this);
    class_info.is_anonymous = is_anonymous;

    // Create an arbitrary non-Null expression to indicate that the class
    // extends something. Doing so unconditionally is fine because:
    //  - the fact whether the class extends something affects parsing of
    //    'super' expressions which cause parse-time SyntaxError if the class
    //    is not a derived one. However, all such errors must have been
    //    reported during initial parse of the class declaration.
    //  - "extends" clause affects class constructor's FunctionKind, but here
    //    we are interested only in the member initializer functions and thus
    //    we can ignore the constructor function details.
    //
    // Given all the above we can simplify things and for the purpose of class
    // member initializers reparsing don't bother propagating the existence of
    // the "extends" clause through scope serialization/deserialization.
    class_info.extends = factory()->NewNullLiteral(kNoSourcePosition);

    // Note that we don't recheck class_name for strict-reserved words or eval
    // because all such checks have already been done during initial paring and
    // respective SyntaxErrors must have been thrown if necessary.

    // Class initializers don't care about position of the class token.
    int class_token_pos = kNoSourcePosition;

#ifdef DEBUG
    scope()->MarkReparsingForClassInitializer();
#endif

    ParseClassLiteralBody(class_info, class_name, class_token_pos, Token::kEos);

    if (initalizer_kind == FunctionKind::kClassMembersInitializerFunction) {
      DCHECK_EQ(class_info.instance_members_function_id, initializer_id);
      initializer = CreateInstanceMembersInitializer(class_name, &class_info);
    } else {
      DCHECK_EQ(class_info.static_elements_function_id, initializer_id);
      initializer = CreateStaticElementsInitializer(class_name, &class_info);
    }
    initializer->scope()->TakeUnresolvedReferencesFromParent();
  }

  if (has_error()) return nullptr;

  DCHECK(IsClassMembersInitializerFunction(initalizer_kind));

  no_expression_scope.ValidateExpression();

  DCHECK_EQ(initializer->kind(), initalizer_kind);
  DCHECK_EQ(initializer->function_literal_id(), initializer_id);
  DCHECK_EQ(initializer->end_position(), initializer_end_pos);

  return initializer;
}

Statement* Parser::ParseModuleItem() {
  // ecma262/#prod-ModuleItem
  // ModuleItem :
  //    ImportDeclaration
  //    ExportDeclaration
  //    StatementListItem

  Token::Value next = peek();

  if (next == Token::kExport) {
    return ParseExportDeclaration();
  }

  if (next == Token::kImport) {
    // We must be careful not to parse a dynamic import expression as an import
    // declaration. Same for import.meta expressions.
    Token::Value peek_ahead = PeekAhead();
    if (peek_ahead != Token::kLeftParen && peek_ahead != Token::kPeriod) {
      ParseImportDeclaration();
      return factory()->EmptyStatement();
    }
  }

  return ParseStatementListItem();
}

void Parser::ParseModuleItemList(ScopedPtrList<Statement>* body) {
  // ecma262/#prod-Module
  // Module :
  //    ModuleBody?
  //
  // ecma262/#prod-ModuleItemList
  // ModuleBody :
  //    ModuleItem*

  DCHECK(scope()->is_module_scope());
  while (peek() != Token::kEos) {
    Statement* stat = ParseModuleItem();
    if (stat == nullptr) return;
    if (stat->IsEmptyStatement()) continue;
    body->Add(stat);
  }
}

const AstRawString* Parser::ParseModuleSpecifier() {
  // ModuleSpecifier :
  //    StringLiteral

  Expect(Token::kString);
  return GetSymbol();
}

ZoneChunkList<Parser::ExportClauseData>* Parser::ParseExportClause(
    Scanner::Location* reserved_loc,
    Scanner::Location* string_literal_local_name_loc) {
  // ExportClause :
  //   '{' '}'
  //   '{' ExportsList '}'
  //   '{' ExportsList ',' '}'
  //
  // ExportsList :
  //   ExportSpecifier
  //   ExportsList ',' ExportSpecifier
  //
  // ExportSpecifier :
  //   IdentifierName
  //   IdentifierName 'as' IdentifierName
  //   IdentifierName 'as' ModuleExportName
  //   ModuleExportName
  //   ModuleExportName 'as' ModuleExportName
  //
  // ModuleExportName :
  //   StringLiteral
  ZoneChunkList<ExportClauseData>* export_data =
      zone()->New<ZoneChunkList<ExportClauseData>>(zone());

  Expect(Token::kLeftBrace);

  Token::Value name_tok;
  while ((name_tok = peek()) != Token::kRightBrace) {
    const AstRawString* local_name = ParseExportSpecifierName();
    if (!string_literal_local_name_loc->IsValid() &&
        name_tok == Token::kString) {
      // Keep track of the first string literal local name exported for error
      // reporting. These must be followed by a 'from' clause.
      *string_literal_local_name_loc = scanner()->location();
    } else if (!reserved_loc->IsValid() &&
               !Token::IsValidIdentifier(name_tok, LanguageMode::kStrict, false,
                                         flags().is_module())) {
      // Keep track of the first reserved word encountered in case our
      // caller needs to report an error.
      *reserved_loc = scanner()->location();
    }
    const AstRawString* export_name;
    Scanner::Location location = scanner()->location();
    if (CheckContextualKeyword(ast_value_factory()->as_string())) {
      export_name = ParseExportSpecifierName();
      // Set the location to the whole "a as b" string, so that it makes sense
      // both for errors due to "a" and for errors due to "b".
      location.end_pos = scanner()->location().end_pos;
    } else {
      export_name = local_name;
    }
    export_data->push_back({export_name, local_name, location});
    if (peek() == Token::kRightBrace) break;
    if (V8_UNLIKELY(!Check(Token::kComma))) {
      ReportUnexpectedToken(Next());
      break;
    }
  }

  Expect(Token::kRightBrace);
  return export_data;
}

const AstRawString* Parser::ParseExportSpecifierName() {
  Token::Value next = Next();

  // IdentifierName
  if (V8_LIKELY(Token::IsPropertyName(next))) {
    return GetSymbol();
  }

  // ModuleExportName
  if (next == Token::kString) {
    const AstRawString* export_name = GetSymbol();
    if (V8_LIKELY(export_name->is_one_byte())) return export_name;
    if (!unibrow::Utf16::HasUnpairedSurrogate(
            reinterpret_cast<const uint16_t*>(export_name->raw_data()),
            export_name->length())) {
      return export_name;
    }
    ReportMessage(MessageTemplate::kInvalidModuleExportName);
    return EmptyIdentifierString();
  }

  ReportUnexpectedToken(next);
  return EmptyIdentifierString();
}

ZonePtrList<const Parser::NamedImport>* Parser::ParseNamedImports(int pos) {
  // NamedImports :
  //   '{' '}'
  //   '{' ImportsList '}'
  //   '{' ImportsList ',' '}'
  //
  // ImportsList :
  //   ImportSpecifier
  //   ImportsList ',' ImportSpecifier
  //
  // ImportSpecifier :
  //   BindingIdentifier
  //   IdentifierName 'as' BindingIdentifier
  //   ModuleExportName 'as' BindingIdentifier

  Expect(Token::kLeftBrace);

  auto result = zone()->New<ZonePtrList<const NamedImport>>(1, zone());
  while (peek() != Token::kRightBrace) {
    const AstRawString* import_name = ParseExportSpecifierName();
    const AstRawString* local_name = import_name;
    Scanner::Location location = scanner()->location();
    // In the presence of 'as', the left-side of the 'as' can
    // be any IdentifierName. But without 'as', it must be a valid
    // BindingIdentifier.
    if (CheckContextualKeyword(ast_value_factory()->as_string())) {
      local_name = ParsePropertyName();
    }
    if (!Token::IsValidIdentifier(scanner()->current_token(),
                                  LanguageMode::kStrict, false,
                                  flags().is_module())) {
      ReportMessage(MessageTemplate::kUnexpectedReserved);
      return nullptr;
    } else if (IsEvalOrArguments(local_name)) {
      ReportMessage(MessageTemplate::kStrictEvalArguments);
      return nullptr;
    }

    DeclareUnboundVariable(local_name, VariableMode::kConst,
                           kNeedsInitialization, position());

    NamedImport* import =
        zone()->New<NamedImport>(import_name, local_name, location);
    result->Add(import, zone());

    if (peek() == Token::kRightBrace) break;
    Expect(Token::kComma);
  }

  Expect(Token::kRightBrace);
  return result;
}

ImportAttributes* Parser::ParseImportWithOrAssertClause() {
  // WithClause :
  //    with '{' '}'
  //    with '{' WithEntries ','? '}'

  // WithEntries :
  //    LiteralPropertyName
  //    LiteralPropertyName ':' StringLiteral , WithEntries

  // (DEPRECATED)
  // AssertClause :
  //    assert '{' '}'
  //    assert '{' WithEntries ','? '}'

  auto import_attributes = zone()->New<ImportAttributes>(zone());

  if (v8_flags.harmony_import_attributes && Check(Token::kWith)) {
    // 'with' keyword consumed
  } else if (v8_flags.harmony_import_assertions &&
             !scanner()->HasLineTerminatorBeforeNext() &&
             CheckContextualKeyword(ast_value_factory()->assert_string())) {
    // The 'assert' contextual keyword is deprecated in favor of 'with', and we
    // need to investigate feasibility of unshipping.
    //
    // TODO(v8:13856): Remove once decision is made to unship 'assert' or keep.
    ++use_counts_[v8::Isolate::kImportAssertionDeprecatedSyntax];
    info_->pending_error_handler()->ReportWarningAt(
        position(), end_position(), MessageTemplate::kImportAssertDeprecated,
        "V8 v12.6 and Chrome 126");
  } else {
    return import_attributes;
  }

  Expect(Token::kLeftBrace);

  while (peek() != Token::kRightBrace) {
    const AstRawString* attribute_key =
        Check(Token::kString) ? GetSymbol() : ParsePropertyName();

    Scanner::Location location = scanner()->location();

    Expect(Token::kColon);
    Expect(Token::kString);

    const AstRawString* attribute_value = GetSymbol();

    // Set the location to the whole "key: 'value'"" string, so that it makes
    // sense both for errors due to the key and errors due to the value.
    location.end_pos = scanner()->location().end_pos;

    auto result = import_attributes->insert(std::make_pair(
        attribute_key, std::make_pair(attribute_value, location)));
    if (!result.second) {
      // It is a syntax error if two WithEntries have the same key.
      ReportMessageAt(location, MessageTemplate::kImportAssertionDuplicateKey,
                      attribute_key);
      break;
    }

    if (peek() == Token::kRightBrace) break;
    if (V8_UNLIKELY(!Check(Token::kComma))) {
      ReportUnexpectedToken(Next());
      break;
    }
  }

  Expect(Token::kRightBrace);

  return import_attributes;
}

void Parser::ParseImportDeclaration() {
  // ImportDeclaration :
  //   'import' ImportClause 'from' ModuleSpecifier ';'
  //   'import' ModuleSpecifier ';'
  //   'import' ImportClause 'from' ModuleSpecifier [no LineTerminator here]
  //       AssertClause ';'
  //   'import' ModuleSpecifier [no LineTerminator here] AssertClause';'
  //   'import' 'source' ImportedBinding 'from' ModuleSpecifier ';'
  //
  // ImportClause :
  //   ImportedDefaultBinding
  //   NameSpaceImport
  //   NamedImports
  //   ImportedDefaultBinding ',' NameSpaceImport
  //   ImportedDefaultBinding ',' NamedImports
  //
  // NameSpaceImport :
  //   '*' 'as' ImportedBinding

  int pos = peek_position();
  Expect(Token::kImport);

  Token::Value tok = peek();

  // 'import' ModuleSpecifier ';'
  if (tok == Token::kString) {
    Scanner::Location specifier_loc = scanner()->peek_location();
    const AstRawString* module_specifier = ParseModuleSpecifier();
    const ImportAttributes* import_attributes = ParseImportWithOrAssertClause();
    ExpectSemicolon();
    module()->AddEmptyImport(module_specifier, import_attributes, specifier_loc,
                             zone());
    return;
  }

  // Parse ImportedDefaultBinding or 'source' ImportedBinding if present.
  const AstRawString* import_default_binding = nullptr;
  Scanner::Location import_default_binding_loc;
  ModuleImportPhase import_phase = ModuleImportPhase::kEvaluation;
  if (tok != Token::kMul && tok != Token::kLeftBrace) {
    if (v8_flags.js_source_phase_imports &&
        PeekContextualKeyword(ast_value_factory()->source_string()) &&
        PeekAhead() == Token::kIdentifier &&
        PeekAheadAhead() == Token::kIdentifier) {
      Consume(Token::kIdentifier);
      import_phase = ModuleImportPhase::kSource;
    }
    import_default_binding = ParseNonRestrictedIdentifier();
    import_default_binding_loc = scanner()->location();
    DeclareUnboundVariable(import_default_binding, VariableMode::kConst,
                           kNeedsInitialization, pos);
  }

  // Parse NameSpaceImport or NamedImports if present.
  const AstRawString* module_namespace_binding = nullptr;
  Scanner::Location module_namespace_binding_loc;
  const ZonePtrList<const NamedImport>* named_imports = nullptr;
  if (import_phase == ModuleImportPhase::kEvaluation &&
      (import_default_binding == nullptr || Check(Token::kComma))) {
    switch (peek()) {
      case Token::kMul: {
        Consume(Token::kMul);
        ExpectContextualKeyword(ast_value_factory()->as_string());
        module_namespace_binding = ParseNonRestrictedIdentifier();
        module_namespace_binding_loc = scanner()->location();
        DeclareUnboundVariable(module_namespace_binding, VariableMode::kConst,
                               kCreatedInitialized, pos);
        break;
      }

      case Token::kLeftBrace:
        named_imports = ParseNamedImports(pos);
        break;

      default:
        ReportUnexpectedToken(scanner()->current_token());
        return;
    }
  }

  ExpectContextualKeyword(ast_value_factory()->from_string());
  Scanner::Location specifier_loc = scanner()->peek_location();
  const AstRawString* module_specifier = ParseModuleSpecifier();
  // TODO(42204365): Enable import attributes with source phase import once
  // specified.
  const ImportAttributes* import_attributes =
      import_phase == ModuleImportPhase::kEvaluation
          ? ParseImportWithOrAssertClause()
          : zone()->New<ImportAttributes>(zone());
  ExpectSemicolon();

  // Now that we have all the information, we can make the appropriate
  // declarations.

  // TODO(neis): Would prefer to call DeclareVariable for each case below rather
  // than above and in ParseNamedImports, but then a possible error message
  // would point to the wrong location.  Maybe have a DeclareAt version of
  // Declare that takes a location?

  if (module_namespace_binding != nullptr) {
    DCHECK_EQ(ModuleImportPhase::kEvaluation, import_phase);
    module()->AddStarImport(module_namespace_binding, module_specifier,
                            import_attributes, module_namespace_binding_loc,
                            specifier_loc, zone());
  }

  if (import_default_binding != nullptr) {
    DCHECK_IMPLIES(import_phase == ModuleImportPhase::kSource,
                   v8_flags.js_source_phase_imports);
    module()->AddImport(ast_value_factory()->default_string(),
                        import_default_binding, module_specifier, import_phase,
                        import_attributes, import_default_binding_loc,
                        specifier_loc, zone());
  }

  if (named_imports != nullptr) {
    DCHECK_EQ(ModuleImportPhase::kEvaluation, import_phase);
    if (named_imports->length() == 0) {
      module()->AddEmptyImport(module_specifier, import_attributes,
                               specifier_loc, zone());
    } else {
      for (const NamedImport* import : *named_imports) {
        module()->AddImport(import->import_name, import->local_name,
                            module_specifier, import_phase, import_attributes,
                            import->location, specifier_loc, zone());
      }
    }
  }
}

Statement* Parser::ParseExportDefault() {
  //  Supports the following productions, starting after the 'default' token:
  //    'export' 'default' HoistableDeclaration
  //    'export' 'default' ClassDeclaration
  //    'export' 'default' AssignmentExpression[In] ';'

  Expect(Token::kDefault);
  Scanner::Location default_loc = scanner()->location();

  ZonePtrList<const AstRawString> local_names(1, zone());
  Statement* result = nullptr;
  switch (peek()) {
    case Token::kFunction:
      result = ParseHoistableDeclaration(&local_names, true);
      break;

    case Token::kClass:
      Consume(Token::kClass);
      result = ParseClassDeclaration(&local_names, true);
      break;

    case Token::kAsync:
      if (PeekAhead() == Token::kFunction &&
          !scanner()->HasLineTerminatorAfterNext()) {
        Consume(Token::kAsync);
        result = ParseAsyncFunctionDeclaration(&local_names, true);
        break;
      }
      [[fallthrough]];

    default: {
      int pos = position();
      AcceptINScope scope(this, true);
      Expression* value = ParseAssignmentExpression();
      SetFunctionName(value, ast_value_factory()->default_string());

      const AstRawString* local_name =
          ast_value_factory()->dot_default_string();
      local_names.Add(local_name, zone());

      // It's fine to declare this as VariableMode::kConst because the user has
      // no way of writing to it.
      VariableProxy* proxy =
          DeclareBoundVariable(local_name, VariableMode::kConst, pos);
      proxy->var()->set_initializer_position(position());

      Assignment* assignment = factory()->NewAssignment(
          Token::kInit, proxy, value, kNoSourcePosition);
      result = IgnoreCompletion(
          factory()->NewExpressionStatement(assignment, kNoSourcePosition));

      ExpectSemicolon();
      break;
    }
  }

  if (result != nullptr) {
    DCHECK_EQ(local_names.length(), 1);
    module()->AddExport(local_names.first(),
                        ast_value_factory()->default_string(), default_loc,
                        zone());
  }

  return result;
}

const AstRawString* Parser::NextInternalNamespaceExportName() {
  const char* prefix = ".ns-export";
  std::string s(prefix);
  s.append(std::to_string(number_of_named_namespace_exports_++));
  return ast_value_factory()->GetOneByteString(s.c_str());
}

void Parser::ParseExportStar() {
  int pos = position();
  Consume(Token::kMul);

  if (!PeekContextualKeyword(ast_value_factory()->as_string())) {
    // 'export' '*' 'from' ModuleSpecifier ';'
    Scanner::Location loc = scanner()->location();
    ExpectContextualKeyword(ast_value_factory()->from_string());
    Scanner::Location specifier_loc = scanner()->peek_location();
    const AstRawString* module_specifier = ParseModuleSpecifier();
    const ImportAttributes* import_attributes = ParseImportWithOrAssertClause();
    ExpectSemicolon();
    module()->AddStarExport(module_specifier, import_attributes, loc,
                            specifier_loc, zone());
    return;
  }

  // 'export' '*' 'as' IdentifierName 'from' ModuleSpecifier ';'
  //
  // Desugaring:
  //   export * as x from "...";
  // ~>
  //   import * as .x from "..."; export {.x as x};
  //
  // Note that the desugared internal namespace export name (.x above) will
  // never conflict with a string literal export name, as literal string export
  // names in local name positions (i.e. left of 'as' or in a clause without
  // 'as') are disallowed without a following 'from' clause.

  ExpectContextualKeyword(ast_value_factory()->as_string());
  const AstRawString* export_name = ParseExportSpecifierName();
  Scanner::Location export_name_loc = scanner()->location();
  const AstRawString* local_name = NextInternalNamespaceExportName();
  Scanner::Location local_name_loc = Scanner::Location::invalid();
  DeclareUnboundVariable(local_name, VariableMode::kConst, kCreatedInitialized,
                         pos);

  ExpectContextualKeyword(ast_value_factory()->from_string());
  Scanner::Location specifier_loc = scanner()->peek_location();
  const AstRawString* module_specifier = ParseModuleSpecifier();
  const ImportAttributes* import_attributes = ParseImportWithOrAssertClause();
  ExpectSemicolon();

  module()->AddStarImport(local_name, module_specifier, import_attributes,
                          local_name_loc, specifier_loc, zone());
  module()->AddExport(local_name, export_name, export_name_loc, zone());
}

Statement* Parser::ParseExportDeclaration() {
  // ExportDeclaration:
  //    'export' '*' 'from' ModuleSpecifier ';'
  //    'export' '*' 'from' ModuleSpecifier [no LineTerminator here]
  //        AssertClause ';'
  //    'export' '*' 'as' IdentifierName 'from' ModuleSpecifier ';'
  //    'export' '*' 'as' IdentifierName 'from' ModuleSpecifier
  //        [no LineTerminator here] AssertClause ';'
  //    'export' '*' 'as' ModuleExportName 'from' ModuleSpecifier ';'
  //    'export' '*' 'as' ModuleExportName 'from' ModuleSpecifier ';'
  //        [no LineTerminator here] AssertClause ';'
  //    'export' ExportClause ('from' ModuleSpecifier)? ';'
  //    'export' ExportClause ('from' ModuleSpecifier [no LineTerminator here]
  //        AssertClause)? ';'
  //    'export' VariableStatement
  //    'export' Declaration
  //    'export' 'default' ... (handled in ParseExportDefault)
  //
  // ModuleExportName :
  //   StringLiteral

  Expect(Token::kExport);
  Statement* result = nullptr;
  ZonePtrList<const AstRawString> names(1, zone());
  Scanner::Location loc = scanner()->peek_location();
  switch (peek()) {
    case Token::kDefault:
      return ParseExportDefault();

    case Token::kMul:
      ParseExportStar();
      return factory()->EmptyStatement();

    case Token::kLeftBrace: {
      // There are two cases here:
      //
      // 'export' ExportClause ';'
      // and
      // 'export' ExportClause FromClause ';'
      //
      // In the first case, the exported identifiers in ExportClause must
      // not be reserved words, while in the latter they may be. We
      // pass in a location that gets filled with the first reserved word
      // encountered, and then throw a SyntaxError if we are in the
      // non-FromClause case.
      Scanner::Location reserved_loc = Scanner::Location::invalid();
      Scanner::Location string_literal_local_name_loc =
          Scanner::Location::invalid();
      ZoneChunkList<ExportClauseData>* export_data =
          ParseExportClause(&reserved_loc, &string_literal_local_name_loc);
      if (CheckContextualKeyword(ast_value_factory()->from_string())) {
        Scanner::Location specifier_loc = scanner()->peek_location();
        const AstRawString* module_specifier = ParseModuleSpecifier();
        const ImportAttributes* import_attributes =
            ParseImportWithOrAssertClause();
        ExpectSemicolon();

        if (export_data->empty()) {
          module()->AddEmptyImport(module_specifier, import_attributes,
                                   specifier_loc, zone());
        } else {
          for (const ExportClauseData& data : *export_data) {
            module()->AddExport(data.local_name, data.export_name,
                                module_specifier, import_attributes,
                                data.location, specifier_loc, zone());
          }
        }
      } else {
        if (reserved_loc.IsValid()) {
          // No FromClause, so reserved words are invalid in ExportClause.
          ReportMessageAt(reserved_loc, MessageTemplate::kUnexpectedReserved);
          return nullptr;
        } else if (string_literal_local_name_loc.IsValid()) {
          ReportMessageAt(string_literal_local_name_loc,
                          MessageTemplate::kModuleExportNameWithoutFromClause);
          return nullptr;
        }

        ExpectSemicolon();

        for (const ExportClauseData& data : *export_data) {
          module()->AddExport(data.local_name, data.export_name, data.location,
                              zone());
        }
      }
      return factory()->EmptyStatement();
    }

    case Token::kFunction:
      result = ParseHoistableDeclaration(&names, false);
      break;

    case Token::kClass:
      Consume(Token::kClass);
      result = ParseClassDeclaration(&names, false);
      break;

    case Token::kVar:
    case Token::kLet:
    case Token::kConst:
      result = ParseVariableStatement(kStatementListItem, &names);
      break;

    case Token::kAsync:
      Consume(Token::kAsync);
      if (peek() == Token::kFunction &&
          !scanner()->HasLineTerminatorBeforeNext()) {
        result = ParseAsyncFunctionDeclaration(&names, false);
        break;
      }
      [[fallthrough]];

    default:
      ReportUnexpectedToken(scanner()->current_token());
      return nullptr;
  }
  loc.end_pos = scanner()->location().end_pos;

  SourceTextModuleDescriptor* descriptor = module();
  for (const AstRawString* name : names) {
    descriptor->AddExport(name, name, loc, zone());
  }

  return result;
}

void Parser::DeclareUnboundVariable(const AstRawString* name, VariableMode mode,
                                    InitializationFlag init, int pos) {
  bool was_added;
  Variable* var = DeclareVariable(name, NORMAL_VARIABLE, mode, init, scope(),
                                  &was_added, pos, end_position());
  // The variable will be added to the declarations list, but since we are not
  // binding it to anything, we can simply ignore it here.
  USE(var);
}

VariableProxy* Parser::DeclareBoundVariable(const AstRawString* name,
                                            VariableMode mode, int pos) {
  DCHECK_NOT_NULL(name);
  VariableProxy* proxy =
      factory()->NewVariableProxy(name, NORMAL_VARIABLE, position());
  bool was_added;
  Variable* var = DeclareVariable(name, NORMAL_VARIABLE, mode,
                                  Variable::DefaultInitializationFlag(mode),
                                  scope(), &was_added, pos, end_position());
  proxy->BindTo(var);
  return proxy;
}

void Parser::DeclareAndBindVariable(VariableProxy* proxy, VariableKind kind,
                                    VariableMode mode, Scope* scope,
                                    bool* was_added, int initializer_position) {
  Variable* var = DeclareVariable(
      proxy->raw_name(), kind, mode, Variable::DefaultInitializationFlag(mode),
      scope, was_added, proxy->position(), kNoSourcePosition);
  var->set_initializer_position(initializer_position);
  proxy->BindTo(var);
}

Variable* Parser::DeclareVariable(const AstRawString* name, VariableKind kind,
                                  VariableMode mode, InitializationFlag init,
                                  Scope* scope, bool* was_added, int begin,
                                  int end) {
  Declaration* declaration;
  if (mode == VariableMode::kVar && !scope->is_declaration_scope()) {
    DCHECK(scope->is_block_scope() || scope->is_with_scope());
    declaration = factory()->NewNestedVariableDeclaration(scope, begin);
  } else {
    declaration = factory()->NewVariableDeclaration(begin);
  }
  Declare(declaration, name, kind, mode, init, scope, was_added, begin, end);
  return declaration->var();
}

void Parser::Declare(Declaration* declaration, const AstRawString* name,
                     VariableKind variable_kind, VariableMode mode,
                     InitializationFlag init, Scope* scope, bool* was_added,
                     int var_begin_pos, int var_end_pos) {
  bool local_ok = true;
  bool sloppy_mode_block_scope_function_redefinition = false;
  scope->DeclareVariable(
      declaration, name, var_begin_pos, mode, variable_kind, init, was_added,
      &sloppy_mode_block_scope_function_redefinition, &local_ok);
  if (!local_ok) {
    // If we only have the start position of a proxy, we can't highlight the
    // whole variable name.  Pretend its length is 1 so that we highlight at
    // least the first character.
    Scanner::Location loc(var_begin_pos, var_end_pos != kNoSourcePosition
                                             ? var_end_pos
                                             : var_begin_pos + 1);
    if (variable_kind == PARAMETER_VARIABLE) {
      ReportMessageAt(loc, MessageTemplate::kParamDupe);
    } else {
      ReportMessageAt(loc, MessageTemplate::kVarRedeclaration,
                      declaration->var()->raw_name());
    }
  } else if (sloppy_mode_block_scope_function_redefinition) {
    ++use_counts_[v8::Isolate::kSloppyModeBlockScopedFunctionRedefinition];
  }
}

Statement* Parser::BuildInitializationBlock(
    DeclarationParsingResult* parsing_result) {
  ScopedPtrList<Statement> statements(pointer_buffer());
  for (const auto& declaration : parsing_result->declarations) {
    if (!declaration.initializer) continue;
    InitializeVariables(&statements, parsing_result->descriptor.kind,
                        &declaration);
  }
  return factory()->NewBlock(true, statements);
}

Statement* Parser::DeclareFunction(const AstRawString* variable_name,
                                   FunctionLiteral* function, VariableMode mode,
                                   VariableKind kind, int beg_pos, int end_pos,
                                   ZonePtrList<const AstRawString>* names) {
  Declaration* declaration =
      factory()->NewFunctionDeclaration(function, beg_pos);
  bool was_added;
  Declare(declaration, variable_name, kind, mode, kCreatedInitialized, scope(),
          &was_added, beg_pos);
  if (info()->flags().coverage_enabled()) {
    // Force the function to be allocated when collecting source coverage, so
    // that even dead functions get source coverage data.
    declaration->var()->set_is_used();
  }
  if (names) names->Add(variable_name, zone());
  if (kind == SLOPPY_BLOCK_FUNCTION_VARIABLE) {
    Token::Value init =
        loop_nesting_depth() > 0 ? Token::kAssign : Token::kInit;
    SloppyBlockFunctionStatement* statement =
        factory()->NewSloppyBlockFunctionStatement(end_pos, declaration->var(),
                                                   init);
    GetDeclarationScope()->DeclareSloppyBlockFunction(statement);
    return statement;
  }
  return factory()->EmptyStatement();
}

Statement* Parser::DeclareClass(const AstRawString* variable_name,
                                Expression* value,
                                ZonePtrList<const AstRawString>* names,
                                int class_token_pos, int end_pos) {
  VariableProxy* proxy =
      DeclareBoundVariable(variable_name, VariableMode::kLet, class_token_pos);
  proxy->var()->set_initializer_position(end_pos);
  if (names) names->Add(variable_name, zone());

  Assignment* assignment =
      factory()->NewAssignment(Token::kInit, proxy, value, class_token_pos);
  return IgnoreCompletion(
      factory()->NewExpressionStatement(assignment, kNoSourcePosition));
}

Statement* Parser::DeclareNative(const AstRawString* name, int pos) {
  // Make sure that the function containing the native declaration
  // isn't lazily compiled. The extension structures are only
  // accessible while parsing the first time not when reparsing
  // because of lazy compilation.
  GetClosureScope()->ForceEagerCompilation();

  // TODO(1240846): It's weird that native function declarations are
  // introduced dynamically when we meet their declarations, whereas
  // other functions are set up when entering the surrounding scope.
  VariableProxy* proxy = DeclareBoundVariable(name, VariableMode::kVar, pos);
  NativeFunctionLiteral* lit =
      factory()->NewNativeFunctionLiteral(name, extension(), kNoSourcePosition);
  return factory()->NewExpressionStatement(
      factory()->NewAssignment(Token::kInit, proxy, lit, kNoSourcePosition),
      pos);
}

Block* Parser::IgnoreCompletion(Statement* statement) {
  Block* block = factory()->NewBlock(1, true);
  block->statements()->Add(statement, zone());
  return block;
}

Statement* Parser::RewriteSwitchStatement(SwitchStatement* switch_statement,
                                          Scope* scope) {
  // In order to get the CaseClauses to execute in their own lexical scope,
  // but without requiring downstream code to have special scope handling
  // code for switch statements, desugar into blocks as follows:
  // {  // To group the statements--harmless to evaluate Expression in scope
  //   .tag_variable = Expression;
  //   {  // To give CaseClauses a scope
  //     switch (.tag_variable) { CaseClause* }
  //   }
  // }
  DCHECK_NOT_NULL(scope);
  DCHECK(scope->is_block_scope());
  DCHECK_GE(switch_statement->position(), scope->start_position());
  DCHECK_LT(switch_statement->position(), scope->end_position());

  Block* switch_block = factory()->NewBlock(2, false);

  Expression* tag = switch_statement->tag();
  Variable* tag_variable =
      NewTemporary(ast_value_factory()->dot_switch_tag_string());
  Assignment* tag_assign = factory()->NewAssignment(
      Token::kAssign, factory()->NewVariableProxy(tag_variable), tag,
      tag->position());
  // Wrap with IgnoreCompletion so the tag isn't returned as the completion
  // value, in case the switch statements don't have a value.
  Statement* tag_statement = IgnoreCompletion(
      factory()->NewExpressionStatement(tag_assign, kNoSourcePosition));
  switch_block->statements()->Add(tag_statement, zone());

  switch_statement->set_tag(factory()->NewVariableProxy(tag_variable));
  Block* cases_block = factory()->NewBlock(1, false);
  cases_block->statements()->Add(switch_statement, zone());
  cases_block->set_scope(scope);
  switch_block->statements()->Add(cases_block, zone());
  return switch_block;
}

void Parser::InitializeVariables(
    ScopedPtrList<Statement>* statements, VariableKind kind,
    const DeclarationParsingResult::Declaration* declaration) {
  if (has_error()) return;

  DCHECK_NOT_NULL(declaration->initializer);

  int pos = declaration->value_beg_pos;
  if (pos == kNoSourcePosition) {
    pos = declaration->initializer->position();
  }
  Assignment* assignment = factory()->NewAssignment(
      Token::kInit, declaration->pattern, declaration->initializer, pos);
  statements->Add(factory()->NewExpressionStatement(assignment, pos));
}

Block* Parser::RewriteCatchPattern(CatchInfo* catch_info) {
  DCHECK_NOT_NULL(catch_info->pattern);

  DeclarationParsingResult::Declaration decl(
      catch_info->pattern, factory()->NewVariableProxy(catch_info->variable));

  ScopedPtrList<Statement> init_statements(pointer_buffer());
  InitializeVariables(&init_statements, NORMAL_VARIABLE, &decl);
  return factory()->NewBlock(true, init_statements);
}

void Parser::ReportVarRedeclarationIn(const AstRawString* name, Scope* scope) {
  for (Declaration* decl : *scope->declarations()) {
    if (decl->var()->raw_name() == name) {
      int position = decl->position();
      Scanner::Location location =
          position == kNoSourcePosition
              ? Scanner::Location::invalid()
              : Scanner::Location(position, position + name->length());
      ReportMessageAt(location, MessageTemplate::kVarRedeclaration, name);
      return;
    }
  }
  UNREACHABLE();
}

Statement* Parser::RewriteTryStatement(Block* try_block, Block* catch_block,
                                       const SourceRange& catch_range,
                                       Block* finally_block,
                                       const SourceRange& finally_range,
                                       const CatchInfo& catch_info, int pos) {
  // Simplify the AST nodes by converting:
  //   'try B0 catch B1 finally B2'
  // to:
  //   'try { try B0 catch B1 } finally B2'

  if (catch_block != nullptr && finally_block != nullptr) {
    // If we have both, create an inner try/catch.
    TryCatchStatement* statement;
    statement = factory()->NewTryCatchStatement(try_block, catch_info.scope,
                                                catch_block, kNoSourcePosition);
    RecordTryCatchStatementSourceRange(statement, catch_range);

    try_block = factory()->NewBlock(1, false);
    try_block->statements()->Add(statement, zone());
    catch_block = nullptr;  // Clear to indicate it's been handled.
  }

  if (catch_block != nullptr) {
    DCHECK_NULL(finally_block);
    TryCatchStatement* stmt = factory()->NewTryCatchStatement(
        try_block, catch_info.scope, catch_block, pos);
    RecordTryCatchStatementSourceRange(stmt, catch_range);
    return stmt;
  } else {
    DCHECK_NOT_NULL(finally_block);
    TryFinallyStatement* stmt =
        factory()->NewTryFinallyStatement(try_block, finally_block, pos);
    RecordTryFinallyStatementSourceRange(stmt, finally_range);
    return stmt;
  }
}

void Parser::ParseGeneratorFunctionBody(int pos, FunctionKind kind,
                                        ScopedPtrList<Statement>* body) {
  // For ES6 Generators, we just prepend the initial yield.
  Expression* initial_yield = BuildInitialYield(pos, kind);
  body->Add(
      factory()->NewExpressionStatement(initial_yield, kNoSourcePosition));
  ParseStatementList(body, Token::kRightBrace);
}

void Parser::ParseAndRewriteAsyncGeneratorFunctionBody(
    int pos, FunctionKind kind, ScopedPtrList<Statement>* body) {
  // For ES2017 Async Generators, we produce:
  //
  // try {
  //   InitialYield;
  //   ...body...;
  //   // fall through to the implicit return after the try-finally
  // } catch (.catch) {
  //   %AsyncGeneratorReject(generator, .catch);
  // } finally {
  //   %_GeneratorClose(generator);
  // }
  //
  // - InitialYield yields the actual generator object.
  // - Any return statement inside the body will have its argument wrapped
  //   in an iterator result object with a "done" property set to `true`.
  // - If the generator terminates for whatever reason, we must close it.
  //   Hence the finally clause.
  // - BytecodeGenerator performs special handling for ReturnStatements in
  //   async generator functions, resolving the appropriate Promise with an
  //   "done" iterator result object containing a Promise-unwrapped value.
  DCHECK(IsAsyncGeneratorFunction(kind));

  Block* try_block;
  {
    ScopedPtrList<Statement> statements(pointer_buffer());
    Expression* initial_yield = BuildInitialYield(pos, kind);
    statements.Add(
        factory()->NewExpressionStatement(initial_yield, kNoSourcePosition));
    ParseStatementList(&statements, Token::kRightBrace);
    // Since the whole body is wrapped in a try-catch, make the implicit
    // end-of-function return explicit to ensure BytecodeGenerator's special
    // handling for ReturnStatements in async generators applies.
    statements.Add(factory()->NewSyntheticAsyncReturnStatement(
        factory()->NewUndefinedLiteral(kNoSourcePosition), kNoSourcePosition));

    // Don't create iterator result for async generators, as the resume methods
    // will create it.
    try_block = factory()->NewBlock(false, statements);
  }

  // For AsyncGenerators, a top-level catch block will reject the Promise.
  Scope* catch_scope = NewHiddenCatchScope();

  Block* catch_block;
  {
    ScopedPtrList<Expression> reject_args(pointer_buffer());
    reject_args.Add(factory()->NewVariableProxy(
        function_state_->scope()->generator_object_var()));
    reject_args.Add(factory()->NewVariableProxy(catch_scope->catch_variable()));

    Expression* reject_call = factory()->NewCallRuntime(
        Runtime::kInlineAsyncGeneratorReject, reject_args, kNoSourcePosition);
    catch_block = IgnoreCompletion(factory()->NewReturnStatement(
        reject_call, kNoSourcePosition, kNoSourcePosition));
  }

  {
    ScopedPtrList<Statement> statements(pointer_buffer());
    TryStatement* try_catch = factory()->NewTryCatchStatementForAsyncAwait(
        try_block, catch_scope, catch_block, kNoSourcePosition);
    statements.Add(try_catch);
    try_block = factory()->NewBlock(false, statements);
  }

  Expression* close_call;
  {
    ScopedPtrList<Expression> close_args(pointer_buffer());
    VariableProxy* call_proxy = factory()->NewVariableProxy(
        function_state_->scope()->generator_object_var());
    close_args.Add(call_proxy);
    close_call = factory()->NewCallRuntime(Runtime::kInlineGeneratorClose,
                                           close_args, kNoSourcePosition);
  }

  Block* finally_block;
  {
    ScopedPtrList<Statement> statements(pointer_buffer());
    statements.Add(
        factory()->NewExpressionStatement(close_call, kNoSourcePosition));
    finally_block = factory()->NewBlock(false, statements);
  }

  body->Add(factory()->NewTryFinallyStatement(try_block, finally_block,
                                              kNoSourcePosition));
}

void Parser::DeclareFunctionNameVar(const AstRawString* function_name,
                                    FunctionSyntaxKind function_syntax_kind,
                                    DeclarationScope* function_scope) {
  if (function_syntax_kind == FunctionSyntaxKind::kNamedExpression &&
      function_scope->LookupLocal(function_name) == nullptr) {
    DCHECK_EQ(function_scope, scope());
    function_scope->DeclareFunctionVar(function_name);
  }
}

// Special case for legacy for
//
//    for (var x = initializer in enumerable) body
//
// An initialization block of the form
//
//    {
//      x = initializer;
//    }
//
// is returned in this case.  It has reserved space for two statements,
// so that (later on during parsing), the equivalent of
//
//   for (x in enumerable) body
//
// is added as a second statement to it.
Block* Parser::RewriteForVarInLegacy(const ForInfo& for_info) {
  const DeclarationParsingResult::Declaration& decl =
      for_info.parsing_result.declarations[0];
  if (!IsLexicalVariableMode(for_info.parsing_result.descriptor.mode) &&
      decl.initializer != nullptr && decl.pattern->IsVariableProxy()) {
    ++use_counts_[v8::Isolate::kForInInitializer];
    const AstRawString* name = decl.pattern->AsVariableProxy()->raw_name();
    VariableProxy* single_var = NewUnresolved(name);
    Block* init_block = factory()->NewBlock(2, true);
    init_block->statements()->Add(
        factory()->NewExpressionStatement(
            factory()->NewAssignment(Token::kAssign, single_var,
                                     decl.initializer, decl.value_beg_pos),
            kNoSourcePosition),
        zone());
    return init_block;
  }
  return nullptr;
}

// Rewrite a for-in/of statement of the form
//
//   for (let/const/var x in/of e) b
//
// into
//
//   {
//     var temp;
//     for (temp in/of e) {
//       let/const/var x = temp;
//       b;
//     }
//     let x;  // for TDZ
//   }
void Parser::DesugarBindingInForEachStatement(ForInfo* for_info,
                                              Block** body_block,
                                              Expression** each_variable) {
  DCHECK_EQ(1, for_info->parsing_result.declarations.size());
  DeclarationParsingResult::Declaration& decl =
      for_info->parsing_result.declarations[0];
  Variable* temp = NewTemporary(ast_value_factory()->dot_for_string());
  ScopedPtrList<Statement> each_initialization_statements(pointer_buffer());
  DCHECK_IMPLIES(!has_error(), decl.pattern != nullptr);
  decl.initializer = factory()->NewVariableProxy(temp, for_info->position);
  InitializeVariables(&each_initialization_statements, NORMAL_VARIABLE, &decl);

  *body_block = factory()->NewBlock(3, false);
  (*body_block)
      ->statements()
      ->Add(factory()->NewBlock(true, each_initialization_statements), zone());
  *each_variable = factory()->NewVariableProxy(temp, for_info->position);
}

// Create a TDZ for any lexically-bound names in for in/of statements.
Block* Parser::CreateForEachStatementTDZ(Block* init_block,
                                         const ForInfo& for_info) {
  if (IsLexicalVariableMode(for_info.parsing_result.descriptor.mode)) {
    DCHECK_NULL(init_block);

    init_block = factory()->NewBlock(1, false);

    for (const AstRawString* bound_name : for_info.bound_names) {
      // TODO(adamk): This needs to be some sort of special
      // INTERNAL variable that's invisible to the debugger
      // but visible to everything else.
      VariableProxy* tdz_proxy = DeclareBoundVariable(
          bound_name, VariableMode::kLet, kNoSourcePosition);
      tdz_proxy->var()->set_initializer_position(position());
    }
  }
  return init_block;
}

Statement* Parser::DesugarLexicalBindingsInForStatement(
    ForStatement* loop, Statement* init, Expression* cond, Statement* next,
    Statement* body, Scope* inner_scope, const ForInfo& for_info) {
  // ES6 13.7.4.8 specifies that on each loop iteration the let variables are
  // copied into a new environment.  Moreover, the "next" statement must be
  // evaluated not in the environment of the just completed iteration but in
  // that of the upcoming one.  We achieve this with the following desugaring.
  // Extra care is needed to preserve the completion value of the original loop.
  //
  // We are given a for statement of the form
  //
  //  labels: for (let/const x = i; cond; next) body
  //
  // and rewrite it as follows.  Here we write {{ ... }} for init-blocks, ie.,
  // blocks whose ignore_completion_value_ flag is set.
  //
  //  {
  //    let/const x = i;
  //    temp_x = x;
  //    first = 1;
  //    undefined;
  //    outer: for (;;) {
  //      let/const x = temp_x;
  //      {{ if (first == 1) {
  //           first = 0;
  //         } else {
  //           next;
  //         }
  //         flag = 1;
  //         if (!cond) break;
  //      }}
  //      labels: for (; flag == 1; flag = 0, temp_x = x) {
  //        body
  //      }
  //      {{ if (flag == 1)  // Body used break.
  //           break;
  //      }}
  //    }
  //  }

  DCHECK_GT(for_info.bound_names.length(), 0);
  ScopedPtrList<Variable> temps(pointer_buffer());

  Block* outer_block =
      factory()->NewBlock(for_info.bound_names.length() + 4, false);

  // Add statement: let/const x = i.
  outer_block->statements()->Add(init, zone());

  const AstRawString* temp_name = ast_value_factory()->dot_for_string();

  // For each lexical variable x:
  //   make statement: temp_x = x.
  for (const AstRawString* bound_name : for_info.bound_names) {
    VariableProxy* proxy = NewUnresolved(bound_name);
    Variable* temp = NewTemporary(temp_name);
    VariableProxy* temp_proxy = factory()->NewVariableProxy(temp);
    Assignment* assignment = factory()->NewAssignment(
        Token::kAssign, temp_proxy, proxy, kNoSourcePosition);
    Statement* assignment_statement =
        factory()->NewExpressionStatement(assignment, kNoSourcePosition);
    outer_block->statements()->Add(assignment_statement, zone());
    temps.Add(temp);
  }

  Variable* first = nullptr;
  // Make statement: first = 1.
  if (next) {
    first = NewTemporary(temp_name);
    VariableProxy* first_proxy = factory()->NewVariableProxy(first);
    Expression* const1 = factory()->NewSmiLiteral(1, kNoSourcePosition);
    Assignment* assignment = factory()->NewAssignment(
        Token::kAssign, first_proxy, const1, kNoSourcePosition);
    Statement* assignment_statement =
        factory()->NewExpressionStatement(assignment, kNoSourcePosition);
    outer_block->statements()->Add(assignment_statement, zone());
  }

  // make statement: undefined;
  outer_block->statements()->Add(
      factory()->NewExpressionStatement(
          factory()->NewUndefinedLiteral(kNoSourcePosition), kNoSourcePosition),
      zone());

  // Make statement: outer: for (;;)
  // Note that we don't actually create the label, or set this loop up as an
  // explicit break target, instead handing it directly to those nodes that
  // need to know about it. This should be safe because we don't run any code
  // in this function that looks up break targets.
  ForStatement* outer_loop = factory()->NewForStatement(kNoSourcePosition);
  outer_block->statements()->Add(outer_loop, zone());
  outer_block->set_scope(scope());

  Block* inner_block = factory()->NewBlock(3, false);
  {
    BlockState block_state(&scope_, inner_scope);

    Block* ignore_completion_block =
        factory()->NewBlock(for_info.bound_names.length() + 3, true);
    ScopedPtrList<Variable> inner_vars(pointer_buffer());
    // For each let variable x:
    //    make statement: let/const x = temp_x.
    for (int i = 0; i < for_info.bound_names.length(); i++) {
      VariableProxy* proxy = DeclareBoundVariable(
          for_info.bound_names[i], for_info.parsing_result.descriptor.mode,
          kNoSourcePosition);
      inner_vars.Add(proxy->var());
      VariableProxy* temp_proxy = factory()->NewVariableProxy(temps.at(i));
      Assignment* assignment = factory()->NewAssignment(
          Token::kInit, proxy, temp_proxy, kNoSourcePosition);
      Statement* assignment_statement =
          factory()->NewExpressionStatement(assignment, kNoSourcePosition);
      int declaration_pos = for_info.parsing_result.descriptor.declaration_pos;
      DCHECK_NE(declaration_pos, kNoSourcePosition);
      proxy->var()->set_initializer_position(declaration_pos);
      ignore_completion_block->statements()->Add(assignment_statement, zone());
    }

    // Make statement: if (first == 1) { first = 0; } else { next; }
    if (next) {
      DCHECK(first);
      Expression* compare = nullptr;
      // Make compare expression: first == 1.
      {
        Expression* const1 = factory()->NewSmiLiteral(1, kNoSourcePosition);
        VariableProxy* first_proxy = factory()->NewVariableProxy(first);
        compare = factory()->NewCompareOperation(Token::kEq, first_proxy,
                                                 const1, kNoSourcePosition);
      }
      Statement* clear_first = nullptr;
      // Make statement: first = 0.
      {
        VariableProxy* first_proxy = factory()->NewVariableProxy(first);
        Expression* const0 = factory()->NewSmiLiteral(0, kNoSourcePosition);
        Assignment* assignment = factory()->NewAssignment(
            Token::kAssign, first_proxy, const0, kNoSourcePosition);
        clear_first =
            factory()->NewExpressionStatement(assignment, kNoSourcePosition);
      }
      Statement* clear_first_or_next = factory()->NewIfStatement(
          compare, clear_first, next, kNoSourcePosition);
      ignore_completion_block->statements()->Add(clear_first_or_next, zone());
    }

    Variable* flag = NewTemporary(temp_name);
    // Make statement: flag = 1.
    {
      VariableProxy* flag_proxy = factory()->NewVariableProxy(flag);
      Expression* const1 = factory()->NewSmiLiteral(1, kNoSourcePosition);
      Assignment* assignment = factory()->NewAssignment(
          Token::kAssign, flag_proxy, const1, kNoSourcePosition);
      Statement* assignment_statement =
          factory()->NewExpressionStatement(assignment, kNoSourcePosition);
      ignore_completion_block->statements()->Add(assignment_statement, zone());
    }

    // Make statement: if (!cond) break.
    if (cond) {
      Statement* stop =
          factory()->NewBreakStatement(outer_loop, kNoSourcePosition);
      Statement* noop = factory()->EmptyStatement();
      ignore_completion_block->statements()->Add(
          factory()->NewIfStatement(cond, noop, stop, cond->position()),
          zone());
    }

    inner_block->statements()->Add(ignore_completion_block, zone());
    // Make cond expression for main loop: flag == 1.
    Expression* flag_cond = nullptr;
    {
      Expression* const1 = factory()->NewSmiLiteral(1, kNoSourcePosition);
      VariableProxy* flag_proxy = factory()->NewVariableProxy(flag);
      flag_cond = factory()->NewCompareOperation(Token::kEq, flag_proxy, const1,
                                                 kNoSourcePosition);
    }

    // Create chain of expressions "flag = 0, temp_x = x, ..."
    Statement* compound_next_statement = nullptr;
    {
      Expression* compound_next = nullptr;
      // Make expression: flag = 0.
      {
        VariableProxy* flag_proxy = factory()->NewVariableProxy(flag);
        Expression* const0 = factory()->NewSmiLiteral(0, kNoSourcePosition);
        compound_next = factory()->NewAssignment(Token::kAssign, flag_proxy,
                                                 const0, kNoSourcePosition);
      }

      // Make the comma-separated list of temp_x = x assignments.
      int inner_var_proxy_pos = scanner()->location().beg_pos;
      for (int i = 0; i < for_info.bound_names.length(); i++) {
        VariableProxy* temp_proxy = factory()->NewVariableProxy(temps.at(i));
        VariableProxy* proxy =
            factory()->NewVariableProxy(inner_vars.at(i), inner_var_proxy_pos);
        Assignment* assignment = factory()->NewAssignment(
            Token::kAssign, temp_proxy, proxy, kNoSourcePosition);
        compound_next = factory()->NewBinaryOperation(
            Token::kComma, compound_next, assignment, kNoSourcePosition);
      }

      compound_next_statement =
          factory()->NewExpressionStatement(compound_next, kNoSourcePosition);
    }

    // Make statement: labels: for (; flag == 1; flag = 0, temp_x = x)
    // Note that we re-use the original loop node, which retains its labels
    // and ensures that any break or continue statements in body point to
    // the right place.
    loop->Initialize(nullptr, flag_cond, compound_next_statement, body);
    inner_block->statements()->Add(loop, zone());

    // Make statement: {{if (flag == 1) break;}}
    {
      Expression* compare = nullptr;
      // Make compare expresion: flag == 1.
      {
        Expression* const1 = factory()->NewSmiLiteral(1, kNoSourcePosition);
        VariableProxy* flag_proxy = factory()->NewVariableProxy(flag);
        compare = factory()->NewCompareOperation(Token::kEq, flag_proxy, const1,
                                                 kNoSourcePosition);
      }
      Statement* stop =
          factory()->NewBreakStatement(outer_loop, kNoSourcePosition);
      Statement* empty = factory()->EmptyStatement();
      Statement* if_flag_break =
          factory()->NewIfStatement(compare, stop, empty, kNoSourcePosition);
      inner_block->statements()->Add(IgnoreCompletion(if_flag_break), zone());
    }

    inner_block->set_scope(inner_scope);
  }

  outer_loop->Initialize(nullptr, nullptr, nullptr, inner_block);

  return outer_block;
}

void ParserFormalParameters::ValidateDuplicate(Parser* parser) const {
  if (has_duplicate()) {
    parser->ReportMessageAt(duplicate_loc, MessageTemplate::kParamDupe);
  }
}
void ParserFormalParameters::ValidateStrictMode(Parser* parser) const {
  if (strict_error_loc.IsValid()) {
    parser->ReportMessageAt(strict_error_loc, strict_error_message);
  }
}

void Parser::AddArrowFunctionFormalParameters(
    ParserFormalParameters* parameters, Expression* expr, int end_pos) {
  // ArrowFunctionFormals ::
  //    Nary(Token::kComma, VariableProxy*, Tail)
  //    Binary(Token::kComma, NonTailArrowFunctionFormals, Tail)
  //    Tail
  // NonTailArrowFunctionFormals ::
  //    Binary(Token::kComma, NonTailArrowFunctionFormals, VariableProxy)
  //    VariableProxy
  // Tail ::
  //    VariableProxy
  //    Spread(VariableProxy)
  //
  // We need to visit the parameters in left-to-right order
  //

  // For the Nary case, we simply visit the parameters in a loop.
  if (expr->IsNaryOperation()) {
    NaryOperation* nary = expr->AsNaryOperation();
    // The classifier has already run, so we know that the expression is a valid
    // arrow function formals production.
    DCHECK_EQ(nary->op(), Token::kComma);
    // Each op position is the end position of the *previous* expr, with the
    // second (i.e. first "subsequent") op position being the end position of
    // the first child expression.
    Expression* next = nary->first();
    for (size_t i = 0; i < nary->subsequent_length(); ++i) {
      AddArrowFunctionFormalParameters(parameters, next,
                                       nary->subsequent_op_position(i));
      next = nary->subsequent(i);
    }
    AddArrowFunctionFormalParameters(parameters, next, end_pos);
    return;
  }

  // For the binary case, we recurse on the left-hand side of binary comma
  // expressions.
  if (expr->IsBinaryOperation()) {
    BinaryOperation* binop = expr->AsBinaryOperation();
    // The classifier has already run, so we know that the expression is a valid
    // arrow function formals production.
    DCHECK_EQ(binop->op(), Token::kComma);
    Expression* left = binop->left();
    Expression* right = binop->right();
    int comma_pos = binop->position();
    AddArrowFunctionFormalParameters(parameters, left, comma_pos);
    // LHS of comma expression should be unparenthesized.
    expr = right;
  }

  // Only the right-most expression may be a rest parameter.
  DCHECK(!parameters->has_rest);

  bool is_rest = expr->IsSpread();
  if (is_rest) {
    expr = expr->AsSpread()->expression();
    parameters->has_rest = true;
  }
  DCHECK_IMPLIES(parameters->is_simple, !is_rest);
  DCHECK_IMPLIES(parameters->is_simple, expr->IsVariableProxy());

  Expression* initializer = nullptr;
  if (expr->IsAssignment()) {
    Assignment* assignment = expr->AsAssignment();
    DCHECK(!assignment->IsCompoundAssignment());
    initializer = assignment->value();
    expr = assignment->target();
  }

  AddFormalParameter(parameters, expr, initializer, end_pos, is_rest);
}

void Parser::DeclareArrowFunctionFormalParameters(
    ParserFormalParameters* parameters, Expression* expr,
    const Scanner::Location& params_loc) {
  if (expr->IsEmptyParentheses() || has_error()) return;

  AddArrowFunctionFormalParameters(parameters, expr, params_loc.end_pos);

  if (parameters->arity > Code::kMaxArguments) {
    ReportMessageAt(params_loc, MessageTemplate::kMalformedArrowFunParamList);
    return;
  }

  DeclareFormalParameters(parameters);
  DCHECK_IMPLIES(parameters->is_simple,
                 parameters->scope->has_simple_parameters());
}

void Parser::ReindexArrowFunctionFormalParameters(
    ParserFormalParameters* parameters) {
  // Make space for the arrow function above the formal parameters.
  AstFunctionLiteralIdReindexer reindexer(stack_limit_, 1);
  for (auto p : parameters->params) {
    if (p->pattern != nullptr) reindexer.Reindex(p->pattern);
    if (p->initializer() != nullptr) {
      reindexer.Reindex(p->initializer());
    }
    if (reindexer.HasStackOverflow()) {
      set_stack_overflow();
      return;
    }
  }
}

void Parser::ReindexComputedMemberName(Expression* computed_name) {
  // Make space for the member initializer function above the computed property
  // name.
  AstFunctionLiteralIdReindexer reindexer(stack_limit_, 1);
  reindexer.Reindex(computed_name);
}

void Parser::PrepareGeneratorVariables() {
  // Calling a generator returns a generator object.  That object is stored
  // in a temporary variable, a definition that is used by "yield"
  // expressions.
  function_state_->scope()->DeclareGeneratorObjectVar(
      ast_value_factory()->dot_generator_object_string());
}

FunctionLiteral* Parser::ParseFunctionLiteral(
    const AstRawString* function_name, Scanner::Location function_name_location,
    FunctionNameValidity function_name_validity, FunctionKind kind,
    int function_token_pos, FunctionSyntaxKind function_syntax_kind,
    LanguageMode language_mode,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function) {
  // Function ::
  //   '(' FormalParameterList? ')' '{' FunctionBody '}'
  //
  // Getter ::
  //   '(' ')' '{' FunctionBody '}'
  //
  // Setter ::
  //   '(' PropertySetParameterList ')' '{' FunctionBody '}'

  bool is_wrapped = function_syntax_kind == FunctionSyntaxKind::kWrapped;
  DCHECK_EQ(is_wrapped, arguments_for_wrapped_function != nullptr);

  int pos = function_token_pos == kNoSourcePosition ? peek_position()
                                                    : function_token_pos;
  DCHECK_NE(kNoSourcePosition, pos);

  // Anonymous functions were passed either the empty symbol or a null
  // handle as the function name.  Remember if we were passed a non-empty
  // handle to decide whether to invoke function name inference.
  bool should_infer_name = function_name == nullptr;

  // We want a non-null handle as the function name by default. We will handle
  // the "function does not have a shared name" case later.
  if (should_infer_name) {
    function_name = ast_value_factory()->empty_string();
  }

  // This is true if we get here through CreateDynamicFunction.
  bool params_need_validation = parameters_end_pos_ != kNoSourcePosition;

  FunctionLiteral::EagerCompileHint eager_compile_hint =
      function_state_->next_function_is_likely_called() || is_wrapped ||
              params_need_validation ||
              (info()->flags().compile_hints_magic_enabled() &&
               scanner()->SawMagicCommentCompileHintsAll())
          ? FunctionLiteral::kShouldEagerCompile
          : default_eager_compile_hint();

  // Determine if the function can be parsed lazily. Lazy parsing is
  // different from lazy compilation; we need to parse more eagerly than we
  // compile.

  // We can only parse lazily if we also compile lazily. The heuristics for lazy
  // compilation are:
  // - It must not have been prohibited by the caller to Parse (some callers
  //   need a full AST).
  // - The outer scope must allow lazy compilation of inner functions.
  // - The function mustn't be a function expression with an open parenthesis
  //   before; we consider that a hint that the function will be called
  //   immediately, and it would be a waste of time to make it lazily
  //   compiled.
  // These are all things we can know at this point, without looking at the
  // function itself.

  // We separate between lazy parsing top level functions and lazy parsing inner
  // functions, because the latter needs to do more work. In particular, we need
  // to track unresolved variables to distinguish between these cases:
  // (function foo() {
  //   bar = function() { return 1; }
  //  })();
  // and
  // (function foo() {
  //   var a = 1;
  //   bar = function() { return a; }
  //  })();

  // Now foo will be parsed eagerly and compiled eagerly (optimization: assume
  // parenthesis before the function means that it will be called
  // immediately). bar can be parsed lazily, but we need to parse it in a mode
  // that tracks unresolved variables.
  DCHECK_IMPLIES(parse_lazily(), info()->flags().allow_lazy_compile());
  DCHECK_IMPLIES(parse_lazily(), has_error() || allow_lazy_);
  DCHECK_IMPLIES(parse_lazily(), extension() == nullptr);

  int compile_hint_position = peek_position();
  eager_compile_hint =
      GetEmbedderCompileHint(eager_compile_hint, compile_hint_position);

  const bool is_lazy =
      eager_compile_hint == FunctionLiteral::kShouldLazyCompile;
  const bool is_top_level = AllowsLazyParsingWithoutUnresolvedVariables();
  const bool is_eager_top_level_function = !is_lazy && is_top_level;

  RCS_SCOPE(runtime_call_stats_, RuntimeCallCounterId::kParseFunctionLiteral,
            RuntimeCallStats::kThreadSpecific);
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(v8_flags.log_function_events)) timer.Start();

  // Determine whether we can lazy parse the inner function. Lazy compilation
  // has to be enabled, which is either forced by overall parse flags or via a
  // ParsingModeScope.
  const bool can_preparse = parse_lazily();

  // Determine whether we can post any parallel compile tasks. Preparsing must
  // be possible, there has to be a dispatcher, and the character stream must be
  // cloneable.
  const bool can_post_parallel_task =
      can_preparse && info()->dispatcher() &&
      scanner()->stream()->can_be_cloned_for_parallel_access();

  // If parallel compile tasks are enabled, and this isn't a re-parse, enable
  // parallel compile for the subset of functions as defined by flags.
  bool should_post_parallel_task =
      can_post_parallel_task && !flags().is_reparse() &&
      ((is_eager_top_level_function &&
        flags().post_parallel_compile_tasks_for_eager_toplevel()) ||
       (is_lazy && flags().post_parallel_compile_tasks_for_lazy()));

  // Determine whether we should lazy parse the inner function. This will be
  // when either the function is lazy by inspection, or when we force it to be
  // preparsed now so that we can then post a parallel full parse & compile task
  // for it.
  const bool should_preparse =
      can_preparse && (is_lazy || should_post_parallel_task);

  ScopedPtrList<Statement> body(pointer_buffer());
  int expected_property_count = 0;
  int suspend_count = -1;
  int num_parameters = -1;
  int function_length = -1;
  bool has_duplicate_parameters = false;
  int function_literal_id = GetNextInfoId();
  ProducedPreparseData* produced_preparse_data = nullptr;

  // Inner functions will be parsed using a temporary Zone. After parsing, we
  // will migrate unresolved variable into a Scope in the main Zone.
  Zone* parse_zone = should_preparse ? &preparser_zone_ : zone();
  // This Scope lives in the main zone. We'll migrate data into that zone later.
  DeclarationScope* scope = NewFunctionScope(kind, parse_zone);
  SetLanguageMode(scope, language_mode);
  if (is_wrapped) {
    scope->set_is_wrapped_function();
  }
#ifdef DEBUG
  scope->SetScopeName(function_name);
#endif

  if (!is_wrapped && V8_UNLIKELY(!Check(Token::kLeftParen))) {
    ReportUnexpectedToken(Next());
    return nullptr;
  }
  scope->set_start_position(position());

  // Eager or lazy parse? If is_lazy_top_level_function, we'll parse
  // lazily. We'll call SkipFunction, which may decide to
  // abort lazy parsing if it suspects that wasn't a good idea. If so (in
  // which case the parser is expected to have backtracked), or if we didn't
  // try to lazy parse in the first place, we'll have to parse eagerly.
  bool did_preparse_successfully =
      should_preparse &&
      SkipFunction(function_name, kind, function_syntax_kind, scope,
                   &num_parameters, &function_length, &produced_preparse_data);

  if (!did_preparse_successfully) {
    // If skipping aborted, it rewound the scanner until before the lparen.
    // Consume it in that case.
    if (should_preparse) Consume(Token::kLeftParen);
    should_post_parallel_task = false;
    ParseFunction(&body, function_name, pos, kind, function_syntax_kind, scope,
                  &num_parameters, &function_length, &has_duplicate_parameters,
                  &expected_property_count, &suspend_count,
                  arguments_for_wrapped_function);
  }

  if (V8_UNLIKELY(v8_flags.log_function_events)) {
    double ms = timer.Elapsed().InMillisecondsF();
    const char* event_name =
        should_preparse
            ? (is_top_level ? "preparse-no-resolution" : "preparse-resolution")
            : "full-parse";
    v8_file_logger_->FunctionEvent(
        event_name, flags().script_id(), ms, scope->start_position(),
        scope->end_position(),
        reinterpret_cast<const char*>(function_name->raw_data()),
        function_name->byte_length(), function_name->is_one_byte());
  }
#ifdef V8_RUNTIME_CALL_STATS
  if (did_preparse_successfully && runtime_call_stats_ &&
      V8_UNLIKELY(TracingFlags::is_runtime_stats_enabled())) {
    runtime_call_stats_->CorrectCurrentCounterId(
        RuntimeCallCounterId::kPreParseWithVariableResolution,
        RuntimeCallStats::kThreadSpecific);
  }
#endif  // V8_RUNTIME_CALL_STATS

  // Validate function name. We can do this only after parsing the function,
  // since the function can declare itself strict.
  language_mode = scope->language_mode();
  CheckFunctionName(language_mode, function_name, function_name_validity,
                    function_name_location);

  if (is_strict(language_mode)) {
    CheckStrictOctalLiteral(scope->start_position(), scope->end_position());
  }

  FunctionLiteral::ParameterFlag duplicate_parameters =
      has_duplicate_parameters ? FunctionLiteral::kHasDuplicateParameters
                               : FunctionLiteral::kNoDuplicateParameters;

  // Note that the FunctionLiteral needs to be created in the main Zone again.
  FunctionLiteral* function_literal = factory()->NewFunctionLiteral(
      function_name, scope, body, expected_property_count, num_parameters,
      function_length, duplicate_parameters, function_syntax_kind,
      eager_compile_hint, pos, true, function_literal_id,
      produced_preparse_data);
  function_literal->set_function_token_position(function_token_pos);
  function_literal->set_suspend_count(suspend_count);

  RecordFunctionLiteralSourceRange(function_literal);

  if (should_post_parallel_task && !has_error()) {
    function_literal->set_should_parallel_compile();
  }

  if (should_infer_name) {
    fni_.AddFunction(function_literal);
  }
  return function_literal;
}

bool Parser::SkipFunction(const AstRawString* function_name, FunctionKind kind,
                          FunctionSyntaxKind function_syntax_kind,
                          DeclarationScope* function_scope, int* num_parameters,
                          int* function_length,
                          ProducedPreparseData** produced_preparse_data) {
  FunctionState function_state(&function_state_, &scope_, function_scope);
  function_scope->set_zone(&preparser_zone_);

  DCHECK_NE(kNoSourcePosition, function_scope->start_position());
  DCHECK_EQ(kNoSourcePosition, parameters_end_pos_);

  DCHECK_IMPLIES(IsArrowFunction(kind),
                 scanner()->current_token() == Token::kArrow);

  // FIXME(marja): There are 2 ways to skip functions now. Unify them.
  if (consumed_preparse_data_) {
    int end_position;
    LanguageMode language_mode;
    int num_inner_infos;
    bool uses_super_property;
    if (stack_overflow()) return true;
    {
      UnparkedScopeIfOnBackground unparked_scope(local_isolate_);
      *produced_preparse_data =
          consumed_preparse_data_->GetDataForSkippableFunction(
              main_zone(), function_scope->start_position(), &end_position,
              num_parameters, function_length, &num_inner_infos,
              &uses_super_property, &language_mode);
    }

    function_scope->outer_scope()->SetMustUsePreparseData();
    function_scope->set_is_skipped_function(true);
    function_scope->set_end_position(end_position);
    scanner()->SeekForward(end_position - 1);
    Expect(Token::kRightBrace);
    SetLanguageMode(function_scope, language_mode);
    if (uses_super_property) {
      function_scope->RecordSuperPropertyUsage();
    }
    SkipInfos(num_inner_infos);
    function_scope->ResetAfterPreparsing(ast_value_factory_, false);
    return true;
  }

  Scanner::BookmarkScope bookmark(scanner());
  bookmark.Set(function_scope->start_position());

  UnresolvedList::Iterator unresolved_private_tail;
  PrivateNameScopeIterator private_name_scope_iter(function_scope);
  if (!private_name_scope_iter.Done()) {
    unresolved_private_tail =
        private_name_scope_iter.GetScope()->GetUnresolvedPrivateNameTail();
  }

  // With no cached data, we partially parse the function, without building an
  // AST. This gathers the data needed to build a lazy function.
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "V8.PreParse");

  PreParser::PreParseResult result = reusable_preparser()->PreParseFunction(
      function_name, kind, function_syntax_kind, function_scope, use_counts_,
      produced_preparse_data);

  if (result == PreParser::kPreParseStackOverflow) {
    // Propagate stack overflow.
    set_stack_overflow();
  } else if (pending_error_handler()->has_error_unidentifiable_by_preparser()) {
    // Make sure we don't re-preparse inner functions of the aborted function.
    // The error might be in an inner function.
    allow_lazy_ = false;
    mode_ = PARSE_EAGERLY;
    DCHECK(!pending_error_handler()->stack_overflow());
    // If we encounter an error that the preparser can not identify we reset to
    // the state before preparsing. The caller may then fully parse the function
    // to identify the actual error.
    bookmark.Apply();
    if (!private_name_scope_iter.Done()) {
      private_name_scope_iter.GetScope()->ResetUnresolvedPrivateNameTail(
          unresolved_private_tail);
    }
    function_scope->ResetAfterPreparsing(ast_value_factory_, true);
    pending_error_handler()->clear_unidentifiable_error();
    return false;
  } else if (pending_error_handler()->has_pending_error()) {
    DCHECK(!pending_error_handler()->stack_overflow());
    DCHECK(has_error());
  } else {
    DCHECK(!pending_error_handler()->stack_overflow());
    set_allow_eval_cache(reusable_preparser()->allow_eval_cache());

    PreParserLogger* logger = reusable_preparser()->logger();
    function_scope->set_end_position(logger->end());
    Expect(Token::kRightBrace);
    total_preparse_skipped_ +=
        function_scope->end_position() - function_scope->start_position();
    *num_parameters = logger->num_parameters();
    *function_length = logger->function_length();
    SkipInfos(logger->num_inner_infos());
    if (!private_name_scope_iter.Done()) {
      private_name_scope_iter.GetScope()->MigrateUnresolvedPrivateNameTail(
          factory(), unresolved_private_tail);
    }
    function_scope->AnalyzePartially(this, factory(), MaybeParsingArrowhead());
  }

  return true;
}

Block* Parser::BuildParameterInitializationBlock(
    const ParserFormalParameters& parameters) {
  DCHECK(!parameters.is_simple);
  DCHECK(scope()->is_function_scope());
  DCHECK_EQ(scope(), parameters.scope);
  ScopedPtrList<Statement> init_statements(pointer_buffer());
  int index = 0;
  for (auto parameter : parameters.params) {
    Expression* initial_value =
        factory()->NewVariableProxy(parameters.scope->parameter(index));
    if (parameter->initializer() != nullptr) {
      // IS_UNDEFINED($param) ? initializer : $param

      auto condition = factory()->NewCompareOperation(
          Token::kEqStrict,
          factory()->NewVariableProxy(parameters.scope->parameter(index)),
          factory()->NewUndefinedLiteral(kNoSourcePosition), kNoSourcePosition);
      initial_value =
          factory()->NewConditional(condition, parameter->initializer(),
                                    initial_value, kNoSourcePosition);
    }

    BlockState block_state(&scope_, scope()->AsDeclarationScope());
    DeclarationParsingResult::Declaration decl(parameter->pattern,
                                               initial_value);
    InitializeVariables(&init_statements, PARAMETER_VARIABLE, &decl);

    ++index;
  }
  return factory()->NewBlock(true, init_statements);
}

// TODO(verwaest): Consider building these try/catches in the bytecode generator
// without hidden scopes.
Scope* Parser::NewHiddenCatchScope() {
  DCHECK(scope()->is_declaration_scope());
  Scope* catch_scope = NewScopeWithParent(scope(), CATCH_SCOPE);
  catch_scope->set_start_position(position());
  catch_scope->set_end_position(end_position());
  bool was_added;
  catch_scope->DeclareLocal(ast_value_factory()->dot_catch_string(),
                            VariableMode::kVar, NORMAL_VARIABLE, &was_added);
  DCHECK(was_added);
  catch_scope->set_is_hidden();
  return catch_scope;
}

Expression* Parser::BuildInitialYield(int pos, FunctionKind kind) {
  Expression* yield_result = factory()->NewVariableProxy(
      function_state_->scope()->generator_object_var());
  // The position of the yield is important for reporting the exception
  // caused by calling the .throw method on a generator suspended at the
  // initial yield (i.e. right after generator instantiation).
  function_state_->AddSuspend();
  return factory()->NewYield(yield_result, scope()->start_position(),
                             Suspend::kOnExceptionThrow);
}

void Parser::ParseFunction(
    ScopedPtrList<Statement>* body, const AstRawString* function_name, int pos,
    FunctionKind kind, FunctionSyntaxKind function_syntax_kind,
    DeclarationScope* function_scope, int* num_parameters, int* function_length,
    bool* has_duplicate_parameters, int* expected_property_count,
    int* suspend_count,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function) {
  FunctionParsingScope function_parsing_scope(this);
  ParsingModeScope mode(this, allow_lazy_ ? PARSE_LAZILY : PARSE_EAGERLY);

  FunctionState function_state(&function_state_, &scope_, function_scope);

  bool is_wrapped = function_syntax_kind == FunctionSyntaxKind::kWrapped;

  int expected_parameters_end_pos = parameters_end_pos_;
  if (expected_parameters_end_pos != kNoSourcePosition) {
    // This is the first function encountered in a CreateDynamicFunction eval.
    parameters_end_pos_ = kNoSourcePosition;
    // The function name should have been ignored, giving us the empty string
    // here.
    DCHECK_EQ(function_name, ast_value_factory()->empty_string());
  }

  ParserFormalParameters formals(function_scope);

  {
    ParameterDeclarationParsingScope formals_scope(this);
    if (is_wrapped) {
      // For a function implicitly wrapped in function header and footer, the
      // function arguments are provided separately to the source, and are
      // declared directly here.
      for (const AstRawString* arg : *arguments_for_wrapped_function) {
        const bool is_rest = false;
        Expression* argument = ExpressionFromIdentifier(arg, kNoSourcePosition);
        AddFormalParameter(&formals, argument, NullExpression(),
                           kNoSourcePosition, is_rest);
      }
      DCHECK_EQ(arguments_for_wrapped_function->length(),
                formals.num_parameters());
      DeclareFormalParameters(&formals);
    } else {
      // For a regular function, the function arguments are parsed from source.
      DCHECK_NULL(arguments_for_wrapped_function);
      ParseFormalParameterList(&formals);
      if (expected_parameters_end_pos != kNoSourcePosition) {
        // Check for '(' or ')' shenanigans in the parameter string for dynamic
        // functions.
        int position = peek_position();
        if (position < expected_parameters_end_pos) {
          ReportMessageAt(Scanner::Location(position, position + 1),
                          MessageTemplate::kArgStringTerminatesParametersEarly);
          return;
        } else if (position > expected_parameters_end_pos) {
          ReportMessageAt(Scanner::Location(expected_parameters_end_pos - 2,
                                            expected_parameters_end_pos),
                          MessageTemplate::kUnexpectedEndOfArgString);
          return;
        }
      }
      Expect(Token::kRightParen);
      int formals_end_position = end_position();

      CheckArityRestrictions(formals.arity, kind, formals.has_rest,
                             function_scope->start_position(),
                             formals_end_position);
      Expect(Token::kLeftBrace);
    }
    formals.duplicate_loc = formals_scope.duplicate_location();
  }

  *num_parameters = formals.num_parameters();
  *function_length = formals.function_length;

  AcceptINScope scope(this, true);
  ParseFunctionBody(body, function_name, pos, formals, kind,
                    function_syntax_kind, FunctionBodyType::kBlock);

  *has_duplicate_parameters = formals.has_duplicate();

  *expected_property_count = function_state.expected_property_count();
  *suspend_count = function_state.suspend_count();
}

void Parser::DeclareClassVariable(ClassScope* scope, const AstRawString* name,
                                  ClassInfo* class_info, int class_token_pos) {
#ifdef DEBUG
  scope->SetScopeName(name);
#endif

  DCHECK_IMPLIES(IsEmptyIdentifier(name), class_info->is_anonymous);
  // Declare a special class variable for anonymous classes with the dot
  // if we need to save it for static private method access.
  Variable* class_variable =
      scope->DeclareClassVariable(ast_value_factory(), name, class_token_pos);
  Declaration* declaration = factory()->NewVariableDeclaration(class_token_pos);
  scope->declarations()->Add(declaration);
  declaration->set_var(class_variable);
}

VariableProxy* Parser::CreateSyntheticContextVariableProxy(
    ClassScope* scope, ClassInfo* class_info, const AstRawString* name,
    bool is_static) {
  if (scope->is_reparsed()) {
    DeclarationScope* declaration_scope =
        is_static ? class_info->static_elements_scope
                  : class_info->instance_members_scope;
    return declaration_scope->NewUnresolved(factory()->ast_node_factory(), name,
                                            position());
  }
  VariableProxy* proxy =
      DeclareBoundVariable(name, VariableMode::kConst, kNoSourcePosition);
  proxy->var()->ForceContextAllocation();
  return proxy;
}

VariableProxy* Parser::CreatePrivateNameVariable(ClassScope* scope,
                                                 VariableMode mode,
                                                 IsStaticFlag is_static_flag,
                                                 const AstRawString* name) {
  DCHECK_NOT_NULL(name);
  int begin = position();
  int end = end_position();
  bool was_added = false;
  DCHECK(IsImmutableLexicalOrPrivateVariableMode(mode));
  Variable* var =
      scope->DeclarePrivateName(name, mode, is_static_flag, &was_added);
  if (!was_added) {
    Scanner::Location loc(begin, end);
    ReportMessageAt(loc, MessageTemplate::kVarRedeclaration, var->raw_name());
  }
  return factory()->NewVariableProxy(var, begin);
}

void Parser::DeclarePublicClassField(ClassScope* scope,
                                     ClassLiteralProperty* property,
                                     bool is_static, bool is_computed_name,
                                     ClassInfo* class_info) {
  if (is_static) {
    class_info->static_elements->Add(
        factory()->NewClassLiteralStaticElement(property), zone());
  } else {
    class_info->instance_fields->Add(property, zone());
  }

  if (is_computed_name) {
    // We create a synthetic variable name here so that scope
    // analysis doesn't dedupe the vars.
    const AstRawString* name = ClassFieldVariableName(
        ast_value_factory(), class_info->computed_field_count);
    VariableProxy* proxy =
        CreateSyntheticContextVariableProxy(scope, class_info, name, is_static);
    property->set_computed_name_proxy(proxy);
    class_info->public_members->Add(property, zone());
  }
}

void Parser::DeclarePrivateClassMember(ClassScope* scope,
                                       const AstRawString* property_name,
                                       ClassLiteralProperty* property,
                                       ClassLiteralProperty::Kind kind,
                                       bool is_static, ClassInfo* class_info) {
  if (kind == ClassLiteralProperty::Kind::FIELD ||
      kind == ClassLiteralProperty::Kind::AUTO_ACCESSOR) {
    if (is_static) {
      class_info->static_elements->Add(
          factory()->NewClassLiteralStaticElement(property), zone());
    } else {
      class_info->instance_fields->Add(property, zone());
    }
  }
  class_info->private_members->Add(property, zone());

  VariableProxy* proxy;
  if (scope->is_reparsed()) {
    PrivateNameScopeIterator private_name_scope_iter(scope);
    proxy = ExpressionFromPrivateName(&private_name_scope_iter, property_name,
                                      position());
  } else {
    proxy = CreatePrivateNameVariable(
        scope, GetVariableMode(kind),
        is_static ? IsStaticFlag::kStatic : IsStaticFlag::kNotStatic,
        property_name);
    int pos = property->value()->position();
    if (pos == kNoSourcePosition) {
      pos = property->key()->position();
    }
    proxy->var()->set_initializer_position(pos);
  }
  property->SetPrivateNameProxy(proxy);
}

// This method declares a property of the given class.  It updates the
// following fields of class_info, as appropriate:
//   - constructor
//   - properties
void Parser::DeclarePublicClassMethod(const AstRawString* class_name,
                                      ClassLiteralProperty* property,
                                      bool is_constructor,
                                      ClassInfo* class_info) {
  if (is_constructor) {
    DCHECK(!class_info->constructor);
    class_info->constructor = property->value()->AsFunctionLiteral();
    DCHECK_NOT_NULL(class_info->constructor);
    class_info->constructor->set_raw_name(
        class_name != nullptr ? ast_value_factory()->NewConsString(class_name)
                              : nullptr);
    return;
  }

  class_info->public_members->Add(property, zone());
}

void Parser::AddClassStaticBlock(Block* block, ClassInfo* class_info) {
  DCHECK(class_info->has_static_elements());
  class_info->static_elements->Add(
      factory()->NewClassLiteralStaticElement(block), zone());
}

FunctionLiteral* Parser::CreateInitializerFunction(
    const AstRawString* class_name, DeclarationScope* scope,
    int function_literal_id, Statement* initializer_stmt) {
  DCHECK(IsClassMembersInitializerFunction(scope->function_kind()));
  // function() { .. class fields initializer .. }
  ScopedPtrList<Statement> statements(pointer_buffer());
  statements.Add(initializer_stmt);
  FunctionLiteral* result = factory()->NewFunctionLiteral(
      class_name, scope, statements, 0, 0, 0,
      FunctionLiteral::kNoDuplicateParameters,
      FunctionSyntaxKind::kAccessorOrMethod,
      FunctionLiteral::kShouldEagerCompile, scope->start_position(), false,
      function_literal_id);
#ifdef DEBUG
  scope->SetScopeName(class_name);
#endif
  RecordFunctionLiteralSourceRange(result);

  return result;
}

FunctionLiteral* Parser::CreateStaticElementsInitializer(
    const AstRawString* name, ClassInfo* class_info) {
  return CreateInitializerFunction(
      name, class_info->static_elements_scope,
      class_info->static_elements_function_id,
      factory()->NewInitializeClassStaticElementsStatement(
          class_info->static_elements, kNoSourcePosition));
}

FunctionLiteral* Parser::CreateInstanceMembersInitializer(
    const AstRawString* name, ClassInfo* class_info) {
  return CreateInitializerFunction(
      name, class_info->instance_members_scope,
      class_info->instance_members_function_id,
      factory()->NewInitializeClassMembersStatement(class_info->instance_fields,
                                                    kNoSourcePosition));
}

// This method generates a ClassLiteral AST node.
// It uses the following fields of class_info:
//   - constructor (if missing, it updates it with a default constructor)
//   - proxy
//   - extends
//   - properties
//   - has_static_computed_names
Expression* Parser::RewriteClassLiteral(ClassScope* block_scope,
                                        const AstRawString* name,
                                        ClassInfo* class_info, int pos) {
  DCHECK_NOT_NULL(block_scope);
  DCHECK_EQ(block_scope->scope_type(), CLASS_SCOPE);
  DCHECK_EQ(block_scope->language_mode(), LanguageMode::kStrict);

  bool has_extends = class_info->extends != nullptr;
  bool has_default_constructor = class_info->constructor == nullptr;
  int end_pos = block_scope->end_position();
  if (has_default_constructor) {
    class_info->constructor = DefaultConstructor(name, has_extends, pos);
  }

  if (!IsEmptyIdentifier(name)) {
    DCHECK_NOT_NULL(block_scope->class_variable());
    block_scope->class_variable()->set_initializer_position(end_pos);
  }

  FunctionLiteral* static_initializer = nullptr;
  if (class_info->has_static_elements()) {
    static_initializer = CreateStaticElementsInitializer(name, class_info);
  }

  FunctionLiteral* instance_members_initializer_function = nullptr;
  if (class_info->has_instance_members()) {
    instance_members_initializer_function =
        CreateInstanceMembersInitializer(name, class_info);
    class_info->constructor->set_requires_instance_members_initializer(true);
    class_info->constructor->add_expected_properties(
        class_info->instance_fields->length());
  }

  if (class_info->requires_brand) {
    class_info->constructor->set_class_scope_has_private_brand(true);
  }
  if (class_info->has_static_private_methods_or_accessors) {
    class_info->constructor->set_has_static_private_methods_or_accessors(true);
  }
  ClassLiteral* class_literal = factory()->NewClassLiteral(
      block_scope, class_info->extends, class_info->constructor,
      class_info->public_members, class_info->private_members,
      static_initializer, instance_members_initializer_function, pos, end_pos,
      class_info->has_static_computed_names, class_info->is_anonymous,
      class_info->home_object_variable,
      class_info->static_home_object_variable);

  AddFunctionForNameInference(class_info->constructor);
  return class_literal;
}

void Parser::InsertShadowingVarBindingInitializers(Block* inner_block) {
  // For each var-binding that shadows a parameter, insert an assignment
  // initializing the variable with the parameter.
  Scope* inner_scope = inner_block->scope();
  DCHECK(inner_scope->is_declaration_scope());
  Scope* function_scope = inner_scope->outer_scope();
  DCHECK(function_scope->is_function_scope());
  BlockState block_state(&scope_, inner_scope);
  // According to https://tc39.es/ecma262/#sec-functiondeclarationinstantiation
  // If a variable's name conflicts with the names of both parameters and
  // functions, no bindings should be created for it. A set is used here
  // to record such variables.
  std::set<Variable*> hoisted_func_vars;
  std::vector<std::pair<Variable*, Variable*>> var_param_bindings;
  for (Declaration* decl : *inner_scope->declarations()) {
    if (!decl->IsVariableDeclaration()) {
      hoisted_func_vars.insert(decl->var());
      continue;
    } else if (decl->var()->mode() != VariableMode::kVar) {
      continue;
    }
    const AstRawString* name = decl->var()->raw_name();
    Variable* parameter = function_scope->LookupLocal(name);
    if (parameter == nullptr) continue;
    var_param_bindings.push_back(std::pair(decl->var(), parameter));
  }

  for (auto decl : var_param_bindings) {
    if (hoisted_func_vars.find(decl.first) != hoisted_func_vars.end()) {
      continue;
    }
    const AstRawString* name = decl.first->raw_name();
    VariableProxy* to = NewUnresolved(name);
    VariableProxy* from = factory()->NewVariableProxy(decl.second);
    Expression* assignment =
        factory()->NewAssignment(Token::kAssign, to, from, kNoSourcePosition);
    Statement* statement =
        factory()->NewExpressionStatement(assignment, kNoSourcePosition);
    inner_block->statements()->InsertAt(0, statement, zone());
  }
}

void Parser::InsertSloppyBlockFunctionVarBindings(DeclarationScope* scope) {
  // For the outermost eval scope, we cannot hoist during parsing: let
  // declarations in the surrounding scope may prevent hoisting, but the
  // information is unaccessible during parsing. In this case, we hoist later in
  // DeclarationScope::Analyze.
  if (scope->is_eval_scope() && scope->outer_scope() == original_scope_) {
    return;
  }
  scope->HoistSloppyBlockFunctions(factory());
}

// ----------------------------------------------------------------------------
// Parser support

template <typename IsolateT>
void Parser::HandleSourceURLComments(IsolateT* isolate,
                                     DirectHandle<Script> script) {
  Handle<String> source_url = scanner_.SourceUrl(isolate);
  if (!source_url.is_null()) {
    script->set_source_url(*source_url);
  }
  Handle<String> source_mapping_url = scanner_.SourceMappingUrl(isolate);
  // The API can provide a source map URL and the API should take precedence.
  // Let's make sure we do not override the API with the magic comment.
  if (!source_mapping_url.is_null() &&
      IsUndefined(script->source_mapping_url(isolate), isolate)) {
    script->set_source_mapping_url(*source_mapping_url);
  }
}

template void Parser::HandleSourceURLComments(Isolate* isolate,
                                              DirectHandle<Script> script);
template void Parser::HandleSourceURLComments(LocalIsolate* isolate,
                                              DirectHandle<Script> script);

void Parser::UpdateStatistics(Isolate* isolate, DirectHandle<Script> script) {
  CHECK_NOT_NULL(isolate);

  // Move statistics to Isolate.
  for (int feature = 0; feature < v8::Isolate::kUseCounterFeatureCount;
       ++feature) {
    if (use_counts_[feature] > 0) {
      isolate->CountUsage(v8::Isolate::UseCounterFeature(feature));
    }
  }
  if (scanner_.FoundHtmlComment()) {
    isolate->CountUsage(v8::Isolate::kHtmlComment);
    if (script->line_offset() == 0 && script->column_offset() == 0) {
      isolate->CountUsage(v8::Isolate::kHtmlCommentInExternalScript);
    }
  }
  if (scanner_.SawMagicCommentCompileHintsAll()) {
    isolate->CountUsage(v8::Isolate::kCompileHintsMagicAll);
  }
  if (scanner_.SawSourceMappingUrlMagicCommentAtSign()) {
    isolate->CountUsage(v8::Isolate::kSourceMappingUrlMagicCommentAtSign);
  }
}

void Parser::UpdateStatistics(
    DirectHandle<Script> script,
    base::SmallVector<v8::Isolate::UseCounterFeature, 8>* use_counts,
    int* preparse_skipped) {
  // Move statistics to Isolate.
  for (int feature = 0; feature < v8::Isolate::kUseCounterFeatureCount;
       ++feature) {
    if (use_counts_[feature] > 0) {
      use_counts->emplace_back(v8::Isolate::UseCounterFeature(feature));
    }
  }
  if (scanner_.FoundHtmlComment()) {
    use_counts->emplace_back(v8::Isolate::kHtmlComment);
    if (script->line_offset() == 0 && script->column_offset() == 0) {
      use_counts->emplace_back(v8::Isolate::kHtmlCommentInExternalScript);
    }
  }
  if (scanner_.SawMagicCommentCompileHintsAll()) {
    use_counts->emplace_back(v8::Isolate::kCompileHintsMagicAll);
  }
  if (scanner_.SawSourceMappingUrlMagicCommentAtSign()) {
    use_counts->emplace_back(v8::Isolate::kSourceMappingUrlMagicCommentAtSign);
  }

  *preparse_skipped = total_preparse_skipped_;
}

void Parser::ParseOnBackground(LocalIsolate* isolate, ParseInfo* info,
                               DirectHandle<Script> script, int start_position,
                               int end_position, int function_literal_id) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kParseProgram,
            RuntimeCallStats::CounterMode::kThreadSpecific);
  parsing_on_main_thread_ = false;

  DCHECK_NULL(info->literal());
  FunctionLiteral* result = nullptr;

  // We can park the isolate while parsing, it doesn't need to allocate or
  // access the main thread.
  isolate->ParkIfOnBackgroundAndExecute([this, start_position, end_position,
                                         function_literal_id, info, &result]() {
    scanner_.Initialize();

    DCHECK(original_scope_);

    // When streaming, we don't know the length of the source until we have
    // parsed it. The raw data can be UTF-8, so we wouldn't know the source
    // length until we have decoded it anyway even if we knew the raw data
    // length (which we don't). We work around this by storing all the scopes
    // which need their end position set at the end of the script (the top scope
    // and possible eval scopes) and set their end position after we know the
    // script length.
    if (flags().is_toplevel()) {
      DCHECK_EQ(start_position, 0);
      DCHECK_EQ(end_position, 0);
      DCHECK_EQ(function_literal_id, kFunctionLiteralIdTopLevel);
      result = DoParseProgram(/* isolate = */ nullptr, info);
    } else {
      std::optional<ClassScope::HeritageParsingScope> heritage;
      if (V8_UNLIKELY(flags().private_name_lookup_skips_outer_class() &&
                      original_scope_->is_class_scope())) {
        // If the function skips the outer class and the outer scope is a class,
        // the function is in heritage position. Otherwise the function scope's
        // skip bit will be correctly inherited from the outer scope.
        heritage.emplace(original_scope_->AsClassScope());
      }
      result = DoParseFunction(/* isolate = */ nullptr, info, start_position,
                               end_position, function_literal_id,
                               info->function_name());
    }
    if (result == nullptr) return;
    MaybeProcessSourceRanges(info, result, stack_limit_);
  });
  // We need to unpark by now though, to be able to internalize.
  if (flags().is_toplevel()) {
    HandleSourceURLComments(isolate, script);
  }
  if (result == nullptr) return;
  PostProcessParseResult(isolate, info, result);
}

Parser::TemplateLiteralState Parser::OpenTemplateLiteral(int pos) {
  return zone()->New<TemplateLiteral>(zone(), pos);
}

void Parser::AddTemplateSpan(TemplateLiteralState* state, bool should_cook,
                             bool tail) {
  int end = scanner()->location().end_pos - (tail ? 1 : 2);
  const AstRawString* raw = scanner()->CurrentRawSymbol(ast_value_factory());
  if (should_cook) {
    const AstRawString* cooked = scanner()->CurrentSymbol(ast_value_factory());
    (*state)->AddTemplateSpan(cooked, raw, end, zone());
  } else {
    (*state)->AddTemplateSpan(nullptr, raw, end, zone());
  }
}

void Parser::AddTemplateExpression(TemplateLiteralState* state,
                                   Expression* expression) {
  (*state)->AddExpression(expression, zone());
}

Expression* Parser::CloseTemplateLiteral(TemplateLiteralState* state, int start,
                                         Expression* tag) {
  TemplateLiteral* lit = *state;
  int pos = lit->position();
  const ZonePtrList<const AstRawString>* cooked_strings = lit->cooked();
  const ZonePtrList<const AstRawString>* raw_strings = lit->raw();
  const ZonePtrList<Expression>* expressions = lit->expressions();
  DCHECK_EQ(cooked_strings->length(), raw_strings->length());
  DCHECK_EQ(cooked_strings->length(), expressions->length() + 1);

  if (!tag) {
    if (cooked_strings->length() == 1) {
      return factory()->NewStringLiteral(cooked_strings->first(), pos);
    }
    return factory()->NewTemplateLiteral(cooked_strings, expressions, pos);
  } else {
    // GetTemplateObject
    Expression* template_object =
        factory()->NewGetTemplateObject(cooked_strings, raw_strings, pos);

    // Call TagFn
    ScopedPtrList<Expression> call_args(pointer_buffer());
    call_args.Add(template_object);
    call_args.AddAll(expressions->ToConstVector());
    return factory()->NewTaggedTemplate(tag, call_args, pos);
  }
}

void Parser::SetLanguageMode(Scope* scope, LanguageMode mode) {
  v8::Isolate::UseCounterFeature feature;
  if (is_sloppy(mode))
    feature = v8::Isolate::kSloppyMode;
  else if (is_strict(mode))
    feature = v8::Isolate::kStrictMode;
  else
    UNREACHABLE();
  ++use_counts_[feature];
  scope->SetLanguageMode(mode);
}

#if V8_ENABLE_WEBASSEMBLY
void Parser::SetAsmModule() {
  // Store the usage count; The actual use counter on the isolate is
  // incremented after parsing is done.
  ++use_counts_[v8::Isolate::kUseAsm];
  DCHECK(scope()->is_declaration_scope());
  scope()->AsDeclarationScope()->set_is_asm_module();
  info_->set_contains_asm_module(true);
}
#endif  // V8_ENABLE_WEBASSEMBLY

Expression* Parser::ExpressionListToExpression(
    const ScopedPtrList<Expression>& args) {
  Expression* expr = args.at(0);
  if (args.length() == 1) return expr;
  if (args.length() == 2) {
    return factory()->NewBinaryOperation(Token::kComma, expr, args.at(1),
                                         args.at(1)->position());
  }
  NaryOperation* result =
      factory()->NewNaryOperation(Token::kComma, expr, args.length() - 1);
  for (int i = 1; i < args.length(); i++) {
    result->AddSubsequent(args.at(i), args.at(i)->position());
  }
  return result;
}

void Parser::SetFunctionNameFromPropertyName(LiteralProperty* property,
                                             const AstRawString* name,
                                             const AstRawString* prefix) {
  if (has_error()) return;
  // Ensure that the function we are going to create has shared name iff
  // we are not going to set it later.
  if (property->NeedsSetFunctionName()) {
    name = nullptr;
    prefix = nullptr;
  } else {
    // If the property value is an anonymous function or an anonymous class or
    // a concise method or an accessor function which doesn't require the name
    // to be set then the shared name must be provided.
    DCHECK_IMPLIES(property->value()->IsAnonymousFunctionDefinition() ||
                       property->value()->IsConciseMethodDefinition() ||
                       property->value()->IsAccessorFunctionDefinition(),
                   name != nullptr);
  }

  Expression* value = property->value();
  SetFunctionName(value, name, prefix);
}

void Parser::SetFunctionNameFromPropertyName(ObjectLiteralProperty* property,
                                             const AstRawString* name,
                                             const AstRawString* prefix) {
  // Ignore "__proto__" as a name when it's being used to set the [[Prototype]]
  // of an object literal.
  // See ES #sec-__proto__-property-names-in-object-initializers.
  if (property->IsPrototype() || has_error()) return;

  DCHECK(!property->value()->IsAnonymousFunctionDefinition() ||
         property->kind() == ObjectLiteralProperty::COMPUTED);

  SetFunctionNameFromPropertyName(static_cast<LiteralProperty*>(property), name,
                                  prefix);
}

void Parser::SetFunctionNameFromIdentifierRef(Expression* value,
                                              Expression* identifier) {
  if (!identifier->IsVariableProxy()) return;
  // IsIdentifierRef of parenthesized expressions is false.
  if (identifier->is_parenthesized()) return;
  SetFunctionName(value, identifier->AsVariableProxy()->raw_name());
}

void Parser::SetFunctionName(Expression* value, const AstRawString* name,
                             const AstRawString* prefix) {
  if (!value->IsAnonymousFunctionDefinition() &&
      !value->IsConciseMethodDefinition() &&
      !value->IsAccessorFunctionDefinition()) {
    return;
  }
  auto function = value->AsFunctionLiteral();
  if (value->IsClassLiteral()) {
    function = value->AsClassLiteral()->constructor();
  }
  if (function != nullptr) {
    AstConsString* cons_name = nullptr;
    if (name != nullptr) {
      if (prefix != nullptr) {
        cons_name = ast_value_factory()->NewConsString(prefix, name);
      } else {
        cons_name = ast_value_factory()->NewConsString(name);
      }
    } else {
      DCHECK_NULL(prefix);
    }
    function->set_raw_name(cons_name);
  }
}

}  // namespace v8::internal
