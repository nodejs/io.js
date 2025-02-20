// Copyright 2022 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <array>  // IWYU pragma: keep

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/compress_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

// Regenerate tables used in the implementation, instead of testing.
#define HWY_PRINT_TABLES 0

#if !HWY_PRINT_TABLES || HWY_IDE

template <class D, class DI, typename T = TFromD<D>, typename TI = TFromD<DI>>
void CheckStored(D d, DI di, const char* op, size_t expected_pos,
                 size_t actual_pos, size_t num_to_check,
                 const AlignedFreeUniquePtr<T[]>& in,
                 const AlignedFreeUniquePtr<TI[]>& mask_lanes,
                 const AlignedFreeUniquePtr<T[]>& expected, const T* actual_u,
                 int line) {
  if (expected_pos != actual_pos) {
    hwy::Abort(__FILE__, line,
               "%s: size mismatch for %s: expected %d, actual %d\n", op,
               TypeName(T(), Lanes(d)).c_str(), static_cast<int>(expected_pos),
               static_cast<int>(actual_pos));
  }
  // Modified from AssertVecEqual - we may not be checking all lanes.
  for (size_t i = 0; i < num_to_check; ++i) {
    if (!IsEqual(expected[i], actual_u[i])) {
      const size_t N = Lanes(d);
      fprintf(stderr, "%s: mismatch at i=%d of %d, line %d:\n\n", op,
              static_cast<int>(i), static_cast<int>(num_to_check), line);
      Print(di, "mask", Load(di, mask_lanes.get()), 0, N);
      Print(d, "in", Load(d, in.get()), 0, N);
      Print(d, "expect", Load(d, expected.get()), 0, num_to_check);
      Print(d, "actual", Load(d, actual_u), 0, num_to_check);
      HWY_ASSERT(false);
    }
  }
}

struct TestCompress {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    using TU = MakeUnsigned<T>;
    const Rebind<TI, D> di;
    const size_t N = Lanes(d);
    const size_t bits_size = RoundUpTo((N + 7) / 8, 8);

