// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_SERIALIZER_H_
#define V8_SNAPSHOT_SERIALIZER_H_

#include "src/codegen/external-reference-encoder.h"
#include "src/common/assert-scope.h"
#include "src/execution/isolate.h"
#include "src/handles/global-handles.h"
#include "src/logging/log.h"
#include "src/objects/abstract-code.h"
#include "src/objects/bytecode-array.h"
#include "src/objects/instruction-stream.h"
#include "src/objects/objects.h"
#include "src/snapshot/serializer-deserializer.h"
#include "src/snapshot/snapshot-source-sink.h"
#include "src/snapshot/snapshot.h"
#include "src/utils/identity-map.h"

namespace v8 {
namespace internal {

class CodeAddressMap : public CodeEventLogger {
 public:
  explicit CodeAddressMap(Isolate* isolate) : CodeEventLogger(isolate) {
    CHECK(isolate->logger()->AddListener(this));
  }

  ~CodeAddressMap() override {
    CHECK(isolate_->logger()->RemoveListener(this));
  }

  void CodeMoveEvent(Tagged<InstructionStream> from,
                     Tagged<InstructionStream> to) override {
    address_to_name_map_.Move(from.address(), to.address());
  }
  void BytecodeMoveEvent(Tagged<BytecodeArray> from,
                         Tagged<BytecodeArray> to) override {
    address_to_name_map_.Move(from.address(), to.address());
  }

  void CodeDisableOptEvent(Handle<AbstractCode> code,
                           Handle<SharedFunctionInfo> shared) override {}

  const char* Lookup(Address address) {
    return address_to_name_map_.Lookup(address);
  }

 private:
  class NameMap {
   public:
    NameMap() : impl_() {}
    NameMap(const NameMap&) = delete;
    NameMap& operator=(const NameMap&) = delete;

    ~NameMap() {
      for (base::HashMap::Entry* p = impl_.Start(); p != nullptr;
           p = impl_.Next(p)) {
        DeleteArray(static_cast<const char*>(p->value));
      }
    }

    void Insert(Address code_address, const char* name, int name_size) {
      base::HashMap::Entry* entry = FindOrCreateEntry(code_address);
      if (entry->value == nullptr) {
        entry->value = CopyName(name, name_size);
      }
    }

    const char* Lookup(Address code_address) {
      base::HashMap::Entry* entry = FindEntry(code_address);
      return (entry != nullptr) ? static_cast<const char*>(entry->value)
                                : nullptr;
    }

    void Remove(Address code_address) {
      base::HashMap::Entry* entry = FindEntry(code_address);
      if (entry != nullptr) {
        DeleteArray(static_cast<char*>(entry->value));
        RemoveEntry(entry);
      }
    }

    void Move(Address from, Address to) {
      if (from == to) return;
      base::HashMap::Entry* from_entry = FindEntry(from);
      DCHECK_NOT_NULL(from_entry);
      void* value = from_entry->value;
      RemoveEntry(from_entry);
      base::HashMap::Entry* to_entry = FindOrCreateEntry(to);
      DCHECK_NULL(to_entry->value);
      to_entry->value = value;
    }

   private:
    static char* CopyName(const char* name, int name_size) {
      char* result = NewArray<char>(name_size + 1);
      for (int i = 0; i < name_size; ++i) {
        char c = name[i];
        if (c == '\0') c = ' ';
        result[i] = c;
      }
      result[name_size] = '\0';
      return result;
    }

    base::HashMap::Entry* FindOrCreateEntry(Address code_address) {
      return impl_.LookupOrInsert(reinterpret_cast<void*>(code_address),
                                  ComputeAddressHash(code_address));
    }

    base::HashMap::Entry* FindEntry(Address code_address) {
      return impl_.Lookup(reinterpret_cast<void*>(code_address),
                          ComputeAddressHash(code_address));
    }

    void RemoveEntry(base::HashMap::Entry* entry) {
      impl_.Remove(entry->key, entry->hash);
    }

    base::HashMap impl_;
  };

  void LogRecordedBuffer(Tagged<AbstractCode> code,
                         MaybeHandle<SharedFunctionInfo>, const char* name,
                         int length) override {
    DisallowGarbageCollection no_gc;
    address_to_name_map_.Insert(code.address(), name, length);
  }

#if V8_ENABLE_WEBASSEMBLY
  void LogRecordedBuffer(const wasm::WasmCode* code, const char* name,
                         int length) override {
    UNREACHABLE();
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  NameMap address_to_name_map_;
};

class ObjectCacheIndexMap {
 public:
  explicit ObjectCacheIndexMap(Heap* heap) : map_(heap), next_index_(0) {}
  ObjectCacheIndexMap(const ObjectCacheIndexMap&) = delete;
  ObjectCacheIndexMap& operator=(const ObjectCacheIndexMap&) = delete;

