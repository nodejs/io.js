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

// Hardware accelerated CRC32 computation on Intel and ARM architecture.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/prefetch.h"
#include "absl/crc/internal/cpu_detect.h"
#include "absl/crc/internal/crc32_x86_arm_combined_simd.h"
#include "absl/crc/internal/crc_internal.h"
#include "absl/memory/memory.h"
#include "absl/numeric/bits.h"

#if defined(ABSL_CRC_INTERNAL_HAVE_ARM_SIMD) || \
    defined(ABSL_CRC_INTERNAL_HAVE_X86_SIMD)
#define ABSL_INTERNAL_CAN_USE_SIMD_CRC32C
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

#if defined(ABSL_INTERNAL_CAN_USE_SIMD_CRC32C)

// Implementation details not exported outside of file
namespace {

// Some machines have CRC acceleration hardware.
// We can do a faster version of Extend() on such machines.
class CRC32AcceleratedX86ARMCombined : public CRC32 {
 public:
  CRC32AcceleratedX86ARMCombined() {}
  ~CRC32AcceleratedX86ARMCombined() override {}
  void ExtendByZeroes(uint32_t* crc, size_t length) const override;
  uint32_t ComputeZeroConstant(size_t length) const;

 private:
  CRC32AcceleratedX86ARMCombined(const CRC32AcceleratedX86ARMCombined&) =
      delete;
  CRC32AcceleratedX86ARMCombined& operator=(
      const CRC32AcceleratedX86ARMCombined&) = delete;
};

// Constants for switching between algorithms.
// Chosen by comparing speed at different powers of 2.
constexpr size_t kSmallCutoff = 256;
constexpr size_t kMediumCutoff = 2048;

#define ABSL_INTERNAL_STEP1(crc)                      \
  do {                                                \
    crc = CRC32_u8(static_cast<uint32_t>(crc), *p++); \
  } while (0)
#define ABSL_INTERNAL_STEP2(crc)                                               \
  do {                                                                         \
    crc =                                                                      \
        CRC32_u16(static_cast<uint32_t>(crc), absl::little_endian::Load16(p)); \
    p += 2;                                                                    \
  } while (0)
#define ABSL_INTERNAL_STEP4(crc)                                               \
  do {                                                                         \
    crc =                                                                      \
        CRC32_u32(static_cast<uint32_t>(crc), absl::little_endian::Load32(p)); \
    p += 4;                                                                    \
  } while (0)
#define ABSL_INTERNAL_STEP8(crc, data)                  \
  do {                                                  \
    crc = CRC32_u64(static_cast<uint32_t>(crc),         \
                    absl::little_endian::Load64(data)); \
    data += 8;                                          \
  } while (0)
#define ABSL_INTERNAL_STEP8BY2(crc0, crc1, p0, p1) \
  do {                                             \
    ABSL_INTERNAL_STEP8(crc0, p0);                 \
    ABSL_INTERNAL_STEP8(crc1, p1);                 \
  } while (0)
#define ABSL_INTERNAL_STEP8BY3(crc0, crc1, crc2, p0, p1, p2) \
  do {                                                       \
    ABSL_INTERNAL_STEP8(crc0, p0);                           \
    ABSL_INTERNAL_STEP8(crc1, p1);                           \
    ABSL_INTERNAL_STEP8(crc2, p2);                           \
  } while (0)

namespace {

uint32_t multiply(uint32_t a, uint32_t b) {
  V128 shifts = V128_From2x64(0, 1);
  V128 power = V128_From2x64(0, a);
  V128 crc = V128_From2x64(0, b);
  V128 res = V128_PMulLow(power, crc);

  // Combine crc values
  res = V128_ShiftLeft64(res, shifts);
  return static_cast<uint32_t>(V128_Extract32<1>(res)) ^
         CRC32_u32(0, static_cast<uint32_t>(V128_Low64(res)));
}

// Powers of crc32c polynomial, for faster ExtendByZeros.
// Verified against folly:
// folly/hash/detail/Crc32CombineDetail.cpp
constexpr uint32_t kCRC32CPowers[] = {
    0x82f63b78, 0x6ea2d55c, 0x18b8ea18, 0x510ac59a, 0xb82be955, 0xb8fdb1e7,
    0x88e56f72, 0x74c360a4, 0xe4172b16, 0x0d65762a, 0x35d73a62, 0x28461564,
    0xbf455269, 0xe2ea32dc, 0xfe7740e6, 0xf946610b, 0x3c204f8f, 0x538586e3,
    0x59726915, 0x734d5309, 0xbc1ac763, 0x7d0722cc, 0xd289cabe, 0xe94ca9bc,
    0x05b74f3f, 0xa51e1f42, 0x40000000, 0x20000000, 0x08000000, 0x00800000,
    0x00008000, 0x82f63b78, 0x6ea2d55c, 0x18b8ea18, 0x510ac59a, 0xb82be955,
    0xb8fdb1e7, 0x88e56f72, 0x74c360a4, 0xe4172b16, 0x0d65762a, 0x35d73a62,
    0x28461564, 0xbf455269, 0xe2ea32dc, 0xfe7740e6, 0xf946610b, 0x3c204f8f,
    0x538586e3, 0x59726915, 0x734d5309, 0xbc1ac763, 0x7d0722cc, 0xd289cabe,
    0xe94ca9bc, 0x05b74f3f, 0xa51e1f42, 0x40000000, 0x20000000, 0x08000000,
    0x00800000, 0x00008000,
};

}  // namespace

// Compute a magic constant, so that multiplying by it is the same as
// extending crc by length zeros.
uint32_t CRC32AcceleratedX86ARMCombined::ComputeZeroConstant(
    size_t length) const {
  // Lowest 2 bits are handled separately in ExtendByZeroes
  length >>= 2;

  int index = absl::countr_zero(length);
  uint32_t prev = kCRC32CPowers[index];
  length &= length - 1;

  while (length) {
    // For each bit of length, extend by 2**n zeros.
    index = absl::countr_zero(length);
    prev = multiply(prev, kCRC32CPowers[index]);
    length &= length - 1;
  }
  return prev;
}

void CRC32AcceleratedX86ARMCombined::ExtendByZeroes(uint32_t* crc,
                                                    size_t length) const {
  uint32_t val = *crc;
  // Don't bother with multiplication for small length.
  switch (length & 3) {
    case 0:
      break;
    case 1:
      val = CRC32_u8(val, 0);
      break;
    case 2:
      val = CRC32_u16(val, 0);
      break;
    case 3:
      val = CRC32_u8(val, 0);
      val = CRC32_u16(val, 0);
      break;
  }
  if (length > 3) {
    val = multiply(val, ComputeZeroConstant(length));
  }
  *crc = val;
}

// Taken from Intel paper "Fast CRC Computation for iSCSI Polynomial Using CRC32
// Instruction"
// https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/crc-iscsi-polynomial-crc32-instruction-paper.pdf
// We only need every 4th value, because we unroll loop by 4.
constexpr uint64_t kClmulConstants[] = {
    0x09e4addf8, 0x0ba4fc28e, 0x00d3b6092, 0x09e4addf8, 0x0ab7aff2a,
    0x102f9b8a2, 0x0b9e02b86, 0x00d3b6092, 0x1bf2e8b8a, 0x18266e456,
    0x0d270f1a2, 0x0ab7aff2a, 0x11eef4f8e, 0x083348832, 0x0dd7e3b0c,
    0x0b9e02b86, 0x0271d9844, 0x1b331e26a, 0x06b749fb2, 0x1bf2e8b8a,
    0x0e6fc4e6a, 0x0ce7f39f4, 0x0d7a4825c, 0x0d270f1a2, 0x026f6a60a,
    0x12ed0daac, 0x068bce87a, 0x11eef4f8e, 0x1329d9f7e, 0x0b3e32c28,
    0x0170076fa, 0x0dd7e3b0c, 0x1fae1cc66, 0x010746f3c, 0x086d8e4d2,
    0x0271d9844, 0x0b3af077a, 0x093a5f730, 0x1d88abd4a, 0x06b749fb2,
    0x0c9c8b782, 0x0cec3662e, 0x1ddffc5d4, 0x0e6fc4e6a, 0x168763fa6,
    0x0b0cd4768, 0x19b1afbc4, 0x0d7a4825c, 0x123888b7a, 0x00167d312,
    0x133d7a042, 0x026f6a60a, 0x000bcf5f6, 0x19d34af3a, 0x1af900c24,
    0x068bce87a, 0x06d390dec, 0x16cba8aca, 0x1f16a3418, 0x1329d9f7e,
    0x19fb2a8b0, 0x02178513a, 0x1a0f717c4, 0x0170076fa,
};

enum class CutoffStrategy {
  // Use 3 CRC streams to fold into 1.
  Fold3,
  // Unroll CRC instructions for 64 bytes.
  Unroll64CRC,
};

// Base class for CRC32AcceleratedX86ARMCombinedMultipleStreams containing the
// methods and data that don't need the template arguments.
class CRC32AcceleratedX86ARMCombinedMultipleStreamsBase
    : public CRC32AcceleratedX86ARMCombined {
 protected:
  // Update partialCRC with crc of 64 byte block. Calling FinalizePclmulStream
  // would produce a single crc checksum, but it is expensive. PCLMULQDQ has a
  // high latency, so we run 4 128-bit partial checksums that can be reduced to
  // a single value by FinalizePclmulStream later. Computing crc for arbitrary
  // polynomialas with PCLMULQDQ is described in Intel paper "Fast CRC
  // Computation for Generic Polynomials Using PCLMULQDQ Instruction"
  // https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
  // We are applying it to CRC32C polynomial.
  ABSL_ATTRIBUTE_ALWAYS_INLINE void Process64BytesPclmul(
      const uint8_t* p, V128* partialCRC) const {
    V128 loopMultiplicands = V128_Load(reinterpret_cast<const V128*>(k1k2));

    V128 partialCRC1 = partialCRC[0];
    V128 partialCRC2 = partialCRC[1];
    V128 partialCRC3 = partialCRC[2];
    V128 partialCRC4 = partialCRC[3];

    V128 tmp1 = V128_PMulHi(partialCRC1, loopMultiplicands);
    V128 tmp2 = V128_PMulHi(partialCRC2, loopMultiplicands);
    V128 tmp3 = V128_PMulHi(partialCRC3, loopMultiplicands);
    V128 tmp4 = V128_PMulHi(partialCRC4, loopMultiplicands);
    V128 data1 = V128_LoadU(reinterpret_cast<const V128*>(p + 16 * 0));
    V128 data2 = V128_LoadU(reinterpret_cast<const V128*>(p + 16 * 1));
    V128 data3 = V128_LoadU(reinterpret_cast<const V128*>(p + 16 * 2));
    V128 data4 = V128_LoadU(reinterpret_cast<const V128*>(p + 16 * 3));
    partialCRC1 = V128_PMulLow(partialCRC1, loopMultiplicands);
    partialCRC2 = V128_PMulLow(partialCRC2, loopMultiplicands);
    partialCRC3 = V128_PMulLow(partialCRC3, loopMultiplicands);
    partialCRC4 = V128_PMulLow(partialCRC4, loopMultiplicands);
    partialCRC1 = V128_Xor(tmp1, partialCRC1);
    partialCRC2 = V128_Xor(tmp2, partialCRC2);
    partialCRC3 = V128_Xor(tmp3, partialCRC3);
    partialCRC4 = V128_Xor(tmp4, partialCRC4);
    partialCRC1 = V128_Xor(partialCRC1, data1);
    partialCRC2 = V128_Xor(partialCRC2, data2);
    partialCRC3 = V128_Xor(partialCRC3, data3);
    partialCRC4 = V128_Xor(partialCRC4, data4);
    partialCRC[0] = partialCRC1;
    partialCRC[1] = partialCRC2;
    partialCRC[2] = partialCRC3;
    partialCRC[3] = partialCRC4;
  }

  // Reduce partialCRC produced by Process64BytesPclmul into a single value,
  // that represents crc checksum of all the processed bytes.
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t
  FinalizePclmulStream(V128* partialCRC) const {
    V128 partialCRC1 = partialCRC[0];
    V128 partialCRC2 = partialCRC[1];
    V128 partialCRC3 = partialCRC[2];
    V128 partialCRC4 = partialCRC[3];

    // Combine 4 vectors of partial crc into a single vector.
    V128 reductionMultiplicands =
        V128_Load(reinterpret_cast<const V128*>(k5k6));

    V128 low = V128_PMulLow(reductionMultiplicands, partialCRC1);
    V128 high = V128_PMulHi(reductionMultiplicands, partialCRC1);

    partialCRC1 = V128_Xor(low, high);
    partialCRC1 = V128_Xor(partialCRC1, partialCRC2);

    low = V128_PMulLow(reductionMultiplicands, partialCRC3);
    high = V128_PMulHi(reductionMultiplicands, partialCRC3);

    partialCRC3 = V128_Xor(low, high);
    partialCRC3 = V128_Xor(partialCRC3, partialCRC4);

    reductionMultiplicands = V128_Load(reinterpret_cast<const V128*>(k3k4));

    low = V128_PMulLow(reductionMultiplicands, partialCRC1);
    high = V128_PMulHi(reductionMultiplicands, partialCRC1);
    V128 fullCRC = V128_Xor(low, high);
    fullCRC = V128_Xor(fullCRC, partialCRC3);

    // Reduce fullCRC into scalar value.
    reductionMultiplicands = V128_Load(reinterpret_cast<const V128*>(k5k6));

    V128 mask = V128_Load(reinterpret_cast<const V128*>(kMask));

    V128 tmp = V128_PMul01(reductionMultiplicands, fullCRC);
    fullCRC = V128_ShiftRight<8>(fullCRC);
    fullCRC = V128_Xor(fullCRC, tmp);

    reductionMultiplicands = V128_Load(reinterpret_cast<const V128*>(k7k0));

    tmp = V128_ShiftRight<4>(fullCRC);
    fullCRC = V128_And(fullCRC, mask);
    fullCRC = V128_PMulLow(reductionMultiplicands, fullCRC);
    fullCRC = V128_Xor(tmp, fullCRC);

    reductionMultiplicands = V128_Load(reinterpret_cast<const V128*>(kPoly));

    tmp = V128_And(fullCRC, mask);
    tmp = V128_PMul01(reductionMultiplicands, tmp);
    tmp = V128_And(tmp, mask);
    tmp = V128_PMulLow(reductionMultiplicands, tmp);

    fullCRC = V128_Xor(tmp, fullCRC);

    return static_cast<uint64_t>(V128_Extract32<1>(fullCRC));
  }

  // Update crc with 64 bytes of data from p.
  ABSL_ATTRIBUTE_ALWAYS_INLINE uint64_t Process64BytesCRC(const uint8_t* p,
                                                          uint64_t crc) const {
    for (int i = 0; i < 8; i++) {
      crc =
          CRC32_u64(static_cast<uint32_t>(crc), absl::little_endian::Load64(p));
      p += 8;
    }
    return crc;
  }

  // Generated by crc32c_x86_test --crc32c_generate_constants=true
  // and verified against constants in linux kernel for S390:
  // https://github.com/torvalds/linux/blob/master/arch/s390/crypto/crc32le-vx.S
  alignas(16) static constexpr uint64_t k1k2[2] = {0x0740eef02, 0x09e4addf8};
  alignas(16) static constexpr uint64_t k3k4[2] = {0x1384aa63a, 0x0ba4fc28e};
  alignas(16) static constexpr uint64_t k5k6[2] = {0x0f20c0dfe, 0x14cd00bd6};
  alignas(16) static constexpr uint64_t k7k0[2] = {0x0dd45aab8, 0x000000000};
  alignas(16) static constexpr uint64_t kPoly[2] = {0x105ec76f0, 0x0dea713f1};
  alignas(16) static constexpr uint32_t kMask[4] = {~0u, 0u, ~0u, 0u};

  // Medium runs of bytes are broken into groups of kGroupsSmall blocks of same
  // size. Each group is CRCed in parallel then combined at the end of the
  // block.
  static constexpr size_t kGroupsSmall = 3;
  // For large runs we use up to kMaxStreams blocks computed with CRC
  // instruction, and up to kMaxStreams blocks computed with PCLMULQDQ, which
  // are combined in the end.
  static constexpr size_t kMaxStreams = 3;
};

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
alignas(16) constexpr uint64_t
    CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::k1k2[2];
alignas(16) constexpr uint64_t
    CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::k3k4[2];
alignas(16) constexpr uint64_t
    CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::k5k6[2];
alignas(16) constexpr uint64_t
    CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::k7k0[2];
alignas(16) constexpr uint64_t
    CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::kPoly[2];
alignas(16) constexpr uint32_t
    CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::kMask[4];
constexpr size_t
    CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::kGroupsSmall;
constexpr size_t CRC32AcceleratedX86ARMCombinedMultipleStreamsBase::kMaxStreams;
#endif  // ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL

template <size_t num_crc_streams, size_t num_pclmul_streams,
          CutoffStrategy strategy>
class CRC32AcceleratedX86ARMCombinedMultipleStreams
    : public CRC32AcceleratedX86ARMCombinedMultipleStreamsBase {
  ABSL_ATTRIBUTE_HOT
  void Extend(uint32_t* crc, const void* bytes, size_t length) const override {
    static_assert(num_crc_streams >= 1 && num_crc_streams <= kMaxStreams,
                  "Invalid number of crc streams");
    static_assert(num_pclmul_streams >= 0 && num_pclmul_streams <= kMaxStreams,
                  "Invalid number of pclmul streams");
    const uint8_t* p = static_cast<const uint8_t*>(bytes);
    const uint8_t* e = p + length;
    uint32_t l = *crc;
    uint64_t l64;

    // We have dedicated instruction for 1,2,4 and 8 bytes.
    if (length & 8) {
      ABSL_INTERNAL_STEP8(l, p);
      length &= ~size_t{8};
    }
    if (length & 4) {
      ABSL_INTERNAL_STEP4(l);
      length &= ~size_t{4};
    }
    if (length & 2) {
      ABSL_INTERNAL_STEP2(l);
      length &= ~size_t{2};
    }
    if (length & 1) {
      ABSL_INTERNAL_STEP1(l);
      length &= ~size_t{1};
    }
    if (length == 0) {
      *crc = l;
      return;
    }
    // length is now multiple of 16.

    // For small blocks just run simple loop, because cost of combining multiple
    // streams is significant.
    if (strategy != CutoffStrategy::Unroll64CRC) {
      if (length < kSmallCutoff) {
        while (length >= 16) {
          ABSL_INTERNAL_STEP8(l, p);
          ABSL_INTERNAL_STEP8(l, p);
          length -= 16;
        }
        *crc = l;
        return;
      }
    }

    // For medium blocks we run 3 crc streams and combine them as described in
    // Intel paper above. Running 4th stream doesn't help, because crc
    // instruction has latency 3 and throughput 1.
    if (length < kMediumCutoff) {
      l64 = l;
      if (strategy == CutoffStrategy::Fold3) {
        uint64_t l641 = 0;
        uint64_t l642 = 0;
        const size_t blockSize = 32;
        size_t bs = static_cast<size_t>(e - p) / kGroupsSmall / blockSize;
        const uint8_t* p1 = p + bs * blockSize;
        const uint8_t* p2 = p1 + bs * blockSize;

        for (size_t i = 0; i + 1 < bs; ++i) {
          ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
          ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
          ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
          ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
          PrefetchToLocalCache(
              reinterpret_cast<const char*>(p + kPrefetchHorizonMedium));
          PrefetchToLocalCache(
              reinterpret_cast<const char*>(p1 + kPrefetchHorizonMedium));
          PrefetchToLocalCache(
              reinterpret_cast<const char*>(p2 + kPrefetchHorizonMedium));
        }
        // Don't run crc on last 8 bytes.
        ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
        ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
        ABSL_INTERNAL_STEP8BY3(l64, l641, l642, p, p1, p2);
        ABSL_INTERNAL_STEP8BY2(l64, l641, p, p1);

        V128 magic = *(reinterpret_cast<const V128*>(kClmulConstants) + bs - 1);

        V128 tmp = V128_From2x64(0, l64);

        V128 res1 = V128_PMulLow(tmp, magic);

        tmp = V128_From2x64(0, l641);

        V128 res2 = V128_PMul10(tmp, magic);
        V128 x = V128_Xor(res1, res2);
        l64 = static_cast<uint64_t>(V128_Low64(x)) ^
              absl::little_endian::Load64(p2);
        l64 = CRC32_u64(static_cast<uint32_t>(l642), l64);

        p = p2 + 8;
      } else if (strategy == CutoffStrategy::Unroll64CRC) {
        while ((e - p) >= 64) {
          l64 = Process64BytesCRC(p, l64);
          p += 64;
        }
      }
    } else {
      // There is a lot of data, we can ignore combine costs and run all
      // requested streams (num_crc_streams + num_pclmul_streams),
      // using prefetch. CRC and PCLMULQDQ use different cpu execution units,
      // so on some cpus it makes sense to execute both of them for different
      // streams.

      // Point x at first 8-byte aligned byte in string.
      const uint8_t* x = RoundUp<8>(p);
      // Process bytes until p is 8-byte aligned, if that isn't past the end.
      while (p != x) {
        ABSL_INTERNAL_STEP1(l);
      }

      size_t bs = static_cast<size_t>(e - p) /
                  (num_crc_streams + num_pclmul_streams) / 64;
      const uint8_t* crc_streams[kMaxStreams];
      const uint8_t* pclmul_streams[kMaxStreams];
      // We are guaranteed to have at least one crc stream.
      crc_streams[0] = p;
      for (size_t i = 1; i < num_crc_streams; i++) {
        crc_streams[i] = crc_streams[i - 1] + bs * 64;
      }
      pclmul_streams[0] = crc_streams[num_crc_streams - 1] + bs * 64;
      for (size_t i = 1; i < num_pclmul_streams; i++) {
        pclmul_streams[i] = pclmul_streams[i - 1] + bs * 64;
      }

      // Per stream crc sums.
      uint64_t l64_crc[kMaxStreams] = {l};
      uint64_t l64_pclmul[kMaxStreams] = {0};

      // Peel first iteration, because PCLMULQDQ stream, needs setup.
      for (size_t i = 0; i < num_crc_streams; i++) {
        l64_crc[i] = Process64BytesCRC(crc_streams[i], l64_crc[i]);
        crc_streams[i] += 16 * 4;
      }

      V128 partialCRC[kMaxStreams][4];
      for (size_t i = 0; i < num_pclmul_streams; i++) {
        partialCRC[i][0] = V128_LoadU(
            reinterpret_cast<const V128*>(pclmul_streams[i] + 16 * 0));
        partialCRC[i][1] = V128_LoadU(
            reinterpret_cast<const V128*>(pclmul_streams[i] + 16 * 1));
        partialCRC[i][2] = V128_LoadU(
            reinterpret_cast<const V128*>(pclmul_streams[i] + 16 * 2));
        partialCRC[i][3] = V128_LoadU(
            reinterpret_cast<const V128*>(pclmul_streams[i] + 16 * 3));
        pclmul_streams[i] += 16 * 4;
      }

      for (size_t i = 1; i < bs; i++) {
        // Prefetch data for next iterations.
        for (size_t j = 0; j < num_crc_streams; j++) {
          PrefetchToLocalCache(
              reinterpret_cast<const char*>(crc_streams[j] + kPrefetchHorizon));
        }
        for (size_t j = 0; j < num_pclmul_streams; j++) {
          PrefetchToLocalCache(reinterpret_cast<const char*>(pclmul_streams[j] +
                                                             kPrefetchHorizon));
        }

        // We process each stream in 64 byte blocks. This can be written as
        // for (int i = 0; i < num_pclmul_streams; i++) {
        //   Process64BytesPclmul(pclmul_streams[i], partialCRC[i]);
        //   pclmul_streams[i] += 16 * 4;
        // }
        // for (int i = 0; i < num_crc_streams; i++) {
        //   l64_crc[i] = Process64BytesCRC(crc_streams[i], l64_crc[i]);
        //   crc_streams[i] += 16*4;
        // }
        // But unrolling and interleaving PCLMULQDQ and CRC blocks manually
        // gives ~2% performance boost.
        l64_crc[0] = Process64BytesCRC(crc_streams[0], l64_crc[0]);
        crc_streams[0] += 16 * 4;
        if (num_pclmul_streams > 0) {
          Process64BytesPclmul(pclmul_streams[0], partialCRC[0]);
          pclmul_streams[0] += 16 * 4;
        }
        if (num_crc_streams > 1) {
          l64_crc[1] = Process64BytesCRC(crc_streams[1], l64_crc[1]);
          crc_streams[1] += 16 * 4;
        }
        if (num_pclmul_streams > 1) {
          Process64BytesPclmul(pclmul_streams[1], partialCRC[1]);
          pclmul_streams[1] += 16 * 4;
        }
        if (num_crc_streams > 2) {
          l64_crc[2] = Process64BytesCRC(crc_streams[2], l64_crc[2]);
          crc_streams[2] += 16 * 4;
        }
        if (num_pclmul_streams > 2) {
          Process64BytesPclmul(pclmul_streams[2], partialCRC[2]);
          pclmul_streams[2] += 16 * 4;
        }
      }

      // PCLMULQDQ based streams require special final step;
      // CRC based don't.
      for (size_t i = 0; i < num_pclmul_streams; i++) {
        l64_pclmul[i] = FinalizePclmulStream(partialCRC[i]);
      }

      // Combine all streams into single result.
      uint32_t magic = ComputeZeroConstant(bs * 64);
      l64 = l64_crc[0];
      for (size_t i = 1; i < num_crc_streams; i++) {
        l64 = multiply(static_cast<uint32_t>(l64), magic);
        l64 ^= l64_crc[i];
      }
      for (size_t i = 0; i < num_pclmul_streams; i++) {
        l64 = multiply(static_cast<uint32_t>(l64), magic);
        l64 ^= l64_pclmul[i];
      }

      // Update p.
      if (num_pclmul_streams > 0) {
        p = pclmul_streams[num_pclmul_streams - 1];
      } else {
        p = crc_streams[num_crc_streams - 1];
      }
    }
    l = static_cast<uint32_t>(l64);

    while ((e - p) >= 16) {
      ABSL_INTERNAL_STEP8(l, p);
      ABSL_INTERNAL_STEP8(l, p);
    }
    // Process the last few bytes
    while (p != e) {
      ABSL_INTERNAL_STEP1(l);
    }

#undef ABSL_INTERNAL_STEP8BY3
#undef ABSL_INTERNAL_STEP8BY2
#undef ABSL_INTERNAL_STEP8
#undef ABSL_INTERNAL_STEP4
#undef ABSL_INTERNAL_STEP2
#undef ABSL_INTERNAL_STEP1

    *crc = l;
  }
};

}  // namespace

