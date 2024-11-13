// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_INL_H_
#define V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_INL_H_

#include <algorithm>

#include "include/v8-internal.h"
#include "src/base/logging.h"
#include "src/codegen/reloc-info.h"
#include "src/common/globals.h"
#include "src/ic/handler-configuration.h"
#include "src/objects/arguments-inl.h"
#include "src/objects/bigint.h"
#include "src/objects/call-site-info-inl.h"
#include "src/objects/call-site-info.h"
#include "src/objects/cell.h"
#include "src/objects/data-handler.h"
#include "src/objects/embedder-data-array-inl.h"
#include "src/objects/fixed-array.h"
#include "src/objects/foreign-inl.h"
#include "src/objects/free-space-inl.h"
#include "src/objects/hash-table.h"
#include "src/objects/heap-number.h"
#include "src/objects/instance-type.h"
#include "src/objects/js-array-buffer.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "src/objects/js-collection.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-weak-refs.h"
#include "src/objects/literal-objects.h"
#include "src/objects/megadom-handler-inl.h"
#include "src/objects/objects-body-descriptors.h"
#include "src/objects/ordered-hash-table-inl.h"
#include "src/objects/property-descriptor-object.h"
#include "src/objects/source-text-module.h"
#include "src/objects/swiss-name-dictionary-inl.h"
#include "src/objects/synthetic-module.h"
#include "src/objects/template-objects-inl.h"
#include "src/objects/torque-defined-classes-inl.h"
#include "src/objects/transitions.h"
#include "src/objects/turbofan-types-inl.h"
#include "src/objects/turboshaft-types-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects-inl.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

template <int start_offset>
int FlexibleBodyDescriptor<start_offset>::SizeOf(Tagged<Map> map,
                                                 Tagged<HeapObject> object) {
  return object->SizeFromMap(map);
}

template <int start_offset>
int FlexibleWeakBodyDescriptor<start_offset>::SizeOf(
    Tagged<Map> map, Tagged<HeapObject> object) {
  return object->SizeFromMap(map);
}

template <typename ObjectVisitor>
void BodyDescriptorBase::IterateJSObjectBodyImpl(Tagged<Map> map,
                                                 Tagged<HeapObject> obj,
                                                 int start_offset,
                                                 int end_offset,
                                                 ObjectVisitor* v) {
#ifdef V8_COMPRESS_POINTERS
  static_assert(kEmbedderDataSlotSize == 2 * kTaggedSize);
  int header_end_offset = JSObject::GetHeaderSize(map);
  int inobject_fields_start_offset = map->GetInObjectPropertyOffset(0);
  // We are always requested to process header and embedder fields.
  DCHECK_LE(inobject_fields_start_offset, end_offset);
  // Embedder fields are located between header and inobject properties.
  if (header_end_offset < inobject_fields_start_offset) {
    // There are embedder fields.
    DCHECK_EQ(header_end_offset, JSObject::GetEmbedderFieldsStartOffset(map));
    IteratePointers(obj, start_offset, header_end_offset, v);
    for (int offset = header_end_offset; offset < inobject_fields_start_offset;
         offset += kEmbedderDataSlotSize) {
      IteratePointer(obj, offset + EmbedderDataSlot::kTaggedPayloadOffset, v);
      v->VisitExternalPointer(
          obj, obj->RawExternalPointerField(
                   offset + EmbedderDataSlot::kExternalPointerOffset,
                   kEmbedderDataSlotPayloadTag));
    }
    // Proceed processing inobject properties.
    start_offset = inobject_fields_start_offset;
  }
#else
  // We store raw aligned pointers as Smis, so it's safe to iterate the whole
  // embedder field area as tagged slots.
  static_assert(kEmbedderDataSlotSize == kTaggedSize);
#endif
  IteratePointers(obj, start_offset, end_offset, v);
}

template <typename ObjectVisitor>
// static
void BodyDescriptorBase::IterateJSObjectBodyWithoutEmbedderFieldsImpl(
    Tagged<Map> map, Tagged<HeapObject> obj, int start_offset, int end_offset,
    ObjectVisitor* v) {
  // This body iteration assumes that there's no embedder fields.
  DCHECK_IMPLIES(JSObject::MayHaveEmbedderFields(map),
                 UncheckedCast<JSObject>(obj)->GetEmbedderFieldCount() == 0);
  IteratePointers(obj, start_offset, end_offset, v);
}

// This is a BodyDescriptor helper for usage within JSAPIObjectWithEmbedderSlots
// and JSSpecialObject. The class hierarchies are separate but
// `kCppHeapWrappableOffset` is the same for both.
class JSAPIObjectWithEmbedderSlotsOrJSSpecialObjectBodyDescriptor
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateJSAPIObjectWithEmbedderSlotsHeader(
      Tagged<Map> map, Tagged<HeapObject> obj, int object_size,
      ObjectVisitor* v) {
    // Visit JSObject header.
    IteratePointers(obj, JSObject::kPropertiesOrHashOffset,
                    JSObject::kEndOfStrongFieldsOffset, v);

    // Visit JSAPIObjectWithEmbedderSlots or JSSpecialObject header.
    static_assert(JSObject::kEndOfStrongFieldsOffset ==
                  JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset);
    static_assert(JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset ==
                  JSSpecialObject::kCppHeapWrappableOffset);
    static_assert(JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffsetEnd +
                      1 ==
                  JSAPIObjectWithEmbedderSlots::kHeaderSize);
    v->VisitCppHeapPointer(
        obj, obj->RawCppHeapPointerField(
                 JSAPIObjectWithEmbedderSlots::kCppHeapWrappableOffset));
  }

  template <typename ConcreteType, typename ObjectVisitor>
  static inline void IterateJSAPIObjectWithEmbedderSlotsTail(
      Tagged<Map> map, Tagged<HeapObject> obj, int object_size,
      ObjectVisitor* v) {
    // Visit the tail of JSObject with possible embedder fields and in-object
    // properties. Note that embedder fields are processed in the JSObject base
    // class as there's other object hierarchies that contain embedder fields as
    // well.
    IterateJSObjectBodyImpl(map, obj, ConcreteType::kHeaderSize, object_size,
                            v);
  }

  template <typename ConcreteType, typename ObjectVisitor>
  static inline void IterateJSAPIObjectWithoutEmbedderSlotsTail(
      Tagged<Map> map, Tagged<HeapObject> obj, int object_size,
      ObjectVisitor* v) {
    IterateJSObjectBodyWithoutEmbedderFieldsImpl(
        map, obj, ConcreteType::kHeaderSize, object_size, v);
  }

  static constexpr int kHeaderSize = JSSpecialObject::kHeaderSize;

  static_assert(JSAPIObjectWithEmbedderSlots::kHeaderSize ==
                JSSpecialObject::kHeaderSize);
  static_assert(Internals::kJSAPIObjectWithEmbedderSlotsHeaderSize ==
                JSSpecialObject::kHeaderSize);
};

