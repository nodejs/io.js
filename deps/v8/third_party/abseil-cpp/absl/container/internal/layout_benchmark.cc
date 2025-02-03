// Copyright 2018 The Abseil Authors.
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
//
// Every benchmark should have the same performance as the corresponding
// headroom benchmark.

#include <cstddef>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "absl/container/internal/layout.h"
#include "benchmark/benchmark.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::benchmark::DoNotOptimize;

using Int128 = int64_t[2];

constexpr size_t MyAlign(size_t n, size_t m) { return (n + m - 1) & ~(m - 1); }

// This benchmark provides the upper bound on performance for BM_OffsetConstant.
template <size_t Offset, class... Ts>
void BM_OffsetConstantHeadroom(benchmark::State& state) {
  for (auto _ : state) {
    DoNotOptimize(Offset);
  }
}

template <size_t Offset, class... Ts>
void BM_OffsetConstantStatic(benchmark::State& state) {
  using L = typename Layout<Ts...>::template WithStaticSizes<3, 5, 7>;
  ABSL_RAW_CHECK(L::Partial().template Offset<3>() == Offset, "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(L::Partial().template Offset<3>());
  }
}

template <size_t Offset, class... Ts>
void BM_OffsetConstant(benchmark::State& state) {
  using L = Layout<Ts...>;
  ABSL_RAW_CHECK(L::Partial(3, 5, 7).template Offset<3>() == Offset,
                 "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(L::Partial(3, 5, 7).template Offset<3>());
  }
}

template <size_t Offset, class... Ts>
void BM_OffsetConstantIndirect(benchmark::State& state) {
  using L = Layout<Ts...>;
  auto p = L::Partial(3, 5, 7);
  ABSL_RAW_CHECK(p.template Offset<3>() == Offset, "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(p);
    DoNotOptimize(p.template Offset<3>());
  }
}

template <class... Ts>
size_t PartialOffset(size_t k);

template <>
size_t PartialOffset<int8_t, int16_t, int32_t, Int128>(size_t k) {
  constexpr size_t o = MyAlign(MyAlign(3 * 1, 2) + 5 * 2, 4);
  return MyAlign(o + k * 4, 8);
}

template <>
size_t PartialOffset<Int128, int32_t, int16_t, int8_t>(size_t k) {
  // No alignment is necessary.
  return 3 * 16 + 5 * 4 + k * 2;
}

// This benchmark provides the upper bound on performance for BM_OffsetVariable.
template <size_t Offset, class... Ts>
void BM_OffsetPartialHeadroom(benchmark::State& state) {
  size_t k = 7;
  ABSL_RAW_CHECK(PartialOffset<Ts...>(k) == Offset, "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(k);
    DoNotOptimize(PartialOffset<Ts...>(k));
  }
}

template <size_t Offset, class... Ts>
void BM_OffsetPartialStatic(benchmark::State& state) {
  using L = typename Layout<Ts...>::template WithStaticSizes<3, 5>;
  size_t k = 7;
  ABSL_RAW_CHECK(L::Partial(k).template Offset<3>() == Offset,
                 "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(k);
    DoNotOptimize(L::Partial(k).template Offset<3>());
  }
}

template <size_t Offset, class... Ts>
void BM_OffsetPartial(benchmark::State& state) {
  using L = Layout<Ts...>;
  size_t k = 7;
  ABSL_RAW_CHECK(L::Partial(3, 5, k).template Offset<3>() == Offset,
                 "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(k);
    DoNotOptimize(L::Partial(3, 5, k).template Offset<3>());
  }
}

template <class... Ts>
size_t VariableOffset(size_t n, size_t m, size_t k);

template <>
size_t VariableOffset<int8_t, int16_t, int32_t, Int128>(size_t n, size_t m,
                                                        size_t k) {
  return MyAlign(MyAlign(MyAlign(n * 1, 2) + m * 2, 4) + k * 4, 8);
}

template <>
size_t VariableOffset<Int128, int32_t, int16_t, int8_t>(size_t n, size_t m,
                                                        size_t k) {
  // No alignment is necessary.
  return n * 16 + m * 4 + k * 2;
}

// This benchmark provides the upper bound on performance for BM_OffsetVariable.
template <size_t Offset, class... Ts>
void BM_OffsetVariableHeadroom(benchmark::State& state) {
  size_t n = 3;
  size_t m = 5;
  size_t k = 7;
  ABSL_RAW_CHECK(VariableOffset<Ts...>(n, m, k) == Offset, "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(n);
    DoNotOptimize(m);
    DoNotOptimize(k);
    DoNotOptimize(VariableOffset<Ts...>(n, m, k));
  }
}

template <size_t Offset, class... Ts>
void BM_OffsetVariable(benchmark::State& state) {
  using L = Layout<Ts...>;
  size_t n = 3;
  size_t m = 5;
  size_t k = 7;
  ABSL_RAW_CHECK(L::Partial(n, m, k).template Offset<3>() == Offset,
                 "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(n);
    DoNotOptimize(m);
    DoNotOptimize(k);
    DoNotOptimize(L::Partial(n, m, k).template Offset<3>());
  }
}

template <class... Ts>
size_t AllocSize(size_t x);

template <>
size_t AllocSize<int8_t, int16_t, int32_t, Int128>(size_t x) {
  constexpr size_t o =
      Layout<int8_t, int16_t, int32_t, Int128>::Partial(3, 5, 7)
          .template Offset<Int128>();
  return o + sizeof(Int128) * x;
}

template <>
size_t AllocSize<Int128, int32_t, int16_t, int8_t>(size_t x) {
  constexpr size_t o =
      Layout<Int128, int32_t, int16_t, int8_t>::Partial(3, 5, 7)
          .template Offset<int8_t>();
  return o + sizeof(int8_t) * x;
}

