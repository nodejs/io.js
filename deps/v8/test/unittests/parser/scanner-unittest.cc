// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests v8::internal::Scanner. Note that presently most unit tests for the
// Scanner are in parsing-unittest.cc, rather than here.

#include "src/parsing/scanner.h"

#include "src/handles/handles-inl.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/scanner-character-streams.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class ScannerTest : public TestWithIsolate {
 public:
  struct ScannerTestHelper {
    ScannerTestHelper() = default;
    ScannerTestHelper(ScannerTestHelper&& other) V8_NOEXCEPT
        : stream(std::move(other.stream)),
          scanner(std::move(other.scanner)) {}

    std::unique_ptr<Utf16CharacterStream> stream;
    std::unique_ptr<Scanner> scanner;

    Scanner* operator->() const { return scanner.get(); }
    Scanner* get() const { return scanner.get(); }
  };
  ScannerTestHelper make_scanner(const char* src) {
    ScannerTestHelper helper;
    helper.stream = ScannerStream::ForTesting(src);
    helper.scanner = std::unique_ptr<Scanner>(new Scanner(
        helper.stream.get(),
        UnoptimizedCompileFlags::ForTest(
            reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent()))));

    helper.scanner->Initialize();
    return helper;
  }
};
namespace {

const char src_simple[] = "function foo() { var x = 2 * a() + b; }";

}  // anonymous namespace

// CHECK_TOK checks token equality, but by checking for equality of the token
// names. That should have the same result, but has much nicer error messaages.
#define CHECK_TOK(a, b) CHECK_EQ(Token::Name(a), Token::Name(b))

TEST_F(ScannerTest, Bookmarks) {
  // Scan through the given source and record the tokens for use as reference
  // below.
  std::vector<Token::Value> tokens;
  {
    auto scanner = make_scanner(src_simple);
    do {
      tokens.push_back(scanner->Next());
    } while (scanner->current_token() != Token::kEos);
  }

  // For each position:
  // - Scan through file,
  // - set a bookmark once the position is reached,
  // - scan a bit more,
  // - reset to the bookmark, and
  // - scan until the end.
  // At each step, compare to the reference token sequence generated above.
  for (size_t bookmark_pos = 0; bookmark_pos < tokens.size(); bookmark_pos++) {
    auto scanner = make_scanner(src_simple);
    Scanner::BookmarkScope bookmark(scanner.get());

    for (size_t i = 0; i < std::min(bookmark_pos + 10, tokens.size()); i++) {
      if (i == bookmark_pos) {
        bookmark.Set(scanner->peek_location().beg_pos);
      }
      CHECK_TOK(tokens[i], scanner->Next());
    }

    bookmark.Apply();
    for (size_t i = bookmark_pos; i < tokens.size(); i++) {
      CHECK_TOK(tokens[i], scanner->Next());
    }
  }
}

TEST_F(ScannerTest, AllThePushbacks) {
  const struct {
    const char* src;
    const Token::Value tokens[5];  // Large enough for any of the test cases.
  } test_cases[] = {
      {"<-x", {Token::kLessThan, Token::kSub, Token::kIdentifier, Token::kEos}},
      {"<!x", {Token::kLessThan, Token::kNot, Token::kIdentifier, Token::kEos}},
      {"<!-x",
       {Token::kLessThan, Token::kNot, Token::kSub, Token::kIdentifier,
        Token::kEos}},
      {"<!-- xx -->\nx", {Token::kIdentifier, Token::kEos}},
  };

  for (const auto& test_case : test_cases) {
    auto scanner = make_scanner(test_case.src);
    for (size_t i = 0; test_case.tokens[i] != Token::kEos; i++) {
      CHECK_TOK(test_case.tokens[i], scanner->Next());
    }
    CHECK_TOK(Token::kEos, scanner->Next());
  }
}

TEST_F(ScannerTest, PeekAheadAheadAwaitUsingDeclaration) {
  const char src[] = "await using a = 2;";

  std::vector<Token::Value> tokens;
  {
    auto scanner = make_scanner(src);
    do {
      tokens.push_back(scanner->Next());
    } while (scanner->current_token() != Token::kEos);
  }

  auto scanner = make_scanner(src);
  Scanner::BookmarkScope bookmark(scanner.get());
  bookmark.Set(scanner->peek_location().beg_pos);
  bookmark.Apply();

  CHECK_TOK(tokens[0], scanner->Next());
  CHECK_TOK(tokens[1], scanner->peek());
  CHECK_TOK(tokens[2], scanner->PeekAhead());
  CHECK_TOK(tokens[3], scanner->PeekAheadAhead());
}

TEST_F(ScannerTest, PeekAheadAheadAwaitExpression) {
  const char src[] = "await using + 5;";

  std::vector<Token::Value> tokens;
  {
    auto scanner = make_scanner(src);
    do {
      tokens.push_back(scanner->Next());
    } while (scanner->current_token() != Token::kEos);
  }

  auto scanner = make_scanner(src);
  Scanner::BookmarkScope bookmark(scanner.get());
  bookmark.Set(scanner->peek_location().beg_pos);
  bookmark.Apply();

  CHECK_TOK(tokens[0], scanner->Next());
  CHECK_TOK(tokens[1], scanner->peek());
  CHECK_TOK(tokens[2], scanner->PeekAhead());
  CHECK_TOK(tokens[3], scanner->PeekAheadAhead());
}

}  // namespace internal
}  // namespace v8
