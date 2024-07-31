// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_OBJECTS_VISITING_INL_H_
#define V8_HEAP_OBJECTS_VISITING_INL_H_

#include "src/base/logging.h"
#include "src/heap/mark-compact.h"
#include "src/heap/object-lock.h"
#include "src/heap/objects-visiting.h"
#include "src/objects/arguments.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/free-space-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/module-inl.h"
#include "src/objects/objects-body-descriptors-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/ordered-hash-table.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/synthetic-module-inl.h"
#include "src/objects/torque-defined-classes.h"
#include "src/objects/visitors.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

template <VisitorId visitor_id>
inline bool ContainsReadOnlyMap(PtrComprCageBase, Tagged<HeapObject>) {
  return false;
}

// List of visitor ids that can only appear in read-only maps. Unfortunately,
// these are generally contained in all other lists. Adding an instance type
// here optimizes handling in visitors that do not need to Map objects with such
// visitor ids.
#define READ_ONLY_MAPS_VISITOR_ID_LIST(V) \
  V(AccessorInfo)                         \
  V(AllocationSite)                       \
  V(BigInt)                               \
  V(BytecodeArray)                        \
  V(BytecodeWrapper)                      \
  V(ByteArray)                            \
  V(Cell)                                 \
  V(Code)                                 \
  V(CodeWrapper)                          \
  V(DataHandler)                          \
  V(DataObject)                           \
  V(DescriptorArray)                      \
  V(EmbedderDataArray)                    \
  V(ExternalString)                       \
  V(FeedbackCell)                         \
  V(FeedbackMetadata)                     \
  V(FeedbackVector)                       \
  V(FixedArray)                           \
  V(FixedDoubleArray)                     \
  V(FunctionTemplateInfo)                 \
  V(InstructionStream)                    \
  V(PreparseData)                         \
  V(PropertyArray)                        \
  V(PropertyCell)                         \
  V(PrototypeInfo)                        \
  V(ScopeInfo)                            \
  V(SeqOneByteString)                     \
  V(SeqTwoByteString)                     \
  V(SharedFunctionInfo)                   \
  V(ShortcutCandidate)                    \
  V(SlicedString)                         \
  V(SloppyArgumentsElements)              \
  V(Symbol)                               \
  V(ThinString)                           \
  V(TransitionArray)                      \
  V(UncompiledDataWithoutPreparseData)    \
  V(UncompiledDataWithPreparseData)       \
  V(WeakArrayList)                        \
  V(WeakFixedArray)

#define DEFINE_READ_ONLY_MAP_SPECIALIZATION(VisitorIdType)                    \
  template <>                                                                 \
  inline bool ContainsReadOnlyMap<VisitorId::kVisit##VisitorIdType>(          \
      PtrComprCageBase cage_base, Tagged<HeapObject> object) {                \
    /* If you see this DCHECK fail we encountered a Map with a VisitorId that \
     * should have only ever appeared in read-only space. */                  \
    DCHECK(InReadOnlySpace(object->map(cage_base)));                          \
    return true;                                                              \
  }
READ_ONLY_MAPS_VISITOR_ID_LIST(DEFINE_READ_ONLY_MAP_SPECIALIZATION)
#undef DEFINE_READ_ONLY_MAP_SPECIALIZATION
#undef READ_ONLY_MAPS_VISITOR_ID_LIST

template <typename ResultType, typename ConcreteVisitor>
HeapVisitor<ResultType, ConcreteVisitor>::HeapVisitor(
    PtrComprCageBase cage_base, PtrComprCageBase code_cage_base)
    : ObjectVisitorWithCageBases(cage_base, code_cage_base) {}

template <typename ResultType, typename ConcreteVisitor>
HeapVisitor<ResultType, ConcreteVisitor>::HeapVisitor(Isolate* isolate)
    : ObjectVisitorWithCageBases(isolate) {}

template <typename ResultType, typename ConcreteVisitor>
HeapVisitor<ResultType, ConcreteVisitor>::HeapVisitor(Heap* heap)
    : ObjectVisitorWithCageBases(heap) {}