class JSAPIObjectWithEmbedderSlots::BodyDescriptor
    : public JSAPIObjectWithEmbedderSlotsOrJSSpecialObjectBodyDescriptor {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateJSAPIObjectWithEmbedderSlotsHeader(map, obj, object_size, v);
    IterateJSAPIObjectWithEmbedderSlotsTail<
        JSAPIObjectWithEmbedderSlotsOrJSSpecialObjectBodyDescriptor>(
        map, obj, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

template <typename ObjectVisitor>
DISABLE_CFI_PERF void BodyDescriptorBase::IteratePointers(
    Tagged<HeapObject> obj, int start_offset, int end_offset,
    ObjectVisitor* v) {
  if (start_offset == HeapObject::kMapOffset) {
    v->VisitMapPointer(obj);
    start_offset += kTaggedSize;
  }
  v->VisitPointers(obj, obj->RawField(start_offset), obj->RawField(end_offset));
}

template <typename ObjectVisitor>
void BodyDescriptorBase::IteratePointer(Tagged<HeapObject> obj, int offset,
                                        ObjectVisitor* v) {
  DCHECK_NE(offset, HeapObject::kMapOffset);
  v->VisitPointer(obj, obj->RawField(offset));
}

template <typename ObjectVisitor>
DISABLE_CFI_PERF void BodyDescriptorBase::IterateMaybeWeakPointers(
    Tagged<HeapObject> obj, int start_offset, int end_offset,
    ObjectVisitor* v) {
  v->VisitPointers(obj, obj->RawMaybeWeakField(start_offset),
                   obj->RawMaybeWeakField(end_offset));
}

template <typename ObjectVisitor>
void BodyDescriptorBase::IterateMaybeWeakPointer(Tagged<HeapObject> obj,
                                                 int offset, ObjectVisitor* v) {
  DCHECK_NE(offset, HeapObject::kMapOffset);
  v->VisitPointer(obj, obj->RawMaybeWeakField(offset));
}

template <typename ObjectVisitor>
DISABLE_CFI_PERF void BodyDescriptorBase::IterateCustomWeakPointers(
    Tagged<HeapObject> obj, int start_offset, int end_offset,
    ObjectVisitor* v) {
  v->VisitCustomWeakPointers(obj, obj->RawField(start_offset),
                             obj->RawField(end_offset));
}

template <typename ObjectVisitor>
DISABLE_CFI_PERF void BodyDescriptorBase::IterateEphemeron(
    Tagged<HeapObject> obj, int index, int key_offset, int value_offset,
    ObjectVisitor* v) {
  v->VisitEphemeron(obj, index, obj->RawField(key_offset),
                    obj->RawField(value_offset));
}

template <typename ObjectVisitor>
void BodyDescriptorBase::IterateCustomWeakPointer(Tagged<HeapObject> obj,
                                                  int offset,
                                                  ObjectVisitor* v) {
  v->VisitCustomWeakPointer(obj, obj->RawField(offset));
}

template <typename ObjectVisitor>
void BodyDescriptorBase::IterateTrustedPointer(Tagged<HeapObject> obj,
                                               int offset, ObjectVisitor* v,
                                               IndirectPointerMode mode,
                                               IndirectPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
  v->VisitIndirectPointer(obj, obj->RawIndirectPointerField(offset, tag), mode);
#else
  if (mode == IndirectPointerMode::kStrong) {
    IteratePointer(obj, offset, v);
  } else {
    IterateCustomWeakPointer(obj, offset, v);
  }
#endif
}

template <typename ObjectVisitor>
void BodyDescriptorBase::IterateCodePointer(Tagged<HeapObject> obj, int offset,
                                            ObjectVisitor* v,
                                            IndirectPointerMode mode) {
  IterateTrustedPointer(obj, offset, v, mode, kCodeIndirectPointerTag);
}

template <typename ObjectVisitor>
void BodyDescriptorBase::IterateSelfIndirectPointer(Tagged<HeapObject> obj,
                                                    IndirectPointerTag tag,
                                                    ObjectVisitor* v) {
#ifdef V8_ENABLE_SANDBOX
  v->VisitTrustedPointerTableEntry(
      obj, obj->RawIndirectPointerField(
               ExposedTrustedObject::kSelfIndirectPointerOffset, tag));
#endif
}

template <typename ObjectVisitor>
void BodyDescriptorBase::IterateProtectedPointer(Tagged<HeapObject> obj,
                                                 int offset, ObjectVisitor* v) {
  DCHECK(IsTrustedObject(obj));
  Tagged<TrustedObject> host = Cast<TrustedObject>(obj);
  v->VisitProtectedPointer(host, host->RawProtectedPointerField(offset));
}

#ifdef V8_ENABLE_LEAPTIERING
template <typename ObjectVisitor>
void BodyDescriptorBase::IterateJSDispatchEntry(Tagged<HeapObject> obj,
                                                int offset, ObjectVisitor* v) {
  JSDispatchHandle handle = obj->Relaxed_ReadField<JSDispatchHandle>(offset);
  v->VisitJSDispatchTableEntry(obj, handle);
}
#endif  // V8_ENABLE_LEAPTIERING

class HeapNumber::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static constexpr int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return sizeof(HeapNumber);
  }
};

// This is a descriptor for one/two pointer fillers.
class FreeSpaceFillerBodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return map->instance_size();
  }
};

class FreeSpace::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<FreeSpace>(raw_object)->Size();
  }
};

class JSObject::BodyDescriptor final : public BodyDescriptorBase {
 public:
  static const int kStartOffset = JSReceiver::kPropertiesOrHashOffset;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateJSObjectBodyImpl(map, obj, kStartOffset, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSObject::FastBodyDescriptor final : public BodyDescriptorBase {
 public:
  static const int kStartOffset = JSReceiver::kPropertiesOrHashOffset;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, kStartOffset, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSDate::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, JSObject::BodyDescriptor::kStartOffset,
                    JSDate::kValueOffset, v);
    IterateJSObjectBodyImpl(map, obj, JSDate::kStartOfStrongFieldsOffset,
                            object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSRegExp::BodyDescriptor final : public BodyDescriptorBase {
 public:
  static const int kStartOffset = JSReceiver::kPropertiesOrHashOffset;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, kPropertiesOrHashOffset, JSObject::kHeaderSize, v);
    IterateTrustedPointer(obj, kDataOffset, v, IndirectPointerMode::kStrong,
                          kRegExpDataIndirectPointerTag);
    IteratePointer(obj, kSourceOffset, v);
    IterateJSObjectBodyImpl(map, obj, kHeaderSize, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class RegExpData::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kRegExpDataIndirectPointerTag, v);
    IteratePointer(obj, kSourceOffset, v);
    IteratePointer(obj, kWrapperOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return kSize;
  }
};

class AtomRegExpData::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kRegExpDataIndirectPointerTag, v);

    IteratePointer(obj, kSourceOffset, v);
    IteratePointer(obj, kWrapperOffset, v);

    IteratePointer(obj, kPatternOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return kSize;
  }
};

class IrRegExpData::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kRegExpDataIndirectPointerTag, v);

    IteratePointer(obj, kSourceOffset, v);
    IteratePointer(obj, kWrapperOffset, v);

    IterateCodePointer(obj, kLatin1CodeOffset, v, IndirectPointerMode::kStrong);
    IterateCodePointer(obj, kUc16CodeOffset, v, IndirectPointerMode::kStrong);
    IterateProtectedPointer(obj, kLatin1BytecodeOffset, v);
    IterateProtectedPointer(obj, kUc16BytecodeOffset, v);
    IteratePointer(obj, kCaptureNameMapOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return kSize;
  }
};

class RegExpDataWrapper::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateTrustedPointer(obj, kDataOffset, v, IndirectPointerMode::kStrong,
                          kRegExpDataIndirectPointerTag);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return kSize;
  }
};

