// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_VISITOR_H_
#define V8_HEAP_HEAP_VISITOR_H_

#include "src/base/logging.h"
#include "src/execution/local-isolate.h"
#include "src/objects/bytecode-array.h"
#include "src/objects/contexts.h"
#include "src/objects/fixed-array.h"
#include "src/objects/js-weak-refs.h"
#include "src/objects/map.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/objects.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string.h"
#include "src/objects/visitors.h"

namespace v8 {
namespace internal {

// This class is used as argument to the HeapVisitor::Visit() method as a
// cheaper alternative to std::optional<size_t>.
class MaybeObjectSize final {
 public:
  explicit MaybeObjectSize(size_t size) : raw_size_(size) {
    DCHECK_GT(size, 0);
  }

  MaybeObjectSize() : raw_size_(0) {}

  size_t AssumeSize() const {
    DCHECK_GT(raw_size_, 0);
    return raw_size_;
  }

  bool IsNone() const { return raw_size_ == 0; }

 private:
  size_t raw_size_;
};

// Visitation in here will refer to BodyDescriptors with the regular instance
// size.
#define TYPED_VISITOR_ID_LIST(V)      \
  V(AccessorInfo)                     \
  V(AllocationSite)                   \
  V(BigInt)                           \
  V(BytecodeWrapper)                  \
  V(CallSiteInfo)                     \
  V(Cell)                             \
  V(CodeWrapper)                      \
  V(ConsString)                       \
  V(ContextSidePropertyCell)          \
  V(CoverageInfo)                     \
  V(DataHandler)                      \
  V(DebugInfo)                        \
  V(EmbedderDataArray)                \
  V(EphemeronHashTable)               \
  V(ExternalString)                   \
  V(FeedbackCell)                     \
  V(FeedbackMetadata)                 \
  V(Foreign)                          \
  V(FunctionTemplateInfo)             \
  V(HeapNumber)                       \
  V(Hole)                             \
  V(Map)                              \
  V(NativeContext)                    \
  V(Oddball)                          \
  V(PreparseData)                     \
  V(PropertyArray)                    \
  V(PropertyCell)                     \
  V(PrototypeInfo)                    \
  V(RegExpBoilerplateDescription)     \
  V(RegExpDataWrapper)                \
  V(SeqOneByteString)                 \
  V(SeqTwoByteString)                 \
  V(SharedFunctionInfo)               \
  V(SlicedString)                     \
  V(SloppyArgumentsElements)          \
  V(SmallOrderedHashMap)              \
  V(SmallOrderedHashSet)              \
  V(SmallOrderedNameDictionary)       \
  V(SourceTextModule)                 \
  V(SwissNameDictionary)              \
  V(Symbol)                           \
  V(SyntheticModule)                  \
  V(ThinString)                       \
  V(TransitionArray)                  \
  V(WeakCell)                         \
  IF_WASM(V, WasmArray)               \
  IF_WASM(V, WasmContinuationObject)  \
  IF_WASM(V, WasmFuncRef)             \
  IF_WASM(V, WasmMemoryMapDescriptor) \
  IF_WASM(V, WasmNull)                \
  IF_WASM(V, WasmResumeData)          \
  IF_WASM(V, WasmStruct)              \
  IF_WASM(V, WasmSuspenderObject)     \
  IF_WASM(V, WasmTypeInfo)            \
  SIMPLE_HEAP_OBJECT_LIST1(V)

// Visitation in here will refer to BodyDescriptors with the used size of the
// map. Slack will thus be ignored. We are not allowed to visit slack as that's
// visiting free space fillers.
#define TYPED_VISITOR_WITH_SLACK_ID_LIST(V) \
  V(JSArrayBuffer)                          \
  V(JSDataViewOrRabGsabDataView)            \
  V(JSDate)                                 \
  V(JSExternalObject)                       \
  V(JSFinalizationRegistry)                 \
  V(JSFunction)                             \
  V(JSObject)                               \
  V(JSRegExp)                               \
  V(JSSynchronizationPrimitive)             \
  V(JSTypedArray)                           \
  V(JSWeakCollection)                       \
  V(JSWeakRef)                              \
  IF_WASM(V, WasmGlobalObject)              \
  IF_WASM(V, WasmInstanceObject)            \
  IF_WASM(V, WasmMemoryObject)              \
  IF_WASM(V, WasmSuspendingObject)          \
  IF_WASM(V, WasmTableObject)               \
  IF_WASM(V, WasmTagObject)

// List of visitor ids that can only appear in read-only maps. Unfortunately,
// these are generally contained in all other lists.
//
// Adding an instance type here allows skipping vistiation of Map slots for
// visitors with `ShouldVisitReadOnlyMapPointer() == false`.
#define VISITOR_IDS_WITH_READ_ONLY_MAPS_LIST(V)           \
  /* All trusted objects have maps in read-only space. */ \
  CONCRETE_TRUSTED_OBJECT_TYPE_LIST1(V)                   \
  V(AccessorInfo)                                         \
  V(AllocationSite)                                       \
  V(BigInt)                                               \
  V(BytecodeWrapper)                                      \
  V(ByteArray)                                            \
  V(Cell)                                                 \
  V(CodeWrapper)                                          \
  V(DataHandler)                                          \
  V(DescriptorArray)                                      \
  V(EmbedderDataArray)                                    \
  V(ExternalString)                                       \
  V(FeedbackCell)                                         \
  V(FeedbackMetadata)                                     \
  V(FeedbackVector)                                       \
  V(Filler)                                               \
  V(FixedArray)                                           \
  V(FixedDoubleArray)                                     \
  V(FunctionTemplateInfo)                                 \
  V(FreeSpace)                                            \
  V(HeapNumber)                                           \
  V(PreparseData)                                         \
  V(PropertyArray)                                        \
  V(PropertyCell)                                         \
  V(PrototypeInfo)                                        \
  V(RegExpBoilerplateDescription)                         \
  V(RegExpDataWrapper)                                    \
  V(ScopeInfo)                                            \
  V(SeqOneByteString)                                     \
  V(SeqTwoByteString)                                     \
  V(SharedFunctionInfo)                                   \
  V(ShortcutCandidate)                                    \
  V(SlicedString)                                         \
  V(SloppyArgumentsElements)                              \
  V(Symbol)                                               \
  V(ThinString)                                           \
  V(TransitionArray)                                      \
  V(WeakArrayList)                                        \
  V(WeakFixedArray)

#define FORWARD_DECLARE(TypeName) class TypeName;
TYPED_VISITOR_ID_LIST(FORWARD_DECLARE)
TYPED_VISITOR_WITH_SLACK_ID_LIST(FORWARD_DECLARE)
TORQUE_VISITOR_ID_LIST(FORWARD_DECLARE)
TRUSTED_VISITOR_ID_LIST(FORWARD_DECLARE)
#undef FORWARD_DECLARE

// The base class for visitors that need to dispatch on object type. The default
// behavior of all visit functions is to iterate body of the given object using
// the BodyDescriptor of the object.
//
// The visit functions return the size of the object.
//
// This class is intended to be used in the following way:
//
//   class SomeVisitor : public HeapVisitor<SomeVisitor> {
//     ...
//   }
template <typename ConcreteVisitor>
class HeapVisitor : public ObjectVisitorWithCageBases {
 public:
  inline explicit HeapVisitor(LocalIsolate* isolate);
  inline explicit HeapVisitor(Isolate* isolate);
  inline explicit HeapVisitor(Heap* heap);

