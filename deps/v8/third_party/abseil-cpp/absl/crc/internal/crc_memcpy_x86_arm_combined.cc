// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Simultaneous memcopy and CRC-32C for x86-64 and ARM 64. Uses integer
// registers because XMM registers do not support the CRC instruction (yet).
// While copying, compute the running CRC of the data being copied.
//
// It is assumed that any CPU running this code has SSE4.2 instructions
// available (for CRC32C).  This file will do nothing if that is not true.
//
// The CRC instruction has a 3-byte latency, and we are stressing the ALU ports
// here (unlike a traditional memcopy, which has almost no ALU use), so we will
// need to copy in such a way that the CRC unit is used efficiently. We have two
// regimes in this code:
//  1. For operations of size < kCrcSmallSize, do the CRC then the memcpy
//  2. For operations of size > kCrcSmallSize:
//      a) compute an initial CRC + copy on a small amount of data to align the
//         destination pointer on a 16-byte boundary.
//      b) Split the data into 3 main regions and a tail (smaller than 48 bytes)
//      c) Do the copy and CRC of the 3 main regions, interleaving (start with
//         full cache line copies for each region, then move to single 16 byte
//         pieces per region).
//      d) Combine the CRCs with CRC32C::Concat.
//      e) Copy the tail and extend the CRC with the CRC of the tail.
// This method is not ideal for op sizes between ~1k and ~8k because CRC::Concat
// takes a significant amount of time.  A medium-sized approach could be added
// using 3 CRCs over fixed-size blocks where the zero-extensions required for
// CRC32C::Concat can be precomputed.

#ifdef __SSE4_2__
#include <immintrin.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/base/prefetch.h"
#include "absl/crc/crc32c.h"
#include "absl/crc/internal/cpu_detect.h"
#include "absl/crc/internal/crc32_x86_arm_combined_simd.h"
#include "absl/crc/internal/crc_memcpy.h"
#include "absl/strings/string_view.h"

#if defined(ABSL_INTERNAL_HAVE_X86_64_ACCELERATED_CRC_MEMCPY_ENGINE) || \
    defined(ABSL_INTERNAL_HAVE_ARM_ACCELERATED_CRC_MEMCPY_ENGINE)

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

namespace {

inline crc32c_t ShortCrcCopy(char* dst, const char* src, std::size_t length,
                             crc32c_t crc) {
  // Small copy: just go 1 byte at a time: being nice to the branch predictor
  // is more important here than anything else
  uint32_t crc_uint32 = static_cast<uint32_t>(crc);
  for (std::size_t i = 0; i < length; i++) {
    uint8_t data = *reinterpret_cast<const uint8_t*>(src);
    crc_uint32 = CRC32_u8(crc_uint32, data);
    *reinterpret_cast<uint8_t*>(dst) = data;
    ++src;
    ++dst;
  }
  return crc32c_t{crc_uint32};
}

constexpr size_t kIntLoadsPerVec = sizeof(V128) / sizeof(uint64_t);

// Common function for copying the tails of multiple large regions.
// Disable ubsan for benign unaligned access. See b/254108538.
template <size_t vec_regions, size_t int_regions>
ABSL_ATTRIBUTE_NO_SANITIZE_UNDEFINED inline void LargeTailCopy(
    crc32c_t* crcs, char** dst, const char** src, size_t region_size,
    size_t copy_rounds) {
  std::array<V128, vec_regions> data;
  std::array<uint64_t, kIntLoadsPerVec * int_regions> int_data;

  while (copy_rounds > 0) {
    for (size_t i = 0; i < vec_regions; i++) {
      size_t region = i;

      auto* vsrc = reinterpret_cast<const V128*>(*src + region_size * region);
      auto* vdst = reinterpret_cast<V128*>(*dst + region_size * region);

      // Load the blocks, unaligned
      data[i] = V128_LoadU(vsrc);

      // Store the blocks, aligned
      V128_Store(vdst, data[i]);

      // Compute the running CRC
      crcs[region] = crc32c_t{static_cast<uint32_t>(
          CRC32_u64(static_cast<uint32_t>(crcs[region]),
                    static_cast<uint64_t>(V128_Extract64<0>(data[i]))))};
      crcs[region] = crc32c_t{static_cast<uint32_t>(
          CRC32_u64(static_cast<uint32_t>(crcs[region]),
                    static_cast<uint64_t>(V128_Extract64<1>(data[i]))))};
    }

    for (size_t i = 0; i < int_regions; i++) {
      size_t region = vec_regions + i;

      auto* usrc =
          reinterpret_cast<const uint64_t*>(*src + region_size * region);
      auto* udst = reinterpret_cast<uint64_t*>(*dst + region_size * region);

      for (size_t j = 0; j < kIntLoadsPerVec; j++) {
        size_t data_index = i * kIntLoadsPerVec + j;

        int_data[data_index] = *(usrc + j);
        crcs[region] = crc32c_t{CRC32_u64(static_cast<uint32_t>(crcs[region]),
                                          int_data[data_index])};

        *(udst + j) = int_data[data_index];
      }
    }

    // Increment pointers
    *src += sizeof(V128);
    *dst += sizeof(V128);
    --copy_rounds;
  }
}

}  // namespace

