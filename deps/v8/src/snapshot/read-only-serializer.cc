// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/read-only-serializer.h"

#include "src/heap/heap-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"
#include "src/snapshot/read-only-serializer-deserializer.h"

namespace v8 {
namespace internal {

namespace {

// Preprocess an object to prepare it for serialization.
class ObjectPreProcessor final {
 public:
  explicit ObjectPreProcessor(Isolate* isolate)
      : isolate_(isolate), extref_encoder_(isolate) {}

#define PRE_PROCESS_TYPE_LIST(V) \
  V(AccessorInfo)                \
  V(FunctionTemplateInfo)        \
  V(Code)

  void PreProcessIfNeeded(Tagged<HeapObject> o) {
    const InstanceType itype = o->map(isolate_)->instance_type();
#define V(TYPE)                               \
  if (InstanceTypeChecker::Is##TYPE(itype)) { \
    return PreProcess##TYPE(Cast<TYPE>(o));   \
  }
    PRE_PROCESS_TYPE_LIST(V)
#undef V
    // If we reach here, no preprocessing is needed for this object.
  }
#undef PRE_PROCESS_TYPE_LIST

 private:
  void EncodeExternalPointerSlot(ExternalPointerSlot slot) {
    Address value = slot.load(isolate_);
    EncodeExternalPointerSlot(slot, value);
  }

  void EncodeExternalPointerSlot(ExternalPointerSlot slot, Address value) {
    // Note it's possible that `value != slot.load(...)`, e.g. for
    // AccessorInfo::remove_getter_indirection.
    ExternalReferenceEncoder::Value encoder_value =
        extref_encoder_.Encode(value);
    DCHECK_LT(encoder_value.index(),
              1UL << ro::EncodedExternalReference::kIndexBits);
    ro::EncodedExternalReference encoded{encoder_value.is_from_api(),
                                         encoder_value.index()};
    // Constructing no_gc here is not the intended use pattern (instead we
    // should pass it along the entire callchain); but there's little point of
    // doing that here - all of the code in this file relies on GC being
    // disabled, and that's guarded at entry points.
    DisallowGarbageCollection no_gc;
    slot.ReplaceContentWithIndexForSerialization(no_gc, encoded.ToUint32());
  }
  void PreProcessAccessorInfo(Tagged<AccessorInfo> o) {
    EncodeExternalPointerSlot(
        o->RawExternalPointerField(AccessorInfo::kMaybeRedirectedGetterOffset,
                                   kAccessorInfoGetterTag),
        o->getter(isolate_));  // Pass the non-redirected value.
    EncodeExternalPointerSlot(o->RawExternalPointerField(
        AccessorInfo::kSetterOffset, kAccessorInfoSetterTag));
  }
  void PreProcessFunctionTemplateInfo(Tagged<FunctionTemplateInfo> o) {
    EncodeExternalPointerSlot(
        o->RawExternalPointerField(
            FunctionTemplateInfo::kMaybeRedirectedCallbackOffset,
            kFunctionTemplateInfoCallbackTag),
        o->callback(isolate_));  // Pass the non-redirected value.
  }
  void PreProcessCode(Tagged<Code> o) {
    o->ClearInstructionStartForSerialization(isolate_);
    DCHECK(!o->has_source_position_table_or_bytecode_offset_table());
    DCHECK(!o->has_deoptimization_data_or_interpreter_data());
  }

  Isolate* const isolate_;
  ExternalReferenceEncoder extref_encoder_;
};

struct ReadOnlySegmentForSerialization {
  ReadOnlySegmentForSerialization(Isolate* isolate,
                                  const ReadOnlyPageMetadata* page,
                                  Address segment_start, size_t segment_size,
                                  ObjectPreProcessor* pre_processor)
      : page(page),
        segment_start(segment_start),
        segment_size(segment_size),
        segment_offset(segment_start - page->area_start()),
        contents(new uint8_t[segment_size]),
        tagged_slots(segment_size / kTaggedSize) {
    // .. because tagged_slots records a bit for each slot:
    DCHECK(IsAligned(segment_size, kTaggedSize));

    MemCopy(contents.get(), reinterpret_cast<void*>(segment_start),
            segment_size);
    PreProcessSegment(pre_processor);
    if (!V8_STATIC_ROOTS_BOOL) EncodeTaggedSlots(isolate);
  }

