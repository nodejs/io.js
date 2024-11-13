// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_OBJECTS_INL_H_
#define V8_OBJECTS_JS_OBJECTS_INL_H_

#include <optional>

#include "src/common/globals.h"
#include "src/heap/heap-write-barrier.h"
#include "src/objects/dictionary.h"
#include "src/objects/elements.h"
#include "src/objects/embedder-data-slot-inl.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/field-index-inl.h"
#include "src/objects/fixed-array.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/keys.h"
#include "src/objects/lookup-inl.h"
#include "src/objects/primitive-heap-object.h"
#include "src/objects/property-array-inl.h"
#include "src/objects/prototype-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/slots.h"
#include "src/objects/smi-inl.h"
#include "src/objects/string.h"
#include "src/objects/swiss-name-dictionary-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

#include "torque-generated/src/objects/js-objects-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSReceiver)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSObjectWithEmbedderSlots)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSAPIObjectWithEmbedderSlots)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSCustomElementsObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSSpecialObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSAsyncFromSyncIterator)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSDate)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSGlobalObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSGlobalProxy)
JSIteratorResult::JSIteratorResult(Address ptr) : JSObject(ptr) {}
TQ_OBJECT_CONSTRUCTORS_IMPL(JSMessageObject)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSPrimitiveWrapper)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSStringIterator)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSValidIteratorWrapper)

NEVER_READ_ONLY_SPACE_IMPL(JSReceiver)

DEF_GETTER(JSObject, elements, Tagged<FixedArrayBase>) {
  return TaggedField<FixedArrayBase, kElementsOffset>::load(cage_base, *this);
}

Tagged<FixedArrayBase> JSObject::elements(RelaxedLoadTag tag) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return elements(cage_base, tag);
}

Tagged<FixedArrayBase> JSObject::elements(PtrComprCageBase cage_base,
                                          RelaxedLoadTag) const {
  return TaggedField<FixedArrayBase, kElementsOffset>::Relaxed_Load(cage_base,
                                                                    *this);
}

void JSObject::set_elements(Tagged<FixedArrayBase> value,
                            WriteBarrierMode mode) {
  // Note the relaxed atomic store.
  TaggedField<FixedArrayBase, kElementsOffset>::Relaxed_Store(*this, value);
  CONDITIONAL_WRITE_BARRIER(*this, kElementsOffset, value, mode);
}

MaybeHandle<Object> JSReceiver::GetProperty(Isolate* isolate,
                                            Handle<JSReceiver> receiver,
                                            Handle<Name> name) {
  LookupIterator it(isolate, receiver, name, receiver);
  if (!it.IsFound()) return it.factory()->undefined_value();
  return Object::GetProperty(&it);
}

MaybeHandle<Object> JSReceiver::GetElement(Isolate* isolate,
                                           Handle<JSReceiver> receiver,
                                           uint32_t index) {
  LookupIterator it(isolate, receiver, index, receiver);
  if (!it.IsFound()) return it.factory()->undefined_value();
  return Object::GetProperty(&it);
}

Handle<Object> JSReceiver::GetDataProperty(Isolate* isolate,
                                           Handle<JSReceiver> object,
                                           Handle<Name> name) {
  LookupIterator it(isolate, object, name, object,
                    LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);
  if (!it.IsFound()) return it.factory()->undefined_value();
  return GetDataProperty(&it);
}

MaybeHandle<HeapObject> JSReceiver::GetPrototype(Isolate* isolate,
                                                 Handle<JSReceiver> receiver) {
  // We don't expect access checks to be needed on JSProxy objects.
  DCHECK(!IsAccessCheckNeeded(*receiver) || IsJSObject(*receiver));

  PrototypeIterator iter(isolate, receiver, kStartAtReceiver,
                         PrototypeIterator::END_AT_NON_HIDDEN);
  do {
    if (!iter.AdvanceFollowingProxies()) return MaybeHandle<HeapObject>();
  } while (!iter.IsAtEnd());
  return PrototypeIterator::GetCurrent(iter);
}

MaybeHandle<Object> JSReceiver::GetProperty(Isolate* isolate,
                                            Handle<JSReceiver> receiver,
                                            const char* name) {
  Handle<String> str = isolate->factory()->InternalizeUtf8String(name);
  return GetProperty(isolate, receiver, str);
}

// static
V8_WARN_UNUSED_RESULT MaybeHandle<FixedArray> JSReceiver::OwnPropertyKeys(
    Isolate* isolate, Handle<JSReceiver> object) {
  return KeyAccumulator::GetKeys(isolate, object, KeyCollectionMode::kOwnOnly,
                                 ALL_PROPERTIES,
                                 GetKeysConversion::kConvertToString);
}

bool JSObject::PrototypeHasNoElements(Isolate* isolate,
                                      Tagged<JSObject> object) {
  DisallowGarbageCollection no_gc;
  Tagged<HeapObject> prototype = Cast<HeapObject>(object->map()->prototype());
  ReadOnlyRoots roots(isolate);
  Tagged<HeapObject> null = roots.null_value();
  Tagged<FixedArrayBase> empty_fixed_array = roots.empty_fixed_array();
  Tagged<FixedArrayBase> empty_slow_element_dictionary =
      roots.empty_slow_element_dictionary();
  while (prototype != null) {
    Tagged<Map> map = prototype->map();
    if (IsCustomElementsReceiverMap(map)) return false;
    Tagged<FixedArrayBase> elements = Cast<JSObject>(prototype)->elements();
    if (elements != empty_fixed_array &&
        elements != empty_slow_element_dictionary) {
      return false;
    }
    prototype = Cast<HeapObject>(map->prototype());
  }
  return true;
}

