// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/serializer.h"

#include "include/v8-internal.h"
#include "src/codegen/assembler-inl.h"
#include "src/common/globals.h"
#include "src/handles/global-handles-inl.h"
#include "src/heap/heap-inl.h"  // For Space::identity().
#include "src/heap/mutable-page-metadata-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/objects/code.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/instance-type-checker.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/map.h"
#include "src/objects/objects-body-descriptors-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/slots.h"
#include "src/objects/smi.h"
#include "src/snapshot/embedded/embedded-data.h"
#include "src/snapshot/serializer-deserializer.h"
#include "src/snapshot/serializer-inl.h"

namespace v8 {
namespace internal {

Serializer::Serializer(Isolate* isolate, Snapshot::SerializerFlags flags)
    : isolate_(isolate),
#if V8_COMPRESS_POINTERS
      cage_base_(isolate),
#endif  // V8_COMPRESS_POINTERS
      hot_objects_(isolate->heap()),
      reference_map_(isolate),
      external_reference_encoder_(isolate),
      root_index_map_(isolate),
      deferred_objects_(isolate->heap()),
      forward_refs_per_pending_object_(isolate->heap()),
      flags_(flags)
#ifdef DEBUG
      ,
      back_refs_(isolate->heap()),
      stack_(isolate->heap())
#endif
{
#ifdef VERBOSE_SERIALIZATION_STATISTICS
  if (v8_flags.serialization_statistics) {
    for (int space = 0; space < kNumberOfSnapshotSpaces; ++space) {
      // Value-initialized to 0.
      instance_type_count_[space] = std::make_unique<int[]>(kInstanceTypes);
      instance_type_size_[space] = std::make_unique<size_t[]>(kInstanceTypes);
    }
  }
#endif  // VERBOSE_SERIALIZATION_STATISTICS
}

#ifdef DEBUG
void Serializer::PopStack() { stack_.Pop(); }
#endif

void Serializer::CountAllocation(Tagged<Map> map, int size,
                                 SnapshotSpace space) {
  DCHECK(v8_flags.serialization_statistics);

  const int space_number = static_cast<int>(space);
  allocation_size_[space_number] += size;
#ifdef VERBOSE_SERIALIZATION_STATISTICS
  int instance_type = map->instance_type();
  instance_type_count_[space_number][instance_type]++;
  instance_type_size_[space_number][instance_type] += size;
#endif  // VERBOSE_SERIALIZATION_STATISTICS
}

int Serializer::TotalAllocationSize() const {
  int sum = 0;
  for (int space = 0; space < kNumberOfSnapshotSpaces; space++) {
    sum += allocation_size_[space];
  }
  return sum;
}

namespace {

const char* ToString(SnapshotSpace space) {
  switch (space) {
    case SnapshotSpace::kReadOnlyHeap:
      return "ReadOnlyHeap";
    case SnapshotSpace::kOld:
      return "Old";
    case SnapshotSpace::kCode:
      return "Code";
    case SnapshotSpace::kTrusted:
      return "Trusted";
  }
}

}  // namespace

void Serializer::OutputStatistics(const char* name) {
  if (!v8_flags.serialization_statistics) return;

  PrintF("%s:\n", name);
  if (!serializer_tracks_serialization_statistics()) {
    PrintF("  <serialization statistics are not tracked>\n");
    return;
  }

  PrintF("  Spaces (bytes):\n");

  static constexpr SnapshotSpace kAllSnapshotSpaces[] = {
      SnapshotSpace::kReadOnlyHeap,
      SnapshotSpace::kOld,
      SnapshotSpace::kCode,
  };

  for (SnapshotSpace space : kAllSnapshotSpaces) {
    PrintF("%16s", ToString(space));
  }
  PrintF("\n");

  for (SnapshotSpace space : kAllSnapshotSpaces) {
    PrintF("%16zu", allocation_size_[static_cast<int>(space)]);
  }
  PrintF("\n");

#ifdef VERBOSE_SERIALIZATION_STATISTICS
  PrintF("  Instance types (count and bytes):\n");
#define PRINT_INSTANCE_TYPE(Name)                                           \
  for (SnapshotSpace space : kAllSnapshotSpaces) {                          \
    const int space_i = static_cast<int>(space);                            \
    if (instance_type_count_[space_i][Name]) {                              \
      PrintF("%10d %10zu  %-10s %s\n", instance_type_count_[space_i][Name], \
             instance_type_size_[space_i][Name], ToString(space), #Name);   \
    }                                                                       \
  }
  INSTANCE_TYPE_LIST(PRINT_INSTANCE_TYPE)
#undef PRINT_INSTANCE_TYPE
  PrintF("\n");
#endif  // VERBOSE_SERIALIZATION_STATISTICS
}

void Serializer::SerializeDeferredObjects() {
  if (v8_flags.trace_serializer) {
    PrintF("Serializing deferred objects\n");
  }
  WHILE_WITH_HANDLE_SCOPE(isolate(), !deferred_objects_.empty(), {
    Handle<HeapObject> obj = handle(deferred_objects_.Pop(), isolate());

    ObjectSerializer obj_serializer(this, obj, &sink_);
    obj_serializer.SerializeDeferred();
  });
  sink_.Put(kSynchronize, "Finished with deferred objects");
}

void Serializer::SerializeObject(Handle<HeapObject> obj, SlotType slot_type) {
  // ThinStrings are just an indirection to an internalized string, so elide the
  // indirection and serialize the actual string directly.
  if (IsThinString(*obj, isolate())) {
    obj = handle(Cast<ThinString>(*obj)->actual(), isolate());
  } else if (IsCode(*obj, isolate())) {
    Tagged<Code> code = Cast<Code>(*obj);
    if (code->kind() == CodeKind::BASELINE) {
      // For now just serialize the BytecodeArray instead of baseline code.
      // TODO(v8:11429,pthier): Handle Baseline code in cases we want to
      // serialize it.
      obj = handle(code->bytecode_or_interpreter_data(), isolate());
    }
  }
  SerializeObjectImpl(obj, slot_type);
}

bool Serializer::MustBeDeferred(Tagged<HeapObject> object) { return false; }

void Serializer::VisitRootPointers(Root root, const char* description,
                                   FullObjectSlot start, FullObjectSlot end) {
  for (FullObjectSlot current = start; current < end; ++current) {
    SerializeRootObject(current);
  }
}

void Serializer::SerializeRootObject(FullObjectSlot slot) {
  Tagged<Object> o = *slot;
  if (IsSmi(o)) {
    PutSmiRoot(slot);
  } else {
    SerializeObject(Handle<HeapObject>(slot.location()), SlotType::kAnySlot);
  }
}

#ifdef DEBUG
void Serializer::PrintStack() { PrintStack(std::cout); }

void Serializer::PrintStack(std::ostream& out) {
  for (const auto o : stack_) {
    Print(*o, out);
    out << "\n";
  }
}
#endif  // DEBUG

bool Serializer::SerializeRoot(Tagged<HeapObject> obj) {
  RootIndex root_index;
  // Derived serializers are responsible for determining if the root has
  // actually been serialized before calling this.
  if (root_index_map()->Lookup(obj, &root_index)) {
    PutRoot(root_index);
    return true;
  }
  return false;
}

bool Serializer::SerializeHotObject(Tagged<HeapObject> obj) {
  DisallowGarbageCollection no_gc;
  // Encode a reference to a hot object by its index in the working set.
  int index = hot_objects_.Find(obj);
  if (index == HotObjectsList::kNotFound) return false;
  DCHECK(index >= 0 && index < kHotObjectCount);
  if (v8_flags.trace_serializer) {
    PrintF(" Encoding hot object %d:", index);
    ShortPrint(obj);
    PrintF("\n");
  }
  sink_.Put(HotObject::Encode(index), "HotObject");
  return true;
}

bool Serializer::SerializeBackReference(Tagged<HeapObject> obj) {
  DisallowGarbageCollection no_gc;
  const SerializerReference* reference = reference_map_.LookupReference(obj);
  if (reference == nullptr) return false;
  // Encode the location of an already deserialized object in order to write
  // its location into a later object.  We can encode the location as an
  // offset fromthe start of the deserialized objects or as an offset
  // backwards from the current allocation pointer.
  if (reference->is_attached_reference()) {
    if (v8_flags.trace_serializer) {
      PrintF(" Encoding attached reference %d\n",
             reference->attached_reference_index());
    }
    PutAttachedReference(*reference);
  } else {
    DCHECK(reference->is_back_reference());
    if (v8_flags.trace_serializer) {
      PrintF(" Encoding back reference to: ");
      ShortPrint(obj);
      PrintF("\n");
    }

    sink_.Put(kBackref, "Backref");
    PutBackReference(obj, *reference);
  }
  return true;
}

bool Serializer::SerializePendingObject(Tagged<HeapObject> obj) {
  PendingObjectReferences* refs_to_object =
      forward_refs_per_pending_object_.Find(obj);
  if (refs_to_object == nullptr) {
    return false;
  }
  PutPendingForwardReference(*refs_to_object);
  return true;
}

bool Serializer::ObjectIsBytecodeHandler(Tagged<HeapObject> obj) const {
  if (!IsCode(obj)) return false;
  return (Cast<Code>(obj)->kind() == CodeKind::BYTECODE_HANDLER);
}

void Serializer::PutRoot(RootIndex root) {
  DisallowGarbageCollection no_gc;
  int root_index = static_cast<int>(root);
  Tagged<HeapObject> object = Cast<HeapObject>(isolate()->root(root));
  if (v8_flags.trace_serializer) {
    PrintF(" Encoding root %d:", root_index);
    ShortPrint(object);
    PrintF("\n");
  }

  // Assert that the first 32 root array items are a conscious choice. They are
  // chosen so that the most common ones can be encoded more efficiently.
  static_assert(static_cast<int>(RootIndex::kArgumentsMarker) ==
                kRootArrayConstantsCount - 1);

  // TODO(ulan): Check that it works with young large objects.
  if (root_index < kRootArrayConstantsCount &&
      !Heap::InYoungGeneration(object)) {
    sink_.Put(RootArrayConstant::Encode(root), "RootConstant");
  } else {
    sink_.Put(kRootArray, "RootSerialization");
    sink_.PutUint30(root_index, "root_index");
    hot_objects_.Add(object);
  }
}

void Serializer::PutSmiRoot(FullObjectSlot slot) {
  // Serializing a smi root in compressed pointer builds will serialize the
  // full object slot (of kSystemPointerSize) to avoid complications during
  // deserialization (endianness or smi sequences).
  static_assert(decltype(slot)::kSlotDataSize == sizeof(Address));
  static_assert(decltype(slot)::kSlotDataSize == kSystemPointerSize);
  static constexpr int bytes_to_output = decltype(slot)::kSlotDataSize;
  static constexpr int size_in_tagged = bytes_to_output >> kTaggedSizeLog2;
  sink_.Put(FixedRawDataWithSize::Encode(size_in_tagged), "Smi");

  Address raw_value = Cast<Smi>(*slot).ptr();
  const uint8_t* raw_value_as_bytes =
      reinterpret_cast<const uint8_t*>(&raw_value);
  sink_.PutRaw(raw_value_as_bytes, bytes_to_output, "Bytes");
}

void Serializer::PutBackReference(Tagged<HeapObject> object,
                                  SerializerReference reference) {
  DCHECK_EQ(object, *back_refs_[reference.back_ref_index()]);
  sink_.PutUint30(reference.back_ref_index(), "BackRefIndex");
  hot_objects_.Add(object);
}

void Serializer::PutAttachedReference(SerializerReference reference) {
  DCHECK(reference.is_attached_reference());
  sink_.Put(kAttachedReference, "AttachedRef");
  sink_.PutUint30(reference.attached_reference_index(), "AttachedRefIndex");
}

void Serializer::PutRepeat(int repeat_count) {
  if (repeat_count <= kLastEncodableFixedRepeatCount) {
    sink_.Put(FixedRepeatWithCount::Encode(repeat_count), "FixedRepeat");
  } else {
    sink_.Put(kVariableRepeat, "VariableRepeat");
    sink_.PutUint30(VariableRepeatCount::Encode(repeat_count), "repeat count");
  }
}

void Serializer::PutPendingForwardReference(PendingObjectReferences& refs) {
  sink_.Put(kRegisterPendingForwardRef, "RegisterPendingForwardRef");
  unresolved_forward_refs_++;
  // Register the current slot with the pending object.
  int forward_ref_id = next_forward_ref_id_++;
  if (refs == nullptr) {
    // The IdentityMap holding the pending object reference vectors does not
    // support non-trivial types; in particular it doesn't support destructors
    // on values. So, we manually allocate a vector with new, and delete it when
    // resolving the pending object.
    refs = new std::vector<int>();
  }
  refs->push_back(forward_ref_id);
}

void Serializer::ResolvePendingForwardReference(int forward_reference_id) {
  sink_.Put(kResolvePendingForwardRef, "ResolvePendingForwardRef");
  sink_.PutUint30(forward_reference_id, "with this index");
  unresolved_forward_refs_--;

  // If there are no more unresolved forward refs, reset the forward ref id to
  // zero so that future forward refs compress better.
  if (unresolved_forward_refs_ == 0) {
    next_forward_ref_id_ = 0;
  }
}

ExternalReferenceEncoder::Value Serializer::EncodeExternalReference(
    Address addr) {
  Maybe<ExternalReferenceEncoder::Value> result =
      external_reference_encoder_.TryEncode(addr);
  if (result.IsNothing()) {
#ifdef DEBUG
    PrintStack(std::cerr);
#endif
    void* addr_ptr = reinterpret_cast<void*>(addr);
    v8::base::OS::PrintError("Unknown external reference %p.\n", addr_ptr);
    v8::base::OS::PrintError("%s\n",
                             ExternalReferenceTable::ResolveSymbol(addr_ptr));
    v8::base::OS::Abort();
  }
  return result.FromJust();
}

void Serializer::RegisterObjectIsPending(Tagged<HeapObject> obj) {
  DisallowGarbageCollection no_gc;
  if (IsNotMappedSymbol(obj)) return;

  // Add the given object to the pending objects -> forward refs map.
  auto find_result = forward_refs_per_pending_object_.FindOrInsert(obj);
  USE(find_result);

  // If the above emplace didn't actually add the object, then the object must
  // already have been registered pending by deferring. It might not be in the
  // deferred objects queue though, since it may be the very object we just
  // popped off that queue, so just check that it can be deferred.
  DCHECK_IMPLIES(find_result.already_exists, *find_result.entry != nullptr);
  DCHECK_IMPLIES(find_result.already_exists,
                 CanBeDeferred(obj, SlotType::kAnySlot));
}

void Serializer::ResolvePendingObject(Tagged<HeapObject> obj) {
  DisallowGarbageCollection no_gc;
  if (IsNotMappedSymbol(obj)) return;

  std::vector<int>* refs;
  CHECK(forward_refs_per_pending_object_.Delete(obj, &refs));
  if (refs) {
    for (int index : *refs) {
      ResolvePendingForwardReference(index);
    }
    // See PutPendingForwardReference -- we have to manually manage the memory
    // of non-trivial IdentityMap values.
    delete refs;
  }
}

void Serializer::Pad(int padding_offset) {
  // The non-branching GetInt will read up to 3 bytes too far, so we need
  // to pad the snapshot to make sure we don't read over the end.
  for (unsigned i = 0; i < sizeof(int32_t) - 1; i++) {
    sink_.Put(kNop, "Padding");
  }
  // Pad up to pointer size for checksum.
  while (!IsAligned(sink_.Position() + padding_offset, kPointerAlignment)) {
    sink_.Put(kNop, "Padding");
  }
}

void Serializer::InitializeCodeAddressMap() {
  isolate_->InitializeLoggingAndCounters();
  code_address_map_ = std::make_unique<CodeAddressMap>(isolate_);
}

Tagged<InstructionStream> Serializer::CopyCode(
    Tagged<InstructionStream> istream) {
  code_buffer_.clear();  // Clear buffer without deleting backing store.
  // Add InstructionStream padding which is usually added by the allocator.
  // While this doesn't guarantee the exact same alignment, it's enough to
  // fulfill the alignment requirements of writes during relocation.
  code_buffer_.resize(InstructionStream::kCodeAlignmentMinusCodeHeader);
  int size = istream->Size();
  code_buffer_.insert(code_buffer_.end(),
                      reinterpret_cast<uint8_t*>(istream.address()),
                      reinterpret_cast<uint8_t*>(istream.address() + size));
  // When pointer compression is enabled the checked cast will try to
  // decompress map field of off-heap InstructionStream object.
  return UncheckedCast<InstructionStream>(
      HeapObject::FromAddress(reinterpret_cast<Address>(
          &code_buffer_[InstructionStream::kCodeAlignmentMinusCodeHeader])));
}

void Serializer::ObjectSerializer::SerializePrologue(SnapshotSpace space,
                                                     int size,
                                                     Tagged<Map> map) {
  if (serializer_->code_address_map_) {
    const char* code_name =
        serializer_->code_address_map_->Lookup(object_->address());
    LOG(serializer_->isolate_,
        CodeNameEvent(object_->address(), sink_->Position(), code_name));
  }

  if (map.SafeEquals(*object_)) {
    if (map == ReadOnlyRoots(isolate()).meta_map()) {
      DCHECK_EQ(space, SnapshotSpace::kReadOnlyHeap);
      sink_->Put(kNewContextlessMetaMap, "NewContextlessMetaMap");
    } else {
      DCHECK_EQ(space, SnapshotSpace::kOld);
      DCHECK(IsContext(map->native_context_or_null()));
      sink_->Put(kNewContextfulMetaMap, "NewContextfulMetaMap");

      // Defer serialization of the native context in order to break
      // a potential cycle through the map slot:
      //   MAP -> meta map -> NativeContext -> ... -> MAP
      // Otherwise it'll be a "forward ref to a map" problem: deserializer
      // will not be able to create {obj} because {MAP} is not deserialized yet.
      Tagged<NativeContext> native_context = map->native_context();

      // Sanity check - the native context must not be serialized yet since
      // it has a contextful map and thus the respective meta map must be
      // serialized first. So we don't have to search the native context
      // among the back refs before adding it to the deferred queue.
      DCHECK_NULL(
          serializer_->reference_map()->LookupReference(native_context));

      if (!serializer_->forward_refs_per_pending_object_.Find(native_context)) {
        serializer_->RegisterObjectIsPending(native_context);
        serializer_->QueueDeferredObject(native_context);
      }
    }
    DCHECK_EQ(size, Map::kSize);
  } else {
    sink_->Put(NewObject::Encode(space), "NewObject");

    // TODO(leszeks): Skip this when the map has a fixed size.
    sink_->PutUint30(size >> kObjectAlignmentBits, "ObjectSizeInWords");

    // Until the space for the object is allocated, it is considered "pending".
    serializer_->RegisterObjectIsPending(*object_);

    // Serialize map (first word of the object) before anything else, so that
    // the deserializer can access it when allocating. Make sure that the map
    // is known to be being serialized for the map slot, so that it is not
    // deferred.
    DCHECK(IsMap(map));
    serializer_->SerializeObject(handle(map, isolate()), SlotType::kMapSlot);

    // Make sure the map serialization didn't accidentally recursively serialize
    // this object.
    DCHECK_IMPLIES(
        !serializer_->IsNotMappedSymbol(*object_),
        serializer_->reference_map()->LookupReference(object_) == nullptr);

    // To support deserializing pending objects referenced through indirect
    // pointers, we need to make sure that the 'self' indirect pointer is
    // initialized before the pending reference is resolved. Otherwise, the
    // object cannot be referenced.
    if (V8_ENABLE_SANDBOX_BOOL && IsExposedTrustedObject(*object_)) {
      sink_->Put(kInitializeSelfIndirectPointer,
                 "InitializeSelfIndirectPointer");
    }

    // Now that the object is allocated, we can resolve pending references to
    // it.
    serializer_->ResolvePendingObject(*object_);
  }

  if (v8_flags.serialization_statistics) {
    serializer_->CountAllocation(object_->map(), size, space);
  }

  // The snapshot should only contain internalized strings (since these end up
  // in RO space). If this DCHECK fails, allocate the object_ String through
  // Factory::InternalizeString instead.
  // TODO(jgruber,v8:13789): Try to enable this DCHECK once custom snapshots
  // can extend RO space. We may have to do a pass over the heap prior to
  // serialization that in-place converts all strings to internalized strings.
  // DCHECK_IMPLIES(object_->IsString(), object_->IsInternalizedString());

  // Mark this object as already serialized, and add it to the reference map so
  // that it can be accessed by backreference by future objects.
  serializer_->num_back_refs_++;
#ifdef DEBUG
  serializer_->back_refs_.Push(*object_);
  DCHECK_EQ(serializer_->back_refs_.size(), serializer_->num_back_refs_);
#endif
  if (!serializer_->IsNotMappedSymbol(*object_)) {
    // Only add the object to the map if it's not not_mapped_symbol, else
    // the reference IdentityMap has issues. We don't expect to have back
    // references to the not_mapped_symbol anyway, so it's fine.
    SerializerReference back_reference =
        SerializerReference::BackReference(serializer_->num_back_refs_ - 1);
    serializer_->reference_map()->Add(*object_, back_reference);
    DCHECK_EQ(*object_,
              *serializer_->back_refs_[back_reference.back_ref_index()]);
    DCHECK_EQ(back_reference.back_ref_index(), serializer_->reference_map()
                                                   ->LookupReference(object_)
                                                   ->back_ref_index());
  }
}

uint32_t Serializer::ObjectSerializer::SerializeBackingStore(
    void* backing_store, uint32_t byte_length,
    Maybe<uint32_t> max_byte_length) {
  DisallowGarbageCollection no_gc;
  const SerializerReference* reference_ptr =
      serializer_->reference_map()->LookupBackingStore(backing_store);

  // Serialize the off-heap backing store.
  if (reference_ptr) {
    return reference_ptr->off_heap_backing_store_index();
  }
  if (max_byte_length.IsJust()) {
    sink_->Put(kOffHeapResizableBackingStore,
               "Off-heap resizable backing store");
  } else {
    sink_->Put(kOffHeapBackingStore, "Off-heap backing store");
  }
  sink_->PutUint32(byte_length, "length");
  if (max_byte_length.IsJust()) {
    sink_->PutUint32(max_byte_length.FromJust(), "max length");
  }
  sink_->PutRaw(static_cast<uint8_t*>(backing_store), byte_length,
                "BackingStore");
  DCHECK_NE(0, serializer_->seen_backing_stores_index_);
  SerializerReference reference =
      SerializerReference::OffHeapBackingStoreReference(
          serializer_->seen_backing_stores_index_++);
  // Mark this backing store as already serialized.
  serializer_->reference_map()->AddBackingStore(backing_store, reference);
  return reference.off_heap_backing_store_index();
}

void Serializer::ObjectSerializer::SerializeJSTypedArray() {
  {
    DisallowGarbageCollection no_gc;
    Tagged<JSTypedArray> typed_array = Cast<JSTypedArray>(*object_);
    if (typed_array->is_on_heap()) {
      typed_array->RemoveExternalPointerCompensationForSerialization(isolate());
    } else {
      if (!typed_array->IsDetachedOrOutOfBounds()) {
        // Explicitly serialize the backing store now.
        Tagged<JSArrayBuffer> buffer =
            Cast<JSArrayBuffer>(typed_array->buffer());
        // We cannot store byte_length or max_byte_length larger than uint32
        // range in the snapshot.
        size_t byte_length_size = buffer->GetByteLength();
        CHECK_LE(byte_length_size,
                 size_t{std::numeric_limits<uint32_t>::max()});
        uint32_t byte_length = static_cast<uint32_t>(byte_length_size);
        Maybe<uint32_t> max_byte_length = Nothing<uint32_t>();
        if (buffer->is_resizable_by_js()) {
          CHECK_LE(buffer->max_byte_length(),
                   std::numeric_limits<uint32_t>::max());
          max_byte_length =
              Just(static_cast<uint32_t>(buffer->max_byte_length()));
        }
        size_t byte_offset = typed_array->byte_offset();

        // We need to calculate the backing store from the data pointer
        // because the ArrayBuffer may already have been serialized.
        void* backing_store = reinterpret_cast<void*>(
            reinterpret_cast<Address>(typed_array->DataPtr()) - byte_offset);

        uint32_t ref =
            SerializeBackingStore(backing_store, byte_length, max_byte_length);
        typed_array->SetExternalBackingStoreRefForSerialization(ref);
      } else {
        typed_array->SetExternalBackingStoreRefForSerialization(0);
      }
    }
  }
  SerializeObject();
}

void Serializer::ObjectSerializer::SerializeJSArrayBuffer() {
  ArrayBufferExtension* extension;
  void* backing_store;
  {
    DisallowGarbageCollection no_gc;
    Tagged<JSArrayBuffer> buffer = Cast<JSArrayBuffer>(*object_);
    backing_store = buffer->backing_store();
    // We cannot store byte_length or max_byte_length larger than uint32 range
    // in the snapshot.
    CHECK_LE(buffer->byte_length(), std::numeric_limits<uint32_t>::max());
    uint32_t byte_length = static_cast<uint32_t>(buffer->byte_length());
    Maybe<uint32_t> max_byte_length = Nothing<uint32_t>();
    if (buffer->is_resizable_by_js()) {
      CHECK_LE(buffer->max_byte_length(), std::numeric_limits<uint32_t>::max());
      max_byte_length = Just(static_cast<uint32_t>(buffer->max_byte_length()));
    }
    extension = buffer->extension();

    // Only serialize non-empty backing stores.
    if (buffer->IsEmpty()) {
      buffer->SetBackingStoreRefForSerialization(kEmptyBackingStoreRefSentinel);
    } else {
      uint32_t ref =
          SerializeBackingStore(backing_store, byte_length, max_byte_length);
      buffer->SetBackingStoreRefForSerialization(ref);
    }

    // Ensure deterministic output by setting extension to null during
    // serialization.
    buffer->set_extension(nullptr);
  }
  SerializeObject();
  {
    Tagged<JSArrayBuffer> buffer = Cast<JSArrayBuffer>(*object_);
    buffer->set_backing_store(isolate(), backing_store);
    buffer->set_extension(extension);
  }
}

void Serializer::ObjectSerializer::SerializeExternalString() {
  // For external strings with known resources, we replace the resource field
  // with the encoded external reference, which we restore upon deserialize.
  // For the rest we serialize them to look like ordinary sequential strings.
  auto string = Cast<ExternalString>(object_);
  Address resource = string->resource_as_address();
  ExternalReferenceEncoder::Value reference;
  if (serializer_->external_reference_encoder_.TryEncode(resource).To(
          &reference)) {
    DCHECK(reference.is_from_api());
#ifdef V8_ENABLE_SANDBOX
    uint32_t external_pointer_entry =
        string->GetResourceRefForDeserialization();
#endif
    string->SetResourceRefForSerialization(reference.index());
    SerializeObject();
#ifdef V8_ENABLE_SANDBOX
    string->SetResourceRefForSerialization(external_pointer_entry);
#else
    string->set_address_as_resource(isolate(), resource);
#endif
  } else {
    SerializeExternalStringAsSequentialString();
  }
}

void Serializer::ObjectSerializer::SerializeExternalStringAsSequentialString() {
  // Instead of serializing this as an external string, we serialize
  // an imaginary sequential string with the same content.
  ReadOnlyRoots roots(isolate());
  PtrComprCageBase cage_base(isolate());
  DCHECK(IsExternalString(*object_, cage_base));
  Handle<ExternalString> string = Cast<ExternalString>(object_);
  int length = string->length();
  Tagged<Map> map;
  int content_size;
  int allocation_size;
  const uint8_t* resource;
  // Find the map and size for the imaginary sequential string.
  bool internalized = IsInternalizedString(*object_, cage_base);
  if (IsExternalOneByteString(*object_, cage_base)) {
    map = internalized ? roots.internalized_one_byte_string_map()
                       : roots.seq_one_byte_string_map();
    allocation_size = SeqOneByteString::SizeFor(length);
    content_size = length * kCharSize;
    resource = reinterpret_cast<const uint8_t*>(
        Cast<ExternalOneByteString>(string)->resource()->data());
  } else {
    map = internalized ? roots.internalized_two_byte_string_map()
                       : roots.seq_two_byte_string_map();
    allocation_size = SeqTwoByteString::SizeFor(length);
    content_size = length * kShortSize;
    resource = reinterpret_cast<const uint8_t*>(
        Cast<ExternalTwoByteString>(string)->resource()->data());
  }

  SnapshotSpace space = SnapshotSpace::kOld;
  SerializePrologue(space, allocation_size, map);

  // Output the rest of the imaginary string.
  int bytes_to_output = allocation_size - HeapObject::kHeaderSize;
  DCHECK(IsAligned(bytes_to_output, kTaggedSize));
  int slots_to_output = bytes_to_output >> kTaggedSizeLog2;

  // Output raw data header. Do not bother with common raw length cases here.
  sink_->Put(kVariableRawData, "RawDataForString");
  sink_->PutUint30(slots_to_output, "length");

  // Serialize string header (except for map).
  uint8_t* string_start = reinterpret_cast<uint8_t*>(string->address());
  for (size_t i = sizeof(HeapObjectLayout); i < sizeof(SeqString); i++) {
    sink_->Put(string_start[i], "StringHeader");
  }

  // Serialize string content.
  sink_->PutRaw(resource, content_size, "StringContent");

  // Since the allocation size is rounded up to object alignment, there
  // maybe left-over bytes that need to be padded.
  size_t padding_size = allocation_size - sizeof(SeqString) - content_size;
  DCHECK(0 <= padding_size && padding_size < kObjectAlignment);
  for (size_t i = 0; i < padding_size; i++) {
    sink_->Put(static_cast<uint8_t>(0), "StringPadding");
  }
}

// Clear and later restore the next link in the weak cell or allocation site.
// TODO(all): replace this with proper iteration of weak slots in serializer.
class V8_NODISCARD UnlinkWeakNextScope {
 public:
  explicit UnlinkWeakNextScope(Heap* heap, Tagged<HeapObject> object) {
    Isolate* isolate = heap->isolate();
    if (IsAllocationSite(object, isolate) &&
        Cast<AllocationSite>(object)->HasWeakNext()) {
      object_ = object;
      next_ = Cast<AllocationSite>(object)->weak_next();
      Cast<AllocationSite>(object)->set_weak_next(
          ReadOnlyRoots(isolate).undefined_value());
    }
  }

  ~UnlinkWeakNextScope() {
    if (next_ == Smi::zero()) return;
    Cast<AllocationSite>(object_)->set_weak_next(next_, UPDATE_WRITE_BARRIER);
  }

 private:
  Tagged<HeapObject> object_;
  Tagged<Object> next_ = Smi::zero();
  DISALLOW_GARBAGE_COLLECTION(no_gc_)
};

void Serializer::ObjectSerializer::Serialize(SlotType slot_type) {
  RecursionScope recursion(serializer_);

  {
    DisallowGarbageCollection no_gc;
    Tagged<HeapObject> raw = *object_;
    // Defer objects as "pending" if they cannot be serialized now, or if we
    // exceed a certain recursion depth. Some objects cannot be deferred.
    bool should_defer =
        recursion.ExceedsMaximum() || serializer_->MustBeDeferred(raw);
    if (should_defer && CanBeDeferred(raw, slot_type)) {
      if (v8_flags.trace_serializer) {
        PrintF(" Deferring heap object: ");
        ShortPrint(*object_);
        PrintF("\n");
      }
      // Deferred objects are considered "pending".
      serializer_->RegisterObjectIsPending(raw);
      serializer_->PutPendingForwardReference(
          *serializer_->forward_refs_per_pending_object_.Find(raw));
      serializer_->QueueDeferredObject(raw);
      return;
    } else {
      if (v8_flags.trace_serializer && recursion.ExceedsMaximum()) {
        PrintF(" Exceeding max recursion depth by %d for: ",
               recursion.ExceedsMaximumBy());
        ShortPrint(*object_);
        PrintF("\n");
      }
    }

    if (v8_flags.trace_serializer) {
      PrintF(" Encoding heap object: ");
      ShortPrint(*object_);
      PrintF("\n");
    }
  }

  PtrComprCageBase cage_base(isolate());
  InstanceType instance_type = object_->map(cage_base)->instance_type();
  if (InstanceTypeChecker::IsExternalString(instance_type)) {
    SerializeExternalString();
    return;
  }
  if (InstanceTypeChecker::IsJSTypedArray(instance_type)) {
    SerializeJSTypedArray();
    return;
  }
  if (InstanceTypeChecker::IsJSArrayBuffer(instance_type)) {
    SerializeJSArrayBuffer();
    return;
  }
  if (InstanceTypeChecker::IsScript(instance_type)) {
    // Clear cached line ends & compiled lazy function positions.
    Cast<Script>(object_)->set_line_ends(Smi::zero());
    Cast<Script>(object_)->set_compiled_lazy_function_positions(
        ReadOnlyRoots(isolate()).undefined_value());
  }

#if V8_ENABLE_WEBASSEMBLY
  // The padding for wasm null is a free space filler. We put it into the roots
  // table to be able to skip its payload when serializing the read only heap
  // in the ReadOnlyHeapImageSerializer.
  DCHECK_IMPLIES(
      !object_->SafeEquals(ReadOnlyRoots(isolate()).wasm_null_padding()),
      !IsFreeSpaceOrFiller(*object_, cage_base));
#else
  DCHECK(!IsFreeSpaceOrFiller(*object_, cage_base));
#endif

  SerializeObject();
}

namespace {
SnapshotSpace GetSnapshotSpace(Tagged<HeapObject> object) {
  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    if (IsInstructionStream(object)) {
      return SnapshotSpace::kCode;
    } else if (ReadOnlyHeap::Contains(object)) {
      return SnapshotSpace::kReadOnlyHeap;
    } else {
      return SnapshotSpace::kOld;
    }
  } else if (ReadOnlyHeap::Contains(object)) {
    return SnapshotSpace::kReadOnlyHeap;
  } else {
    AllocationSpace heap_space =
        MutablePageMetadata::FromHeapObject(object)->owner_identity();
    // Large code objects are not supported and cannot be expressed by
    // SnapshotSpace.
    DCHECK_NE(heap_space, CODE_LO_SPACE);
    switch (heap_space) {
      case OLD_SPACE:
      // Young generation objects are tenured, as objects that have survived
      // until snapshot building probably deserve to be considered 'old'.
      case NEW_SPACE:
      // Large objects (young and old) are encoded as simply 'old' snapshot
      // obects, as "normal" objects vs large objects is a heap implementation
      // detail and isn't relevant to the snapshot.
      case NEW_LO_SPACE:
      case LO_SPACE:
      // Shared objects are currently encoded as 'old' snapshot objects. This
      // basically duplicates shared heap objects for each isolate again.
      case SHARED_SPACE:
      case SHARED_LO_SPACE:
        return SnapshotSpace::kOld;
      case CODE_SPACE:
        return SnapshotSpace::kCode;
      case TRUSTED_SPACE:
      case TRUSTED_LO_SPACE:
        return SnapshotSpace::kTrusted;
      // Shared objects are currently encoded as 'trusteds' snapshot objects.
      // This basically duplicates shared trusted heap objects for each isolate
      // again.
      case SHARED_TRUSTED_SPACE:
      case SHARED_TRUSTED_LO_SPACE:
        return SnapshotSpace::kTrusted;
      case CODE_LO_SPACE:
      case RO_SPACE:
        UNREACHABLE();
    }
  }
}
}  // namespace

void Serializer::ObjectSerializer::SerializeObject() {
  Tagged<Map> map = object_->map(serializer_->cage_base());
  int size = object_->SizeFromMap(map);

  // Descriptor arrays have complex element weakness, that is dependent on the
  // maps pointing to them. During deserialization, this can cause them to get
  // prematurely trimmed one of their owners isn't deserialized yet. We work
  // around this by forcing all descriptor arrays to be serialized as "strong",
  // i.e. no custom weakness, and "re-weaken" them in the deserializer once
  // deserialization completes.
  //
  // See also `Deserializer::WeakenDescriptorArrays`.
  if (map == ReadOnlyRoots(isolate()).descriptor_array_map()) {
    map = ReadOnlyRoots(isolate()).strong_descriptor_array_map();
  }
  SnapshotSpace space = GetSnapshotSpace(*object_);
  SerializePrologue(space, size, map);

  // Serialize the rest of the object.
  CHECK_EQ(0, bytes_processed_so_far_);
  bytes_processed_so_far_ = kTaggedSize;

  SerializeContent(map, size);
}

void Serializer::ObjectSerializer::SerializeDeferred() {
  const SerializerReference* back_reference =
      serializer_->reference_map()->LookupReference(object_);

  if (back_reference != nullptr) {
    if (v8_flags.trace_serializer) {
      PrintF(" Deferred heap object ");
      ShortPrint(*object_);
      PrintF(" was already serialized\n");
    }
    return;
  }

  if (v8_flags.trace_serializer) {
    PrintF(" Encoding deferred heap object\n");
  }
  Serialize(SlotType::kAnySlot);
}

void Serializer::ObjectSerializer::SerializeContent(Tagged<Map> map, int size) {
  Tagged<HeapObject> raw = *object_;
  UnlinkWeakNextScope unlink_weak_next(isolate()->heap(), raw);
  // Iterate references first.
  raw->IterateBody(map, size, this);
  // Then output data payload, if any.
  OutputRawData(raw.address() + size);
}

void Serializer::ObjectSerializer::VisitPointers(Tagged<HeapObject> host,
                                                 ObjectSlot start,
                                                 ObjectSlot end) {
  VisitPointers(host, MaybeObjectSlot(start), MaybeObjectSlot(end));
}

void Serializer::ObjectSerializer::VisitPointers(Tagged<HeapObject> host,
                                                 MaybeObjectSlot start,
                                                 MaybeObjectSlot end) {
  HandleScope scope(isolate());
  PtrComprCageBase cage_base(isolate());
  DisallowGarbageCollection no_gc;

  MaybeObjectSlot current = start;
  while (current < end) {
    while (current < end && current.load(cage_base).IsSmi()) {
      ++current;
    }
    if (current < end) {
      OutputRawData(current.address());
    }
    // TODO(ishell): Revisit this change once we stick to 32-bit compressed
    // tagged values.
    while (current < end && current.load(cage_base).IsCleared()) {
      sink_->Put(kClearedWeakReference, "ClearedWeakReference");
      bytes_processed_so_far_ += kTaggedSize;
      ++current;
    }
    Tagged<HeapObject> current_contents;
    HeapObjectReferenceType reference_type;
    while (current < end && current.load(cage_base).GetHeapObject(
                                &current_contents, &reference_type)) {
      // Write a weak prefix if we need it. This has to be done before the
      // potential pending object serialization.
      if (reference_type == HeapObjectReferenceType::WEAK) {
        sink_->Put(kWeakPrefix, "WeakReference");
      }

      Handle<HeapObject> obj = handle(current_contents, isolate());
      if (serializer_->SerializePendingObject(*obj)) {
        bytes_processed_so_far_ += kTaggedSize;
        ++current;
        continue;
      }

      RootIndex root_index;
      // Compute repeat count and write repeat prefix if applicable.
      // Repeats are not subject to the write barrier so we can only use
      // immortal immovable root members.
      MaybeObjectSlot repeat_end = current + 1;
      if (repeat_end < end &&
          serializer_->root_index_map()->Lookup(*obj, &root_index) &&
          RootsTable::IsImmortalImmovable(root_index) &&
          current.load(cage_base) == repeat_end.load(cage_base) &&
          reference_type == HeapObjectReferenceType::STRONG) {
        DCHECK(!Heap::InYoungGeneration(*obj));
        while (repeat_end < end &&
               repeat_end.load(cage_base) == current.load(cage_base)) {
          repeat_end++;
        }
        int repeat_count = static_cast<int>(repeat_end - current);
        current = repeat_end;
        bytes_processed_so_far_ += repeat_count * kTaggedSize;
        serializer_->PutRepeat(repeat_count);
      } else {
        bytes_processed_so_far_ += kTaggedSize;
        ++current;
      }
      // Now write the object itself.
      serializer_->SerializeObject(obj, SlotType::kAnySlot);
    }
  }
}

void Serializer::ObjectSerializer::VisitInstructionStreamPointer(
    Tagged<Code> host, InstructionStreamSlot slot) {
  DCHECK(!host->has_instruction_stream());
}

// All of these visitor functions are unreachable since we don't serialize
// InstructionStream objects anymore.
void Serializer::ObjectSerializer::VisitEmbeddedPointer(
    Tagged<InstructionStream> host, RelocInfo* rinfo) {
  UNREACHABLE();
}

void Serializer::ObjectSerializer::VisitExternalReference(
    Tagged<InstructionStream> host, RelocInfo* rinfo) {
  UNREACHABLE();
}

void Serializer::ObjectSerializer::VisitInternalReference(
    Tagged<InstructionStream> host, RelocInfo* rinfo) {
  UNREACHABLE();
}

void Serializer::ObjectSerializer::VisitOffHeapTarget(
    Tagged<InstructionStream> host, RelocInfo* rinfo) {
  UNREACHABLE();
}

void Serializer::ObjectSerializer::VisitCodeTarget(
    Tagged<InstructionStream> host, RelocInfo* rinfo) {
  UNREACHABLE();
}

void Serializer::ObjectSerializer::OutputExternalReference(
    Address target, int target_size, bool sandboxify, ExternalPointerTag tag) {
  DCHECK_LE(target_size, sizeof(target));  // Must fit in Address.
  DCHECK_IMPLIES(sandboxify, V8_ENABLE_SANDBOX_BOOL);
  DCHECK_IMPLIES(sandboxify, tag != kExternalPointerNullTag);
  DCHECK_NE(tag, kAnyExternalPointerTag);
  ExternalReferenceEncoder::Value encoded_reference;
  bool encoded_successfully;

  if (serializer_->allow_unknown_external_references_for_testing()) {
    encoded_successfully =
        serializer_->TryEncodeExternalReference(target).To(&encoded_reference);
  } else {
    encoded_reference = serializer_->EncodeExternalReference(target);
    encoded_successfully = true;
  }

  if (!encoded_successfully) {
    // In this case the serialized snapshot will not be used in a different
    // Isolate and thus the target address will not change between
    // serialization and deserialization. We can serialize seen external
    // references verbatim.
    CHECK(serializer_->allow_unknown_external_references_for_testing());
    CHECK(IsAligned(target_size, kTaggedSize));
    CHECK_LE(target_size, kFixedRawDataCount * kTaggedSize);
    if (sandboxify) {
      CHECK_EQ(target_size, kSystemPointerSize);
      sink_->Put(kSandboxedRawExternalReference, "SandboxedRawReference");
      sink_->PutRaw(reinterpret_cast<uint8_t*>(&target), target_size,
                    "raw pointer");
    } else {
      // Encode as FixedRawData instead of RawExternalReference as the target
      // may be less than kSystemPointerSize large.
      int size_in_tagged = target_size >> kTaggedSizeLog2;
      sink_->Put(FixedRawDataWithSize::Encode(size_in_tagged), "FixedRawData");
      sink_->PutRaw(reinterpret_cast<uint8_t*>(&target), target_size,
                    "raw pointer");
    }
  } else if (encoded_reference.is_from_api()) {
    if (sandboxify) {
      sink_->Put(kSandboxedApiReference, "SandboxedApiRef");
    } else {
      sink_->Put(kApiReference, "ApiRef");
    }
    sink_->PutUint30(encoded_reference.index(), "reference index");
  } else {
    if (sandboxify) {
      sink_->Put(kSandboxedExternalReference, "SandboxedExternalRef");
    } else {
      sink_->Put(kExternalReference, "ExternalRef");
    }
    sink_->PutUint30(encoded_reference.index(), "reference index");
  }
  if (sandboxify) {
    sink_->PutUint30(static_cast<uint32_t>(tag >> kExternalPointerTagShift),
                     "external pointer tag");
  }
}

void Serializer::ObjectSerializer::VisitCppHeapPointer(
    Tagged<HeapObject> host, CppHeapPointerSlot slot) {
  PtrComprCageBase cage_base(isolate());
  // Currently there's only very limited support for CppHeapPointerSlot
  // serialization as it's only used for API wrappers.
  //
  // We serialize the slot as initialized-but-unused slot.  The actual API
  // wrapper serialization is implemented in
  // `ContextSerializer::SerializeApiWrapperFields()`.
  DCHECK(IsJSApiWrapperObject(object_->map(cage_base)));
  static_assert(kCppHeapPointerSlotSize % kTaggedSize == 0);
  sink_->Put(
      FixedRawDataWithSize::Encode(kCppHeapPointerSlotSize >> kTaggedSizeLog2),
      "FixedRawData");
  sink_->PutRaw(reinterpret_cast<const uint8_t*>(&kNullCppHeapPointer),
                kCppHeapPointerSlotSize, "empty cpp heap pointer handle");
  bytes_processed_so_far_ += kCppHeapPointerSlotSize;
}

void Serializer::ObjectSerializer::VisitExternalPointer(
    Tagged<HeapObject> host, ExternalPointerSlot slot) {
  PtrComprCageBase cage_base(isolate());
  InstanceType instance_type = object_->map(cage_base)->instance_type();
  if (InstanceTypeChecker::IsForeign(instance_type) ||
      InstanceTypeChecker::IsJSExternalObject(instance_type) ||
      InstanceTypeChecker::IsAccessorInfo(instance_type) ||
      InstanceTypeChecker::IsFunctionTemplateInfo(instance_type)) {
    // Output raw data payload, if any.
    OutputRawData(slot.address());
    Address value = slot.load(isolate());
#ifdef V8_ENABLE_SANDBOX
    // We need to load the actual tag from the table here since the slot may
    // use a generic tag (e.g. kAnyExternalPointerTag) if the concrete tag is
    // unknown by the visitor (for example the case for Foreigns).
    ExternalPointerHandle handle = slot.Relaxed_LoadHandle();
    ExternalPointerTag tag = isolate()->external_pointer_table().GetTag(handle);
#else
    ExternalPointerTag tag = kExternalPointerNullTag;
#endif  // V8_ENABLE_SANDBOX
    const bool sandboxify = V8_ENABLE_SANDBOX_BOOL;
    OutputExternalReference(value, kSystemPointerSize, sandboxify, tag);
    bytes_processed_so_far_ += kExternalPointerSlotSize;
  } else {
    // Serialization of external references in other objects is handled
    // elsewhere or not supported.
    DCHECK(
        // Serialization of external pointers stored in EmbedderDataArray
        // is not supported yet, mostly because it's not used.
        InstanceTypeChecker::IsEmbedderDataArray(instance_type) ||
        // See ObjectSerializer::SerializeJSTypedArray().
        InstanceTypeChecker::IsJSTypedArray(instance_type) ||
        // See ObjectSerializer::SerializeJSArrayBuffer().
        InstanceTypeChecker::IsJSArrayBuffer(instance_type) ||
        // See ObjectSerializer::SerializeExternalString().
        InstanceTypeChecker::IsExternalString(instance_type) ||
        // See ObjectSerializer::SanitizeNativeContextScope.
        InstanceTypeChecker::IsNativeContext(instance_type) ||
        // Serialization of external pointers stored in
        // JSSynchronizationPrimitive is not supported.
        // TODO(v8:12547): JSSynchronizationPrimitives should also be sanitized
        // to always be serialized in an unlocked state.
        InstanceTypeChecker::IsJSSynchronizationPrimitive(instance_type) ||
        // See ContextSerializer::SerializeObjectWithEmbedderFields().
        (InstanceTypeChecker::IsJSObject(instance_type) &&
         Cast<JSObject>(host)->GetEmbedderFieldCount() > 0));
  }
}

void Serializer::ObjectSerializer::VisitIndirectPointer(
    Tagged<HeapObject> host, IndirectPointerSlot slot,
    IndirectPointerMode mode) {
#ifdef V8_ENABLE_SANDBOX
  // If the slot is empty (i.e. contains a null handle), then we can just skip
  // it since in that case the correct action is to encode the null handle as
  // raw data, which will automatically happen if the slot is skipped here.
  if (slot.IsEmpty()) return;

  // If necessary, output any raw data preceeding this slot.
  OutputRawData(slot.address());

  // The slot must be properly initialized at this point, so will always contain
  // a reference to a HeapObject.
  Handle<HeapObject> slot_value(Cast<HeapObject>(slot.load(isolate())),
                                isolate());
  CHECK(IsHeapObject(*slot_value));
  bytes_processed_so_far_ += kIndirectPointerSize;

  // Currently we cannot see pending objects here, but we may need to support
  // them in the future. They should already be supported by the deserializer.
  CHECK(!serializer_->SerializePendingObject(*slot_value));
  sink_->Put(kIndirectPointerPrefix, "IndirectPointer");
  serializer_->SerializeObject(slot_value, SlotType::kAnySlot);
#else
  UNREACHABLE();
#endif
}

void Serializer::ObjectSerializer::VisitTrustedPointerTableEntry(
    Tagged<HeapObject> host, IndirectPointerSlot slot) {
#ifdef V8_ENABLE_SANDBOX
  // These fields only exist on the ExposedTrustedObject class, and they are
  // located directly after the Map word.
  DCHECK_EQ(bytes_processed_so_far_,
            ExposedTrustedObject::kSelfIndirectPointerOffset);

  // Nothing to do here. We already emitted the kInitializeSelfIndirectPointer
  // after processing the Map word in SerializePrologue.
  bytes_processed_so_far_ += kIndirectPointerSize;
#else
  UNREACHABLE();
#endif
}

void Serializer::ObjectSerializer::VisitProtectedPointer(
    Tagged<TrustedObject> host, ProtectedPointerSlot slot) {
  Tagged<Object> content = slot.load(isolate());

  // Similar to the indirect pointer case, if the slot is empty (i.e. contains
  // Smi::zero()), then we skip it here.
  if (content == Smi::zero()) return;
  DCHECK(!IsSmi(content));

  // If necessary, output any raw data preceeding this slot.
  OutputRawData(slot.address());

  Handle<HeapObject> object(Cast<HeapObject>(content), isolate());
  bytes_processed_so_far_ += kTaggedSize;

  // Currently we cannot see pending objects here, but we may need to support
  // them in the future. They should already be supported by the deserializer.
  CHECK(!serializer_->SerializePendingObject(*object));
  sink_->Put(kProtectedPointerPrefix, "ProtectedPointer");
  serializer_->SerializeObject(object, SlotType::kAnySlot);
}

void Serializer::ObjectSerializer::VisitJSDispatchTableEntry(
    Tagged<HeapObject> host, JSDispatchHandle handle) {
#ifdef V8_ENABLE_LEAPTIERING
  // New dispatch table entries will be allocated during deserialization if
  // necessary. Here we just serialize an empty handle.
  // TODO(saelo): we could also emit a kInitializeJSDispatchHandle opcode here
  // and then allocate a dispatch entry during deserialization when we
  // encounter that. That way we don't need to hook into object post
  // processing. If the dispatch handle is empty, we would just skip it here
  // (and would then serialize the raw null handle, which is correct).
  // TODO(saelo): we might want to call OutputRawData here, but for that we
  // first need to pass the slot address to this method (e.g. as part of a
  // JSDispatchHandleSlot struct).
  static_assert(kJSDispatchHandleSize % kTaggedSize == 0);
  sink_->Put(
      FixedRawDataWithSize::Encode(kJSDispatchHandleSize >> kTaggedSizeLog2),
      "FixedRawData");
  sink_->PutRaw(reinterpret_cast<const uint8_t*>(&kNullJSDispatchHandle),
                kJSDispatchHandleSize, "empty js dispatch handle");
  bytes_processed_so_far_ += kJSDispatchHandleSize;
#else
  UNREACHABLE();
#endif  // V8_ENABLE_LEAPTIERING
}
namespace {

// Similar to OutputRawData, but substitutes the given field with the given
// value instead of reading it from the object.
void OutputRawWithCustomField(SnapshotByteSink* sink, Address object_start,
                              int written_so_far, int bytes_to_write,
                              int field_offset, int field_size,
                              const uint8_t* field_value) {
  int offset = field_offset - written_so_far;
  if (0 <= offset && offset < bytes_to_write) {
    DCHECK_GE(bytes_to_write, offset + field_size);
    sink->PutRaw(reinterpret_cast<uint8_t*>(object_start + written_so_far),
                 offset, "Bytes");
    sink->PutRaw(field_value, field_size, "Bytes");
    written_so_far += offset + field_size;
    bytes_to_write -= offset + field_size;
    sink->PutRaw(reinterpret_cast<uint8_t*>(object_start + written_so_far),
                 bytes_to_write, "Bytes");
  } else {
    sink->PutRaw(reinterpret_cast<uint8_t*>(object_start + written_so_far),
                 bytes_to_write, "Bytes");
  }
}
}  // anonymous namespace

void Serializer::ObjectSerializer::OutputRawData(Address up_to) {
  Address object_start = object_->address();
  int base = bytes_processed_so_far_;
  int up_to_offset = static_cast<int>(up_to - object_start);
  int to_skip = up_to_offset - bytes_processed_so_far_;
  int bytes_to_output = to_skip;
  DCHECK(IsAligned(bytes_to_output, kTaggedSize));
  int tagged_to_output = bytes_to_output / kTaggedSize;
  bytes_processed_so_far_ += to_skip;
  DCHECK_GE(to_skip, 0);
  if (bytes_to_output != 0) {
    DCHECK(to_skip == bytes_to_output);
    if (tagged_to_output <= kFixedRawDataCount) {
      sink_->Put(FixedRawDataWithSize::Encode(tagged_to_output),
                 "FixedRawData");
    } else {
      sink_->Put(kVariableRawData, "VariableRawData");
      sink_->PutUint30(tagged_to_output, "length");
    }
#ifdef MEMORY_SANITIZER
    // Check that we do not serialize uninitialized memory.
    __msan_check_mem_is_initialized(
        reinterpret_cast<void*>(object_start + base), bytes_to_output);
#endif  // MEMORY_SANITIZER
    PtrComprCageBase cage_base(isolate_);
    if (IsSharedFunctionInfo(*object_, cage_base)) {
      // The bytecode age field can be changed by GC concurrently.
      static_assert(SharedFunctionInfo::kAgeSize == kUInt16Size);
      uint16_t field_value = 0;
      OutputRawWithCustomField(sink_, object_start, base, bytes_to_output,
                               SharedFunctionInfo::kAgeOffset,
                               sizeof(field_value),
                               reinterpret_cast<uint8_t*>(&field_value));
    } else if (IsDescriptorArray(*object_, cage_base)) {
      // The number of marked descriptors field can be changed by GC
      // concurrently.
      const auto field_value = DescriptorArrayMarkingState::kInitialGCState;
      static_assert(sizeof(field_value) == DescriptorArray::kSizeOfRawGcState);
      OutputRawWithCustomField(sink_, object_start, base, bytes_to_output,
                               DescriptorArray::kRawGcStateOffset,
                               sizeof(field_value),
                               reinterpret_cast<const uint8_t*>(&field_value));
    } else if (IsCode(*object_, cage_base)) {
#ifdef V8_ENABLE_SANDBOX
      // When the sandbox is enabled, this field contains the handle to this
      // Code object's code pointer table entry. This will be recomputed after
      // deserialization.
      static uint8_t field_value[kIndirectPointerSize] = {0};
      OutputRawWithCustomField(sink_, object_start, base, bytes_to_output,
                               Code::kSelfIndirectPointerOffset,
                               sizeof(field_value), field_value);
#else
      // In this case, instruction_start field contains a raw value that will
      // similarly be recomputed after deserialization, so write zeros to keep
      // the snapshot deterministic.
      static uint8_t field_value[kSystemPointerSize] = {0};
      OutputRawWithCustomField(sink_, object_start, base, bytes_to_output,
                               Code::kInstructionStartOffset,
                               sizeof(field_value), field_value);
#endif  // V8_ENABLE_SANDBOX
    } else if (IsSeqString(*object_)) {
      // SeqStrings may contain padding. Serialize the padding bytes as 0s to
      // make the snapshot content deterministic.
      SeqString::DataAndPaddingSizes sizes =
          Cast<SeqString>(*object_)->GetDataAndPaddingSizes();
      DCHECK_EQ(bytes_to_output, sizes.data_size - base + sizes.padding_size);
      int data_bytes_to_output = sizes.data_size - base;
      sink_->PutRaw(reinterpret_cast<uint8_t*>(object_start + base),
                    data_bytes_to_output, "SeqStringData");
      sink_->PutN(sizes.padding_size, 0, "SeqStringPadding");
    } else {
      sink_->PutRaw(reinterpret_cast<uint8_t*>(object_start + base),
                    bytes_to_output, "Bytes");
    }
  }
}

Serializer::HotObjectsList::HotObjectsList(Heap* heap) : heap_(heap) {
  strong_roots_entry_ = heap->RegisterStrongRoots(
      "Serializer::HotObjectsList", FullObjectSlot(&circular_queue_[0]),
      FullObjectSlot(&circular_queue_[kSize]));
}
Serializer::HotObjectsList::~HotObjectsList() {
  heap_->UnregisterStrongRoots(strong_roots_entry_);
}

Handle<FixedArray> ObjectCacheIndexMap::Values(Isolate* isolate) {
  if (size() == 0) {
    return isolate->factory()->empty_fixed_array();
  }
  Handle<FixedArray> externals = isolate->factory()->NewFixedArray(size());
  DisallowGarbageCollection no_gc;
  Tagged<FixedArray> raw = *externals;
  IdentityMap<int, base::DefaultAllocationPolicy>::IteratableScope it_scope(
      &map_);
  for (auto it = it_scope.begin(); it != it_scope.end(); ++it) {
    raw->set(*it.entry(), it.key());
  }

  return externals;
}

bool Serializer::SerializeReadOnlyObjectReference(Tagged<HeapObject> obj,
                                                  SnapshotByteSink* sink) {
  if (!ReadOnlyHeap::Contains(obj)) return false;

  // For objects on the read-only heap, never serialize the object, but instead
  // create a back reference that encodes the page number as the chunk_index and
  // the offset within the page as the chunk_offset.
  Address address = obj.address();
  MemoryChunkMetadata* chunk = MemoryChunkMetadata::FromAddress(address);
  uint32_t chunk_index = 0;
  ReadOnlySpace* const read_only_space = isolate()->heap()->read_only_space();
  DCHECK(!read_only_space->writable());
  for (ReadOnlyPageMetadata* page : read_only_space->pages()) {
    if (chunk == page) break;
    ++chunk_index;
  }
  uint32_t chunk_offset = static_cast<uint32_t>(chunk->Offset(address));
  sink->Put(kReadOnlyHeapRef, "ReadOnlyHeapRef");
  sink->PutUint30(chunk_index, "ReadOnlyHeapRefChunkIndex");
  sink->PutUint30(chunk_offset, "ReadOnlyHeapRefChunkOffset");
  return true;
}

}  // namespace internal
}  // namespace v8
