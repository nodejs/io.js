// Copyright 2022 The Abseil Authors.
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
// File: charset.h
// -----------------------------------------------------------------------------
//
// This file contains absl::CharSet, a fast, bit-vector set of 8-bit unsigned
// characters.
//
// Instances can be initialized as constexpr constants. For example:
//
//   constexpr absl::CharSet kJustX = absl::CharSet::Char('x');
//   constexpr absl::CharSet kMySymbols = absl::CharSet("$@!");
//   constexpr absl::CharSet kLetters = absl::CharSet::Range('a', 'z');
//
// Multiple instances can be combined that still forms a constexpr expression.
// For example:
//
//   constexpr absl::CharSet kLettersAndNumbers =
//       absl::CharSet::Range('a', 'z') | absl::CharSet::Range('0', '9');
//
// Several pre-defined character classes are available that mirror the methods
// from <cctype>. For example:
//
//   constexpr absl::CharSet kLettersAndWhitespace =
//       absl::CharSet::AsciiAlphabet() | absl::CharSet::AsciiWhitespace();
//
// To check membership, use the .contains method, e.g.
//
//   absl::CharSet hex_letters("abcdef");
//   hex_letters.contains('a');  // true
//   hex_letters.contains('g');  // false

#ifndef ABSL_STRINGS_CHARSET_H_
#define ABSL_STRINGS_CHARSET_H_

#include <cstdint>

#include "absl/base/config.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

class CharSet {
 public:
  constexpr CharSet() : m_() {}

  // Initializes with a given string_view.
  constexpr explicit CharSet(absl::string_view str) : m_() {
    for (char c : str) {
      SetChar(static_cast<unsigned char>(c));
    }
  }

  constexpr bool contains(char c) const {
    return ((m_[static_cast<unsigned char>(c) / 64] >>
             (static_cast<unsigned char>(c) % 64)) &
            0x1) == 0x1;
  }

  constexpr bool empty() const {
    for (uint64_t c : m_) {
      if (c != 0) return false;
    }
    return true;
  }

  // Containing only a single specified char.
  static constexpr CharSet Char(char x) {
    return CharSet(CharMaskForWord(x, 0), CharMaskForWord(x, 1),
                   CharMaskForWord(x, 2), CharMaskForWord(x, 3));
  }

  // Containing all the chars in the closed interval [lo,hi].
  static constexpr CharSet Range(char lo, char hi) {
    return CharSet(RangeForWord(lo, hi, 0), RangeForWord(lo, hi, 1),
                   RangeForWord(lo, hi, 2), RangeForWord(lo, hi, 3));
  }

  friend constexpr CharSet operator&(const CharSet& a, const CharSet& b) {
    return CharSet(a.m_[0] & b.m_[0], a.m_[1] & b.m_[1], a.m_[2] & b.m_[2],
                   a.m_[3] & b.m_[3]);
  }

  friend constexpr CharSet operator|(const CharSet& a, const CharSet& b) {
    return CharSet(a.m_[0] | b.m_[0], a.m_[1] | b.m_[1], a.m_[2] | b.m_[2],
                   a.m_[3] | b.m_[3]);
  }

  friend constexpr CharSet operator~(const CharSet& a) {
    return CharSet(~a.m_[0], ~a.m_[1], ~a.m_[2], ~a.m_[3]);
  }

  // Mirrors the char-classifying predicates in <cctype>.
  static constexpr CharSet AsciiUppercase() { return CharSet::Range('A', 'Z'); }
  static constexpr CharSet AsciiLowercase() { return CharSet::Range('a', 'z'); }
  static constexpr CharSet AsciiDigits() { return CharSet::Range('0', '9'); }
  static constexpr CharSet AsciiAlphabet() {
    return AsciiLowercase() | AsciiUppercase();
  }
  static constexpr CharSet AsciiAlphanumerics() {
    return AsciiDigits() | AsciiAlphabet();
  }
  static constexpr CharSet AsciiHexDigits() {
    return AsciiDigits() | CharSet::Range('A', 'F') | CharSet::Range('a', 'f');
  }
  static constexpr CharSet AsciiPrintable() {
    return CharSet::Range(0x20, 0x7e);
  }
  static constexpr CharSet AsciiWhitespace() { return CharSet("\t\n\v\f\r "); }
  static constexpr CharSet AsciiPunctuation() {
    return AsciiPrintable() & ~AsciiWhitespace() & ~AsciiAlphanumerics();
  }

 private:
  constexpr CharSet(uint64_t b0, uint64_t b1, uint64_t b2, uint64_t b3)
      : m_{b0, b1, b2, b3} {}

  static constexpr uint64_t RangeForWord(char lo, char hi, uint64_t word) {
    return OpenRangeFromZeroForWord(static_cast<unsigned char>(hi) + 1, word) &
           ~OpenRangeFromZeroForWord(static_cast<unsigned char>(lo), word);
  }

  // All the chars in the specified word of the range [0, upper).
  static constexpr uint64_t OpenRangeFromZeroForWord(uint64_t upper,
                                                     uint64_t word) {
    return (upper <= 64 * word) ? 0
           : (upper >= 64 * (word + 1))
               ? ~static_cast<uint64_t>(0)
               : (~static_cast<uint64_t>(0) >> (64 - upper % 64));
  }

  static constexpr uint64_t CharMaskForWord(char x, uint64_t word) {
    return (static_cast<unsigned char>(x) / 64 == word)
               ? (static_cast<uint64_t>(1)
                  << (static_cast<unsigned char>(x) % 64))
               : 0;
  }

  constexpr void SetChar(unsigned char c) {
    m_[c / 64] |= static_cast<uint64_t>(1) << (c % 64);
  }

  uint64_t m_[4];
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_CHARSET_H_
