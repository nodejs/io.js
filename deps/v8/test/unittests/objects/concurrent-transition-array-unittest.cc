// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/api/api.h"
#include "src/base/platform/semaphore.h"
#include "src/handles/handles-inl.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "src/objects/transitions-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using ConcurrentTransitionArrayTest = TestWithContext;

namespace internal {

namespace {

// Background search thread class
class ConcurrentSearchThread : public v8::base::Thread {
 public:
  ConcurrentSearchThread(Heap* heap, base::Semaphore* background_thread_started,
                         std::unique_ptr<PersistentHandles> ph,
                         Handle<Name> name, Handle<Map> map,
                         std::optional<Handle<Map>> result_map)
      : v8::base::Thread(base::Thread::Options("ThreadWithLocalHeap")),
        heap_(heap),
        background_thread_started_(background_thread_started),
        ph_(std::move(ph)),
        name_(name),
        map_(map),
        result_map_(result_map) {}

  void Run() override {
    LocalHeap local_heap(heap_, ThreadKind::kBackground, std::move(ph_));
    UnparkedScope scope(&local_heap);

    background_thread_started_->Signal();

    CHECK_EQ(TransitionsAccessor(heap_->isolate(), *map_, true)
                 .SearchTransition(*name_, PropertyKind::kData, NONE),
             result_map_ ? **result_map_ : Tagged<Map>());
  }

  Heap* heap() { return heap_; }