class WeakCell::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, HeapObject::kHeaderSize, kTargetOffset, v);
    IterateCustomWeakPointer(obj, kTargetOffset, v);
    IterateCustomWeakPointer(obj, kUnregisterTokenOffset, v);
    IteratePointers(obj, kUnregisterTokenOffset + kTaggedSize, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSWeakRef::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, JSReceiver::kPropertiesOrHashOffset, kTargetOffset, v);
    IterateCustomWeakPointer(obj, kTargetOffset, v);
    IterateJSObjectBodyImpl(map, obj, kTargetOffset + kTaggedSize, object_size,
                            v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSFinalizationRegistry::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, JSObject::BodyDescriptor::kStartOffset,
                    kNextDirtyOffset, v);
    IterateCustomWeakPointer(obj, kNextDirtyOffset, v);
    IterateJSObjectBodyImpl(map, obj, kNextDirtyOffset + kTaggedSize,
                            object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class AllocationSite::BodyDescriptor final : public BodyDescriptorBase {
 public:
  static_assert(AllocationSite::kCommonPointerFieldEndOffset ==
                AllocationSite::kPretenureDataOffset);
  static_assert(AllocationSite::kPretenureDataOffset + kInt32Size ==
                AllocationSite::kPretenureCreateCountOffset);
  static_assert(AllocationSite::kPretenureCreateCountOffset + kInt32Size ==
                AllocationSite::kWeakNextOffset);

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // Iterate over all the common pointer fields
    IteratePointers(obj, AllocationSite::kStartOffset,
                    AllocationSite::kCommonPointerFieldEndOffset, v);
    // Skip PretenureDataOffset and PretenureCreateCount which are Int32 fields.
    // Visit weak_next only if it has weak_next field.
    if (object_size == AllocationSite::kSizeWithWeakNext) {
      IterateCustomWeakPointers(obj, AllocationSite::kWeakNextOffset,
                                AllocationSite::kSizeWithWeakNext, v);
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSFunction::BodyDescriptor final : public BodyDescriptorBase {
 public:
  static const int kStartOffset = JSObject::BodyDescriptor::kStartOffset;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // Iterate JSFunction header fields first.
    int header_size = JSFunction::GetHeaderSize(map->has_prototype_slot());
    DCHECK_GE(object_size, header_size);
    IteratePointers(obj, kStartOffset, kCodeOffset, v);

    // The code field is treated as a custom weak pointer. This field
    // is visited as a weak pointer if the Code is baseline code
    // and the bytecode array corresponding to this function is old. In the rest
    // of the cases this field is treated as strong pointer.
    // See MarkingVisitorBase::VisitJSFunction.
    IterateCodePointer(obj, kCodeOffset, v, IndirectPointerMode::kCustom);
    DCHECK_GE(header_size, kCodeOffset);

#ifdef V8_ENABLE_LEAPTIERING
    IterateJSDispatchEntry(obj, kDispatchHandleOffset, v);
#endif

    // Iterate rest of the header fields
    IteratePointers(obj, kCodeOffset + kTaggedSize, header_size, v);
    // Iterate rest of the fields starting after the header.
    IterateJSObjectBodyImpl(map, obj, header_size, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSArrayBuffer::BodyDescriptor final
    : public JSAPIObjectWithEmbedderSlots::BodyDescriptor {
 public:
  using Base = JSAPIObjectWithEmbedderSlots::BodyDescriptor;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // JSObject with wrapper field.
    IterateJSAPIObjectWithEmbedderSlotsHeader(map, obj, object_size, v);
    // JSArrayBuffer.
    IteratePointers(obj, JSArrayBuffer::kStartOfStrongFieldsOffset,
                    JSArrayBuffer::kEndOfStrongFieldsOffset, v);
    v->VisitExternalPointer(
        obj, obj->RawExternalPointerField(JSArrayBuffer::kExtensionOffset,
                                          kArrayBufferExtensionTag));
    // JSObject tail: possible embedder fields + in-object properties.
    if constexpr (JSArrayBuffer::kContainsEmbedderFields) {
      IterateJSAPIObjectWithEmbedderSlotsTail<JSArrayBuffer>(map, obj,
                                                             object_size, v);
    } else {
      IterateJSAPIObjectWithoutEmbedderSlotsTail<JSArrayBuffer>(map, obj,
                                                                object_size, v);
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSArrayBufferView::BodyDescriptor
    : public JSAPIObjectWithEmbedderSlots::BodyDescriptor {
 public:
  using Base = JSAPIObjectWithEmbedderSlots::BodyDescriptor;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // JSObject with wrapper field.
    IterateJSAPIObjectWithEmbedderSlotsHeader(map, obj, object_size, v);
    // JSArrayBufferView.
    IteratePointers(obj, JSArrayBufferView::kStartOfStrongFieldsOffset,
                    JSArrayBufferView::kEndOfStrongFieldsOffset, v);
  }
};

class JSTypedArray::BodyDescriptor : public JSArrayBufferView::BodyDescriptor {
 public:
  using Base = JSArrayBufferView::BodyDescriptor;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // JSArrayBufferView (including JSObject).
    Base::IterateBody(map, obj, object_size, v);
    // JSTypedArray.
    IteratePointers(obj, JSTypedArray::kStartOfStrongFieldsOffset,
                    JSTypedArray::kEndOfStrongFieldsOffset, v);

    // JSObject tail: possible embedder fields + in-object properties.
    if constexpr (JSTypedArray::kContainsEmbedderFields) {
      IterateJSAPIObjectWithEmbedderSlotsTail<JSTypedArray>(map, obj,
                                                            object_size, v);
    } else {
      IterateJSAPIObjectWithoutEmbedderSlotsTail<JSTypedArray>(map, obj,
                                                               object_size, v);
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSDataViewOrRabGsabDataView::BodyDescriptor final
    : public JSArrayBufferView::BodyDescriptor {
 public:
  using Base = JSArrayBufferView::BodyDescriptor;

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // JSArrayBufferView (including JSObject).
    Base::IterateBody(map, obj, object_size, v);
    // JSDataViewOrRabGsabDataView.
    IteratePointers(obj,
                    JSDataViewOrRabGsabDataView::kStartOfStrongFieldsOffset,
                    JSDataViewOrRabGsabDataView::kEndOfStrongFieldsOffset, v);
    // JSObject tail: possible embedder fields + in-object properties.
    if constexpr (JSDataViewOrRabGsabDataView::kContainsEmbedderFields) {
      IterateJSAPIObjectWithEmbedderSlotsTail<JSDataViewOrRabGsabDataView>(
          map, obj, object_size, v);
    } else {
      IterateJSAPIObjectWithoutEmbedderSlotsTail<JSDataViewOrRabGsabDataView>(
          map, obj, object_size, v);
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSExternalObject::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    DCHECK_EQ(0, map->GetInObjectProperties());
    IteratePointers(obj, kPropertiesOrHashOffset, kEndOfTaggedFieldsOffset, v);
    v->VisitExternalPointer(obj, obj->RawExternalPointerField(
                                     kValueOffset, kExternalObjectValueTag));
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

template <typename Derived>
class V8_EXPORT_PRIVATE SmallOrderedHashTable<Derived>::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    Tagged<Derived> table = Cast<Derived>(obj);
    int start_offset = DataTableStartOffset();
    int end_offset = table->GetBucketsStartOffset();
    IteratePointers(obj, start_offset, end_offset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    Tagged<Derived> table = Cast<Derived>(obj);
    return Derived::SizeFor(table->Capacity());
  }
};

class V8_EXPORT_PRIVATE SwissNameDictionary::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    Tagged<SwissNameDictionary> table = UncheckedCast<SwissNameDictionary>(obj);
    static_assert(MetaTablePointerOffset() + kTaggedSize ==
                  DataTableStartOffset());
    int start_offset = MetaTablePointerOffset();
    int end_offset = table->DataTableEndOffset(table->Capacity());
    IteratePointers(obj, start_offset, end_offset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    Tagged<SwissNameDictionary> table = UncheckedCast<SwissNameDictionary>(obj);
    return SwissNameDictionary::SizeFor(table->Capacity());
  }
};

class ByteArray::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return UncheckedCast<ByteArray>(obj)->AllocatedSize();
  }
};

class TrustedByteArray::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return UncheckedCast<TrustedByteArray>(obj)->AllocatedSize();
  }
};

class BytecodeArray::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kBytecodeArrayIndirectPointerTag, v);
    IteratePointer(obj, kWrapperOffset, v);
    IterateProtectedPointer(obj, kSourcePositionTableOffset, v);
    IterateProtectedPointer(obj, kHandlerTableOffset, v);
    IterateProtectedPointer(obj, kConstantPoolOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return BytecodeArray::SizeFor(
        Cast<BytecodeArray>(obj)->length(kAcquireLoad));
  }
};

class BytecodeWrapper::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateTrustedPointer(obj, kBytecodeOffset, v, IndirectPointerMode::kStrong,
                          kBytecodeArrayIndirectPointerTag);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return kSize;
  }
};