ACCESSORS(JSReceiver, raw_properties_or_hash, Tagged<Object>,
          kPropertiesOrHashOffset)
RELAXED_ACCESSORS(JSReceiver, raw_properties_or_hash, Tagged<Object>,
                  kPropertiesOrHashOffset)

void JSObject::EnsureCanContainHeapObjectElements(Handle<JSObject> object) {
  JSObject::ValidateElements(*object);
  ElementsKind elements_kind = object->map()->elements_kind();
  if (!IsObjectElementsKind(elements_kind)) {
    if (IsHoleyElementsKind(elements_kind)) {
      TransitionElementsKind(object, HOLEY_ELEMENTS);
    } else {
      TransitionElementsKind(object, PACKED_ELEMENTS);
    }
  }
}

template <typename TSlot>
void JSObject::EnsureCanContainElements(Handle<JSObject> object, TSlot objects,
                                        uint32_t count,
                                        EnsureElementsMode mode) {
  static_assert(std::is_same<TSlot, FullObjectSlot>::value ||
                    std::is_same<TSlot, ObjectSlot>::value,
                "Only ObjectSlot and FullObjectSlot are expected here");
  ElementsKind current_kind = object->GetElementsKind();
  ElementsKind target_kind = current_kind;
  {
    DisallowGarbageCollection no_gc;
    DCHECK(mode != ALLOW_COPIED_DOUBLE_ELEMENTS);
    bool is_holey = IsHoleyElementsKind(current_kind);
    if (current_kind == HOLEY_ELEMENTS) return;
    Tagged<Object> the_hole = object->GetReadOnlyRoots().the_hole_value();
    for (uint32_t i = 0; i < count; ++i, ++objects) {
      Tagged<Object> current = *objects;
      if (current == the_hole) {
        is_holey = true;
        target_kind = GetHoleyElementsKind(target_kind);
      } else if (!IsSmi(current)) {
        if (mode == ALLOW_CONVERTED_DOUBLE_ELEMENTS && IsNumber(current)) {
          if (IsSmiElementsKind(target_kind)) {
            if (is_holey) {
              target_kind = HOLEY_DOUBLE_ELEMENTS;
            } else {
              target_kind = PACKED_DOUBLE_ELEMENTS;
            }
          }
        } else if (is_holey) {
          target_kind = HOLEY_ELEMENTS;
          break;
        } else {
          target_kind = PACKED_ELEMENTS;
        }
      }
    }
  }
  if (target_kind != current_kind) {
    TransitionElementsKind(object, target_kind);
  }
}

void JSObject::EnsureCanContainElements(Handle<JSObject> object,
                                        Handle<FixedArrayBase> elements,
                                        uint32_t length,
                                        EnsureElementsMode mode) {
  ReadOnlyRoots roots = object->GetReadOnlyRoots();
  if (elements->map() != roots.fixed_double_array_map()) {
    DCHECK(elements->map() == roots.fixed_array_map() ||
           elements->map() == roots.fixed_cow_array_map());
    if (mode == ALLOW_COPIED_DOUBLE_ELEMENTS) {
      mode = DONT_ALLOW_DOUBLE_ELEMENTS;
    }
    ObjectSlot objects = Cast<FixedArray>(elements)->RawFieldOfFirstElement();
    EnsureCanContainElements(object, objects, length, mode);
    return;
  }

  DCHECK(mode == ALLOW_COPIED_DOUBLE_ELEMENTS);
  if (object->GetElementsKind() == HOLEY_SMI_ELEMENTS) {
    TransitionElementsKind(object, HOLEY_DOUBLE_ELEMENTS);
  } else if (object->GetElementsKind() == PACKED_SMI_ELEMENTS) {
    auto double_array = Cast<FixedDoubleArray>(elements);
    for (uint32_t i = 0; i < length; ++i) {
      if (double_array->is_the_hole(i)) {
        TransitionElementsKind(object, HOLEY_DOUBLE_ELEMENTS);
        return;
      }
    }
    TransitionElementsKind(object, PACKED_DOUBLE_ELEMENTS);
  }
}

void JSObject::SetMapAndElements(DirectHandle<JSObject> object,
                                 DirectHandle<Map> new_map,
                                 DirectHandle<FixedArrayBase> value) {
  Isolate* isolate = object->GetIsolate();
  JSObject::MigrateToMap(isolate, object, new_map);
  DCHECK((object->map()->has_fast_smi_or_object_elements() ||
          (*value == ReadOnlyRoots(isolate).empty_fixed_array()) ||
          object->map()->has_fast_string_wrapper_elements()) ==
         (value->map() == ReadOnlyRoots(isolate).fixed_array_map() ||
          value->map() == ReadOnlyRoots(isolate).fixed_cow_array_map()));
  DCHECK((*value == ReadOnlyRoots(isolate).empty_fixed_array()) ||
         (object->map()->has_fast_double_elements() ==
          IsFixedDoubleArray(*value)));
  object->set_elements(*value);
}

