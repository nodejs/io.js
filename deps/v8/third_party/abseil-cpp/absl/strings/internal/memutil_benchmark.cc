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

#include "absl/strings/internal/memutil.h"

#include <algorithm>
#include <cstdlib>

#include "benchmark/benchmark.h"
#include "absl/strings/ascii.h"

// We fill the haystack with aaaaaaaaaaaaaaaaaa...aaaab.
// That gives us:
// - an easy search: 'b'
// - a medium search: 'ab'.  That means every letter is a possible match.
// - a pathological search: 'aaaaaa.......aaaaab' (half as many a's as haytack)

namespace {

constexpr int kHaystackSize = 10000;
constexpr int64_t kHaystackSize64 = kHaystackSize;
const char* MakeHaystack() {
  char* haystack = new char[kHaystackSize];
  for (int i = 0; i < kHaystackSize - 1; ++i) haystack[i] = 'a';
  haystack[kHaystackSize - 1] = 'b';
  return haystack;
}
const char* const kHaystack = MakeHaystack();

bool case_eq(const char a, const char b) {
  return absl::ascii_tolower(a) == absl::ascii_tolower(b);
}

void BM_Searchcase(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(kHaystack, kHaystack + kHaystackSize,
                                         kHaystack + kHaystackSize - 1,
                                         kHaystack + kHaystackSize, case_eq));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_Searchcase);

void BM_SearchcaseMedium(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(kHaystack, kHaystack + kHaystackSize,
                                         kHaystack + kHaystackSize - 2,
                                         kHaystack + kHaystackSize, case_eq));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_SearchcaseMedium);

void BM_SearchcasePathological(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::search(kHaystack, kHaystack + kHaystackSize,
                                         kHaystack + kHaystackSize / 2,
                                         kHaystack + kHaystackSize, case_eq));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_SearchcasePathological);

char* memcasechr(const char* s, int c, size_t slen) {
  c = absl::ascii_tolower(c);
  for (; slen; ++s, --slen) {
    if (absl::ascii_tolower(*s) == c) return const_cast<char*>(s);
  }
  return nullptr;
}

const char* memcasematch(const char* phaystack, size_t haylen,
                         const char* pneedle, size_t neelen) {
  if (0 == neelen) {
    return phaystack;  // even if haylen is 0
  }
  if (haylen < neelen) return nullptr;

  const char* match;
  const char* hayend = phaystack + haylen - neelen + 1;
  while ((match = static_cast<char*>(
              memcasechr(phaystack, pneedle[0], hayend - phaystack)))) {
    if (absl::strings_internal::memcasecmp(match, pneedle, neelen) == 0)
      return match;
    else
      phaystack = match + 1;
  }
  return nullptr;
}

void BM_Memcasematch(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasematch(kHaystack, kHaystackSize, "b", 1));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_Memcasematch);

void BM_MemcasematchMedium(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasematch(kHaystack, kHaystackSize, "ab", 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemcasematchMedium);

void BM_MemcasematchPathological(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(memcasematch(kHaystack, kHaystackSize,
                                          kHaystack + kHaystackSize / 2,
                                          kHaystackSize - kHaystackSize / 2));
  }
  state.SetBytesProcessed(kHaystackSize64 * state.iterations());
}
BENCHMARK(BM_MemcasematchPathological);

}  // namespace
