// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/str_split.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"

namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST(Split, TraitsTest) {
  static_assert(!absl::strings_internal::SplitterIsConvertibleTo<int>::value,
                "");
  static_assert(
      !absl::strings_internal::SplitterIsConvertibleTo<std::string>::value, "");
  static_assert(absl::strings_internal::SplitterIsConvertibleTo<
                    std::vector<std::string>>::value,
                "");
  static_assert(
      !absl::strings_internal::SplitterIsConvertibleTo<std::vector<int>>::value,
      "");
  static_assert(absl::strings_internal::SplitterIsConvertibleTo<
                    std::vector<absl::string_view>>::value,
                "");
  static_assert(absl::strings_internal::SplitterIsConvertibleTo<
                    std::map<std::string, std::string>>::value,
                "");
  static_assert(absl::strings_internal::SplitterIsConvertibleTo<
                    std::map<absl::string_view, absl::string_view>>::value,
                "");
  static_assert(!absl::strings_internal::SplitterIsConvertibleTo<
                    std::map<int, std::string>>::value,
                "");
  static_assert(!absl::strings_internal::SplitterIsConvertibleTo<
                    std::map<std::string, int>>::value,
                "");
}

// This tests the overall split API, which is made up of the absl::StrSplit()
// function and the Delimiter objects in the absl:: namespace.
// This TEST macro is outside of any namespace to require full specification of
// namespaces just like callers will need to use.
TEST(Split, APIExamples) {
  {
    // Passes string delimiter. Assumes the default of ByString.
    std::vector<std::string> v = absl::StrSplit("a,b,c", ",");  // NOLINT
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));

    // Equivalent to...
    using absl::ByString;
    v = absl::StrSplit("a,b,c", ByString(","));
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));

    // Equivalent to...
    EXPECT_THAT(absl::StrSplit("a,b,c", ByString(",")),
                ElementsAre("a", "b", "c"));
  }

  {
    // Same as above, but using a single character as the delimiter.
    std::vector<std::string> v = absl::StrSplit("a,b,c", ',');
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));

    // Equivalent to...
    using absl::ByChar;
    v = absl::StrSplit("a,b,c", ByChar(','));
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // Uses the Literal string "=>" as the delimiter.
    const std::vector<std::string> v = absl::StrSplit("a=>b=>c", "=>");
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // The substrings are returned as string_views, eliminating copying.
    std::vector<absl::string_view> v = absl::StrSplit("a,b,c", ',');
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // Leading and trailing empty substrings.
    std::vector<std::string> v = absl::StrSplit(",a,b,c,", ',');
    EXPECT_THAT(v, ElementsAre("", "a", "b", "c", ""));
  }

  {
    // Splits on a delimiter that is not found.
    std::vector<std::string> v = absl::StrSplit("abc", ',');
    EXPECT_THAT(v, ElementsAre("abc"));
  }

  {
    // Splits the input string into individual characters by using an empty
    // string as the delimiter.
    std::vector<std::string> v = absl::StrSplit("abc", "");
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // Splits string data with embedded NUL characters, using NUL as the
    // delimiter. A simple delimiter of "\0" doesn't work because strlen() will
    // say that's the empty string when constructing the absl::string_view
    // delimiter. Instead, a non-empty string containing NUL can be used as the
    // delimiter.
    std::string embedded_nulls("a\0b\0c", 5);
    std::string null_delim("\0", 1);
    std::vector<std::string> v = absl::StrSplit(embedded_nulls, null_delim);
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // Stores first two split strings as the members in a std::pair.
    std::pair<std::string, std::string> p = absl::StrSplit("a,b,c", ',');
    EXPECT_EQ("a", p.first);
    EXPECT_EQ("b", p.second);
    // "c" is omitted because std::pair can hold only two elements.
  }

  {
    // Results stored in std::set<std::string>
    std::set<std::string> v = absl::StrSplit("a,b,c,a,b,c,a,b,c", ',');
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // Uses a non-const char* delimiter.
    char a[] = ",";
    char* d = a + 0;
    std::vector<std::string> v = absl::StrSplit("a,b,c", d);
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // Results split using either of , or ;
    using absl::ByAnyChar;
    std::vector<std::string> v = absl::StrSplit("a,b;c", ByAnyChar(",;"));
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // Uses the SkipWhitespace predicate.
    using absl::SkipWhitespace;
    std::vector<std::string> v =
        absl::StrSplit(" a , ,,b,", ',', SkipWhitespace());
    EXPECT_THAT(v, ElementsAre(" a ", "b"));
  }

  {
    // Uses the ByLength delimiter.
    using absl::ByLength;
    std::vector<std::string> v = absl::StrSplit("abcdefg", ByLength(3));
    EXPECT_THAT(v, ElementsAre("abc", "def", "g"));
  }

  {
    // Different forms of initialization / conversion.
    std::vector<std::string> v1 = absl::StrSplit("a,b,c", ',');
    EXPECT_THAT(v1, ElementsAre("a", "b", "c"));
    std::vector<std::string> v2(absl::StrSplit("a,b,c", ','));
    EXPECT_THAT(v2, ElementsAre("a", "b", "c"));
    auto v3 = std::vector<std::string>(absl::StrSplit("a,b,c", ','));
    EXPECT_THAT(v3, ElementsAre("a", "b", "c"));
    v3 = absl::StrSplit("a,b,c", ',');
    EXPECT_THAT(v3, ElementsAre("a", "b", "c"));
  }

  {
    // Results stored in a std::map.
    std::map<std::string, std::string> m = absl::StrSplit("a,1,b,2,a,3", ',');
    EXPECT_EQ(2, m.size());
    EXPECT_EQ("3", m["a"]);
    EXPECT_EQ("2", m["b"]);
  }

  {
    // Results stored in a std::multimap.
    std::multimap<std::string, std::string> m =
        absl::StrSplit("a,1,b,2,a,3", ',');
    EXPECT_EQ(3, m.size());
    auto it = m.find("a");
    EXPECT_EQ("1", it->second);
    ++it;
    EXPECT_EQ("3", it->second);
    it = m.find("b");
    EXPECT_EQ("2", it->second);
  }

  {
    // Demonstrates use in a range-based for loop in C++11.
    std::string s = "x,x,x,x,x,x,x";
    for (absl::string_view sp : absl::StrSplit(s, ',')) {
      EXPECT_EQ("x", sp);
    }
  }

  {
    // Demonstrates use with a Predicate in a range-based for loop.
    using absl::SkipWhitespace;
    std::string s = " ,x,,x,,x,x,x,,";
    for (absl::string_view sp : absl::StrSplit(s, ',', SkipWhitespace())) {
      EXPECT_EQ("x", sp);
    }
  }

  {
    // Demonstrates a "smart" split to std::map using two separate calls to
    // absl::StrSplit. One call to split the records, and another call to split
    // the keys and values. This also uses the Limit delimiter so that the
    // std::string "a=b=c" will split to "a" -> "b=c".
    std::map<std::string, std::string> m;
    for (absl::string_view sp : absl::StrSplit("a=b=c,d=e,f=,g", ',')) {
      m.insert(absl::StrSplit(sp, absl::MaxSplits('=', 1)));
    }
    EXPECT_EQ("b=c", m.find("a")->second);
    EXPECT_EQ("e", m.find("d")->second);
    EXPECT_EQ("", m.find("f")->second);
    EXPECT_EQ("", m.find("g")->second);
  }
}

