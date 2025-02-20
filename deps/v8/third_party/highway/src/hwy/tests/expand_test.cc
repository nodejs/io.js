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
#define HWY_TARGET_INCLUDE "tests/expand_test.cc"
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
void CheckExpanded(D d, DI di, const char* op,
                   const AlignedFreeUniquePtr<T[]>& in,
                   const AlignedFreeUniquePtr<TI[]>& mask_lanes,
                   const AlignedFreeUniquePtr<T[]>& expected, const T* actual_u,
                   int line) {
  const size_t N = Lanes(d);
  // Modified from AssertVecEqual to also print mask etc.
  for (size_t i = 0; i < N; ++i) {
    if (!IsEqual(expected[i], actual_u[i])) {
      fprintf(stderr, "%s: mismatch at i=%d of %d, line %d:\n\n", op,
              static_cast<int>(i), static_cast<int>(N), line);
      Print(di, "mask", Load(di, mask_lanes.get()), 0, N);
      Print(d, "in", Load(d, in.get()), 0, N);
      Print(d, "expect", Load(d, expected.get()), 0, N);
      Print(d, "actual", Load(d, actual_u), 0, N);
      HWY_ASSERT(false);
    }
  }
}

struct TestExpand {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // Used for mask > 0 comparison.
    const Rebind<TI, D> di;
    const size_t N = Lanes(d);
    const size_t bits_size = RoundUpTo((N + 7) / 8, 8);

    for (int frac : {0, 2, 3}) {
      // For LoadExpand
      const size_t misalign = static_cast<size_t>(frac) * N / 4;

      auto in_lanes = AllocateAligned<T>(N);
      auto mask_lanes = AllocateAligned<TI>(N);
      auto expected = AllocateAligned<T>(N);
      auto actual_a = AllocateAligned<T>(misalign + N);
      auto bits = AllocateAligned<uint8_t>(bits_size);
      HWY_ASSERT(in_lanes && mask_lanes && expected && actual_a && bits);

      T* actual_u = actual_a.get() + misalign;
      ZeroBytes(bits.get(), bits_size);  // Prevents MSAN error.

      // Random input vector, used in all iterations.
      for (size_t i = 0; i < N; ++i) {
        in_lanes[i] = RandomFiniteValue<T>(&rng);
      }

      // Each lane should have a chance of having mask=true.
      for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
        size_t in_pos = 0;
        for (size_t i = 0; i < N; ++i) {
          mask_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
          if (mask_lanes[i] > 0) {
            expected[i] = in_lanes[in_pos++];
          } else {
            expected[i] = ConvertScalarTo<T>(0);
          }
        }

        const auto in = Load(d, in_lanes.get());
        const auto mask =
            RebindMask(d, Gt(Load(di, mask_lanes.get()), Zero(di)));
        StoreMaskBits(d, mask, bits.get());

        // Expand
        ZeroBytes(actual_u, N * sizeof(T));
        StoreU(Expand(in, mask), d, actual_u);
        CheckExpanded(d, di, "Expand", in_lanes, mask_lanes, expected, actual_u,
                      __LINE__);

        // LoadExpand
        ZeroBytes(actual_u, N * sizeof(T));
        StoreU(LoadExpand(mask, d, in_lanes.get()), d, actual_u);
        CheckExpanded(d, di, "LoadExpand", in_lanes, mask_lanes, expected,
                      actual_u, __LINE__);
      }  // rep
    }    // frac
  }      // operator()
};

HWY_NOINLINE void TestAllExpand() {
  ForAllTypes(ForPartialVectors<TestExpand>());
}

#endif  // !HWY_PRINT_TABLES

#if HWY_PRINT_TABLES || HWY_IDE
void PrintExpand8x8Tables() {
  printf("// %s\n", __FUNCTION__);
  constexpr size_t N = 8;
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint8_t, N> indices{0};
    size_t pos = 0;
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[i] = pos++;
      } else {
        indices[i] = 0x80;  // Output of TableLookupBytes will be zero.
      }
    }
    HWY_ASSERT(pos == PopCount(code));

    for (size_t i = 0; i < N; ++i) {
      printf("%d,", indices[i]);
    }
    printf("//\n");
  }
  printf("\n");
}

