// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/handles.h"

#include "src/api/api.h"
#include "src/base/logging.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/execution/isolate.h"
#include "src/execution/thread-id.h"
#include "src/handles/maybe-handles.h"
#include "src/heap/base/stack.h"
#include "src/heap/heap-layout-inl.h"
#include "src/objects/objects-inl.h"
#include "src/roots/roots-inl.h"
#include "src/utils/address-map.h"
#include "src/utils/identity-map.h"

#ifdef V8_ENABLE_MAGLEV
#include "src/maglev/maglev-concurrent-dispatcher.h"
#endif  // V8_ENABLE_MAGLEV

#ifdef DEBUG
// For GetIsolateFromWritableHeapObject.
#include "src/heap/heap-write-barrier-inl.h"
// For GetIsolateFromWritableObject.
#include "src/execution/isolate-utils-inl.h"
#endif

#ifdef V8_ENABLE_DIRECT_HANDLE
// For Isolate::Current() in indirect_handle.
#include "src/execution/isolate-inl.h"
#endif

namespace v8 {
namespace internal {

// Handles should be trivially copyable so that the contained value can be
// efficiently passed by value in a register. This is important for two
// reasons: better performance and a simpler ABI for generated code and fast
// API calls.
ASSERT_TRIVIALLY_COPYABLE(HandleBase);
ASSERT_TRIVIALLY_COPYABLE(Handle<Object>);
ASSERT_TRIVIALLY_COPYABLE(MaybeHandle<Object>);

#ifdef V8_ENABLE_DIRECT_HANDLE

#if !(defined(DEBUG) && V8_HAS_ATTRIBUTE_TRIVIAL_ABI)
// Direct handles should be trivially copyable, for the same reasons as above.
// In debug builds, however, we want to define a non-default copy constructor
// and destructor for debugging purposes. This makes them non-trivially
// copyable. We only do it in builds where we can declare them as "trivial ABI",
// which guarantees that they can be efficiently passed by value in a register.
ASSERT_TRIVIALLY_COPYABLE(DirectHandle<Object>);
ASSERT_TRIVIALLY_COPYABLE(MaybeDirectHandle<Object>);
#endif

// static
Address* HandleBase::indirect_handle(Address object) {
  return HandleScope::CreateHandle(Isolate::Current(), object);
}

// static
Address* HandleBase::indirect_handle(Address object, Isolate* isolate) {
  return HandleScope::CreateHandle(isolate, object);
}

// static
Address* HandleBase::indirect_handle(Address object, LocalIsolate* isolate) {
  return LocalHandleScope::GetHandle(isolate->heap(), object);
}

// static
Address* HandleBase::indirect_handle(Address object, LocalHeap* local_heap) {
  return LocalHandleScope::GetHandle(local_heap, object);
}

#endif  // V8_ENABLE_DIRECT_HANDLE

#ifdef DEBUG

bool HandleBase::IsDereferenceAllowed() const {
  DCHECK_NOT_NULL(location_);
  Tagged<Object> object(*location_);
  if (IsSmi(object)) return true;
  Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
  if (HeapLayout::InReadOnlySpace(heap_object)) return true;
  Isolate* isolate = Isolate::Current();
  RootIndex root_index;
  if (isolate->roots_table().IsRootHandleLocation(location_, &root_index) &&
      RootsTable::IsImmortalImmovable(root_index)) {
    return true;
  }
  if (isolate->IsBuiltinTableHandleLocation(location_)) return true;
  if (!AllowHandleDereference::IsAllowed()) return false;

  // Allocations in the shared heap may be dereferenced by multiple threads.
  if (HeapLayout::InWritableSharedSpace(heap_object)) return true;

  // Deref is explicitly allowed from any thread. Used for running internal GC
  // epilogue callbacks in the safepoint after a GC.
  if (AllowHandleUsageOnAllThreads::IsAllowed()) return true;

  LocalHeap* local_heap = isolate->CurrentLocalHeap();

  // Local heap can't access handles when parked
  if (!local_heap->IsHandleDereferenceAllowed()) {
    StdoutStream{} << "Cannot dereference handle owned by "
                   << "non-running local heap\n";
    return false;
  }

  // We are pretty strict with handle dereferences on background threads: A
  // background local heap is only allowed to dereference its own local or
  // persistent handles.
  if (!local_heap->is_main_thread()) {
    // The current thread owns the handle and thus can dereference it.
    return local_heap->ContainsPersistentHandle(location_) ||
           local_heap->ContainsLocalHandle(location_);
  }
  // If LocalHeap::Current() is null, we're on the main thread -- if we were to
  // check main thread HandleScopes here, we should additionally check the
  // main-thread LocalHeap.
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());