class ExternalPointerArray::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    Tagged<ExternalPointerArray> array =
        UncheckedCast<ExternalPointerArray>(obj);
    for (int i = 0; i < array->length(); i++) {
      // We don't currently track the (expected) tag of the elements of this
      // array, so we have to use the generic tag here. This is ok as long as
      // the visitor does not try to dereference the pointer (which it never
      // should). The alternative would probably be to store the tag as
      // additional metadata in the array itself, but then we also need to be
      // careful since an attacker could then modify the tag.
      v->VisitExternalPointer(array,
                              array->RawExternalPointerField(
                                  ExternalPointerArray::OffsetOfElementAt(i),
                                  kAnyExternalPointerTag));
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return ExternalPointerArray::SizeFor(
        Cast<ExternalPointerArray>(obj)->length(kAcquireLoad));
  }
};

class BigInt::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return BigInt::SizeFor(UncheckedCast<BigInt>(obj)->length(kAcquireLoad));
  }
};

class FixedDoubleArray::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return UncheckedCast<FixedDoubleArray>(obj)->AllocatedSize();
  }
};

class FeedbackMetadata::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return UncheckedCast<FeedbackMetadata>(obj)->AllocatedSize();
  }
};

class PreparseData::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    Tagged<PreparseData> data = UncheckedCast<PreparseData>(obj);
    int start_offset = data->inner_start_offset();
    int end_offset = start_offset + data->children_length() * kTaggedSize;
    IteratePointers(obj, start_offset, end_offset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    Tagged<PreparseData> data = UncheckedCast<PreparseData>(obj);
    return PreparseData::SizeFor(data->data_length(), data->children_length());
  }
};

class InterpreterData::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kInterpreterDataIndirectPointerTag, v);
    IterateProtectedPointer(obj, kBytecodeArrayOffset, v);
    IterateProtectedPointer(obj, kInterpreterTrampolineOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return kSize;
  }
};

class UncompiledDataWithoutPreparseData::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kUncompiledDataIndirectPointerTag, v);
    IteratePointer(obj, kInferredNameOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return kSize;
  }
};

class UncompiledDataWithPreparseData::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kUncompiledDataIndirectPointerTag, v);
    IteratePointer(obj, kInferredNameOffset, v);
    IteratePointer(obj, kPreparseDataOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return kSize;
  }
};

class UncompiledDataWithoutPreparseDataWithJob::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kUncompiledDataIndirectPointerTag, v);
    IteratePointer(obj, kInferredNameOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return kSize;
  }
};

class UncompiledDataWithPreparseDataAndJob::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kUncompiledDataIndirectPointerTag, v);
    IteratePointer(obj, kInferredNameOffset, v);
    IteratePointer(obj, kPreparseDataOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return kSize;
  }
};

class SharedFunctionInfo::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateTrustedPointer(obj, kTrustedFunctionDataOffset, v,
                          IndirectPointerMode::kCustom,
                          kUnknownIndirectPointerTag);
    IteratePointers(obj, kStartOfStrongFieldsOffset, kEndOfStrongFieldsOffset,
                    v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return kSize;
  }
};

class SharedFunctionInfoWrapper::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointer(obj, kSharedInfoOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return kSize;
  }
};

class DebugInfo::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, kStartOfStrongFieldsOffset, kEndOfStrongFieldsOffset,
                    v);
    IterateTrustedPointer(obj, kDebugBytecodeArrayOffset, v,
                          IndirectPointerMode::kStrong,
                          kBytecodeArrayIndirectPointerTag);
    IterateTrustedPointer(obj, kOriginalBytecodeArrayOffset, v,
                          IndirectPointerMode::kStrong,
                          kBytecodeArrayIndirectPointerTag);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return obj->SizeFromMap(map);
  }
};

class CallSiteInfo::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // The field can contain either a Code or a BytecodeArray object, so we need
    // to use the kUnknownIndirectPointerTag here.
    IterateTrustedPointer(obj, kCodeObjectOffset, v,
                          IndirectPointerMode::kStrong,
                          kUnknownIndirectPointerTag);
    IteratePointers(obj, kStartOfStrongFieldsOffset, kEndOfStrongFieldsOffset,
                    v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return obj->SizeFromMap(map);
  }
};

class PrototypeInfo::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, HeapObject::kHeaderSize, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return obj->SizeFromMap(map);
  }
};

class JSWeakCollection::BodyDescriptorImpl final : public BodyDescriptorBase {
 public:
  static_assert(kTableOffset + kTaggedSize == kHeaderSizeOfAllWeakCollections);

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateJSObjectBodyImpl(map, obj, kPropertiesOrHashOffset, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class JSSynchronizationPrimitive::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, kPropertiesOrHashOffset, kEndOfTaggedFieldsOffset, v);
    v->VisitExternalPointer(obj,
                            obj->RawExternalPointerField(kWaiterQueueHeadOffset,
                                                         kWaiterQueueNodeTag));
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

#if V8_ENABLE_WEBASSEMBLY
class WasmTypeInfo::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    v->VisitExternalPointer(
        obj, obj->RawExternalPointerField(kNativeTypeOffset,
                                          kWasmTypeInfoNativeTypeTag));

    IterateTrustedPointer(obj, kTrustedDataOffset, v,
                          IndirectPointerMode::kStrong,
                          kWasmTrustedInstanceDataIndirectPointerTag);
    IteratePointers(obj, kSupertypesOffset, SizeOf(map, obj), v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return kSupertypesOffset +
           Cast<WasmTypeInfo>(object)->supertypes_length() * kTaggedSize;
  }
};

class WasmInstanceObject::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, kPropertiesOrHashOffset, JSObject::kHeaderSize, v);
    IterateTrustedPointer(obj, kTrustedDataOffset, v,
                          IndirectPointerMode::kStrong,
                          kWasmTrustedInstanceDataIndirectPointerTag);
    IteratePointer(obj, kModuleObjectOffset, v);
    IteratePointer(obj, kExportsObjectOffset, v);
    IterateJSObjectBodyImpl(map, obj, kHeaderSize, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class WasmTrustedInstanceData::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kWasmTrustedInstanceDataIndirectPointerTag,
                               v);
    for (uint16_t offset : kTaggedFieldOffsets) {
      IteratePointer(obj, offset, v);
    }

    for (uint16_t offset : kProtectedFieldOffsets) {
      IterateProtectedPointer(obj, offset, v);
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return kSize;
  }
};

class WasmTableObject::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, JSObject::BodyDescriptor::kStartOffset,
                    kTrustedDataOffset, v);
    IterateTrustedPointer(obj, kTrustedDataOffset, v,
                          IndirectPointerMode::kStrong,
                          kWasmTrustedInstanceDataIndirectPointerTag);
    IterateJSObjectBodyImpl(map, obj, kIsTable64Offset + kTaggedSize,
                            object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class WasmTagObject::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, JSObject::BodyDescriptor::kStartOffset,
                    kTrustedDataOffset, v);
    IterateTrustedPointer(obj, kTrustedDataOffset, v,
                          IndirectPointerMode::kStrong,
                          kWasmTrustedInstanceDataIndirectPointerTag);
    IterateJSObjectBodyImpl(map, obj, kHeaderSize, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class WasmGlobalObject::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, JSObject::BodyDescriptor::kStartOffset,
                    kTrustedDataOffset, v);
    IterateTrustedPointer(obj, kTrustedDataOffset, v,
                          IndirectPointerMode::kStrong,
                          kWasmTrustedInstanceDataIndirectPointerTag);
    IteratePointer(obj, kUntaggedBufferOffset, v);
    IteratePointer(obj, kTaggedBufferOffset, v);
    IterateJSObjectBodyImpl(map, obj, kIsMutableOffset + kTaggedSize,
                            object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
  }
};

