// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_DESERIALIZER_H_
#define V8_SNAPSHOT_DESERIALIZER_H_

#include <utility>
#include <vector>

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/execution/local-isolate.h"
#include "src/handles/global-handles.h"
#include "src/objects/allocation-site.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/backing-store.h"
#include "src/objects/code.h"
#include "src/objects/map.h"
#include "src/objects/objects.h"
#include "src/objects/string-table.h"
#include "src/objects/string.h"
#include "src/snapshot/serializer-deserializer.h"
#include "src/snapshot/snapshot-source-sink.h"

namespace v8 {
namespace internal {

class HeapObject;
class Object;

// Used for platforms with embedded constant pools to trigger deserialization
// of objects found in code.
#if defined(V8_TARGET_ARCH_MIPS64) || defined(V8_TARGET_ARCH_S390X) ||  \
    defined(V8_TARGET_ARCH_PPC64) || defined(V8_TARGET_ARCH_RISCV32) || \
    defined(V8_TARGET_ARCH_RISCV64) || V8_EMBEDDED_CONSTANT_POOL_BOOL
#define V8_CODE_EMBEDS_OBJECT_POINTER 1
#else
#define V8_CODE_EMBEDS_OBJECT_POINTER 0
#endif

// A Deserializer reads a snapshot and reconstructs the Object graph it defines.
template <typename IsolateT>
class Deserializer : public SerializerDeserializer {
 public:
  ~Deserializer() override;
  Deserializer(const Deserializer&) = delete;
  Deserializer& operator=(const Deserializer&) = delete;

 protected:
  // Create a deserializer from a snapshot byte source.
  Deserializer(IsolateT* isolate, base::Vector<const uint8_t> payload,
               uint32_t magic_number, bool deserializing_user_code,
               bool can_rehash);

  void DeserializeDeferredObjects();

  // Create Log events for newly deserialized objects.
  void LogNewObjectEvents();
  void LogScriptEvents(Tagged<Script> script);
  void LogNewMapEvents();

  // Descriptor arrays are deserialized as "strong", so that there is no risk of
  // them getting trimmed during a partial deserialization. This method makes
  // them "weak" again after deserialization completes.
  void WeakenDescriptorArrays();

  // This returns the address of an object that has been described in the
  // snapshot by object vector index.
  Handle<HeapObject> GetBackReferencedObject();
  Handle<HeapObject> GetBackReferencedObject(uint32_t index);

  // Add an object to back an attached reference. The order to add objects must
  // mirror the order they are added in the serializer.
  void AddAttachedObject(DirectHandle<HeapObject> attached_object) {
    attached_objects_.push_back(attached_object);
  }

  IsolateT* isolate() const { return isolate_; }

  Isolate* main_thread_isolate() const { return isolate_->AsIsolate(); }

  SnapshotByteSource* source() { return &source_; }

  base::Vector<const DirectHandle<AllocationSite>> new_allocation_sites()
      const {
    return {new_allocation_sites_.data(), new_allocation_sites_.size()};
  }
  base::Vector<const DirectHandle<InstructionStream>> new_code_objects() const {
    return {new_code_objects_.data(), new_code_objects_.size()};
  }
  base::Vector<const DirectHandle<Map>> new_maps() const {
    return {new_maps_.data(), new_maps_.size()};
  }
  base::Vector<const DirectHandle<AccessorInfo>> accessor_infos() const {
    return {accessor_infos_.data(), accessor_infos_.size()};
  }
  base::Vector<const DirectHandle<FunctionTemplateInfo>>
  function_template_infos() const {
    return {function_template_infos_.data(), function_template_infos_.size()};
  }
  base::Vector<const DirectHandle<Script>> new_scripts() const {
    return {new_scripts_.data(), new_scripts_.size()};
  }

  std::shared_ptr<BackingStore> backing_store(size_t i) {
    DCHECK_LT(i, backing_stores_.size());
    return backing_stores_[i];
  }

  bool deserializing_user_code() const { return deserializing_user_code_; }
  bool should_rehash() const { return should_rehash_; }

  void PushObjectToRehash(DirectHandle<HeapObject> object) {
    to_rehash_.push_back(object);
  }
  void Rehash();

  DirectHandle<HeapObject> ReadObject();

