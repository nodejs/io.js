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

#include <stdint.h>

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "benchmark/benchmark.h"
#include "absl/algorithm/container.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/btree_test.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/internal/hashtable_debug.h"
#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

constexpr size_t kBenchmarkValues = 1 << 20;

// How many times we add and remove sub-batches in one batch of *AddRem
// benchmarks.
constexpr size_t kAddRemBatchSize = 1 << 2;

// Generates n values in the range [0, 4 * n].
template <typename V>
std::vector<V> GenerateValues(int n) {
  constexpr int kSeed = 23;
  return GenerateValuesWithSeed<V>(n, 4 * n, kSeed);
}

// Benchmark insertion of values into a container.
template <typename T>
void BM_InsertImpl(benchmark::State& state, bool sorted) {
  using V = typename remove_pair_const<typename T::value_type>::type;
  typename KeyOfValue<typename T::key_type, V>::type key_of_value;

  std::vector<V> values = GenerateValues<V>(kBenchmarkValues);
  if (sorted) {
    std::sort(values.begin(), values.end());
  }
  T container(values.begin(), values.end());

  // Remove and re-insert 10% of the keys per batch.
  const int batch_size = (kBenchmarkValues + 9) / 10;
  while (state.KeepRunningBatch(batch_size)) {
    state.PauseTiming();
    const auto i = static_cast<int>(state.iterations());

    for (int j = i; j < i + batch_size; j++) {
      int x = j % kBenchmarkValues;
      container.erase(key_of_value(values[x]));
    }

    state.ResumeTiming();

    for (int j = i; j < i + batch_size; j++) {
      int x = j % kBenchmarkValues;
      container.insert(values[x]);
    }
  }
}

template <typename T>
void BM_Insert(benchmark::State& state) {
  BM_InsertImpl<T>(state, false);
}

template <typename T>
void BM_InsertSorted(benchmark::State& state) {
  BM_InsertImpl<T>(state, true);
}

// Benchmark inserting the first few elements in a container. In b-tree, this is
// when the root node grows.
template <typename T>
void BM_InsertSmall(benchmark::State& state) {
  using V = typename remove_pair_const<typename T::value_type>::type;

  const int kSize = 8;
  std::vector<V> values = GenerateValues<V>(kSize);
  T container;

  while (state.KeepRunningBatch(kSize)) {
    for (int i = 0; i < kSize; ++i) {
      benchmark::DoNotOptimize(container.insert(values[i]));
    }
    state.PauseTiming();
    // Do not measure the time it takes to clear the container.
    container.clear();
    state.ResumeTiming();
  }
}

template <typename T>
void BM_LookupImpl(benchmark::State& state, bool sorted) {
  using V = typename remove_pair_const<typename T::value_type>::type;
  typename KeyOfValue<typename T::key_type, V>::type key_of_value;

  std::vector<V> values = GenerateValues<V>(kBenchmarkValues);
  if (sorted) {
    std::sort(values.begin(), values.end());
  }
  T container(values.begin(), values.end());

  while (state.KeepRunning()) {
    int idx = state.iterations() % kBenchmarkValues;
    benchmark::DoNotOptimize(container.find(key_of_value(values[idx])));
  }
}

// Benchmark lookup of values in a container.
template <typename T>
void BM_Lookup(benchmark::State& state) {
  BM_LookupImpl<T>(state, false);
}

// Benchmark lookup of values in a full container, meaning that values
// are inserted in-order to take advantage of biased insertion, which
// yields a full tree.
template <typename T>
void BM_FullLookup(benchmark::State& state) {
  BM_LookupImpl<T>(state, true);
}

// Benchmark erasing values from a container.
template <typename T>
void BM_Erase(benchmark::State& state) {
  using V = typename remove_pair_const<typename T::value_type>::type;
  typename KeyOfValue<typename T::key_type, V>::type key_of_value;
  std::vector<V> values = GenerateValues<V>(kBenchmarkValues);
  T container(values.begin(), values.end());

  // Remove and re-insert 10% of the keys per batch.
  const int batch_size = (kBenchmarkValues + 9) / 10;
  while (state.KeepRunningBatch(batch_size)) {
    const int i = state.iterations();

    for (int j = i; j < i + batch_size; j++) {
      int x = j % kBenchmarkValues;
      container.erase(key_of_value(values[x]));
    }

    state.PauseTiming();
    for (int j = i; j < i + batch_size; j++) {
      int x = j % kBenchmarkValues;
      container.insert(values[x]);
    }
    state.ResumeTiming();
  }
}