// This benchmark provides the upper bound on performance for BM_AllocSize
template <size_t Size, class... Ts>
void BM_AllocSizeHeadroom(benchmark::State& state) {
  size_t x = 9;
  ABSL_RAW_CHECK(AllocSize<Ts...>(x) == Size, "Invalid size");
  for (auto _ : state) {
    DoNotOptimize(x);
    DoNotOptimize(AllocSize<Ts...>(x));
  }
}

template <size_t Size, class... Ts>
void BM_AllocSizeStatic(benchmark::State& state) {
  using L = typename Layout<Ts...>::template WithStaticSizes<3, 5, 7>;
  size_t x = 9;
  ABSL_RAW_CHECK(L(x).AllocSize() == Size, "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(x);
    DoNotOptimize(L(x).AllocSize());
  }
}

template <size_t Size, class... Ts>
void BM_AllocSize(benchmark::State& state) {
  using L = Layout<Ts...>;
  size_t n = 3;
  size_t m = 5;
  size_t k = 7;
  size_t x = 9;
  ABSL_RAW_CHECK(L(n, m, k, x).AllocSize() == Size, "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(n);
    DoNotOptimize(m);
    DoNotOptimize(k);
    DoNotOptimize(x);
    DoNotOptimize(L(n, m, k, x).AllocSize());
  }
}

template <size_t Size, class... Ts>
void BM_AllocSizeIndirect(benchmark::State& state) {
  using L = Layout<Ts...>;
  auto l = L(3, 5, 7, 9);
  ABSL_RAW_CHECK(l.AllocSize() == Size, "Invalid offset");
  for (auto _ : state) {
    DoNotOptimize(l);
    DoNotOptimize(l.AllocSize());
  }
}

// Run all benchmarks in two modes:
//
//   Layout with padding: int8_t[3], int16_t[5], int32_t[7], Int128[?].
//   Layout without padding: Int128[3], int32_t[5], int16_t[7], int8_t[?].

#define OFFSET_BENCHMARK(NAME, OFFSET, T1, T2, T3, T4) \
  auto& NAME##_##OFFSET##_##T1##_##T2##_##T3##_##T4 =  \
      NAME<OFFSET, T1, T2, T3, T4>;                    \
  BENCHMARK(NAME##_##OFFSET##_##T1##_##T2##_##T3##_##T4)

OFFSET_BENCHMARK(BM_OffsetConstantHeadroom, 48, int8_t, int16_t, int32_t,
                 Int128);
OFFSET_BENCHMARK(BM_OffsetConstantStatic, 48, int8_t, int16_t, int32_t, Int128);
OFFSET_BENCHMARK(BM_OffsetConstant, 48, int8_t, int16_t, int32_t, Int128);
OFFSET_BENCHMARK(BM_OffsetConstantIndirect, 48, int8_t, int16_t, int32_t,
                 Int128);

OFFSET_BENCHMARK(BM_OffsetConstantHeadroom, 82, Int128, int32_t, int16_t,
                 int8_t);
OFFSET_BENCHMARK(BM_OffsetConstantStatic, 82, Int128, int32_t, int16_t, int8_t);
OFFSET_BENCHMARK(BM_OffsetConstant, 82, Int128, int32_t, int16_t, int8_t);
OFFSET_BENCHMARK(BM_OffsetConstantIndirect, 82, Int128, int32_t, int16_t,
                 int8_t);

OFFSET_BENCHMARK(BM_OffsetPartialHeadroom, 48, int8_t, int16_t, int32_t,
                 Int128);
OFFSET_BENCHMARK(BM_OffsetPartialStatic, 48, int8_t, int16_t, int32_t, Int128);
OFFSET_BENCHMARK(BM_OffsetPartial, 48, int8_t, int16_t, int32_t, Int128);

OFFSET_BENCHMARK(BM_OffsetPartialHeadroom, 82, Int128, int32_t, int16_t,
                 int8_t);
OFFSET_BENCHMARK(BM_OffsetPartialStatic, 82, Int128, int32_t, int16_t, int8_t);
OFFSET_BENCHMARK(BM_OffsetPartial, 82, Int128, int32_t, int16_t, int8_t);

OFFSET_BENCHMARK(BM_OffsetVariableHeadroom, 48, int8_t, int16_t, int32_t,
                 Int128);
OFFSET_BENCHMARK(BM_OffsetVariable, 48, int8_t, int16_t, int32_t, Int128);

OFFSET_BENCHMARK(BM_OffsetVariableHeadroom, 82, Int128, int32_t, int16_t,
                 int8_t);
OFFSET_BENCHMARK(BM_OffsetVariable, 82, Int128, int32_t, int16_t, int8_t);

OFFSET_BENCHMARK(BM_AllocSizeHeadroom, 192, int8_t, int16_t, int32_t, Int128);
OFFSET_BENCHMARK(BM_AllocSizeStatic, 192, int8_t, int16_t, int32_t, Int128);
OFFSET_BENCHMARK(BM_AllocSize, 192, int8_t, int16_t, int32_t, Int128);
OFFSET_BENCHMARK(BM_AllocSizeIndirect, 192, int8_t, int16_t, int32_t, Int128);

OFFSET_BENCHMARK(BM_AllocSizeHeadroom, 91, Int128, int32_t, int16_t, int8_t);
OFFSET_BENCHMARK(BM_AllocSizeStatic, 91, Int128, int32_t, int16_t, int8_t);
OFFSET_BENCHMARK(BM_AllocSize, 91, Int128, int32_t, int16_t, int8_t);
OFFSET_BENCHMARK(BM_AllocSizeIndirect, 91, Int128, int32_t, int16_t, int8_t);

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
