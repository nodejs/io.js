// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/globals.h"
#include "src/execution/isolate-inl.h"
#include "src/heap/heap-inl.h"
#include "test/cctest/cctest.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

UNINITIALIZED_TEST(PtrComprCageAndIsolateRoot) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();

  v8::Isolate* isolate1 = v8::Isolate::New(create_params);
  Isolate* i_isolate1 = reinterpret_cast<Isolate*>(isolate1);
  v8::Isolate* isolate2 = v8::Isolate::New(create_params);
  Isolate* i_isolate2 = reinterpret_cast<Isolate*>(isolate2);

#ifdef V8_COMPRESS_POINTERS
  CHECK_NE(i_isolate1->isolate_root(), i_isolate2->isolate_root());
#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
  CHECK_EQ(i_isolate1->cage_base(), i_isolate2->cage_base());
#else
  CHECK_NE(i_isolate1->cage_base(), i_isolate2->cage_base());
#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE
#endif  // V8_COMPRESS_POINTERS

  isolate1->Dispose();
  isolate2->Dispose();
}

UNINITIALIZED_TEST(PtrComprCageCodeRange) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();

  v8::Isolate* isolate = v8::Isolate::New(create_params);
  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  VirtualMemoryCage* cage = i_isolate->GetPtrComprCodeCageForTesting();
  if (i_isolate->RequiresCodeRange()) {
    CHECK(!i_isolate->heap()->code_region().is_empty());
    CHECK(cage->reservation()->InVM(i_isolate->heap()->code_region().begin(),
                                    i_isolate->heap()->code_region().size()));
  }

  isolate->Dispose();
}

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
UNINITIALIZED_TEST(SharedPtrComprCage) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();

  v8::Isolate* isolate1 = v8::Isolate::New(create_params);
  Isolate* i_isolate1 = reinterpret_cast<Isolate*>(isolate1);
  v8::Isolate* isolate2 = v8::Isolate::New(create_params);
  Isolate* i_isolate2 = reinterpret_cast<Isolate*>(isolate2);

  Factory* factory1 = i_isolate1->factory();
  Factory* factory2 = i_isolate2->factory();

  {
    HandleScope scope1(i_isolate1);
    HandleScope scope2(i_isolate2);

    DirectHandle<FixedArray> isolate1_object = factory1->NewFixedArray(100);
    DirectHandle<FixedArray> isolate2_object = factory2->NewFixedArray(100);

    CHECK_EQ(GetPtrComprCageBase(*isolate1_object),
             GetPtrComprCageBase(*isolate2_object));
  }

  isolate1->Dispose();
  isolate2->Dispose();
}

UNINITIALIZED_TEST(SharedPtrComprCageCodeRange) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();

  v8::Isolate* isolate1 = v8::Isolate::New(create_params);
  Isolate* i_isolate1 = reinterpret_cast<Isolate*>(isolate1);
  v8::Isolate* isolate2 = v8::Isolate::New(create_params);
  Isolate* i_isolate2 = reinterpret_cast<Isolate*>(isolate2);

  if (i_isolate1->RequiresCodeRange() || i_isolate2->RequiresCodeRange()) {
    CHECK_EQ(i_isolate1->heap()->code_region(),
             i_isolate2->heap()->code_region());
  }

  isolate1->Dispose();
  isolate2->Dispose();
}

namespace {
constexpr int kIsolatesToAllocate = 25;

class IsolateAllocatingThread final : public v8::base::Thread {
 public:
  IsolateAllocatingThread()
      : v8::base::Thread(base::Thread::Options("IsolateAllocatingThread")) {}

  void Run() override {
    std::vector<v8::Isolate*> isolates;
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = CcTest::array_buffer_allocator();

    for (int i = 0; i < kIsolatesToAllocate; i++) {
      isolates.push_back(v8::Isolate::New(create_params));
    }

    for (auto* isolate : isolates) {
      isolate->Dispose();
    }
  }
};
}  // namespace

UNINITIALIZED_TEST(SharedPtrComprCageRace) {
  // Make a bunch of Isolates concurrently as a smoke test against races during
  // initialization and de-initialization.

  // Repeat twice to enforce multiple initializations of CodeRange instances.
  constexpr int kRepeats = 2;
  for (int repeat = 0; repeat < kRepeats; repeat++) {
    std::vector<std::unique_ptr<IsolateAllocatingThread>> threads;
    constexpr int kThreads = 10;

    for (int i = 0; i < kThreads; i++) {
      auto thread = std::make_unique<IsolateAllocatingThread>();
      CHECK(thread->Start());
      threads.push_back(std::move(thread));
    }

    for (auto& thread : threads) {
      thread->Join();
    }
  }
}

#ifdef V8_SHARED_RO_HEAP
UNINITIALIZED_TEST(SharedPtrComprCageImpliesSharedReadOnlyHeap) {
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();

  v8::Isolate* isolate1 = v8::Isolate::New(create_params);
  Isolate* i_isolate1 = reinterpret_cast<Isolate*>(isolate1);
  v8::Isolate* isolate2 = v8::Isolate::New(create_params);
  Isolate* i_isolate2 = reinterpret_cast<Isolate*>(isolate2);

  CHECK_EQ(i_isolate1->read_only_heap(), i_isolate2->read_only_heap());

  // Spot check that some read-only roots are the same.
  CHECK_EQ(ReadOnlyRoots(i_isolate1).the_hole_value(),
           ReadOnlyRoots(i_isolate2).the_hole_value());
  CHECK_EQ(ReadOnlyRoots(i_isolate1).instruction_stream_map(),
           ReadOnlyRoots(i_isolate2).instruction_stream_map());
  CHECK_EQ(ReadOnlyRoots(i_isolate1).exception(),
           ReadOnlyRoots(i_isolate2).exception());

  isolate1->Dispose();
  isolate2->Dispose();
}
#endif  // V8_SHARED_RO_HEAP
#endif  // V8_COMPRESS_POINTERS_IN_SHARED_CAGE

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS
