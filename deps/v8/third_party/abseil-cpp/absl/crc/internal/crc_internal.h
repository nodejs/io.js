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

#ifndef ABSL_CRC_INTERNAL_CRC_INTERNAL_H_
#define ABSL_CRC_INTERNAL_CRC_INTERNAL_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/crc/internal/crc.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace crc_internal {

// Prefetch constants used in some Extend() implementations
constexpr int kPrefetchHorizon = ABSL_CACHELINE_SIZE * 4;  // Prefetch this far
// Shorter prefetch distance for smaller buffers
constexpr int kPrefetchHorizonMedium = ABSL_CACHELINE_SIZE * 1;
static_assert(kPrefetchHorizon >= 64, "CRCPrefetchHorizon less than loop len");

// We require the Scramble() function:
//  - to be reversible (Unscramble() must exist)
//  - to be non-linear in the polynomial's Galois field (so the CRC of a
//    scrambled CRC is not linearly affected by the scrambled CRC, even if
//    using the same polynomial)
//  - not to be its own inverse.  Preferably, if X=Scramble^N(X) and N!=0, then
//    N is large.
//  - to be fast.
//  - not to change once defined.
// We introduce non-linearity in two ways:
//     Addition of a constant.
//         - The carries introduce non-linearity; we use bits of an irrational
//           (phi) to make it unlikely that we introduce no carries.
//     Rotate by a constant number of bits.
//         - We use floor(degree/2)+1, which does not divide the degree, and
//           splits the bits nearly evenly, which makes it less likely the
//           halves will be the same or one will be all zeroes.
// We do both things to improve the chances of non-linearity in the face of
// bit patterns with low numbers of bits set, while still being fast.
// Below is the constant that we add.  The bits are the first 128 bits of the
// fractional part of phi, with a 1 ored into the bottom bit to maximize the
// cycle length of repeated adds.
constexpr uint64_t kScrambleHi = (static_cast<uint64_t>(0x4f1bbcdcU) << 32) |
                                 static_cast<uint64_t>(0xbfa53e0aU);
constexpr uint64_t kScrambleLo = (static_cast<uint64_t>(0xf9ce6030U) << 32) |
                                 static_cast<uint64_t>(0x2e76e41bU);

class CRCImpl : public CRC {  // Implementation of the abstract class CRC
 public:
  using Uint32By256 = uint32_t[256];

  CRCImpl() = default;
  ~CRCImpl() override = default;

  // The internal version of CRC::New().
  static CRCImpl* NewInternal();

  // Fill in a table for updating a CRC by one word of 'word_size' bytes
  // [last_lo, last_hi] contains the answer if the last bit in the word
  // is set.
  static void FillWordTable(uint32_t poly, uint32_t last, int word_size,
                            Uint32By256* t);

  // Build the table for extending by zeroes, returning the number of entries.
  // For a in {1, 2, ..., ZEROES_BASE-1}, b in {0, 1, 2, 3, ...},
  // entry j=a-1+(ZEROES_BASE-1)*b
  // contains a polynomial Pi such that multiplying
  // a CRC by Pi mod P, where P is the CRC polynomial, is equivalent to
  // appending a*2**(ZEROES_BASE_LG*b) zero bytes to the original string.
  static int FillZeroesTable(uint32_t poly, Uint32By256* t);

  virtual void InitTables() = 0;

 private:
  CRCImpl(const CRCImpl&) = delete;
  CRCImpl& operator=(const CRCImpl&) = delete;
};

// This is the 32-bit implementation.  It handles all sizes from 8 to 32.
class CRC32 : public CRCImpl {
 public:
  CRC32() = default;
  ~CRC32() override = default;

  void Extend(uint32_t* crc, const void* bytes, size_t length) const override;
  void ExtendByZeroes(uint32_t* crc, size_t length) const override;
  void Scramble(uint32_t* crc) const override;
  void Unscramble(uint32_t* crc) const override;
  void UnextendByZeroes(uint32_t* crc, size_t length) const override;

  void InitTables() override;

 private:
  // Common implementation guts for ExtendByZeroes and UnextendByZeroes().
  //
  // zeroes_table is a table as returned by FillZeroesTable(), containing
  // polynomials representing CRCs of strings-of-zeros of various lengths,
  // and which can be combined by polynomial multiplication.  poly_table is
  // a table of CRC byte extension values.  These tables are determined by
  // the generator polynomial.
  //
  // These will be set to reverse_zeroes_ and reverse_table0_ for Unextend, and
  // CRC32::zeroes_ and CRC32::table0_ for Extend.
  static void ExtendByZeroesImpl(uint32_t* crc, size_t length,
                                 const uint32_t zeroes_table[256],
                                 const uint32_t poly_table[256]);

  uint32_t table0_[256];  // table of byte extensions
  uint32_t zeroes_[256];  // table of zero extensions

  // table of 4-byte extensions shifted by 12 bytes of zeroes
  uint32_t table_[4][256];

  // Reverse lookup tables, using the alternate polynomial used by
  // UnextendByZeroes().
  uint32_t reverse_table0_[256];  // table of reverse byte extensions
  uint32_t reverse_zeroes_[256];  // table of reverse zero extensions

  CRC32(const CRC32&) = delete;
  CRC32& operator=(const CRC32&) = delete;
};

// Helpers

// Return a bit mask containing len 1-bits.
// Requires 0 < len <= sizeof(T)
template <typename T>
T MaskOfLength(int len) {
  // shift 2 by len-1 rather than 1 by len because shifts of wordsize
  // are undefined.
  return (T(2) << (len - 1)) - 1;
}

// Rotate low-order "width" bits of "in" right by "r" bits,
// setting other bits in word to arbitrary values.
template <typename T>
T RotateRight(T in, int width, int r) {
  return (in << (width - r)) | ((in >> r) & MaskOfLength<T>(width - r));
}

// RoundUp<N>(p) returns the lowest address >= p aligned to an N-byte
// boundary.  Requires that N is a power of 2.
template <int alignment>
const uint8_t* RoundUp(const uint8_t* p) {
  static_assert((alignment & (alignment - 1)) == 0, "alignment is not 2^n");
  constexpr uintptr_t mask = alignment - 1;
  const uintptr_t as_uintptr = reinterpret_cast<uintptr_t>(p);
  return reinterpret_cast<const uint8_t*>((as_uintptr + mask) & ~mask);
}

// Return a newly created CRC32AcceleratedX86ARMCombined if we can use Intel's
// or ARM's CRC acceleration for a given polynomial.  Return nullptr otherwise.
CRCImpl* TryNewCRC32AcceleratedX86ARMCombined();

// Return all possible hardware accelerated implementations. For testing only.
std::vector<std::unique_ptr<CRCImpl>> NewCRC32AcceleratedX86ARMCombinedAll();

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CRC_INTERNAL_CRC_INTERNAL_H_
