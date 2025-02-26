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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/random/random.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

namespace {

using absl::Hash;

template <template <typename> class H, typename T>
void RunBenchmark(benchmark::State& state, T value) {
  H<T> h;
  for (auto _ : state) {
    benchmark::DoNotOptimize(value);
    benchmark::DoNotOptimize(h(value));
  }
}

}  // namespace

template <typename T>
using AbslHash = absl::Hash<T>;

class TypeErasedInterface {
 public:
  virtual ~TypeErasedInterface() = default;

  template <typename H>
  friend H AbslHashValue(H state, const TypeErasedInterface& wrapper) {
    state = H::combine(std::move(state), std::type_index(typeid(wrapper)));
    wrapper.HashValue(absl::HashState::Create(&state));
    return state;
  }

 private:
  virtual void HashValue(absl::HashState state) const = 0;
};

template <typename T>
struct TypeErasedAbslHash {
  class Wrapper : public TypeErasedInterface {
   public:
    explicit Wrapper(const T& value) : value_(value) {}

   private:
    void HashValue(absl::HashState state) const override {
      absl::HashState::combine(std::move(state), value_);
    }

    const T& value_;
  };

  size_t operator()(const T& value) {
    return absl::Hash<Wrapper>{}(Wrapper(value));
  }
};

absl::Cord FlatCord(size_t size) {
  absl::Cord result(std::string(size, 'a'));
  result.Flatten();
  return result;
}

absl::Cord FragmentedCord(size_t size) {
  const size_t orig_size = size;
  std::vector<std::string> chunks;
  size_t chunk_size = std::max<size_t>(1, size / 10);
  while (size > chunk_size) {
    chunks.push_back(std::string(chunk_size, 'a'));
    size -= chunk_size;
  }
  if (size > 0) {
    chunks.push_back(std::string(size, 'a'));
  }
  absl::Cord result = absl::MakeFragmentedCord(chunks);
  (void) orig_size;
  assert(result.size() == orig_size);
  return result;
}

template <typename T>
std::vector<T> Vector(size_t count) {
  std::vector<T> result;
  for (size_t v = 0; v < count; ++v) {
    result.push_back(v);
  }
  return result;
}

// Bogus type that replicates an unorderd_set's bit mixing, but with
// vector-speed iteration. This is intended to measure the overhead of unordered
// hashing without counting the speed of unordered_set iteration.
template <typename T>
struct FastUnorderedSet {
  explicit FastUnorderedSet(size_t count) {
    for (size_t v = 0; v < count; ++v) {
      values.push_back(v);
    }
  }
  std::vector<T> values;

  template <typename H>
  friend H AbslHashValue(H h, const FastUnorderedSet& fus) {
    return H::combine(H::combine_unordered(std::move(h), fus.values.begin(),
                                           fus.values.end()),
                      fus.values.size());
  }
};

template <typename T>
absl::flat_hash_set<T> FlatHashSet(size_t count) {
  absl::flat_hash_set<T> result;
  for (size_t v = 0; v < count; ++v) {
    result.insert(v);
  }
  return result;
}

template <typename T>
struct LongCombine {
  T a[200]{};
  template <typename H>
  friend H AbslHashValue(H state, const LongCombine& v) {
    // This is testing a single call to `combine` with a lot of arguments to
    // test the performance of the folding logic.
    return H::combine(
        std::move(state),  //
        v.a[0], v.a[1], v.a[2], v.a[3], v.a[4], v.a[5], v.a[6], v.a[7], v.a[8],
        v.a[9], v.a[10], v.a[11], v.a[12], v.a[13], v.a[14], v.a[15], v.a[16],
        v.a[17], v.a[18], v.a[19], v.a[20], v.a[21], v.a[22], v.a[23], v.a[24],
        v.a[25], v.a[26], v.a[27], v.a[28], v.a[29], v.a[30], v.a[31], v.a[32],
        v.a[33], v.a[34], v.a[35], v.a[36], v.a[37], v.a[38], v.a[39], v.a[40],
        v.a[41], v.a[42], v.a[43], v.a[44], v.a[45], v.a[46], v.a[47], v.a[48],
        v.a[49], v.a[50], v.a[51], v.a[52], v.a[53], v.a[54], v.a[55], v.a[56],
        v.a[57], v.a[58], v.a[59], v.a[60], v.a[61], v.a[62], v.a[63], v.a[64],
        v.a[65], v.a[66], v.a[67], v.a[68], v.a[69], v.a[70], v.a[71], v.a[72],
        v.a[73], v.a[74], v.a[75], v.a[76], v.a[77], v.a[78], v.a[79], v.a[80],
        v.a[81], v.a[82], v.a[83], v.a[84], v.a[85], v.a[86], v.a[87], v.a[88],
        v.a[89], v.a[90], v.a[91], v.a[92], v.a[93], v.a[94], v.a[95], v.a[96],
        v.a[97], v.a[98], v.a[99], v.a[100], v.a[101], v.a[102], v.a[103],
        v.a[104], v.a[105], v.a[106], v.a[107], v.a[108], v.a[109], v.a[110],
        v.a[111], v.a[112], v.a[113], v.a[114], v.a[115], v.a[116], v.a[117],
        v.a[118], v.a[119], v.a[120], v.a[121], v.a[122], v.a[123], v.a[124],
        v.a[125], v.a[126], v.a[127], v.a[128], v.a[129], v.a[130], v.a[131],
        v.a[132], v.a[133], v.a[134], v.a[135], v.a[136], v.a[137], v.a[138],
        v.a[139], v.a[140], v.a[141], v.a[142], v.a[143], v.a[144], v.a[145],
        v.a[146], v.a[147], v.a[148], v.a[149], v.a[150], v.a[151], v.a[152],
        v.a[153], v.a[154], v.a[155], v.a[156], v.a[157], v.a[158], v.a[159],
        v.a[160], v.a[161], v.a[162], v.a[163], v.a[164], v.a[165], v.a[166],
        v.a[167], v.a[168], v.a[169], v.a[170], v.a[171], v.a[172], v.a[173],
        v.a[174], v.a[175], v.a[176], v.a[177], v.a[178], v.a[179], v.a[180],
        v.a[181], v.a[182], v.a[183], v.a[184], v.a[185], v.a[186], v.a[187],
        v.a[188], v.a[189], v.a[190], v.a[191], v.a[192], v.a[193], v.a[194],
        v.a[195], v.a[196], v.a[197], v.a[198], v.a[199]);
  }
};

