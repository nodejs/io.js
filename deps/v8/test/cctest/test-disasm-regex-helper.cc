// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/test-disasm-regex-helper.h"

#include "include/v8-function.h"
#include "src/api/api-inl.h"
#include "src/diagnostics/disassembler.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

namespace {
std::string DisassembleFunction(const char* function) {
  v8::Local<v8::Context> context = CcTest::isolate()->GetCurrentContext();
  Handle<JSFunction> f = Handle<JSFunction>::cast(
      v8::Utils::OpenHandle(*v8::Local<v8::Function>::Cast(
          CcTest::global()->Get(context, v8_str(function)).ToLocalChecked())));

  Isolate* isolate = CcTest::i_isolate();
  Handle<Code> code(f->code(isolate), isolate);
  Address begin = code->instruction_start();
  Address end = code->instruction_end();
  std::ostringstream os;
  Disassembler::Decode(isolate, os, reinterpret_cast<uint8_t*>(begin),
                       reinterpret_cast<uint8_t*>(end), CodeReference(code));
  return os.str();
}

}  // namespace

bool CheckDisassemblyRegexPatterns(
    const char* function_name, const std::vector<std::string>& patterns_array) {
  std::istringstream reader(DisassembleFunction(function_name));
  size_t size = patterns_array.size();
  DCHECK_GT(size, 0);

  std::smatch match;
  std::string line;
  RegexParser parser;
  const std::string& first_pattern = patterns_array[0];
  while (std::getline(reader, line)) {
    RegexParser::Status status = parser.ProcessPattern(line, first_pattern);
    if (status == RegexParser::Status::kSuccess) {
      CHECK(std::getline(reader, line));
      for (size_t i = 1; i < size; i++) {
        const std::string& pattern = patterns_array[i];
        status = parser.ProcessPattern(line, pattern);
        if (status != RegexParser::Status::kSuccess) {
          std::cout << "Pattern \"" << pattern << "\" not found" << std::endl;
          std::cout << "Line: \"" << line << "\":" << std::endl;
          parser.PrintSymbols(std::cout);
          return false;
        }
        CHECK(std::getline(reader, line));
      }

      return true;
    }
  }
  return false;
}

namespace {
void RegexCheck(
    const std::vector<std::string>& inputs,
    const std::vector<std::string>& patterns,
    RegexParser::Status expected_status,
    std::function<void(const RegexParser&)> func = [](const RegexParser&) {}) {
  size_t size = patterns.size();
  CHECK_EQ(size, inputs.size());
  RegexParser parser;
  RegexParser::Status status;
  size_t i = 0;
  for (; i < size - 1; i++) {
    const std::string& line = inputs[i];
    const std::string& pattern = patterns[i];
    status = parser.ProcessPattern(line, pattern);
    CHECK_EQ(status, RegexParser::Status::kSuccess);
  }
  const std::string& line = inputs[i];
  const std::string& pattern = patterns[i];
  status = parser.ProcessPattern(line, pattern);

  if (status != expected_status) {
    parser.PrintSymbols(std::cout);
  }
  CHECK_EQ(status, expected_status);
  func(parser);
}

// Check a line against a pattern.
void RegexCheckOne(
    const std::string& line, const std::string& pattern,
    RegexParser::Status expected_status,
    std::function<void(const RegexParser&)> func = [](const RegexParser&) {}) {
  RegexParser parser;
  RegexParser::Status status = parser.ProcessPattern(line, pattern);
  CHECK_EQ(status, expected_status);
  func(parser);
}

void TestSymbolValue(const std::string& sym_name, const std::string& value,
                     const RegexParser& p) {
  CHECK(p.IsSymbolDefined(sym_name));
  CHECK_EQ(p.GetSymbolMatchedValue(sym_name).compare(value), 0);
}

}  // namespace