class WasmDispatchTable::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    int length = Cast<WasmDispatchTable>(obj)->length(kAcquireLoad);
    for (int i = 0; i < length; ++i) {
      IterateProtectedPointer(obj, OffsetOf(i) + kImplicitArgBias, v);
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    int capacity = Cast<WasmDispatchTable>(object)->capacity();
    return SizeFor(capacity);
  }
};

class WasmArray::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // The type is safe to use because it's kept alive by the {map}'s
    // WasmTypeInfo.
    if (!WasmArray::GcSafeType(map)->element_type().is_reference()) return;
    IteratePointers(obj, WasmArray::kHeaderSize, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return WasmArray::SizeFor(map, UncheckedCast<WasmArray>(object)->length());
  }
};

class WasmStruct::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    Tagged<WasmStruct> wasm_struct = UncheckedCast<WasmStruct>(obj);
    // The {type} is safe to use because it's kept alive by the {map}'s
    // WasmTypeInfo.
    wasm::StructType* type = WasmStruct::GcSafeType(map);
    for (uint32_t i = 0; i < type->field_count(); i++) {
      if (!type->field(i).is_reference()) continue;
      int offset = static_cast<int>(type->field_offset(i));
      v->VisitPointer(wasm_struct, wasm_struct->RawField(offset));
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return WasmStruct::GcSafeSize(map);
  }
};

class WasmNull::BodyDescriptor : public DataOnlyBodyDescriptor {
 public:
  static_assert(WasmNull::kStartOfStrongFieldsOffset ==
                WasmNull::kEndOfStrongFieldsOffset);

  static constexpr int kSize = WasmNull::kSize;

  static constexpr int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return kSize;
  }
};

#endif  // V8_ENABLE_WEBASSEMBLY

class ExternalString::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    Tagged<ExternalString> string = UncheckedCast<ExternalString>(obj);
    v->VisitExternalPointer(obj, ExternalPointerSlot(&string->resource_));
    if (string->is_uncached()) return;
    v->VisitExternalPointer(obj, ExternalPointerSlot(&string->resource_data_));
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    InstanceType type = map->instance_type();
    const auto is_uncached =
        (type & kUncachedExternalStringMask) == kUncachedExternalStringTag;
    return is_uncached ? sizeof(UncachedExternalString)
                       : sizeof(ExternalString);
  }
};

class CoverageInfo::BodyDescriptor final : public DataOnlyBodyDescriptor {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    Tagged<CoverageInfo> info = Cast<CoverageInfo>(object);
    return CoverageInfo::SizeFor(info->slot_count());
  }
};

class InstructionStream::BodyDescriptor final : public BodyDescriptorBase {
 public:
  static_assert(static_cast<int>(HeapObject::kHeaderSize) ==
                static_cast<int>(kCodeOffset));
  static_assert(kCodeOffset + kTaggedSize == kRelocationInfoOffset);
  static_assert(kRelocationInfoOffset + kTaggedSize == kDataStart);

  static constexpr int kRelocModeMask =
      RelocInfo::ModeMask(RelocInfo::CODE_TARGET) |
      RelocInfo::ModeMask(RelocInfo::RELATIVE_CODE_TARGET) |
      RelocInfo::ModeMask(RelocInfo::FULL_EMBEDDED_OBJECT) |
      RelocInfo::ModeMask(RelocInfo::COMPRESSED_EMBEDDED_OBJECT) |
      RelocInfo::ModeMask(RelocInfo::EXTERNAL_REFERENCE) |
      RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE) |
      RelocInfo::ModeMask(RelocInfo::INTERNAL_REFERENCE_ENCODED) |
      RelocInfo::ModeMask(RelocInfo::OFF_HEAP_TARGET) |
      RelocInfo::ModeMask(RelocInfo::WASM_STUB_CALL);

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 ObjectVisitor* v) {
    IterateProtectedPointer(obj, kCodeOffset, v);
    IterateProtectedPointer(obj, kRelocationInfoOffset, v);

    Tagged<InstructionStream> istream = UncheckedCast<InstructionStream>(obj);
    if (istream->IsFullyInitialized()) {
      RelocIterator it(istream, kRelocModeMask);
      v->VisitRelocInfo(istream, &it);
    }
  }

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateBody(map, obj, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return UncheckedCast<InstructionStream>(object)->Size();
  }
};

class Map::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, Map::kStartOfStrongFieldsOffset,
                    Map::kEndOfStrongFieldsOffset, v);
    IterateMaybeWeakPointer(obj, kTransitionsOrPrototypeInfoOffset, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return Map::kSize;
  }
};

class DataHandler::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    static_assert(kSmiHandlerOffset < kData1Offset,
                  "Field order must be in sync with this iteration code");
    static_assert(kData1Offset < kSizeWithData1,
                  "Field order must be in sync with this iteration code");
    IteratePointers(obj, kSmiHandlerOffset, kData1Offset, v);
    IterateMaybeWeakPointers(obj, kData1Offset, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return object->SizeFromMap(map);
  }
};

class NativeContext::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, NativeContext::kStartOfStrongFieldsOffset,
                    NativeContext::kEndOfStrongFieldsOffset, v);
    IterateCustomWeakPointers(obj, NativeContext::kStartOfWeakFieldsOffset,
                              NativeContext::kEndOfWeakFieldsOffset, v);
    v->VisitExternalPointer(
        obj, obj->RawExternalPointerField(kMicrotaskQueueOffset,
                                          kNativeContextMicrotaskQueueTag));
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return NativeContext::kSize;
  }
};

class Code::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateSelfIndirectPointer(obj, kCodeIndirectPointerTag, v);
    IterateProtectedPointer(
        obj, Code::kDeoptimizationDataOrInterpreterDataOffset, v);
    IterateProtectedPointer(obj, Code::kPositionTableOffset, v);
    IteratePointers(obj, Code::kStartOfStrongFieldsOffset,
                    Code::kEndOfStrongFieldsWithMainCageBaseOffset, v);

    static_assert(Code::kEndOfStrongFieldsWithMainCageBaseOffset ==
                  Code::kInstructionStreamOffset);
    static_assert(Code::kInstructionStreamOffset + kTaggedSize ==
                  Code::kEndOfStrongFieldsOffset);
    v->VisitInstructionStreamPointer(
        Cast<Code>(obj),
        obj->RawInstructionStreamField(kInstructionStreamOffset));
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return Code::kSize;
  }
};

class CodeWrapper::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IterateCodePointer(obj, kCodeOffset, v, IndirectPointerMode::kStrong);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> obj) {
    return kSize;
  }
};

class EmbedderDataArray::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
#ifdef V8_COMPRESS_POINTERS
    static_assert(kEmbedderDataSlotSize == 2 * kTaggedSize);
    for (int offset = EmbedderDataArray::OffsetOfElementAt(0);
         offset < object_size; offset += kEmbedderDataSlotSize) {
      IteratePointer(obj, offset + EmbedderDataSlot::kTaggedPayloadOffset, v);
      v->VisitExternalPointer(
          obj, obj->RawExternalPointerField(
                   offset + EmbedderDataSlot::kExternalPointerOffset,
                   kEmbedderDataSlotPayloadTag));
    }

#else
    // We store raw aligned pointers as Smis, so it's safe to iterate the whole
    // array.
    static_assert(kEmbedderDataSlotSize == kTaggedSize);
    IteratePointers(obj, EmbedderDataArray::kHeaderSize, object_size, v);
#endif
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return object->SizeFromMap(map);
  }
};

