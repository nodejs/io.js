// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/preparser.h"

#include <cmath>

#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/numbers/conversions-inl.h"
#include "src/numbers/conversions.h"
#include "src/parsing/parser-base.h"
#include "src/parsing/preparse-data.h"
#include "src/strings/unicode.h"
#include "src/utils/allocation.h"
#include "src/utils/utils.h"
#include "src/zone/zone-list-inl.h"

namespace v8 {
namespace internal {

namespace {

PreParserIdentifier GetIdentifierHelper(Scanner* scanner,
                                        const AstRawString* string,
                                        AstValueFactory* avf) {
  // These symbols require slightly different treatement:
  // - regular keywords (async, etc.; treated in 1st switch.)
  // - 'contextual' keywords (and may contain escaped; treated in 2nd switch.)
  // - 'contextual' keywords, but may not be escaped (3rd switch).
  switch (scanner->current_token()) {
    case Token::kAsync:
      return PreParserIdentifier::Async();
    case Token::kPrivateName:
      return PreParserIdentifier::PrivateName();
    default:
      break;
  }
  if (string == avf->constructor_string()) {
    return PreParserIdentifier::Constructor();
  }
  if (scanner->literal_contains_escapes()) {
    return PreParserIdentifier::Default();
  }
  if (string == avf->eval_string()) {
    return PreParserIdentifier::Eval();
  }
  if (string == avf->arguments_string()) {
    return PreParserIdentifier::Arguments();
  }
  return PreParserIdentifier::Default();
}

}  // namespace

PreParserIdentifier PreParser::GetIdentifier() const {
  const AstRawString* result = scanner()->CurrentSymbol(ast_value_factory());
  PreParserIdentifier symbol =
      GetIdentifierHelper(scanner(), result, ast_value_factory());
  DCHECK_NOT_NULL(result);
  symbol.string_ = result;
  return symbol;
}

PreParser::PreParseResult PreParser::PreParseProgram() {
  DCHECK_NULL(scope_);
  DeclarationScope* scope = NewScriptScope(REPLMode::kNo);
#ifdef DEBUG
  scope->set_is_being_lazily_parsed(true);
#endif

  // ModuleDeclarationInstantiation for Source Text Module Records creates a
  // new Module Environment Record whose outer lexical environment record is
  // the global scope.
  if (flags().is_module()) scope = NewModuleScope(scope);

  FunctionState top_scope(&function_state_, &scope_, scope);
  original_scope_ = scope_;
  int start_position = peek_position();
  PreParserScopedStatementList body(pointer_buffer());
  ParseStatementList(&body, Token::kEos);
  CheckConflictingVarDeclarations(scope);
  original_scope_ = nullptr;
  if (stack_overflow()) return kPreParseStackOverflow;
  if (is_strict(language_mode())) {
    CheckStrictOctalLiteral(start_position, scanner()->location().end_pos);
  }
  return kPreParseSuccess;
}

void PreParserFormalParameters::ValidateDuplicate(PreParser* preparser) const {
  if (has_duplicate_) preparser->ReportUnidentifiableError();
}

void PreParserFormalParameters::ValidateStrictMode(PreParser* preparser) const {
  if (strict_parameter_error_) preparser->ReportUnidentifiableError();
}

PreParser::PreParseResult PreParser::PreParseFunction(
    const AstRawString* function_name, FunctionKind kind,
    FunctionSyntaxKind function_syntax_kind, DeclarationScope* function_scope,
    int* use_counts, ProducedPreparseData** produced_preparse_data) {
  DCHECK_EQ(FUNCTION_SCOPE, function_scope->scope_type());
  use_counts_ = use_counts;
#ifdef DEBUG
  function_scope->set_is_being_lazily_parsed(true);
#endif

  PreParserFormalParameters formals(function_scope);

  // In the preparser, we use the function literal ids to count how many
  // FunctionLiterals were encountered. The PreParser doesn't actually persist
  // FunctionLiterals, so there IDs don't matter.
  ResetInfoId();

  // The caller passes the function_scope which is not yet inserted into the
  // scope stack. All scopes above the function_scope are ignored by the
  // PreParser.
  DCHECK_NULL(function_state_);
  DCHECK_NULL(scope_);
  FunctionState function_state(&function_state_, &scope_, function_scope);

  // Start collecting data for a new function which might contain skippable
  // functions.
  PreparseDataBuilder::DataGatheringScope preparse_data_builder_scope(this);

  if (IsArrowFunction(kind)) {
    formals.is_simple = function_scope->has_simple_parameters();
  } else {
    preparse_data_builder_scope.Start(function_scope);

    // Parse non-arrow function parameters. For arrow functions, the parameters
    // have already been parsed.
    ParameterDeclarationParsingScope formals_scope(this);
    // We return kPreParseSuccess in failure cases too - errors are retrieved
    // separately by Parser::SkipLazyFunctionBody.
    ParseFormalParameterList(&formals);
    if (formals_scope.has_duplicate()) formals.set_has_duplicate();
    if (!formals.is_simple) {
      BuildParameterInitializationBlock(formals);
    }

    Expect(Token::kRightParen);
    int formals_end_position = scanner()->location().end_pos;

    CheckArityRestrictions(formals.arity, kind, formals.has_rest,
                           function_scope->start_position(),
                           formals_end_position);
  }

  Expect(Token::kLeftBrace);
  DeclarationScope* inner_scope = function_scope;

  if (!formals.is_simple) {
    inner_scope = NewVarblockScope();
    inner_scope->set_start_position(position());
  }

  {
    BlockState block_state(&scope_, inner_scope);
    ParseStatementListAndLogFunction(&formals);
  }

  bool allow_duplicate_parameters = false;
  CheckConflictingVarDeclarations(inner_scope);

  if (!has_error()) {
    if (formals.is_simple) {
      if (is_sloppy(function_scope->language_mode())) {
        function_scope->HoistSloppyBlockFunctions(nullptr);
      }

      allow_duplicate_parameters =
          is_sloppy(function_scope->language_mode()) && !IsConciseMethod(kind);
    } else {
      if (is_sloppy(inner_scope->language_mode())) {
        inner_scope->HoistSloppyBlockFunctions(nullptr);
      }

      SetLanguageMode(function_scope, inner_scope->language_mode());
      inner_scope->set_end_position(scanner()->peek_location().end_pos);
      if (inner_scope->FinalizeBlockScope() != nullptr) {
        const AstRawString* conflict = inner_scope->FindVariableDeclaredIn(
            function_scope, VariableMode::kLastLexicalVariableMode);
        if (conflict != nullptr)
          ReportVarRedeclarationIn(conflict, inner_scope);
      }
    }
  }

  use_counts_ = nullptr;

  if (stack_overflow()) {
    return kPreParseStackOverflow;
  } else if (pending_error_handler()->has_error_unidentifiable_by_preparser()) {
    return kPreParseNotIdentifiableError;
  } else if (has_error()) {
    DCHECK(pending_error_handler()->has_pending_error());
  } else {
    DCHECK_EQ(Token::kRightBrace, scanner()->peek());

    if (!IsArrowFunction(kind)) {
      // Validate parameter names. We can do this only after parsing the
      // function, since the function can declare itself strict.
      ValidateFormalParameters(language_mode(), formals,
                               allow_duplicate_parameters);
      if (has_error()) {
        if (pending_error_handler()->has_error_unidentifiable_by_preparser()) {
          return kPreParseNotIdentifiableError;
        } else {
          return kPreParseSuccess;
        }
      }

      // Declare arguments after parsing the function since lexical
      // 'arguments' masks the arguments object. Declare arguments before
      // declaring the function var since the arguments object masks 'function
      // arguments'.
      function_scope->DeclareArguments(ast_value_factory());

      DeclareFunctionNameVar(function_name, function_syntax_kind,
                             function_scope);

      if (preparse_data_builder_->HasData()) {
        *produced_preparse_data =
            ProducedPreparseData::For(preparse_data_builder_, main_zone());
      }
    }

    if (pending_error_handler()->has_error_unidentifiable_by_preparser()) {
      return kPreParseNotIdentifiableError;
    }

    if (is_strict(function_scope->language_mode())) {
      int end_pos = scanner()->location().end_pos;
      CheckStrictOctalLiteral(function_scope->start_position(), end_pos);
    }
  }

  DCHECK(!pending_error_handler()->has_error_unidentifiable_by_preparser());
  return kPreParseSuccess;
}

// Preparsing checks a JavaScript program and emits preparse-data that helps
// a later parsing to be faster.
// See preparser-data.h for the data.

// The PreParser checks that the syntax follows the grammar for JavaScript,
// and collects some information about the program along the way.
// The grammar check is only performed in order to understand the program
// sufficiently to deduce some information about it, that can be used
// to speed up later parsing. Finding errors is not the goal of pre-parsing,
// rather it is to speed up properly written and correct programs.
// That means that contextual checks (like a label being declared where
// it is used) are generally omitted.

PreParser::Expression PreParser::ParseFunctionLiteral(
    Identifier function_name, Scanner::Location function_name_location,
    FunctionNameValidity function_name_validity, FunctionKind kind,
    int function_token_pos, FunctionSyntaxKind function_syntax_kind,
    LanguageMode language_mode,
    ZonePtrList<const AstRawString>* arguments_for_wrapped_function) {
  FunctionParsingScope function_parsing_scope(this);
  // Wrapped functions are not parsed in the preparser.
  DCHECK_NULL(arguments_for_wrapped_function);
  DCHECK_NE(FunctionSyntaxKind::kWrapped, function_syntax_kind);
  // Function ::
  //   '(' FormalParameterList? ')' '{' FunctionBody '}'
  RCS_SCOPE(runtime_call_stats_,
            RuntimeCallCounterId::kPreParseWithVariableResolution,
            RuntimeCallStats::kThreadSpecific);

  base::ElapsedTimer timer;
  if (V8_UNLIKELY(v8_flags.log_function_events)) timer.Start();

  DeclarationScope* function_scope = NewFunctionScope(kind);
  function_scope->SetLanguageMode(language_mode);
  int func_id = GetNextInfoId();
  bool skippable_function = false;

  // Start collecting data for a new function which might contain skippable
  // functions.
  {
    PreparseDataBuilder::DataGatheringScope preparse_data_builder_scope(this);
    skippable_function = !function_state_->next_function_is_likely_called() &&
                         preparse_data_builder_ != nullptr;
    if (skippable_function) {
      preparse_data_builder_scope.Start(function_scope);
    }

    FunctionState function_state(&function_state_, &scope_, function_scope);

    Expect(Token::kLeftParen);
    int start_position = position();
    function_scope->set_start_position(start_position);
    PreParserFormalParameters formals(function_scope);
    {
      ParameterDeclarationParsingScope formals_scope(this);
      ParseFormalParameterList(&formals);
      if (formals_scope.has_duplicate()) formals.set_has_duplicate();
    }
    Expect(Token::kRightParen);
    int formals_end_position = scanner()->location().end_pos;

    CheckArityRestrictions(formals.arity, kind, formals.has_rest,
                           start_position, formals_end_position);

    Expect(Token::kLeftBrace);

    // Parse function body.
    PreParserScopedStatementList body(pointer_buffer());
    int pos = function_token_pos == kNoSourcePosition ? peek_position()
                                                      : function_token_pos;
    AcceptINScope scope(this, true);
    ParseFunctionBody(&body, function_name, pos, formals, kind,
                      function_syntax_kind, FunctionBodyType::kBlock);

    // Parsing the body may change the language mode in our scope.
    language_mode = function_scope->language_mode();

    // Validate name and parameter names. We can do this only after parsing the
    // function, since the function can declare itself strict.
    CheckFunctionName(language_mode, function_name, function_name_validity,
                      function_name_location);

    if (is_strict(language_mode)) {
      CheckStrictOctalLiteral(start_position, end_position());
    }
    if (skippable_function) {
      preparse_data_builder_scope.SetSkippableFunction(
          function_scope, formals.function_length, GetLastInfoId() - func_id);
    }
  }

  if (V8_UNLIKELY(v8_flags.log_function_events)) {
    double ms = timer.Elapsed().InMillisecondsF();
    const char* event_name = "preparse-resolution";
    // We might not always get a function name here. However, it can be easily
    // reconstructed from the script id and the byte range in the log processor.
    const char* name = "";
    size_t name_byte_length = 0;
    bool is_one_byte = true;
    const AstRawString* string = function_name.string_;
    if (string != nullptr) {
      name = reinterpret_cast<const char*>(string->raw_data());
      name_byte_length = string->byte_length();
      is_one_byte = string->is_one_byte();
    }
    v8_file_logger_->FunctionEvent(
        event_name, flags().script_id(), ms, function_scope->start_position(),
        function_scope->end_position(), name, name_byte_length, is_one_byte);
  }

  return Expression::Default();
}

void PreParser::ParseStatementListAndLogFunction(
    PreParserFormalParameters* formals) {
  PreParserScopedStatementList body(pointer_buffer());
  ParseStatementList(&body, Token::kRightBrace);

  // Position right after terminal '}'.
  DCHECK_IMPLIES(!has_error(), scanner()->peek() == Token::kRightBrace);
  int body_end = scanner()->peek_location().end_pos;
  DCHECK_EQ(this->scope()->is_function_scope(), formals->is_simple);
  log_.LogFunction(body_end, formals->num_parameters(),
                   formals->function_length, GetLastInfoId());
}

PreParserBlock PreParser::BuildParameterInitializationBlock(
    const PreParserFormalParameters& parameters) {
  DCHECK(!parameters.is_simple);
  DCHECK(scope()->is_function_scope());
  if (scope()->AsDeclarationScope()->sloppy_eval_can_extend_vars() &&
      preparse_data_builder_ != nullptr) {
    // We cannot replicate the Scope structure constructed by the Parser,
    // because we've lost information whether each individual parameter was
    // simple or not. Give up trying to produce data to skip inner functions.
    if (preparse_data_builder_->parent() != nullptr) {
      // Lazy parsing started before the current function; the function which
      // cannot contain skippable functions is the parent function. (Its inner
      // functions cannot either; they are implicitly bailed out.)
      preparse_data_builder_->parent()->Bailout();
    } else {
      // Lazy parsing started at the current function; it cannot contain
      // skippable functions.
      preparse_data_builder_->Bailout();
    }
  }

  return PreParserBlock::Default();
}

bool PreParser::IdentifierEquals(const PreParserIdentifier& identifier,
                                 const AstRawString* other) {
  return identifier.string_ == other;
}

}  // namespace internal
}  // namespace v8