template <typename ResultType, typename ConcreteVisitor>
template <typename T>
Tagged<T> HeapVisitor<ResultType, ConcreteVisitor>::Cast(
    Tagged<HeapObject> object) {
  return T::cast(object);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::Visit(
    Tagged<HeapObject> object) {
  return Visit(object->map(cage_base()), object);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::Visit(
    Tagged<Map> map, Tagged<HeapObject> object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  switch (map->visitor_id()) {
#define CASE(TypeName)                                                        \
  case kVisit##TypeName:                                                      \
    /* If this DCHECK fails, it means that the object type wasn't added       \
     * to the TRUSTED_VISITOR_ID_LIST.                                        \
     * Note: This would normally be just !IsTrustedObject(obj), however we    \
     * might see trusted objects here before they've been migrated to trusted \
     * space, hence the second condition. */                                  \
    DCHECK(!IsTrustedObject(object) || !IsTrustedSpaceObject(object));        \
    return visitor->Visit##TypeName(                                          \
        map, ConcreteVisitor::template Cast<TypeName>(object));
    TYPED_VISITOR_ID_LIST(CASE)
    TORQUE_VISITOR_ID_LIST(CASE)
#undef CASE
#define CASE(TypeName)                                                     \
  case kVisit##TypeName:                                                   \
    DCHECK(IsTrustedObject(object));                                       \
    /* Trusted objects are protected from modifications by an attacker as  \
     * they are located outside of the sandbox. However, an attacker can   \
     * still craft their own fake trusted objects inside the sandbox. In   \
     * this case, bad things might happen if these objects are then        \
     * processed by e.g. an object visitor as they will typically assume   \
     * that these objects are trustworthy. The following check defends     \
     * against that by ensuring that the object is outside of the sandbox. \
     * See also crbug.com/c/1505089. */                                    \
    SBXCHECK(OutsideSandboxOrInReadonlySpace(object));                     \
    return visitor->Visit##TypeName(                                       \
        map, ConcreteVisitor::template Cast<TypeName>(object));
    TRUSTED_VISITOR_ID_LIST(CASE)
#undef CASE
    case kVisitShortcutCandidate:
      return visitor->VisitShortcutCandidate(
          map, ConcreteVisitor::template Cast<ConsString>(object));
    case kVisitDataObject:
      return visitor->VisitDataObject(map, object);
    case kVisitJSObjectFast:
      return visitor->VisitJSObjectFast(
          map, ConcreteVisitor::template Cast<JSObject>(object));
    case kVisitJSApiObject:
      return visitor->VisitJSApiObject(
          map, ConcreteVisitor::template Cast<JSObject>(object));
    case kVisitStruct:
      return visitor->VisitStruct(map, object);
    case kVisitFreeSpace:
      return visitor->VisitFreeSpace(map, FreeSpace::cast(object));
    case kDataOnlyVisitorIdCount:
    case kVisitorIdCount:
      UNREACHABLE();
  }
  // TODO(chromium:327992715): Remove once we have some clarity why execution
  // can reach this point.
  {
    Isolate* isolate;
    if (GetIsolateFromHeapObject(object, &isolate)) {
      isolate->PushParamsAndDie(
          reinterpret_cast<void*>(object.ptr()),
          reinterpret_cast<void*>(map.ptr()),
          reinterpret_cast<void*>(static_cast<intptr_t>(map->visitor_id())));
    }
  }
  UNREACHABLE();
  // Make the compiler happy.
  return ResultType();
}

template <typename ResultType, typename ConcreteVisitor>
template <VisitorId visitor_id>
void HeapVisitor<ResultType, ConcreteVisitor>::VisitMapPointerIfNeeded(
    Tagged<HeapObject> host) {
  DCHECK(!host->map_word(cage_base(), kRelaxedLoad).IsForwardingAddress());
  if constexpr (!ConcreteVisitor::ShouldVisitMapPointer()) {
    return;
  }
  if constexpr (!ConcreteVisitor::ShouldVisitReadOnlyMapPointer()) {
    if (ContainsReadOnlyMap<visitor_id>(cage_base(), host)) {
      return;
    }
  }
  static_cast<ConcreteVisitor*>(this)->VisitMapPointer(host);
}

