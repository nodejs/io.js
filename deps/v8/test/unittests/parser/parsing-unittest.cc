// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/parsing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#include "src/api/api-inl.h"
#include "src/ast/ast-value-factory.h"
#include "src/ast/ast.h"
#include "src/base/enum-set.h"
#include "src/base/strings.h"
#include "src/execution/execution.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parser.h"
#include "src/parsing/preparser.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/parsing/token.h"
#include "src/zone/zone-list-inl.h"  // crbug.com/v8/8816
#include "test/unittests/parser/scope-test-helper.h"
#include "test/unittests/parser/unicode-helpers.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace test_parsing {

namespace {

int* global_use_counts = nullptr;

void MockUseCounterCallback(v8::Isolate* isolate,
                            v8::Isolate::UseCounterFeature feature) {
  ++global_use_counts[feature];
}

enum ParserFlag {
  kAllowLazy,
  kAllowNatives,
};

enum ParserSyncTestResult { kSuccessOrError, kSuccess, kError };

void SetGlobalFlags(base::EnumSet<ParserFlag> flags) {
  i::v8_flags.allow_natives_syntax = flags.contains(kAllowNatives);
}

void SetParserFlags(i::UnoptimizedCompileFlags* compile_flags,
                    base::EnumSet<ParserFlag> flags) {
  compile_flags->set_allow_natives_syntax(flags.contains(kAllowNatives));
}

struct Input {
  bool assigned;
  std::string source;
  std::vector<unsigned> location;  // "Directions" to the relevant scope.
};

}  // namespace

// Helpers for parsing and checking that the result has no error, implemented as
// macros to report the correct test error location.
#define FAIL_WITH_PENDING_PARSER_ERROR(info, script, isolate)                 \
  do {                                                                        \
    (info)->pending_error_handler()->PrepareErrors(                           \
        (isolate), (info)->ast_value_factory());                              \
    (info)->pending_error_handler()->ReportErrors((isolate), (script));       \
                                                                              \
    i::Handle<i::JSObject> exception_handle(                                  \
        i::JSObject::cast((isolate)->exception()), (isolate));                \
    i::Handle<i::String> message_string = i::Handle<i::String>::cast(         \
        i::JSReceiver::GetProperty((isolate), exception_handle, "message")    \
            .ToHandleChecked());                                              \
    (isolate)->clear_exception();                                             \
                                                                              \
    Tagged<String> script_source = String::cast((script)->source());          \
                                                                              \
    FATAL(                                                                    \
        "Parser failed on:\n"                                                 \
        "\t%s\n"                                                              \
        "with error:\n"                                                       \
        "\t%s\n"                                                              \
        "However, we expected no error.",                                     \
        script_source->ToCString().get(), message_string->ToCString().get()); \
  } while (false)

#define CHECK_PARSE_PROGRAM(info, script, isolate)                        \
  do {                                                                    \
    if (!i::parsing::ParseProgram((info), script, (isolate),              \
                                  parsing::ReportStatisticsMode::kYes)) { \
      FAIL_WITH_PENDING_PARSER_ERROR((info), (script), (isolate));        \
    }                                                                     \
                                                                          \
    CHECK(!(info)->pending_error_handler()->has_pending_error());         \
    CHECK_NOT_NULL((info)->literal());                                    \
  } while (false)

#define CHECK_PARSE_FUNCTION(info, shared, isolate)                        \
  do {                                                                     \
    if (!i::parsing::ParseFunction((info), (shared), (isolate),            \
                                   parsing::ReportStatisticsMode::kYes)) { \
      FAIL_WITH_PENDING_PARSER_ERROR(                                      \
          (info), handle(Script::cast((shared)->script()), (isolate)),     \
          (isolate));                                                      \
    }                                                                      \
                                                                           \
    CHECK(!(info)->pending_error_handler()->has_pending_error());          \
    CHECK_NOT_NULL((info)->literal());                                     \
  } while (false)

bool TokenIsAutoSemicolon(Token::Value token) {
  switch (token) {
    case Token::kSemicolon:
    case Token::kEos:
    case Token::kRightBrace:
      return true;
    default:
      return false;
  }
}

class ParsingTest : public TestWithContextAndZone {
 protected:
  void TestStreamScanner(i::Utf16CharacterStream* stream,
                         i::Token::Value* expected_tokens,
                         int skip_pos = 0,  // Zero means not skipping.
                         int skip_to = 0) {
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForTest(i_isolate());

    i::Scanner scanner(stream, flags);
    scanner.Initialize();

    int i = 0;
    do {
      i::Token::Value expected = expected_tokens[i];
      i::Token::Value actual = scanner.Next();
      CHECK_EQ(i::Token::String(expected), i::Token::String(actual));
      if (scanner.location().end_pos == skip_pos) {
        scanner.SeekForward(skip_to);
      }
      i++;
    } while (expected_tokens[i] != i::Token::kIllegal);
  }

  void TestScanRegExp(const char* re_source, const char* expected) {
    auto stream = i::ScannerStream::ForTesting(re_source);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForTest(i_isolate());
    i::Scanner scanner(stream.get(), flags);
    scanner.Initialize();

    i::Token::Value start = scanner.peek();
    CHECK(start == i::Token::kDiv || start == i::Token::kAssignDiv);
    CHECK(scanner.ScanRegExpPattern());
    scanner.Next();  // Current token is now the regexp literal.
    i::AstValueFactory ast_value_factory(
        zone(), i_isolate()->ast_string_constants(), HashSeed(i_isolate()));
    const i::AstRawString* current_symbol =
        scanner.CurrentSymbol(&ast_value_factory);
    ast_value_factory.Internalize(i_isolate());
    i::Handle<i::String> val = current_symbol->string();
    i::DisallowGarbageCollection no_alloc;
    i::String::FlatContent content = val->GetFlatContent(no_alloc);
    CHECK(content.IsOneByte());
    base::Vector<const uint8_t> actual = content.ToOneByteVector();
    for (int i = 0; i < actual.length(); i++) {
      CHECK_NE('\0', expected[i]);
      CHECK_EQ(expected[i], actual[i]);
    }
  }

  void CheckParsesToNumber(const char* source) {
    i::Isolate* isolate = i_isolate();
    i::Factory* factory = isolate->factory();

    std::string full_source = "function f() { return ";
    full_source += source;
    full_source += "; }";

    i::Handle<i::String> source_code =
        factory->NewStringFromUtf8(base::CStrVector(full_source.c_str()))
            .ToHandleChecked();

    i::Handle<i::Script> script = factory->NewScript(source_code);

    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_allow_lazy_parsing(false);
    flags.set_is_toplevel(true);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);

    CHECK_PARSE_PROGRAM(&info, script, isolate);

    CHECK_EQ(1, info.scope()->declarations()->LengthForTest());
    i::Declaration* decl = info.scope()->declarations()->AtForTest(0);
    i::FunctionLiteral* fun = decl->AsFunctionDeclaration()->fun();
    CHECK_EQ(fun->body()->length(), 1);
    CHECK(fun->body()->at(0)->IsReturnStatement());
    i::ReturnStatement* ret = fun->body()->at(0)->AsReturnStatement();
    i::Literal* lit = ret->expression()->AsLiteral();
    CHECK(lit->IsNumberLiteral());
  }

  void TestParserSyncWithFlags(i::Handle<i::String> source,
                               base::EnumSet<ParserFlag> flags,
                               ParserSyncTestResult result,
                               bool is_module = false,
                               bool test_preparser = true,
                               bool ignore_error_msg = false) {
    i::Isolate* isolate = i_isolate();
    i::Factory* factory = isolate->factory();
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags compile_flags =
        i::UnoptimizedCompileFlags::ForToplevelCompile(
            isolate, true, LanguageMode::kSloppy, REPLMode::kNo,
            ScriptType::kClassic, v8_flags.lazy);
    SetParserFlags(&compile_flags, flags);
    compile_flags.set_is_module(is_module);

    uintptr_t stack_limit = isolate->stack_guard()->real_climit();

    // Preparse the data.
    i::PendingCompilationErrorHandler pending_error_handler;
    if (test_preparser) {
      std::unique_ptr<i::Utf16CharacterStream> stream(
          i::ScannerStream::For(isolate, source));
      i::Scanner scanner(stream.get(), compile_flags);
      i::AstValueFactory ast_value_factory(
          zone(), isolate->ast_string_constants(), HashSeed(isolate));
      i::PreParser preparser(zone(), &scanner, stack_limit, &ast_value_factory,
                             &pending_error_handler,
                             isolate->counters()->runtime_call_stats(),
                             isolate->v8_file_logger(), compile_flags);
      scanner.Initialize();
      i::PreParser::PreParseResult pre_parse_result =
          preparser.PreParseProgram();
      CHECK_EQ(i::PreParser::kPreParseSuccess, pre_parse_result);
    }

    // Parse the data
    i::FunctionLiteral* function;
    {
      SetGlobalFlags(flags);
      i::Handle<i::Script> script =
          factory->NewScriptWithId(source, compile_flags.script_id());
      i::ParseInfo info(isolate, compile_flags, &compile_state,
                        &reusable_state);
      if (!i::parsing::ParseProgram(&info, script, isolate,
                                    parsing::ReportStatisticsMode::kYes)) {
        info.pending_error_handler()->PrepareErrors(isolate,
                                                    info.ast_value_factory());
        info.pending_error_handler()->ReportErrors(isolate, script);
      } else {
        CHECK(!info.pending_error_handler()->has_pending_error());
      }
      function = info.literal();
    }

    // Check that preparsing fails iff parsing fails.
    if (function == nullptr) {
      // Extract exception from the parser.
      CHECK(isolate->has_exception());
      i::Handle<i::JSObject> exception_handle(
          i::JSObject::cast(isolate->exception()), isolate);
      i::Handle<i::String> message_string = i::Handle<i::String>::cast(
          i::JSReceiver::GetProperty(isolate, exception_handle, "message")
              .ToHandleChecked());
      isolate->clear_exception();

      if (result == kSuccess) {
        FATAL(
            "Parser failed on:\n"
            "\t%s\n"
            "with error:\n"
            "\t%s\n"
            "However, we expected no error.",
            source->ToCString().get(), message_string->ToCString().get());
      }

      if (test_preparser && !pending_error_handler.has_pending_error() &&
          !pending_error_handler.has_error_unidentifiable_by_preparser()) {
        FATAL(
            "Parser failed on:\n"
            "\t%s\n"
            "with error:\n"
            "\t%s\n"
            "However, the preparser succeeded",
            source->ToCString().get(), message_string->ToCString().get());
      }
      // Check that preparser and parser produce the same error, except for
      // cases where we do not track errors in the preparser.
      if (test_preparser && !ignore_error_msg &&
          !pending_error_handler.has_error_unidentifiable_by_preparser()) {
        i::Handle<i::String> preparser_message =
            pending_error_handler.FormatErrorMessageForTest(i_isolate());
        if (!i::String::Equals(isolate, message_string, preparser_message)) {
          FATAL(
              "Expected parser and preparser to produce the same error on:\n"
              "\t%s\n"
              "However, found the following error messages\n"
              "\tparser:    %s\n"
              "\tpreparser: %s\n",
              source->ToCString().get(), message_string->ToCString().get(),
              preparser_message->ToCString().get());
        }
      }
    } else if (test_preparser && pending_error_handler.has_pending_error()) {
      FATAL(
          "Preparser failed on:\n"
          "\t%s\n"
          "with error:\n"
          "\t%s\n"
          "However, the parser succeeded",
          source->ToCString().get(),
          pending_error_handler.FormatErrorMessageForTest(i_isolate())
              ->ToCString()
              .get());
    } else if (result == kError) {
      FATAL(
          "Expected error on:\n"
          "\t%s\n"
          "However, parser and preparser succeeded",
          source->ToCString().get());
    }
  }

  void TestParserSync(const char* source, const ParserFlag* varying_flags,
                      size_t varying_flags_length,
                      ParserSyncTestResult result = kSuccessOrError,
                      const ParserFlag* always_true_flags = nullptr,
                      size_t always_true_flags_length = 0,
                      const ParserFlag* always_false_flags = nullptr,
                      size_t always_false_flags_length = 0,
                      bool is_module = false, bool test_preparser = true,
                      bool ignore_error_msg = false) {
    i::Handle<i::String> str = i_isolate()
                                   ->factory()
                                   ->NewStringFromUtf8(base::Vector<const char>(
                                       source, strlen(source)))
                                   .ToHandleChecked();
    for (int bits = 0; bits < (1 << varying_flags_length); bits++) {
      base::EnumSet<ParserFlag> flags;
      for (size_t flag_index = 0; flag_index < varying_flags_length;
           ++flag_index) {
        if ((bits & (1 << flag_index)) != 0)
          flags.Add(varying_flags[flag_index]);
      }
      for (size_t flag_index = 0; flag_index < always_true_flags_length;
           ++flag_index) {
        flags.Add(always_true_flags[flag_index]);
      }
      for (size_t flag_index = 0; flag_index < always_false_flags_length;
           ++flag_index) {
        flags.Remove(always_false_flags[flag_index]);
      }
      TestParserSyncWithFlags(str, flags, result, is_module, test_preparser,
                              ignore_error_msg);
    }
  }

  void RunParserSyncTest(
      const char* context_data[][2], const char* statement_data[],
      ParserSyncTestResult result, const ParserFlag* flags = nullptr,
      int flags_len = 0, const ParserFlag* always_true_flags = nullptr,
      int always_true_len = 0, const ParserFlag* always_false_flags = nullptr,
      int always_false_len = 0, bool is_module = false,
      bool test_preparser = true, bool ignore_error_msg = false) {
    i_isolate()->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                              128 * 1024);

    // Experimental feature flags should not go here; pass the flags as
    // always_true_flags if the test needs them.
    static const ParserFlag default_flags[] = {
        kAllowLazy,
        kAllowNatives,
    };
    ParserFlag* generated_flags = nullptr;
    if (flags == nullptr) {
      flags = default_flags;
      flags_len = arraysize(default_flags);
      if (always_true_flags != nullptr || always_false_flags != nullptr) {
        // Remove always_true/false_flags from default_flags (if present).
        CHECK((always_true_flags != nullptr) == (always_true_len > 0));
        CHECK((always_false_flags != nullptr) == (always_false_len > 0));
        generated_flags = new ParserFlag[flags_len + always_true_len];
        int flag_index = 0;
        for (int i = 0; i < flags_len; ++i) {
          bool use_flag = true;
          for (int j = 0; use_flag && j < always_true_len; ++j) {
            if (flags[i] == always_true_flags[j]) use_flag = false;
          }
          for (int j = 0; use_flag && j < always_false_len; ++j) {
            if (flags[i] == always_false_flags[j]) use_flag = false;
          }
          if (use_flag) generated_flags[flag_index++] = flags[i];
        }
        flags_len = flag_index;
        flags = generated_flags;
      }
    }
    for (int i = 0; context_data[i][0] != nullptr; ++i) {
      for (int j = 0; statement_data[j] != nullptr; ++j) {
        int kPrefixLen = static_cast<int>(strlen(context_data[i][0]));
        int kStatementLen = static_cast<int>(strlen(statement_data[j]));
        int kSuffixLen = static_cast<int>(strlen(context_data[i][1]));
        int kProgramSize = kPrefixLen + kStatementLen + kSuffixLen;

        // Plug the source code pieces together.
        base::ScopedVector<char> program(kProgramSize + 1);
        int length = base::SNPrintF(program, "%s%s%s", context_data[i][0],
                                    statement_data[j], context_data[i][1]);
        PrintF("%s\n", program.begin());
        CHECK_EQ(length, kProgramSize);
        TestParserSync(program.begin(), flags, flags_len, result,
                       always_true_flags, always_true_len, always_false_flags,
                       always_false_len, is_module, test_preparser,
                       ignore_error_msg);
      }
    }
    delete[] generated_flags;
  }

  void RunModuleParserSyncTest(
      const char* context_data[][2], const char* statement_data[],
      ParserSyncTestResult result, const ParserFlag* flags = nullptr,
      int flags_len = 0, const ParserFlag* always_true_flags = nullptr,
      int always_true_len = 0, const ParserFlag* always_false_flags = nullptr,
      int always_false_len = 0, bool test_preparser = true,
      bool ignore_error_msg = false) {
    RunParserSyncTest(context_data, statement_data, result, flags, flags_len,
                      always_true_flags, always_true_len, always_false_flags,
                      always_false_len, true, test_preparser, ignore_error_msg);
  }

  void TestLanguageMode(const char* source,
                        i::LanguageMode expected_language_mode) {
    i::Isolate* isolate = i_isolate();
    i::Factory* factory = isolate->factory();
    isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                          128 * 1024);

    i::Handle<i::Script> script =
        factory->NewScript(factory->NewStringFromAsciiChecked(source));
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK_PARSE_PROGRAM(&info, script, isolate);

    CHECK_EQ(expected_language_mode, info.literal()->language_mode());
  }

  void TestMaybeAssigned(Input input, const char* variable, bool module,
                         bool allow_lazy_parsing) {
    i::Isolate* isolate = i_isolate();
    i::Factory* factory = isolate->factory();
    i::Handle<i::String> string =
        factory->InternalizeUtf8String(input.source.c_str());
    string->PrintOn(stdout);
    printf("\n");
    i::Handle<i::Script> script = factory->NewScript(string);

    i::UnoptimizedCompileState state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_is_module(module);
    flags.set_allow_lazy_parsing(allow_lazy_parsing);
    i::ParseInfo info(isolate, flags, &state, &reusable_state);

    CHECK_PARSE_PROGRAM(&info, script, isolate);

    i::Scope* scope = info.literal()->scope();
    CHECK(!scope->AsDeclarationScope()->was_lazily_parsed());
    CHECK_NULL(scope->sibling());
    CHECK(module ? scope->is_module_scope() : scope->is_script_scope());

    i::Variable* var;
    {
      // Find the variable.
      scope = i::ScopeTestHelper::FindScope(scope, input.location);
      const i::AstRawString* var_name =
          info.ast_value_factory()->GetOneByteString(variable);
      var = scope->LookupForTesting(var_name);
    }

    CHECK_NOT_NULL(var);
    CHECK_IMPLIES(input.assigned, var->is_used());
    static_assert(true == i::kMaybeAssigned);
    CHECK_EQ(input.assigned, var->maybe_assigned() == i::kMaybeAssigned);
  }
};

TEST_F(ParsingTest, AutoSemicolonToken) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsAutoSemicolon(token), Token::IsAutoSemicolon(token));
  }
}

bool TokenIsAnyIdentifier(Token::Value token) {
  switch (token) {
    case Token::kIdentifier:
    case Token::kGet:
    case Token::kSet:
    case Token::kUsing:
    case Token::kOf:
    case Token::kAsync:
    case Token::kAwait:
    case Token::kYield:
    case Token::kLet:
    case Token::kStatic:
    case Token::kFutureStrictReservedWord:
    case Token::kEscapedStrictReservedWord:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, AnyIdentifierToken) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsAnyIdentifier(token), Token::IsAnyIdentifier(token));
  }
}

bool TokenIsCallable(Token::Value token) {
  switch (token) {
    case Token::kSuper:
    case Token::kIdentifier:
    case Token::kGet:
    case Token::kSet:
    case Token::kUsing:
    case Token::kOf:
    case Token::kAsync:
    case Token::kAwait:
    case Token::kYield:
    case Token::kLet:
    case Token::kStatic:
    case Token::kFutureStrictReservedWord:
    case Token::kEscapedStrictReservedWord:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, CallableToken) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsCallable(token), Token::IsCallable(token));
  }
}

bool TokenIsValidIdentifier(Token::Value token, LanguageMode language_mode,
                            bool is_generator, bool disallow_await) {
  switch (token) {
    case Token::kIdentifier:
    case Token::kGet:
    case Token::kSet:
    case Token::kUsing:
    case Token::kOf:
    case Token::kAsync:
      return true;
    case Token::kYield:
      return !is_generator && is_sloppy(language_mode);
    case Token::kAwait:
      return !disallow_await;
    case Token::kLet:
    case Token::kStatic:
    case Token::kFutureStrictReservedWord:
    case Token::kEscapedStrictReservedWord:
      return is_sloppy(language_mode);
    default:
      return false;
  }
  UNREACHABLE();
}

TEST_F(ParsingTest, IsValidIdentifierToken) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    for (size_t raw_language_mode = 0; raw_language_mode < LanguageModeSize;
         raw_language_mode++) {
      LanguageMode mode = static_cast<LanguageMode>(raw_language_mode);
      for (int is_generator = 0; is_generator < 2; is_generator++) {
        for (int disallow_await = 0; disallow_await < 2; disallow_await++) {
          CHECK_EQ(
              TokenIsValidIdentifier(token, mode, is_generator, disallow_await),
              Token::IsValidIdentifier(token, mode, is_generator,
                                       disallow_await));
        }
      }
    }
  }
}

bool TokenIsStrictReservedWord(Token::Value token) {
  switch (token) {
    case Token::kLet:
    case Token::kYield:
    case Token::kStatic:
    case Token::kFutureStrictReservedWord:
    case Token::kEscapedStrictReservedWord:
      return true;
    default:
      return false;
  }
  UNREACHABLE();
}

TEST_F(ParsingTest, IsStrictReservedWord) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsStrictReservedWord(token),
             Token::IsStrictReservedWord(token));
  }
}

bool TokenIsLiteral(Token::Value token) {
  switch (token) {
    case Token::kNullLiteral:
    case Token::kTrueLiteral:
    case Token::kFalseLiteral:
    case Token::kNumber:
    case Token::kSmi:
    case Token::kBigInt:
    case Token::kString:
      return true;
    default:
      return false;
  }
  UNREACHABLE();
}

TEST_F(ParsingTest, IsLiteralToken) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsLiteral(token), Token::IsLiteral(token));
  }
}

bool TokenIsAssignmentOp(Token::Value token) {
  switch (token) {
    case Token::kInit:
    case Token::kAssign:
#define T(name, string, precedence) case Token::name:
      BINARY_OP_TOKEN_LIST(T, EXPAND_BINOP_ASSIGN_TOKEN)
#undef T
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, AssignmentOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsAssignmentOp(token), Token::IsAssignmentOp(token));
  }
}

bool TokenIsArrowOrAssignmentOp(Token::Value token) {
  return token == Token::kArrow || TokenIsAssignmentOp(token);
}

TEST_F(ParsingTest, ArrowOrAssignmentOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsArrowOrAssignmentOp(token),
             Token::IsArrowOrAssignmentOp(token));
  }
}

bool TokenIsBinaryOp(Token::Value token) {
  switch (token) {
    case Token::kComma:
#define T(name, string, precedence) case Token::name:
      BINARY_OP_TOKEN_LIST(T, EXPAND_BINOP_TOKEN)
#undef T
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, BinaryOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsBinaryOp(token), Token::IsBinaryOp(token));
  }
}

bool TokenIsCompareOp(Token::Value token) {
  switch (token) {
    case Token::kEq:
    case Token::kEqStrict:
    case Token::kNotEq:
    case Token::kNotEqStrict:
    case Token::kLessThan:
    case Token::kGreaterThan:
    case Token::kLessThanEq:
    case Token::kGreaterThanEq:
    case Token::kInstanceOf:
    case Token::kIn:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, CompareOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsCompareOp(token), Token::IsCompareOp(token));
  }
}

bool TokenIsOrderedRelationalCompareOp(Token::Value token) {
  switch (token) {
    case Token::kLessThan:
    case Token::kGreaterThan:
    case Token::kLessThanEq:
    case Token::kGreaterThanEq:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, IsOrderedRelationalCompareOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsOrderedRelationalCompareOp(token),
             Token::IsOrderedRelationalCompareOp(token));
  }
}

bool TokenIsEqualityOp(Token::Value token) {
  switch (token) {
    case Token::kEq:
    case Token::kEqStrict:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, IsEqualityOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsEqualityOp(token), Token::IsEqualityOp(token));
  }
}

bool TokenIsBitOp(Token::Value token) {
  switch (token) {
    case Token::kBitOr:
    case Token::kBitXor:
    case Token::kBitAnd:
    case Token::kShl:
    case Token::kSar:
    case Token::kShr:
    case Token::kBitNot:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, IsBitOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsBitOp(token), Token::IsBitOp(token));
  }
}

bool TokenIsUnaryOp(Token::Value token) {
  switch (token) {
    case Token::kNot:
    case Token::kBitNot:
    case Token::kDelete:
    case Token::kTypeOf:
    case Token::kVoid:
    case Token::kAdd:
    case Token::kSub:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, IsUnaryOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsUnaryOp(token), Token::IsUnaryOp(token));
  }
}

bool TokenIsPropertyOrCall(Token::Value token) {
  switch (token) {
    case Token::kTemplateSpan:
    case Token::kTemplateTail:
    case Token::kPeriod:
    case Token::kQuestionPeriod:
    case Token::kLeftBracket:
    case Token::kLeftParen:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, IsPropertyOrCall) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsPropertyOrCall(token), Token::IsPropertyOrCall(token));
  }
}

bool TokenIsMember(Token::Value token) {
  switch (token) {
    case Token::kTemplateSpan:
    case Token::kTemplateTail:
    case Token::kPeriod:
    case Token::kLeftBracket:
      return true;
    default:
      return false;
  }
}

bool TokenIsTemplate(Token::Value token) {
  switch (token) {
    case Token::kTemplateSpan:
    case Token::kTemplateTail:
      return true;
    default:
      return false;
  }
}

bool TokenIsProperty(Token::Value token) {
  switch (token) {
    case Token::kPeriod:
    case Token::kLeftBracket:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, IsMember) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsMember(token), Token::IsMember(token));
  }
}

TEST_F(ParsingTest, IsTemplate) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsTemplate(token), Token::IsTemplate(token));
  }
}

TEST_F(ParsingTest, IsProperty) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsProperty(token), Token::IsProperty(token));
  }
}

bool TokenIsCountOp(Token::Value token) {
  switch (token) {
    case Token::kInc:
    case Token::kDec:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, IsCountOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsCountOp(token), Token::IsCountOp(token));
  }
}

TEST_F(ParsingTest, IsUnaryOrCountOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsUnaryOp(token) || TokenIsCountOp(token),
             Token::IsUnaryOrCountOp(token));
  }
}

bool TokenIsShiftOp(Token::Value token) {
  switch (token) {
    case Token::kShl:
    case Token::kSar:
    case Token::kShr:
      return true;
    default:
      return false;
  }
}

TEST_F(ParsingTest, IsShiftOp) {
  for (int i = 0; i < Token::kNumTokens; i++) {
    Token::Value token = static_cast<Token::Value>(i);
    CHECK_EQ(TokenIsShiftOp(token), Token::IsShiftOp(token));
  }
}

TEST_F(ParsingTest, ScanKeywords) {
  struct KeywordToken {
    const char* keyword;
    i::Token::Value token;
  };

  static const KeywordToken keywords[] = {
#define KEYWORD(t, s, d) {s, i::Token::t},
      TOKEN_LIST(IGNORE_TOKEN, KEYWORD)
#undef KEYWORD
          {nullptr, i::Token::kIdentifier}};

  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForTest(i_isolate());
  KeywordToken key_token;
  char buffer[32];
  for (int i = 0; (key_token = keywords[i]).keyword != nullptr; i++) {
    const char* keyword = key_token.keyword;
    size_t length = strlen(key_token.keyword);
    CHECK(static_cast<int>(sizeof(buffer)) >= length);
    {
      auto stream = i::ScannerStream::ForTesting(keyword, length);
      i::Scanner scanner(stream.get(), flags);
      scanner.Initialize();
      CHECK_EQ(key_token.token, scanner.Next());
      CHECK_EQ(i::Token::kEos, scanner.Next());
    }
    // Removing characters will make keyword matching fail.
    {
      auto stream = i::ScannerStream::ForTesting(keyword, length - 1);
      i::Scanner scanner(stream.get(), flags);
      scanner.Initialize();
      CHECK_EQ(i::Token::kIdentifier, scanner.Next());
      CHECK_EQ(i::Token::kEos, scanner.Next());
    }
    // Adding characters will make keyword matching fail.
    static const char chars_to_append[] = {'z', '0', '_'};
    for (int j = 0; j < static_cast<int>(arraysize(chars_to_append)); ++j) {
      i::MemMove(buffer, keyword, length);
      buffer[length] = chars_to_append[j];
      auto stream = i::ScannerStream::ForTesting(buffer, length + 1);
      i::Scanner scanner(stream.get(), flags);
      scanner.Initialize();
      CHECK_EQ(i::Token::kIdentifier, scanner.Next());
      CHECK_EQ(i::Token::kEos, scanner.Next());
    }
    // Replacing characters will make keyword matching fail.
    {
      i::MemMove(buffer, keyword, length);
      buffer[length - 1] = '_';
      auto stream = i::ScannerStream::ForTesting(buffer, length);
      i::Scanner scanner(stream.get(), flags);
      scanner.Initialize();
      CHECK_EQ(i::Token::kIdentifier, scanner.Next());
      CHECK_EQ(i::Token::kEos, scanner.Next());
    }
  }
}

TEST_F(ParsingTest, ScanHTMLEndComments) {
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForTest(i_isolate());

  // Regression test. See:
  //    http://code.google.com/p/chromium/issues/detail?id=53548
  // Tests that --> is correctly interpreted as comment-to-end-of-line if there
  // is only whitespace before it on the line (with comments considered as
  // whitespace, even a multiline-comment containing a newline).
  // This was not the case if it occurred before the first real token
  // in the input.
  // clang-format off
  const char* tests[] = {
      // Before first real token.
      "-->",
      "--> is eol-comment",
      "--> is eol-comment\nvar y = 37;\n",
      "\n --> is eol-comment\nvar y = 37;\n",
      "\n-->is eol-comment\nvar y = 37;\n",
      "\n-->\nvar y = 37;\n",
      "/* precomment */ --> is eol-comment\nvar y = 37;\n",
      "/* precomment */-->eol-comment\nvar y = 37;\n",
      "\n/* precomment */ --> is eol-comment\nvar y = 37;\n",
      "\n/*precomment*/-->eol-comment\nvar y = 37;\n",
      // After first real token.
      "var x = 42;\n--> is eol-comment\nvar y = 37;\n",
      "var x = 42;\n/* precomment */ --> is eol-comment\nvar y = 37;\n",
      "x/* precomment\n */ --> is eol-comment\nvar y = 37;\n",
      "var x = 42; /* precomment\n */ --> is eol-comment\nvar y = 37;\n",
      "var x = 42;/*\n*/-->is eol-comment\nvar y = 37;\n",
      // With multiple comments preceding HTMLEndComment
      "/* MLC \n */ /* SLDC */ --> is eol-comment\nvar y = 37;\n",
      "/* MLC \n */ /* SLDC1 */ /* SLDC2 */ --> is eol-comment\nvar y = 37;\n",
      "/* MLC1 \n */ /* MLC2 \n */ --> is eol-comment\nvar y = 37;\n",
      "/* SLDC */ /* MLC \n */ --> is eol-comment\nvar y = 37;\n",
      "/* MLC1 \n */ /* SLDC1 */ /* MLC2 \n */ /* SLDC2 */ --> is eol-comment\n"
          "var y = 37;\n",
      nullptr
  };

  const char* fail_tests[] = {
      "x --> is eol-comment\nvar y = 37;\n",
      "\"\\n\" --> is eol-comment\nvar y = 37;\n",
      "x/* precomment */ --> is eol-comment\nvar y = 37;\n",
      "var x = 42; --> is eol-comment\nvar y = 37;\n",
      nullptr
  };
  // clang-format on

  // Parser/Scanner needs a stack limit.
  i_isolate()->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                            128 * 1024);
  uintptr_t stack_limit = i_isolate()->stack_guard()->real_climit();
  for (int i = 0; tests[i]; i++) {
    const char* source = tests[i];
    auto stream = i::ScannerStream::ForTesting(source);
    i::Scanner scanner(stream.get(), flags);
    scanner.Initialize();

    i::AstValueFactory ast_value_factory(
        zone(), i_isolate()->ast_string_constants(), HashSeed(i_isolate()));
    i::PendingCompilationErrorHandler pending_error_handler;
    i::PreParser preparser(zone(), &scanner, stack_limit, &ast_value_factory,
                           &pending_error_handler,
                           i_isolate()->counters()->runtime_call_stats(),
                           i_isolate()->v8_file_logger(), flags);
    i::PreParser::PreParseResult result = preparser.PreParseProgram();
    CHECK_EQ(i::PreParser::kPreParseSuccess, result);
    CHECK(!pending_error_handler.has_pending_error());
  }

  for (int i = 0; fail_tests[i]; i++) {
    const char* source = fail_tests[i];
    auto stream = i::ScannerStream::ForTesting(source);
    i::Scanner scanner(stream.get(), flags);
    scanner.Initialize();

    i::AstValueFactory ast_value_factory(
        zone(), i_isolate()->ast_string_constants(), HashSeed(i_isolate()));
    i::PendingCompilationErrorHandler pending_error_handler;
    i::PreParser preparser(zone(), &scanner, stack_limit, &ast_value_factory,
                           &pending_error_handler,
                           i_isolate()->counters()->runtime_call_stats(),
                           i_isolate()->v8_file_logger(), flags);
    i::PreParser::PreParseResult result = preparser.PreParseProgram();
    // Even in the case of a syntax error, kPreParseSuccess is returned.
    CHECK_EQ(i::PreParser::kPreParseSuccess, result);
    CHECK(pending_error_handler.has_pending_error() ||
          pending_error_handler.has_error_unidentifiable_by_preparser());
  }
}

TEST_F(ParsingTest, ScanHtmlComments) {
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForTest(i_isolate());

  const char* src = "a <!-- b --> c";
  // Disallow HTML comments.
  {
    flags.set_is_module(true);
    auto stream = i::ScannerStream::ForTesting(src);
    i::Scanner scanner(stream.get(), flags);
    scanner.Initialize();
    CHECK_EQ(i::Token::kIdentifier, scanner.Next());
    CHECK_EQ(i::Token::kIllegal, scanner.Next());
  }

  // Skip HTML comments:
  {
    flags.set_is_module(false);
    auto stream = i::ScannerStream::ForTesting(src);
    i::Scanner scanner(stream.get(), flags);
    scanner.Initialize();
    CHECK_EQ(i::Token::kIdentifier, scanner.Next());
    CHECK_EQ(i::Token::kEos, scanner.Next());
  }
}

class ScriptResource : public v8::String::ExternalOneByteStringResource {
 public:
  ScriptResource(const char* data, size_t length)
      : data_(data), length_(length) {}

  const char* data() const override { return data_; }
  size_t length() const override { return length_; }

 private:
  const char* data_;
  size_t length_;
};

TEST_F(ParsingTest, StandAlonePreParser) {
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForTest(i_isolate());
  flags.set_allow_natives_syntax(true);

  i_isolate()->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                            128 * 1024);

  const char* programs[] = {"{label: 42}",
                            "var x = 42;",
                            "function foo(x, y) { return x + y; }",
                            "%ArgleBargle(glop);",
                            "var x = new new Function('this.x = 42');",
                            "var f = (x, y) => x + y;",
                            nullptr};

  uintptr_t stack_limit = i_isolate()->stack_guard()->real_climit();
  for (int i = 0; programs[i]; i++) {
    auto stream = i::ScannerStream::ForTesting(programs[i]);
    i::Scanner scanner(stream.get(), flags);
    scanner.Initialize();

    i::AstValueFactory ast_value_factory(
        zone(), i_isolate()->ast_string_constants(), HashSeed(i_isolate()));
    i::PendingCompilationErrorHandler pending_error_handler;
    i::PreParser preparser(zone(), &scanner, stack_limit, &ast_value_factory,
                           &pending_error_handler,
                           i_isolate()->counters()->runtime_call_stats(),
                           i_isolate()->v8_file_logger(), flags);
    i::PreParser::PreParseResult result = preparser.PreParseProgram();
    CHECK_EQ(i::PreParser::kPreParseSuccess, result);
    CHECK(!pending_error_handler.has_pending_error());
  }
}

TEST_F(ParsingTest, StandAlonePreParserNoNatives) {
  i::Isolate* isolate = i_isolate();
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForTest(isolate);

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  const char* programs[] = {"%ArgleBargle(glop);", "var x = %_IsSmi(42);",
                            nullptr};

  uintptr_t stack_limit = isolate->stack_guard()->real_climit();
  for (int i = 0; programs[i]; i++) {
    auto stream = i::ScannerStream::ForTesting(programs[i]);
    i::Scanner scanner(stream.get(), flags);
    scanner.Initialize();

    // Preparser defaults to disallowing natives syntax.
    i::AstValueFactory ast_value_factory(
        zone(), isolate->ast_string_constants(), HashSeed(isolate));
    i::PendingCompilationErrorHandler pending_error_handler;
    i::PreParser preparser(zone(), &scanner, stack_limit, &ast_value_factory,
                           &pending_error_handler,
                           isolate->counters()->runtime_call_stats(),
                           isolate->v8_file_logger(), flags);
    i::PreParser::PreParseResult result = preparser.PreParseProgram();
    CHECK_EQ(i::PreParser::kPreParseSuccess, result);
    CHECK(pending_error_handler.has_pending_error() ||
          pending_error_handler.has_error_unidentifiable_by_preparser());
  }
}

TEST_F(ParsingTest, RegressChromium62639) {
  i::Isolate* isolate = i_isolate();
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForTest(isolate);

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  const char* program =
      "var x = 'something';\n"
      "escape: function() {}";
  // Fails parsing expecting an identifier after "function".
  // Before fix, didn't check *ok after Expect(Token::Identifier, ok),
  // and then used the invalid currently scanned literal. This always
  // failed in debug mode, and sometimes crashed in release mode.

  auto stream = i::ScannerStream::ForTesting(program);
  i::Scanner scanner(stream.get(), flags);
  scanner.Initialize();
  i::AstValueFactory ast_value_factory(zone(), isolate->ast_string_constants(),
                                       HashSeed(isolate));
  i::PendingCompilationErrorHandler pending_error_handler;
  i::PreParser preparser(zone(), &scanner,
                         isolate->stack_guard()->real_climit(),
                         &ast_value_factory, &pending_error_handler,
                         isolate->counters()->runtime_call_stats(),
                         isolate->v8_file_logger(), flags);
  i::PreParser::PreParseResult result = preparser.PreParseProgram();
  // Even in the case of a syntax error, kPreParseSuccess is returned.
  CHECK_EQ(i::PreParser::kPreParseSuccess, result);
  CHECK(pending_error_handler.has_pending_error() ||
        pending_error_handler.has_error_unidentifiable_by_preparser());
}

TEST_F(ParsingTest, PreParseOverflow) {
  i::Isolate* isolate = i_isolate();
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForTest(isolate);

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  size_t kProgramSize = 1024 * 1024;
  std::unique_ptr<char[]> program(i::NewArray<char>(kProgramSize + 1));
  memset(program.get(), '(', kProgramSize);
  program[kProgramSize] = '\0';

  uintptr_t stack_limit = isolate->stack_guard()->real_climit();

  auto stream = i::ScannerStream::ForTesting(program.get(), kProgramSize);
  i::Scanner scanner(stream.get(), flags);
  scanner.Initialize();

  i::AstValueFactory ast_value_factory(zone(), isolate->ast_string_constants(),
                                       HashSeed(isolate));
  i::PendingCompilationErrorHandler pending_error_handler;
  i::PreParser preparser(zone(), &scanner, stack_limit, &ast_value_factory,
                         &pending_error_handler,
                         isolate->counters()->runtime_call_stats(),
                         isolate->v8_file_logger(), flags);
  i::PreParser::PreParseResult result = preparser.PreParseProgram();
  CHECK_EQ(i::PreParser::kPreParseStackOverflow, result);
}

TEST_F(ParsingTest, StreamScanner) {
  const char* str1 = "{ foo get for : */ <- \n\n /*foo*/ bib";
  std::unique_ptr<i::Utf16CharacterStream> stream1(
      i::ScannerStream::ForTesting(str1));
  i::Token::Value expectations1[] = {
      i::Token::kLeftBrace,  i::Token::kIdentifier, i::Token::kGet,
      i::Token::kFor,        i::Token::kColon,      i::Token::kMul,
      i::Token::kDiv,        i::Token::kLessThan,   i::Token::kSub,
      i::Token::kIdentifier, i::Token::kEos,        i::Token::kIllegal};
  TestStreamScanner(stream1.get(), expectations1, 0, 0);

  const char* str2 = "case default const {THIS\nPART\nSKIPPED} do";
  std::unique_ptr<i::Utf16CharacterStream> stream2(
      i::ScannerStream::ForTesting(str2));
  i::Token::Value expectations2[] = {i::Token::kCase, i::Token::kDefault,
                                     i::Token::kConst, i::Token::kLeftBrace,
                                     // Skipped part here
                                     i::Token::kRightBrace, i::Token::kDo,
                                     i::Token::kEos, i::Token::kIllegal};
  CHECK_EQ('{', str2[19]);
  CHECK_EQ('}', str2[37]);
  TestStreamScanner(stream2.get(), expectations2, 20, 37);

  const char* str3 = "{}}}}";
  i::Token::Value expectations3[] = {
      i::Token::kLeftBrace,  i::Token::kRightBrace, i::Token::kRightBrace,
      i::Token::kRightBrace, i::Token::kRightBrace, i::Token::kEos,
      i::Token::kIllegal};
  // Skip zero-four RBRACEs.
  for (int i = 0; i <= 4; i++) {
    expectations3[6 - i] = i::Token::kIllegal;
    expectations3[5 - i] = i::Token::kEos;
    std::unique_ptr<i::Utf16CharacterStream> stream3(
        i::ScannerStream::ForTesting(str3));
    TestStreamScanner(stream3.get(), expectations3, 1, 1 + i);
  }
}

TEST_F(ParsingTest, RegExpScanning) {
  // RegExp token with added garbage at the end. The scanner should only
  // scan the RegExp until the terminating slash just before "flipperwald".
  TestScanRegExp("/b/flipperwald", "b");
  // Incomplete escape sequences doesn't hide the terminating slash.
  TestScanRegExp("/\\x/flipperwald", "\\x");
  TestScanRegExp("/\\u/flipperwald", "\\u");
  TestScanRegExp("/\\u1/flipperwald", "\\u1");
  TestScanRegExp("/\\u12/flipperwald", "\\u12");
  TestScanRegExp("/\\u123/flipperwald", "\\u123");
  TestScanRegExp("/\\c/flipperwald", "\\c");
  TestScanRegExp("/\\c//flipperwald", "\\c");
  // Slashes inside character classes are not terminating.
  TestScanRegExp("/[/]/flipperwald", "[/]");
  TestScanRegExp("/[\\s-/]/flipperwald", "[\\s-/]");
  // Incomplete escape sequences inside a character class doesn't hide
  // the end of the character class.
  TestScanRegExp("/[\\c/]/flipperwald", "[\\c/]");
  TestScanRegExp("/[\\c]/flipperwald", "[\\c]");
  TestScanRegExp("/[\\x]/flipperwald", "[\\x]");
  TestScanRegExp("/[\\x1]/flipperwald", "[\\x1]");
  TestScanRegExp("/[\\u]/flipperwald", "[\\u]");
  TestScanRegExp("/[\\u1]/flipperwald", "[\\u1]");
  TestScanRegExp("/[\\u12]/flipperwald", "[\\u12]");
  TestScanRegExp("/[\\u123]/flipperwald", "[\\u123]");
  // Escaped ']'s wont end the character class.
  TestScanRegExp("/[\\]/]/flipperwald", "[\\]/]");
  // Escaped slashes are not terminating.
  TestScanRegExp("/\\//flipperwald", "\\/");
  // Starting with '=' works too.
  TestScanRegExp("/=/", "=");
  TestScanRegExp("/=?/", "=?");
}

TEST_F(ParsingTest, ScopeUsesArgumentsSuperThis) {
  static const struct {
    const char* prefix;
    const char* suffix;
  } surroundings[] = {
      {"function f() {", "}"},
      {"var f = () => {", "};"},
      {"class C { constructor() {", "} }"},
  };

  enum Expected {
    NONE = 0,
    ARGUMENTS = 1,
    SUPER_PROPERTY = 1 << 1,
    THIS = 1 << 2,
    EVAL = 1 << 4
  };

  // clang-format off
  static const struct {
    const char* body;
    int expected;
  } source_data[] = {
    {"", NONE},
    {"return this", THIS},
    {"return arguments", ARGUMENTS},
    {"return super.x", SUPER_PROPERTY},
    {"return arguments[0]", ARGUMENTS},
    {"return this + arguments[0]", ARGUMENTS | THIS},
    {"return this + arguments[0] + super.x",
     ARGUMENTS | SUPER_PROPERTY | THIS},
    {"return x => this + x", THIS},
    {"return x => super.f() + x", SUPER_PROPERTY},
    {"this.foo = 42;", THIS},
    {"this.foo();", THIS},
    {"if (foo()) { this.f() }", THIS},
    {"if (foo()) { super.f() }", SUPER_PROPERTY},
    {"if (arguments.length) { this.f() }", ARGUMENTS | THIS},
    {"while (true) { this.f() }", THIS},
    {"while (true) { super.f() }", SUPER_PROPERTY},
    {"if (true) { while (true) this.foo(arguments) }", ARGUMENTS | THIS},
    // Multiple nesting levels must work as well.
    {"while (true) { while (true) { while (true) return this } }", THIS},
    {"while (true) { while (true) { while (true) return super.f() } }",
     SUPER_PROPERTY},
    {"if (1) { return () => { while (true) new this() } }", THIS},
    {"return function (x) { return this + x }", NONE},
    {"return { m(x) { return super.m() + x } }", NONE},
    {"var x = function () { this.foo = 42 };", NONE},
    {"var x = { m() { super.foo = 42 } };", NONE},
    {"if (1) { return function () { while (true) new this() } }", NONE},
    {"if (1) { return { m() { while (true) super.m() } } }", NONE},
    {"return function (x) { return () => this }", NONE},
    {"return { m(x) { return () => super.m() } }", NONE},
    // Flags must be correctly set when using block scoping.
    {"\"use strict\"; while (true) { let x; this, arguments; }",
     THIS},
    {"\"use strict\"; while (true) { let x; this, super.f(), arguments; }",
     SUPER_PROPERTY | THIS},
    {"\"use strict\"; if (foo()) { let x; this.f() }", THIS},
    {"\"use strict\"; if (foo()) { let x; super.f() }", SUPER_PROPERTY},
    {"\"use strict\"; if (1) {"
     "  let x; return { m() { return this + super.m() + arguments } }"
     "}",
     NONE},
    {"eval(42)", EVAL},
    {"if (1) { eval(42) }", EVAL},
    {"eval('super.x')", EVAL},
    {"eval('this.x')", EVAL},
    {"eval('arguments')", EVAL},
  };
  // clang-format on

  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned j = 0; j < arraysize(surroundings); ++j) {
    for (unsigned i = 0; i < arraysize(source_data); ++i) {
      // Super property is only allowed in constructor and method.
      if (((source_data[i].expected & SUPER_PROPERTY) ||
           (source_data[i].expected == NONE)) &&
          j != 2) {
        continue;
      }
      int kProgramByteSize = static_cast<int>(strlen(surroundings[j].prefix) +
                                              strlen(surroundings[j].suffix) +
                                              strlen(source_data[i].body));
      base::ScopedVector<char> program(kProgramByteSize + 1);
      base::SNPrintF(program, "%s%s%s", surroundings[j].prefix,
                     source_data[i].body, surroundings[j].suffix);
      i::Handle<i::String> source =
          factory->NewStringFromUtf8(base::CStrVector(program.begin()))
              .ToHandleChecked();
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      // The information we're checking is only produced when eager parsing.
      flags.set_allow_lazy_parsing(false);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK_PARSE_PROGRAM(&info, script, isolate);
      i::DeclarationScope::AllocateScopeInfos(&info, isolate);
      CHECK_NOT_NULL(info.literal());

      i::DeclarationScope* script_scope = info.literal()->scope();
      CHECK(script_scope->is_script_scope());

      i::Scope* scope = script_scope->inner_scope();
      DCHECK_NOT_NULL(scope);
      DCHECK_NULL(scope->sibling());
      // Adjust for constructor scope.
      if (j == 2) {
        scope = scope->inner_scope();
        DCHECK_NOT_NULL(scope);
        DCHECK_NULL(scope->sibling());
      }
      // Arrows themselves never get an arguments object.
      if ((source_data[i].expected & ARGUMENTS) != 0 &&
          !scope->AsDeclarationScope()->is_arrow_scope()) {
        CHECK_NOT_NULL(scope->AsDeclarationScope()->arguments());
      }
      if (IsClassConstructor(scope->AsDeclarationScope()->function_kind())) {
        CHECK_IMPLIES((source_data[i].expected & SUPER_PROPERTY) != 0 ||
                          (source_data[i].expected & EVAL) != 0,
                      scope->GetHomeObjectScope()->needs_home_object());
      } else {
        CHECK_IMPLIES((source_data[i].expected & SUPER_PROPERTY) != 0,
                      scope->GetHomeObjectScope()->needs_home_object());
      }
      if ((source_data[i].expected & THIS) != 0) {
        // Currently the is_used() flag is conservative; all variables in a
        // script scope are marked as used.
        CHECK(scope->GetReceiverScope()->receiver()->is_used());
      }
      if (is_sloppy(scope->language_mode())) {
        CHECK_EQ((source_data[i].expected & EVAL) != 0,
                 scope->AsDeclarationScope()->sloppy_eval_can_extend_vars());
      }
    }
  }
}

TEST_F(ParsingTest, ParseNumbers) {
  CheckParsesToNumber("1.");
  CheckParsesToNumber("1.34");
  CheckParsesToNumber("134");
  CheckParsesToNumber("134e44");
  CheckParsesToNumber("134.e44");
  CheckParsesToNumber("134.44e44");
  CheckParsesToNumber(".44");

  CheckParsesToNumber("-1.");
  CheckParsesToNumber("-1.0");
  CheckParsesToNumber("-1.34");
  CheckParsesToNumber("-134");
  CheckParsesToNumber("-134e44");
  CheckParsesToNumber("-134.e44");
  CheckParsesToNumber("-134.44e44");
  CheckParsesToNumber("-.44");
}

TEST_F(ParsingTest, ScopePositions) {
  // Test the parser for correctly setting the start and end positions
  // of a scope. We check the scope positions of exactly one scope
  // nested in the global scope of a program. 'inner source' is the
  // source code that determines the part of the source belonging
  // to the nested scope. 'outer_prefix' and 'outer_suffix' are
  // parts of the source that belong to the global scope.
  struct SourceData {
    const char* outer_prefix;
    const char* inner_source;
    const char* outer_suffix;
    i::ScopeType scope_type;
    i::LanguageMode language_mode;
  };

  const SourceData source_data[] = {
      {"  with ({}) ", "{ block; }", " more;", i::WITH_SCOPE,
       i::LanguageMode::kSloppy},
      {"  with ({}) ", "{ block; }", "; more;", i::WITH_SCOPE,
       i::LanguageMode::kSloppy},
      {"  with ({}) ",
       "{\n"
       "    block;\n"
       "  }",
       "\n"
       "  more;",
       i::WITH_SCOPE, i::LanguageMode::kSloppy},
      {"  with ({}) ", "statement;", " more;", i::WITH_SCOPE,
       i::LanguageMode::kSloppy},
      {"  with ({}) ", "statement",
       "\n"
       "  more;",
       i::WITH_SCOPE, i::LanguageMode::kSloppy},
      {"  with ({})\n"
       "    ",
       "statement;",
       "\n"
       "  more;",
       i::WITH_SCOPE, i::LanguageMode::kSloppy},
      {"  try {} catch ", "(e) { block; }", " more;", i::CATCH_SCOPE,
       i::LanguageMode::kSloppy},
      {"  try {} catch ", "(e) { block; }", "; more;", i::CATCH_SCOPE,
       i::LanguageMode::kSloppy},
      {"  try {} catch ",
       "(e) {\n"
       "    block;\n"
       "  }",
       "\n"
       "  more;",
       i::CATCH_SCOPE, i::LanguageMode::kSloppy},
      {"  try {} catch ", "(e) { block; }", " finally { block; } more;",
       i::CATCH_SCOPE, i::LanguageMode::kSloppy},
      {"  start;\n"
       "  ",
       "{ let block; }", " more;", i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  start;\n"
       "  ",
       "{ let block; }", "; more;", i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  start;\n"
       "  ",
       "{\n"
       "    let block;\n"
       "  }",
       "\n"
       "  more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  start;\n"
       "  function fun",
       "(a,b) { infunction; }", " more;", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      {"  start;\n"
       "  function fun",
       "(a,b) {\n"
       "    infunction;\n"
       "  }",
       "\n"
       "  more;",
       i::FUNCTION_SCOPE, i::LanguageMode::kSloppy},
      {"  start;\n", "(a,b) => a + b", "; more;", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      {"  start;\n", "(a,b) => { return a+b; }", "\nmore;", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      {"  start;\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      {"  for ", "(let x = 1 ; x < 10; ++ x) { block; }", " more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  for ", "(let x = 1 ; x < 10; ++ x) { block; }", "; more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  for ",
       "(let x = 1 ; x < 10; ++ x) {\n"
       "    block;\n"
       "  }",
       "\n"
       "  more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  for ", "(let x = 1 ; x < 10; ++ x) statement;", " more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  for ", "(let x = 1 ; x < 10; ++ x) statement",
       "\n"
       "  more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  for ",
       "(let x = 1 ; x < 10; ++ x)\n"
       "    statement;",
       "\n"
       "  more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  for ", "(let x in {}) { block; }", " more;", i::BLOCK_SCOPE,
       i::LanguageMode::kStrict},
      {"  for ", "(let x in {}) { block; }", "; more;", i::BLOCK_SCOPE,
       i::LanguageMode::kStrict},
      {"  for ",
       "(let x in {}) {\n"
       "    block;\n"
       "  }",
       "\n"
       "  more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  for ", "(let x in {}) statement;", " more;", i::BLOCK_SCOPE,
       i::LanguageMode::kStrict},
      {"  for ", "(let x in {}) statement",
       "\n"
       "  more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      {"  for ",
       "(let x in {})\n"
       "    statement;",
       "\n"
       "  more;",
       i::BLOCK_SCOPE, i::LanguageMode::kStrict},
      // Check that 6-byte and 4-byte encodings of UTF-8 strings do not throw
      // the preparser off in terms of byte offsets.
      // 2 surrogates, encode a character that doesn't need a surrogate.
      {"  'foo\xED\xA0\x81\xED\xB0\x89';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // 4-byte encoding.
      {"  'foo\xF0\x90\x90\x8A';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // 3-byte encoding of \u0FFF.
      {"  'foo\xE0\xBF\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // 3-byte surrogate, followed by broken 2-byte surrogate w/ impossible 2nd
      // byte and last byte missing.
      {"  'foo\xED\xA0\x81\xED\x89';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Broken 3-byte encoding of \u0FFF with missing last byte.
      {"  'foo\xE0\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Broken 3-byte encoding of \u0FFF with missing 2 last bytes.
      {"  'foo\xE0';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Broken 3-byte encoding of \u00FF should be a 2-byte encoding.
      {"  'foo\xE0\x83\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Broken 3-byte encoding of \u007F should be a 2-byte encoding.
      {"  'foo\xE0\x81\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Unpaired lead surrogate.
      {"  'foo\xED\xA0\x81';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Unpaired lead surrogate where the following code point is a 3-byte
      // sequence.
      {"  'foo\xED\xA0\x81\xE0\xBF\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Unpaired lead surrogate where the following code point is a 4-byte
      // encoding of a trail surrogate.
      {"  'foo\xED\xA0\x81\xF0\x8D\xB0\x89';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Unpaired trail surrogate.
      {"  'foo\xED\xB0\x89';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // 2-byte encoding of \u00FF.
      {"  'foo\xC3\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Broken 2-byte encoding of \u00FF with missing last byte.
      {"  'foo\xC3';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Broken 2-byte encoding of \u007F should be a 1-byte encoding.
      {"  'foo\xC1\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Illegal 5-byte encoding.
      {"  'foo\xF8\xBF\xBF\xBF\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Illegal 6-byte encoding.
      {"  'foo\xFC\xBF\xBF\xBF\xBF\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Illegal 0xFE byte
      {"  'foo\xFE\xBF\xBF\xBF\xBF\xBF\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      // Illegal 0xFF byte
      {"  'foo\xFF\xBF\xBF\xBF\xBF\xBF\xBF\xBF';\n"
       "  (function fun",
       "(a,b) { infunction; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      {"  'foo';\n"
       "  (function fun",
       "(a,b) { 'bar\xED\xA0\x81\xED\xB0\x8B'; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      {"  'foo';\n"
       "  (function fun",
       "(a,b) { 'bar\xF0\x90\x90\x8C'; }", ")();", i::FUNCTION_SCOPE,
       i::LanguageMode::kSloppy},
      {nullptr, nullptr, nullptr, i::EVAL_SCOPE, i::LanguageMode::kSloppy}};

  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (int i = 0; source_data[i].outer_prefix; i++) {
    int kPrefixLen = Utf8LengthHelper(source_data[i].outer_prefix);
    int kInnerLen = Utf8LengthHelper(source_data[i].inner_source);
    int kSuffixLen = Utf8LengthHelper(source_data[i].outer_suffix);
    int kPrefixByteLen = static_cast<int>(strlen(source_data[i].outer_prefix));
    int kInnerByteLen = static_cast<int>(strlen(source_data[i].inner_source));
    int kSuffixByteLen = static_cast<int>(strlen(source_data[i].outer_suffix));
    int kProgramSize = kPrefixLen + kInnerLen + kSuffixLen;
    int kProgramByteSize = kPrefixByteLen + kInnerByteLen + kSuffixByteLen;
    base::ScopedVector<char> program(kProgramByteSize + 1);
    base::SNPrintF(program, "%s%s%s", source_data[i].outer_prefix,
                   source_data[i].inner_source, source_data[i].outer_suffix);

    // Parse program source.
    i::Handle<i::String> source =
        factory->NewStringFromUtf8(base::CStrVector(program.begin()))
            .ToHandleChecked();
    CHECK_EQ(source->length(), kProgramSize);
    i::Handle<i::Script> script = factory->NewScript(source);

    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_outer_language_mode(source_data[i].language_mode);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK_PARSE_PROGRAM(&info, script, isolate);

    // Check scope types and positions.
    i::Scope* scope = info.literal()->scope();
    CHECK(scope->is_script_scope());
    CHECK_EQ(0, scope->start_position());
    CHECK_EQ(scope->end_position(), kProgramSize);

    i::Scope* inner_scope = scope->inner_scope();
    DCHECK_NOT_NULL(inner_scope);
    DCHECK_NULL(inner_scope->sibling());
    CHECK_EQ(inner_scope->scope_type(), source_data[i].scope_type);
    CHECK_EQ(inner_scope->start_position(), kPrefixLen);
    // The end position of a token is one position after the last
    // character belonging to that token.
    CHECK_EQ(inner_scope->end_position(), kPrefixLen + kInnerLen);
  }
}

TEST_F(ParsingTest, DiscardFunctionBody) {
  // Test that inner function bodies are discarded if possible.
  // See comments in ParseFunctionLiteral in parser.cc.
  const char* discard_sources[] = {
      "(function f() { function g() { var a; } })();",
      "(function f() { function g() { { function h() { } } } })();",
      /* TODO(conradw): In future it may be possible to apply this optimisation
       * to these productions.
      "(function f() { 0, function g() { var a; } })();",
      "(function f() { 0, { g() { var a; } } })();",
      "(function f() { 0, class c { g() { var a; } } })();", */
      nullptr};

  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();
  i::FunctionLiteral* function;

  for (int i = 0; discard_sources[i]; i++) {
    const char* source = discard_sources[i];
    i::Handle<i::String> source_code =
        factory->NewStringFromUtf8(base::CStrVector(source)).ToHandleChecked();
    i::Handle<i::Script> script = factory->NewScript(source_code);
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK_PARSE_PROGRAM(&info, script, isolate);
    function = info.literal();
    CHECK_NOT_NULL(function);
    // The rewriter will rewrite this to
    //     .result = (function f(){...})();
    //     return .result;
    // so extract the function from there.
    CHECK_EQ(2, function->body()->length());
    i::FunctionLiteral* inner = function->body()
                                    ->first()
                                    ->AsExpressionStatement()
                                    ->expression()
                                    ->AsAssignment()
                                    ->value()
                                    ->AsCall()
                                    ->expression()
                                    ->AsFunctionLiteral();
    i::Scope* inner_scope = inner->scope();
    i::FunctionLiteral* fun = nullptr;
    if (!inner_scope->declarations()->is_empty()) {
      fun = inner_scope->declarations()
                ->AtForTest(0)
                ->AsFunctionDeclaration()
                ->fun();
    } else {
      // TODO(conradw): This path won't be hit until the other test cases can be
      // uncommented.
      UNREACHABLE();
      CHECK(inner->ShouldEagerCompile());
      CHECK_GE(2, inner->body()->length());
      i::Expression* exp = inner->body()
                               ->at(1)
                               ->AsExpressionStatement()
                               ->expression()
                               ->AsBinaryOperation()
                               ->right();
      if (exp->IsFunctionLiteral()) {
        fun = exp->AsFunctionLiteral();
      } else if (exp->IsObjectLiteral()) {
        fun = exp->AsObjectLiteral()
                  ->properties()
                  ->at(0)
                  ->value()
                  ->AsFunctionLiteral();
      } else {
        fun = exp->AsClassLiteral()
                  ->public_members()
                  ->at(0)
                  ->value()
                  ->AsFunctionLiteral();
      }
    }
    CHECK(!fun->ShouldEagerCompile());
  }
}

TEST_F(ParsingTest, ParserSync) {
  const char* context_data[][2] = {{"", ""},
                                   {"{", "}"},
                                   {"if (true) ", " else {}"},
                                   {"if (true) {} else ", ""},
                                   {"if (true) ", ""},
                                   {"do ", " while (false)"},
                                   {"while (false) ", ""},
                                   {"for (;;) ", ""},
                                   {"with ({})", ""},
                                   {"switch (12) { case 12: ", "}"},
                                   {"switch (12) { default: ", "}"},
                                   {"switch (12) { ", "case 12: }"},
                                   {"label2: ", ""},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {
      "{}", "var x", "var x = 1", "const x", "const x = 1", ";", "12",
      "if (false) {} else ;", "if (false) {} else {}", "if (false) {} else 12",
      "if (false) ;", "if (false) {}", "if (false) 12", "do {} while (false)",
      "for (;;) ;", "for (;;) {}", "for (;;) 12", "continue", "continue label",
      "continue\nlabel", "break", "break label", "break\nlabel",
      // TODO(marja): activate once parsing 'return' is merged into ParserBase.
      // "return",
      // "return  12",
      // "return\n12",
      "with ({}) ;", "with ({}) {}", "with ({}) 12", "switch ({}) { default: }",
      "label3: ", "throw", "throw  12", "throw\n12", "try {} catch(e) {}",
      "try {} finally {}", "try {} catch(e) {} finally {}", "debugger",
      nullptr};

  const char* termination_data[] = {"", ";", "\n", ";\n", "\n;", nullptr};

  i_isolate()->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                            128 * 1024);

  for (int i = 0; context_data[i][0] != nullptr; ++i) {
    for (int j = 0; statement_data[j] != nullptr; ++j) {
      for (int k = 0; termination_data[k] != nullptr; ++k) {
        int kPrefixLen = static_cast<int>(strlen(context_data[i][0]));
        int kStatementLen = static_cast<int>(strlen(statement_data[j]));
        int kTerminationLen = static_cast<int>(strlen(termination_data[k]));
        int kSuffixLen = static_cast<int>(strlen(context_data[i][1]));
        int kProgramSize = kPrefixLen + kStatementLen + kTerminationLen +
                           kSuffixLen +
                           static_cast<int>(strlen("label: for (;;) {  }"));

        // Plug the source code pieces together.
        base::ScopedVector<char> program(kProgramSize + 1);
        int length = base::SNPrintF(program, "label: for (;;) { %s%s%s%s }",
                                    context_data[i][0], statement_data[j],
                                    termination_data[k], context_data[i][1]);
        CHECK_EQ(length, kProgramSize);
        TestParserSync(program.begin(), nullptr, 0);
      }
    }
  }

  // Neither Harmony numeric literals nor our natives syntax have any
  // interaction with the flags above, so test these separately to reduce
  // the combinatorial explosion.
  TestParserSync("0o1234", nullptr, 0);
  TestParserSync("0b1011", nullptr, 0);

  static const ParserFlag flags3[] = {kAllowNatives};
  TestParserSync("%DebugPrint(123)", flags3, arraysize(flags3));
}

TEST_F(ParsingTest, StrictOctal) {
  // Test that syntax error caused by octal literal is reported correctly as
  // such (issue 2220).
  v8::TryCatch try_catch(v8_isolate());
  const char* script =
      "\"use strict\";       \n"
      "a = function() {      \n"
      "  b = function() {    \n"
      "    01;               \n"
      "  };                  \n"
      "};                    \n";
  CHECK(v8::Script::Compile(v8_context(), NewString(script)).IsEmpty());
  CHECK(try_catch.HasCaught());
  v8::String::Utf8Value exception(v8_isolate(), try_catch.Exception());
  CHECK_EQ(0,
           strcmp("SyntaxError: Octal literals are not allowed in strict mode.",
                  *exception));
}

TEST_F(ParsingTest, NonOctalDecimalIntegerStrictError) {
  const char* context_data[][2] = {{"\"use strict\";", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"09", "09.1_2", nullptr};

  RunParserSyncTest(context_data, statement_data, kError, nullptr, 0, nullptr,
                    0, nullptr, 0, false, true);
}

TEST_F(ParsingTest, NumericSeparator) {
  const char* context_data[][2] = {
      {"", ""}, {"\"use strict\";", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {
      "1_0_0_0", "1_0e+1",  "1_0e+1_0", "0xF_F_FF", "0o7_7_7", "0b0_1_0_1_0",
      ".3_2_1",  "0.0_2_1", "1_0.0_1",  ".0_1_2",   nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, NumericSeparatorErrors) {
  const char* context_data[][2] = {
      {"", ""}, {"\"use strict\";", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {
      "1_0_0_0_", "1e_1",    "1e+_1", "1_e+1",  "1__0",    "0x_1",
      "0x1__1",   "0x1_",    "0_x1",  "0_x_1",  "0b_0101", "0b11_",
      "0b1__1",   "0_b1",    "0_b_1", "0o777_", "0o_777",  "0o7__77",
      "0.0_2_1_", "0.0__21", "0_.01", "0._01",  nullptr};

  RunParserSyncTest(context_data, statement_data, kError, nullptr, 0, nullptr,
                    0, nullptr, 0, false, true);
}

TEST_F(ParsingTest, NumericSeparatorImplicitOctalsErrors) {
  const char* context_data[][2] = {
      {"", ""}, {"\"use strict\";", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"00_122",  "0_012",  "07_7_7",
                                  "0_7_7_7", "0_777",  "07_7_7_",
                                  "07__77",  "0__777", nullptr};

  RunParserSyncTest(context_data, statement_data, kError, nullptr, 0, nullptr,
                    0, nullptr, 0, false, true);
}

TEST_F(ParsingTest, NumericSeparatorNonOctalDecimalInteger) {
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"09.1_2", nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess, nullptr, 0, nullptr,
                    0, nullptr, 0, false, true);
}

TEST_F(ParsingTest, NumericSeparatorNonOctalDecimalIntegerErrors) {
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"09_12", nullptr};

  RunParserSyncTest(context_data, statement_data, kError, nullptr, 0, nullptr,
                    0, nullptr, 0, false, true);
}

TEST_F(ParsingTest, NumericSeparatorUnicodeEscapeSequencesErrors) {
  const char* context_data[][2] = {
      {"", ""}, {"'use strict'", ""}, {nullptr, nullptr}};
  // https://github.com/tc39/proposal-numeric-separator/issues/25
  const char* statement_data[] = {"\\u{10_FFFF}", nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, OptionalChaining) {
  const char* context_data[][2] = {
      {"", ""}, {"'use strict';", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"a?.b", "a?.['b']", "a?.()", nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, OptionalChainingTaggedError) {
  const char* context_data[][2] = {
      {"", ""}, {"'use strict';", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"a?.b``", "a?.['b']``", "a?.()``", nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, Nullish) {
  const char* context_data[][2] = {
      {"", ""}, {"'use strict';", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"a ?? b", "a ?? b ?? c",
                                  "a ?? b ? c : d"
                                  "a ?? b ?? c ? d : e",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, NullishNotContained) {
  const char* context_data[][2] = {
      {"", ""}, {"'use strict';", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"a || b ?? c", "a ?? b || c",
                                  "a && b ?? c"
                                  "a ?? b && c",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, ErrorsEvalAndArguments) {
  // Tests that both preparsing and parsing produce the right kind of errors for
  // using "eval" and "arguments" as identifiers. Without the strict mode, it's
  // ok to use "eval" or "arguments" as identifiers. With the strict mode, it
  // isn't.
  const char* context_data[][2] = {
      {"\"use strict\";", ""},
      {"var eval; function test_func() {\"use strict\"; ", "}"},
      {nullptr, nullptr}};

  const char* statement_data[] = {"var eval;",
                                  "var arguments",
                                  "var foo, eval;",
                                  "var foo, arguments;",
                                  "try { } catch (eval) { }",
                                  "try { } catch (arguments) { }",
                                  "function eval() { }",
                                  "function arguments() { }",
                                  "function foo(eval) { }",
                                  "function foo(arguments) { }",
                                  "function foo(bar, eval) { }",
                                  "function foo(bar, arguments) { }",
                                  "(eval) => { }",
                                  "(arguments) => { }",
                                  "(foo, eval) => { }",
                                  "(foo, arguments) => { }",
                                  "eval = 1;",
                                  "arguments = 1;",
                                  "var foo = eval = 1;",
                                  "var foo = arguments = 1;",
                                  "++eval;",
                                  "++arguments;",
                                  "eval++;",
                                  "arguments++;",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsEvalAndArgumentsSloppy) {
  // Tests that both preparsing and parsing accept "eval" and "arguments" as
  // identifiers when needed.
  const char* context_data[][2] = {
      {"", ""}, {"function test_func() {", "}"}, {nullptr, nullptr}};

  const char* statement_data[] = {"var eval;",
                                  "var arguments",
                                  "var foo, eval;",
                                  "var foo, arguments;",
                                  "try { } catch (eval) { }",
                                  "try { } catch (arguments) { }",
                                  "function eval() { }",
                                  "function arguments() { }",
                                  "function foo(eval) { }",
                                  "function foo(arguments) { }",
                                  "function foo(bar, eval) { }",
                                  "function foo(bar, arguments) { }",
                                  "eval = 1;",
                                  "arguments = 1;",
                                  "var foo = eval = 1;",
                                  "var foo = arguments = 1;",
                                  "++eval;",
                                  "++arguments;",
                                  "eval++;",
                                  "arguments++;",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, NoErrorsEvalAndArgumentsStrict) {
  const char* context_data[][2] = {
      {"\"use strict\";", ""},
      {"function test_func() { \"use strict\";", "}"},
      {"() => { \"use strict\"; ", "}"},
      {nullptr, nullptr}};

  const char* statement_data[] = {"eval;",
                                  "arguments;",
                                  "var foo = eval;",
                                  "var foo = arguments;",
                                  "var foo = { eval: 1 };",
                                  "var foo = { arguments: 1 };",
                                  "var foo = { }; foo.eval = {};",
                                  "var foo = { }; foo.arguments = {};",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

#define FUTURE_STRICT_RESERVED_WORDS_NO_LET(V) \
  V(implements)                                \
  V(interface)                                 \
  V(package)                                   \
  V(private)                                   \
  V(protected)                                 \
  V(public)                                    \
  V(static)                                    \
  V(yield)

#define FUTURE_STRICT_RESERVED_WORDS(V) \
  V(let)                                \
  FUTURE_STRICT_RESERVED_WORDS_NO_LET(V)

#define LIMITED_FUTURE_STRICT_RESERVED_WORDS_NO_LET(V) \
  V(implements)                                        \
  V(static)                                            \
  V(yield)

#define LIMITED_FUTURE_STRICT_RESERVED_WORDS(V) \
  V(let)                                        \
  LIMITED_FUTURE_STRICT_RESERVED_WORDS_NO_LET(V)

// clang-format off
#define FUTURE_STRICT_RESERVED_STATEMENTS(NAME) \
  "var " #NAME ";",                             \
  "var foo, " #NAME ";",                        \
  "try { } catch (" #NAME ") { }",              \
  "function " #NAME "() { }",                   \
  "(function " #NAME "() { })",                 \
  "function foo(" #NAME ") { }",                \
  "function foo(bar, " #NAME ") { }",           \
  #NAME " = 1;",                                \
  #NAME " += 1;",                               \
  "var foo = " #NAME " = 1;",                   \
  "++" #NAME ";",                               \
  #NAME " ++;",

#define FUTURE_STRICT_RESERVED_LEX_BINDINGS(NAME) \
  "let " #NAME ";",                               \
  "for (let " #NAME "; false; ) {}",              \
  "for (let " #NAME " in {}) {}",                 \
  "for (let " #NAME " of []) {}",                 \
  "const " #NAME " = null;",                      \
  "for (const " #NAME " = null; false; ) {}",     \
  "for (const " #NAME " in {}) {}",               \
  "for (const " #NAME " of []) {}",
// clang-format on

TEST_F(ParsingTest, ErrorsFutureStrictReservedWords) {
  // Tests that both preparsing and parsing produce the right kind of errors for
  // using future strict reserved words as identifiers. Without the strict mode,
  // it's ok to use future strict reserved words as identifiers. With the strict
  // mode, it isn't.
  const char* strict_contexts[][2] = {
      {"function test_func() {\"use strict\"; ", "}"},
      {"() => { \"use strict\"; ", "}"},
      {nullptr, nullptr}};

  // clang-format off
  const char* statement_data[] {
    LIMITED_FUTURE_STRICT_RESERVED_WORDS(FUTURE_STRICT_RESERVED_STATEMENTS)
    LIMITED_FUTURE_STRICT_RESERVED_WORDS(FUTURE_STRICT_RESERVED_LEX_BINDINGS)
    nullptr
  };
  // clang-format on

  RunParserSyncTest(strict_contexts, statement_data, kError);

  // From ES2015, 13.3.1.1 Static Semantics: Early Errors:
  //
  // > LexicalDeclaration : LetOrConst BindingList ;
  // >
  // > - It is a Syntax Error if the BoundNames of BindingList contains "let".
  const char* non_strict_contexts[][2] = {{"", ""},
                                          {"function test_func() {", "}"},
                                          {"() => {", "}"},
                                          {nullptr, nullptr}};
  const char* invalid_statements[] = {
      FUTURE_STRICT_RESERVED_LEX_BINDINGS(let) nullptr};

  RunParserSyncTest(non_strict_contexts, invalid_statements, kError);
}

#undef LIMITED_FUTURE_STRICT_RESERVED_WORDS

TEST_F(ParsingTest, NoErrorsFutureStrictReservedWords) {
  const char* context_data[][2] = {{"", ""},
                                   {"function test_func() {", "}"},
                                   {"() => {", "}"},
                                   {nullptr, nullptr}};

  // clang-format off
  const char* statement_data[] = {
    FUTURE_STRICT_RESERVED_WORDS(FUTURE_STRICT_RESERVED_STATEMENTS)
    FUTURE_STRICT_RESERVED_WORDS_NO_LET(FUTURE_STRICT_RESERVED_LEX_BINDINGS)
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsReservedWords) {
  // Tests that both preparsing and parsing produce the right kind of errors for
  // using future reserved words as identifiers. These tests don't depend on the
  // strict mode.
  const char* context_data[][2] = {
      {"", ""},
      {"\"use strict\";", ""},
      {"var eval; function test_func() {", "}"},
      {"var eval; function test_func() {\"use strict\"; ", "}"},
      {"var eval; () => {", "}"},
      {"var eval; () => {\"use strict\"; ", "}"},
      {nullptr, nullptr}};

  const char* statement_data[] = {"var super;",
                                  "var foo, super;",
                                  "try { } catch (super) { }",
                                  "function super() { }",
                                  "function foo(super) { }",
                                  "function foo(bar, super) { }",
                                  "(super) => { }",
                                  "(bar, super) => { }",
                                  "super = 1;",
                                  "var foo = super = 1;",
                                  "++super;",
                                  "super++;",
                                  "function foo super",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsLetSloppyAllModes) {
  // In sloppy mode, it's okay to use "let" as identifier.
  const char* context_data[][2] = {{"", ""},
                                   {"function f() {", "}"},
                                   {"(function f() {", "})"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {
      "var let;",
      "var foo, let;",
      "try { } catch (let) { }",
      "function let() { }",
      "(function let() { })",
      "function foo(let) { }",
      "function foo(bar, let) { }",
      "let = 1;",
      "var foo = let = 1;",
      "let * 2;",
      "++let;",
      "let++;",
      "let: 34",
      "function let(let) { let: let(let + let(0)); }",
      "({ let: 1 })",
      "({ get let() { 1 } })",
      "let(100)",
      "L: let\nx",
      "L: let\n{x}",
      nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, NoErrorsYieldSloppyAllModes) {
  // In sloppy mode, it's okay to use "yield" as identifier, *except* inside a
  // generator (see other test).
  const char* context_data[][2] = {{"", ""},
                                   {"function not_gen() {", "}"},
                                   {"(function not_gen() {", "})"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {
      "var yield;",
      "var foo, yield;",
      "try { } catch (yield) { }",
      "function yield() { }",
      "(function yield() { })",
      "function foo(yield) { }",
      "function foo(bar, yield) { }",
      "yield = 1;",
      "var foo = yield = 1;",
      "yield * 2;",
      "++yield;",
      "yield++;",
      "yield: 34",
      "function yield(yield) { yield: yield (yield + yield(0)); }",
      "({ yield: 1 })",
      "({ get yield() { 1 } })",
      "yield(100)",
      "yield[100]",
      nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, NoErrorsYieldSloppyGeneratorsEnabled) {
  // In sloppy mode, it's okay to use "yield" as identifier, *except* inside a
  // generator (see next test).
  const char* context_data[][2] = {
      {"", ""},
      {"function not_gen() {", "}"},
      {"function * gen() { function not_gen() {", "} }"},
      {"(function not_gen() {", "})"},
      {"(function * gen() { (function not_gen() {", "}) })"},
      {nullptr, nullptr}};

  const char* statement_data[] = {
      "var yield;",
      "var foo, yield;",
      "try { } catch (yield) { }",
      "function yield() { }",
      "(function yield() { })",
      "function foo(yield) { }",
      "function foo(bar, yield) { }",
      "function * yield() { }",
      "yield = 1;",
      "var foo = yield = 1;",
      "yield * 2;",
      "++yield;",
      "yield++;",
      "yield: 34",
      "function yield(yield) { yield: yield (yield + yield(0)); }",
      "({ yield: 1 })",
      "({ get yield() { 1 } })",
      "yield(100)",
      "yield[100]",
      nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsYieldStrict) {
  const char* context_data[][2] = {
      {"\"use strict\";", ""},
      {"\"use strict\"; function not_gen() {", "}"},
      {"function test_func() {\"use strict\"; ", "}"},
      {"\"use strict\"; function * gen() { function not_gen() {", "} }"},
      {"\"use strict\"; (function not_gen() {", "})"},
      {"\"use strict\"; (function * gen() { (function not_gen() {", "}) })"},
      {"() => {\"use strict\"; ", "}"},
      {nullptr, nullptr}};

  const char* statement_data[] = {"var yield;",
                                  "var foo, yield;",
                                  "try { } catch (yield) { }",
                                  "function yield() { }",
                                  "(function yield() { })",
                                  "function foo(yield) { }",
                                  "function foo(bar, yield) { }",
                                  "function * yield() { }",
                                  "(function * yield() { })",
                                  "yield = 1;",
                                  "var foo = yield = 1;",
                                  "++yield;",
                                  "yield++;",
                                  "yield: 34;",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, ErrorsYieldSloppy) {
  const char* context_data[][2] = {{"", ""},
                                   {"function not_gen() {", "}"},
                                   {"(function not_gen() {", "})"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {"(function * yield() { })", nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsGenerator) {
  // clang-format off
  const char* context_data[][2] = {
    { "function * gen() {", "}" },
    { "(function * gen() {", "})" },
    { "(function * () {", "})" },
    { nullptr, nullptr }
  };

  const char* statement_data[] = {
    // A generator without a body is valid.
    ""
    // Valid yield expressions inside generators.
    "yield 2;",
    "yield * 2;",
    "yield * \n 2;",
    "yield yield 1;",
    "yield * yield * 1;",
    "yield 3 + (yield 4);",
    "yield * 3 + (yield * 4);",
    "(yield * 3) + (yield * 4);",
    "yield 3; yield 4;",
    "yield * 3; yield * 4;",
    "(function (yield) { })",
    "(function yield() { })",
    "yield { yield: 12 }",
    "yield /* comment */ { yield: 12 }",
    "yield * \n { yield: 12 }",
    "yield /* comment */ * \n { yield: 12 }",
    // You can return in a generator.
    "yield 1; return",
    "yield * 1; return",
    "yield 1; return 37",
    "yield * 1; return 37",
    "yield 1; return 37; yield 'dead';",
    "yield * 1; return 37; yield * 'dead';",
    // Yield is still a valid key in object literals.
    "({ yield: 1 })",
    "({ get yield() { } })",
    // And in assignment pattern computed properties
    "({ [yield]: x } = { })",
    // Yield without RHS.
    "yield;",
    "yield",
    "yield\n",
    "yield /* comment */"
    "yield // comment\n"
    "(yield)",
    "[yield]",
    "{yield}",
    "yield, yield",
    "yield; yield",
    "(yield) ? yield : yield",
    "(yield) \n ? yield : yield",
    // If there is a newline before the next token, we don't look for RHS.
    "yield\nfor (;;) {}",
    "x = class extends (yield) {}",
    "x = class extends f(yield) {}",
    "x = class extends (null, yield) { }",
    "x = class extends (a ? null : yield) { }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsYieldGenerator) {
  // clang-format off
  const char* context_data[][2] = {
    { "function * gen() {", "}" },
    { "\"use strict\"; function * gen() {", "}" },
    { nullptr, nullptr }
  };

  const char* statement_data[] = {
    // Invalid yield expressions inside generators.
    "var yield;",
    "var foo, yield;",
    "try { } catch (yield) { }",
    "function yield() { }",
    // The name of the NFE is bound in the generator, which does not permit
    // yield to be an identifier.
    "(function * yield() { })",
    // Yield isn't valid as a formal parameter for generators.
    "function * foo(yield) { }",
    "(function * foo(yield) { })",
    "yield = 1;",
    "var foo = yield = 1;",
    "++yield;",
    "yield++;",
    "yield *",
    "(yield *)",
    // Yield binds very loosely, so this parses as "yield (3 + yield 4)", which
    // is invalid.
    "yield 3 + yield 4;",
    "yield: 34",
    "yield ? 1 : 2",
    // Parses as yield (/ yield): invalid.
    "yield / yield",
    "+ yield",
    "+ yield 3",
    // Invalid (no newline allowed between yield and *).
    "yield\n*3",
    // Invalid (we see a newline, so we parse {yield:42} as a statement, not an
    // object literal, and yield is not a valid label).
    "yield\n{yield: 42}",
    "yield /* comment */\n {yield: 42}",
    "yield //comment\n {yield: 42}",
    // Destructuring binding and assignment are both disallowed
    "var [yield] = [42];",
    "var {foo: yield} = {a: 42};",
    "[yield] = [42];",
    "({a: yield} = {a: 42});",
    // Also disallow full yield expressions on LHS
    "var [yield 24] = [42];",
    "var {foo: yield 24} = {a: 42};",
    "[yield 24] = [42];",
    "({a: yield 24} = {a: 42});",
    "for (yield 'x' in {});",
    "for (yield 'x' of {});",
    "for (yield 'x' in {} in {});",
    "for (yield 'x' in {} of {});",
    "class C extends yield { }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, ErrorsNameOfStrictFunction) {
  // Tests that illegal tokens as names of a strict function produce the correct
  // errors.
  const char* context_data[][2] = {{"function ", ""},
                                   {"\"use strict\"; function", ""},
                                   {"function * ", ""},
                                   {"\"use strict\"; function * ", ""},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {
      "eval() {\"use strict\";}", "arguments() {\"use strict\";}",
      "interface() {\"use strict\";}", "yield() {\"use strict\";}",
      // Future reserved words are always illegal
      "super() { }", "super() {\"use strict\";}", nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsNameOfStrictFunction) {
  const char* context_data[][2] = {{"function ", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"eval() { }", "arguments() { }",
                                  "interface() { }", "yield() { }", nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, NoErrorsNameOfStrictGenerator) {
  const char* context_data[][2] = {{"function * ", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"eval() { }", "arguments() { }",
                                  "interface() { }", "yield() { }", nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsIllegalWordsAsLabelsSloppy) {
  // Using future reserved words as labels is always an error.
  const char* context_data[][2] = {{"", ""},
                                   {"function test_func() {", "}"},
                                   {"() => {", "}"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {"super: while(true) { break super; }",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, ErrorsIllegalWordsAsLabelsStrict) {
  // Tests that illegal tokens as labels produce the correct errors.
  const char* context_data[][2] = {
      {"\"use strict\";", ""},
      {"function test_func() {\"use strict\"; ", "}"},
      {"() => {\"use strict\"; ", "}"},
      {nullptr, nullptr}};

#define LABELLED_WHILE(NAME) #NAME ": while (true) { break " #NAME "; }",
  const char* statement_data[] = {
      "super: while(true) { break super; }",
      FUTURE_STRICT_RESERVED_WORDS(LABELLED_WHILE) nullptr};
#undef LABELLED_WHILE

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsIllegalWordsAsLabels) {
  // Using eval and arguments as labels is legal even in strict mode.
  const char* context_data[][2] = {
      {"", ""},
      {"function test_func() {", "}"},
      {"() => {", "}"},
      {"\"use strict\";", ""},
      {"\"use strict\"; function test_func() {", "}"},
      {"\"use strict\"; () => {", "}"},
      {nullptr, nullptr}};

  const char* statement_data[] = {"mylabel: while(true) { break mylabel; }",
                                  "eval: while(true) { break eval; }",
                                  "arguments: while(true) { break arguments; }",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, NoErrorsFutureStrictReservedAsLabelsSloppy) {
  const char* context_data[][2] = {{"", ""},
                                   {"function test_func() {", "}"},
                                   {"() => {", "}"},
                                   {nullptr, nullptr}};

#define LABELLED_WHILE(NAME) #NAME ": while (true) { break " #NAME "; }",
  const char* statement_data[]{
      FUTURE_STRICT_RESERVED_WORDS(LABELLED_WHILE) nullptr};
#undef LABELLED_WHILE

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsParenthesizedLabels) {
  // Parenthesized identifiers shouldn't be recognized as labels.
  const char* context_data[][2] = {{"", ""},
                                   {"function test_func() {", "}"},
                                   {"() => {", "}"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {"(mylabel): while(true) { break mylabel; }",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsParenthesizedDirectivePrologue) {
  // Parenthesized directive prologue shouldn't be recognized.
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"(\"use strict\"); var eval;", nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsNotAnIdentifierName) {
  const char* context_data[][2] = {
      {"", ""}, {"\"use strict\";", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"var foo = {}; foo.{;",
                                  "var foo = {}; foo.};",
                                  "var foo = {}; foo.=;",
                                  "var foo = {}; foo.888;",
                                  "var foo = {}; foo.-;",
                                  "var foo = {}; foo.--;",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsIdentifierNames) {
  // Keywords etc. are valid as property names.
  const char* context_data[][2] = {
      {"", ""}, {"\"use strict\";", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"var foo = {}; foo.if;",
                                  "var foo = {}; foo.yield;",
                                  "var foo = {}; foo.super;",
                                  "var foo = {}; foo.interface;",
                                  "var foo = {}; foo.eval;",
                                  "var foo = {}; foo.arguments;",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, FunctionDeclaresItselfStrict) {
  // Tests that we produce the right kinds of errors when a function declares
  // itself strict (we cannot produce there errors as soon as we see the
  // offending identifiers, because we don't know at that point whether the
  // function is strict or not).
  const char* context_data[][2] = {{"function eval() {", "}"},
                                   {"function arguments() {", "}"},
                                   {"function yield() {", "}"},
                                   {"function interface() {", "}"},
                                   {"function foo(eval) {", "}"},
                                   {"function foo(arguments) {", "}"},
                                   {"function foo(yield) {", "}"},
                                   {"function foo(interface) {", "}"},
                                   {"function foo(bar, eval) {", "}"},
                                   {"function foo(bar, arguments) {", "}"},
                                   {"function foo(bar, yield) {", "}"},
                                   {"function foo(bar, interface) {", "}"},
                                   {"function foo(bar, bar) {", "}"},
                                   {nullptr, nullptr}};

  const char* strict_statement_data[] = {"\"use strict\";", nullptr};

  const char* non_strict_statement_data[] = {";", nullptr};

  RunParserSyncTest(context_data, strict_statement_data, kError);
  RunParserSyncTest(context_data, non_strict_statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsTryWithoutCatchOrFinally) {
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"try { }", "try { } foo();",
                                  "try { } catch (e) foo();",
                                  "try { } finally foo();", nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsTryCatchFinally) {
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"try { } catch (e) { }",
                                  "try { } catch (e) { } finally { }",
                                  "try { } finally { }", nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, OptionalCatchBinding) {
  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {"'use strict';", ""},
    {"try {", "} catch (e) { }"},
    {"try {} catch (e) {", "}"},
    {"try {", "} catch ({e}) { }"},
    {"try {} catch ({e}) {", "}"},
    {"function f() {", "}"},
    { nullptr, nullptr }
  };

  const char* statement_data[] = {
    "try { } catch { }",
    "try { } catch { } finally { }",
    "try { let e; } catch { let e; }",
    "try { let e; } catch { let e; } finally { let e; }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsRegexpLiteral) {
  const char* context_data[][2] = {{"var r = ", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"/unterminated", nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsRegexpLiteral) {
  const char* context_data[][2] = {{"var r = ", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"/foo/", "/foo/g", nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, NoErrorsNewExpression) {
  const char* context_data[][2] = {
      {"", ""}, {"var f =", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {
      "new foo", "new foo();", "new foo(1);", "new foo(1, 2);",
      // The first () will be processed as a part of the NewExpression and the
      // second () will be processed as part of LeftHandSideExpression.
      "new foo()();",
      // The first () will be processed as a part of the inner NewExpression and
      // the second () will be processed as a part of the outer NewExpression.
      "new new foo()();", "new foo.bar;", "new foo.bar();", "new foo.bar.baz;",
      "new foo.bar().baz;", "new foo[bar];", "new foo[bar]();",
      "new foo[bar][baz];", "new foo[bar]()[baz];",
      "new foo[bar].baz(baz)()[bar].baz;",
      "new \"foo\"",  // Runtime error
      "new 1",        // Runtime error
      // This even runs:
      "(new new Function(\"this.x = 1\")).x;",
      "new new Test_Two(String, 2).v(0123).length;", nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsNewExpression) {
  const char* context_data[][2] = {
      {"", ""}, {"var f =", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"new foo bar", "new ) foo", "new ++foo",
                                  "new foo ++", nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, StrictObjectLiteralChecking) {
  const char* context_data[][2] = {{"\"use strict\"; var myobject = {", "};"},
                                   {"\"use strict\"; var myobject = {", ",};"},
                                   {"var myobject = {", "};"},
                                   {"var myobject = {", ",};"},
                                   {nullptr, nullptr}};

  // These are only errors in strict mode.
  const char* statement_data[] = {
      "foo: 1, foo: 2", "\"foo\": 1, \"foo\": 2", "foo: 1, \"foo\": 2",
      "1: 1, 1: 2",     "1: 1, \"1\": 2",
      "get: 1, get: 2",  // Not a getter for real, just a property called get.
      "set: 1, set: 2",  // Not a setter for real, just a property called set.
      nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsObjectLiteralChecking) {
  // clang-format off
  const char* context_data[][2] = {
    {"\"use strict\"; var myobject = {", "};"},
    {"var myobject = {", "};"},
    { nullptr, nullptr }
  };

  const char* statement_data[] = {
    ",",
    // Wrong number of parameters
    "get bar(x) {}",
    "get bar(x, y) {}",
    "set bar() {}",
    "set bar(x, y) {}",
    // Parsing FunctionLiteral for getter or setter fails
    "get foo( +",
    "get foo() \"error\"",
    // Various forbidden forms
    "static x: 0",
    "static x(){}",
    "static async x(){}",
    "static get x(){}",
    "static get x : 0",
    "static x",
    "static 0",
    "*x: 0",
    "*x",
    "*get x(){}",
    "*set x(y){}",
    "get *x(){}",
    "set *x(y){}",
    "get x*(){}",
    "set x*(y){}",
    "x = 0",
    "* *x(){}",
    "x*(){}",
    "static async x(){}",
    "static async x : 0",
    "static async get x : 0",
    "async static x(){}",
    "*async x(){}",
    "async x*(){}",
    "async x : 0",
    "async 0 : 0",
    "async get x(){}",
    "async get *x(){}",
    "async set x(y){}",
    "async get : 0",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsObjectLiteralChecking) {
  // clang-format off
  const char* context_data[][2] = {
    {"var myobject = {", "};"},
    {"var myobject = {", ",};"},
    {"\"use strict\"; var myobject = {", "};"},
    {"\"use strict\"; var myobject = {", ",};"},
    { nullptr, nullptr }
  };

  const char* statement_data[] = {
    "foo: 1, get foo() {}",
    "foo: 1, set foo(v) {}",
    "\"foo\": 1, get \"foo\"() {}",
    "\"foo\": 1, set \"foo\"(v) {}",
    "1: 1, get 1() {}",
    "1: 1, set 1(v) {}",
    "get foo() {}, get foo() {}",
    "set foo(_) {}, set foo(v) {}",
    "foo: 1, get \"foo\"() {}",
    "foo: 1, set \"foo\"(v) {}",
    "\"foo\": 1, get foo() {}",
    "\"foo\": 1, set foo(v) {}",
    "1: 1, get \"1\"() {}",
    "1: 1, set \"1\"(v) {}",
    "\"1\": 1, get 1() {}",
    "\"1\": 1, set 1(v) {}",
    "foo: 1, bar: 2",
    "\"foo\": 1, \"bar\": 2",
    "1: 1, 2: 2",
    // Syntax: IdentifierName ':' AssignmentExpression
    "foo: bar = 5 + baz",
    // Syntax: 'get' PropertyName '(' ')' '{' FunctionBody '}'
    "get foo() {}",
    "get \"foo\"() {}",
    "get 1() {}",
    // Syntax: 'set' PropertyName '(' PropertySetParameterList ')'
    //     '{' FunctionBody '}'
    "set foo(v) {}",
    "set \"foo\"(v) {}",
    "set 1(v) {}",
    // Non-colliding getters and setters -> no errors
    "foo: 1, get bar() {}",
    "foo: 1, set bar(v) {}",
    "\"foo\": 1, get \"bar\"() {}",
    "\"foo\": 1, set \"bar\"(v) {}",
    "1: 1, get 2() {}",
    "1: 1, set 2(v) {}",
    "get: 1, get foo() {}",
    "set: 1, set foo(_) {}",
    // Potentially confusing cases
    "get(){}",
    "set(){}",
    "static(){}",
    "async(){}",
    "*get() {}",
    "*set() {}",
    "*static() {}",
    "*async(){}",
    "get : 0",
    "set : 0",
    "static : 0",
    "async : 0",
    // Keywords, future reserved and strict future reserved are also allowed as
    // property names.
    "if: 4",
    "interface: 5",
    "super: 6",
    "eval: 7",
    "arguments: 8",
    "async x(){}",
    "async 0(){}",
    "async get(){}",
    "async set(){}",
    "async static(){}",
    "async async(){}",
    "async : 0",
    "async(){}",
    "*async(){}",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, TooManyArguments) {
  const char* context_data[][2] = {{"foo(", "0)"}, {nullptr, nullptr}};

  using v8::internal::InstructionStream;
  char statement[Code::kMaxArguments * 2 + 1];
  for (int i = 0; i < Code::kMaxArguments; ++i) {
    statement[2 * i] = '0';
    statement[2 * i + 1] = ',';
  }
  statement[Code::kMaxArguments * 2] = 0;

  const char* statement_data[] = {statement, nullptr};

  // The test is quite slow, so run it with a reduced set of flags.
  static const ParserFlag empty_flags[] = {kAllowLazy};
  RunParserSyncTest(context_data, statement_data, kError, empty_flags, 1);
}

TEST_F(ParsingTest, StrictDelete) {
  // "delete <Identifier>" is not allowed in strict mode.
  const char* strict_context_data[][2] = {{"\"use strict\"; ", ""},
                                          {nullptr, nullptr}};

  const char* sloppy_context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  // These are errors in the strict mode.
  const char* sloppy_statement_data[] = {"delete foo;",       "delete foo + 1;",
                                         "delete (foo);",     "delete eval;",
                                         "delete interface;", nullptr};

  // These are always OK
  const char* good_statement_data[] = {"delete this;",
                                       "delete 1;",
                                       "delete 1 + 2;",
                                       "delete foo();",
                                       "delete foo.bar;",
                                       "delete foo[bar];",
                                       "delete foo--;",
                                       "delete --foo;",
                                       "delete new foo();",
                                       "delete new foo(bar);",
                                       nullptr};

  // These are always errors
  const char* bad_statement_data[] = {"delete if;", nullptr};

  RunParserSyncTest(strict_context_data, sloppy_statement_data, kError);
  RunParserSyncTest(sloppy_context_data, sloppy_statement_data, kSuccess);

  RunParserSyncTest(strict_context_data, good_statement_data, kSuccess);
  RunParserSyncTest(sloppy_context_data, good_statement_data, kSuccess);

  RunParserSyncTest(strict_context_data, bad_statement_data, kError);
  RunParserSyncTest(sloppy_context_data, bad_statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsDeclsInCase) {
  const char* context_data[][2] = {
      {"'use strict'; switch(x) { case 1:", "}"},
      {"function foo() {'use strict'; switch(x) { case 1:", "}}"},
      {"'use strict'; switch(x) { case 1: case 2:", "}"},
      {"function foo() {'use strict'; switch(x) { case 1: case 2:", "}}"},
      {"'use strict'; switch(x) { default:", "}"},
      {"function foo() {'use strict'; switch(x) { default:", "}}"},
      {"'use strict'; switch(x) { case 1: default:", "}"},
      {"function foo() {'use strict'; switch(x) { case 1: default:", "}}"},
      {nullptr, nullptr}};

  const char* statement_data[] = {"function f() { }",
                                  "class C { }",
                                  "class C extends Q {}",
                                  "function f() { } class C {}",
                                  "function f() { }; class C {}",
                                  "class C {}; function f() {}",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, InvalidLeftHandSide) {
  const char* assignment_context_data[][2] = {
      {"", " = 1;"}, {"\"use strict\"; ", " = 1;"}, {nullptr, nullptr}};

  const char* prefix_context_data[][2] = {
      {"++", ";"},
      {"\"use strict\"; ++", ";"},
      {nullptr, nullptr},
  };

  const char* postfix_context_data[][2] = {
      {"", "++;"}, {"\"use strict\"; ", "++;"}, {nullptr, nullptr}};

  // Good left hand sides for assigment or prefix / postfix operations.
  const char* good_statement_data[] = {"foo",
                                       "foo.bar",
                                       "foo[bar]",
                                       "foo()[bar]",
                                       "foo().bar",
                                       "this.foo",
                                       "this[foo]",
                                       "new foo()[bar]",
                                       "new foo().bar",
                                       "foo()",
                                       "foo(bar)",
                                       "foo[bar]()",
                                       "foo.bar()",
                                       "this()",
                                       "this.foo()",
                                       "this[foo].bar()",
                                       "this.foo[foo].bar(this)(bar)[foo]()",
                                       nullptr};

  // Bad left hand sides for assigment or prefix / postfix operations.
  const char* bad_statement_data_common[] = {
      "2",
      "new foo",
      "new foo()",
      "null",
      "if",      // Unexpected token
      "{x: 1}",  // Unexpected token
      "this",
      "\"bar\"",
      "(foo + bar)",
      "new new foo()[bar]",  // means: new (new foo()[bar])
      "new new foo().bar",   // means: new (new foo()[bar])
      nullptr};

  // These are not okay for assignment, but okay for prefix / postix.
  const char* bad_statement_data_for_assignment[] = {"++foo", "foo++",
                                                     "foo + bar", nullptr};

  RunParserSyncTest(assignment_context_data, good_statement_data, kSuccess);
  RunParserSyncTest(assignment_context_data, bad_statement_data_common, kError);
  RunParserSyncTest(assignment_context_data, bad_statement_data_for_assignment,
                    kError);

  RunParserSyncTest(prefix_context_data, good_statement_data, kSuccess);
  RunParserSyncTest(prefix_context_data, bad_statement_data_common, kError);

  RunParserSyncTest(postfix_context_data, good_statement_data, kSuccess);
  RunParserSyncTest(postfix_context_data, bad_statement_data_common, kError);
}

TEST_F(ParsingTest, FuncNameInferrerBasic) {
  // Tests that function names are inferred properly.
  i::v8_flags.allow_natives_syntax = true;

  RunJS(
      "var foo1 = function() {}; "
      "var foo2 = function foo3() {}; "
      "function not_ctor() { "
      "  var foo4 = function() {}; "
      "  return %FunctionGetInferredName(foo4); "
      "} "
      "function Ctor() { "
      "  var foo5 = function() {}; "
      "  return %FunctionGetInferredName(foo5); "
      "} "
      "var obj1 = { foo6: function() {} }; "
      "var obj2 = { 'foo7': function() {} }; "
      "var obj3 = {}; "
      "obj3[1] = function() {}; "
      "var obj4 = {}; "
      "obj4[1] = function foo8() {}; "
      "var obj5 = {}; "
      "obj5['foo9'] = function() {}; "
      "var obj6 = { obj7 : { foo10: function() {} } };");
  ExpectString("%FunctionGetInferredName(foo1)", "foo1");
  // foo2 is not unnamed -> its name is not inferred.
  ExpectString("%FunctionGetInferredName(foo2)", "");
  ExpectString("not_ctor()", "foo4");
  ExpectString("Ctor()", "Ctor.foo5");
  ExpectString("%FunctionGetInferredName(obj1.foo6)", "obj1.foo6");
  ExpectString("%FunctionGetInferredName(obj2.foo7)", "obj2.foo7");
  ExpectString("%FunctionGetInferredName(obj3[1])", "obj3.<computed>");
  ExpectString("%FunctionGetInferredName(obj4[1])", "");
  ExpectString("%FunctionGetInferredName(obj5['foo9'])", "obj5.foo9");
  ExpectString("%FunctionGetInferredName(obj6.obj7.foo10)", "obj6.obj7.foo10");
}

TEST_F(ParsingTest, FuncNameInferrerTwoByte) {
  // Tests function name inferring in cases where some parts of the inferred
  // function name are two-byte strings.
  i::v8_flags.allow_natives_syntax = true;
  v8::Isolate* isolate = v8_isolate();

  uint16_t* two_byte_source = AsciiToTwoByteString(
      "var obj1 = { oXj2 : { foo1: function() {} } }; "
      "%FunctionGetInferredName(obj1.oXj2.foo1)");
  uint16_t* two_byte_name = AsciiToTwoByteString("obj1.oXj2.foo1");
  // Make it really non-Latin1 (replace the Xs with a non-Latin1 character).
  two_byte_source[14] = two_byte_source[78] = two_byte_name[6] = 0x010D;
  v8::Local<v8::String> source =
      v8::String::NewFromTwoByte(isolate, two_byte_source).ToLocalChecked();
  v8::Local<v8::Value> result = TryRunJS(isolate, source).ToLocalChecked();
  CHECK(result->IsString());
  v8::Local<v8::String> expected_name =
      v8::String::NewFromTwoByte(isolate, two_byte_name).ToLocalChecked();
  CHECK(result->Equals(isolate->GetCurrentContext(), expected_name).FromJust());
  i::DeleteArray(two_byte_source);
  i::DeleteArray(two_byte_name);
}

TEST_F(ParsingTest, FuncNameInferrerEscaped) {
  // The same as FuncNameInferrerTwoByte, except that we express the two-byte
  // character as a Unicode escape.
  i::v8_flags.allow_natives_syntax = true;
  v8::Isolate* isolate = v8_isolate();

  uint16_t* two_byte_source = AsciiToTwoByteString(
      "var obj1 = { o\\u010dj2 : { foo1: function() {} } }; "
      "%FunctionGetInferredName(obj1.o\\u010dj2.foo1)");
  uint16_t* two_byte_name = AsciiToTwoByteString("obj1.oXj2.foo1");
  // Fix to correspond to the non-ASCII name in two_byte_source.
  two_byte_name[6] = 0x010D;
  v8::Local<v8::String> source =
      v8::String::NewFromTwoByte(isolate, two_byte_source).ToLocalChecked();
  v8::Local<v8::Value> result = TryRunJS(isolate, source).ToLocalChecked();
  CHECK(result->IsString());
  v8::Local<v8::String> expected_name =
      v8::String::NewFromTwoByte(isolate, two_byte_name).ToLocalChecked();
  CHECK(result->Equals(isolate->GetCurrentContext(), expected_name).FromJust());
  i::DeleteArray(two_byte_source);
  i::DeleteArray(two_byte_name);
}

TEST_F(ParsingTest, SerializationOfMaybeAssignmentFlag) {
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  const char* src =
      "function h() {"
      "  var result = [];"
      "  function f() {"
      "    result.push(2);"
      "  }"
      "  function assertResult(r) {"
      "    f();"
      "    result = [];"
      "  }"
      "  assertResult([2]);"
      "  assertResult([2]);"
      "  return f;"
      "};"
      "h();";

  base::ScopedVector<char> program(Utf8LengthHelper(src) + 1);
  base::SNPrintF(program, "%s", src);
  i::Handle<i::String> source = factory->InternalizeUtf8String(program.begin());
  source->PrintOn(stdout);
  printf("\n");
  v8::Local<v8::Value> v = RunJS(src);
  i::Handle<i::Object> o = v8::Utils::OpenHandle(*v);
  i::Handle<i::JSFunction> f = i::Handle<i::JSFunction>::cast(o);
  i::Handle<i::Context> context(f->context(), isolate);
  i::AstValueFactory avf(zone(), isolate->ast_string_constants(),
                         HashSeed(isolate));
  const i::AstRawString* name = avf.GetOneByteString("result");
  avf.Internalize(isolate);
  i::Handle<i::String> str = name->string();
  CHECK(IsInternalizedString(*str));
  i::DeclarationScope* script_scope =
      zone()->New<i::DeclarationScope>(zone(), &avf);
  i::Scope* s = i::Scope::DeserializeScopeChain(
      isolate, zone(), context->scope_info(), script_scope, &avf,
      i::Scope::DeserializationMode::kIncludingVariables);
  CHECK(s != script_scope);
  CHECK_NOT_NULL(name);

  // Get result from h's function context (that is f's context)
  i::Variable* var = s->LookupForTesting(name);

  CHECK_NOT_NULL(var);
  // Maybe assigned should survive deserialization
  CHECK_EQ(var->maybe_assigned(), i::kMaybeAssigned);
  // TODO(sigurds) Figure out if is_used should survive context serialization.
}

TEST_F(ParsingTest, IfArgumentsArrayAccessedThenParametersMaybeAssigned) {
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  const char* src =
      "function f(x) {"
      "    var a = arguments;"
      "    function g(i) {"
      "      ++a[0];"
      "    };"
      "    return g;"
      "  }"
      "f(0);";

  base::ScopedVector<char> program(Utf8LengthHelper(src) + 1);
  base::SNPrintF(program, "%s", src);
  i::Handle<i::String> source = factory->InternalizeUtf8String(program.begin());
  source->PrintOn(stdout);
  printf("\n");
  v8::Local<v8::Value> v = RunJS(src);
  i::Handle<i::Object> o = v8::Utils::OpenHandle(*v);
  i::Handle<i::JSFunction> f = i::Handle<i::JSFunction>::cast(o);
  i::Handle<i::Context> context(f->context(), isolate);
  i::AstValueFactory avf(zone(), isolate->ast_string_constants(),
                         HashSeed(isolate));
  const i::AstRawString* name_x = avf.GetOneByteString("x");
  avf.Internalize(isolate);

  i::DeclarationScope* script_scope =
      zone()->New<i::DeclarationScope>(zone(), &avf);
  i::Scope* s = i::Scope::DeserializeScopeChain(
      isolate, zone(), context->scope_info(), script_scope, &avf,
      i::Scope::DeserializationMode::kIncludingVariables);
  CHECK(s != script_scope);

  // Get result from f's function context (that is g's outer context)
  i::Variable* var_x = s->LookupForTesting(name_x);
  CHECK_NOT_NULL(var_x);
  CHECK_EQ(var_x->maybe_assigned(), i::kMaybeAssigned);
}

TEST_F(ParsingTest, InnerAssignment) {
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  const char* prefix = "function f() {";
  const char* midfix = " function g() {";
  const char* suffix = "}}; f";
  struct {
    const char* source;
    bool assigned;
    bool strict;
  } outers[] = {
      // Actual assignments.
      {"var x; var x = 5;", true, false},
      {"var x; { var x = 5; }", true, false},
      {"'use strict'; let x; x = 6;", true, true},
      {"var x = 5; function x() {}", true, false},
      {"var x = 4; var x = 5;", true, false},
      {"var [x, x] = [4, 5];", true, false},
      {"var x; [x, x] = [4, 5];", true, false},
      {"var {a: x, b: x} = {a: 4, b: 5};", true, false},
      {"var x = {a: 4, b: (x = 5)};", true, false},
      {"var {x=1} = {a: 4, b: (x = 5)};", true, false},
      {"var {x} = {x: 4, b: (x = 5)};", true, false},
      // Actual non-assignments.
      {"var x;", false, false},
      {"var x = 5;", false, false},
      {"'use strict'; let x;", false, true},
      {"'use strict'; let x = 6;", false, true},
      {"'use strict'; var x = 0; { let x = 6; }", false, true},
      {"'use strict'; var x = 0; { let x; x = 6; }", false, true},
      {"'use strict'; let x = 0; { let x = 6; }", false, true},
      {"'use strict'; let x = 0; { let x; x = 6; }", false, true},
      {"var x; try {} catch (x) { x = 5; }", false, false},
      {"function x() {}", false, false},
      // Eval approximation.
      {"var x; eval('');", true, false},
      {"eval(''); var x;", true, false},
      {"'use strict'; let x; eval('');", true, true},
      {"'use strict'; eval(''); let x;", true, true},
      // Non-assignments not recognized, because the analysis is approximative.
      {"var x; var x;", true, false},
      {"var x = 5; var x;", true, false},
      {"var x; { var x; }", true, false},
      {"var x; function x() {}", true, false},
      {"function x() {}; var x;", true, false},
      {"var x; try {} catch (x) { var x = 5; }", true, false},
  };

  // We set allow_error_in_inner_function to true in cases where our handling of
  // assigned variables in lazy inner functions is currently overly pessimistic.
  // FIXME(marja): remove it when no longer needed.
  struct {
    const char* source;
    bool assigned;
    bool with;
    bool allow_error_in_inner_function;
  } inners[] = {
      // Actual assignments.
      {"x = 1;", true, false, false},
      {"x++;", true, false, false},
      {"++x;", true, false, false},
      {"x--;", true, false, false},
      {"--x;", true, false, false},
      {"{ x = 1; }", true, false, false},
      {"'use strict'; { let x; }; x = 0;", true, false, false},
      {"'use strict'; { const x = 1; }; x = 0;", true, false, false},
      {"'use strict'; { function x() {} }; x = 0;", true, false, false},
      {"with ({}) { x = 1; }", true, true, false},
      {"eval('');", true, false, false},
      {"'use strict'; { let y; eval('') }", true, false, false},
      {"function h() { x = 0; }", true, false, false},
      {"(function() { x = 0; })", true, false, false},
      {"(function() { x = 0; })", true, false, false},
      {"with ({}) (function() { x = 0; })", true, true, false},
      {"for (x of [1,2,3]) {}", true, false, false},
      {"for (x in {a: 1}) {}", true, false, false},
      {"for ([x] of [[1],[2],[3]]) {}", true, false, false},
      {"for ([x] in {ab: 1}) {}", true, false, false},
      {"for ([...x] in {ab: 1}) {}", true, false, false},
      {"[x] = [1]", true, false, false},
      // Actual non-assignments.
      {"", false, false, false},
      {"x;", false, false, false},
      {"var x;", false, false, false},
      {"var x = 8;", false, false, false},
      {"var x; x = 8;", false, false, false},
      {"'use strict'; let x;", false, false, false},
      {"'use strict'; let x = 8;", false, false, false},
      {"'use strict'; let x; x = 8;", false, false, false},
      {"'use strict'; const x = 8;", false, false, false},
      {"function x() {}", false, false, false},
      {"function x() { x = 0; }", false, false, true},
      {"function h(x) { x = 0; }", false, false, false},
      {"'use strict'; { let x; x = 0; }", false, false, false},
      {"{ var x; }; x = 0;", false, false, false},
      {"with ({}) {}", false, true, false},
      {"var x; { with ({}) { x = 1; } }", false, true, false},
      {"try {} catch(x) { x = 0; }", false, false, true},
      {"try {} catch(x) { with ({}) { x = 1; } }", false, true, true},
      // Eval approximation.
      {"eval('');", true, false, false},
      {"function h() { eval(''); }", true, false, false},
      {"(function() { eval(''); })", true, false, false},
      // Shadowing not recognized because of eval approximation.
      {"var x; eval('');", true, false, false},
      {"'use strict'; let x; eval('');", true, false, false},
      {"try {} catch(x) { eval(''); }", true, false, false},
      {"function x() { eval(''); }", true, false, false},
      {"(function(x) { eval(''); })", true, false, false},
  };

  int prefix_len = Utf8LengthHelper(prefix);
  int midfix_len = Utf8LengthHelper(midfix);
  int suffix_len = Utf8LengthHelper(suffix);
  for (unsigned i = 0; i < arraysize(outers); ++i) {
    const char* outer = outers[i].source;
    int outer_len = Utf8LengthHelper(outer);
    for (unsigned j = 0; j < arraysize(inners); ++j) {
      for (unsigned lazy = 0; lazy < 2; ++lazy) {
        if (outers[i].strict && inners[j].with) continue;
        const char* inner = inners[j].source;
        int inner_len = Utf8LengthHelper(inner);

        int len = prefix_len + outer_len + midfix_len + inner_len + suffix_len;
        base::ScopedVector<char> program(len + 1);

        base::SNPrintF(program, "%s%s%s%s%s", prefix, outer, midfix, inner,
                       suffix);

        UnoptimizedCompileState compile_state;
        ReusableUnoptimizedCompileState reusable_state(isolate);
        std::unique_ptr<i::ParseInfo> info;
        if (lazy) {
          printf("%s\n", program.begin());
          v8::Local<v8::Value> v = RunJS(program.begin());
          i::Handle<i::Object> o = v8::Utils::OpenHandle(*v);
          i::Handle<i::JSFunction> f = i::Handle<i::JSFunction>::cast(o);
          i::Handle<i::SharedFunctionInfo> shared =
              i::handle(f->shared(), isolate);
          i::UnoptimizedCompileFlags flags =
              i::UnoptimizedCompileFlags::ForFunctionCompile(isolate, *shared);
          info = std::make_unique<i::ParseInfo>(isolate, flags, &compile_state,
                                                &reusable_state);
          CHECK_PARSE_FUNCTION(info.get(), shared, isolate);
        } else {
          i::Handle<i::String> source =
              factory->InternalizeUtf8String(program.begin());
          source->PrintOn(stdout);
          printf("\n");
          i::Handle<i::Script> script = factory->NewScript(source);
          i::UnoptimizedCompileFlags flags =
              i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
          flags.set_allow_lazy_parsing(false);
          info = std::make_unique<i::ParseInfo>(isolate, flags, &compile_state,
                                                &reusable_state);
          CHECK_PARSE_PROGRAM(info.get(), script, isolate);
        }

        i::Scope* scope = info->literal()->scope();
        if (!lazy) {
          scope = scope->inner_scope();
        }
        DCHECK_NOT_NULL(scope);
        DCHECK_NULL(scope->sibling());
        DCHECK(scope->is_function_scope());
        const i::AstRawString* var_name =
            info->ast_value_factory()->GetOneByteString("x");
        i::Variable* var = scope->LookupForTesting(var_name);
        bool expected = outers[i].assigned || inners[j].assigned;
        CHECK_NOT_NULL(var);
        bool is_maybe_assigned = var->maybe_assigned() == i::kMaybeAssigned;
        CHECK(is_maybe_assigned == expected ||
              (is_maybe_assigned && inners[j].allow_error_in_inner_function));
      }
    }
  }
}

TEST_F(ParsingTest, MaybeAssignedParameters) {
  i::Isolate* isolate = i_isolate();

  struct {
    bool arg_assigned;
    const char* source;
  } tests[] = {
      {false, "function f(arg) {}"},
      {false, "function f(arg) {g(arg)}"},
      {false, "function f(arg) {function h() { g(arg) }; h()}"},
      {false, "function f(arg) {function h() { g(arg) }; return h}"},
      {false, "function f(arg=1) {}"},
      {false, "function f(arg=1) {g(arg)}"},
      {false, "function f(arg, arguments) {g(arg); arguments[0] = 42; g(arg)}"},
      {false,
       "function f(arg, ...arguments) {g(arg); arguments[0] = 42; g(arg)}"},
      {false,
       "function f(arg, arguments=[]) {g(arg); arguments[0] = 42; g(arg)}"},
      {false, "function f(...arg) {g(arg); arguments[0] = 42; g(arg)}"},
      {false,
       "function f(arg) {g(arg); g(function() {arguments[0] = 42}); g(arg)}"},

      // strict arguments object
      {false, "function f(arg, x=1) {g(arg); arguments[0] = 42; g(arg)}"},
      {false, "function f(arg, ...x) {g(arg); arguments[0] = 42; g(arg)}"},
      {false, "function f(arg=1) {g(arg); arguments[0] = 42; g(arg)}"},
      {false,
       "function f(arg) {'use strict'; g(arg); arguments[0] = 42; g(arg)}"},
      {false, "function f(arg) {g(arg); f.arguments[0] = 42; g(arg)}"},
      {false, "function f(arg, args=arguments) {g(arg); args[0] = 42; g(arg)}"},

      {true, "function f(arg) {g(arg); arg = 42; g(arg)}"},
      {true, "function f(arg) {g(arg); eval('arg = 42'); g(arg)}"},
      {true, "function f(arg) {g(arg); var arg = 42; g(arg)}"},
      {true, "function f(arg, x=1) {g(arg); arg = 42; g(arg)}"},
      {true, "function f(arg, ...x) {g(arg); arg = 42; g(arg)}"},
      {true, "function f(arg=1) {g(arg); arg = 42; g(arg)}"},
      {true, "function f(arg) {'use strict'; g(arg); arg = 42; g(arg)}"},
      {true, "function f(arg, {a=(g(arg), arg=42)}) {g(arg)}"},
      {true, "function f(arg) {g(arg); g(function() {arg = 42}); g(arg)}"},
      {true,
       "function f(arg) {g(arg); g(function() {eval('arg = 42')}); g(arg)}"},
      {true, "function f(arg) {g(arg); g(() => arg = 42); g(arg)}"},
      {true, "function f(arg) {g(arg); g(() => eval('arg = 42')); g(arg)}"},
      {true, "function f(...arg) {g(arg); eval('arg = 42'); g(arg)}"},

      // sloppy arguments object
      {true, "function f(arg) {g(arg); arguments[0] = 42; g(arg)}"},
      {true, "function f(arg) {g(arg); h(arguments); g(arg)}"},
      {true,
       "function f(arg) {((args) => {arguments[0] = 42})(arguments); "
       "g(arg)}"},
      {true, "function f(arg) {g(arg); eval('arguments[0] = 42'); g(arg)}"},
      {true, "function f(arg) {g(arg); g(() => arguments[0] = 42); g(arg)}"},

      // default values
      {false, "function f({x:arg = 1}) {}"},
      {true, "function f({x:arg = 1}, {y:b=(arg=2)}) {}"},
      {true, "function f({x:arg = (arg = 2)}) {}"},
      {false, "var f = ({x:arg = 1}) => {}"},
      {true, "var f = ({x:arg = 1}, {y:b=(arg=2)}) => {}"},
      {true, "var f = ({x:arg = (arg = 2)}) => {}"},
  };

  const char* suffix = "; f";

  for (unsigned i = 0; i < arraysize(tests); ++i) {
    bool assigned = tests[i].arg_assigned;
    const char* source = tests[i].source;
    for (unsigned allow_lazy = 0; allow_lazy < 2; ++allow_lazy) {
      base::ScopedVector<char> program(Utf8LengthHelper(source) +
                                       Utf8LengthHelper(suffix) + 1);
      base::SNPrintF(program, "%s%s", source, suffix);
      printf("%s\n", program.begin());
      v8::Local<v8::Value> v = RunJS(program.begin());
      i::Handle<i::Object> o = v8::Utils::OpenHandle(*v);
      i::Handle<i::JSFunction> f = i::Handle<i::JSFunction>::cast(o);
      i::Handle<i::SharedFunctionInfo> shared = i::handle(f->shared(), isolate);
      i::UnoptimizedCompileState state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForFunctionCompile(isolate, *shared);
      flags.set_allow_lazy_parsing(allow_lazy);
      i::ParseInfo info(isolate, flags, &state, &reusable_state);
      CHECK_PARSE_FUNCTION(&info, shared, isolate);

      i::Scope* scope = info.literal()->scope();
      CHECK(!scope->AsDeclarationScope()->was_lazily_parsed());
      CHECK_NULL(scope->sibling());
      CHECK(scope->is_function_scope());
      const i::AstRawString* var_name =
          info.ast_value_factory()->GetOneByteString("arg");
      i::Variable* var = scope->LookupForTesting(var_name);
      CHECK(var->is_used() || !assigned);
      bool is_maybe_assigned = var->maybe_assigned() == i::kMaybeAssigned;
      CHECK_EQ(is_maybe_assigned, assigned);
    }
  }
}

static Input wrap(Input input) {
  Input result;
  result.assigned = input.assigned;
  result.source = "function WRAPPED() { " + input.source + " }";
  result.location.push_back(0);
  for (auto n : input.location) {
    result.location.push_back(n);
  }
  return result;
}

TEST_F(ParsingTest, MaybeAssignedInsideLoop) {
  std::vector<unsigned> top;  // Can't use {} in initializers below.

  Input module_and_script_tests[] = {
      {true, "for (j=x; j<10; ++j) { foo = j }", top},
      {true, "for (j=x; j<10; ++j) { [foo] = [j] }", top},
      {true, "for (j=x; j<10; ++j) { [[foo]=[42]] = [] }", top},
      {true, "for (j=x; j<10; ++j) { var foo = j }", top},
      {true, "for (j=x; j<10; ++j) { var [foo] = [j] }", top},
      {true, "for (j=x; j<10; ++j) { var [[foo]=[42]] = [] }", top},
      {true, "for (j=x; j<10; ++j) { var foo; foo = j }", top},
      {true, "for (j=x; j<10; ++j) { var foo; [foo] = [j] }", top},
      {true, "for (j=x; j<10; ++j) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (j=x; j<10; ++j) { let foo; foo = j }", {0}},
      {true, "for (j=x; j<10; ++j) { let foo; [foo] = [j] }", {0}},
      {true, "for (j=x; j<10; ++j) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (j=x; j<10; ++j) { let foo = j }", {0}},
      {false, "for (j=x; j<10; ++j) { let [foo] = [j] }", {0}},
      {false, "for (j=x; j<10; ++j) { const foo = j }", {0}},
      {false, "for (j=x; j<10; ++j) { const [foo] = [j] }", {0}},
      {false, "for (j=x; j<10; ++j) { function foo() {return j} }", {0}},

      {true, "for ({j}=x; j<10; ++j) { foo = j }", top},
      {true, "for ({j}=x; j<10; ++j) { [foo] = [j] }", top},
      {true, "for ({j}=x; j<10; ++j) { [[foo]=[42]] = [] }", top},
      {true, "for ({j}=x; j<10; ++j) { var foo = j }", top},
      {true, "for ({j}=x; j<10; ++j) { var [foo] = [j] }", top},
      {true, "for ({j}=x; j<10; ++j) { var [[foo]=[42]] = [] }", top},
      {true, "for ({j}=x; j<10; ++j) { var foo; foo = j }", top},
      {true, "for ({j}=x; j<10; ++j) { var foo; [foo] = [j] }", top},
      {true, "for ({j}=x; j<10; ++j) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for ({j}=x; j<10; ++j) { let foo; foo = j }", {0}},
      {true, "for ({j}=x; j<10; ++j) { let foo; [foo] = [j] }", {0}},
      {true, "for ({j}=x; j<10; ++j) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for ({j}=x; j<10; ++j) { let foo = j }", {0}},
      {false, "for ({j}=x; j<10; ++j) { let [foo] = [j] }", {0}},
      {false, "for ({j}=x; j<10; ++j) { const foo = j }", {0}},
      {false, "for ({j}=x; j<10; ++j) { const [foo] = [j] }", {0}},
      {false, "for ({j}=x; j<10; ++j) { function foo() {return j} }", {0}},

      {true, "for (var j=x; j<10; ++j) { foo = j }", top},
      {true, "for (var j=x; j<10; ++j) { [foo] = [j] }", top},
      {true, "for (var j=x; j<10; ++j) { [[foo]=[42]] = [] }", top},
      {true, "for (var j=x; j<10; ++j) { var foo = j }", top},
      {true, "for (var j=x; j<10; ++j) { var [foo] = [j] }", top},
      {true, "for (var j=x; j<10; ++j) { var [[foo]=[42]] = [] }", top},
      {true, "for (var j=x; j<10; ++j) { var foo; foo = j }", top},
      {true, "for (var j=x; j<10; ++j) { var foo; [foo] = [j] }", top},
      {true, "for (var j=x; j<10; ++j) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (var j=x; j<10; ++j) { let foo; foo = j }", {0}},
      {true, "for (var j=x; j<10; ++j) { let foo; [foo] = [j] }", {0}},
      {true, "for (var j=x; j<10; ++j) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (var j=x; j<10; ++j) { let foo = j }", {0}},
      {false, "for (var j=x; j<10; ++j) { let [foo] = [j] }", {0}},
      {false, "for (var j=x; j<10; ++j) { const foo = j }", {0}},
      {false, "for (var j=x; j<10; ++j) { const [foo] = [j] }", {0}},
      {false, "for (var j=x; j<10; ++j) { function foo() {return j} }", {0}},

      {true, "for (var {j}=x; j<10; ++j) { foo = j }", top},
      {true, "for (var {j}=x; j<10; ++j) { [foo] = [j] }", top},
      {true, "for (var {j}=x; j<10; ++j) { [[foo]=[42]] = [] }", top},
      {true, "for (var {j}=x; j<10; ++j) { var foo = j }", top},
      {true, "for (var {j}=x; j<10; ++j) { var [foo] = [j] }", top},
      {true, "for (var {j}=x; j<10; ++j) { var [[foo]=[42]] = [] }", top},
      {true, "for (var {j}=x; j<10; ++j) { var foo; foo = j }", top},
      {true, "for (var {j}=x; j<10; ++j) { var foo; [foo] = [j] }", top},
      {true, "for (var {j}=x; j<10; ++j) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (var {j}=x; j<10; ++j) { let foo; foo = j }", {0}},
      {true, "for (var {j}=x; j<10; ++j) { let foo; [foo] = [j] }", {0}},
      {true, "for (var {j}=x; j<10; ++j) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (var {j}=x; j<10; ++j) { let foo = j }", {0}},
      {false, "for (var {j}=x; j<10; ++j) { let [foo] = [j] }", {0}},
      {false, "for (var {j}=x; j<10; ++j) { const foo = j }", {0}},
      {false, "for (var {j}=x; j<10; ++j) { const [foo] = [j] }", {0}},
      {false, "for (var {j}=x; j<10; ++j) { function foo() {return j} }", {0}},

      {true, "for (let j=x; j<10; ++j) { foo = j }", top},
      {true, "for (let j=x; j<10; ++j) { [foo] = [j] }", top},
      {true, "for (let j=x; j<10; ++j) { [[foo]=[42]] = [] }", top},
      {true, "for (let j=x; j<10; ++j) { var foo = j }", top},
      {true, "for (let j=x; j<10; ++j) { var [foo] = [j] }", top},
      {true, "for (let j=x; j<10; ++j) { var [[foo]=[42]] = [] }", top},
      {true, "for (let j=x; j<10; ++j) { var foo; foo = j }", top},
      {true, "for (let j=x; j<10; ++j) { var foo; [foo] = [j] }", top},
      {true, "for (let j=x; j<10; ++j) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (let j=x; j<10; ++j) { let foo; foo = j }", {0, 0}},
      {true, "for (let j=x; j<10; ++j) { let foo; [foo] = [j] }", {0, 0}},
      {true, "for (let j=x; j<10; ++j) { let foo; [[foo]=[42]] = [] }", {0, 0}},
      {false, "for (let j=x; j<10; ++j) { let foo = j }", {0, 0}},
      {false, "for (let j=x; j<10; ++j) { let [foo] = [j] }", {0, 0}},
      {false, "for (let j=x; j<10; ++j) { const foo = j }", {0, 0}},
      {false, "for (let j=x; j<10; ++j) { const [foo] = [j] }", {0, 0}},
      {false,
       "for (let j=x; j<10; ++j) { function foo() {return j} }",
       {0, 0, 0}},

      {true, "for (let {j}=x; j<10; ++j) { foo = j }", top},
      {true, "for (let {j}=x; j<10; ++j) { [foo] = [j] }", top},
      {true, "for (let {j}=x; j<10; ++j) { [[foo]=[42]] = [] }", top},
      {true, "for (let {j}=x; j<10; ++j) { var foo = j }", top},
      {true, "for (let {j}=x; j<10; ++j) { var [foo] = [j] }", top},
      {true, "for (let {j}=x; j<10; ++j) { var [[foo]=[42]] = [] }", top},
      {true, "for (let {j}=x; j<10; ++j) { var foo; foo = j }", top},
      {true, "for (let {j}=x; j<10; ++j) { var foo; [foo] = [j] }", top},
      {true, "for (let {j}=x; j<10; ++j) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (let {j}=x; j<10; ++j) { let foo; foo = j }", {0, 0}},
      {true, "for (let {j}=x; j<10; ++j) { let foo; [foo] = [j] }", {0, 0}},
      {true,
       "for (let {j}=x; j<10; ++j) { let foo; [[foo]=[42]] = [] }",
       {0, 0}},
      {false, "for (let {j}=x; j<10; ++j) { let foo = j }", {0, 0}},
      {false, "for (let {j}=x; j<10; ++j) { let [foo] = [j] }", {0, 0}},
      {false, "for (let {j}=x; j<10; ++j) { const foo = j }", {0, 0}},
      {false, "for (let {j}=x; j<10; ++j) { const [foo] = [j] }", {0, 0}},
      {false,
       "for (let {j}=x; j<10; ++j) { function foo(){return j} }",
       {0, 0, 0}},

      {true, "for (j of x) { foo = j }", top},
      {true, "for (j of x) { [foo] = [j] }", top},
      {true, "for (j of x) { [[foo]=[42]] = [] }", top},
      {true, "for (j of x) { var foo = j }", top},
      {true, "for (j of x) { var [foo] = [j] }", top},
      {true, "for (j of x) { var [[foo]=[42]] = [] }", top},
      {true, "for (j of x) { var foo; foo = j }", top},
      {true, "for (j of x) { var foo; [foo] = [j] }", top},
      {true, "for (j of x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (j of x) { let foo; foo = j }", {0}},
      {true, "for (j of x) { let foo; [foo] = [j] }", {0}},
      {true, "for (j of x) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (j of x) { let foo = j }", {0}},
      {false, "for (j of x) { let [foo] = [j] }", {0}},
      {false, "for (j of x) { const foo = j }", {0}},
      {false, "for (j of x) { const [foo] = [j] }", {0}},
      {false, "for (j of x) { function foo() {return j} }", {0}},

      {true, "for ({j} of x) { foo = j }", top},
      {true, "for ({j} of x) { [foo] = [j] }", top},
      {true, "for ({j} of x) { [[foo]=[42]] = [] }", top},
      {true, "for ({j} of x) { var foo = j }", top},
      {true, "for ({j} of x) { var [foo] = [j] }", top},
      {true, "for ({j} of x) { var [[foo]=[42]] = [] }", top},
      {true, "for ({j} of x) { var foo; foo = j }", top},
      {true, "for ({j} of x) { var foo; [foo] = [j] }", top},
      {true, "for ({j} of x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for ({j} of x) { let foo; foo = j }", {0}},
      {true, "for ({j} of x) { let foo; [foo] = [j] }", {0}},
      {true, "for ({j} of x) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for ({j} of x) { let foo = j }", {0}},
      {false, "for ({j} of x) { let [foo] = [j] }", {0}},
      {false, "for ({j} of x) { const foo = j }", {0}},
      {false, "for ({j} of x) { const [foo] = [j] }", {0}},
      {false, "for ({j} of x) { function foo() {return j} }", {0}},

      {true, "for (var j of x) { foo = j }", top},
      {true, "for (var j of x) { [foo] = [j] }", top},
      {true, "for (var j of x) { [[foo]=[42]] = [] }", top},
      {true, "for (var j of x) { var foo = j }", top},
      {true, "for (var j of x) { var [foo] = [j] }", top},
      {true, "for (var j of x) { var [[foo]=[42]] = [] }", top},
      {true, "for (var j of x) { var foo; foo = j }", top},
      {true, "for (var j of x) { var foo; [foo] = [j] }", top},
      {true, "for (var j of x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (var j of x) { let foo; foo = j }", {0}},
      {true, "for (var j of x) { let foo; [foo] = [j] }", {0}},
      {true, "for (var j of x) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (var j of x) { let foo = j }", {0}},
      {false, "for (var j of x) { let [foo] = [j] }", {0}},
      {false, "for (var j of x) { const foo = j }", {0}},
      {false, "for (var j of x) { const [foo] = [j] }", {0}},
      {false, "for (var j of x) { function foo() {return j} }", {0}},

      {true, "for (var {j} of x) { foo = j }", top},
      {true, "for (var {j} of x) { [foo] = [j] }", top},
      {true, "for (var {j} of x) { [[foo]=[42]] = [] }", top},
      {true, "for (var {j} of x) { var foo = j }", top},
      {true, "for (var {j} of x) { var [foo] = [j] }", top},
      {true, "for (var {j} of x) { var [[foo]=[42]] = [] }", top},
      {true, "for (var {j} of x) { var foo; foo = j }", top},
      {true, "for (var {j} of x) { var foo; [foo] = [j] }", top},
      {true, "for (var {j} of x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (var {j} of x) { let foo; foo = j }", {0}},
      {true, "for (var {j} of x) { let foo; [foo] = [j] }", {0}},
      {true, "for (var {j} of x) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (var {j} of x) { let foo = j }", {0}},
      {false, "for (var {j} of x) { let [foo] = [j] }", {0}},
      {false, "for (var {j} of x) { const foo = j }", {0}},
      {false, "for (var {j} of x) { const [foo] = [j] }", {0}},
      {false, "for (var {j} of x) { function foo() {return j} }", {0}},

      {true, "for (let j of x) { foo = j }", top},
      {true, "for (let j of x) { [foo] = [j] }", top},
      {true, "for (let j of x) { [[foo]=[42]] = [] }", top},
      {true, "for (let j of x) { var foo = j }", top},
      {true, "for (let j of x) { var [foo] = [j] }", top},
      {true, "for (let j of x) { var [[foo]=[42]] = [] }", top},
      {true, "for (let j of x) { var foo; foo = j }", top},
      {true, "for (let j of x) { var foo; [foo] = [j] }", top},
      {true, "for (let j of x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (let j of x) { let foo; foo = j }", {0, 0, 0}},
      {true, "for (let j of x) { let foo; [foo] = [j] }", {0, 0, 0}},
      {true, "for (let j of x) { let foo; [[foo]=[42]] = [] }", {0, 0, 0}},
      {false, "for (let j of x) { let foo = j }", {0, 0, 0}},
      {false, "for (let j of x) { let [foo] = [j] }", {0, 0, 0}},
      {false, "for (let j of x) { const foo = j }", {0, 0, 0}},
      {false, "for (let j of x) { const [foo] = [j] }", {0, 0, 0}},
      {false, "for (let j of x) { function foo() {return j} }", {0, 0, 0}},

      {true, "for (let {j} of x) { foo = j }", top},
      {true, "for (let {j} of x) { [foo] = [j] }", top},
      {true, "for (let {j} of x) { [[foo]=[42]] = [] }", top},
      {true, "for (let {j} of x) { var foo = j }", top},
      {true, "for (let {j} of x) { var [foo] = [j] }", top},
      {true, "for (let {j} of x) { var [[foo]=[42]] = [] }", top},
      {true, "for (let {j} of x) { var foo; foo = j }", top},
      {true, "for (let {j} of x) { var foo; [foo] = [j] }", top},
      {true, "for (let {j} of x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (let {j} of x) { let foo; foo = j }", {0, 0, 0}},
      {true, "for (let {j} of x) { let foo; [foo] = [j] }", {0, 0, 0}},
      {true, "for (let {j} of x) { let foo; [[foo]=[42]] = [] }", {0, 0, 0}},
      {false, "for (let {j} of x) { let foo = j }", {0, 0, 0}},
      {false, "for (let {j} of x) { let [foo] = [j] }", {0, 0, 0}},
      {false, "for (let {j} of x) { const foo = j }", {0, 0, 0}},
      {false, "for (let {j} of x) { const [foo] = [j] }", {0, 0, 0}},
      {false, "for (let {j} of x) { function foo() {return j} }", {0, 0, 0}},

      {true, "for (const j of x) { foo = j }", top},
      {true, "for (const j of x) { [foo] = [j] }", top},
      {true, "for (const j of x) { [[foo]=[42]] = [] }", top},
      {true, "for (const j of x) { var foo = j }", top},
      {true, "for (const j of x) { var [foo] = [j] }", top},
      {true, "for (const j of x) { var [[foo]=[42]] = [] }", top},
      {true, "for (const j of x) { var foo; foo = j }", top},
      {true, "for (const j of x) { var foo; [foo] = [j] }", top},
      {true, "for (const j of x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (const j of x) { let foo; foo = j }", {0, 0, 0}},
      {true, "for (const j of x) { let foo; [foo] = [j] }", {0, 0, 0}},
      {true, "for (const j of x) { let foo; [[foo]=[42]] = [] }", {0, 0, 0}},
      {false, "for (const j of x) { let foo = j }", {0, 0, 0}},
      {false, "for (const j of x) { let [foo] = [j] }", {0, 0, 0}},
      {false, "for (const j of x) { const foo = j }", {0, 0, 0}},
      {false, "for (const j of x) { const [foo] = [j] }", {0, 0, 0}},
      {false, "for (const j of x) { function foo() {return j} }", {0, 0, 0}},

      {true, "for (const {j} of x) { foo = j }", top},
      {true, "for (const {j} of x) { [foo] = [j] }", top},
      {true, "for (const {j} of x) { [[foo]=[42]] = [] }", top},
      {true, "for (const {j} of x) { var foo = j }", top},
      {true, "for (const {j} of x) { var [foo] = [j] }", top},
      {true, "for (const {j} of x) { var [[foo]=[42]] = [] }", top},
      {true, "for (const {j} of x) { var foo; foo = j }", top},
      {true, "for (const {j} of x) { var foo; [foo] = [j] }", top},
      {true, "for (const {j} of x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (const {j} of x) { let foo; foo = j }", {0, 0, 0}},
      {true, "for (const {j} of x) { let foo; [foo] = [j] }", {0, 0, 0}},
      {true, "for (const {j} of x) { let foo; [[foo]=[42]] = [] }", {0, 0, 0}},
      {false, "for (const {j} of x) { let foo = j }", {0, 0, 0}},
      {false, "for (const {j} of x) { let [foo] = [j] }", {0, 0, 0}},
      {false, "for (const {j} of x) { const foo = j }", {0, 0, 0}},
      {false, "for (const {j} of x) { const [foo] = [j] }", {0, 0, 0}},
      {false, "for (const {j} of x) { function foo() {return j} }", {0, 0, 0}},

      {true, "for (j in x) { foo = j }", top},
      {true, "for (j in x) { [foo] = [j] }", top},
      {true, "for (j in x) { [[foo]=[42]] = [] }", top},
      {true, "for (j in x) { var foo = j }", top},
      {true, "for (j in x) { var [foo] = [j] }", top},
      {true, "for (j in x) { var [[foo]=[42]] = [] }", top},
      {true, "for (j in x) { var foo; foo = j }", top},
      {true, "for (j in x) { var foo; [foo] = [j] }", top},
      {true, "for (j in x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (j in x) { let foo; foo = j }", {0}},
      {true, "for (j in x) { let foo; [foo] = [j] }", {0}},
      {true, "for (j in x) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (j in x) { let foo = j }", {0}},
      {false, "for (j in x) { let [foo] = [j] }", {0}},
      {false, "for (j in x) { const foo = j }", {0}},
      {false, "for (j in x) { const [foo] = [j] }", {0}},
      {false, "for (j in x) { function foo() {return j} }", {0}},

      {true, "for ({j} in x) { foo = j }", top},
      {true, "for ({j} in x) { [foo] = [j] }", top},
      {true, "for ({j} in x) { [[foo]=[42]] = [] }", top},
      {true, "for ({j} in x) { var foo = j }", top},
      {true, "for ({j} in x) { var [foo] = [j] }", top},
      {true, "for ({j} in x) { var [[foo]=[42]] = [] }", top},
      {true, "for ({j} in x) { var foo; foo = j }", top},
      {true, "for ({j} in x) { var foo; [foo] = [j] }", top},
      {true, "for ({j} in x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for ({j} in x) { let foo; foo = j }", {0}},
      {true, "for ({j} in x) { let foo; [foo] = [j] }", {0}},
      {true, "for ({j} in x) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for ({j} in x) { let foo = j }", {0}},
      {false, "for ({j} in x) { let [foo] = [j] }", {0}},
      {false, "for ({j} in x) { const foo = j }", {0}},
      {false, "for ({j} in x) { const [foo] = [j] }", {0}},
      {false, "for ({j} in x) { function foo() {return j} }", {0}},

      {true, "for (var j in x) { foo = j }", top},
      {true, "for (var j in x) { [foo] = [j] }", top},
      {true, "for (var j in x) { [[foo]=[42]] = [] }", top},
      {true, "for (var j in x) { var foo = j }", top},
      {true, "for (var j in x) { var [foo] = [j] }", top},
      {true, "for (var j in x) { var [[foo]=[42]] = [] }", top},
      {true, "for (var j in x) { var foo; foo = j }", top},
      {true, "for (var j in x) { var foo; [foo] = [j] }", top},
      {true, "for (var j in x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (var j in x) { let foo; foo = j }", {0}},
      {true, "for (var j in x) { let foo; [foo] = [j] }", {0}},
      {true, "for (var j in x) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (var j in x) { let foo = j }", {0}},
      {false, "for (var j in x) { let [foo] = [j] }", {0}},
      {false, "for (var j in x) { const foo = j }", {0}},
      {false, "for (var j in x) { const [foo] = [j] }", {0}},
      {false, "for (var j in x) { function foo() {return j} }", {0}},

      {true, "for (var {j} in x) { foo = j }", top},
      {true, "for (var {j} in x) { [foo] = [j] }", top},
      {true, "for (var {j} in x) { [[foo]=[42]] = [] }", top},
      {true, "for (var {j} in x) { var foo = j }", top},
      {true, "for (var {j} in x) { var [foo] = [j] }", top},
      {true, "for (var {j} in x) { var [[foo]=[42]] = [] }", top},
      {true, "for (var {j} in x) { var foo; foo = j }", top},
      {true, "for (var {j} in x) { var foo; [foo] = [j] }", top},
      {true, "for (var {j} in x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (var {j} in x) { let foo; foo = j }", {0}},
      {true, "for (var {j} in x) { let foo; [foo] = [j] }", {0}},
      {true, "for (var {j} in x) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "for (var {j} in x) { let foo = j }", {0}},
      {false, "for (var {j} in x) { let [foo] = [j] }", {0}},
      {false, "for (var {j} in x) { const foo = j }", {0}},
      {false, "for (var {j} in x) { const [foo] = [j] }", {0}},
      {false, "for (var {j} in x) { function foo() {return j} }", {0}},

      {true, "for (let j in x) { foo = j }", top},
      {true, "for (let j in x) { [foo] = [j] }", top},
      {true, "for (let j in x) { [[foo]=[42]] = [] }", top},
      {true, "for (let j in x) { var foo = j }", top},
      {true, "for (let j in x) { var [foo] = [j] }", top},
      {true, "for (let j in x) { var [[foo]=[42]] = [] }", top},
      {true, "for (let j in x) { var foo; foo = j }", top},
      {true, "for (let j in x) { var foo; [foo] = [j] }", top},
      {true, "for (let j in x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (let j in x) { let foo; foo = j }", {0, 0, 0}},
      {true, "for (let j in x) { let foo; [foo] = [j] }", {0, 0, 0}},
      {true, "for (let j in x) { let foo; [[foo]=[42]] = [] }", {0, 0, 0}},
      {false, "for (let j in x) { let foo = j }", {0, 0, 0}},
      {false, "for (let j in x) { let [foo] = [j] }", {0, 0, 0}},
      {false, "for (let j in x) { const foo = j }", {0, 0, 0}},
      {false, "for (let j in x) { const [foo] = [j] }", {0, 0, 0}},
      {false, "for (let j in x) { function foo() {return j} }", {0, 0, 0}},

      {true, "for (let {j} in x) { foo = j }", top},
      {true, "for (let {j} in x) { [foo] = [j] }", top},
      {true, "for (let {j} in x) { [[foo]=[42]] = [] }", top},
      {true, "for (let {j} in x) { var foo = j }", top},
      {true, "for (let {j} in x) { var [foo] = [j] }", top},
      {true, "for (let {j} in x) { var [[foo]=[42]] = [] }", top},
      {true, "for (let {j} in x) { var foo; foo = j }", top},
      {true, "for (let {j} in x) { var foo; [foo] = [j] }", top},
      {true, "for (let {j} in x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (let {j} in x) { let foo; foo = j }", {0, 0, 0}},
      {true, "for (let {j} in x) { let foo; [foo] = [j] }", {0, 0, 0}},
      {true, "for (let {j} in x) { let foo; [[foo]=[42]] = [] }", {0, 0, 0}},
      {false, "for (let {j} in x) { let foo = j }", {0, 0, 0}},
      {false, "for (let {j} in x) { let [foo] = [j] }", {0, 0, 0}},
      {false, "for (let {j} in x) { const foo = j }", {0, 0, 0}},
      {false, "for (let {j} in x) { const [foo] = [j] }", {0, 0, 0}},
      {false, "for (let {j} in x) { function foo() {return j} }", {0, 0, 0}},

      {true, "for (const j in x) { foo = j }", top},
      {true, "for (const j in x) { [foo] = [j] }", top},
      {true, "for (const j in x) { [[foo]=[42]] = [] }", top},
      {true, "for (const j in x) { var foo = j }", top},
      {true, "for (const j in x) { var [foo] = [j] }", top},
      {true, "for (const j in x) { var [[foo]=[42]] = [] }", top},
      {true, "for (const j in x) { var foo; foo = j }", top},
      {true, "for (const j in x) { var foo; [foo] = [j] }", top},
      {true, "for (const j in x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (const j in x) { let foo; foo = j }", {0, 0, 0}},
      {true, "for (const j in x) { let foo; [foo] = [j] }", {0, 0, 0}},
      {true, "for (const j in x) { let foo; [[foo]=[42]] = [] }", {0, 0, 0}},
      {false, "for (const j in x) { let foo = j }", {0, 0, 0}},
      {false, "for (const j in x) { let [foo] = [j] }", {0, 0, 0}},
      {false, "for (const j in x) { const foo = j }", {0, 0, 0}},
      {false, "for (const j in x) { const [foo] = [j] }", {0, 0, 0}},
      {false, "for (const j in x) { function foo() {return j} }", {0, 0, 0}},

      {true, "for (const {j} in x) { foo = j }", top},
      {true, "for (const {j} in x) { [foo] = [j] }", top},
      {true, "for (const {j} in x) { [[foo]=[42]] = [] }", top},
      {true, "for (const {j} in x) { var foo = j }", top},
      {true, "for (const {j} in x) { var [foo] = [j] }", top},
      {true, "for (const {j} in x) { var [[foo]=[42]] = [] }", top},
      {true, "for (const {j} in x) { var foo; foo = j }", top},
      {true, "for (const {j} in x) { var foo; [foo] = [j] }", top},
      {true, "for (const {j} in x) { var foo; [[foo]=[42]] = [] }", top},
      {true, "for (const {j} in x) { let foo; foo = j }", {0, 0, 0}},
      {true, "for (const {j} in x) { let foo; [foo] = [j] }", {0, 0, 0}},
      {true, "for (const {j} in x) { let foo; [[foo]=[42]] = [] }", {0, 0, 0}},
      {false, "for (const {j} in x) { let foo = j }", {0, 0, 0}},
      {false, "for (const {j} in x) { let [foo] = [j] }", {0, 0, 0}},
      {false, "for (const {j} in x) { const foo = j }", {0, 0, 0}},
      {false, "for (const {j} in x) { const [foo] = [j] }", {0, 0, 0}},
      {false, "for (const {j} in x) { function foo() {return j} }", {0, 0, 0}},

      {true, "while (j) { foo = j }", top},
      {true, "while (j) { [foo] = [j] }", top},
      {true, "while (j) { [[foo]=[42]] = [] }", top},
      {true, "while (j) { var foo = j }", top},
      {true, "while (j) { var [foo] = [j] }", top},
      {true, "while (j) { var [[foo]=[42]] = [] }", top},
      {true, "while (j) { var foo; foo = j }", top},
      {true, "while (j) { var foo; [foo] = [j] }", top},
      {true, "while (j) { var foo; [[foo]=[42]] = [] }", top},
      {true, "while (j) { let foo; foo = j }", {0}},
      {true, "while (j) { let foo; [foo] = [j] }", {0}},
      {true, "while (j) { let foo; [[foo]=[42]] = [] }", {0}},
      {false, "while (j) { let foo = j }", {0}},
      {false, "while (j) { let [foo] = [j] }", {0}},
      {false, "while (j) { const foo = j }", {0}},
      {false, "while (j) { const [foo] = [j] }", {0}},
      {false, "while (j) { function foo() {return j} }", {0}},

      {true, "do { foo = j } while (j)", top},
      {true, "do { [foo] = [j] } while (j)", top},
      {true, "do { [[foo]=[42]] = [] } while (j)", top},
      {true, "do { var foo = j } while (j)", top},
      {true, "do { var [foo] = [j] } while (j)", top},
      {true, "do { var [[foo]=[42]] = [] } while (j)", top},
      {true, "do { var foo; foo = j } while (j)", top},
      {true, "do { var foo; [foo] = [j] } while (j)", top},
      {true, "do { var foo; [[foo]=[42]] = [] } while (j)", top},
      {true, "do { let foo; foo = j } while (j)", {0}},
      {true, "do { let foo; [foo] = [j] } while (j)", {0}},
      {true, "do { let foo; [[foo]=[42]] = [] } while (j)", {0}},
      {false, "do { let foo = j } while (j)", {0}},
      {false, "do { let [foo] = [j] } while (j)", {0}},
      {false, "do { const foo = j } while (j)", {0}},
      {false, "do { const [foo] = [j] } while (j)", {0}},
      {false, "do { function foo() {return j} } while (j)", {0}},
  };

  Input script_only_tests[] = {
      {true, "for (j=x; j<10; ++j) { function foo() {return j} }", top},
      {true, "for ({j}=x; j<10; ++j) { function foo() {return j} }", top},
      {true, "for (var j=x; j<10; ++j) { function foo() {return j} }", top},
      {true, "for (var {j}=x; j<10; ++j) { function foo() {return j} }", top},
      {true, "for (let j=x; j<10; ++j) { function foo() {return j} }", top},
      {true, "for (let {j}=x; j<10; ++j) { function foo() {return j} }", top},
      {true, "for (j of x) { function foo() {return j} }", top},
      {true, "for ({j} of x) { function foo() {return j} }", top},
      {true, "for (var j of x) { function foo() {return j} }", top},
      {true, "for (var {j} of x) { function foo() {return j} }", top},
      {true, "for (let j of x) { function foo() {return j} }", top},
      {true, "for (let {j} of x) { function foo() {return j} }", top},
      {true, "for (const j of x) { function foo() {return j} }", top},
      {true, "for (const {j} of x) { function foo() {return j} }", top},
      {true, "for (j in x) { function foo() {return j} }", top},
      {true, "for ({j} in x) { function foo() {return j} }", top},
      {true, "for (var j in x) { function foo() {return j} }", top},
      {true, "for (var {j} in x) { function foo() {return j} }", top},
      {true, "for (let j in x) { function foo() {return j} }", top},
      {true, "for (let {j} in x) { function foo() {return j} }", top},
      {true, "for (const j in x) { function foo() {return j} }", top},
      {true, "for (const {j} in x) { function foo() {return j} }", top},
      {true, "while (j) { function foo() {return j} }", top},
      {true, "do { function foo() {return j} } while (j)", top},
  };

  for (unsigned i = 0; i < arraysize(module_and_script_tests); ++i) {
    Input input = module_and_script_tests[i];
    for (unsigned module = 0; module <= 1; ++module) {
      for (unsigned allow_lazy_parsing = 0; allow_lazy_parsing <= 1;
           ++allow_lazy_parsing) {
        TestMaybeAssigned(input, "foo", module, allow_lazy_parsing);
      }
      TestMaybeAssigned(wrap(input), "foo", module, false);
    }
  }

  for (unsigned i = 0; i < arraysize(script_only_tests); ++i) {
    Input input = script_only_tests[i];
    for (unsigned allow_lazy_parsing = 0; allow_lazy_parsing <= 1;
         ++allow_lazy_parsing) {
      TestMaybeAssigned(input, "foo", false, allow_lazy_parsing);
    }
    TestMaybeAssigned(wrap(input), "foo", false, false);
  }
}

TEST_F(ParsingTest, MaybeAssignedTopLevel) {
  const char* prefixes[] = {
      "let foo; ",
      "let foo = 0; ",
      "let [foo] = [1]; ",
      "let {foo} = {foo: 2}; ",
      "let {foo=3} = {}; ",
      "var foo; ",
      "var foo = 0; ",
      "var [foo] = [1]; ",
      "var {foo} = {foo: 2}; ",
      "var {foo=3} = {}; ",
      "{ var foo; }; ",
      "{ var foo = 0; }; ",
      "{ var [foo] = [1]; }; ",
      "{ var {foo} = {foo: 2}; }; ",
      "{ var {foo=3} = {}; }; ",
      "function foo() {}; ",
      "function* foo() {}; ",
      "async function foo() {}; ",
      "class foo {}; ",
      "class foo extends null {}; ",
  };

  const char* module_and_script_tests[] = {
      "function bar() {foo = 42}; ext(bar); ext(foo)",
      "ext(function() {foo++}); ext(foo)",
      "bar = () => --foo; ext(bar); ext(foo)",
      "function* bar() {eval(ext)}; ext(bar); ext(foo)",
  };

  const char* script_only_tests[] = {
      "",
      "{ function foo() {}; }; ",
      "{ function* foo() {}; }; ",
      "{ async function foo() {}; }; ",
  };

  for (unsigned i = 0; i < arraysize(prefixes); ++i) {
    for (unsigned j = 0; j < arraysize(module_and_script_tests); ++j) {
      std::string source(prefixes[i]);
      source += module_and_script_tests[j];
      std::vector<unsigned> top;
      Input input({true, source, top});
      for (unsigned module = 0; module <= 1; ++module) {
        for (unsigned allow_lazy_parsing = 0; allow_lazy_parsing <= 1;
             ++allow_lazy_parsing) {
          TestMaybeAssigned(input, "foo", module, allow_lazy_parsing);
        }
      }
    }
  }

  for (unsigned i = 0; i < arraysize(prefixes); ++i) {
    for (unsigned j = 0; j < arraysize(script_only_tests); ++j) {
      std::string source(prefixes[i]);
      source += script_only_tests[j];
      std::vector<unsigned> top;
      Input input({true, source, top});
      for (unsigned allow_lazy_parsing = 0; allow_lazy_parsing <= 1;
           ++allow_lazy_parsing) {
        TestMaybeAssigned(input, "foo", false, allow_lazy_parsing);
      }
    }
  }
}

#if V8_ENABLE_WEBASSEMBLY
namespace {

i::Scope* DeserializeFunctionScope(i::Isolate* isolate, i::Zone* zone,
                                   i::Handle<i::JSObject> m, const char* name) {
  i::AstValueFactory avf(zone, isolate->ast_string_constants(),
                         HashSeed(isolate));
  i::Handle<i::JSFunction> f = i::Handle<i::JSFunction>::cast(
      i::JSReceiver::GetProperty(isolate, m, name).ToHandleChecked());
  i::DeclarationScope* script_scope =
      zone->New<i::DeclarationScope>(zone, &avf);
  i::Scope* s = i::Scope::DeserializeScopeChain(
      isolate, zone, f->context()->scope_info(), script_scope, &avf,
      i::Scope::DeserializationMode::kIncludingVariables);
  return s;
}

}  // namespace

TEST_F(ParsingTest, AsmModuleFlag) {
  i::v8_flags.validate_asm = false;
  i::Isolate* isolate = i_isolate();

  const char* src =
      "function m() {"
      "  'use asm';"
      "  function f() { return 0 };"
      "  return { f:f };"
      "}"
      "m();";

  v8::Local<v8::Value> v = RunJS(src);
  i::Handle<i::Object> o = v8::Utils::OpenHandle(*v);
  i::Handle<i::JSObject> m = i::Handle<i::JSObject>::cast(o);

  // The asm.js module should be marked as such.
  i::Scope* s = DeserializeFunctionScope(isolate, zone(), m, "f");
  CHECK(s->IsAsmModule() && s->AsDeclarationScope()->is_asm_module());
}

TEST_F(ParsingTest, UseAsmUseCount) {
  int use_counts[v8::Isolate::kUseCounterFeatureCount] = {};
  global_use_counts = use_counts;
  v8_isolate()->SetUseCounterCallback(MockUseCounterCallback);
  RunJS(
      "\"use asm\";\n"
      "var foo = 1;\n"
      "function bar() { \"use asm\"; var baz = 1; }");
  CHECK_LT(0, use_counts[v8::Isolate::kUseAsm]);
}
#endif  // V8_ENABLE_WEBASSEMBLY

TEST_F(ParsingTest, StrictModeUseCount) {
  int use_counts[v8::Isolate::kUseCounterFeatureCount] = {};
  global_use_counts = use_counts;
  v8_isolate()->SetUseCounterCallback(MockUseCounterCallback);
  RunJS(
      "\"use strict\";\n"
      "function bar() { var baz = 1; }");  // strict mode inherits
  CHECK_LT(0, use_counts[v8::Isolate::kStrictMode]);
  CHECK_EQ(0, use_counts[v8::Isolate::kSloppyMode]);
}

TEST_F(ParsingTest, SloppyModeUseCount) {
  int use_counts[v8::Isolate::kUseCounterFeatureCount] = {};
  global_use_counts = use_counts;
  // Force eager parsing (preparser doesn't update use counts).
  i::v8_flags.lazy = false;
  i::v8_flags.lazy_streaming = false;
  v8_isolate()->SetUseCounterCallback(MockUseCounterCallback);
  RunJS("function bar() { var baz = 1; }");
  CHECK_LT(0, use_counts[v8::Isolate::kSloppyMode]);
  CHECK_EQ(0, use_counts[v8::Isolate::kStrictMode]);
}

TEST_F(ParsingTest, BothModesUseCount) {
  int use_counts[v8::Isolate::kUseCounterFeatureCount] = {};
  global_use_counts = use_counts;
  i::v8_flags.lazy = false;
  i::v8_flags.lazy_streaming = false;
  v8_isolate()->SetUseCounterCallback(MockUseCounterCallback);
  RunJS("function bar() { 'use strict'; var baz = 1; }");
  CHECK_LT(0, use_counts[v8::Isolate::kSloppyMode]);
  CHECK_LT(0, use_counts[v8::Isolate::kStrictMode]);
}

TEST_F(ParsingTest, LineOrParagraphSeparatorAsLineTerminator) {
  // Tests that both preparsing and parsing accept U+2028 LINE SEPARATOR and
  // U+2029 PARAGRAPH SEPARATOR as LineTerminator symbols outside of string
  // literals.
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};
  const char* statement_data[] = {"\x31\xE2\x80\xA8\x32",  // 1<U+2028>2
                                  "\x31\xE2\x80\xA9\x32",  // 1<U+2029>2
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, LineOrParagraphSeparatorInStringLiteral) {
  // Tests that both preparsing and parsing don't treat U+2028 LINE SEPARATOR
  // and U+2029 PARAGRAPH SEPARATOR as line terminators within string literals.
  // https://github.com/tc39/proposal-json-superset
  const char* context_data[][2] = {
      {"\"", "\""}, {"'", "'"}, {nullptr, nullptr}};
  const char* statement_data[] = {"\x31\xE2\x80\xA8\x32",  // 1<U+2028>2
                                  "\x31\xE2\x80\xA9\x32",  // 1<U+2029>2
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ErrorsArrowFormalParameters) {
  const char* context_data[][2] = {{"()", "=>{}"},
                                   {"()", "=>{};"},
                                   {"var x = ()", "=>{}"},
                                   {"var x = ()", "=>{};"},

                                   {"a", "=>{}"},
                                   {"a", "=>{};"},
                                   {"var x = a", "=>{}"},
                                   {"var x = a", "=>{};"},

                                   {"(a)", "=>{}"},
                                   {"(a)", "=>{};"},
                                   {"var x = (a)", "=>{}"},
                                   {"var x = (a)", "=>{};"},

                                   {"(...a)", "=>{}"},
                                   {"(...a)", "=>{};"},
                                   {"var x = (...a)", "=>{}"},
                                   {"var x = (...a)", "=>{};"},

                                   {"(a,b)", "=>{}"},
                                   {"(a,b)", "=>{};"},
                                   {"var x = (a,b)", "=>{}"},
                                   {"var x = (a,b)", "=>{};"},

                                   {"(a,...b)", "=>{}"},
                                   {"(a,...b)", "=>{};"},
                                   {"var x = (a,...b)", "=>{}"},
                                   {"var x = (a,...b)", "=>{};"},

                                   {nullptr, nullptr}};
  const char* assignment_expression_suffix_data[] = {
      "?c:d=>{}",
      "=c=>{}",
      "()",
      "(c)",
      "[1]",
      "[c]",
      ".c",
      "-c",
      "+c",
      "c++",
      "`c`",
      "`${c}`",
      "`template-head${c}`",
      "`${c}template-tail`",
      "`template-head${c}template-tail`",
      "`${c}template-tail`",
      nullptr};

  RunParserSyncTest(context_data, assignment_expression_suffix_data, kError);
}

TEST_F(ParsingTest, ErrorsArrowFunctions) {
  // Tests that parser and preparser generate the same kind of errors
  // on invalid arrow function syntax.

  // clang-format off
  const char* context_data[][2] = {
    {"", ";"},
    {"v = ", ";"},
    {"bar ? (", ") : baz;"},
    {"bar ? baz : (", ");"},
    {"bar[", "];"},
    {"bar, ", ";"},
    {"", ", bar;"},
    {nullptr, nullptr}
  };

  const char* statement_data[] = {
    "=> 0",
    "=>",
    "() =>",
    "=> {}",
    ") => {}",
    ", => {}",
    "(,) => {}",
    "return => {}",
    "() => {'value': 42}",

    // Check that the early return introduced in ParsePrimaryExpression
    // does not accept stray closing parentheses.
    ")",
    ") => 0",
    "foo[()]",
    "()",

    // Parameter lists with extra parens should be recognized as errors.
    "(()) => 0",
    "((x)) => 0",
    "((x, y)) => 0",
    "(x, (y)) => 0",
    "((x, y, z)) => 0",
    "(x, (y, z)) => 0",
    "((x, y), z) => 0",

    // Arrow function formal parameters are parsed as StrictFormalParameters,
    // which confusingly only implies that there are no duplicates.  Words
    // reserved in strict mode, and eval or arguments, are indeed valid in
    // sloppy mode.
    "eval => { 'use strict'; 0 }",
    "arguments => { 'use strict'; 0 }",
    "yield => { 'use strict'; 0 }",
    "interface => { 'use strict'; 0 }",
    "(eval) => { 'use strict'; 0 }",
    "(arguments) => { 'use strict'; 0 }",
    "(yield) => { 'use strict'; 0 }",
    "(interface) => { 'use strict'; 0 }",
    "(eval, bar) => { 'use strict'; 0 }",
    "(bar, eval) => { 'use strict'; 0 }",
    "(bar, arguments) => { 'use strict'; 0 }",
    "(bar, yield) => { 'use strict'; 0 }",
    "(bar, interface) => { 'use strict'; 0 }",
    // TODO(aperez): Detecting duplicates does not work in PreParser.
    // "(bar, bar) => {}",

    // The parameter list is parsed as an expression, but only
    // a comma-separated list of identifier is valid.
    "32 => {}",
    "(32) => {}",
    "(a, 32) => {}",
    "if => {}",
    "(if) => {}",
    "(a, if) => {}",
    "a + b => {}",
    "(a + b) => {}",
    "(a + b, c) => {}",
    "(a, b - c) => {}",
    "\"a\" => {}",
    "(\"a\") => {}",
    "(\"a\", b) => {}",
    "(a, \"b\") => {}",
    "-a => {}",
    "(-a) => {}",
    "(-a, b) => {}",
    "(a, -b) => {}",
    "{} => {}",
    "a++ => {}",
    "(a++) => {}",
    "(a++, b) => {}",
    "(a, b++) => {}",
    "[] => {}",
    "(foo ? bar : baz) => {}",
    "(a, foo ? bar : baz) => {}",
    "(foo ? bar : baz, a) => {}",
    "(a.b, c) => {}",
    "(c, a.b) => {}",
    "(a['b'], c) => {}",
    "(c, a['b']) => {}",
    "(...a = b) => b",

    // crbug.com/582626
    "(...rest - a) => b",
    "(a, ...b - 10) => b",

    nullptr
  };
  // clang-format on

  // The test is quite slow, so run it with a reduced set of flags.
  static const ParserFlag flags[] = {kAllowLazy};
  RunParserSyncTest(context_data, statement_data, kError, flags,
                    arraysize(flags));

  // In a context where a concise arrow body is parsed with [~In] variant,
  // ensure that an error is reported in both full parser and preparser.
  const char* loop_context_data[][2] = {{"for (", "; 0;);"},
                                        {nullptr, nullptr}};
  const char* loop_expr_data[] = {"f => 'key' in {}", nullptr};
  RunParserSyncTest(loop_context_data, loop_expr_data, kError, flags,
                    arraysize(flags));
}

TEST_F(ParsingTest, NoErrorsArrowFunctions) {
  // Tests that parser and preparser accept valid arrow functions syntax.
  // clang-format off
  const char* context_data[][2] = {
    {"", ";"},
    {"bar ? (", ") : baz;"},
    {"bar ? baz : (", ");"},
    {"bar, ", ";"},
    {"", ", bar;"},
    {nullptr, nullptr}
  };

  const char* statement_data[] = {
    "() => {}",
    "() => { return 42 }",
    "x => { return x; }",
    "(x) => { return x; }",
    "(x, y) => { return x + y; }",
    "(x, y, z) => { return x + y + z; }",
    "(x, y) => { x.a = y; }",
    "() => 42",
    "x => x",
    "x => x * x",
    "(x) => x",
    "(x) => x * x",
    "(x, y) => x + y",
    "(x, y, z) => x, y, z",
    "(x, y) => x.a = y",
    "() => ({'value': 42})",
    "x => y => x + y",
    "(x, y) => (u, v) => x*u + y*v",
    "(x, y) => z => z * (x + y)",
    "x => (y, z) => z * (x + y)",

    // Those are comma-separated expressions, with arrow functions as items.
    // They stress the code for validating arrow function parameter lists.
    "a, b => 0",
    "a, b, (c, d) => 0",
    "(a, b, (c, d) => 0)",
    "(a, b) => 0, (c, d) => 1",
    "(a, b => {}, a => a + 1)",
    "((a, b) => {}, (a => a + 1))",
    "(a, (a, (b, c) => 0))",

    // Arrow has more precedence, this is the same as: foo ? bar : (baz = {})
    "foo ? bar : baz => {}",

    // Arrows with non-simple parameters.
    "({}) => {}",
    "(a, {}) => {}",
    "({}, a) => {}",
    "([]) => {}",
    "(a, []) => {}",
    "([], a) => {}",
    "(a = b) => {}",
    "(a = b, c) => {}",
    "(a, b = c) => {}",
    "({a}) => {}",
    "(x = 9) => {}",
    "(x, y = 9) => {}",
    "(x = 9, y) => {}",
    "(x, y = 9, z) => {}",
    "(x, y = 9, z = 8) => {}",
    "(...a) => {}",
    "(x, ...a) => {}",
    "(x = 9, ...a) => {}",
    "(x, y = 9, ...a) => {}",
    "(x, y = 9, {b}, z = 8, ...a) => {}",
    "({a} = {}) => {}",
    "([x] = []) => {}",
    "({a = 42}) => {}",
    "([x = 0]) => {}",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kSuccess);

  static const ParserFlag flags[] = {kAllowLazy};
  // In a context where a concise arrow body is parsed with [~In] variant,
  // ensure that nested expressions can still use the 'in' operator,
  const char* loop_context_data[][2] = {{"for (", "; 0;);"},
                                        {nullptr, nullptr}};
  const char* loop_expr_data[] = {"f => ('key' in {})", nullptr};
  RunParserSyncTest(loop_context_data, loop_expr_data, kSuccess, flags,
                    arraysize(flags));
}

TEST_F(ParsingTest, ArrowFunctionsSloppyParameterNames) {
  const char* strict_context_data[][2] = {{"'use strict'; ", ";"},
                                          {"'use strict'; bar ? (", ") : baz;"},
                                          {"'use strict'; bar ? baz : (", ");"},
                                          {"'use strict'; bar, ", ";"},
                                          {"'use strict'; ", ", bar;"},
                                          {nullptr, nullptr}};

  const char* sloppy_context_data[][2] = {
      {"", ";"},      {"bar ? (", ") : baz;"}, {"bar ? baz : (", ");"},
      {"bar, ", ";"}, {"", ", bar;"},          {nullptr, nullptr}};

  const char* statement_data[] = {"eval => {}",
                                  "arguments => {}",
                                  "yield => {}",
                                  "interface => {}",
                                  "(eval) => {}",
                                  "(arguments) => {}",
                                  "(yield) => {}",
                                  "(interface) => {}",
                                  "(eval, bar) => {}",
                                  "(bar, eval) => {}",
                                  "(bar, arguments) => {}",
                                  "(bar, yield) => {}",
                                  "(bar, interface) => {}",
                                  "(interface, eval) => {}",
                                  "(interface, arguments) => {}",
                                  "(eval, interface) => {}",
                                  "(arguments, interface) => {}",
                                  nullptr};

  RunParserSyncTest(strict_context_data, statement_data, kError);
  RunParserSyncTest(sloppy_context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ArrowFunctionsYieldParameterNameInGenerator) {
  const char* sloppy_function_context_data[][2] = {
      {"(function f() { (", "); });"}, {nullptr, nullptr}};

  const char* strict_function_context_data[][2] = {
      {"(function f() {'use strict'; (", "); });"}, {nullptr, nullptr}};

  const char* generator_context_data[][2] = {
      {"(function *g() {'use strict'; (", "); });"},
      {"(function *g() { (", "); });"},
      {nullptr, nullptr}};

  const char* arrow_data[] = {
      "yield => {}",      "(yield) => {}",       "(a, yield) => {}",
      "(yield, a) => {}", "(yield, ...a) => {}", "(a, ...yield) => {}",
      "({yield}) => {}",  "([yield]) => {}",     nullptr};

  RunParserSyncTest(sloppy_function_context_data, arrow_data, kSuccess);
  RunParserSyncTest(strict_function_context_data, arrow_data, kError);
  RunParserSyncTest(generator_context_data, arrow_data, kError);
}

TEST_F(ParsingTest, SuperNoErrors) {
  // Tests that parser and preparser accept 'super' keyword in right places.
  const char* context_data[][2] = {{"class C { m() { ", "; } }"},
                                   {"class C { m() { k = ", "; } }"},
                                   {"class C { m() { foo(", "); } }"},
                                   {"class C { m() { () => ", "; } }"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {"super.x",       "super[27]",
                                  "new super.x",   "new super.x()",
                                  "new super[27]", "new super[27]()",
                                  "z.super",  // Ok, property lookup.
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, SuperErrors) {
  const char* context_data[][2] = {{"class C { m() { ", "; } }"},
                                   {"class C { m() { k = ", "; } }"},
                                   {"class C { m() { foo(", "); } }"},
                                   {"class C { m() { () => ", "; } }"},
                                   {nullptr, nullptr}};

  const char* expression_data[] = {"super",
                                   "super = x",
                                   "y = super",
                                   "f(super)",
                                   "new super",
                                   "new super()",
                                   "new super(12, 45)",
                                   "new new super",
                                   "new new super()",
                                   "new new super()()",
                                   nullptr};

  RunParserSyncTest(context_data, expression_data, kError);
}

TEST_F(ParsingTest, ImportExpressionSuccess) {
  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {nullptr, nullptr}
  };

  const char* data[] = {
    "import(1)",
    "import(y=x)",
    "f(...[import(y=x)])",
    "x = {[import(y=x)]: 1}",
    "var {[import(y=x)]: x} = {}",
    "({[import(y=x)]: x} = {})",
    "async () => { await import(x) }",
    "() => { import(x) }",
    "(import(y=x))",
    "{import(y=x)}",
    "import(import(x))",
    "x = import(x)",
    "var x = import(x)",
    "let x = import(x)",
    "for(x of import(x)) {}",
    "import(x).then()",
    nullptr
  };

  // clang-format on

  RunParserSyncTest(context_data, data, kSuccess);
  RunModuleParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ImportExpressionWithOptionsSuccess) {
  i::v8_flags.harmony_import_assertions = true;

  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {nullptr, nullptr}
  };

  const char* data[] = {
    "import(x,)",
    "import(x,1)",
    "import(x,y)",
    "import(x,y,)",
    "import(x, { 'a': 'b' })",
    "import(x, { a: 'b', 'c': 'd' },)",
    "import(x, { 'a': { b: 'c' }, 'd': 'e' },)",
    "import(x,import(y))",
    "import(x,y=z)",
    "import(x,[y, z])",
    "import(x,undefined)",
    nullptr
  };

  // clang-format on
  RunParserSyncTest(context_data, data, kSuccess);
  RunModuleParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ImportExpressionErrors) {
  {
    // clang-format off
    const char* context_data[][2] = {
      {"", ""},
      {"var ", ""},
      {"let ", ""},
      {"new ", ""},
      {nullptr, nullptr}
    };

    const char* data[] = {
      "import(",
      "import)",
      "import()",
      "import('x",
      "import('x']",
      "import['x')",
      "import = x",
      "import[",
      "import[]",
      "import]",
      "import[x]",
      "import{",
      "import{x",
      "import{x}",
      "import(x, y, z)",
      "import(...y)",
      "import(,)",
      "import(,y)",
      "import(;)",
      "[import]",
      "{import}",
      "import+",
      "import = 1",
      "import.wat",
      "new import(x)",
      nullptr
    };

    // clang-format on
    RunParserSyncTest(context_data, data, kError);

    // We ignore test error messages because the error message from
    // the parser/preparser is different for the same data depending
    // on the context.  For example, a top level "import{" is parsed
    // as an import declaration. The parser parses the import token
    // correctly and then shows an "Unexpected end of input" error
    // message because of the '{'. The preparser shows an "Unexpected
    // token '{'" because it's not a valid token in a CallExpression.
    RunModuleParserSyncTest(context_data, data, kError, nullptr, 0, nullptr, 0,
                            nullptr, 0, true, true);
  }

  {
    // clang-format off
    const char* context_data[][2] = {
      {"var ", ""},
      {"let ", ""},
      {nullptr, nullptr}
    };

    const char* data[] = {
      "import('x')",
      nullptr
    };

    // clang-format on
    RunParserSyncTest(context_data, data, kError);
    RunModuleParserSyncTest(context_data, data, kError);
  }

  // Import statements as arrow function params and destructuring targets.
  {
    // clang-format off
    const char* context_data[][2] = {
      {"(", ") => {}"},
      {"(a, ", ") => {}"},
      {"(1, ", ") => {}"},
      {"let f = ", " => {}"},
      {"[", "] = [1];"},
      {"{", "} = {'a': 1};"},
      {nullptr, nullptr}
    };

    const char* data[] = {
      "import(foo)",
      "import(1)",
      "import(y=x)",
      "import(import(x))",
      "import(x).then()",
      nullptr
    };

    // clang-format on
    RunParserSyncTest(context_data, data, kError);
    RunModuleParserSyncTest(context_data, data, kError);
  }
}

TEST_F(ParsingTest, ImportExpressionWithOptionsErrors) {
  {
    i::v8_flags.harmony_import_assertions = true;

    // clang-format off
    const char* context_data[][2] = {
      {"", ""},
      {"var ", ""},
      {"let ", ""},
      {"new ", ""},
      {nullptr, nullptr}
    };

    const char* data[] = {
      "import(x,,)",
      "import(x))",
      "import(x,))",
      "import(x,())",
      "import(x,y,,)",
      "import(x,y,z)",
      "import(x,y",
      "import(x,y,",
      "import(x,y(",
      nullptr
    };

    // clang-format on
    RunParserSyncTest(context_data, data, kError);
    RunModuleParserSyncTest(context_data, data, kError);
  }

  {
    // clang-format off
    const char* context_data[][2] = {
      {"var ", ""},
      {"let ", ""},
      {nullptr, nullptr}
    };

    const char* data[] = {
      "import('x',y)",
      nullptr
    };

    // clang-format on
    RunParserSyncTest(context_data, data, kError);
    RunModuleParserSyncTest(context_data, data, kError);
  }

  // Import statements as arrow function params and destructuring targets.
  {
    // clang-format off
    const char* context_data[][2] = {
      {"(", ") => {}"},
      {"(a, ", ") => {}"},
      {"(1, ", ") => {}"},
      {"let f = ", " => {}"},
      {"[", "] = [1];"},
      {"{", "} = {'a': 1};"},
      {nullptr, nullptr}
    };

    const char* data[] = {
      "import(foo,y)",
      "import(1,y)",
      "import(y=x,z)",
      "import(import(x),y)",
      "import(x,y).then()",
      nullptr
    };

    // clang-format on
    RunParserSyncTest(context_data, data, kError);
    RunModuleParserSyncTest(context_data, data, kError);
  }
}

TEST_F(ParsingTest, BasicImportAssertionParsing) {
  // clang-format off
  const char* kSources[] = {
    "import { a as b } from 'm.js' assert { };",
    "import n from 'n.js' assert { };",
    "export { a as b } from 'm.js' assert { };",
    "export * from 'm.js' assert { };",
    "import 'm.js' assert { };",
    "import * as foo from 'bar.js' assert { };",

    "import { a as b } from 'm.js' assert { a: 'b' };",
    "import { a as b } from 'm.js' assert { c: 'd' };",
    "import { a as b } from 'm.js' assert { 'c': 'd' };",
    "import { a as b } from 'm.js' assert { a: 'b', 'c': 'd', e: 'f' };",
    "import { a as b } from 'm.js' assert { 'c': 'd', };",
    "import n from 'n.js' assert { 'c': 'd' };",
    "export { a as b } from 'm.js' assert { 'c': 'd' };",
    "export * from 'm.js' assert { 'c': 'd' };",
    "import 'm.js' assert { 'c': 'd' };",
    "import * as foo from 'bar.js' assert { 'c': 'd' };",

    "import { a as b } from 'm.js' assert { \nc: 'd'};",
    "import { a as b } from 'm.js' assert { c:\n 'd'};",
    "import { a as b } from 'm.js' assert { c:'d'\n};",

    "import { a as b } from 'm.js' assert { '0': 'b', };",
  };
  // clang-format on

  i::v8_flags.harmony_import_assertions = true;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kSources[i]);

    // Show that parsing as a module works
    {
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      flags.set_is_module(true);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK_PARSE_PROGRAM(&info, script, isolate);
    }

    // And that parsing a script does not.
    {
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                      parsing::ReportStatisticsMode::kYes));
      CHECK(info.pending_error_handler()->has_pending_error());
    }
  }
}

TEST_F(ParsingTest, ImportAssertionParsingErrors) {
  // clang-format off
  const char* kErrorSources[] = {
    "import { a } from 'm.js' assert {;",
    "import { a } from 'm.js' assert };",
    "import { a } from 'm.js' , assert { };",
    "import { a } from 'm.js' assert , { };",
    "import { a } from 'm.js' assert { , };",
    "import { a } from 'm.js' assert { b };",
    "import { a } from 'm.js' assert { 'b' };",
    "import { a } from 'm.js' assert { for };",
    "import { a } from 'm.js' assert { assert };",
    "export { a } assert { };",
    "export * assert { };",

    "import 'm.js'\n assert { };",
    "import 'm.js' \nassert { };",
    "import { a } from 'm.js'\n assert { };",
    "export * from 'm.js'\n assert { };",

    "import { a } from 'm.js' assert { x: 2 };",
    "import { a } from 'm.js' assert { b: c };",
    "import { a } from 'm.js' assert { 'b': c };",
    "import { a } from 'm.js' assert { , b: c };",
    "import { a } from 'm.js' assert { a: 'b', a: 'c' };",
    "import { a } from 'm.js' assert { a: 'b', 'a': 'c' };",

    "import 'm.js' with { a: 'b' };"
  };
  // clang-format on

  i::v8_flags.harmony_import_assertions = true;
  i::v8_flags.harmony_import_attributes = false;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kErrorSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kErrorSources[i]);

    i::Handle<i::Script> script = factory->NewScript(source);
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_is_module(true);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                    parsing::ReportStatisticsMode::kYes));
    CHECK(info.pending_error_handler()->has_pending_error());
  }
}

TEST_F(ParsingTest, BasicImportAttributesParsing) {
  // clang-format off
  const char* kSources[] = {
    "import { a as b } from 'm.js' with { };",
    "import n from 'n.js' with { };",
    "export { a as b } from 'm.js' with { };",
    "export * from 'm.js' with { };",
    "import 'm.js' with { };",
    "import * as foo from 'bar.js' with { };",

    "import { a as b } from 'm.js' with { a: 'b' };",
    "import { a as b } from 'm.js' with { c: 'd' };",
    "import { a as b } from 'm.js' with { 'c': 'd' };",
    "import { a as b } from 'm.js' with { a: 'b', 'c': 'd', e: 'f' };",
    "import { a as b } from 'm.js' with { 'c': 'd', };",
    "import n from 'n.js' with { 'c': 'd' };",
    "export { a as b } from 'm.js' with { 'c': 'd' };",
    "export * from 'm.js' with { 'c': 'd' };",
    "import 'm.js' with { 'c': 'd' };",
    "import * as foo from 'bar.js' with { 'c': 'd' };",

    "import { a as b } from 'm.js' with { \nc: 'd'};",
    "import { a as b } from 'm.js' with { c:\n 'd'};",
    "import { a as b } from 'm.js' with { c:'d'\n};",

    "import { a as b } from 'm.js' with { '0': 'b', };",

    "import 'm.js'\n with { };",
    "import 'm.js' \nwith { };",
    "import { a } from 'm.js'\n with { };",
    "export * from 'm.js'\n with { };"
  };
  // clang-format on

  i::v8_flags.harmony_import_attributes = true;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kSources[i]);

    // Show that parsing as a module works
    {
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      flags.set_is_module(true);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK_PARSE_PROGRAM(&info, script, isolate);
    }

    // And that parsing a script does not.
    {
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                      parsing::ReportStatisticsMode::kYes));
      CHECK(info.pending_error_handler()->has_pending_error());
    }
  }
}

TEST_F(ParsingTest, ImportAttributesParsingErrors) {
  // clang-format off
  const char* kErrorSources[] = {
    "import { a } from 'm.js' with {;",
    "import { a } from 'm.js' with };",
    "import { a } from 'm.js' , with { };",
    "import { a } from 'm.js' with , { };",
    "import { a } from 'm.js' with { , };",
    "import { a } from 'm.js' with { b };",
    "import { a } from 'm.js' with { 'b' };",
    "import { a } from 'm.js' with { for };",
    "import { a } from 'm.js' with { with };",
    "export { a } with { };",
    "export * with { };",

    "import { a } from 'm.js' with { x: 2 };",
    "import { a } from 'm.js' with { b: c };",
    "import { a } from 'm.js' with { 'b': c };",
    "import { a } from 'm.js' with { , b: c };",
    "import { a } from 'm.js' with { a: 'b', a: 'c' };",
    "import { a } from 'm.js' with { a: 'b', 'a': 'c' };",

    "import 'm.js' assert { a: 'b' };"
  };
  // clang-format on

  i::v8_flags.harmony_import_assertions = false;
  i::v8_flags.harmony_import_attributes = true;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kErrorSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kErrorSources[i]);

    i::Handle<i::Script> script = factory->NewScript(source);
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_is_module(true);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                    parsing::ReportStatisticsMode::kYes));
    CHECK(info.pending_error_handler()->has_pending_error());
  }
}

TEST_F(ParsingTest, BasicImportAttributesAndAssertionsParsing) {
  // clang-format off
  const char* kSources[] = {
    "import { a } from 'm.js' assert { };",
    "import { a } from 'm.js' with { };",
    "import { a } from 'm.js'\n with { };",
  };
  // clang-format on

  i::v8_flags.harmony_import_assertions = true;
  i::v8_flags.harmony_import_attributes = true;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kSources[i]);

    // Show that parsing as a module works
    {
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      flags.set_is_module(true);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK_PARSE_PROGRAM(&info, script, isolate);
    }

    // And that parsing a script does not.
    {
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                      parsing::ReportStatisticsMode::kYes));
      CHECK(info.pending_error_handler()->has_pending_error());
    }
  }
}

TEST_F(ParsingTest, ImportAttributesAndAssertionsParsingErrors) {
  // clang-format off
  const char* kErrorSources[] = {
    "import { a } from 'm.js'\n assert { };",
    "import { a } from 'm.js' with { } assert { };",
    "import { a } from 'm.js' with assert { };",
    "import { a } from 'm.js' assert { } with { };",
    "import { a } from 'm.js' assert with { };",
  };
  // clang-format on

  i::v8_flags.harmony_import_assertions = true;
  i::v8_flags.harmony_import_attributes = true;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kErrorSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kErrorSources[i]);

    i::Handle<i::Script> script = factory->NewScript(source);
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_is_module(true);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                    parsing::ReportStatisticsMode::kYes));
    CHECK(info.pending_error_handler()->has_pending_error());
  }
}

TEST_F(ParsingTest, SuperCall) {
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  const char* success_data[] = {
      "class C extends B { constructor() { super(); } }",
      "class C extends B { constructor() { () => super(); } }", nullptr};

  RunParserSyncTest(context_data, success_data, kSuccess);

  const char* error_data[] = {"class C { constructor() { super(); } }",
                              "class C { method() { super(); } }",
                              "class C { method() { () => super(); } }",
                              "class C { *method() { super(); } }",
                              "class C { get x() { super(); } }",
                              "class C { set x(_) { super(); } }",
                              "({ method() { super(); } })",
                              "({ *method() { super(); } })",
                              "({ get x() { super(); } })",
                              "({ set x(_) { super(); } })",
                              "({ f: function() { super(); } })",
                              "(function() { super(); })",
                              "var f = function() { super(); }",
                              "({ f: function*() { super(); } })",
                              "(function*() { super(); })",
                              "var f = function*() { super(); }",
                              nullptr};

  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, SuperNewNoErrors) {
  const char* context_data[][2] = {{"class C { constructor() { ", " } }"},
                                   {"class C { *method() { ", " } }"},
                                   {"class C { get x() { ", " } }"},
                                   {"class C { set x(_) { ", " } }"},
                                   {"({ method() { ", " } })"},
                                   {"({ *method() { ", " } })"},
                                   {"({ get x() { ", " } })"},
                                   {"({ set x(_) { ", " } })"},
                                   {nullptr, nullptr}};

  const char* expression_data[] = {"new super.x;", "new super.x();",
                                   "() => new super.x;", "() => new super.x();",
                                   nullptr};

  RunParserSyncTest(context_data, expression_data, kSuccess);
}

TEST_F(ParsingTest, SuperNewErrors) {
  const char* context_data[][2] = {{"class C { method() { ", " } }"},
                                   {"class C { *method() { ", " } }"},
                                   {"class C { get x() { ", " } }"},
                                   {"class C { set x(_) { ", " } }"},
                                   {"({ method() { ", " } })"},
                                   {"({ *method() { ", " } })"},
                                   {"({ get x() { ", " } })"},
                                   {"({ set x(_) { ", " } })"},
                                   {"({ f: function() { ", " } })"},
                                   {"(function() { ", " })"},
                                   {"var f = function() { ", " }"},
                                   {"({ f: function*() { ", " } })"},
                                   {"(function*() { ", " })"},
                                   {"var f = function*() { ", " }"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {"new super;", "new super();",
                                  "() => new super;", "() => new super();",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, SuperErrorsNonMethods) {
  // super is only allowed in methods, accessors and constructors.
  const char* context_data[][2] = {{"", ";"},
                                   {"k = ", ";"},
                                   {"foo(", ");"},
                                   {"if (", ") {}"},
                                   {"if (true) {", "}"},
                                   {"if (false) {} else {", "}"},
                                   {"while (true) {", "}"},
                                   {"function f() {", "}"},
                                   {"class C extends (", ") {}"},
                                   {"class C { m() { function f() {", "} } }"},
                                   {"({ m() { function f() {", "} } })"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {
      "super",           "super = x",   "y = super",     "f(super)",
      "super.x",         "super[27]",   "super.x()",     "super[27]()",
      "super()",         "new super.x", "new super.x()", "new super[27]",
      "new super[27]()", nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, NoErrorsMethodDefinition) {
  const char* context_data[][2] = {{"({", "});"},
                                   {"'use strict'; ({", "});"},
                                   {"({*", "});"},
                                   {"'use strict'; ({*", "});"},
                                   {nullptr, nullptr}};

  const char* object_literal_body_data[] = {
      "m() {}",       "m(x) { return x; }", "m(x, y) {}, n() {}",
      "set(x, y) {}", "get(x, y) {}",       nullptr};

  RunParserSyncTest(context_data, object_literal_body_data, kSuccess);
}

TEST_F(ParsingTest, MethodDefinitionNames) {
  const char* context_data[][2] = {{"({", "(x, y) {}});"},
                                   {"'use strict'; ({", "(x, y) {}});"},
                                   {"({*", "(x, y) {}});"},
                                   {"'use strict'; ({*", "(x, y) {}});"},
                                   {nullptr, nullptr}};

  const char* name_data[] = {
      "m", "'m'", "\"m\"", "\"m n\"", "true", "false", "null", "0", "1.2",
      "1e1", "1E1", "1e+1", "1e-1",

      // Keywords
      "async", "await", "break", "case", "catch", "class", "const", "continue",
      "debugger", "default", "delete", "do", "else", "enum", "export",
      "extends", "finally", "for", "function", "if", "implements", "import",
      "in", "instanceof", "interface", "let", "new", "package", "private",
      "protected", "public", "return", "static", "super", "switch", "this",
      "throw", "try", "typeof", "var", "void", "while", "with", "yield",
      nullptr};

  RunParserSyncTest(context_data, name_data, kSuccess);
}

TEST_F(ParsingTest, MethodDefinitionStrictFormalParamereters) {
  const char* context_data[][2] = {{"({method(", "){}});"},
                                   {"'use strict'; ({method(", "){}});"},
                                   {"({*method(", "){}});"},
                                   {"'use strict'; ({*method(", "){}});"},
                                   {nullptr, nullptr}};

  const char* params_data[] = {"x, x", "x, y, x", "var", "const", nullptr};

  RunParserSyncTest(context_data, params_data, kError);
}

TEST_F(ParsingTest, MethodDefinitionEvalArguments) {
  const char* strict_context_data[][2] = {
      {"'use strict'; ({method(", "){}});"},
      {"'use strict'; ({*method(", "){}});"},
      {nullptr, nullptr}};
  const char* sloppy_context_data[][2] = {
      {"({method(", "){}});"}, {"({*method(", "){}});"}, {nullptr, nullptr}};

  const char* data[] = {"eval", "arguments", nullptr};

  // Fail in strict mode
  RunParserSyncTest(strict_context_data, data, kError);

  // OK in sloppy mode
  RunParserSyncTest(sloppy_context_data, data, kSuccess);
}

TEST_F(ParsingTest, MethodDefinitionDuplicateEvalArguments) {
  const char* context_data[][2] = {{"'use strict'; ({method(", "){}});"},
                                   {"'use strict'; ({*method(", "){}});"},
                                   {"({method(", "){}});"},
                                   {"({*method(", "){}});"},
                                   {nullptr, nullptr}};

  const char* data[] = {"eval, eval", "eval, a, eval", "arguments, arguments",
                        "arguments, a, arguments", nullptr};

  // In strict mode, the error is using "eval" or "arguments" as parameter names
  // In sloppy mode, the error is that eval / arguments are duplicated
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, MethodDefinitionDuplicateProperty) {
  const char* context_data[][2] = {{"'use strict'; ({", "});"},
                                   {nullptr, nullptr}};

  const char* params_data[] = {"x: 1, x() {}",
                               "x() {}, x: 1",
                               "x() {}, get x() {}",
                               "x() {}, set x(_) {}",
                               "x() {}, x() {}",
                               "x() {}, y() {}, x() {}",
                               "x() {}, \"x\"() {}",
                               "x() {}, 'x'() {}",
                               "0() {}, '0'() {}",
                               "1.0() {}, 1: 1",

                               "x: 1, *x() {}",
                               "*x() {}, x: 1",
                               "*x() {}, get x() {}",
                               "*x() {}, set x(_) {}",
                               "*x() {}, *x() {}",
                               "*x() {}, y() {}, *x() {}",
                               "*x() {}, *\"x\"() {}",
                               "*x() {}, *'x'() {}",
                               "*0() {}, *'0'() {}",
                               "*1.0() {}, 1: 1",

                               nullptr};

  RunParserSyncTest(context_data, params_data, kSuccess);
}

TEST_F(ParsingTest, ClassExpressionNoErrors) {
  const char* context_data[][2] = {
      {"(", ");"}, {"var C = ", ";"}, {"bar, ", ";"}, {nullptr, nullptr}};
  const char* class_data[] = {"class {}",
                              "class name {}",
                              "class extends F {}",
                              "class name extends F {}",
                              "class extends (F, G) {}",
                              "class name extends (F, G) {}",
                              "class extends class {} {}",
                              "class name extends class {} {}",
                              "class extends class base {} {}",
                              "class name extends class base {} {}",
                              nullptr};

  RunParserSyncTest(context_data, class_data, kSuccess);
}

TEST_F(ParsingTest, ClassDeclarationNoErrors) {
  const char* context_data[][2] = {{"'use strict'; ", ""},
                                   {"'use strict'; {", "}"},
                                   {"'use strict'; if (true) {", "}"},
                                   {nullptr, nullptr}};
  const char* statement_data[] = {"class name {}",
                                  "class name extends F {}",
                                  "class name extends (F, G) {}",
                                  "class name extends class {} {}",
                                  "class name extends class base {} {}",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ClassBodyNoErrors) {
  // clang-format off
  // Tests that parser and preparser accept valid class syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    ";",
    ";;",
    "m() {}",
    "m() {};",
    "; m() {}",
    "m() {}; n(x) {}",
    "get x() {}",
    "set x(v) {}",
    "get() {}",
    "set() {}",
    "*g() {}",
    "*g() {};",
    "; *g() {}",
    "*g() {}; *h(x) {}",
    "async *x(){}",
    "static() {}",
    "get static() {}",
    "set static(v) {}",
    "static m() {}",
    "static get x() {}",
    "static set x(v) {}",
    "static get() {}",
    "static set() {}",
    "static static() {}",
    "static get static() {}",
    "static set static(v) {}",
    "*static() {}",
    "static *static() {}",
    "*get() {}",
    "*set() {}",
    "static *g() {}",
    "async(){}",
    "*async(){}",
    "static async(){}",
    "static *async(){}",
    "static async *x(){}",

    // Escaped 'static' should be allowed anywhere
    // static-as-PropertyName is.
    "st\\u0061tic() {}",
    "get st\\u0061tic() {}",
    "set st\\u0061tic(v) {}",
    "static st\\u0061tic() {}",
    "static get st\\u0061tic() {}",
    "static set st\\u0061tic(v) {}",
    "*st\\u0061tic() {}",
    "static *st\\u0061tic() {}",

    "static async x(){}",
    "static async(){}",
    "static *async(){}",
    "async x(){}",
    "async 0(){}",
    "async get(){}",
    "async set(){}",
    "async static(){}",
    "async async(){}",
    "async(){}",
    "*async(){}",
    nullptr};
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, ClassPropertyNameNoErrors) {
  const char* context_data[][2] = {{"(class {", "() {}});"},
                                   {"(class { get ", "() {}});"},
                                   {"(class { set ", "(v) {}});"},
                                   {"(class { static ", "() {}});"},
                                   {"(class { static get ", "() {}});"},
                                   {"(class { static set ", "(v) {}});"},
                                   {"(class { *", "() {}});"},
                                   {"(class { static *", "() {}});"},
                                   {"class C {", "() {}}"},
                                   {"class C { get ", "() {}}"},
                                   {"class C { set ", "(v) {}}"},
                                   {"class C { static ", "() {}}"},
                                   {"class C { static get ", "() {}}"},
                                   {"class C { static set ", "(v) {}}"},
                                   {"class C { *", "() {}}"},
                                   {"class C { static *", "() {}}"},
                                   {nullptr, nullptr}};
  const char* name_data[] = {
      "42",       "42.5",  "42e2",  "42e+2",   "42e-2",  "null",
      "false",    "true",  "'str'", "\"str\"", "static", "get",
      "set",      "var",   "const", "let",     "this",   "class",
      "function", "yield", "if",    "else",    "for",    "while",
      "do",       "try",   "catch", "finally", nullptr};

  RunParserSyncTest(context_data, name_data, kSuccess);
}

TEST_F(ParsingTest, StaticClassFieldsNoErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    // Basic syntax
    "static a = 0;",
    "static a = 0; b",
    "static a = 0; b(){}",
    "static a = 0; *b(){}",
    "static a = 0; ['b'](){}",
    "static a;",
    "static a; b;",
    "static a; b(){}",
    "static a; *b(){}",
    "static a; ['b'](){}",
    "static ['a'] = 0;",
    "static ['a'] = 0; b",
    "static ['a'] = 0; b(){}",
    "static ['a'] = 0; *b(){}",
    "static ['a'] = 0; ['b'](){}",
    "static ['a'];",
    "static ['a']; b;",
    "static ['a']; b(){}",
    "static ['a']; *b(){}",
    "static ['a']; ['b'](){}",

    "static 0 = 0;",
    "static 0;",
    "static 'a' = 0;",
    "static 'a';",

    "static c = [c] = c",

    // ASI
    "static a = 0\n",
    "static a = 0\n b",
    "static a = 0\n b(){}",
    "static a\n",
    "static a\n b\n",
    "static a\n b(){}",
    "static a\n *b(){}",
    "static a\n ['b'](){}",
    "static ['a'] = 0\n",
    "static ['a'] = 0\n b",
    "static ['a'] = 0\n b(){}",
    "static ['a']\n",
    "static ['a']\n b\n",
    "static ['a']\n b(){}",
    "static ['a']\n *b(){}",
    "static ['a']\n ['b'](){}",

    "static a = function t() { arguments; }",
    "static a = () => function t() { arguments; }",

    // ASI edge cases
    "static a\n get",
    "static get\n *a(){}",
    "static a\n static",

    // Misc edge cases
    "static yield",
    "static yield = 0",
    "static yield\n a",
    "static async;",
    "static async = 0;",
    "static async",
    "static async = 0",
    "static async\n a(){}",  // a field named async, and a method named a.
    "static async\n a",
    "static await;",
    "static await = 0;",
    "static await\n a",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, ClassFieldsNoErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    // Basic syntax
    "a = 0;",
    "a = 0; b",
    "a = 0; b(){}",
    "a = 0; *b(){}",
    "a = 0; ['b'](){}",
    "a;",
    "a; b;",
    "a; b(){}",
    "a; *b(){}",
    "a; ['b'](){}",
    "['a'] = 0;",
    "['a'] = 0; b",
    "['a'] = 0; b(){}",
    "['a'] = 0; *b(){}",
    "['a'] = 0; ['b'](){}",
    "['a'];",
    "['a']; b;",
    "['a']; b(){}",
    "['a']; *b(){}",
    "['a']; ['b'](){}",

    "0 = 0;",
    "0;",
    "'a' = 0;",
    "'a';",

    "c = [c] = c",

    // ASI
    "a = 0\n",
    "a = 0\n b",
    "a = 0\n b(){}",
    "a\n",
    "a\n b\n",
    "a\n b(){}",
    "a\n *b(){}",
    "a\n ['b'](){}",
    "['a'] = 0\n",
    "['a'] = 0\n b",
    "['a'] = 0\n b(){}",
    "['a']\n",
    "['a']\n b\n",
    "['a']\n b(){}",
    "['a']\n *b(){}",
    "['a']\n ['b'](){}",

    // ASI edge cases
    "a\n get",
    "get\n *a(){}",
    "a\n static",

    "a = function t() { arguments; }",
    "a = () => function() { arguments; }",

    // Misc edge cases
    "yield",
    "yield = 0",
    "yield\n a",
    "async;",
    "async = 0;",
    "async",
    "async = 0",
    "async\n a(){}",  // a field named async, and a method named a.
    "async\n a",
    "await;",
    "await = 0;",
    "await\n a",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, PrivateMethodsNoErrors) {
  // clang-format off
  // Tests proposed class methods syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    // Basic syntax
    "#a() { }",
    "get #a() { }",
    "set #a(foo) { }",
    "*#a() { }",
    "async #a() { }",
    "async *#a() { }",

    "#a() { } #b() {}",
    "get #a() { } set #a(foo) {}",
    "get #a() { } get #b() {} set #a(foo) {}",
    "get #a() { } get #b() {} set #a(foo) {} set #b(foo) {}",
    "set #a(foo) { } set #b(foo) {}",
    "get #a() { } get #b() {}",

    "#a() { } static a() {}",
    "#a() { } a() {}",
    "#a() { } a() {} static a() {}",
    "get #a() { } get a() {} static get a() {}",
    "set #a(foo) { } set a(foo) {} static set a(foo) {}",

    "#a() { } get #b() {}",
    "#a() { } async #b() {}",
    "#a() { } async *#b() {}",

    // With arguments
    "#a(...args) { }",
    "#a(a = 1) { }",
    "get #a() { }",
    "set #a(a = (...args) => {}) { }",

    // Misc edge cases
    "#get() {}",
    "#set() {}",
    "#yield() {}",
    "#await() {}",
    "#async() {}",
    "#static() {}",
    "#arguments() {}",
    "get #yield() {}",
    "get #await() {}",
    "get #async() {}",
    "get #get() {}",
    "get #static() {}",
    "get #arguments() {}",
    "set #yield(test) {}",
    "set #async(test) {}",
    "set #await(test) {}",
    "set #set(test) {}",
    "set #static(test) {}",
    "set #arguments(test) {}",
    "async #yield() {}",
    "async #async() {}",
    "async #await() {}",
    "async #get() {}",
    "async #set() {}",
    "async #static() {}",
    "async #arguments() {}",
    "*#async() {}",
    "*#await() {}",
    "*#yield() {}",
    "*#get() {}",
    "*#set() {}",
    "*#static() {}",
    "*#arguments() {}",
    "async *#yield() {}",
    "async *#async() {}",
    "async *#await() {}",
    "async *#get() {}",
    "async *#set() {}",
    "async *#static() {}",
    "async *#arguments() {}",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, PrivateMethodsAndFieldsNoErrors) {
  // clang-format off
  // Tests proposed class methods syntax in combination with fields
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    // Basic syntax
    "#b;#a() { }",
    "#b;get #a() { }",
    "#b;set #a(foo) { }",
    "#b;*#a() { }",
    "#b;async #a() { }",
    "#b;async *#a() { }",
    "#b = 1;#a() { }",
    "#b = 1;get #a() { }",
    "#b = 1;set #a(foo) { }",
    "#b = 1;*#a() { }",
    "#b = 1;async #a() { }",
    "#b = 1;async *#a() { }",

    // With public fields
    "a;#a() { }",
    "a;get #a() { }",
    "a;set #a(foo) { }",
    "a;*#a() { }",
    "a;async #a() { }",
    "a;async *#a() { }",
    "a = 1;#a() { }",
    "a = 1;get #a() { }",
    "a = 1;set #a(foo) { }",
    "a = 1;*#a() { }",
    "a = 1;async #a() { }",
    "a = 1;async *#a() { }",

    // ASI
    "#a = 0\n #b(){}",
    "#a\n *#b(){}",
    "#a = 0\n get #b(){}",
    "#a\n *#b(){}",

    "b = 0\n #b(){}",
    "b\n *#b(){}",
    "b = 0\n get #b(){}",
    "b\n *#b(){}",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, PrivateMethodsErrors) {
  // clang-format off
  // Tests proposed class methods syntax in combination with fields
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "#a() : 0",
    "#a() =",
    "#a() => {}",
    "#a => {}",
    "*#a() = 0",
    "*#a() => 0",
    "*#a() => {}",
    "get #a()[]",
    "yield #a()[]",
    "yield #a => {}",
    "async #a() = 0",
    "async #a => {}",
    "#a(arguments) {}",
    "set #a(arguments) {}",

    "#['a']() { }",
    "get #['a']() { }",
    "set #['a'](foo) { }",
    "*#['a']() { }",
    "async #['a']() { }",
    "async *#['a]() { }",

    "get #a() {} get #a() {}",
    "get #a() {} get #['a']() {}",
    "set #a(val) {} set #a(val) {}",
    "set #a(val) {} set #['a'](val) {}",

    "#a\n#",
    "#a() c",
    "#a() #",
    "#a(arg) c",
    "#a(arg) #",
    "#a(arg) #c",
    "#a#",
    "#a#b",
    "#a#b(){}",
    "#[test](){}",

    "async *#constructor() {}",
    "*#constructor() {}",
    "async #constructor() {}",
    "set #constructor(test) {}",
    "#constructor() {}",
    "get #constructor() {}",

    "static async *#constructor() {}",
    "static *#constructor() {}",
    "static async #constructor() {}",
    "static set #constructor(test) {}",
    "static #constructor() {}",
    "static get #constructor() {}",

    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kError);
}

// Test that private members parse in class bodies nested in object literals
TEST_F(ParsingTest, PrivateMembersNestedInObjectLiteralsNoErrors) {
  // clang-format off
  const char* context_data[][2] = {{"({", "})"},
                                   {"'use strict'; ({", "});"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "a: class { #a = 1 }",
    "a: class { #a = () => {} }",
    "a: class { #a }",
    "a: class { #a() { } }",
    "a: class { get #a() { } }",
    "a: class { set #a(foo) { } }",
    "a: class { *#a() { } }",
    "a: class { async #a() { } }",
    "a: class { async *#a() { } }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

// Test that private members parse in class bodies nested in classes
TEST_F(ParsingTest, PrivateMembersInNestedClassNoErrors) {
  // clang-format off
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "a = class { #a = 1 }",
    "a = class { #a = () => {} }",
    "a = class { #a }",
    "a = class { #a() { } }",
    "a = class { get #a() { } }",
    "a = class { set #a(foo) { } }",
    "a = class { *#a() { } }",
    "a = class { async #a() { } }",
    "a = class { async *#a() { } }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

// Test that private members do not parse outside class bodies
TEST_F(ParsingTest, PrivateMembersInNonClassErrors) {
  // clang-format off
  const char* context_data[][2] = {{"", ""},
                                   {"({", "})"},
                                   {"'use strict'; ({", "});"},
                                   {"function() {", "}"},
                                   {"() => {", "}"},
                                   {"class C { test() {", "} }"},
                                   {"const {", "} = {}"},
                                   {"({", "} = {})"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "#a = 1",
    "#a = () => {}",
    "#a",
    "#a() { }",
    "get #a() { }",
    "set #a(foo) { }",
    "*#a() { }",
    "async #a() { }",
    "async *#a() { }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kError);
}

// Test that nested private members parse
TEST_F(ParsingTest, PrivateMembersNestedNoErrors) {
  // clang-format off
  const char* context_data[][2] = {{"(class { get #a() { ", "} });"},
                                   {
                                     "(class { set #a(val) {} get #a() { ",
                                     "} });"
                                    },
                                   {"(class { set #a(val) {", "} });"},
                                   {"(class { #a() { ", "} });"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "class C { #a() {} }",
    "class C { get #a() {} }",
    "class C { get #a() {} set #a(val) {} }",
    "class C { set #a(val) {} }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

// Test that acessing undeclared private members result in early errors
TEST_F(ParsingTest, PrivateMembersEarlyErrors) {
  // clang-format off
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "set #b(val) { this.#a = val; }",
    "get #b() { return this.#a; }",
    "foo() { return this.#a; }",
    "foo() { this.#a = 1; }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kError);
}

// Test that acessing wrong kind private members do not error early.
// Instead these should be runtime errors.
TEST_F(ParsingTest, PrivateMembersWrongAccessNoEarlyErrors) {
  // clang-format off
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    // Private setter only
    "set #b(val) {} fn() { return this.#b; }",
    "set #b(val) {} fn() { this.#b++; }",
    // Nested private setter only
    R"(get #b() {}
    fn() {
      return new class { set #b(val) {} fn() { this.#b++; } };
    })",
    R"(get #b() {}
    fn() {
      return new class { set #b(val) {} fn() { return this.#b; } };
    })",

    // Private getter only
    "get #b() { } fn() { this.#b = 1; }",
    "get #b() { } fn() { this.#b++; }",
    "get #b() { } fn(obj) { ({ y: this.#b } = obj); }",
    // Nested private getter only
    R"(set #b(val) {}
    fn() {
      return new class { get #b() {} fn() { this.#b++; } };
    })",
    R"(set #b(val) {}
    fn() {
      return new class { get #b() {} fn() { this.#b = 1; } };
    })",
    R"(set #b(val) {}
    fn() {
      return new class { get #b() {} fn() { ({ y: this.#b } = obj); } };
    })",

    // Writing to private methods
    "#b() { } fn() { this.#b = 1; }",
    "#b() { } fn() { this.#b++; }",
    "#b() {} fn(obj) { ({ y: this.#b } = obj); }",
    // Writing to nested private methods
    R"(#b() {}
    fn() {
      return new class { get #b() {} fn() { this.#b++; } };
    })",
    R"(#b() {}
    fn() {
      return new class { get #b() {} fn() { this.#b = 1; } };
    })",
    R"(#b() {}
    fn() {
      return new class { get #b() {} fn() { ({ y: this.#b } = obj); } };
    })",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, PrivateStaticClassMethodsAndAccessorsNoErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "static #a() { }",
    "static get #a() { }",
    "static set #a(val) { }",
    "static get #a() { } static set #a(val) { }",
    "static *#a() { }",
    "static async #a() { }",
    "static async *#a() { }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, PrivateStaticClassMethodsAndAccessorsDuplicateErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "static get #a() {} static get #a() {}",
    "static get #a() {} static #a() {}",
    "static get #a() {} get #a() {}",
    "static get #a() {} set #a(val) {}",
    "static get #a() {} #a() {}",

    "static set #a(val) {} static set #a(val) {}",
    "static set #a(val) {} static #a() {}",
    "static set #a(val) {} get #a() {}",
    "static set #a(val) {} set #a(val) {}",
    "static set #a(val) {} #a() {}",

    "static #a() {} static #a() {}",
    "static #a() {} #a(val) {}",
    "static #a() {} set #a(val) {}",
    "static #a() {} get #a() {}",

    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, PrivateClassFieldsNoErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    // Basic syntax
    "#a = 0;",
    "#a = 0; #b",
    "#a = 0; b",
    "#a = 0; b(){}",
    "#a = 0; *b(){}",
    "#a = 0; ['b'](){}",
    "#a;",
    "#a; #b;",
    "#a; b;",
    "#a; b(){}",
    "#a; *b(){}",
    "#a; ['b'](){}",

    // ASI
    "#a = 0\n",
    "#a = 0\n #b",
    "#a = 0\n b",
    "#a = 0\n b(){}",
    "#a\n",
    "#a\n #b\n",
    "#a\n b\n",
    "#a\n b(){}",
    "#a\n *b(){}",
    "#a\n ['b'](){}",

    // ASI edge cases
    "#a\n get",
    "#get\n *a(){}",
    "#a\n static",

    "#a = function t() { arguments; }",
    "#a = () => function() { arguments; }",

    // Misc edge cases
    "#yield",
    "#yield = 0",
    "#yield\n a",
    "#async;",
    "#async = 0;",
    "#async",
    "#async = 0",
    "#async\n a(){}",  // a field named async, and a method named a.
    "#async\n a",
    "#await;",
    "#await = 0;",
    "#await\n a",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, StaticClassFieldsErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "static a : 0",
    "static a =",
    "static constructor",
    "static prototype",
    "static *a = 0",
    "static *a",
    "static get a",
    "static get\n a",
    "static yield a",
    "static async a = 0",
    "static async a",

    "static a = arguments",
    "static a = () => arguments",
    "static a = () => { arguments }",
    "static a = arguments[0]",
    "static a = delete arguments[0]",
    "static a = f(arguments)",
    "static a = () => () => arguments",

    // ASI requires a linebreak
    "static a b",
    "static a = 0 b",

    "static c = [1] = [c]",

    // ASI requires that the next token is not part of any legal production
    "static a = 0\n *b(){}",
    "static a = 0\n ['b'](){}",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, ClassFieldsErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "a : 0",
    "a =",
    "constructor",
    "*a = 0",
    "*a",
    "get a",
    "yield a",
    "async a = 0",
    "async a",

    "a = arguments",
    "a = () => arguments",
    "a = () => { arguments }",
    "a = arguments[0]",
    "a = delete arguments[0]",
    "a = f(arguments)",
    "a = () => () => arguments",

    // ASI requires a linebreak
    "a b",
    "a = 0 b",

    "c = [1] = [c]",

    // ASI requires that the next token is not part of any legal production
    "a = 0\n *b(){}",
    "a = 0\n ['b'](){}",
    "get\n a",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, PrivateClassFieldsErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    "#a : 0",
    "#a =",
    "#*a = 0",
    "#*a",
    "#get a",
    "#yield a",
    "#async a = 0",
    "#async a",

    "#a; #a",
    "#a = 1; #a",
    "#a; #a = 1;",

    "#constructor",
    "#constructor = function() {}",

    "# a = 0",
    "#get a() { }",
    "#set a() { }",
    "#*a() { }",
    "async #*a() { }",

    "#0 = 0;",
    "#0;",
    "#'a' = 0;",
    "#'a';",

    "#['a']",
    "#['a'] = 1",
    "#[a]",
    "#[a] = 1",

    "#a = arguments",
    "#a = () => arguments",
    "#a = () => { arguments }",
    "#a = arguments[0]",
    "#a = delete arguments[0]",
    "#a = f(arguments)",
    "#a = () => () => arguments",

    "foo() { delete this.#a }",
    "foo() { delete this.x.#a }",
    "foo() { delete this.x().#a }",

    "foo() { delete this?.#a }",
    "foo() { delete this.x?.#a }",
    "foo() { delete this?.x.#a }",
    "foo() { delete this.x()?.#a }",
    "foo() { delete this?.x().#a }",

    "foo() { delete f.#a }",
    "foo() { delete f.x.#a }",
    "foo() { delete f.x().#a }",

    "foo() { delete f?.#a }",
    "foo() { delete f.x?.#a }",
    "foo() { delete f?.x.#a }",
    "foo() { delete f.x()?.#a }",
    "foo() { delete f?.x().#a }",

    // ASI requires a linebreak
    "#a b",
    "#a = 0 b",

    // ASI requires that the next token is not part of any legal production
    "#a = 0\n *b(){}",
    "#a = 0\n ['b'](){}",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, PrivateStaticClassFieldsNoErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    // Basic syntax
    "static #a = 0;",
    "static #a = 0; b",
    "static #a = 0; #b",
    "static #a = 0; b(){}",
    "static #a = 0; *b(){}",
    "static #a = 0; ['b'](){}",
    "static #a;",
    "static #a; b;",
    "static #a; b(){}",
    "static #a; *b(){}",
    "static #a; ['b'](){}",

    "#prototype",
    "#prototype = function() {}",

    // ASI
    "static #a = 0\n",
    "static #a = 0\n b",
    "static #a = 0\n #b",
    "static #a = 0\n b(){}",
    "static #a\n",
    "static #a\n b\n",
    "static #a\n #b\n",
    "static #a\n b(){}",
    "static #a\n *b(){}",
    "static #a\n ['b'](){}",

    "static #a = function t() { arguments; }",
    "static #a = () => function t() { arguments; }",

    // ASI edge cases
    "static #a\n get",
    "static #get\n *a(){}",
    "static #a\n static",

    // Misc edge cases
    "static #yield",
    "static #yield = 0",
    "static #yield\n a",
    "static #async;",
    "static #async = 0;",
    "static #async",
    "static #async = 0",
    "static #async\n a(){}",  // a field named async, and a method named a.
    "static #async\n a",
    "static #await;",
    "static #await = 0;",
    "static #await\n a",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kSuccess, nullptr);
}

TEST_F(ParsingTest, PrivateStaticClassFieldsErrors) {
  // clang-format off
  // Tests proposed class fields syntax.
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* class_body_data[] = {
    // Basic syntax
    "static #['a'] = 0;",
    "static #['a'] = 0; b",
    "static #['a'] = 0; #b",
    "static #['a'] = 0; b(){}",
    "static #['a'] = 0; *b(){}",
    "static #['a'] = 0; ['b'](){}",
    "static #['a'];",
    "static #['a']; b;",
    "static #['a']; #b;",
    "static #['a']; b(){}",
    "static #['a']; *b(){}",
    "static #['a']; ['b'](){}",

    "static #0 = 0;",
    "static #0;",
    "static #'a' = 0;",
    "static #'a';",

    "static # a = 0",
    "static #get a() { }",
    "static #set a() { }",
    "static #*a() { }",
    "static async #*a() { }",

    "#a = arguments",
    "#a = () => arguments",
    "#a = () => { arguments }",
    "#a = arguments[0]",
    "#a = delete arguments[0]",
    "#a = f(arguments)",
    "#a = () => () => arguments",

    "#a; static #a",
    "static #a; #a",

    // ASI
    "static #['a'] = 0\n",
    "static #['a'] = 0\n b",
    "static #['a'] = 0\n #b",
    "static #['a'] = 0\n b(){}",
    "static #['a']\n",
    "static #['a']\n b\n",
    "static #['a']\n #b\n",
    "static #['a']\n b(){}",
    "static #['a']\n *b(){}",
    "static #['a']\n ['b'](){}",

    // ASI requires a linebreak
    "static #a b",
    "static #a = 0 b",

    // ASI requires that the next token is not part of any legal production
    "static #a = 0\n *b(){}",
    "static #a = 0\n ['b'](){}",

    "static #a : 0",
    "static #a =",
    "static #*a = 0",
    "static #*a",
    "static #get a",
    "static #yield a",
    "static #async a = 0",
    "static #async a",
    "static # a = 0",

    "#constructor",
    "#constructor = function() {}",

    "foo() { delete this.#a }",
    "foo() { delete this.x.#a }",
    "foo() { delete this.x().#a }",

    "foo() { delete f.#a }",
    "foo() { delete f.x.#a }",
    "foo() { delete f.x().#a }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, PrivateNameResolutionErrors) {
  // clang-format off
  const char* context_data[][2] = {
      {"class X { bar() { ", " } }"},
      {"\"use strict\";", ""},
      {nullptr, nullptr}
  };

  const char* statement_data[] = {
    "this.#a",
    "this.#a()",
    "this.#b.#a",
    "this.#b.#a()",

    "foo.#a",
    "foo.#a()",
    "foo.#b.#a",
    "foo.#b.#a()",

    "foo().#a",
    "foo().b.#a",
    "foo().b().#a",
    "foo().b().#a()",
    "foo().b().#a.bar",
    "foo().b().#a.bar()",

    "foo(this.#a)",
    "foo(bar().#a)",

    "new foo.#a",
    "new foo.#b.#a",
    "new foo.#b.#a()",

    "foo.#if;",
    "foo.#yield;",
    "foo.#super;",
    "foo.#interface;",
    "foo.#eval;",
    "foo.#arguments;",

    nullptr
  };

  // clang-format on
  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, PrivateNameErrors) {
  // clang-format off
  const char* context_data[][2] = {
      {"", ""},
      {"function t() { ", " }"},
      {"var t => { ", " }"},
      {"var t = { [ ", " ] }"},
      {"\"use strict\";", ""},
      {nullptr, nullptr}
  };

  const char* statement_data[] = {
    "#foo",
    "#foo = 1",

    "# a;",
    "#\n a;",
    "a, # b",
    "a, #, b;",

    "foo.#[a];",
    "foo.#['a'];",

    "foo()#a",
    "foo()#[a]",
    "foo()#['a']",

    "super.#a;",
    "super.#a = 1;",
    "super.#['a']",
    "super.#[a]",

    "new.#a",
    "new.#[a]",

    "foo.#{;",
    "foo.#};",
    "foo.#=;",
    "foo.#888;",
    "foo.#-;",
    "foo.#--;",
    nullptr
  };

  // clang-format on
  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, ClassExpressionErrors) {
  const char* context_data[][2] = {
      {"(", ");"}, {"var C = ", ";"}, {"bar, ", ";"}, {nullptr, nullptr}};
  const char* class_data[] = {
      "class",
      "class name",
      "class name extends",
      "class extends",
      "class {",
      "class { m: 1 }",
      "class { m(); n() }",
      "class { get m }",
      "class { get m() }",
      "class { get m() { }",
      "class { set m() {} }",      // Missing required parameter.
      "class { m() {}, n() {} }",  // No commas allowed.
      nullptr};

  RunParserSyncTest(context_data, class_data, kError);
}

TEST_F(ParsingTest, ClassDeclarationErrors) {
  const char* context_data[][2] = {
      {"", ""}, {"{", "}"}, {"if (true) {", "}"}, {nullptr, nullptr}};
  const char* class_data[] = {
      "class",
      "class name",
      "class name extends",
      "class extends",
      "class name {",
      "class name { m: 1 }",
      "class name { m(); n() }",
      "class name { get x }",
      "class name { get x() }",
      "class name { set x() {) }",  // missing required param
      "class {}",                   // Name is required for declaration
      "class extends base {}",
      "class name { *",
      "class name { * }",
      "class name { *; }",
      "class name { *get x() {} }",
      "class name { *set x(_) {} }",
      "class name { *static m() {} }",
      nullptr};

  RunParserSyncTest(context_data, class_data, kError);
}

TEST_F(ParsingTest, ClassAsyncErrors) {
  // clang-format off
  const char* context_data[][2] = {{"(class {", "});"},
                                   {"(class extends Base {", "});"},
                                   {"class C {", "}"},
                                   {"class C extends Base {", "}"},
                                   {nullptr, nullptr}};
  const char* async_data[] = {
    "*async x(){}",
    "async *(){}",
    "async get x(){}",
    "async set x(y){}",
    "async x : 0",
    "async : 0",

    "async static x(){}",

    "static *async x(){}",
    "static async *(){}",
    "static async get x(){}",
    "static async set x(y){}",
    "static async x : 0",
    "static async : 0",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, async_data, kError);
}

TEST_F(ParsingTest, ClassNameErrors) {
  const char* context_data[][2] = {{"class ", "{}"},
                                   {"(class ", "{});"},
                                   {"'use strict'; class ", "{}"},
                                   {"'use strict'; (class ", "{});"},
                                   {nullptr, nullptr}};
  const char* class_name[] = {"arguments", "eval",    "implements", "interface",
                              "let",       "package", "private",    "protected",
                              "public",    "static",  "var",        "yield",
                              nullptr};

  RunParserSyncTest(context_data, class_name, kError);
}

TEST_F(ParsingTest, ClassGetterParamNameErrors) {
  const char* context_data[][2] = {
      {"class C { get name(", ") {} }"},
      {"(class { get name(", ") {} });"},
      {"'use strict'; class C { get name(", ") {} }"},
      {"'use strict'; (class { get name(", ") {} })"},
      {nullptr, nullptr}};

  const char* class_name[] = {"arguments", "eval",    "implements", "interface",
                              "let",       "package", "private",    "protected",
                              "public",    "static",  "var",        "yield",
                              nullptr};

  RunParserSyncTest(context_data, class_name, kError);
}

TEST_F(ParsingTest, ClassStaticPrototypeErrors) {
  const char* context_data[][2] = {
      {"class C {", "}"}, {"(class {", "});"}, {nullptr, nullptr}};

  const char* class_body_data[] = {"static prototype() {}",
                                   "static get prototype() {}",
                                   "static set prototype(_) {}",
                                   "static *prototype() {}",
                                   "static 'prototype'() {}",
                                   "static *'prototype'() {}",
                                   "static prot\\u006ftype() {}",
                                   "static 'prot\\u006ftype'() {}",
                                   "static get 'prot\\u006ftype'() {}",
                                   "static set 'prot\\u006ftype'(_) {}",
                                   "static *'prot\\u006ftype'() {}",
                                   nullptr};

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, ClassSpecialConstructorErrors) {
  const char* context_data[][2] = {
      {"class C {", "}"}, {"(class {", "});"}, {nullptr, nullptr}};

  const char* class_body_data[] = {"get constructor() {}",
                                   "get constructor(_) {}",
                                   "*constructor() {}",
                                   "get 'constructor'() {}",
                                   "*'constructor'() {}",
                                   "get c\\u006fnstructor() {}",
                                   "*c\\u006fnstructor() {}",
                                   "get 'c\\u006fnstructor'() {}",
                                   "get 'c\\u006fnstructor'(_) {}",
                                   "*'c\\u006fnstructor'() {}",
                                   nullptr};

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, ClassConstructorNoErrors) {
  const char* context_data[][2] = {
      {"class C {", "}"}, {"(class {", "});"}, {nullptr, nullptr}};

  const char* class_body_data[] = {"constructor() {}",
                                   "static constructor() {}",
                                   "static get constructor() {}",
                                   "static set constructor(_) {}",
                                   "static *constructor() {}",
                                   nullptr};

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, ClassMultipleConstructorErrors) {
  const char* context_data[][2] = {
      {"class C {", "}"}, {"(class {", "});"}, {nullptr, nullptr}};

  const char* class_body_data[] = {"constructor() {}; constructor() {}",
                                   nullptr};

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, ClassMultiplePropertyNamesNoErrors) {
  const char* context_data[][2] = {
      {"class C {", "}"}, {"(class {", "});"}, {nullptr, nullptr}};

  const char* class_body_data[] = {
      "constructor() {}; static constructor() {}",
      "m() {}; static m() {}",
      "m() {}; m() {}",
      "static m() {}; static m() {}",
      "get m() {}; set m(_) {}; get m() {}; set m(_) {};",
      nullptr};

  RunParserSyncTest(context_data, class_body_data, kSuccess);
}

TEST_F(ParsingTest, ClassesAreStrictErrors) {
  const char* context_data[][2] = {{"", ""}, {"(", ");"}, {nullptr, nullptr}};

  const char* class_body_data[] = {
      "class C { method() { with ({}) {} } }",
      "class C extends function() { with ({}) {} } {}",
      "class C { *method() { with ({}) {} } }", nullptr};

  RunParserSyncTest(context_data, class_body_data, kError);
}

TEST_F(ParsingTest, ObjectLiteralPropertyShorthandKeywordsError) {
  const char* context_data[][2] = {
      {"({", "});"}, {"'use strict'; ({", "});"}, {nullptr, nullptr}};

  const char* name_data[] = {
      "break",    "case",    "catch",  "class",      "const", "continue",
      "debugger", "default", "delete", "do",         "else",  "enum",
      "export",   "extends", "false",  "finally",    "for",   "function",
      "if",       "import",  "in",     "instanceof", "new",   "null",
      "return",   "super",   "switch", "this",       "throw", "true",
      "try",      "typeof",  "var",    "void",       "while", "with",
      nullptr};

  RunParserSyncTest(context_data, name_data, kError);
}

TEST_F(ParsingTest, ObjectLiteralPropertyShorthandStrictKeywords) {
  const char* context_data[][2] = {{"({", "});"}, {nullptr, nullptr}};

  const char* name_data[] = {"implements", "interface", "let",    "package",
                             "private",    "protected", "public", "static",
                             "yield",      nullptr};

  RunParserSyncTest(context_data, name_data, kSuccess);

  const char* context_strict_data[][2] = {{"'use strict'; ({", "});"},
                                          {nullptr, nullptr}};
  RunParserSyncTest(context_strict_data, name_data, kError);
}

TEST_F(ParsingTest, ObjectLiteralPropertyShorthandError) {
  const char* context_data[][2] = {
      {"({", "});"}, {"'use strict'; ({", "});"}, {nullptr, nullptr}};

  const char* name_data[] = {"1",   "1.2", "0",     "0.1", "1.0",
                             "1e1", "0x1", "\"s\"", "'s'", nullptr};

  RunParserSyncTest(context_data, name_data, kError);
}

TEST_F(ParsingTest, ObjectLiteralPropertyShorthandYieldInGeneratorError) {
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  const char* name_data[] = {"function* g() { ({yield}); }", nullptr};

  RunParserSyncTest(context_data, name_data, kError);
}

TEST_F(ParsingTest, ConstParsingInForIn) {
  const char* context_data[][2] = {{"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {
      "for(const x = 1; ; ) {}", "for(const x = 1, y = 2;;){}",
      "for(const x in [1,2,3]) {}", "for(const x of [1,2,3]) {}", nullptr};
  RunParserSyncTest(context_data, data, kSuccess, nullptr, 0, nullptr, 0);
}

TEST_F(ParsingTest, StatementParsingInForIn) {
  const char* context_data[][2] = {{"", ""},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"for(x in {}, {}) {}", "for(var x in {}, {}) {}",
                        "for(let x in {}, {}) {}", "for(const x in {}, {}) {}",
                        nullptr};

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ConstParsingInForInError) {
  const char* context_data[][2] = {{"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {
      "for(const x,y = 1; ; ) {}",         "for(const x = 4 in [1,2,3]) {}",
      "for(const x = 4, y in [1,2,3]) {}", "for(const x = 4 of [1,2,3]) {}",
      "for(const x = 4, y of [1,2,3]) {}", "for(const x = 1, y = 2 in []) {}",
      "for(const x,y in []) {}",           "for(const x = 1, y = 2 of []) {}",
      "for(const x,y of []) {}",           nullptr};
  RunParserSyncTest(context_data, data, kError, nullptr, 0, nullptr, 0);
}

TEST_F(ParsingTest, InitializedDeclarationsInForInOf) {
  // https://tc39.github.io/ecma262/#sec-initializers-in-forin-statement-heads

  // Initialized declarations only allowed for
  // - sloppy mode (not strict mode)
  // - for-in (not for-of)
  // - var (not let / const)

  // clang-format off
  const char* strict_context[][2] = {{"'use strict';", ""},
                                     {"function foo(){ 'use strict';", "}"},
                                     {"function* foo(){ 'use strict';", "}"},
                                     {nullptr, nullptr}};

  const char* sloppy_context[][2] = {{"", ""},
                                     {"function foo(){ ", "}"},
                                     {"function* foo(){ ", "}"},
                                     {"function foo(){ var yield = 0; ", "}"},
                                     {nullptr, nullptr}};

  const char* let_const_var_for_of[] = {
      "for (let i = 1 of {}) {}",
      "for (let i = void 0 of [1, 2, 3]) {}",
      "for (const i = 1 of {}) {}",
      "for (const i = void 0 of [1, 2, 3]) {}",
      "for (var i = 1 of {}) {}",
      "for (var i = void 0 of [1, 2, 3]) {}",
      nullptr};

  const char* let_const_for_in[] = {
      "for (let i = 1 in {}) {}",
      "for (let i = void 0 in [1, 2, 3]) {}",
      "for (const i = 1 in {}) {}",
      "for (const i = void 0 in [1, 2, 3]) {}",
      nullptr};

  const char* var_for_in[] = {
      "for (var i = 1 in {}) {}",
      "for (var i = void 0 in [1, 2, 3]) {}",
      "for (var i = yield in [1, 2, 3]) {}",
      nullptr};
  // clang-format on

  // The only allowed case is sloppy + var + for-in.
  RunParserSyncTest(sloppy_context, var_for_in, kSuccess);

  // Everything else is disallowed.
  RunParserSyncTest(sloppy_context, let_const_var_for_of, kError);
  RunParserSyncTest(sloppy_context, let_const_for_in, kError);

  RunParserSyncTest(strict_context, let_const_var_for_of, kError);
  RunParserSyncTest(strict_context, let_const_for_in, kError);
  RunParserSyncTest(strict_context, var_for_in, kError);
}

TEST_F(ParsingTest, ForInMultipleDeclarationsError) {
  const char* context_data[][2] = {{"", ""},
                                   {"function foo(){", "}"},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"for (var i, j in {}) {}",
                        "for (var i, j in [1, 2, 3]) {}",
                        "for (var i, j = 1 in {}) {}",
                        "for (var i, j = void 0 in [1, 2, 3]) {}",

                        "for (let i, j in {}) {}",
                        "for (let i, j in [1, 2, 3]) {}",
                        "for (let i, j = 1 in {}) {}",
                        "for (let i, j = void 0 in [1, 2, 3]) {}",

                        "for (const i, j in {}) {}",
                        "for (const i, j in [1, 2, 3]) {}",
                        "for (const i, j = 1 in {}) {}",
                        "for (const i, j = void 0 in [1, 2, 3]) {}",
                        nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ForOfMultipleDeclarationsError) {
  const char* context_data[][2] = {{"", ""},
                                   {"function foo(){", "}"},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"for (var i, j of {}) {}",
                        "for (var i, j of [1, 2, 3]) {}",
                        "for (var i, j = 1 of {}) {}",
                        "for (var i, j = void 0 of [1, 2, 3]) {}",

                        "for (let i, j of {}) {}",
                        "for (let i, j of [1, 2, 3]) {}",
                        "for (let i, j = 1 of {}) {}",
                        "for (let i, j = void 0 of [1, 2, 3]) {}",

                        "for (const i, j of {}) {}",
                        "for (const i, j of [1, 2, 3]) {}",
                        "for (const i, j = 1 of {}) {}",
                        "for (const i, j = void 0 of [1, 2, 3]) {}",
                        nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ForInOfLetExpression) {
  const char* sloppy_context_data[][2] = {
      {"", ""}, {"function foo(){", "}"}, {nullptr, nullptr}};

  const char* strict_context_data[][2] = {
      {"'use strict';", ""},
      {"function foo(){ 'use strict';", "}"},
      {nullptr, nullptr}};

  const char* async_context_data[][2] = {
      {"async function foo(){", "}"},
      {"async function foo(){ 'use strict';", "}"},
      {nullptr, nullptr}};

  const char* for_let_in[] = {"for (let.x in {}) {}", nullptr};

  const char* for_let_of[] = {"for (let.x of []) {}", nullptr};

  const char* for_await_let_of[] = {"for await (let.x of []) {}", nullptr};

  // The only place `let.x` is legal as a left-hand side expression
  // is in sloppy mode in a for-in loop.
  RunParserSyncTest(sloppy_context_data, for_let_in, kSuccess);
  RunParserSyncTest(strict_context_data, for_let_in, kError);
  RunParserSyncTest(sloppy_context_data, for_let_of, kError);
  RunParserSyncTest(strict_context_data, for_let_of, kError);
  RunParserSyncTest(async_context_data, for_await_let_of, kError);
}

TEST_F(ParsingTest, ForInNoDeclarationsError) {
  const char* context_data[][2] = {{"", ""},
                                   {"function foo(){", "}"},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"for (var in {}) {}", "for (const in {}) {}", nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ForOfNoDeclarationsError) {
  const char* context_data[][2] = {{"", ""},
                                   {"function foo(){", "}"},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"for (var of [1, 2, 3]) {}",
                        "for (const of [1, 2, 3]) {}", nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ForOfInOperator) {
  const char* context_data[][2] = {{"", ""},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"for(x of 'foo' in {}) {}",
                        "for(var x of 'foo' in {}) {}",
                        "for(let x of 'foo' in {}) {}",
                        "for(const x of 'foo' in {}) {}", nullptr};

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ForOfYieldIdentifier) {
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  const char* data[] = {"for(x of yield) {}", "for(var x of yield) {}",
                        "for(let x of yield) {}", "for(const x of yield) {}",
                        nullptr};

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ForOfYieldExpression) {
  const char* context_data[][2] = {{"", ""},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"function* g() { for(x of yield) {} }",
                        "function* g() { for(var x of yield) {} }",
                        "function* g() { for(let x of yield) {} }",
                        "function* g() { for(const x of yield) {} }", nullptr};

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ForOfExpressionError) {
  const char* context_data[][2] = {{"", ""},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {
      "for(x of [], []) {}", "for(var x of [], []) {}",
      "for(let x of [], []) {}", "for(const x of [], []) {}",

      // AssignmentExpression should be validated statically:
      "for(x of { y = 23 }) {}", "for(var x of { y = 23 }) {}",
      "for(let x of { y = 23 }) {}", "for(const x of { y = 23 }) {}", nullptr};

  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ForOfAsync) {
  const char* context_data[][2] = {{"", ""},
                                   {"'use strict';", ""},
                                   {"function foo(){ 'use strict';", "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"for(\\u0061sync of []) {}", nullptr};

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, InvalidUnicodeEscapes) {
  const char* context_data[][2] = {
      {"", ""}, {"'use strict';", ""}, {nullptr, nullptr}};
  const char* data[] = {
      "var foob\\u123r = 0;", "var \\u123roo = 0;", "\"foob\\u123rr\"",
      // No escapes allowed in regexp flags
      "/regex/\\u0069g", "/regex/\\u006g",
      // Braces gone wrong
      "var foob\\u{c481r = 0;", "var foob\\uc481}r = 0;", "var \\u{0052oo = 0;",
      "var \\u0052}oo = 0;", "\"foob\\u{c481r\"", "var foob\\u{}ar = 0;",
      // Too high value for the Unicode code point escape
      "\"\\u{110000}\"",
      // Not a Unicode code point escape
      "var foob\\v1234r = 0;", "var foob\\U1234r = 0;",
      "var foob\\v{1234}r = 0;", "var foob\\U{1234}r = 0;", nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, UnicodeEscapes) {
  const char* context_data[][2] = {
      {"", ""}, {"'use strict';", ""}, {nullptr, nullptr}};
  const char* data[] = {
      // Identifier starting with escape
      "var \\u0052oo = 0;", "var \\u{0052}oo = 0;", "var \\u{52}oo = 0;",
      "var \\u{00000000052}oo = 0;",
      // Identifier with an escape but not starting with an escape
      "var foob\\uc481r = 0;", "var foob\\u{c481}r = 0;",
      // String with an escape
      "\"foob\\uc481r\"", "\"foob\\{uc481}r\"",
      // This character is a valid Unicode character, representable as a
      // surrogate pair, not representable as 4 hex digits.
      "\"foo\\u{10e6d}\"",
      // Max value for the Unicode code point escape
      "\"\\u{10ffff}\"", nullptr};
  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, OctalEscapes) {
  const char* sloppy_context_data[][2] = {{"", ""},    // as a directive
                                          {"0;", ""},  // as a string literal
                                          {nullptr, nullptr}};

  const char* strict_context_data[][2] = {
      {"'use strict';", ""},     // as a directive before 'use strict'
      {"", ";'use strict';"},    // as a directive after 'use strict'
      {"'use strict'; 0;", ""},  // as a string literal
      {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "'\\1'",
    "'\\01'",
    "'\\001'",
    "'\\08'",
    "'\\09'",
    nullptr};
  // clang-format on

  // Permitted in sloppy mode
  RunParserSyncTest(sloppy_context_data, data, kSuccess);

  // Error in strict mode
  RunParserSyncTest(strict_context_data, data, kError);
}

TEST_F(ParsingTest, ScanTemplateLiterals) {
  const char* context_data[][2] = {{"'use strict';", ""},
                                   {"function foo(){ 'use strict';"
                                    "  var a, b, c; return ",
                                    "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"``",
                        "`no-subst-template`",
                        "`template-head${a}`",
                        "`${a}`",
                        "`${a}template-tail`",
                        "`template-head${a}template-tail`",
                        "`${a}${b}${c}`",
                        "`a${a}b${b}c${c}`",
                        "`${a}a${b}b${c}c`",
                        "`foo\n\nbar\r\nbaz`",
                        "`foo\n\n${  bar  }\r\nbaz`",
                        "`foo${a /* comment */}`",
                        "`foo${a // comment\n}`",
                        "`foo${a \n}`",
                        "`foo${a \r\n}`",
                        "`foo${a \r}`",
                        "`foo${/* comment */ a}`",
                        "`foo${// comment\na}`",
                        "`foo${\n a}`",
                        "`foo${\r\n a}`",
                        "`foo${\r a}`",
                        "`foo${'a' in a}`",
                        nullptr};
  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ScanTaggedTemplateLiterals) {
  const char* context_data[][2] = {{"'use strict';", ""},
                                   {"function foo(){ 'use strict';"
                                    "  function tag() {}"
                                    "  var a, b, c; return ",
                                    "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"tag ``",
                        "tag `no-subst-template`",
                        "tag`template-head${a}`",
                        "tag `${a}`",
                        "tag `${a}template-tail`",
                        "tag   `template-head${a}template-tail`",
                        "tag\n`${a}${b}${c}`",
                        "tag\r\n`a${a}b${b}c${c}`",
                        "tag    `${a}a${b}b${c}c`",
                        "tag\t`foo\n\nbar\r\nbaz`",
                        "tag\r`foo\n\n${  bar  }\r\nbaz`",
                        "tag`foo${a /* comment */}`",
                        "tag`foo${a // comment\n}`",
                        "tag`foo${a \n}`",
                        "tag`foo${a \r\n}`",
                        "tag`foo${a \r}`",
                        "tag`foo${/* comment */ a}`",
                        "tag`foo${// comment\na}`",
                        "tag`foo${\n a}`",
                        "tag`foo${\r\n a}`",
                        "tag`foo${\r a}`",
                        "tag`foo${'a' in a}`",
                        nullptr};
  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, TemplateMaterializedLiterals) {
  const char* context_data[][2] = {{"'use strict';\n"
                                    "function tag() {}\n"
                                    "var a, b, c;\n"
                                    "(",
                                    ")"},
                                   {nullptr, nullptr}};

  const char* data[] = {"tag``", "tag`a`", "tag`a${1}b`", "tag`a${1}b${2}c`",
                        "``",    "`a`",    "`a${1}b`",    "`a${1}b${2}c`",
                        nullptr};

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ScanUnterminatedTemplateLiterals) {
  const char* context_data[][2] = {{"'use strict';", ""},
                                   {"function foo(){ 'use strict';"
                                    "  var a, b, c; return ",
                                    "}"},
                                   {nullptr, nullptr}};

  const char* data[] = {"`no-subst-template",
                        "`template-head${a}",
                        "`${a}template-tail",
                        "`template-head${a}template-tail",
                        "`${a}${b}${c}",
                        "`a${a}b${b}c${c}",
                        "`${a}a${b}b${c}c",
                        "`foo\n\nbar\r\nbaz",
                        "`foo\n\n${  bar  }\r\nbaz",
                        "`foo${a /* comment } */`",
                        "`foo${a /* comment } `*/",
                        "`foo${a // comment}`",
                        "`foo${a \n`",
                        "`foo${a \r\n`",
                        "`foo${a \r`",
                        "`foo${/* comment */ a`",
                        "`foo${// commenta}`",
                        "`foo${\n a`",
                        "`foo${\r\n a`",
                        "`foo${\r a`",
                        "`foo${fn(}`",
                        "`foo${1 if}`",
                        nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, TemplateLiteralsIllegalTokens) {
  const char* context_data[][2] = {{"'use strict';", ""},
                                   {"function foo(){ 'use strict';"
                                    "  var a, b, c; return ",
                                    "}"},
                                   {nullptr, nullptr}};
  const char* data[] = {
      "`hello\\x`",         "`hello\\x${1}`",       "`hello${1}\\x`",
      "`hello${1}\\x${2}`", "`hello\\x\n`",         "`hello\\x\n${1}`",
      "`hello${1}\\x\n`",   "`hello${1}\\x\n${2}`", nullptr};

  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ParseRestParameters) {
  const char* context_data[][2] = {{"'use strict';(function(",
                                    "){ return args;})(1, [], /regexp/, 'str',"
                                    "function(){});"},
                                   {"(function(",
                                    "){ return args;})(1, [],"
                                    "/regexp/, 'str', function(){});"},
                                   {nullptr, nullptr}};

  const char* data[] = {"...args",
                        "a, ...args",
                        "...   args",
                        "a, ...   args",
                        "...\targs",
                        "a, ...\targs",
                        "...\r\nargs",
                        "a, ...\r\nargs",
                        "...\rargs",
                        "a, ...\rargs",
                        "...\t\n\t\t\n  args",
                        "a, ...  \n  \n  args",
                        "...{ length, 0: a, 1: b}",
                        "...{}",
                        "...[a, b]",
                        "...[]",
                        "...[...[a, b, ...c]]",
                        nullptr};
  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ParseRestParametersErrors) {
  const char* context_data[][2] = {{"'use strict';(function(",
                                    "){ return args;}(1, [], /regexp/, 'str',"
                                    "function(){});"},
                                   {"(function(",
                                    "){ return args;}(1, [],"
                                    "/regexp/, 'str', function(){});"},
                                   {nullptr, nullptr}};

  const char* data[] = {"...args, b",
                        "a, ...args, b",
                        "...args,   b",
                        "a, ...args,   b",
                        "...args,\tb",
                        "a,...args\t,b",
                        "...args\r\n, b",
                        "a, ... args,\r\nb",
                        "...args\r,b",
                        "a, ... args,\rb",
                        "...args\t\n\t\t\n,  b",
                        "a, ... args,  \n  \n  b",
                        "a, a, ...args",
                        "a,\ta, ...args",
                        "a,\ra, ...args",
                        "a,\na, ...args",
                        nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, RestParameterInSetterMethodError) {
  const char* context_data[][2] = {
      {"'use strict';({ set prop(", ") {} }).prop = 1;"},
      {"'use strict';(class { static set prop(", ") {} }).prop = 1;"},
      {"'use strict';(new (class { set prop(", ") {} })).prop = 1;"},
      {"({ set prop(", ") {} }).prop = 1;"},
      {"(class { static set prop(", ") {} }).prop = 1;"},
      {"(new (class { set prop(", ") {} })).prop = 1;"},
      {nullptr, nullptr}};
  const char* data[] = {"...a", "...arguments", "...eval", nullptr};

  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, RestParametersEvalArguments) {
  // clang-format off
  const char* strict_context_data[][2] =
      {{"'use strict';(function(",
        "){ return;})(1, [], /regexp/, 'str',function(){});"},
       {nullptr, nullptr}};
  const char* sloppy_context_data[][2] =
      {{"(function(",
        "){ return;})(1, [],/regexp/, 'str', function(){});"},
       {nullptr, nullptr}};

  const char* data[] = {
      "...eval",
      "eval, ...args",
      "...arguments",
      // See https://bugs.chromium.org/p/v8/issues/detail?id=4577
      // "arguments, ...args",
      nullptr};
  // clang-format on

  // Fail in strict mode
  RunParserSyncTest(strict_context_data, data, kError);

  // OK in sloppy mode
  RunParserSyncTest(sloppy_context_data, data, kSuccess);
}

TEST_F(ParsingTest, RestParametersDuplicateEvalArguments) {
  const char* context_data[][2] = {
      {"'use strict';(function(",
       "){ return;})(1, [], /regexp/, 'str',function(){});"},
      {"(function(", "){ return;})(1, [],/regexp/, 'str', function(){});"},
      {nullptr, nullptr}};

  const char* data[] = {"eval, ...eval", "eval, eval, ...args",
                        "arguments, ...arguments",
                        "arguments, arguments, ...args", nullptr};

  // In strict mode, the error is using "eval" or "arguments" as parameter names
  // In sloppy mode, the error is that eval / arguments are duplicated
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, SpreadCall) {
  const char* context_data[][2] = {{"function fn() { 'use strict';} fn(", ");"},
                                   {"function fn() {} fn(", ");"},
                                   {nullptr, nullptr}};

  const char* data[] = {"...([1, 2, 3])",
                        "...'123', ...'456'",
                        "...new Set([1, 2, 3]), 4",
                        "1, ...[2, 3], 4",
                        "...Array(...[1,2,3,4])",
                        "...NaN",
                        "0, 1, ...[2, 3, 4], 5, 6, 7, ...'89'",
                        "0, 1, ...[2, 3, 4], 5, 6, 7, ...'89', 10",
                        "...[0, 1, 2], 3, 4, 5, 6, ...'7', 8, 9",
                        "...[0, 1, 2], 3, 4, 5, 6, ...'7', 8, 9, ...[10]",
                        nullptr};

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, SpreadCallErrors) {
  const char* context_data[][2] = {{"function fn() { 'use strict';} fn(", ");"},
                                   {"function fn() {} fn(", ");"},
                                   {nullptr, nullptr}};

  const char* data[] = {"(...[1, 2, 3])", "......[1,2,3]", nullptr};

  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, BadRestSpread) {
  const char* context_data[][2] = {{"function fn() { 'use strict';", "} fn();"},
                                   {"function fn() { ", "} fn();"},
                                   {nullptr, nullptr}};
  const char* data[] = {
      "return ...[1,2,3];",          "var ...x = [1,2,3];",
      "var [...x,] = [1,2,3];",      "var [...x, y] = [1,2,3];",
      "var { x } = {x: ...[1,2,3]}", nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, LexicalScopingSloppyMode) {
  const char* context_data[][2] = {
      {"", ""}, {"function f() {", "}"}, {"{", "}"}, {nullptr, nullptr}};

  const char* good_data[] = {"let = 1;", "for(let = 1;;){}", nullptr};
  RunParserSyncTest(context_data, good_data, kSuccess);
}

TEST_F(ParsingTest, ComputedPropertyName) {
  const char* context_data[][2] = {{"({[", "]: 1});"},
                                   {"({get [", "]() {}});"},
                                   {"({set [", "](_) {}});"},
                                   {"({[", "]() {}});"},
                                   {"({*[", "]() {}});"},
                                   {"(class {get [", "]() {}});"},
                                   {"(class {set [", "](_) {}});"},
                                   {"(class {[", "]() {}});"},
                                   {"(class {*[", "]() {}});"},
                                   {nullptr, nullptr}};
  const char* error_data[] = {"1, 2", "var name", nullptr};

  RunParserSyncTest(context_data, error_data, kError);

  const char* name_data[] = {"1",  "1 + 2", "'name'", "\"name\"",
                             "[]", "{}",    nullptr};

  RunParserSyncTest(context_data, name_data, kSuccess);
}

TEST_F(ParsingTest, ComputedPropertyNameShorthandError) {
  const char* context_data[][2] = {{"({", "});"}, {nullptr, nullptr}};
  const char* error_data[] = {"a: 1, [2]", "[1], a: 1", nullptr};

  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, BasicImportExportParsing) {
  // clang-format off
  const char* kSources[] = {
      "export let x = 0;",
      "export var y = 0;",
      "export const z = 0;",
      "export function func() { };",
      "export class C { };",
      "export { };",
      "function f() {}; f(); export { f };",
      "var a, b, c; export { a, b as baz, c };",
      "var d, e; export { d as dreary, e, };",
      "export default function f() {}",
      "export default function() {}",
      "export default function*() {}",
      "export default class C {}",
      "export default class {}",
      "export default class extends C {}",
      "export default 42",
      "var x; export default x = 7",
      "export { Q } from 'somemodule.js';",
      "export * from 'somemodule.js';",
      "var foo; export { foo as for };",
      "export { arguments } from 'm.js';",
      "export { for } from 'm.js';",
      "export { yield } from 'm.js'",
      "export { static } from 'm.js'",
      "export { let } from 'm.js'",
      "var a; export { a as b, a as c };",
      "var a; export { a as await };",
      "var a; export { a as enum };",

      "import 'somemodule.js';",
      "import { } from 'm.js';",
      "import { a } from 'm.js';",
      "import { a, b as d, c, } from 'm.js';",
      "import * as thing from 'm.js';",
      "import thing from 'm.js';",
      "import thing, * as rest from 'm.js';",
      "import thing, { a, b, c } from 'm.js';",
      "import { arguments as a } from 'm.js';",
      "import { for as f } from 'm.js';",
      "import { yield as y } from 'm.js';",
      "import { static as s } from 'm.js';",
      "import { let as l } from 'm.js';",

      "import thing from 'a.js'; export {thing};",
      "export {thing}; import thing from 'a.js';",
      "import {thing} from 'a.js'; export {thing};",
      "export {thing}; import {thing} from 'a.js';",
      "import * as thing from 'a.js'; export {thing};",
      "export {thing}; import * as thing from 'a.js';",
  };
  // clang-format on

  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kSources[i]);

    // Show that parsing as a module works
    {
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      flags.set_is_module(true);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK_PARSE_PROGRAM(&info, script, isolate);
    }

    // And that parsing a script does not.
    {
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
      CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                      parsing::ReportStatisticsMode::kYes));
      CHECK(info.pending_error_handler()->has_pending_error());
    }
  }
}

TEST_F(ParsingTest, NamespaceExportParsing) {
  // clang-format off
  const char* kSources[] = {
      "export * as arguments from 'bar'",
      "export * as await from 'bar'",
      "export * as default from 'bar'",
      "export * as enum from 'bar'",
      "export * as foo from 'bar'",
      "export * as for from 'bar'",
      "export * as let from 'bar'",
      "export * as static from 'bar'",
      "export * as yield from 'bar'",
  };
  // clang-format on

  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kSources[i]);
    i::Handle<i::Script> script = factory->NewScript(source);
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_is_module(true);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK_PARSE_PROGRAM(&info, script, isolate);
  }
}

TEST_F(ParsingTest, ImportExportParsingErrors) {
  // clang-format off
  const char* kErrorSources[] = {
      "export {",
      "var a; export { a",
      "var a; export { a,",
      "var a; export { a, ;",
      "var a; export { a as };",
      "var a, b; export { a as , b};",
      "export }",
      "var foo, bar; export { foo bar };",
      "export { foo };",
      "export { , };",
      "export default;",
      "export default var x = 7;",
      "export default let x = 7;",
      "export default const x = 7;",
      "export *;",
      "export * from;",
      "export { Q } from;",
      "export default from 'module.js';",
      "export { for }",
      "export { for as foo }",
      "export { arguments }",
      "export { arguments as foo }",
      "var a; export { a, a };",
      "var a, b; export { a as b, b };",
      "var a, b; export { a as c, b as c };",
      "export default function f(){}; export default class C {};",
      "export default function f(){}; var a; export { a as default };",
      "export function() {}",
      "export function*() {}",
      "export class {}",
      "export class extends C {}",

      "import from;",
      "import from 'm.js';",
      "import { };",
      "import {;",
      "import };",
      "import { , };",
      "import { , } from 'm.js';",
      "import { a } from;",
      "import { a } 'm.js';",
      "import , from 'm.js';",
      "import a , from 'm.js';",
      "import a { b, c } from 'm.js';",
      "import arguments from 'm.js';",
      "import eval from 'm.js';",
      "import { arguments } from 'm.js';",
      "import { eval } from 'm.js';",
      "import { a as arguments } from 'm.js';",
      "import { for } from 'm.js';",
      "import { y as yield } from 'm.js'",
      "import { s as static } from 'm.js'",
      "import { l as let } from 'm.js'",
      "import { a as await } from 'm.js';",
      "import { a as enum } from 'm.js';",
      "import { x }, def from 'm.js';",
      "import def, def2 from 'm.js';",
      "import * as x, def from 'm.js';",
      "import * as x, * as y from 'm.js';",
      "import {x}, {y} from 'm.js';",
      "import * as x, {y} from 'm.js';",

      "export *;",
      "export * as;",
      "export * as foo;",
      "export * as foo from;",
      "export * as foo from ';",
      "export * as ,foo from 'bar'",
  };
  // clang-format on

  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kErrorSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kErrorSources[i]);

    i::Handle<i::Script> script = factory->NewScript(source);
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_is_module(true);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                    parsing::ReportStatisticsMode::kYes));
    CHECK(info.pending_error_handler()->has_pending_error());
  }
}

TEST_F(ParsingTest, ModuleTopLevelFunctionDecl) {
  // clang-format off
  const char* kErrorSources[] = {
      "function f() {} function f() {}",
      "var f; function f() {}",
      "function f() {} var f;",
      "function* f() {} function* f() {}",
      "var f; function* f() {}",
      "function* f() {} var f;",
      "function f() {} function* f() {}",
      "function* f() {} function f() {}",
  };
  // clang-format on

  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  for (unsigned i = 0; i < arraysize(kErrorSources); ++i) {
    i::Handle<i::String> source =
        factory->NewStringFromAsciiChecked(kErrorSources[i]);

    i::Handle<i::Script> script = factory->NewScript(source);
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_is_module(true);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK(!i::parsing::ParseProgram(&info, script, isolate,
                                    parsing::ReportStatisticsMode::kYes));
    CHECK(info.pending_error_handler()->has_pending_error());
  }
}

TEST_F(ParsingTest, ModuleAwaitReserved) {
  // clang-format off
  const char* kErrorSources[] = {
      "await;",
      "await: ;",
      "var await;",
      "var [await] = [];",
      "var { await } = {};",
      "var { x: await } = {};",
      "{ var await; }",
      "let await;",
      "let [await] = [];",
      "let { await } = {};",
      "let { x: await } = {};",
      "{ let await; }",
      "const await = null;",
      "const [await] = [];",
      "const { await } = {};",
      "const { x: await } = {};",
      "{ const await = null; }",
      "function await() {}",
      "function f(await) {}",
      "function* await() {}",
      "function* g(await) {}",
      "(function await() {});",
      "(function (await) {});",
      "(function* await() {});",
      "(function* (await) {});",
      "(await) => {};",
      "await => {};",
      "class await {}",
      "class C { constructor(await) {} }",
      "class C { m(await) {} }",
      "class C { static m(await) {} }",
      "class C { *m(await) {} }",
      "class C { static *m(await) {} }",
      "(class await {})",
      "(class { constructor(await) {} });",
      "(class { m(await) {} });",
      "(class { static m(await) {} });",
      "(class { *m(await) {} });",
      "(class { static *m(await) {} });",
      "({ m(await) {} });",
      "({ *m(await) {} });",
      "({ set p(await) {} });",
      "try {} catch (await) {}",
      "try {} catch (await) {} finally {}",
      nullptr
  };
  // clang-format on
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  RunModuleParserSyncTest(context_data, kErrorSources, kError);
}

TEST_F(ParsingTest, ModuleAwaitReservedPreParse) {
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};
  const char* error_data[] = {"function f() { var await = 0; }", nullptr};

  RunModuleParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, ModuleAwaitPermitted) {
  // clang-format off
  const char* kValidSources[] = {
    "({}).await;",
    "({ await: null });",
    "({ await() {} });",
    "({ get await() {} });",
    "({ set await(x) {} });",
    "(class { await() {} });",
    "(class { static await() {} });",
    "(class { *await() {} });",
    "(class { static *await() {} });",
    nullptr
  };
  // clang-format on
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  RunModuleParserSyncTest(context_data, kValidSources, kSuccess);
}

TEST_F(ParsingTest, EnumReserved) {
  // clang-format off
  const char* kErrorSources[] = {
      "enum;",
      "enum: ;",
      "var enum;",
      "var [enum] = [];",
      "var { enum } = {};",
      "var { x: enum } = {};",
      "{ var enum; }",
      "let enum;",
      "let [enum] = [];",
      "let { enum } = {};",
      "let { x: enum } = {};",
      "{ let enum; }",
      "const enum = null;",
      "const [enum] = [];",
      "const { enum } = {};",
      "const { x: enum } = {};",
      "{ const enum = null; }",
      "function enum() {}",
      "function f(enum) {}",
      "function* enum() {}",
      "function* g(enum) {}",
      "(function enum() {});",
      "(function (enum) {});",
      "(function* enum() {});",
      "(function* (enum) {});",
      "(enum) => {};",
      "enum => {};",
      "class enum {}",
      "class C { constructor(enum) {} }",
      "class C { m(enum) {} }",
      "class C { static m(enum) {} }",
      "class C { *m(enum) {} }",
      "class C { static *m(enum) {} }",
      "(class enum {})",
      "(class { constructor(enum) {} });",
      "(class { m(enum) {} });",
      "(class { static m(enum) {} });",
      "(class { *m(enum) {} });",
      "(class { static *m(enum) {} });",
      "({ m(enum) {} });",
      "({ *m(enum) {} });",
      "({ set p(enum) {} });",
      "try {} catch (enum) {}",
      "try {} catch (enum) {} finally {}",
      nullptr
  };
  // clang-format on
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  RunModuleParserSyncTest(context_data, kErrorSources, kError);
}

static void CheckEntry(const i::SourceTextModuleDescriptor::Entry* entry,
                       const char* export_name, const char* local_name,
                       const char* import_name, int module_request) {
  CHECK_NOT_NULL(entry);
  if (export_name == nullptr) {
    CHECK_NULL(entry->export_name);
  } else {
    CHECK(entry->export_name->IsOneByteEqualTo(export_name));
  }
  if (local_name == nullptr) {
    CHECK_NULL(entry->local_name);
  } else {
    CHECK(entry->local_name->IsOneByteEqualTo(local_name));
  }
  if (import_name == nullptr) {
    CHECK_NULL(entry->import_name);
  } else {
    CHECK(entry->import_name->IsOneByteEqualTo(import_name));
  }
  CHECK_EQ(entry->module_request, module_request);
}

TEST_F(ParsingTest, ModuleParsingInternals) {
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();
  isolate->stack_guard()->SetStackLimit(i::GetCurrentStackPosition() -
                                        128 * 1024);

  static const char kSource[] =
      "let x = 5;"
      "export { x as y };"
      "import { q as z } from 'm.js';"
      "import n from 'n.js';"
      "export { a as b } from 'm.js';"
      "export * from 'p.js';"
      "export var foo;"
      "export function goo() {};"
      "export let hoo;"
      "export const joo = 42;"
      "export default (function koo() {});"
      "import 'q.js';"
      "let nonexport = 42;"
      "import {m as mm} from 'm.js';"
      "import {aa} from 'm.js';"
      "export {aa as bb, x};"
      "import * as loo from 'bar.js';"
      "import * as foob from 'bar.js';"
      "export {foob};";
  i::Handle<i::String> source = factory->NewStringFromAsciiChecked(kSource);
  i::Handle<i::Script> script = factory->NewScript(source);
  i::UnoptimizedCompileState compile_state;
  i::ReusableUnoptimizedCompileState reusable_state(isolate);
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
  flags.set_is_module(true);
  i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
  CHECK_PARSE_PROGRAM(&info, script, isolate);

  i::FunctionLiteral* func = info.literal();
  i::ModuleScope* module_scope = func->scope()->AsModuleScope();
  i::Scope* outer_scope = module_scope->outer_scope();
  CHECK(outer_scope->is_script_scope());
  CHECK_NULL(outer_scope->outer_scope());
  CHECK(module_scope->is_module_scope());
  const i::SourceTextModuleDescriptor::Entry* entry;
  i::Declaration::List* declarations = module_scope->declarations();
  CHECK_EQ(13, declarations->LengthForTest());

  CHECK(declarations->AtForTest(0)->var()->raw_name()->IsOneByteEqualTo("x"));
  CHECK(declarations->AtForTest(0)->var()->mode() == i::VariableMode::kLet);
  CHECK(declarations->AtForTest(0)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(0)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(1)->var()->raw_name()->IsOneByteEqualTo("z"));
  CHECK(declarations->AtForTest(1)->var()->mode() == i::VariableMode::kConst);
  CHECK(declarations->AtForTest(1)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(1)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(2)->var()->raw_name()->IsOneByteEqualTo("n"));
  CHECK(declarations->AtForTest(2)->var()->mode() == i::VariableMode::kConst);
  CHECK(declarations->AtForTest(2)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(2)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(3)->var()->raw_name()->IsOneByteEqualTo("foo"));
  CHECK(declarations->AtForTest(3)->var()->mode() == i::VariableMode::kVar);
  CHECK(!declarations->AtForTest(3)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(3)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(4)->var()->raw_name()->IsOneByteEqualTo("goo"));
  CHECK(declarations->AtForTest(4)->var()->mode() == i::VariableMode::kLet);
  CHECK(!declarations->AtForTest(4)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(4)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(5)->var()->raw_name()->IsOneByteEqualTo("hoo"));
  CHECK(declarations->AtForTest(5)->var()->mode() == i::VariableMode::kLet);
  CHECK(declarations->AtForTest(5)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(5)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(6)->var()->raw_name()->IsOneByteEqualTo("joo"));
  CHECK(declarations->AtForTest(6)->var()->mode() == i::VariableMode::kConst);
  CHECK(declarations->AtForTest(6)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(6)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(7)->var()->raw_name()->IsOneByteEqualTo(
      ".default"));
  CHECK(declarations->AtForTest(7)->var()->mode() == i::VariableMode::kConst);
  CHECK(declarations->AtForTest(7)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(7)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(8)->var()->raw_name()->IsOneByteEqualTo(
      "nonexport"));
  CHECK(!declarations->AtForTest(8)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(8)->var()->location() ==
        i::VariableLocation::LOCAL);

  CHECK(declarations->AtForTest(9)->var()->raw_name()->IsOneByteEqualTo("mm"));
  CHECK(declarations->AtForTest(9)->var()->mode() == i::VariableMode::kConst);
  CHECK(declarations->AtForTest(9)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(9)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(declarations->AtForTest(10)->var()->raw_name()->IsOneByteEqualTo("aa"));
  CHECK(declarations->AtForTest(10)->var()->mode() == i::VariableMode::kConst);
  CHECK(declarations->AtForTest(10)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(10)->var()->location() ==
        i::VariableLocation::MODULE);

  CHECK(
      declarations->AtForTest(11)->var()->raw_name()->IsOneByteEqualTo("loo"));
  CHECK(declarations->AtForTest(11)->var()->mode() == i::VariableMode::kConst);
  CHECK(!declarations->AtForTest(11)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(11)->var()->location() !=
        i::VariableLocation::MODULE);

  CHECK(
      declarations->AtForTest(12)->var()->raw_name()->IsOneByteEqualTo("foob"));
  CHECK(declarations->AtForTest(12)->var()->mode() == i::VariableMode::kConst);
  CHECK(!declarations->AtForTest(12)->var()->binding_needs_init());
  CHECK(declarations->AtForTest(12)->var()->location() ==
        i::VariableLocation::MODULE);

  i::SourceTextModuleDescriptor* descriptor = module_scope->module();
  CHECK_NOT_NULL(descriptor);

  CHECK_EQ(5u, descriptor->module_requests().size());
  for (const auto& elem : descriptor->module_requests()) {
    if (elem->specifier()->IsOneByteEqualTo("m.js")) {
      CHECK_EQ(0, elem->index());
      CHECK_EQ(51, elem->position());
    } else if (elem->specifier()->IsOneByteEqualTo("n.js")) {
      CHECK_EQ(1, elem->index());
      CHECK_EQ(72, elem->position());
    } else if (elem->specifier()->IsOneByteEqualTo("p.js")) {
      CHECK_EQ(2, elem->index());
      CHECK_EQ(123, elem->position());
    } else if (elem->specifier()->IsOneByteEqualTo("q.js")) {
      CHECK_EQ(3, elem->index());
      CHECK_EQ(249, elem->position());
    } else if (elem->specifier()->IsOneByteEqualTo("bar.js")) {
      CHECK_EQ(4, elem->index());
      CHECK_EQ(370, elem->position());
    } else {
      UNREACHABLE();
    }
  }

  CHECK_EQ(3, descriptor->special_exports().size());
  CheckEntry(descriptor->special_exports().at(0), "b", nullptr, "a", 0);
  CheckEntry(descriptor->special_exports().at(1), nullptr, nullptr, nullptr, 2);
  CheckEntry(descriptor->special_exports().at(2), "bb", nullptr, "aa",
             0);  // !!!

  CHECK_EQ(8u, descriptor->regular_exports().size());
  entry = descriptor->regular_exports()
              .find(declarations->AtForTest(3)->var()->raw_name())
              ->second;
  CheckEntry(entry, "foo", "foo", nullptr, -1);
  entry = descriptor->regular_exports()
              .find(declarations->AtForTest(4)->var()->raw_name())
              ->second;
  CheckEntry(entry, "goo", "goo", nullptr, -1);
  entry = descriptor->regular_exports()
              .find(declarations->AtForTest(5)->var()->raw_name())
              ->second;
  CheckEntry(entry, "hoo", "hoo", nullptr, -1);
  entry = descriptor->regular_exports()
              .find(declarations->AtForTest(6)->var()->raw_name())
              ->second;
  CheckEntry(entry, "joo", "joo", nullptr, -1);
  entry = descriptor->regular_exports()
              .find(declarations->AtForTest(7)->var()->raw_name())
              ->second;
  CheckEntry(entry, "default", ".default", nullptr, -1);
  entry = descriptor->regular_exports()
              .find(declarations->AtForTest(12)->var()->raw_name())
              ->second;
  CheckEntry(entry, "foob", "foob", nullptr, -1);
  // TODO(neis): The next lines are terrible. Find a better way.
  auto name_x = declarations->AtForTest(0)->var()->raw_name();
  CHECK_EQ(2u, descriptor->regular_exports().count(name_x));
  auto it = descriptor->regular_exports().equal_range(name_x).first;
  entry = it->second;
  if (entry->export_name->IsOneByteEqualTo("y")) {
    CheckEntry(entry, "y", "x", nullptr, -1);
    entry = (++it)->second;
    CheckEntry(entry, "x", "x", nullptr, -1);
  } else {
    CheckEntry(entry, "x", "x", nullptr, -1);
    entry = (++it)->second;
    CheckEntry(entry, "y", "x", nullptr, -1);
  }

  CHECK_EQ(2, descriptor->namespace_imports().size());
  CheckEntry(descriptor->namespace_imports().at(0), nullptr, "loo", nullptr, 4);
  CheckEntry(descriptor->namespace_imports().at(1), nullptr, "foob", nullptr,
             4);

  CHECK_EQ(4u, descriptor->regular_imports().size());
  entry = descriptor->regular_imports()
              .find(declarations->AtForTest(1)->var()->raw_name())
              ->second;
  CheckEntry(entry, nullptr, "z", "q", 0);
  entry = descriptor->regular_imports()
              .find(declarations->AtForTest(2)->var()->raw_name())
              ->second;
  CheckEntry(entry, nullptr, "n", "default", 1);
  entry = descriptor->regular_imports()
              .find(declarations->AtForTest(9)->var()->raw_name())
              ->second;
  CheckEntry(entry, nullptr, "mm", "m", 0);
  entry = descriptor->regular_imports()
              .find(declarations->AtForTest(10)->var()->raw_name())
              ->second;
  CheckEntry(entry, nullptr, "aa", "aa", 0);
}

TEST_F(ParsingTest, ModuleParsingInternalsWithImportAssertions) {
  i::v8_flags.harmony_import_assertions = true;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();
  isolate->stack_guard()->SetStackLimit(base::Stack::GetCurrentStackPosition() -
                                        128 * 1024);

  static const char kSource[] =
      "import { q as z } from 'm.js';"
      "import { q as z2 } from 'm.js' assert { foo: 'bar'};"
      "import { q as z3 } from 'm.js' assert { foo2: 'bar'};"
      "import { q as z4 } from 'm.js' assert { foo: 'bar2'};"
      "import { q as z5 } from 'm.js' assert { foo: 'bar', foo2: 'bar'};"
      "import { q as z6 } from 'n.js' assert { foo: 'bar'};"
      "import 'm.js' assert { foo: 'bar'};"
      "export * from 'm.js' assert { foo: 'bar', foo2: 'bar'};";
  i::Handle<i::String> source = factory->NewStringFromAsciiChecked(kSource);
  i::Handle<i::Script> script = factory->NewScript(source);
  i::UnoptimizedCompileState compile_state;
  i::ReusableUnoptimizedCompileState reusable_state(isolate);
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
  flags.set_is_module(true);
  i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
  CHECK_PARSE_PROGRAM(&info, script, isolate);

  i::FunctionLiteral* func = info.literal();
  i::ModuleScope* module_scope = func->scope()->AsModuleScope();
  CHECK(module_scope->is_module_scope());

  i::SourceTextModuleDescriptor* descriptor = module_scope->module();
  CHECK_NOT_NULL(descriptor);

  const i::AstRawString* foo_string =
      info.ast_value_factory()->GetOneByteString("foo");
  const i::AstRawString* foo2_string =
      info.ast_value_factory()->GetOneByteString("foo2");
  CHECK_EQ(6u, descriptor->module_requests().size());
  for (const auto& elem : descriptor->module_requests()) {
    if (elem->index() == 0) {
      CHECK(elem->specifier()->IsOneByteEqualTo("m.js"));
      CHECK_EQ(0, elem->import_attributes()->size());
      CHECK_EQ(23, elem->position());
    } else if (elem->index() == 1) {
      CHECK(elem->specifier()->IsOneByteEqualTo("m.js"));
      CHECK_EQ(1, elem->import_attributes()->size());
      CHECK_EQ(54, elem->position());
      CHECK(elem->import_attributes()
                ->at(foo_string)
                .first->IsOneByteEqualTo("bar"));
      CHECK_EQ(70, elem->import_attributes()->at(foo_string).second.beg_pos);
    } else if (elem->index() == 2) {
      CHECK(elem->specifier()->IsOneByteEqualTo("m.js"));
      CHECK_EQ(1, elem->import_attributes()->size());
      CHECK_EQ(106, elem->position());
      CHECK(elem->import_attributes()
                ->at(foo2_string)
                .first->IsOneByteEqualTo("bar"));
      CHECK_EQ(122, elem->import_attributes()->at(foo2_string).second.beg_pos);
    } else if (elem->index() == 3) {
      CHECK(elem->specifier()->IsOneByteEqualTo("m.js"));
      CHECK_EQ(1, elem->import_attributes()->size());
      CHECK_EQ(159, elem->position());
      CHECK(elem->import_attributes()
                ->at(foo_string)
                .first->IsOneByteEqualTo("bar2"));
      CHECK_EQ(175, elem->import_attributes()->at(foo_string).second.beg_pos);
    } else if (elem->index() == 4) {
      CHECK(elem->specifier()->IsOneByteEqualTo("m.js"));
      CHECK_EQ(2, elem->import_attributes()->size());
      CHECK_EQ(212, elem->position());
      CHECK(elem->import_attributes()
                ->at(foo_string)
                .first->IsOneByteEqualTo("bar"));
      CHECK_EQ(228, elem->import_attributes()->at(foo_string).second.beg_pos);
      CHECK(elem->import_attributes()
                ->at(foo2_string)
                .first->IsOneByteEqualTo("bar"));
      CHECK_EQ(240, elem->import_attributes()->at(foo2_string).second.beg_pos);
    } else if (elem->index() == 5) {
      CHECK(elem->specifier()->IsOneByteEqualTo("n.js"));
      CHECK_EQ(1, elem->import_attributes()->size());
      CHECK_EQ(277, elem->position());
      CHECK(elem->import_attributes()
                ->at(foo_string)
                .first->IsOneByteEqualTo("bar"));
      CHECK_EQ(293, elem->import_attributes()->at(foo_string).second.beg_pos);
    } else {
      UNREACHABLE();
    }
  }
}

TEST_F(ParsingTest, ModuleParsingModuleRequestOrdering) {
  i::v8_flags.harmony_import_assertions = true;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();
  isolate->stack_guard()->SetStackLimit(base::Stack::GetCurrentStackPosition() -
                                        128 * 1024);

  static const char kSource[] =
      "import 'foo' assert { };"
      "import 'baaaaaar' assert { };"
      "import 'aa' assert { };"
      "import 'a' assert { a: 'b' };"
      "import 'b' assert { };"
      "import 'd' assert { a: 'b' };"
      "import 'c' assert { };"
      "import 'f' assert { };"
      "import 'f' assert { a: 'b'};"
      "import 'g' assert { a: 'b' };"
      "import 'g' assert { };"
      "import 'h' assert { a: 'd' };"
      "import 'h' assert { b: 'c' };"
      "import 'i' assert { b: 'c' };"
      "import 'i' assert { a: 'd' };"
      "import 'j' assert { a: 'b' };"
      "import 'j' assert { a: 'c' };"
      "import 'k' assert { a: 'c' };"
      "import 'k' assert { a: 'b' };"
      "import 'l' assert { a: 'b', e: 'f' };"
      "import 'l' assert { a: 'c', d: 'g' };"
      "import 'm' assert { a: 'c', d: 'g' };"
      "import 'm' assert { a: 'b', e: 'f' };"
      "import 'n' assert { 'd': '' };"
      "import 'n' assert { 'a': 'b' };"
      "import 'o' assert { 'a': 'b' };"
      "import 'o' assert { 'd': '' };"
      "import 'p' assert { 'z': 'c' };"
      "import 'p' assert { 'a': 'c', 'b': 'c' };";
  i::Handle<i::String> source = factory->NewStringFromAsciiChecked(kSource);
  i::Handle<i::Script> script = factory->NewScript(source);
  i::UnoptimizedCompileState compile_state;
  i::ReusableUnoptimizedCompileState reusable_state(isolate);
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
  flags.set_is_module(true);
  i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
  CHECK_PARSE_PROGRAM(&info, script, isolate);

  i::FunctionLiteral* func = info.literal();
  i::ModuleScope* module_scope = func->scope()->AsModuleScope();
  CHECK(module_scope->is_module_scope());

  i::SourceTextModuleDescriptor* descriptor = module_scope->module();
  CHECK_NOT_NULL(descriptor);

  const i::AstRawString* a_string =
      info.ast_value_factory()->GetOneByteString("a");
  const i::AstRawString* b_string =
      info.ast_value_factory()->GetOneByteString("b");
  const i::AstRawString* d_string =
      info.ast_value_factory()->GetOneByteString("d");
  const i::AstRawString* e_string =
      info.ast_value_factory()->GetOneByteString("e");
  const i::AstRawString* z_string =
      info.ast_value_factory()->GetOneByteString("z");
  CHECK_EQ(29u, descriptor->module_requests().size());
  auto request_iterator = descriptor->module_requests().cbegin();

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("a"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("aa"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("b"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("baaaaaar"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("c"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("d"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("f"));
  CHECK_EQ(0, (*request_iterator)->import_attributes()->size());
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("f"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("foo"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("g"));
  CHECK_EQ(0, (*request_iterator)->import_attributes()->size());
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("g"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("h"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("d"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("h"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(b_string)
            .first->IsOneByteEqualTo("c"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("i"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("d"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("i"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(b_string)
            .first->IsOneByteEqualTo("c"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("j"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("b"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("j"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("c"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("k"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("b"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("k"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("c"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("l"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("b"));
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(e_string)
            .first->IsOneByteEqualTo("f"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("l"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("c"));
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(d_string)
            .first->IsOneByteEqualTo("g"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("m"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("b"));
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(e_string)
            .first->IsOneByteEqualTo("f"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("m"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("c"));
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(d_string)
            .first->IsOneByteEqualTo("g"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("n"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("b"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("n"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(d_string)
            .first->IsOneByteEqualTo(""));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("o"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("b"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("o"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(d_string)
            .first->IsOneByteEqualTo(""));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("p"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(a_string)
            .first->IsOneByteEqualTo("c"));
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(b_string)
            .first->IsOneByteEqualTo("c"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("p"));
  CHECK_EQ(1, (*request_iterator)->import_attributes()->size());
  CHECK((*request_iterator)
            ->import_attributes()
            ->at(z_string)
            .first->IsOneByteEqualTo("c"));
}

TEST_F(ParsingTest, ModuleParsingImportAssertionKeySorting) {
  i::v8_flags.harmony_import_assertions = true;
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();
  isolate->stack_guard()->SetStackLimit(base::Stack::GetCurrentStackPosition() -
                                        128 * 1024);

  static const char kSource[] =
      "import 'a' assert { 'b':'z', 'a': 'c' };"
      "import 'b' assert { 'aaaaaa': 'c', 'b': 'z' };"
      "import 'c' assert { '': 'c', 'b': 'z' };"
      "import 'd' assert { 'aabbbb': 'c', 'aaabbb': 'z' };"
      // zzzz\u0005 is a one-byte string, yyyy\u0100 is a two-byte string.
      "import 'e' assert { 'zzzz\\u0005': 'second', 'yyyy\\u0100': 'first' };"
      // Both keys are two-byte strings.
      "import 'f' assert { 'xxxx\\u0005\\u0101': 'first', "
      "'xxxx\\u0100\\u0101': 'second' };";
  i::Handle<i::String> source = factory->NewStringFromAsciiChecked(kSource);
  i::Handle<i::Script> script = factory->NewScript(source);
  i::UnoptimizedCompileState compile_state;
  i::ReusableUnoptimizedCompileState reusable_state(isolate);
  i::UnoptimizedCompileFlags flags =
      i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
  flags.set_is_module(true);
  i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
  CHECK_PARSE_PROGRAM(&info, script, isolate);

  i::FunctionLiteral* func = info.literal();
  i::ModuleScope* module_scope = func->scope()->AsModuleScope();
  CHECK(module_scope->is_module_scope());

  i::SourceTextModuleDescriptor* descriptor = module_scope->module();
  CHECK_NOT_NULL(descriptor);

  CHECK_EQ(6u, descriptor->module_requests().size());
  auto request_iterator = descriptor->module_requests().cbegin();

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("a"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  auto attributes_iterator = (*request_iterator)->import_attributes()->cbegin();
  CHECK(attributes_iterator->first->IsOneByteEqualTo("a"));
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("c"));
  ++attributes_iterator;
  CHECK(attributes_iterator->first->IsOneByteEqualTo("b"));
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("z"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("b"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  attributes_iterator = (*request_iterator)->import_attributes()->cbegin();
  CHECK(attributes_iterator->first->IsOneByteEqualTo("aaaaaa"));
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("c"));
  ++attributes_iterator;
  CHECK(attributes_iterator->first->IsOneByteEqualTo("b"));
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("z"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("c"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  attributes_iterator = (*request_iterator)->import_attributes()->cbegin();
  CHECK(attributes_iterator->first->IsOneByteEqualTo(""));
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("c"));
  ++attributes_iterator;
  CHECK(attributes_iterator->first->IsOneByteEqualTo("b"));
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("z"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("d"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  attributes_iterator = (*request_iterator)->import_attributes()->cbegin();
  CHECK(attributes_iterator->first->IsOneByteEqualTo("aaabbb"));
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("z"));
  ++attributes_iterator;
  CHECK(attributes_iterator->first->IsOneByteEqualTo("aabbbb"));
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("c"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("e"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  attributes_iterator = (*request_iterator)->import_attributes()->cbegin();
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("first"));
  ++attributes_iterator;
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("second"));
  ++request_iterator;

  CHECK((*request_iterator)->specifier()->IsOneByteEqualTo("f"));
  CHECK_EQ(2, (*request_iterator)->import_attributes()->size());
  attributes_iterator = (*request_iterator)->import_attributes()->cbegin();
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("first"));
  ++attributes_iterator;
  CHECK(attributes_iterator->second.first->IsOneByteEqualTo("second"));
}

TEST_F(ParsingTest, DuplicateProtoError) {
  const char* context_data[][2] = {
      {"({", "});"}, {"'use strict'; ({", "});"}, {nullptr, nullptr}};
  const char* error_data[] = {"__proto__: {}, __proto__: {}",
                              "__proto__: {}, \"__proto__\": {}",
                              "__proto__: {}, \"__\x70roto__\": {}",
                              "__proto__: {}, a: 1, __proto__: {}", nullptr};

  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, DuplicateProtoNoError) {
  const char* context_data[][2] = {
      {"({", "});"}, {"'use strict'; ({", "});"}, {nullptr, nullptr}};
  const char* error_data[] = {
      "__proto__: {}, ['__proto__']: {}",  "__proto__: {}, __proto__() {}",
      "__proto__: {}, get __proto__() {}", "__proto__: {}, set __proto__(v) {}",
      "__proto__: {}, __proto__",          nullptr};

  RunParserSyncTest(context_data, error_data, kSuccess);
}

TEST_F(ParsingTest, DeclarationsError) {
  const char* context_data[][2] = {{"'use strict'; if (true)", ""},
                                   {"'use strict'; if (false) {} else", ""},
                                   {"'use strict'; while (false)", ""},
                                   {"'use strict'; for (;;)", ""},
                                   {"'use strict'; for (x in y)", ""},
                                   {"'use strict'; do ", " while (false)"},
                                   {nullptr, nullptr}};

  const char* statement_data[] = {"let x = 1;", "const x = 1;", "class C {}",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, LanguageModeDirectives) {
  TestLanguageMode("\"use nothing\"", i::LanguageMode::kSloppy);
  TestLanguageMode("\"use strict\"", i::LanguageMode::kStrict);

  TestLanguageMode("var x = 1; \"use strict\"", i::LanguageMode::kSloppy);

  TestLanguageMode("\"use some future directive\"; \"use strict\";",
                   i::LanguageMode::kStrict);
}

TEST_F(ParsingTest, PropertyNameEvalArguments) {
  const char* context_data[][2] = {{"'use strict';", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"({eval: 1})",
                                  "({arguments: 1})",
                                  "({eval() {}})",
                                  "({arguments() {}})",
                                  "({*eval() {}})",
                                  "({*arguments() {}})",
                                  "({get eval() {}})",
                                  "({get arguments() {}})",
                                  "({set eval(_) {}})",
                                  "({set arguments(_) {}})",

                                  "class C {eval() {}}",
                                  "class C {arguments() {}}",
                                  "class C {*eval() {}}",
                                  "class C {*arguments() {}}",
                                  "class C {get eval() {}}",
                                  "class C {get arguments() {}}",
                                  "class C {set eval(_) {}}",
                                  "class C {set arguments(_) {}}",

                                  "class C {static eval() {}}",
                                  "class C {static arguments() {}}",
                                  "class C {static *eval() {}}",
                                  "class C {static *arguments() {}}",
                                  "class C {static get eval() {}}",
                                  "class C {static get arguments() {}}",
                                  "class C {static set eval(_) {}}",
                                  "class C {static set arguments(_) {}}",

                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, FunctionLiteralDuplicateParameters) {
  const char* strict_context_data[][2] = {
      {"'use strict';(function(", "){})();"},
      {"(function(", ") { 'use strict'; })();"},
      {"'use strict'; function fn(", ") {}; fn();"},
      {"function fn(", ") { 'use strict'; }; fn();"},
      {nullptr, nullptr}};

  const char* sloppy_context_data[][2] = {{"(function(", "){})();"},
                                          {"(function(", ") {})();"},
                                          {"function fn(", ") {}; fn();"},
                                          {"function fn(", ") {}; fn();"},
                                          {nullptr, nullptr}};

  const char* data[] = {
      "a, a",
      "a, a, a",
      "b, a, a",
      "a, b, c, c",
      "a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, w",
      nullptr};

  RunParserSyncTest(strict_context_data, data, kError);
  RunParserSyncTest(sloppy_context_data, data, kSuccess);
}

TEST_F(ParsingTest, ArrowFunctionASIErrors) {
  const char* context_data[][2] = {
      {"'use strict';", ""}, {"", ""}, {nullptr, nullptr}};

  const char* data[] = {"(a\n=> a)(1)",
                        "(a/*\n*/=> a)(1)",
                        "((a)\n=> a)(1)",
                        "((a)/*\n*/=> a)(1)",
                        "((a, b)\n=> a + b)(1, 2)",
                        "((a, b)/*\n*/=> a + b)(1, 2)",
                        nullptr};
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ObjectSpreadPositiveTests) {
  // clang-format off
  const char* context_data[][2] = {
    {"x = ", ""},
    {"'use strict'; x = ", ""},
    {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "{ ...y }",
    "{ a: 1, ...y }",
    "{ b: 1, ...y }",
    "{ y, ...y}",
    "{ ...z = y}",
    "{ ...y, y }",
    "{ ...y, ...y}",
    "{ a: 1, ...y, b: 1}",
    "{ ...y, b: 1}",
    "{ ...1}",
    "{ ...null}",
    "{ ...undefined}",
    "{ ...1 in {}}",
    "{ ...[]}",
    "{ ...async function() { }}",
    "{ ...async () => { }}",
    "{ ...new Foo()}",
    nullptr};
  // clang-format on

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ObjectSpreadNegativeTests) {
  const char* context_data[][2] = {
      {"x = ", ""}, {"'use strict'; x = ", ""}, {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "{ ...var z = y}",
    "{ ...var}",
    "{ ...foo bar}",
    "{* ...foo}",
    "{get ...foo}",
    "{set ...foo}",
    "{async ...foo}",
    nullptr};

  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, TemplateEscapesPositiveTests) {
  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {"'use strict';", ""},
    {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "tag`\\08`",
    "tag`\\01`",
    "tag`\\01${0}right`",
    "tag`left${0}\\01`",
    "tag`left${0}\\01${1}right`",
    "tag`\\1`",
    "tag`\\1${0}right`",
    "tag`left${0}\\1`",
    "tag`left${0}\\1${1}right`",
    "tag`\\xg`",
    "tag`\\xg${0}right`",
    "tag`left${0}\\xg`",
    "tag`left${0}\\xg${1}right`",
    "tag`\\xAg`",
    "tag`\\xAg${0}right`",
    "tag`left${0}\\xAg`",
    "tag`left${0}\\xAg${1}right`",
    "tag`\\u0`",
    "tag`\\u0${0}right`",
    "tag`left${0}\\u0`",
    "tag`left${0}\\u0${1}right`",
    "tag`\\u0g`",
    "tag`\\u0g${0}right`",
    "tag`left${0}\\u0g`",
    "tag`left${0}\\u0g${1}right`",
    "tag`\\u00g`",
    "tag`\\u00g${0}right`",
    "tag`left${0}\\u00g`",
    "tag`left${0}\\u00g${1}right`",
    "tag`\\u000g`",
    "tag`\\u000g${0}right`",
    "tag`left${0}\\u000g`",
    "tag`left${0}\\u000g${1}right`",
    "tag`\\u{}`",
    "tag`\\u{}${0}right`",
    "tag`left${0}\\u{}`",
    "tag`left${0}\\u{}${1}right`",
    "tag`\\u{-0}`",
    "tag`\\u{-0}${0}right`",
    "tag`left${0}\\u{-0}`",
    "tag`left${0}\\u{-0}${1}right`",
    "tag`\\u{g}`",
    "tag`\\u{g}${0}right`",
    "tag`left${0}\\u{g}`",
    "tag`left${0}\\u{g}${1}right`",
    "tag`\\u{0`",
    "tag`\\u{0${0}right`",
    "tag`left${0}\\u{0`",
    "tag`left${0}\\u{0${1}right`",
    "tag`\\u{\\u{0}`",
    "tag`\\u{\\u{0}${0}right`",
    "tag`left${0}\\u{\\u{0}`",
    "tag`left${0}\\u{\\u{0}${1}right`",
    "tag`\\u{110000}`",
    "tag`\\u{110000}${0}right`",
    "tag`left${0}\\u{110000}`",
    "tag`left${0}\\u{110000}${1}right`",
    "tag` ${tag`\\u`}`",
    "tag` ``\\u`",
    "tag`\\u`` `",
    "tag`\\u``\\u`",
    "` ${tag`\\u`}`",
    "` ``\\u`",
    nullptr};
  // clang-format on

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, TemplateEscapesNegativeTests) {
  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {"'use strict';", ""},
    {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "`\\08`",
    "`\\01`",
    "`\\01${0}right`",
    "`left${0}\\01`",
    "`left${0}\\01${1}right`",
    "`\\1`",
    "`\\1${0}right`",
    "`left${0}\\1`",
    "`left${0}\\1${1}right`",
    "`\\xg`",
    "`\\xg${0}right`",
    "`left${0}\\xg`",
    "`left${0}\\xg${1}right`",
    "`\\xAg`",
    "`\\xAg${0}right`",
    "`left${0}\\xAg`",
    "`left${0}\\xAg${1}right`",
    "`\\u0`",
    "`\\u0${0}right`",
    "`left${0}\\u0`",
    "`left${0}\\u0${1}right`",
    "`\\u0g`",
    "`\\u0g${0}right`",
    "`left${0}\\u0g`",
    "`left${0}\\u0g${1}right`",
    "`\\u00g`",
    "`\\u00g${0}right`",
    "`left${0}\\u00g`",
    "`left${0}\\u00g${1}right`",
    "`\\u000g`",
    "`\\u000g${0}right`",
    "`left${0}\\u000g`",
    "`left${0}\\u000g${1}right`",
    "`\\u{}`",
    "`\\u{}${0}right`",
    "`left${0}\\u{}`",
    "`left${0}\\u{}${1}right`",
    "`\\u{-0}`",
    "`\\u{-0}${0}right`",
    "`left${0}\\u{-0}`",
    "`left${0}\\u{-0}${1}right`",
    "`\\u{g}`",
    "`\\u{g}${0}right`",
    "`left${0}\\u{g}`",
    "`left${0}\\u{g}${1}right`",
    "`\\u{0`",
    "`\\u{0${0}right`",
    "`left${0}\\u{0`",
    "`left${0}\\u{0${1}right`",
    "`\\u{\\u{0}`",
    "`\\u{\\u{0}${0}right`",
    "`left${0}\\u{\\u{0}`",
    "`left${0}\\u{\\u{0}${1}right`",
    "`\\u{110000}`",
    "`\\u{110000}${0}right`",
    "`left${0}\\u{110000}`",
    "`left${0}\\u{110000}${1}right`",
    "`\\1``\\2`",
    "tag` ${`\\u`}`",
    "`\\u```",
    nullptr};
  // clang-format on

  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, DestructuringPositiveTests) {
  const char* context_data[][2] = {{"'use strict'; let ", " = {};"},
                                   {"var ", " = {};"},
                                   {"'use strict'; const ", " = {};"},
                                   {"function f(", ") {}"},
                                   {"function f(argument1, ", ") {}"},
                                   {"var f = (", ") => {};"},
                                   {"var f = (argument1,", ") => {};"},
                                   {"try {} catch(", ") {}"},
                                   {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "a",
    "{ x : y }",
    "{ x : y = 1 }",
    "{ get, set }",
    "{ get = 1, set = 2 }",
    "[a]",
    "[a = 1]",
    "[a,b,c]",
    "[a, b = 42, c]",
    "{ x : x, y : y }",
    "{ x : x = 1, y : y }",
    "{ x : x, y : y = 42 }",
    "[]",
    "{}",
    "[{x:x, y:y}, [a,b,c]]",
    "[{x:x = 1, y:y = 2}, [a = 3, b = 4, c = 5]]",
    "{x}",
    "{x, y}",
    "{x = 42, y = 15}",
    "[a,,b]",
    "{42 : x}",
    "{42 : x = 42}",
    "{42e-2 : x}",
    "{42e-2 : x = 42}",
    "{x : y, x : z}",
    "{'hi' : x}",
    "{'hi' : x = 42}",
    "{var: x}",
    "{var: x = 42}",
    "{[x] : z}",
    "{[1+1] : z}",
    "{[foo()] : z}",
    "{}",
    "[...rest]",
    "[a,b,...rest]",
    "[a,,...rest]",
    "{ __proto__: x, __proto__: y}",
    "{arguments: x}",
    "{eval: x}",
    "{ x : y, ...z }",
    "{ x : y = 1, ...z }",
    "{ x : x, y : y, ...z }",
    "{ x : x = 1, y : y, ...z }",
    "{ x : x, y : y = 42, ...z }",
    "[{x:x, y:y, ...z}, [a,b,c]]",
    "[{x:x = 1, y:y = 2, ...z}, [a = 3, b = 4, c = 5]]",
    "{...x}",
    "{x, ...y}",
    "{x = 42, y = 15, ...z}",
    "{42 : x = 42, ...y}",
    "{'hi' : x, ...z}",
    "{'hi' : x = 42, ...z}",
    "{var: x = 42, ...z}",
    "{[x] : z, ...y}",
    "{[1+1] : z, ...x}",
    "{arguments: x, ...z}",
    "{ __proto__: x, __proto__: y, ...z}",
    nullptr
  };

  // clang-format on
  RunParserSyncTest(context_data, data, kSuccess);
}

// v8:5201
TEST_F(ParsingTest, SloppyContextDestructuringPositiveTests) {
  // clang-format off
  const char* sloppy_context_data[][2] = {
    {"var ", " = {};"},
    {"function f(", ") {}"},
    {"function f(argument1, ", ") {}"},
    {"var f = (", ") => {};"},
    {"var f = (argument1,", ") => {};"},
    {"try {} catch(", ") {}"},
    {nullptr, nullptr}
  };

  const char* data[] = {
    "{arguments}",
    "{eval}",
    "{x: arguments}",
    "{x: eval}",
    "{arguments = false}",
    "{eval = false}",
    "{...arguments}",
    "{...eval}",
    nullptr
  };
  // clang-format on
  RunParserSyncTest(sloppy_context_data, data, kSuccess);
}

TEST_F(ParsingTest, DestructuringNegativeTests) {
  {  // All modes.
    const char* context_data[][2] = {{"'use strict'; let ", " = {};"},
                                     {"var ", " = {};"},
                                     {"'use strict'; const ", " = {};"},
                                     {"function f(", ") {}"},
                                     {"function f(argument1, ", ") {}"},
                                     {"var f = (", ") => {};"},
                                     {"var f = ", " => {};"},
                                     {"var f = (argument1,", ") => {};"},
                                     {"try {} catch(", ") {}"},
                                     {nullptr, nullptr}};

    // clang-format off
    const char* data[] = {
        "a++",
        "++a",
        "delete a",
        "void a",
        "typeof a",
        "--a",
        "+a",
        "-a",
        "~a",
        "!a",
        "{ x : y++ }",
        "[a++]",
        "(x => y)",
        "(async x => y)",
        "((x, z) => y)",
        "(async (x, z) => y)",
        "a[i]", "a()",
        "a.b",
        "new a",
        "a + a",
        "a - a",
        "a * a",
        "a / a",
        "a == a",
        "a != a",
        "a > a",
        "a < a",
        "a <<< a",
        "a >>> a",
        "function a() {}",
        "function* a() {}",
        "async function a() {}",
        "a`bcd`",
        "this",
        "null",
        "true",
        "false",
        "1",
        "'abc'",
        "/abc/",
        "`abc`",
        "class {}",
        "{+2 : x}",
        "{-2 : x}",
        "var",
        "[var]",
        "{x : {y : var}}",
        "{x : x = a+}",
        "{x : x = (a+)}",
        "{x : x += a}",
        "{m() {} = 0}",
        "{[1+1]}",
        "[...rest, x]",
        "[a,b,...rest, x]",
        "[a,,...rest, x]",
        "[...rest,]",
        "[a,b,...rest,]",
        "[a,,...rest,]",
        "[...rest,...rest1]",
        "[a,b,...rest,...rest1]",
        "[a,,..rest,...rest1]",
        "[x, y, ...z = 1]",
        "[...z = 1]",
        "[x, y, ...[z] = [1]]",
        "[...[z] = [1]]",
        "{ x : 3 }",
        "{ x : 'foo' }",
        "{ x : /foo/ }",
        "{ x : `foo` }",
        "{ get a() {} }",
        "{ set a() {} }",
        "{ method() {} }",
        "{ *method() {} }",
        "...a++",
        "...++a",
        "...typeof a",
        "...[a++]",
        "...(x => y)",
        "{ ...x, }",
        "{ ...x, y }",
        "{ y, ...x, y }",
        "{ ...x, ...y }",
        "{ ...x, ...x }",
        "{ ...x, ...x = {} }",
        "{ ...x, ...x = ...x }",
        "{ ...x, ...x = ...{ x } }",
        "{ ,, ...x }",
        "{ ...get a() {} }",
        "{ ...set a() {} }",
        "{ ...method() {} }",
        "{ ...function() {} }",
        "{ ...*method() {} }",
        "{...{x} }",
        "{...[x] }",
        "{...{ x = 5 } }",
        "{...[ x = 5 ] }",
        "{...x.f }",
        "{...x[0] }",
        "async function* a() {}",
        nullptr
    };

    // clang-format on
    RunParserSyncTest(context_data, data, kError);
  }

  {  // All modes.
    const char* context_data[][2] = {
        {"'use strict'; let ", " = {};"},    {"var ", " = {};"},
        {"'use strict'; const ", " = {};"},  {"function f(", ") {}"},
        {"function f(argument1, ", ") {}"},  {"var f = (", ") => {};"},
        {"var f = (argument1,", ") => {};"}, {nullptr, nullptr}};

    // clang-format off
    const char* data[] = {
        "x => x",
        "() => x",
        nullptr};
    // clang-format on
    RunParserSyncTest(context_data, data, kError);
  }

  {  // Strict mode.
    const char* context_data[][2] = {
        {"'use strict'; var ", " = {};"},
        {"'use strict'; let ", " = {};"},
        {"'use strict'; const ", " = {};"},
        {"'use strict'; function f(", ") {}"},
        {"'use strict'; function f(argument1, ", ") {}"},
        {nullptr, nullptr}};

    // clang-format off
    const char* data[] = {
      "[arguments]",
      "[eval]",
      "{ a : arguments }",
      "{ a : eval }",
      "[public]",
      "{ x : private }",
      "{ x : arguments }",
      "{ x : eval }",
      "{ arguments }",
      "{ eval }",
      "{ arguments = false }"
      "{ eval = false }",
      "{ ...eval }",
      "{ ...arguments }",
      nullptr};

    // clang-format on
    RunParserSyncTest(context_data, data, kError);
  }

  {  // 'yield' in generators.
    const char* context_data[][2] = {
        {"function*() { var ", " = {};"},
        {"function*() { 'use strict'; let ", " = {};"},
        {"function*() { 'use strict'; const ", " = {};"},
        {nullptr, nullptr}};

    // clang-format off
    const char* data[] = {
      "yield",
      "[yield]",
      "{ x : yield }",
      nullptr};
    // clang-format on
    RunParserSyncTest(context_data, data, kError);
  }

  {  // Declaration-specific errors
    const char* context_data[][2] = {{"'use strict'; var ", ""},
                                     {"'use strict'; let ", ""},
                                     {"'use strict'; const ", ""},
                                     {"'use strict'; for (var ", ";;) {}"},
                                     {"'use strict'; for (let ", ";;) {}"},
                                     {"'use strict'; for (const ", ";;) {}"},
                                     {"var ", ""},
                                     {"let ", ""},
                                     {"const ", ""},
                                     {"for (var ", ";;) {}"},
                                     {"for (let ", ";;) {}"},
                                     {"for (const ", ";;) {}"},
                                     {nullptr, nullptr}};

    // clang-format off
    const char* data[] = {
      "{ a }",
      "[ a ]",
      "{ ...a }",
      nullptr
    };
    // clang-format on
    RunParserSyncTest(context_data, data, kError);
  }
}

TEST_F(ParsingTest, ObjectRestNegativeTestSlow) {
  // clang-format off
  const char* context_data[][2] = {
    {"var { ", " } = { a: 1};"},
    { nullptr, nullptr }
  };

  using v8::internal::InstructionStream;
  std::string statement;
  for (int i = 0; i < Code::kMaxArguments; ++i) {
    statement += std::to_string(i) + " : " + "x, ";
  }
  statement += "...y";

  const char* statement_data[] = {
    statement.c_str(),
    nullptr
  };

  // clang-format on
  // The test is quite slow, so run it with a reduced set of flags.
  static const ParserFlag flags[] = {kAllowLazy};
  RunParserSyncTest(context_data, statement_data, kError, nullptr, 0, flags,
                    arraysize(flags));
}

TEST_F(ParsingTest, DestructuringAssignmentPositiveTests) {
  const char* context_data[][2] = {
      {"'use strict'; let x, y, z; (", " = {});"},
      {"var x, y, z; (", " = {});"},
      {"'use strict'; let x, y, z; for (x in ", " = {});"},
      {"'use strict'; let x, y, z; for (x of ", " = {});"},
      {"var x, y, z; for (x in ", " = {});"},
      {"var x, y, z; for (x of ", " = {});"},
      {"var x, y, z; for (", " in {});"},
      {"var x, y, z; for (", " of {});"},
      {"'use strict'; var x, y, z; for (", " in {});"},
      {"'use strict'; var x, y, z; for (", " of {});"},
      {"var x, y, z; m(['a']) ? ", " = {} : rhs"},
      {"var x, y, z; m(['b']) ? lhs : ", " = {}"},
      {"'use strict'; var x, y, z; m(['a']) ? ", " = {} : rhs"},
      {"'use strict'; var x, y, z; m(['b']) ? lhs : ", " = {}"},
      {nullptr, nullptr}};

  const char* mixed_assignments_context_data[][2] = {
      {"'use strict'; let x, y, z; (", " = z = {});"},
      {"var x, y, z; (", " = z = {});"},
      {"'use strict'; let x, y, z; (x = ", " = z = {});"},
      {"var x, y, z; (x = ", " = z = {});"},
      {"'use strict'; let x, y, z; for (x in ", " = z = {});"},
      {"'use strict'; let x, y, z; for (x in x = ", " = z = {});"},
      {"'use strict'; let x, y, z; for (x of ", " = z = {});"},
      {"'use strict'; let x, y, z; for (x of x = ", " = z = {});"},
      {"var x, y, z; for (x in ", " = z = {});"},
      {"var x, y, z; for (x in x = ", " = z = {});"},
      {"var x, y, z; for (x of ", " = z = {});"},
      {"var x, y, z; for (x of x = ", " = z = {});"},
      {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "x",

    "{ x : y }",
    "{ x : foo().y }",
    "{ x : foo()[y] }",
    "{ x : y.z }",
    "{ x : y[z] }",
    "{ x : { y } }",
    "{ x : { foo: y } }",
    "{ x : { foo: foo().y } }",
    "{ x : { foo: foo()[y] } }",
    "{ x : { foo: y.z } }",
    "{ x : { foo: y[z] } }",
    "{ x : [ y ] }",
    "{ x : [ foo().y ] }",
    "{ x : [ foo()[y] ] }",
    "{ x : [ y.z ] }",
    "{ x : [ y[z] ] }",

    "{ x : y = 10 }",
    "{ x : foo().y = 10 }",
    "{ x : foo()[y] = 10 }",
    "{ x : y.z = 10 }",
    "{ x : y[z] = 10 }",
    "{ x : { y = 10 } = {} }",
    "{ x : { foo: y = 10 } = {} }",
    "{ x : { foo: foo().y = 10 } = {} }",
    "{ x : { foo: foo()[y] = 10 } = {} }",
    "{ x : { foo: y.z = 10 } = {} }",
    "{ x : { foo: y[z] = 10 } = {} }",
    "{ x : [ y = 10 ] = {} }",
    "{ x : [ foo().y = 10 ] = {} }",
    "{ x : [ foo()[y] = 10 ] = {} }",
    "{ x : [ y.z = 10 ] = {} }",
    "{ x : [ y[z] = 10 ] = {} }",
    "{ z : { __proto__: x, __proto__: y } = z }"

    "[ x ]",
    "[ foo().x ]",
    "[ foo()[x] ]",
    "[ x.y ]",
    "[ x[y] ]",
    "[ { x } ]",
    "[ { x : y } ]",
    "[ { x : foo().y } ]",
    "[ { x : foo()[y] } ]",
    "[ { x : x.y } ]",
    "[ { x : x[y] } ]",
    "[ [ x ] ]",
    "[ [ foo().x ] ]",
    "[ [ foo()[x] ] ]",
    "[ [ x.y ] ]",
    "[ [ x[y] ] ]",

    "[ x = 10 ]",
    "[ foo().x = 10 ]",
    "[ foo()[x] = 10 ]",
    "[ x.y = 10 ]",
    "[ x[y] = 10 ]",
    "[ { x = 10 } = {} ]",
    "[ { x : y = 10 } = {} ]",
    "[ { x : foo().y = 10 } = {} ]",
    "[ { x : foo()[y] = 10 } = {} ]",
    "[ { x : x.y = 10 } = {} ]",
    "[ { x : x[y] = 10 } = {} ]",
    "[ [ x = 10 ] = {} ]",
    "[ [ foo().x = 10 ] = {} ]",
    "[ [ foo()[x] = 10 ] = {} ]",
    "[ [ x.y = 10 ] = {} ]",
    "[ [ x[y] = 10 ] = {} ]",
    "{ x : y = 1 }",
    "{ x }",
    "{ x, y, z }",
    "{ x = 1, y: z, z: y }",
    "{x = 42, y = 15}",
    "[x]",
    "[x = 1]",
    "[x,y,z]",
    "[x, y = 42, z]",
    "{ x : x, y : y }",
    "{ x : x = 1, y : y }",
    "{ x : x, y : y = 42 }",
    "[]",
    "{}",
    "[{x:x, y:y}, [,x,z,]]",
    "[{x:x = 1, y:y = 2}, [z = 3, z = 4, z = 5]]",
    "[x,,y]",
    "[(x),,(y)]",
    "[(x)]",
    "{42 : x}",
    "{42 : x = 42}",
    "{42e-2 : x}",
    "{42e-2 : x = 42}",
    "{'hi' : x}",
    "{'hi' : x = 42}",
    "{var: x}",
    "{var: x = 42}",
    "{var: (x) = 42}",
    "{[x] : z}",
    "{[1+1] : z}",
    "{[1+1] : (z)}",
    "{[foo()] : z}",
    "{[foo()] : (z)}",
    "{[foo()] : foo().bar}",
    "{[foo()] : foo()['bar']}",
    "{[foo()] : this.bar}",
    "{[foo()] : this['bar']}",
    "{[foo()] : 'foo'.bar}",
    "{[foo()] : 'foo'['bar']}",
    "[...x]",
    "[x,y,...z]",
    "[x,,...z]",
    "{ x: y }",
    "[x, y]",
    "[((x, y) => z).x]",
    "{x: ((y, z) => z).x}",
    "[((x, y) => z)['x']]",
    "{x: ((y, z) => z)['x']}",

    "{x: { y = 10 } }",
    "[(({ x } = { x: 1 }) => x).a]",

    "{ ...d.x }",
    "{ ...c[0]}",

    // v8:4662
    "{ x: (y) }",
    "{ x: (y) = [] }",
    "{ x: (foo.bar) }",
    "{ x: (foo['bar']) }",
    "[ ...(a) ]",
    "[ ...(foo['bar']) ]",
    "[ ...(foo.bar) ]",
    "[ (y) ]",
    "[ (foo.bar) ]",
    "[ (foo['bar']) ]",

    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, data, kSuccess);

  RunParserSyncTest(mixed_assignments_context_data, data, kSuccess);

  const char* empty_context_data[][2] = {
      {"'use strict';", ""}, {"", ""}, {nullptr, nullptr}};

  // CoverInitializedName ambiguity handling in various contexts
  const char* ambiguity_data[] = {
      "var foo = { x = 10 } = {};",
      "var foo = { q } = { x = 10 } = {};",
      "var foo; foo = { x = 10 } = {};",
      "var foo; foo = { q } = { x = 10 } = {};",
      "var x; ({ x = 10 } = {});",
      "var q, x; ({ q } = { x = 10 } = {});",
      "var x; [{ x = 10 } = {}]",
      "var x; (true ? { x = true } = {} : { x = false } = {})",
      "var q, x; (q, { x = 10 } = {});",
      "var { x = 10 } = { x = 20 } = {};",
      "var { __proto__: x, __proto__: y } = {}",
      "({ __proto__: x, __proto__: y } = {})",
      "var { x = 10 } = (o = { x = 20 } = {});",
      "var x; (({ x = 10 } = { x = 20 } = {}) => x)({})",
      nullptr,
  };
  RunParserSyncTest(empty_context_data, ambiguity_data, kSuccess);
}

TEST_F(ParsingTest, DestructuringAssignmentNegativeTests) {
  const char* context_data[][2] = {
      {"'use strict'; let x, y, z; (", " = {});"},
      {"var x, y, z; (", " = {});"},
      {"'use strict'; let x, y, z; for (x in ", " = {});"},
      {"'use strict'; let x, y, z; for (x of ", " = {});"},
      {"var x, y, z; for (x in ", " = {});"},
      {"var x, y, z; for (x of ", " = {});"},
      {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "{ x : ++y }",
    "{ x : y * 2 }",
    "{ get x() {} }",
    "{ set x() {} }",
    "{ x: y() }",
    "{ this }",
    "{ x: this }",
    "{ x: this = 1 }",
    "{ super }",
    "{ x: super }",
    "{ x: super = 1 }",
    "{ new.target }",
    "{ x: new.target }",
    "{ x: new.target = 1 }",
    "{ import.meta }",
    "{ x: import.meta }",
    "{ x: import.meta = 1 }",
    "[x--]",
    "[--x = 1]",
    "[x()]",
    "[this]",
    "[this = 1]",
    "[new.target]",
    "[new.target = 1]",
    "[import.meta]",
    "[import.meta = 1]",
    "[super]",
    "[super = 1]",
    "[function f() {}]",
    "[async function f() {}]",
    "[function* f() {}]",
    "[50]",
    "[(50)]",
    "[(function() {})]",
    "[(async function() {})]",
    "[(function*() {})]",
    "[(foo())]",
    "{ x: 50 }",
    "{ x: (50) }",
    "['str']",
    "{ x: 'str' }",
    "{ x: ('str') }",
    "{ x: (foo()) }",
    "{ x: function() {} }",
    "{ x: async function() {} }",
    "{ x: function*() {} }",
    "{ x: (function() {}) }",
    "{ x: (async function() {}) }",
    "{ x: (function*() {}) }",
    "{ x: y } = 'str'",
    "[x, y] = 'str'",
    "[(x,y) => z]",
    "[async(x,y) => z]",
    "[async x => z]",
    "{x: (y) => z}",
    "{x: (y,w) => z}",
    "{x: async (y) => z}",
    "{x: async (y,w) => z}",
    "[x, ...y, z]",
    "[...x,]",
    "[x, y, ...z = 1]",
    "[...z = 1]",
    "[x, y, ...[z] = [1]]",
    "[...[z] = [1]]",

    "[...++x]",
    "[...x--]",
    "[...!x]",
    "[...x + y]",

    // v8:4657
    "({ x: x4, x: (x+=1e4) })",
    "(({ x: x4, x: (x+=1e4) }))",
    "({ x: x4, x: (x+=1e4) } = {})",
    "(({ x: x4, x: (x+=1e4) } = {}))",
    "(({ x: x4, x: (x+=1e4) }) = {})",
    "({ x: y } = {})",
    "(({ x: y } = {}))",
    "(({ x: y }) = {})",
    "([a])",
    "(([a]))",
    "([a] = [])",
    "(([a] = []))",
    "(([a]) = [])",

    // v8:4662
    "{ x: ([y]) }",
    "{ x: ([y] = []) }",
    "{ x: ({y}) }",
    "{ x: ({y} = {}) }",
    "{ x: (++y) }",
    "[ (...[a]) ]",
    "[ ...([a]) ]",
    "[ ...([a] = [])",
    "[ ...[ ( [ a ] ) ] ]",
    "[ ([a]) ]",
    "[ (...[a]) ]",
    "[ ([a] = []) ]",
    "[ (++y) ]",
    "[ ...(++y) ]",

    "[ x += x ]",
    "{ foo: x += x }",

    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, data, kError);

  const char* empty_context_data[][2] = {
      {"'use strict';", ""}, {"", ""}, {nullptr, nullptr}};

  // CoverInitializedName ambiguity handling in various contexts
  const char* ambiguity_data[] = {
      "var foo = { x = 10 };",
      "var foo = { q } = { x = 10 };",
      "var foo; foo = { x = 10 };",
      "var foo; foo = { q } = { x = 10 };",
      "var x; ({ x = 10 });",
      "var q, x; ({ q } = { x = 10 });",
      "var x; [{ x = 10 }]",
      "var x; (true ? { x = true } : { x = false })",
      "var q, x; (q, { x = 10 });",
      "var { x = 10 } = { x = 20 };",
      "var { x = 10 } = (o = { x = 20 });",
      "var x; (({ x = 10 } = { x = 20 }) => x)({})",

      // Not ambiguous, but uses same context data
      "switch([window %= []] = []) { default: }",

      nullptr,
  };
  RunParserSyncTest(empty_context_data, ambiguity_data, kError);

  // Strict mode errors
  const char* strict_context_data[][2] = {{"'use strict'; (", " = {})"},
                                          {"'use strict'; for (", " of {}) {}"},
                                          {"'use strict'; for (", " in {}) {}"},
                                          {nullptr, nullptr}};
  const char* strict_data[] = {
      "{ eval }", "{ arguments }", "{ foo: eval }", "{ foo: arguments }",
      "{ eval = 0 }", "{ arguments = 0 }", "{ foo: eval = 0 }",
      "{ foo: arguments = 0 }", "[ eval ]", "[ arguments ]", "[ eval = 0 ]",
      "[ arguments = 0 ]",

      // v8:4662
      "{ x: (eval) }", "{ x: (arguments) }", "{ x: (eval = 0) }",
      "{ x: (arguments = 0) }", "{ x: (eval) = 0 }", "{ x: (arguments) = 0 }",
      "[ (eval) ]", "[ (arguments) ]", "[ (eval = 0) ]", "[ (arguments = 0) ]",
      "[ (eval) = 0 ]", "[ (arguments) = 0 ]", "[ ...(eval) ]",
      "[ ...(arguments) ]", "[ ...(eval = 0) ]", "[ ...(arguments = 0) ]",
      "[ ...(eval) = 0 ]", "[ ...(arguments) = 0 ]",

      nullptr};
  RunParserSyncTest(strict_context_data, strict_data, kError);
}

TEST_F(ParsingTest, DestructuringDisallowPatternsInForVarIn) {
  const char* context_data[][2] = {
      {"", ""}, {"function f() {", "}"}, {nullptr, nullptr}};
  // clang-format off
  const char* error_data[] = {
    "for (let x = {} in null);",
    "for (let x = {} of null);",
    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, error_data, kError);

  // clang-format off
  const char* success_data[] = {
    "for (var x = {} in null);",
    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, success_data, kSuccess);
}

TEST_F(ParsingTest, DestructuringDuplicateParams) {
  const char* context_data[][2] = {{"'use strict';", ""},
                                   {"function outer() { 'use strict';", "}"},
                                   {nullptr, nullptr}};

  // clang-format off
  const char* error_data[] = {
    "function f(x,x){}",
    "function f(x, {x : x}){}",
    "function f(x, {x}){}",
    "function f({x,x}) {}",
    "function f([x,x]) {}",
    "function f(x, [y,{z:x}]) {}",
    "function f([x,{y:x}]) {}",
    // non-simple parameter list causes duplicates to be errors in sloppy mode.
    "function f(x, x, {a}) {}",
    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, DestructuringDuplicateParamsSloppy) {
  const char* context_data[][2] = {
      {"", ""}, {"function outer() {", "}"}, {nullptr, nullptr}};

  // clang-format off
  const char* error_data[] = {
    // non-simple parameter list causes duplicates to be errors in sloppy mode.
    "function f(x, {x : x}){}",
    "function f(x, {x}){}",
    "function f({x,x}) {}",
    "function f(x, x, {a}) {}",
    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, DestructuringDisallowPatternsInSingleParamArrows) {
  const char* context_data[][2] = {{"'use strict';", ""},
                                   {"function outer() { 'use strict';", "}"},
                                   {"", ""},
                                   {"function outer() { ", "}"},
                                   {nullptr, nullptr}};

  // clang-format off
  const char* error_data[] = {
    "var f = {x} => {};",
    "var f = {x,y} => {};",
    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, DefaultParametersYieldInInitializers) {
  // clang-format off
  const char* sloppy_function_context_data[][2] = {
    {"(function f(", ") { });"},
    {nullptr, nullptr}
  };

  const char* strict_function_context_data[][2] = {
    {"'use strict'; (function f(", ") { });"},
    {nullptr, nullptr}
  };

  const char* sloppy_arrow_context_data[][2] = {
    {"((", ")=>{});"},
    {nullptr, nullptr}
  };

  const char* strict_arrow_context_data[][2] = {
    {"'use strict'; ((", ")=>{});"},
    {nullptr, nullptr}
  };

  const char* generator_context_data[][2] = {
    {"'use strict'; (function *g(", ") { });"},
    {"(function *g(", ") { });"},
    // Arrow function within generator has the same rules.
    {"'use strict'; (function *g() { (", ") => {} });"},
    {"(function *g() { (", ") => {} });"},
    // And similarly for arrow functions in the parameter list.
    {"'use strict'; (function *g(z = (", ") => {}) { });"},
    {"(function *g(z = (", ") => {}) { });"},
    {nullptr, nullptr}
  };

  const char* parameter_data[] = {
    "x=yield",
    "x, y=yield",
    "{x=yield}",
    "[x=yield]",

    "x=(yield)",
    "x, y=(yield)",
    "{x=(yield)}",
    "[x=(yield)]",

    "x=f(yield)",
    "x, y=f(yield)",
    "{x=f(yield)}",
    "[x=f(yield)]",

    "{x}=yield",
    "[x]=yield",

    "{x}=(yield)",
    "[x]=(yield)",

    "{x}=f(yield)",
    "[x]=f(yield)",
    nullptr
  };

  // Because classes are always in strict mode, these are always errors.
  const char* always_error_param_data[] = {
    "x = class extends (yield) { }",
    "x = class extends f(yield) { }",
    "x = class extends (null, yield) { }",
    "x = class extends (a ? null : yield) { }",
    "[x] = [class extends (a ? null : yield) { }]",
    "[x = class extends (a ? null : yield) { }]",
    "[x = class extends (a ? null : yield) { }] = [null]",
    "x = class { [yield]() { } }",
    "x = class { static [yield]() { } }",
    "x = class { [(yield, 1)]() { } }",
    "x = class { [y = (yield, 1)]() { } }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(sloppy_function_context_data, parameter_data, kSuccess);
  RunParserSyncTest(sloppy_arrow_context_data, parameter_data, kSuccess);

  RunParserSyncTest(strict_function_context_data, parameter_data, kError);
  RunParserSyncTest(strict_arrow_context_data, parameter_data, kError);

  RunParserSyncTest(generator_context_data, parameter_data, kError);
  RunParserSyncTest(generator_context_data, always_error_param_data, kError);
}

TEST_F(ParsingTest, SpreadArray) {
  const char* context_data[][2] = {
      {"'use strict';", ""}, {"", ""}, {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "[...a]",
    "[a, ...b]",
    "[...a,]",
    "[...a, ,]",
    "[, ...a]",
    "[...a, ...b]",
    "[...a, , ...b]",
    "[...[...a]]",
    "[, ...a]",
    "[, , ...a]",
    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, SpreadArrayError) {
  const char* context_data[][2] = {
      {"'use strict';", ""}, {"", ""}, {nullptr, nullptr}};

  // clang-format off
  const char* data[] = {
    "[...]",
    "[a, ...]",
    "[..., ]",
    "[..., ...]",
    "[ (...a)]",
    nullptr};
  // clang-format on
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, NewTarget) {
  // clang-format off
  const char* good_context_data[][2] = {
    {"function f() {", "}"},
    {"'use strict'; function f() {", "}"},
    {"var f = function() {", "}"},
    {"'use strict'; var f = function() {", "}"},
    {"({m: function() {", "}})"},
    {"'use strict'; ({m: function() {", "}})"},
    {"({m() {", "}})"},
    {"'use strict'; ({m() {", "}})"},
    {"({get x() {", "}})"},
    {"'use strict'; ({get x() {", "}})"},
    {"({set x(_) {", "}})"},
    {"'use strict'; ({set x(_) {", "}})"},
    {"class C {m() {", "}}"},
    {"class C {get x() {", "}}"},
    {"class C {set x(_) {", "}}"},
    {nullptr}
  };

  const char* bad_context_data[][2] = {
    {"", ""},
    {"'use strict';", ""},
    {nullptr}
  };

  const char* data[] = {
    "new.target",
    "{ new.target }",
    "() => { new.target }",
    "() => new.target",
    "if (1) { new.target }",
    "if (1) {} else { new.target }",
    "while (0) { new.target }",
    "do { new.target } while (0)",
    nullptr
  };

  // clang-format on

  RunParserSyncTest(good_context_data, data, kSuccess);
  RunParserSyncTest(bad_context_data, data, kError);
}

TEST_F(ParsingTest, ImportMetaSuccess) {
  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {"'use strict';", ""},
    {"function f() {", "}"},
    {"'use strict'; function f() {", "}"},
    {"var f = function() {", "}"},
    {"'use strict'; var f = function() {", "}"},
    {"({m: function() {", "}})"},
    {"'use strict'; ({m: function() {", "}})"},
    {"({m() {", "}})"},
    {"'use strict'; ({m() {", "}})"},
    {"({get x() {", "}})"},
    {"'use strict'; ({get x() {", "}})"},
    {"({set x(_) {", "}})"},
    {"'use strict'; ({set x(_) {", "}})"},
    {"class C {m() {", "}}"},
    {"class C {get x() {", "}}"},
    {"class C {set x(_) {", "}}"},
    {nullptr}
  };

  const char* data[] = {
    "import.meta",
    "() => { import.meta }",
    "() => import.meta",
    "if (1) { import.meta }",
    "if (1) {} else { import.meta }",
    "while (0) { import.meta }",
    "do { import.meta } while (0)",
    "import.meta.url",
    "import.meta[0]",
    "import.meta.couldBeMutable = true",
    "import.meta()",
    "new import.meta.MagicClass",
    "new import.meta",
    "t = [...import.meta]",
    "f = {...import.meta}",
    "delete import.meta",
    nullptr
  };

  // clang-format on

  // 2.1.1 Static Semantics: Early Errors
  // ImportMeta
  // * It is an early Syntax Error if Module is not the syntactic goal symbol.
  RunParserSyncTest(context_data, data, kError);

  RunModuleParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ImportMetaFailure) {
  // clang-format off
  const char* context_data[][2] = {
    {"var ", ""},
    {"let ", ""},
    {"const ", ""},
    {"var [", "] = [1]"},
    {"([", "] = [1])"},
    {"({", "} = {1})"},
    {"var {", " = 1} = 1"},
    {"for (var ", " of [1]) {}"},
    {"(", ") => {}"},
    {"let f = ", " => {}"},
    {nullptr}
  };

  const char* data[] = {
    "import.meta",
    nullptr
  };

  // clang-format on

  RunParserSyncTest(context_data, data, kError);
  RunModuleParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ConstSloppy) {
  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {"{", "}"},
    {nullptr, nullptr}
  };

  const char* data[] = {
    "const x = 1",
    "for (const x = 1; x < 1; x++) {}",
    "for (const x in {}) {}",
    "for (const x of []) {}",
    nullptr
  };
  // clang-format on
  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, LetSloppy) {
  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {"'use strict';", ""},
    {"{", "}"},
    {nullptr, nullptr}
  };

  const char* data[] = {
    "let x",
    "let x = 1",
    "for (let x = 1; x < 1; x++) {}",
    "for (let x in {}) {}",
    "for (let x of []) {}",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, LanguageModeDirectivesNonSimpleParameterListErrors) {
  // TC39 deemed "use strict" directives to be an error when occurring in the
  // body of a function with non-simple parameter list, on 29/7/2015.
  // https://goo.gl/ueA7Ln
  const char* context_data[][2] = {
      {"function f(", ") { 'use strict'; }"},
      {"function* g(", ") { 'use strict'; }"},
      {"class c { foo(", ") { 'use strict' }"},
      {"var a = (", ") => { 'use strict'; }"},
      {"var o = { m(", ") { 'use strict'; }"},
      {"var o = { *gm(", ") { 'use strict'; }"},
      {"var c = { m(", ") { 'use strict'; }"},
      {"var c = { *gm(", ") { 'use strict'; }"},

      {"'use strict'; function f(", ") { 'use strict'; }"},
      {"'use strict'; function* g(", ") { 'use strict'; }"},
      {"'use strict'; class c { foo(", ") { 'use strict' }"},
      {"'use strict'; var a = (", ") => { 'use strict'; }"},
      {"'use strict'; var o = { m(", ") { 'use strict'; }"},
      {"'use strict'; var o = { *gm(", ") { 'use strict'; }"},
      {"'use strict'; var c = { m(", ") { 'use strict'; }"},
      {"'use strict'; var c = { *gm(", ") { 'use strict'; }"},

      {nullptr, nullptr}};

  const char* data[] = {
      // TODO(@caitp): support formal parameter initializers
      "{}",
      "[]",
      "[{}]",
      "{a}",
      "a, {b}",
      "a, b, {c, d, e}",
      "initializer = true",
      "a, b, c = 1",
      "...args",
      "a, b, ...rest",
      "[a, b, ...rest]",
      "{ bindingPattern = {} }",
      "{ initializedBindingPattern } = { initializedBindingPattern: true }",
      nullptr};

  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, LetSloppyOnly) {
  // clang-format off
  const char* context_data[][2] = {
    {"", ""},
    {"{", "}"},
    {"(function() {", "})()"},
    {nullptr, nullptr}
  };

  const char* data[] = {
    "let",
    "let = 1",
    "for (let = 1; let < 1; let++) {}",
    "for (let in {}) {}",
    "for (var let = 1; let < 1; let++) {}",
    "for (var let in {}) {}",
    "for (var [let] = 1; let < 1; let++) {}",
    "for (var [let] in {}) {}",
    "var let",
    "var [let] = []",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, data, kSuccess);

  // Some things should be rejected even in sloppy mode
  // This addresses BUG(v8:4403).

  // clang-format off
  const char* fail_data[] = {
    "let let = 1",
    "for (let let = 1; let < 1; let++) {}",
    "for (let let in {}) {}",
    "for (let let of []) {}",
    "const let = 1",
    "for (const let = 1; let < 1; let++) {}",
    "for (const let in {}) {}",
    "for (const let of []) {}",
    "let [let] = 1",
    "for (let [let] = 1; let < 1; let++) {}",
    "for (let [let] in {}) {}",
    "for (let [let] of []) {}",
    "const [let] = 1",
    "for (const [let] = 1; let < 1; let++) {}",
    "for (const [let] in {}) {}",
    "for (const [let] of []) {}",

    // Sprinkle in the escaped version too.
    "let l\\u0065t = 1",
    "const l\\u0065t = 1",
    "let [l\\u0065t] = 1",
    "const [l\\u0065t] = 1",
    "for (let l\\u0065t in {}) {}",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, fail_data, kError);
}

TEST_F(ParsingTest, EscapedKeywords) {
  // clang-format off
  const char* sloppy_context_data[][2] = {
    {"", ""},
    {nullptr, nullptr}
  };

  const char* strict_context_data[][2] = {
    {"'use strict';", ""},
    {nullptr, nullptr}
  };

  const char* fail_data[] = {
    "for (var i = 0; i < 100; ++i) { br\\u0065ak; }",
    "cl\\u0061ss Foo {}",
    "var x = cl\\u0061ss {}",
    "\\u0063onst foo = 1;",
    "while (i < 10) { if (i++ & 1) c\\u006fntinue; this.x++; }",
    "d\\u0065bugger;",
    "d\\u0065lete this.a;",
    "\\u0063o { } while(0)",
    "if (d\\u006f { true }) {}",
    "if (false) { this.a = 1; } \\u0065lse { this.b = 1; }",
    "e\\u0078port var foo;",
    "try { } catch (e) {} f\\u0069nally { }",
    "f\\u006fr (var i = 0; i < 10; ++i);",
    "f\\u0075nction fn() {}",
    "var f = f\\u0075nction() {}",
    "\\u0069f (true) { }",
    "\\u0069mport blah from './foo.js';",
    "n\\u0065w function f() {}",
    "(function() { r\\u0065turn; })()",
    "class C extends function() {} { constructor() { sup\\u0065r() } }",
    "class C extends function() {} { constructor() { sup\\u0065r.a = 1 } }",
    "sw\\u0069tch (this.a) {}",
    "var x = th\\u0069s;",
    "th\\u0069s.a = 1;",
    "thr\\u006fw 'boo';",
    "t\\u0072y { true } catch (e) {}",
    "var x = typ\\u0065of 'blah'",
    "v\\u0061r a = true",
    "var v\\u0061r = true",
    "(function() { return v\\u006fid 0; })()",
    "wh\\u0069le (true) { }",
    "w\\u0069th (this.scope) { }",
    "(function*() { y\\u0069eld 1; })()",
    "(function*() { var y\\u0069eld = 1; })()",

    "var \\u0065num = 1;",
    "var { \\u0065num } = {}",
    "(\\u0065num = 1);",

    // Null / Boolean literals
    "(x === n\\u0075ll);",
    "var x = n\\u0075ll;",
    "var n\\u0075ll = 1;",
    "var { n\\u0075ll } = { 1 };",
    "n\\u0075ll = 1;",
    "(x === tr\\u0075e);",
    "var x = tr\\u0075e;",
    "var tr\\u0075e = 1;",
    "var { tr\\u0075e } = {};",
    "tr\\u0075e = 1;",
    "(x === f\\u0061lse);",
    "var x = f\\u0061lse;",
    "var f\\u0061lse = 1;",
    "var { f\\u0061lse } = {};",
    "f\\u0061lse = 1;",

    // TODO(caitp): consistent error messages for labeled statements and
    // expressions
    "switch (this.a) { c\\u0061se 6: break; }",
    "try { } c\\u0061tch (e) {}",
    "switch (this.a) { d\\u0065fault: break; }",
    "class C \\u0065xtends function B() {} {}",
    "for (var a i\\u006e this) {}",
    "if ('foo' \\u0069n this) {}",
    "if (this \\u0069nstanceof Array) {}",
    "(n\\u0065w function f() {})",
    "(typ\\u0065of 123)",
    "(v\\u006fid 0)",
    "do { ; } wh\\u0069le (true) { }",
    "(function*() { return (n++, y\\u0069eld 1); })()",
    "class C { st\\u0061tic bar() {} }",
    "class C { st\\u0061tic *bar() {} }",
    "class C { st\\u0061tic get bar() {} }",
    "class C { st\\u0061tic set bar() {} }",
    "(async ()=>{\\u0061wait 100})()",
    "({\\u0067et get(){}})",
    "({\\u0073et set(){}})",
    "(async ()=>{var \\u0061wait = 100})()",
    "for (var x o\\u0066 [])",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(sloppy_context_data, fail_data, kError);
  RunParserSyncTest(strict_context_data, fail_data, kError);
  RunModuleParserSyncTest(sloppy_context_data, fail_data, kError);

  // clang-format off
  const char* let_data[] = {
    "var l\\u0065t = 1;",
    "l\\u0065t = 1;",
    "(l\\u0065t === 1);",
    "(y\\u0069eld);",
    "var y\\u0069eld = 1;",
    "var { y\\u0069eld } = {};",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(sloppy_context_data, let_data, kSuccess);
  RunParserSyncTest(strict_context_data, let_data, kError);

  // Non-errors in sloppy mode
  const char* valid_data[] = {"(\\u0069mplements = 1);",
                              "var impl\\u0065ments = 1;",
                              "var { impl\\u0065ments  } = {};",
                              "(\\u0069nterface = 1);",
                              "var int\\u0065rface = 1;",
                              "var { int\\u0065rface  } = {};",
                              "(p\\u0061ckage = 1);",
                              "var packa\\u0067e = 1;",
                              "var { packa\\u0067e  } = {};",
                              "(p\\u0072ivate = 1);",
                              "var p\\u0072ivate;",
                              "var { p\\u0072ivate } = {};",
                              "(prot\\u0065cted);",
                              "var prot\\u0065cted = 1;",
                              "var { prot\\u0065cted  } = {};",
                              "(publ\\u0069c);",
                              "var publ\\u0069c = 1;",
                              "var { publ\\u0069c } = {};",
                              "(st\\u0061tic);",
                              "var st\\u0061tic = 1;",
                              "var { st\\u0061tic } = {};",
                              nullptr};
  RunParserSyncTest(sloppy_context_data, valid_data, kSuccess);
  RunParserSyncTest(strict_context_data, valid_data, kError);
  RunModuleParserSyncTest(strict_context_data, valid_data, kError);
}

TEST_F(ParsingTest, MiscSyntaxErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "'use strict'", "" },
    { "", "" },
    { nullptr, nullptr }
  };
  const char* error_data[] = {
    "for (();;) {}",

    // crbug.com/582626
    "{ NaN ,chA((evarA=new t ( l = !.0[((... co -a0([1]))=> greturnkf",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, EscapeSequenceErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "'", "'" },
    { "\"", "\"" },
    { "`", "`" },
    { "`${'", "'}`" },
    { "`${\"", "\"}`" },
    { "`${`", "`}`" },
    { nullptr, nullptr }
  };
  const char* error_data[] = {
    "\\uABCG",
    "\\u{ZZ}",
    "\\u{FFZ}",
    "\\u{FFFFFFFFFF }",
    "\\u{110000}",
    "\\u{110000",
    "\\u{FFFD }",
    "\\xZF",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, NewTargetErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "'use strict'", "" },
    { "", "" },
    { nullptr, nullptr }
  };
  const char* error_data[] = {
    "var x = new.target",
    "function f() { return new.t\\u0061rget; }",
    nullptr
  };
  // clang-format on
  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, FunctionDeclarationError) {
  // clang-format off
  const char* strict_context[][2] = {
    { "'use strict';", "" },
    { "'use strict'; { ", "}" },
    {"(function() { 'use strict';", "})()"},
    {"(function() { 'use strict'; {", "} })()"},
    { nullptr, nullptr }
  };
  const char* sloppy_context[][2] = {
    { "", "" },
    { "{", "}" },
    {"(function() {", "})()"},
    {"(function() { {", "} })()"},
    { nullptr, nullptr }
  };
  // Invalid in all contexts
  const char* error_data[] = {
    "try function foo() {} catch (e) {}",
    "do function foo() {} while (0);",
    "for (;false;) function foo() {}",
    "for (var i = 0; i < 1; i++) function f() { };",
    "for (var x in {a: 1}) function f() { };",
    "for (var x in {}) function f() { };",
    "for (var x in {}) function foo() {}",
    "for (x in {a: 1}) function f() { };",
    "for (x in {}) function f() { };",
    "var x; for (x in {}) function foo() {}",
    "with ({}) function f() { };",
    "do label: function foo() {} while (0);",
    "for (;false;) label: function foo() {}",
    "for (var i = 0; i < 1; i++) label: function f() { };",
    "for (var x in {a: 1}) label: function f() { };",
    "for (var x in {}) label: function f() { };",
    "for (var x in {}) label: function foo() {}",
    "for (x in {a: 1}) label: function f() { };",
    "for (x in {}) label: function f() { };",
    "var x; for (x in {}) label: function foo() {}",
    "with ({}) label: function f() { };",
    "if (true) label: function f() {}",
    "if (true) {} else label: function f() {}",
    "if (true) function* f() { }",
    "label: function* f() { }",
    "if (true) async function f() { }",
    "label: async function f() { }",
    "if (true) async function* f() { }",
    "label: async function* f() { }",
    nullptr
  };
  // Valid only in sloppy mode.
  const char* sloppy_data[] = {
    "if (true) function foo() {}",
    "if (false) {} else function f() { };",
    "label: function f() { }",
    "label: if (true) function f() { }",
    "label: if (true) {} else function f() { }",
    "label: label2: function f() { }",
    nullptr
  };
  // clang-format on

  // Nothing parses in strict mode without a SyntaxError
  RunParserSyncTest(strict_context, error_data, kError);
  RunParserSyncTest(strict_context, sloppy_data, kError);

  // In sloppy mode, sloppy_data is successful
  RunParserSyncTest(sloppy_context, error_data, kError);
  RunParserSyncTest(sloppy_context, sloppy_data, kSuccess);
}

TEST_F(ParsingTest, ExponentiationOperator) {
  // clang-format off
  const char* context_data[][2] = {
    { "var O = { p: 1 }, x = 10; ; if (", ") { foo(); }" },
    { "var O = { p: 1 }, x = 10; ; (", ")" },
    { "var O = { p: 1 }, x = 10; foo(", ")" },
    { nullptr, nullptr }
  };
  const char* data[] = {
    "(delete O.p) ** 10",
    "(delete x) ** 10",
    "(~O.p) ** 10",
    "(~x) ** 10",
    "(!O.p) ** 10",
    "(!x) ** 10",
    "(+O.p) ** 10",
    "(+x) ** 10",
    "(-O.p) ** 10",
    "(-x) ** 10",
    "(typeof O.p) ** 10",
    "(typeof x) ** 10",
    "(void 0) ** 10",
    "(void O.p) ** 10",
    "(void x) ** 10",
    "++O.p ** 10",
    "++x ** 10",
    "--O.p ** 10",
    "--x ** 10",
    "O.p++ ** 10",
    "x++ ** 10",
    "O.p-- ** 10",
    "x-- ** 10",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, ExponentiationOperatorErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "var O = { p: 1 }, x = 10; ; if (", ") { foo(); }" },
    { "var O = { p: 1 }, x = 10; ; (", ")" },
    { "var O = { p: 1 }, x = 10; foo(", ")" },
    { nullptr, nullptr }
  };
  const char* error_data[] = {
    "delete O.p ** 10",
    "delete x ** 10",
    "~O.p ** 10",
    "~x ** 10",
    "!O.p ** 10",
    "!x ** 10",
    "+O.p ** 10",
    "+x ** 10",
    "-O.p ** 10",
    "-x ** 10",
    "typeof O.p ** 10",
    "typeof x ** 10",
    "void ** 10",
    "void O.p ** 10",
    "void x ** 10",
    "++delete O.p ** 10",
    "--delete O.p ** 10",
    "++~O.p ** 10",
    "++~x ** 10",
    "--!O.p ** 10",
    "--!x ** 10",
    "++-O.p ** 10",
    "++-x ** 10",
    "--+O.p ** 10",
    "--+x ** 10",
    "[ x ] **= [ 2 ]",
    "[ x **= 2 ] = [ 2 ]",
    "{ x } **= { x: 2 }",
    "{ x: x **= 2 ] = { x: 2 }",
    // TODO(caitp): a Call expression as LHS should be an early ReferenceError!
    // "Array() **= 10",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, error_data, kError);
}

TEST_F(ParsingTest, AsyncAwait) {
  // clang-format off
  const char* context_data[][2] = {
    { "'use strict';", "" },
    { "", "" },
    { nullptr, nullptr }
  };

  const char* data[] = {
    "var asyncFn = async function() { await 1; };",
    "var asyncFn = async function withName() { await 1; };",
    "var asyncFn = async () => await 'test';",
    "var asyncFn = async x => await x + 'test';",
    "async function asyncFn() { await 1; }",
    "var O = { async method() { await 1; } }",
    "var O = { async ['meth' + 'od']() { await 1; } }",
    "var O = { async 'method'() { await 1; } }",
    "var O = { async 0() { await 1; } }",
    "async function await() {}",

    "var asyncFn = async({ foo = 1 }) => foo;",
    "var asyncFn = async({ foo = 1 } = {}) => foo;",

    "function* g() { var f = async(yield); }",
    "function* g() { var f = async(x = yield); }",

    // v8:7817 assert that `await` is still allowed in the body of an arrow fn
    // within formal parameters
    "async(a = a => { var await = 1; return 1; }) => a()",
    "async(a = await => 1); async(a) => 1",
    "(async(a = await => 1), async(a) => 1)",
    "async(a = await => 1, b = async() => 1);",

    "async (x = class { p = await }) => {};",

    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, data, kSuccess);

  // clang-format off
  const char* async_body_context_data[][2] = {
    { "async function f() {", "}" },
    { "var f = async function() {", "}" },
    { "var f = async() => {", "}" },
    { "var O = { async method() {", "} }" },
    { "'use strict'; async function f() {", "}" },
    { "'use strict'; var f = async function() {", "}" },
    { "'use strict'; var f = async() => {", "}" },
    { "'use strict'; var O = { async method() {", "} }" },
    { nullptr, nullptr }
  };

  const char* body_context_data[][2] = {
    { "function f() {", "}" },
    { "function* g() {", "}" },
    { "var f = function() {", "}" },
    { "var g = function*() {", "}" },
    { "var O = { method() {", "} }" },
    { "var O = { *method() {", "} }" },
    { "var f = () => {", "}" },
    { "'use strict'; function f() {", "}" },
    { "'use strict'; function* g() {", "}" },
    { "'use strict'; var f = function() {", "}" },
    { "'use strict'; var g = function*() {", "}" },
    { "'use strict'; var O = { method() {", "} }" },
    { "'use strict'; var O = { *method() {", "} }" },
    { "'use strict'; var f = () => {", "}" },
    { nullptr, nullptr }
  };

  const char* body_data[] = {
    "var async = 1; return async;",
    "let async = 1; return async;",
    "const async = 1; return async;",
    "function async() {} return async();",
    "var async = async => async; return async();",
    "function foo() { var await = 1; return await; }",
    "function foo(await) { return await; }",
    "function* foo() { var await = 1; return await; }",
    "function* foo(await) { return await; }",
    "var f = () => { var await = 1; return await; }",
    "var O = { method() { var await = 1; return await; } };",
    "var O = { method(await) { return await; } };",
    "var O = { *method() { var await = 1; return await; } };",
    "var O = { *method(await) { return await; } };",
    "var asyncFn = async function*() {}",
    "async function* f() {}",
    "var O = { async *method() {} };",

    "(function await() {})",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(async_body_context_data, body_data, kSuccess);
  RunParserSyncTest(body_context_data, body_data, kSuccess);
}

TEST_F(ParsingTest, AsyncAwaitErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "'use strict';", "" },
    { "", "" },
    { nullptr, nullptr }
  };

  const char* strict_context_data[][2] = {
    { "'use strict';", "" },
    { nullptr, nullptr }
  };

  const char* error_data[] = {
    "var asyncFn = async function await() {};",
    "var asyncFn = async () => var await = 'test';",
    "var asyncFn = async await => await + 'test';",
    "var asyncFn = async function(await) {};",
    "var asyncFn = async (await) => 'test';",
    "async function f(await) {}",

    "var O = { async method(a, a) {} }",
    "var O = { async ['meth' + 'od'](a, a) {} }",
    "var O = { async 'method'(a, a) {} }",
    "var O = { async 0(a, a) {} }",

    "var f = async() => await;",

    "var O = { *async method() {} };",
    "var O = { async method*() {} };",

    "var asyncFn = async function(x = await 1) { return x; }",
    "async function f(x = await 1) { return x; }",
    "var f = async(x = await 1) => x;",
    "var O = { async method(x = await 1) { return x; } };",

    "function* g() { var f = async yield => 1; }",
    "function* g() { var f = async(yield) => 1; }",
    "function* g() { var f = async(x = yield) => 1; }",
    "function* g() { var f = async({x = yield}) => 1; }",

    "class C { async constructor() {} }",
    "class C {}; class C2 extends C { async constructor() {} }",
    "class C { static async prototype() {} }",
    "class C {}; class C2 extends C { static async prototype() {} }",

    "var f = async() => ((async(x = await 1) => x)();",

    // Henrique Ferreiro's bug (tm)
    "(async function foo1() { } foo2 => 1)",
    "(async function foo3() { } () => 1)",
    "(async function foo4() { } => 1)",
    "(async function() { } foo5 => 1)",
    "(async function() { } () => 1)",
    "(async function() { } => 1)",
    "(async.foo6 => 1)",
    "(async.foo7 foo8 => 1)",
    "(async.foo9 () => 1)",
    "(async().foo10 => 1)",
    "(async().foo11 foo12 => 1)",
    "(async().foo13 () => 1)",
    "(async['foo14'] => 1)",
    "(async['foo15'] foo16 => 1)",
    "(async['foo17'] () => 1)",
    "(async()['foo18'] => 1)",
    "(async()['foo19'] foo20 => 1)",
    "(async()['foo21'] () => 1)",
    "(async`foo22` => 1)",
    "(async`foo23` foo24 => 1)",
    "(async`foo25` () => 1)",
    "(async`foo26`.bar27 => 1)",
    "(async`foo28`.bar29 foo30 => 1)",
    "(async`foo31`.bar32 () => 1)",

    // v8:5148 assert that errors are still thrown for calls that may have been
    // async functions
    "async({ foo33 = 1 })",

    "async(...a = b) => b",
    "async(...a,) => b",
    "async(...a, b) => b",

    // v8:7817 assert that `await` is an invalid identifier in arrow formal
    // parameters nested within an async arrow function
    "async(a = await => 1) => a",
    "async(a = (await) => 1) => a",
    "async(a = (...await) => 1) => a",
    nullptr
  };

  const char* strict_error_data[] = {
    "var O = { async method(eval) {} }",
    "var O = { async ['meth' + 'od'](eval) {} }",
    "var O = { async 'method'(eval) {} }",
    "var O = { async 0(eval) {} }",

    "var O = { async method(arguments) {} }",
    "var O = { async ['meth' + 'od'](arguments) {} }",
    "var O = { async 'method'(arguments) {} }",
    "var O = { async 0(arguments) {} }",

    "var O = { async method(dupe, dupe) {} }",

    // TODO(caitp): preparser needs to report duplicate parameter errors, too.
    // "var f = async(dupe, dupe) => {}",

    nullptr
  };

  RunParserSyncTest(context_data, error_data, kError);
  RunParserSyncTest(strict_context_data, strict_error_data, kError);

  // clang-format off
  const char* async_body_context_data[][2] = {
    { "async function f() {", "}" },
    { "var f = async function() {", "}" },
    { "var f = async() => {", "}" },
    { "var O = { async method() {", "} }" },
    { "'use strict'; async function f() {", "}" },
    { "'use strict'; var f = async function() {", "}" },
    { "'use strict'; var f = async() => {", "}" },
    { "'use strict'; var O = { async method() {", "} }" },
    { nullptr, nullptr }
  };

  const char* async_body_error_data[] = {
    "var await = 1;",
    "var { await } = 1;",
    "var [ await ] = 1;",
    "return async (await) => {};",
    "var O = { async [await](a, a) {} }",
    "await;",

    "function await() {}",

    "var f = await => 42;",
    "var f = (await) => 42;",
    "var f = (await, a) => 42;",
    "var f = (...await) => 42;",

    "var e = (await);",
    "var e = (await, f);",
    "var e = (await = 42)",

    "var e = [await];",
    "var e = {await};",

    nullptr
  };
  // clang-format on

  RunParserSyncTest(async_body_context_data, async_body_error_data, kError);
}

TEST_F(ParsingTest, Regress7173) {
  // Await expression is an invalid destructuring target, and should not crash

  // clang-format off
  const char* error_context_data[][2] = {
    { "'use strict'; async function f() {", "}" },
    { "async function f() {", "}" },
    { "'use strict'; function f() {", "}" },
    { "function f() {", "}" },
    { "let f = async() => {", "}" },
    { "let f = () => {", "}" },
    { "'use strict'; async function* f() {", "}" },
    { "async function* f() {", "}" },
    { "'use strict'; function* f() {", "}" },
    { "function* f() {", "}" },
    { nullptr, nullptr }
  };

  const char* error_data[] = {
    "var [await f] = [];",
    "let [await f] = [];",
    "const [await f] = [];",

    "var [...await f] = [];",
    "let [...await f] = [];",
    "const [...await f] = [];",

    "var { await f } = {};",
    "let { await f } = {};",
    "const { await f } = {};",

    "var { ...await f } = {};",
    "let { ...await f } = {};",
    "const { ...await f } = {};",

    "var { f: await f } = {};",
    "let { f: await f } = {};",
    "const { f: await f } = {};"

    "var { f: ...await f } = {};",
    "let { f: ...await f } = {};",
    "const { f: ...await f } = {};"

    "var { [f]: await f } = {};",
    "let { [f]: await f } = {};",
    "const { [f]: await f } = {};",

    "var { [f]: ...await f } = {};",
    "let { [f]: ...await f } = {};",
    "const { [f]: ...await f } = {};",

    nullptr
  };
  // clang-format on

  RunParserSyncTest(error_context_data, error_data, kError);
}

TEST_F(ParsingTest, AsyncAwaitFormalParameters) {
  // clang-format off
  const char* context_for_formal_parameters[][2] = {
    { "async function f(", ") {}" },
    { "var f = async function f(", ") {}" },
    { "var f = async(", ") => {}" },
    { "'use strict'; async function f(", ") {}" },
    { "'use strict'; var f = async function f(", ") {}" },
    { "'use strict'; var f = async(", ") => {}" },
    { nullptr, nullptr }
  };

  const char* good_formal_parameters[] = {
    "x = function await() {}",
    "x = function *await() {}",
    "x = function() { let await = 0; }",
    "x = () => { let await = 0; }",
    nullptr
  };

  const char* bad_formal_parameters[] = {
    "{ await }",
    "{ await = 1 }",
    "{ await } = {}",
    "{ await = 1 } = {}",
    "[await]",
    "[await] = []",
    "[await = 1]",
    "[await = 1] = []",
    "...await",
    "await",
    "await = 1",
    "...[await]",
    "x = await",

    // v8:5190
    "1) => 1",
    "'str') => 1",
    "/foo/) => 1",
    "{ foo = async(1) => 1 }) => 1",
    "{ foo = async(a) => 1 })",

    "x = async(await)",
    "x = { [await]: 1 }",
    "x = class extends (await) { }",
    "x = class { static [await]() {} }",
    "{ x = await }",

    // v8:6714
    "x = class await {}",
    "x = 1 ? class await {} : 0",
    "x = async function await() {}",

    "x = y[await]",
    "x = `${await}`",
    "x = y()[await]",

    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_for_formal_parameters, good_formal_parameters,
                    kSuccess);

  RunParserSyncTest(context_for_formal_parameters, bad_formal_parameters,
                    kError);
}

TEST_F(ParsingTest, AsyncAwaitModule) {
  // clang-format off
  const char* context_data[][2] = {
    { "", "" },
    { nullptr, nullptr }
  };

  const char* data[] = {
    "export default async function() { await 1; }",
    "export default async function async() { await 1; }",
    "export async function async() { await 1; }",
    nullptr
  };
  // clang-format on

  RunModuleParserSyncTest(context_data, data, kSuccess, nullptr, 0, nullptr, 0,
                          nullptr, 0, false);
}

TEST_F(ParsingTest, AsyncAwaitModuleErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "", "" },
    { nullptr, nullptr }
  };

  const char* error_data[] = {
    "export default (async function await() {})",
    "export default async function await() {}",
    "export async function await() {}",
    "export async function() {}",
    "export async",
    "export async\nfunction async() { await 1; }",
    nullptr
  };
  // clang-format on

  RunModuleParserSyncTest(context_data, error_data, kError, nullptr, 0, nullptr,
                          0, nullptr, 0, false);
}

TEST_F(ParsingTest, RestrictiveForInErrors) {
  // clang-format off
  const char* strict_context_data[][2] = {
    { "'use strict'", "" },
    { nullptr, nullptr }
  };
  const char* sloppy_context_data[][2] = {
    { "", "" },
    { nullptr, nullptr }
  };
  const char* error_data[] = {
    "for (const x = 0 in {});",
    "for (let x = 0 in {});",
    nullptr
  };
  const char* sloppy_data[] = {
    "for (var x = 0 in {});",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(strict_context_data, error_data, kError);
  RunParserSyncTest(strict_context_data, sloppy_data, kError);
  RunParserSyncTest(sloppy_context_data, error_data, kError);
  RunParserSyncTest(sloppy_context_data, sloppy_data, kSuccess);
}

TEST_F(ParsingTest, NoDuplicateGeneratorsInBlock) {
  const char* block_context_data[][2] = {
      {"'use strict'; {", "}"},
      {"{", "}"},
      {"(function() { {", "} })()"},
      {"(function() {'use strict'; {", "} })()"},
      {nullptr, nullptr}};
  const char* top_level_context_data[][2] = {
      {"'use strict';", ""},
      {"", ""},
      {"(function() {", "})()"},
      {"(function() {'use strict';", "})()"},
      {nullptr, nullptr}};
  const char* error_data[] = {"function* x() {} function* x() {}",
                              "function x() {} function* x() {}",
                              "function* x() {} function x() {}", nullptr};
  // The preparser doesn't enforce the restriction, so turn it off.
  bool test_preparser = false;
  RunParserSyncTest(block_context_data, error_data, kError, nullptr, 0, nullptr,
                    0, nullptr, 0, false, test_preparser);
  RunParserSyncTest(top_level_context_data, error_data, kSuccess);
}

TEST_F(ParsingTest, NoDuplicateAsyncFunctionInBlock) {
  const char* block_context_data[][2] = {
      {"'use strict'; {", "}"},
      {"{", "}"},
      {"(function() { {", "} })()"},
      {"(function() {'use strict'; {", "} })()"},
      {nullptr, nullptr}};
  const char* top_level_context_data[][2] = {
      {"'use strict';", ""},
      {"", ""},
      {"(function() {", "})()"},
      {"(function() {'use strict';", "})()"},
      {nullptr, nullptr}};
  const char* error_data[] = {"async function x() {} async function x() {}",
                              "function x() {} async function x() {}",
                              "async function x() {} function x() {}",
                              "function* x() {} async function x() {}",
                              "function* x() {} async function x() {}",
                              "async function x() {} function* x() {}",
                              "function* x() {} async function x() {}",
                              nullptr};
  // The preparser doesn't enforce the restriction, so turn it off.
  bool test_preparser = false;
  RunParserSyncTest(block_context_data, error_data, kError, nullptr, 0, nullptr,
                    0, nullptr, 0, false, test_preparser);
  RunParserSyncTest(top_level_context_data, error_data, kSuccess);
}

TEST_F(ParsingTest, TrailingCommasInParameters) {
  // clang-format off
  const char* context_data[][2] = {
    { "", "" },
    { "'use strict';", "" },
    { "function foo() {", "}" },
    { "function foo() {'use strict';", "}" },
    { nullptr, nullptr }
  };

  const char* data[] = {
    " function  a(b,) {}",
    " function* a(b,) {}",
    "(function  a(b,) {});",
    "(function* a(b,) {});",
    "(function   (b,) {});",
    "(function*  (b,) {});",
    " function  a(b,c,d,) {}",
    " function* a(b,c,d,) {}",
    "(function  a(b,c,d,) {});",
    "(function* a(b,c,d,) {});",
    "(function   (b,c,d,) {});",
    "(function*  (b,c,d,) {});",
    "(b,) => {};",
    "(b,c,d,) => {};",
    "a(1,);",
    "a(1,2,3,);",
    "a(...[],);",
    "a(1, 2, ...[],);",
    "a(...[], 2, ...[],);",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, data, kSuccess);
}

TEST_F(ParsingTest, TrailingCommasInParametersErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "", "" },
    { "'use strict';", "" },
    { "function foo() {", "}" },
    { "function foo() {'use strict';", "}" },
    { nullptr, nullptr }
  };

  const char* data[] = {
    // too many trailing commas
    " function  a(b,,) {}",
    " function* a(b,,) {}",
    "(function  a(b,,) {});",
    "(function* a(b,,) {});",
    "(function   (b,,) {});",
    "(function*  (b,,) {});",
    " function  a(b,c,d,,) {}",
    " function* a(b,c,d,,) {}",
    "(function  a(b,c,d,,) {});",
    "(function* a(b,c,d,,) {});",
    "(function   (b,c,d,,) {});",
    "(function*  (b,c,d,,) {});",
    "(b,,) => {};",
    "(b,c,d,,) => {};",
    "a(1,,);",
    "a(1,2,3,,);",
    // only a trailing comma and no parameters
    " function  a1(,) {}",
    " function* a2(,) {}",
    "(function  a3(,) {});",
    "(function* a4(,) {});",
    "(function    (,) {});",
    "(function*   (,) {});",
    "(,) => {};",
    "a1(,);",
    // no trailing commas after rest parameter declaration
    " function  a(...b,) {}",
    " function* a(...b,) {}",
    "(function  a(...b,) {});",
    "(function* a(...b,) {});",
    "(function   (...b,) {});",
    "(function*  (...b,) {});",
    " function  a(b, c, ...d,) {}",
    " function* a(b, c, ...d,) {}",
    "(function  a(b, c, ...d,) {});",
    "(function* a(b, c, ...d,) {});",
    "(function   (b, c, ...d,) {});",
    "(function*  (b, c, ...d,) {});",
    "(...b,) => {};",
    "(b, c, ...d,) => {};",
    // parenthesized trailing comma without arrow is still an error
    "(,);",
    "(a,);",
    "(a,b,c,);",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ArgumentsRedeclaration) {
  {
    // clang-format off
    const char* context_data[][2] = {
      { "function f(", ") {}" },
      { nullptr, nullptr }
    };
    const char* success_data[] = {
      "{arguments}",
      "{arguments = false}",
      "arg1, arguments",
      "arg1, ...arguments",
      nullptr
    };
    // clang-format on
    RunParserSyncTest(context_data, success_data, kSuccess);
  }

  {
    // clang-format off
    const char* context_data[][2] = {
      { "function f() {", "}" },
      { nullptr, nullptr }
    };
    const char* data[] = {
      "const arguments = 1",
      "let arguments",
      "var arguments",
      nullptr
    };
    // clang-format on
    RunParserSyncTest(context_data, data, kSuccess);
  }
}

// Test that lazily parsed inner functions don't result in overly pessimistic
// context allocations.
TEST_F(ParsingTest, NoPessimisticContextAllocation) {
  i::Isolate* isolate = i_isolate();
  i::Factory* factory = isolate->factory();

  const char* prefix = "(function outer() { var my_var; ";
  const char* suffix = " })();";
  int prefix_len = Utf8LengthHelper(prefix);
  int suffix_len = Utf8LengthHelper(suffix);

  // Test both normal inner functions and inner arrow functions.
  const char* inner_functions[] = {"function inner(%s) { %s }",
                                   "(%s) => { %s }"};

  struct {
    const char* params;
    const char* source;
    bool ctxt_allocate;
  } inners[] = {
      // Context allocating because we need to:
      {"", "my_var;", true},
      {"", "if (true) { let my_var; } my_var;", true},
      {"", "eval('foo');", true},
      {"", "function inner2() { my_var; }", true},
      {"", "function inner2() { eval('foo'); }", true},
      {"", "var {my_var : a} = {my_var};", true},
      {"", "let {my_var : a} = {my_var};", true},
      {"", "const {my_var : a} = {my_var};", true},
      {"", "var [a, b = my_var] = [1, 2];", true},
      {"", "var [a, b = my_var] = [1, 2]; my_var;", true},
      {"", "let [a, b = my_var] = [1, 2];", true},
      {"", "let [a, b = my_var] = [1, 2]; my_var;", true},
      {"", "const [a, b = my_var] = [1, 2];", true},
      {"", "const [a, b = my_var] = [1, 2]; my_var;", true},
      {"", "var {a = my_var} = {}", true},
      {"", "var {a: b = my_var} = {}", true},
      {"", "let {a = my_var} = {}", true},
      {"", "let {a: b = my_var} = {}", true},
      {"", "const {a = my_var} = {}", true},
      {"", "const {a: b = my_var} = {}", true},
      {"a = my_var", "", true},
      {"a = my_var", "let my_var;", true},
      {"", "function inner2(a = my_var) { }", true},
      {"", "(a = my_var) => { }", true},
      {"{a} = {a: my_var}", "", true},
      {"", "function inner2({a} = {a: my_var}) { }", true},
      {"", "({a} = {a: my_var}) => { }", true},
      {"[a] = [my_var]", "", true},
      {"", "function inner2([a] = [my_var]) { }", true},
      {"", "([a] = [my_var]) => { }", true},
      {"", "function inner2(a = eval('')) { }", true},
      {"", "(a = eval('')) => { }", true},
      {"", "try { } catch (my_var) { } my_var;", true},
      {"", "for (my_var in {}) { my_var; }", true},
      {"", "for (my_var in {}) { }", true},
      {"", "for (my_var of []) { my_var; }", true},
      {"", "for (my_var of []) { }", true},
      {"", "for ([a, my_var, b] in {}) { my_var; }", true},
      {"", "for ([a, my_var, b] of []) { my_var; }", true},
      {"", "for ({x: my_var} in {}) { my_var; }", true},
      {"", "for ({x: my_var} of []) { my_var; }", true},
      {"", "for ({my_var} in {}) { my_var; }", true},
      {"", "for ({my_var} of []) { my_var; }", true},
      {"", "for ({y, x: my_var} in {}) { my_var; }", true},
      {"", "for ({y, x: my_var} of []) { my_var; }", true},
      {"", "for ({a, my_var} in {}) { my_var; }", true},
      {"", "for ({a, my_var} of []) { my_var; }", true},
      {"", "for (let my_var in {}) { } my_var;", true},
      {"", "for (let my_var of []) { } my_var;", true},
      {"", "for (let [a, my_var, b] in {}) { } my_var;", true},
      {"", "for (let [a, my_var, b] of []) { } my_var;", true},
      {"", "for (let {x: my_var} in {}) { } my_var;", true},
      {"", "for (let {x: my_var} of []) { } my_var;", true},
      {"", "for (let {my_var} in {}) { } my_var;", true},
      {"", "for (let {my_var} of []) { } my_var;", true},
      {"", "for (let {y, x: my_var} in {}) { } my_var;", true},
      {"", "for (let {y, x: my_var} of []) { } my_var;", true},
      {"", "for (let {a, my_var} in {}) { } my_var;", true},
      {"", "for (let {a, my_var} of []) { } my_var;", true},
      {"", "for (let my_var = 0; my_var < 1; ++my_var) { } my_var;", true},
      {"", "'use strict'; if (true) { function my_var() {} } my_var;", true},
      {"",
       "'use strict'; function inner2() { if (true) { function my_var() {} }  "
       "my_var; }",
       true},
      {"",
       "function inner2() { 'use strict'; if (true) { function my_var() {} }  "
       "my_var; }",
       true},
      {"",
       "() => { 'use strict'; if (true) { function my_var() {} }  my_var; }",
       true},
      {"",
       "if (true) { let my_var; if (true) { function my_var() {} } } my_var;",
       true},
      {"", "function inner2(a = my_var) {}", true},
      {"", "function inner2(a = my_var) { let my_var; }", true},
      {"", "(a = my_var) => {}", true},
      {"", "(a = my_var) => { let my_var; }", true},
      // No pessimistic context allocation:
      {"", "var my_var; my_var;", false},
      {"", "var my_var;", false},
      {"", "var my_var = 0;", false},
      {"", "if (true) { var my_var; } my_var;", false},
      {"", "let my_var; my_var;", false},
      {"", "let my_var;", false},
      {"", "let my_var = 0;", false},
      {"", "const my_var = 0; my_var;", false},
      {"", "const my_var = 0;", false},
      {"", "var [a, my_var] = [1, 2]; my_var;", false},
      {"", "let [a, my_var] = [1, 2]; my_var;", false},
      {"", "const [a, my_var] = [1, 2]; my_var;", false},
      {"", "var {a: my_var} = {a: 3}; my_var;", false},
      {"", "let {a: my_var} = {a: 3}; my_var;", false},
      {"", "const {a: my_var} = {a: 3}; my_var;", false},
      {"", "var {my_var} = {my_var: 3}; my_var;", false},
      {"", "let {my_var} = {my_var: 3}; my_var;", false},
      {"", "const {my_var} = {my_var: 3}; my_var;", false},
      {"my_var", "my_var;", false},
      {"my_var", "", false},
      {"my_var = 5", "my_var;", false},
      {"my_var = 5", "", false},
      {"...my_var", "my_var;", false},
      {"...my_var", "", false},
      {"[a, my_var, b]", "my_var;", false},
      {"[a, my_var, b]", "", false},
      {"[a, my_var, b] = [1, 2, 3]", "my_var;", false},
      {"[a, my_var, b] = [1, 2, 3]", "", false},
      {"{x: my_var}", "my_var;", false},
      {"{x: my_var}", "", false},
      {"{x: my_var} = {x: 0}", "my_var;", false},
      {"{x: my_var} = {x: 0}", "", false},
      {"{my_var}", "my_var;", false},
      {"{my_var}", "", false},
      {"{my_var} = {my_var: 0}", "my_var;", false},
      {"{my_var} = {my_var: 0}", "", false},
      {"", "function inner2(my_var) { my_var; }", false},
      {"", "function inner2(my_var) { }", false},
      {"", "function inner2(my_var = 5) { my_var; }", false},
      {"", "function inner2(my_var = 5) { }", false},
      {"", "function inner2(...my_var) { my_var; }", false},
      {"", "function inner2(...my_var) { }", false},
      {"", "function inner2([a, my_var, b]) { my_var; }", false},
      {"", "function inner2([a, my_var, b]) { }", false},
      {"", "function inner2([a, my_var, b] = [1, 2, 3]) { my_var; }", false},
      {"", "function inner2([a, my_var, b] = [1, 2, 3]) { }", false},
      {"", "function inner2({x: my_var}) { my_var; }", false},
      {"", "function inner2({x: my_var}) { }", false},
      {"", "function inner2({x: my_var} = {x: 0}) { my_var; }", false},
      {"", "function inner2({x: my_var} = {x: 0}) { }", false},
      {"", "function inner2({my_var}) { my_var; }", false},
      {"", "function inner2({my_var}) { }", false},
      {"", "function inner2({my_var} = {my_var: 8}) { my_var; } ", false},
      {"", "function inner2({my_var} = {my_var: 8}) { }", false},
      {"", "my_var => my_var;", false},
      {"", "my_var => { }", false},
      {"", "(my_var = 5) => my_var;", false},
      {"", "(my_var = 5) => { }", false},
      {"", "(...my_var) => my_var;", false},
      {"", "(...my_var) => { }", false},
      {"", "([a, my_var, b]) => my_var;", false},
      {"", "([a, my_var, b]) => { }", false},
      {"", "([a, my_var, b] = [1, 2, 3]) => my_var;", false},
      {"", "([a, my_var, b] = [1, 2, 3]) => { }", false},
      {"", "({x: my_var}) => my_var;", false},
      {"", "({x: my_var}) => { }", false},
      {"", "({x: my_var} = {x: 0}) => my_var;", false},
      {"", "({x: my_var} = {x: 0}) => { }", false},
      {"", "({my_var}) => my_var;", false},
      {"", "({my_var}) => { }", false},
      {"", "({my_var} = {my_var: 5}) => my_var;", false},
      {"", "({my_var} = {my_var: 5}) => { }", false},
      {"", "({a, my_var}) => my_var;", false},
      {"", "({a, my_var}) => { }", false},
      {"", "({a, my_var} = {a: 0, my_var: 5}) => my_var;", false},
      {"", "({a, my_var} = {a: 0, my_var: 5}) => { }", false},
      {"", "({y, x: my_var}) => my_var;", false},
      {"", "({y, x: my_var}) => { }", false},
      {"", "({y, x: my_var} = {y: 0, x: 0}) => my_var;", false},
      {"", "({y, x: my_var} = {y: 0, x: 0}) => { }", false},
      {"", "try { } catch (my_var) { my_var; }", false},
      {"", "try { } catch ([a, my_var, b]) { my_var; }", false},
      {"", "try { } catch ({x: my_var}) { my_var; }", false},
      {"", "try { } catch ({y, x: my_var}) { my_var; }", false},
      {"", "try { } catch ({my_var}) { my_var; }", false},
      {"", "for (let my_var in {}) { my_var; }", false},
      {"", "for (let my_var in {}) { }", false},
      {"", "for (let my_var of []) { my_var; }", false},
      {"", "for (let my_var of []) { }", false},
      {"", "for (let [a, my_var, b] in {}) { my_var; }", false},
      {"", "for (let [a, my_var, b] of []) { my_var; }", false},
      {"", "for (let {x: my_var} in {}) { my_var; }", false},
      {"", "for (let {x: my_var} of []) { my_var; }", false},
      {"", "for (let {my_var} in {}) { my_var; }", false},
      {"", "for (let {my_var} of []) { my_var; }", false},
      {"", "for (let {y, x: my_var} in {}) { my_var; }", false},
      {"", "for (let {y, x: my_var} of []) { my_var; }", false},
      {"", "for (let {a, my_var} in {}) { my_var; }", false},
      {"", "for (let {a, my_var} of []) { my_var; }", false},
      {"", "for (var my_var in {}) { my_var; }", false},
      {"", "for (var my_var in {}) { }", false},
      {"", "for (var my_var of []) { my_var; }", false},
      {"", "for (var my_var of []) { }", false},
      {"", "for (var [a, my_var, b] in {}) { my_var; }", false},
      {"", "for (var [a, my_var, b] of []) { my_var; }", false},
      {"", "for (var {x: my_var} in {}) { my_var; }", false},
      {"", "for (var {x: my_var} of []) { my_var; }", false},
      {"", "for (var {my_var} in {}) { my_var; }", false},
      {"", "for (var {my_var} of []) { my_var; }", false},
      {"", "for (var {y, x: my_var} in {}) { my_var; }", false},
      {"", "for (var {y, x: my_var} of []) { my_var; }", false},
      {"", "for (var {a, my_var} in {}) { my_var; }", false},
      {"", "for (var {a, my_var} of []) { my_var; }", false},
      {"", "for (var my_var in {}) { } my_var;", false},
      {"", "for (var my_var of []) { } my_var;", false},
      {"", "for (var [a, my_var, b] in {}) { } my_var;", false},
      {"", "for (var [a, my_var, b] of []) { } my_var;", false},
      {"", "for (var {x: my_var} in {}) { } my_var;", false},
      {"", "for (var {x: my_var} of []) { } my_var;", false},
      {"", "for (var {my_var} in {}) { } my_var;", false},
      {"", "for (var {my_var} of []) { } my_var;", false},
      {"", "for (var {y, x: my_var} in {}) { } my_var;", false},
      {"", "for (var {y, x: my_var} of []) { } my_var;", false},
      {"", "for (var {a, my_var} in {}) { } my_var;", false},
      {"", "for (var {a, my_var} of []) { } my_var;", false},
      {"", "for (let my_var = 0; my_var < 1; ++my_var) { my_var; }", false},
      {"", "for (var my_var = 0; my_var < 1; ++my_var) { my_var; }", false},
      {"", "for (var my_var = 0; my_var < 1; ++my_var) { } my_var; ", false},
      {"", "for (let a = 0, my_var = 0; my_var < 1; ++my_var) { my_var }",
       false},
      {"", "for (var a = 0, my_var = 0; my_var < 1; ++my_var) { my_var }",
       false},
      {"", "class my_var {}; my_var; ", false},
      {"", "function my_var() {} my_var;", false},
      {"", "if (true) { function my_var() {} }  my_var;", false},
      {"", "function inner2() { if (true) { function my_var() {} }  my_var; }",
       false},
      {"", "() => { if (true) { function my_var() {} }  my_var; }", false},
      {"",
       "if (true) { var my_var; if (true) { function my_var() {} } }  my_var;",
       false},
  };

  for (unsigned inner_ix = 0; inner_ix < arraysize(inner_functions);
       ++inner_ix) {
    const char* inner_function = inner_functions[inner_ix];
    int inner_function_len = Utf8LengthHelper(inner_function) - 4;

    for (unsigned i = 0; i < arraysize(inners); ++i) {
      int params_len = Utf8LengthHelper(inners[i].params);
      int source_len = Utf8LengthHelper(inners[i].source);
      int len = prefix_len + inner_function_len + params_len + source_len +
                suffix_len;

      base::ScopedVector<char> program(len + 1);
      base::SNPrintF(program, "%s", prefix);
      base::SNPrintF(program + prefix_len, inner_function, inners[i].params,
                     inners[i].source);
      base::SNPrintF(
          program + prefix_len + inner_function_len + params_len + source_len,
          "%s", suffix);

      i::Handle<i::String> source =
          factory->InternalizeUtf8String(program.begin());
      source->PrintOn(stdout);
      printf("\n");

      i::Handle<i::Script> script = factory->NewScript(source);
      i::UnoptimizedCompileState compile_state;
      i::ReusableUnoptimizedCompileState reusable_state(isolate);
      i::UnoptimizedCompileFlags flags =
          i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
      i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);

      CHECK_PARSE_PROGRAM(&info, script, isolate);

      i::Scope* scope = info.literal()->scope()->inner_scope();
      DCHECK_NOT_NULL(scope);
      DCHECK_NULL(scope->sibling());
      DCHECK(scope->is_function_scope());
      const i::AstRawString* var_name =
          info.ast_value_factory()->GetOneByteString("my_var");
      i::Variable* var = scope->LookupForTesting(var_name);
      CHECK_EQ(inners[i].ctxt_allocate,
               i::ScopeTestHelper::MustAllocateInContext(var));
    }
  }
}

TEST_F(ParsingTest, EscapedStrictReservedWord) {
  // Test that identifiers which are both escaped and only reserved in the
  // strict mode are accepted in non-strict mode.
  const char* context_data[][2] = {{"", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {"if (true) l\\u0065t: ;",
                                  "function l\\u0065t() { }",
                                  "(function l\\u0065t() { })",
                                  "async function l\\u0065t() { }",
                                  "(async function l\\u0065t() { })",
                                  "l\\u0065t => 42",
                                  "async l\\u0065t => 42",
                                  "function packag\\u0065() {}",
                                  "function impl\\u0065ments() {}",
                                  "function privat\\u0065() {}",
                                  nullptr};

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, ForAwaitOf) {
  // clang-format off
  const char* context_data[][2] = {
    { "async function f() { for await ", " ; }" },
    { "async function f() { for await ", " { } }" },
    { "async function * f() { for await ", " { } }" },
    { "async function f() { 'use strict'; for await ", " ; }" },
    { "async function f() { 'use strict'; for await ", "  { } }" },
    { "async function * f() { 'use strict'; for await ", "  { } }" },
    { "async function f() { for\nawait ", " ; }" },
    { "async function f() { for\nawait ", " { } }" },
    { "async function * f() { for\nawait ", " { } }" },
    { "async function f() { 'use strict'; for\nawait ", " ; }" },
    { "async function f() { 'use strict'; for\nawait ", " { } }" },
    { "async function * f() { 'use strict'; for\nawait ", " { } }" },
    { "async function f() { for await\n", " ; }" },
    { "async function f() { for await\n", " { } }" },
    { "async function * f() { for await\n", " { } }" },
    { "async function f() { 'use strict'; for await\n", " ; }" },
    { "async function f() { 'use strict'; for await\n", " { } }" },
    { "async function * f() { 'use strict'; for await\n", " { } }" },
    { nullptr, nullptr }
  };

  const char* context_data2[][2] = {
    { "async function f() { let a; for await ", " ; }" },
    { "async function f() { let a; for await ", " { } }" },
    { "async function * f() { let a; for await ", " { } }" },
    { "async function f() { 'use strict'; let a; for await ", " ; }" },
    { "async function f() { 'use strict'; let a; for await ", "  { } }" },
    { "async function * f() { 'use strict'; let a; for await ", "  { } }" },
    { "async function f() { let a; for\nawait ", " ; }" },
    { "async function f() { let a; for\nawait ", " { } }" },
    { "async function * f() { let a; for\nawait ", " { } }" },
    { "async function f() { 'use strict'; let a; for\nawait ", " ; }" },
    { "async function f() { 'use strict'; let a; for\nawait ", " { } }" },
    { "async function * f() { 'use strict'; let a; for\nawait ", " { } }" },
    { "async function f() { let a; for await\n", " ; }" },
    { "async function f() { let a; for await\n", " { } }" },
    { "async function * f() { let a; for await\n", " { } }" },
    { "async function f() { 'use strict'; let a; for await\n", " ; }" },
    { "async function f() { 'use strict'; let a; for await\n", " { } }" },
    { "async function * f() { 'use strict'; let a; for await\n", " { } }" },
    { nullptr, nullptr }
  };

  const char* expr_data[] = {
    // Primary Expressions
    "(a of [])",
    "(a.b of [])",
    "([a] of [])",
    "([a = 1] of [])",
    "([a = 1, ...b] of [])",
    "({a} of [])",
    "({a: a} of [])",
    "({'a': a} of [])",
    "({\"a\": a} of [])",
    "({[Symbol.iterator]: a} of [])",
    "({0: a} of [])",
    "({a = 1} of [])",
    "({a: a = 1} of [])",
    "({'a': a = 1} of [])",
    "({\"a\": a = 1} of [])",
    "({[Symbol.iterator]: a = 1} of [])",
    "({0: a = 1} of [])",
    nullptr
  };

  const char* var_data[] = {
    // VarDeclarations
    "(var a of [])",
    "(var [a] of [])",
    "(var [a = 1] of [])",
    "(var [a = 1, ...b] of [])",
    "(var {a} of [])",
    "(var {a: a} of [])",
    "(var {'a': a} of [])",
    "(var {\"a\": a} of [])",
    "(var {[Symbol.iterator]: a} of [])",
    "(var {0: a} of [])",
    "(var {a = 1} of [])",
    "(var {a: a = 1} of [])",
    "(var {'a': a = 1} of [])",
    "(var {\"a\": a = 1} of [])",
    "(var {[Symbol.iterator]: a = 1} of [])",
    "(var {0: a = 1} of [])",
    nullptr
  };

  const char* lexical_data[] = {
    // LexicalDeclartions
    "(let a of [])",
    "(let [a] of [])",
    "(let [a = 1] of [])",
    "(let [a = 1, ...b] of [])",
    "(let {a} of [])",
    "(let {a: a} of [])",
    "(let {'a': a} of [])",
    "(let {\"a\": a} of [])",
    "(let {[Symbol.iterator]: a} of [])",
    "(let {0: a} of [])",
    "(let {a = 1} of [])",
    "(let {a: a = 1} of [])",
    "(let {'a': a = 1} of [])",
    "(let {\"a\": a = 1} of [])",
    "(let {[Symbol.iterator]: a = 1} of [])",
    "(let {0: a = 1} of [])",

    "(const a of [])",
    "(const [a] of [])",
    "(const [a = 1] of [])",
    "(const [a = 1, ...b] of [])",
    "(const {a} of [])",
    "(const {a: a} of [])",
    "(const {'a': a} of [])",
    "(const {\"a\": a} of [])",
    "(const {[Symbol.iterator]: a} of [])",
    "(const {0: a} of [])",
    "(const {a = 1} of [])",
    "(const {a: a = 1} of [])",
    "(const {'a': a = 1} of [])",
    "(const {\"a\": a = 1} of [])",
    "(const {[Symbol.iterator]: a = 1} of [])",
    "(const {0: a = 1} of [])",
    nullptr
  };
  // clang-format on
  RunParserSyncTest(context_data, expr_data, kSuccess);
  RunParserSyncTest(context_data2, expr_data, kSuccess);

  RunParserSyncTest(context_data, var_data, kSuccess);
  // TODO(marja): PreParser doesn't report early errors.
  //              (https://bugs.chromium.org/p/v8/issues/detail?id=2728)
  // RunParserSyncTest(context_data2, var_data, kError, nullptr, 0,
  // always_flags,
  //                   arraysize(always_flags));

  RunParserSyncTest(context_data, lexical_data, kSuccess);
  RunParserSyncTest(context_data2, lexical_data, kSuccess);
}

TEST_F(ParsingTest, ForAwaitOfErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "async function f() { for await ", " ; }" },
    { "async function f() { for await ", " { } }" },
    { "async function f() { 'use strict'; for await ", " ; }" },
    { "async function f() { 'use strict'; for await ", "  { } }" },
    { "async function * f() { for await ", " ; }" },
    { "async function * f() { for await ", " { } }" },
    { "async function * f() { 'use strict'; for await ", " ; }" },
    { "async function * f() { 'use strict'; for await ", "  { } }" },
    { nullptr, nullptr }
  };

  const char* data[] = {
    // Primary Expressions
    "(a = 1 of [])",
    "(a = 1) of [])",
    "(a.b = 1 of [])",
    "((a.b = 1) of [])",
    "([a] = 1 of [])",
    "(([a] = 1) of [])",
    "([a = 1] = 1 of [])",
    "(([a = 1] = 1) of [])",
    "([a = 1 = 1, ...b] = 1 of [])",
    "(([a = 1 = 1, ...b] = 1) of [])",
    "({a} = 1 of [])",
    "(({a} = 1) of [])",
    "({a: a} = 1 of [])",
    "(({a: a} = 1) of [])",
    "({'a': a} = 1 of [])",
    "(({'a': a} = 1) of [])",
    "({\"a\": a} = 1 of [])",
    "(({\"a\": a} = 1) of [])",
    "({[Symbol.iterator]: a} = 1 of [])",
    "(({[Symbol.iterator]: a} = 1) of [])",
    "({0: a} = 1 of [])",
    "(({0: a} = 1) of [])",
    "({a = 1} = 1 of [])",
    "(({a = 1} = 1) of [])",
    "({a: a = 1} = 1 of [])",
    "(({a: a = 1} = 1) of [])",
    "({'a': a = 1} = 1 of [])",
    "(({'a': a = 1} = 1) of [])",
    "({\"a\": a = 1} = 1 of [])",
    "(({\"a\": a = 1} = 1) of [])",
    "({[Symbol.iterator]: a = 1} = 1 of [])",
    "(({[Symbol.iterator]: a = 1} = 1) of [])",
    "({0: a = 1} = 1 of [])",
    "(({0: a = 1} = 1) of [])",
    "(function a() {} of [])",
    "([1] of [])",
    "({a: 1} of [])"

    // VarDeclarations
    "(var a = 1 of [])",
    "(var a, b of [])",
    "(var [a] = 1 of [])",
    "(var [a], b of [])",
    "(var [a = 1] = 1 of [])",
    "(var [a = 1], b of [])",
    "(var [a = 1 = 1, ...b] of [])",
    "(var [a = 1, ...b], c of [])",
    "(var {a} = 1 of [])",
    "(var {a}, b of [])",
    "(var {a: a} = 1 of [])",
    "(var {a: a}, b of [])",
    "(var {'a': a} = 1 of [])",
    "(var {'a': a}, b of [])",
    "(var {\"a\": a} = 1 of [])",
    "(var {\"a\": a}, b of [])",
    "(var {[Symbol.iterator]: a} = 1 of [])",
    "(var {[Symbol.iterator]: a}, b of [])",
    "(var {0: a} = 1 of [])",
    "(var {0: a}, b of [])",
    "(var {a = 1} = 1 of [])",
    "(var {a = 1}, b of [])",
    "(var {a: a = 1} = 1 of [])",
    "(var {a: a = 1}, b of [])",
    "(var {'a': a = 1} = 1 of [])",
    "(var {'a': a = 1}, b of [])",
    "(var {\"a\": a = 1} = 1 of [])",
    "(var {\"a\": a = 1}, b of [])",
    "(var {[Symbol.iterator]: a = 1} = 1 of [])",
    "(var {[Symbol.iterator]: a = 1}, b of [])",
    "(var {0: a = 1} = 1 of [])",
    "(var {0: a = 1}, b of [])",

    // LexicalDeclartions
    "(let a = 1 of [])",
    "(let a, b of [])",
    "(let [a] = 1 of [])",
    "(let [a], b of [])",
    "(let [a = 1] = 1 of [])",
    "(let [a = 1], b of [])",
    "(let [a = 1, ...b] = 1 of [])",
    "(let [a = 1, ...b], c of [])",
    "(let {a} = 1 of [])",
    "(let {a}, b of [])",
    "(let {a: a} = 1 of [])",
    "(let {a: a}, b of [])",
    "(let {'a': a} = 1 of [])",
    "(let {'a': a}, b of [])",
    "(let {\"a\": a} = 1 of [])",
    "(let {\"a\": a}, b of [])",
    "(let {[Symbol.iterator]: a} = 1 of [])",
    "(let {[Symbol.iterator]: a}, b of [])",
    "(let {0: a} = 1 of [])",
    "(let {0: a}, b of [])",
    "(let {a = 1} = 1 of [])",
    "(let {a = 1}, b of [])",
    "(let {a: a = 1} = 1 of [])",
    "(let {a: a = 1}, b of [])",
    "(let {'a': a = 1} = 1 of [])",
    "(let {'a': a = 1}, b of [])",
    "(let {\"a\": a = 1} = 1 of [])",
    "(let {\"a\": a = 1}, b of [])",
    "(let {[Symbol.iterator]: a = 1} = 1 of [])",
    "(let {[Symbol.iterator]: a = 1}, b of [])",
    "(let {0: a = 1} = 1 of [])",
    "(let {0: a = 1}, b of [])",

    "(const a = 1 of [])",
    "(const a, b of [])",
    "(const [a] = 1 of [])",
    "(const [a], b of [])",
    "(const [a = 1] = 1 of [])",
    "(const [a = 1], b of [])",
    "(const [a = 1, ...b] = 1 of [])",
    "(const [a = 1, ...b], b of [])",
    "(const {a} = 1 of [])",
    "(const {a}, b of [])",
    "(const {a: a} = 1 of [])",
    "(const {a: a}, b of [])",
    "(const {'a': a} = 1 of [])",
    "(const {'a': a}, b of [])",
    "(const {\"a\": a} = 1 of [])",
    "(const {\"a\": a}, b of [])",
    "(const {[Symbol.iterator]: a} = 1 of [])",
    "(const {[Symbol.iterator]: a}, b of [])",
    "(const {0: a} = 1 of [])",
    "(const {0: a}, b of [])",
    "(const {a = 1} = 1 of [])",
    "(const {a = 1}, b of [])",
    "(const {a: a = 1} = 1 of [])",
    "(const {a: a = 1}, b of [])",
    "(const {'a': a = 1} = 1 of [])",
    "(const {'a': a = 1}, b of [])",
    "(const {\"a\": a = 1} = 1 of [])",
    "(const {\"a\": a = 1}, b of [])",
    "(const {[Symbol.iterator]: a = 1} = 1 of [])",
    "(const {[Symbol.iterator]: a = 1}, b of [])",
    "(const {0: a = 1} = 1 of [])",
    "(const {0: a = 1}, b of [])",

    nullptr
  };
  // clang-format on
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, ForAwaitOfFunctionDeclaration) {
  // clang-format off
  const char* context_data[][2] = {
    { "async function f() {", "}" },
    { "async function f() { 'use strict'; ", "}" },
    { nullptr, nullptr }
  };

  const char* data[] = {
    "for await (x of []) function d() {};",
    "for await (x of []) function d() {}; return d;",
    "for await (x of []) function* g() {};",
    "for await (x of []) function* g() {}; return g;",
    // TODO(caitp): handle async function declarations in ParseScopedStatement.
    // "for await (x of []) async function a() {};",
    // "for await (x of []) async function a() {}; return a;",
    nullptr
  };

  // clang-format on
  RunParserSyncTest(context_data, data, kError);
}

TEST_F(ParsingTest, AsyncGenerator) {
  // clang-format off
  const char* context_data[][2] = {
    { "async function * gen() {", "}" },
    { "(async function * gen() {", "})" },
    { "(async function * () {", "})" },
    { "({ async * gen () {", "} })" },
    { nullptr, nullptr }
  };

  const char* statement_data[] = {
    // An async generator without a body is valid.
    ""
    // Valid yield expressions inside generators.
    "yield 2;",
    "yield * 2;",
    "yield * \n 2;",
    "yield yield 1;",
    "yield * yield * 1;",
    "yield 3 + (yield 4);",
    "yield * 3 + (yield * 4);",
    "(yield * 3) + (yield * 4);",
    "yield 3; yield 4;",
    "yield * 3; yield * 4;",
    "(function (yield) { })",
    "(function yield() { })",
    "(function (await) { })",
    "(function await() { })",
    "yield { yield: 12 }",
    "yield /* comment */ { yield: 12 }",
    "yield * \n { yield: 12 }",
    "yield /* comment */ * \n { yield: 12 }",
    // You can return in an async generator.
    "yield 1; return",
    "yield * 1; return",
    "yield 1; return 37",
    "yield * 1; return 37",
    "yield 1; return 37; yield 'dead';",
    "yield * 1; return 37; yield * 'dead';",
    // Yield/Await are still a valid key in object literals.
    "({ yield: 1 })",
    "({ get yield() { } })",
    "({ await: 1 })",
    "({ get await() { } })",
    // And in assignment pattern computed properties
    "({ [yield]: x } = { })",
    "({ [await 1]: x } = { })",
    // Yield without RHS.
    "yield;",
    "yield",
    "yield\n",
    "yield /* comment */"
    "yield // comment\n"
    "(yield)",
    "[yield]",
    "{yield}",
    "yield, yield",
    "yield; yield",
    "(yield) ? yield : yield",
    "(yield) \n ? yield : yield",
    // If there is a newline before the next token, we don't look for RHS.
    "yield\nfor (;;) {}",
    "x = class extends (yield) {}",
    "x = class extends f(yield) {}",
    "x = class extends (null, yield) { }",
    "x = class extends (a ? null : yield) { }",
    "x = class extends (await 10) {}",
    "x = class extends f(await 10) {}",
    "x = class extends (null, await 10) { }",
    "x = class extends (a ? null : await 10) { }",

    // More tests featuring AwaitExpressions
    "await 10",
    "await 10; return",
    "await 10; return 20",
    "await 10; return 20; yield 'dead'",
    "await (yield 10)",
    "await (yield 10); return",
    "await (yield 10); return 20",
    "await (yield 10); return 20; yield 'dead'",
    "yield await 10",
    "yield await 10; return",
    "yield await 10; return 20",
    "yield await 10; return 20; yield 'dead'",
    "await /* comment */ 10",
    "await // comment\n 10",
    "yield await /* comment\n */ 10",
    "yield await // comment\n 10",
    "await (yield /* comment */)",
    "await (yield // comment\n)",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kSuccess);
}

TEST_F(ParsingTest, AsyncGeneratorErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "async function * gen() {", "}" },
    { "\"use strict\"; async function * gen() {", "}" },
    { nullptr, nullptr }
  };

  const char* statement_data[] = {
    // Invalid yield expressions inside generators.
    "var yield;",
    "var await;",
    "var foo, yield;",
    "var foo, await;",
    "try { } catch (yield) { }",
    "try { } catch (await) { }",
    "function yield() { }",
    "function await() { }",
    // The name of the NFE is bound in the generator, which does not permit
    // yield or await to be identifiers.
    "(async function * yield() { })",
    "(async function * await() { })",
    // Yield and Await aren't valid as a formal parameter for generators.
    "async function * foo(yield) { }",
    "(async function * foo(yield) { })",
    "async function * foo(await) { }",
    "(async function * foo(await) { })",
    "yield = 1;",
    "await = 1;",
    "var foo = yield = 1;",
    "var foo = await = 1;",
    "++yield;",
    "++await;",
    "yield++;",
    "await++;",
    "yield *",
    "(yield *)",
    // Yield binds very loosely, so this parses as "yield (3 + yield 4)", which
    // is invalid.
    "yield 3 + yield 4;",
    "yield: 34",
    "yield ? 1 : 2",
    // Parses as yield (/ yield): invalid.
    "yield / yield",
    "+ yield",
    "+ yield 3",
    // Invalid (no newline allowed between yield and *).
    "yield\n*3",
    // Invalid (we see a newline, so we parse {yield:42} as a statement, not an
    // object literal, and yield is not a valid label).
    "yield\n{yield: 42}",
    "yield /* comment */\n {yield: 42}",
    "yield //comment\n {yield: 42}",
    // Destructuring binding and assignment are both disallowed
    "var [yield] = [42];",
    "var [await] = [42];",
    "var {foo: yield} = {a: 42};",
    "var {foo: await} = {a: 42};",
    "[yield] = [42];",
    "[await] = [42];",
    "({a: yield} = {a: 42});",
    "({a: await} = {a: 42});",
    // Also disallow full yield/await expressions on LHS
    "var [yield 24] = [42];",
    "var [await 24] = [42];",
    "var {foo: yield 24} = {a: 42};",
    "var {foo: await 24} = {a: 42};",
    "[yield 24] = [42];",
    "[await 24] = [42];",
    "({a: yield 24} = {a: 42});",
    "({a: await 24} = {a: 42});",
    "for (yield 'x' in {});",
    "for (await 'x' in {});",
    "for (yield 'x' of {});",
    "for (await 'x' of {});",
    "for (yield 'x' in {} in {});",
    "for (await 'x' in {} in {});",
    "for (yield 'x' in {} of {});",
    "for (await 'x' in {} of {});",
    "class C extends yield { }",
    "class C extends await { }",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, LexicalLoopVariable) {
  i::Isolate* isolate = i_isolate();

  using TestCB =
      std::function<void(const i::ParseInfo& info, i::DeclarationScope*)>;
  auto TestProgram = [isolate](const char* program, TestCB test) {
    i::Factory* const factory = isolate->factory();
    i::Handle<i::String> source =
        factory->NewStringFromUtf8(base::CStrVector(program)).ToHandleChecked();
    i::Handle<i::Script> script = factory->NewScript(source);
    i::UnoptimizedCompileState compile_state;
    i::ReusableUnoptimizedCompileState reusable_state(isolate);
    i::UnoptimizedCompileFlags flags =
        i::UnoptimizedCompileFlags::ForScriptCompile(isolate, *script);
    flags.set_allow_lazy_parsing(false);
    i::ParseInfo info(isolate, flags, &compile_state, &reusable_state);
    CHECK_PARSE_PROGRAM(&info, script, isolate);

    i::DeclarationScope::AllocateScopeInfos(&info, isolate);
    CHECK_NOT_NULL(info.literal());

    i::DeclarationScope* script_scope = info.literal()->scope();
    CHECK(script_scope->is_script_scope());

    test(info, script_scope);
  };

  // Check `let` loop variables is a stack local when not captured by
  // an eval or closure within the area of the loop body.
  const char* local_bindings[] = {
      "function loop() {"
      "  for (let loop_var = 0; loop_var < 10; ++loop_var) {"
      "  }"
      "  eval('0');"
      "}",

      "function loop() {"
      "  for (let loop_var = 0; loop_var < 10; ++loop_var) {"
      "  }"
      "  function foo() {}"
      "  foo();"
      "}",
  };
  for (const char* source : local_bindings) {
    TestProgram(source, [=](const i::ParseInfo& info, i::DeclarationScope* s) {
      i::Scope* fn = s->inner_scope();
      CHECK(fn->is_function_scope());

      i::Scope* loop_block = fn->inner_scope();
      if (loop_block->is_function_scope()) loop_block = loop_block->sibling();
      CHECK(loop_block->is_block_scope());

      const i::AstRawString* var_name =
          info.ast_value_factory()->GetOneByteString("loop_var");
      i::Variable* loop_var = loop_block->LookupLocal(var_name);
      CHECK_NOT_NULL(loop_var);
      CHECK(loop_var->IsStackLocal());
      CHECK_EQ(loop_block->ContextLocalCount(), 0);
      CHECK_NULL(loop_block->inner_scope());
    });
  }

  // Check `let` loop variable is not a stack local, and is duplicated in the
  // loop body to ensure capturing can work correctly.
  // In this version of the test, the inner loop block's duplicate `loop_var`
  // binding is not captured, and is a local.
  const char* context_bindings1[] = {
      "function loop() {"
      "  for (let loop_var = eval('0'); loop_var < 10; ++loop_var) {"
      "  }"
      "}",

      "function loop() {"
      "  for (let loop_var = (() => (loop_var, 0))(); loop_var < 10;"
      "       ++loop_var) {"
      "  }"
      "}"};
  for (const char* source : context_bindings1) {
    TestProgram(source, [=](const i::ParseInfo& info, i::DeclarationScope* s) {
      i::Scope* fn = s->inner_scope();
      CHECK(fn->is_function_scope());

      i::Scope* loop_block = fn->inner_scope();
      CHECK(loop_block->is_block_scope());

      const i::AstRawString* var_name =
          info.ast_value_factory()->GetOneByteString("loop_var");
      i::Variable* loop_var = loop_block->LookupLocal(var_name);
      CHECK_NOT_NULL(loop_var);
      CHECK(loop_var->IsContextSlot());
      CHECK_EQ(loop_block->ContextLocalCount(), 1);

      i::Variable* loop_var2 = loop_block->inner_scope()->LookupLocal(var_name);
      CHECK_NE(loop_var, loop_var2);
      CHECK(loop_var2->IsStackLocal());
      CHECK_EQ(loop_block->inner_scope()->ContextLocalCount(), 0);
    });
  }

  // Check `let` loop variable is not a stack local, and is duplicated in the
  // loop body to ensure capturing can work correctly.
  // In this version of the test, the inner loop block's duplicate `loop_var`
  // binding is captured, and must be context allocated.
  const char* context_bindings2[] = {
      "function loop() {"
      "  for (let loop_var = 0; loop_var < 10; ++loop_var) {"
      "    eval('0');"
      "  }"
      "}",

      "function loop() {"
      "  for (let loop_var = 0; loop_var < eval('10'); ++loop_var) {"
      "  }"
      "}",

      "function loop() {"
      "  for (let loop_var = 0; loop_var < 10; eval('++loop_var')) {"
      "  }"
      "}",
  };

  for (const char* source : context_bindings2) {
    TestProgram(source, [=](const i::ParseInfo& info, i::DeclarationScope* s) {
      i::Scope* fn = s->inner_scope();
      CHECK(fn->is_function_scope());

      i::Scope* loop_block = fn->inner_scope();
      CHECK(loop_block->is_block_scope());

      const i::AstRawString* var_name =
          info.ast_value_factory()->GetOneByteString("loop_var");
      i::Variable* loop_var = loop_block->LookupLocal(var_name);
      CHECK_NOT_NULL(loop_var);
      CHECK(loop_var->IsContextSlot());
      CHECK_EQ(loop_block->ContextLocalCount(), 1);

      i::Variable* loop_var2 = loop_block->inner_scope()->LookupLocal(var_name);
      CHECK_NE(loop_var, loop_var2);
      CHECK(loop_var2->IsContextSlot());
      CHECK_EQ(loop_block->inner_scope()->ContextLocalCount(), 1);
    });
  }

  // Similar to the above, but the first block scope's variables are not
  // captured due to the closure occurring in a nested scope.
  const char* context_bindings3[] = {
      "function loop() {"
      "  for (let loop_var = 0; loop_var < 10; ++loop_var) {"
      "    (() => loop_var)();"
      "  }"
      "}",

      "function loop() {"
      "  for (let loop_var = 0; loop_var < (() => (loop_var, 10))();"
      "       ++loop_var) {"
      "  }"
      "}",

      "function loop() {"
      "  for (let loop_var = 0; loop_var < 10; (() => ++loop_var)()) {"
      "  }"
      "}",
  };

  for (const char* source : context_bindings3) {
    TestProgram(source, [=](const i::ParseInfo& info, i::DeclarationScope* s) {
      i::Scope* fn = s->inner_scope();
      CHECK(fn->is_function_scope());

      i::Scope* loop_block = fn->inner_scope();
      CHECK(loop_block->is_block_scope());

      const i::AstRawString* var_name =
          info.ast_value_factory()->GetOneByteString("loop_var");
      i::Variable* loop_var = loop_block->LookupLocal(var_name);
      CHECK_NOT_NULL(loop_var);
      CHECK(loop_var->IsStackLocal());
      CHECK_EQ(loop_block->ContextLocalCount(), 0);

      i::Variable* loop_var2 = loop_block->inner_scope()->LookupLocal(var_name);
      CHECK_NE(loop_var, loop_var2);
      CHECK(loop_var2->IsContextSlot());
      CHECK_EQ(loop_block->inner_scope()->ContextLocalCount(), 1);
    });
  }
}

TEST_F(ParsingTest, PrivateNamesSyntaxErrorEarly) {
  const char* context_data[][2] = {
      {"", ""}, {"\"use strict\";", ""}, {nullptr, nullptr}};

  const char* statement_data[] = {
      "class A {"
      "  foo() { return this.#bar; }"
      "}",

      "let A = class {"
      "  foo() { return this.#bar; }"
      "}",

      "class A {"
      "  #foo;  "
      "  bar() { return this.#baz; }"
      "}",

      "let A = class {"
      "  #foo;  "
      "  bar() { return this.#baz; }"
      "}",

      "class A {"
      "  bar() {"
      "    class D { #baz = 1; };"
      "    return this.#baz;"
      "  }"
      "}",

      "let A = class {"
      "  bar() {"
      "    class D { #baz = 1; };"
      "    return this.#baz;"
      "  }"
      "}",

      "a.#bar",

      "class Foo {};"
      "Foo.#bar;",

      "let Foo = class {};"
      "Foo.#bar;",

      "class Foo {};"
      "(new Foo).#bar;",

      "let Foo = class {};"
      "(new Foo).#bar;",

      "class Foo { #bar; };"
      "(new Foo).#bar;",

      "let Foo = class { #bar; };"
      "(new Foo).#bar;",

      "function t(){"
      "  class Foo { getA() { return this.#foo; } }"
      "}",

      "function t(){"
      "  return class { getA() { return this.#foo; } }"
      "}",

      nullptr};

  RunParserSyncTest(context_data, statement_data, kError);
}

TEST_F(ParsingTest, HashbangSyntax) {
  const char* context_data[][2] = {
      {"#!\n", ""},
      {"#!---IGNORED---\n", ""},
      {"#!---IGNORED---\r", ""},
      {"#!---IGNORED---\xE2\x80\xA8", ""},  // <U+2028>
      {"#!---IGNORED---\xE2\x80\xA9", ""},  // <U+2029>
      {nullptr, nullptr}};

  const char* data[] = {"function\nFN\n(\n)\n {\n}\nFN();", nullptr};

  RunParserSyncTest(context_data, data, kSuccess);
  RunParserSyncTest(context_data, data, kSuccess, nullptr, 0, nullptr, 0,
                    nullptr, 0, true);
}

TEST_F(ParsingTest, HashbangSyntaxErrors) {
  const char* file_context_data[][2] = {{"", ""}, {nullptr, nullptr}};
  const char* other_context_data[][2] = {{"/**/", ""},
                                         {"//---\n", ""},
                                         {";", ""},
                                         {"function fn() {", "}"},
                                         {"function* fn() {", "}"},
                                         {"async function fn() {", "}"},
                                         {"async function* fn() {", "}"},
                                         {"() => {", "}"},
                                         {"() => ", ""},
                                         {"function fn(a = ", ") {}"},
                                         {"function* fn(a = ", ") {}"},
                                         {"async function fn(a = ", ") {}"},
                                         {"async function* fn(a = ", ") {}"},
                                         {"(a = ", ") => {}"},
                                         {"(a = ", ") => a"},
                                         {"class k {", "}"},
                                         {"[", "]"},
                                         {"{", "}"},
                                         {"({", "})"},
                                         {nullptr, nullptr}};

  const char* invalid_hashbang_data[] = {// Encoded characters are not allowed
                                         "#\\u0021\n"
                                         "#\\u{21}\n",
                                         "#\\x21\n",
                                         "#\\041\n",
                                         "\\u0023!\n",
                                         "\\u{23}!\n",
                                         "\\x23!\n",
                                         "\\043!\n",
                                         "\\u0023\\u0021\n",

                                         "\n#!---IGNORED---\n",
                                         " #!---IGNORED---\n",
                                         nullptr};
  const char* hashbang_data[] = {"#!\n", "#!---IGNORED---\n", nullptr};

  auto SyntaxErrorTest = [this](const char* context_data[][2],
                                const char* data[]) {
    RunParserSyncTest(context_data, data, kError);
    RunParserSyncTest(context_data, data, kError, nullptr, 0, nullptr, 0,
                      nullptr, 0, true);
  };

  SyntaxErrorTest(file_context_data, invalid_hashbang_data);
  SyntaxErrorTest(other_context_data, invalid_hashbang_data);
  SyntaxErrorTest(other_context_data, hashbang_data);
}

TEST_F(ParsingTest, LogicalAssignmentDestructuringErrors) {
  // clang-format off
  const char* context_data[][2] = {
    { "if (", ") { foo(); }" },
    { "(", ")" },
    { "foo(", ")" },
    { nullptr, nullptr }
  };
  const char* error_data[] = {
    "[ x ] ||= [ 2 ]",
    "[ x ||= 2 ] = [ 2 ]",
    "{ x } ||= { x: 2 }",
    "{ x: x ||= 2 ] = { x: 2 }",
    "[ x ] &&= [ 2 ]",
    "[ x &&= 2 ] = [ 2 ]",
    "{ x } &&= { x: 2 }",
    "{ x: x &&= 2 ] = { x: 2 }",
    R"JS([ x ] ??= [ 2 ])JS",
    R"JS([ x ??= 2 ] = [ 2 ])JS",
    R"JS({ x } ??= { x: 2 })JS",
    R"JS({ x: x ??= 2 ] = { x: 2 })JS",
    nullptr
  };
  // clang-format on

  RunParserSyncTest(context_data, error_data, kError);
}

}  // namespace test_parsing
}  // namespace internal
}  // namespace v8