  // If |obj| is in the map, immediately return true.  Otherwise add it to the
  // map and return false. In either case set |*index_out| to the index
  // associated with the map.
  bool LookupOrInsert(Tagged<HeapObject> obj, int* index_out) {
    auto find_result = map_.FindOrInsert(obj);
    if (!find_result.already_exists) {
      *find_result.entry = next_index_++;
    }
    *index_out = *find_result.entry;
    return find_result.already_exists;
  }
  bool LookupOrInsert(Handle<HeapObject> obj, int* index_out) {
    return LookupOrInsert(*obj, index_out);
  }

  bool Lookup(Tagged<HeapObject> obj, int* index_out) const {
    int* index = map_.Find(obj);
    if (index == nullptr) {
      return false;
    }
    *index_out = *index;
    return true;
  }

  Handle<FixedArray> Values(Isolate* isolate);

  int size() const { return next_index_; }

 private:
  IdentityMap<int, base::DefaultAllocationPolicy> map_;
  int next_index_;
};

class Serializer : public SerializerDeserializer {
 public:
  Serializer(Isolate* isolate, Snapshot::SerializerFlags flags);
  ~Serializer() override { DCHECK_EQ(unresolved_forward_refs_, 0); }
  Serializer(const Serializer&) = delete;
  Serializer& operator=(const Serializer&) = delete;

  const std::vector<uint8_t>* Payload() const { return sink_.data(); }

  bool ReferenceMapContains(Handle<HeapObject> o) {
    return reference_map()->LookupReference(o) != nullptr;
  }

  Isolate* isolate() const { return isolate_; }

  // The pointer compression cage base value used for decompression of all
  // tagged values except references to InstructionStream objects.
  PtrComprCageBase cage_base() const {
#if V8_COMPRESS_POINTERS
    return cage_base_;
#else
    return PtrComprCageBase{};
#endif  // V8_COMPRESS_POINTERS
  }

  int TotalAllocationSize() const;

 protected:
  using PendingObjectReferences = std::vector<int>*;

  class ObjectSerializer;
  class V8_NODISCARD RecursionScope {
   public:
    explicit RecursionScope(Serializer* serializer) : serializer_(serializer) {
      serializer_->recursion_depth_++;
    }
    ~RecursionScope() { serializer_->recursion_depth_--; }
    bool ExceedsMaximum() const {
      return serializer_->recursion_depth_ > kMaxRecursionDepth;
    }
    int ExceedsMaximumBy() const {
      return serializer_->recursion_depth_ - kMaxRecursionDepth;
    }

   private:
    static const int kMaxRecursionDepth = 32;
    Serializer* serializer_;
  };

  // Compares obj with not_mapped_symbol root. When V8_EXTERNAL_CODE_SPACE is
  // enabled it compares full pointers.
  V8_INLINE bool IsNotMappedSymbol(Tagged<HeapObject> obj) const;

  void SerializeDeferredObjects();
  void SerializeObject(Handle<HeapObject> o, SlotType slot_type);
  virtual void SerializeObjectImpl(Handle<HeapObject> o,
                                   SlotType slot_type) = 0;

  virtual bool MustBeDeferred(Tagged<HeapObject> object);

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override;
  void SerializeRootObject(FullObjectSlot slot);

  void PutRoot(RootIndex root_index);
  void PutSmiRoot(FullObjectSlot slot);
  void PutBackReference(Tagged<HeapObject> object,
                        SerializerReference reference);
  void PutAttachedReference(SerializerReference reference);
  void PutNextChunk(SnapshotSpace space);
  void PutRepeat(int repeat_count);

  // Emit a marker noting that this slot is a forward reference to the an
  // object which has not yet been serialized.
  void PutPendingForwardReference(PendingObjectReferences& ref);
  // Resolve the given previously registered forward reference to the current
  // object.
  void ResolvePendingForwardReference(int obj);

  // Returns true if the object was successfully serialized as a root.
  bool SerializeRoot(Tagged<HeapObject> obj);

  // Returns true if the object was successfully serialized as hot object.
  bool SerializeHotObject(Tagged<HeapObject> obj);

  // Returns true if the object was successfully serialized as back reference.
  bool SerializeBackReference(Tagged<HeapObject> obj);

  // Returns true if the object was successfully serialized as pending object.
  bool SerializePendingObject(Tagged<HeapObject> obj);

  // Returns true if the given heap object is a bytecode handler code object.
  bool ObjectIsBytecodeHandler(Tagged<HeapObject> obj) const;

  ExternalReferenceEncoder::Value EncodeExternalReference(Address addr);

