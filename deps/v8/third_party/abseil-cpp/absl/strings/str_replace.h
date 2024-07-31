//
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
//
// -----------------------------------------------------------------------------
// File: str_replace.h
// -----------------------------------------------------------------------------
//
// This file defines `absl::StrReplaceAll()`, a general-purpose string
// replacement function designed for large, arbitrary text substitutions,
// especially on strings which you are receiving from some other system for
// further processing (e.g. processing regular expressions, escaping HTML
// entities, etc.). `StrReplaceAll` is designed to be efficient even when only
// one substitution is being performed, or when substitution is rare.
//
// If the string being modified is known at compile-time, and the substitutions
// vary, `absl::Substitute()` may be a better choice.
//
// Example:
//
// std::string html_escaped = absl::StrReplaceAll(user_input, {
//                                                {"&", "&amp;"},
//                                                {"<", "&lt;"},
//                                                {">", "&gt;"},
//                                                {"\"", "&quot;"},
//                                                {"'", "&#39;"}});
#ifndef ABSL_STRINGS_STR_REPLACE_H_
#define ABSL_STRINGS_STR_REPLACE_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// StrReplaceAll()
//
// Replaces character sequences within a given string with replacements provided
// within an initializer list of key/value pairs. Candidate replacements are
// considered in order as they occur within the string, with earlier matches
// taking precedence, and longer matches taking precedence for candidates
// starting at the same position in the string. Once a substitution is made, the
// replaced text is not considered for any further substitutions.
//
// Example:
//
//   std::string s = absl::StrReplaceAll(
//       "$who bought $count #Noun. Thanks $who!",
//       {{"$count", absl::StrCat(5)},
//        {"$who", "Bob"},
//        {"#Noun", "Apples"}});
//   EXPECT_EQ("Bob bought 5 Apples. Thanks Bob!", s);
ABSL_MUST_USE_RESULT std::string StrReplaceAll(
    absl::string_view s,
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        replacements);

// Overload of `StrReplaceAll()` to accept a container of key/value replacement
// pairs (typically either an associative map or a `std::vector` of `std::pair`
// elements). A vector of pairs is generally more efficient.
//
// Examples:
//
//   std::map<const absl::string_view, const absl::string_view> replacements;
//   replacements["$who"] = "Bob";
//   replacements["$count"] = "5";
//   replacements["#Noun"] = "Apples";
//   std::string s = absl::StrReplaceAll(
//       "$who bought $count #Noun. Thanks $who!",
//       replacements);
//   EXPECT_EQ("Bob bought 5 Apples. Thanks Bob!", s);
//
//   // A std::vector of std::pair elements can be more efficient.
//   std::vector<std::pair<const absl::string_view, std::string>> replacements;
//   replacements.push_back({"&", "&amp;"});
//   replacements.push_back({"<", "&lt;"});
//   replacements.push_back({">", "&gt;"});
//   std::string s = absl::StrReplaceAll("if (ptr < &foo)",
//                                  replacements);
//   EXPECT_EQ("if (ptr &lt; &amp;foo)", s);
template <typename StrToStrMapping>
std::string StrReplaceAll(absl::string_view s,
                          const StrToStrMapping& replacements);

// Overload of `StrReplaceAll()` to replace character sequences within a given
// output string *in place* with replacements provided within an initializer
// list of key/value pairs, returning the number of substitutions that occurred.
//
// Example:
//
//   std::string s = std::string("$who bought $count #Noun. Thanks $who!");
//   int count;
//   count = absl::StrReplaceAll({{"$count", absl::StrCat(5)},
//                               {"$who", "Bob"},
//                               {"#Noun", "Apples"}}, &s);
//  EXPECT_EQ(count, 4);
//  EXPECT_EQ("Bob bought 5 Apples. Thanks Bob!", s);
int StrReplaceAll(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        replacements,
    absl::Nonnull<std::string*> target);

// Overload of `StrReplaceAll()` to replace patterns within a given output
// string *in place* with replacements provided within a container of key/value
// pairs.
//
// Example:
//
//   std::string s = std::string("if (ptr < &foo)");
//   int count = absl::StrReplaceAll({{"&", "&amp;"},
//                                    {"<", "&lt;"},
//                                    {">", "&gt;"}}, &s);
//  EXPECT_EQ(count, 2);
//  EXPECT_EQ("if (ptr &lt; &amp;foo)", s);
template <typename StrToStrMapping>
int StrReplaceAll(const StrToStrMapping& replacements,
                  absl::Nonnull<std::string*> target);

// Implementation details only, past this point.
namespace strings_internal {

struct ViableSubstitution {
  absl::string_view old;
  absl::string_view replacement;
  size_t offset;

  ViableSubstitution(absl::string_view old_str,
                     absl::string_view replacement_str, size_t offset_val)
      : old(old_str), replacement(replacement_str), offset(offset_val) {}

  // One substitution occurs "before" another (takes priority) if either
  // it has the lowest offset, or it has the same offset but a larger size.
  bool OccursBefore(const ViableSubstitution& y) const {
    if (offset != y.offset) return offset < y.offset;
    return old.size() > y.old.size();
  }
};

// Build a vector of ViableSubstitutions based on the given list of
// replacements. subs can be implemented as a priority_queue. However, it turns
// out that most callers have small enough a list of substitutions that the
// overhead of such a queue isn't worth it.
template <typename StrToStrMapping>
std::vector<ViableSubstitution> FindSubstitutions(
    absl::string_view s, const StrToStrMapping& replacements) {
  std::vector<ViableSubstitution> subs;
  subs.reserve(replacements.size());

  for (const auto& rep : replacements) {
    using std::get;
    absl::string_view old(get<0>(rep));

    size_t pos = s.find(old);
    if (pos == s.npos) continue;

    // Ignore attempts to replace "". This condition is almost never true,
    // but above condition is frequently true. That's why we test for this
    // now and not before.
    if (old.empty()) continue;

    subs.emplace_back(old, get<1>(rep), pos);

    // Insertion sort to ensure the last ViableSubstitution comes before
    // all the others.
    size_t index = subs.size();
    while (--index && subs[index - 1].OccursBefore(subs[index])) {
      std::swap(subs[index], subs[index - 1]);
    }
  }
  return subs;
}

int ApplySubstitutions(absl::string_view s,
                       absl::Nonnull<std::vector<ViableSubstitution>*> subs_ptr,
                       absl::Nonnull<std::string*> result_ptr);

}  // namespace strings_internal

template <typename StrToStrMapping>
std::string StrReplaceAll(absl::string_view s,
                          const StrToStrMapping& replacements) {
  auto subs = strings_internal::FindSubstitutions(s, replacements);
  std::string result;
  result.reserve(s.size());
  strings_internal::ApplySubstitutions(s, &subs, &result);
  return result;
}

template <typename StrToStrMapping>
int StrReplaceAll(const StrToStrMapping& replacements,
                  absl::Nonnull<std::string*> target) {
  auto subs = strings_internal::FindSubstitutions(*target, replacements);
  if (subs.empty()) return 0;

  std::string result;
  result.reserve(target->size());
  int substitutions =
      strings_internal::ApplySubstitutions(*target, &subs, &result);
  target->swap(result);
  return substitutions;
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_STR_REPLACE_H_
