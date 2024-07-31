// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_REFERENCES_H_
#define V8_SNAPSHOT_REFERENCES_H_

#include "src/base/bit-field.h"
#include "src/base/hashmap.h"
#include "src/execution/isolate.h"
#include "src/utils/identity-map.h"

namespace v8 {
namespace internal {

// Values must be contiguous and start at 0 since they're directly used as
// array indices.
enum class SnapshotSpace : uint8_t {
  kReadOnlyHeap = 0,
  kOld = 1,
  kCode = 2,
  kTrusted = 3,
};
static constexpr int kNumberOfSnapshotSpaces = 4;

class SerializerReference {
 private:
  enum SpecialValueType {
    kBackReference,
    kAttachedReference,
    kOffHeapBackingStore,
    kBuiltinReference,
  };

  SerializerReference(SpecialValueType type, uint32_t value)
      : bit_field_(TypeBits::encode(type) | ValueBits::encode(value)) {}

 public:
  static SerializerReference BackReference(uint32_t index) {
    return SerializerReference(kBackReference, index);
  }

  static SerializerReference OffHeapBackingStoreReference(uint32_t index) {
    return SerializerReference(kOffHeapBackingStore, index);
  }

  static SerializerReference AttachedReference(uint32_t index) {
    return SerializerReference(kAttachedReference, index);
  }

  static SerializerReference BuiltinReference(uint32_t index) {
    return SerializerReference(kBuiltinReference, index);
  }

  bool is_back_reference() const {
    return TypeBits::decode(bit_field_) == kBackReference;
  }

  uint32_t back_ref_index() const {
    DCHECK(is_back_reference());
    return ValueBits::decode(bit_field_);
  }

  bool is_off_heap_backing_store_reference() const {
    return TypeBits::decode(bit_field_) == kOffHeapBackingStore;
  }

  uint32_t off_heap_backing_store_index() const {
    DCHECK(is_off_heap_backing_store_reference());
    return ValueBits::decode(bit_field_);
  }

  bool is_attached_reference() const {
    return TypeBits::decode(bit_field_) == kAttachedReference;
  }

  uint32_t attached_reference_index() const {
    DCHECK(is_attached_reference());
    return ValueBits::decode(bit_field_);
  }

  bool is_builtin_reference() const {
    return TypeBits::decode(bit_field_) == kBuiltinReference;
  }

  uint32_t builtin_index() const {
    DCHECK(is_builtin_reference());
    return ValueBits::decode(bit_field_);
  }

 private:
  using TypeBits = base::BitField<SpecialValueType, 0, 2>;
  using ValueBits = TypeBits::Next<uint32_t, 32 - TypeBits::kSize>;

  uint32_t bit_field_;

  friend class SerializerReferenceMap;
};

// SerializerReference has to fit in an IdentityMap value field.
static_assert(sizeof(SerializerReference) <= sizeof(void*));

class SerializerReferenceMap {
 public:
  explicit SerializerReferenceMap(Isolate* isolate)
      : map_(isolate->heap()), attached_reference_index_(0) {}

  const SerializerReference* LookupReference(Tagged<HeapObject> object) const {
    return map_.Find(object);
  }

  const SerializerReference* LookupReference(Handle<HeapObject> object) const {
    return map_.Find(object);
  }

  const SerializerReference* LookupBackingStore(void* backing_store) const {
    auto it = backing_store_map_.find(backing_store);
    if (it == backing_store_map_.end()) return nullptr;
    return &it->second;
  }

  void Add(Tagged<HeapObject> object, SerializerReference reference) {
    DCHECK_NULL(LookupReference(object));
    map_.Insert(object, reference);
  }

  void AddBackingStore(void* backing_store, SerializerReference reference) {
    DCHECK(backing_store_map_.find(backing_store) == backing_store_map_.end());
    backing_store_map_.emplace(backing_store, reference);
  }

  SerializerReference AddAttachedReference(Tagged<HeapObject> object) {
    SerializerReference reference =
        SerializerReference::AttachedReference(attached_reference_index_++);
    map_.Insert(object, reference);
    return reference;
  }

 private:
  IdentityMap<SerializerReference, base::DefaultAllocationPolicy> map_;
  std::unordered_map<void*, SerializerReference> backing_store_map_;
  int attached_reference_index_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_REFERENCES_H_
