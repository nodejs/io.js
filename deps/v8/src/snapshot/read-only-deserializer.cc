// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/read-only-deserializer.h"

#include "src/handles/handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/logging/counters-scopes.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/snapshot/read-only-serializer-deserializer.h"
#include "src/snapshot/snapshot-data.h"

namespace v8 {
namespace internal {

class ReadOnlyHeapImageDeserializer final {
 public:
  static void Deserialize(Isolate* isolate, SnapshotByteSource* source) {
    ReadOnlyHeapImageDeserializer{isolate, source}.DeserializeImpl();
  }

 private:
  using Bytecode = ro::Bytecode;

  ReadOnlyHeapImageDeserializer(Isolate* isolate, SnapshotByteSource* source)
      : source_(source), isolate_(isolate) {}

  void DeserializeImpl() {
    while (true) {
      int bytecode_as_int = source_->Get();
      DCHECK_LT(bytecode_as_int, ro::kNumberOfBytecodes);
      switch (static_cast<Bytecode>(bytecode_as_int)) {
        case Bytecode::kAllocatePage:
          AllocatePage(false);
          break;
        case Bytecode::kAllocatePageAt:
          AllocatePage(true);
          break;
        case Bytecode::kSegment:
          DeserializeSegment();
          break;
        case Bytecode::kRelocateSegment:
          UNREACHABLE();  // Handled together with kSegment.
        case Bytecode::kReadOnlyRootsTable:
          DeserializeReadOnlyRootsTable();
          break;
        case Bytecode::kFinalizeReadOnlySpace:
          ro_space()->FinalizeSpaceForDeserialization();
          return;
      }
    }
  }

  void AllocatePage(bool fixed_offset) {
    CHECK_EQ(V8_STATIC_ROOTS_BOOL, fixed_offset);
    size_t expected_page_index = static_cast<size_t>(source_->GetUint30());
    size_t actual_page_index = static_cast<size_t>(-1);
    size_t area_size_in_bytes = static_cast<size_t>(source_->GetUint30());
    if (fixed_offset) {
#ifdef V8_COMPRESS_POINTERS
      uint32_t compressed_page_addr = source_->GetUint32();
      Address pos = isolate_->cage_base() + compressed_page_addr;
      actual_page_index = ro_space()->AllocateNextPageAt(pos);
#else
      UNREACHABLE();
#endif  // V8_COMPRESS_POINTERS
    } else {
      actual_page_index = ro_space()->AllocateNextPage();
    }
    CHECK_EQ(actual_page_index, expected_page_index);
    ro_space()->InitializePageForDeserialization(PageAt(actual_page_index),
                                                 area_size_in_bytes);
  }

  void DeserializeSegment() {
    uint32_t page_index = source_->GetUint30();
    ReadOnlyPageMetadata* page = PageAt(page_index);

    // Copy over raw contents.
    Address start = page->area_start() + source_->GetUint30();
    int size_in_bytes = source_->GetUint30();
    CHECK_LE(start + size_in_bytes, page->area_end());
    source_->CopyRaw(reinterpret_cast<void*>(start), size_in_bytes);

    if (!V8_STATIC_ROOTS_BOOL) {
      uint8_t relocate_marker_bytecode = source_->Get();
      CHECK_EQ(relocate_marker_bytecode, Bytecode::kRelocateSegment);
      int tagged_slots_size_in_bits = size_in_bytes / kTaggedSize;
      // The const_cast is unfortunate, but we promise not to mutate data.
      uint8_t* data =
          const_cast<uint8_t*>(source_->data() + source_->position());
      ro::BitSet tagged_slots(data, tagged_slots_size_in_bits);
      DecodeTaggedSlots(start, tagged_slots);
      source_->Advance(static_cast<int>(tagged_slots.size_in_bytes()));
    }
  }

  Address Decode(ro::EncodedTagged encoded) const {
    ReadOnlyPageMetadata* page = PageAt(encoded.page_index);
    return page->OffsetToAddress(encoded.offset * kTaggedSize);
  }

  void DecodeTaggedSlots(Address segment_start,
                         const ro::BitSet& tagged_slots) {
    DCHECK(!V8_STATIC_ROOTS_BOOL);
    for (size_t i = 0; i < tagged_slots.size_in_bits(); i++) {
      // TODO(jgruber): Depending on sparseness, different iteration methods
      // could be more efficient.
      if (!tagged_slots.contains(static_cast<int>(i))) continue;
      Address slot_addr = segment_start + i * kTaggedSize;
      Address obj_addr = Decode(ro::EncodedTagged::FromAddress(slot_addr));
      Address obj_ptr = obj_addr + kHeapObjectTag;

      Tagged_t* dst = reinterpret_cast<Tagged_t*>(slot_addr);
      *dst = COMPRESS_POINTERS_BOOL
                 ? V8HeapCompressionScheme::CompressObject(obj_ptr)
                 : static_cast<Tagged_t>(obj_ptr);
    }
  }

