// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdlib.h>
#include <string.h>

#if V8_OS_POSIX
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "src/init/v8.h"

#include "test/cctest/cctest.h"

using v8::internal::AccountingAllocator;

using v8::IdleTask;
using v8::Isolate;
using v8::Task;

#include "src/base/platform/platform.h"
#include "src/utils/allocation.h"
#include "src/zone/accounting-allocator.h"

// ASAN isn't configured to return nullptr, so skip all of these tests.
#if !defined(V8_USE_ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER) && \
    !defined(THREAD_SANITIZER)

namespace {

// Implementation of v8::Platform that can register OOM callbacks.
class AllocationPlatform : public TestPlatform {
 public:
  AllocationPlatform() { current_platform = this; }

  void OnCriticalMemoryPressure() override { oom_callback_called = true; }

  static AllocationPlatform* current_platform;
  bool oom_callback_called = false;
};

AllocationPlatform* AllocationPlatform::current_platform = nullptr;

bool DidCallOnCriticalMemoryPressure() {
  return AllocationPlatform::current_platform &&
         AllocationPlatform::current_platform->oom_callback_called;
}

// No OS should be able to malloc/new this number of bytes. Generate enough
// random values in the address space to get a very large fraction of it. Using
// even larger values is that overflow from rounding or padding can cause the
// allocations to succeed somehow.
size_t GetHugeMemoryAmount() {
  static size_t huge_memory = 0;
  if (!huge_memory) {
    for (int i = 0; i < 100; i++) {
      huge_memory |=
          reinterpret_cast<size_t>(v8::internal::GetRandomMmapAddr());
    }
    // Make it larger than the available address space.
    huge_memory *= 2;
    CHECK_NE(0, huge_memory);
  }
  return huge_memory;
}

void OnMallocedOperatorNewOOM(const char* location, const char* message) {
  // exit(0) if the OOM callback was called and location matches expectation.
  CHECK(DidCallOnCriticalMemoryPressure());
  CHECK_EQ(0, strcmp(location, "Malloced operator new"));
  v8::base::OS::ExitProcess(0);
}

void OnNewArrayOOM(const char* location, const char* message) {
  // exit(0) if the OOM callback was called and location matches expectation.
  CHECK(DidCallOnCriticalMemoryPressure());
  CHECK_EQ(0, strcmp(location, "NewArray"));
  v8::base::OS::ExitProcess(0);
}

void OnAlignedAllocOOM(const char* location, const char* message) {
  // exit(0) if the OOM callback was called and location matches expectation.
  CHECK(DidCallOnCriticalMemoryPressure());
  CHECK_EQ(0, strcmp(location, "AlignedAlloc"));
  v8::base::OS::ExitProcess(0);
}

}  // namespace

TEST_WITH_PLATFORM(AccountingAllocatorOOM, AllocationPlatform) {
  v8::internal::AccountingAllocator allocator;
  CHECK(!platform.oom_callback_called);
  const bool support_compression = false;
  v8::internal::Segment* result =
      allocator.AllocateSegment(GetHugeMemoryAmount(), support_compression);
  // On a few systems, allocation somehow succeeds.
  CHECK_EQ(result == nullptr, platform.oom_callback_called);
}

// We use |AllocateAtLeast| in the accounting allocator, so we check only that
// we have _at least_ the expected amount of memory allocated.
TEST_WITH_PLATFORM(AccountingAllocatorCurrentAndMax, AllocationPlatform) {
  v8::internal::AccountingAllocator allocator;
  static constexpr size_t kAllocationSizes[] = {51, 231, 27};
  std::vector<v8::internal::Segment*> segments;
  const bool support_compression = false;
  CHECK_EQ(0, allocator.GetCurrentMemoryUsage());
  CHECK_EQ(0, allocator.GetMaxMemoryUsage());
  size_t expected_current = 0;
  size_t expected_max = 0;
  for (size_t size : kAllocationSizes) {
    segments.push_back(allocator.AllocateSegment(size, support_compression));
    CHECK_NOT_NULL(segments.back());
    CHECK_LE(size, segments.back()->total_size());
    expected_current += segments.back()->total_size();
    if (expected_current > expected_max) expected_max = expected_current;
    CHECK_EQ(expected_current, allocator.GetCurrentMemoryUsage());
    CHECK_EQ(expected_max, allocator.GetMaxMemoryUsage());
  }
  for (auto* segment : segments) {
    expected_current -= segment->total_size();
    allocator.ReturnSegment(segment, support_compression);
    CHECK_EQ(expected_current, allocator.GetCurrentMemoryUsage());
  }
  CHECK_EQ(expected_max, allocator.GetMaxMemoryUsage());
  CHECK_EQ(0, allocator.GetCurrentMemoryUsage());
  CHECK(!platform.oom_callback_called);
}

TEST_WITH_PLATFORM(MallocedOperatorNewOOM, AllocationPlatform) {
  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnMallocedOperatorNewOOM);
  // On failure, this won't return, since a Malloced::New failure is fatal.
  // In that case, behavior is checked in OnMallocedOperatorNewOOM before exit.
  void* result = v8::internal::Malloced::operator new(GetHugeMemoryAmount());
  CHECK_EQ(result == nullptr, platform.oom_callback_called);
}

TEST_WITH_PLATFORM(NewArrayOOM, AllocationPlatform) {
  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnNewArrayOOM);
  // On failure, this won't return, since a NewArray failure is fatal.
  // In that case, behavior is checked in OnNewArrayOOM before exit.
  void* result = v8::internal::NewArray<int8_t>(GetHugeMemoryAmount());
  CHECK_EQ(result == nullptr, platform.oom_callback_called);
}

TEST_WITH_PLATFORM(AlignedAllocOOM, AllocationPlatform) {
  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnAlignedAllocOOM);
  // On failure, this won't return, since an AlignedAlloc failure is fatal.
  // In that case, behavior is checked in OnAlignedAllocOOM before exit.
  void* result = v8::internal::AlignedAllocWithRetry(
      GetHugeMemoryAmount(), v8::internal::AllocatePageSize());
  CHECK_EQ(result == nullptr, platform.oom_callback_called);
}

TEST_WITH_PLATFORM(AllocVirtualMemoryOOM, AllocationPlatform) {
  CHECK(!platform.oom_callback_called);
  v8::internal::VirtualMemory result(v8::internal::GetPlatformPageAllocator(),
                                     GetHugeMemoryAmount(), nullptr);
  // On a few systems, allocation somehow succeeds.
  CHECK_IMPLIES(!result.IsReserved(), platform.oom_callback_called);
}

TEST_WITH_PLATFORM(AlignedAllocVirtualMemoryOOM, AllocationPlatform) {
  CHECK(!platform.oom_callback_called);
  v8::internal::VirtualMemory result(v8::internal::GetPlatformPageAllocator(),
                                     GetHugeMemoryAmount(), nullptr,
                                     v8::internal::AllocatePageSize());
  // On a few systems, allocation somehow succeeds.
  CHECK_IMPLIES(!result.IsReserved(), platform.oom_callback_called);
}

#endif  // !defined(V8_USE_ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER) &&
        // !defined(THREAD_SANITIZER)