void JSObject::initialize_elements() {
  Tagged<FixedArrayBase> elements = map()->GetInitialElements();
  set_elements(elements, SKIP_WRITE_BARRIER);
}

DEF_GETTER(JSObject, GetIndexedInterceptor, Tagged<InterceptorInfo>) {
  return map(cage_base)->GetIndexedInterceptor(cage_base);
}

DEF_GETTER(JSObject, GetNamedInterceptor, Tagged<InterceptorInfo>) {
  return map(cage_base)->GetNamedInterceptor(cage_base);
}

// static
int JSObject::GetHeaderSize(Tagged<Map> map) {
  // Check for the most common kind of JavaScript object before
  // falling into the generic switch. This speeds up the internal
  // field operations considerably on average.
  InstanceType instance_type = map->instance_type();
  return instance_type == JS_OBJECT_TYPE
             ? JSObject::kHeaderSize
             : GetHeaderSize(instance_type, map->has_prototype_slot());
}

// static
int JSObject::GetEmbedderFieldsStartOffset(Tagged<Map> map) {
  // Embedder fields are located after the object header.
  return GetHeaderSize(map);
}

int JSObject::GetEmbedderFieldsStartOffset() {
  return GetEmbedderFieldsStartOffset(map());
}

// static
bool JSObject::MayHaveEmbedderFields(Tagged<Map> map) {
  InstanceType instance_type = map->instance_type();
  // TODO(v8) It'd be nice if all objects with embedder data slots inherited
  // from JSObjectJSAPIObjectWithEmbedderSlotsWithEmbedderSlots, but this is
  // currently not possible due to instance_type constraints.
  return InstanceTypeChecker::IsJSObjectWithEmbedderSlots(instance_type) ||
         InstanceTypeChecker::IsJSAPIObjectWithEmbedderSlots(instance_type) ||
         InstanceTypeChecker::IsJSSpecialObject(instance_type);
}

bool JSObject::MayHaveEmbedderFields() const {
  return MayHaveEmbedderFields(map());
}

// static
int JSObject::GetEmbedderFieldCount(Tagged<Map> map) {
  int instance_size = map->instance_size();
  if (instance_size == kVariableSizeSentinel) return 0;
  // Embedder fields are located after the object header, whereas in-object
  // properties are located at the end of the object. We don't have to round up
  // the header size here because division by kEmbedderDataSlotSizeInTaggedSlots
  // will swallow potential padding in case of (kTaggedSize !=
  // kSystemPointerSize) anyway.
  return (((instance_size - GetEmbedderFieldsStartOffset(map)) >>
           kTaggedSizeLog2) -
          map->GetInObjectProperties()) /
         kEmbedderDataSlotSizeInTaggedSlots;
}

int JSObject::GetEmbedderFieldCount() const {
  return GetEmbedderFieldCount(map());
}

int JSObject::GetEmbedderFieldOffset(int index) {
  DCHECK_LT(static_cast<unsigned>(index),
            static_cast<unsigned>(GetEmbedderFieldCount()));
  return GetEmbedderFieldsStartOffset() + (kEmbedderDataSlotSize * index);
}

Tagged<Object> JSObject::GetEmbedderField(int index) {
  return EmbedderDataSlot(Tagged(*this), index).load_tagged();
}

void JSObject::SetEmbedderField(int index, Tagged<Object> value) {
  EmbedderDataSlot::store_tagged(Tagged(*this), index, value);
}

void JSObject::SetEmbedderField(int index, Tagged<Smi> value) {
  EmbedderDataSlot(Tagged(*this), index).store_smi(value);
}

// static
bool JSObject::IsDroppableApiObject(const Tagged<Map> map) {
  auto instance_type = map->instance_type();
  return InstanceTypeChecker::IsJSApiObject(instance_type) ||
         instance_type == JS_SPECIAL_API_OBJECT_TYPE;
}

bool JSObject::IsDroppableApiObject() const {
  return IsDroppableApiObject(map());
}

// Access fast-case object properties at index. The use of these routines
// is needed to correctly distinguish between properties stored in-object and
// properties stored in the properties array.
Tagged<JSAny> JSObject::RawFastPropertyAt(FieldIndex index) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return RawFastPropertyAt(cage_base, index);
}

Tagged<JSAny> JSObject::RawFastPropertyAt(PtrComprCageBase cage_base,
                                          FieldIndex index) const {
  if (index.is_inobject()) {
    return TaggedField<JSAny>::Relaxed_Load(cage_base, *this, index.offset());
  } else {
    return property_array(cage_base)->get(cage_base,
                                          index.outobject_array_index());
  }
}

// The SeqCst versions of RawFastPropertyAt are used for atomically accessing
// shared struct fields.
Tagged<JSAny> JSObject::RawFastPropertyAt(FieldIndex index,
                                          SeqCstAccessTag tag) const {
  PtrComprCageBase cage_base = GetPtrComprCageBase(*this);
  return RawFastPropertyAt(cage_base, index, tag);
}

Tagged<JSAny> JSObject::RawFastPropertyAt(PtrComprCageBase cage_base,
                                          FieldIndex index,
                                          SeqCstAccessTag tag) const {
  if (index.is_inobject()) {
    return TaggedField<JSAny>::SeqCst_Load(cage_base, *this, index.offset());
  } else {
    return property_array(cage_base)->get(cage_base,
                                          index.outobject_array_index(), tag);
  }
}

