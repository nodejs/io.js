// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_BENCHMARK_CPP_CPPGC_BENCHMARK_UTILS_H_
#define TEST_BENCHMARK_CPP_CPPGC_BENCHMARK_UTILS_H_

#include "include/cppgc/heap.h"
#include "test/unittests/heap/cppgc/test-platform.h"
#include "third_party/google_benchmark_chrome/src/include/benchmark/benchmark.h"

namespace cppgc {
namespace internal {
namespace testing {

class BenchmarkWithHeap : public benchmark::Fixture {
 public:
  static void InitializeProcess();
  static void ShutdownProcess();

 protected:
  void SetUp(::benchmark::State& state) override {
    heap_ = cppgc::Heap::Create(GetPlatform());
  }

  void TearDown(::benchmark::State& state) override { heap_.reset(); }

  cppgc::Heap& heap() const { return *heap_.get(); }

 private:
  static std::shared_ptr<testing::TestPlatform> GetPlatform() {
    return platform_;
  }

  static std::shared_ptr<testing::TestPlatform> platform_;

  std::unique_ptr<cppgc::Heap> heap_;
};

}  // namespace testing
}  // namespace internal
}  // namespace cppgc

#endif  // TEST_BENCHMARK_CPP_CPPGC_BENCHMARK_UTILS_H_
