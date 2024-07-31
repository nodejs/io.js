// Copyright 2019 The Abseil Authors.
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

#include <array>
#include <string>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"

namespace {

void BM_InlinedVectorFill(benchmark::State& state) {
  const int len = state.range(0);
  absl::InlinedVector<int, 8> v;
  v.reserve(len);
  for (auto _ : state) {
    v.resize(0);  // Use resize(0) as InlinedVector releases storage on clear().
    for (int i = 0; i < len; ++i) {
      v.push_back(i);
    }
    benchmark::DoNotOptimize(v);
  }
}
BENCHMARK(BM_InlinedVectorFill)->Range(1, 256);

void BM_InlinedVectorFillRange(benchmark::State& state) {
  const int len = state.range(0);
  const std::vector<int> src(len, len);
  absl::InlinedVector<int, 8> v;
  v.reserve(len);
  for (auto _ : state) {
    benchmark::DoNotOptimize(src);
    v.assign(src.begin(), src.end());
    benchmark::DoNotOptimize(v);
  }
}
BENCHMARK(BM_InlinedVectorFillRange)->Range(1, 256);

void BM_StdVectorFill(benchmark::State& state) {
  const int len = state.range(0);
  std::vector<int> v;
  v.reserve(len);
  for (auto _ : state) {
    v.clear();
    for (int i = 0; i < len; ++i) {
      v.push_back(i);
    }
    benchmark::DoNotOptimize(v);
  }
}
BENCHMARK(BM_StdVectorFill)->Range(1, 256);

// The purpose of the next two benchmarks is to verify that
// absl::InlinedVector is efficient when moving is more efficient than
// copying. To do so, we use strings that are larger than the short
// string optimization.
bool StringRepresentedInline(std::string s) {
  const char* chars = s.data();
  std::string s1 = std::move(s);
  return s1.data() != chars;
}

int GetNonShortStringOptimizationSize() {
  for (int i = 24; i <= 192; i *= 2) {
    if (!StringRepresentedInline(std::string(i, 'A'))) {
      return i;
    }
  }
  ABSL_RAW_LOG(
      FATAL,
      "Failed to find a string larger than the short string optimization");
  return -1;
}

void BM_InlinedVectorFillString(benchmark::State& state) {
  const int len = state.range(0);
  const int no_sso = GetNonShortStringOptimizationSize();
  std::string strings[4] = {std::string(no_sso, 'A'), std::string(no_sso, 'B'),
                            std::string(no_sso, 'C'), std::string(no_sso, 'D')};

  for (auto _ : state) {
    absl::InlinedVector<std::string, 8> v;
    for (int i = 0; i < len; i++) {
      v.push_back(strings[i & 3]);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * len);
}
BENCHMARK(BM_InlinedVectorFillString)->Range(0, 1024);

void BM_StdVectorFillString(benchmark::State& state) {
  const int len = state.range(0);
  const int no_sso = GetNonShortStringOptimizationSize();
  std::string strings[4] = {std::string(no_sso, 'A'), std::string(no_sso, 'B'),
                            std::string(no_sso, 'C'), std::string(no_sso, 'D')};

  for (auto _ : state) {
    std::vector<std::string> v;
    for (int i = 0; i < len; i++) {
      v.push_back(strings[i & 3]);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * len);
}
BENCHMARK(BM_StdVectorFillString)->Range(0, 1024);

struct Buffer {  // some arbitrary structure for benchmarking.
  char* base;
  int length;
  int capacity;
  void* user_data;
};

void BM_InlinedVectorAssignments(benchmark::State& state) {
  const int len = state.range(0);
  using BufferVec = absl::InlinedVector<Buffer, 2>;

  BufferVec src;
  src.resize(len);

  BufferVec dst;
  for (auto _ : state) {
    benchmark::DoNotOptimize(dst);
    benchmark::DoNotOptimize(src);
    dst = src;
  }
}
BENCHMARK(BM_InlinedVectorAssignments)
    ->Arg(0)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(20);

void BM_CreateFromContainer(benchmark::State& state) {
  for (auto _ : state) {
    absl::InlinedVector<int, 4> src{1, 2, 3};
    benchmark::DoNotOptimize(src);
    absl::InlinedVector<int, 4> dst(std::move(src));
    benchmark::DoNotOptimize(dst);
  }
}
BENCHMARK(BM_CreateFromContainer);

struct LargeCopyableOnly {
  LargeCopyableOnly() : d(1024, 17) {}
  LargeCopyableOnly(const LargeCopyableOnly& o) = default;
  LargeCopyableOnly& operator=(const LargeCopyableOnly& o) = default;

  std::vector<int> d;
};

struct LargeCopyableSwappable {
  LargeCopyableSwappable() : d(1024, 17) {}

  LargeCopyableSwappable(const LargeCopyableSwappable& o) = default;

  LargeCopyableSwappable& operator=(LargeCopyableSwappable o) {
    using std::swap;
    swap(*this, o);
    return *this;
  }

  friend void swap(LargeCopyableSwappable& a, LargeCopyableSwappable& b) {
    using std::swap;
    swap(a.d, b.d);
  }

  std::vector<int> d;
};

struct LargeCopyableMovable {
  LargeCopyableMovable() : d(1024, 17) {}
  // Use implicitly defined copy and move.

  std::vector<int> d;
};

struct LargeCopyableMovableSwappable {
  LargeCopyableMovableSwappable() : d(1024, 17) {}
  LargeCopyableMovableSwappable(const LargeCopyableMovableSwappable& o) =
      default;
  LargeCopyableMovableSwappable(LargeCopyableMovableSwappable&& o) = default;

  LargeCopyableMovableSwappable& operator=(LargeCopyableMovableSwappable o) {
    using std::swap;
    swap(*this, o);
    return *this;
  }
  LargeCopyableMovableSwappable& operator=(LargeCopyableMovableSwappable&& o) =
      default;

  friend void swap(LargeCopyableMovableSwappable& a,
                   LargeCopyableMovableSwappable& b) {
    using std::swap;
    swap(a.d, b.d);
  }

  std::vector<int> d;
};

template <typename ElementType>
void BM_SwapElements(benchmark::State& state) {
  const int len = state.range(0);
  using Vec = absl::InlinedVector<ElementType, 32>;
  Vec a(len);
  Vec b;
  for (auto _ : state) {
    using std::swap;
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    swap(a, b);
  }
}
BENCHMARK_TEMPLATE(BM_SwapElements, LargeCopyableOnly)->Range(0, 1024);
BENCHMARK_TEMPLATE(BM_SwapElements, LargeCopyableSwappable)->Range(0, 1024);
BENCHMARK_TEMPLATE(BM_SwapElements, LargeCopyableMovable)->Range(0, 1024);
BENCHMARK_TEMPLATE(BM_SwapElements, LargeCopyableMovableSwappable)
    ->Range(0, 1024);

// The following benchmark is meant to track the efficiency of the vector size
// as a function of stored type via the benchmark label. It is not meant to
// output useful sizeof operator performance. The loop is a dummy operation
// to fulfill the requirement of running the benchmark.
template <typename VecType>
void BM_Sizeof(benchmark::State& state) {
  int size = 0;
  for (auto _ : state) {
    VecType vec;
    size = sizeof(vec);
  }
  state.SetLabel(absl::StrCat("sz=", size));
}
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<char, 1>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<char, 4>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<char, 7>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<char, 8>);

BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<int, 1>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<int, 4>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<int, 7>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<int, 8>);

BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<void*, 1>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<void*, 4>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<void*, 7>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<void*, 8>);

BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<std::string, 1>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<std::string, 4>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<std::string, 7>);
BENCHMARK_TEMPLATE(BM_Sizeof, absl::InlinedVector<std::string, 8>);

void BM_InlinedVectorIndexInlined(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v[4]);
  }
}
BENCHMARK(BM_InlinedVectorIndexInlined);

void BM_InlinedVectorIndexExternal(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v[4]);
  }
}
BENCHMARK(BM_InlinedVectorIndexExternal);

void BM_StdVectorIndex(benchmark::State& state) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v[4]);
  }
}
BENCHMARK(BM_StdVectorIndex);

void BM_InlinedVectorDataInlined(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.data());
  }
}
BENCHMARK(BM_InlinedVectorDataInlined);

void BM_InlinedVectorDataExternal(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.data());
  }
  state.SetItemsProcessed(16 * static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_InlinedVectorDataExternal);

void BM_StdVectorData(benchmark::State& state) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.data());
  }
  state.SetItemsProcessed(16 * static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_StdVectorData);

void BM_InlinedVectorSizeInlined(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.size());
  }
}
BENCHMARK(BM_InlinedVectorSizeInlined);

void BM_InlinedVectorSizeExternal(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.size());
  }
}
BENCHMARK(BM_InlinedVectorSizeExternal);

