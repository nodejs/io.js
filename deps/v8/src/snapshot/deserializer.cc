// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/deserializer.h"

#include "src/base/logging.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/reloc-info-inl.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap-inl.h"
#include "src/logging/local-logger.h"
#include "src/logging/log.h"
#include "src/objects/backing-store.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/maybe-object.h"
#include "src/objects/objects-body-descriptors-inl.h"
#include "src/objects/objects.h"
#include "src/objects/slots.h"
#include "src/objects/string.h"
#include "src/roots/roots.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/snapshot/references.h"
#include "src/snapshot/serializer-deserializer.h"
#include "src/snapshot/shared-heap-serializer.h"
#include "src/snapshot/snapshot-data.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {

// A SlotAccessor for a slot in a HeapObject, which abstracts the slot
// operations done by the deserializer in a way which is GC-safe. In particular,
// rather than an absolute slot address, this accessor holds a Handle to the
// HeapObject, which is updated if the HeapObject moves.
class SlotAccessorForHeapObject {
 public:
  static SlotAccessorForHeapObject ForSlotIndex(Handle<HeapObject> object,
                                                int index) {
    return SlotAccessorForHeapObject(object, index * kTaggedSize);
  }
  static SlotAccessorForHeapObject ForSlotOffset(Handle<HeapObject> object,
                                                 int offset) {
    return SlotAccessorForHeapObject(object, offset);
  }

  MaybeObjectSlot slot() const { return object_->RawMaybeWeakField(offset_); }
  ExternalPointerSlot external_pointer_slot(ExternalPointerTag tag) const {
    return object_->RawExternalPointerField(offset_, tag);
  }
  Handle<HeapObject> object() const { return object_; }
  int offset() const { return offset_; }

  // Writes the given value to this slot, optionally with an offset (e.g. for
  // repeat writes). Returns the number of slots written (which is one).
  int Write(Tagged<MaybeObject> value, int slot_offset = 0) {
    MaybeObjectSlot current_slot = slot() + slot_offset;
    current_slot.Relaxed_Store(value);
    CombinedWriteBarrier(*object_, current_slot, value, UPDATE_WRITE_BARRIER);
    return 1;
  }
  int Write(Tagged<HeapObject> value, HeapObjectReferenceType ref_type,
            int slot_offset = 0) {
    return Write(Tagged<HeapObjectReference>(value, ref_type), slot_offset);
  }
  int Write(DirectHandle<HeapObject> value, HeapObjectReferenceType ref_type,
            int slot_offset = 0) {
    return Write(*value, ref_type, slot_offset);
  }

  int WriteIndirectPointerTo(Tagged<HeapObject> value) {
    // Only ExposedTrustedObjects can be referenced via indirect pointers, so
    // we must have one of these objects here. See the comments in
    // trusted-object.h for more details.
    DCHECK(IsExposedTrustedObject(value));
    Tagged<ExposedTrustedObject> object = Cast<ExposedTrustedObject>(value);

    InstanceType instance_type = value->map()->instance_type();
    IndirectPointerTag tag = IndirectPointerTagFromInstanceType(instance_type);
    IndirectPointerSlot dest = object_->RawIndirectPointerField(offset_, tag);
    dest.store(object);

    IndirectPointerWriteBarrier(*object_, dest, value, UPDATE_WRITE_BARRIER);
    return 1;
  }

  int WriteProtectedPointerTo(Tagged<TrustedObject> value) {
    DCHECK(IsTrustedObject(*object_));
    Tagged<TrustedObject> host = Cast<TrustedObject>(*object_);
    ProtectedPointerSlot dest = host->RawProtectedPointerField(offset_);
    dest.store(value);
    ProtectedPointerWriteBarrier(host, dest, value, UPDATE_WRITE_BARRIER);
    return 1;
  }

 private:
  SlotAccessorForHeapObject(Handle<HeapObject> object, int offset)
      : object_(object), offset_(offset) {}

  const Handle<HeapObject> object_;
  const int offset_;
};

// A SlotAccessor for absolute full slot addresses.
class SlotAccessorForRootSlots {
 public:
  explicit SlotAccessorForRootSlots(FullMaybeObjectSlot slot) : slot_(slot) {}

  FullMaybeObjectSlot slot() const { return slot_; }
  ExternalPointerSlot external_pointer_slot(ExternalPointerTag tag) const {
    UNREACHABLE();
  }
  Handle<HeapObject> object() const { UNREACHABLE(); }
  int offset() const { UNREACHABLE(); }

  // Writes the given value to this slot, optionally with an offset (e.g. for
  // repeat writes). Returns the number of slots written (which is one).
  int Write(Tagged<MaybeObject> value, int slot_offset = 0) {
    FullMaybeObjectSlot current_slot = slot() + slot_offset;
    current_slot.Relaxed_Store(value);
    return 1;
  }
  int Write(Tagged<HeapObject> value, HeapObjectReferenceType ref_type,
            int slot_offset = 0) {
    return Write(Tagged<HeapObjectReference>(value, ref_type), slot_offset);
  }
  int Write(DirectHandle<HeapObject> value, HeapObjectReferenceType ref_type,
            int slot_offset = 0) {
    return Write(*value, ref_type, slot_offset);
  }
  int WriteIndirectPointerTo(Tagged<HeapObject> value) { UNREACHABLE(); }
  int WriteProtectedPointerTo(Tagged<TrustedObject> value) { UNREACHABLE(); }

 private:
  const FullMaybeObjectSlot slot_;
};

// A SlotAccessor for creating a Handle, which saves a Handle allocation when
// a Handle already exists.
template <typename IsolateT>
class SlotAccessorForHandle {
 public:
  SlotAccessorForHandle(Handle<HeapObject>* handle, IsolateT* isolate)
      : handle_(handle), isolate_(isolate) {}

  MaybeObjectSlot slot() const { UNREACHABLE(); }
  ExternalPointerSlot external_pointer_slot(ExternalPointerTag tag) const {
    UNREACHABLE();
  }
  Handle<HeapObject> object() const { UNREACHABLE(); }
  int offset() const { UNREACHABLE(); }

  int Write(Tagged<MaybeObject> value, int slot_offset = 0) { UNREACHABLE(); }
  int Write(Tagged<HeapObject> value, HeapObjectReferenceType ref_type,
            int slot_offset = 0) {
    DCHECK_EQ(slot_offset, 0);
    DCHECK_EQ(ref_type, HeapObjectReferenceType::STRONG);
    *handle_ = handle(value, isolate_);
    return 1;
  }
  int Write(Handle<HeapObject> value, HeapObjectReferenceType ref_type,
            int slot_offset = 0) {
    DCHECK_EQ(slot_offset, 0);
    DCHECK_EQ(ref_type, HeapObjectReferenceType::STRONG);
    *handle_ = value;
    return 1;
  }
  int WriteIndirectPointerTo(Tagged<HeapObject> value) { UNREACHABLE(); }
  int WriteProtectedPointerTo(Tagged<TrustedObject> value) { UNREACHABLE(); }

