// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/regexp/regexp.h"

#include <cstdlib>
#include <memory>
#include <sstream>

#include "include/v8-context.h"
#include "include/v8-initialization.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/api/api-inl.h"
#include "src/ast/ast.h"
#include "src/base/strings.h"
#include "src/codegen/assembler-arch.h"
#include "src/codegen/macro-assembler.h"
#include "src/init/v8.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/objects-inl.h"
#include "src/regexp/regexp-bytecode-generator.h"
#include "src/regexp/regexp-bytecodes.h"
#include "src/regexp/regexp-compiler.h"
#include "src/regexp/regexp-interpreter.h"
#include "src/regexp/regexp-macro-assembler-arch.h"
#include "src/regexp/regexp-parser.h"
#include "src/strings/char-predicates-inl.h"
#include "src/strings/string-stream.h"
#include "src/strings/unicode-inl.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-list-inl.h"
#include "test/common/flag-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

TEST_F(TestWithNativeContext, ConvertRegExpFlagsToString) {
  RunJS("let regexp = new RegExp(/ab+c/ig);");
  Handle<JSRegExp> regexp = RunJS<JSRegExp>("regexp");
  Handle<String> flags = RunJS<String>("regexp.flags");
  Handle<String> converted_flags =
      JSRegExp::StringFromFlags(isolate(), regexp->flags());
  EXPECT_TRUE(String::Equals(isolate(), flags, converted_flags));
}

TEST_F(TestWithNativeContext, ConvertRegExpFlagsToStringNoFlags) {
  RunJS("let regexp = new RegExp(/ab+c/);");
  Handle<JSRegExp> regexp = RunJS<JSRegExp>("regexp");
  Handle<String> flags = RunJS<String>("regexp.flags");
  Handle<String> converted_flags =
      JSRegExp::StringFromFlags(isolate(), regexp->flags());
  EXPECT_TRUE(String::Equals(isolate(), flags, converted_flags));
}

TEST_F(TestWithNativeContext, ConvertRegExpFlagsToStringAllFlags) {
  RunJS("let regexp = new RegExp(/ab+c/dgimsuy);");
  Handle<JSRegExp> regexp = RunJS<JSRegExp>("regexp");
  Handle<String> flags = RunJS<String>("regexp.flags");
  Handle<String> converted_flags =
      JSRegExp::StringFromFlags(isolate(), regexp->flags());
  EXPECT_TRUE(String::Equals(isolate(), flags, converted_flags));
}

using RegExpTest = TestWithIsolate;

static bool CheckParse(const char* input) {
  Isolate* isolate = reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent());

  v8::HandleScope scope(v8::Isolate::GetCurrent());
  Zone zone(isolate->allocator(), ZONE_NAME);
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(input);
  RegExpCompileData result;
  return RegExpParser::ParseRegExpFromHeapString(isolate, &zone, str, {},
                                                 &result);
}

static void CheckParseEq(const char* input, const char* expected,
                         bool unicode = false) {
  Isolate* isolate = reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent());

  v8::HandleScope scope(v8::Isolate::GetCurrent());
  Zone zone(isolate->allocator(), ZONE_NAME);
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(input);
  RegExpCompileData result;
  RegExpFlags flags;
  if (unicode) flags |= RegExpFlag::kUnicode;
  CHECK(RegExpParser::ParseRegExpFromHeapString(isolate, &zone, str, flags,
                                                &result));
  CHECK_NOT_NULL(result.tree);
  CHECK_EQ(RegExpError::kNone, result.error);
  std::ostringstream os;
  result.tree->Print(os, &zone);
  if (strcmp(expected, os.str().c_str()) != 0) {
    printf("%s | %s\n", expected, os.str().c_str());
  }
  CHECK_EQ(0, strcmp(expected, os.str().c_str()));
}

static bool CheckSimple(const char* input) {
  Isolate* isolate = reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent());

  v8::HandleScope scope(v8::Isolate::GetCurrent());
  Zone zone(isolate->allocator(), ZONE_NAME);
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(input);
  RegExpCompileData result;
  CHECK(RegExpParser::ParseRegExpFromHeapString(isolate, &zone, str, {},
                                                &result));
  CHECK_NOT_NULL(result.tree);
  CHECK_EQ(RegExpError::kNone, result.error);
  return result.simple;
}

struct MinMaxPair {
  int min_match;
  int max_match;
};

static MinMaxPair CheckMinMaxMatch(const char* input) {
  Isolate* isolate = reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent());

  v8::HandleScope scope(v8::Isolate::GetCurrent());
  Zone zone(isolate->allocator(), ZONE_NAME);
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(input);
  RegExpCompileData result;
  CHECK(RegExpParser::ParseRegExpFromHeapString(isolate, &zone, str, {},
                                                &result));
  CHECK_NOT_NULL(result.tree);
  CHECK_EQ(RegExpError::kNone, result.error);
  int min_match = result.tree->min_match();
  int max_match = result.tree->max_match();
  MinMaxPair pair = {min_match, max_match};
  return pair;
}

#define CHECK_PARSE_ERROR(input) CHECK(!CheckParse(input))
#define CHECK_SIMPLE(input, simple) CHECK_EQ(simple, CheckSimple(input));
#define CHECK_MIN_MAX(input, min, max)            \
  {                                               \
    MinMaxPair min_max = CheckMinMaxMatch(input); \
    CHECK_EQ(min, min_max.min_match);             \
    CHECK_EQ(max, min_max.max_match);             \
  }