  Maybe<ExternalReferenceEncoder::Value> TryEncodeExternalReference(
      Address addr) {
    return external_reference_encoder_.TryEncode(addr);
  }

  bool SerializeReadOnlyObjectReference(Tagged<HeapObject> obj,
                                        SnapshotByteSink* sink);

  // GetInt reads 4 bytes at once, requiring padding at the end.
  // Use padding_offset to specify the space you want to use after padding.
  void Pad(int padding_offset = 0);

  // We may not need the code address map for logging for every instance
  // of the serializer.  Initialize it on demand.
  void InitializeCodeAddressMap();

  Tagged<InstructionStream> CopyCode(Tagged<InstructionStream> istream);

  void QueueDeferredObject(Tagged<HeapObject> obj) {
    DCHECK_NULL(reference_map_.LookupReference(obj));
    deferred_objects_.Push(obj);
  }

  // Register that the the given object shouldn't be immediately serialized, but
  // will be serialized later and any references to it should be pending forward
  // references.
  void RegisterObjectIsPending(Tagged<HeapObject> obj);

  // Resolve the given pending object reference with the current object.
  void ResolvePendingObject(Tagged<HeapObject> obj);

  void OutputStatistics(const char* name);

  void CountAllocation(Tagged<Map> map, int size, SnapshotSpace space);

#ifdef DEBUG
  void PushStack(Handle<HeapObject> o) { stack_.Push(*o); }
  void PopStack();
  void PrintStack();
  void PrintStack(std::ostream&);
#endif  // DEBUG

  SerializerReferenceMap* reference_map() { return &reference_map_; }
  const RootIndexMap* root_index_map() const { return &root_index_map_; }

  SnapshotByteSink sink_;  // Used directly by subclasses.

  bool allow_unknown_external_references_for_testing() const {
    return (flags_ & Snapshot::kAllowUnknownExternalReferencesForTesting) != 0;
  }
  bool allow_active_isolate_for_testing() const {
    return (flags_ & Snapshot::kAllowActiveIsolateForTesting) != 0;
  }

  bool reconstruct_read_only_and_shared_object_caches_for_testing() const {
    return (flags_ &
            Snapshot::kReconstructReadOnlyAndSharedObjectCachesForTesting) != 0;
  }

  bool deferred_objects_empty() { return deferred_objects_.size() == 0; }

 protected:
  bool serializer_tracks_serialization_statistics() const {
    return serializer_tracks_serialization_statistics_;
  }
  void set_serializer_tracks_serialization_statistics(bool v) {
    serializer_tracks_serialization_statistics_ = v;
  }

 private:
  // A circular queue of hot objects. This is added to in the same order as in
  // Deserializer::HotObjectsList, but this stores the objects as an array of
  // raw addresses that are considered strong roots. This allows objects to be
  // added to the list without having to extend their handle's lifetime.
  //
  // We should never allow this class to return Handles to objects in the queue,
  // as the object in the queue may change if kSize other objects are added to
  // the queue during that Handle's lifetime.
  class HotObjectsList {
   public:
    explicit HotObjectsList(Heap* heap);
    ~HotObjectsList();
    HotObjectsList(const HotObjectsList&) = delete;
    HotObjectsList& operator=(const HotObjectsList&) = delete;

    void Add(Tagged<HeapObject> object) {
      circular_queue_[index_] = object.ptr();
      index_ = (index_ + 1) & kSizeMask;
    }

    static const int kNotFound = -1;

    int Find(Tagged<HeapObject> object) {
      DCHECK(!AllowGarbageCollection::IsAllowed());
      for (int i = 0; i < kSize; i++) {
        if (circular_queue_[i] == object.ptr()) {
          return i;
        }
      }
      return kNotFound;
    }

   private:
    static const int kSize = kHotObjectCount;
    static const int kSizeMask = kSize - 1;
    static_assert(base::bits::IsPowerOfTwo(kSize));
    Heap* heap_;
    StrongRootsEntry* strong_roots_entry_;
    Address circular_queue_[kSize] = {kNullAddress};
    int index_ = 0;
  };

  // Disallow GC during serialization.
  // TODO(leszeks, v8:10815): Remove this constraint.
  DISALLOW_GARBAGE_COLLECTION(no_gc_)

  Isolate* isolate_;
#if V8_COMPRESS_POINTERS
  const PtrComprCageBase cage_base_;
#endif  // V8_COMPRESS_POINTERS
  HotObjectsList hot_objects_;
  SerializerReferenceMap reference_map_;
  ExternalReferenceEncoder external_reference_encoder_;
  RootIndexMap root_index_map_;
  std::unique_ptr<CodeAddressMap> code_address_map_;
  std::vector<uint8_t> code_buffer_;
  GlobalHandleVector<HeapObject>
      deferred_objects_;  // To handle stack overflow.
  int num_back_refs_ = 0;