  ReadOnlyPageMetadata* PageAt(size_t index) const {
    DCHECK_LT(index, ro_space()->pages().size());
    return ro_space()->pages()[index];
  }

  void DeserializeReadOnlyRootsTable() {
    ReadOnlyRoots roots(isolate_);
    if (V8_STATIC_ROOTS_BOOL) {
      roots.InitFromStaticRootsTable(isolate_->cage_base());
    } else {
      for (size_t i = 0; i < ReadOnlyRoots::kEntriesCount; i++) {
        uint32_t encoded_as_int = source_->GetUint32();
        Address rudolf = Decode(ro::EncodedTagged::FromUint32(encoded_as_int));
        roots.read_only_roots_[i] = rudolf + kHeapObjectTag;
      }
    }
  }

  ReadOnlySpace* ro_space() const {
    return isolate_->read_only_heap()->read_only_space();
  }

  SnapshotByteSource* const source_;
  Isolate* const isolate_;
};

ReadOnlyDeserializer::ReadOnlyDeserializer(Isolate* isolate,
                                           const SnapshotData* data,
                                           bool can_rehash)
    : Deserializer(isolate, data->Payload(), data->GetMagicNumber(), false,
                   can_rehash) {}

void ReadOnlyDeserializer::DeserializeIntoIsolate() {
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(v8_flags.profile_deserialization)) timer.Start();
  NestedTimedHistogramScope histogram_timer(
      isolate()->counters()->snapshot_deserialize_rospace());
  HandleScope scope(isolate());

  ReadOnlyHeapImageDeserializer::Deserialize(isolate(), source());
  ReadOnlyHeap* ro_heap = isolate()->read_only_heap();
  ro_heap->read_only_space()->RepairFreeSpacesAfterDeserialization();
  PostProcessNewObjects();

  ReadOnlyRoots roots(isolate());
  roots.VerifyNameForProtectorsPages();
#ifdef DEBUG
  roots.VerifyNameForProtectors();
#endif

  if (should_rehash()) {
    isolate()->heap()->InitializeHashSeed();
    Rehash();
  }

  if (V8_UNLIKELY(v8_flags.profile_deserialization)) {
    // ATTENTION: The Memory.json benchmark greps for this exact output. Do not
    // change it without also updating Memory.json.
    const int bytes = source()->length();
    const double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Deserializing read-only space (%d bytes) took %0.3f ms]\n", bytes,
           ms);
  }
}

void NoExternalReferencesCallback() {
  // The following check will trigger if a function or object template with
  // references to native functions have been deserialized from snapshot, but
  // no actual external references were provided when the isolate was created.
  FATAL("No external references provided via API");
}

class ObjectPostProcessor final {
 public:
  explicit ObjectPostProcessor(Isolate* isolate)
      : isolate_(isolate), embedded_data_(EmbeddedData::FromBlob(isolate_)) {}

  void Finalize() {
#ifdef V8_ENABLE_SANDBOX
    DCHECK(ReadOnlyHeap::IsReadOnlySpaceShared());
    std::vector<ReadOnlyArtifacts::ExternalPointerRegistryEntry> registry;
    registry.reserve(external_pointer_slots_.size());
    for (auto& slot : external_pointer_slots_) {
      registry.emplace_back(slot.Relaxed_LoadHandle(), slot.load(isolate_),
                            slot.tag());
    }

    isolate_->read_only_artifacts()->set_external_pointer_registry(
        std::move(registry));
#endif  // V8_ENABLE_SANDBOX
  }
#define POST_PROCESS_TYPE_LIST(V) \
  V(AccessorInfo)                 \
  V(FunctionTemplateInfo)         \
  V(Code)                         \
  V(SharedFunctionInfo)

  V8_INLINE void PostProcessIfNeeded(Tagged<HeapObject> o,
                                     InstanceType instance_type) {
    DCHECK_EQ(o->map(isolate_)->instance_type(), instance_type);
#define V(TYPE)                                       \
  if (InstanceTypeChecker::Is##TYPE(instance_type)) { \
    return PostProcess##TYPE(Cast<TYPE>(o));          \
  }
    POST_PROCESS_TYPE_LIST(V)
#undef V
    // If we reach here, no postprocessing is needed for this object.
  }
#undef POST_PROCESS_TYPE_LIST