//
// Tests for SplitIterator
//

TEST(SplitIterator, Basics) {
  auto splitter = absl::StrSplit("a,b", ',');
  auto it = splitter.begin();
  auto end = splitter.end();

  EXPECT_NE(it, end);
  EXPECT_EQ("a", *it);  // tests dereference
  ++it;                 // tests preincrement
  EXPECT_NE(it, end);
  EXPECT_EQ("b",
            std::string(it->data(), it->size()));  // tests dereference as ptr
  it++;                                            // tests postincrement
  EXPECT_EQ(it, end);
}

// Simple Predicate to skip a particular string.
class Skip {
 public:
  explicit Skip(const std::string& s) : s_(s) {}
  bool operator()(absl::string_view sp) { return sp != s_; }

 private:
  std::string s_;
};

TEST(SplitIterator, Predicate) {
  auto splitter = absl::StrSplit("a,b,c", ',', Skip("b"));
  auto it = splitter.begin();
  auto end = splitter.end();

  EXPECT_NE(it, end);
  EXPECT_EQ("a", *it);  // tests dereference
  ++it;                 // tests preincrement -- "b" should be skipped here.
  EXPECT_NE(it, end);
  EXPECT_EQ("c",
            std::string(it->data(), it->size()));  // tests dereference as ptr
  it++;                                            // tests postincrement
  EXPECT_EQ(it, end);
}

TEST(SplitIterator, EdgeCases) {
  // Expected input and output, assuming a delimiter of ','
  struct {
    std::string in;
    std::vector<std::string> expect;
  } specs[] = {
      {"", {""}},
      {"foo", {"foo"}},
      {",", {"", ""}},
      {",foo", {"", "foo"}},
      {"foo,", {"foo", ""}},
      {",foo,", {"", "foo", ""}},
      {"foo,bar", {"foo", "bar"}},
  };

  for (const auto& spec : specs) {
    SCOPED_TRACE(spec.in);
    auto splitter = absl::StrSplit(spec.in, ',');
    auto it = splitter.begin();
    auto end = splitter.end();
    for (const auto& expected : spec.expect) {
      EXPECT_NE(it, end);
      EXPECT_EQ(expected, *it++);
    }
    EXPECT_EQ(it, end);
  }
}

