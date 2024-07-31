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

#include "absl/strings/string_view.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "benchmark/benchmark.h"
#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/macros.h"
#include "absl/strings/str_cat.h"

namespace {

void BM_StringViewFromString(benchmark::State& state) {
  std::string s(state.range(0), 'x');
  std::string* ps = &s;
  struct SV {
    SV() = default;
    explicit SV(const std::string& s) : sv(s) {}
    absl::string_view sv;
  } sv;
  SV* psv = &sv;
  benchmark::DoNotOptimize(ps);
  benchmark::DoNotOptimize(psv);
  for (auto _ : state) {
    new (psv) SV(*ps);
    benchmark::DoNotOptimize(sv);
  }
}
BENCHMARK(BM_StringViewFromString)->Arg(12)->Arg(128);

// Provide a forcibly out-of-line wrapper for operator== that can be used in
// benchmarks to measure the impact of inlining.
ABSL_ATTRIBUTE_NOINLINE
bool NonInlinedEq(absl::string_view a, absl::string_view b) { return a == b; }

// We use functions that cannot be inlined to perform the comparison loops so
// that inlining of the operator== can't optimize away *everything*.
ABSL_ATTRIBUTE_NOINLINE
void DoEqualityComparisons(benchmark::State& state, absl::string_view a,
                           absl::string_view b) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(a == b);
  }
}

void BM_EqualIdentical(benchmark::State& state) {
  std::string x(state.range(0), 'a');
  DoEqualityComparisons(state, x, x);
}
BENCHMARK(BM_EqualIdentical)->DenseRange(0, 3)->Range(4, 1 << 10);

void BM_EqualSame(benchmark::State& state) {
  std::string x(state.range(0), 'a');
  std::string y = x;
  DoEqualityComparisons(state, x, y);
}
BENCHMARK(BM_EqualSame)
    ->DenseRange(0, 10)
    ->Arg(20)
    ->Arg(40)
    ->Arg(70)
    ->Arg(110)
    ->Range(160, 4096);

void BM_EqualDifferent(benchmark::State& state) {
  const int len = state.range(0);
  std::string x(len, 'a');
  std::string y = x;
  if (len > 0) {
    y[len - 1] = 'b';
  }
  DoEqualityComparisons(state, x, y);
}
BENCHMARK(BM_EqualDifferent)->DenseRange(0, 3)->Range(4, 1 << 10);

// This benchmark is intended to check that important simplifications can be
// made with absl::string_view comparisons against constant strings. The idea is
// that if constant strings cause redundant components of the comparison, the
// compiler should detect and eliminate them. Here we use 8 different strings,
// each with the same size. Provided our comparison makes the implementation
// inline-able by the compiler, it should fold all of these away into a single
// size check once per loop iteration.
ABSL_ATTRIBUTE_NOINLINE
void DoConstantSizeInlinedEqualityComparisons(benchmark::State& state,
                                              absl::string_view a) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(a == "aaa");
    benchmark::DoNotOptimize(a == "bbb");
    benchmark::DoNotOptimize(a == "ccc");
    benchmark::DoNotOptimize(a == "ddd");
    benchmark::DoNotOptimize(a == "eee");
    benchmark::DoNotOptimize(a == "fff");
    benchmark::DoNotOptimize(a == "ggg");
    benchmark::DoNotOptimize(a == "hhh");
  }
}
void BM_EqualConstantSizeInlined(benchmark::State& state) {
  std::string x(state.range(0), 'a');
  DoConstantSizeInlinedEqualityComparisons(state, x);
}
// We only need to check for size of 3, and <> 3 as this benchmark only has to
// do with size differences.
BENCHMARK(BM_EqualConstantSizeInlined)->DenseRange(2, 4);

// This benchmark exists purely to give context to the above timings: this is
// what they would look like if the compiler is completely unable to simplify
// between two comparisons when they are comparing against constant strings.
ABSL_ATTRIBUTE_NOINLINE
void DoConstantSizeNonInlinedEqualityComparisons(benchmark::State& state,
                                                 absl::string_view a) {
  for (auto _ : state) {
    // Force these out-of-line to compare with the above function.
    benchmark::DoNotOptimize(NonInlinedEq(a, "aaa"));
    benchmark::DoNotOptimize(NonInlinedEq(a, "bbb"));
    benchmark::DoNotOptimize(NonInlinedEq(a, "ccc"));
    benchmark::DoNotOptimize(NonInlinedEq(a, "ddd"));
    benchmark::DoNotOptimize(NonInlinedEq(a, "eee"));
    benchmark::DoNotOptimize(NonInlinedEq(a, "fff"));
    benchmark::DoNotOptimize(NonInlinedEq(a, "ggg"));
    benchmark::DoNotOptimize(NonInlinedEq(a, "hhh"));
  }
}

