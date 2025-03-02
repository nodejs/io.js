// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_STRINGS_STRING_HASHER_INL_H_
#define V8_STRINGS_STRING_HASHER_INL_H_

#include "src/common/globals.h"
#include "src/strings/string-hasher.h"

#ifdef __SSE2__
#include <emmintrin.h>
#elif defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

// Comment inserted to prevent header reordering.
#include <type_traits>

#include "src/objects/name-inl.h"
#include "src/objects/string-inl.h"
#include "src/strings/char-predicates-inl.h"
#include "src/utils/utils-inl.h"
#include "third_party/rapidhash-v8/rapidhash.h"

namespace v8 {
namespace internal {

namespace detail {
V8_EXPORT_PRIVATE uint64_t HashConvertingTo8Bit(const uint16_t* chars,
                                                uint32_t length, uint64_t seed);
}

namespace {
template <typename T>
uint32_t ConvertRawHashToUsableHash(T raw_hash) {
  // Limit to the supported bits.
  const int32_t hash = static_cast<int32_t>(raw_hash & String::HashBits::kMax);
  // Ensure that the hash is kZeroHash, if the computed value is 0.
  return hash == 0 ? StringHasher::kZeroHash : hash;
}

V8_INLINE bool IsOnly8Bit(const uint16_t* chars, unsigned len) {
  // TODO(leszeks): This could be SIMD for efficiency on large strings, if we
  // need it.
  for (unsigned i = 0; i < len; ++i) {
    if (chars[i] > 255) {
      return false;
    }
  }
  return true;
}

V8_INLINE uint64_t GetRapidHash(const uint8_t* chars, uint32_t length,
                                uint64_t seed) {
  return rapidhash(chars, length, seed);
}

V8_INLINE uint64_t GetRapidHash(const uint16_t* chars, uint32_t length,
                                uint64_t seed) {
  // For 2-byte strings we need to preserve the same hash for strings in just
  // the latin-1 range.
  if (V8_UNLIKELY(IsOnly8Bit(chars, length))) {
    return detail::HashConvertingTo8Bit(chars, length, seed);
  }
  return rapidhash(reinterpret_cast<const uint8_t*>(chars), 2 * length, seed);
}

template <typename uchar>
V8_INLINE uint32_t GetUsableRapidHash(const uchar* chars, uint32_t length,
                                      uint64_t seed) {
  return ConvertRawHashToUsableHash(GetRapidHash(chars, length, seed));
}
}  // namespace

void RunningStringHasher::AddCharacter(uint16_t c) {
  running_hash_ += c;
  running_hash_ += (running_hash_ << 10);
  running_hash_ ^= (running_hash_ >> 6);
}

uint32_t RunningStringHasher::Finalize() {
  running_hash_ += (running_hash_ << 3);
  running_hash_ ^= (running_hash_ >> 11);
  running_hash_ += (running_hash_ << 15);
  return ConvertRawHashToUsableHash(running_hash_);
}

uint32_t StringHasher::GetTrivialHash(uint32_t length) {
  DCHECK_GT(length, String::kMaxHashCalcLength);
  // The hash of a large string is simply computed from the length.
  // Ensure that the max length is small enough to be encoded without losing
  // information.
  static_assert(String::kMaxLength <= String::HashBits::kMax);
  uint32_t hash = length;
  return String::CreateHashFieldValue(hash, String::HashFieldType::kHash);
}

uint32_t StringHasher::MakeArrayIndexHash(uint32_t value, uint32_t length) {
  // For array indexes mix the length into the hash as an array index could
  // be zero.
  DCHECK_LE(length, String::kMaxArrayIndexSize);
  DCHECK(TenToThe(String::kMaxCachedArrayIndexLength) <
         (1 << String::kArrayIndexValueBits));

  value <<= String::ArrayIndexValueBits::kShift;
  value |= length << String::ArrayIndexLengthBits::kShift;

  DCHECK(String::IsIntegerIndex(value));
  DCHECK_EQ(length <= String::kMaxCachedArrayIndexLength,
            Name::ContainsCachedArrayIndex(value));
  return value;
}

template <typename char_t>
uint32_t StringHasher::HashSequentialString(const char_t* chars_raw,
                                            uint32_t length, uint64_t seed) {
  static_assert(std::is_integral_v<char_t>);
  static_assert(sizeof(char_t) <= 2);
  using uchar = std::make_unsigned_t<char_t>;
  const uchar* chars = reinterpret_cast<const uchar*>(chars_raw);
  DCHECK_IMPLIES(length > 0, chars != nullptr);
  if (length >= 1) {
    if (length <= String::kMaxIntegerIndexSize && IsDecimalDigit(chars[0]) &&
        (length == 1 || chars[0] != '0')) {
      uint32_t index = chars[0] - '0';
      uint32_t i = 1;
      if (length <= String::kMaxArrayIndexSize) {
        // Possible array index; try to compute the array index hash.
        static_assert(String::kMaxArrayIndexSize <=
                      String::kMaxIntegerIndexSize);

        // We can safely add digits until `String::kMaxArrayIndexSize - 1`
        // without needing an overflow check -- if the whole string is
        // smaller than this, we can skip the overflow check entirely.
        bool needs_overflow_check = length == String::kMaxArrayIndexSize;

        uint32_t safe_length = needs_overflow_check ? length - 1 : length;
        for (; i < safe_length; i++) {
          char_t c = chars[i];
          if (!IsDecimalDigit(c)) {
            // If there is a non-decimal digit, we can skip doing anything
            // else and emit a non-index hash.
            goto non_index_hash;
          }
          index = (10 * index) + (c - '0');
        }
        DCHECK_EQ(i, safe_length);
        // If we didn't need to check for an overflowing value, we're
        // done.
        if (!needs_overflow_check) {
          DCHECK_EQ(i, length);
          return MakeArrayIndexHash(index, length);
        }
        // Otherwise, the last character needs to be checked for both being
        // digit, and the result being in bounds of the maximum array index.
        DCHECK_EQ(i, length - 1);
        char_t c = chars[i];
        if (!IsDecimalDigit(c)) {
          // If there is a non-decimal digit, we can skip doing anything
          // else and emit a non-index hash.
          goto non_index_hash;
        }
        if (TryAddArrayIndexChar(&index, c)) {
          return MakeArrayIndexHash(index, length);
        }
        // If the range check fails, this falls through into the integer index
        // check.
      }

      // The following block wouldn't do anything on 32-bit platforms,
      // because kMaxArrayIndexSize == kMaxIntegerIndexSize there, and
      // if we wanted to compile it everywhere, then {index_big} would
      // have to be a {size_t}, which the Mac compiler doesn't like to
      // implicitly cast to uint64_t for the {TryAddIndexChar} call.
#if V8_HOST_ARCH_64_BIT
      // No "else" here: if the block above was entered and fell through,
      // we have something that's not an array index but might still have
      // been all digits and therefore a valid in-range integer index.
      // Check if there are any remaining non-digit characters, and if
      // we fit inside the overflow.
      uint64_t index_big = index;
      for (; i < length; i++) {
        char_t c = chars[i];
        if (!IsDecimalDigit(c)) {
          // If there is a non-decimal digit, we can skip doing anything
          // else and emit a non-index hash.
          goto non_index_hash;
        }
        // We should never be anywhere near overflowing, so we can just do
        // one range check at the end.
        static_assert(kMaxSafeIntegerUint64 < (kMaxUInt64 / 100));
        DCHECK_LT(index_big, kMaxUInt64 / 100);

        index_big = (10 * index_big) + (c - '0');
      }
      if (index_big <= kMaxSafeIntegerUint64) {
        uint32_t hash = String::CreateHashFieldValue(
            GetUsableRapidHash(chars, length, seed),
            String::HashFieldType::kIntegerIndex);
        if (Name::ContainsCachedArrayIndex(hash)) {
          // The hash accidentally looks like a cached index. Fix that by
          // setting a bit that looks like a longer-than-cacheable string
          // length.
          hash |= (String::kMaxCachedArrayIndexLength + 1)
                  << String::ArrayIndexLengthBits::kShift;
        }
        DCHECK(!Name::ContainsCachedArrayIndex(hash));
        return hash;
      }
      // If the range check fails, this falls through into the non-index
      // hash case.
      // TODO(leszeks): Since we've bounded the length here to 16 or less,
      // we could consider calling rapidhash directly, as it special cases
      // small strings.
#endif
    } else if (length > String::kMaxHashCalcLength) {
      // We should never go down this path if we might have an index value.
      static_assert(String::kMaxHashCalcLength > String::kMaxIntegerIndexSize);
      static_assert(String::kMaxHashCalcLength > String::kMaxArrayIndexSize);
      return GetTrivialHash(length);
    }
  }

non_index_hash:
  // Non-index hash.
  return String::CreateHashFieldValue(GetUsableRapidHash(chars, length, seed),
                                      String::HashFieldType::kHash);
}

std::size_t SeededStringHasher::operator()(const char* name) const {
  return StringHasher::HashSequentialString(
      name, static_cast<uint32_t>(strlen(name)), hashseed_);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_STRINGS_STRING_HASHER_INL_H_