    for (int frac : {0, 2, 3}) {
      // For CompressStore
      const size_t misalign = static_cast<size_t>(frac) * N / 4;

      auto in_lanes = AllocateAligned<T>(N);
      auto mask_lanes = AllocateAligned<TI>(N);
      auto garbage = AllocateAligned<TU>(N);
      auto expected = AllocateAligned<T>(N);
      auto actual_a = AllocateAligned<T>(misalign + N);
      auto bits = AllocateAligned<uint8_t>(bits_size);
      HWY_ASSERT(in_lanes && mask_lanes && garbage && expected && actual_a &&
                 bits);

      T* actual_u = actual_a.get() + misalign;
      ZeroBytes(bits.get(), bits_size);  // for MSAN

      // Each lane should have a chance of having mask=true.
      for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
        size_t expected_pos = 0;
        for (size_t i = 0; i < N; ++i) {
          in_lanes[i] = RandomFiniteValue<T>(&rng);
          mask_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
          if (mask_lanes[i] > 0) {
            expected[expected_pos++] = in_lanes[i];
          }
          garbage[i] = static_cast<TU>(Random64(&rng));
        }
        size_t num_to_check;
        if (CompressIsPartition<T>::value) {
          // For non-native Compress, also check that mask=false lanes were
          // moved to the back of the vector (highest indices).
          size_t extra = expected_pos;
          for (size_t i = 0; i < N; ++i) {
            if (mask_lanes[i] == 0) {
              expected[extra++] = in_lanes[i];
            }
          }
          HWY_ASSERT(extra == N);
          num_to_check = N;
        } else {
          // For native Compress, only the mask=true lanes are defined.
          num_to_check = expected_pos;
        }

        const auto in = Load(d, in_lanes.get());
        const auto mask =
            RebindMask(d, Gt(Load(di, mask_lanes.get()), Zero(di)));
        StoreMaskBits(d, mask, bits.get());

        // Compress
        ZeroBytes(actual_u, N * sizeof(T));
        StoreU(Compress(in, mask), d, actual_u);
        CheckStored(d, di, "Compress", expected_pos, expected_pos, num_to_check,
                    in_lanes, mask_lanes, expected, actual_u, __LINE__);

        // CompressNot
        ZeroBytes(actual_u, N * sizeof(T));
        StoreU(CompressNot(in, Not(mask)), d, actual_u);
        CheckStored(d, di, "CompressNot", expected_pos, expected_pos,
                    num_to_check, in_lanes, mask_lanes, expected, actual_u,
                    __LINE__);

        // CompressStore
        ZeroBytes(actual_u, N * sizeof(T));
        const size_t size1 = CompressStore(in, mask, d, actual_u);
        // expected_pos instead of num_to_check because this op is not
        // affected by CompressIsPartition.
        CheckStored(d, di, "CompressStore", expected_pos, size1, expected_pos,
                    in_lanes, mask_lanes, expected, actual_u, __LINE__);

        // CompressBlendedStore
        memcpy(actual_u, garbage.get(), N * sizeof(T));
        const size_t size2 = CompressBlendedStore(in, mask, d, actual_u);
        // expected_pos instead of num_to_check because this op only writes
        // the mask=true lanes.
        CheckStored(d, di, "CompressBlendedStore", expected_pos, size2,
                    expected_pos, in_lanes, mask_lanes, expected, actual_u,
                    __LINE__);
        // Subsequent lanes are untouched.
        for (size_t i = size2; i < N; ++i) {
#if HWY_COMPILER_MSVC && HWY_TARGET == HWY_AVX2
          // TODO(eustas): re-enable when compiler is fixed
#else
          HWY_ASSERT_EQ(garbage[i], reinterpret_cast<TU*>(actual_u)[i]);
#endif
        }

        // CompressBits
        ZeroBytes(actual_u, N * sizeof(T));
        StoreU(CompressBits(in, bits.get()), d, actual_u);
        CheckStored(d, di, "CompressBits", expected_pos, expected_pos,
                    num_to_check, in_lanes, mask_lanes, expected, actual_u,
                    __LINE__);

        // CompressBitsStore
        ZeroBytes(actual_u, N * sizeof(T));
        const size_t size3 = CompressBitsStore(in, bits.get(), d, actual_u);
        // expected_pos instead of num_to_check because this op is not
        // affected by CompressIsPartition.
        CheckStored(d, di, "CompressBitsStore", expected_pos, size3,
                    expected_pos, in_lanes, mask_lanes, expected, actual_u,
                    __LINE__);
      }  // rep
    }    // frac
  }      // operator()
};

HWY_NOINLINE void TestAllCompress() {
  ForAllTypes(ForPartialVectors<TestCompress>());
}

struct TestCompressBlocks {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET == HWY_SCALAR
    (void)d;
#else
    static_assert(sizeof(T) == 8 && !IsSigned<T>(), "Should be u64");
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(d);

    auto in_lanes = AllocateAligned<T>(N);
    auto mask_lanes = AllocateAligned<TI>(N);
    auto expected = AllocateAligned<T>(N);
    auto actual = AllocateAligned<T>(N);
    HWY_ASSERT(in_lanes && mask_lanes && expected && actual);

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      size_t expected_pos = 0;
      for (size_t i = 0; i < N; i += 2) {
        in_lanes[i] = RandomFiniteValue<T>(&rng);
        in_lanes[i + 1] = RandomFiniteValue<T>(&rng);
        mask_lanes[i + 1] = mask_lanes[i] = TI{(Random32(&rng) & 8) ? 1 : 0};
        if (mask_lanes[i] > 0) {
          expected[expected_pos++] = in_lanes[i];
          expected[expected_pos++] = in_lanes[i + 1];
        }
      }
      size_t num_to_check;
      if (CompressIsPartition<T>::value) {
        // For non-native Compress, also check that mask=false lanes were
        // moved to the back of the vector (highest indices).
        size_t extra = expected_pos;
        for (size_t i = 0; i < N; ++i) {
          if (mask_lanes[i] == 0) {
            expected[extra++] = in_lanes[i];
          }
        }
        HWY_ASSERT(extra == N);
        num_to_check = N;
      } else {
        // For native Compress, only the mask=true lanes are defined.
        num_to_check = expected_pos;
      }