template <typename Op, typename... Args>
auto BodyDescriptorApply(InstanceType type, Args&&... args) {
#define CALL_APPLY(ClassName)                                         \
  Op::template apply<ObjectTraits<ClassName>::BodyDescriptor, false>( \
      std::forward<Args>(args)...)
#define CALL_APPLY_TRUSTED(ClassName)                                \
  Op::template apply<ObjectTraits<ClassName>::BodyDescriptor, true>( \
      std::forward<Args>(args)...)

  if (type < FIRST_NONSTRING_TYPE) {
    switch (type & kStringRepresentationMask) {
      case kSeqStringTag:
        if ((type & kStringEncodingMask) == kOneByteStringTag) {
          return CALL_APPLY(SeqOneByteString);
        } else {
          return CALL_APPLY(SeqTwoByteString);
        }
      case kConsStringTag:
        return CALL_APPLY(ConsString);
      case kThinStringTag:
        return CALL_APPLY(ThinString);
      case kSlicedStringTag:
        return CALL_APPLY(SlicedString);
      case kExternalStringTag:
        return CALL_APPLY(ExternalString);
    }
    UNREACHABLE();
  }
  if (InstanceTypeChecker::IsJSApiObject(type)) {
    return CALL_APPLY(JSAPIObjectWithEmbedderSlots);
  }

  switch (type) {
    case EMBEDDER_DATA_ARRAY_TYPE:
      return CALL_APPLY(EmbedderDataArray);
    case HASH_TABLE_TYPE:
    case ORDERED_HASH_MAP_TYPE:
    case ORDERED_HASH_SET_TYPE:
    case ORDERED_NAME_DICTIONARY_TYPE:
    case NAME_DICTIONARY_TYPE:
    case GLOBAL_DICTIONARY_TYPE:
    case NUMBER_DICTIONARY_TYPE:
    case SIMPLE_NUMBER_DICTIONARY_TYPE:
    case NAME_TO_INDEX_HASH_TABLE_TYPE:
    case REGISTERED_SYMBOL_TABLE_TYPE:
      return CALL_APPLY(FixedArray);
#define CASE(TypeCamelCase, TYPE_UPPER_CASE) \
  case TYPE_UPPER_CASE##_TYPE:               \
    return CALL_APPLY(TypeCamelCase);
      SIMPLE_HEAP_OBJECT_LIST2(CASE)
#undef CASE
    case SLOPPY_ARGUMENTS_ELEMENTS_TYPE:
      return CALL_APPLY(SloppyArgumentsElements);
    case EPHEMERON_HASH_TABLE_TYPE:
      return CALL_APPLY(EphemeronHashTable);
    case AWAIT_CONTEXT_TYPE:
    case BLOCK_CONTEXT_TYPE:
    case CATCH_CONTEXT_TYPE:
    case DEBUG_EVALUATE_CONTEXT_TYPE:
    case EVAL_CONTEXT_TYPE:
    case FUNCTION_CONTEXT_TYPE:
    case MODULE_CONTEXT_TYPE:
    case SCRIPT_CONTEXT_TYPE:
    case WITH_CONTEXT_TYPE:
      return CALL_APPLY(Context);
    case NATIVE_CONTEXT_TYPE:
      return CALL_APPLY(NativeContext);
    case FEEDBACK_METADATA_TYPE:
      return CALL_APPLY(FeedbackMetadata);
    case PROPERTY_ARRAY_TYPE:
      return CALL_APPLY(PropertyArray);
    case TRANSITION_ARRAY_TYPE:
      return CALL_APPLY(TransitionArray);
    case FEEDBACK_CELL_TYPE:
      return CALL_APPLY(FeedbackCell);
    case COVERAGE_INFO_TYPE:
      return CALL_APPLY(CoverageInfo);
#if V8_ENABLE_WEBASSEMBLY
    case WASM_ARRAY_TYPE:
      return CALL_APPLY(WasmArray);
    case WASM_EXCEPTION_PACKAGE_TYPE:
      return CALL_APPLY(WasmExceptionPackage);
    case WASM_FUNC_REF_TYPE:
      return CALL_APPLY(WasmFuncRef);
    case WASM_GLOBAL_OBJECT_TYPE:
      return CALL_APPLY(WasmGlobalObject);
    case WASM_RESUME_DATA_TYPE:
      return CALL_APPLY(WasmResumeData);
    case WASM_CONTINUATION_OBJECT_TYPE:
      return CALL_APPLY(WasmContinuationObject);
    case WASM_STRUCT_TYPE:
      return CALL_APPLY(WasmStruct);
    case WASM_TABLE_OBJECT_TYPE:
      return CALL_APPLY(WasmTableObject);
    case WASM_TYPE_INFO_TYPE:
      return CALL_APPLY(WasmTypeInfo);
    case WASM_SUSPENDER_OBJECT_TYPE:
      return CALL_APPLY(WasmSuspenderObject);
    case WASM_SUSPENDING_OBJECT_TYPE:
      return CALL_APPLY(WasmSuspendingObject);
#endif  // V8_ENABLE_WEBASSEMBLY
    case JS_ARGUMENTS_OBJECT_TYPE:
    case JS_ARRAY_ITERATOR_PROTOTYPE_TYPE:
    case JS_ARRAY_ITERATOR_TYPE:
    case JS_ARRAY_TYPE:
    case JS_ASYNC_DISPOSABLE_STACK_TYPE:
    case JS_ASYNC_FROM_SYNC_ITERATOR_TYPE:
    case JS_ASYNC_FUNCTION_OBJECT_TYPE:
    case JS_ASYNC_GENERATOR_OBJECT_TYPE:
    case JS_BOUND_FUNCTION_TYPE:
    case JS_CONTEXT_EXTENSION_OBJECT_TYPE:
    case JS_DISPOSABLE_STACK_BASE_TYPE:
    case JS_ERROR_TYPE:
    case JS_FINALIZATION_REGISTRY_TYPE:
    case JS_GENERATOR_OBJECT_TYPE:
    case JS_ITERATOR_FILTER_HELPER_TYPE:
    case JS_ITERATOR_MAP_HELPER_TYPE:
    case JS_ITERATOR_TAKE_HELPER_TYPE:
    case JS_ITERATOR_DROP_HELPER_TYPE:
    case JS_ITERATOR_FLAT_MAP_HELPER_TYPE:
    case JS_ITERATOR_PROTOTYPE_TYPE:
    case JS_MAP_ITERATOR_PROTOTYPE_TYPE:
    case JS_MAP_KEY_ITERATOR_TYPE:
    case JS_MAP_KEY_VALUE_ITERATOR_TYPE:
    case JS_MAP_TYPE:
    case JS_MAP_VALUE_ITERATOR_TYPE:
    case JS_MESSAGE_OBJECT_TYPE:
    case JS_MODULE_NAMESPACE_TYPE:
    case JS_OBJECT_PROTOTYPE_TYPE:
    case JS_OBJECT_TYPE:
    case JS_PRIMITIVE_WRAPPER_TYPE:
    case JS_PROMISE_PROTOTYPE_TYPE:
    case JS_PROMISE_TYPE:
    case JS_REG_EXP_PROTOTYPE_TYPE:
    case JS_REG_EXP_STRING_ITERATOR_TYPE:
    case JS_SET_ITERATOR_PROTOTYPE_TYPE:
    case JS_SET_KEY_VALUE_ITERATOR_TYPE:
    case JS_SET_PROTOTYPE_TYPE:
    case JS_SET_TYPE:
    case JS_SET_VALUE_ITERATOR_TYPE:
    case JS_SHADOW_REALM_TYPE:
    case JS_SHARED_ARRAY_TYPE:
    case JS_SHARED_STRUCT_TYPE:
    case JS_STRING_ITERATOR_PROTOTYPE_TYPE:
    case JS_STRING_ITERATOR_TYPE:
    case JS_SYNC_DISPOSABLE_STACK_TYPE:
    case JS_TEMPORAL_CALENDAR_TYPE:
    case JS_TEMPORAL_DURATION_TYPE:
    case JS_TEMPORAL_INSTANT_TYPE:
    case JS_TEMPORAL_PLAIN_DATE_TYPE:
    case JS_TEMPORAL_PLAIN_DATE_TIME_TYPE:
    case JS_TEMPORAL_PLAIN_MONTH_DAY_TYPE:
    case JS_TEMPORAL_PLAIN_TIME_TYPE:
    case JS_TEMPORAL_PLAIN_YEAR_MONTH_TYPE:
    case JS_TEMPORAL_TIME_ZONE_TYPE:
    case JS_TEMPORAL_ZONED_DATE_TIME_TYPE:
    case JS_TYPED_ARRAY_PROTOTYPE_TYPE:
    case JS_VALID_ITERATOR_WRAPPER_TYPE:
    case JS_WRAPPED_FUNCTION_TYPE:
    case JS_RAW_JSON_TYPE:
#ifdef V8_INTL_SUPPORT
    case JS_V8_BREAK_ITERATOR_TYPE:
    case JS_COLLATOR_TYPE:
    case JS_DATE_TIME_FORMAT_TYPE:
    case JS_DISPLAY_NAMES_TYPE:
    case JS_DURATION_FORMAT_TYPE:
    case JS_LIST_FORMAT_TYPE:
    case JS_LOCALE_TYPE:
    case JS_NUMBER_FORMAT_TYPE:
    case JS_PLURAL_RULES_TYPE:
    case JS_RELATIVE_TIME_FORMAT_TYPE:
    case JS_SEGMENT_ITERATOR_TYPE:
    case JS_SEGMENTER_TYPE:
    case JS_SEGMENTS_TYPE:
#endif  // V8_INTL_SUPPORT
#if V8_ENABLE_WEBASSEMBLY
    case WASM_MEMORY_OBJECT_TYPE:
    case WASM_MODULE_OBJECT_TYPE:
    case WASM_TAG_OBJECT_TYPE:
    case WASM_VALUE_OBJECT_TYPE:
#endif  // V8_ENABLE_WEBASSEMBLY
      return CALL_APPLY(JSObject);

    case JS_DATE_TYPE:
      return CALL_APPLY(JSDate);

    case JS_API_OBJECT_TYPE:
    case JS_GLOBAL_OBJECT_TYPE:
    case JS_GLOBAL_PROXY_TYPE:
    case JS_SPECIAL_API_OBJECT_TYPE:
      return Op::template apply<JSAPIObjectWithEmbedderSlots::BodyDescriptor,
                                false>(std::forward<Args>(args)...);

    case JS_FUNCTION_TYPE:
    case JS_CLASS_CONSTRUCTOR_TYPE:
    case JS_PROMISE_CONSTRUCTOR_TYPE:
    case JS_REG_EXP_CONSTRUCTOR_TYPE:
    case JS_ARRAY_CONSTRUCTOR_TYPE:
#define TYPED_ARRAY_CONSTRUCTORS_SWITCH(Type, type, TYPE, Ctype) \
  case TYPE##_TYPED_ARRAY_CONSTRUCTOR_TYPE:
      TYPED_ARRAYS(TYPED_ARRAY_CONSTRUCTORS_SWITCH)
#undef TYPED_ARRAY_CONSTRUCTORS_SWITCH
      return CALL_APPLY(JSFunction);
#if V8_ENABLE_WEBASSEMBLY
    case WASM_INSTANCE_OBJECT_TYPE:
      return CALL_APPLY(WasmInstanceObject);
    case WASM_NULL_TYPE:
      return CALL_APPLY(WasmNull);
#endif  // V8_ENABLE_WEBASSEMBLY
    case JS_WEAK_MAP_TYPE:
    case JS_WEAK_SET_TYPE:
      return CALL_APPLY(JSWeakCollection);
    case JS_ARRAY_BUFFER_TYPE:
      return CALL_APPLY(JSArrayBuffer);
    case JS_DATA_VIEW_TYPE:
      return CALL_APPLY(JSDataView);
    case JS_RAB_GSAB_DATA_VIEW_TYPE:
      return CALL_APPLY(JSRabGsabDataView);
    case JS_TYPED_ARRAY_TYPE:
      return CALL_APPLY(JSTypedArray);
    case JS_EXTERNAL_OBJECT_TYPE:
      return CALL_APPLY(JSExternalObject);
    case WEAK_CELL_TYPE:
      return CALL_APPLY(WeakCell);
    case JS_WEAK_REF_TYPE:
      return CALL_APPLY(JSWeakRef);
    case JS_PROXY_TYPE:
      return CALL_APPLY(JSProxy);
    case JS_ATOMICS_MUTEX_TYPE:
    case JS_ATOMICS_CONDITION_TYPE:
      return CALL_APPLY(JSSynchronizationPrimitive);
    case FOREIGN_TYPE:
      return CALL_APPLY(Foreign);
    case MAP_TYPE:
      return CALL_APPLY(Map);
    case CELL_TYPE:
      return CALL_APPLY(Cell);
    case PROPERTY_CELL_TYPE:
      return CALL_APPLY(PropertyCell);
    case CONST_TRACKING_LET_CELL_TYPE:
      return CALL_APPLY(ConstTrackingLetCell);
    case SYMBOL_TYPE:
      return CALL_APPLY(Symbol);
    case SMALL_ORDERED_HASH_SET_TYPE:
      return CALL_APPLY(SmallOrderedHashTable<SmallOrderedHashSet>);
    case SMALL_ORDERED_HASH_MAP_TYPE:
      return CALL_APPLY(SmallOrderedHashTable<SmallOrderedHashMap>);
    case SMALL_ORDERED_NAME_DICTIONARY_TYPE:
      return CALL_APPLY(SmallOrderedHashTable<SmallOrderedNameDictionary>);
    case SWISS_NAME_DICTIONARY_TYPE:
      return CALL_APPLY(SwissNameDictionary);
    case PREPARSE_DATA_TYPE:
      return CALL_APPLY(PreparseData);
    case SHARED_FUNCTION_INFO_TYPE:
      return CALL_APPLY(SharedFunctionInfo);
    case HEAP_NUMBER_TYPE:
      return CALL_APPLY(HeapNumber);
    case EXTERNAL_POINTER_ARRAY_TYPE:
      return CALL_APPLY(ExternalPointerArray);
    case BIGINT_TYPE:
      return CALL_APPLY(BigInt);
    case ALLOCATION_SITE_TYPE:
      return CALL_APPLY(AllocationSite);
    case ODDBALL_TYPE:
      return CALL_APPLY(Oddball);
    case HOLE_TYPE:
      return CALL_APPLY(Hole);
    case JS_REG_EXP_TYPE:
      return CALL_APPLY(JSRegExp);

#define MAKE_STRUCT_CASE(TYPE, Name, name) \
  case TYPE:                               \
    return CALL_APPLY(Name);
      STRUCT_LIST(MAKE_STRUCT_CASE)
#undef MAKE_STRUCT_CASE
    case ACCESSOR_INFO_TYPE:
      return CALL_APPLY(AccessorInfo);
    case FUNCTION_TEMPLATE_INFO_TYPE:
      return CALL_APPLY(FunctionTemplateInfo);
    case OBJECT_TEMPLATE_INFO_TYPE:
      return CALL_APPLY(ObjectTemplateInfo);
    case LOAD_HANDLER_TYPE:
      return CALL_APPLY(LoadHandler);
    case STORE_HANDLER_TYPE:
      return CALL_APPLY(StoreHandler);
    case SOURCE_TEXT_MODULE_TYPE:
      return CALL_APPLY(SourceTextModule);
    case SYNTHETIC_MODULE_TYPE:
      return CALL_APPLY(SyntheticModule);
// TODO(turbofan): Avoid duplicated cases when the body descriptors are
// identical.
#define MAKE_TORQUE_BODY_DESCRIPTOR_APPLY(TYPE, TypeName) \
  case TYPE:                                              \
    return CALL_APPLY(TypeName);
      TORQUE_INSTANCE_TYPE_TO_BODY_DESCRIPTOR_LIST(
          MAKE_TORQUE_BODY_DESCRIPTOR_APPLY)
#undef MAKE_TORQUE_BODY_DESCRIPTOR_APPLY

#define MAKE_TRUSTED_OBJECT_CASE(TypeCamelCase, TYPE_UPPER_CASE) \
  case TYPE_UPPER_CASE##_TYPE:                                   \
    return CALL_APPLY_TRUSTED(TypeCamelCase);
      CONCRETE_TRUSTED_OBJECT_TYPE_LIST2(MAKE_TRUSTED_OBJECT_CASE)
#undef MAKE_TRUSTED_OBJECT_CASE

    case FILLER_TYPE:
      return Op::template apply<FreeSpaceFillerBodyDescriptor, false>(
          std::forward<Args>(args)...);

    case FREE_SPACE_TYPE:
      return CALL_APPLY(FreeSpace);

    default:
      PrintF("Unknown type: %d\n", type);
      UNREACHABLE();
  }
#undef CALL_APPLY
}

