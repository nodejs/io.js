// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/testing.h"

#include "src/base/logging.h"
#include "src/heap/cppgc/heap-base.h"

namespace cppgc {
namespace testing {

OverrideEmbedderStackStateScope::OverrideEmbedderStackStateScope(
    HeapHandle& heap_handle, EmbedderStackState state)
    : heap_handle_(heap_handle) {
  internal::HeapBase::From(heap_handle_).set_override_stack_state(state);
}

OverrideEmbedderStackStateScope::~OverrideEmbedderStackStateScope() {
  internal::HeapBase::From(heap_handle_).clear_overridden_stack_state();
}

StandaloneTestingHeap::StandaloneTestingHeap(HeapHandle& heap_handle)
    : heap_handle_(heap_handle) {}

void StandaloneTestingHeap::StartGarbageCollection() {
  internal::HeapBase::From(heap_handle_)
      .StartIncrementalGarbageCollectionForTesting();
}

bool StandaloneTestingHeap::PerformMarkingStep(EmbedderStackState stack_state) {
  return internal::HeapBase::From(heap_handle_)
      .marker()
      ->IncrementalMarkingStepForTesting(stack_state);
}

void StandaloneTestingHeap::FinalizeGarbageCollection(
    EmbedderStackState stack_state) {
  internal::HeapBase::From(heap_handle_)
      .FinalizeIncrementalGarbageCollectionForTesting(stack_state);
}

void StandaloneTestingHeap::ToggleMainThreadMarking(bool should_mark) {
  internal::HeapBase::From(heap_handle_)
      .marker()
      ->SetMainThreadMarkingDisabledForTesting(!should_mark);
}

void StandaloneTestingHeap::ForceCompactionForNextGarbageCollection() {
  internal::HeapBase::From(heap_handle_)
      .compactor()
      .EnableForNextGCForTesting();
}

bool IsHeapObjectOld(void* object) {
#if defined(CPPGC_YOUNG_GENERATION)
  return internal::HeapObjectHeader::FromObject(object).IsMarked();
#else
  return true;
#endif
}

}  // namespace testing
}  // namespace cppgc
