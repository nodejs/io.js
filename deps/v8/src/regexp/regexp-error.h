// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_ERROR_H_
#define V8_REGEXP_REGEXP_ERROR_H_

#include "src/base/logging.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

#define REGEXP_ERROR_MESSAGES(T)                                          \
  T(None, "")                                                             \
  T(StackOverflow, "Maximum call stack size exceeded")                    \
  T(AnalysisStackOverflow, "Stack overflow")                              \
  T(TooLarge, "Regular expression too large")                             \
  T(UnterminatedGroup, "Unterminated group")                              \
  T(UnmatchedParen, "Unmatched ')'")                                      \
  T(EscapeAtEndOfPattern, "\\ at end of pattern")                         \
  T(InvalidPropertyName, "Invalid property name")                         \
  T(InvalidEscape, "Invalid escape")                                      \
  T(InvalidDecimalEscape, "Invalid decimal escape")                       \
  T(InvalidUnicodeEscape, "Invalid Unicode escape")                       \
  T(NothingToRepeat, "Nothing to repeat")                                 \
  T(LoneQuantifierBrackets, "Lone quantifier brackets")                   \
  T(RangeOutOfOrder, "numbers out of order in {} quantifier")             \
  T(IncompleteQuantifier, "Incomplete quantifier")                        \
  T(InvalidQuantifier, "Invalid quantifier")                              \
  T(InvalidGroup, "Invalid group")                                        \
  T(MultipleFlagDashes, "Multiple dashes in flag group")                  \
  T(NotLinear, "Cannot be executed in linear time")                       \
  T(RepeatedFlag, "Repeated flag in flag group")                          \
  T(InvalidFlagGroup, "Invalid flag group")                               \
  T(TooManyCaptures, "Too many captures")                                 \
  T(InvalidCaptureGroupName, "Invalid capture group name")                \
  T(DuplicateCaptureGroupName, "Duplicate capture group name")            \
  T(InvalidNamedReference, "Invalid named reference")                     \
  T(InvalidNamedCaptureReference, "Invalid named capture referenced")     \
  T(InvalidClassPropertyName, "Invalid property name in character class") \
  T(InvalidCharacterClass, "Invalid character class")                     \
  T(UnterminatedCharacterClass, "Unterminated character class")           \
  T(OutOfOrderCharacterClass, "Range out of order in character class")    \
  T(InvalidClassSetOperation, "Invalid set operation in character class") \
  T(InvalidCharacterInClass, "Invalid character in character class")      \
  T(NegatedCharacterClassWithStrings,                                     \
    "Negated character class may contain strings")

enum class RegExpError : uint32_t {
#define TEMPLATE(NAME, STRING) k##NAME,
  REGEXP_ERROR_MESSAGES(TEMPLATE)
#undef TEMPLATE
      NumErrors
};

V8_EXPORT_PRIVATE const char* RegExpErrorString(RegExpError error);

inline constexpr bool RegExpErrorIsStackOverflow(RegExpError error) {
  return error == RegExpError::kStackOverflow ||
         error == RegExpError::kAnalysisStackOverflow;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_ERROR_H_
