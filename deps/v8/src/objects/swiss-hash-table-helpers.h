// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Collection of swiss table helpers that are independent from a specific
// container, like SwissNameDictionary. Taken almost in verbatim from Abseil,
// comments in this file indicate what is taken from what Abseil file.

#include <climits>
#include <cstdint>
#include <type_traits>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/memory.h"

#ifndef V8_OBJECTS_SWISS_HASH_TABLE_HELPERS_H_
#define V8_OBJECTS_SWISS_HASH_TABLE_HELPERS_H_

// The following #defines are taken from Abseil's have_sse.h (but renamed).
#ifndef V8_SWISS_TABLE_HAVE_SSE2_HOST
#if (defined(__SSE2__) ||  \
     (defined(_MSC_VER) && \
      (defined(_M_X64) || (defined(_M_IX86) && _M_IX86_FP >= 2))))
#define V8_SWISS_TABLE_HAVE_SSE2_HOST 1
#else
#define V8_SWISS_TABLE_HAVE_SSE2_HOST 0
#endif
#endif

#ifndef V8_SWISS_TABLE_HAVE_SSSE3_HOST
#if defined(__SSSE3__)
#define V8_SWISS_TABLE_HAVE_SSSE3_HOST 1
#else
#define V8_SWISS_TABLE_HAVE_SSSE3_HOST 0
#endif
#endif

#if V8_SWISS_TABLE_HAVE_SSSE3_HOST && !V8_SWISS_TABLE_HAVE_SSE2_HOST
#error "Bad configuration!"
#endif

// Unlike Abseil, we cannot select SSE purely by host capabilities. When
// creating a snapshot, the group width must be compatible. The SSE
// implementation uses a group width of 16, whereas the non-SSE version uses 8.
// Thus we select the group size based on target capabilities and, if the host
// does not match, select a polyfill implementation. This means, in supported
// cross-compiling configurations, we must be able to determine matching target
// capabilities from the host.
#ifndef V8_SWISS_TABLE_HAVE_SSE2_TARGET
#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
// x64 always has SSE2, and ia32 without SSE2 is not supported by V8.
#define V8_SWISS_TABLE_HAVE_SSE2_TARGET 1
#else
#define V8_SWISS_TABLE_HAVE_SSE2_TARGET 0
#endif
#endif

#if V8_SWISS_TABLE_HAVE_SSE2_HOST
#include <emmintrin.h>
#endif

#if V8_SWISS_TABLE_HAVE_SSSE3_HOST
#include <tmmintrin.h>
#endif

namespace v8 {
namespace internal {
namespace swiss_table {

// All definitions below are taken from Abseil's raw_hash_set.h with only minor
// changes, like using existing V8 versions of certain helper functions.

// Denotes the group of the control table currently being probed.
// Implements quadratic probing by advancing by i groups after the i-th
// (unsuccessful) probe.
template <size_t GroupSize>
class ProbeSequence {
 public:
  ProbeSequence(uint32_t hash, uint32_t mask) {
    // Mask must be a power of 2 minus 1.
    DCHECK_EQ(0, ((mask + 1) & mask));
    mask_ = mask;
    offset_ = hash & mask_;
  }
  uint32_t offset() const { return offset_; }
  uint32_t offset(int i) const { return (offset_ + i) & mask_; }

  void next() {
    index_ += GroupSize;
    offset_ += index_;
    offset_ &= mask_;
  }

  size_t index() const { return index_; }

 private:
  // Used for modulo calculation.
  uint32_t mask_;

  // The index/offset into the control table, meaning that {ctrl[offset_]} is
  // the start of the group currently being probed, assuming that |ctrl| is the
  // pointer to the beginning of the control table.
  uint32_t offset_;

  // States the number of probes that have been performed (starting at 0),
  // multiplied by GroupSize.
  uint32_t index_ = 0;
};

// An abstraction over a bitmask. It provides an easy way to iterate through the
// indexes of the set bits of a bitmask. When Shift=0 (platforms with SSE),
// this is a true bitmask.
// When Shift=3 (used on non-SSE platforms), we obtain a "byte mask", where each
// logical bit is represented by a full byte. The logical bit 0 is represented
// as 0x00, whereas 1 is represented as 0x80. Other values must not appear.
//
// For example:
//   for (int i : BitMask<uint32_t, 16>(0x5)) -> yields 0, 2
//   for (int i : BitMask<uint64_t, 8, 3>(0x0000000080800000)) -> yields 2, 3
template <class T, int SignificantBits, int Shift = 0>
class BitMask {
  static_assert(std::is_unsigned<T>::value);
  static_assert(Shift == 0 || Shift == 3);

 public:
  // These are useful for unit tests (gunit).
  using value_type = int;
  using iterator = BitMask;
  using const_iterator = BitMask;