// Benchmark erasing multiple values from a container.
template <typename T>
void BM_EraseRange(benchmark::State& state) {
  using V = typename remove_pair_const<typename T::value_type>::type;
  typename KeyOfValue<typename T::key_type, V>::type key_of_value;
  std::vector<V> values = GenerateValues<V>(kBenchmarkValues);
  T container(values.begin(), values.end());

  // Remove and re-insert 10% of the keys per batch.
  const int batch_size = (kBenchmarkValues + 9) / 10;
  while (state.KeepRunningBatch(batch_size)) {
    const int i = state.iterations();

    const int start_index = i % kBenchmarkValues;

    state.PauseTiming();
    {
      std::vector<V> removed;
      removed.reserve(batch_size);
      auto itr = container.find(key_of_value(values[start_index]));
      auto start = itr;
      for (int j = 0; j < batch_size; j++) {
        if (itr == container.end()) {
          state.ResumeTiming();
          container.erase(start, itr);
          state.PauseTiming();
          itr = container.begin();
          start = itr;
        }
        removed.push_back(*itr++);
      }

      state.ResumeTiming();
      container.erase(start, itr);
      state.PauseTiming();

      container.insert(removed.begin(), removed.end());
    }
    state.ResumeTiming();
  }
}

// Predicate that erases every other element. We can't use a lambda because
// C++11 doesn't support generic lambdas.
// TODO(b/207389011): consider adding benchmarks that remove different fractions
// of keys (e.g. 10%, 90%).
struct EraseIfPred {
  uint64_t i = 0;
  template <typename T>
  bool operator()(const T&) {
    return ++i % 2;
  }
};

// Benchmark erasing multiple values from a container with a predicate.
template <typename T>
void BM_EraseIf(benchmark::State& state) {
  using V = typename remove_pair_const<typename T::value_type>::type;
  std::vector<V> values = GenerateValues<V>(kBenchmarkValues);

  // Removes half of the keys per batch.
  const int batch_size = (kBenchmarkValues + 1) / 2;
  EraseIfPred pred;
  while (state.KeepRunningBatch(batch_size)) {
    state.PauseTiming();
    {
      T container(values.begin(), values.end());
      state.ResumeTiming();
      erase_if(container, pred);
      benchmark::DoNotOptimize(container);
      state.PauseTiming();
    }
    state.ResumeTiming();
  }
}

// Benchmark steady-state insert (into first half of range) and remove (from
// second half of range), treating the container approximately like a queue with
// log-time access for all elements. This benchmark does not test the case where
// insertion and removal happen in the same region of the tree.  This benchmark
// counts two value constructors.
template <typename T>
void BM_QueueAddRem(benchmark::State& state) {
  using V = typename remove_pair_const<typename T::value_type>::type;
  typename KeyOfValue<typename T::key_type, V>::type key_of_value;

  ABSL_RAW_CHECK(kBenchmarkValues % 2 == 0, "for performance");

  T container;

  const size_t half = kBenchmarkValues / 2;
  std::vector<int> remove_keys(half);
  std::vector<int> add_keys(half);

  // We want to do the exact same work repeatedly, and the benchmark can end
  // after a different number of iterations depending on the speed of the
  // individual run so we use a large batch size here and ensure that we do
  // deterministic work every batch.
  while (state.KeepRunningBatch(half * kAddRemBatchSize)) {
    state.PauseTiming();

    container.clear();

    for (size_t i = 0; i < half; ++i) {
      remove_keys[i] = i;
      add_keys[i] = i;
    }
    constexpr int kSeed = 5;
    std::mt19937_64 rand(kSeed);
    std::shuffle(remove_keys.begin(), remove_keys.end(), rand);
    std::shuffle(add_keys.begin(), add_keys.end(), rand);

    // Note needs lazy generation of values.
    Generator<V> g(kBenchmarkValues * kAddRemBatchSize);

    for (size_t i = 0; i < half; ++i) {
      container.insert(g(add_keys[i]));
      container.insert(g(half + remove_keys[i]));
    }

    // There are three parts each of size "half":
    // 1 is being deleted from  [offset - half, offset)
    // 2 is standing            [offset, offset + half)
    // 3 is being inserted into [offset + half, offset + 2 * half)
    size_t offset = 0;

    for (size_t i = 0; i < kAddRemBatchSize; ++i) {
      std::shuffle(remove_keys.begin(), remove_keys.end(), rand);
      std::shuffle(add_keys.begin(), add_keys.end(), rand);
      offset += half;

      state.ResumeTiming();
      for (size_t idx = 0; idx < half; ++idx) {
        container.erase(key_of_value(g(offset - half + remove_keys[idx])));
        container.insert(g(offset + half + add_keys[idx]));
      }
      state.PauseTiming();
    }
    state.ResumeTiming();
  }
}