TEST_F(RegExpTest, RegExpParser) {
  CHECK_PARSE_ERROR("?");

  CheckParseEq("abc", "'abc'");
  CheckParseEq("", "%");
  CheckParseEq("abc|def", "(| 'abc' 'def')");
  CheckParseEq("abc|def|ghi", "(| 'abc' 'def' 'ghi')");
  CheckParseEq("^xxx$", "(: @^i 'xxx' @$i)");
  CheckParseEq("ab\\b\\d\\bcd", "(: 'ab' @b [0-9] @b 'cd')");
  CheckParseEq("\\w|\\d", "(| [0-9 A-Z _ a-z] [0-9])");
  CheckParseEq("a*", "(# 0 - g 'a')");
  CheckParseEq("a*?", "(# 0 - n 'a')");
  CheckParseEq("abc+", "(: 'ab' (# 1 - g 'c'))");
  CheckParseEq("abc+?", "(: 'ab' (# 1 - n 'c'))");
  CheckParseEq("xyz?", "(: 'xy' (# 0 1 g 'z'))");
  CheckParseEq("xyz??", "(: 'xy' (# 0 1 n 'z'))");
  CheckParseEq("xyz{0,1}", "(: 'xy' (# 0 1 g 'z'))");
  CheckParseEq("xyz{0,1}?", "(: 'xy' (# 0 1 n 'z'))");
  CheckParseEq("xyz{93}", "(: 'xy' (# 93 93 g 'z'))");
  CheckParseEq("xyz{93}?", "(: 'xy' (# 93 93 n 'z'))");
  CheckParseEq("xyz{1,32}", "(: 'xy' (# 1 32 g 'z'))");
  CheckParseEq("xyz{1,32}?", "(: 'xy' (# 1 32 n 'z'))");
  CheckParseEq("xyz{1,}", "(: 'xy' (# 1 - g 'z'))");
  CheckParseEq("xyz{1,}?", "(: 'xy' (# 1 - n 'z'))");
  CheckParseEq("a\\fb\\nc\\rd\\te\\vf", "'a\\x0cb\\x0ac\\x0dd\\x09e\\x0bf'");
  CheckParseEq("a\\nb\\bc", "(: 'a\\x0ab' @b 'c')");
  CheckParseEq("(?:foo)", "(?: 'foo')");
  CheckParseEq("(?: foo )", "(?: ' foo ')");
  CheckParseEq("(foo|bar|baz)", "(^ (| 'foo' 'bar' 'baz'))");
  CheckParseEq("foo|(bar|baz)|quux", "(| 'foo' (^ (| 'bar' 'baz')) 'quux')");
  CheckParseEq("foo(?=bar)baz", "(: 'foo' (-> + 'bar') 'baz')");
  CheckParseEq("foo(?!bar)baz", "(: 'foo' (-> - 'bar') 'baz')");
  CheckParseEq("foo(?<=bar)baz", "(: 'foo' (<- + 'bar') 'baz')");
  CheckParseEq("foo(?<!bar)baz", "(: 'foo' (<- - 'bar') 'baz')");
  CheckParseEq("()", "(^ %)");
  CheckParseEq("(?=)", "(-> + %)");
  CheckParseEq("[]", "^[\\x00-\\u{10ffff}]");  // Doesn't compile on windows
  CheckParseEq("[^]", "[\\x00-\\u{10ffff}]");  // \uffff isn't in codepage 1252
  CheckParseEq("[x]", "[x]");
  CheckParseEq("[xyz]", "[x y z]");
  CheckParseEq("[a-zA-Z0-9]", "[a-z A-Z 0-9]");
  CheckParseEq("[-123]", "[- 1 2 3]");
  CheckParseEq("[^123]", "^[1 2 3]");
  CheckParseEq("]", "']'");
  CheckParseEq("}", "'}'");
  CheckParseEq("[a-b-c]", "[a-b - c]");
  CheckParseEq("[\\d]", "[0-9]");
  CheckParseEq("[x\\dz]", "[x 0-9 z]");
  CheckParseEq("[\\d-z]", "[0-9 - z]");
  CheckParseEq("[\\d-\\d]", "[0-9 0-9 -]");
  CheckParseEq("[z-\\d]", "[0-9 z -]");
  // Control character outside character class.
  CheckParseEq("\\cj\\cJ\\ci\\cI\\ck\\cK", "'\\x0a\\x0a\\x09\\x09\\x0b\\x0b'");
  CheckParseEq("\\c!", "'\\c!'");
  CheckParseEq("\\c_", "'\\c_'");
  CheckParseEq("\\c~", "'\\c~'");
  CheckParseEq("\\c1", "'\\c1'");
  // Control character inside character class.
  CheckParseEq("[\\c!]", "[\\ c !]");
  CheckParseEq("[\\c_]", "[\\x1f]");
  CheckParseEq("[\\c~]", "[\\ c ~]");
  CheckParseEq("[\\ca]", "[\\x01]");
  CheckParseEq("[\\cz]", "[\\x1a]");
  CheckParseEq("[\\cA]", "[\\x01]");
  CheckParseEq("[\\cZ]", "[\\x1a]");
  CheckParseEq("[\\c1]", "[\\x11]");

  CheckParseEq("[a\\]c]", "[a ] c]");
  CheckParseEq("\\[\\]\\{\\}\\(\\)\\%\\^\\#\\ ", "'[]{}()%^# '");
  CheckParseEq("[\\[\\]\\{\\}\\(\\)\\%\\^\\#\\ ]", "[[ ] { } ( ) % ^ #  ]");
  CheckParseEq("\\0", "'\\x00'");
  CheckParseEq("\\8", "'8'");
  CheckParseEq("\\9", "'9'");
  CheckParseEq("\\11", "'\\x09'");
  CheckParseEq("\\11a", "'\\x09a'");
  CheckParseEq("\\011", "'\\x09'");
  CheckParseEq("\\00011", "'\\x0011'");
  CheckParseEq("\\118", "'\\x098'");
  CheckParseEq("\\111", "'I'");
  CheckParseEq("\\1111", "'I1'");
  CheckParseEq("(x)(x)(x)\\1", "(: (^ 'x') (^ 'x') (^ 'x') (<- 1))");
  CheckParseEq("(x)(x)(x)\\2", "(: (^ 'x') (^ 'x') (^ 'x') (<- 2))");
  CheckParseEq("(x)(x)(x)\\3", "(: (^ 'x') (^ 'x') (^ 'x') (<- 3))");
  CheckParseEq("(x)(x)(x)\\4", "(: (^ 'x') (^ 'x') (^ 'x') '\\x04')");
  CheckParseEq("(x)(x)(x)\\1*",
               "(: (^ 'x') (^ 'x') (^ 'x')"
               " (# 0 - g (<- 1)))");
  CheckParseEq("(x)(x)(x)\\2*",
               "(: (^ 'x') (^ 'x') (^ 'x')"
               " (# 0 - g (<- 2)))");
  CheckParseEq("(x)(x)(x)\\3*",
               "(: (^ 'x') (^ 'x') (^ 'x')"
               " (# 0 - g (<- 3)))");
  CheckParseEq("(x)(x)(x)\\4*",
               "(: (^ 'x') (^ 'x') (^ 'x')"
               " (# 0 - g '\\x04'))");
  CheckParseEq("(x)(x)(x)(x)(x)(x)(x)(x)(x)(x)\\10",
               "(: (^ 'x') (^ 'x') (^ 'x') (^ 'x') (^ 'x') (^ 'x')"
               " (^ 'x') (^ 'x') (^ 'x') (^ 'x') (<- 10))");
  CheckParseEq("(x)(x)(x)(x)(x)(x)(x)(x)(x)(x)\\11",
               "(: (^ 'x') (^ 'x') (^ 'x') (^ 'x') (^ 'x') (^ 'x')"
               " (^ 'x') (^ 'x') (^ 'x') (^ 'x') '\\x09')");
  CheckParseEq("(a)\\1", "(: (^ 'a') (<- 1))");
  CheckParseEq("(a\\1)", "(^ 'a')");
  CheckParseEq("(\\1a)", "(^ 'a')");
  CheckParseEq("(\\2)(\\1)", "(: (^ (<- 2)) (^ (<- 1)))");
  CheckParseEq("(?=a)?a", "'a'");
  CheckParseEq("(?=a){0,10}a", "'a'");
  CheckParseEq("(?=a){1,10}a", "(: (-> + 'a') 'a')");
  CheckParseEq("(?=a){9,10}a", "(: (-> + 'a') 'a')");
  CheckParseEq("(?!a)?a", "'a'");
  CheckParseEq("\\1(a)", "(: (<- 1) (^ 'a'))");
  CheckParseEq("(?!(a))\\1", "(: (-> - (^ 'a')) (<- 1))");
  CheckParseEq("(?!\\1(a\\1)\\1)\\1",
               "(: (-> - (: (<- 1) (^ 'a') (<- 1))) (<- 1))");
  CheckParseEq("\\1\\2(a(?:\\1(b\\1\\2))\\2)\\1",
               "(: (<- 1) (<- 2) (^ (: 'a' (?: (^ 'b')) (<- 2))) (<- 1))");
  CheckParseEq("\\1\\2(a(?<=\\1(b\\1\\2))\\2)\\1",
               "(: (<- 1) (<- 2) (^ (: 'a' (<- + (^ 'b')) (<- 2))) (<- 1))");
  CheckParseEq("[\\0]", "[\\x00]");
  CheckParseEq("[\\11]", "[\\x09]");
  CheckParseEq("[\\11a]", "[\\x09 a]");
  CheckParseEq("[\\011]", "[\\x09]");
  CheckParseEq("[\\00011]", "[\\x00 1 1]");
  CheckParseEq("[\\118]", "[\\x09 8]");
  CheckParseEq("[\\111]", "[I]");
  CheckParseEq("[\\1111]", "[I 1]");
  CheckParseEq("\\x34", "'\x34'");
  CheckParseEq("\\x60", "'\x60'");
  CheckParseEq("\\x3z", "'x3z'");
  CheckParseEq("\\c", "'\\c'");
  CheckParseEq("\\u0034", "'\x34'");
  CheckParseEq("\\u003z", "'u003z'");
  CheckParseEq("foo[z]*", "(: 'foo' (# 0 - g [z]))");
  CheckParseEq("^^^$$$\\b\\b\\b\\b", "(: @^i @^i @^i @$i @$i @$i @b @b @b @b)");
  CheckParseEq("\\b\\b\\b\\b\\B\\B\\B\\B\\b\\b\\b\\b",
               "(: @b @b @b @b @B @B @B @B @b @b @b @b)");
  CheckParseEq("\\b\\B\\b", "(: @b @B @b)");

  // Unicode regexps
  CheckParseEq("\\u{12345}", "'\\ud808\\udf45'", true);
  CheckParseEq("\\u{12345}\\u{23456}", "(! '\\ud808\\udf45' '\\ud84d\\udc56')",
               true);
  CheckParseEq("\\u{12345}|\\u{23456}", "(| '\\ud808\\udf45' '\\ud84d\\udc56')",
               true);
  CheckParseEq("\\u{12345}{3}", "(# 3 3 g '\\ud808\\udf45')", true);
  CheckParseEq("\\u{12345}*", "(# 0 - g '\\ud808\\udf45')", true);

  CheckParseEq("\\ud808\\udf45*", "(# 0 - g '\\ud808\\udf45')", true);
  CheckParseEq("[\\ud808\\udf45-\\ud809\\udccc]", "[\\u{012345}-\\u{0124cc}]",
               true);

  CHECK_SIMPLE("", false);
  CHECK_SIMPLE("a", true);
  CHECK_SIMPLE("a|b", false);
  CHECK_SIMPLE("a\\n", false);
  CHECK_SIMPLE("^a", false);
  CHECK_SIMPLE("a$", false);
  CHECK_SIMPLE("a\\b!", false);
  CHECK_SIMPLE("a\\Bb", false);
  CHECK_SIMPLE("a*", false);
  CHECK_SIMPLE("a*?", false);
  CHECK_SIMPLE("a?", false);
  CHECK_SIMPLE("a??", false);
  CHECK_SIMPLE("a{0,1}?", false);
  CHECK_SIMPLE("a{1,1}?", false);
  CHECK_SIMPLE("a{1,2}?", false);
  CHECK_SIMPLE("a+?", false);
  CHECK_SIMPLE("(a)", false);
  CHECK_SIMPLE("(a)\\1", false);
  CHECK_SIMPLE("(\\1a)", false);
  CHECK_SIMPLE("\\1(a)", false);
  CHECK_SIMPLE("a\\s", false);
  CHECK_SIMPLE("a\\S", false);
  CHECK_SIMPLE("a\\d", false);
  CHECK_SIMPLE("a\\D", false);
  CHECK_SIMPLE("a\\w", false);
  CHECK_SIMPLE("a\\W", false);
  CHECK_SIMPLE("a.", false);
  CHECK_SIMPLE("a\\q", false);
  CHECK_SIMPLE("a[a]", false);
  CHECK_SIMPLE("a[^a]", false);
  CHECK_SIMPLE("a[a-z]", false);
  CHECK_SIMPLE("a[\\q]", false);
  CHECK_SIMPLE("a(?:b)", false);
  CHECK_SIMPLE("a(?=b)", false);
  CHECK_SIMPLE("a(?!b)", false);
  CHECK_SIMPLE("\\x60", false);
  CHECK_SIMPLE("\\u0060", false);
  CHECK_SIMPLE("\\cA", false);
  CHECK_SIMPLE("\\q", false);
  CHECK_SIMPLE("\\1112", false);
  CHECK_SIMPLE("\\0", false);
  CHECK_SIMPLE("(a)\\1", false);
  CHECK_SIMPLE("(?=a)?a", false);
  CHECK_SIMPLE("(?!a)?a\\1", false);
  CHECK_SIMPLE("(?:(?=a))a\\1", false);

  CheckParseEq("a{}", "'a{}'");
  CheckParseEq("a{,}", "'a{,}'");
  CheckParseEq("a{", "'a{'");
  CheckParseEq("a{z}", "'a{z}'");
  CheckParseEq("a{1z}", "'a{1z}'");
  CheckParseEq("a{12z}", "'a{12z}'");
  CheckParseEq("a{12,", "'a{12,'");
  CheckParseEq("a{12,3b", "'a{12,3b'");
  CheckParseEq("{}", "'{}'");
  CheckParseEq("{,}", "'{,}'");
  CheckParseEq("{", "'{'");
  CheckParseEq("{z}", "'{z}'");
  CheckParseEq("{1z}", "'{1z}'");
  CheckParseEq("{12z}", "'{12z}'");
  CheckParseEq("{12,", "'{12,'");
  CheckParseEq("{12,3b", "'{12,3b'");

  CHECK_MIN_MAX("a", 1, 1);
  CHECK_MIN_MAX("abc", 3, 3);
  CHECK_MIN_MAX("a[bc]d", 3, 3);
  CHECK_MIN_MAX("a|bc", 1, 2);
  CHECK_MIN_MAX("ab|c", 1, 2);
  CHECK_MIN_MAX("a||bc", 0, 2);
  CHECK_MIN_MAX("|", 0, 0);
  CHECK_MIN_MAX("(?:ab)", 2, 2);
  CHECK_MIN_MAX("(?:ab|cde)", 2, 3);
  CHECK_MIN_MAX("(?:ab)|cde", 2, 3);
  CHECK_MIN_MAX("(ab)", 2, 2);
  CHECK_MIN_MAX("(ab|cde)", 2, 3);
  CHECK_MIN_MAX("(ab)\\1", 2, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(ab|cde)\\1", 2, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:ab)?", 0, 2);
  CHECK_MIN_MAX("(?:ab)*", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:ab)+", 2, RegExpTree::kInfinity);
  CHECK_MIN_MAX("a?", 0, 1);
  CHECK_MIN_MAX("a*", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("a+", 1, RegExpTree::kInfinity);
  CHECK_MIN_MAX("a??", 0, 1);
  CHECK_MIN_MAX("a*?", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("a+?", 1, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a?)?", 0, 1);
  CHECK_MIN_MAX("(?:a*)?", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a+)?", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a?)+", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a*)+", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a+)+", 1, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a?)*", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a*)*", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a+)*", 0, RegExpTree::kInfinity);
  CHECK_MIN_MAX("a{0}", 0, 0);
  CHECK_MIN_MAX("(?:a+){0}", 0, 0);
  CHECK_MIN_MAX("(?:a+){0,0}", 0, 0);
  CHECK_MIN_MAX("a*b", 1, RegExpTree::kInfinity);
  CHECK_MIN_MAX("a+b", 2, RegExpTree::kInfinity);
  CHECK_MIN_MAX("a*b|c", 1, RegExpTree::kInfinity);
  CHECK_MIN_MAX("a+b|c", 1, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:a{5,1000000}){3,1000000}", 15, RegExpTree::kInfinity);
  CHECK_MIN_MAX("(?:ab){4,7}", 8, 14);
  CHECK_MIN_MAX("a\\bc", 2, 2);
  CHECK_MIN_MAX("a\\Bc", 2, 2);
  CHECK_MIN_MAX("a\\sc", 3, 3);
  CHECK_MIN_MAX("a\\Sc", 3, 3);
  CHECK_MIN_MAX("a(?=b)c", 2, 2);
  CHECK_MIN_MAX("a(?=bbb|bb)c", 2, 2);
  CHECK_MIN_MAX("a(?!bbb|bb)c", 2, 2);

  CheckParseEq("(?<a>x)(?<b>x)(?<c>x)\\k<a>",
               "(: (^ 'x') (^ 'x') (^ 'x') (<- 1))", true);
  CheckParseEq("(?<a>x)(?<b>x)(?<c>x)\\k<b>",
               "(: (^ 'x') (^ 'x') (^ 'x') (<- 2))", true);
  CheckParseEq("(?<a>x)(?<b>x)(?<c>x)\\k<c>",
               "(: (^ 'x') (^ 'x') (^ 'x') (<- 3))", true);
  CheckParseEq("(?<a>a)\\k<a>", "(: (^ 'a') (<- 1))", true);
  CheckParseEq("(?<a>a\\k<a>)", "(^ 'a')", true);
  CheckParseEq("(?<a>\\k<a>a)", "(^ 'a')", true);
  CheckParseEq("(?<a>\\k<b>)(?<b>\\k<a>)", "(: (^ (<- 2)) (^ (<- 1)))", true);
  CheckParseEq("\\k<a>(?<a>a)", "(: (<- 1) (^ 'a'))", true);

  CheckParseEq("(?<\\u{03C0}>a)", "(^ 'a')", true);
  CheckParseEq("(?<\\u03C0>a)", "(^ 'a')", true);
}

