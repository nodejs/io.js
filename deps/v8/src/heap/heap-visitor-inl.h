// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_VISITOR_INL_H_
#define V8_HEAP_HEAP_VISITOR_INL_H_

#include <optional>

#include "src/base/logging.h"
#include "src/execution/local-isolate.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/heap-visitor.h"
#include "src/heap/mark-compact.h"
#include "src/heap/object-lock-inl.h"
#include "src/objects/arguments.h"
#include "src/objects/data-handler-inl.h"
#include "src/objects/free-space-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-weak-refs-inl.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/map.h"
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
constexpr bool SupportsRightTrim() {
  switch (visitor_id) {
    case kVisitFixedArray:
    case kVisitFixedDoubleArray:
    case kVisitWeakFixedArray:
      return true;
    default:
      return false;
  }
  UNREACHABLE();
}

template <VisitorId visitor_id>
inline bool ContainsReadOnlyMap(PtrComprCageBase, Tagged<HeapObject>) {
  return false;
}

#define DEFINE_READ_ONLY_MAP_SPECIALIZATION(VisitorIdType)                    \
  template <>                                                                 \
  inline bool ContainsReadOnlyMap<VisitorId::kVisit##VisitorIdType>(          \
      PtrComprCageBase cage_base, Tagged<HeapObject> object) {                \
    /* If you see this DCHECK fail we encountered a Map with a VisitorId that \
     * should have only ever appeared in read-only space. */                  \
    DCHECK(HeapLayout::InReadOnlySpace(object->map(cage_base)));              \
    return true;                                                              \
  }
VISITOR_IDS_WITH_READ_ONLY_MAPS_LIST(DEFINE_READ_ONLY_MAP_SPECIALIZATION)
#undef DEFINE_READ_ONLY_MAP_SPECIALIZATION

template <typename ConcreteVisitor>
HeapVisitor<ConcreteVisitor>::HeapVisitor(LocalIsolate* isolate)
    : ObjectVisitorWithCageBases(PtrComprCageBase(isolate->cage_base()),
                                 PtrComprCageBase(isolate->code_cage_base())),
      heap_(isolate->heap()->heap()) {}

template <typename ConcreteVisitor>
HeapVisitor<ConcreteVisitor>::HeapVisitor(Isolate* isolate)
    : ObjectVisitorWithCageBases(isolate), heap_(isolate->heap()) {}

template <typename ConcreteVisitor>
HeapVisitor<ConcreteVisitor>::HeapVisitor(Heap* heap)
    : ObjectVisitorWithCageBases(heap), heap_(heap) {}

