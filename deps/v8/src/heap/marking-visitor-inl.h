// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_VISITOR_INL_H_
#define V8_HEAP_MARKING_VISITOR_INL_H_

#include "src/common/globals.h"
#include "src/heap/ephemeron-remembered-set.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/marking-visitor.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/heap/marking.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/objects-visiting.h"
#include "src/heap/pretenuring-handler-inl.h"
#include "src/heap/progress-bar.h"
#include "src/heap/spaces.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/js-objects.h"
#include "src/objects/objects.h"
#include "src/objects/property-details.h"
#include "src/objects/slots.h"
#include "src/objects/smi.h"
#include "src/objects/string.h"
#include "src/sandbox/external-pointer-inl.h"
#include "src/sandbox/js-dispatch-table-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// ===========================================================================
// Visiting strong and weak pointers =========================================
// ===========================================================================

template <typename ConcreteVisitor>
bool MarkingVisitorBase<ConcreteVisitor>::MarkObject(
    Tagged<HeapObject> retainer, Tagged<HeapObject> object,
    MarkingHelper::WorklistTarget target_worklist) {
  DCHECK(heap_->Contains(object));
  SynchronizePageAccess(object);
  concrete_visitor()->AddStrongReferenceForReferenceSummarizer(retainer,
                                                               object);
  return MarkingHelper::TryMarkAndPush(heap_, local_marking_worklists_,
                                       concrete_visitor()->marking_state(),
                                       target_worklist, object);
}

// class template arguments
template <typename ConcreteVisitor>
// method template arguments
template <typename THeapObjectSlot>
void MarkingVisitorBase<ConcreteVisitor>::ProcessStrongHeapObject(
    Tagged<HeapObject> host, THeapObjectSlot slot,
    Tagged<HeapObject> heap_object) {
  SynchronizePageAccess(heap_object);
  const auto target_worklist =
      MarkingHelper::ShouldMarkObject(heap_, heap_object);
  if (!target_worklist) {
    return;
  }
  // TODO(chromium:1495151): Remove after diagnosing.
  if (V8_UNLIKELY(!MemoryChunk::FromHeapObject(heap_object)->IsMarking() &&
                  IsFreeSpaceOrFiller(
                      heap_object, ObjectVisitorWithCageBases::cage_base()))) {
    heap_->isolate()->PushStackTraceAndDie(
        reinterpret_cast<void*>(host->map().ptr()),
        reinterpret_cast<void*>(host->address()),
        reinterpret_cast<void*>(slot.address()),
        reinterpret_cast<void*>(MemoryChunkMetadata::FromHeapObject(heap_object)
                                    ->owner()
                                    ->identity()));
  }
  MarkObject(host, heap_object, target_worklist.value());
  concrete_visitor()->RecordSlot(host, slot, heap_object);
}

// static
template <typename ConcreteVisitor>
V8_INLINE constexpr bool
MarkingVisitorBase<ConcreteVisitor>::IsTrivialWeakReferenceValue(
    Tagged<HeapObject> host, Tagged<HeapObject> heap_object) {
  return !IsMap(heap_object) ||
         !(IsMap(host) || IsTransitionArray(host) || IsDescriptorArray(host));
}

// class template arguments
template <typename ConcreteVisitor>
// method template arguments
template <typename THeapObjectSlot>
void MarkingVisitorBase<ConcreteVisitor>::ProcessWeakHeapObject(
    Tagged<HeapObject> host, THeapObjectSlot slot,
    Tagged<HeapObject> heap_object) {
  SynchronizePageAccess(heap_object);
  concrete_visitor()->AddWeakReferenceForReferenceSummarizer(host, heap_object);
  const auto target_worklist =
      MarkingHelper::ShouldMarkObject(heap_, heap_object);
  if (!target_worklist) {
    return;
  }
  if (concrete_visitor()->marking_state()->IsMarked(heap_object)) {
    // Weak references with live values are directly processed here to
    // reduce the processing time of weak cells during the main GC
    // pause.
    concrete_visitor()->RecordSlot(host, slot, heap_object);
  } else {
    // If we do not know about liveness of the value, we have to process
    // the reference when we know the liveness of the whole transitive
    // closure.
    // Distinguish trivial cases (non involving custom weakness) from
    // non-trivial ones. The latter are maps in host objects of type Map,
    // TransitionArray and DescriptorArray.
    if (V8_LIKELY(IsTrivialWeakReferenceValue(host, heap_object))) {
      local_weak_objects_->weak_references_trivial_local.Push(
          HeapObjectAndSlot{host, slot});
    } else {
      local_weak_objects_->weak_references_non_trivial_local.Push(
          HeapObjectAndSlot{host, slot});
    }
  }
}