TEST_F(RegExpTest, ParserRegression) {
  CheckParseEq("[A-Z$-][x]", "(! [A-Z $ -] [x])");
  CheckParseEq("a{3,4*}", "(: 'a{3,' (# 0 - g '4') '}')");
  CheckParseEq("{", "'{'");
  CheckParseEq("a|", "(| 'a' %)");
}

static void ExpectError(const char* input, const char* expected,
                        bool unicode = false) {
  Isolate* isolate = reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent());

  v8::HandleScope scope(v8::Isolate::GetCurrent());
  Zone zone(isolate->allocator(), ZONE_NAME);
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(input);
  RegExpCompileData result;
  RegExpFlags flags;
  if (unicode) flags |= RegExpFlag::kUnicode;
  CHECK(!RegExpParser::ParseRegExpFromHeapString(isolate, &zone, str, flags,
                                                 &result));
  CHECK_NULL(result.tree);
  CHECK_NE(RegExpError::kNone, result.error);
  CHECK_EQ(0, strcmp(expected, RegExpErrorString(result.error)));
}

TEST_F(RegExpTest, Errors) {
  const char* kEndBackslash = "\\ at end of pattern";
  ExpectError("\\", kEndBackslash);
  const char* kUnterminatedGroup = "Unterminated group";
  ExpectError("(foo", kUnterminatedGroup);
  const char* kInvalidGroup = "Invalid group";
  ExpectError("(?", kInvalidGroup);
  const char* kUnterminatedCharacterClass = "Unterminated character class";
  ExpectError("[", kUnterminatedCharacterClass);
  ExpectError("[a-", kUnterminatedCharacterClass);
  const char* kNothingToRepeat = "Nothing to repeat";
  ExpectError("*", kNothingToRepeat);
  ExpectError("?", kNothingToRepeat);
  ExpectError("+", kNothingToRepeat);
  ExpectError("{1}", kNothingToRepeat);
  ExpectError("{1,2}", kNothingToRepeat);
  ExpectError("{1,}", kNothingToRepeat);

  // Check that we don't allow more than kMaxCapture captures
  const int kMaxCaptures = 1 << 16;  // Must match RegExpParser::kMaxCaptures.
  const char* kTooManyCaptures = "Too many captures";
  std::ostringstream os;
  for (int i = 0; i <= kMaxCaptures; i++) {
    os << "()";
  }
  ExpectError(os.str().c_str(), kTooManyCaptures);

  const char* kInvalidCaptureName = "Invalid capture group name";
  ExpectError("(?<>.)", kInvalidCaptureName, true);
  ExpectError("(?<1>.)", kInvalidCaptureName, true);
  ExpectError("(?<_%>.)", kInvalidCaptureName, true);
  ExpectError("\\k<a", kInvalidCaptureName, true);
  const char* kDuplicateCaptureName = "Duplicate capture group name";
  ExpectError("(?<a>.)(?<a>.)", kDuplicateCaptureName, true);
  const char* kInvalidUnicodeEscape = "Invalid Unicode escape";
  ExpectError("(?<\\u{FISK}", kInvalidUnicodeEscape, true);
  const char* kInvalidCaptureReferenced = "Invalid named capture referenced";
  ExpectError("\\k<a>", kInvalidCaptureReferenced, true);
  ExpectError("(?<b>)\\k<a>", kInvalidCaptureReferenced, true);
  const char* kInvalidNamedReference = "Invalid named reference";
  ExpectError("\\ka", kInvalidNamedReference, true);
}

static bool IsDigit(base::uc32 c) { return ('0' <= c && c <= '9'); }

static bool NotDigit(base::uc32 c) { return !IsDigit(c); }

static bool NotWhiteSpaceNorLineTermiantor(base::uc32 c) {
  return !IsWhiteSpaceOrLineTerminator(c);
}

static bool NotWord(base::uc32 c) { return !IsRegExpWord(c); }

static bool NotLineTerminator(base::uc32 c) {
  return !unibrow::IsLineTerminator(c);
}

static void TestCharacterClassEscapes(StandardCharacterSet c,
                                      bool(pred)(base::uc32 c)) {
  Zone zone(
      reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent())->allocator(),
      ZONE_NAME);
  ZoneList<CharacterRange>* ranges =
      zone.New<ZoneList<CharacterRange>>(2, &zone);
  CharacterRange::AddClassEscape(c, ranges, false, &zone);
  for (base::uc32 i = 0; i < (1 << 16); i++) {
    bool in_class = false;
    for (int j = 0; !in_class && j < ranges->length(); j++) {
      CharacterRange& range = ranges->at(j);
      in_class = (range.from() <= i && i <= range.to());
    }
    CHECK_EQ(pred(i), in_class);
  }
}

TEST_F(RegExpTest, CharacterClassEscapes) {
  TestCharacterClassEscapes(StandardCharacterSet::kNotLineTerminator,
                            NotLineTerminator);
  TestCharacterClassEscapes(StandardCharacterSet::kDigit, IsDigit);
  TestCharacterClassEscapes(StandardCharacterSet::kNotDigit, NotDigit);
  TestCharacterClassEscapes(StandardCharacterSet::kWhitespace,
                            IsWhiteSpaceOrLineTerminator);
  TestCharacterClassEscapes(StandardCharacterSet::kNotWhitespace,
                            NotWhiteSpaceNorLineTermiantor);
  TestCharacterClassEscapes(StandardCharacterSet::kWord, IsRegExpWord);
  TestCharacterClassEscapes(StandardCharacterSet::kNotWord, NotWord);
}

static RegExpNode* Compile(const char* input, bool multiline, bool unicode,
                           bool is_one_byte, Zone* zone) {
  Isolate* isolate = reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent());
  Handle<String> str = isolate->factory()->NewStringFromAsciiChecked(input);
  RegExpCompileData compile_data;
  compile_data.compilation_target = RegExpCompilationTarget::kNative;
  RegExpFlags flags;
  if (multiline) flags |= RegExpFlag::kMultiline;
  if (unicode) flags |= RegExpFlag::kUnicode;
  if (!RegExpParser::ParseRegExpFromHeapString(isolate, zone, str, flags,
                                               &compile_data)) {
    return nullptr;
  }
  Handle<String> pattern = isolate->factory()
                               ->NewStringFromUtf8(base::CStrVector(input))
                               .ToHandleChecked();
  Handle<String> sample_subject = isolate->factory()
                                      ->NewStringFromUtf8(base::CStrVector(""))
                                      .ToHandleChecked();
  RegExp::CompileForTesting(isolate, zone, &compile_data, flags, pattern,
                            sample_subject, is_one_byte);
  return compile_data.node;
}

static void Execute(const char* input, bool multiline, bool unicode,
                    bool is_one_byte, bool dot_output = false) {
  v8::HandleScope scope(v8::Isolate::GetCurrent());
  Zone zone(
      reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent())->allocator(),
      ZONE_NAME);
  RegExpNode* node = Compile(input, multiline, unicode, is_one_byte, &zone);
  USE(node);
#ifdef DEBUG
  if (dot_output) RegExp::DotPrintForTesting(input, node);
#endif  // DEBUG
}

// Test of debug-only syntax.
#ifdef DEBUG

TEST_F(RegExpTest, ParsePossessiveRepetition) {
  bool old_flag_value = v8_flags.regexp_possessive_quantifier;

  // Enable possessive quantifier syntax.
  v8_flags.regexp_possessive_quantifier = true;

  CheckParseEq("a*+", "(# 0 - p 'a')");
  CheckParseEq("a++", "(# 1 - p 'a')");
  CheckParseEq("a?+", "(# 0 1 p 'a')");
  CheckParseEq("a{10,20}+", "(# 10 20 p 'a')");
  CheckParseEq("za{10,20}+b", "(: 'z' (# 10 20 p 'a') 'b')");

  // Disable possessive quantifier syntax.
  v8_flags.regexp_possessive_quantifier = false;

  CHECK_PARSE_ERROR("a*+");
  CHECK_PARSE_ERROR("a++");
  CHECK_PARSE_ERROR("a?+");
  CHECK_PARSE_ERROR("a{10,20}+");
  CHECK_PARSE_ERROR("a{10,20}+b");

  v8_flags.regexp_possessive_quantifier = old_flag_value;
}

#endif

// Tests of interpreter.

#if V8_TARGET_ARCH_IA32
using ArchRegExpMacroAssembler = RegExpMacroAssemblerIA32;
#elif V8_TARGET_ARCH_X64
using ArchRegExpMacroAssembler = RegExpMacroAssemblerX64;
#elif V8_TARGET_ARCH_ARM
using ArchRegExpMacroAssembler = RegExpMacroAssemblerARM;
#elif V8_TARGET_ARCH_ARM64
using ArchRegExpMacroAssembler = RegExpMacroAssemblerARM64;
#elif V8_TARGET_ARCH_S390
using ArchRegExpMacroAssembler = RegExpMacroAssemblerS390;
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
using ArchRegExpMacroAssembler = RegExpMacroAssemblerPPC;
#elif V8_TARGET_ARCH_MIPS64
using ArchRegExpMacroAssembler = RegExpMacroAssemblerMIPS;
#elif V8_TARGET_ARCH_LOONG64
using ArchRegExpMacroAssembler = RegExpMacroAssemblerLOONG64;
#elif V8_TARGET_ARCH_RISCV64
using ArchRegExpMacroAssembler = RegExpMacroAssemblerRISCV;
#elif V8_TARGET_ARCH_RISCV32
using ArchRegExpMacroAssembler = RegExpMacroAssemblerRISCV;
#endif

class ContextInitializer {
 public:
  ContextInitializer()
      : scope_(v8::Isolate::GetCurrent()),
        env_(v8::Context::New(v8::Isolate::GetCurrent())) {
    env_->Enter();
  }
  ~ContextInitializer() { env_->Exit(); }

 private:
  v8::HandleScope scope_;
  v8::Local<v8::Context> env_;
};

// Create new JSRegExp object with only necessary fields (for this tests)
// initialized.
static Handle<JSRegExp> CreateJSRegExp(Handle<String> source, Handle<Code> code,
                                       bool is_unicode = false) {
  Isolate* isolate = reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent());
  Factory* factory = isolate->factory();
  Handle<JSFunction> constructor = isolate->regexp_function();
  Handle<JSRegExp> regexp =
      Handle<JSRegExp>::cast(factory->NewJSObject(constructor));

  factory->SetRegExpIrregexpData(regexp, source, {}, 0,
                                 JSRegExp::kNoBacktrackLimit);
  const bool is_latin1 = !is_unicode;
  regexp->set_code(is_latin1, code);

  return regexp;
}

static ArchRegExpMacroAssembler::Result Execute(
    Tagged<JSRegExp> regexp, Tagged<String> input, int start_offset,
    Address input_start, Address input_end, int* captures) {
  return static_cast<NativeRegExpMacroAssembler::Result>(
      NativeRegExpMacroAssembler::ExecuteForTesting(
          input, start_offset, reinterpret_cast<uint8_t*>(input_start),
          reinterpret_cast<uint8_t*>(input_end), captures, 0,
          reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent()), regexp));
}

TEST_F(RegExpTest, MacroAssemblerNativeSuccess) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 4);

  m.Succeed();

  Handle<String> source = factory->NewStringFromStaticChars("");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  int captures[4] = {42, 37, 87, 117};
  Handle<String> input = factory->NewStringFromStaticChars("foofoo");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + seq_input->length(), captures);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
  CHECK_EQ(-1, captures[0]);
  CHECK_EQ(-1, captures[1]);
  CHECK_EQ(-1, captures[2]);
  CHECK_EQ(-1, captures[3]);
}