template <typename ConcreteVisitor>
template <typename T>
Tagged<T> HeapVisitor<ConcreteVisitor>::Cast(Tagged<HeapObject> object,
                                             const Heap* heap) {
  if constexpr (ConcreteVisitor::ShouldUseUncheckedCast()) {
    return i::UncheckedCast<T>(object);
  }
  return i::Cast<T>(object);
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::Visit(Tagged<HeapObject> object)
  requires(!ConcreteVisitor::UsePrecomputedObjectSize())
{
  return Visit(object->map(cage_base()), object);
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::Visit(Tagged<Map> map,
                                           Tagged<HeapObject> object)
  requires(!ConcreteVisitor::UsePrecomputedObjectSize())
{
  return Visit(map, object, MaybeObjectSize());
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::Visit(Tagged<Map> map,
                                           Tagged<HeapObject> object,
                                           int object_size)
  requires(ConcreteVisitor::UsePrecomputedObjectSize())
{
  return Visit(map, object, MaybeObjectSize(object_size));
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::Visit(Tagged<Map> map,
                                           Tagged<HeapObject> object,
                                           MaybeObjectSize maybe_object_size) {
  if constexpr (ConcreteVisitor::UsePrecomputedObjectSize()) {
    DCHECK_EQ(maybe_object_size.AssumeSize(), object->SizeFromMap(map));
    static_assert(!ConcreteVisitor::EnableConcurrentVisitation());
  } else {
    DCHECK(maybe_object_size.IsNone());
  }

  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  switch (map->visitor_id()) {
#define CASE(TypeName)                                                        \
  case kVisit##TypeName:                                                      \
    /* If this DCHECK fails, it means that the object type wasn't added       \
     * to the TRUSTED_VISITOR_ID_LIST.                                        \
     * Note: This would normally be just !IsTrustedObject(obj), however we    \
     * might see trusted objects here before they've been migrated to trusted \
     * space, hence the second condition. */                                  \
    DCHECK(!InstanceTypeChecker::IsTrustedObject(map) ||                      \
           !HeapLayout::InTrustedSpace(object));                              \
    return visitor->Visit##TypeName(                                          \
        map, ConcreteVisitor::template Cast<TypeName>(object, heap_),         \
        maybe_object_size);
    TYPED_VISITOR_ID_LIST(CASE)
    TYPED_VISITOR_WITH_SLACK_ID_LIST(CASE)
    TORQUE_VISITOR_ID_LIST(CASE)
#undef CASE
#define CASE(TypeName)                                                     \
  case kVisit##TypeName:                                                   \
    DCHECK(InstanceTypeChecker::IsTrustedObject(map));                     \
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
        map, ConcreteVisitor::template Cast<TypeName>(object, heap_),      \
        maybe_object_size);
    TRUSTED_VISITOR_ID_LIST(CASE)
#undef CASE
    case kVisitShortcutCandidate:
      return visitor->VisitShortcutCandidate(
          map, ConcreteVisitor::template Cast<ConsString>(object, heap_),
          maybe_object_size);
    case kVisitJSObjectFast:
      return visitor->VisitJSObjectFast(
          map, ConcreteVisitor::template Cast<JSObject>(object, heap_),
          maybe_object_size);
    case kVisitJSApiObject:
      return visitor->VisitJSApiObject(
          map, ConcreteVisitor::template Cast<JSObject>(object, heap_),
          maybe_object_size);
    case kVisitStruct:
      return visitor->VisitStruct(map, object, maybe_object_size);
    case kVisitFiller:
      return visitor->VisitFiller(map, object, maybe_object_size);
    case kVisitFreeSpace:
      return visitor->VisitFreeSpace(map, i::Cast<FreeSpace>(object),
                                     maybe_object_size);
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
}

template <typename ConcreteVisitor>
template <VisitorId visitor_id>
void HeapVisitor<ConcreteVisitor>::VisitMapPointerIfNeeded(
    Tagged<HeapObject> host) {
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

#define VISIT(TypeName)                                                 \
  template <typename ConcreteVisitor>                                   \
  size_t HeapVisitor<ConcreteVisitor>::Visit##TypeName(                 \
      Tagged<Map> map, Tagged<TypeName> object,                         \
      MaybeObjectSize maybe_object_size) {                              \
    return static_cast<ConcreteVisitor*>(this)                          \
        ->template VisitWithBodyDescriptor<                             \
            VisitorId::kVisit##TypeName, TypeName,                      \
            ObjectTraits<TypeName>::BodyDescriptor>(map, object,        \
                                                    maybe_object_size); \
  }

TYPED_VISITOR_ID_LIST(VISIT)
TORQUE_VISITOR_ID_LIST(VISIT)
TRUSTED_VISITOR_ID_LIST(VISIT)
#undef VISIT

#define VISIT_WITH_SLACK(TypeName)                                            \
  template <typename ConcreteVisitor>                                         \
  size_t HeapVisitor<ConcreteVisitor>::Visit##TypeName(                       \
      Tagged<Map> map, Tagged<TypeName> object,                               \
      MaybeObjectSize maybe_object_size) {                                    \
    return static_cast<ConcreteVisitor*>(this)                                \
        ->template VisitJSObjectSubclass<TypeName, TypeName::BodyDescriptor>( \
            map, object, maybe_object_size);                                  \
  }

TYPED_VISITOR_WITH_SLACK_ID_LIST(VISIT_WITH_SLACK)
#undef VISIT_WITH_SLACK

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::VisitShortcutCandidate(
    Tagged<Map> map, Tagged<ConsString> object,
    MaybeObjectSize maybe_object_size) {
  return static_cast<ConcreteVisitor*>(this)->VisitConsString(
      map, object, maybe_object_size);
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::VisitFiller(
    Tagged<Map> map, Tagged<HeapObject> object,
    MaybeObjectSize maybe_object_size) {
  if constexpr (!ConcreteVisitor::CanEncounterFillerOrFreeSpace()) {
    UNREACHABLE();
  }
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  visitor->template VisitMapPointerIfNeeded<VisitorId::kVisitFiller>(object);
  return ConcreteVisitor::UsePrecomputedObjectSize()
             ? maybe_object_size.AssumeSize()
             : map->instance_size();
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::VisitFreeSpace(
    Tagged<Map> map, Tagged<FreeSpace> object,
    MaybeObjectSize maybe_object_size) {
  if constexpr (!ConcreteVisitor::CanEncounterFillerOrFreeSpace()) {
    UNREACHABLE();
  }
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  visitor->template VisitMapPointerIfNeeded<VisitorId::kVisitFreeSpace>(object);
  return object->size(kRelaxedLoad);
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::VisitJSObjectFast(
    Tagged<Map> map, Tagged<JSObject> object,
    MaybeObjectSize maybe_object_size) {
  return static_cast<ConcreteVisitor*>(this)
      ->template VisitJSObjectSubclass<JSObject, JSObject::FastBodyDescriptor>(
          map, object, maybe_object_size);
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::VisitJSApiObject(
    Tagged<Map> map, Tagged<JSObject> object,
    MaybeObjectSize maybe_object_size) {
  return static_cast<ConcreteVisitor*>(this)
      ->template VisitJSObjectSubclass<
          JSObject, JSAPIObjectWithEmbedderSlots::BodyDescriptor>(
          map, object, maybe_object_size);
}

template <typename ConcreteVisitor>
size_t HeapVisitor<ConcreteVisitor>::VisitStruct(
    Tagged<Map> map, Tagged<HeapObject> object,
    MaybeObjectSize maybe_object_size) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  int size = ConcreteVisitor::UsePrecomputedObjectSize()
                 ? static_cast<int>(maybe_object_size.AssumeSize())
                 : map->instance_size();
  visitor->template VisitMapPointerIfNeeded<VisitorId::kVisitStruct>(object);
  StructBodyDescriptor::IterateBody(map, object, size, visitor);
  return size;
}

template <typename ConcreteVisitor>
template <typename T, typename TBodyDescriptor>
size_t HeapVisitor<ConcreteVisitor>::VisitJSObjectSubclass(
    Tagged<Map> map, Tagged<T> object, MaybeObjectSize maybe_object_size) {
  // JSObject types are subject to slack tracking. At the end of slack tracking
  // a Map's instance size is adjusted properly. Since this changes the instance
  // size, we cannot DCHECK that `SizeFromMap()` is consistent with
  // `TBodyDescriptor::SizeOf()` as that would require taking a snapshot of the
  // Map.

  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  visitor->template VisitMapPointerIfNeeded<VisitorId::kVisitJSObject>(object);

  const size_t size = ConcreteVisitor::UsePrecomputedObjectSize()
                          ? maybe_object_size.AssumeSize()
                          : TBodyDescriptor::SizeOf(map, object);

  int visitation_size = static_cast<int>(size);

  if (!ConcreteVisitor::ShouldVisitFullJSObject()) {
    // It is important to visit only the used field and ignore the slack fields
    // because the slack fields may be trimmed concurrently and we don't want to
    // find fillers (slack) during pointer visitation.
    const int used_size = map->UsedInstanceSize();
    DCHECK_LE(used_size, size);
    DCHECK_GE(used_size, JSObject::GetHeaderSize(map));

    visitation_size = used_size;
  }

  TBodyDescriptor::IterateBody(map, object, visitation_size, visitor);

  return size;
}

template <typename ConcreteVisitor>
template <VisitorId visitor_id, typename T, typename TBodyDescriptor>
size_t HeapVisitor<ConcreteVisitor>::VisitWithBodyDescriptor(
    Tagged<Map> map, Tagged<T> object, MaybeObjectSize maybe_object_size) {
  // If you see the following DCHECK fail, then the size computation of
  // BodyDescriptor doesn't match the size return via obj.Size(). This is
  // problematic as the GC requires those sizes to match for accounting reasons.
  // The fix likely involves adding a padding field in the object definitions.
  //
  // We can only perform this check for types that do not support right trimming
  // when running concurrently. `RefineAllocatedBytesAfterSweeping()` ensures
  // that we only see sizes that get smaller during marking.
#ifdef DEBUG
  if (!SupportsRightTrim<visitor_id>() ||
      !ConcreteVisitor::EnableConcurrentVisitation()) {
    DCHECK_EQ(object->SizeFromMap(map), TBodyDescriptor::SizeOf(map, object));
  }
#endif  // DEBUG
  DCHECK(!map->IsInobjectSlackTrackingInProgress());

  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  visitor->template VisitMapPointerIfNeeded<visitor_id>(object);
  const int size = ConcreteVisitor::UsePrecomputedObjectSize()
                       ? static_cast<int>(maybe_object_size.AssumeSize())
                       : TBodyDescriptor::SizeOf(map, object);
  TBodyDescriptor::IterateBody(map, object, size, visitor);
  return size;
}

template <typename ConcreteVisitor>
template <typename TSlot>
std::optional<Tagged<Object>>
HeapVisitor<ConcreteVisitor>::GetObjectFilterReadOnlyAndSmiFast(
    TSlot slot) const {
  auto raw = slot.Relaxed_Load_Raw();
  // raw is either Tagged_t or Address depending on the slot type. Both can be
  // cast to Tagged_t for the fast check.
  if (FastInReadOnlySpaceOrSmallSmi(static_cast<Tagged_t>(raw))) {
    return std::nullopt;
  }
  return TSlot::RawToTagged(ObjectVisitorWithCageBases::cage_base(), raw);
}

template <typename ConcreteVisitor>
ConcurrentHeapVisitor<ConcreteVisitor>::ConcurrentHeapVisitor(Isolate* isolate)
    : HeapVisitor<ConcreteVisitor>(isolate) {}

template <typename T>
struct ConcurrentVisitorCastHelper {
  static V8_INLINE Tagged<T> Cast(Tagged<HeapObject> object) {
    return i::Cast<T>(object);
  }
};

#define UNCHECKED_CAST(VisitorId, TypeName)                               \
  template <>                                                             \
  V8_INLINE Tagged<TypeName> ConcurrentVisitorCastHelper<TypeName>::Cast( \
      Tagged<HeapObject> object) {                                        \
    return UncheckedCast<TypeName>(object);                               \
  }
SAFE_STRING_TRANSITION_SOURCES(UNCHECKED_CAST)
// Casts are also needed for unsafe ones for the initial dispatch in
// HeapVisitor.
UNSAFE_STRING_TRANSITION_SOURCES(UNCHECKED_CAST)
#undef UNCHECKED_CAST

template <typename ConcreteVisitor>
template <typename T>
Tagged<T> ConcurrentHeapVisitor<ConcreteVisitor>::Cast(
    Tagged<HeapObject> object, const Heap* heap) {
  if constexpr (ConcreteVisitor::EnableConcurrentVisitation()) {
    return ConcurrentVisitorCastHelper<T>::Cast(object);
  }
  return i::Cast<T>(object);
}

#define VISIT_AS_LOCKED_STRING(VisitorId, TypeName)                          \
  template <typename ConcreteVisitor>                                        \
  size_t ConcurrentHeapVisitor<ConcreteVisitor>::Visit##TypeName(            \
      Tagged<Map> map, Tagged<TypeName> object,                              \
      MaybeObjectSize maybe_object_size) {                                   \
    if constexpr (ConcreteVisitor::EnableConcurrentVisitation()) {           \
      return VisitStringLocked(object);                                      \
    }                                                                        \
    return HeapVisitor<ConcreteVisitor>::Visit##TypeName(map, object,        \
                                                         maybe_object_size); \
  }

UNSAFE_STRING_TRANSITION_SOURCES(VISIT_AS_LOCKED_STRING)
#undef VISIT_AS_LOCKED_STRING

template <typename ConcreteVisitor>
template <typename T>
size_t ConcurrentHeapVisitor<ConcreteVisitor>::VisitStringLocked(
    Tagged<T> object) {
  ConcreteVisitor* visitor = static_cast<ConcreteVisitor*>(this);
  ObjectLockGuard guard(object);
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
        map, UncheckedCast<TypeName>(object), size, visitor);                 \
    break;

    UNSAFE_STRING_TRANSITION_TARGETS(UNSAFE_STRING_TRANSITION_TARGET_CASE)
#undef UNSAFE_STRING_TRANSITION_TARGET_CASE
    default:
      UNREACHABLE();
  }
  return size;
}

template <typename ConcreteVisitor>
NewSpaceVisitor<ConcreteVisitor>::NewSpaceVisitor(Isolate* isolate)
    : ConcurrentHeapVisitor<ConcreteVisitor>(isolate) {}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_VISITOR_INL_H_
