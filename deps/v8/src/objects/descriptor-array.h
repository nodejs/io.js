// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DESCRIPTOR_ARRAY_H_
#define V8_OBJECTS_DESCRIPTOR_ARRAY_H_

#include "src/common/globals.h"
#include "src/objects/fixed-array.h"
// TODO(jkummerow): Consider forward-declaring instead.
#include "src/base/bit-field.h"
#include "src/objects/internal-index.h"
#include "src/objects/objects.h"
#include "src/objects/struct.h"
#include "src/utils/utils.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class StructBodyDescriptor;

#include "torque-generated/src/objects/descriptor-array-tq.inc"

// An EnumCache is a pair used to hold keys and indices caches.
class EnumCache : public TorqueGeneratedEnumCache<EnumCache, Struct> {
 public:
  DECL_VERIFIER(EnumCache)

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(EnumCache)
};

// A DescriptorArray is a custom array that holds instance descriptors.
// It has the following layout:
//   Header:
//     [16:0  bits]: number_of_all_descriptors (including slack)
//     [32:16 bits]: number_of_descriptors
//     [64:32 bits]: raw_gc_state (used by GC)
//     [kEnumCacheOffset]: enum cache
//   Elements:
//     [kHeaderSize + 0]: first key (and internalized String)
//     [kHeaderSize + 1]: first descriptor details (see PropertyDetails)
//     [kHeaderSize + 2]: first value for constants / Tagged<Smi>(1) when not
//     used
//   Slack:
//     [kHeaderSize + number of descriptors * 3]: start of slack
// The "value" fields store either values or field types. A field type is either
// FieldType::None(), FieldType::Any() or a weak reference to a Map. All other
// references are strong.
class DescriptorArray
    : public TorqueGeneratedDescriptorArray<DescriptorArray, HeapObject> {
 public:
  DECL_INT16_ACCESSORS(number_of_all_descriptors)
  DECL_INT16_ACCESSORS(number_of_descriptors)
  inline int16_t number_of_slack_descriptors() const;
  inline int number_of_entries() const;

  void ClearEnumCache();
  inline void CopyEnumCacheFrom(Tagged<DescriptorArray> array);
  static void InitializeOrChangeEnumCache(
      DirectHandle<DescriptorArray> descriptors, Isolate* isolate,
      DirectHandle<FixedArray> keys, DirectHandle<FixedArray> indices,
      AllocationType allocation_if_initialize);

  // Accessors for fetching instance descriptor at descriptor number.
  inline Tagged<Name> GetKey(InternalIndex descriptor_number) const;
  inline Tagged<Name> GetKey(PtrComprCageBase cage_base,
                             InternalIndex descriptor_number) const;
  inline Tagged<Object> GetStrongValue(InternalIndex descriptor_number);
  inline Tagged<Object> GetStrongValue(PtrComprCageBase cage_base,
                                       InternalIndex descriptor_number);
  inline Tagged<MaybeObject> GetValue(InternalIndex descriptor_number);
  inline Tagged<MaybeObject> GetValue(PtrComprCageBase cage_base,
                                      InternalIndex descriptor_number);
  inline PropertyDetails GetDetails(InternalIndex descriptor_number);
  inline int GetFieldIndex(InternalIndex descriptor_number);
  inline Tagged<FieldType> GetFieldType(InternalIndex descriptor_number);
  inline Tagged<FieldType> GetFieldType(PtrComprCageBase cage_base,
                                        InternalIndex descriptor_number);

  inline Tagged<Name> GetSortedKey(int descriptor_number);
  inline Tagged<Name> GetSortedKey(PtrComprCageBase cage_base,
                                   int descriptor_number);
  inline int GetSortedKeyIndex(int descriptor_number);

  // Accessor for complete descriptor.
  inline void Set(InternalIndex descriptor_number, Descriptor* desc);
  inline void Set(InternalIndex descriptor_number, Tagged<Name> key,
                  Tagged<MaybeObject> value, PropertyDetails details);
  void Replace(InternalIndex descriptor_number, Descriptor* descriptor);

  // Generalizes constness, representation and field type of all field
  // descriptors.
  void GeneralizeAllFields(bool clear_constness);

  // Append automatically sets the enumeration index. This should only be used
  // to add descriptors in bulk at the end, followed by sorting the descriptor
  // array.
  inline void Append(Descriptor* desc);

  static Handle<DescriptorArray> CopyUpTo(Isolate* isolate,
                                          DirectHandle<DescriptorArray> desc,
                                          int enumeration_index, int slack = 0);

  static Handle<DescriptorArray> CopyUpToAddAttributes(
      Isolate* isolate, DirectHandle<DescriptorArray> desc,
      int enumeration_index, PropertyAttributes attributes, int slack = 0);

  // Sort the instance descriptors by the hash codes of their keys.
  V8_EXPORT_PRIVATE void Sort();

  // Iterate through Name hash collisions in the descriptor array starting from
  // insertion index checking for Name collisions. Note: If we ever add binary
  // insertion for large DescriptorArrays it would need to be hardened in a
  // similar way. This function only expects to be called on Sorted
  // DescriptorArrays.
  V8_EXPORT_PRIVATE void CheckNameCollisionDuringInsertion(
      Descriptor* desc, uint32_t descriptor_hash, int insertion_index);

  // Search the instance descriptors for given name. {concurrent_search} signals
  // if we are doing the search on a background thread. If so, we will sacrifice
  // speed for thread-safety.
  V8_INLINE InternalIndex Search(Tagged<Name> name,
                                 int number_of_own_descriptors,
                                 bool concurrent_search = false);
  V8_INLINE InternalIndex Search(Tagged<Name> name, Tagged<Map> map,
                                 bool concurrent_search = false);

  // Search the instance descriptors for given field offset.
  V8_INLINE InternalIndex Search(int field_offset,
                                 int number_of_own_descriptors);
  V8_INLINE InternalIndex Search(int field_offset, Tagged<Map> map);

  // As the above, but uses DescriptorLookupCache and updates it when
  // necessary.
  V8_INLINE InternalIndex SearchWithCache(Isolate* isolate, Tagged<Name> name,
                                          Tagged<Map> map);

  bool IsEqualUpTo(Tagged<DescriptorArray> desc, int nof_descriptors);

  // Allocates a DescriptorArray, but returns the singleton
  // empty descriptor array object if number_of_descriptors is 0.
  template <typename IsolateT>
  V8_EXPORT_PRIVATE static Handle<DescriptorArray> Allocate(
      IsolateT* isolate, int nof_descriptors, int slack,
      AllocationType allocation = AllocationType::kYoung);

  void Initialize(Tagged<EnumCache> enum_cache,
                  Tagged<HeapObject> undefined_value, int nof_descriptors,
                  int slack, uint32_t raw_gc_state);

  // Constant for denoting key was not found.
  static const int kNotFound = -1;

  static_assert(IsAligned(kStartOfWeakFieldsOffset, kTaggedSize));
  static_assert(IsAligned(kHeaderSize, kTaggedSize));

  // Garbage collection support.
  DECL_RELAXED_UINT32_ACCESSORS(raw_gc_state)
  static constexpr size_t kSizeOfRawGcState =
      kRawGcStateOffsetEnd - kRawGcStateOffset + 1;

  static constexpr int SizeFor(int number_of_all_descriptors) {
    return OffsetOfDescriptorAt(number_of_all_descriptors);
  }
  static constexpr int OffsetOfDescriptorAt(int descriptor) {
    return kDescriptorsOffset + descriptor * kEntrySize * kTaggedSize;
  }
  inline ObjectSlot GetFirstPointerSlot();
  inline ObjectSlot GetDescriptorSlot(int descriptor);

  static_assert(kEndOfStrongFieldsOffset == kStartOfWeakFieldsOffset,
                "Weak fields follow strong fields.");
  static_assert(kEndOfWeakFieldsOffset == kHeaderSize,
                "Weak fields extend up to the end of the header.");
  static_assert(kDescriptorsOffset == kHeaderSize,
                "Variable-size array follows header.");
  class BodyDescriptor;

  // Layout of descriptor.
  // Naming is consistent with Dictionary classes for easy templating.
  static const int kEntryKeyIndex = 0;
  static const int kEntryDetailsIndex = 1;
  static const int kEntryValueIndex = 2;
  static const int kEntrySize = 3;

  static const int kEntryKeyOffset = kEntryKeyIndex * kTaggedSize;
  static const int kEntryDetailsOffset = kEntryDetailsIndex * kTaggedSize;
  static const int kEntryValueOffset = kEntryValueIndex * kTaggedSize;

  // Print all the descriptors.
  void PrintDescriptors(std::ostream& os);
  void PrintDescriptorDetails(std::ostream& os, InternalIndex descriptor,
                              PropertyDetails::PrintMode mode);

  DECL_PRINTER(DescriptorArray)
  DECL_VERIFIER(DescriptorArray)

#ifdef DEBUG
  // Is the descriptor array sorted and without duplicates?
  V8_EXPORT_PRIVATE bool IsSortedNoDuplicates();

  // Are two DescriptorArrays equal?
  bool IsEqualTo(Tagged<DescriptorArray> other);
#endif

  static constexpr int ToDetailsIndex(int descriptor_number) {
    return (descriptor_number * kEntrySize) + kEntryDetailsIndex;
  }

  // Conversion from descriptor number to array indices.
  static constexpr int ToKeyIndex(int descriptor_number) {
    return (descriptor_number * kEntrySize) + kEntryKeyIndex;
  }

  static constexpr int ToValueIndex(int descriptor_number) {
    return (descriptor_number * kEntrySize) + kEntryValueIndex;
  }

  using EntryKeyField = TaggedField<HeapObject, kEntryKeyOffset>;
  using EntryDetailsField = TaggedField<Smi, kEntryDetailsOffset>;
  using EntryValueField = TaggedField<MaybeObject, kEntryValueOffset>;

 private:
  inline void SetKey(InternalIndex descriptor_number, Tagged<Name> key);
  inline void SetValue(InternalIndex descriptor_number,
                       Tagged<MaybeObject> value);
  inline void SetDetails(InternalIndex descriptor_number,
                         PropertyDetails details);

  // Transfer a complete descriptor from the src descriptor array to this
  // descriptor array.
  void CopyFrom(InternalIndex index, Tagged<DescriptorArray> src);

  inline void SetSortedKey(int pointer, int descriptor_number);

  // Swap first and second descriptor.
  inline void SwapSortedKeys(int first, int second);

  TQ_OBJECT_CONSTRUCTORS(DescriptorArray)
};

