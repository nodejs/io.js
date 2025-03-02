// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/persistent-handles.h"

#include "src/heap/parked-scope.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using PersistentHandlesTest = TestWithIsolate;

TEST_F(PersistentHandlesTest, OrderOfBlocks) {
  Isolate* isolate = i_isolate();
  Heap* heap = isolate->heap();
  HandleScope scope(isolate);
  handle(ReadOnlyRoots(heap).empty_string(), isolate);
  HandleScopeData* data = isolate->handle_scope_data();

  Address* next;
  Address* limit;
  DirectHandle<String> first_empty, last_empty;
  std::unique_ptr<PersistentHandles> ph;

  {
    PersistentHandlesScope persistent_scope(isolate);

    // fill block
    first_empty = direct_handle(ReadOnlyRoots(heap).empty_string(), isolate);

    while (data->next < data->limit) {
      handle(ReadOnlyRoots(heap).empty_string(), isolate);
    }

    // add second block and two more handles on it
    handle(ReadOnlyRoots(heap).empty_string(), isolate);
    last_empty = direct_handle(ReadOnlyRoots(heap).empty_string(), isolate);

    // remember next and limit in second block
    next = data->next;
    limit = data->limit;

    ph = persistent_scope.Detach();
  }

  CHECK_EQ(ph->block_next_, next);
  CHECK_EQ(ph->block_limit_, limit);

  CHECK_EQ(first_empty->length(), 0);
  CHECK_EQ(last_empty->length(), 0);
  CHECK_EQ(*first_empty, *last_empty);
}

namespace {
class CounterDummyVisitor : public RootVisitor {
 public:
  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override {
    counter += end - start;
  }
  size_t counter = 0;
};

size_t count_handles(Isolate* isolate) {
  CounterDummyVisitor visitor;
  isolate->handle_scope_implementer()->Iterate(&visitor);
  return visitor.counter;
}

size_t count_handles(PersistentHandles* ph) {
  CounterDummyVisitor visitor;
  ph->Iterate(&visitor);
  return visitor.counter;
}
}  // namespace

TEST_F(PersistentHandlesTest, Iterate) {
  Isolate* isolate = i_isolate();
  Heap* heap = isolate->heap();
  v8::HandleScope scope(reinterpret_cast<v8::Isolate*>(isolate));
  HandleScopeData* data = isolate->handle_scope_data();

  size_t handles_in_empty_scope = count_handles(isolate);

  IndirectHandle<Object> init(ReadOnlyRoots(heap).empty_string(), isolate);
  Address* old_limit = data->limit;
  CHECK_EQ(count_handles(isolate), handles_in_empty_scope + 1);

  std::unique_ptr<PersistentHandles> ph;
  IndirectHandle<String> verify_handle;

  {
    PersistentHandlesScope persistent_scope(isolate);
    verify_handle = handle(ReadOnlyRoots(heap).empty_string(), isolate);
    CHECK_NE(old_limit, data->limit);
    CHECK_EQ(count_handles(isolate), handles_in_empty_scope + 2);
    ph = persistent_scope.Detach();
  }

#if DEBUG
  CHECK(ph->Contains(verify_handle.location()));
#else
  USE(verify_handle);
#endif

  ph->NewHandle(ReadOnlyRoots(heap).empty_string());
  CHECK_EQ(count_handles(ph.get()), 2);
  CHECK_EQ(count_handles(isolate), handles_in_empty_scope + 1);
}

static constexpr int kNumHandles = kHandleBlockSize * 2 + kHandleBlockSize / 2;

class PersistentHandlesThread final : public v8::base::Thread {
 public:
  PersistentHandlesThread(Heap* heap, std::vector<Handle<HeapNumber>> handles,
                          std::unique_ptr<PersistentHandles> ph,
                          Tagged<HeapNumber> number,
                          base::Semaphore* sema_started,
                          base::Semaphore* sema_gc_finished)
      : v8::base::Thread(base::Thread::Options("ThreadWithLocalHeap")),
        heap_(heap),
        handles_(std::move(handles)),
        ph_(std::move(ph)),
        number_(number),
        sema_started_(sema_started),
        sema_gc_finished_(sema_gc_finished) {}