TEST(Splitter, Const) {
  const auto splitter = absl::StrSplit("a,b,c", ',');
  EXPECT_THAT(splitter, ElementsAre("a", "b", "c"));
}

TEST(Split, EmptyAndNull) {
  // Attention: Splitting a null absl::string_view is different than splitting
  // an empty absl::string_view even though both string_views are considered
  // equal. This behavior is likely surprising and undesirable. However, to
  // maintain backward compatibility, there is a small "hack" in
  // str_split_internal.h that preserves this behavior. If that behavior is ever
  // changed/fixed, this test will need to be updated.
  EXPECT_THAT(absl::StrSplit(absl::string_view(""), '-'), ElementsAre(""));
  EXPECT_THAT(absl::StrSplit(absl::string_view(), '-'), ElementsAre());
}

TEST(SplitIterator, EqualityAsEndCondition) {
  auto splitter = absl::StrSplit("a,b,c", ',');
  auto it = splitter.begin();
  auto it2 = it;

  // Increments it2 twice to point to "c" in the input text.
  ++it2;
  ++it2;
  EXPECT_EQ("c", *it2);

  // This test uses a non-end SplitIterator as the terminating condition in a
  // for loop. This relies on SplitIterator equality for non-end SplitIterators
  // working correctly. At this point it2 points to "c", and we use that as the
  // "end" condition in this test.
  std::vector<absl::string_view> v;
  for (; it != it2; ++it) {
    v.push_back(*it);
  }
  EXPECT_THAT(v, ElementsAre("a", "b"));
}

//
// Tests for Splitter
//

TEST(Splitter, RangeIterators) {
  auto splitter = absl::StrSplit("a,b,c", ',');
  std::vector<absl::string_view> output;
  for (absl::string_view p : splitter) {
    output.push_back(p);
  }
  EXPECT_THAT(output, ElementsAre("a", "b", "c"));
}

// Some template functions for use in testing conversion operators
template <typename ContainerType, typename Splitter>
void TestConversionOperator(const Splitter& splitter) {
  ContainerType output = splitter;
  EXPECT_THAT(output, UnorderedElementsAre("a", "b", "c", "d"));
}

template <typename MapType, typename Splitter>
void TestMapConversionOperator(const Splitter& splitter) {
  MapType m = splitter;
  EXPECT_THAT(m, UnorderedElementsAre(Pair("a", "b"), Pair("c", "d")));
}

template <typename FirstType, typename SecondType, typename Splitter>
void TestPairConversionOperator(const Splitter& splitter) {
  std::pair<FirstType, SecondType> p = splitter;
  EXPECT_EQ(p, (std::pair<FirstType, SecondType>("a", "b")));
}