 private:
  Handle<HeapObject>* handle_;
  IsolateT* isolate_;
};

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::WriteHeapPointer(SlotAccessor slot_accessor,
                                             Tagged<HeapObject> heap_object,
                                             ReferenceDescriptor descr) {
  if (descr.is_indirect_pointer) {
    return slot_accessor.WriteIndirectPointerTo(heap_object);
  } else {
    return slot_accessor.Write(heap_object, descr.type);
  }
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::WriteHeapPointer(SlotAccessor slot_accessor,
                                             Handle<HeapObject> heap_object,
                                             ReferenceDescriptor descr) {
  if (descr.is_indirect_pointer) {
    return slot_accessor.WriteIndirectPointerTo(*heap_object);
  } else if (descr.is_protected_pointer) {
    DCHECK(IsTrustedObject(*heap_object));
    return slot_accessor.WriteProtectedPointerTo(
        Cast<TrustedObject>(*heap_object));
  } else {
    return slot_accessor.Write(heap_object, descr.type);
  }
}

template <typename IsolateT>
int Deserializer<IsolateT>::WriteExternalPointer(Tagged<HeapObject> host,
                                                 ExternalPointerSlot dest,
                                                 Address value) {
  DCHECK(!next_reference_is_weak_ && !next_reference_is_indirect_pointer_ &&
         !next_reference_is_protected_pointer);

  #ifdef V8_ENABLE_SANDBOX
  ExternalPointerTable::ManagedResource* managed_resource = nullptr;
  ExternalPointerTable* owning_table = nullptr;
  ExternalPointerHandle original_handle = kNullExternalPointerHandle;
  if (IsManagedExternalPointerType(dest.tag())) {
    // This can currently only happen during snapshot stress mode as we cannot
    // normally serialized managed resources. In snapshot stress mode, the new
    // isolate will be destroyed and the old isolate (really, the old isolate's
    // external pointer table) therefore effectively retains ownership of the
    // resource. As such, we need to save and restore the relevant fields of
    // the external resource. Once the external pointer table itself destroys
    // the managed resource when freeing the corresponding table entry, this
    // workaround can be removed again.
    DCHECK(v8_flags.stress_snapshot);
    managed_resource =
        reinterpret_cast<ExternalPointerTable::ManagedResource*>(value);
    owning_table = managed_resource->owning_table_;
    original_handle = managed_resource->ept_entry_;
    managed_resource->owning_table_ = nullptr;
    managed_resource->ept_entry_ = kNullExternalPointerHandle;
  }
#endif  // V8_ENABLE_SANDBOX

  dest.init(main_thread_isolate(), host, value);

#ifdef V8_ENABLE_SANDBOX
  if (managed_resource) {
    managed_resource->owning_table_ = owning_table;
    managed_resource->ept_entry_ = original_handle;
  }
#endif  // V8_ENABLE_SANDBOX

  // ExternalPointers can only be written into HeapObject fields, therefore they
  // cover (kExternalPointerSlotSize / kTaggedSize) slots.
  return (kExternalPointerSlotSize / kTaggedSize);
}

namespace {
#ifdef DEBUG
int GetNumApiReferences(Isolate* isolate) {
  int num_api_references = 0;
  // The read-only deserializer is run by read-only heap set-up before the
  // heap is fully set up. External reference table relies on a few parts of
  // this set-up (like old-space), so it may be uninitialized at this point.
  if (isolate->isolate_data()->external_reference_table()->is_initialized()) {
    // Count the number of external references registered through the API.
    if (isolate->api_external_references() != nullptr) {
      while (isolate->api_external_references()[num_api_references] != 0) {
        num_api_references++;
      }
    }
  }
  return num_api_references;
}
int GetNumApiReferences(LocalIsolate* isolate) { return 0; }
#endif
}  // namespace

template <typename IsolateT>
Deserializer<IsolateT>::Deserializer(IsolateT* isolate,
                                     base::Vector<const uint8_t> payload,
                                     uint32_t magic_number,
                                     bool deserializing_user_code,
                                     bool can_rehash)
    : isolate_(isolate),
      source_(payload),
      magic_number_(magic_number),
      new_descriptor_arrays_(isolate->heap()),
      deserializing_user_code_(deserializing_user_code),
      should_rehash_((v8_flags.rehash_snapshot && can_rehash) ||
                     deserializing_user_code) {
  DCHECK_NOT_NULL(isolate);
  isolate->RegisterDeserializerStarted();

  // We start the indices here at 1, so that we can distinguish between an
  // actual index and an empty backing store (serialized as
  // kEmptyBackingStoreRefSentinel) in a deserialized object requiring fix-up.
  static_assert(kEmptyBackingStoreRefSentinel == 0);
  backing_stores_.push_back({});

#ifdef DEBUG
  num_api_references_ = GetNumApiReferences(isolate);
#endif  // DEBUG
  CHECK_EQ(magic_number_, SerializedData::kMagicNumber);
}

template <typename IsolateT>
void Deserializer<IsolateT>::Rehash() {
  DCHECK(should_rehash());
  for (DirectHandle<HeapObject> item : to_rehash_) {
    item->RehashBasedOnMap(isolate());
  }
}

template <typename IsolateT>
Deserializer<IsolateT>::~Deserializer() {
#ifdef DEBUG
  // Do not perform checks if we aborted deserialization.
  if (source_.position() == 0) return;
  // Check that we only have padding bytes remaining.
  while (source_.HasMore()) DCHECK_EQ(kNop, source_.Get());
  // Check that there are no remaining forward refs.
  DCHECK_EQ(num_unresolved_forward_refs_, 0);
  DCHECK(unresolved_forward_refs_.empty());
#endif  // DEBUG
  isolate_->RegisterDeserializerFinished();
}

// This is called on the roots.  It is the driver of the deserialization
// process.  It is also called on the body of each function.
template <typename IsolateT>
void Deserializer<IsolateT>::VisitRootPointers(Root root,
                                               const char* description,
                                               FullObjectSlot start,
                                               FullObjectSlot end) {
  ReadData(FullMaybeObjectSlot(start), FullMaybeObjectSlot(end));
}

template <typename IsolateT>
void Deserializer<IsolateT>::Synchronize(VisitorSynchronization::SyncTag tag) {
  static const uint8_t expected = kSynchronize;
  CHECK_EQ(expected, source_.Get());
}

template <typename IsolateT>
void Deserializer<IsolateT>::DeserializeDeferredObjects() {
  for (int code = source_.Get(); code != kSynchronize; code = source_.Get()) {
    SnapshotSpace space = NewObject::Decode(code);
    ReadObject(space);
  }
}

template <typename IsolateT>
void Deserializer<IsolateT>::LogNewMapEvents() {
  if (V8_LIKELY(!v8_flags.log_maps)) return;
  DisallowGarbageCollection no_gc;
  for (DirectHandle<Map> map : new_maps_) {
    DCHECK(v8_flags.log_maps);
    LOG(isolate(), MapCreate(*map));
    LOG(isolate(), MapDetails(*map));
  }
}

template <typename IsolateT>
void Deserializer<IsolateT>::WeakenDescriptorArrays() {
  isolate()->heap()->WeakenDescriptorArrays(std::move(new_descriptor_arrays_));
}

template <typename IsolateT>
void Deserializer<IsolateT>::LogScriptEvents(Tagged<Script> script) {
  DisallowGarbageCollection no_gc;
  LOG(isolate(), ScriptEvent(ScriptEventType::kDeserialize, script->id()));
  LOG(isolate(), ScriptDetails(script));
}

namespace {
template <typename IsolateT>
uint32_t ComputeRawHashField(IsolateT* isolate, Tagged<String> string) {
  // Make sure raw_hash_field() is computed.
  string->EnsureHash(SharedStringAccessGuardIfNeeded(isolate));
  return string->raw_hash_field();
}
}  // namespace

StringTableInsertionKey::StringTableInsertionKey(
    Isolate* isolate, DirectHandle<String> string,
    DeserializingUserCodeOption deserializing_user_code)
    : StringTableKey(ComputeRawHashField(isolate, *string), string->length()),
      string_(string) {
#ifdef DEBUG
  deserializing_user_code_ = deserializing_user_code;
#endif
  DCHECK(IsInternalizedString(*string));
}

StringTableInsertionKey::StringTableInsertionKey(
    LocalIsolate* isolate, DirectHandle<String> string,
    DeserializingUserCodeOption deserializing_user_code)
    : StringTableKey(ComputeRawHashField(isolate, *string), string->length()),
      string_(string) {
#ifdef DEBUG
  deserializing_user_code_ = deserializing_user_code;
#endif
  DCHECK(IsInternalizedString(*string));
}

template <typename IsolateT>
bool StringTableInsertionKey::IsMatch(IsolateT* isolate,
                                      Tagged<String> string) {
  // We want to compare the content of two strings here.
  return string_->SlowEquals(string, SharedStringAccessGuardIfNeeded(isolate));
}
template bool StringTableInsertionKey::IsMatch(Isolate* isolate,
                                               Tagged<String> string);
template bool StringTableInsertionKey::IsMatch(LocalIsolate* isolate,
                                               Tagged<String> string);

namespace {

void NoExternalReferencesCallback() {
  // The following check will trigger if a function or object template
  // with references to native functions have been deserialized from
  // snapshot, but no actual external references were provided when the
  // isolate was created.
  FATAL("No external references provided via API");
}

void PostProcessExternalString(Tagged<ExternalString> string,
                               Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  uint32_t index = string->GetResourceRefForDeserialization();
  Address address =
      static_cast<Address>(isolate->api_external_references()[index]);
  string->InitExternalPointerFields(isolate);
  string->set_address_as_resource(isolate, address);
  isolate->heap()->UpdateExternalString(string, 0,
                                        string->ExternalPayloadSize());
  isolate->heap()->RegisterExternalString(string);
}

}  // namespace

// Should be called only on the main thread (not thread safe).
template <>
void Deserializer<Isolate>::PostProcessNewJSReceiver(
    Tagged<Map> map, DirectHandle<JSReceiver> obj, InstanceType instance_type,
    SnapshotSpace space) {
  DCHECK_EQ(map->instance_type(), instance_type);

  if (InstanceTypeChecker::IsJSDataView(instance_type) ||
      InstanceTypeChecker::IsJSRabGsabDataView(instance_type)) {
    auto data_view = Cast<JSDataViewOrRabGsabDataView>(*obj);
    auto buffer = Cast<JSArrayBuffer>(data_view->buffer());
    if (buffer->was_detached()) {
      // Directly set the data pointer to point to the EmptyBackingStoreBuffer.
      // Otherwise, we might end up setting it to EmptyBackingStoreBuffer() +
      // byte_offset() which would result in an invalid pointer.
      data_view->set_data_pointer(main_thread_isolate(),
                                  EmptyBackingStoreBuffer());
    } else {
      void* backing_store = buffer->backing_store();
      data_view->set_data_pointer(
          main_thread_isolate(),
          reinterpret_cast<uint8_t*>(backing_store) + data_view->byte_offset());
    }
  } else if (InstanceTypeChecker::IsJSTypedArray(instance_type)) {
    auto typed_array = Cast<JSTypedArray>(*obj);
    // Note: ByteArray objects must not be deferred s.t. they are
    // available here for is_on_heap(). See also: CanBeDeferred.
    // Fixup typed array pointers.
    if (typed_array->is_on_heap()) {
      typed_array->AddExternalPointerCompensationForDeserialization(
          main_thread_isolate());
    } else {
      // Serializer writes backing store ref as a DataPtr() value.
      uint32_t store_index =
          typed_array->GetExternalBackingStoreRefForDeserialization();
      auto backing_store = backing_stores_[store_index];
      void* start = backing_store ? backing_store->buffer_start() : nullptr;
      if (!start) start = EmptyBackingStoreBuffer();
      typed_array->SetOffHeapDataPtr(main_thread_isolate(), start,
                                     typed_array->byte_offset());
    }
  } else if (InstanceTypeChecker::IsJSArrayBuffer(instance_type)) {
    auto buffer = Cast<JSArrayBuffer>(*obj);
    uint32_t store_index = buffer->GetBackingStoreRefForDeserialization();
    buffer->init_extension();
    if (store_index == kEmptyBackingStoreRefSentinel) {
      buffer->set_backing_store(main_thread_isolate(),
                                EmptyBackingStoreBuffer());
    } else {
      auto bs = backing_store(store_index);
      SharedFlag shared =
          bs && bs->is_shared() ? SharedFlag::kShared : SharedFlag::kNotShared;
      DCHECK_IMPLIES(bs,
                     buffer->is_resizable_by_js() == bs->is_resizable_by_js());
      ResizableFlag resizable = bs && bs->is_resizable_by_js()
                                    ? ResizableFlag::kResizable
                                    : ResizableFlag::kNotResizable;
      buffer->Setup(shared, resizable, bs, main_thread_isolate());
    }
  }
}

template <>
void Deserializer<LocalIsolate>::PostProcessNewJSReceiver(
    Tagged<Map> map, DirectHandle<JSReceiver> obj, InstanceType instance_type,
    SnapshotSpace space) {
  UNREACHABLE();
}

template <typename IsolateT>
void Deserializer<IsolateT>::PostProcessNewObject(DirectHandle<Map> map,
                                                  Handle<HeapObject> obj,
                                                  SnapshotSpace space) {
  DisallowGarbageCollection no_gc;
  Tagged<Map> raw_map = *map;
  DCHECK_EQ(raw_map, obj->map(isolate_));
  InstanceType instance_type = raw_map->instance_type();
  Tagged<HeapObject> raw_obj = *obj;
  DCHECK_IMPLIES(deserializing_user_code(), should_rehash());
  if (should_rehash()) {
    if (InstanceTypeChecker::IsString(instance_type)) {
      // Uninitialize hash field as we need to recompute the hash.
      Tagged<String> string = Cast<String>(raw_obj);
      string->set_raw_hash_field(String::kEmptyHashField);
      // Rehash strings before read-only space is sealed. Strings outside
      // read-only space are rehashed lazily. (e.g. when rehashing dictionaries)
      if (space == SnapshotSpace::kReadOnlyHeap) {
        PushObjectToRehash(obj);
      }
    } else if (raw_obj->NeedsRehashing(instance_type)) {
      PushObjectToRehash(obj);
    }

    if (deserializing_user_code()) {
      if (InstanceTypeChecker::IsInternalizedString(instance_type)) {
        // Canonicalize the internalized string. If it already exists in the
        // string table, set the string to point to the existing one and patch
        // the deserialized string handle to point to the existing one.
        // TODO(leszeks): This handle patching is ugly, consider adding an
        // explicit internalized string bytecode. Also, the new thin string
        // should be dead, try immediately freeing it.
        Handle<String> string = Cast<String>(obj);

        StringTableInsertionKey key(
            isolate(), string,
            DeserializingUserCodeOption::kIsDeserializingUserCode);
        Tagged<String> result =
            *isolate()->string_table()->LookupKey(isolate(), &key);

        if (result != raw_obj) {
          Cast<String>(raw_obj)->MakeThin(isolate(), result);
          // Mutate the given object handle so that the backreference entry is
          // also updated.
          obj.PatchValue(result);
        }
        return;
      } else if (InstanceTypeChecker::IsScript(instance_type)) {
        new_scripts_.push_back(Cast<Script>(obj));
      } else if (InstanceTypeChecker::IsAllocationSite(instance_type)) {
        // We should link new allocation sites, but we can't do this immediately
        // because |AllocationSite::HasWeakNext()| internally accesses
        // |Heap::roots_| that may not have been initialized yet. So defer this
        // to |ObjectDeserializer::CommitPostProcessedObjects()|.
        new_allocation_sites_.push_back(Cast<AllocationSite>(obj));
      } else {
        // We dont defer ByteArray because JSTypedArray needs the base_pointer
        // ByteArray immediately if it's on heap.
        DCHECK(CanBeDeferred(*obj, SlotType::kAnySlot) ||
               InstanceTypeChecker::IsByteArray(instance_type));
      }
    }
  }

  if (InstanceTypeChecker::IsInstructionStream(instance_type)) {
    // We flush all code pages after deserializing the startup snapshot.
    // Hence we only remember each individual code object when deserializing
    // user code.
    if (deserializing_user_code()) {
      new_code_objects_.push_back(Cast<InstructionStream>(obj));
    }
  } else if (InstanceTypeChecker::IsCode(instance_type)) {
    Tagged<Code> code = Cast<Code>(raw_obj);
    if (!code->has_instruction_stream()) {
      code->SetInstructionStartForOffHeapBuiltin(
          main_thread_isolate(), EmbeddedData::FromBlob(main_thread_isolate())
                                     .InstructionStartOf(code->builtin_id()));
    } else {
      code->UpdateInstructionStart(main_thread_isolate(),
                                   code->instruction_stream());
    }
  } else if (InstanceTypeChecker::IsSharedFunctionInfo(instance_type)) {
    Tagged<SharedFunctionInfo> sfi = Cast<SharedFunctionInfo>(raw_obj);
    // Reset the id to avoid collisions - it must be unique in this isolate.
    sfi->set_unique_id(isolate()->GetAndIncNextUniqueSfiId());
  } else if (InstanceTypeChecker::IsMap(instance_type)) {
    if (v8_flags.log_maps) {
      // Keep track of all seen Maps to log them later since they might be only
      // partially initialized at this point.
      new_maps_.push_back(Cast<Map>(obj));
    }
  } else if (InstanceTypeChecker::IsAccessorInfo(instance_type)) {
#ifdef USE_SIMULATOR
    accessor_infos_.push_back(Cast<AccessorInfo>(obj));
#endif
  } else if (InstanceTypeChecker::IsFunctionTemplateInfo(instance_type)) {
#ifdef USE_SIMULATOR
    function_template_infos_.push_back(Cast<FunctionTemplateInfo>(obj));
#endif
  } else if (InstanceTypeChecker::IsExternalString(instance_type)) {
    PostProcessExternalString(Cast<ExternalString>(raw_obj),
                              main_thread_isolate());
  } else if (InstanceTypeChecker::IsJSReceiver(instance_type)) {
    // PostProcessNewJSReceiver may trigger GC.
    no_gc.Release();
    return PostProcessNewJSReceiver(raw_map, Cast<JSReceiver>(obj),
                                    instance_type, space);
  } else if (InstanceTypeChecker::IsDescriptorArray(instance_type)) {
    DCHECK(InstanceTypeChecker::IsStrongDescriptorArray(instance_type));
    auto descriptors = Cast<DescriptorArray>(obj);
    new_descriptor_arrays_.Push(*descriptors);
  } else if (InstanceTypeChecker::IsNativeContext(instance_type)) {
    Cast<NativeContext>(raw_obj)->init_microtask_queue(main_thread_isolate(),
                                                       nullptr);
  } else if (InstanceTypeChecker::IsScript(instance_type)) {
    LogScriptEvents(Cast<Script>(*obj));
  }
}

template <typename IsolateT>
typename Deserializer<IsolateT>::ReferenceDescriptor
Deserializer<IsolateT>::GetAndResetNextReferenceDescriptor() {
  DCHECK(!(next_reference_is_weak_ && next_reference_is_indirect_pointer_));
  ReferenceDescriptor desc;
  desc.type = next_reference_is_weak_ ? HeapObjectReferenceType::WEAK
                                      : HeapObjectReferenceType::STRONG;
  next_reference_is_weak_ = false;
  desc.is_indirect_pointer = next_reference_is_indirect_pointer_;
  next_reference_is_indirect_pointer_ = false;
  desc.is_protected_pointer = next_reference_is_protected_pointer;
  next_reference_is_protected_pointer = false;
  return desc;
}

template <typename IsolateT>
Handle<HeapObject> Deserializer<IsolateT>::GetBackReferencedObject() {
  Handle<HeapObject> obj = back_refs_[source_.GetUint30()];

  // We don't allow ThinStrings in backreferences -- if internalization produces
  // a thin string, then it should also update the backref handle.
  DCHECK(!IsThinString(*obj, isolate()));

  hot_objects_.Add(obj);
  DCHECK(!HasWeakHeapObjectTag(*obj));
  return obj;
}

template <typename IsolateT>
Handle<HeapObject> Deserializer<IsolateT>::ReadObject() {
  Handle<HeapObject> ret;
  CHECK_EQ(ReadSingleBytecodeData(
               source_.Get(), SlotAccessorForHandle<IsolateT>(&ret, isolate())),
           1);
  return ret;
}

namespace {
AllocationType SpaceToAllocation(SnapshotSpace space) {
  switch (space) {
    case SnapshotSpace::kCode:
      return AllocationType::kCode;
    case SnapshotSpace::kOld:
      return AllocationType::kOld;
    case SnapshotSpace::kReadOnlyHeap:
      return AllocationType::kReadOnly;
    case SnapshotSpace::kTrusted:
      return AllocationType::kTrusted;
  }
}
}  // namespace

template <typename IsolateT>
Handle<HeapObject> Deserializer<IsolateT>::ReadObject(SnapshotSpace space) {
  const int size_in_tagged = source_.GetUint30();
  const int size_in_bytes = size_in_tagged * kTaggedSize;

  // The map can't be a forward ref. If you want the map to be a forward ref,
  // then you're probably serializing the meta-map, in which case you want to
  // use the kNewContextlessMetaMap/kNewContextfulMetaMap bytecode.
  DCHECK_NE(source()->Peek(), kRegisterPendingForwardRef);
  Handle<Map> map = Cast<Map>(ReadObject());

  AllocationType allocation = SpaceToAllocation(space);

  // When sharing a string table, all in-place internalizable and internalized
  // strings internalized strings are allocated in the shared heap.
  //
  // TODO(12007): When shipping, add a new SharedOld SnapshotSpace.
  if (v8_flags.shared_string_table) {
    InstanceType instance_type = map->instance_type();
    if (InstanceTypeChecker::IsInternalizedString(instance_type) ||
        String::IsInPlaceInternalizable(instance_type)) {
      allocation = isolate()
                       ->factory()
                       ->RefineAllocationTypeForInPlaceInternalizableString(
                           allocation, *map);
    }
  }

  // Filling an object's fields can cause GCs and heap walks, so this object has
  // to be in a 'sufficiently initialised' state by the time the next allocation
  // can happen. For this to be the case, the object is carefully deserialized
  // as follows:
  //   * The space for the object is allocated.
  //   * The map is set on the object so that the GC knows what type the object
  //     has.
  //   * The rest of the object is filled with a fixed Smi value
  //     - This is a Smi so that tagged fields become initialized to a valid
  //       tagged value.
  //     - It's a fixed value, "Smi::uninitialized_deserialization_value()", so
  //       that we can DCHECK for it when reading objects that are assumed to be
  //       partially initialized objects.
  //   * The fields of the object are deserialized in order, under the
  //     assumption that objects are laid out in such a way that any fields
  //     required for object iteration (e.g. length fields) are deserialized
  //     before fields with objects.
  //     - We ensure this is the case by DCHECKing on object allocation that the
  //       previously allocated object has a valid size (see `Allocate`).
  Tagged<HeapObject> raw_obj =
      Allocate(allocation, size_in_bytes, HeapObject::RequiredAlignment(*map));
  raw_obj->set_map_after_allocation(*map);
  MemsetTagged(raw_obj->RawField(kTaggedSize),
               Smi::uninitialized_deserialization_value(), size_in_tagged - 1);
  DCHECK(raw_obj->CheckRequiredAlignment(isolate()));

  // Make sure BytecodeArrays have a valid age, so that the marker doesn't
  // break when making them older.
  if (IsSharedFunctionInfo(raw_obj, isolate())) {
    Cast<SharedFunctionInfo>(raw_obj)->set_age(0);
  } else if (IsEphemeronHashTable(raw_obj)) {
    // Make sure EphemeronHashTables have valid HeapObject keys, so that the
    // marker does not break when marking EphemeronHashTable, see
    // MarkingVisitorBase::VisitEphemeronHashTable.
    Tagged<EphemeronHashTable> table = Cast<EphemeronHashTable>(raw_obj);
    MemsetTagged(table->RawField(table->kElementsStartOffset),
                 ReadOnlyRoots(isolate()).undefined_value(),
                 (size_in_bytes - table->kElementsStartOffset) / kTaggedSize);
  }

#ifdef DEBUG
  PtrComprCageBase cage_base(isolate());
  // We want to make sure that all embedder pointers are initialized to null.
  if (IsJSObject(raw_obj, cage_base) &&
      Cast<JSObject>(raw_obj)->MayHaveEmbedderFields()) {
    Tagged<JSObject> js_obj = Cast<JSObject>(raw_obj);
    for (int i = 0; i < js_obj->GetEmbedderFieldCount(); ++i) {
      void* pointer;
      CHECK(EmbedderDataSlot(js_obj, i).ToAlignedPointer(main_thread_isolate(),
                                                         &pointer));
      CHECK_NULL(pointer);
    }
  } else if (IsEmbedderDataArray(raw_obj, cage_base)) {
    Tagged<EmbedderDataArray> array = Cast<EmbedderDataArray>(raw_obj);
    EmbedderDataSlot start(array, 0);
    EmbedderDataSlot end(array, array->length());
    for (EmbedderDataSlot slot = start; slot < end; ++slot) {
      void* pointer;
      CHECK(slot.ToAlignedPointer(main_thread_isolate(), &pointer));
      CHECK_NULL(pointer);
    }
  }
#endif

  Handle<HeapObject> obj = handle(raw_obj, isolate());
  back_refs_.push_back(obj);

  ReadData(obj, 1, size_in_tagged);
  PostProcessNewObject(map, obj, space);

#ifdef DEBUG
  if (IsInstructionStream(*obj, cage_base)) {
    DCHECK(space == SnapshotSpace::kCode ||
           space == SnapshotSpace::kReadOnlyHeap);
  } else {
    DCHECK_NE(space, SnapshotSpace::kCode);
  }
  if (IsTrustedObject(*obj)) {
    DCHECK_EQ(space, SnapshotSpace::kTrusted);
  } else {
    DCHECK_NE(space, SnapshotSpace::kTrusted);
  }
#endif  // DEBUG

  return obj;
}

template <typename IsolateT>
Handle<HeapObject> Deserializer<IsolateT>::ReadMetaMap(SnapshotSpace space) {
  const int size_in_bytes = Map::kSize;
  const int size_in_tagged = size_in_bytes / kTaggedSize;

  Tagged<HeapObject> raw_obj =
      Allocate(SpaceToAllocation(space), size_in_bytes, kTaggedAligned);
  raw_obj->set_map_after_allocation(UncheckedCast<Map>(raw_obj));
  MemsetTagged(raw_obj->RawField(kTaggedSize),
               Smi::uninitialized_deserialization_value(), size_in_tagged - 1);
  DCHECK(raw_obj->CheckRequiredAlignment(isolate()));

  Handle<HeapObject> obj = handle(raw_obj, isolate());
  back_refs_.push_back(obj);

  // Set the instance-type manually, to allow backrefs to read it.
  UncheckedCast<Map>(*obj)->set_instance_type(MAP_TYPE);

  ReadData(obj, 1, size_in_tagged);
  PostProcessNewObject(Cast<Map>(obj), obj, space);

  return obj;
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadRepeatedObject(SlotAccessor slot_accessor,
                                               int repeat_count) {
  CHECK_LE(2, repeat_count);

  Handle<HeapObject> heap_object = ReadObject();
  DCHECK(!Heap::InYoungGeneration(*heap_object));
  for (int i = 0; i < repeat_count; i++) {
    // TODO(leszeks): Use a ranged barrier here.
    slot_accessor.Write(heap_object, HeapObjectReferenceType::STRONG, i);
  }
  return repeat_count;
}

namespace {

// Template used by the below CASE_RANGE macro to statically verify that the
// given number of cases matches the number of expected cases for that bytecode.
template <int byte_code_count, int expected>
constexpr uint8_t VerifyBytecodeCount(uint8_t bytecode) {
  static_assert(byte_code_count == expected);
  return bytecode;
}

}  // namespace

// Helper macro (and its implementation detail) for specifying a range of cases.
// Use as "case CASE_RANGE(byte_code, num_bytecodes):"
#define CASE_RANGE(byte_code, num_bytecodes) \
  CASE_R##num_bytecodes(                     \
      (VerifyBytecodeCount<byte_code##Count, num_bytecodes>(byte_code)))
#define CASE_R1(byte_code) byte_code
#define CASE_R2(byte_code) CASE_R1(byte_code) : case CASE_R1(byte_code + 1)
#define CASE_R3(byte_code) CASE_R2(byte_code) : case CASE_R1(byte_code + 2)
#define CASE_R4(byte_code) CASE_R2(byte_code) : case CASE_R2(byte_code + 2)
#define CASE_R8(byte_code) CASE_R4(byte_code) : case CASE_R4(byte_code + 4)
#define CASE_R16(byte_code) CASE_R8(byte_code) : case CASE_R8(byte_code + 8)
#define CASE_R32(byte_code) CASE_R16(byte_code) : case CASE_R16(byte_code + 16)

// This generates a case range for all the spaces.
// clang-format off
#define CASE_RANGE_ALL_SPACES(bytecode)                                \
  SpaceEncoder<bytecode>::Encode(SnapshotSpace::kOld):                 \
    case SpaceEncoder<bytecode>::Encode(SnapshotSpace::kCode):         \
    case SpaceEncoder<bytecode>::Encode(SnapshotSpace::kReadOnlyHeap): \
    case SpaceEncoder<bytecode>::Encode(SnapshotSpace::kTrusted)
// clang-format on

template <typename IsolateT>
void Deserializer<IsolateT>::ReadData(Handle<HeapObject> object,
                                      int start_slot_index,
                                      int end_slot_index) {
  int current = start_slot_index;
  while (current < end_slot_index) {
    uint8_t data = source_.Get();
    current += ReadSingleBytecodeData(
        data, SlotAccessorForHeapObject::ForSlotIndex(object, current));
  }
  CHECK_EQ(current, end_slot_index);
}

template <typename IsolateT>
void Deserializer<IsolateT>::ReadData(FullMaybeObjectSlot start,
                                      FullMaybeObjectSlot end) {
  FullMaybeObjectSlot current = start;
  while (current < end) {
    uint8_t data = source_.Get();
    current += ReadSingleBytecodeData(data, SlotAccessorForRootSlots(current));
  }
  CHECK_EQ(current, end);
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadSingleBytecodeData(uint8_t data,
                                                   SlotAccessor slot_accessor) {
  switch (data) {
    case CASE_RANGE_ALL_SPACES(kNewObject):
      return ReadNewObject(data, slot_accessor);
    case kBackref:
      return ReadBackref(data, slot_accessor);
    case kReadOnlyHeapRef:
      return ReadReadOnlyHeapRef(data, slot_accessor);
    case kRootArray:
      return ReadRootArray(data, slot_accessor);
    case kStartupObjectCache:
      return ReadStartupObjectCache(data, slot_accessor);
    case kSharedHeapObjectCache:
      return ReadSharedHeapObjectCache(data, slot_accessor);
    case kNewContextlessMetaMap:
    case kNewContextfulMetaMap:
      return ReadNewMetaMap(data, slot_accessor);
    case kSandboxedExternalReference:
    case kExternalReference:
      return ReadExternalReference(data, slot_accessor);
    case kSandboxedRawExternalReference:
      return ReadRawExternalReference(data, slot_accessor);
    case kAttachedReference:
      return ReadAttachedReference(data, slot_accessor);
    case kNop:
      return 0;
    case kRegisterPendingForwardRef:
      return ReadRegisterPendingForwardRef(data, slot_accessor);
    case kResolvePendingForwardRef:
      return ReadResolvePendingForwardRef(data, slot_accessor);
    case kSynchronize:
      //  If we get here then that indicates that you have a mismatch between
      //  the number of GC roots when serializing and deserializing.
      UNREACHABLE();
    case kVariableRawData:
      return ReadVariableRawData(data, slot_accessor);
    case kVariableRepeat:
      return ReadVariableRepeat(data, slot_accessor);
    case kOffHeapBackingStore:
    case kOffHeapResizableBackingStore:
      return ReadOffHeapBackingStore(data, slot_accessor);
    case kSandboxedApiReference:
    case kApiReference:
      return ReadApiReference(data, slot_accessor);
    case kClearedWeakReference:
      return ReadClearedWeakReference(data, slot_accessor);
    case kWeakPrefix:
      return ReadWeakPrefix(data, slot_accessor);
    case kIndirectPointerPrefix:
      return ReadIndirectPointerPrefix(data, slot_accessor);
    case kInitializeSelfIndirectPointer:
      return ReadInitializeSelfIndirectPointer(data, slot_accessor);
    case kProtectedPointerPrefix:
      return ReadProtectedPointerPrefix(data, slot_accessor);
    case CASE_RANGE(kRootArrayConstants, 32):
      return ReadRootArrayConstants(data, slot_accessor);
    case CASE_RANGE(kHotObject, 8):
      return ReadHotObject(data, slot_accessor);
    case CASE_RANGE(kFixedRawData, 32):
      return ReadFixedRawData(data, slot_accessor);
    case CASE_RANGE(kFixedRepeat, 16):
      return ReadFixedRepeat(data, slot_accessor);

#ifdef DEBUG
#define UNUSED_CASE(byte_code) \
  case byte_code:              \
    UNREACHABLE();
      UNUSED_SERIALIZER_BYTE_CODES(UNUSED_CASE)
#endif
#undef UNUSED_CASE
  }

  // The above switch, including UNUSED_SERIALIZER_BYTE_CODES, covers all
  // possible bytecodes; but, clang doesn't realize this, so we have an explicit
  // UNREACHABLE here too.
  UNREACHABLE();
}

// Deserialize a new object and write a pointer to it to the current
// object.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadNewObject(uint8_t data,
                                          SlotAccessor slot_accessor) {
  SnapshotSpace space = NewObject::Decode(data);
  DCHECK_IMPLIES(V8_STATIC_ROOTS_BOOL, space != SnapshotSpace::kReadOnlyHeap);
  // Save the descriptor before recursing down into reading the object.
  ReferenceDescriptor descr = GetAndResetNextReferenceDescriptor();
  Handle<HeapObject> heap_object = ReadObject(space);
  return WriteHeapPointer(slot_accessor, heap_object, descr);
}

// Find a recently deserialized object using its offset from the current
// allocation point and write a pointer to it to the current object.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadBackref(uint8_t data,
                                        SlotAccessor slot_accessor) {
  Handle<HeapObject> heap_object = GetBackReferencedObject();
  return WriteHeapPointer(slot_accessor, heap_object,
                          GetAndResetNextReferenceDescriptor());
}

// Reference an object in the read-only heap.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadReadOnlyHeapRef(uint8_t data,
                                                SlotAccessor slot_accessor) {
  uint32_t chunk_index = source_.GetUint30();
  uint32_t chunk_offset = source_.GetUint30();

  ReadOnlySpace* read_only_space = isolate()->heap()->read_only_space();
  ReadOnlyPageMetadata* page = read_only_space->pages()[chunk_index];
  Address address = page->OffsetToAddress(chunk_offset);
  Tagged<HeapObject> heap_object = HeapObject::FromAddress(address);

  return WriteHeapPointer(slot_accessor, heap_object,
                          GetAndResetNextReferenceDescriptor());
}

// Find an object in the roots array and write a pointer to it to the
// current object.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadRootArray(uint8_t data,
                                          SlotAccessor slot_accessor) {
  int id = source_.GetUint30();
  RootIndex root_index = static_cast<RootIndex>(id);
  Handle<HeapObject> heap_object =
      Cast<HeapObject>(isolate()->root_handle(root_index));
  hot_objects_.Add(heap_object);
  return WriteHeapPointer(slot_accessor, heap_object,
                          GetAndResetNextReferenceDescriptor());
}