  // Objects which have started being serialized, but haven't yet been allocated
  // with the allocator, are considered "pending". References to them don't have
  // an allocation to backref to, so instead they are registered as pending
  // forward references, which are resolved once the object is allocated.
  //
  // Forward references are registered in a deterministic order, and can
  // therefore be identified by an incrementing integer index, which is
  // effectively an index into a vector of the currently registered forward
  // refs. The references in this vector might not be resolved in order, so we
  // can only clear it (and reset the indices) when there are no unresolved
  // forward refs remaining.
  int next_forward_ref_id_ = 0;
  int unresolved_forward_refs_ = 0;
  IdentityMap<PendingObjectReferences, base::DefaultAllocationPolicy>
      forward_refs_per_pending_object_;

  // Used to keep track of the off-heap backing stores used by TypedArrays/
  // ArrayBuffers. Note that the index begins at 1 and not 0, because when a
  // TypedArray has an on-heap backing store, the backing_store pointer in the
  // corresponding ArrayBuffer will be null, which makes it indistinguishable
  // from index 0.
  uint32_t seen_backing_stores_index_ = 1;

  int recursion_depth_ = 0;
  const Snapshot::SerializerFlags flags_;

  bool serializer_tracks_serialization_statistics_ = true;
  size_t allocation_size_[kNumberOfSnapshotSpaces] = {0};
#ifdef OBJECT_PRINT
// Verbose serialization_statistics output is only enabled conditionally.
#define VERBOSE_SERIALIZATION_STATISTICS
#endif
#ifdef VERBOSE_SERIALIZATION_STATISTICS
  static constexpr int kInstanceTypes = LAST_TYPE + 1;
  std::unique_ptr<int[]> instance_type_count_[kNumberOfSnapshotSpaces];
  std::unique_ptr<size_t[]> instance_type_size_[kNumberOfSnapshotSpaces];
#endif  // VERBOSE_SERIALIZATION_STATISTICS

#ifdef DEBUG
  GlobalHandleVector<HeapObject> back_refs_;
  GlobalHandleVector<HeapObject> stack_;
#endif  // DEBUG
};

class Serializer::ObjectSerializer : public ObjectVisitor {
 public:
  ObjectSerializer(Serializer* serializer, Handle<HeapObject> obj,
                   SnapshotByteSink* sink)
      : isolate_(serializer->isolate()),
        serializer_(serializer),
        object_(obj),
        sink_(sink),
        bytes_processed_so_far_(0) {
#ifdef DEBUG
    serializer_->PushStack(obj);
#endif  // DEBUG
  }
  ~ObjectSerializer() override {
#ifdef DEBUG
    serializer_->PopStack();
#endif  // DEBUG
  }
  void Serialize(SlotType slot_type);
  void SerializeObject();
  void SerializeDeferred();
  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override;
  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) override;
  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override;
  void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                            RelocInfo* target) override;
  void VisitExternalReference(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) override;
  void VisitInternalReference(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) override;
  void VisitCodeTarget(Tagged<InstructionStream> host,
                       RelocInfo* target) override;
  void VisitOffHeapTarget(Tagged<InstructionStream> host,
                          RelocInfo* target) override;

  void VisitExternalPointer(Tagged<HeapObject> host,
                            ExternalPointerSlot slot) override;
  void VisitIndirectPointer(Tagged<HeapObject> host, IndirectPointerSlot slot,
                            IndirectPointerMode mode) override;
  void VisitTrustedPointerTableEntry(Tagged<HeapObject> host,
                                     IndirectPointerSlot slot) override;
  void VisitProtectedPointer(Tagged<TrustedObject> host,
                             ProtectedPointerSlot slot) override;

  Isolate* isolate() { return isolate_; }

 private:
  void SerializePrologue(SnapshotSpace space, int size, Tagged<Map> map);

  // This function outputs or skips the raw data between the last pointer and
  // up to the current position.
  void SerializeContent(Tagged<Map> map, int size);
  void OutputExternalReference(Address target, int target_size, bool sandboxify,
                               ExternalPointerTag tag);
  void OutputRawData(Address up_to);
  uint32_t SerializeBackingStore(void* backing_store, uint32_t byte_length,
                                 Maybe<uint32_t> max_byte_length);
  void SerializeJSTypedArray();
  void SerializeJSArrayBuffer();
  void SerializeExternalString();
  void SerializeExternalStringAsSequentialString();

  Isolate* isolate_;
  Serializer* serializer_;
  Handle<HeapObject> object_;
  SnapshotByteSink* sink_;
  int bytes_processed_so_far_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_SERIALIZER_H_