TEST(Splitter, ConversionOperator) {
  auto splitter = absl::StrSplit("a,b,c,d", ',');

  TestConversionOperator<std::vector<absl::string_view>>(splitter);
  TestConversionOperator<std::vector<std::string>>(splitter);
  TestConversionOperator<std::list<absl::string_view>>(splitter);
  TestConversionOperator<std::list<std::string>>(splitter);
  TestConversionOperator<std::deque<absl::string_view>>(splitter);
  TestConversionOperator<std::deque<std::string>>(splitter);
  TestConversionOperator<std::set<absl::string_view>>(splitter);
  TestConversionOperator<std::set<std::string>>(splitter);
  TestConversionOperator<std::multiset<absl::string_view>>(splitter);
  TestConversionOperator<std::multiset<std::string>>(splitter);
  TestConversionOperator<absl::btree_set<absl::string_view>>(splitter);
  TestConversionOperator<absl::btree_set<std::string>>(splitter);
  TestConversionOperator<absl::btree_multiset<absl::string_view>>(splitter);
  TestConversionOperator<absl::btree_multiset<std::string>>(splitter);
  TestConversionOperator<std::unordered_set<std::string>>(splitter);

  // Tests conversion to map-like objects.

  TestMapConversionOperator<std::map<absl::string_view, absl::string_view>>(
      splitter);
  TestMapConversionOperator<std::map<absl::string_view, std::string>>(splitter);
  TestMapConversionOperator<std::map<std::string, absl::string_view>>(splitter);
  TestMapConversionOperator<std::map<std::string, std::string>>(splitter);
  TestMapConversionOperator<
      std::multimap<absl::string_view, absl::string_view>>(splitter);
  TestMapConversionOperator<std::multimap<absl::string_view, std::string>>(
      splitter);
  TestMapConversionOperator<std::multimap<std::string, absl::string_view>>(
      splitter);
  TestMapConversionOperator<std::multimap<std::string, std::string>>(splitter);
  TestMapConversionOperator<
      absl::btree_map<absl::string_view, absl::string_view>>(splitter);
  TestMapConversionOperator<absl::btree_map<absl::string_view, std::string>>(
      splitter);
  TestMapConversionOperator<absl::btree_map<std::string, absl::string_view>>(
      splitter);
  TestMapConversionOperator<absl::btree_map<std::string, std::string>>(
      splitter);
  TestMapConversionOperator<
      absl::btree_multimap<absl::string_view, absl::string_view>>(splitter);
  TestMapConversionOperator<
      absl::btree_multimap<absl::string_view, std::string>>(splitter);
  TestMapConversionOperator<
      absl::btree_multimap<std::string, absl::string_view>>(splitter);
  TestMapConversionOperator<absl::btree_multimap<std::string, std::string>>(
      splitter);
  TestMapConversionOperator<std::unordered_map<std::string, std::string>>(
      splitter);
  TestMapConversionOperator<
      absl::node_hash_map<absl::string_view, absl::string_view>>(splitter);
  TestMapConversionOperator<
      absl::node_hash_map<absl::string_view, std::string>>(splitter);
  TestMapConversionOperator<
      absl::node_hash_map<std::string, absl::string_view>>(splitter);
  TestMapConversionOperator<
      absl::flat_hash_map<absl::string_view, absl::string_view>>(splitter);
  TestMapConversionOperator<
      absl::flat_hash_map<absl::string_view, std::string>>(splitter);
  TestMapConversionOperator<
      absl::flat_hash_map<std::string, absl::string_view>>(splitter);

  // Tests conversion to std::pair

  TestPairConversionOperator<absl::string_view, absl::string_view>(splitter);
  TestPairConversionOperator<absl::string_view, std::string>(splitter);
  TestPairConversionOperator<std::string, absl::string_view>(splitter);
  TestPairConversionOperator<std::string, std::string>(splitter);
}

// A few additional tests for conversion to std::pair. This conversion is
// different from others because a std::pair always has exactly two elements:
// .first and .second. The split has to work even when the split has
// less-than, equal-to, and more-than 2 strings.
TEST(Splitter, ToPair) {
  {
    // Empty string
    std::pair<std::string, std::string> p = absl::StrSplit("", ',');
    EXPECT_EQ("", p.first);
    EXPECT_EQ("", p.second);
  }

  {
    // Only first
    std::pair<std::string, std::string> p = absl::StrSplit("a", ',');
    EXPECT_EQ("a", p.first);
    EXPECT_EQ("", p.second);
  }

  {
    // Only second
    std::pair<std::string, std::string> p = absl::StrSplit(",b", ',');
    EXPECT_EQ("", p.first);
    EXPECT_EQ("b", p.second);
  }

  {
    // First and second.
    std::pair<std::string, std::string> p = absl::StrSplit("a,b", ',');
    EXPECT_EQ("a", p.first);
    EXPECT_EQ("b", p.second);
  }

  {
    // First and second and then more stuff that will be ignored.
    std::pair<std::string, std::string> p = absl::StrSplit("a,b,c", ',');
    EXPECT_EQ("a", p.first);
    EXPECT_EQ("b", p.second);
    // "c" is omitted.
  }
}

TEST(Splitter, Predicates) {
  static const char kTestChars[] = ",a, ,b,";
  using absl::AllowEmpty;
  using absl::SkipEmpty;
  using absl::SkipWhitespace;

  {
    // No predicate. Does not skip empties.
    auto splitter = absl::StrSplit(kTestChars, ',');
    std::vector<std::string> v = splitter;
    EXPECT_THAT(v, ElementsAre("", "a", " ", "b", ""));
  }

  {
    // Allows empty strings. Same behavior as no predicate at all.
    auto splitter = absl::StrSplit(kTestChars, ',', AllowEmpty());
    std::vector<std::string> v_allowempty = splitter;
    EXPECT_THAT(v_allowempty, ElementsAre("", "a", " ", "b", ""));

    // Ensures AllowEmpty equals the behavior with no predicate.
    auto splitter_nopredicate = absl::StrSplit(kTestChars, ',');
    std::vector<std::string> v_nopredicate = splitter_nopredicate;
    EXPECT_EQ(v_allowempty, v_nopredicate);
  }

  {
    // Skips empty strings.
    auto splitter = absl::StrSplit(kTestChars, ',', SkipEmpty());
    std::vector<std::string> v = splitter;
    EXPECT_THAT(v, ElementsAre("a", " ", "b"));
  }

  {
    // Skips empty and all-whitespace strings.
    auto splitter = absl::StrSplit(kTestChars, ',', SkipWhitespace());
    std::vector<std::string> v = splitter;
    EXPECT_THAT(v, ElementsAre("a", "b"));
  }
}

