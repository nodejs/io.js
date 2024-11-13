// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/earley-parser.h"

#include <optional>

#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace torque {

namespace {

template <int op(int, int)>
std::optional<ParseResult> MakeBinop(ParseResultIterator* child_results) {
  // Ideally, we would want to use int as a result type here instead of
  // std::string. This is possible, but requires adding int to the list of
  // supported ParseResult types in torque-parser.cc. To avoid changing that
  // code, we use std::string here, which is already used in the Torque parser.
  auto a = child_results->NextAs<std::string>();
  auto b = child_results->NextAs<std::string>();
  return ParseResult{std::to_string(op(std::stoi(a), std::stoi(b)))};
}

int plus(int a, int b) { return a + b; }
int minus(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }

}  // namespace

struct SimpleArithmeticGrammar : Grammar {
  static bool MatchWhitespace(InputPosition* pos) {
    while (MatchChar(std::isspace, pos)) {
    }
    return true;
  }

  static bool MatchInteger(InputPosition* pos) {
    InputPosition current = *pos;
    MatchString("-", &current);
    if (MatchChar(std::isdigit, &current)) {
      while (MatchChar(std::isdigit, &current)) {
      }
      *pos = current;
      return true;
    }
    return false;
  }

  SimpleArithmeticGrammar() : Grammar(&sum_expression) {
    SetWhitespace(MatchWhitespace);
  }

  Symbol integer = {Rule({Pattern(MatchInteger)}, YieldMatchedInput)};

  Symbol atomic_expression = {Rule({&integer}),
                              Rule({Token("("), &sum_expression, Token(")")})};

  Symbol mul_expression = {
      Rule({&atomic_expression}),
      Rule({&mul_expression, Token("*"), &atomic_expression}, MakeBinop<mul>)};

  Symbol sum_expression = {
      Rule({&mul_expression}),
      Rule({&sum_expression, Token("+"), &mul_expression}, MakeBinop<plus>),
      Rule({&sum_expression, Token("-"), &mul_expression}, MakeBinop<minus>)};
};

TEST(EarleyParser, SimpleArithmetic) {
  SimpleArithmeticGrammar grammar;
  SourceFileMap::Scope source_file_map("");
  CurrentSourceFile::Scope current_source_file{
      SourceFileMap::AddSource("dummy_filename")};
  std::string result1 =
      grammar.Parse("-5 - 5 + (3 + 5) * 2")->Cast<std::string>();
  ASSERT_EQ("6", result1);
  std::string result2 = grammar.Parse("((-1 + (1) * 2 + 3 - 4 * 5 + -6 * 7))")
                            ->Cast<std::string>();
  ASSERT_EQ("-58", result2);
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
