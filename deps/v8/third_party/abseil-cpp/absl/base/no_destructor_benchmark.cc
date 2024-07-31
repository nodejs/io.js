// Copyright 2023 The Abseil Authors.
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

#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/no_destructor.h"
#include "benchmark/benchmark.h"

namespace {

// Number of static-NoDestructor-in-a-function to exercise.
// This must be low enough not to hit template instantiation limits
// (happens around 1000).
constexpr int kNumObjects = 1;  // set to 512 when doing benchmarks
                                // 1 is faster to compile: just one templated
                                // function instantiation

// Size of individual objects to benchmark static-NoDestructor-in-a-function
// usage with.
constexpr int kObjSize = sizeof(void*)*1;

// Simple object of kObjSize bytes (rounded to int).
// We benchmark complete reading of its state via Verify().
class BM_Blob {
 public:
  BM_Blob(int val) { for (auto& d : data_) d = val; }
  BM_Blob() : BM_Blob(-1) {}
  void Verify(int val) const {  // val must be the c-tor argument
    for (auto& d : data_) ABSL_INTERNAL_CHECK(d == val, "");
  }
 private:
  int data_[kObjSize / sizeof(int) > 0 ? kObjSize / sizeof(int) : 1];
};

// static-NoDestructor-in-a-function pattern instances.
// We'll instantiate kNumObjects of them.
template<int i>
const BM_Blob& NoDestrBlobFunc() {
  static absl::NoDestructor<BM_Blob> x(i);
  return *x;
}

// static-heap-ptr-in-a-function pattern instances
// We'll instantiate kNumObjects of them.
template<int i>
const BM_Blob& OnHeapBlobFunc() {
  static BM_Blob* x = new BM_Blob(i);
  return *x;
}

// Type for NoDestrBlobFunc or OnHeapBlobFunc.
typedef const BM_Blob& (*FuncType)();

// ========================================================================= //
// Simple benchmarks that read a single BM_Blob over and over, hence
// all they touch fits into L1 CPU cache:

// Direct non-POD global variable (style guide violation) as a baseline.
static BM_Blob direct_blob(0);

void BM_Direct(benchmark::State& state) {
  for (auto s : state) {
    direct_blob.Verify(0);
  }
}
BENCHMARK(BM_Direct);

void BM_NoDestr(benchmark::State& state) {
  for (auto s : state) {
    NoDestrBlobFunc<0>().Verify(0);
  }
}
BENCHMARK(BM_NoDestr);

void BM_OnHeap(benchmark::State& state) {
  for (auto s : state) {
    OnHeapBlobFunc<0>().Verify(0);
  }
}
BENCHMARK(BM_OnHeap);

// ========================================================================= //
// Benchmarks that read kNumObjects of BM_Blob over and over, hence with
// appropriate values of sizeof(BM_Blob) and kNumObjects their working set
// can exceed a given layer of CPU cache.

// Type of benchmark to select between NoDestrBlobFunc and OnHeapBlobFunc.
enum BM_Type { kNoDestr, kOnHeap, kDirect };

// BlobFunc<n>(t, i) returns the i-th function of type t.
// n must be larger than i (we'll use kNumObjects for n).
template<int n>
FuncType BlobFunc(BM_Type t, int i) {
  if (i == n) {
    switch (t) {
      case kNoDestr:  return &NoDestrBlobFunc<n>;
      case kOnHeap:   return &OnHeapBlobFunc<n>;
      case kDirect:   return nullptr;
    }
  }
  return BlobFunc<n-1>(t, i);
}

template<>
FuncType BlobFunc<0>(BM_Type t, int i) {
  ABSL_INTERNAL_CHECK(i == 0, "");
  switch (t) {
    case kNoDestr:  return &NoDestrBlobFunc<0>;
    case kOnHeap:   return &OnHeapBlobFunc<0>;
    case kDirect:   return nullptr;
  }
  return nullptr;
}

// Direct non-POD global variables (style guide violation) as a baseline.
static BM_Blob direct_blobs[kNumObjects];

// Helper that cheaply maps benchmark iteration to randomish index in
// [0, kNumObjects).
int RandIdx(int i) {
  // int64 is to avoid overflow and generating negative return values:
  return (static_cast<int64_t>(i) * 13) % kNumObjects;
}

// Generic benchmark working with kNumObjects for any of the possible BM_Type.
template <BM_Type t>
void BM_Many(benchmark::State& state) {
  FuncType funcs[kNumObjects];
  for (int i = 0; i < kNumObjects; ++i) {
    funcs[i] = BlobFunc<kNumObjects-1>(t, i);
  }
  if (t == kDirect) {
    for (auto s : state) {
      int idx = RandIdx(state.iterations());
      direct_blobs[idx].Verify(-1);
    }
  } else {
    for (auto s : state) {
      int idx = RandIdx(state.iterations());
      funcs[idx]().Verify(idx);
    }
  }
}

void BM_DirectMany(benchmark::State& state) { BM_Many<kDirect>(state); }
void BM_NoDestrMany(benchmark::State& state) { BM_Many<kNoDestr>(state); }
void BM_OnHeapMany(benchmark::State& state) { BM_Many<kOnHeap>(state); }

BENCHMARK(BM_DirectMany);
BENCHMARK(BM_NoDestrMany);
BENCHMARK(BM_OnHeapMany);

}  // namespace