std::optional<Tagged<Object>> JSObject::RawInobjectPropertyAt(
    PtrComprCageBase cage_base, Tagged<Map> original_map,
    FieldIndex index) const {
  CHECK(index.is_inobject());

  // This method implements a "snapshot" protocol to protect against reading out
  // of bounds of an object. It's used to access a fast in-object property from
  // a background thread with no locking. That caller does have the guarantee
  // that a garbage collection cannot happen during its query. However, it must
  // contend with the main thread altering the object in heavy ways through
  // object migration. Specifically, the object can get smaller. Initially, this
  // may seem benign, because object migration fills the freed-up space with
  // FillerMap words which, even though they offer wrong values, are at
  // least tagged values.

  // However, there is an additional danger. Sweeper threads may discover the
  // filler words and offer that space to the main thread for allocation. Should
  // a HeapNumber be allocated into that space while we're reading a property at
  // that location (from our out-of-date information), we risk interpreting a
  // double value as a pointer. This must be prevented.
  //
  // We do this by:
  //
  // a) Reading the map first
  // b) Reading the property with acquire semantics (but do not inspect it!)
  // c) Re-read the map with acquire semantics.
  //
  // Only if the maps match can the property be inspected. It may have a "wrong"
  // value, but it will be within the bounds of the objects instance size as
  // given by the map and it will be a valid Smi or object pointer.
  Tagged<Object> maybe_tagged_object =
      TaggedField<Object>::Acquire_Load(cage_base, *this, index.offset());
  if (original_map != map(cage_base, kAcquireLoad)) return {};
  return maybe_tagged_object;
}

void JSObject::RawFastInobjectPropertyAtPut(FieldIndex index,
                                            Tagged<Object> value,
                                            WriteBarrierMode mode) {
  DCHECK(index.is_inobject());
  int offset = index.offset();
  RELAXED_WRITE_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);
}

void JSObject::RawFastInobjectPropertyAtPut(FieldIndex index,
                                            Tagged<Object> value,
                                            SeqCstAccessTag tag) {
  DCHECK(index.is_inobject());
  DCHECK(IsShared(value));
  SEQ_CST_WRITE_FIELD(*this, index.offset(), value);
  CONDITIONAL_WRITE_BARRIER(*this, index.offset(), value, UPDATE_WRITE_BARRIER);
}

void JSObject::FastPropertyAtPut(FieldIndex index, Tagged<Object> value,
                                 WriteBarrierMode mode) {
  if (index.is_inobject()) {
    RawFastInobjectPropertyAtPut(index, value, mode);
  } else {
    DCHECK_EQ(UPDATE_WRITE_BARRIER, mode);
    property_array()->set(index.outobject_array_index(), value);
  }
}

void JSObject::FastPropertyAtPut(FieldIndex index, Tagged<Object> value,
                                 SeqCstAccessTag tag) {
  if (index.is_inobject()) {
    RawFastInobjectPropertyAtPut(index, value, tag);
  } else {
    property_array()->set(index.outobject_array_index(), value, tag);
  }
}

void JSObject::WriteToField(InternalIndex descriptor, PropertyDetails details,
                            Tagged<Object> value) {
  DCHECK_EQ(PropertyLocation::kField, details.location());
  DCHECK_EQ(PropertyKind::kData, details.kind());
  DisallowGarbageCollection no_gc;
  FieldIndex index = FieldIndex::ForDetails(map(), details);
  if (details.representation().IsDouble()) {
    // Manipulating the signaling NaN used for the hole and uninitialized
    // double field sentinel in C++, e.g. with base::bit_cast or
    // value()/set_value(), will change its value on ia32 (the x87 stack is used
    // to return values and stores to the stack silently clear the signalling
    // bit).
    uint64_t bits;
    if (IsSmi(value)) {
      bits = base::bit_cast<uint64_t>(static_cast<double>(Smi::ToInt(value)));
    } else if (IsUninitialized(value)) {
      bits = kHoleNanInt64;
    } else {
      DCHECK(IsHeapNumber(value));
      bits = Cast<HeapNumber>(value)->value_as_bits();
    }
    auto box = Cast<HeapNumber>(RawFastPropertyAt(index));
    box->set_value_as_bits(bits);
  } else {
    FastPropertyAtPut(index, value);
  }
}

Tagged<Object> JSObject::RawFastInobjectPropertyAtSwap(FieldIndex index,
                                                       Tagged<Object> value,
                                                       SeqCstAccessTag tag) {
  DCHECK(index.is_inobject());
  DCHECK(IsShared(value));
  int offset = index.offset();
  Tagged<Object> old_value = SEQ_CST_SWAP_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, UPDATE_WRITE_BARRIER);
  return old_value;
}

Tagged<Object> JSObject::RawFastPropertyAtSwap(FieldIndex index,
                                               Tagged<Object> value,
                                               SeqCstAccessTag tag) {
  if (index.is_inobject()) {
    return RawFastInobjectPropertyAtSwap(index, value, tag);
  }
  return property_array()->Swap(index.outobject_array_index(), value, tag);
}