void BM_StdVectorSize(benchmark::State& state) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.size());
  }
}
BENCHMARK(BM_StdVectorSize);

void BM_InlinedVectorEmptyInlined(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.empty());
  }
}
BENCHMARK(BM_InlinedVectorEmptyInlined);

void BM_InlinedVectorEmptyExternal(benchmark::State& state) {
  absl::InlinedVector<int, 8> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.empty());
  }
}
BENCHMARK(BM_InlinedVectorEmptyExternal);

void BM_StdVectorEmpty(benchmark::State& state) {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(v.empty());
  }
}
BENCHMARK(BM_StdVectorEmpty);

constexpr size_t kInlinedCapacity = 4;
constexpr size_t kLargeSize = kInlinedCapacity * 2;
constexpr size_t kSmallSize = kInlinedCapacity / 2;
constexpr size_t kBatchSize = 100;

#define ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_FunctionTemplate, T) \
  BENCHMARK_TEMPLATE(BM_FunctionTemplate, T, kLargeSize);        \
  BENCHMARK_TEMPLATE(BM_FunctionTemplate, T, kSmallSize)

#define ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_FunctionTemplate, T)      \
  BENCHMARK_TEMPLATE(BM_FunctionTemplate, T, kLargeSize, kLargeSize); \
  BENCHMARK_TEMPLATE(BM_FunctionTemplate, T, kLargeSize, kSmallSize); \
  BENCHMARK_TEMPLATE(BM_FunctionTemplate, T, kSmallSize, kLargeSize); \
  BENCHMARK_TEMPLATE(BM_FunctionTemplate, T, kSmallSize, kSmallSize)

template <typename T>
using InlVec = absl::InlinedVector<T, kInlinedCapacity>;

struct TrivialType {
  size_t val;
};

class NontrivialType {
 public:
  ABSL_ATTRIBUTE_NOINLINE NontrivialType() : val_() {
    benchmark::DoNotOptimize(*this);
  }

  ABSL_ATTRIBUTE_NOINLINE NontrivialType(const NontrivialType& other)
      : val_(other.val_) {
    benchmark::DoNotOptimize(*this);
  }

  ABSL_ATTRIBUTE_NOINLINE NontrivialType& operator=(
      const NontrivialType& other) {
    val_ = other.val_;
    benchmark::DoNotOptimize(*this);
    return *this;
  }

  ABSL_ATTRIBUTE_NOINLINE ~NontrivialType() noexcept {
    benchmark::DoNotOptimize(*this);
  }

 private:
  size_t val_;
};

template <typename T, typename PrepareVecFn, typename TestVecFn>
void BatchedBenchmark(benchmark::State& state, PrepareVecFn prepare_vec,
                      TestVecFn test_vec) {
  std::array<InlVec<T>, kBatchSize> vector_batch{};

  while (state.KeepRunningBatch(kBatchSize)) {
    // Prepare batch
    state.PauseTiming();
    for (size_t i = 0; i < kBatchSize; ++i) {
      prepare_vec(vector_batch.data() + i, i);
    }
    benchmark::DoNotOptimize(vector_batch);
    state.ResumeTiming();

    // Test batch
    for (size_t i = 0; i < kBatchSize; ++i) {
      test_vec(vector_batch.data() + i, i);
    }
  }
}

template <typename T, size_t ToSize>
void BM_ConstructFromSize(benchmark::State& state) {
  using VecT = InlVec<T>;
  auto size = ToSize;
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */ [](InlVec<T>* vec, size_t) { vec->~VecT(); },
      /* test_vec = */
      [&](void* ptr, size_t) {
        benchmark::DoNotOptimize(size);
        ::new (ptr) VecT(size);
      });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromSize, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromSize, NontrivialType);

template <typename T, size_t ToSize>
void BM_ConstructFromSizeRef(benchmark::State& state) {
  using VecT = InlVec<T>;
  auto size = ToSize;
  auto ref = T();
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */ [](InlVec<T>* vec, size_t) { vec->~VecT(); },
      /* test_vec = */
      [&](void* ptr, size_t) {
        benchmark::DoNotOptimize(size);
        benchmark::DoNotOptimize(ref);
        ::new (ptr) VecT(size, ref);
      });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromSizeRef, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromSizeRef, NontrivialType);