 private:
  // A circular queue of hot objects. This is added to in the same order as in
  // Serializer::HotObjectsList, but this stores the objects as a vector of
  // existing handles. This allows us to add Handles to the queue without having
  // to create new handles. Note that this depends on those Handles staying
  // valid as long as the HotObjectsList is alive.
  class HotObjectsList {
   public:
    HotObjectsList() = default;
    HotObjectsList(const HotObjectsList&) = delete;
    HotObjectsList& operator=(const HotObjectsList&) = delete;

    void Add(DirectHandle<HeapObject> object) {
      circular_queue_[index_] = object;
      index_ = (index_ + 1) & kSizeMask;
    }

    DirectHandle<HeapObject> Get(int index) {
      DCHECK(!circular_queue_[index].is_null());
      return circular_queue_[index];
    }

   private:
    static const int kSize = kHotObjectCount;
    static const int kSizeMask = kSize - 1;
    static_assert(base::bits::IsPowerOfTwo(kSize));
    DirectHandle<HeapObject> circular_queue_[kSize];
    int index_ = 0;
  };

  struct ReferenceDescriptor {
    HeapObjectReferenceType type;
    bool is_indirect_pointer;
    bool is_protected_pointer;
  };

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) override;

  void Synchronize(VisitorSynchronization::SyncTag tag) override;