TEST_F(RegExpTest, MacroAssemblerNativeSimple) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 4);

  Label fail, backtrack;
  m.PushBacktrack(&fail);
  m.CheckNotAtStart(0, nullptr);
  m.LoadCurrentCharacter(2, nullptr);
  m.CheckNotCharacter('o', nullptr);
  m.LoadCurrentCharacter(1, nullptr, false);
  m.CheckNotCharacter('o', nullptr);
  m.LoadCurrentCharacter(0, nullptr, false);
  m.CheckNotCharacter('f', nullptr);
  m.WriteCurrentPositionToRegister(0, 0);
  m.WriteCurrentPositionToRegister(1, 3);
  m.AdvanceCurrentPosition(3);
  m.PushBacktrack(&backtrack);
  m.Succeed();
  m.BindJumpTarget(&backtrack);
  m.Backtrack();
  m.BindJumpTarget(&fail);
  m.Fail();

  Handle<String> source = factory->NewStringFromStaticChars("^foo");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  int captures[4] = {42, 37, 87, 117};
  Handle<String> input = factory->NewStringFromStaticChars("foofoo");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), captures);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(3, captures[1]);
  CHECK_EQ(-1, captures[2]);
  CHECK_EQ(-1, captures[3]);

  input = factory->NewStringFromStaticChars("barbarbar");
  seq_input = Handle<SeqOneByteString>::cast(input);
  start_adr = seq_input->GetCharsAddress();

  result = Execute(*regexp, *input, 0, start_adr, start_adr + input->length(),
                   captures);

  CHECK_EQ(NativeRegExpMacroAssembler::FAILURE, result);
}

TEST_F(RegExpTest, MacroAssemblerNativeSimpleUC16) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::UC16, 4);

  Label fail, backtrack;
  m.PushBacktrack(&fail);
  m.CheckNotAtStart(0, nullptr);
  m.LoadCurrentCharacter(2, nullptr);
  m.CheckNotCharacter('o', nullptr);
  m.LoadCurrentCharacter(1, nullptr, false);
  m.CheckNotCharacter('o', nullptr);
  m.LoadCurrentCharacter(0, nullptr, false);
  m.CheckNotCharacter('f', nullptr);
  m.WriteCurrentPositionToRegister(0, 0);
  m.WriteCurrentPositionToRegister(1, 3);
  m.AdvanceCurrentPosition(3);
  m.PushBacktrack(&backtrack);
  m.Succeed();
  m.BindJumpTarget(&backtrack);
  m.Backtrack();
  m.BindJumpTarget(&fail);
  m.Fail();

  Handle<String> source = factory->NewStringFromStaticChars("^foo");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code, true);

  int captures[4] = {42, 37, 87, 117};
  const base::uc16 input_data[6] = {'f', 'o', 'o',
                                    'f', 'o', static_cast<base::uc16>(0x2603)};
  Handle<String> input =
      factory
          ->NewStringFromTwoByte(base::Vector<const base::uc16>(input_data, 6))
          .ToHandleChecked();
  Handle<SeqTwoByteString> seq_input = Handle<SeqTwoByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), captures);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(3, captures[1]);
  CHECK_EQ(-1, captures[2]);
  CHECK_EQ(-1, captures[3]);

  const base::uc16 input_data2[9] = {
      'b', 'a', 'r', 'b', 'a', 'r', 'b', 'a', static_cast<base::uc16>(0x2603)};
  input =
      factory
          ->NewStringFromTwoByte(base::Vector<const base::uc16>(input_data2, 9))
          .ToHandleChecked();
  seq_input = Handle<SeqTwoByteString>::cast(input);
  start_adr = seq_input->GetCharsAddress();

  result = Execute(*regexp, *input, 0, start_adr,
                   start_adr + input->length() * 2, captures);

  CHECK_EQ(NativeRegExpMacroAssembler::FAILURE, result);
}

TEST_F(RegExpTest, MacroAssemblerNativeBacktrack) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 0);

  Label fail;
  Label backtrack;
  m.LoadCurrentCharacter(10, &fail);
  m.Succeed();
  m.BindJumpTarget(&fail);
  m.PushBacktrack(&backtrack);
  m.LoadCurrentCharacter(10, nullptr);
  m.Succeed();
  m.BindJumpTarget(&backtrack);
  m.Fail();

  Handle<String> source = factory->NewStringFromStaticChars("..........");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  Handle<String> input = factory->NewStringFromStaticChars("foofoo");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), nullptr);

  CHECK_EQ(NativeRegExpMacroAssembler::FAILURE, result);
}

TEST_F(RegExpTest, MacroAssemblerNativeBackReferenceLATIN1) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 4);

  m.WriteCurrentPositionToRegister(0, 0);
  m.AdvanceCurrentPosition(2);
  m.WriteCurrentPositionToRegister(1, 0);
  Label nomatch;
  m.CheckNotBackReference(0, false, &nomatch);
  m.Fail();
  m.Bind(&nomatch);
  m.AdvanceCurrentPosition(2);
  Label missing_match;
  m.CheckNotBackReference(0, false, &missing_match);
  m.WriteCurrentPositionToRegister(2, 0);
  m.Succeed();
  m.Bind(&missing_match);
  m.Fail();

  Handle<String> source = factory->NewStringFromStaticChars("^(..)..\1");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  Handle<String> input = factory->NewStringFromStaticChars("fooofo");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  int output[4];
  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), output);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
  CHECK_EQ(0, output[0]);
  CHECK_EQ(2, output[1]);
  CHECK_EQ(6, output[2]);
  CHECK_EQ(-1, output[3]);
}

TEST_F(RegExpTest, MacroAssemblerNativeBackReferenceUC16) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::UC16, 4);

  m.WriteCurrentPositionToRegister(0, 0);
  m.AdvanceCurrentPosition(2);
  m.WriteCurrentPositionToRegister(1, 0);
  Label nomatch;
  m.CheckNotBackReference(0, false, &nomatch);
  m.Fail();
  m.Bind(&nomatch);
  m.AdvanceCurrentPosition(2);
  Label missing_match;
  m.CheckNotBackReference(0, false, &missing_match);
  m.WriteCurrentPositionToRegister(2, 0);
  m.Succeed();
  m.Bind(&missing_match);
  m.Fail();

  Handle<String> source = factory->NewStringFromStaticChars("^(..)..\1");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code, true);

  const base::uc16 input_data[6] = {'f', 0x2028, 'o', 'o', 'f', 0x2028};
  Handle<String> input =
      factory
          ->NewStringFromTwoByte(base::Vector<const base::uc16>(input_data, 6))
          .ToHandleChecked();
  Handle<SeqTwoByteString> seq_input = Handle<SeqTwoByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  int output[4];
  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length() * 2, output);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
  CHECK_EQ(0, output[0]);
  CHECK_EQ(2, output[1]);
  CHECK_EQ(6, output[2]);
  CHECK_EQ(-1, output[3]);
}

TEST_F(RegExpTest, MacroAssemblernativeAtStart) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 0);

  Label not_at_start, newline, fail;
  m.CheckNotAtStart(0, &not_at_start);
  // Check that prevchar = '\n' and current = 'f'.
  m.CheckCharacter('\n', &newline);
  m.BindJumpTarget(&fail);
  m.Fail();
  m.Bind(&newline);
  m.LoadCurrentCharacter(0, &fail);
  m.CheckNotCharacter('f', &fail);
  m.Succeed();

  m.Bind(&not_at_start);
  // Check that prevchar = 'o' and current = 'b'.
  Label prevo;
  m.CheckCharacter('o', &prevo);
  m.Fail();
  m.Bind(&prevo);
  m.LoadCurrentCharacter(0, &fail);
  m.CheckNotCharacter('b', &fail);
  m.Succeed();

  Handle<String> source = factory->NewStringFromStaticChars("(^f|ob)");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  Handle<String> input = factory->NewStringFromStaticChars("foobar");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), nullptr);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);

  result = Execute(*regexp, *input, 3, start_adr + 3,
                   start_adr + input->length(), nullptr);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
}

TEST_F(RegExpTest, MacroAssemblerNativeBackRefNoCase) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 4);

  Label fail, succ;

  m.WriteCurrentPositionToRegister(0, 0);
  m.WriteCurrentPositionToRegister(2, 0);
  m.AdvanceCurrentPosition(3);
  m.WriteCurrentPositionToRegister(3, 0);
  m.CheckNotBackReferenceIgnoreCase(2, false, false, &fail);  // Match "AbC".
  m.CheckNotBackReferenceIgnoreCase(2, false, false, &fail);  // Match "ABC".
  Label expected_fail;
  m.CheckNotBackReferenceIgnoreCase(2, false, false, &expected_fail);
  m.BindJumpTarget(&fail);
  m.Fail();

  m.Bind(&expected_fail);
  m.AdvanceCurrentPosition(3);  // Skip "xYz"
  m.CheckNotBackReferenceIgnoreCase(2, false, false, &succ);
  m.Fail();

  m.Bind(&succ);
  m.WriteCurrentPositionToRegister(1, 0);
  m.Succeed();

  Handle<String> source =
      factory->NewStringFromStaticChars("^(abc)\1\1(?!\1)...(?!\1)");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  Handle<String> input = factory->NewStringFromStaticChars("aBcAbCABCxYzab");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  int output[4];
  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), output);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
  CHECK_EQ(0, output[0]);
  CHECK_EQ(12, output[1]);
  CHECK_EQ(0, output[2]);
  CHECK_EQ(3, output[3]);
}

TEST_F(RegExpTest, MacroAssemblerNativeRegisters) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 6);

  base::uc16 foo_chars[3] = {'f', 'o', 'o'};
  base::Vector<const base::uc16> foo(foo_chars, 3);

  enum registers { out1, out2, out3, out4, out5, out6, sp, loop_cnt };
  Label fail;
  Label backtrack;
  m.WriteCurrentPositionToRegister(out1, 0);  // Output: [0]
  m.PushRegister(out1, RegExpMacroAssembler::kNoStackLimitCheck);
  m.PushBacktrack(&backtrack);
  m.WriteStackPointerToRegister(sp);
  // Fill stack and registers
  m.AdvanceCurrentPosition(2);
  m.WriteCurrentPositionToRegister(out1, 0);
  m.PushRegister(out1, RegExpMacroAssembler::kNoStackLimitCheck);
  m.PushBacktrack(&fail);
  // Drop backtrack stack frames.
  m.ReadStackPointerFromRegister(sp);
  // And take the first backtrack (to &backtrack)
  m.Backtrack();

  m.PushCurrentPosition();
  m.AdvanceCurrentPosition(2);
  m.PopCurrentPosition();

  m.BindJumpTarget(&backtrack);
  m.PopRegister(out1);
  m.ReadCurrentPositionFromRegister(out1);
  m.AdvanceCurrentPosition(3);
  m.WriteCurrentPositionToRegister(out2, 0);  // [0,3]

  Label loop;
  m.SetRegister(loop_cnt, 0);  // loop counter
  m.Bind(&loop);
  m.AdvanceRegister(loop_cnt, 1);
  m.AdvanceCurrentPosition(1);
  m.IfRegisterLT(loop_cnt, 3, &loop);
  m.WriteCurrentPositionToRegister(out3, 0);  // [0,3,6]

  Label loop2;
  m.SetRegister(loop_cnt, 2);  // loop counter
  m.Bind(&loop2);
  m.AdvanceRegister(loop_cnt, -1);
  m.AdvanceCurrentPosition(1);
  m.IfRegisterGE(loop_cnt, 0, &loop2);
  m.WriteCurrentPositionToRegister(out4, 0);  // [0,3,6,9]

  Label loop3;
  Label exit_loop3;
  m.PushRegister(out4, RegExpMacroAssembler::kNoStackLimitCheck);
  m.PushRegister(out4, RegExpMacroAssembler::kNoStackLimitCheck);
  m.ReadCurrentPositionFromRegister(out3);
  m.Bind(&loop3);
  m.AdvanceCurrentPosition(1);
  m.CheckGreedyLoop(&exit_loop3);
  m.GoTo(&loop3);
  m.Bind(&exit_loop3);
  m.PopCurrentPosition();
  m.WriteCurrentPositionToRegister(out5, 0);  // [0,3,6,9,9,-1]

  m.Succeed();

  m.BindJumpTarget(&fail);
  m.Fail();

  Handle<String> source = factory->NewStringFromStaticChars("<loop test>");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  // String long enough for test (content doesn't matter).
  Handle<String> input = factory->NewStringFromStaticChars("foofoofoofoofoo");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  int output[6];
  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), output);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
  CHECK_EQ(0, output[0]);
  CHECK_EQ(3, output[1]);
  CHECK_EQ(6, output[2]);
  CHECK_EQ(9, output[3]);
  CHECK_EQ(9, output[4]);
  CHECK_EQ(-1, output[5]);
}

