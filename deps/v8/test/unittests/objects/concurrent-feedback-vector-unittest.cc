// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <unordered_set>

#include "src/api/api.h"
#include "src/base/platform/semaphore.h"
#include "src/handles/handles-inl.h"
#include "src/handles/local-handles-inl.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using ConcurrentFeedbackVectorTest = TestWithContext;

namespace internal {

namespace {

// kCycles is large enough to ensure we see every state we are interested in.
const int kCycles = 1000;
static std::atomic<bool> all_states_seen{false};

class FeedbackVectorExplorationThread final : public v8::base::Thread {
 public:
  FeedbackVectorExplorationThread(Heap* heap, base::Semaphore* sema_started,
                                  base::Semaphore* vector_ready,
                                  base::Semaphore* vector_consumed,
                                  std::unique_ptr<PersistentHandles> ph,
                                  Handle<FeedbackVector> feedback_vector)
      : v8::base::Thread(base::Thread::Options("ThreadWithLocalHeap")),
        heap_(heap),
        feedback_vector_(feedback_vector),
        ph_(std::move(ph)),
        sema_started_(sema_started),
        vector_ready_(vector_ready),
        vector_consumed_(vector_consumed) {}

  using InlineCacheSet = std::unordered_set<InlineCacheState>;
  bool AllRequiredStatesSeen(const InlineCacheSet& found) {
    auto end = found.end();
    return (found.find(InlineCacheState::UNINITIALIZED) != end &&
            found.find(InlineCacheState::MONOMORPHIC) != end &&
            found.find(InlineCacheState::POLYMORPHIC) != end &&
            found.find(InlineCacheState::MEGAMORPHIC) != end);
  }

  void Run() override {
    Isolate* isolate = heap_->isolate();
    LocalHeap local_heap(heap_, ThreadKind::kBackground, std::move(ph_));
    UnparkedScope scope(&local_heap);

    // Get the feedback vector
    NexusConfig nexus_config =
        NexusConfig::FromBackgroundThread(isolate, &local_heap);
    FeedbackSlot slot(0);

    // FeedbackVectorExplorationThread signals that it's beginning it's loop.
    sema_started_->Signal();

    InlineCacheSet found_states;
    for (int i = 0; i < kCycles; i++) {
      FeedbackNexus nexus(feedback_vector_, slot, nexus_config);
      auto state = nexus.ic_state();
      if (state == InlineCacheState::MONOMORPHIC ||
          state == InlineCacheState::POLYMORPHIC) {
        MapHandles maps;
        nexus.ExtractMaps(&maps);
        for (unsigned int j = 0; j < maps.size(); j++) {
          EXPECT_TRUE(IsMap(*maps[j]));
        }
      }

      if (found_states.find(state) == found_states.end()) {
        found_states.insert(state);
        if (AllRequiredStatesSeen(found_states)) {
          // We are finished.
          break;
        }
      }
    }

    if (!AllRequiredStatesSeen(found_states)) {
      // Repeat the exercise with an explicit handshaking protocol. This ensures
      // at least coverage of the necessary code paths even though it is
      // avoiding actual concurrency. I found that in test runs, there is always
      // one or two bots that have a thread interleaving that doesn't allow all
      // states to be seen. This is for that situation.
      vector_ready_->Wait();
      fprintf(stderr, "Worker beginning to check for uninitialized\n");
      {
        FeedbackNexus nexus(feedback_vector_, slot, nexus_config);
        auto state = nexus.ic_state();
        EXPECT_EQ(state, InlineCacheState::UNINITIALIZED);
      }
      vector_consumed_->Signal();
      vector_ready_->Wait();
      fprintf(stderr, "Worker beginning to check for monomorphic\n");
      {
        FeedbackNexus nexus(feedback_vector_, slot, nexus_config);
        auto state = nexus.ic_state();
        EXPECT_EQ(state, InlineCacheState::MONOMORPHIC);
        MapHandles maps;
        nexus.ExtractMaps(&maps);
        EXPECT_TRUE(IsMap(*maps[0]));
      }
      vector_consumed_->Signal();
      vector_ready_->Wait();
      fprintf(stderr, "Worker beginning to check for polymorphic\n");
      {
        FeedbackNexus nexus(feedback_vector_, slot, nexus_config);
        auto state = nexus.ic_state();
        EXPECT_EQ(state, InlineCacheState::POLYMORPHIC);
        MapHandles maps;
        nexus.ExtractMaps(&maps);
        for (unsigned int i = 0; i < maps.size(); i++) {
          EXPECT_TRUE(IsMap(*maps[i]));
        }
      }
      vector_consumed_->Signal();
      vector_ready_->Wait();
      fprintf(stderr, "Worker beginning to check for megamorphic\n");
      {
        FeedbackNexus nexus(feedback_vector_, slot, nexus_config);
        auto state = nexus.ic_state();
        EXPECT_EQ(state, InlineCacheState::MEGAMORPHIC);
      }
    }

    all_states_seen.store(true, std::memory_order_release);
    vector_consumed_->Signal();

    EXPECT_TRUE(!ph_);
    ph_ = local_heap.DetachPersistentHandles();
  }

  Heap* heap_;
  Handle<FeedbackVector> feedback_vector_;
  std::unique_ptr<PersistentHandles> ph_;
  base::Semaphore* sema_started_;