// Intel processors with SSE4.2 have an instruction for one particular
// 32-bit CRC polynomial:  crc32c
CRCImpl* TryNewCRC32AcceleratedX86ARMCombined() {
  CpuType type = GetCpuType();
  switch (type) {
    case CpuType::kIntelHaswell:
    case CpuType::kAmdRome:
    case CpuType::kAmdNaples:
    case CpuType::kAmdMilan:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 1, CutoffStrategy::Fold3>();
    // PCLMULQDQ is fast, use combined PCLMULQDQ + CRC implementation.
    case CpuType::kIntelCascadelakeXeon:
    case CpuType::kIntelSkylakeXeon:
    case CpuType::kIntelBroadwell:
    case CpuType::kIntelSkylake:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 2, CutoffStrategy::Fold3>();
    // PCLMULQDQ is slow, don't use it.
    case CpuType::kIntelIvybridge:
    case CpuType::kIntelSandybridge:
    case CpuType::kIntelWestmere:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 0, CutoffStrategy::Fold3>();
    case CpuType::kArmNeoverseN1:
    case CpuType::kArmNeoverseN2:
    case CpuType::kArmNeoverseV1:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          1, 1, CutoffStrategy::Unroll64CRC>();
    case CpuType::kAmpereSiryn:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 2, CutoffStrategy::Fold3>();
    case CpuType::kArmNeoverseV2:
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          1, 2, CutoffStrategy::Unroll64CRC>();
#if defined(__aarch64__)
    default:
      // Not all ARM processors support the needed instructions, so check here
      // before trying to use an accelerated implementation.
      if (SupportsArmCRC32PMULL()) {
        return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
            1, 1, CutoffStrategy::Unroll64CRC>();
      } else {
        return nullptr;
      }
#else
    default:
      // Something else, play it safe and assume slow PCLMULQDQ.
      return new CRC32AcceleratedX86ARMCombinedMultipleStreams<
          3, 0, CutoffStrategy::Fold3>();
#endif
  }
}