  V8_INLINE size_t Visit(Tagged<HeapObject> object)
    requires(!ConcreteVisitor::UsePrecomputedObjectSize());

  V8_INLINE size_t Visit(Tagged<Map> map, Tagged<HeapObject> object)
    requires(!ConcreteVisitor::UsePrecomputedObjectSize());

  V8_INLINE size_t Visit(Tagged<Map> map, Tagged<HeapObject> object,
                         int object_size)
    requires(ConcreteVisitor::UsePrecomputedObjectSize());

 protected:
  V8_INLINE size_t Visit(Tagged<Map> map, Tagged<HeapObject> object,
                         MaybeObjectSize maybe_object_size);

  // If this predicate returns false the default implementations of Visit*
  // functions bail out from visiting the map pointer.
  V8_INLINE static constexpr bool ShouldVisitMapPointer() { return true; }
  // If this predicate returns false the default implementations of Visit*
  // functions bail out from visiting known read-only maps.
  V8_INLINE static constexpr bool ShouldVisitReadOnlyMapPointer() {
    return true;
  }
  // If this predicate returns false the default implementation of
  // `VisitFiller()` and `VisitFreeSpace()` will be unreachable.
  V8_INLINE static constexpr bool CanEncounterFillerOrFreeSpace() {
    return true;
  }
  // If this predicate returns false the default implementation of
  // `VisitFiller()` and `VisitFreeSpace()` will be unreachable.
  V8_INLINE static constexpr bool ShouldUseUncheckedCast() { return false; }

