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
#include "src/heap/heap-layout-inl.h"
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
#include "src/objects/tagged-field.h"
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
                 UncheckedCast<JSObject>(obj)->GetEmbedderFieldCount(map) == 0);
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
  JSDispatchHandle handle(
      obj->Relaxed_ReadField<JSDispatchHandle::underlying_type>(offset));
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
    // If new pointers are added to RegExpData, make sure to also add them to
    // the subclasses descriptors (AtomRegExpData and IrRegExpData).
    // We don't directly call the base class IterateBody, as in the future
    // the subclasses will have a different indirect pointer tag from the base
    // class (once inheritance hierarchies are supported for indirect pointer
    // tags).
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

    IterateProtectedPointer(obj, kLatin1BytecodeOffset, v);
    IterateProtectedPointer(obj, kUc16BytecodeOffset, v);
    IterateCodePointer(obj, kLatin1CodeOffset, v, IndirectPointerMode::kStrong);
    IterateCodePointer(obj, kUc16CodeOffset, v, IndirectPointerMode::kStrong);
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
#ifdef V8_ENABLE_LEAPTIERING
  static const int kCodeFieldOffset = JSFunction::kDispatchHandleOffset;
#else
  static const int kCodeFieldOffset = JSFunction::kCodeOffset;
#endif

  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    // Iterate JSFunction header fields first.
    int header_size = JSFunction::GetHeaderSize(map->has_prototype_slot());
    DCHECK_GE(object_size, header_size);
    IteratePointers(obj, kStartOffset, kCodeFieldOffset, v);

#ifdef V8_ENABLE_LEAPTIERING
    IterateJSDispatchEntry(obj, kDispatchHandleOffset, v);
#else
    // The code field is treated as a custom weak pointer. This field
    // is visited as a weak pointer if the Code is baseline code
    // and the bytecode array corresponding to this function is old. In the rest
    // of the cases this field is treated as strong pointer.
    // See MarkingVisitorBase::VisitJSFunction.
    IterateCodePointer(obj, kCodeOffset, v, IndirectPointerMode::kCustom);
    DCHECK_GE(header_size, kCodeOffset);
#endif  // V8_ENABLE_LEAPTIERING

    // Iterate rest of the header fields
    IteratePointers(obj, kCodeFieldOffset + kTaggedSize, header_size, v);
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
    IteratePointers(obj, kSupertypesOffset, SizeOf(map, obj), v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return kSupertypesOffset +
           Cast<WasmTypeInfo>(object)->supertypes_length() * kTaggedSize;
  }
};

class WasmMemoryMapDescriptor::BodyDescriptor : public DataOnlyBodyDescriptor {
 public:
  static_assert(WasmMemoryMapDescriptor::kStartOfStrongFieldsOffset ==
                WasmMemoryMapDescriptor::kEndOfStrongFieldsOffset);

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
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
                    kTrustedDispatchTableOffset, v);
    IterateTrustedPointer(obj, kTrustedDispatchTableOffset, v,
                          IndirectPointerMode::kStrong,
                          kWasmDispatchTableIndirectPointerTag);
    IterateTrustedPointer(obj, kTrustedDataOffset, v,
                          IndirectPointerMode::kStrong,
                          kWasmTrustedInstanceDataIndirectPointerTag);
    IterateJSObjectBodyImpl(map, obj, kHeaderSize, object_size, v);
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
    IterateSelfIndirectPointer(obj, kWasmDispatchTableIndirectPointerTag, v);
    IterateProtectedPointer(obj, kProtectedOffheapDataOffset, v);
    IterateProtectedPointer(obj, kProtectedUsesOffset, v);
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
    if (!WasmArray::GcSafeElementType(map).is_reference()) return;
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
    const wasm::CanonicalStructType* type = WasmStruct::GcSafeType(map);
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

class WasmMemoryObject::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    IteratePointers(obj, JSObject::BodyDescriptor::kStartOffset,
                    kEndOfStrongFieldsOffset, v);
    IterateJSObjectBodyImpl(map, obj, kHeaderSize, object_size, v);
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> object) {
    return map->instance_size();
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

#ifdef V8_ENABLE_LEAPTIERING
    IterateJSDispatchEntry(obj, kDispatchHandleOffset, v);
#endif  // V8_ENABLE_LEAPTIERING

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

class EphemeronHashTable::BodyDescriptor final : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    int entries_start = EphemeronHashTable::OffsetOfElementAt(
        EphemeronHashTable::kElementsStartIndex);
    IteratePointers(obj, OFFSET_OF_DATA_START(EphemeronHashTable),
                    entries_start, v);
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
    for (int offset = OFFSET_OF_DATA_START(ProtectedFixedArray);
         offset < object_size; offset += kTaggedSize) {
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

class ProtectedWeakFixedArray::BodyDescriptor final
    : public BodyDescriptorBase {
 public:
  template <typename ObjectVisitor>
  static inline void IterateBody(Tagged<Map> map, Tagged<HeapObject> obj,
                                 int object_size, ObjectVisitor* v) {
    Tagged<TrustedObject> host = Cast<TrustedObject>(obj);
    for (int offset = OFFSET_OF_DATA_START(ProtectedWeakFixedArray);
         offset < object_size; offset += kTaggedSize) {
      v->VisitProtectedPointer(host,
                               host->RawProtectedMaybeObjectField(offset));
    }
  }

  static inline int SizeOf(Tagged<Map> map, Tagged<HeapObject> raw_object) {
    return UncheckedCast<ProtectedWeakFixedArray>(raw_object)->AllocatedSize();
  }
};

#include "torque-generated/objects-body-descriptors-inl.inc"

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_OBJECTS_BODY_DESCRIPTORS_INL_H_