  explicit BitMask(T mask) : mask_(mask) {}
  BitMask& operator++() {
    // Clear the least significant bit that is set.
    mask_ &= (mask_ - 1);
    return *this;
  }
  explicit operator bool() const { return mask_ != 0; }
  int operator*() const { return LowestBitSet(); }
  int LowestBitSet() const { return TrailingZeros(); }
  int HighestBitSet() const {
    return (sizeof(T) * CHAR_BIT - base::bits::CountLeadingZeros(mask_) - 1) >>
           Shift;
  }

  BitMask begin() const { return *this; }
  BitMask end() const { return BitMask(0); }

  int TrailingZeros() const {
    DCHECK_NE(mask_, 0);
    return base::bits::CountTrailingZerosNonZero(mask_) >> Shift;
  }

  int LeadingZeros() const {
    constexpr int total_significant_bits = SignificantBits << Shift;
    constexpr int extra_bits = sizeof(T) * 8 - total_significant_bits;
    return base::bits::CountLeadingZeros(mask_ << extra_bits) >> Shift;
  }

 private:
  friend bool operator==(const BitMask& a, const BitMask& b) {
    return a.mask_ == b.mask_;
  }
  friend bool operator!=(const BitMask& a, const BitMask& b) {
    return a.mask_ != b.mask_;
  }

  T mask_;
};

using ctrl_t = signed char;
using h2_t = uint8_t;

// The values here are selected for maximum performance. See the static asserts
// below for details.
enum Ctrl : ctrl_t {
  kEmpty = -128,   // 0b10000000
  kDeleted = -2,   // 0b11111110
  kSentinel = -1,  // 0b11111111
};
static_assert(
    kEmpty & kDeleted & kSentinel & 0x80,
    "Special markers need to have the MSB to make checking for them efficient");
static_assert(kEmpty < kSentinel && kDeleted < kSentinel,
              "kEmpty and kDeleted must be smaller than kSentinel to make the "
              "SIMD test of IsEmptyOrDeleted() efficient");
static_assert(kSentinel == -1,
              "kSentinel must be -1 to elide loading it from memory into SIMD "
              "registers (pcmpeqd xmm, xmm)");
static_assert(kEmpty == -128,
              "kEmpty must be -128 to make the SIMD check for its "
              "existence efficient (psignb xmm, xmm)");
static_assert(~kEmpty & ~kDeleted & kSentinel & 0x7F,
              "kEmpty and kDeleted must share an unset bit that is not shared "
              "by kSentinel to make the scalar test for MatchEmptyOrDeleted() "
              "efficient");
static_assert(kDeleted == -2,
              "kDeleted must be -2 to make the implementation of "
              "ConvertSpecialToEmptyAndFullToDeleted efficient");

// See below for explanation of H2. Just here for documentation purposes, Swiss
// Table implementations rely on this being 7.
static constexpr int kH2Bits = 7;

static constexpr int kNotFullMask = (1 << kH2Bits);
static_assert(
    kEmpty & kDeleted & kSentinel & kNotFullMask,
    "Special markers need to have the MSB to make checking for them efficient");

// Extracts H1 from the given overall hash, which means discarding the lowest 7
// bits of the overall hash. H1 is used to determine the first group to probe.
inline static uint32_t H1(uint32_t hash) { return (hash >> kH2Bits); }

// Extracts H2 from the given overall hash, which means using only the lowest 7
// bits of the overall hash. H2 is stored in the control table byte for each
// present entry.
inline static swiss_table::ctrl_t H2(uint32_t hash) {
  return hash & ((1 << kH2Bits) - 1);
}

#if V8_SWISS_TABLE_HAVE_SSE2_HOST
struct GroupSse2Impl {
  static constexpr size_t kWidth = 16;  // the number of slots per group

  explicit GroupSse2Impl(const ctrl_t* pos) {
    ctrl = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
  }

  // Returns a bitmask representing the positions of slots that match |hash|.
  BitMask<uint32_t, kWidth> Match(h2_t hash) const {
    auto match = _mm_set1_epi8(hash);
    return BitMask<uint32_t, kWidth>(
        _mm_movemask_epi8(_mm_cmpeq_epi8(match, ctrl)));
  }

  // Returns a bitmask representing the positions of empty slots.
  BitMask<uint32_t, kWidth> MatchEmpty() const {
#if V8_SWISS_TABLE_HAVE_SSSE3_HOST
    // This only works because kEmpty is -128.
    return BitMask<uint32_t, kWidth>(
        _mm_movemask_epi8(_mm_sign_epi8(ctrl, ctrl)));
#else
    return Match(static_cast<h2_t>(kEmpty));
#endif
  }

  __m128i ctrl;
};
#endif  // V8_SWISS_TABLE_HAVE_SSE2_HOST

// A portable, inefficient version of GroupSse2Impl. This exists so SSE2-less
// hosts can generate snapshots for SSE2-capable targets.
struct GroupSse2Polyfill {
  static constexpr size_t kWidth = 16;  // the number of slots per group

