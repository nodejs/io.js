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

#include <stdio.h>

#include <vector>

#include "hwy/aligned_allocator.h"
#include "hwy/base.h"
#include "hwy/nanobenchmark.h"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/bit_pack/bit_pack_test.cc"  // NOLINT
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/timer.h"
#include "hwy/contrib/bit_pack/bit_pack-inl.h"
#include "hwy/tests/test_util-inl.h"
// clang-format on

#ifndef HWY_BIT_PACK_BENCHMARK
#define HWY_BIT_PACK_BENCHMARK 0
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
// Used to prevent running benchmark (slow) for partial vectors and targets
// except the best available. Global, not per-target, hence must be outside
// HWY_NAMESPACE. Declare first because HWY_ONCE is only true after some code
// has been re-included.
extern size_t last_bits;
extern uint64_t best_target;
#if HWY_ONCE
size_t last_bits = 0;
uint64_t best_target = ~0ull;
#endif
namespace HWY_NAMESPACE {
namespace {

template <size_t kBits, typename T>
T Random(RandomState& rng) {
  return ConvertScalarTo<T>(Random32(&rng) & kBits);
}

template <typename T>
class Checker {
 public:
  explicit Checker(size_t num) { raw_.reserve(num); }
  void NotifyRaw(T raw) { raw_.push_back(raw); }

  void NotifyRawOutput(size_t bits, T raw) {
    if (raw_[num_verified_] != raw) {
      HWY_ABORT("%zu bits: pos %zu of %zu, expected %.0f actual %.0f\n", bits,
                num_verified_, raw_.size(),
                ConvertScalarTo<double>(raw_[num_verified_]),
                ConvertScalarTo<double>(raw));
    }
    ++num_verified_;
  }