      const auto in = Load(d, in_lanes.get());
      const auto mask = RebindMask(d, Gt(Load(di, mask_lanes.get()), Zero(di)));

      // CompressBlocksNot
      ZeroBytes(actual.get(), N * sizeof(T));
      StoreU(CompressBlocksNot(in, Not(mask)), d, actual.get());
      CheckStored(d, di, "CompressBlocksNot", expected_pos, expected_pos,
                  num_to_check, in_lanes, mask_lanes, expected, actual.get(),
                  __LINE__);
    }  // rep
#endif  // HWY_TARGET == HWY_SCALAR
  }     // operator()
};

HWY_NOINLINE void TestAllCompressBlocks() {
  ForGE128Vectors<TestCompressBlocks>()(uint64_t());
}

#endif  // !HWY_PRINT_TABLES

#if HWY_PRINT_TABLES || HWY_IDE
void PrintCompress8x8Tables() {
  printf("======================================= 8x8\n");
  constexpr size_t N = 8;
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint8_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    for (size_t i = 0; i < N; ++i) {
      printf("%d,", indices[i]);
    }
    printf(code & 1 ? "//\n" : "/**/");
  }
  printf("\n");
}

void PrintCompress16x8Tables() {
  printf("======================================= 16x8\n");
  constexpr size_t N = 8;  // 128-bit SIMD
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint8_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Doubled (for converting lane to byte indices)
    for (size_t i = 0; i < N; ++i) {
      printf("%d,", 2 * indices[i]);
    }
    printf(code & 1 ? "//\n" : "/**/");
  }
  printf("\n");
}

void PrintCompressNot16x8Tables() {
  printf("======================================= Not 16x8\n");
  constexpr size_t N = 8;  // 128-bit SIMD
  for (uint64_t not_code = 0; not_code < (1ull << N); ++not_code) {
    const uint64_t code = ~not_code;
    std::array<uint8_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Doubled (for converting lane to byte indices)
    for (size_t i = 0; i < N; ++i) {
      printf("%d,", 2 * indices[i]);
    }
    printf(not_code & 1 ? "//\n" : "/**/");
  }
  printf("\n");
}

// Compressed to nibbles, unpacked via variable right shift. Also includes
// FirstN bits in the nibble MSB.
void PrintCompress32x8Tables() {
  printf("======================================= 32/64x8\n");
  constexpr size_t N = 8;  // AVX2 or 64-bit AVX3
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    const size_t count = PopCount(code);
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Convert to nibbles
    uint64_t packed = 0;
    for (size_t i = 0; i < N; ++i) {
      HWY_ASSERT(indices[i] < N);
      if (i < count) {
        indices[i] |= N;
        HWY_ASSERT(indices[i] < 0x10);
      }
      packed += indices[i] << (i * 4);
    }

    HWY_ASSERT(packed < (1ull << (N * 4)));
    printf("0x%08x,", static_cast<uint32_t>(packed));
  }
  printf("\n");
}

void PrintCompressNot32x8Tables() {
  printf("======================================= Not 32/64x8\n");
  constexpr size_t N = 8;  // AVX2 or 64-bit AVX3
  for (uint64_t not_code = 0; not_code < (1ull << N); ++not_code) {
    const uint64_t code = ~not_code;
    const size_t count = PopCount(code);
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Convert to nibbles
    uint64_t packed = 0;
    for (size_t i = 0; i < N; ++i) {
      HWY_ASSERT(indices[i] < N);
      if (i < count) {
        indices[i] |= N;
        HWY_ASSERT(indices[i] < 0x10);
      }
      packed += indices[i] << (i * 4);
    }

    HWY_ASSERT(packed < (1ull << (N * 4)));
    printf("0x%08x,", static_cast<uint32_t>(packed));
  }
  printf("\n");
}

// Compressed to nibbles (for AVX3 64x4)
void PrintCompress64x4NibbleTables() {
  printf("======================================= 64x4Nibble\n");
  constexpr size_t N = 4;  // AVX2
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Convert to nibbles
    uint64_t packed = 0;
    for (size_t i = 0; i < N; ++i) {
      HWY_ASSERT(indices[i] < N);
      packed += indices[i] << (i * 4);
    }

    HWY_ASSERT(packed < (1ull << (N * 4)));
    printf("0x%08x,", static_cast<uint32_t>(packed));
  }
  printf("\n");
}