TEST_F(RegExpTest, MacroAssemblerStackOverflow) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 0);

  Label loop;
  m.Bind(&loop);
  m.PushBacktrack(&loop);
  m.GoTo(&loop);

  Handle<String> source =
      factory->NewStringFromStaticChars("<stack overflow test>");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  // String long enough for test (content doesn't matter).
  Handle<String> input = factory->NewStringFromStaticChars("dummy");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), nullptr);

  CHECK_EQ(NativeRegExpMacroAssembler::EXCEPTION, result);
  CHECK(isolate()->has_exception());
  isolate()->clear_exception();
}

TEST_F(RegExpTest, MacroAssemblerNativeLotsOfRegisters) {
  ContextInitializer initializer;
  Factory* factory = i_isolate()->factory();
  Zone zone(i_isolate()->allocator(), ZONE_NAME);

  ArchRegExpMacroAssembler m(i_isolate(), &zone,
                             NativeRegExpMacroAssembler::LATIN1, 2);

  // At least 2048, to ensure the allocated space for registers
  // span one full page.
  const int large_number = 8000;
  m.WriteCurrentPositionToRegister(large_number, 42);
  m.WriteCurrentPositionToRegister(0, 0);
  m.WriteCurrentPositionToRegister(1, 1);
  Label done;
  m.CheckNotBackReference(0, false, &done);  // Performs a system-stack push.
  m.Bind(&done);
  m.PushRegister(large_number, RegExpMacroAssembler::kNoStackLimitCheck);
  m.PopRegister(1);
  m.Succeed();

  Handle<String> source =
      factory->NewStringFromStaticChars("<huge register space test>");
  Handle<Object> code_object = m.GetCode(source);
  Handle<Code> code = Handle<Code>::cast(code_object);
  Handle<JSRegExp> regexp = CreateJSRegExp(source, code);

  // String long enough for test (content doesn't matter).
  Handle<String> input = factory->NewStringFromStaticChars("sample text");
  Handle<SeqOneByteString> seq_input = Handle<SeqOneByteString>::cast(input);
  Address start_adr = seq_input->GetCharsAddress();

  int captures[2];
  NativeRegExpMacroAssembler::Result result = Execute(
      *regexp, *input, 0, start_adr, start_adr + input->length(), captures);

  CHECK_EQ(NativeRegExpMacroAssembler::SUCCESS, result);
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(42, captures[1]);

  isolate()->clear_exception();
}

TEST_F(RegExpTest, MacroAssembler) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  RegExpBytecodeGenerator m(i_isolate(), &zone);
  // ^f(o)o.
  Label start, fail, backtrack;

  m.SetRegister(4, 42);
  m.PushRegister(4, RegExpMacroAssembler::kNoStackLimitCheck);
  m.AdvanceRegister(4, 42);
  m.GoTo(&start);
  m.Fail();
  m.Bind(&start);
  m.PushBacktrack(&fail);
  m.CheckNotAtStart(0, nullptr);
  m.LoadCurrentCharacter(0, nullptr);
  m.CheckNotCharacter('f', nullptr);
  m.LoadCurrentCharacter(1, nullptr);
  m.CheckNotCharacter('o', nullptr);
  m.LoadCurrentCharacter(2, nullptr);
  m.CheckNotCharacter('o', nullptr);
  m.WriteCurrentPositionToRegister(0, 0);
  m.WriteCurrentPositionToRegister(1, 3);
  m.WriteCurrentPositionToRegister(2, 1);
  m.WriteCurrentPositionToRegister(3, 2);
  m.AdvanceCurrentPosition(3);
  m.PushBacktrack(&backtrack);
  m.Succeed();
  m.BindJumpTarget(&backtrack);
  m.ClearRegisters(2, 3);
  m.Backtrack();
  m.BindJumpTarget(&fail);
  m.PopRegister(0);
  m.Fail();

  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  Handle<String> source = factory->NewStringFromStaticChars("^f(o)o");
  Handle<ByteArray> array = Handle<ByteArray>::cast(m.GetCode(source));
  int captures[5];
  std::memset(captures, 0, sizeof(captures));

  const base::uc16 str1[] = {'f', 'o', 'o', 'b', 'a', 'r'};
  Handle<String> f1_16 =
      factory->NewStringFromTwoByte(base::Vector<const base::uc16>(str1, 6))
          .ToHandleChecked();

  CHECK_EQ(IrregexpInterpreter::SUCCESS,
           IrregexpInterpreter::MatchInternal(
               isolate(), *array, *f1_16, captures, 5, 5, 0,
               RegExp::CallOrigin::kFromRuntime, JSRegExp::kNoBacktrackLimit));
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(3, captures[1]);
  CHECK_EQ(1, captures[2]);
  CHECK_EQ(2, captures[3]);
  CHECK_EQ(84, captures[4]);

  const base::uc16 str2[] = {'b', 'a', 'r', 'f', 'o', 'o'};
  Handle<String> f2_16 =
      factory->NewStringFromTwoByte(base::Vector<const base::uc16>(str2, 6))
          .ToHandleChecked();

  std::memset(captures, 0, sizeof(captures));
  CHECK_EQ(IrregexpInterpreter::FAILURE,
           IrregexpInterpreter::MatchInternal(
               isolate(), *array, *f2_16, captures, 5, 5, 0,
               RegExp::CallOrigin::kFromRuntime, JSRegExp::kNoBacktrackLimit));
  // Failed matches don't alter output registers.
  CHECK_EQ(0, captures[0]);
  CHECK_EQ(0, captures[1]);
  CHECK_EQ(0, captures[2]);
  CHECK_EQ(0, captures[3]);
  CHECK_EQ(0, captures[4]);
}

#ifndef V8_INTL_SUPPORT
static base::uc32 canonicalize(base::uc32 c) {
  unibrow::uchar canon[unibrow::Ecma262Canonicalize::kMaxWidth];
  int count = unibrow::Ecma262Canonicalize::Convert(c, '\0', canon, nullptr);
  if (count == 0) {
    return c;
  } else {
    CHECK_EQ(1, count);
    return canon[0];
  }
}

TEST_F(RegExpTest, LatinCanonicalize) {
  unibrow::Mapping<unibrow::Ecma262UnCanonicalize> un_canonicalize;
  for (unibrow::uchar lower = 'a'; lower <= 'z'; lower++) {
    unibrow::uchar upper = lower + ('A' - 'a');
    CHECK_EQ(canonicalize(lower), canonicalize(upper));
    unibrow::uchar uncanon[unibrow::Ecma262UnCanonicalize::kMaxWidth];
    int length = un_canonicalize.get(lower, '\0', uncanon);
    CHECK_EQ(2, length);
    CHECK_EQ(upper, uncanon[0]);
    CHECK_EQ(lower, uncanon[1]);
  }
  for (base::uc32 c = 128; c < (1 << 21); c++) CHECK_GE(canonicalize(c), 128);
  unibrow::Mapping<unibrow::ToUppercase> to_upper;
  // Canonicalization is only defined for the Basic Multilingual Plane.
  for (base::uc32 c = 0; c < (1 << 16); c++) {
    unibrow::uchar upper[unibrow::ToUppercase::kMaxWidth];
    int length = to_upper.get(c, '\0', upper);
    if (length == 0) {
      length = 1;
      upper[0] = c;
    }
    base::uc32 u = upper[0];
    if (length > 1 || (c >= 128 && u < 128)) u = c;
    CHECK_EQ(u, canonicalize(c));
  }
}

static base::uc32 CanonRangeEnd(base::uc32 c) {
  unibrow::uchar canon[unibrow::CanonicalizationRange::kMaxWidth];
  int count = unibrow::CanonicalizationRange::Convert(c, '\0', canon, nullptr);
  if (count == 0) {
    return c;
  } else {
    CHECK_EQ(1, count);
    return canon[0];
  }
}

TEST_F(RegExpTest, RangeCanonicalization) {
  // Check that we arrive at the same result when using the basic
  // range canonicalization primitives as when using immediate
  // canonicalization.
  unibrow::Mapping<unibrow::Ecma262UnCanonicalize> un_canonicalize;
  int block_start = 0;
  while (block_start <= 0xFFFF) {
    base::uc32 block_end = CanonRangeEnd(block_start);
    unsigned block_length = block_end - block_start + 1;
    if (block_length > 1) {
      unibrow::uchar first[unibrow::Ecma262UnCanonicalize::kMaxWidth];
      int first_length = un_canonicalize.get(block_start, '\0', first);
      for (unsigned i = 1; i < block_length; i++) {
        unibrow::uchar succ[unibrow::Ecma262UnCanonicalize::kMaxWidth];
        int succ_length = un_canonicalize.get(block_start + i, '\0', succ);
        CHECK_EQ(first_length, succ_length);
        for (int j = 0; j < succ_length; j++) {
          int calc = first[j] + i;
          int found = succ[j];
          CHECK_EQ(calc, found);
        }
      }
    }
    block_start = block_start + block_length;
  }
}

TEST_F(RegExpTest, UncanonicalizeEquivalence) {
  unibrow::Mapping<unibrow::Ecma262UnCanonicalize> un_canonicalize;
  unibrow::uchar chars[unibrow::Ecma262UnCanonicalize::kMaxWidth];
  for (int i = 0; i < (1 << 16); i++) {
    int length = un_canonicalize.get(i, '\0', chars);
    for (int j = 0; j < length; j++) {
      unibrow::uchar chars2[unibrow::Ecma262UnCanonicalize::kMaxWidth];
      int length2 = un_canonicalize.get(chars[j], '\0', chars2);
      CHECK_EQ(length, length2);
      for (int k = 0; k < length; k++)
        CHECK_EQ(static_cast<int>(chars[k]), static_cast<int>(chars2[k]));
    }
  }
}

#endif

static void TestRangeCaseIndependence(Isolate* isolate, CharacterRange input,
                                      base::Vector<CharacterRange> expected) {
  Zone zone(
      reinterpret_cast<i::Isolate*>(v8::Isolate::GetCurrent())->allocator(),
      ZONE_NAME);
  int count = expected.length();
  ZoneList<CharacterRange>* list =
      zone.New<ZoneList<CharacterRange>>(count, &zone);
  list->Add(input, &zone);
  CharacterRange::AddCaseEquivalents(isolate, &zone, list, false);
  list->Remove(0);  // Remove the input before checking results.
  CHECK_EQ(count, list->length());
  for (int i = 0; i < list->length(); i++) {
    CHECK_EQ(expected[i].from(), list->at(i).from());
    CHECK_EQ(expected[i].to(), list->at(i).to());
  }
}

static void TestSimpleRangeCaseIndependence(Isolate* isolate,
                                            CharacterRange input,
                                            CharacterRange expected) {
  base::EmbeddedVector<CharacterRange, 1> vector;
  vector[0] = expected;
  TestRangeCaseIndependence(isolate, input, vector);
}

TEST_F(RegExpTest, CharacterRangeCaseIndependence) {
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Singleton('a'),
                                  CharacterRange::Singleton('A'));
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Singleton('z'),
                                  CharacterRange::Singleton('Z'));
#ifndef V8_INTL_SUPPORT
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Range('a', 'z'),
                                  CharacterRange::Range('A', 'Z'));
#endif  // !V8_INTL_SUPPORT
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Range('c', 'f'),
                                  CharacterRange::Range('C', 'F'));
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Range('a', 'b'),
                                  CharacterRange::Range('A', 'B'));
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Range('y', 'z'),
                                  CharacterRange::Range('Y', 'Z'));
#ifndef V8_INTL_SUPPORT
  TestSimpleRangeCaseIndependence(i_isolate(),
                                  CharacterRange::Range('a' - 1, 'z' + 1),
                                  CharacterRange::Range('A', 'Z'));
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Range('A', 'Z'),
                                  CharacterRange::Range('a', 'z'));
#endif  // !V8_INTL_SUPPORT
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Range('C', 'F'),
                                  CharacterRange::Range('c', 'f'));
#ifndef V8_INTL_SUPPORT
  TestSimpleRangeCaseIndependence(i_isolate(),
                                  CharacterRange::Range('A' - 1, 'Z' + 1),
                                  CharacterRange::Range('a', 'z'));
  // Here we need to add [l-z] to complete the case independence of
  // [A-Za-z] but we expect [a-z] to be added since we always add a
  // whole block at a time.
  TestSimpleRangeCaseIndependence(i_isolate(), CharacterRange::Range('A', 'k'),
                                  CharacterRange::Range('a', 'z'));