Tagged<Object> JSObject::RawFastInobjectPropertyAtCompareAndSwap(
    FieldIndex index, Tagged<Object> expected, Tagged<Object> value,
    SeqCstAccessTag tag) {
  DCHECK(index.is_inobject());
  DCHECK(IsShared(value));
  Tagged<Object> previous_value =
      SEQ_CST_COMPARE_AND_SWAP_FIELD(*this, index.offset(), expected, value);
  if (previous_value == expected) {
    CONDITIONAL_WRITE_BARRIER(*this, index.offset(), value,
                              UPDATE_WRITE_BARRIER);
  }
  return previous_value;
}

Tagged<Object> JSObject::RawFastPropertyAtCompareAndSwapInternal(
    FieldIndex index, Tagged<Object> expected, Tagged<Object> value,
    SeqCstAccessTag tag) {
  if (index.is_inobject()) {
    return RawFastInobjectPropertyAtCompareAndSwap(index, expected, value, tag);
  }
  return property_array()->CompareAndSwap(index.outobject_array_index(),
                                          expected, value, tag);
}

int JSObject::GetInObjectPropertyOffset(int index) {
  return map()->GetInObjectPropertyOffset(index);
}

Tagged<Object> JSObject::InObjectPropertyAt(int index) {
  int offset = GetInObjectPropertyOffset(index);
  return TaggedField<Object>::load(*this, offset);
}

Tagged<Object> JSObject::InObjectPropertyAtPut(int index, Tagged<Object> value,
                                               WriteBarrierMode mode) {
  // Adjust for the number of properties stored in the object.
  int offset = GetInObjectPropertyOffset(index);
  WRITE_FIELD(*this, offset, value);
  CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);
  return value;
}

void JSObject::InitializeBody(Tagged<Map> map, int start_offset,
                              bool is_slack_tracking_in_progress,
                              MapWord filler_map,
                              Tagged<Object> undefined_filler) {
  int size = map->instance_size();
  int offset = start_offset;

  // embedder data slots need to be initialized separately
  if (MayHaveEmbedderFields(map)) {
    int embedder_field_start = GetEmbedderFieldsStartOffset(map);
    int embedder_field_count = GetEmbedderFieldCount(map);

    // fill start with references to the undefined value object
    DCHECK_LE(offset, embedder_field_start);
    while (offset < embedder_field_start) {
      WRITE_FIELD(*this, offset, undefined_filler);
      offset += kTaggedSize;
    }

    // initialize embedder data slots
    DCHECK_EQ(offset, embedder_field_start);
    for (int i = 0; i < embedder_field_count; i++) {
      // TODO(v8): consider initializing embedded data slots with Smi::zero().
      EmbedderDataSlot(Tagged<JSObject>(*this), i).Initialize(undefined_filler);
      offset += kEmbedderDataSlotSize;
    }
  } else {
    DCHECK_EQ(0, GetEmbedderFieldCount(map));
  }

  DCHECK_LE(offset, size);
  if (is_slack_tracking_in_progress) {
    int end_of_pre_allocated_offset =
        size - (map->UnusedPropertyFields() * kTaggedSize);
    DCHECK_LE(kHeaderSize, end_of_pre_allocated_offset);
    DCHECK_LE(offset, end_of_pre_allocated_offset);
    // fill pre allocated slots with references to the undefined value object
    while (offset < end_of_pre_allocated_offset) {
      WRITE_FIELD(*this, offset, undefined_filler);
      offset += kTaggedSize;
    }
    // fill the remainder with one word filler objects (ie just a map word)
    while (offset < size) {
      Tagged<Object> fm = Tagged<Object>(filler_map.ptr());
      WRITE_FIELD(*this, offset, fm);
      offset += kTaggedSize;
    }
  } else {
    while (offset < size) {
      // fill everything with references to the undefined value object
      WRITE_FIELD(*this, offset, undefined_filler);
      offset += kTaggedSize;
    }
  }
}

TQ_OBJECT_CONSTRUCTORS_IMPL(JSExternalObject)

EXTERNAL_POINTER_ACCESSORS(JSExternalObject, value, void*, kValueOffset,
                           kExternalObjectValueTag)

JSApiWrapper::JSApiWrapper(Tagged<JSObject> object) : object_(object) {
  DCHECK(IsJSApiWrapperObject(object));
}

template <CppHeapPointerTag lower_bound, CppHeapPointerTag upper_bound>
void* JSApiWrapper::GetCppHeapWrappable(
    IsolateForPointerCompression isolate) const {
  return reinterpret_cast<void*>(
      object_->ReadCppHeapPointerField<lower_bound, upper_bound>(
          kCppHeapWrappableOffset, isolate));
}

void* JSApiWrapper::GetCppHeapWrappable(
    IsolateForPointerCompression isolate,
    CppHeapPointerTagRange tag_range) const {
  return reinterpret_cast<void*>(object_->ReadCppHeapPointerField(
      kCppHeapWrappableOffset, isolate, tag_range));
}

template <CppHeapPointerTag tag>
void JSApiWrapper::SetCppHeapWrappable(IsolateForPointerCompression isolate,
                                       void* instance) {
  object_->WriteLazilyInitializedCppHeapPointerField<tag>(
      JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset, isolate,
      reinterpret_cast<Address>(instance));
  if (instance) {
    WriteBarrier::CombinedBarrierForCppHeapPointer(object_, instance);
  }
}