template <size_t vec_regions, size_t int_regions>
class AcceleratedCrcMemcpyEngine : public CrcMemcpyEngine {
 public:
  AcceleratedCrcMemcpyEngine() = default;
  AcceleratedCrcMemcpyEngine(const AcceleratedCrcMemcpyEngine&) = delete;
  AcceleratedCrcMemcpyEngine operator=(const AcceleratedCrcMemcpyEngine&) =
      delete;

  crc32c_t Compute(void* __restrict dst, const void* __restrict src,
                   std::size_t length, crc32c_t initial_crc) const override;
};

// Disable ubsan for benign unaligned access. See b/254108538.
template <size_t vec_regions, size_t int_regions>
ABSL_ATTRIBUTE_NO_SANITIZE_UNDEFINED crc32c_t
AcceleratedCrcMemcpyEngine<vec_regions, int_regions>::Compute(
    void* __restrict dst, const void* __restrict src, std::size_t length,
    crc32c_t initial_crc) const {
  constexpr std::size_t kRegions = vec_regions + int_regions;
  static_assert(kRegions > 0, "Must specify at least one region.");
  constexpr uint32_t kCrcDataXor = uint32_t{0xffffffff};
  constexpr std::size_t kBlockSize = sizeof(V128);
  constexpr std::size_t kCopyRoundSize = kRegions * kBlockSize;

  // Number of blocks per cacheline.
  constexpr std::size_t kBlocksPerCacheLine = ABSL_CACHELINE_SIZE / kBlockSize;

  char* dst_bytes = static_cast<char*>(dst);
  const char* src_bytes = static_cast<const char*>(src);

  // Make sure that one prefetch per big block is enough to cover the whole
  // dataset, and we don't prefetch too much.
  static_assert(ABSL_CACHELINE_SIZE % kBlockSize == 0,
                "Cache lines are not divided evenly into blocks, may have "
                "unintended behavior!");

  // Experimentally-determined boundary between a small and large copy.
  // Below this number, spin-up and concatenation of CRCs takes enough time that
  // it kills the throughput gains of using 3 regions and wide vectors.
  constexpr size_t kCrcSmallSize = 256;

  // Experimentally-determined prefetch distance.  Main loop copies will
  // prefeth data 2 cache lines ahead.
  constexpr std::size_t kPrefetchAhead = 2 * ABSL_CACHELINE_SIZE;

  // Small-size CRC-memcpy : just do CRC + memcpy
  if (length < kCrcSmallSize) {
    crc32c_t crc =
        ExtendCrc32c(initial_crc, absl::string_view(src_bytes, length));
    memcpy(dst, src, length);
    return crc;
  }

  // Start work on the CRC: undo the XOR from the previous calculation or set up
  // the initial value of the CRC.
  initial_crc = crc32c_t{static_cast<uint32_t>(initial_crc) ^ kCrcDataXor};

  // Do an initial alignment copy, so we can use aligned store instructions to
  // the destination pointer.  We align the destination pointer because the
  // penalty for an unaligned load is small compared to the penalty of an
  // unaligned store on modern CPUs.
  std::size_t bytes_from_last_aligned =
      reinterpret_cast<uintptr_t>(dst) & (kBlockSize - 1);
  if (bytes_from_last_aligned != 0) {
    std::size_t bytes_for_alignment = kBlockSize - bytes_from_last_aligned;

    // Do the short-sized copy and CRC.
    initial_crc =
        ShortCrcCopy(dst_bytes, src_bytes, bytes_for_alignment, initial_crc);
    src_bytes += bytes_for_alignment;
    dst_bytes += bytes_for_alignment;
    length -= bytes_for_alignment;
  }

  // We are going to do the copy and CRC in kRegions regions to make sure that
  // we can saturate the CRC unit.  The CRCs will be combined at the end of the
  // run.  Copying will use the SSE registers, and we will extract words from
  // the SSE registers to add to the CRC.  Initially, we run the loop one full
  // cache line per region at a time, in order to insert prefetches.

  // Initialize CRCs for kRegions regions.
  crc32c_t crcs[kRegions];
  crcs[0] = initial_crc;
  for (size_t i = 1; i < kRegions; i++) {
    crcs[i] = crc32c_t{kCrcDataXor};
  }

  // Find the number of rounds to copy and the region size.  Also compute the
  // tail size here.
  size_t copy_rounds = length / kCopyRoundSize;

  // Find the size of each region and the size of the tail.
  const std::size_t region_size = copy_rounds * kBlockSize;
  const std::size_t tail_size = length - (kRegions * region_size);

  // Holding registers for data in each region.
  std::array<V128, vec_regions> vec_data;
  std::array<uint64_t, int_regions * kIntLoadsPerVec> int_data;

  // Main loop.
  while (copy_rounds > kBlocksPerCacheLine) {
    // Prefetch kPrefetchAhead bytes ahead of each pointer.
    for (size_t i = 0; i < kRegions; i++) {
      absl::PrefetchToLocalCache(src_bytes + kPrefetchAhead + region_size * i);
#ifdef ABSL_INTERNAL_HAVE_X86_64_ACCELERATED_CRC_MEMCPY_ENGINE
      // TODO(b/297082454): investigate dropping prefetch on x86.
      absl::PrefetchToLocalCache(dst_bytes + kPrefetchAhead + region_size * i);
#endif
    }

    // Load and store data, computing CRC on the way.
    for (size_t i = 0; i < kBlocksPerCacheLine; i++) {
      // Copy and CRC the data for the CRC regions.
      for (size_t j = 0; j < vec_regions; j++) {
        // Cycle which regions get vector load/store and integer load/store, to
        // engage prefetching logic around vector load/stores and save issue
        // slots by using the integer registers.
        size_t region = (j + i) % kRegions;

        auto* vsrc =
            reinterpret_cast<const V128*>(src_bytes + region_size * region);
        auto* vdst = reinterpret_cast<V128*>(dst_bytes + region_size * region);

        // Load and CRC data.
        vec_data[j] = V128_LoadU(vsrc + i);
        crcs[region] = crc32c_t{static_cast<uint32_t>(
            CRC32_u64(static_cast<uint32_t>(crcs[region]),
                      static_cast<uint64_t>(V128_Extract64<0>(vec_data[j]))))};
        crcs[region] = crc32c_t{static_cast<uint32_t>(
            CRC32_u64(static_cast<uint32_t>(crcs[region]),
                      static_cast<uint64_t>(V128_Extract64<1>(vec_data[j]))))};

        // Store the data.
        V128_Store(vdst + i, vec_data[j]);
      }

      // Preload the partial CRCs for the CLMUL subregions.
      for (size_t j = 0; j < int_regions; j++) {
        // Cycle which regions get vector load/store and integer load/store, to
        // engage prefetching logic around vector load/stores and save issue
        // slots by using the integer registers.
        size_t region = (j + vec_regions + i) % kRegions;

        auto* usrc =
            reinterpret_cast<const uint64_t*>(src_bytes + region_size * region);
        auto* udst =
            reinterpret_cast<uint64_t*>(dst_bytes + region_size * region);

        for (size_t k = 0; k < kIntLoadsPerVec; k++) {
          size_t data_index = j * kIntLoadsPerVec + k;

          // Load and CRC the data.
          int_data[data_index] = *(usrc + i * kIntLoadsPerVec + k);
          crcs[region] = crc32c_t{CRC32_u64(static_cast<uint32_t>(crcs[region]),
                                            int_data[data_index])};

          // Store the data.
          *(udst + i * kIntLoadsPerVec + k) = int_data[data_index];
        }
      }
    }

    // Increment pointers
    src_bytes += kBlockSize * kBlocksPerCacheLine;
    dst_bytes += kBlockSize * kBlocksPerCacheLine;
    copy_rounds -= kBlocksPerCacheLine;
  }

  // Copy and CRC the tails of each region.
  LargeTailCopy<vec_regions, int_regions>(crcs, &dst_bytes, &src_bytes,
                                          region_size, copy_rounds);

  // Move the source and destination pointers to the end of the region
  src_bytes += region_size * (kRegions - 1);
  dst_bytes += region_size * (kRegions - 1);

  // Copy and CRC the tail through the XMM registers.
  std::size_t tail_blocks = tail_size / kBlockSize;
  LargeTailCopy<0, 1>(&crcs[kRegions - 1], &dst_bytes, &src_bytes, 0,
                      tail_blocks);

  // Final tail copy for under 16 bytes.
  crcs[kRegions - 1] =
      ShortCrcCopy(dst_bytes, src_bytes, tail_size - tail_blocks * kBlockSize,
                   crcs[kRegions - 1]);

  if (kRegions == 1) {
    // If there is only one region, finalize and return its CRC.
    return crc32c_t{static_cast<uint32_t>(crcs[0]) ^ kCrcDataXor};
  }

  // Finalize the first CRCs: XOR the internal CRCs by the XOR mask to undo the
  // XOR done before doing block copy + CRCs.
  for (size_t i = 0; i + 1 < kRegions; i++) {
    crcs[i] = crc32c_t{static_cast<uint32_t>(crcs[i]) ^ kCrcDataXor};
  }

  // Build a CRC of the first kRegions - 1 regions.
  crc32c_t full_crc = crcs[0];
  for (size_t i = 1; i + 1 < kRegions; i++) {
    full_crc = ConcatCrc32c(full_crc, crcs[i], region_size);
  }

  // Finalize and concatenate the final CRC, then return.
  crcs[kRegions - 1] =
      crc32c_t{static_cast<uint32_t>(crcs[kRegions - 1]) ^ kCrcDataXor};
  return ConcatCrc32c(full_crc, crcs[kRegions - 1], region_size + tail_size);
}

