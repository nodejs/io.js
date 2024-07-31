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
// File: ascii.h
// -----------------------------------------------------------------------------
//
// This package contains functions operating on characters and strings
// restricted to standard ASCII. These include character classification
// functions analogous to those found in the ANSI C Standard Library <ctype.h>
// header file.
//
// C++ implementations provide <ctype.h> functionality based on their
// C environment locale. In general, reliance on such a locale is not ideal, as
// the locale standard is problematic (and may not return invariant information
// for the same character set, for example). These `ascii_*()` functions are
// hard-wired for standard ASCII, much faster, and guaranteed to behave
// consistently.  They will never be overloaded, nor will their function
// signature change.
//
// `ascii_isalnum()`, `ascii_isalpha()`, `ascii_isascii()`, `ascii_isblank()`,
// `ascii_iscntrl()`, `ascii_isdigit()`, `ascii_isgraph()`, `ascii_islower()`,
// `ascii_isprint()`, `ascii_ispunct()`, `ascii_isspace()`, `ascii_isupper()`,
// `ascii_isxdigit()`
//   Analogous to the <ctype.h> functions with similar names, these
//   functions take an unsigned char and return a bool, based on whether the
//   character matches the condition specified.
//
//   If the input character has a numerical value greater than 127, these
//   functions return `false`.
//
// `ascii_tolower()`, `ascii_toupper()`
//   Analogous to the <ctype.h> functions with similar names, these functions
//   take an unsigned char and return a char.
//
//   If the input character is not an ASCII {lower,upper}-case letter (including
//   numerical values greater than 127) then the functions return the same value
//   as the input character.

#ifndef ABSL_STRINGS_ASCII_H_
#define ABSL_STRINGS_ASCII_H_

#include <algorithm>
#include <cstddef>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace ascii_internal {

// Declaration for an array of bitfields holding character information.
ABSL_DLL extern const unsigned char kPropertyBits[256];

// Declaration for the array of characters to upper-case characters.
ABSL_DLL extern const char kToUpper[256];

// Declaration for the array of characters to lower-case characters.
ABSL_DLL extern const char kToLower[256];

}  // namespace ascii_internal

// ascii_isalpha()
//
// Determines whether the given character is an alphabetic character.
inline bool ascii_isalpha(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x01) != 0;
}

// ascii_isalnum()
//
// Determines whether the given character is an alphanumeric character.
inline bool ascii_isalnum(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x04) != 0;
}

// ascii_isspace()
//
// Determines whether the given character is a whitespace character (space,
// tab, vertical tab, formfeed, linefeed, or carriage return).
inline bool ascii_isspace(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x08) != 0;
}

// ascii_ispunct()
//
// Determines whether the given character is a punctuation character.
inline bool ascii_ispunct(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x10) != 0;
}

// ascii_isblank()
//
// Determines whether the given character is a blank character (tab or space).
inline bool ascii_isblank(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x20) != 0;
}

// ascii_iscntrl()
//
// Determines whether the given character is a control character.
inline bool ascii_iscntrl(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x40) != 0;
}

// ascii_isxdigit()
//
// Determines whether the given character can be represented as a hexadecimal
// digit character (i.e. {0-9} or {A-F}).
inline bool ascii_isxdigit(unsigned char c) {
  return (ascii_internal::kPropertyBits[c] & 0x80) != 0;
}

// ascii_isdigit()
//
// Determines whether the given character can be represented as a decimal
// digit character (i.e. {0-9}).
inline bool ascii_isdigit(unsigned char c) { return c >= '0' && c <= '9'; }

// ascii_isprint()
//
// Determines whether the given character is printable, including spaces.
inline bool ascii_isprint(unsigned char c) { return c >= 32 && c < 127; }

// ascii_isgraph()
//
// Determines whether the given character has a graphical representation.
inline bool ascii_isgraph(unsigned char c) { return c > 32 && c < 127; }

// ascii_isupper()
//
// Determines whether the given character is uppercase.
inline bool ascii_isupper(unsigned char c) { return c >= 'A' && c <= 'Z'; }

// ascii_islower()
//
// Determines whether the given character is lowercase.
inline bool ascii_islower(unsigned char c) { return c >= 'a' && c <= 'z'; }

// ascii_isascii()
//
// Determines whether the given character is ASCII.
inline bool ascii_isascii(unsigned char c) { return c < 128; }

// ascii_tolower()
//
// Returns an ASCII character, converting to lowercase if uppercase is
// passed. Note that character values > 127 are simply returned.
inline char ascii_tolower(unsigned char c) {
  return ascii_internal::kToLower[c];
}

// Converts the characters in `s` to lowercase, changing the contents of `s`.
void AsciiStrToLower(absl::Nonnull<std::string*> s);

// Creates a lowercase string from a given absl::string_view.
ABSL_MUST_USE_RESULT inline std::string AsciiStrToLower(absl::string_view s) {
  std::string result(s);
  absl::AsciiStrToLower(&result);
  return result;
}

// ascii_toupper()
//
// Returns the ASCII character, converting to upper-case if lower-case is
// passed. Note that characters values > 127 are simply returned.
inline char ascii_toupper(unsigned char c) {
  return ascii_internal::kToUpper[c];
}

// Converts the characters in `s` to uppercase, changing the contents of `s`.
void AsciiStrToUpper(absl::Nonnull<std::string*> s);

// Creates an uppercase string from a given absl::string_view.
ABSL_MUST_USE_RESULT inline std::string AsciiStrToUpper(absl::string_view s) {
  std::string result(s);
  absl::AsciiStrToUpper(&result);
  return result;
}

// Returns absl::string_view with whitespace stripped from the beginning of the
// given string_view.
ABSL_MUST_USE_RESULT inline absl::string_view StripLeadingAsciiWhitespace(
    absl::string_view str) {
  auto it = std::find_if_not(str.begin(), str.end(), absl::ascii_isspace);
  return str.substr(static_cast<size_t>(it - str.begin()));
}

// Strips in place whitespace from the beginning of the given string.
inline void StripLeadingAsciiWhitespace(absl::Nonnull<std::string*> str) {
  auto it = std::find_if_not(str->begin(), str->end(), absl::ascii_isspace);
  str->erase(str->begin(), it);
}

// Returns absl::string_view with whitespace stripped from the end of the given
// string_view.
ABSL_MUST_USE_RESULT inline absl::string_view StripTrailingAsciiWhitespace(
    absl::string_view str) {
  auto it = std::find_if_not(str.rbegin(), str.rend(), absl::ascii_isspace);
  return str.substr(0, static_cast<size_t>(str.rend() - it));
}

// Strips in place whitespace from the end of the given string
inline void StripTrailingAsciiWhitespace(absl::Nonnull<std::string*> str) {
  auto it = std::find_if_not(str->rbegin(), str->rend(), absl::ascii_isspace);
  str->erase(static_cast<size_t>(str->rend() - it));
}

// Returns absl::string_view with whitespace stripped from both ends of the
// given string_view.
ABSL_MUST_USE_RESULT inline absl::string_view StripAsciiWhitespace(
    absl::string_view str) {
  return StripTrailingAsciiWhitespace(StripLeadingAsciiWhitespace(str));
}

// Strips in place whitespace from both ends of the given string
inline void StripAsciiWhitespace(absl::Nonnull<std::string*> str) {
  StripTrailingAsciiWhitespace(str);
  StripLeadingAsciiWhitespace(str);
}

// Removes leading, trailing, and consecutive internal whitespace.
void RemoveExtraAsciiWhitespace(absl::Nonnull<std::string*> str);

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_ASCII_H_