  // This should really only be defined and used in ConcurrentHeapVisitor but we
  // need it here for a DCHECK in HeapVisitor::VisitWithBodyDescriptor.
  V8_INLINE static constexpr bool EnableConcurrentVisitation() { return false; }

  // Avoids size computation in visitors and uses the input argument instead.
  V8_INLINE static constexpr bool UsePrecomputedObjectSize() { return false; }

  // Only visits the Map pointer if `ShouldVisitMapPointer()` returns true.
  template <VisitorId visitor_id>
  V8_INLINE void VisitMapPointerIfNeeded(Tagged<HeapObject> host);

  // If this predicate returns true, the visitor will visit the full JSObject
  // (including slack).
  V8_INLINE static constexpr bool ShouldVisitFullJSObject() { return false; }

  ConcreteVisitor* concrete_visitor() {
    return static_cast<ConcreteVisitor*>(this);
  }

  const ConcreteVisitor* concrete_visitor() const {
    return static_cast<const ConcreteVisitor*>(this);
  }

#define VISIT(TypeName)                                                      \
  V8_INLINE size_t Visit##TypeName(Tagged<Map> map, Tagged<TypeName> object, \
                                   MaybeObjectSize maybe_object_size);
  TYPED_VISITOR_ID_LIST(VISIT)
  TYPED_VISITOR_WITH_SLACK_ID_LIST(VISIT)
  TORQUE_VISITOR_ID_LIST(VISIT)
  TRUSTED_VISITOR_ID_LIST(VISIT)
#undef VISIT
  V8_INLINE size_t VisitShortcutCandidate(Tagged<Map> map,
                                          Tagged<ConsString> object,
                                          MaybeObjectSize maybe_object_size);
  V8_INLINE size_t VisitJSObjectFast(Tagged<Map> map, Tagged<JSObject> object,
                                     MaybeObjectSize maybe_object_size);
  V8_INLINE size_t VisitJSApiObject(Tagged<Map> map, Tagged<JSObject> object,
                                    MaybeObjectSize maybe_object_size);
  V8_INLINE size_t VisitStruct(Tagged<Map> map, Tagged<HeapObject> object,
                               MaybeObjectSize maybe_object_size);
  V8_INLINE size_t VisitFiller(Tagged<Map> map, Tagged<HeapObject> object,
                               MaybeObjectSize maybe_object_size);
  V8_INLINE size_t VisitFreeSpace(Tagged<Map> map, Tagged<FreeSpace> object,
                                  MaybeObjectSize maybe_object_size);

  template <typename T, typename TBodyDescriptor = typename T::BodyDescriptor>
  V8_INLINE size_t VisitJSObjectSubclass(Tagged<Map> map, Tagged<T> object,
                                         MaybeObjectSize maybe_object_size);

  template <VisitorId visitor_id, typename T,
            typename TBodyDescriptor = typename T::BodyDescriptor>
  V8_INLINE size_t VisitWithBodyDescriptor(Tagged<Map> map, Tagged<T> object,
                                           MaybeObjectSize maybe_object_size);

  template <typename T>
  static V8_INLINE Tagged<T> Cast(Tagged<HeapObject> object, const Heap* heap);

  // Inspects the slot and filters some well-known RO objects and Smis in a fast
  // way. May still return Smis or RO objects.
  template <typename TSlot>
  std::optional<Tagged<Object>> GetObjectFilterReadOnlyAndSmiFast(
      TSlot slot) const;

