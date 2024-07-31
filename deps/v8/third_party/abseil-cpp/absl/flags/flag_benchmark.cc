//
// Copyright 2020 The Abseil Authors.
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

#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/marshalling.h"
#include "absl/flags/parse.h"
#include "absl/flags/reflection.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "benchmark/benchmark.h"

namespace {
using String = std::string;
using VectorOfStrings = std::vector<std::string>;
using AbslDuration = absl::Duration;

// We do not want to take over marshalling for the types absl::optional<int>,
// absl::optional<std::string> which we do not own. Instead we introduce unique
// "aliases" to these types, which we do.
using AbslOptionalInt = absl::optional<int>;
struct OptionalInt : AbslOptionalInt {
  using AbslOptionalInt::AbslOptionalInt;
};
// Next two functions represent Abseil Flags marshalling for OptionalInt.
bool AbslParseFlag(absl::string_view src, OptionalInt* flag,
                   std::string* error) {
  int val;
  if (src.empty())
    flag->reset();
  else if (!absl::ParseFlag(src, &val, error))
    return false;
  *flag = val;
  return true;
}
std::string AbslUnparseFlag(const OptionalInt& flag) {
  return !flag ? "" : absl::UnparseFlag(*flag);
}

using AbslOptionalString = absl::optional<std::string>;
struct OptionalString : AbslOptionalString {
  using AbslOptionalString::AbslOptionalString;
};
// Next two functions represent Abseil Flags marshalling for OptionalString.
bool AbslParseFlag(absl::string_view src, OptionalString* flag,
                   std::string* error) {
  std::string val;
  if (src.empty())
    flag->reset();
  else if (!absl::ParseFlag(src, &val, error))
    return false;
  *flag = val;
  return true;
}
std::string AbslUnparseFlag(const OptionalString& flag) {
  return !flag ? "" : absl::UnparseFlag(*flag);
}

struct UDT {
  UDT() = default;
  UDT(const UDT&) {}
  UDT& operator=(const UDT&) { return *this; }
};
// Next two functions represent Abseil Flags marshalling for UDT.
bool AbslParseFlag(absl::string_view, UDT*, std::string*) { return true; }
std::string AbslUnparseFlag(const UDT&) { return ""; }

}  // namespace

#define BENCHMARKED_TYPES(A) \
  A(bool)                    \
  A(int16_t)                 \
  A(uint16_t)                \
  A(int32_t)                 \
  A(uint32_t)                \
  A(int64_t)                 \
  A(uint64_t)                \
  A(double)                  \
  A(float)                   \
  A(String)                  \
  A(VectorOfStrings)         \
  A(OptionalInt)             \
  A(OptionalString)          \
  A(AbslDuration)            \
  A(UDT)

