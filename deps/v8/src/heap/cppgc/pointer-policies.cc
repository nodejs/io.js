// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/pointer-policies.h"

#include "include/cppgc/internal/persistent-node.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/prefinalizer-handler.h"
#include "src/heap/cppgc/process-heap.h"

namespace cppgc {
namespace internal {

namespace {

#if defined(DEBUG)
bool IsOnStack(const void* address) {
  return v8::base::Stack::GetCurrentStackPosition() <= address &&
         address < v8::base::Stack::GetStackStart();
}
#endif  // defined(DEBUG)

}  // namespace

void SameThreadEnabledCheckingPolicyBase::CheckPointerImpl(
    const void* ptr, bool points_to_payload, bool check_off_heap_assignments) {
  // `ptr` must not reside on stack.
  DCHECK(!IsOnStack(ptr));
#if defined(CPPGC_CAGED_HEAP)
  // `ptr` must reside in the cage.
  DCHECK(CagedHeapBase::IsWithinCage(ptr));
#endif  // defined(CPPGC_CAGED_HEAP)
  // Check for the most commonly used wrong sentinel value (-1).
  DCHECK_NE(reinterpret_cast<void*>(-1), ptr);
  auto* base_page = BasePage::FromPayload(ptr);
  // Large objects do not support mixins. This also means that `base_page` is
  // valid for large objects.
  DCHECK_IMPLIES(base_page->is_large(), points_to_payload);

  // References cannot change their heap association which means that state is
  // immutable once it is set.
  bool is_on_heap = true;
  if (!heap_) {
    heap_ = &base_page->heap();
    if (!heap_->page_backend()->Lookup(reinterpret_cast<Address>(this))) {
      // If `this` is not contained within the heap of `ptr`, we must deal with
      // an on-stack or off-heap reference. For both cases there should be no
      // heap registered.
      is_on_heap = false;
      CHECK(!HeapRegistry::TryFromManagedPointer(this));
    }
  }

  // Member references should never mix heaps.
  DCHECK_EQ(heap_, &base_page->heap());

  DCHECK(heap_->CurrentThreadIsHeapThread());

  // Header checks.
  const HeapObjectHeader* header = nullptr;
  if (points_to_payload) {
    header = &HeapObjectHeader::FromObject(ptr);
    DCHECK_EQ(
        header,
        &base_page->ObjectHeaderFromInnerAddress<AccessMode::kAtomic>(ptr));
  } else {
    // Mixin case. Access the ObjectStartBitmap atomically since sweeping can be
    // in progress.
    header = &base_page->ObjectHeaderFromInnerAddress<AccessMode::kAtomic>(ptr);
    DCHECK_LE(header->ObjectStart(), ptr);
    DCHECK_GT(header->ObjectEnd<AccessMode::kAtomic>(), ptr);
  }
  if (header) {
    DCHECK(!header->IsFree());
  }

#ifdef CPPGC_VERIFY_HEAP
  if (check_off_heap_assignments || is_on_heap) {
    if (heap_->prefinalizer_handler()->IsInvokingPreFinalizers()) {
      // Slot can be in a large object.
      const auto* slot_page = BasePage::FromInnerAddress(heap_, this);
      // Off-heap slots (from other heaps or on-stack) are considered live.
      bool slot_is_live =
          !slot_page ||
          slot_page->ObjectHeaderFromInnerAddress(this).IsMarked();
      // During prefinalizers invocation, check that if the slot is live then
      // |ptr| refers to a live object.
      DCHECK_IMPLIES(slot_is_live, header->IsMarked());
      USE(slot_is_live);
    }
  }
#else
  USE(is_on_heap);
#endif  // CPPGC_VERIFY_HEAP
}

PersistentRegion& StrongPersistentPolicy::GetPersistentRegion(
    const void* object) {
  return BasePage::FromPayload(object)->heap().GetStrongPersistentRegion();
}

PersistentRegion& WeakPersistentPolicy::GetPersistentRegion(
    const void* object) {
  return BasePage::FromPayload(object)->heap().GetWeakPersistentRegion();
}

CrossThreadPersistentRegion&
StrongCrossThreadPersistentPolicy::GetPersistentRegion(const void* object) {
  return BasePage::FromPayload(object)
      ->heap()
      .GetStrongCrossThreadPersistentRegion();
}

CrossThreadPersistentRegion&
WeakCrossThreadPersistentPolicy::GetPersistentRegion(const void* object) {
  return BasePage::FromPayload(object)
      ->heap()
      .GetWeakCrossThreadPersistentRegion();
}

}  // namespace internal
}  // namespace cppgc