// For SVE
void PrintExpand16x8LaneTables() {
  printf("// %s\n", __FUNCTION__);
  constexpr size_t N = 8;  // (128-bit SIMD)
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint8_t, N> indices{0};
    size_t pos = 0;
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[i] = pos++;
      } else {
        indices[i] = 0xFF;  // This is out of bounds for SVE.
      }
    }
    HWY_ASSERT(pos == PopCount(code));

    for (size_t i = 0; i < N; ++i) {
      printf("%d,", indices[i]);
    }
    printf("//\n");
  }
  printf("\n");
}

void PrintExpand16x8ByteTables() {
  printf("// %s\n", __FUNCTION__);
  constexpr size_t N = 8;  // 128-bit SIMD
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint8_t, N> indices{0};
    size_t pos = 0;
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[i] = pos++;
      } else {
        indices[i] = 64;  // The output of TableLookupBytesOr0 will be zero.
      }
    }
    HWY_ASSERT(pos == PopCount(code));

    // Doubled (for converting lane to byte indices)
    for (size_t i = 0; i < N; ++i) {
      printf("%d,", 2 * indices[i]);
    }
    printf("//\n");
  }
  printf("\n");
}

// Compressed to nibbles, unpacked via variable right shift. MSB indicates the
// output should be zero (which AVX2 permutevar8x32 cannot do by itself).
void PrintExpand32x8NibbleTables() {
  printf("// %s\n", __FUNCTION__);
  constexpr size_t N = 8;  // (AVX2 or 64-bit AVX3)
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[i] = pos++;
      } else {
        indices[i] = 0xF;
      }
    }
    HWY_ASSERT(pos == PopCount(code));

    // Convert to nibbles.
    uint64_t packed = 0;
    for (size_t i = 0; i < N; ++i) {
      HWY_ASSERT(indices[i] < N || indices[i] == 0xF);
      packed += indices[i] << (i * 4);
    }

    HWY_ASSERT(packed < (1ull << (N * 4)));
    printf("0x%08x,", static_cast<uint32_t>(packed));
  }
  printf("\n");
}

// Compressed to nibbles, MSB set if output should be zero.
void PrintExpand64x4NibbleTables() {
  printf("// %s\n", __FUNCTION__);
  constexpr size_t N = 4;  // AVX2
  for (uint64_t code = 0; code < (1ull << N); ++code) {
    std::array<uint32_t, N> indices{0};
    size_t pos = 0;
    for (size_t i = 0; i < N; ++i) {
      if (code & (1ull << i)) {
        indices[i] = pos++;
      } else {
        indices[i] = 0xF;
      }
    }
    HWY_ASSERT(pos == PopCount(code));

    // Convert to nibbles
    uint64_t packed = 0;
    for (size_t i = 0; i < N; ++i) {
      HWY_ASSERT(indices[i] < N || indices[i] == 0xF);
      packed += indices[i] << (i * 4);
    }

    HWY_ASSERT(packed < (1ull << (N * 4)));
    printf("0x%08x,", static_cast<uint32_t>(packed));
  }
  printf("\n");
}

HWY_NOINLINE void PrintTables() {
  // Only print once.
#if HWY_TARGET == HWY_STATIC_TARGET
  PrintExpand32x8NibbleTables();
  PrintExpand64x4NibbleTables();
  PrintExpand16x8LaneTables();
  PrintExpand16x8ByteTables();
  PrintExpand8x8Tables();
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
HWY_BEFORE_TEST(HwyExpandTest);
#if HWY_PRINT_TABLES
// Only print instead of running tests; this will be visible in the log.
HWY_EXPORT_AND_TEST_P(HwyExpandTest, PrintTables);
#else
HWY_EXPORT_AND_TEST_P(HwyExpandTest, TestAllExpand);
#endif
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