  void Run() override {
    LocalHeap local_heap(heap_, ThreadKind::kBackground, std::move(ph_));
    UnparkedScope unparked_scope(&local_heap);
    LocalHandleScope scope(&local_heap);

    for (int i = 0; i < kNumHandles; i++) {
      handles_.push_back(local_heap.NewPersistentHandle(number_));
    }

    sema_started_->Signal();

    local_heap.ExecuteWhileParked([this]() { sema_gc_finished_->Wait(); });

    for (DirectHandle<HeapNumber> handle : handles_) {
      CHECK_EQ(42.0, handle->value());
    }

    CHECK_EQ(handles_.size(), kNumHandles * 2);

    CHECK(!ph_);
    ph_ = local_heap.DetachPersistentHandles();
  }

  std::unique_ptr<PersistentHandles> DetachPersistentHandles() {
    CHECK(ph_);
    return std::move(ph_);
  }

 private:
  Heap* heap_;
  std::vector<Handle<HeapNumber>> handles_;
  std::unique_ptr<PersistentHandles> ph_;
  Tagged<HeapNumber> number_;
  base::Semaphore* sema_started_;
  base::Semaphore* sema_gc_finished_;
};

TEST_F(PersistentHandlesTest, CreatePersistentHandles) {
  std::unique_ptr<PersistentHandles> ph = isolate()->NewPersistentHandles();
  std::vector<Handle<HeapNumber>> handles;

  HandleScope handle_scope(isolate());
  Handle<HeapNumber> number = isolate()->factory()->NewHeapNumber(42.0);

  for (int i = 0; i < kNumHandles; i++) {
    handles.push_back(ph->NewHandle(number));
  }

  base::Semaphore sema_started(0);
  base::Semaphore sema_gc_finished(0);

  // pass persistent handles to background thread
  std::unique_ptr<PersistentHandlesThread> thread(new PersistentHandlesThread(
      isolate()->heap(), std::move(handles), std::move(ph), *number,
      &sema_started, &sema_gc_finished));
  CHECK(thread->Start());

  sema_started.Wait();

  InvokeMajorGC();
  sema_gc_finished.Signal();

  thread->Join();

  // get persistent handles back to main thread
  ph = thread->DetachPersistentHandles();
  ph->NewHandle(number);
}

TEST_F(PersistentHandlesTest, DereferencePersistentHandle) {
  std::unique_ptr<PersistentHandles> phs = isolate()->NewPersistentHandles();
  IndirectHandle<HeapNumber> ph;
  {
    HandleScope handle_scope(isolate());
    Handle<HeapNumber> number = isolate()->factory()->NewHeapNumber(42.0);
    ph = phs->NewHandle(number);
  }
  {
    LocalHeap local_heap(isolate()->heap(), ThreadKind::kBackground,
                         std::move(phs));
    UnparkedScope scope(&local_heap);
    CHECK_EQ(42, ph->value());
  }
}

TEST_F(PersistentHandlesTest, DereferencePersistentHandleFailsWhenDisallowed) {
  HandleScope handle_scope(isolate());
  std::unique_ptr<PersistentHandles> phs = isolate()->NewPersistentHandles();
  IndirectHandle<HeapNumber> ph;
  {
    HandleScope inner_handle_scope(isolate());
    Handle<HeapNumber> number = isolate()->factory()->NewHeapNumber(42.0);
    ph = phs->NewHandle(number);
  }
  {
    LocalHeap local_heap(isolate()->heap(), ThreadKind::kBackground,
                         std::move(phs));
    UnparkedScope scope(&local_heap);
    DisallowHandleDereference disallow_scope;
    CHECK_EQ(42, ph->value());
  }
}

TEST_F(PersistentHandlesTest, NewPersistentHandleFailsWhenParked) {
  LocalHeap local_heap(isolate()->heap(), ThreadKind::kBackground);
  // Fail here in debug mode: Persistent handles can't be created if local heap
  // is parked
  local_heap.NewPersistentHandle(Smi::FromInt(1));
}

TEST_F(PersistentHandlesTest, NewPersistentHandleFailsWhenParkedExplicit) {
  LocalHeap local_heap(isolate()->heap(), ThreadKind::kBackground,
                       isolate()->NewPersistentHandles());
  // Fail here in debug mode: Persistent handles can't be created if local heap
  // is parked
  local_heap.NewPersistentHandle(Smi::FromInt(1));
}

}  // namespace internal
}  // namespace v8