// Mixed insertion and deletion in the same range using pre-constructed values.
template <typename T>
void BM_MixedAddRem(benchmark::State& state) {
  using V = typename remove_pair_const<typename T::value_type>::type;
  typename KeyOfValue<typename T::key_type, V>::type key_of_value;

  ABSL_RAW_CHECK(kBenchmarkValues % 2 == 0, "for performance");

  T container;

  // Create two random shuffles
  std::vector<int> remove_keys(kBenchmarkValues);
  std::vector<int> add_keys(kBenchmarkValues);

  // We want to do the exact same work repeatedly, and the benchmark can end
  // after a different number of iterations depending on the speed of the
  // individual run so we use a large batch size here and ensure that we do
  // deterministic work every batch.
  while (state.KeepRunningBatch(kBenchmarkValues * kAddRemBatchSize)) {
    state.PauseTiming();

    container.clear();

    constexpr int kSeed = 7;
    std::mt19937_64 rand(kSeed);

    std::vector<V> values = GenerateValues<V>(kBenchmarkValues * 2);

    // Insert the first half of the values (already in random order)
    container.insert(values.begin(), values.begin() + kBenchmarkValues);

    // Insert the first half of the values (already in random order)
    for (size_t i = 0; i < kBenchmarkValues; ++i) {
      // remove_keys and add_keys will be swapped before each round,
      // therefore fill add_keys here w/ the keys being inserted, so
      // they'll be the first to be removed.
      remove_keys[i] = i + kBenchmarkValues;
      add_keys[i] = i;
    }

    for (size_t i = 0; i < kAddRemBatchSize; ++i) {
      remove_keys.swap(add_keys);
      std::shuffle(remove_keys.begin(), remove_keys.end(), rand);
      std::shuffle(add_keys.begin(), add_keys.end(), rand);

      state.ResumeTiming();
      for (size_t idx = 0; idx < kBenchmarkValues; ++idx) {
        container.erase(key_of_value(values[remove_keys[idx]]));
        container.insert(values[add_keys[idx]]);
      }
      state.PauseTiming();
    }
    state.ResumeTiming();
  }
}

// Insertion at end, removal from the beginning.  This benchmark
// counts two value constructors.
// TODO(ezb): we could add a GenerateNext version of generator that could reduce
// noise for string-like types.
template <typename T>
void BM_Fifo(benchmark::State& state) {
  using V = typename remove_pair_const<typename T::value_type>::type;

  T container;
  // Need lazy generation of values as state.max_iterations is large.
  Generator<V> g(kBenchmarkValues + state.max_iterations);

  for (int i = 0; i < kBenchmarkValues; i++) {
    container.insert(g(i));
  }

  while (state.KeepRunning()) {
    container.erase(container.begin());
    container.insert(container.end(), g(state.iterations() + kBenchmarkValues));
  }
}

// Iteration (forward) through the tree
template <typename T>
void BM_FwdIter(benchmark::State& state) {
  using V = typename remove_pair_const<typename T::value_type>::type;
  using R = typename T::value_type const*;

  std::vector<V> values = GenerateValues<V>(kBenchmarkValues);
  T container(values.begin(), values.end());

  auto iter = container.end();

  R r = nullptr;

  while (state.KeepRunning()) {
    if (iter == container.end()) iter = container.begin();
    r = &(*iter);
    ++iter;
  }

  benchmark::DoNotOptimize(r);
}