// Find an object in the startup object cache and write a pointer to it to
// the current object.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadStartupObjectCache(uint8_t data,
                                                   SlotAccessor slot_accessor) {
  int cache_index = source_.GetUint30();
  // TODO(leszeks): Could we use the address of the startup_object_cache
  // entry as a Handle backing?
  Tagged<HeapObject> heap_object = Cast<HeapObject>(
      main_thread_isolate()->startup_object_cache()->at(cache_index));
  return WriteHeapPointer(slot_accessor, heap_object,
                          GetAndResetNextReferenceDescriptor());
}

// Find an object in the shared heap object cache and write a pointer to it
// to the current object.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadSharedHeapObjectCache(
    uint8_t data, SlotAccessor slot_accessor) {
  int cache_index = source_.GetUint30();
  // TODO(leszeks): Could we use the address of the
  // shared_heap_object_cache entry as a Handle backing?
  Tagged<HeapObject> heap_object = Cast<HeapObject>(
      main_thread_isolate()->shared_heap_object_cache()->at(cache_index));
  DCHECK(SharedHeapSerializer::ShouldBeInSharedHeapObjectCache(heap_object));
  return WriteHeapPointer(slot_accessor, heap_object,
                          GetAndResetNextReferenceDescriptor());
}

// Deserialize a new meta-map and write a pointer to it to the current
// object.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadNewMetaMap(uint8_t data,
                                           SlotAccessor slot_accessor) {
  SnapshotSpace space = data == kNewContextlessMetaMap
                            ? SnapshotSpace::kReadOnlyHeap
                            : SnapshotSpace::kOld;
  Handle<HeapObject> heap_object = ReadMetaMap(space);
  return slot_accessor.Write(heap_object, HeapObjectReferenceType::STRONG);
}

