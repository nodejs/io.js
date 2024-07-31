// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "include/cppgc/platform.h"
#include "include/libplatform/libplatform.h"
#include "include/v8-initialization.h"
#include "src/base/compiler-specific.h"
#include "src/base/page-allocator.h"
#include "testing/gmock/include/gmock/gmock.h"

#ifdef V8_ENABLE_FUZZTEST
#include "test/unittests/fuzztest-init-adapter.h"
#endif  // V8_ENABLE_FUZZTEST

#ifdef V8_USE_PERFETTO
#include "src/tracing/trace-event.h"
#endif  // V8_USE_PERFETTO

namespace {

class CppGCEnvironment final : public ::testing::Environment {
 public:
  void SetUp() override {
    // Initialize the process for cppgc with an arbitrary page allocator. This
    // has to survive as long as the process, so it's ok to leak the allocator
    // here.
    cppgc::InitializeProcess(new v8::base::PageAllocator());

#ifdef V8_USE_PERFETTO
    // Set up the in-process perfetto backend.
    perfetto::TracingInitArgs init_args;
    init_args.backends = perfetto::BackendType::kInProcessBackend;
    perfetto::Tracing::Initialize(init_args);
#endif  // V8_USE_PERFETTO
  }

  void TearDown() override { cppgc::ShutdownProcess(); }
};

}  // namespace


int main(int argc, char** argv) {
  // Don't catch SEH exceptions and continue as the following tests might hang
  // in an broken environment on windows.
  GTEST_FLAG_SET(catch_exceptions, false);

  // Most V8 unit-tests are multi-threaded, so enable thread-safe death-tests.
  GTEST_FLAG_SET(death_test_style, "threadsafe");

  testing::InitGoogleMock(&argc, argv);
  testing::AddGlobalTestEnvironment(new CppGCEnvironment);
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  v8::V8::InitializeExternalStartupData(argv[0]);
  v8::V8::InitializeICUDefaultLocation(argv[0]);

#ifdef V8_ENABLE_FUZZTEST
  absl::ParseCommandLine(argc, argv);
  fuzztest::InitFuzzTest(&argc, &argv);
#endif  // V8_ENABLE_FUZZTEST

  return RUN_ALL_TESTS();
}