  template <typename SlotAccessor>
  int WriteHeapPointer(SlotAccessor slot_accessor,
                       Tagged<HeapObject> heap_object,
                       ReferenceDescriptor descr,
                       WriteBarrierMode mode = UPDATE_WRITE_BARRIER);
  template <typename SlotAccessor>
  int WriteHeapPointer(SlotAccessor slot_accessor,
                       DirectHandle<HeapObject> heap_object,
                       ReferenceDescriptor descr,
                       WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  inline int WriteExternalPointer(Tagged<HeapObject> host,
                                  ExternalPointerSlot dest, Address value,
                                  ExternalPointerTag tag);
  inline int WriteIndirectPointer(IndirectPointerSlot dest,
                                  Tagged<HeapObject> value);

  // Fills in a heap object's data from start to end (exclusive). Start and end
  // are slot indices within the object.
  void ReadData(Handle<HeapObject> object, int start_slot_index,
                int end_slot_index);

  // Fills in a contiguous range of full object slots (e.g. root pointers) from
  // start to end (exclusive).
  void ReadData(FullMaybeObjectSlot start, FullMaybeObjectSlot end);

  // Helper for ReadData which reads the given bytecode and fills in some heap
  // data into the given slot. May fill in zero or multiple slots, so it returns
  // the number of slots filled.
  template <typename SlotAccessor>
  int ReadSingleBytecodeData(uint8_t data, SlotAccessor slot_accessor);

  template <typename SlotAccessor>
  int ReadNewObject(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadBackref(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadReadOnlyHeapRef(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadRootArray(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadStartupObjectCache(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadSharedHeapObjectCache(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadNewMetaMap(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadExternalReference(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadRawExternalReference(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadAttachedReference(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadRegisterPendingForwardRef(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadResolvePendingForwardRef(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadVariableRawData(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadVariableRepeatRoot(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadOffHeapBackingStore(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadApiReference(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadClearedWeakReference(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadWeakPrefix(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadIndirectPointerPrefix(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadInitializeSelfIndirectPointer(uint8_t data,
                                        SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadAllocateJSDispatchEntry(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadJSDispatchEntry(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadProtectedPointerPrefix(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadRootArrayConstants(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadHotObject(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadFixedRawData(uint8_t data, SlotAccessor slot_accessor);
  template <typename SlotAccessor>
  int ReadFixedRepeatRoot(uint8_t data, SlotAccessor slot_accessor);

  // A helper function for ReadData for reading external references.
  inline Address ReadExternalReferenceCase();

  // A helper function for reading external pointer tags.
  ExternalPointerTag ReadExternalPointerTag();

  Handle<HeapObject> ReadObject(SnapshotSpace space);
  Handle<HeapObject> ReadMetaMap(SnapshotSpace space);

  ReferenceDescriptor GetAndResetNextReferenceDescriptor();

  template <typename SlotGetter>
  int ReadRepeatedRoot(SlotGetter slot_getter, int repeat_count);

  // Special handling for serialized code like hooking up internalized strings.
  void PostProcessNewObject(DirectHandle<Map> map, Handle<HeapObject> obj,
                            SnapshotSpace space);
  void PostProcessNewJSReceiver(Tagged<Map> map, DirectHandle<JSReceiver> obj,
                                InstanceType instance_type,
                                SnapshotSpace space);

  Tagged<HeapObject> Allocate(AllocationType allocation, int size,
                              AllocationAlignment alignment);

  // Cached current isolate.
  IsolateT* isolate_;

  // Objects from the attached object descriptions in the serialized user code.
  DirectHandleVector<HeapObject> attached_objects_;

  SnapshotByteSource source_;
  uint32_t magic_number_;

  HotObjectsList hot_objects_;
  DirectHandleVector<Map> new_maps_;
  DirectHandleVector<AllocationSite> new_allocation_sites_;
  DirectHandleVector<InstructionStream> new_code_objects_;
  DirectHandleVector<AccessorInfo> accessor_infos_;
  DirectHandleVector<FunctionTemplateInfo> function_template_infos_;
  DirectHandleVector<Script> new_scripts_;
  std::vector<std::shared_ptr<BackingStore>> backing_stores_;

  // Roots vector as those arrays are passed to Heap, see
  // WeakenDescriptorArrays().
  GlobalHandleVector<DescriptorArray> new_descriptor_arrays_;

  // Vector of allocated objects that can be accessed by a backref, by index.
  std::vector<IndirectHandle<HeapObject>> back_refs_;

  // Vector of already allocated JSDispatchTable entries.
  std::vector<JSDispatchHandle> js_dispatch_entries_;

  // Unresolved forward references (registered with kRegisterPendingForwardRef)
  // are collected in order as (object, field offset) pairs. The subsequent
  // forward ref resolution (with kResolvePendingForwardRef) accesses this
  // vector by index.
  //
  // The vector is cleared when there are no more unresolved forward refs.
  struct UnresolvedForwardRef {
    UnresolvedForwardRef(Handle<HeapObject> object, int offset,
                         ReferenceDescriptor descr)
        : object(object), offset(offset), descr(descr) {}

    IndirectHandle<HeapObject> object;
    int offset;
    ReferenceDescriptor descr;
  };
  std::vector<UnresolvedForwardRef> unresolved_forward_refs_;
  int num_unresolved_forward_refs_ = 0;

  const bool deserializing_user_code_;

  bool next_reference_is_weak_ = false;
  bool next_reference_is_indirect_pointer_ = false;
  bool next_reference_is_protected_pointer = false;

  // TODO(6593): generalize rehashing, and remove this flag.
  const bool should_rehash_;
  DirectHandleVector<HeapObject> to_rehash_;

  // Do not collect any gc stats during deserialization since objects might
  // be in an invalid state
  class V8_NODISCARD DisableGCStats {
   public:
    DisableGCStats() {
      original_gc_stats_ = TracingFlags::gc_stats;
      TracingFlags::gc_stats = 0;
    }
    ~DisableGCStats() { TracingFlags::gc_stats = original_gc_stats_; }

   private:
    unsigned int original_gc_stats_;
  };
  DisableGCStats no_gc_stats_;

  int depth_ = 0;

#ifdef DEBUG
  uint32_t num_api_references_;

  // Record the previous object allocated for DCHECKs.
  DirectHandle<HeapObject> previous_allocation_obj_;
  int previous_allocation_size_ = 0;
#endif  // DEBUG
};

enum class DeserializingUserCodeOption {
  kNotDeserializingUserCode,
  kIsDeserializingUserCode
};

// Used to insert a deserialized internalized string into the string table.
class StringTableInsertionKey final : public StringTableKey {
 public:
  explicit StringTableInsertionKey(
      Isolate* isolate, DirectHandle<String> string,
      DeserializingUserCodeOption deserializing_user_code);
  explicit StringTableInsertionKey(
      LocalIsolate* isolate, DirectHandle<String> string,
      DeserializingUserCodeOption deserializing_user_code);

  template <typename IsolateT>
  bool IsMatch(IsolateT* isolate, Tagged<String> string);

  void PrepareForInsertion(Isolate* isolate) {
    // When sharing the string table, all string table lookups during snapshot
    // deserialization are hits.
    DCHECK(isolate->OwnsStringTables() ||
           deserializing_user_code_ ==
               DeserializingUserCodeOption::kIsDeserializingUserCode);
  }
  void PrepareForInsertion(LocalIsolate* isolate) {}
  V8_WARN_UNUSED_RESULT DirectHandle<String> GetHandleForInsertion(
      Isolate* isolate) {
    return string_;
  }

 private:
  DirectHandle<String> string_;
#ifdef DEBUG
  DeserializingUserCodeOption deserializing_user_code_;
#endif
  DISALLOW_GARBAGE_COLLECTION(no_gc)
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_DESERIALIZER_H_