std::vector<std::unique_ptr<CRCImpl>> NewCRC32AcceleratedX86ARMCombinedAll() {
  auto ret = std::vector<std::unique_ptr<CRCImpl>>();
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    1, 0, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    1, 1, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    1, 2, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    1, 3, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    2, 0, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    2, 1, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    2, 2, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    2, 3, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    3, 0, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    3, 1, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    3, 2, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    3, 3, CutoffStrategy::Fold3>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    1, 0, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    1, 1, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    1, 2, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    1, 3, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    2, 0, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    2, 1, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    2, 2, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    2, 3, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    3, 0, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    3, 1, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    3, 2, CutoffStrategy::Unroll64CRC>>());
  ret.push_back(absl::make_unique<CRC32AcceleratedX86ARMCombinedMultipleStreams<
                    3, 3, CutoffStrategy::Unroll64CRC>>());

  return ret;
}

#else  // !ABSL_INTERNAL_CAN_USE_SIMD_CRC32C

std::vector<std::unique_ptr<CRCImpl>> NewCRC32AcceleratedX86ARMCombinedAll() {
  return std::vector<std::unique_ptr<CRCImpl>>();
}

// no hardware acceleration available
CRCImpl* TryNewCRC32AcceleratedX86ARMCombined() { return nullptr; }

#endif

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl
