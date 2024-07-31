// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/heap-consistency.h"

#include "include/cppgc/heap.h"
#include "src/base/logging.h"
#include "src/heap/cppgc/heap-base.h"

namespace cppgc {
namespace subtle {

// static
bool DisallowGarbageCollectionScope::IsGarbageCollectionAllowed(
    cppgc::HeapHandle& heap_handle) {
  auto& heap_base = internal::HeapBase::From(heap_handle);
  return !heap_base.IsGCForbidden();
}

// static
void DisallowGarbageCollectionScope::Enter(cppgc::HeapHandle& heap_handle) {
  auto& heap_base = internal::HeapBase::From(heap_handle);
  heap_base.EnterDisallowGCScope();
}

// static
void DisallowGarbageCollectionScope::Leave(cppgc::HeapHandle& heap_handle) {
  auto& heap_base = internal::HeapBase::From(heap_handle);
  heap_base.LeaveDisallowGCScope();
}

DisallowGarbageCollectionScope::DisallowGarbageCollectionScope(
    cppgc::HeapHandle& heap_handle)
    : heap_handle_(heap_handle) {
  Enter(heap_handle);
}

DisallowGarbageCollectionScope::~DisallowGarbageCollectionScope() {
  Leave(heap_handle_);
}

// static
void NoGarbageCollectionScope::Enter(cppgc::HeapHandle& heap_handle) {
  auto& heap_base = internal::HeapBase::From(heap_handle);
  heap_base.EnterNoGCScope();
}

// static
void NoGarbageCollectionScope::Leave(cppgc::HeapHandle& heap_handle) {
  auto& heap_base = internal::HeapBase::From(heap_handle);
  heap_base.LeaveNoGCScope();
}

NoGarbageCollectionScope::NoGarbageCollectionScope(
    cppgc::HeapHandle& heap_handle)
    : heap_handle_(heap_handle) {
  Enter(heap_handle);
}

NoGarbageCollectionScope::~NoGarbageCollectionScope() { Leave(heap_handle_); }

}  // namespace subtle
}  // namespace cppgc