// clang-format off
TEST(RegexParserSingleLines) {
  //
  // Simple one-liners for found/not found.
  //
  RegexCheckOne(" a b a b c a",
                "a b c",
                RegexParser::Status::kSuccess);

  RegexCheckOne(" a b a bc a",
                "a b c",
                RegexParser::Status::kNotMatched);

  RegexCheckOne("aaabbaaa",
                "ab.*?a",
                RegexParser::Status::kSuccess);

  RegexCheckOne("aaabbaa",
                "^(?:aa+|b)+$",
                RegexParser::Status::kSuccess);

  RegexCheckOne("aaabba",
                "^(?:aa+|b)+$",
                RegexParser::Status::kNotMatched);

  RegexCheckOne("(aaa)",
                "\\(a+\\)",
                RegexParser::Status::kSuccess);

  RegexCheckOne("r19 qwerty",
                "r<<Def:[0-9]+>>",
                RegexParser::Status::kSuccess,
                [] (const RegexParser& p) {
                  TestSymbolValue("Def", "19", p);
                });

  RegexCheckOne("r19 qwerty",
                "r<<Def:[a-z]+>>",
                RegexParser::Status::kSuccess,
                [] (const RegexParser& p) {
                  TestSymbolValue("Def", "ty", p);
                });

  // Backreference/submatch groups are forbidden.
  RegexCheckOne("aaabba",
                "((aa+)|b)+?",
                RegexParser::Status::kWrongPattern);

  // Using passive groups.
  RegexCheckOne("aaabba",
                "(?:(?:aa+)|b)+?",
                RegexParser::Status::kSuccess);

  //
  // Symbol definitions.
  //
  RegexCheckOne("r19 r20",
                "r<<Def:19>>",
                RegexParser::Status::kSuccess,
                [] (const RegexParser& p) {
                  TestSymbolValue("Def", "19", p);
                });

  RegexCheckOne("r19 r20",
                "r<<Def:[0-9]+>>",
                RegexParser::Status::kSuccess,
                [] (const RegexParser& p) {
                  TestSymbolValue("Def", "19", p);
                });

  RegexCheckOne("r19 r20",
                "r<<Def0:[0-9]+>>.*?r<<Def1:[0-9]+>>",
                RegexParser::Status::kSuccess,
                [] (const RegexParser& p) {
                  TestSymbolValue("Def0", "19", p);
                  TestSymbolValue("Def1", "20", p);
                });

  RegexCheckOne("r19 r20",
                "r<<Def0:[0-9]+>>.*?r[0-9]",
                RegexParser::Status::kSuccess,
                [] (const RegexParser& p) {
                  TestSymbolValue("Def0", "19", p);
                });

  // Checks that definitions are not committed unless the pattern is matched.
  RegexCheckOne("r19",
                "r<<Def0:[0-9]+>>.*?r<<Def1:[0-9]+>>",
                RegexParser::Status::kNotMatched,
                [] (const RegexParser& p) {
                  CHECK(!p.IsSymbolDefined("Def0"));
                  CHECK(!p.IsSymbolDefined("Def1"));
                });

  RegexCheckOne("r19 r19 r1",
                "r<<Def0:[0-9]+>>.*?r<<Def0:[0-9]+>> r<<Def1:[0-9]+>>",
                RegexParser::Status::kRedefinition,
                [] (const RegexParser& p) {
                  CHECK(!p.IsSymbolDefined("Def0"));
                  CHECK(!p.IsSymbolDefined("Def1"));
                });

  RegexCheckOne("r19 r1",
                "r<<Def0:[0-9]+>> (r1)",
                RegexParser::Status::kWrongPattern,
                [] (const RegexParser& p) {
                  CHECK(!p.IsSymbolDefined("Def0"));
                });

  //
  // Undefined symbol references.
  //
  RegexCheckOne("r19 r1",
                "r[0-9].*?r<<Undef>>",
                RegexParser::Status::kDefNotFound,
                [] (const RegexParser& p) {
                  CHECK(!p.IsSymbolDefined("Undef"));
                });

  RegexCheckOne("r19 r1",
                "r<<Def0:[0-9]+>>.*?<<Undef>>",
                RegexParser::Status::kDefNotFound,
                [] (const RegexParser& p) {
                  CHECK(!p.IsSymbolDefined("Undef"));
                  CHECK(!p.IsSymbolDefined("Def0"));
                });

  RegexCheckOne("r19 r19",
                "r<<Def0:[0-9]+>>.*?<<Def0>>",
                RegexParser::Status::kDefNotFound,
                [] (const RegexParser& p) {
                  CHECK(!p.IsSymbolDefined("Def0"));
                });
}

TEST(RegexParserMultiLines) {
  RegexCheck({ " a b a b c a",
               " a b a b c a" },
             { "a b c",
               "a b c" },
             RegexParser::Status::kSuccess);

  RegexCheck({ "r16 = r15",
               "r17 = r16" },
             { "<<Def:r[0-9]+>> = r[0-9]+",
               "[0-9]+ = <<Def>>" },
             RegexParser::Status::kSuccess,
             [] (const RegexParser& p) {
               TestSymbolValue("Def", "r16", p);
             });

  RegexCheck({ "r16 = r15 + r13",
               "r17 = r16 + r14",
               "r19 = r14" },
             { "<<Def0:r[0-9]+>> = r[0-9]+",
               "<<Def1:r[0-9]+>> = <<Def0>> \\+ <<Def2:r[0-9]+>>",
               "<<Def3:r[0-9]+>> = <<Def2>>" },
             RegexParser::Status::kSuccess,
             [] (const RegexParser& p) {
               TestSymbolValue("Def0", "r16", p);
               TestSymbolValue("Def1", "r17", p);
               TestSymbolValue("Def2", "r14", p);
               TestSymbolValue("Def3", "r19", p);
             });

  // Constraint is not met for Def (r19 != r16).
  RegexCheck({ "r16 = r15",
               "r17 = r19" },
             { "<<Def:r[0-9]+>> = r[0-9]+",
               "[0-9]+ = <<Def>>" },
             RegexParser::Status::kNotMatched,
             [] (const RegexParser& p) {
               TestSymbolValue("Def", "r16", p);
             });
}
// clang-format on

}  // namespace internal
}  // namespace v8