  // These two semaphores control the explicit handshaking mode in case we
  // didn't see all states within kCycles loops.
  base::Semaphore* vector_ready_;
  base::Semaphore* vector_consumed_;
};

static void CheckedWait(base::Semaphore& semaphore) {
  while (!all_states_seen.load(std::memory_order_acquire)) {
    if (semaphore.WaitFor(base::TimeDelta::FromMilliseconds(1))) break;
  }
}

// Verify that a LoadIC can be cycled through different states and safely
// read on a background thread.
TEST_F(ConcurrentFeedbackVectorTest, CheckLoadICStates) {
  v8_flags.lazy_feedback_allocation = false;

  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();
  HandleScope handle_scope(i_isolate());

  DirectHandle<HeapObject> o1 =
      Cast<HeapObject>(Utils::OpenDirectHandle(*RunJS("o1 = { bar: {} };")));
  DirectHandle<HeapObject> o2 = Cast<HeapObject>(
      Utils::OpenDirectHandle(*RunJS("o2 = { baz: 3, bar: 3 };")));
  DirectHandle<HeapObject> o3 = Cast<HeapObject>(
      Utils::OpenDirectHandle(*RunJS("o3 = { blu: 3, baz: 3, bar: 3 };")));
  DirectHandle<HeapObject> o4 = Cast<HeapObject>(Utils::OpenDirectHandle(
      *RunJS("o4 = { ble: 3, blu: 3, baz: 3, bar: 3 };")));
  auto result = RunJS(
      "function foo(o) {"
      "  let a = o.bar;"
      "  return a;"
      "}"
      "foo(o1);"
      "foo;");
  DirectHandle<JSFunction> function =
      Cast<JSFunction>(Utils::OpenDirectHandle(*result));
  Handle<FeedbackVector> vector(function->feedback_vector(), i_isolate());
  FeedbackSlot slot(0);
  FeedbackNexus nexus(i_isolate(), vector, slot);
  EXPECT_TRUE(IsLoadICKind(nexus.kind()));
  EXPECT_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());
  nexus.ConfigureUninitialized();

  // Now the basic environment is set up. Start the worker thread.
  base::Semaphore sema_started(0);
  base::Semaphore vector_ready(0);
  base::Semaphore vector_consumed(0);
  Handle<FeedbackVector> persistent_vector =
      Cast<FeedbackVector>(ph->NewHandle(vector));
  std::unique_ptr<FeedbackVectorExplorationThread> thread(
      new FeedbackVectorExplorationThread(i_isolate()->heap(), &sema_started,
                                          &vector_ready, &vector_consumed,
                                          std::move(ph), persistent_vector));
  EXPECT_TRUE(thread->Start());
  sema_started.Wait();

  // Cycle the IC through all states repeatedly.

  // {dummy_handler} is just an arbitrary value to associate with a map in order
  // to fill in the feedback vector slots in a minimally acceptable way.
  MaybeObjectHandle dummy_handler(Tagged<Object>(Smi::FromInt(10)),
                                  i_isolate());
  for (int i = 0; i < kCycles; i++) {
    if (all_states_seen.load(std::memory_order_acquire)) break;

    EXPECT_EQ(InlineCacheState::UNINITIALIZED, nexus.ic_state());
    if (i == (kCycles - 1)) {
      // If we haven't seen all states by the last attempt, enter an explicit
      // handshaking mode.
      vector_ready.Signal();
      CheckedWait(vector_consumed);
      fprintf(stderr, "Main thread configuring monomorphic\n");
    }
    nexus.ConfigureMonomorphic(
        Handle<Name>(), Handle<Map>(o1->map(), i_isolate()), dummy_handler);
    EXPECT_EQ(InlineCacheState::MONOMORPHIC, nexus.ic_state());

    if (i == (kCycles - 1)) {
      vector_ready.Signal();
      CheckedWait(vector_consumed);
      fprintf(stderr, "Main thread configuring polymorphic\n");
    }

    // Go polymorphic.
    std::vector<MapAndHandler> map_and_handlers;
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o1->map(), i_isolate()), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o2->map(), i_isolate()), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o3->map(), i_isolate()), dummy_handler));
    map_and_handlers.push_back(
        MapAndHandler(Handle<Map>(o4->map(), i_isolate()), dummy_handler));
    nexus.ConfigurePolymorphic(Handle<Name>(), map_and_handlers);
    EXPECT_EQ(InlineCacheState::POLYMORPHIC, nexus.ic_state());

    if (i == (kCycles - 1)) {
      vector_ready.Signal();
      CheckedWait(vector_consumed);
      fprintf(stderr, "Main thread configuring megamorphic\n");
    }

    // Go Megamorphic
    nexus.ConfigureMegamorphic();
    EXPECT_EQ(InlineCacheState::MEGAMORPHIC, nexus.ic_state());

    if (i == (kCycles - 1)) {
      vector_ready.Signal();
      CheckedWait(vector_consumed);
      fprintf(stderr, "Main thread finishing\n");
    }

    nexus.ConfigureUninitialized();
  }

  EXPECT_TRUE(all_states_seen.load(std::memory_order_acquire));
  thread->Join();
}

}  // anonymous namespace

}  // namespace internal
}  // namespace v8
