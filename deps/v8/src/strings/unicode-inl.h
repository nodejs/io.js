// Copyright 2007-2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_UNICODE_INL_H_
#define V8_STRINGS_UNICODE_INL_H_

#include "src/base/logging.h"
#include "src/strings/unicode.h"
#include "src/utils/utils.h"

namespace unibrow {

#ifndef V8_INTL_SUPPORT
template <class T, int s>
bool Predicate<T, s>::get(uchar code_point) {
  CacheEntry entry = entries_[code_point & kMask];
  if (entry.code_point() == code_point) return entry.value();
  return CalculateValue(code_point);
}

template <class T, int s>
bool Predicate<T, s>::CalculateValue(uchar code_point) {
  bool result = T::Is(code_point);
  entries_[code_point & kMask] = CacheEntry(code_point, result);
  return result;
}

template <class T, int s>
int Mapping<T, s>::get(uchar c, uchar n, uchar* result) {
  CacheEntry entry = entries_[c & kMask];
  if (entry.code_point_ == c) {
    if (entry.offset_ == 0) {
      return 0;
    } else {
      result[0] = c + entry.offset_;
      return 1;
    }
  } else {
    return CalculateValue(c, n, result);
  }
}

template <class T, int s>
int Mapping<T, s>::CalculateValue(uchar c, uchar n, uchar* result) {
  bool allow_caching = true;
  int length = T::Convert(c, n, result, &allow_caching);
  if (allow_caching) {
    if (length == 1) {
      entries_[c & kMask] = CacheEntry(c, result[0] - c);
      return 1;
    } else {
      entries_[c & kMask] = CacheEntry(c, 0);
      return 0;
    }
  } else {
    return length;
  }
}
#endif  // !V8_INTL_SUPPORT

bool Utf16::HasUnpairedSurrogate(const uint16_t* code_units, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const int code_unit = code_units[i];
    if (IsLeadSurrogate(code_unit)) {
      // The current code unit is a leading surrogate. Check if it is followed
      // by a trailing surrogate.
      if (i == length - 1) return true;
      if (!IsTrailSurrogate(code_units[i + 1])) return true;
      // Skip the paired trailing surrogate.
      ++i;
    } else if (IsTrailSurrogate(code_unit)) {
      // All paired trailing surrogates are skipped above, so this branch is
      // only for those that are unpaired.
      return true;
    }
  }
  return false;
}

// Decodes UTF-8 bytes incrementally, allowing the decoding of bytes as they
// stream in. This **must** be followed by a call to ValueOfIncrementalFinish
// when the stream is complete, to ensure incomplete sequences are handled.
uchar Utf8::ValueOfIncremental(const uint8_t** cursor, State* state,
                               Utf8IncrementalBuffer* buffer) {
  DCHECK_NOT_NULL(buffer);
  State old_state = *state;
  uint8_t next = **cursor;
  *cursor += 1;

  if (V8_LIKELY(next <= kMaxOneByteChar && old_state == State::kAccept)) {
    DCHECK_EQ(0u, *buffer);
    return static_cast<uchar>(next);
  }

  // So we're at the lead byte of a 2/3/4 sequence, or we're at a continuation
  // char in that sequence.
  Utf8DfaDecoder::Decode(next, state, buffer);

  switch (*state) {
    case State::kAccept: {
      uchar t = *buffer;
      *buffer = 0;
      return t;
    }

    case State::kReject:
      *state = State::kAccept;
      *buffer = 0;

      // If we hit a bad byte, we need to determine if we were trying to start
      // a sequence or continue one. If we were trying to start a sequence,
      // that means it's just an invalid lead byte and we need to continue to
      // the next (which we already did above). If we were already in a
      // sequence, we need to reprocess this same byte after resetting to the
      // initial state.
      if (old_state != State::kAccept) {
        // We were trying to continue a sequence, so let's reprocess this byte
        // next time.
        *cursor -= 1;
      }
      return kBadChar;

    default:
      return kIncomplete;
  }
}

unsigned Utf8::EncodeOneByte(char* str, uint8_t c) {
  static const int kMask = ~(1 << 6);
  if (c <= kMaxOneByteChar) {
    str[0] = c;
    return 1;
  } else {
    str[0] = 0xC0 | (c >> 6);
    str[1] = 0x80 | (c & kMask);
    return 2;
  }
}

// Encode encodes the UTF-16 code units c and previous into the given str
// buffer, and combines surrogate code units into single code points. If
// replace_invalid is set to true, orphan surrogate code units will be replaced
// with kBadChar.
unsigned Utf8::Encode(char* str, uchar c, int previous, bool replace_invalid) {
  static const int kMask = ~(1 << 6);
  if (c <= kMaxOneByteChar) {
    str[0] = c;
    return 1;
  } else if (c <= kMaxTwoByteChar) {
    str[0] = 0xC0 | (c >> 6);
    str[1] = 0x80 | (c & kMask);
    return 2;
  } else if (c <= kMaxThreeByteChar) {
    DCHECK(!Utf16::IsLeadSurrogate(Utf16::kNoPreviousCharacter));
    if (Utf16::IsSurrogatePair(previous, c)) {
      const int kUnmatchedSize = kSizeOfUnmatchedSurrogate;
      return Encode(str - kUnmatchedSize,
                    Utf16::CombineSurrogatePair(previous, c),
                    Utf16::kNoPreviousCharacter, replace_invalid) -
             kUnmatchedSize;
    } else if (replace_invalid &&
               (Utf16::IsLeadSurrogate(c) || Utf16::IsTrailSurrogate(c))) {
      c = kBadChar;
    }
    str[0] = 0xE0 | (c >> 12);
    str[1] = 0x80 | ((c >> 6) & kMask);
    str[2] = 0x80 | (c & kMask);
    return 3;
  } else {
    str[0] = 0xF0 | (c >> 18);
    str[1] = 0x80 | ((c >> 12) & kMask);
    str[2] = 0x80 | ((c >> 6) & kMask);
    str[3] = 0x80 | (c & kMask);
    return 4;
  }
}

uchar Utf8::ValueOf(const uint8_t* bytes, size_t length, size_t* cursor) {
  if (length == 0) return kBadChar;
  uint8_t first = bytes[0];
  // Characters between 0000 and 007F are encoded as a single character
  if (V8_LIKELY(first <= kMaxOneByteChar)) {
    *cursor += 1;
    return first;
  }
  return CalculateValue(bytes, length, cursor);
}

unsigned Utf8::LengthOneByte(uint8_t c) {
  if (c <= kMaxOneByteChar) {
    return 1;
  } else {
    return 2;
  }
}

unsigned Utf8::Length(uchar c, int previous) {
  if (c <= kMaxOneByteChar) {
    return 1;
  } else if (c <= kMaxTwoByteChar) {
    return 2;
  } else if (c <= kMaxThreeByteChar) {
    DCHECK(!Utf16::IsLeadSurrogate(Utf16::kNoPreviousCharacter));
    if (Utf16::IsSurrogatePair(previous, c)) {
      return kSizeOfUnmatchedSurrogate - kBytesSavedByCombiningSurrogates;
    }
    return 3;
  } else {
    return 4;
  }
}

bool Utf8::IsValidCharacter(uchar c) {
  return c < 0xD800u || (c >= 0xE000u && c < 0xFDD0u) ||
         (c > 0xFDEFu && c <= 0x10FFFFu && (c & 0xFFFEu) != 0xFFFEu &&
          c != kBadChar);
}

template <typename Char>
Utf8::EncodingResult Utf8::Encode(v8::base::Vector<const Char> string,
                                  char* buffer, size_t capacity,
                                  bool write_null, bool replace_invalid_utf8) {
  constexpr bool kSourceIsOneByte = sizeof(Char) == 1;

  if constexpr (kSourceIsOneByte) {
    // Only 16-bit characters can contain invalid unicode.
    replace_invalid_utf8 = false;
  }

  size_t write_index = 0;
  const Char* characters = string.begin();
  size_t content_capacity = capacity - write_null;
  CHECK_LE(content_capacity, capacity);
  uint16_t last = Utf16::kNoPreviousCharacter;
  size_t read_index = 0;
  for (; read_index < string.size(); read_index++) {
    Char character = characters[read_index];

    size_t required_capacity;
    if constexpr (kSourceIsOneByte) {
      required_capacity = Utf8::LengthOneByte(character);
    } else {
      required_capacity = Utf8::Length(character, last);
    }
    size_t remaining_capacity = content_capacity - write_index;
    if (remaining_capacity < required_capacity) {
      // Not enough space left, so stop here.
      if (replace_invalid_utf8 && Utf16::IsLeadSurrogate(last)) {
        DCHECK_GE(write_index, Utf8::kSizeOfUnmatchedSurrogate);
        // We're in the middle of a surrogate pair. Delete the first part again.
        write_index -= Utf8::kSizeOfUnmatchedSurrogate;
      }
      break;
    }

    if constexpr (kSourceIsOneByte) {
      write_index += Utf8::EncodeOneByte(buffer + write_index, character);
    } else {
      write_index += Utf8::Encode(buffer + write_index, character, last,
                                  replace_invalid_utf8);
    }

    last = character;
  }
  DCHECK_LE(write_index, capacity);

  if (write_null) {
    DCHECK_LT(write_index, capacity);
    buffer[write_index++] = '\0';
  }

  size_t bytes_written = write_index;
  size_t characters_processed = read_index;
  return {bytes_written, characters_processed};
}

}  // namespace unibrow

#endif  // V8_STRINGS_UNICODE_INL_H_