void BM_EqualConstantSizeNonInlined(benchmark::State& state) {
  std::string x(state.range(0), 'a');
  DoConstantSizeNonInlinedEqualityComparisons(state, x);
}
// We only need to check for size of 3, and <> 3 as this benchmark only has to
// do with size differences.
BENCHMARK(BM_EqualConstantSizeNonInlined)->DenseRange(2, 4);

void BM_CompareSame(benchmark::State& state) {
  const int len = state.range(0);
  std::string x;
  for (int i = 0; i < len; i++) {
    x += 'a';
  }
  std::string y = x;
  absl::string_view a = x;
  absl::string_view b = y;

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    benchmark::DoNotOptimize(a.compare(b));
  }
}
BENCHMARK(BM_CompareSame)->DenseRange(0, 3)->Range(4, 1 << 10);

void BM_CompareFirstOneLess(benchmark::State& state) {
  const int len = state.range(0);
  std::string x(len, 'a');
  std::string y = x;
  y.back() = 'b';
  absl::string_view a = x;
  absl::string_view b = y;

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    benchmark::DoNotOptimize(a.compare(b));
  }
}
BENCHMARK(BM_CompareFirstOneLess)->DenseRange(1, 3)->Range(4, 1 << 10);

void BM_CompareSecondOneLess(benchmark::State& state) {
  const int len = state.range(0);
  std::string x(len, 'a');
  std::string y = x;
  x.back() = 'b';
  absl::string_view a = x;
  absl::string_view b = y;

  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    benchmark::DoNotOptimize(a.compare(b));
  }
}
BENCHMARK(BM_CompareSecondOneLess)->DenseRange(1, 3)->Range(4, 1 << 10);

void BM_find_string_view_len_one(benchmark::State& state) {
  std::string haystack(state.range(0), '0');
  absl::string_view s(haystack);
  for (auto _ : state) {
    benchmark::DoNotOptimize(s.find("x"));  // not present; length 1
  }
}
BENCHMARK(BM_find_string_view_len_one)->Range(1, 1 << 20);

void BM_find_string_view_len_two(benchmark::State& state) {
  std::string haystack(state.range(0), '0');
  absl::string_view s(haystack);
  for (auto _ : state) {
    benchmark::DoNotOptimize(s.find("xx"));  // not present; length 2
  }
}
BENCHMARK(BM_find_string_view_len_two)->Range(1, 1 << 20);

void BM_find_one_char(benchmark::State& state) {
  std::string haystack(state.range(0), '0');
  absl::string_view s(haystack);
  for (auto _ : state) {
    benchmark::DoNotOptimize(s.find('x'));  // not present
  }
}
BENCHMARK(BM_find_one_char)->Range(1, 1 << 20);

void BM_rfind_one_char(benchmark::State& state) {
  std::string haystack(state.range(0), '0');
  absl::string_view s(haystack);
  for (auto _ : state) {
    benchmark::DoNotOptimize(s.rfind('x'));  // not present
  }
}
BENCHMARK(BM_rfind_one_char)->Range(1, 1 << 20);

void BM_worst_case_find_first_of(benchmark::State& state, int haystack_len) {
  const int needle_len = state.range(0);
  std::string needle;
  for (int i = 0; i < needle_len; ++i) {
    needle += 'a' + i;
  }
  std::string haystack(haystack_len, '0');  // 1000 zeros.

  absl::string_view s(haystack);
  for (auto _ : state) {
    benchmark::DoNotOptimize(s.find_first_of(needle));
  }
}

void BM_find_first_of_short(benchmark::State& state) {
  BM_worst_case_find_first_of(state, 10);
}

void BM_find_first_of_medium(benchmark::State& state) {
  BM_worst_case_find_first_of(state, 100);
}

void BM_find_first_of_long(benchmark::State& state) {
  BM_worst_case_find_first_of(state, 1000);
}

BENCHMARK(BM_find_first_of_short)->DenseRange(0, 4)->Arg(8)->Arg(16)->Arg(32);
BENCHMARK(BM_find_first_of_medium)->DenseRange(0, 4)->Arg(8)->Arg(16)->Arg(32);
BENCHMARK(BM_find_first_of_long)->DenseRange(0, 4)->Arg(8)->Arg(16)->Arg(32);

struct EasyMap : public std::map<absl::string_view, uint64_t> {
  explicit EasyMap(size_t) {}
};

// This templated benchmark helper function is intended to stress operator== or
// operator< in a realistic test.  It surely isn't entirely realistic, but it's
// a start.  The test creates a map of type Map, a template arg, and populates
// it with table_size key/value pairs. Each key has WordsPerKey words.  After
// creating the map, a number of lookups are done in random order.  Some keys
// are used much more frequently than others in this phase of the test.
template <typename Map, int WordsPerKey>
void StringViewMapBenchmark(benchmark::State& state) {
  const int table_size = state.range(0);
  const double kFractionOfKeysThatAreHot = 0.2;
  const int kNumLookupsOfHotKeys = 20;
  const int kNumLookupsOfColdKeys = 1;
  const char* words[] = {"the",   "quick",  "brown",    "fox",      "jumped",
                         "over",  "the",    "lazy",     "dog",      "and",
                         "found", "a",      "large",    "mushroom", "and",
                         "a",     "couple", "crickets", "eating",   "pie"};
  // Create some keys that consist of words in random order.
  std::random_device r;
  std::seed_seq seed({r(), r(), r(), r(), r(), r(), r(), r()});
  std::mt19937 rng(seed);
  std::vector<std::string> keys(table_size);
  std::vector<int> all_indices;
  const int kBlockSize = 1 << 12;
  std::unordered_set<std::string> t(kBlockSize);
  std::uniform_int_distribution<int> uniform(0, ABSL_ARRAYSIZE(words) - 1);
  for (int i = 0; i < table_size; i++) {
    all_indices.push_back(i);
    do {
      keys[i].clear();
      for (int j = 0; j < WordsPerKey; j++) {
        absl::StrAppend(&keys[i], j > 0 ? " " : "", words[uniform(rng)]);
      }
    } while (!t.insert(keys[i]).second);
  }

  // Create a list of strings to lookup: a permutation of the array of
  // keys we just created, with repeats.  "Hot" keys get repeated more.
  std::shuffle(all_indices.begin(), all_indices.end(), rng);
  const int num_hot = table_size * kFractionOfKeysThatAreHot;
  const int num_cold = table_size - num_hot;
  std::vector<int> hot_indices(all_indices.begin(),
                               all_indices.begin() + num_hot);
  std::vector<int> indices;
  for (int i = 0; i < kNumLookupsOfColdKeys; i++) {
    indices.insert(indices.end(), all_indices.begin(), all_indices.end());
  }
  for (int i = 0; i < kNumLookupsOfHotKeys - kNumLookupsOfColdKeys; i++) {
    indices.insert(indices.end(), hot_indices.begin(), hot_indices.end());
  }
  std::shuffle(indices.begin(), indices.end(), rng);
  ABSL_RAW_CHECK(
      num_cold * kNumLookupsOfColdKeys + num_hot * kNumLookupsOfHotKeys ==
          indices.size(),
      "");
  // After constructing the array we probe it with absl::string_views built from
  // test_strings.  This means operator== won't see equal pointers, so
  // it'll have to check for equal lengths and equal characters.
  std::vector<std::string> test_strings(indices.size());
  for (int i = 0; i < indices.size(); i++) {
    test_strings[i] = keys[indices[i]];
  }

  // Run the benchmark. It includes map construction but is mostly
  // map lookups.
  for (auto _ : state) {
    Map h(table_size);
    for (int i = 0; i < table_size; i++) {
      h[keys[i]] = i * 2;
    }
    ABSL_RAW_CHECK(h.size() == table_size, "");
    uint64_t sum = 0;
    for (int i = 0; i < indices.size(); i++) {
      sum += h[test_strings[i]];
    }
    benchmark::DoNotOptimize(sum);
  }
}

void BM_StdMap_4(benchmark::State& state) {
  StringViewMapBenchmark<EasyMap, 4>(state);
}
BENCHMARK(BM_StdMap_4)->Range(1 << 10, 1 << 16);

void BM_StdMap_8(benchmark::State& state) {
  StringViewMapBenchmark<EasyMap, 8>(state);
}
BENCHMARK(BM_StdMap_8)->Range(1 << 10, 1 << 16);

void BM_CopyToStringNative(benchmark::State& state) {
  std::string src(state.range(0), 'x');
  absl::string_view sv(src);
  std::string dst;
  for (auto _ : state) {
    dst.assign(sv.begin(), sv.end());
  }
}
BENCHMARK(BM_CopyToStringNative)->Range(1 << 3, 1 << 12);

void BM_AppendToStringNative(benchmark::State& state) {
  std::string src(state.range(0), 'x');
  absl::string_view sv(src);
  std::string dst;
  for (auto _ : state) {
    dst.clear();
    dst.insert(dst.end(), sv.begin(), sv.end());
  }
}
BENCHMARK(BM_AppendToStringNative)->Range(1 << 3, 1 << 12);

}  // namespace