// Benchmark random range-construction of a container.
template <typename T>
void BM_RangeConstructionImpl(benchmark::State& state, bool sorted) {
  using V = typename remove_pair_const<typename T::value_type>::type;

  std::vector<V> values = GenerateValues<V>(kBenchmarkValues);
  if (sorted) {
    std::sort(values.begin(), values.end());
  }
  {
    T container(values.begin(), values.end());
  }

  while (state.KeepRunning()) {
    T container(values.begin(), values.end());
    benchmark::DoNotOptimize(container);
  }
}

template <typename T>
void BM_InsertRangeRandom(benchmark::State& state) {
  BM_RangeConstructionImpl<T>(state, false);
}

template <typename T>
void BM_InsertRangeSorted(benchmark::State& state) {
  BM_RangeConstructionImpl<T>(state, true);
}

#define STL_ORDERED_TYPES(value)                     \
  using stl_set_##value = std::set<value>;           \
  using stl_map_##value = std::map<value, intptr_t>; \
  using stl_multiset_##value = std::multiset<value>; \
  using stl_multimap_##value = std::multimap<value, intptr_t>

using StdString = std::string;
STL_ORDERED_TYPES(int32_t);
STL_ORDERED_TYPES(int64_t);
STL_ORDERED_TYPES(StdString);
STL_ORDERED_TYPES(Cord);
STL_ORDERED_TYPES(Time);

#define STL_UNORDERED_TYPES(value)                                       \
  using stl_unordered_set_##value = std::unordered_set<value>;           \
  using stl_unordered_map_##value = std::unordered_map<value, intptr_t>; \
  using flat_hash_set_##value = flat_hash_set<value>;                    \
  using flat_hash_map_##value = flat_hash_map<value, intptr_t>;          \
  using stl_unordered_multiset_##value = std::unordered_multiset<value>; \
  using stl_unordered_multimap_##value =                                 \
      std::unordered_multimap<value, intptr_t>

#define STL_UNORDERED_TYPES_CUSTOM_HASH(value, hash)                           \
  using stl_unordered_set_##value = std::unordered_set<value, hash>;           \
  using stl_unordered_map_##value = std::unordered_map<value, intptr_t, hash>; \
  using flat_hash_set_##value = flat_hash_set<value, hash>;                    \
  using flat_hash_map_##value = flat_hash_map<value, intptr_t, hash>;          \
  using stl_unordered_multiset_##value = std::unordered_multiset<value, hash>; \
  using stl_unordered_multimap_##value =                                       \
      std::unordered_multimap<value, intptr_t, hash>

STL_UNORDERED_TYPES_CUSTOM_HASH(Cord, absl::Hash<absl::Cord>);

STL_UNORDERED_TYPES(int32_t);
STL_UNORDERED_TYPES(int64_t);
STL_UNORDERED_TYPES(StdString);
STL_UNORDERED_TYPES_CUSTOM_HASH(Time, absl::Hash<absl::Time>);

#define BTREE_TYPES(value)                                            \
  using btree_256_set_##value =                                       \
      btree_set<value, std::less<value>, std::allocator<value>>;      \
  using btree_256_map_##value =                                       \
      btree_map<value, intptr_t, std::less<value>,                    \
                std::allocator<std::pair<const value, intptr_t>>>;    \
  using btree_256_multiset_##value =                                  \
      btree_multiset<value, std::less<value>, std::allocator<value>>; \
  using btree_256_multimap_##value =                                  \
      btree_multimap<value, intptr_t, std::less<value>,               \
                     std::allocator<std::pair<const value, intptr_t>>>

BTREE_TYPES(int32_t);
BTREE_TYPES(int64_t);
BTREE_TYPES(StdString);
BTREE_TYPES(Cord);
BTREE_TYPES(Time);