  void PreProcessSegment(ObjectPreProcessor* pre_processor) {
    // Iterate the RO page and the contents copy in lockstep, preprocessing
    // objects as we go along.
    //
    // See also ObjectSerializer::OutputRawData.
    DCHECK_GE(segment_start, page->area_start());
    const Address segment_end = segment_start + segment_size;
    ReadOnlyPageObjectIterator it(page, segment_start);
    for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
      if (o.address() >= segment_end) break;
      size_t o_offset = o.ptr() - segment_start;
      Address o_dst = reinterpret_cast<Address>(contents.get()) + o_offset;
      pre_processor->PreProcessIfNeeded(
          Cast<HeapObject>(Tagged<Object>(o_dst)));
    }
  }

  void EncodeTaggedSlots(Isolate* isolate);

  const ReadOnlyPageMetadata* const page;
  const Address segment_start;
  const size_t segment_size;
  const size_t segment_offset;
  // The (mutated) off-heap copy of the on-heap segment.
  std::unique_ptr<uint8_t[]> contents;
  // The relocation table.
  ro::BitSet tagged_slots;

  friend class EncodeRelocationsVisitor;
};

ro::EncodedTagged Encode(Isolate* isolate, Tagged<HeapObject> o) {
  Address o_address = o.address();
  MemoryChunkMetadata* chunk = MemoryChunkMetadata::FromAddress(o_address);

  ro::EncodedTagged encoded;
  ReadOnlySpace* ro_space = isolate->read_only_heap()->read_only_space();
  int index = static_cast<int>(ro_space->IndexOf(chunk));
  DCHECK_LT(index, 1UL << ro::EncodedTagged::kPageIndexBits);
  encoded.page_index = index;
  uint32_t offset = static_cast<int>(chunk->Offset(o_address));
  DCHECK(IsAligned(offset, kTaggedSize));
  DCHECK_LT(offset / kTaggedSize, 1UL << ro::EncodedTagged::kOffsetBits);
  encoded.offset = offset / kTaggedSize;

  return encoded;
}

// If relocations are needed, this class
// - encodes all tagged slots s.t. valid pointers can be reconstructed during
//   deserialization, and
// - records the location of all tagged slots in a table.
class EncodeRelocationsVisitor final : public ObjectVisitor {
 public:
  EncodeRelocationsVisitor(Isolate* isolate,
                           ReadOnlySegmentForSerialization* segment)
      : isolate_(isolate), segment_(segment) {
    DCHECK(!V8_STATIC_ROOTS_BOOL);
  }

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override {
    VisitPointers(host, MaybeObjectSlot(start), MaybeObjectSlot(end));
  }

  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) override {
    for (MaybeObjectSlot slot = start; slot < end; slot++) {
      ProcessSlot(slot);
    }
  }

  void VisitMapPointer(Tagged<HeapObject> host) override {
    ProcessSlot(host->RawMaybeWeakField(HeapObject::kMapOffset));
  }