template <typename ObjectVisitor>
void HeapObject::IterateFast(PtrComprCageBase cage_base, ObjectVisitor* v) {
  v->VisitMapPointer(*this);
  IterateBodyFast(cage_base, v);
}

template <typename ObjectVisitor>
void HeapObject::IterateFast(Tagged<Map> map, ObjectVisitor* v) {
  v->VisitMapPointer(*this);
  IterateBodyFast(map, SizeFromMap(map), v);
}

template <typename ObjectVisitor>
void HeapObject::IterateFast(Tagged<Map> map, int object_size,
                             ObjectVisitor* v) {
  v->VisitMapPointer(*this);
  IterateBodyFast(map, object_size, v);
}

template <typename ObjectVisitor>
void HeapObject::IterateBodyFast(PtrComprCageBase cage_base, ObjectVisitor* v) {
  Tagged<Map> m = map(cage_base);
  IterateBodyFast(m, SizeFromMap(m), v);
}

struct CallIterateBody {
  template <typename BodyDescriptor, bool kIsTrusted, typename ObjectVisitor>
  static void apply(Tagged<Map> map, Tagged<HeapObject> obj, int object_size,
                    ObjectVisitor* v) {
    if constexpr (kIsTrusted) {
      DCHECK(IsTrustedObject(obj));
      // This CHECK defends against an attacker crafting fake trusted objects
      // inside the sandbox. See also crbug.com/c/1505089 for more details.
      SBXCHECK(OutsideSandboxOrInReadonlySpace(obj));
    } else {
      // If this DCHECK fails, it means that a trusted object was not added to
      // the HEAP_OBJECT_TRUSTED_TYPE_LIST.
      // Note: This would normally be just !IsTrustedObject(obj), however we
      // might see trusted objects here before they've been migrated to trusted
      // space, hence the second condition.
      DCHECK(!IsTrustedObject(obj) || !IsTrustedSpaceObject(obj));
    }
    BodyDescriptor::IterateBody(map, obj, object_size, v);
  }
};