void JSApiWrapper::SetCppHeapWrappable(IsolateForPointerCompression isolate,
                                       void* instance, CppHeapPointerTag tag) {
  object_->WriteLazilyInitializedCppHeapPointerField(
      JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset, isolate,
      reinterpret_cast<Address>(instance), tag);
  if (instance) {
    WriteBarrier::CombinedBarrierForCppHeapPointer(object_, instance);
  }
}

bool JSMessageObject::DidEnsureSourcePositionsAvailable() const {
  return shared_info() == Smi::zero();
}

// static
void JSMessageObject::EnsureSourcePositionsAvailable(
    Isolate* isolate, DirectHandle<JSMessageObject> message) {
  if (message->DidEnsureSourcePositionsAvailable()) {
    DCHECK(message->script()->has_line_ends());
  } else {
    JSMessageObject::InitializeSourcePositions(isolate, message);
  }
}

int JSMessageObject::GetStartPosition() const {
  // TODO(cbruni): make this DCHECK stricter (>= 0).
  DCHECK_LE(-1, start_position());
  return start_position();
}

int JSMessageObject::GetEndPosition() const {
  // TODO(cbruni): make this DCHECK stricter (>= 0).
  DCHECK_LE(-1, end_position());
  return end_position();
}

MessageTemplate JSMessageObject::type() const {
  return MessageTemplateFromInt(raw_type());
}

void JSMessageObject::set_type(MessageTemplate value) {
  set_raw_type(static_cast<int>(value));
}

ACCESSORS(JSMessageObject, shared_info, Tagged<Object>, kSharedInfoOffset)
ACCESSORS(JSMessageObject, bytecode_offset, Tagged<Smi>, kBytecodeOffsetOffset)
SMI_ACCESSORS(JSMessageObject, start_position, kStartPositionOffset)
SMI_ACCESSORS(JSMessageObject, end_position, kEndPositionOffset)
SMI_ACCESSORS(JSMessageObject, error_level, kErrorLevelOffset)
SMI_ACCESSORS(JSMessageObject, raw_type, kMessageTypeOffset)

DEF_GETTER(JSObject, GetElementsKind, ElementsKind) {
  ElementsKind kind = map(cage_base)->elements_kind();
#if VERIFY_HEAP && DEBUG
  Tagged<FixedArrayBase> fixed_array = UncheckedCast<FixedArrayBase>(
      TaggedField<HeapObject, kElementsOffset>::load(cage_base, *this));

  // If a GC was caused while constructing this object, the elements
  // pointer may point to a one pointer filler map.
  if (ElementsAreSafeToExamine(cage_base)) {
    Tagged<Map> map = fixed_array->map(cage_base);
    if (IsSmiOrObjectElementsKind(kind)) {
      DCHECK(map == GetReadOnlyRoots(cage_base).fixed_array_map() ||
             map == GetReadOnlyRoots(cage_base).fixed_cow_array_map());
    } else if (IsDoubleElementsKind(kind)) {
      DCHECK(IsFixedDoubleArray(fixed_array, cage_base) ||
             fixed_array == GetReadOnlyRoots(cage_base).empty_fixed_array());
    } else if (kind == DICTIONARY_ELEMENTS) {
      DCHECK(IsFixedArray(fixed_array, cage_base));
      DCHECK(IsNumberDictionary(fixed_array, cage_base));
    } else {
      DCHECK(kind > DICTIONARY_ELEMENTS ||
             IsAnyNonextensibleElementsKind(kind));
    }
    DCHECK(!IsSloppyArgumentsElementsKind(kind) ||
           IsSloppyArgumentsElements(elements(cage_base)));
  }
#endif
  return kind;
}

DEF_GETTER(JSObject, GetElementsAccessor, ElementsAccessor*) {
  return ElementsAccessor::ForKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasObjectElements, bool) {
  return IsObjectElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasSmiElements, bool) {
  return IsSmiElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasSmiOrObjectElements, bool) {
  return IsSmiOrObjectElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasDoubleElements, bool) {
  return IsDoubleElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasHoleyElements, bool) {
  return IsHoleyElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasFastElements, bool) {
  return IsFastElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasFastPackedElements, bool) {
  return IsFastPackedElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasDictionaryElements, bool) {
  return IsDictionaryElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasPackedElements, bool) {
  return GetElementsKind(cage_base) == PACKED_ELEMENTS;
}

DEF_GETTER(JSObject, HasAnyNonextensibleElements, bool) {
  return IsAnyNonextensibleElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasSealedElements, bool) {
  return IsSealedElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasSharedArrayElements, bool) {
  return GetElementsKind(cage_base) == SHARED_ARRAY_ELEMENTS;
}

DEF_GETTER(JSObject, HasNonextensibleElements, bool) {
  return IsNonextensibleElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasFastArgumentsElements, bool) {
  return IsFastArgumentsElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasSlowArgumentsElements, bool) {
  return IsSlowArgumentsElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasSloppyArgumentsElements, bool) {
  return IsSloppyArgumentsElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasStringWrapperElements, bool) {
  return IsStringWrapperElementsKind(GetElementsKind(cage_base));
}