//
// Tests for StrSplit()
//

TEST(Split, Basics) {
  {
    // Doesn't really do anything useful because the return value is ignored,
    // but it should work.
    absl::StrSplit("a,b,c", ',');
  }

  {
    std::vector<absl::string_view> v = absl::StrSplit("a,b,c", ',');
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    std::vector<std::string> v = absl::StrSplit("a,b,c", ',');
    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
  }

  {
    // Ensures that assignment works. This requires a little extra work with
    // C++11 because of overloads with initializer_list.
    std::vector<std::string> v;
    v = absl::StrSplit("a,b,c", ',');

    EXPECT_THAT(v, ElementsAre("a", "b", "c"));
    std::map<std::string, std::string> m;
    m = absl::StrSplit("a,b,c", ',');
    EXPECT_EQ(2, m.size());
    std::unordered_map<std::string, std::string> hm;
    hm = absl::StrSplit("a,b,c", ',');
    EXPECT_EQ(2, hm.size());
  }
}

absl::string_view ReturnStringView() { return "Hello World"; }
const char* ReturnConstCharP() { return "Hello World"; }
char* ReturnCharP() { return const_cast<char*>("Hello World"); }

TEST(Split, AcceptsCertainTemporaries) {
  std::vector<std::string> v;
  v = absl::StrSplit(ReturnStringView(), ' ');
  EXPECT_THAT(v, ElementsAre("Hello", "World"));
  v = absl::StrSplit(ReturnConstCharP(), ' ');
  EXPECT_THAT(v, ElementsAre("Hello", "World"));
  v = absl::StrSplit(ReturnCharP(), ' ');
  EXPECT_THAT(v, ElementsAre("Hello", "World"));
}

TEST(Split, Temporary) {
  // Use a std::string longer than the SSO length, so that when the temporary is
  // destroyed, if the splitter keeps a reference to the string's contents,
  // it'll reference freed memory instead of just dead on-stack memory.
  const char input[] = "a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u";
  EXPECT_LT(sizeof(std::string), ABSL_ARRAYSIZE(input))
      << "Input should be larger than fits on the stack.";

  // This happens more often in C++11 as part of a range-based for loop.
  auto splitter = absl::StrSplit(std::string(input), ',');
  std::string expected = "a";
  for (absl::string_view letter : splitter) {
    EXPECT_EQ(expected, letter);
    ++expected[0];
  }
  EXPECT_EQ("v", expected);

  // This happens more often in C++11 as part of a range-based for loop.
  auto std_splitter = absl::StrSplit(std::string(input), ',');
  expected = "a";
  for (absl::string_view letter : std_splitter) {
    EXPECT_EQ(expected, letter);
    ++expected[0];
  }
  EXPECT_EQ("v", expected);
}

template <typename T>
static std::unique_ptr<T> CopyToHeap(const T& value) {
  return std::unique_ptr<T>(new T(value));
}

TEST(Split, LvalueCaptureIsCopyable) {
  std::string input = "a,b";
  auto heap_splitter = CopyToHeap(absl::StrSplit(input, ','));
  auto stack_splitter = *heap_splitter;
  heap_splitter.reset();
  std::vector<std::string> result = stack_splitter;
  EXPECT_THAT(result, testing::ElementsAre("a", "b"));
}

TEST(Split, TemporaryCaptureIsCopyable) {
  auto heap_splitter = CopyToHeap(absl::StrSplit(std::string("a,b"), ','));
  auto stack_splitter = *heap_splitter;
  heap_splitter.reset();
  std::vector<std::string> result = stack_splitter;
  EXPECT_THAT(result, testing::ElementsAre("a", "b"));
}

TEST(Split, SplitterIsCopyableAndMoveable) {
  auto a = absl::StrSplit("foo", '-');

  // Ensures that the following expressions compile.
  auto b = a;             // Copy construct
  auto c = std::move(a);  // Move construct
  b = c;                  // Copy assign
  c = std::move(b);       // Move assign

  EXPECT_THAT(c, ElementsAre("foo"));
}

TEST(Split, StringDelimiter) {
  {
    std::vector<absl::string_view> v = absl::StrSplit("a,b", ',');
    EXPECT_THAT(v, ElementsAre("a", "b"));
  }

  {
    std::vector<absl::string_view> v = absl::StrSplit("a,b", std::string(","));
    EXPECT_THAT(v, ElementsAre("a", "b"));
  }

  {
    std::vector<absl::string_view> v =
        absl::StrSplit("a,b", absl::string_view(","));
    EXPECT_THAT(v, ElementsAre("a", "b"));
  }
}