void PrintCompressNot64x4NibbleTables() {
  printf("======================================= Not 64x4Nibble\n");
  constexpr size_t N = 4;  // AVX2
  for (uint64_t not_code = 0; not_code < (1ull << N); ++not_code) {
    const uint64_t code = ~not_code;
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Convert to nibbles
    uint64_t packed = 0;
    for (size_t i = 0; i < N; ++i) {
      HWY_ASSERT(indices[i] < N);
      packed += indices[i] << (i * 4);
    }

    HWY_ASSERT(packed < (1ull << (N * 4)));
    printf("0x%08x,", static_cast<uint32_t>(packed));
  }
  printf("\n");
}

void PrintCompressNot64x2NibbleTables() {
  printf("======================================= Not 64x2Nibble\n");
  constexpr size_t N = 2;  // 128-bit
  for (uint64_t not_code = 0; not_code < (1ull << N); ++not_code) {
    const uint64_t code = ~not_code;
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Convert to nibbles
    uint64_t packed = 0;
    for (size_t i = 0; i < N; ++i) {
      HWY_ASSERT(indices[i] < N);
      packed += indices[i] << (i * 4);
    }

    HWY_ASSERT(packed < (1ull << (N * 4)));
    printf("0x%08x,", static_cast<uint32_t>(packed));
  }
  printf("\n");
}

void PrintCompress64x4Tables() {
  printf("======================================= 64x4 uncompressed\n");
  constexpr size_t N = 4;  // SVE_256
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<size_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Store uncompressed indices because SVE TBL returns 0 if an index is out
    // of bounds. On AVX3 we simply variable-shift because permute indices are
    // interpreted modulo N. Compression is not worth the extra shift+AND
    // because the table is anyway only 512 bytes.
    for (size_t i = 0; i < N; ++i) {
      printf("%d,", static_cast<int>(indices[i]));
    }
  }
  printf("\n");
}

void PrintCompressNot64x4Tables() {
  printf("======================================= Not 64x4 uncompressed\n");
  constexpr size_t N = 4;  // SVE_256
  for (uint64_t not_code = 0; not_code < (1ull << N); ++not_code) {
    const uint64_t code = ~not_code;
    std::array<size_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Store uncompressed indices because SVE TBL returns 0 if an index is out
    // of bounds. On AVX3 we simply variable-shift because permute indices are
    // interpreted modulo N. Compression is not worth the extra shift+AND
    // because the table is anyway only 512 bytes.
    for (size_t i = 0; i < N; ++i) {
      printf("%d,", static_cast<int>(indices[i]));
    }
  }
  printf("\n");
}

// Same as above, but prints pairs of u32 indices (for AVX2). Also includes
// FirstN bits in the nibble MSB.
void PrintCompress64x4PairTables() {
  printf("======================================= 64x4 u32 index\n");
  constexpr size_t N = 4;  // AVX2
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    const size_t count = PopCount(code);
    std::array<size_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Store uncompressed indices because SVE TBL returns 0 if an index is out
    // of bounds. On AVX3 we simply variable-shift because permute indices are
    // interpreted modulo N. Compression is not worth the extra shift+AND
    // because the table is anyway only 512 bytes.
    for (size_t i = 0; i < N; ++i) {
      const int first_n_bit = i < count ? 8 : 0;
      const int low = static_cast<int>(2 * indices[i]) + first_n_bit;
      HWY_ASSERT(low < 0x10);
      printf("%d, %d, ", low, low + 1);
    }
  }
  printf("\n");
}

void PrintCompressNot64x4PairTables() {
  printf("======================================= Not 64x4 u32 index\n");
  constexpr size_t N = 4;  // AVX2
  for (uint64_t not_code = 0; not_code < (1ull << N); ++not_code) {
    const uint64_t code = ~not_code;
    const size_t count = PopCount(code);
    std::array<size_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    // Store uncompressed indices because SVE TBL returns 0 if an index is out
    // of bounds. On AVX3 we simply variable-shift because permute indices are
    // interpreted modulo N. Compression is not worth the extra shift+AND
    // because the table is anyway only 512 bytes.
    for (size_t i = 0; i < N; ++i) {
      const int first_n_bit = i < count ? 8 : 0;
      const int low = static_cast<int>(2 * indices[i]) + first_n_bit;
      HWY_ASSERT(low < 0x10);
      printf("%d, %d, ", low, low + 1);
    }
  }
  printf("\n");
}