#endif  // !V8_INTL_SUPPORT
}

static bool InClass(base::uc32 c,
                    const UnicodeRangeSplitter::CharacterRangeVector* ranges) {
  if (ranges == nullptr) return false;
  for (size_t i = 0; i < ranges->size(); i++) {
    CharacterRange range = ranges->at(i);
    if (range.from() <= c && c <= range.to()) return true;
  }
  return false;
}

TEST_F(RegExpTest, UnicodeRangeSplitter) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  ZoneList<CharacterRange>* base = zone.New<ZoneList<CharacterRange>>(1, &zone);
  base->Add(CharacterRange::Everything(), &zone);
  UnicodeRangeSplitter splitter(base);
  // BMP
  for (base::uc32 c = 0; c < 0xD800; c++) {
    CHECK(InClass(c, splitter.bmp()));
    CHECK(!InClass(c, splitter.lead_surrogates()));
    CHECK(!InClass(c, splitter.trail_surrogates()));
    CHECK(!InClass(c, splitter.non_bmp()));
  }
  // Lead surrogates
  for (base::uc32 c = 0xD800; c < 0xDBFF; c++) {
    CHECK(!InClass(c, splitter.bmp()));
    CHECK(InClass(c, splitter.lead_surrogates()));
    CHECK(!InClass(c, splitter.trail_surrogates()));
    CHECK(!InClass(c, splitter.non_bmp()));
  }
  // Trail surrogates
  for (base::uc32 c = 0xDC00; c < 0xDFFF; c++) {
    CHECK(!InClass(c, splitter.bmp()));
    CHECK(!InClass(c, splitter.lead_surrogates()));
    CHECK(InClass(c, splitter.trail_surrogates()));
    CHECK(!InClass(c, splitter.non_bmp()));
  }
  // BMP
  for (base::uc32 c = 0xE000; c < 0xFFFF; c++) {
    CHECK(InClass(c, splitter.bmp()));
    CHECK(!InClass(c, splitter.lead_surrogates()));
    CHECK(!InClass(c, splitter.trail_surrogates()));
    CHECK(!InClass(c, splitter.non_bmp()));
  }
  // Non-BMP
  for (base::uc32 c = 0x10000; c < 0x10FFFF; c++) {
    CHECK(!InClass(c, splitter.bmp()));
    CHECK(!InClass(c, splitter.lead_surrogates()));
    CHECK(!InClass(c, splitter.trail_surrogates()));
    CHECK(InClass(c, splitter.non_bmp()));
  }
}

TEST_F(RegExpTest, CanonicalizeCharacterSets) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  ZoneList<CharacterRange>* list = zone.New<ZoneList<CharacterRange>>(4, &zone);
  CharacterSet set(list);

  list->Add(CharacterRange::Range(10, 20), &zone);
  list->Add(CharacterRange::Range(30, 40), &zone);
  list->Add(CharacterRange::Range(50, 60), &zone);
  set.Canonicalize();
  CHECK_EQ(3, list->length());
  CHECK_EQ(10, list->at(0).from());
  CHECK_EQ(20, list->at(0).to());
  CHECK_EQ(30, list->at(1).from());
  CHECK_EQ(40, list->at(1).to());
  CHECK_EQ(50, list->at(2).from());
  CHECK_EQ(60, list->at(2).to());

  list->Rewind(0);
  list->Add(CharacterRange::Range(10, 20), &zone);
  list->Add(CharacterRange::Range(50, 60), &zone);
  list->Add(CharacterRange::Range(30, 40), &zone);
  set.Canonicalize();
  CHECK_EQ(3, list->length());
  CHECK_EQ(10, list->at(0).from());
  CHECK_EQ(20, list->at(0).to());
  CHECK_EQ(30, list->at(1).from());
  CHECK_EQ(40, list->at(1).to());
  CHECK_EQ(50, list->at(2).from());
  CHECK_EQ(60, list->at(2).to());

  list->Rewind(0);
  list->Add(CharacterRange::Range(30, 40), &zone);
  list->Add(CharacterRange::Range(10, 20), &zone);
  list->Add(CharacterRange::Range(25, 25), &zone);
  list->Add(CharacterRange::Range(100, 100), &zone);
  list->Add(CharacterRange::Range(1, 1), &zone);
  set.Canonicalize();
  CHECK_EQ(5, list->length());
  CHECK_EQ(1, list->at(0).from());
  CHECK_EQ(1, list->at(0).to());
  CHECK_EQ(10, list->at(1).from());
  CHECK_EQ(20, list->at(1).to());
  CHECK_EQ(25, list->at(2).from());
  CHECK_EQ(25, list->at(2).to());
  CHECK_EQ(30, list->at(3).from());
  CHECK_EQ(40, list->at(3).to());
  CHECK_EQ(100, list->at(4).from());
  CHECK_EQ(100, list->at(4).to());

  list->Rewind(0);
  list->Add(CharacterRange::Range(10, 19), &zone);
  list->Add(CharacterRange::Range(21, 30), &zone);
  list->Add(CharacterRange::Range(20, 20), &zone);
  set.Canonicalize();
  CHECK_EQ(1, list->length());
  CHECK_EQ(10, list->at(0).from());
  CHECK_EQ(30, list->at(0).to());
}

TEST_F(RegExpTest, CharacterRangeMerge) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  ZoneList<CharacterRange> l1(4, &zone);
  ZoneList<CharacterRange> l2(4, &zone);
  // Create all combinations of intersections of ranges, both singletons and
  // longer.

  int offset = 0;

  // The five kinds of singleton intersections:
  //     X
  //   Y      - outside before
  //    Y     - outside touching start
  //     Y    - overlap
  //      Y   - outside touching end
  //       Y  - outside after

  for (int i = 0; i < 5; i++) {
    l1.Add(CharacterRange::Singleton(offset + 2), &zone);
    l2.Add(CharacterRange::Singleton(offset + i), &zone);
    offset += 6;
  }

  // The seven kinds of singleton/non-singleton intersections:
  //    XXX
  //  Y        - outside before
  //   Y       - outside touching start
  //    Y      - inside touching start
  //     Y     - entirely inside
  //      Y    - inside touching end
  //       Y   - outside touching end
  //        Y  - disjoint after

  for (int i = 0; i < 7; i++) {
    l1.Add(CharacterRange::Range(offset + 2, offset + 4), &zone);
    l2.Add(CharacterRange::Singleton(offset + i), &zone);
    offset += 8;
  }

  // The eleven kinds of non-singleton intersections:
  //
  //       XXXXXXXX
  // YYYY                  - outside before.
  //   YYYY                - outside touching start.
  //     YYYY              - overlapping start
  //       YYYY            - inside touching start
  //         YYYY          - entirely inside
  //           YYYY        - inside touching end
  //             YYYY      - overlapping end
  //               YYYY    - outside touching end
  //                 YYYY  - outside after
  //       YYYYYYYY        - identical
  //     YYYYYYYYYYYY      - containing entirely.

  for (int i = 0; i < 9; i++) {
    l1.Add(CharacterRange::Range(offset + 6, offset + 15), &zone);  // Length 8.
    l2.Add(CharacterRange::Range(offset + 2 * i, offset + 2 * i + 3), &zone);
    offset += 22;
  }
  l1.Add(CharacterRange::Range(offset + 6, offset + 15), &zone);
  l2.Add(CharacterRange::Range(offset + 6, offset + 15), &zone);
  offset += 22;
  l1.Add(CharacterRange::Range(offset + 6, offset + 15), &zone);
  l2.Add(CharacterRange::Range(offset + 4, offset + 17), &zone);
  offset += 22;

  // Different kinds of multi-range overlap:
  // XXXXXXXXXXXXXXXXXXXXXX         XXXXXXXXXXXXXXXXXXXXXX
  //   YYYY  Y  YYYY  Y  YYYY  Y  YYYY  Y  YYYY  Y  YYYY  Y

  l1.Add(CharacterRange::Range(offset, offset + 21), &zone);
  l1.Add(CharacterRange::Range(offset + 31, offset + 52), &zone);
  for (int i = 0; i < 6; i++) {
    l2.Add(CharacterRange::Range(offset + 2, offset + 5), &zone);
    l2.Add(CharacterRange::Singleton(offset + 8), &zone);
    offset += 9;
  }

  CHECK(CharacterRange::IsCanonical(&l1));
  CHECK(CharacterRange::IsCanonical(&l2));

  ZoneList<CharacterRange> first_only(4, &zone);
  ZoneList<CharacterRange> second_only(4, &zone);
  ZoneList<CharacterRange> both(4, &zone);
}

TEST_F(RegExpTest, Graph) { Execute("\\b\\w+\\b", false, true, true); }

namespace {

int* global_use_counts = nullptr;

void MockUseCounterCallback(v8::Isolate* isolate,
                            v8::Isolate::UseCounterFeature feature) {
  ++global_use_counts[feature];
}

}  // namespace

using RegExpTestWithContext = TestWithContext;
// Test that ES2015+ RegExp compatibility fixes are in place, that they
// are not overly broad, and the appropriate UseCounters are incremented
TEST_F(RegExpTestWithContext, UseCountRegExp) {
  v8::HandleScope scope(isolate());
  int use_counts[v8::Isolate::kUseCounterFeatureCount] = {};
  global_use_counts = use_counts;
  isolate()->SetUseCounterCallback(MockUseCounterCallback);

  // Compat fix: RegExp.prototype.sticky == undefined; UseCounter tracks it
  v8::Local<v8::Value> resultSticky = RunJS("RegExp.prototype.sticky");
  CHECK_EQ(1, use_counts[v8::Isolate::kRegExpPrototypeStickyGetter]);
  CHECK_EQ(0, use_counts[v8::Isolate::kRegExpPrototypeToString]);
  CHECK(resultSticky->IsUndefined());

  // re.sticky has approriate value and doesn't touch UseCounter
  v8::Local<v8::Value> resultReSticky = RunJS("/a/.sticky");
  CHECK_EQ(1, use_counts[v8::Isolate::kRegExpPrototypeStickyGetter]);
  CHECK_EQ(0, use_counts[v8::Isolate::kRegExpPrototypeToString]);
  CHECK(resultReSticky->IsFalse());

  // When the getter is called on another object, throw an exception
  // and don't increment the UseCounter
  v8::Local<v8::Value> resultStickyError = RunJS(
      "var exception;"
      "try { "
      "  Object.getOwnPropertyDescriptor(RegExp.prototype, 'sticky')"
      "      .get.call(null);"
      "} catch (e) {"
      "  exception = e;"
      "}"
      "exception");
  CHECK_EQ(1, use_counts[v8::Isolate::kRegExpPrototypeStickyGetter]);
  CHECK_EQ(0, use_counts[v8::Isolate::kRegExpPrototypeToString]);
  CHECK(resultStickyError->IsObject());

  // RegExp.prototype.toString() returns '/(?:)/' as a compatibility fix;
  // a UseCounter is incremented to track it.
  v8::Local<v8::Value> resultToString =
      RunJS("RegExp.prototype.toString().length");
  CHECK_EQ(2, use_counts[v8::Isolate::kRegExpPrototypeStickyGetter]);
  CHECK_EQ(1, use_counts[v8::Isolate::kRegExpPrototypeToString]);
  CHECK(resultToString->IsInt32());
  CHECK_EQ(
      6, resultToString->Int32Value(isolate()->GetCurrentContext()).FromJust());

  // .toString() works on normal RegExps
  v8::Local<v8::Value> resultReToString = RunJS("/a/.toString().length");
  CHECK_EQ(2, use_counts[v8::Isolate::kRegExpPrototypeStickyGetter]);
  CHECK_EQ(1, use_counts[v8::Isolate::kRegExpPrototypeToString]);
  CHECK(resultReToString->IsInt32());
  CHECK_EQ(
      3,
      resultReToString->Int32Value(isolate()->GetCurrentContext()).FromJust());

  // .toString() throws on non-RegExps that aren't RegExp.prototype
  v8::Local<v8::Value> resultToStringError = RunJS(
      "var exception;"
      "try { RegExp.prototype.toString.call(null) }"
      "catch (e) { exception = e; }"
      "exception");
  CHECK_EQ(2, use_counts[v8::Isolate::kRegExpPrototypeStickyGetter]);
  CHECK_EQ(1, use_counts[v8::Isolate::kRegExpPrototypeToString]);
  CHECK(resultToStringError->IsObject());
}