// Custom DescriptorArray marking state for visitors that are allowed to write
// into the heap. The marking state uses DescriptorArray::raw_gc_state() as
// storage.
//
// The state essentially keeps track of 3 fields:
// 1. The collector epoch: The rest of the state is only valid if the epoch
//    matches. If the epoch doesn't match, the other fields should be considered
//    invalid. The epoch is necessary, as not all DescriptorArray objects are
//    eventually trimmed in the atomic pause and thus available for resetting
//    the state.
// 2. Number of already marked descriptors.
// 3. Delta of to be marked descriptors in this cycle. This must be 0 after
//    marking is done.
class DescriptorArrayMarkingState final {
 public:
#define BIT_FIELD_FIELDS(V, _) \
  V(Epoch, unsigned, 2, _)     \
  V(Marked, uint16_t, 14, _)   \
  V(Delta, uint16_t, 16, _)
  DEFINE_BIT_FIELDS(BIT_FIELD_FIELDS)
#undef BIT_FIELD_FIELDS
  static_assert(Marked::kMax <= Delta::kMax);
  static_assert(kMaxNumberOfDescriptors <= Marked::kMax);

  using DescriptorIndex = uint16_t;
  using RawGCStateType = uint32_t;

  static constexpr RawGCStateType kInitialGCState = 0;

