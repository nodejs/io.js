// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TEST_BENCHMARK_CPP_BENCHMARK_UTILS_H_
#define TEST_BENCHMARK_CPP_BENCHMARK_UTILS_H_

#include "include/v8-array-buffer.h"
#include "include/v8-cppgc.h"
#include "include/v8-isolate.h"
#include "include/v8-platform.h"
#include "third_party/google_benchmark_chrome/src/include/benchmark/benchmark.h"

namespace v8::benchmarking {

static constexpr uint16_t kEmbedderId = 0;
static constexpr size_t kTypeOffset = 0;
static constexpr size_t kInstanceOffset = 1;

// BenchmarkWithIsolate is a basic benchmark fixture that sets up the process
// with a single Isolate.
class BenchmarkWithIsolate : public benchmark::Fixture {
 public:
  static void InitializeProcess();
  static void ShutdownProcess();

 protected:
  V8_INLINE v8::Isolate* v8_isolate() { return v8_isolate_; }
  V8_INLINE cppgc::AllocationHandle& allocation_handle() {
    return v8_isolate_->GetCppHeap()->GetAllocationHandle();
  }

 private:
  static v8::Platform* platform_;
  static v8::Isolate* v8_isolate_;
  static v8::ArrayBuffer::Allocator* v8_ab_allocator_;
};

}  // namespace v8::benchmarking

#endif  // TEST_BENCHMARK_CPP_BENCHMARK_UTILS_H_