template <typename T, size_t ToSize>
void BM_ConstructFromRange(benchmark::State& state) {
  using VecT = InlVec<T>;
  std::array<T, ToSize> arr{};
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */ [](InlVec<T>* vec, size_t) { vec->~VecT(); },
      /* test_vec = */
      [&](void* ptr, size_t) {
        benchmark::DoNotOptimize(arr);
        ::new (ptr) VecT(arr.begin(), arr.end());
      });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromRange, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromRange, NontrivialType);

template <typename T, size_t ToSize>
void BM_ConstructFromCopy(benchmark::State& state) {
  using VecT = InlVec<T>;
  VecT other_vec(ToSize);
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) { vec->~VecT(); },
      /* test_vec = */
      [&](void* ptr, size_t) {
        benchmark::DoNotOptimize(other_vec);
        ::new (ptr) VecT(other_vec);
      });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromCopy, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromCopy, NontrivialType);

template <typename T, size_t ToSize>
void BM_ConstructFromMove(benchmark::State& state) {
  using VecT = InlVec<T>;
  std::array<VecT, kBatchSize> vector_batch{};
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [&](InlVec<T>* vec, size_t i) {
        vector_batch[i].clear();
        vector_batch[i].resize(ToSize);
        vec->~VecT();
      },
      /* test_vec = */
      [&](void* ptr, size_t i) {
        benchmark::DoNotOptimize(vector_batch[i]);
        ::new (ptr) VecT(std::move(vector_batch[i]));
      });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromMove, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_ConstructFromMove, NontrivialType);

// Measure cost of copy-constructor+destructor.
void BM_CopyTrivial(benchmark::State& state) {
  const int n = state.range(0);
  InlVec<int64_t> src(n);
  for (auto s : state) {
    InlVec<int64_t> copy(src);
    benchmark::DoNotOptimize(copy);
  }
}
BENCHMARK(BM_CopyTrivial)->Arg(0)->Arg(1)->Arg(kLargeSize);

// Measure cost of copy-constructor+destructor.
void BM_CopyNonTrivial(benchmark::State& state) {
  const int n = state.range(0);
  InlVec<InlVec<int64_t>> src(n);
  for (auto s : state) {
    InlVec<InlVec<int64_t>> copy(src);
    benchmark::DoNotOptimize(copy);
  }
}
BENCHMARK(BM_CopyNonTrivial)->Arg(0)->Arg(1)->Arg(kLargeSize);

template <typename T, size_t FromSize, size_t ToSize>
void BM_AssignSizeRef(benchmark::State& state) {
  auto size = ToSize;
  auto ref = T();
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */ [](InlVec<T>* vec, size_t) { vec->resize(FromSize); },
      /* test_vec = */
      [&](InlVec<T>* vec, size_t) {
        benchmark::DoNotOptimize(size);
        benchmark::DoNotOptimize(ref);
        vec->assign(size, ref);
      });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_AssignSizeRef, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_AssignSizeRef, NontrivialType);

template <typename T, size_t FromSize, size_t ToSize>
void BM_AssignRange(benchmark::State& state) {
  std::array<T, ToSize> arr{};
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */ [](InlVec<T>* vec, size_t) { vec->resize(FromSize); },
      /* test_vec = */
      [&](InlVec<T>* vec, size_t) {
        benchmark::DoNotOptimize(arr);
        vec->assign(arr.begin(), arr.end());
      });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_AssignRange, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_AssignRange, NontrivialType);

template <typename T, size_t FromSize, size_t ToSize>
void BM_AssignFromCopy(benchmark::State& state) {
  InlVec<T> other_vec(ToSize);
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */ [](InlVec<T>* vec, size_t) { vec->resize(FromSize); },
      /* test_vec = */
      [&](InlVec<T>* vec, size_t) {
        benchmark::DoNotOptimize(other_vec);
        *vec = other_vec;
      });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_AssignFromCopy, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_AssignFromCopy, NontrivialType);

template <typename T, size_t FromSize, size_t ToSize>
void BM_AssignFromMove(benchmark::State& state) {
  using VecT = InlVec<T>;
  std::array<VecT, kBatchSize> vector_batch{};
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [&](InlVec<T>* vec, size_t i) {
        vector_batch[i].clear();
        vector_batch[i].resize(ToSize);
        vec->resize(FromSize);
      },
      /* test_vec = */
      [&](InlVec<T>* vec, size_t i) {
        benchmark::DoNotOptimize(vector_batch[i]);
        *vec = std::move(vector_batch[i]);
      });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_AssignFromMove, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_AssignFromMove, NontrivialType);