CrcMemcpy::ArchSpecificEngines CrcMemcpy::GetArchSpecificEngines() {
#ifdef UNDEFINED_BEHAVIOR_SANITIZER
  // UBSAN does not play nicely with unaligned loads (which we use a lot).
  // Get the underlying architecture.
  CpuType cpu_type = GetCpuType();
  switch (cpu_type) {
    case CpuType::kAmdRome:
    case CpuType::kAmdNaples:
    case CpuType::kAmdMilan:
    case CpuType::kAmdGenoa:
    case CpuType::kAmdRyzenV3000:
    case CpuType::kIntelCascadelakeXeon:
    case CpuType::kIntelSkylakeXeon:
    case CpuType::kIntelSkylake:
    case CpuType::kIntelBroadwell:
    case CpuType::kIntelHaswell:
    case CpuType::kIntelIvybridge:
      return {
          /*.temporal=*/new FallbackCrcMemcpyEngine(),
          /*.non_temporal=*/new CrcNonTemporalMemcpyAVXEngine(),
      };
    // INTEL_SANDYBRIDGE performs better with SSE than AVX.
    case CpuType::kIntelSandybridge:
      return {
          /*.temporal=*/new FallbackCrcMemcpyEngine(),
          /*.non_temporal=*/new CrcNonTemporalMemcpyEngine(),
      };
    default:
      return {/*.temporal=*/new FallbackCrcMemcpyEngine(),
              /*.non_temporal=*/new FallbackCrcMemcpyEngine()};
  }
#else
  // Get the underlying architecture.
  CpuType cpu_type = GetCpuType();
  switch (cpu_type) {
    // On Zen 2, PEXTRQ uses 2 micro-ops, including one on the vector store port
    // which data movement from the vector registers to the integer registers
    // (where CRC32C happens) to crowd the same units as vector stores.  As a
    // result, using that path exclusively causes bottlenecking on this port.
    // We can avoid this bottleneck by using the integer side of the CPU for
    // most operations rather than the vector side.  We keep a vector region to
    // engage some of the prefetching logic in the cache hierarchy which seems
    // to give vector instructions special treatment.  These prefetch units see
    // strided access to each region, and do the right thing.
    case CpuType::kAmdRome:
    case CpuType::kAmdNaples:
    case CpuType::kAmdMilan:
    case CpuType::kAmdGenoa:
    case CpuType::kAmdRyzenV3000:
      return {
          /*.temporal=*/new AcceleratedCrcMemcpyEngine<1, 2>(),
          /*.non_temporal=*/new CrcNonTemporalMemcpyAVXEngine(),
      };
    // PCLMULQDQ is slow and we don't have wide enough issue width to take
    // advantage of it.  For an unknown architecture, don't risk using CLMULs.
    case CpuType::kIntelCascadelakeXeon:
    case CpuType::kIntelSkylakeXeon:
    case CpuType::kIntelSkylake:
    case CpuType::kIntelBroadwell:
    case CpuType::kIntelHaswell:
    case CpuType::kIntelIvybridge:
      return {
          /*.temporal=*/new AcceleratedCrcMemcpyEngine<3, 0>(),
          /*.non_temporal=*/new CrcNonTemporalMemcpyAVXEngine(),
      };
    // INTEL_SANDYBRIDGE performs better with SSE than AVX.
    case CpuType::kIntelSandybridge:
      return {
          /*.temporal=*/new AcceleratedCrcMemcpyEngine<3, 0>(),
          /*.non_temporal=*/new CrcNonTemporalMemcpyEngine(),
      };
    default:
      return {/*.temporal=*/new FallbackCrcMemcpyEngine(),
              /*.non_temporal=*/new FallbackCrcMemcpyEngine()};
  }
#endif  // UNDEFINED_BEHAVIOR_SANITIZER
}

// For testing, allow the user to specify which engine they want.
std::unique_ptr<CrcMemcpyEngine> CrcMemcpy::GetTestEngine(int vector,
                                                          int integer) {
  if (vector == 3 && integer == 0) {
    return std::make_unique<AcceleratedCrcMemcpyEngine<3, 0>>();
  } else if (vector == 1 && integer == 2) {
    return std::make_unique<AcceleratedCrcMemcpyEngine<1, 2>>();
  } else if (vector == 1 && integer == 0) {
    return std::make_unique<AcceleratedCrcMemcpyEngine<1, 0>>();
  }
  return nullptr;
}

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_X86_64_ACCELERATED_CRC_MEMCPY_ENGINE ||
        // ABSL_INTERNAL_HAVE_ARM_ACCELERATED_CRC_MEMCPY_ENGINE