#define REPLICATE_0(A, T, name, index) A(T, name, index)
#define REPLICATE_1(A, T, name, index) \
  REPLICATE_0(A, T, name, index##0) REPLICATE_0(A, T, name, index##1)
#define REPLICATE_2(A, T, name, index) \
  REPLICATE_1(A, T, name, index##0) REPLICATE_1(A, T, name, index##1)
#define REPLICATE_3(A, T, name, index) \
  REPLICATE_2(A, T, name, index##0) REPLICATE_2(A, T, name, index##1)
#define REPLICATE_4(A, T, name, index) \
  REPLICATE_3(A, T, name, index##0) REPLICATE_3(A, T, name, index##1)
#define REPLICATE_5(A, T, name, index) \
  REPLICATE_4(A, T, name, index##0) REPLICATE_4(A, T, name, index##1)
#define REPLICATE_6(A, T, name, index) \
  REPLICATE_5(A, T, name, index##0) REPLICATE_5(A, T, name, index##1)
#define REPLICATE_7(A, T, name, index) \
  REPLICATE_6(A, T, name, index##0) REPLICATE_6(A, T, name, index##1)
#define REPLICATE_8(A, T, name, index) \
  REPLICATE_7(A, T, name, index##0) REPLICATE_7(A, T, name, index##1)
#define REPLICATE_9(A, T, name, index) \
  REPLICATE_8(A, T, name, index##0) REPLICATE_8(A, T, name, index##1)
#if defined(_MSC_VER)
#define REPLICATE(A, T, name) \
  REPLICATE_7(A, T, name, 0) REPLICATE_7(A, T, name, 1)
#define SINGLE_FLAG(T) FLAGS_##T##_flag_00000000
#else
#define REPLICATE(A, T, name) \
  REPLICATE_9(A, T, name, 0) REPLICATE_9(A, T, name, 1)
#define SINGLE_FLAG(T) FLAGS_##T##_flag_0000000000
#endif
#define REPLICATE_ALL(A, T, name) \
  REPLICATE_9(A, T, name, 0) REPLICATE_9(A, T, name, 1)

#define COUNT(T, name, index) +1
constexpr size_t kNumFlags = 0 REPLICATE(COUNT, _, _);

#if defined(__clang__) && defined(__linux__)
// Force the flags used for benchmarks into a separate ELF section.
// This ensures that, even when other parts of the code might change size,
// the layout of the flags across cachelines is kept constant. This makes
// benchmark results more reproducible across unrelated code changes.
#pragma clang section data = ".benchmark_flags"
#endif
#define DEFINE_FLAG(T, name, index) ABSL_FLAG(T, name##_##index, {}, "");
#define FLAG_DEF(T) REPLICATE(DEFINE_FLAG, T, T##_flag);
BENCHMARKED_TYPES(FLAG_DEF)
#if defined(__clang__) && defined(__linux__)
#pragma clang section data = ""
#endif
// Register thousands of flags to bloat up the size of the registry.
// This mimics real life production binaries.
#define BLOAT_FLAG(_unused1, _unused2, index) \
  ABSL_FLAG(int, bloat_flag_##index, 0, "");
REPLICATE_ALL(BLOAT_FLAG, _, _)

namespace {

#define FLAG_PTR(T, name, index) &FLAGS_##name##_##index,
#define FLAG_PTR_ARR(T)                              \
  static constexpr absl::Flag<T>* FlagPtrs_##T[] = { \
      REPLICATE(FLAG_PTR, T, T##_flag)};
BENCHMARKED_TYPES(FLAG_PTR_ARR)

#define BM_SingleGetFlag(T)                                    \
  void BM_SingleGetFlag_##T(benchmark::State& state) {         \
    for (auto _ : state) {                                     \
      benchmark::DoNotOptimize(absl::GetFlag(SINGLE_FLAG(T))); \
    }                                                          \
  }                                                            \
  BENCHMARK(BM_SingleGetFlag_##T)->ThreadRange(1, 16);

BENCHMARKED_TYPES(BM_SingleGetFlag)

template <typename T>
struct Accumulator {
  using type = T;
};
template <>
struct Accumulator<String> {
  using type = size_t;
};
template <>
struct Accumulator<VectorOfStrings> {
  using type = size_t;
};
template <>
struct Accumulator<OptionalInt> {
  using type = bool;
};
template <>
struct Accumulator<OptionalString> {
  using type = bool;
};
template <>
struct Accumulator<UDT> {
  using type = bool;
};

template <typename T>
void Accumulate(typename Accumulator<T>::type& a, const T& f) {
  a += f;
}
void Accumulate(bool& a, bool f) { a = a || f; }
void Accumulate(size_t& a, const std::string& f) { a += f.size(); }
void Accumulate(size_t& a, const std::vector<std::string>& f) { a += f.size(); }
void Accumulate(bool& a, const OptionalInt& f) { a |= f.has_value(); }
void Accumulate(bool& a, const OptionalString& f) { a |= f.has_value(); }
void Accumulate(bool& a, const UDT& f) {
  a |= reinterpret_cast<int64_t>(&f) & 0x1;
}

#define BM_ManyGetFlag(T)                            \
  void BM_ManyGetFlag_##T(benchmark::State& state) { \
    Accumulator<T>::type res = {};                   \
    while (state.KeepRunningBatch(kNumFlags)) {      \
      for (auto* flag_ptr : FlagPtrs_##T) {          \
        Accumulate(res, absl::GetFlag(*flag_ptr));   \
      }                                              \
    }                                                \
    benchmark::DoNotOptimize(res);                   \
  }                                                  \
  BENCHMARK(BM_ManyGetFlag_##T)->ThreadRange(1, 8);

BENCHMARKED_TYPES(BM_ManyGetFlag)

void BM_ThreadedFindCommandLineFlag(benchmark::State& state) {
  char dummy[] = "dummy";
  char* argv[] = {dummy};
  // We need to ensure that flags have been parsed. That is where the registry
  // is finalized.
  absl::ParseCommandLine(1, argv);

  while (state.KeepRunningBatch(kNumFlags)) {
    for (auto* flag_ptr : FlagPtrs_bool) {
      benchmark::DoNotOptimize(absl::FindCommandLineFlag(flag_ptr->Name()));
    }
  }
}
BENCHMARK(BM_ThreadedFindCommandLineFlag)->ThreadRange(1, 16);

}  // namespace

#ifdef __llvm__
// To view disassembly use: gdb ${BINARY}  -batch -ex "disassemble /s $FUNC"
#define InvokeGetFlag(T)                                             \
  T AbslInvokeGetFlag##T() { return absl::GetFlag(SINGLE_FLAG(T)); } \
  int odr##T = (benchmark::DoNotOptimize(AbslInvokeGetFlag##T), 1);

BENCHMARKED_TYPES(InvokeGetFlag)
#endif  // __llvm__
