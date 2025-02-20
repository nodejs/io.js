// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_ISOLATE_INL_H_
#define V8_SANDBOX_ISOLATE_INL_H_

#include "src/execution/isolate.h"
#include "src/heap/heap-layout-inl.h"
#include "src/objects/heap-object.h"
#include "src/sandbox/external-pointer-table-inl.h"
#include "src/sandbox/indirect-pointer-tag.h"
#include "src/sandbox/isolate.h"

namespace v8 {
namespace internal {

template <typename IsolateT>
IsolateForSandbox::IsolateForSandbox(IsolateT* isolate)
#ifdef V8_ENABLE_SANDBOX
    : isolate_(isolate->ForSandbox()) {
}
#else
{
}
#endif

#ifdef V8_ENABLE_SANDBOX
ExternalPointerTable& IsolateForSandbox::GetExternalPointerTableFor(
    ExternalPointerTagRange tag_range) {
  IsolateForPointerCompression isolate(isolate_);
  return isolate.GetExternalPointerTableFor(tag_range);
}

ExternalPointerTable::Space* IsolateForSandbox::GetExternalPointerTableSpaceFor(
    ExternalPointerTagRange tag_range, Address host) {
  IsolateForPointerCompression isolate(isolate_);
  return isolate.GetExternalPointerTableSpaceFor(tag_range, host);
}

CodePointerTable::Space* IsolateForSandbox::GetCodePointerTableSpaceFor(
    Address owning_slot) {
  return ReadOnlyHeap::Contains(owning_slot)
             ? isolate_->read_only_heap()->code_pointer_space()
             : isolate_->heap()->code_pointer_space();
}

TrustedPointerTable& IsolateForSandbox::GetTrustedPointerTableFor(
    IndirectPointerTag tag) {
  return IsSharedTrustedPointerType(tag)
             ? isolate_->shared_trusted_pointer_table()
             : isolate_->trusted_pointer_table();
}

TrustedPointerTable::Space* IsolateForSandbox::GetTrustedPointerTableSpaceFor(
    IndirectPointerTag tag) {
  return IsSharedTrustedPointerType(tag)
             ? isolate_->shared_trusted_pointer_space()
             : isolate_->heap()->trusted_pointer_space();
}

inline ExternalPointerTag IsolateForSandbox::GetExternalPointerTableTagFor(
    Tagged<HeapObject> witness, ExternalPointerHandle handle) {
  DCHECK(!HeapLayout::InWritableSharedSpace(witness));
  return isolate_->external_pointer_table().GetTag(handle);
}

TrustedPointerPublishingScope*
IsolateForSandbox::GetTrustedPointerPublishingScope() {
  return isolate_->trusted_pointer_publishing_scope();
}

void IsolateForSandbox::SetTrustedPointerPublishingScope(
    TrustedPointerPublishingScope* scope) {
  DCHECK((isolate_->trusted_pointer_publishing_scope() == nullptr) !=
         (scope == nullptr));
  isolate_->set_trusted_pointer_publishing_scope(scope);
}

#endif  // V8_ENABLE_SANDBOX

template <typename IsolateT>
IsolateForPointerCompression::IsolateForPointerCompression(IsolateT* isolate)
#ifdef V8_COMPRESS_POINTERS
    : isolate_(isolate->ForSandbox()) {
}
#else
{
}
#endif

#ifdef V8_COMPRESS_POINTERS

ExternalPointerTable& IsolateForPointerCompression::GetExternalPointerTableFor(
    ExternalPointerTagRange tag_range) {
  DCHECK(!tag_range.IsEmpty());
  return IsSharedExternalPointerType(tag_range)
             ? isolate_->shared_external_pointer_table()
             : isolate_->external_pointer_table();
}

ExternalPointerTable::Space*
IsolateForPointerCompression::GetExternalPointerTableSpaceFor(
    ExternalPointerTagRange tag_range, Address host) {
  DCHECK(!tag_range.IsEmpty());

  if (V8_UNLIKELY(IsSharedExternalPointerType(tag_range))) {
    DCHECK(!ReadOnlyHeap::Contains(host));
    return isolate_->shared_external_pointer_space();
  }

  if (V8_UNLIKELY(IsMaybeReadOnlyExternalPointerType(tag_range) &&
                  ReadOnlyHeap::Contains(host))) {
    return isolate_->heap()->read_only_external_pointer_space();
  }

  if (HeapLayout::InYoungGeneration(HeapObject::FromAddress(host))) {
    return isolate_->heap()->young_external_pointer_space();
  }

  return isolate_->heap()->old_external_pointer_space();
}

CppHeapPointerTable& IsolateForPointerCompression::GetCppHeapPointerTable() {
  return isolate_->cpp_heap_pointer_table();
}

CppHeapPointerTable::Space*
IsolateForPointerCompression::GetCppHeapPointerTableSpace() {
  return isolate_->heap()->cpp_heap_pointer_space();
}

#endif  // V8_COMPRESS_POINTERS

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_ISOLATE_INL_H_