DEF_GETTER(JSObject, HasFastStringWrapperElements, bool) {
  return GetElementsKind(cage_base) == FAST_STRING_WRAPPER_ELEMENTS;
}

DEF_GETTER(JSObject, HasSlowStringWrapperElements, bool) {
  return GetElementsKind(cage_base) == SLOW_STRING_WRAPPER_ELEMENTS;
}

DEF_GETTER(JSObject, HasTypedArrayOrRabGsabTypedArrayElements, bool) {
  DCHECK(!elements(cage_base).is_null());
  return map(cage_base)->has_typed_array_or_rab_gsab_typed_array_elements();
}

#define FIXED_TYPED_ELEMENTS_CHECK(Type, type, TYPE, ctype)    \
  DEF_GETTER(JSObject, HasFixed##Type##Elements, bool) {       \
    return map(cage_base)->elements_kind() == TYPE##_ELEMENTS; \
  }

TYPED_ARRAYS(FIXED_TYPED_ELEMENTS_CHECK)

#undef FIXED_TYPED_ELEMENTS_CHECK

DEF_GETTER(JSObject, HasNamedInterceptor, bool) {
  return map(cage_base)->has_named_interceptor();
}

DEF_GETTER(JSObject, HasIndexedInterceptor, bool) {
  return map(cage_base)->has_indexed_interceptor();
}

RELEASE_ACQUIRE_ACCESSORS_CHECKED2(JSGlobalObject, global_dictionary,
                                   Tagged<GlobalDictionary>,
                                   kPropertiesOrHashOffset,
                                   !HasFastProperties(cage_base), true)

DEF_GETTER(JSObject, element_dictionary, Tagged<NumberDictionary>) {
  DCHECK(HasDictionaryElements(cage_base) ||
         HasSlowStringWrapperElements(cage_base));
  return Cast<NumberDictionary>(elements(cage_base));
}

void JSReceiver::initialize_properties(Isolate* isolate) {
  ReadOnlyRoots roots(isolate);
  DCHECK(!ObjectInYoungGeneration(roots.empty_fixed_array()));
  DCHECK(!ObjectInYoungGeneration(roots.empty_property_dictionary()));
  DCHECK(!ObjectInYoungGeneration(roots.empty_ordered_property_dictionary()));
  if (map(isolate)->is_dictionary_map()) {
    if (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
      WRITE_FIELD(*this, kPropertiesOrHashOffset,
                  roots.empty_swiss_property_dictionary());
    } else {
      WRITE_FIELD(*this, kPropertiesOrHashOffset,
                  roots.empty_property_dictionary());
    }
  } else {
    WRITE_FIELD(*this, kPropertiesOrHashOffset, roots.empty_fixed_array());
  }
}

DEF_GETTER(JSReceiver, HasFastProperties, bool) {
  Tagged<Object> raw_properties_or_hash_obj =
      raw_properties_or_hash(cage_base, kRelaxedLoad);
  DCHECK(IsSmi(raw_properties_or_hash_obj) ||
         ((IsGlobalDictionary(raw_properties_or_hash_obj, cage_base) ||
           IsPropertyDictionary(raw_properties_or_hash_obj, cage_base)) ==
          map(cage_base)->is_dictionary_map()));
  USE(raw_properties_or_hash_obj);
  return !map(cage_base)->is_dictionary_map();
}

DEF_GETTER(JSReceiver, property_dictionary, Tagged<NameDictionary>) {
  DCHECK(!IsJSGlobalObject(*this, cage_base));
  DCHECK(!HasFastProperties(cage_base));
  DCHECK(!V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL);

  Tagged<Object> prop = raw_properties_or_hash(cage_base);
  if (IsSmi(prop)) {
    return GetReadOnlyRoots(cage_base).empty_property_dictionary();
  }
  return Cast<NameDictionary>(prop);
}

DEF_GETTER(JSReceiver, property_dictionary_swiss, Tagged<SwissNameDictionary>) {
  DCHECK(!IsJSGlobalObject(*this, cage_base));
  DCHECK(!HasFastProperties(cage_base));
  DCHECK(V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL);

  Tagged<Object> prop = raw_properties_or_hash(cage_base);
  if (IsSmi(prop)) {
    return GetReadOnlyRoots(cage_base).empty_swiss_property_dictionary();
  }
  return Cast<SwissNameDictionary>(prop);
}

// TODO(gsathya): Pass isolate directly to this function and access
// the heap from this.
DEF_GETTER(JSReceiver, property_array, Tagged<PropertyArray>) {
  DCHECK(HasFastProperties(cage_base));
  Tagged<Object> prop = raw_properties_or_hash(cage_base);
  if (IsSmi(prop) || prop == GetReadOnlyRoots(cage_base).empty_fixed_array()) {
    return GetReadOnlyRoots(cage_base).empty_property_array();
  }
  return Cast<PropertyArray>(prop);
}

std::optional<Tagged<NativeContext>> JSReceiver::GetCreationContext() {
  DisallowGarbageCollection no_gc;
  Tagged<Map> meta_map = map()->map();
  DCHECK(IsMapMap(meta_map));
  Tagged<Object> maybe_native_context = meta_map->native_context_or_null();
  if (IsNull(maybe_native_context)) return {};
  DCHECK(IsNativeContext(maybe_native_context));
  return Cast<NativeContext>(maybe_native_context);
}