template <typename ObjectVisitor>
void HeapObject::IterateBodyFast(Tagged<Map> map, int object_size,
                                 ObjectVisitor* v) {
  BodyDescriptorApply<CallIterateBody>(map->instance_type(), map, *this,
                                       object_size, v);
}

class EphemeronHashTable::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    int entries_start = EphemeronHashTable::kHeaderSize +
                        EphemeronHashTable::kElementsStartIndex * kTaggedSize;
    IteratePointers(obj, EphemeronHashTable::kHeaderSize, entries_start, v);
    Tagged<EphemeronHashTable> table = UncheckedCast<EphemeronHashTable>(obj);
    for (InternalIndex i : table->IterateEntries()) {
      const int key_index = EphemeronHashTable::EntryToIndex(i);
      const int value_index = EphemeronHashTable::EntryToValueIndex(i);
      IterateEphemeron(obj, i.as_int(), OffsetOfElementAt(key_index),
                       OffsetOfElementAt(value_index), v);
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return object->SizeFromMap(map);
  }
};

class AccessorInfo::BodyDescriptor final : public BodyDescriptorBase {
 public:
  static_assert(AccessorInfo::kEndOfStrongFieldsOffset ==
                AccessorInfo::kMaybeRedirectedGetterOffset);
  static_assert(AccessorInfo::kMaybeRedirectedGetterOffset <
                AccessorInfo::kSetterOffset);
  static_assert(AccessorInfo::kSetterOffset < AccessorInfo::kFlagsOffset);
  static_assert(AccessorInfo::kFlagsOffset < AccessorInfo::kSize);

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, HeapObject::kHeaderSize,
                    AccessorInfo::kEndOfStrongFieldsOffset, v);
    v->VisitExternalPointer(obj, obj->RawExternalPointerField(
                                     AccessorInfo::kMaybeRedirectedGetterOffset,
                                     kAccessorInfoGetterTag));
    v->VisitExternalPointer(
        obj, obj->RawExternalPointerField(AccessorInfo::kSetterOffset,
                                          kAccessorInfoSetterTag));
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return kSize;
  }
};

class FunctionTemplateInfo::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, HeapObject::kHeaderSize,
                    FunctionTemplateInfo::kEndOfStrongFieldsOffset, v);
    v->VisitExternalPointer(
        obj, obj->RawExternalPointerField(
                 FunctionTemplateInfo::kMaybeRedirectedCallbackOffset,
                 kFunctionTemplateInfoCallbackTag));
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return kSize;
  }
};

// TODO(jgruber): Combine these into generic Suffix descriptors.
class FixedArray::BodyDescriptor final
    : public SuffixRangeBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<FixedArray>(raw_object)->AllocatedSize();
  }
};

class TrustedFixedArray::BodyDescriptor final
    : public SuffixRangeBodyDescriptor<TrustedObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<TrustedFixedArray>(raw_object)->AllocatedSize();
  }
};

class ProtectedFixedArray::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    for (int offset = kHeaderSize; offset < object_size;
         offset += kTaggedSize) {
      IterateProtectedPointer(obj, offset, v);
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<ProtectedFixedArray>(raw_object)->AllocatedSize();
  }
};

class SloppyArgumentsElements::BodyDescriptor final
    : public SuffixRangeBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<SloppyArgumentsElements>(raw_object)->AllocatedSize();
  }
};

class RegExpMatchInfo::BodyDescriptor final
    : public SuffixRangeBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<RegExpMatchInfo>(raw_object)->AllocatedSize();
  }
};

class ArrayList::BodyDescriptor final
    : public SuffixRangeBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<ArrayList>(raw_object)->AllocatedSize();
  }
};

class ObjectBoilerplateDescription::BodyDescriptor final
    : public SuffixRangeBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<ObjectBoilerplateDescription>(raw_object)
        ->AllocatedSize();
  }
};

class FeedbackCell::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointer(obj, kValueOffset, v);

#ifdef V8_ENABLE_LEAPTIERING
    IterateJSDispatchEntry(obj, kDispatchHandleOffset, v);
#endif
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return kAlignedSize;
  }
};

class ClosureFeedbackCellArray::BodyDescriptor final
    : public SuffixRangeBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<ClosureFeedbackCellArray>(raw_object)->AllocatedSize();
  }
};

class ScriptContextTable::BodyDescriptor final
    : public SuffixRangeBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<ScriptContextTable>(raw_object)->AllocatedSize();
  }
};

class WeakFixedArray::BodyDescriptor final
    : public SuffixRangeWeakBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<WeakFixedArray>(raw_object)->AllocatedSize();
  }
};

class TrustedWeakFixedArray::BodyDescriptor final
    : public SuffixRangeWeakBodyDescriptor<HeapObject::kHeaderSize> {
 public:
  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<TrustedWeakFixedArray>(raw_object)->AllocatedSize();
  }
};

#include "torque-generated/objects-body-descriptors-inl.inc"

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_INL_H_