// class template arguments
template <typename ConcreteVisitor>
// method template arguments
template <typename TSlot>
void MarkingVisitorBase<ConcreteVisitor>::VisitPointersImpl(
    Tagged<HeapObject> host, TSlot start, TSlot end) {
  using THeapObjectSlot = typename TSlot::THeapObjectSlot;
  for (TSlot slot = start; slot < end; ++slot) {
    const std::optional<Tagged<Object>> optional_object =
        this->GetObjectFilterReadOnlyAndSmiFast(slot);
    if (!optional_object) {
      continue;
    }
    typename TSlot::TObject object = *optional_object;
    Tagged<HeapObject> heap_object;
    if (object.GetHeapObjectIfStrong(&heap_object)) {
      // If the reference changes concurrently from strong to weak, the write
      // barrier will treat the weak reference as strong, so we won't miss the
      // weak reference.
      ProcessStrongHeapObject(host, THeapObjectSlot(slot), heap_object);
    } else if (TSlot::kCanBeWeak && object.GetHeapObjectIfWeak(&heap_object)) {
      ProcessWeakHeapObject(host, THeapObjectSlot(slot), heap_object);
    }
  }
}

// class template arguments
template <typename ConcreteVisitor>
// method template arguments
template <typename TSlot>
void MarkingVisitorBase<ConcreteVisitor>::VisitStrongPointerImpl(
    Tagged<HeapObject> host, TSlot slot) {
  static_assert(!TSlot::kCanBeWeak);
  using THeapObjectSlot = typename TSlot::THeapObjectSlot;
  typename TSlot::TObject object = slot.Relaxed_Load();
  Tagged<HeapObject> heap_object;
  if (object.GetHeapObject(&heap_object)) {
    ProcessStrongHeapObject(host, THeapObjectSlot(slot), heap_object);
  }
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::VisitEmbeddedPointer(
    Tagged<InstructionStream> host, RelocInfo* rinfo) {
  DCHECK(RelocInfo::IsEmbeddedObjectMode(rinfo->rmode()));
  Tagged<HeapObject> object =
      rinfo->target_object(ObjectVisitorWithCageBases::cage_base());
  const auto target_worklist = MarkingHelper::ShouldMarkObject(heap_, object);
  if (!target_worklist) {
    return;
  }

  if (!concrete_visitor()->marking_state()->IsMarked(object)) {
    Tagged<Code> code = UncheckedCast<Code>(host->raw_code(kAcquireLoad));
    if (code->IsWeakObject(object)) {
      local_weak_objects_->weak_objects_in_code_local.Push(
          HeapObjectAndCode{object, code});
      concrete_visitor()->AddWeakReferenceForReferenceSummarizer(host, object);
    } else {
      MarkObject(host, object, target_worklist.value());
    }
  }
  concrete_visitor()->RecordRelocSlot(host, rinfo, object);
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::VisitCodeTarget(
    Tagged<InstructionStream> host, RelocInfo* rinfo) {
  DCHECK(RelocInfo::IsCodeTargetMode(rinfo->rmode()));
  Tagged<InstructionStream> target =
      InstructionStream::FromTargetAddress(rinfo->target_address());

  const auto target_worklist = MarkingHelper::ShouldMarkObject(heap_, target);
  if (!target_worklist) {
    return;
  }
  MarkObject(host, target, target_worklist.value());
  concrete_visitor()->RecordRelocSlot(host, rinfo, target);
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::VisitExternalPointer(
    Tagged<HeapObject> host, ExternalPointerSlot slot) {
#ifdef V8_COMPRESS_POINTERS
  DCHECK_NE(slot.tag(), kExternalPointerNullTag);
  if (slot.HasExternalPointerHandle()) {
    ExternalPointerHandle handle = slot.Relaxed_LoadHandle();
    ExternalPointerTable* table;
    ExternalPointerTable::Space* space;
    if (IsSharedExternalPointerType(slot.tag())) {
      table = shared_external_pointer_table_;
      space = shared_external_pointer_space_;
    } else {
      table = external_pointer_table_;
      if (v8_flags.sticky_mark_bits) {
        // Everything is considered old during major GC.
        DCHECK(!Heap::InYoungGeneration(host));
        if (handle == kNullExternalPointerHandle) return;
        // The object may either be in young or old EPT.
        if (table->Contains(heap_->young_external_pointer_space(), handle)) {
          space = heap_->young_external_pointer_space();
        } else {
          DCHECK(table->Contains(heap_->old_external_pointer_space(), handle));
          space = heap_->old_external_pointer_space();
        }
      } else {
        space = Heap::InYoungGeneration(host)
                    ? heap_->young_external_pointer_space()
                    : heap_->old_external_pointer_space();
      }
    }
    table->Mark(space, handle, slot.address());
  }
#endif  // V8_COMPRESS_POINTERS
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::VisitCppHeapPointer(
    Tagged<HeapObject> host, CppHeapPointerSlot slot) {
#ifdef V8_COMPRESS_POINTERS
  const ExternalPointerHandle handle = slot.Relaxed_LoadHandle();
  if (handle == kNullExternalPointerHandle) {
    return;
  }
  CppHeapPointerTable* table = cpp_heap_pointer_table_;
  CppHeapPointerTable::Space* space = heap_->cpp_heap_pointer_space();
  table->Mark(space, handle, slot.address());
#endif  // V8_COMPRESS_POINTERS
  if (auto cpp_heap_pointer =
          slot.try_load(heap_->isolate(), kAnyCppHeapPointer)) {
    local_marking_worklists_->cpp_marking_state()->MarkAndPush(
        reinterpret_cast<void*>(cpp_heap_pointer));
  }
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::VisitIndirectPointer(
    Tagged<HeapObject> host, IndirectPointerSlot slot,
    IndirectPointerMode mode) {
#ifdef V8_ENABLE_SANDBOX
  if (mode == IndirectPointerMode::kStrong) {
    // Load the referenced object (if the slot is initialized) and mark it as
    // alive if necessary. Indirect pointers never have to be added to a
    // remembered set because the referenced object will update the pointer
    // table entry when it is relocated.
    Tagged<Object> value = slot.Relaxed_Load(heap_->isolate());
    if (IsHeapObject(value)) {
      Tagged<HeapObject> obj = Cast<HeapObject>(value);
      SynchronizePageAccess(obj);
      const auto target_worklist = MarkingHelper::ShouldMarkObject(heap_, obj);
      if (!target_worklist) {
        return;
      }
      MarkObject(host, obj, target_worklist.value());
    }
  }
#else
  UNREACHABLE();
#endif
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::VisitTrustedPointerTableEntry(
    Tagged<HeapObject> host, IndirectPointerSlot slot) {
  concrete_visitor()->MarkPointerTableEntry(host, slot);
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::VisitJSDispatchTableEntry(
    Tagged<HeapObject> host, JSDispatchHandle handle) {
#ifdef V8_ENABLE_LEAPTIERING
  JSDispatchTable* table = GetProcessWideJSDispatchTable();
#ifdef DEBUG
  JSDispatchTable::Space* space = heap_->js_dispatch_table_space();
  JSDispatchTable::Space* ro_space =
      heap_->isolate()->read_only_heap()->js_dispatch_table_space();
  table->VerifyEntry(handle, space, ro_space);
#endif  // DEBUG

  table->Mark(handle);
#endif  // V8_ENABLE_LEAPTIERING
}

// ===========================================================================
// Object participating in bytecode flushing =================================
// ===========================================================================

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitJSFunction(
    Tagged<Map> map, Tagged<JSFunction> js_function) {
  if (ShouldFlushBaselineCode(js_function)) {
    DCHECK(IsBaselineCodeFlushingEnabled(code_flush_mode_));
    local_weak_objects_->baseline_flushing_candidates_local.Push(js_function);
  } else {
#ifdef V8_ENABLE_SANDBOX
    VisitIndirectPointer(js_function,
                         js_function->RawIndirectPointerField(
                             JSFunction::kCodeOffset, kCodeIndirectPointerTag),
                         IndirectPointerMode::kStrong);
#else
    VisitPointer(js_function, js_function->RawField(JSFunction::kCodeOffset));
#endif  // V8_ENABLE_SANDBOX
    // TODO(mythria): Consider updating the check for ShouldFlushBaselineCode to
    // also include cases where there is old bytecode even when there is no
    // baseline code and remove this check here.
    if (IsByteCodeFlushingEnabled(code_flush_mode_) &&
        js_function->NeedsResetDueToFlushedBytecode(heap_->isolate())) {
      local_weak_objects_->flushed_js_functions_local.Push(js_function);
    }
  }
  return Base::VisitJSFunction(map, js_function);
}

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitSharedFunctionInfo(
    Tagged<Map> map, Tagged<SharedFunctionInfo> shared_info) {
  const bool can_flush_bytecode = HasBytecodeArrayForFlushing(shared_info);

  // We found a BytecodeArray that can be flushed. Increment the age of the SFI.
  if (can_flush_bytecode && !should_keep_ages_unchanged_) {
    MakeOlder(shared_info);
  }

  if (!can_flush_bytecode || !ShouldFlushCode(shared_info)) {
    // If the SharedFunctionInfo doesn't have old bytecode visit the function
    // data strongly.
#ifdef V8_ENABLE_SANDBOX
    VisitIndirectPointer(shared_info,
                         shared_info->RawIndirectPointerField(
                             SharedFunctionInfo::kTrustedFunctionDataOffset,
                             kUnknownIndirectPointerTag),
                         IndirectPointerMode::kStrong);
#else
    VisitPointer(
        shared_info,
        shared_info->RawField(SharedFunctionInfo::kTrustedFunctionDataOffset));
#endif
    VisitPointer(shared_info,
                 shared_info->RawField(
                     SharedFunctionInfo::kUntrustedFunctionDataOffset));
  } else if (!IsByteCodeFlushingEnabled(code_flush_mode_)) {
    // If bytecode flushing is disabled but baseline code flushing is enabled
    // then we have to visit the bytecode but not the baseline code.
    DCHECK(IsBaselineCodeFlushingEnabled(code_flush_mode_));
    Tagged<Code> baseline_code = shared_info->baseline_code(kAcquireLoad);
    // Visit the bytecode hanging off baseline code.
    VisitProtectedPointer(
        baseline_code, baseline_code->RawProtectedPointerField(
                           Code::kDeoptimizationDataOrInterpreterDataOffset));
    local_weak_objects_->code_flushing_candidates_local.Push(shared_info);
  } else {
    // In other cases, record as a flushing candidate since we have old
    // bytecode.
    local_weak_objects_->code_flushing_candidates_local.Push(shared_info);
  }
  return Base::VisitSharedFunctionInfo(map, shared_info);
}

template <typename ConcreteVisitor>
bool MarkingVisitorBase<ConcreteVisitor>::HasBytecodeArrayForFlushing(
    Tagged<SharedFunctionInfo> sfi) const {
  if (IsFlushingDisabled(code_flush_mode_)) return false;

  // TODO(rmcilroy): Enable bytecode flushing for resumable functions.
  if (IsResumableFunction(sfi->kind()) || !sfi->allows_lazy_compilation()) {
    return false;
  }

  // Get a snapshot of the function data field, and if it is a bytecode array,
  // check if it is old. Note, this is done this way since this function can be
  // called by the concurrent marker.
  Tagged<Object> data = sfi->GetTrustedData(heap_->isolate());
  if (IsCode(data)) {
    Tagged<Code> baseline_code = Cast<Code>(data);
    DCHECK_EQ(baseline_code->kind(), CodeKind::BASELINE);
    // If baseline code flushing isn't enabled and we have baseline data on SFI
    // we cannot flush baseline / bytecode.
    if (!IsBaselineCodeFlushingEnabled(code_flush_mode_)) return false;
    data = baseline_code->bytecode_or_interpreter_data();
  } else if (!IsByteCodeFlushingEnabled(code_flush_mode_)) {
    // If bytecode flushing isn't enabled and there is no baseline code there is
    // nothing to flush.
    return false;
  }

  return IsBytecodeArray(data);
}

template <typename ConcreteVisitor>
bool MarkingVisitorBase<ConcreteVisitor>::ShouldFlushCode(
    Tagged<SharedFunctionInfo> sfi) const {
  return IsStressFlushingEnabled(code_flush_mode_) || IsOld(sfi);
}

template <typename ConcreteVisitor>
bool MarkingVisitorBase<ConcreteVisitor>::IsOld(
    Tagged<SharedFunctionInfo> sfi) const {
  if (v8_flags.flush_code_based_on_time) {
    return sfi->age() >= v8_flags.bytecode_old_time;
  } else if (v8_flags.flush_code_based_on_tab_visibility) {
    return isolate_in_background_ ||
           V8_UNLIKELY(sfi->age() == SharedFunctionInfo::kMaxAge);
  } else {
    return sfi->age() >= v8_flags.bytecode_old_age;
  }
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::MakeOlder(
    Tagged<SharedFunctionInfo> sfi) const {
  if (v8_flags.flush_code_based_on_time) {
    if (code_flushing_increase_ == 0) {
      return;
    }

    uint16_t current_age;
    uint16_t updated_age;
    do {
      current_age = sfi->age();
      // When the age is 0, it was reset by the function prologue in
      // Ignition/Sparkplug. But that might have been some time after the last
      // full GC. So in this case we don't increment the value like we normally
      // would but just set the age to 1. All non-0 values can be incremented as
      // expected (we add the number of seconds since the last GC) as they were
      // definitely last executed before the last full GC.
      updated_age = current_age == 0
                        ? 1
                        : SaturateAdd(current_age, code_flushing_increase_);
    } while (sfi->CompareExchangeAge(current_age, updated_age) != current_age);
  } else if (v8_flags.flush_code_based_on_tab_visibility) {
    // No need to increment age.
  } else {
    uint16_t age = sfi->age();
    if (age < v8_flags.bytecode_old_age) {
      sfi->CompareExchangeAge(age, age + 1);
    }
    DCHECK_LE(sfi->age(), v8_flags.bytecode_old_age);
  }
}

template <typename ConcreteVisitor>
bool MarkingVisitorBase<ConcreteVisitor>::ShouldFlushBaselineCode(
    Tagged<JSFunction> js_function) const {
  if (!IsBaselineCodeFlushingEnabled(code_flush_mode_)) return false;
  // Do a raw read for shared and code fields here since this function may be
  // called on a concurrent thread. JSFunction itself should be fully
  // initialized here but the SharedFunctionInfo, InstructionStream objects may
  // not be initialized. We read using acquire loads to defend against that.
  Tagged<Object> maybe_shared =
      ACQUIRE_READ_FIELD(js_function, JSFunction::kSharedFunctionInfoOffset);
  if (!IsSharedFunctionInfo(maybe_shared)) return false;

  // See crbug.com/v8/11972 for more details on acquire / release semantics for
  // code field. We don't use release stores when copying code pointers from
  // SFI / FV to JSFunction but it is safe in practice.
  Tagged<Object> maybe_code =
      js_function->raw_code(heap_->isolate(), kAcquireLoad);

#ifdef THREAD_SANITIZER
  // This is needed because TSAN does not process the memory fence
  // emitted after page initialization.
  MemoryChunk::FromAddress(maybe_code.ptr())->SynchronizedLoad();
#endif
  if (!IsCode(maybe_code)) return false;
  Tagged<Code> code = Cast<Code>(maybe_code);
  if (code->kind() != CodeKind::BASELINE) return false;

  Tagged<SharedFunctionInfo> shared = Cast<SharedFunctionInfo>(maybe_shared);
  return HasBytecodeArrayForFlushing(shared) && ShouldFlushCode(shared);
}

// ===========================================================================
// Fixed arrays that need incremental processing =============================
// ===========================================================================

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitFixedArrayWithProgressBar(
    Tagged<Map> map, Tagged<FixedArray> object, ProgressBar& progress_bar) {
  const int kProgressBarScanningChunk = kMaxRegularHeapObjectSize;
  static_assert(kMaxRegularHeapObjectSize % kTaggedSize == 0);
  DCHECK(concrete_visitor()->marking_state()->IsMarked(object));
  const int size = FixedArray::BodyDescriptor::SizeOf(map, object);
  const size_t current_progress_bar = progress_bar.Value();
  int start = static_cast<int>(current_progress_bar);
  if (start == 0) {
    concrete_visitor()
        ->template VisitMapPointerIfNeeded<VisitorId::kVisitFixedArray>(object);
    start = FixedArray::BodyDescriptor::kStartOffset;
  }
  const int end = std::min(size, start + kProgressBarScanningChunk);
  if (start < end) {
    VisitPointers(object, object->RawField(start), object->RawField(end));
    const bool success = progress_bar.TrySetNewValue(current_progress_bar, end);
    CHECK(success);
    if (end < size) {
      // The object can be pushed back onto the marking worklist only after
      // progress bar was updated.
#ifdef DEBUG
      const auto target_worklist =
          MarkingHelper::ShouldMarkObject(heap_, object);
      DCHECK(target_worklist);
      DCHECK_EQ(target_worklist.value(),
                MarkingHelper::WorklistTarget::kRegular);
#endif  // DEBUG
      local_marking_worklists_->Push(object);
    }
  }
  return end - start;
}

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitFixedArray(
    Tagged<Map> map, Tagged<FixedArray> object) {
  ProgressBar& progress_bar =
      MutablePageMetadata::FromHeapObject(object)->ProgressBar();
  return concrete_visitor()->CanUpdateValuesInHeap() && progress_bar.IsEnabled()
             ? VisitFixedArrayWithProgressBar(map, object, progress_bar)
             : Base::VisitFixedArray(map, object);
}

// ===========================================================================
// Custom visitation =========================================================
// ===========================================================================

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitJSArrayBuffer(
    Tagged<Map> map, Tagged<JSArrayBuffer> object) {
  object->MarkExtension();
  return Base::VisitJSArrayBuffer(map, object);
}

// ===========================================================================
// Weak JavaScript objects ===================================================
// ===========================================================================

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitEphemeronHashTable(
    Tagged<Map> map, Tagged<EphemeronHashTable> table) {
  local_weak_objects_->ephemeron_hash_tables_local.Push(table);

  for (InternalIndex i : table->IterateEntries()) {
    ObjectSlot key_slot =
        table->RawFieldOfElementAt(EphemeronHashTable::EntryToIndex(i));
    Tagged<HeapObject> key = Cast<HeapObject>(table->KeyAt(i));

    SynchronizePageAccess(key);
    concrete_visitor()->RecordSlot(table, key_slot, key);
    concrete_visitor()->AddWeakReferenceForReferenceSummarizer(table, key);

    ObjectSlot value_slot =
        table->RawFieldOfElementAt(EphemeronHashTable::EntryToValueIndex(i));

    // Objects in the shared heap are prohibited from being used as keys in
    // WeakMaps and WeakSets and therefore cannot be ephemeron keys. See also
    // MarkCompactCollector::ProcessEphemeron.
    DCHECK(!InWritableSharedSpace(key));
    if (InReadOnlySpace(key) ||
        concrete_visitor()->marking_state()->IsMarked(key)) {
      VisitPointer(table, value_slot);
    } else {
      Tagged<Object> value_obj = table->ValueAt(i);

      if (IsHeapObject(value_obj)) {
        Tagged<HeapObject> value = Cast<HeapObject>(value_obj);
        SynchronizePageAccess(value);
        concrete_visitor()->RecordSlot(table, value_slot, value);
        concrete_visitor()->AddWeakReferenceForReferenceSummarizer(table,
                                                                   value);

        const auto target_worklist =
            MarkingHelper::ShouldMarkObject(heap_, value);
        if (!target_worklist) {
          continue;
        }

        // Revisit ephemerons with both key and value unreachable at end
        // of concurrent marking cycle.
        if (concrete_visitor()->marking_state()->IsUnmarked(value)) {
          local_weak_objects_->discovered_ephemerons_local.Push(
              Ephemeron{key, value});
        }
      }
    }
  }
  return table->SizeFromMap(map);
}

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitJSWeakRef(
    Tagged<Map> map, Tagged<JSWeakRef> weak_ref) {
  if (IsHeapObject(weak_ref->target())) {
    Tagged<HeapObject> target = Cast<HeapObject>(weak_ref->target());
    SynchronizePageAccess(target);
    concrete_visitor()->AddWeakReferenceForReferenceSummarizer(weak_ref,
                                                               target);
    if (InReadOnlySpace(target) ||
        concrete_visitor()->marking_state()->IsMarked(target)) {
      // Record the slot inside the JSWeakRef, since the VisitJSWeakRef above
      // didn't visit it.
      ObjectSlot slot = weak_ref->RawField(JSWeakRef::kTargetOffset);
      concrete_visitor()->RecordSlot(weak_ref, slot, target);
    } else {
      // JSWeakRef points to a potentially dead object. We have to process them
      // when we know the liveness of the whole transitive closure.
      local_weak_objects_->js_weak_refs_local.Push(weak_ref);
    }
  }
  return Base::VisitJSWeakRef(map, weak_ref);
}

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitWeakCell(
    Tagged<Map> map, Tagged<WeakCell> weak_cell) {
  Tagged<HeapObject> target = weak_cell->relaxed_target();
  Tagged<HeapObject> unregister_token = weak_cell->relaxed_unregister_token();
  SynchronizePageAccess(target);
  SynchronizePageAccess(unregister_token);
  if ((InReadOnlySpace(target) ||
       concrete_visitor()->marking_state()->IsMarked(target)) &&
      (InReadOnlySpace(unregister_token) ||
       concrete_visitor()->marking_state()->IsMarked(unregister_token))) {
    // Record the slots inside the WeakCell, since its IterateBody doesn't visit
    // it.
    ObjectSlot slot = weak_cell->RawField(WeakCell::kTargetOffset);
    concrete_visitor()->RecordSlot(weak_cell, slot, target);
    slot = weak_cell->RawField(WeakCell::kUnregisterTokenOffset);
    concrete_visitor()->RecordSlot(weak_cell, slot, unregister_token);
  } else {
    // WeakCell points to a potentially dead object or a dead unregister
    // token. We have to process them when we know the liveness of the whole
    // transitive closure.
    local_weak_objects_->weak_cells_local.Push(weak_cell);
    concrete_visitor()->AddWeakReferenceForReferenceSummarizer(weak_cell,
                                                               target);
    concrete_visitor()->AddWeakReferenceForReferenceSummarizer(
        weak_cell, unregister_token);
  }
  return Base::VisitWeakCell(map, weak_cell);
}

// ===========================================================================
// Custom weakness in descriptor arrays and transition arrays ================
// ===========================================================================

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitDescriptorArrayStrongly(
    Tagged<Map> map, Tagged<DescriptorArray> array) {
  this->template VisitMapPointerIfNeeded<VisitorId::kVisitDescriptorArray>(
      array);
  const int size = DescriptorArray::BodyDescriptor::SizeOf(map, array);
  VisitPointers(array, array->GetFirstPointerSlot(),
                array->GetDescriptorSlot(0));
  VisitPointers(array, MaybeObjectSlot(array->GetDescriptorSlot(0)),
                MaybeObjectSlot(
                    array->GetDescriptorSlot(array->number_of_descriptors())));
  return size;
}

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitDescriptorArray(
    Tagged<Map> map, Tagged<DescriptorArray> array) {
  if (!concrete_visitor()->CanUpdateValuesInHeap()) {
    // If we cannot update the values in the heap, we just treat the array
    // strongly.
    return VisitDescriptorArrayStrongly(map, array);
  }

  // The markbit is not used anymore. This is different from a checked
  // transition in that the array is re-added to the worklist and thus there's
  // many invocations of this transition. All cases (roots, marking via map,
  // write barrier) are handled here as they all update the state accordingly.
  const auto [start, end] =
      DescriptorArrayMarkingState::AcquireDescriptorRangeToMark(
          mark_compact_epoch_, array);
  if (start != end) {
    DCHECK_LT(start, end);
    VisitPointers(array, MaybeObjectSlot(array->GetDescriptorSlot(start)),
                  MaybeObjectSlot(array->GetDescriptorSlot(end)));
    if (start == 0) {
      // We are processing the object the first time. Visit the header and
      // return a size for accounting.
      int size = DescriptorArray::BodyDescriptor::SizeOf(map, array);
      VisitPointers(array, array->GetFirstPointerSlot(),
                    array->GetDescriptorSlot(0));
      concrete_visitor()
          ->template VisitMapPointerIfNeeded<VisitorId::kVisitDescriptorArray>(
              array);
      return size;
    }
  }
  return 0;
}

template <typename ConcreteVisitor>
void MarkingVisitorBase<ConcreteVisitor>::VisitDescriptorsForMap(
    Tagged<Map> map) {
  if (!concrete_visitor()->CanUpdateValuesInHeap() || !map->CanTransition())
    return;

  // Maps that can transition share their descriptor arrays and require
  // special visiting logic to avoid memory leaks.
  // Since descriptor arrays are potentially shared, ensure that only the
  // descriptors that belong to this map are marked. The first time a
  // non-empty descriptor array is marked, its header is also visited. The
  // slot holding the descriptor array will be implicitly recorded when the
  // pointer fields of this map are visited.
  Tagged<Object> maybe_descriptors =
      TaggedField<Object, Map::kInstanceDescriptorsOffset>::Acquire_Load(
          heap_->isolate(), map);

  // If the descriptors are a Smi, then this Map is in the process of being
  // deserialized, and doesn't yet have an initialized descriptor field.
  if (IsSmi(maybe_descriptors)) {
    DCHECK_EQ(maybe_descriptors, Smi::uninitialized_deserialization_value());
    return;
  }

  Tagged<DescriptorArray> descriptors =
      Cast<DescriptorArray>(maybe_descriptors);
  // Synchronize reading of page flags for tsan.
  SynchronizePageAccess(descriptors);
  // Normal processing of descriptor arrays through the pointers iteration that
  // follows this call:
  // - Array in read only space;
  // - StrongDescriptor array;
  if (InReadOnlySpace(descriptors) || IsStrongDescriptorArray(descriptors)) {
    return;
  }

  const int number_of_own_descriptors = map->NumberOfOwnDescriptors();
  if (number_of_own_descriptors) {
    // It is possible that the concurrent marker observes the
    // number_of_own_descriptors out of sync with the descriptors. In that
    // case the marking write barrier for the descriptor array will ensure
    // that all required descriptors are marked. The concurrent marker
    // just should avoid crashing in that case. That's why we need the
    // std::min<int>() below.
    const auto descriptors_to_mark = std::min<int>(
        number_of_own_descriptors, descriptors->number_of_descriptors());
    concrete_visitor()->marking_state()->TryMark(descriptors);
    if (DescriptorArrayMarkingState::TryUpdateIndicesToMark(
            mark_compact_epoch_, descriptors, descriptors_to_mark)) {
#ifdef DEBUG
      const auto target_worklist =
          MarkingHelper::ShouldMarkObject(heap_, descriptors);
      DCHECK(target_worklist);
      DCHECK_EQ(target_worklist.value(),
                MarkingHelper::WorklistTarget::kRegular);
#endif  // DEBUG
      local_marking_worklists_->Push(descriptors);
    }
  }
}

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitMap(Tagged<Map> meta_map,
                                                  Tagged<Map> map) {
  VisitDescriptorsForMap(map);
  // Mark the pointer fields of the Map. If there is a transitions array, it has
  // been marked already, so it is fine that one of these fields contains a
  // pointer to it.
  return Base::VisitMap(meta_map, map);
}

template <typename ConcreteVisitor>
int MarkingVisitorBase<ConcreteVisitor>::VisitTransitionArray(
    Tagged<Map> map, Tagged<TransitionArray> array) {
  local_weak_objects_->transition_arrays_local.Push(array);
  return Base::VisitTransitionArray(map, array);
}

template <typename ConcreteVisitor>
void FullMarkingVisitorBase<ConcreteVisitor>::MarkPointerTableEntry(
    Tagged<HeapObject> host, IndirectPointerSlot slot) {
#ifdef V8_ENABLE_SANDBOX
  DCHECK_NE(slot.tag(), kUnknownIndirectPointerTag);

  IndirectPointerHandle handle = slot.Relaxed_LoadHandle();

  // We must not see an uninitialized 'self' indirect pointer as we might
  // otherwise fail to mark the table entry as alive.
  DCHECK_NE(handle, kNullIndirectPointerHandle);

  if (slot.tag() == kCodeIndirectPointerTag) {
    CodePointerTable* table = GetProcessWideCodePointerTable();
    CodePointerTable::Space* space = this->heap_->code_pointer_space();
    table->Mark(space, handle);
  } else {
    TrustedPointerTable* table = this->trusted_pointer_table_;
    TrustedPointerTable::Space* space = this->heap_->trusted_pointer_space();
    table->Mark(space, handle);
  }
#else
  UNREACHABLE();
#endif
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_HEAP_MARKING_VISITOR_INL_H_