MaybeHandle<NativeContext> JSReceiver::GetCreationContext(Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  std::optional<Tagged<NativeContext>> maybe_context = GetCreationContext();
  if (!maybe_context.has_value()) return {};
  return handle(maybe_context.value(), isolate);
}

Maybe<bool> JSReceiver::HasProperty(Isolate* isolate, Handle<JSReceiver> object,
                                    Handle<Name> name) {
  PropertyKey key(isolate, name);
  LookupIterator it(isolate, object, key, object);
  return HasProperty(&it);
}

Maybe<bool> JSReceiver::HasOwnProperty(Isolate* isolate,
                                       Handle<JSReceiver> object,
                                       uint32_t index) {
  if (IsJSObject(*object)) {  // Shortcut.
    LookupIterator it(isolate, object, index, object, LookupIterator::OWN);
    return HasProperty(&it);
  }

  Maybe<PropertyAttributes> attributes =
      JSReceiver::GetOwnPropertyAttributes(object, index);
  MAYBE_RETURN(attributes, Nothing<bool>());
  return Just(attributes.FromJust() != ABSENT);
}

Maybe<PropertyAttributes> JSReceiver::GetPropertyAttributes(
    Handle<JSReceiver> object, Handle<Name> name) {
  Isolate* isolate = object->GetIsolate();
  PropertyKey key(isolate, name);
  LookupIterator it(isolate, object, key, object);
  return GetPropertyAttributes(&it);
}

Maybe<PropertyAttributes> JSReceiver::GetOwnPropertyAttributes(
    Handle<JSReceiver> object, Handle<Name> name) {
  Isolate* isolate = object->GetIsolate();
  PropertyKey key(isolate, name);
  LookupIterator it(isolate, object, key, object, LookupIterator::OWN);
  return GetPropertyAttributes(&it);
}

Maybe<PropertyAttributes> JSReceiver::GetOwnPropertyAttributes(
    Handle<JSReceiver> object, uint32_t index) {
  LookupIterator it(object->GetIsolate(), object, index, object,
                    LookupIterator::OWN);
  return GetPropertyAttributes(&it);
}

Maybe<bool> JSReceiver::HasElement(Isolate* isolate, Handle<JSReceiver> object,
                                   uint32_t index) {
  LookupIterator it(isolate, object, index, object);
  return HasProperty(&it);
}

Maybe<PropertyAttributes> JSReceiver::GetElementAttributes(
    Handle<JSReceiver> object, uint32_t index) {
  Isolate* isolate = object->GetIsolate();
  LookupIterator it(isolate, object, index, object);
  return GetPropertyAttributes(&it);
}

Maybe<PropertyAttributes> JSReceiver::GetOwnElementAttributes(
    Handle<JSReceiver> object, uint32_t index) {
  Isolate* isolate = object->GetIsolate();
  LookupIterator it(isolate, object, index, object, LookupIterator::OWN);
  return GetPropertyAttributes(&it);
}

Tagged<NativeContext> JSGlobalObject::native_context() {
  return *GetCreationContext();
}

bool JSGlobalObject::IsDetached() {
  return global_proxy()->IsDetachedFrom(*this);
}

bool JSGlobalProxy::IsDetachedFrom(Tagged<JSGlobalObject> global) const {
  const PrototypeIterator iter(this->GetIsolate(), Tagged<JSReceiver>(*this));
  return iter.GetCurrent() != global;
}

inline int JSGlobalProxy::SizeWithEmbedderFields(int embedder_field_count) {
  DCHECK_GE(embedder_field_count, 0);
  return kHeaderSize + embedder_field_count * kEmbedderDataSlotSize;
}

ACCESSORS(JSIteratorResult, value, Tagged<Object>, kValueOffset)
ACCESSORS(JSIteratorResult, done, Tagged<Object>, kDoneOffset)

// If the fast-case backing storage takes up much more memory than a dictionary
// backing storage would, the object should have slow elements.
// static
static inline bool ShouldConvertToSlowElements(uint32_t used_elements,
                                               uint32_t new_capacity) {
  uint32_t size_threshold = NumberDictionary::kPreferFastElementsSizeFactor *
                            NumberDictionary::ComputeCapacity(used_elements) *
                            NumberDictionary::kEntrySize;
  return size_threshold <= new_capacity;
}

static inline bool ShouldConvertToSlowElements(Tagged<JSObject> object,
                                               uint32_t capacity,
                                               uint32_t index,
                                               uint32_t* new_capacity) {
  static_assert(JSObject::kMaxUncheckedOldFastElementsLength <=
                JSObject::kMaxUncheckedFastElementsLength);
  if (index < capacity) {
    *new_capacity = capacity;
    return false;
  }
  if (index - capacity >= JSObject::kMaxGap) return true;
  *new_capacity = JSObject::NewElementsCapacity(index + 1);
  DCHECK_LT(index, *new_capacity);
  if (*new_capacity <= JSObject::kMaxUncheckedOldFastElementsLength ||
      (*new_capacity <= JSObject::kMaxUncheckedFastElementsLength &&
       ObjectInYoungGeneration(object))) {
    return false;
  }
  return ShouldConvertToSlowElements(object->GetFastElementsUsage(),
                                     *new_capacity);
}

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_OBJECTS_INL_H_