class UncachedExternalStringResource
    : public v8::String::ExternalOneByteStringResource {
 public:
  const char* data() const override { return "abcdefghijklmnopqrstuvwxyz"; }
  size_t length() const override { return 26; }
  bool IsCacheable() const override { return false; }
};

TEST_F(RegExpTestWithContext, UncachedExternalString) {
  v8::HandleScope scope(isolate());
  v8::Local<v8::String> external =
      v8::String::NewExternalOneByte(isolate(),
                                     new UncachedExternalStringResource())
          .ToLocalChecked();
  CHECK_EQ(v8::Utils::OpenDirectHandle(*external)->map(),
           ReadOnlyRoots(i_isolate()).uncached_external_one_byte_string_map());
  v8::Local<v8::Object> global = context()->Global();
  global->Set(context(), NewString("external"), external).FromJust();
  RunJS("var re = /y(.)/; re.test('ab');");
  // TODO(v8:12781): once IsString matcher is added in
  // gmock-support.h, we could replace this.
  Local<Value> result = RunJS("external.substring(1).match(re)[1]");
  CHECK(result->IsString());
  v8::String::Utf8Value utf8(isolate(), result);
  CHECK_EQ(0, strcmp("z", *utf8));
}

// Test bytecode peephole optimization

void CreatePeepholeNoChangeBytecode(RegExpMacroAssembler* m) {
  Label fail, backtrack;
  m->PushBacktrack(&fail);
  m->CheckNotAtStart(0, nullptr);
  m->LoadCurrentCharacter(2, nullptr);
  m->CheckNotCharacter('o', nullptr);
  m->LoadCurrentCharacter(1, nullptr, false);
  m->CheckNotCharacter('o', nullptr);
  m->LoadCurrentCharacter(0, nullptr, false);
  m->CheckNotCharacter('f', nullptr);
  m->WriteCurrentPositionToRegister(0, 0);
  m->WriteCurrentPositionToRegister(1, 3);
  m->AdvanceCurrentPosition(3);
  m->PushBacktrack(&backtrack);
  m->Succeed();
  m->Bind(&backtrack);
  m->Backtrack();
  m->Bind(&fail);
  m->Fail();
}

TEST_F(RegExpTest, PeepholeNoChange) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  CreatePeepholeNoChangeBytecode(&orig);
  CreatePeepholeNoChangeBytecode(&opt);

  Handle<String> source = factory->NewStringFromStaticChars("^foo");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));
  int length = array->length();
  uint8_t* byte_array = array->begin();

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));
  uint8_t* byte_array_optimized = array_optimized->begin();

  CHECK_EQ(0, memcmp(byte_array, byte_array_optimized, length));
}

void CreatePeepholeSkipUntilCharBytecode(RegExpMacroAssembler* m) {
  Label start;
  m->Bind(&start);
  m->LoadCurrentCharacter(0, nullptr, true);
  m->CheckCharacter('x', nullptr);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&start);
}

TEST_F(RegExpTest, PeepholeSkipUntilChar) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  CreatePeepholeSkipUntilCharBytecode(&orig);
  CreatePeepholeSkipUntilCharBytecode(&opt);

  Handle<String> source = factory->NewStringFromStaticChars("dummy");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));
  int length = array->length();

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));
  int length_optimized = array_optimized->length();

  int length_expected = RegExpBytecodeLength(BC_LOAD_CURRENT_CHAR) +
                        RegExpBytecodeLength(BC_CHECK_CHAR) +
                        RegExpBytecodeLength(BC_ADVANCE_CP_AND_GOTO) +
                        RegExpBytecodeLength(BC_POP_BT);
  int length_optimized_expected = RegExpBytecodeLength(BC_SKIP_UNTIL_CHAR) +
                                  RegExpBytecodeLength(BC_POP_BT);

  CHECK_EQ(length, length_expected);
  CHECK_EQ(length_optimized, length_optimized_expected);

  CHECK_EQ(BC_SKIP_UNTIL_CHAR, array_optimized->get(0));
  CHECK_EQ(BC_POP_BT,
           array_optimized->get(RegExpBytecodeLength(BC_SKIP_UNTIL_CHAR)));
}

void CreatePeepholeSkipUntilBitInTableBytecode(RegExpMacroAssembler* m,
                                               Factory* factory) {
  Handle<ByteArray> bit_table = factory->NewByteArray(
      RegExpMacroAssembler::kTableSize, AllocationType::kOld);
  for (uint32_t i = 0; i < RegExpMacroAssembler::kTableSize; i++) {
    bit_table->set(i, 0);
  }

  Label start;
  m->Bind(&start);
  m->LoadCurrentCharacter(0, nullptr, true);
  m->CheckBitInTable(bit_table, nullptr);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&start);
}

TEST_F(RegExpTest, PeepholeSkipUntilBitInTable) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  CreatePeepholeSkipUntilBitInTableBytecode(&orig, factory);
  CreatePeepholeSkipUntilBitInTableBytecode(&opt, factory);

  Handle<String> source = factory->NewStringFromStaticChars("dummy");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));
  int length = array->length();

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));
  int length_optimized = array_optimized->length();

  int length_expected = RegExpBytecodeLength(BC_LOAD_CURRENT_CHAR) +
                        RegExpBytecodeLength(BC_CHECK_BIT_IN_TABLE) +
                        RegExpBytecodeLength(BC_ADVANCE_CP_AND_GOTO) +
                        RegExpBytecodeLength(BC_POP_BT);
  int length_optimized_expected =
      RegExpBytecodeLength(BC_SKIP_UNTIL_BIT_IN_TABLE) +
      RegExpBytecodeLength(BC_POP_BT);

  CHECK_EQ(length, length_expected);
  CHECK_EQ(length_optimized, length_optimized_expected);

  CHECK_EQ(BC_SKIP_UNTIL_BIT_IN_TABLE, array_optimized->get(0));
  CHECK_EQ(BC_POP_BT, array_optimized->get(
                          RegExpBytecodeLength(BC_SKIP_UNTIL_BIT_IN_TABLE)));
}

void CreatePeepholeSkipUntilCharPosCheckedBytecode(RegExpMacroAssembler* m) {
  Label start;
  m->Bind(&start);
  m->LoadCurrentCharacter(0, nullptr, true, 1, 2);
  m->CheckCharacter('x', nullptr);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&start);
}

TEST_F(RegExpTest, PeepholeSkipUntilCharPosChecked) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  CreatePeepholeSkipUntilCharPosCheckedBytecode(&orig);
  CreatePeepholeSkipUntilCharPosCheckedBytecode(&opt);

  Handle<String> source = factory->NewStringFromStaticChars("dummy");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));
  int length = array->length();

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));
  int length_optimized = array_optimized->length();

  int length_expected = RegExpBytecodeLength(BC_CHECK_CURRENT_POSITION) +
                        RegExpBytecodeLength(BC_LOAD_CURRENT_CHAR_UNCHECKED) +
                        RegExpBytecodeLength(BC_CHECK_CHAR) +
                        RegExpBytecodeLength(BC_ADVANCE_CP_AND_GOTO) +
                        RegExpBytecodeLength(BC_POP_BT);
  int length_optimized_expected =
      RegExpBytecodeLength(BC_SKIP_UNTIL_CHAR_POS_CHECKED) +
      RegExpBytecodeLength(BC_POP_BT);

  CHECK_EQ(length, length_expected);
  CHECK_EQ(length_optimized, length_optimized_expected);

  CHECK_EQ(BC_SKIP_UNTIL_CHAR_POS_CHECKED, array_optimized->get(0));
  CHECK_EQ(BC_POP_BT, array_optimized->get(RegExpBytecodeLength(
                          BC_SKIP_UNTIL_CHAR_POS_CHECKED)));
}

void CreatePeepholeSkipUntilCharAndBytecode(RegExpMacroAssembler* m) {
  Label start;
  m->Bind(&start);
  m->LoadCurrentCharacter(0, nullptr, true, 1, 2);
  m->CheckCharacterAfterAnd('x', 0xFF, nullptr);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&start);
}

TEST_F(RegExpTest, PeepholeSkipUntilCharAnd) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  CreatePeepholeSkipUntilCharAndBytecode(&orig);
  CreatePeepholeSkipUntilCharAndBytecode(&opt);

  Handle<String> source = factory->NewStringFromStaticChars("dummy");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));
  int length = array->length();

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));
  int length_optimized = array_optimized->length();

  int length_expected = RegExpBytecodeLength(BC_CHECK_CURRENT_POSITION) +
                        RegExpBytecodeLength(BC_LOAD_CURRENT_CHAR_UNCHECKED) +
                        RegExpBytecodeLength(BC_AND_CHECK_CHAR) +
                        RegExpBytecodeLength(BC_ADVANCE_CP_AND_GOTO) +
                        RegExpBytecodeLength(BC_POP_BT);
  int length_optimized_expected = RegExpBytecodeLength(BC_SKIP_UNTIL_CHAR_AND) +
                                  RegExpBytecodeLength(BC_POP_BT);

  CHECK_EQ(length, length_expected);
  CHECK_EQ(length_optimized, length_optimized_expected);

  CHECK_EQ(BC_SKIP_UNTIL_CHAR_AND, array_optimized->get(0));
  CHECK_EQ(BC_POP_BT,
           array_optimized->get(RegExpBytecodeLength(BC_SKIP_UNTIL_CHAR_AND)));
}

void CreatePeepholeSkipUntilCharOrCharBytecode(RegExpMacroAssembler* m) {
  Label start;
  m->Bind(&start);
  m->LoadCurrentCharacter(0, nullptr, true);
  m->CheckCharacter('x', nullptr);
  m->CheckCharacter('y', nullptr);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&start);
}

TEST_F(RegExpTest, PeepholeSkipUntilCharOrChar) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  CreatePeepholeSkipUntilCharOrCharBytecode(&orig);
  CreatePeepholeSkipUntilCharOrCharBytecode(&opt);

  Handle<String> source = factory->NewStringFromStaticChars("dummy");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));
  int length = array->length();

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));
  int length_optimized = array_optimized->length();

  int length_expected = RegExpBytecodeLength(BC_LOAD_CURRENT_CHAR) +
                        RegExpBytecodeLength(BC_CHECK_CHAR) +
                        RegExpBytecodeLength(BC_CHECK_CHAR) +
                        RegExpBytecodeLength(BC_ADVANCE_CP_AND_GOTO) +
                        RegExpBytecodeLength(BC_POP_BT);
  int length_optimized_expected =
      RegExpBytecodeLength(BC_SKIP_UNTIL_CHAR_OR_CHAR) +
      RegExpBytecodeLength(BC_POP_BT);

  CHECK_EQ(length, length_expected);
  CHECK_EQ(length_optimized, length_optimized_expected);

  CHECK_EQ(BC_SKIP_UNTIL_CHAR_OR_CHAR, array_optimized->get(0));
  CHECK_EQ(BC_POP_BT, array_optimized->get(
                          RegExpBytecodeLength(BC_SKIP_UNTIL_CHAR_OR_CHAR)));
}

void CreatePeepholeSkipUntilGtOrNotBitInTableBytecode(RegExpMacroAssembler* m,
                                                      Factory* factory) {
  Handle<ByteArray> bit_table = factory->NewByteArray(
      RegExpMacroAssembler::kTableSize, AllocationType::kOld);
  for (uint32_t i = 0; i < RegExpMacroAssembler::kTableSize; i++) {
    bit_table->set(i, 0);
  }

  Label start, end, advance;
  m->Bind(&start);
  m->LoadCurrentCharacter(0, nullptr, true);
  m->CheckCharacterGT('x', nullptr);
  m->CheckBitInTable(bit_table, &advance);
  m->GoTo(&end);
  m->Bind(&advance);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&start);
  m->Bind(&end);
}