#define MY_BENCHMARK4(type, func)                                              \
  void BM_##type##_##func(benchmark::State& state) { BM_##func<type>(state); } \
  BENCHMARK(BM_##type##_##func)

#define MY_BENCHMARK3_STL(type)           \
  MY_BENCHMARK4(type, Insert);            \
  MY_BENCHMARK4(type, InsertSorted);      \
  MY_BENCHMARK4(type, InsertSmall);       \
  MY_BENCHMARK4(type, Lookup);            \
  MY_BENCHMARK4(type, FullLookup);        \
  MY_BENCHMARK4(type, Erase);             \
  MY_BENCHMARK4(type, EraseRange);        \
  MY_BENCHMARK4(type, QueueAddRem);       \
  MY_BENCHMARK4(type, MixedAddRem);       \
  MY_BENCHMARK4(type, Fifo);              \
  MY_BENCHMARK4(type, FwdIter);           \
  MY_BENCHMARK4(type, InsertRangeRandom); \
  MY_BENCHMARK4(type, InsertRangeSorted)

#define MY_BENCHMARK3(type)     \
  MY_BENCHMARK4(type, EraseIf); \
  MY_BENCHMARK3_STL(type)

#define MY_BENCHMARK2_SUPPORTS_MULTI_ONLY(type) \
  MY_BENCHMARK3_STL(stl_##type);                \
  MY_BENCHMARK3_STL(stl_unordered_##type);      \
  MY_BENCHMARK3(btree_256_##type)

#define MY_BENCHMARK2(type)                \
  MY_BENCHMARK2_SUPPORTS_MULTI_ONLY(type); \
  MY_BENCHMARK3(flat_hash_##type)

// Define MULTI_TESTING to see benchmarks for multi-containers also.
//
// You can use --copt=-DMULTI_TESTING.
#ifdef MULTI_TESTING
#define MY_BENCHMARK(type)                            \
  MY_BENCHMARK2(set_##type);                          \
  MY_BENCHMARK2(map_##type);                          \
  MY_BENCHMARK2_SUPPORTS_MULTI_ONLY(multiset_##type); \
  MY_BENCHMARK2_SUPPORTS_MULTI_ONLY(multimap_##type)
#else
#define MY_BENCHMARK(type)   \
  MY_BENCHMARK2(set_##type); \
  MY_BENCHMARK2(map_##type)
#endif

MY_BENCHMARK(int32_t);
MY_BENCHMARK(int64_t);
MY_BENCHMARK(StdString);
MY_BENCHMARK(Cord);
MY_BENCHMARK(Time);

// Define a type whose size and cost of moving are independently customizable.
// When sizeof(value_type) increases, we expect btree to no longer have as much
// cache-locality advantage over STL. When cost of moving increases, we expect
// btree to actually do more work than STL because it has to move values around
// and STL doesn't have to.
template <int Size, int Copies>
struct BigType {
  BigType() : BigType(0) {}
  explicit BigType(int x) { std::iota(values.begin(), values.end(), x); }

  void Copy(const BigType& other) {
    for (int i = 0; i < Size && i < Copies; ++i) values[i] = other.values[i];
    // If Copies > Size, do extra copies.
    for (int i = Size, idx = 0; i < Copies; ++i) {
      int64_t tmp = other.values[idx];
      benchmark::DoNotOptimize(tmp);
      idx = idx + 1 == Size ? 0 : idx + 1;
    }
  }

  BigType(const BigType& other) { Copy(other); }
  BigType& operator=(const BigType& other) {
    Copy(other);
    return *this;
  }

  // Compare only the first Copies elements if Copies is less than Size.
  bool operator<(const BigType& other) const {
    return std::lexicographical_compare(
        values.begin(), values.begin() + std::min(Size, Copies),
        other.values.begin(), other.values.begin() + std::min(Size, Copies));
  }
  bool operator==(const BigType& other) const {
    return std::equal(values.begin(), values.begin() + std::min(Size, Copies),
                      other.values.begin());
  }

  // Support absl::Hash.
  template <typename State>
  friend State AbslHashValue(State h, const BigType& b) {
    for (int i = 0; i < Size && i < Copies; ++i)
      h = State::combine(std::move(h), b.values[i]);
    return h;
  }

  std::array<int64_t, Size> values;
};

#define BIG_TYPE_BENCHMARKS(SIZE, COPIES)                                     \
  using stl_set_size##SIZE##copies##COPIES = std::set<BigType<SIZE, COPIES>>; \
  using stl_map_size##SIZE##copies##COPIES =                                  \
      std::map<BigType<SIZE, COPIES>, intptr_t>;                              \
  using stl_multiset_size##SIZE##copies##COPIES =                             \
      std::multiset<BigType<SIZE, COPIES>>;                                   \
  using stl_multimap_size##SIZE##copies##COPIES =                             \
      std::multimap<BigType<SIZE, COPIES>, intptr_t>;                         \
  using stl_unordered_set_size##SIZE##copies##COPIES =                        \
      std::unordered_set<BigType<SIZE, COPIES>,                               \
                         absl::Hash<BigType<SIZE, COPIES>>>;                  \
  using stl_unordered_map_size##SIZE##copies##COPIES =                        \
      std::unordered_map<BigType<SIZE, COPIES>, intptr_t,                     \
                         absl::Hash<BigType<SIZE, COPIES>>>;                  \
  using flat_hash_set_size##SIZE##copies##COPIES =                            \
      flat_hash_set<BigType<SIZE, COPIES>>;                                   \
  using flat_hash_map_size##SIZE##copies##COPIES =                            \
      flat_hash_map<BigType<SIZE, COPIES>, intptr_t>;                         \
  using stl_unordered_multiset_size##SIZE##copies##COPIES =                   \
      std::unordered_multiset<BigType<SIZE, COPIES>,                          \
                              absl::Hash<BigType<SIZE, COPIES>>>;             \
  using stl_unordered_multimap_size##SIZE##copies##COPIES =                   \
      std::unordered_multimap<BigType<SIZE, COPIES>, intptr_t,                \
                              absl::Hash<BigType<SIZE, COPIES>>>;             \
  using btree_256_set_size##SIZE##copies##COPIES =                            \
      btree_set<BigType<SIZE, COPIES>>;                                       \
  using btree_256_map_size##SIZE##copies##COPIES =                            \
      btree_map<BigType<SIZE, COPIES>, intptr_t>;                             \
  using btree_256_multiset_size##SIZE##copies##COPIES =                       \
      btree_multiset<BigType<SIZE, COPIES>>;                                  \
  using btree_256_multimap_size##SIZE##copies##COPIES =                       \
      btree_multimap<BigType<SIZE, COPIES>, intptr_t>;                        \
  MY_BENCHMARK(size##SIZE##copies##COPIES)

// Define BIG_TYPE_TESTING to see benchmarks for more big types.
//
// You can use --copt=-DBIG_TYPE_TESTING.
#ifndef NODESIZE_TESTING
#ifdef BIG_TYPE_TESTING
BIG_TYPE_BENCHMARKS(1, 4);
BIG_TYPE_BENCHMARKS(4, 1);
BIG_TYPE_BENCHMARKS(4, 4);
BIG_TYPE_BENCHMARKS(1, 8);
BIG_TYPE_BENCHMARKS(8, 1);
BIG_TYPE_BENCHMARKS(8, 8);
BIG_TYPE_BENCHMARKS(1, 16);
BIG_TYPE_BENCHMARKS(16, 1);
BIG_TYPE_BENCHMARKS(16, 16);
BIG_TYPE_BENCHMARKS(1, 32);
BIG_TYPE_BENCHMARKS(32, 1);
BIG_TYPE_BENCHMARKS(32, 32);
#else
BIG_TYPE_BENCHMARKS(32, 32);
#endif
#endif

// Benchmark using unique_ptrs to large value types. In order to be able to use
// the same benchmark code as the other types, use a type that holds a
// unique_ptr and has a copy constructor.
template <int Size>
struct BigTypePtr {
  BigTypePtr() : BigTypePtr(0) {}
  explicit BigTypePtr(int x) {
    ptr = absl::make_unique<BigType<Size, Size>>(x);
  }
  BigTypePtr(const BigTypePtr& other) {
    ptr = absl::make_unique<BigType<Size, Size>>(*other.ptr);
  }
  BigTypePtr(BigTypePtr&& other) noexcept = default;
  BigTypePtr& operator=(const BigTypePtr& other) {
    ptr = absl::make_unique<BigType<Size, Size>>(*other.ptr);
  }
  BigTypePtr& operator=(BigTypePtr&& other) noexcept = default;

  bool operator<(const BigTypePtr& other) const { return *ptr < *other.ptr; }
  bool operator==(const BigTypePtr& other) const { return *ptr == *other.ptr; }

  std::unique_ptr<BigType<Size, Size>> ptr;
};

template <int Size>
double ContainerInfo(const btree_set<BigTypePtr<Size>>& b) {
  const double bytes_used =
      b.bytes_used() + b.size() * sizeof(BigType<Size, Size>);
  const double bytes_per_value = bytes_used / b.size();
  BtreeContainerInfoLog(b, bytes_used, bytes_per_value);
  return bytes_per_value;
}
template <int Size>
double ContainerInfo(const btree_map<int, BigTypePtr<Size>>& b) {
  const double bytes_used =
      b.bytes_used() + b.size() * sizeof(BigType<Size, Size>);
  const double bytes_per_value = bytes_used / b.size();
  BtreeContainerInfoLog(b, bytes_used, bytes_per_value);
  return bytes_per_value;
}

#define BIG_TYPE_PTR_BENCHMARKS(SIZE)                                          \
  using stl_set_size##SIZE##copies##SIZE##ptr = std::set<BigType<SIZE, SIZE>>; \
  using stl_map_size##SIZE##copies##SIZE##ptr =                                \
      std::map<int, BigType<SIZE, SIZE>>;                                      \
  using stl_unordered_set_size##SIZE##copies##SIZE##ptr =                      \
      std::unordered_set<BigType<SIZE, SIZE>,                                  \
                         absl::Hash<BigType<SIZE, SIZE>>>;                     \
  using stl_unordered_map_size##SIZE##copies##SIZE##ptr =                      \
      std::unordered_map<int, BigType<SIZE, SIZE>>;                            \
  using flat_hash_set_size##SIZE##copies##SIZE##ptr =                          \
      flat_hash_set<BigType<SIZE, SIZE>>;                                      \
  using flat_hash_map_size##SIZE##copies##SIZE##ptr =                          \
      flat_hash_map<int, BigTypePtr<SIZE>>;                                    \
  using btree_256_set_size##SIZE##copies##SIZE##ptr =                          \
      btree_set<BigTypePtr<SIZE>>;                                             \
  using btree_256_map_size##SIZE##copies##SIZE##ptr =                          \
      btree_map<int, BigTypePtr<SIZE>>;                                        \
  MY_BENCHMARK3_STL(stl_set_size##SIZE##copies##SIZE##ptr);                    \
  MY_BENCHMARK3_STL(stl_unordered_set_size##SIZE##copies##SIZE##ptr);          \
  MY_BENCHMARK3(flat_hash_set_size##SIZE##copies##SIZE##ptr);                  \
  MY_BENCHMARK3(btree_256_set_size##SIZE##copies##SIZE##ptr);                  \
  MY_BENCHMARK3_STL(stl_map_size##SIZE##copies##SIZE##ptr);                    \
  MY_BENCHMARK3_STL(stl_unordered_map_size##SIZE##copies##SIZE##ptr);          \
  MY_BENCHMARK3(flat_hash_map_size##SIZE##copies##SIZE##ptr);                  \
  MY_BENCHMARK3(btree_256_map_size##SIZE##copies##SIZE##ptr)

BIG_TYPE_PTR_BENCHMARKS(32);

void BM_BtreeSet_IteratorSubtraction(benchmark::State& state) {
  absl::InsecureBitGen bitgen;
  std::vector<int> vec;
  // Randomize the set's insertion order so the nodes aren't all full.
  vec.reserve(state.range(0));
  for (int i = 0; i < state.range(0); ++i) vec.push_back(i);
  absl::c_shuffle(vec, bitgen);

  absl::btree_set<int> set;
  for (int i : vec) set.insert(i);

  size_t distance = absl::Uniform(bitgen, 0u, set.size());
  while (state.KeepRunningBatch(distance)) {
    size_t end = absl::Uniform(bitgen, distance, set.size());
    size_t begin = end - distance;
    benchmark::DoNotOptimize(set.find(static_cast<int>(end)) -
                             set.find(static_cast<int>(begin)));
    distance = absl::Uniform(bitgen, 0u, set.size());
  }
}

BENCHMARK(BM_BtreeSet_IteratorSubtraction)->Range(1 << 10, 1 << 20);

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