  explicit GroupSse2Polyfill(const ctrl_t* pos) { memcpy(ctrl_, pos, kWidth); }

  // Returns a bitmask representing the positions of slots that match |hash|.
  BitMask<uint32_t, kWidth> Match(h2_t hash) const {
    uint32_t mask = 0;
    for (size_t i = 0; i < kWidth; i++) {
      if (static_cast<h2_t>(ctrl_[i]) == hash) {
        mask |= 1u << i;
      }
    }
    return BitMask<uint32_t, kWidth>(mask);
  }

  // Returns a bitmask representing the positions of empty slots.
  BitMask<uint32_t, kWidth> MatchEmpty() const {
    return Match(static_cast<h2_t>(kEmpty));
  }

 private:
  uint32_t MatchEmptyOrDeletedMask() const {
    uint32_t mask = 0;
    for (size_t i = 0; i < kWidth; i++) {
      if (ctrl_[i] < kSentinel) {
        mask |= 1u << i;
      }
    }
    return mask;
  }

  ctrl_t ctrl_[kWidth];
};

struct GroupPortableImpl {
  static constexpr size_t kWidth = 8;  // the number of slots per group

  explicit GroupPortableImpl(const ctrl_t* pos)
      : ctrl(base::ReadLittleEndianValue<uint64_t>(
            reinterpret_cast<uintptr_t>(const_cast<ctrl_t*>(pos)))) {}

  static constexpr uint64_t kMsbs = 0x8080808080808080ULL;
  static constexpr uint64_t kLsbs = 0x0101010101010101ULL;

  // Returns a bitmask representing the positions of slots that match |hash|.
  BitMask<uint64_t, kWidth, 3> Match(h2_t hash) const {
    // For the technique, see:
    // http://graphics.stanford.edu/~seander/bithacks.html##ValueInWord
    // (Determine if a word has a byte equal to n).
    //
    // Caveat: there are false positives but:
    // - they only occur if |hash| actually appears elsewhere in |ctrl|
    // - they never occur on kEmpty, kDeleted, kSentinel
    // - they will be handled gracefully by subsequent checks in code
    //
    // Example:
    //   v = 0x1716151413121110
    //   hash = 0x12
    //   retval = (v - lsbs) & ~v & msbs = 0x0000000080800000
    auto x = ctrl ^ (kLsbs * hash);
    return BitMask<uint64_t, kWidth, 3>((x - kLsbs) & ~x & kMsbs);
  }

  // Returns a bitmask representing the positions of empty slots.
  BitMask<uint64_t, kWidth, 3> MatchEmpty() const {
    return BitMask<uint64_t, kWidth, 3>((ctrl & (~ctrl << 6)) & kMsbs);
  }

  uint64_t ctrl;
};

// Determine which Group implementation SwissNameDictionary uses.
#if defined(V8_ENABLE_SWISS_NAME_DICTIONARY) && DEBUG
// TODO(v8:11388) If v8_enable_swiss_name_dictionary is enabled, we are supposed
// to use SwissNameDictionary as the dictionary backing store. If we want to use
// the SIMD version of SwissNameDictionary, that would require us to compile SSE
// instructions into the snapshot that exceed the minimum requirements for V8
// SSE support. Therefore, this fails a DCHECK. However, given the experimental
// nature of v8_enable_swiss_name_dictionary mode, we only except this to be run
// by developers/bots, that always have the necessary instructions. This means
// that if v8_enable_swiss_name_dictionary is enabled and debug mode isn't, we
// ignore the DCHECK that would fail in debug mode. However, if both
// v8_enable_swiss_name_dictionary and debug mode are enabled, we must fallback
// to the non-SSE implementation. Given that V8 requires SSE2, there should be a
// solution that doesn't require the workaround present here. Instead, the
// backend should only use SSE2 when compiling the SIMD version of
// SwissNameDictionary into the builtin.
using Group = GroupPortableImpl;
#elif V8_SWISS_TABLE_HAVE_SSE2_TARGET
// Use a matching group size between host and target.
#if V8_SWISS_TABLE_HAVE_SSE2_HOST
using Group = GroupSse2Impl;
#else
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
// If we do not detect SSE2 when building for the ia32/x64 target, the
// V8_SWISS_TABLE_HAVE_SSE2_TARGET logic will incorrectly cause the final output
// to use the inefficient polyfill implementation. Detect this case and warn if
// it happens.
#warning "Did not detect required SSE2 support on ia32/x64."
#endif
using Group = GroupSse2Polyfill;
#endif
#else
using Group = GroupPortableImpl;
#endif

}  // namespace swiss_table
}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_SWISS_HASH_TABLE_HELPERS_H_