 protected:
  const Heap* heap_;
};

// These strings can be sources of safe string transitions. Transitions are safe
// if they don't result in invalidated slots. It's safe to read the length field
// on such strings as that's common for all.
//
// No special visitors are generated for such strings.
// V(VisitorId, TypeName)
#define SAFE_STRING_TRANSITION_SOURCES(V) \
  V(SeqOneByteString, SeqOneByteString)   \
  V(SeqTwoByteString, SeqTwoByteString)

// These strings can be sources of unsafe string transitions.
// V(VisitorId, TypeName)
#define UNSAFE_STRING_TRANSITION_SOURCES(V) \
  V(ExternalString, ExternalString)         \
  V(ConsString, ConsString)                 \
  V(SlicedString, SlicedString)

// V(VisitorId, TypeName)
#define UNSAFE_STRING_TRANSITION_TARGETS(V) \
  UNSAFE_STRING_TRANSITION_SOURCES(V)       \
  V(ShortcutCandidate, ConsString)          \
  V(ThinString, ThinString)

// A HeapVisitor that allows for concurrently tracing through objects. Tracing
// through objects with unsafe shape changes is guarded by
// `EnableConcurrentVisitation()` which defaults to off.
template <typename ConcreteVisitor>
class ConcurrentHeapVisitor : public HeapVisitor<ConcreteVisitor> {
 public:
  V8_INLINE explicit ConcurrentHeapVisitor(Isolate* isolate);

  V8_INLINE static constexpr bool EnableConcurrentVisitation() { return false; }

 protected:
#define VISIT_AS_LOCKED_STRING(VisitorId, TypeName)                          \
  V8_INLINE size_t Visit##TypeName(Tagged<Map> map, Tagged<TypeName> object, \
                                   MaybeObjectSize maybe_object_size);

  UNSAFE_STRING_TRANSITION_SOURCES(VISIT_AS_LOCKED_STRING)
#undef VISIT_AS_LOCKED_STRING

  template <typename T>
  static V8_INLINE Tagged<T> Cast(Tagged<HeapObject> object, const Heap* heap);

 private:
  template <typename T>
  V8_INLINE size_t VisitStringLocked(Tagged<T> object);

  friend class HeapVisitor<ConcreteVisitor>;
};

template <typename ConcreteVisitor>
class NewSpaceVisitor : public ConcurrentHeapVisitor<ConcreteVisitor> {
 public:
  V8_INLINE explicit NewSpaceVisitor(Isolate* isolate);

  // Special cases: Unreachable visitors for objects that are never found in the
  // young generation.
  void VisitInstructionStreamPointer(Tagged<Code>,
                                     InstructionStreamSlot) final {
    UNREACHABLE();
  }
  void VisitCodeTarget(Tagged<InstructionStream> host, RelocInfo*) final {
    UNREACHABLE();
  }
  void VisitEmbeddedPointer(Tagged<InstructionStream> host, RelocInfo*) final {
    UNREACHABLE();
  }
  void VisitMapPointer(Tagged<HeapObject>) override { UNREACHABLE(); }

 protected:
  V8_INLINE static constexpr bool ShouldVisitMapPointer() { return false; }

  // Special cases: Unreachable visitors for objects that are never found in the
  // young generation.
  size_t VisitNativeContext(Tagged<Map>, Tagged<NativeContext>,
                            MaybeObjectSize) {
    UNREACHABLE();
  }
  size_t VisitBytecodeArray(Tagged<Map>, Tagged<BytecodeArray>,
                            MaybeObjectSize) {
    UNREACHABLE();
  }
  size_t VisitSharedFunctionInfo(Tagged<Map> map, Tagged<SharedFunctionInfo>,
                                 MaybeObjectSize) {
    UNREACHABLE();
  }
  size_t VisitWeakCell(Tagged<Map>, Tagged<WeakCell>, MaybeObjectSize) {
    UNREACHABLE();
  }

  friend class HeapVisitor<ConcreteVisitor>;
};

class WeakObjectRetainer;

// A weak list is single linked list where each element has a weak pointer to
// the next element. Given the head of the list, this function removes dead
// elements from the list and if requested records slots for next-element
// pointers. The template parameter T is a WeakListVisitor that defines how to
// access the next-element pointers.
template <class T>
Tagged<Object> VisitWeakList(Heap* heap, Tagged<Object> list,
                             WeakObjectRetainer* retainer);
}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_VISITOR_H_