template <typename T>
auto MakeLongTuple() {
  auto t1 = std::tuple<T>();
  auto t2 = std::tuple_cat(t1, t1);
  auto t3 = std::tuple_cat(t2, t2);
  auto t4 = std::tuple_cat(t3, t3);
  auto t5 = std::tuple_cat(t4, t4);
  auto t6 = std::tuple_cat(t5, t5);
  // Ideally this would be much larger, but some configurations can't handle
  // making tuples with that many elements. They break inside std::tuple itself.
  static_assert(std::tuple_size<decltype(t6)>::value == 32, "");
  return t6;
}

// Generates a benchmark and a codegen method for the provided types.  The
// codegen method provides a well known entrypoint for dumping assembly.
#define MAKE_BENCHMARK(hash, name, ...)                          \
  namespace {                                                    \
  void BM_##hash##_##name(benchmark::State& state) {             \
    RunBenchmark<hash>(state, __VA_ARGS__);                      \
  }                                                              \
  BENCHMARK(BM_##hash##_##name);                                 \
  }                                                              \
  size_t Codegen##hash##name(const decltype(__VA_ARGS__)& arg);  \
  size_t Codegen##hash##name(const decltype(__VA_ARGS__)& arg) { \
    return hash<decltype(__VA_ARGS__)>{}(arg);                   \
  }                                                              \
  bool absl_hash_test_odr_use##hash##name =                      \
      (benchmark::DoNotOptimize(&Codegen##hash##name), false)

MAKE_BENCHMARK(AbslHash, Int32, int32_t{});
MAKE_BENCHMARK(AbslHash, Int64, int64_t{});
MAKE_BENCHMARK(AbslHash, Double, 1.2);
MAKE_BENCHMARK(AbslHash, DoubleZero, 0.0);
MAKE_BENCHMARK(AbslHash, PairInt32Int32, std::pair<int32_t, int32_t>{});
MAKE_BENCHMARK(AbslHash, PairInt64Int64, std::pair<int64_t, int64_t>{});
MAKE_BENCHMARK(AbslHash, TupleInt32BoolInt64,
               std::tuple<int32_t, bool, int64_t>{});
MAKE_BENCHMARK(AbslHash, String_0, std::string());
MAKE_BENCHMARK(AbslHash, String_1, std::string(1, 'a'));
MAKE_BENCHMARK(AbslHash, String_2, std::string(2, 'a'));
MAKE_BENCHMARK(AbslHash, String_4, std::string(4, 'a'));
MAKE_BENCHMARK(AbslHash, String_8, std::string(8, 'a'));
MAKE_BENCHMARK(AbslHash, String_10, std::string(10, 'a'));
MAKE_BENCHMARK(AbslHash, String_30, std::string(30, 'a'));
MAKE_BENCHMARK(AbslHash, String_90, std::string(90, 'a'));
MAKE_BENCHMARK(AbslHash, String_200, std::string(200, 'a'));
MAKE_BENCHMARK(AbslHash, String_5000, std::string(5000, 'a'));
MAKE_BENCHMARK(AbslHash, Cord_Flat_0, absl::Cord());
MAKE_BENCHMARK(AbslHash, Cord_Flat_10, FlatCord(10));
MAKE_BENCHMARK(AbslHash, Cord_Flat_30, FlatCord(30));
MAKE_BENCHMARK(AbslHash, Cord_Flat_90, FlatCord(90));
MAKE_BENCHMARK(AbslHash, Cord_Flat_200, FlatCord(200));
MAKE_BENCHMARK(AbslHash, Cord_Flat_5000, FlatCord(5000));
MAKE_BENCHMARK(AbslHash, Cord_Fragmented_200, FragmentedCord(200));
MAKE_BENCHMARK(AbslHash, Cord_Fragmented_5000, FragmentedCord(5000));
MAKE_BENCHMARK(AbslHash, VectorInt64_10, Vector<int64_t>(10));
MAKE_BENCHMARK(AbslHash, VectorInt64_100, Vector<int64_t>(100));
MAKE_BENCHMARK(AbslHash, VectorInt64_1000, Vector<int64_t>(1000));
MAKE_BENCHMARK(AbslHash, VectorDouble_10, Vector<double>(10));
MAKE_BENCHMARK(AbslHash, VectorDouble_100, Vector<double>(100));
MAKE_BENCHMARK(AbslHash, VectorDouble_1000, Vector<double>(1000));
MAKE_BENCHMARK(AbslHash, FlatHashSetInt64_10, FlatHashSet<int64_t>(10));
MAKE_BENCHMARK(AbslHash, FlatHashSetInt64_100, FlatHashSet<int64_t>(100));
MAKE_BENCHMARK(AbslHash, FlatHashSetInt64_1000, FlatHashSet<int64_t>(1000));
MAKE_BENCHMARK(AbslHash, FlatHashSetDouble_10, FlatHashSet<double>(10));
MAKE_BENCHMARK(AbslHash, FlatHashSetDouble_100, FlatHashSet<double>(100));
MAKE_BENCHMARK(AbslHash, FlatHashSetDouble_1000, FlatHashSet<double>(1000));
MAKE_BENCHMARK(AbslHash, FastUnorderedSetInt64_1000,
               FastUnorderedSet<int64_t>(1000));
MAKE_BENCHMARK(AbslHash, FastUnorderedSetDouble_1000,
               FastUnorderedSet<double>(1000));
MAKE_BENCHMARK(AbslHash, PairStringString_0,
               std::make_pair(std::string(), std::string()));
MAKE_BENCHMARK(AbslHash, PairStringString_10,
               std::make_pair(std::string(10, 'a'), std::string(10, 'b')));
MAKE_BENCHMARK(AbslHash, PairStringString_30,
               std::make_pair(std::string(30, 'a'), std::string(30, 'b')));
MAKE_BENCHMARK(AbslHash, PairStringString_90,
               std::make_pair(std::string(90, 'a'), std::string(90, 'b')));
MAKE_BENCHMARK(AbslHash, PairStringString_200,
               std::make_pair(std::string(200, 'a'), std::string(200, 'b')));
MAKE_BENCHMARK(AbslHash, PairStringString_5000,
               std::make_pair(std::string(5000, 'a'), std::string(5000, 'b')));
MAKE_BENCHMARK(AbslHash, LongTupleInt32, MakeLongTuple<int>());
MAKE_BENCHMARK(AbslHash, LongTupleString, MakeLongTuple<std::string>());
MAKE_BENCHMARK(AbslHash, LongCombineInt32, LongCombine<int>());
MAKE_BENCHMARK(AbslHash, LongCombineString, LongCombine<std::string>());

MAKE_BENCHMARK(TypeErasedAbslHash, Int32, int32_t{});
MAKE_BENCHMARK(TypeErasedAbslHash, Int64, int64_t{});
MAKE_BENCHMARK(TypeErasedAbslHash, PairInt32Int32,
               std::pair<int32_t, int32_t>{});
MAKE_BENCHMARK(TypeErasedAbslHash, PairInt64Int64,
               std::pair<int64_t, int64_t>{});
MAKE_BENCHMARK(TypeErasedAbslHash, TupleInt32BoolInt64,
               std::tuple<int32_t, bool, int64_t>{});
MAKE_BENCHMARK(TypeErasedAbslHash, String_0, std::string());
MAKE_BENCHMARK(TypeErasedAbslHash, String_10, std::string(10, 'a'));
MAKE_BENCHMARK(TypeErasedAbslHash, String_30, std::string(30, 'a'));
MAKE_BENCHMARK(TypeErasedAbslHash, String_90, std::string(90, 'a'));
MAKE_BENCHMARK(TypeErasedAbslHash, String_200, std::string(200, 'a'));
MAKE_BENCHMARK(TypeErasedAbslHash, String_5000, std::string(5000, 'a'));
MAKE_BENCHMARK(TypeErasedAbslHash, VectorDouble_10,
               std::vector<double>(10, 1.1));
MAKE_BENCHMARK(TypeErasedAbslHash, VectorDouble_100,
               std::vector<double>(100, 1.1));
MAKE_BENCHMARK(TypeErasedAbslHash, VectorDouble_1000,
               std::vector<double>(1000, 1.1));
MAKE_BENCHMARK(TypeErasedAbslHash, FlatHashSetInt64_10,
               FlatHashSet<int64_t>(10));
MAKE_BENCHMARK(TypeErasedAbslHash, FlatHashSetInt64_100,
               FlatHashSet<int64_t>(100));
MAKE_BENCHMARK(TypeErasedAbslHash, FlatHashSetInt64_1000,
               FlatHashSet<int64_t>(1000));
MAKE_BENCHMARK(TypeErasedAbslHash, FlatHashSetDouble_10,
               FlatHashSet<double>(10));
MAKE_BENCHMARK(TypeErasedAbslHash, FlatHashSetDouble_100,
               FlatHashSet<double>(100));
MAKE_BENCHMARK(TypeErasedAbslHash, FlatHashSetDouble_1000,
               FlatHashSet<double>(1000));
MAKE_BENCHMARK(TypeErasedAbslHash, FastUnorderedSetInt64_1000,
               FastUnorderedSet<int64_t>(1000));
MAKE_BENCHMARK(TypeErasedAbslHash, FastUnorderedSetDouble_1000,
               FastUnorderedSet<double>(1000));

// The latency benchmark attempts to model the speed of the hash function in
// production. When a hash function is used for hashtable lookups it is rarely
// used to hash N items in a tight loop nor on constant sized strings. Instead,
// after hashing there is a potential equality test plus a (usually) large
// amount of user code. To simulate this effectively we introduce a data
// dependency between elements we hash by using the hash of the Nth element as
// the selector of the N+1th element to hash. This isolates the hash function
// code much like in production. As a bonus we use the hash to generate strings
// of size [1,N] (instead of fixed N) to disable perfect branch predictions in
// hash function implementations.
namespace {
// 16kb fits in L1 cache of most CPUs we care about. Keeping memory latency low
// will allow us to attribute most time to CPU which means more accurate
// measurements.
static constexpr size_t kEntropySize = 16 << 10;
static char entropy[kEntropySize + 1024];
ABSL_ATTRIBUTE_UNUSED static const bool kInitialized = [] {
  absl::BitGen gen;
  static_assert(sizeof(entropy) % sizeof(uint64_t) == 0, "");
  for (int i = 0; i != sizeof(entropy); i += sizeof(uint64_t)) {
    auto rand = absl::Uniform<uint64_t>(gen);
    memcpy(&entropy[i], &rand, sizeof(uint64_t));
  }
  return true;
}();
}  // namespace

template <class T>
struct PodRand {
  static_assert(std::is_pod<T>::value, "");
  static_assert(kEntropySize + sizeof(T) < sizeof(entropy), "");

  T Get(size_t i) const {
    T v;
    memcpy(&v, &entropy[i % kEntropySize], sizeof(T));
    return v;
  }
};

template <size_t N>
struct StringRand {
  static_assert(kEntropySize + N < sizeof(entropy), "");

  absl::string_view Get(size_t i) const {
    // This has a small bias towards small numbers. Because max N is ~200 this
    // is very small and prefer to be very fast instead of absolutely accurate.
    // Also we pass N = 2^K+1 so that mod reduces to a bitand.
    size_t s = (i % (N - 1)) + 1;
    return {&entropy[i % kEntropySize], s};
  }
};

#define MAKE_LATENCY_BENCHMARK(hash, name, ...)              \
  namespace {                                                \
  void BM_latency_##hash##_##name(benchmark::State& state) { \
    __VA_ARGS__ r;                                           \
    hash<decltype(r.Get(0))> h;                              \
    size_t i = 871401241;                                    \
    for (auto _ : state) {                                   \
      benchmark::DoNotOptimize(i = h(r.Get(i)));             \
    }                                                        \
  }                                                          \
  BENCHMARK(BM_latency_##hash##_##name);                     \
  }  // namespace

MAKE_LATENCY_BENCHMARK(AbslHash, Int32, PodRand<int32_t>)
MAKE_LATENCY_BENCHMARK(AbslHash, Int64, PodRand<int64_t>)
MAKE_LATENCY_BENCHMARK(AbslHash, String9, StringRand<9>)
MAKE_LATENCY_BENCHMARK(AbslHash, String33, StringRand<33>)
MAKE_LATENCY_BENCHMARK(AbslHash, String65, StringRand<65>)
MAKE_LATENCY_BENCHMARK(AbslHash, String257, StringRand<257>)