 private:
  std::vector<T> raw_;
  size_t num_verified_ = 0;
};

template <template <size_t> class PackT, size_t kVectors, size_t kBits>
struct TestPack {
  template <typename T, class D>
  void operator()(T /* t */, D d) {
    constexpr size_t kLoops = 16;  // working set slightly larger than L1
    const size_t N = Lanes(d);
    RandomState rng(N * 129);
    static_assert(kBits <= kVectors, "");
    const size_t num_per_loop = N * kVectors;
    const size_t num = num_per_loop * kLoops;
    const size_t num_packed_per_loop = N * kBits;
    const size_t num_packed = num_packed_per_loop * kLoops;
    Checker<T> checker(num);
    AlignedFreeUniquePtr<T[]> raw = hwy::AllocateAligned<T>(num);
    AlignedFreeUniquePtr<T[]> raw2 = hwy::AllocateAligned<T>(num);
    AlignedFreeUniquePtr<T[]> packed = hwy::AllocateAligned<T>(num_packed);
    HWY_ASSERT(raw && raw2 && packed);

    for (size_t i = 0; i < num; ++i) {
      raw[i] = Random<kBits, T>(rng);
      checker.NotifyRaw(raw[i]);
    }

    best_target = HWY_MIN(best_target, HWY_TARGET);
    const bool run_bench = HWY_BIT_PACK_BENCHMARK && (kBits != last_bits) &&
                           (HWY_TARGET == best_target);
    last_bits = kBits;

    const PackT<kBits> func;

    if (run_bench) {
      const size_t kNumInputs = 1;
      const size_t num_items = num * size_t(Unpredictable1());
      const FuncInput inputs[kNumInputs] = {num_items};
      Result results[kNumInputs];

      Params p;
      p.verbose = false;
      p.max_evals = 7;
      p.target_rel_mad = 0.002;
      const size_t num_results = MeasureClosure(
          [&](FuncInput) HWY_ATTR {
            for (size_t i = 0, pi = 0; i < num;
                 i += num_per_loop, pi += num_packed_per_loop) {
              func.Pack(d, raw.get() + i, packed.get() + pi);
            }
            T& val = packed.get()[Random32(&rng) % num_packed];
            T zero = static_cast<T>(Unpredictable1() - 1);
            val = static_cast<T>(val + zero);
            for (size_t i = 0, pi = 0; i < num;
                 i += num_per_loop, pi += num_packed_per_loop) {
              func.Unpack(d, packed.get() + pi, raw2.get() + i);
            }
            return raw2[Random32(&rng) % num];
          },
          inputs, kNumInputs, results, p);
      if (num_results != kNumInputs) {
        fprintf(stderr, "MeasureClosure failed.\n");
        return;
      }
      // Print throughput for pack+unpack round trip
      for (size_t i = 0; i < num_results; ++i) {
        const size_t bytes_per_element = (kBits + 7) / 8;
        const double bytes =
            static_cast<double>(results[i].input * bytes_per_element);
        const double seconds =
            results[i].ticks / platform::InvariantTicksPerSecond();
        printf("Bits:%2d elements:%3d GB/s:%4.1f (+/-%3.1f%%)\n",
               static_cast<int>(kBits), static_cast<int>(results[i].input),
               1E-9 * bytes / seconds, results[i].variability * 100.0);
      }
    } else {
      for (size_t i = 0, pi = 0; i < num;
           i += num_per_loop, pi += num_packed_per_loop) {
        func.Pack(d, raw.get() + i, packed.get() + pi);
      }
      T& val = packed.get()[Random32(&rng) % num_packed];
      T zero = static_cast<T>(Unpredictable1() - 1);
      val = static_cast<T>(val + zero);
      for (size_t i = 0, pi = 0; i < num;
           i += num_per_loop, pi += num_packed_per_loop) {
        func.Unpack(d, packed.get() + pi, raw2.get() + i);
      }
    }

    for (size_t i = 0; i < num; ++i) {
      checker.NotifyRawOutput(kBits, raw2[i]);
    }
  }
};

void TestAllPack8() {
  ForShrinkableVectors<TestPack<Pack8, 8, 1>>()(uint8_t());
  ForShrinkableVectors<TestPack<Pack8, 8, 2>>()(uint8_t());
  ForShrinkableVectors<TestPack<Pack8, 8, 3>>()(uint8_t());
  ForShrinkableVectors<TestPack<Pack8, 8, 4>>()(uint8_t());
  ForShrinkableVectors<TestPack<Pack8, 8, 5>>()(uint8_t());
  ForShrinkableVectors<TestPack<Pack8, 8, 6>>()(uint8_t());
  ForShrinkableVectors<TestPack<Pack8, 8, 7>>()(uint8_t());
  ForShrinkableVectors<TestPack<Pack8, 8, 8>>()(uint8_t());
}

void TestAllPack16() {
  ForShrinkableVectors<TestPack<Pack16, 16, 1>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 2>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 3>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 4>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 5>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 6>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 7>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 8>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 9>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 10>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 11>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 12>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 13>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 14>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 15>>()(uint16_t());
  ForShrinkableVectors<TestPack<Pack16, 16, 16>>()(uint16_t());
}

void TestAllPack32() {
  ForShrinkableVectors<TestPack<Pack32, 32, 1>>()(uint32_t());
  ForShrinkableVectors<TestPack<Pack32, 32, 2>>()(uint32_t());
  ForShrinkableVectors<TestPack<Pack32, 32, 6>>()(uint32_t());
  ForShrinkableVectors<TestPack<Pack32, 32, 11>>()(uint32_t());
  ForShrinkableVectors<TestPack<Pack32, 32, 16>>()(uint32_t());
  ForShrinkableVectors<TestPack<Pack32, 32, 31>>()(uint32_t());
  ForShrinkableVectors<TestPack<Pack32, 32, 32>>()(uint32_t());
}

void TestAllPack64() {
  // Fails, but only on GCC 13.
#if !(HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1400 && \
      HWY_TARGET == HWY_RVV)
  ForShrinkableVectors<TestPack<Pack64, 64, 1>>()(uint64_t());
  ForShrinkableVectors<TestPack<Pack64, 64, 5>>()(uint64_t());
  ForShrinkableVectors<TestPack<Pack64, 64, 12>>()(uint64_t());
  ForShrinkableVectors<TestPack<Pack64, 64, 16>>()(uint64_t());
  ForShrinkableVectors<TestPack<Pack64, 64, 27>>()(uint64_t());
  ForShrinkableVectors<TestPack<Pack64, 64, 31>>()(uint64_t());
  ForShrinkableVectors<TestPack<Pack64, 64, 33>>()(uint64_t());
  ForShrinkableVectors<TestPack<Pack64, 64, 41>>()(uint64_t());
  ForShrinkableVectors<TestPack<Pack64, 64, 61>>()(uint64_t());
#endif
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(BitPackTest);
HWY_EXPORT_AND_TEST_P(BitPackTest, TestAllPack8);
HWY_EXPORT_AND_TEST_P(BitPackTest, TestAllPack16);
HWY_EXPORT_AND_TEST_P(BitPackTest, TestAllPack32);
HWY_EXPORT_AND_TEST_P(BitPackTest, TestAllPack64);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