#if !defined(__cpp_char8_t)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++2a-compat"
#endif
TEST(Split, UTF8) {
  // Tests splitting utf8 strings and utf8 delimiters.
  std::string utf8_string = u8"\u03BA\u1F79\u03C3\u03BC\u03B5";
  {
    // A utf8 input string with an ascii delimiter.
    std::string to_split = "a," + utf8_string;
    std::vector<absl::string_view> v = absl::StrSplit(to_split, ',');
    EXPECT_THAT(v, ElementsAre("a", utf8_string));
  }

  {
    // A utf8 input string and a utf8 delimiter.
    std::string to_split = "a," + utf8_string + ",b";
    std::string unicode_delimiter = "," + utf8_string + ",";
    std::vector<absl::string_view> v =
        absl::StrSplit(to_split, unicode_delimiter);
    EXPECT_THAT(v, ElementsAre("a", "b"));
  }

  {
    // A utf8 input string and ByAnyChar with ascii chars.
    std::vector<absl::string_view> v =
        absl::StrSplit(u8"Foo h\u00E4llo th\u4E1Ere", absl::ByAnyChar(" \t"));
    EXPECT_THAT(v, ElementsAre("Foo", u8"h\u00E4llo", u8"th\u4E1Ere"));
  }
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif  // !defined(__cpp_char8_t)

TEST(Split, EmptyStringDelimiter) {
  {
    std::vector<std::string> v = absl::StrSplit("", "");
    EXPECT_THAT(v, ElementsAre(""));
  }

  {
    std::vector<std::string> v = absl::StrSplit("a", "");
    EXPECT_THAT(v, ElementsAre("a"));
  }

  {
    std::vector<std::string> v = absl::StrSplit("ab", "");
    EXPECT_THAT(v, ElementsAre("a", "b"));
  }

  {
    std::vector<std::string> v = absl::StrSplit("a b", "");
    EXPECT_THAT(v, ElementsAre("a", " ", "b"));
  }
}

TEST(Split, SubstrDelimiter) {
  std::vector<absl::string_view> results;
  absl::string_view delim("//");

  results = absl::StrSplit("", delim);
  EXPECT_THAT(results, ElementsAre(""));

  results = absl::StrSplit("//", delim);
  EXPECT_THAT(results, ElementsAre("", ""));

  results = absl::StrSplit("ab", delim);
  EXPECT_THAT(results, ElementsAre("ab"));

  results = absl::StrSplit("ab//", delim);
  EXPECT_THAT(results, ElementsAre("ab", ""));

  results = absl::StrSplit("ab/", delim);
  EXPECT_THAT(results, ElementsAre("ab/"));

  results = absl::StrSplit("a/b", delim);
  EXPECT_THAT(results, ElementsAre("a/b"));

  results = absl::StrSplit("a//b", delim);
  EXPECT_THAT(results, ElementsAre("a", "b"));

  results = absl::StrSplit("a///b", delim);
  EXPECT_THAT(results, ElementsAre("a", "/b"));

  results = absl::StrSplit("a////b", delim);
  EXPECT_THAT(results, ElementsAre("a", "", "b"));
}

TEST(Split, EmptyResults) {
  std::vector<absl::string_view> results;

  results = absl::StrSplit("", '#');
  EXPECT_THAT(results, ElementsAre(""));

  results = absl::StrSplit("#", '#');
  EXPECT_THAT(results, ElementsAre("", ""));

  results = absl::StrSplit("#cd", '#');
  EXPECT_THAT(results, ElementsAre("", "cd"));

  results = absl::StrSplit("ab#cd#", '#');
  EXPECT_THAT(results, ElementsAre("ab", "cd", ""));

  results = absl::StrSplit("ab##cd", '#');
  EXPECT_THAT(results, ElementsAre("ab", "", "cd"));

  results = absl::StrSplit("ab##", '#');
  EXPECT_THAT(results, ElementsAre("ab", "", ""));

  results = absl::StrSplit("ab#ab#", '#');
  EXPECT_THAT(results, ElementsAre("ab", "ab", ""));

  results = absl::StrSplit("aaaa", 'a');
  EXPECT_THAT(results, ElementsAre("", "", "", "", ""));

  results = absl::StrSplit("", '#', absl::SkipEmpty());
  EXPECT_THAT(results, ElementsAre());
}

template <typename Delimiter>
static bool IsFoundAtStartingPos(absl::string_view text, Delimiter d,
                                 size_t starting_pos, int expected_pos) {
  absl::string_view found = d.Find(text, starting_pos);
  return found.data() != text.data() + text.size() &&
         expected_pos == found.data() - text.data();
}

// Helper function for testing Delimiter objects. Returns true if the given
// Delimiter is found in the given string at the given position. This function
// tests two cases:
//   1. The actual text given, staring at position 0
//   2. The text given with leading padding that should be ignored
template <typename Delimiter>
static bool IsFoundAt(absl::string_view text, Delimiter d, int expected_pos) {
  const std::string leading_text = ",x,y,z,";
  return IsFoundAtStartingPos(text, d, 0, expected_pos) &&
         IsFoundAtStartingPos(leading_text + std::string(text), d,
                              leading_text.length(),
                              expected_pos + leading_text.length());
}

//
// Tests for ByString
//

// Tests using any delimiter that represents a single comma.
template <typename Delimiter>
void TestComma(Delimiter d) {
  EXPECT_TRUE(IsFoundAt(",", d, 0));
  EXPECT_TRUE(IsFoundAt("a,", d, 1));
  EXPECT_TRUE(IsFoundAt(",b", d, 0));
  EXPECT_TRUE(IsFoundAt("a,b", d, 1));
  EXPECT_TRUE(IsFoundAt("a,b,", d, 1));
  EXPECT_TRUE(IsFoundAt("a,b,c", d, 1));
  EXPECT_FALSE(IsFoundAt("", d, -1));
  EXPECT_FALSE(IsFoundAt(" ", d, -1));
  EXPECT_FALSE(IsFoundAt("a", d, -1));
  EXPECT_FALSE(IsFoundAt("a b c", d, -1));
  EXPECT_FALSE(IsFoundAt("a;b;c", d, -1));
  EXPECT_FALSE(IsFoundAt(";", d, -1));
}

TEST(Delimiter, ByString) {
  using absl::ByString;
  TestComma(ByString(","));

  // Works as named variable.
  ByString comma_string(",");
  TestComma(comma_string);

  // The first occurrence of empty string ("") in a string is at position 0.
  // There is a test below that demonstrates this for absl::string_view::find().
  // If the ByString delimiter returned position 0 for this, there would
  // be an infinite loop in the SplitIterator code. To avoid this, empty string
  // is a special case in that it always returns the item at position 1.
  absl::string_view abc("abc");
  EXPECT_EQ(0, abc.find(""));  // "" is found at position 0
  ByString empty("");
  EXPECT_FALSE(IsFoundAt("", empty, 0));
  EXPECT_FALSE(IsFoundAt("a", empty, 0));
  EXPECT_TRUE(IsFoundAt("ab", empty, 1));
  EXPECT_TRUE(IsFoundAt("abc", empty, 1));
}

TEST(Split, ByChar) {
  using absl::ByChar;
  TestComma(ByChar(','));

  // Works as named variable.
  ByChar comma_char(',');
  TestComma(comma_char);
}

//
// Tests for ByAnyChar
//

TEST(Delimiter, ByAnyChar) {
  using absl::ByAnyChar;
  ByAnyChar one_delim(",");
  // Found
  EXPECT_TRUE(IsFoundAt(",", one_delim, 0));
  EXPECT_TRUE(IsFoundAt("a,", one_delim, 1));
  EXPECT_TRUE(IsFoundAt("a,b", one_delim, 1));
  EXPECT_TRUE(IsFoundAt(",b", one_delim, 0));
  // Not found
  EXPECT_FALSE(IsFoundAt("", one_delim, -1));
  EXPECT_FALSE(IsFoundAt(" ", one_delim, -1));
  EXPECT_FALSE(IsFoundAt("a", one_delim, -1));
  EXPECT_FALSE(IsFoundAt("a;b;c", one_delim, -1));
  EXPECT_FALSE(IsFoundAt(";", one_delim, -1));

  ByAnyChar two_delims(",;");
  // Found
  EXPECT_TRUE(IsFoundAt(",", two_delims, 0));
  EXPECT_TRUE(IsFoundAt(";", two_delims, 0));
  EXPECT_TRUE(IsFoundAt(",;", two_delims, 0));
  EXPECT_TRUE(IsFoundAt(";,", two_delims, 0));
  EXPECT_TRUE(IsFoundAt(",;b", two_delims, 0));
  EXPECT_TRUE(IsFoundAt(";,b", two_delims, 0));
  EXPECT_TRUE(IsFoundAt("a;,", two_delims, 1));
  EXPECT_TRUE(IsFoundAt("a,;", two_delims, 1));
  EXPECT_TRUE(IsFoundAt("a;,b", two_delims, 1));
  EXPECT_TRUE(IsFoundAt("a,;b", two_delims, 1));
  // Not found
  EXPECT_FALSE(IsFoundAt("", two_delims, -1));
  EXPECT_FALSE(IsFoundAt(" ", two_delims, -1));
  EXPECT_FALSE(IsFoundAt("a", two_delims, -1));
  EXPECT_FALSE(IsFoundAt("a=b=c", two_delims, -1));
  EXPECT_FALSE(IsFoundAt("=", two_delims, -1));

  // ByAnyChar behaves just like ByString when given a delimiter of empty
  // string. That is, it always returns a zero-length absl::string_view
  // referring to the item at position 1, not position 0.
  ByAnyChar empty("");
  EXPECT_FALSE(IsFoundAt("", empty, 0));
  EXPECT_FALSE(IsFoundAt("a", empty, 0));
  EXPECT_TRUE(IsFoundAt("ab", empty, 1));
  EXPECT_TRUE(IsFoundAt("abc", empty, 1));
}

//
// Tests for ByAsciiWhitespace
//
TEST(Split, ByAsciiWhitespace) {
  using absl::ByAsciiWhitespace;
  using absl::SkipEmpty;
  std::vector<absl::string_view> results;

  results = absl::StrSplit("aaaa\n", ByAsciiWhitespace());
  EXPECT_THAT(results, ElementsAre("aaaa", ""));

  results = absl::StrSplit("aaaa\n", ByAsciiWhitespace(), SkipEmpty());
  EXPECT_THAT(results, ElementsAre("aaaa"));

  results = absl::StrSplit(" ", ByAsciiWhitespace());
  EXPECT_THAT(results, ElementsAre("", ""));

  results = absl::StrSplit(" ", ByAsciiWhitespace(), SkipEmpty());
  EXPECT_THAT(results, IsEmpty());

  results = absl::StrSplit("a", ByAsciiWhitespace());
  EXPECT_THAT(results, ElementsAre("a"));

  results = absl::StrSplit("", ByAsciiWhitespace());
  EXPECT_THAT(results, ElementsAre(""));

  results = absl::StrSplit("", ByAsciiWhitespace(), SkipEmpty());
  EXPECT_THAT(results, IsEmpty());

  results = absl::StrSplit("a b\tc\n  d\n", ByAsciiWhitespace());
  EXPECT_THAT(results, ElementsAre("a", "b", "c", "", "", "d", ""));

  results = absl::StrSplit("a b\tc\n  d  \n", ByAsciiWhitespace(), SkipEmpty());
  EXPECT_THAT(results, ElementsAre("a", "b", "c", "d"));

  results = absl::StrSplit("a\t\n\v\f\r b", ByAsciiWhitespace(), SkipEmpty());
  EXPECT_THAT(results, ElementsAre("a", "b"));
}

//
// Tests for ByLength
//

TEST(Delimiter, ByLength) {
  using absl::ByLength;

  ByLength four_char_delim(4);

  // Found
  EXPECT_TRUE(IsFoundAt("abcde", four_char_delim, 4));
  EXPECT_TRUE(IsFoundAt("abcdefghijklmnopqrstuvwxyz", four_char_delim, 4));
  EXPECT_TRUE(IsFoundAt("a b,c\nd", four_char_delim, 4));
  // Not found
  EXPECT_FALSE(IsFoundAt("", four_char_delim, 0));
  EXPECT_FALSE(IsFoundAt("a", four_char_delim, 0));
  EXPECT_FALSE(IsFoundAt("ab", four_char_delim, 0));
  EXPECT_FALSE(IsFoundAt("abc", four_char_delim, 0));
  EXPECT_FALSE(IsFoundAt("abcd", four_char_delim, 0));
}

TEST(Split, WorksWithLargeStrings) {
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
  constexpr size_t kSize = (uint32_t{1} << 26) + 1;  // 64M + 1 byte
#else
  constexpr size_t kSize = (uint32_t{1} << 31) + 1;  // 2G + 1 byte
#endif
  if (sizeof(size_t) > 4) {
    std::string s(kSize, 'x');
    s.back() = '-';
    std::vector<absl::string_view> v = absl::StrSplit(s, '-');
    EXPECT_EQ(2, v.size());
    // The first element will contain 2G of 'x's.
    // testing::StartsWith is too slow with a 2G string.
    EXPECT_EQ('x', v[0][0]);
    EXPECT_EQ('x', v[0][1]);
    EXPECT_EQ('x', v[0][3]);
    EXPECT_EQ("", v[1]);
  }
}

TEST(SplitInternalTest, TypeTraits) {
  EXPECT_FALSE(absl::strings_internal::HasMappedType<int>::value);
  EXPECT_TRUE(
      (absl::strings_internal::HasMappedType<std::map<int, int>>::value));
  EXPECT_FALSE(absl::strings_internal::HasValueType<int>::value);
  EXPECT_TRUE(
      (absl::strings_internal::HasValueType<std::map<int, int>>::value));
  EXPECT_FALSE(absl::strings_internal::HasConstIterator<int>::value);
  EXPECT_TRUE(
      (absl::strings_internal::HasConstIterator<std::map<int, int>>::value));
  EXPECT_FALSE(absl::strings_internal::IsInitializerList<int>::value);
  EXPECT_TRUE((absl::strings_internal::IsInitializerList<
               std::initializer_list<int>>::value));
}

}  // namespace