 private:
  Address GetAnyExternalReferenceAt(int index, bool is_api_reference) const {
    if (is_api_reference) {
      const intptr_t* refs = isolate_->api_external_references();
      Address address =
          refs == nullptr
              ? reinterpret_cast<Address>(NoExternalReferencesCallback)
              : static_cast<Address>(refs[index]);
      DCHECK_NE(address, kNullAddress);
      return address;
    }
    // Note we allow `address` to be kNullAddress since some of our tests
    // rely on this (e.g. when testing an incompletely initialized ER table).
    return isolate_->external_reference_table_unsafe()->address(index);
  }

  void DecodeExternalPointerSlot(Tagged<HeapObject> host,
                                 ExternalPointerSlot slot) {
    // Constructing no_gc here is not the intended use pattern (instead we
    // should pass it along the entire callchain); but there's little point of
    // doing that here - all of the code in this file relies on GC being
    // disabled, and that's guarded at entry points.
    DisallowGarbageCollection no_gc;
    auto encoded = ro::EncodedExternalReference::FromUint32(
        slot.GetContentAsIndexAfterDeserialization(no_gc));
    Address slot_value =
        GetAnyExternalReferenceAt(encoded.index, encoded.is_api_reference);
    slot.init(isolate_, host, slot_value);
#ifdef V8_ENABLE_SANDBOX
    // Register these slots during deserialization s.t. later isolates (which
    // share the RO space we are currently deserializing) can properly
    // initialize their external pointer table RO space. Note that slot values
    // are only fully finalized at the end of deserialization, thus we only
    // register the slot itself now and read the handle/value in Finalize.
    external_pointer_slots_.emplace_back(slot);
#endif  // V8_ENABLE_SANDBOX
  }
  void PostProcessAccessorInfo(Tagged<AccessorInfo> o) {
    DecodeExternalPointerSlot(
        o, o->RawExternalPointerField(AccessorInfo::kSetterOffset,
                                      kAccessorInfoSetterTag));
    DecodeExternalPointerSlot(o, o->RawExternalPointerField(
                                     AccessorInfo::kMaybeRedirectedGetterOffset,
                                     kAccessorInfoGetterTag));
    if (USE_SIMULATOR_BOOL) o->init_getter_redirection(isolate_);
  }
  void PostProcessFunctionTemplateInfo(Tagged<FunctionTemplateInfo> o) {
    DecodeExternalPointerSlot(
        o, o->RawExternalPointerField(
               FunctionTemplateInfo::kMaybeRedirectedCallbackOffset,
               kFunctionTemplateInfoCallbackTag));
    if (USE_SIMULATOR_BOOL) o->init_callback_redirection(isolate_);
  }
  void PostProcessCode(Tagged<Code> o) {
    o->init_self_indirect_pointer(isolate_);
    o->wrapper()->set_code(o);
    // RO space only contains builtin Code objects which don't have an
    // attached InstructionStream.
    DCHECK(o->is_builtin());
    DCHECK(!o->has_instruction_stream());
    o->SetInstructionStartForOffHeapBuiltin(
        isolate_,
        EmbeddedData::FromBlob(isolate_).InstructionStartOf(o->builtin_id()));
  }
  void PostProcessSharedFunctionInfo(Tagged<SharedFunctionInfo> o) {
    // Reset the id to avoid collisions - it must be unique in this isolate.
    o->set_unique_id(isolate_->GetAndIncNextUniqueSfiId());
  }

  Isolate* const isolate_;
  const EmbeddedData embedded_data_;

#ifdef V8_ENABLE_SANDBOX
  std::vector<ExternalPointerSlot> external_pointer_slots_;
#endif  // V8_ENABLE_SANDBOX
};

void ReadOnlyDeserializer::PostProcessNewObjects() {
  // Since we are not deserializing individual objects we need to scan the
  // heap and search for objects that need post-processing.
  //
  // See also Deserializer<IsolateT>::PostProcessNewObject.
  PtrComprCageBase cage_base(isolate());
#ifdef V8_COMPRESS_POINTERS
  ExternalPointerTable::UnsealReadOnlySegmentScope unseal_scope(
      &isolate()->external_pointer_table());
#endif  // V8_COMPRESS_POINTERS
  ObjectPostProcessor post_processor(isolate());
  ReadOnlyHeapObjectIterator it(isolate()->read_only_heap());
  for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
    const InstanceType instance_type = o->map(cage_base)->instance_type();
    if (should_rehash()) {
      if (InstanceTypeChecker::IsString(instance_type)) {
        Tagged<String> str = Cast<String>(o);
        str->set_raw_hash_field(Name::kEmptyHashField);
        PushObjectToRehash(handle(str, isolate()));
      } else if (o->NeedsRehashing(instance_type)) {
        PushObjectToRehash(handle(o, isolate()));
      }
    }

    post_processor.PostProcessIfNeeded(o, instance_type);
  }
  post_processor.Finalize();
}

}  // namespace internal
}  // namespace v8
