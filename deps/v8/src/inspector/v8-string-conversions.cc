// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-string-conversions.h"

#include <limits>
#include <vector>

#include "src/base/logging.h"

namespace v8_inspector {
namespace {
using UChar = char16_t;
using UChar32 = uint32_t;

bool isASCII(UChar c) { return !(c & ~0x7F); }

const UChar replacementCharacter = 0xFFFD;

inline int inlineUTF8SequenceLengthNonASCII(char b0) {
  if ((b0 & 0xC0) != 0xC0) return 0;
  if ((b0 & 0xE0) == 0xC0) return 2;
  if ((b0 & 0xF0) == 0xE0) return 3;
  if ((b0 & 0xF8) == 0xF0) return 4;
  return 0;
}

inline int inlineUTF8SequenceLength(char b0) {
  return isASCII(b0) ? 1 : inlineUTF8SequenceLengthNonASCII(b0);
}

// Once the bits are split out into bytes of UTF-8, this is a mask OR-ed
// into the first byte, depending on how many bytes follow.  There are
// as many entries in this table as there are UTF-8 sequence types.
// (I.e., one byte sequence, two byte... etc.). Remember that sequences
// for *legal* UTF-8 will be 4 or fewer bytes total.
static const unsigned char firstByteMark[7] = {0x00, 0x00, 0xC0, 0xE0,
                                               0xF0, 0xF8, 0xFC};

enum ConversionResult {
  conversionOK,     // conversion successful
  sourceExhausted,  // partial character in source, but hit end
  targetExhausted,  // insuff. room in target for conversion
  sourceIllegal     // source sequence is illegal/malformed
};

ConversionResult convertUTF16ToUTF8(const UChar** sourceStart,
                                    const UChar* sourceEnd, char** targetStart,
                                    char* targetEnd, bool strict) {
  ConversionResult result = conversionOK;
  const UChar* source = *sourceStart;
  char* target = *targetStart;
  while (source < sourceEnd) {
    UChar32 ch;
    uint32_t bytesToWrite = 0;
    const UChar32 byteMask = 0xBF;
    const UChar32 byteMark = 0x80;
    const UChar* oldSource =
        source;  // In case we have to back up because of target overflow.
    ch = static_cast<uint16_t>(*source++);
    // If we have a surrogate pair, convert to UChar32 first.
    if (ch >= 0xD800 && ch <= 0xDBFF) {
      // If the 16 bits following the high surrogate are in the source buffer...
      if (source < sourceEnd) {
        UChar32 ch2 = static_cast<uint16_t>(*source);
        // If it's a low surrogate, convert to UChar32.
        if (ch2 >= 0xDC00 && ch2 <= 0xDFFF) {
          ch = ((ch - 0xD800) << 10) + (ch2 - 0xDC00) + 0x0010000;
          ++source;
        } else if (strict) {  // it's an unpaired high surrogate
          --source;           // return to the illegal value itself
          result = sourceIllegal;
          break;
        }
      } else {     // We don't have the 16 bits following the high surrogate.
        --source;  // return to the high surrogate
        result = sourceExhausted;
        break;
      }
    } else if (strict) {
      // UTF-16 surrogate values are illegal in UTF-32
      if (ch >= 0xDC00 && ch <= 0xDFFF) {
        --source;  // return to the illegal value itself
        result = sourceIllegal;
        break;
      }
    }
    // Figure out how many bytes the result will require
    if (ch < static_cast<UChar32>(0x80)) {
      bytesToWrite = 1;
    } else if (ch < static_cast<UChar32>(0x800)) {
      bytesToWrite = 2;
    } else if (ch < static_cast<UChar32>(0x10000)) {
      bytesToWrite = 3;
    } else if (ch < static_cast<UChar32>(0x110000)) {
      bytesToWrite = 4;
    } else {
      bytesToWrite = 3;
      ch = replacementCharacter;
    }

    target += bytesToWrite;
    if (target > targetEnd) {
      source = oldSource;  // Back up source pointer!
      target -= bytesToWrite;
      result = targetExhausted;
      break;
    }
    switch (bytesToWrite) {
      case 4:
        *--target = static_cast<char>((ch | byteMark) & byteMask);
        ch >>= 6;
        [[fallthrough]];
      case 3:
        *--target = static_cast<char>((ch | byteMark) & byteMask);
        ch >>= 6;
        [[fallthrough]];
      case 2:
        *--target = static_cast<char>((ch | byteMark) & byteMask);
        ch >>= 6;
        [[fallthrough]];
      case 1:
        *--target = static_cast<char>(ch | firstByteMark[bytesToWrite]);
    }
    target += bytesToWrite;
  }
  *sourceStart = source;
  *targetStart = target;
  return result;
}

/**
 * Is this code point a BMP code point (U+0000..U+ffff)?
 * @param c 32-bit code point
 * @return TRUE or FALSE
 * @stable ICU 2.8
 */
#define U_IS_BMP(c) ((uint32_t)(c) <= 0xFFFF)

/**
 * Is this code point a supplementary code point (U+010000..U+10FFFF)?
 * @param c 32-bit code point
 * @return TRUE or FALSE
 * @stable ICU 2.8
 */
#define U_IS_SUPPLEMENTARY(c) ((uint32_t)((c)-0x010000) <= 0xFFFFF)

/**
 * Is this code point a surrogate (U+d800..U+dfff)?
 * @param c 32-bit code point
 * @return TRUE or FALSE
 * @stable ICU 2.4
 */
#define U_IS_SURROGATE(c) (((c)&0xFFFFF800) == 0xD800)

/**
 * Get the lead surrogate (0xD800..0xDBFF) for a
 * supplementary code point (0x010000..0x10FFFF).
 * @param supplementary 32-bit code point (U+010000..U+10FFFF)
 * @return lead surrogate (U+D800..U+DBFF) for supplementary
 * @stable ICU 2.4
 */
#define U16_LEAD(supplementary) (UChar)(((supplementary) >> 10) + 0xD7C0)

/**
 * Get the trail surrogate (0xDC00..0xDFFF) for a
 * supplementary code point (0x010000..0x10FFFF).
 * @param supplementary 32-bit code point (U+010000..U+10FFFF)
 * @return trail surrogate (U+DC00..U+DFFF) for supplementary
 * @stable ICU 2.4
 */
#define U16_TRAIL(supplementary) (UChar)(((supplementary)&0x3FF) | 0xDC00)

// This must be called with the length pre-determined by the first byte.
// If presented with a length > 4, this returns false.  The Unicode
// definition of UTF-8 goes up to 4-byte sequences.
static bool isLegalUTF8(const unsigned char* source, int length) {
  unsigned char a;
  const unsigned char* srcptr = source + length;
  switch (length) {
    default:
      return false;
    // Everything else falls through when "true"...
    case 4:
      if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return false;
      [[fallthrough]];
    case 3:
      if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return false;
      [[fallthrough]];
    case 2:
      if ((a = (*--srcptr)) > 0xBF) return false;

      // no fall-through in this inner switch
      switch (*source) {
        case 0xE0:
          if (a < 0xA0) return false;
          break;
        case 0xED:
          if (a > 0x9F) return false;
          break;
        case 0xF0:
          if (a < 0x90) return false;
          break;
        case 0xF4:
          if (a > 0x8F) return false;
          break;
        default:
          if (a < 0x80) return false;
      }
      [[fallthrough]];

    case 1:
      if (*source >= 0x80 && *source < 0xC2) return false;
  }
  if (*source > 0xF4) return false;
  return true;
}

// Magic values subtracted from a buffer value during UTF8 conversion.
// This table contains as many values as there might be trailing bytes
// in a UTF-8 sequence.
static const UChar32 offsetsFromUTF8[6] = {0x00000000UL,
                                           0x00003080UL,
                                           0x000E2080UL,
                                           0x03C82080UL,
                                           static_cast<UChar32>(0xFA082080UL),
                                           static_cast<UChar32>(0x82082080UL)};

static inline UChar32 readUTF8Sequence(const char*& sequence, size_t length) {
  UChar32 character = 0;

  // The cases all fall through.
  switch (length) {
    case 6:
      character += static_cast<unsigned char>(*sequence++);
      character <<= 6;
      [[fallthrough]];
    case 5:
      character += static_cast<unsigned char>(*sequence++);
      character <<= 6;
      [[fallthrough]];
    case 4:
      character += static_cast<unsigned char>(*sequence++);
      character <<= 6;
      [[fallthrough]];
    case 3:
      character += static_cast<unsigned char>(*sequence++);
      character <<= 6;
      [[fallthrough]];
    case 2:
      character += static_cast<unsigned char>(*sequence++);
      character <<= 6;
      [[fallthrough]];
    case 1:
      character += static_cast<unsigned char>(*sequence++);
  }

  return character - offsetsFromUTF8[length - 1];
}

ConversionResult convertUTF8ToUTF16(const char** sourceStart,
                                    const char* sourceEnd, UChar** targetStart,
                                    UChar* targetEnd, bool* sourceAllASCII,
                                    bool strict) {
  ConversionResult result = conversionOK;
  const char* source = *sourceStart;
  UChar* target = *targetStart;
  UChar orAllData = 0;
  while (source < sourceEnd) {
    int utf8SequenceLength = inlineUTF8SequenceLength(*source);
    if (sourceEnd - source < utf8SequenceLength) {
      result = sourceExhausted;
      break;
    }
    // Do this check whether lenient or strict
    if (!isLegalUTF8(reinterpret_cast<const unsigned char*>(source),
                     utf8SequenceLength)) {
      result = sourceIllegal;
      break;
    }

    UChar32 character = readUTF8Sequence(source, utf8SequenceLength);

    if (target >= targetEnd) {
      source -= utf8SequenceLength;  // Back up source pointer!
      result = targetExhausted;
      break;
    }

    if (U_IS_BMP(character)) {
      // UTF-16 surrogate values are illegal in UTF-32
      if (U_IS_SURROGATE(character)) {
        if (strict) {
          source -= utf8SequenceLength;  // return to the illegal value itself
          result = sourceIllegal;
          break;
        }
        *target++ = replacementCharacter;
        orAllData |= replacementCharacter;
      } else {
        *target++ = static_cast<UChar>(character);  // normal case
        orAllData |= character;
      }
    } else if (U_IS_SUPPLEMENTARY(character)) {
      // target is a character in range 0xFFFF - 0x10FFFF
      if (target + 1 >= targetEnd) {
        source -= utf8SequenceLength;  // Back up source pointer!
        result = targetExhausted;
        break;
      }
      *target++ = U16_LEAD(character);
      *target++ = U16_TRAIL(character);
      orAllData = 0xFFFF;
    } else {
      if (strict) {
        source -= utf8SequenceLength;  // return to the start
        result = sourceIllegal;
        break;  // Bail out; shouldn't continue
      } else {
        *target++ = replacementCharacter;
        orAllData |= replacementCharacter;
      }
    }
  }
  *sourceStart = source;
  *targetStart = target;

  if (sourceAllASCII) *sourceAllASCII = !(orAllData & ~0x7F);

  return result;
}

// Helper to write a three-byte UTF-8 code point to the buffer, caller must
// check room is available.
static inline void putUTF8Triple(char*& buffer, UChar ch) {
  *buffer++ = static_cast<char>(((ch >> 12) & 0x0F) | 0xE0);
  *buffer++ = static_cast<char>(((ch >> 6) & 0x3F) | 0x80);
  *buffer++ = static_cast<char>((ch & 0x3F) | 0x80);
}
}  // namespace

std::string UTF16ToUTF8(const UChar* stringStart, size_t length) {
  if (!stringStart || !length) return std::string();

  // Allocate a buffer big enough to hold all the characters
  // (an individual UTF-16 UChar can only expand to 3 UTF-8 bytes).
  // Optimization ideas, if we find this function is hot:
  //  * We could speculatively create a CStringBuffer to contain 'length'
  //    characters, and resize if necessary (i.e. if the buffer contains
  //    non-ascii characters). (Alternatively, scan the buffer first for
  //    ascii characters, so we know this will be sufficient).
  //  * We could allocate a CStringBuffer with an appropriate size to
  //    have a good chance of being able to write the string into the
  //    buffer without reallocing (say, 1.5 x length).
  if (length > std::numeric_limits<unsigned>::max() / 3) return std::string();

  std::string output(length * 3, '\0');
  const UChar* characters = stringStart;
  const UChar* characters_end = characters + length;
  char* buffer = &*output.begin();
  char* buffer_end = &*output.end();
  while (characters < characters_end) {
    // Use strict conversion to detect unpaired surrogates.
    ConversionResult result = convertUTF16ToUTF8(
        &characters, characters_end, &buffer, buffer_end, /* strict= */ true);
    DCHECK_NE(result, targetExhausted);
    // Conversion fails when there is an unpaired surrogate.  Put
    // replacement character (U+FFFD) instead of the unpaired
    // surrogate.
    if (result != conversionOK) {
      DCHECK_LE(0xD800, *characters);
      DCHECK_LE(*characters, 0xDFFF);
      // There should be room left, since one UChar hasn't been
      // converted.
      DCHECK_LE(buffer + 3, buffer_end);
      putUTF8Triple(buffer, replacementCharacter);
      ++characters;
    }
  }

  output.resize(buffer - output.data());
  return output;
}

std::basic_string<UChar> UTF8ToUTF16(const char* stringStart, size_t length) {
  if (!stringStart || !length) return std::basic_string<UChar>();
  std::vector<UChar> buffer(length);
  UChar* bufferStart = buffer.data();

  UChar* bufferCurrent = bufferStart;
  const char* stringCurrent = reinterpret_cast<const char*>(stringStart);
  if (convertUTF8ToUTF16(&stringCurrent,
                         reinterpret_cast<const char*>(stringStart + length),
                         &bufferCurrent, bufferCurrent + buffer.size(), nullptr,
                         true) != conversionOK)
    return std::basic_string<UChar>();
  size_t utf16Length = bufferCurrent - bufferStart;
  return std::basic_string<UChar>(bufferStart, bufferStart + utf16Length);
}

}  // namespace v8_inspector