#define VISIT(TypeName)                                                      \
  template <typename ResultType, typename ConcreteVisitor>                   \
  ResultType HeapVisitor<ResultType, ConcreteVisitor>::Visit##TypeName(      \
      Tagged<Map> map, Tagged<TypeName> object) {                            \
    ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);          \
    /* If you see the following DCHECK fail, then the size computation of    \
     * BodyDescriptor doesn't match the size return via obj.Size(). This is  \
     * problematic as the GC requires those sizes to match for accounting    \
     * reasons. The fix likely involves adding a padding field in the object \
     * defintions. */                                                        \
    DCHECK_EQ(object->SizeFromMap(map),                                      \
              ObjectTraits<TypeName>::BodyDescriptor::SizeOf(map, object));  \
    visitor->template VisitMapPointerIfNeeded<VisitorId::kVisit##TypeName>(  \
        object);                                                             \
    const int size =                                                         \
        ObjectTraits<TypeName>::BodyDescriptor::SizeOf(map, object);         \
    ObjectTraits<TypeName>::BodyDescriptor::IterateBody(map, object, size,   \
                                                        visitor);            \
    return static_cast<ResultType>(size);                                    \
  }

TYPED_VISITOR_ID_LIST(VISIT)
TORQUE_VISITOR_ID_LIST(VISIT)
TRUSTED_VISITOR_ID_LIST(VISIT)
#undef VISIT

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitShortcutCandidate(
    Tagged<Map> map, Tagged<ConsString> object) {
  return static_cast<ConcreteVisitor*>(this)->VisitConsString(map, object);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitDataObject(
    Tagged<Map> map, Tagged<HeapObject> object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  int size = map->instance_size();
  visitor->template VisitMapPointerIfNeeded<VisitorId::kVisitDataObject>(
      object);
#ifdef V8_ENABLE_SANDBOX
  // The following types have external pointers, which must be visited.
  // TODO(v8:10391) Consider adding custom visitor IDs for these and making
  // this block not depend on V8_ENABLE_SANDBOX.
  if (IsForeign(object, cage_base())) {
    Foreign::BodyDescriptor::IterateBody(map, object, size, visitor);
  }
#endif  // V8_ENABLE_SANDBOX
  return static_cast<ResultType>(size);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitJSObjectFast(
    Tagged<Map> map, Tagged<JSObject> object) {
  return VisitJSObjectSubclass<JSObject, JSObject::FastBodyDescriptor>(map,
                                                                       object);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitJSApiObject(
    Tagged<Map> map, Tagged<JSObject> object) {
  return VisitJSObjectSubclass<JSObject, JSObject::BodyDescriptor>(map, object);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitStruct(
    Tagged<Map> map, Tagged<HeapObject> object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  int size = map->instance_size();
  visitor->template VisitMapPointerIfNeeded<VisitorId::kVisitStruct>(object);
  StructBodyDescriptor::IterateBody(map, object, size, visitor);
  return static_cast<ResultType>(size);
}

template <typename ResultType, typename ConcreteVisitor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitFreeSpace(
    Tagged<Map> map, Tagged<FreeSpace> object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  visitor->template VisitMapPointerIfNeeded<VisitorId::kVisitFreeSpace>(object);
  return static_cast<ResultType>(object->size(kRelaxedLoad));
}

template <typename ResultType, typename ConcreteVisitor>
template <typename T, typename TBodyDescriptor>
ResultType HeapVisitor<ResultType, ConcreteVisitor>::VisitJSObjectSubclass(
    Tagged<Map> map, Tagged<T> object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  visitor->template VisitMapPointerIfNeeded<VisitorId::kVisitJSObject>(object);
  const int size = TBodyDescriptor::SizeOf(map, object);
  const int used_size = map->UsedInstanceSize();
  DCHECK_LE(used_size, size);
  DCHECK_GE(used_size, JSObject::GetHeaderSize(map));
  // It is important to visit only the used field and ignore the slack fields
  // because the slack fields may be trimmed concurrently. For non-concurrent
  // visitors this merely is an optimization in that we only visit the actually
  // used fields.
  TBodyDescriptor::IterateBody(map, object, used_size, visitor);
  return size;
}

template <typename ResultType, typename ConcreteVisitor>
ConcurrentHeapVisitor<ResultType, ConcreteVisitor>::ConcurrentHeapVisitor(
    Isolate* isolate)
    : HeapVisitor<ResultType, ConcreteVisitor>(isolate) {}

template <typename T>
struct ConcurrentVisitorCastHelper {
  static V8_INLINE Tagged<T> Cast(Tagged<HeapObject> object) {
    return T::cast(object);
  }
};

#define UNCHECKED_CAST(VisitorId, TypeName)                               \
  template <>                                                             \
  V8_INLINE Tagged<TypeName> ConcurrentVisitorCastHelper<TypeName>::Cast( \
      Tagged<HeapObject> object) {                                        \
    return TypeName::unchecked_cast(object);                              \
  }
SAFE_STRING_TRANSITION_SOURCES(UNCHECKED_CAST)
// Casts are also needed for unsafe ones for the initial dispatch in
// HeapVisitor.
UNSAFE_STRING_TRANSITION_SOURCES(UNCHECKED_CAST)
#undef UNCHECKED_CAST

template <typename ResultType, typename ConcreteVisitor>
template <typename T>
Tagged<T> ConcurrentHeapVisitor<ResultType, ConcreteVisitor>::Cast(
    Tagged<HeapObject> object) {
  if constexpr (ConcreteVisitor::EnableConcurrentVisitation()) {
    return ConcurrentVisitorCastHelper<T>::Cast(object);
  }
  return T::cast(object);
}

#define VISIT_AS_LOCKED_STRING(VisitorId, TypeName)                           \
  template <typename ResultType, typename ConcreteVisitor>                    \
  ResultType                                                                  \
      ConcurrentHeapVisitor<ResultType, ConcreteVisitor>::Visit##TypeName(    \
          Tagged<Map> map, Tagged<TypeName> object) {                         \
    if constexpr (ConcreteVisitor::EnableConcurrentVisitation()) {            \
      return VisitStringLocked(object);                                       \
    }                                                                         \
    return HeapVisitor<ResultType, ConcreteVisitor>::Visit##TypeName(map,     \
                                                                     object); \
  }

UNSAFE_STRING_TRANSITION_SOURCES(VISIT_AS_LOCKED_STRING)
#undef VISIT_AS_LOCKED_STRING

template <typename ResultType, typename ConcreteVisitor>
template <typename T>
ResultType
ConcurrentHeapVisitor<ResultType, ConcreteVisitor>::VisitStringLocked(
    Tagged<T> object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  SharedObjectLockGuard guard(object);
  // The object has been locked. At this point shared read access is
  // guaranteed but we must re-read the map and check whether the string has
  // transitioned.
  Tagged<Map> map = object->map();
  int size;
  switch (map->visitor_id()) {
#define UNSAFE_STRING_TRANSITION_TARGET_CASE(VisitorIdType, TypeName)         \
  case kVisit##VisitorIdType:                                                 \
    visitor                                                                   \
        ->template VisitMapPointerIfNeeded<VisitorId::kVisit##VisitorIdType>( \
            object);                                                          \
    size = ObjectTraits<TypeName>::BodyDescriptor::SizeOf(map, object);       \
    ObjectTraits<TypeName>::BodyDescriptor::IterateBody(                      \
        map, TypeName::unchecked_cast(object), size, visitor);                \
    break;

    UNSAFE_STRING_TRANSITION_TARGETS(UNSAFE_STRING_TRANSITION_TARGET_CASE)
#undef UNSAFE_STRING_TRANSITION_TARGET_CASE
    default:
      UNREACHABLE();
  }
  return static_cast<ResultType>(size);
}

template <typename ConcreteVisitor>
NewSpaceVisitor<ConcreteVisitor>::NewSpaceVisitor(Isolate* isolate)
    : ConcurrentHeapVisitor<int, ConcreteVisitor>(isolate) {}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_OBJECTS_VISITING_INL_H_