template <typename T, size_t FromSize, size_t ToSize>
void BM_ResizeSize(benchmark::State& state) {
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [](InlVec<T>* vec, size_t) { vec->resize(ToSize); });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_ResizeSize, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_ResizeSize, NontrivialType);

template <typename T, size_t FromSize, size_t ToSize>
void BM_ResizeSizeRef(benchmark::State& state) {
  auto t = T();
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [&](InlVec<T>* vec, size_t) {
        benchmark::DoNotOptimize(t);
        vec->resize(ToSize, t);
      });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_ResizeSizeRef, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_ResizeSizeRef, NontrivialType);

template <typename T, size_t FromSize, size_t ToSize>
void BM_InsertSizeRef(benchmark::State& state) {
  auto t = T();
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [&](InlVec<T>* vec, size_t) {
        benchmark::DoNotOptimize(t);
        auto* pos = vec->data() + (vec->size() / 2);
        vec->insert(pos, t);
      });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_InsertSizeRef, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_InsertSizeRef, NontrivialType);

template <typename T, size_t FromSize, size_t ToSize>
void BM_InsertRange(benchmark::State& state) {
  InlVec<T> other_vec(ToSize);
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [&](InlVec<T>* vec, size_t) {
        benchmark::DoNotOptimize(other_vec);
        auto* pos = vec->data() + (vec->size() / 2);
        vec->insert(pos, other_vec.begin(), other_vec.end());
      });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_InsertRange, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_InsertRange, NontrivialType);

template <typename T, size_t FromSize>
void BM_EmplaceBack(benchmark::State& state) {
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [](InlVec<T>* vec, size_t) { vec->emplace_back(); });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_EmplaceBack, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_EmplaceBack, NontrivialType);

template <typename T, size_t FromSize>
void BM_PopBack(benchmark::State& state) {
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [](InlVec<T>* vec, size_t) { vec->pop_back(); });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_PopBack, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_PopBack, NontrivialType);

template <typename T, size_t FromSize>
void BM_EraseOne(benchmark::State& state) {
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [](InlVec<T>* vec, size_t) {
        auto* pos = vec->data() + (vec->size() / 2);
        vec->erase(pos);
      });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_EraseOne, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_EraseOne, NontrivialType);

template <typename T, size_t FromSize>
void BM_EraseRange(benchmark::State& state) {
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [](InlVec<T>* vec, size_t) {
        auto* pos = vec->data() + (vec->size() / 2);
        vec->erase(pos, pos + 1);
      });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_EraseRange, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_EraseRange, NontrivialType);

template <typename T, size_t FromSize>
void BM_Clear(benchmark::State& state) {
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */ [](InlVec<T>* vec, size_t) { vec->resize(FromSize); },
      /* test_vec = */ [](InlVec<T>* vec, size_t) { vec->clear(); });
}
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_Clear, TrivialType);
ABSL_INTERNAL_BENCHMARK_ONE_SIZE(BM_Clear, NontrivialType);

template <typename T, size_t FromSize, size_t ToCapacity>
void BM_Reserve(benchmark::State& state) {
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(FromSize);
      },
      /* test_vec = */
      [](InlVec<T>* vec, size_t) { vec->reserve(ToCapacity); });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_Reserve, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_Reserve, NontrivialType);

template <typename T, size_t FromCapacity, size_t ToCapacity>
void BM_ShrinkToFit(benchmark::State& state) {
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [](InlVec<T>* vec, size_t) {
        vec->clear();
        vec->resize(ToCapacity);
        vec->reserve(FromCapacity);
      },
      /* test_vec = */ [](InlVec<T>* vec, size_t) { vec->shrink_to_fit(); });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_ShrinkToFit, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_ShrinkToFit, NontrivialType);

template <typename T, size_t FromSize, size_t ToSize>
void BM_Swap(benchmark::State& state) {
  using VecT = InlVec<T>;
  std::array<VecT, kBatchSize> vector_batch{};
  BatchedBenchmark<T>(
      state,
      /* prepare_vec = */
      [&](InlVec<T>* vec, size_t i) {
        vector_batch[i].clear();
        vector_batch[i].resize(ToSize);
        vec->resize(FromSize);
      },
      /* test_vec = */
      [&](InlVec<T>* vec, size_t i) {
        using std::swap;
        benchmark::DoNotOptimize(vector_batch[i]);
        swap(*vec, vector_batch[i]);
      });
}
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_Swap, TrivialType);
ABSL_INTERNAL_BENCHMARK_TWO_SIZE(BM_Swap, NontrivialType);

}  // namespace