// Find an external reference and write a pointer to it to the current
// object.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadExternalReference(uint8_t data,
                                                  SlotAccessor slot_accessor) {
  DCHECK_IMPLIES(data == kSandboxedExternalReference, V8_ENABLE_SANDBOX_BOOL);
  Address address = ReadExternalReferenceCase();
  ExternalPointerTag tag = kExternalPointerNullTag;
  if (data == kSandboxedExternalReference) {
    tag = ReadExternalPointerTag();
  }
  return WriteExternalPointer(*slot_accessor.object(),
                              slot_accessor.external_pointer_slot(tag),
                              address);
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadRawExternalReference(
    uint8_t data, SlotAccessor slot_accessor) {
  DCHECK_IMPLIES(data == kSandboxedExternalReference, V8_ENABLE_SANDBOX_BOOL);
  Address address;
  source_.CopyRaw(&address, kSystemPointerSize);
  ExternalPointerTag tag = kExternalPointerNullTag;
  if (data == kSandboxedRawExternalReference) {
    tag = ReadExternalPointerTag();
  }
  return WriteExternalPointer(*slot_accessor.object(),
                              slot_accessor.external_pointer_slot(tag),
                              address);
}

// Find an object in the attached references and write a pointer to it to
// the current object.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadAttachedReference(uint8_t data,
                                                  SlotAccessor slot_accessor) {
  int index = source_.GetUint30();
  Handle<HeapObject> heap_object = attached_objects_[index];
  return WriteHeapPointer(slot_accessor, heap_object,
                          GetAndResetNextReferenceDescriptor());
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadRegisterPendingForwardRef(
    uint8_t data, SlotAccessor slot_accessor) {
  ReferenceDescriptor descr = GetAndResetNextReferenceDescriptor();
  unresolved_forward_refs_.emplace_back(slot_accessor.object(),
                                        slot_accessor.offset(), descr);
  num_unresolved_forward_refs_++;
  return 1;
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadResolvePendingForwardRef(
    uint8_t data, SlotAccessor slot_accessor) {
  // Pending forward refs can only be resolved after the heap object's map
  // field is deserialized; currently they only appear immediately after
  // the map field or after the 'self' indirect pointer for trusted objects.
  DCHECK(slot_accessor.offset() == HeapObject::kHeaderSize ||
         slot_accessor.offset() == ExposedTrustedObject::kHeaderSize);
  Handle<HeapObject> obj = slot_accessor.object();
  int index = source_.GetUint30();
  auto& forward_ref = unresolved_forward_refs_[index];
  auto slot = SlotAccessorForHeapObject::ForSlotOffset(forward_ref.object,
                                                       forward_ref.offset);
  WriteHeapPointer(slot, obj, forward_ref.descr);
  num_unresolved_forward_refs_--;
  if (num_unresolved_forward_refs_ == 0) {
    // If there's no more pending fields, clear the entire pending field
    // vector.
    unresolved_forward_refs_.clear();
  } else {
    // Otherwise, at least clear the pending field.
    forward_ref.object = Handle<HeapObject>();
  }
  return 0;
}

// Deserialize raw data of variable length.
template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadVariableRawData(uint8_t data,
                                                SlotAccessor slot_accessor) {
  // This operation is only supported for tagged-size slots, else we might
  // become misaligned.
  DCHECK_EQ(decltype(slot_accessor.slot())::kSlotDataSize, kTaggedSize);
  int size_in_tagged = source_.GetUint30();
  // TODO(leszeks): Only copy slots when there are Smis in the serialized
  // data.
  source_.CopySlots(slot_accessor.slot().location(), size_in_tagged);
  return size_in_tagged;
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadVariableRepeat(uint8_t data,
                                               SlotAccessor slot_accessor) {
  int repeats = VariableRepeatCount::Decode(source_.GetUint30());
  return ReadRepeatedObject(slot_accessor, repeats);
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadOffHeapBackingStore(
    uint8_t data, SlotAccessor slot_accessor) {
  int byte_length = source_.GetUint32();
  std::unique_ptr<BackingStore> backing_store;
  if (data == kOffHeapBackingStore) {
    backing_store = BackingStore::Allocate(main_thread_isolate(), byte_length,
                                           SharedFlag::kNotShared,
                                           InitializedFlag::kUninitialized);
  } else {
    int max_byte_length = source_.GetUint32();
    size_t page_size, initial_pages, max_pages;
    Maybe<bool> result =
        JSArrayBuffer::GetResizableBackingStorePageConfiguration(
            nullptr, byte_length, max_byte_length, kDontThrow, &page_size,
            &initial_pages, &max_pages);
    DCHECK(result.FromJust());
    USE(result);
    backing_store = BackingStore::TryAllocateAndPartiallyCommitMemory(
        main_thread_isolate(), byte_length, max_byte_length, page_size,
        initial_pages, max_pages, WasmMemoryFlag::kNotWasm,
        SharedFlag::kNotShared);
  }
  CHECK_NOT_NULL(backing_store);
  source_.CopyRaw(backing_store->buffer_start(), byte_length);
  backing_stores_.push_back(std::move(backing_store));
  return 0;
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadApiReference(uint8_t data,
                                             SlotAccessor slot_accessor) {
  DCHECK_IMPLIES(data == kSandboxedApiReference, V8_ENABLE_SANDBOX_BOOL);
  uint32_t reference_id = static_cast<uint32_t>(source_.GetUint30());
  Address address;
  if (main_thread_isolate()->api_external_references()) {
    DCHECK_WITH_MSG(reference_id < num_api_references_,
                    "too few external references provided through the API");
    address = static_cast<Address>(
        main_thread_isolate()->api_external_references()[reference_id]);
  } else {
    address = reinterpret_cast<Address>(NoExternalReferencesCallback);
  }
  ExternalPointerTag tag = kExternalPointerNullTag;
  if (data == kSandboxedApiReference) {
    tag = ReadExternalPointerTag();
  }
  return WriteExternalPointer(*slot_accessor.object(),
                              slot_accessor.external_pointer_slot(tag),
                              address);
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadClearedWeakReference(
    uint8_t data, SlotAccessor slot_accessor) {
  return slot_accessor.Write(ClearedValue(isolate()));
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadWeakPrefix(uint8_t data,
                                           SlotAccessor slot_accessor) {
  // We shouldn't have two weak prefixes in a row.
  DCHECK(!next_reference_is_weak_);
  // We shouldn't have weak refs without a current object.
  DCHECK_NE(slot_accessor.object()->address(), kNullAddress);
  next_reference_is_weak_ = true;
  return 0;
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadIndirectPointerPrefix(
    uint8_t data, SlotAccessor slot_accessor) {
  // We shouldn't have two indirect pointer prefixes in a row.
  DCHECK(!next_reference_is_indirect_pointer_);
  // We shouldn't have a indirect pointer prefix without a current object.
  DCHECK_NE(slot_accessor.object()->address(), kNullAddress);
  next_reference_is_indirect_pointer_ = true;
  return 0;
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadInitializeSelfIndirectPointer(
    uint8_t data, SlotAccessor slot_accessor) {
#ifdef V8_ENABLE_SANDBOX
  DCHECK_NE(slot_accessor.object()->address(), kNullAddress);
  DCHECK(IsExposedTrustedObject(*slot_accessor.object()));
  DCHECK_EQ(slot_accessor.offset(),
            ExposedTrustedObject::kSelfIndirectPointerOffset);

  Tagged<ExposedTrustedObject> host =
      Cast<ExposedTrustedObject>(*slot_accessor.object());
  host->init_self_indirect_pointer(isolate());

  return 1;
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadProtectedPointerPrefix(
    uint8_t data, SlotAccessor slot_accessor) {
  // We shouldn't have two protected pointer prefixes in a row.
  DCHECK(!next_reference_is_protected_pointer);
  // We shouldn't have a protected pointer prefix without a current object.
  DCHECK_NE(slot_accessor.object()->address(), kNullAddress);
  next_reference_is_protected_pointer = true;
  return 0;
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadRootArrayConstants(uint8_t data,
                                                   SlotAccessor slot_accessor) {
  // First kRootArrayConstantsCount roots are guaranteed to be in
  // the old space.
  static_assert(static_cast<int>(RootIndex::kFirstImmortalImmovableRoot) == 0);
  static_assert(kRootArrayConstantsCount <=
                static_cast<int>(RootIndex::kLastImmortalImmovableRoot));

  RootIndex root_index = RootArrayConstant::Decode(data);
  Handle<HeapObject> heap_object =
      Cast<HeapObject>(isolate()->root_handle(root_index));
  return slot_accessor.Write(heap_object, HeapObjectReferenceType::STRONG);
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadHotObject(uint8_t data,
                                          SlotAccessor slot_accessor) {
  int index = HotObject::Decode(data);
  Handle<HeapObject> hot_object = hot_objects_.Get(index);
  return WriteHeapPointer(slot_accessor, hot_object,
                          GetAndResetNextReferenceDescriptor());
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadFixedRawData(uint8_t data,
                                             SlotAccessor slot_accessor) {
  using TSlot = decltype(slot_accessor.slot());

  // Deserialize raw data of fixed length from 1 to 32 times kTaggedSize.
  int size_in_tagged = FixedRawDataWithSize::Decode(data);
  static_assert(TSlot::kSlotDataSize == kTaggedSize ||
                TSlot::kSlotDataSize == 2 * kTaggedSize);
  int size_in_slots = size_in_tagged / (TSlot::kSlotDataSize / kTaggedSize);
  // kFixedRawData can have kTaggedSize != TSlot::kSlotDataSize when
  // serializing Smi roots in pointer-compressed builds. In this case, the
  // size in bytes is unconditionally the (full) slot size.
  DCHECK_IMPLIES(kTaggedSize != TSlot::kSlotDataSize, size_in_slots == 1);
  // TODO(leszeks): Only copy slots when there are Smis in the serialized
  // data.
  source_.CopySlots(slot_accessor.slot().location(), size_in_slots);
  return size_in_slots;
}

template <typename IsolateT>
template <typename SlotAccessor>
int Deserializer<IsolateT>::ReadFixedRepeat(uint8_t data,
                                            SlotAccessor slot_accessor) {
  int repeats = FixedRepeatWithCount::Decode(data);
  return ReadRepeatedObject(slot_accessor, repeats);
}

#undef CASE_RANGE_ALL_SPACES
#undef CASE_RANGE
#undef CASE_R32
#undef CASE_R16
#undef CASE_R8
#undef CASE_R4
#undef CASE_R3
#undef CASE_R2
#undef CASE_R1

template <typename IsolateT>
Address Deserializer<IsolateT>::ReadExternalReferenceCase() {
  uint32_t reference_id = static_cast<uint32_t>(source_.GetUint30());
  return main_thread_isolate()->external_reference_table()->address(
      reference_id);
}

template <typename IsolateT>
ExternalPointerTag Deserializer<IsolateT>::ReadExternalPointerTag() {
  uint64_t shifted_tag = static_cast<uint64_t>(source_.GetUint30());
  return static_cast<ExternalPointerTag>(shifted_tag
                                         << kExternalPointerTagShift);
}

template <typename IsolateT>
Tagged<HeapObject> Deserializer<IsolateT>::Allocate(
    AllocationType allocation, int size, AllocationAlignment alignment) {
#ifdef DEBUG
  if (!previous_allocation_obj_.is_null()) {
    // Make sure that the previous object is initialized sufficiently to
    // be iterated over by the GC.
    int object_size = previous_allocation_obj_->Size(isolate_);
    DCHECK_LE(object_size, previous_allocation_size_);
  }
#endif

  Tagged<HeapObject> obj =
      HeapObject::FromAddress(isolate()->heap()->AllocateRawOrFail(
          size, allocation, AllocationOrigin::kRuntime, alignment));

#ifdef DEBUG
  previous_allocation_obj_ = handle(obj, isolate());
  previous_allocation_size_ = size;
#endif

  return obj;
}

template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE) Deserializer<Isolate>;
template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Deserializer<LocalIsolate>;

}  // namespace internal
}  // namespace v8