  // TODO(leszeks): Check if the main thread owns this handle.
  return true;
}

#ifdef V8_ENABLE_DIRECT_HANDLE
bool DirectHandleBase::IsDereferenceAllowed() const {
  DCHECK_NE(obj_, kTaggedNullAddress);
  Tagged<Object> object(obj_);
  if (IsSmi(object)) return true;
  Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
  if (HeapLayout::InReadOnlySpace(heap_object)) return true;
  Isolate* isolate = Isolate::Current();
  if (!AllowHandleDereference::IsAllowed()) return false;

  // Allocations in the shared heap may be dereferenced by multiple threads.
  if (HeapLayout::InWritableSharedSpace(heap_object)) return true;

  // Deref is explicitly allowed from any thread. Used for running internal GC
  // epilogue callbacks in the safepoint after a GC.
  if (AllowHandleUsageOnAllThreads::IsAllowed()) return true;

  LocalHeap* local_heap = isolate->CurrentLocalHeap();

  // Local heap can't access handles when parked
  if (!local_heap->IsHandleDereferenceAllowed()) {
    StdoutStream{} << "Cannot dereference handle owned by "
                   << "non-running local heap\n";
    return false;
  }

  // We are pretty strict with handle dereferences on background threads: A
  // background local heap is only allowed to dereference its own local handles.
  if (!local_heap->is_main_thread())
    return ::heap::base::Stack::IsOnStack(this);

  // If LocalHeap::Current() is null, we're on the main thread -- if we were to
  // check main thread HandleScopes here, we should additionally check the
  // main-thread LocalHeap.
  DCHECK_EQ(ThreadId::Current(), isolate->thread_id());

  return true;
}
#endif  // V8_ENABLE_DIRECT_HANDLE

#endif  // DEBUG

int HandleScope::NumberOfHandles(Isolate* isolate) {
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  int n = static_cast<int>(impl->blocks()->size());
  if (n == 0) return 0;
  return ((n - 1) * kHandleBlockSize) +
         static_cast<int>(
             (isolate->handle_scope_data()->next - impl->blocks()->back()));
}

Address* HandleScope::Extend(Isolate* isolate) {
  HandleScopeData* current = isolate->handle_scope_data();

  Address* result = current->next;

  DCHECK(result == current->limit);
  // Make sure there's at least one scope on the stack and that the
  // top of the scope stack isn't a barrier.
  if (!Utils::ApiCheck(current->level != current->sealed_level,
                       "v8::HandleScope::CreateHandle()",
                       "Cannot create a handle without a HandleScope")) {
    return nullptr;
  }
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  // If there's more room in the last block, we use that. This is used
  // for fast creation of scopes after scope barriers.
  if (!impl->blocks()->empty()) {
    Address* limit = &impl->blocks()->back()[kHandleBlockSize];
    if (current->limit != limit) {
      current->limit = limit;
      DCHECK_LT(limit - current->next, kHandleBlockSize);
    }
  }

  // If we still haven't found a slot for the handle, we extend the
  // current handle scope by allocating a new handle block.
  if (result == current->limit) {
    // If there's a spare block, use it for growing the current scope.
    result = impl->GetSpareOrNewBlock();
    // Add the extension to the global list of blocks, but count the
    // extension as part of the current scope.
    impl->blocks()->push_back(result);
    current->limit = &result[kHandleBlockSize];
  }

  return result;
}

void HandleScope::DeleteExtensions(Isolate* isolate) {
  HandleScopeData* current = isolate->handle_scope_data();
  isolate->handle_scope_implementer()->DeleteExtensions(current->limit);
}

#if defined(ENABLE_GLOBAL_HANDLE_ZAPPING) || \
    defined(ENABLE_LOCAL_HANDLE_ZAPPING)
void HandleScope::ZapRange(Address* start, Address* end, uintptr_t zap_value) {
  DCHECK_LE(end - start, kHandleBlockSize);
  for (Address* p = start; p != end; p++) {
    *p = static_cast<Address>(zap_value);
  }
}
#endif

Address HandleScope::current_level_address(Isolate* isolate) {
  return reinterpret_cast<Address>(&isolate->handle_scope_data()->level);
}

Address HandleScope::current_next_address(Isolate* isolate) {
  return reinterpret_cast<Address>(&isolate->handle_scope_data()->next);
}

Address HandleScope::current_limit_address(Isolate* isolate) {
  return reinterpret_cast<Address>(&isolate->handle_scope_data()->limit);
}

}  // namespace internal
}  // namespace v8