  static constexpr RawGCStateType GetFullyMarkedState(
      unsigned epoch, DescriptorIndex number_of_descriptors) {
    return NewState(epoch & Epoch::kMask, number_of_descriptors, 0);
  }

  // Potentially updates the delta of to be marked descriptors. Returns true if
  // the update was successful and the object should be processed via a marking
  // visitor.
  //
  // The call issues and Acq/Rel barrier to allow synchronizing other state
  // (e.g. value of descriptor slots) with it.
  static inline bool TryUpdateIndicesToMark(unsigned gc_epoch,
                                            Tagged<DescriptorArray> array,
                                            DescriptorIndex index_to_mark);

  // Used from the visitor when processing a DescriptorArray. Returns a range of
  // start and end descriptor indices. No processing is required for start ==
  // end. The method signals the first invocation by returning start == 0, and
  // end != 0.
  static inline std::pair<DescriptorIndex, DescriptorIndex>
  AcquireDescriptorRangeToMark(unsigned gc_epoch,
                               Tagged<DescriptorArray> array);

 private:
  static constexpr RawGCStateType NewState(unsigned masked_epoch,
                                           DescriptorIndex marked,
                                           DescriptorIndex delta) {
    return Epoch::encode(masked_epoch) | Marked::encode(marked) |
           Delta::encode(delta);
  }

  static bool SwapState(Tagged<DescriptorArray> array, RawGCStateType old_state,
                        RawGCStateType new_state) {
    return static_cast<RawGCStateType>(base::AcquireRelease_CompareAndSwap(
               reinterpret_cast<base::Atomic32*>(
                   FIELD_ADDR(array, DescriptorArray::kRawGcStateOffset)),
               old_state, new_state)) == old_state;
  }
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DESCRIPTOR_ARRAY_H_
