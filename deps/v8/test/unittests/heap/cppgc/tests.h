// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_HEAP_CPPGC_TESTS_H_
#define V8_UNITTESTS_HEAP_CPPGC_TESTS_H_

#include "include/cppgc/heap-consistency.h"
#include "include/cppgc/heap.h"
#include "include/cppgc/platform.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/trace-event.h"
#include "test/unittests/heap/cppgc/test-platform.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {
namespace testing {
class DelegatingTracingController : public TracingController {
 public:
#if !defined(V8_USE_PERFETTO)
  const uint8_t* GetCategoryGroupEnabled(const char* name) override {
    static const std::string disabled_by_default_tag =
        TRACE_DISABLED_BY_DEFAULT("");
    static uint8_t yes = 1;
    static uint8_t no = 0;
    if (strncmp(name, disabled_by_default_tag.c_str(),
                disabled_by_default_tag.length()) == 0) {
      return &no;
    }
    return &yes;
  }

  uint64_t AddTraceEvent(
      char phase, const uint8_t* category_enabled_flag, const char* name,
      const char* scope, uint64_t id, uint64_t bind_id, int32_t num_args,
      const char** arg_names, const uint8_t* arg_types,
      const uint64_t* arg_values,
      std::unique_ptr<ConvertableToTraceFormat>* arg_convertables,
      unsigned int flags) override {
    return tracing_controller_->AddTraceEvent(
        phase, category_enabled_flag, name, scope, id, bind_id, num_args,
        arg_names, arg_types, arg_values, arg_convertables, flags);
  }
#endif  // !defined(V8_USE_PERFETTO)

  void SetTracingController(
      std::unique_ptr<TracingController> tracing_controller_impl) {
    tracing_controller_ = std::move(tracing_controller_impl);
  }

 private:
  std::unique_ptr<TracingController> tracing_controller_ =
      std::make_unique<TracingController>();
};

class TestWithPlatform : public ::testing::Test {
 public:
  static void SetUpTestSuite();
  static void TearDownTestSuite();

  TestPlatform& GetPlatform() const { return *platform_; }

  std::shared_ptr<TestPlatform> GetPlatformHandle() const { return platform_; }

  void SetTracingController(
      std::unique_ptr<TracingController> tracing_controller_impl) {
    static_cast<DelegatingTracingController*>(platform_->GetTracingController())
        ->SetTracingController(std::move(tracing_controller_impl));
  }

 protected:
  static std::shared_ptr<TestPlatform> platform_;
};

class TestWithHeap : public TestWithPlatform {
 public:
  TestWithHeap();
  ~TestWithHeap() override;

  void PreciseGC() {
    heap_->ForceGarbageCollectionSlow(
        ::testing::UnitTest::GetInstance()->current_test_info()->name(),
        "Testing", cppgc::Heap::StackState::kNoHeapPointers);
  }

  void ConservativeGC() {
    heap_->ForceGarbageCollectionSlow(
        ::testing::UnitTest::GetInstance()->current_test_info()->name(),
        "Testing", cppgc::Heap::StackState::kMayContainHeapPointers);
  }

  // GC that also discards unused memory and thus changes the resident size
  // size of the heap and corresponding pages.
  void ConservativeMemoryDiscardingGC() {
    internal::Heap::From(GetHeap())->CollectGarbage(
        {CollectionType::kMajor, Heap::StackState::kMayContainHeapPointers,
         cppgc::Heap::MarkingType::kAtomic, cppgc::Heap::SweepingType::kAtomic,
         GCConfig::FreeMemoryHandling::kDiscardWherePossible});
  }

  cppgc::Heap* GetHeap() const { return heap_.get(); }

  cppgc::AllocationHandle& GetAllocationHandle() const {
    return allocation_handle_;
  }

  cppgc::HeapHandle& GetHeapHandle() const {
    return GetHeap()->GetHeapHandle();
  }

  std::unique_ptr<MarkerBase>& GetMarkerRef() {
    return Heap::From(GetHeap())->GetMarkerRefForTesting();
  }

  void ResetLinearAllocationBuffers();

 private:
  std::unique_ptr<cppgc::Heap> heap_;
  cppgc::AllocationHandle& allocation_handle_;
};

// Restrictive test fixture that supports allocation but will make sure no
// garbage collection is triggered. This is useful for writing idiomatic
// tests where object are allocated on the managed heap while still avoiding
// far reaching test consequences of full garbage collection calls.
class TestSupportingAllocationOnly : public TestWithHeap {
 protected:
  TestSupportingAllocationOnly();

 private:
  CPPGC_STACK_ALLOCATED_IGNORE("permitted for test code")
  subtle::NoGarbageCollectionScope no_gc_scope_;
};

}  // namespace testing
}  // namespace internal
}  // namespace cppgc

#endif  // V8_UNITTESTS_HEAP_CPPGC_TESTS_H_