  // protected instead of private due to having a subclass.
 protected:
  Heap* heap_;
  base::Semaphore* background_thread_started_;
  std::unique_ptr<PersistentHandles> ph_;
  Handle<Name> name_;
  Handle<Map> map_;
  std::optional<Handle<Map>> result_map_;
};

// Background search thread class that creates the transitions accessor before
// the main thread modifies the TransitionArray, and searches the transition
// only after the main thread finished.
class ConcurrentSearchOnOutdatedAccessorThread final
    : public ConcurrentSearchThread {
 public:
  ConcurrentSearchOnOutdatedAccessorThread(
      Heap* heap, base::Semaphore* background_thread_started,
      base::Semaphore* main_thread_finished,
      std::unique_ptr<PersistentHandles> ph, Handle<Name> name, Handle<Map> map,
      Handle<Map> result_map)
      : ConcurrentSearchThread(heap, background_thread_started, std::move(ph),
                               name, map, result_map),
        main_thread_finished_(main_thread_finished) {}

  void Run() override {
    LocalHeap local_heap(heap_, ThreadKind::kBackground, std::move(ph_));
    UnparkedScope scope(&local_heap);

    background_thread_started_->Signal();
    main_thread_finished_->Wait();

    CHECK_EQ(TransitionsAccessor(heap()->isolate(), *map_, true)
                 .SearchTransition(*name_, PropertyKind::kData, NONE),
             result_map_ ? **result_map_ : Tagged<Map>());
  }

  base::Semaphore* main_thread_finished_;
};

// Search on the main thread and in the background thread at the same time.
TEST_F(ConcurrentTransitionArrayTest, FullFieldTransitions_OnlySearch) {
  v8::HandleScope scope(isolate());

  Handle<String> name = MakeString("name");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = PropertyKind::kData;

  // Set map0 to be a full transition array with transition 'name' to map1.
  Handle<Map> map0 = Map::Create(i_isolate(), 0);
  Handle<Map> map1 =
      Map::CopyWithField(i_isolate(), map0, name, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor::Insert(i_isolate(), map0, name, map1,
                              PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  Handle<Name> persistent_name = ph->NewHandle(name);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore background_thread_started(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      i_isolate()->heap(), &background_thread_started, std::move(ph),
      persistent_name, persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  background_thread_started.Wait();

  CHECK_EQ(*map1, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name, kind, attributes)
                       .ToHandleChecked());

  thread->Join();
}

// Search and insert on the main thread, while the background thread searches at
// the same time.
TEST_F(ConcurrentTransitionArrayTest, FullFieldTransitions) {
  v8::HandleScope scope(isolate());

  Handle<String> name1 = MakeString("name1");
  DirectHandle<String> name2 = MakeString("name2");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = PropertyKind::kData;

  // Set map0 to be a full transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(i_isolate(), 0);
  Handle<Map> map1 =
      Map::CopyWithField(i_isolate(), map0, name1, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  DirectHandle<Map> map2 =
      Map::CopyWithField(i_isolate(), map0, name2, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor::Insert(i_isolate(), map0, name1, map1,
                              PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  Handle<Name> persistent_name1 = ph->NewHandle(name1);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore background_thread_started(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      i_isolate()->heap(), &background_thread_started, std::move(ph),
      persistent_name1, persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  background_thread_started.Wait();

  CHECK_EQ(*map1, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name1, kind, attributes)
                       .ToHandleChecked());
  TransitionsAccessor::Insert(i_isolate(), map0, name2, map2,
                              PROPERTY_TRANSITION);
  CHECK_EQ(*map2, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name2, kind, attributes)
                       .ToHandleChecked());

  thread->Join();
}

// Search and insert on the main thread which changes the encoding from kWeakRef
// to kFullTransitionArray, while the background thread searches at the same
// time.
TEST_F(ConcurrentTransitionArrayTest, WeakRefToFullFieldTransitions) {
  v8::HandleScope scope(isolate());

  Handle<String> name1 = MakeString("name1");
  DirectHandle<String> name2 = MakeString("name2");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = PropertyKind::kData;

  // Set map0 to be a simple transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(i_isolate(), 0);
  Handle<Map> map1 =
      Map::CopyWithField(i_isolate(), map0, name1, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  DirectHandle<Map> map2 =
      Map::CopyWithField(i_isolate(), map0, name2, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor::Insert(i_isolate(), map0, name1, map1,
                              SIMPLE_PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK(transitions.IsWeakRefEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  Handle<Name> persistent_name1 = ph->NewHandle(name1);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore background_thread_started(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      i_isolate()->heap(), &background_thread_started, std::move(ph),
      persistent_name1, persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  background_thread_started.Wait();

  CHECK_EQ(*map1, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name1, kind, attributes)
                       .ToHandleChecked());
  TransitionsAccessor::Insert(i_isolate(), map0, name2, map2,
                              SIMPLE_PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }
  CHECK_EQ(*map2, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name2, kind, attributes)
                       .ToHandleChecked());

  thread->Join();
}

// Search and insert on the main thread, while the background thread searches at
// the same time. In this case, we have a kFullTransitionArray with enough slack
// when we are concurrently writing.
TEST_F(ConcurrentTransitionArrayTest, FullFieldTransitions_withSlack) {
  v8::HandleScope scope(isolate());

  Handle<String> name1 = MakeString("name1");
  DirectHandle<String> name2 = MakeString("name2");
  DirectHandle<String> name3 = MakeString("name3");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = PropertyKind::kData;

  // Set map0 to be a full transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(i_isolate(), 0);
  Handle<Map> map1 =
      Map::CopyWithField(i_isolate(), map0, name1, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  DirectHandle<Map> map2 =
      Map::CopyWithField(i_isolate(), map0, name2, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  DirectHandle<Map> map3 =
      Map::CopyWithField(i_isolate(), map0, name3, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor::Insert(i_isolate(), map0, name1, map1,
                              PROPERTY_TRANSITION);
  TransitionsAccessor::Insert(i_isolate(), map0, name2, map2,
                              PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  Handle<Name> persistent_name1 = ph->NewHandle(name1);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore background_thread_started(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      i_isolate()->heap(), &background_thread_started, std::move(ph),
      persistent_name1, persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  background_thread_started.Wait();

  CHECK_EQ(*map1, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name1, kind, attributes)
                       .ToHandleChecked());
  CHECK_EQ(*map2, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name2, kind, attributes)
                       .ToHandleChecked());
  {
    // Check that we have enough slack for the 3rd insertion into the
    // TransitionArray.
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK_GE(transitions.Capacity(), 3);
  }
  TransitionsAccessor::Insert(i_isolate(), map0, name3, map3,
                              PROPERTY_TRANSITION);
  CHECK_EQ(*map3, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name3, kind, attributes)
                       .ToHandleChecked());

  thread->Join();
}

// Search and insert on the main thread which changes the encoding from
// kUninitialized to kFullTransitionArray, while the background thread searches
// at the same time.
TEST_F(ConcurrentTransitionArrayTest, UninitializedToFullFieldTransitions) {
  v8::HandleScope scope(isolate());

  DirectHandle<String> name1 = MakeString("name1");
  Handle<String> name2 = MakeString("name2");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = PropertyKind::kData;

  // Set map0 to be a full transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(i_isolate(), 0);
  DirectHandle<Map> map1 =
      Map::CopyWithField(i_isolate(), map0, name1, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  {
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK(transitions.IsUninitializedEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  Handle<Name> persistent_name2 = ph->NewHandle(name2);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);

  base::Semaphore background_thread_started(0);

  // Pass persistent handles to background thread.
  // Background thread will search for name2, guaranteed to *not* be on the map.
  std::unique_ptr<ConcurrentSearchThread> thread(new ConcurrentSearchThread(
      i_isolate()->heap(), &background_thread_started, std::move(ph),
      persistent_name2, persistent_map0, std::nullopt));
  CHECK(thread->Start());

  background_thread_started.Wait();

  TransitionsAccessor::Insert(i_isolate(), map0, name1, map1,
                              PROPERTY_TRANSITION);
  CHECK_EQ(*map1, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name1, kind, attributes)
                       .ToHandleChecked());
  {
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }
  thread->Join();
}

// In this test the background search will hold a pointer to an old transition
// array with no slack, while the main thread will try to insert a value into
// it. This makes it so that the main thread will create a new array, and the
// background thread will have a pointer to the old one.
TEST_F(ConcurrentTransitionArrayTest,
       FullFieldTransitions_BackgroundSearchOldPointer) {
  v8::HandleScope scope(isolate());

  Handle<String> name1 = MakeString("name1");
  DirectHandle<String> name2 = MakeString("name2");
  const PropertyAttributes attributes = NONE;
  const PropertyKind kind = PropertyKind::kData;

  // Set map0 to be a full transition array with transition 'name1' to map1.
  Handle<Map> map0 = Map::Create(i_isolate(), 0);
  Handle<Map> map1 =
      Map::CopyWithField(i_isolate(), map0, name1, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  DirectHandle<Map> map2 =
      Map::CopyWithField(i_isolate(), map0, name2, FieldType::Any(i_isolate()),
                         attributes, PropertyConstness::kMutable,
                         Representation::Tagged(), OMIT_TRANSITION)
          .ToHandleChecked();
  TransitionsAccessor::Insert(i_isolate(), map0, name1, map1,
                              PROPERTY_TRANSITION);
  {
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK(transitions.IsFullTransitionArrayEncoding());
  }

  std::unique_ptr<PersistentHandles> ph = i_isolate()->NewPersistentHandles();

  Handle<Name> persistent_name1 = ph->NewHandle(name1);
  Handle<Map> persistent_map0 = ph->NewHandle(map0);
  Handle<Map> persistent_result_map1 = ph->NewHandle(map1);

  base::Semaphore background_thread_started(0);
  base::Semaphore main_thread_finished(0);

  // Pass persistent handles to background thread.
  std::unique_ptr<ConcurrentSearchThread> thread(
      new ConcurrentSearchOnOutdatedAccessorThread(
          i_isolate()->heap(), &background_thread_started,
          &main_thread_finished, std::move(ph), persistent_name1,
          persistent_map0, persistent_result_map1));
  CHECK(thread->Start());

  background_thread_started.Wait();

  CHECK_EQ(*map1, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name1, kind, attributes)
                       .ToHandleChecked());
  {
    // Check that we do not have enough slack for the 2nd insertion into the
    // TransitionArray.
    TestTransitionsAccessor transitions(i_isolate(), map0);
    CHECK_EQ(transitions.Capacity(), 1);
  }
  TransitionsAccessor::Insert(i_isolate(), map0, name2, map2,
                              PROPERTY_TRANSITION);
  CHECK_EQ(*map2, *TransitionsAccessor::SearchTransition(
                       i_isolate(), map0, *name2, kind, attributes)
                       .ToHandleChecked());
  main_thread_finished.Signal();

  thread->Join();
}

}  // anonymous namespace

}  // namespace internal
}  // namespace v8