// 4-tuple of byte indices
void PrintCompress32x4Tables() {
  printf("======================================= 32x4\n");
  using T = uint32_t;
  constexpr size_t N = 4;  // SSE4
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    for (size_t i = 0; i < N; ++i) {
      for (size_t idx_byte = 0; idx_byte < sizeof(T); ++idx_byte) {
        printf("%d,", static_cast<int>(sizeof(T) * indices[i] + idx_byte));
      }
    }
  }
  printf("\n");
}

void PrintCompressNot32x4Tables() {
  printf("======================================= Not 32x4\n");
  using T = uint32_t;
  constexpr size_t N = 4;  // SSE4
  for (uint64_t not_code = 0; not_code < (1ull << N); ++not_code) {
    const uint64_t code = ~not_code;
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    for (size_t i = 0; i < N; ++i) {
      for (size_t idx_byte = 0; idx_byte < sizeof(T); ++idx_byte) {
        printf("%d,", static_cast<int>(sizeof(T) * indices[i] + idx_byte));
      }
    }
  }
  printf("\n");
}

// 8-tuple of byte indices
void PrintCompress64x2Tables() {
  printf("======================================= 64x2\n");
  using T = uint64_t;
  constexpr size_t N = 2;  // SSE4
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    for (size_t i = 0; i < N; ++i) {
      for (size_t idx_byte = 0; idx_byte < sizeof(T); ++idx_byte) {
        printf("%d,", static_cast<int>(sizeof(T) * indices[i] + idx_byte));
      }
    }
  }
  printf("\n");
}

void PrintCompressNot64x2Tables() {
  printf("======================================= Not 64x2\n");
  using T = uint64_t;
  constexpr size_t N = 2;  // SSE4
  for (uint64_t not_code = 0; not_code < (1ull << N); ++not_code) {
    const uint64_t code = ~not_code;
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    // All lanes where mask = true
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[pos++] = i;
      }
    }
    // All lanes where mask = false
    for (size_t i = 0; i < N; ++i) {
      if (!(code & (1ull << i))) {
        indices[pos++] = i;
      }
    }
    HWY_ASSERT(pos == N);

    for (size_t i = 0; i < N; ++i) {
      for (size_t idx_byte = 0; idx_byte < sizeof(T); ++idx_byte) {
        printf("%d,", static_cast<int>(sizeof(T) * indices[i] + idx_byte));
      }
    }
  }
  printf("\n");
}

HWY_NOINLINE void PrintTables() {
  // Only print once.
#if HWY_TARGET == HWY_STATIC_TARGET
  PrintCompress32x8Tables();
  PrintCompressNot32x8Tables();
  PrintCompress64x4NibbleTables();
  PrintCompressNot64x4NibbleTables();
  PrintCompressNot64x2NibbleTables();
  PrintCompress64x4Tables();
  PrintCompressNot64x4Tables();
  PrintCompress32x4Tables();
  PrintCompressNot32x4Tables();
  PrintCompress64x2Tables();
  PrintCompressNot64x2Tables();
  PrintCompress64x4PairTables();
  PrintCompressNot64x4PairTables();
  PrintCompress16x8Tables();
  PrintCompress8x8Tables();
  PrintCompressNot16x8Tables();
#endif
}

#endif  // HWY_PRINT_TABLES

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyCompressTest);
#if HWY_PRINT_TABLES
// Only print instead of running tests; this will be visible in the log.
HWY_EXPORT_AND_TEST_P(HwyCompressTest, PrintTables);
#else
HWY_EXPORT_AND_TEST_P(HwyCompressTest, TestAllCompress);
HWY_EXPORT_AND_TEST_P(HwyCompressTest, TestAllCompressBlocks);
#endif
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