TEST_F(RegExpTest, PeepholeSkipUntilGtOrNotBitInTable) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  CreatePeepholeSkipUntilGtOrNotBitInTableBytecode(&orig, factory);
  CreatePeepholeSkipUntilGtOrNotBitInTableBytecode(&opt, factory);

  Handle<String> source = factory->NewStringFromStaticChars("dummy");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));
  int length = array->length();

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));
  int length_optimized = array_optimized->length();

  int length_expected = RegExpBytecodeLength(BC_LOAD_CURRENT_CHAR) +
                        RegExpBytecodeLength(BC_CHECK_GT) +
                        RegExpBytecodeLength(BC_CHECK_BIT_IN_TABLE) +
                        RegExpBytecodeLength(BC_GOTO) +
                        RegExpBytecodeLength(BC_ADVANCE_CP_AND_GOTO) +
                        RegExpBytecodeLength(BC_POP_BT);
  int length_optimized_expected =
      RegExpBytecodeLength(BC_SKIP_UNTIL_GT_OR_NOT_BIT_IN_TABLE) +
      RegExpBytecodeLength(BC_POP_BT);

  CHECK_EQ(length, length_expected);
  CHECK_EQ(length_optimized, length_optimized_expected);

  CHECK_EQ(BC_SKIP_UNTIL_GT_OR_NOT_BIT_IN_TABLE, array_optimized->get(0));
  CHECK_EQ(BC_POP_BT, array_optimized->get(RegExpBytecodeLength(
                          BC_SKIP_UNTIL_GT_OR_NOT_BIT_IN_TABLE)));
}

void CreatePeepholeLabelFixupsInsideBytecode(RegExpMacroAssembler* m,
                                             Label* dummy_before,
                                             Label* dummy_after,
                                             Label* dummy_inside) {
  Label loop;
  m->Bind(dummy_before);
  m->LoadCurrentCharacter(0, dummy_before);
  m->CheckCharacter('a', dummy_after);
  m->CheckCharacter('b', dummy_inside);
  m->Bind(&loop);
  m->LoadCurrentCharacter(0, nullptr, true);
  m->CheckCharacter('x', nullptr);
  m->Bind(dummy_inside);
  m->CheckCharacter('y', nullptr);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&loop);
  m->Bind(dummy_after);
  m->LoadCurrentCharacter(0, dummy_before);
  m->CheckCharacter('a', dummy_after);
  m->CheckCharacter('b', dummy_inside);
}

TEST_F(RegExpTest, PeepholeLabelFixupsInside) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  {
    Label dummy_before, dummy_after, dummy_inside;
    CreatePeepholeLabelFixupsInsideBytecode(&opt, &dummy_before, &dummy_after,
                                            &dummy_inside);
  }
  Label dummy_before, dummy_after, dummy_inside;
  CreatePeepholeLabelFixupsInsideBytecode(&orig, &dummy_before, &dummy_after,
                                          &dummy_inside);

  CHECK_EQ(0x00, dummy_before.pos());
  CHECK_EQ(0x28, dummy_inside.pos());
  CHECK_EQ(0x38, dummy_after.pos());

  const Label* labels[] = {&dummy_before, &dummy_after, &dummy_inside};
  const int label_positions[4][3] = {
      {0x04, 0x3C},  // dummy_before
      {0x0C, 0x44},  // dummy after
      {0x14, 0x4C}   // dummy inside
  };

  Handle<String> source = factory->NewStringFromStaticChars("dummy");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));

  for (int label_idx = 0; label_idx < 3; label_idx++) {
    for (int pos_idx = 0; pos_idx < 2; pos_idx++) {
      CHECK_EQ(labels[label_idx]->pos(),
               array->get(label_positions[label_idx][pos_idx]));
    }
  }

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));

  const int pos_fixups[] = {
      0,  // Position before optimization should be unchanged.
      4,  // Position after first replacement should be 4 (optimized size (20) -
          // original size (32) + preserve length (16)).
  };
  const int target_fixups[] = {
      0,  // dummy_before should be unchanged
      4,  // dummy_inside should be 4
      4   // dummy_after should be 4
  };

  for (int label_idx = 0; label_idx < 3; label_idx++) {
    for (int pos_idx = 0; pos_idx < 2; pos_idx++) {
      int label_pos = label_positions[label_idx][pos_idx] + pos_fixups[pos_idx];
      int jump_address =
          *reinterpret_cast<uint32_t*>(array_optimized->begin() + label_pos);
      int expected_jump_address =
          labels[label_idx]->pos() + target_fixups[label_idx];
      CHECK_EQ(expected_jump_address, jump_address);
    }
  }
}

void CreatePeepholeLabelFixupsComplexBytecode(RegExpMacroAssembler* m,
                                              Label* dummy_before,
                                              Label* dummy_between,
                                              Label* dummy_after,
                                              Label* dummy_inside) {
  Label loop1, loop2;
  m->Bind(dummy_before);
  m->LoadCurrentCharacter(0, dummy_before);
  m->CheckCharacter('a', dummy_between);
  m->CheckCharacter('b', dummy_after);
  m->CheckCharacter('c', dummy_inside);
  m->Bind(&loop1);
  m->LoadCurrentCharacter(0, nullptr, true);
  m->CheckCharacter('x', nullptr);
  m->CheckCharacter('y', nullptr);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&loop1);
  m->Bind(dummy_between);
  m->LoadCurrentCharacter(0, dummy_before);
  m->CheckCharacter('a', dummy_between);
  m->CheckCharacter('b', dummy_after);
  m->CheckCharacter('c', dummy_inside);
  m->Bind(&loop2);
  m->LoadCurrentCharacter(0, nullptr, true);
  m->CheckCharacter('x', nullptr);
  m->Bind(dummy_inside);
  m->CheckCharacter('y', nullptr);
  m->AdvanceCurrentPosition(1);
  m->GoTo(&loop2);
  m->Bind(dummy_after);
  m->LoadCurrentCharacter(0, dummy_before);
  m->CheckCharacter('a', dummy_between);
  m->CheckCharacter('b', dummy_after);
  m->CheckCharacter('c', dummy_inside);
}

TEST_F(RegExpTest, PeepholeLabelFixupsComplex) {
  Zone zone(i_isolate()->allocator(), ZONE_NAME);
  Factory* factory = i_isolate()->factory();
  HandleScope scope(i_isolate());

  RegExpBytecodeGenerator orig(i_isolate(), &zone);
  RegExpBytecodeGenerator opt(i_isolate(), &zone);

  {
    Label dummy_before, dummy_between, dummy_after, dummy_inside;
    CreatePeepholeLabelFixupsComplexBytecode(
        &opt, &dummy_before, &dummy_between, &dummy_after, &dummy_inside);
  }
  Label dummy_before, dummy_between, dummy_after, dummy_inside;
  CreatePeepholeLabelFixupsComplexBytecode(&orig, &dummy_before, &dummy_between,
                                           &dummy_after, &dummy_inside);

  CHECK_EQ(0x00, dummy_before.pos());
  CHECK_EQ(0x40, dummy_between.pos());
  CHECK_EQ(0x70, dummy_inside.pos());
  CHECK_EQ(0x80, dummy_after.pos());

  const Label* labels[] = {&dummy_before, &dummy_between, &dummy_after,
                           &dummy_inside};
  const int label_positions[4][3] = {
      {0x04, 0x44, 0x84},  // dummy_before
      {0x0C, 0x4C, 0x8C},  // dummy between
      {0x14, 0x54, 0x94},  // dummy after
      {0x1C, 0x5C, 0x9C}   // dummy inside
  };

  Handle<String> source = factory->NewStringFromStaticChars("dummy");

  v8_flags.regexp_peephole_optimization = false;
  Handle<ByteArray> array = Handle<ByteArray>::cast(orig.GetCode(source));

  for (int label_idx = 0; label_idx < 4; label_idx++) {
    for (int pos_idx = 0; pos_idx < 3; pos_idx++) {
      CHECK_EQ(labels[label_idx]->pos(),
               array->get(label_positions[label_idx][pos_idx]));
    }
  }

  v8_flags.regexp_peephole_optimization = true;
  Handle<ByteArray> array_optimized =
      Handle<ByteArray>::cast(opt.GetCode(source));

  const int pos_fixups[] = {
      0,    // Position before optimization should be unchanged.
      -12,  // Position after first replacement should be -12 (optimized size =
            // 20 - 32 = original size).
      -8    // Position after second replacement should be -8 (-12 from first
            // optimization -12 from second optimization + 16 preserved
            // bytecodes).
  };
  const int target_fixups[] = {
      0,    // dummy_before should be unchanged
      -12,  // dummy_between should be -12
      -8,   // dummy_inside should be -8
      -8    // dummy_after should be -8
  };

  for (int label_idx = 0; label_idx < 4; label_idx++) {
    for (int pos_idx = 0; pos_idx < 3; pos_idx++) {
      int label_pos = label_positions[label_idx][pos_idx] + pos_fixups[pos_idx];
      int jump_address =
          *reinterpret_cast<uint32_t*>(array_optimized->begin() + label_pos);
      int expected_jump_address =
          labels[label_idx]->pos() + target_fixups[label_idx];
      CHECK_EQ(expected_jump_address, jump_address);
    }
  }
}

TEST_F(RegExpTestWithContext, UnicodePropertyEscapeCodeSize) {
  FlagScope<bool> f(&v8_flags.regexp_tier_up, false);

  v8::HandleScope scope(isolate());
  i::Handle<i::JSRegExp> re = Utils::OpenHandle(
      *RunJS("const r = /\\p{L}\\p{L}\\p{L}/u; r.exec('\\u200b'); r;")
           .As<v8::RegExp>());

  static constexpr int kMaxSize = 200 * KB;
  static constexpr bool kIsNotLatin1 = false;
  Tagged<Object> maybe_code = re->code(i_isolate(), kIsNotLatin1);
  Tagged<Object> maybe_bytecode = re->bytecode(kIsNotLatin1);
  if (IsByteArray(maybe_bytecode)) {
    // On x64, excessive inlining produced >250KB.
    CHECK_LT(ByteArray::cast(maybe_bytecode)->AllocatedSize(), kMaxSize);
  } else if (IsCode(maybe_code)) {
    // On x64, excessive inlining produced >360KB.
    CHECK_LT(Code::cast(maybe_code)->Size(), kMaxSize);
    CHECK_EQ(Code::cast(maybe_code)->kind(), CodeKind::REGEXP);
  } else {
    UNREACHABLE();
  }
}

namespace {

struct RegExpExecData {
  i::Isolate* isolate;
  i::Handle<i::JSRegExp> regexp;
  i::Handle<i::String> subject;
};

i::Handle<i::Object> RegExpExec(const RegExpExecData* d) {
  return i::RegExp::Exec(d->isolate, d->regexp, d->subject, 0,
                         d->isolate->regexp_last_match_info())
      .ToHandleChecked();
}

void ReenterRegExp(v8::Isolate* isolate, void* data) {
  RegExpExecData* d = static_cast<RegExpExecData*>(data);
  i::Handle<i::Object> result = RegExpExec(d);
  CHECK(IsNull(*result));
}

}  // namespace

// Tests reentrant irregexp calls.
TEST_F(RegExpTestWithContext, RegExpInterruptReentrantExecution) {
  CHECK(!v8_flags.jitless);
  v8_flags.regexp_tier_up = false;  // Enter irregexp, not the interpreter.

  v8::HandleScope scope(isolate());

  RegExpExecData d;
  d.isolate = reinterpret_cast<i::Isolate*>(isolate());
  d.regexp = v8::Utils::OpenHandle(
      *v8::RegExp::New(context(), NewString("(a*)*x"), v8::RegExp::kNone)
           .ToLocalChecked());
  d.subject = v8::Utils::OpenHandle(*NewString("aaaa"));

  isolate()->RequestInterrupt(&ReenterRegExp, &d);

  i::Handle<i::Object> result = RegExpExec(&d);
  CHECK(IsNull(*result));
}

#undef CHECK_PARSE_ERROR
#undef CHECK_SIMPLE
#undef CHECK_MIN_MAX

}  // namespace internal
}  // namespace v8