  // Sanity-checks:
  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    // RO space contains only builtin Code objects.
    DCHECK(!host->has_instruction_stream());
  }
  void VisitCodeTarget(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitEmbeddedPointer(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitExternalReference(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitInternalReference(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitOffHeapTarget(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitExternalPointer(Tagged<HeapObject>,
                            ExternalPointerSlot slot) override {
    // This slot was encoded in a previous pass, see EncodeExternalPointerSlot.
#ifdef DEBUG
    ExternalPointerSlot slot_in_segment{
        reinterpret_cast<Address>(segment_->contents.get() +
                                  SegmentOffsetOf(slot)),
        slot.tag()};
    // Constructing no_gc here is not the intended use pattern (instead we
    // should pass it along the entire callchain); but there's little point of
    // doing that here - all of the code in this file relies on GC being
    // disabled, and that's guarded at entry points.
    DisallowGarbageCollection no_gc;
    auto encoded = ro::EncodedExternalReference::FromUint32(
        slot_in_segment.GetContentAsIndexAfterDeserialization(no_gc));
    if (encoded.is_api_reference) {
      // Can't validate these since we don't know how many entries
      // api_external_references contains.
    } else {
      CHECK_LT(encoded.index, ExternalReferenceTable::kSize);
    }
#endif  // DEBUG
  }

 private:
  void ProcessSlot(MaybeObjectSlot slot) {
    Tagged<MaybeObject> o = *slot;
    if (!o.IsStrongOrWeak()) return;  // Smis don't need relocation.
    DCHECK(o.IsStrong());

    int slot_offset = SegmentOffsetOf(slot);
    DCHECK(IsAligned(slot_offset, kTaggedSize));

    // Encode:
    ro::EncodedTagged encoded = Encode(isolate_, o.GetHeapObject());
    memcpy(segment_->contents.get() + slot_offset, &encoded,
           ro::EncodedTagged::kSize);

    // Record:
    segment_->tagged_slots.set(AsSlot(slot_offset));
  }

  template <class SlotT>
  int SegmentOffsetOf(SlotT slot) const {
    Address addr = slot.address();
    DCHECK_GE(addr, segment_->segment_start);
    DCHECK_LT(addr, segment_->segment_start + segment_->segment_size);
    return static_cast<int>(addr - segment_->segment_start);
  }

  static constexpr int AsSlot(int byte_offset) {
    return byte_offset / kTaggedSize;
  }

  Isolate* const isolate_;
  ReadOnlySegmentForSerialization* const segment_;
};

void ReadOnlySegmentForSerialization::EncodeTaggedSlots(Isolate* isolate) {
  DCHECK(!V8_STATIC_ROOTS_BOOL);
  EncodeRelocationsVisitor v(isolate, this);
  PtrComprCageBase cage_base(isolate);

  DCHECK_GE(segment_start, page->area_start());
  const Address segment_end = segment_start + segment_size;
  ReadOnlyPageObjectIterator it(page, segment_start,
                                SkipFreeSpaceOrFiller::kNo);
  for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
    if (o.address() >= segment_end) break;
    o->Iterate(cage_base, &v);
  }
}

class ReadOnlyHeapImageSerializer {
 public:
  struct MemoryRegion {
    Address start;
    size_t size;
  };

  static void Serialize(Isolate* isolate, SnapshotByteSink* sink,
                        const std::vector<MemoryRegion>& unmapped_regions) {
    ReadOnlyHeapImageSerializer{isolate, sink}.SerializeImpl(unmapped_regions);
  }

 private:
  using Bytecode = ro::Bytecode;

  ReadOnlyHeapImageSerializer(Isolate* isolate, SnapshotByteSink* sink)
      : isolate_(isolate), sink_(sink), pre_processor_(isolate) {}

  void SerializeImpl(const std::vector<MemoryRegion>& unmapped_regions) {
    DCHECK_EQ(sink_->Position(), 0);

    ReadOnlySpace* ro_space = isolate_->read_only_heap()->read_only_space();

    // Allocate all pages first s.t. the deserializer can easily handle forward
    // references (e.g.: an object on page i points at an object on page i+1).
    for (const ReadOnlyPageMetadata* page : ro_space->pages()) {
      EmitAllocatePage(page, unmapped_regions);
    }

    // Now write the page contents.
    for (const ReadOnlyPageMetadata* page : ro_space->pages()) {
      SerializePage(page, unmapped_regions);
    }

    EmitReadOnlyRootsTable();
    sink_->Put(Bytecode::kFinalizeReadOnlySpace, "space end");
  }

  uint32_t IndexOf(const ReadOnlyPageMetadata* page) {
    ReadOnlySpace* ro_space = isolate_->read_only_heap()->read_only_space();
    return static_cast<uint32_t>(ro_space->IndexOf(page));
  }

  void EmitAllocatePage(const ReadOnlyPageMetadata* page,
                        const std::vector<MemoryRegion>& unmapped_regions) {
    if (V8_STATIC_ROOTS_BOOL) {
      sink_->Put(Bytecode::kAllocatePageAt, "fixed page begin");
    } else {
      sink_->Put(Bytecode::kAllocatePage, "page begin");
    }
    sink_->PutUint30(IndexOf(page), "page index");
    sink_->PutUint30(
        static_cast<uint32_t>(page->HighWaterMark() - page->area_start()),
        "area size in bytes");
    if (V8_STATIC_ROOTS_BOOL) {
      auto page_addr = page->ChunkAddress();
      sink_->PutUint32(V8HeapCompressionScheme::CompressAny(page_addr),
                       "page start offset");
    }
  }

  void SerializePage(const ReadOnlyPageMetadata* page,
                     const std::vector<MemoryRegion>& unmapped_regions) {
    Address pos = page->area_start();

    // If this page contains unmapped regions split it into multiple segments.
    for (auto r = unmapped_regions.begin(); r != unmapped_regions.end(); ++r) {
      // Regions must be sorted and non-overlapping.
      if (r + 1 != unmapped_regions.end()) {
        CHECK(r->start < (r + 1)->start);
        CHECK(r->start + r->size < (r + 1)->start);
      }
      if (base::IsInRange(r->start, pos, page->HighWaterMark())) {
        size_t segment_size = r->start - pos;
        ReadOnlySegmentForSerialization segment(isolate_, page, pos,
                                                segment_size, &pre_processor_);
        EmitSegment(&segment);
        pos += segment_size + r->size;
      }
    }

    // Pages are shrunk, but memory at the end of the area is still
    // uninitialized and we do not want to include it in the snapshot.
    size_t segment_size = page->HighWaterMark() - pos;
    ReadOnlySegmentForSerialization segment(isolate_, page, pos, segment_size,
                                            &pre_processor_);
    EmitSegment(&segment);
  }

  void EmitSegment(const ReadOnlySegmentForSerialization* segment) {
    sink_->Put(Bytecode::kSegment, "segment begin");
    sink_->PutUint30(IndexOf(segment->page), "page index");
    sink_->PutUint30(static_cast<uint32_t>(segment->segment_offset),
                     "segment start offset");
    sink_->PutUint30(static_cast<uint32_t>(segment->segment_size),
                     "segment byte size");
    sink_->PutRaw(segment->contents.get(),
                  static_cast<int>(segment->segment_size), "page");
    if (!V8_STATIC_ROOTS_BOOL) {
      sink_->Put(Bytecode::kRelocateSegment, "relocate segment");
      sink_->PutRaw(segment->tagged_slots.data(),
                    static_cast<int>(segment->tagged_slots.size_in_bytes()),
                    "tagged_slots");
    }
  }

  void EmitReadOnlyRootsTable() {
    sink_->Put(Bytecode::kReadOnlyRootsTable, "read only roots table");
    if (!V8_STATIC_ROOTS_BOOL) {
      ReadOnlyRoots roots(isolate_);
      for (size_t i = 0; i < ReadOnlyRoots::kEntriesCount; i++) {
        RootIndex rudi = static_cast<RootIndex>(i);
        Tagged<HeapObject> rudolf = Cast<HeapObject>(roots.object_at(rudi));
        ro::EncodedTagged encoded = Encode(isolate_, rudolf);
        sink_->PutUint32(encoded.ToUint32(), "read only roots entry");
      }
    }
  }

  Isolate* const isolate_;
  SnapshotByteSink* const sink_;
  ObjectPreProcessor pre_processor_;
};

std::vector<ReadOnlyHeapImageSerializer::MemoryRegion> GetUnmappedRegions(
    Isolate* isolate) {
#ifdef V8_STATIC_ROOTS
  // WasmNull's payload is aligned to the OS page and consists of
  // WasmNull::kPayloadSize bytes of unmapped memory. To avoid inflating the
  // snapshot size and accessing uninitialized and/or unmapped memory, the
  // serializer skips the padding bytes and the payload.
  ReadOnlyRoots ro_roots(isolate);
  Tagged<WasmNull> wasm_null = ro_roots.wasm_null();
  Tagged<HeapObject> wasm_null_padding = ro_roots.wasm_null_padding();
  CHECK(IsFreeSpace(wasm_null_padding));
  Address wasm_null_padding_start =
      wasm_null_padding.address() + FreeSpace::kHeaderSize;
  std::vector<ReadOnlyHeapImageSerializer::MemoryRegion> unmapped;
  if (wasm_null.address() > wasm_null_padding_start) {
    unmapped.push_back({wasm_null_padding_start,
                        wasm_null.address() - wasm_null_padding_start});
  }
  unmapped.push_back({wasm_null->payload(), WasmNull::kPayloadSize});
  return unmapped;
#else
  return {};
#endif  // V8_STATIC_ROOTS
}

}  // namespace

ReadOnlySerializer::ReadOnlySerializer(Isolate* isolate,
                                       Snapshot::SerializerFlags flags)
    : RootsSerializer(isolate, flags, RootIndex::kFirstReadOnlyRoot) {}

ReadOnlySerializer::~ReadOnlySerializer() {
  OutputStatistics("ReadOnlySerializer");
}

void ReadOnlySerializer::Serialize() {
  DisallowGarbageCollection no_gc;
  ReadOnlyHeapImageSerializer::Serialize(isolate(), &sink_,
                                         GetUnmappedRegions(isolate()));

  ReadOnlyHeapObjectIterator it(isolate()->read_only_heap());
  for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
    CheckRehashability(o);
    if (v8_flags.serialization_statistics) {
      CountAllocation(o->map(), o->Size(), SnapshotSpace::kReadOnlyHeap);
    }
  }
}

}  // namespace internal
}  // namespace v8
