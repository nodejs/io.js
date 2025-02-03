// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/heap/concurrent-marking.h"
#include "src/heap/heap.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8::internal::heap {

TEST(ConcurrentMarkingMarkedBytes) {
  if (!v8_flags.incremental_marking) return;
  if (!i::v8_flags.concurrent_marking) return;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = CcTest::heap();
  HandleScope sc(isolate);
  Handle<FixedArray> root = isolate->factory()->NewFixedArray(1000000);
  heap::InvokeMajorGC(heap);
  if (!heap->incremental_marking()->IsStopped()) return;

  // Store array in Global such that it is part of the root set when
  // starting incremental marking.
  v8::Global<Value> global_root(CcTest::isolate(),
                                Utils::ToLocal(Cast<Object>(root)));

  heap::SimulateIncrementalMarking(heap, false);
  // Ensure that objects are published to the global marking worklist such that
  // the concurrent markers can pick it up.
  heap->mark_compact_collector()->local_marking_worklists()->Publish();
  heap->concurrent_marking()->Join();
  CHECK_GE(heap->concurrent_marking()->TotalMarkedBytes(), root->Size());
}

UNINITIALIZED_TEST(ConcurrentMarkingStoppedOnTeardown) {
  if (!v8_flags.incremental_marking) return;
  if (!i::v8_flags.concurrent_marking) return;

  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);

  {
    Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
    Factory* factory = i_isolate->factory();

    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Context::New(isolate)->Enter();

    for (int i = 0; i < 10000; i++) {
      factory->NewJSWeakMap();
    }

    Heap* heap = i_isolate->heap();
    heap::SimulateIncrementalMarking(heap, false);
  }

  isolate->Dispose();
}

}  // namespace v8::internal::heap
