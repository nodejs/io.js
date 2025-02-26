// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_VISITORS_H_
#define V8_OBJECTS_VISITORS_H_

#include "src/common/globals.h"
#include "src/objects/casting.h"
#include "src/objects/code.h"
#include "src/objects/compressed-slots.h"
#include "src/objects/instruction-stream.h"
#include "src/objects/slots.h"

namespace v8 {
namespace internal {

class Code;

#define ROOT_ID_LIST(V)                                        \
  V(kBootstrapper, "(Bootstrapper)")                           \
  V(kBuiltins, "(Builtins)")                                   \
  V(kClientHeap, "(Client heap)")                              \
  V(kCodeFlusher, "(Code flusher)")                            \
  V(kCompilationCache, "(Compilation cache)")                  \
  V(kDebug, "(Debugger)")                                      \
  V(kExtensions, "(Extensions)")                               \
  V(kEternalHandles, "(Eternal handles)")                      \
  V(kExternalStringsTable, "(External strings)")               \
  V(kGlobalHandles, "(Global handles)")                        \
  V(kHandleScope, "(Handle scope)")                            \
  V(kMicroTasks, "(Micro tasks)")                              \
  V(kReadOnlyRootList, "(Read-only roots)")                    \
  V(kRelocatable, "(Relocatable)")                             \
  V(kRetainMaps, "(Retain maps)")                              \
  V(kSharedHeapObjectCache, "(Shareable object cache)")        \
  V(kSharedStructTypeRegistry, "(SharedStruct type registry)") \
  V(kSmiRootList, "(Smi roots)")                               \
  V(kStackRoots, "(Stack roots)")                              \
  V(kStartupObjectCache, "(Startup object cache)")             \
  V(kStringTable, "(Internalized strings)")                    \
  V(kStrongRootList, "(Strong root list)")                     \
  V(kStrongRoots, "(Strong roots)")                            \
  V(kThreadManager, "(Thread manager)")                        \
  V(kTracedHandles, "(Traced handles)")                        \
  V(kWeakRoots, "(Weak roots)")                                \
  V(kWriteBarrier, "(Write barrier)")

class VisitorSynchronization : public AllStatic {
 public:
#define DECLARE_ENUM(enum_item, ignore) enum_item,
  enum SyncTag { ROOT_ID_LIST(DECLARE_ENUM) kNumberOfSyncTags };
#undef DECLARE_ENUM
};

enum class Root {
#define DECLARE_ENUM(enum_item, ignore) enum_item,
  ROOT_ID_LIST(DECLARE_ENUM)
#undef DECLARE_ENUM
      kNumberOfRoots
};

// Abstract base class for visiting, and optionally modifying, the
// pointers contained in roots. Used in GC and serialization/deserialization.
class RootVisitor {
 public:
  virtual ~RootVisitor() = default;

  // Visits a contiguous arrays of pointers in the half-open range
  // [start, end). Any or all of the values may be modified on return.
  virtual void VisitRootPointers(Root root, const char* description,
                                 FullObjectSlot start, FullObjectSlot end) = 0;

  // Handy shorthand for visiting a single pointer.
  virtual void VisitRootPointer(Root root, const char* description,
                                FullObjectSlot p) {
    VisitRootPointers(root, description, p, p + 1);
  }

  // Visits a contiguous arrays of off-heap pointers in the half-open range
  // [start, end). Any or all of the values may be modified on return.
  //
  // This should be implemented for any visitor that visits off-heap data
  // structures, of which there are currently only two: the string table and the
  // shared struct type registry. Visitors for those structures are limited in
  // scope.
  //
  // If we ever add new off-heap data structures that we want to walk as roots
  // using this function, we should make it generic, by
  //
  //   1) Making this function pure virtual, and
  //   2) Implementing it for all visitors.
  virtual void VisitRootPointers(Root root, const char* description,
                                 OffHeapObjectSlot start,
                                 OffHeapObjectSlot end) {
    UNREACHABLE();
  }

  // Visits a running Code object and potentially its associated
  // InstructionStream from the execution stack.
  virtual void VisitRunningCode(FullObjectSlot code_slot,
                                FullObjectSlot istream_or_smi_zero_slot) {
    // For most visitors, currently running code is no different than any other
    // on-stack pointer.
    VisitRootPointer(Root::kStackRoots, nullptr, istream_or_smi_zero_slot);
    VisitRootPointer(Root::kStackRoots, nullptr, code_slot);
  }

  // Intended for serialization/deserialization checking: insert, or
  // check for the presence of, a tag at this position in the stream.
  // Also used for marking up GC roots in heap snapshots.
  virtual void Synchronize(VisitorSynchronization::SyncTag tag) {}

  static const char* RootName(Root root);

  // The type of collector that invokes this visitor. This is used by the
  // ConservativeStackVisitor to determine which root pointers on the stack
  // to follow, during conservative stack scanning. For MARK_COMPACTOR (the
  // default) all pointers are followed, whereas for young generation
  // collectors only pointers to objects in the young generation are followed.
  virtual GarbageCollector collector() const {
    return GarbageCollector::MARK_COMPACTOR;
  }
};

class RelocIterator;

// Abstract base class for visiting, and optionally modifying, the
// pointers contained in Objects. Used in GC and serialization/deserialization.
class ObjectVisitor {
 public:
  virtual ~ObjectVisitor() = default;

  // Visits a contiguous arrays of pointers in the half-open range
  // [start, end). Any or all of the values may be modified on return.
  virtual void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                             ObjectSlot end) = 0;
  virtual void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                             MaybeObjectSlot end) = 0;
  // When V8_EXTERNAL_CODE_SPACE is enabled, visits an InstructionStream pointer
  // slot. The values may be modified on return. Not used when
  // V8_EXTERNAL_CODE_SPACE is not enabled (the InstructionStream pointer slots
  // are visited as a part of on-heap slot visitation - via VisitPointers()).
  virtual void VisitInstructionStreamPointer(Tagged<Code> host,
                                             InstructionStreamSlot slot) = 0;

  // Custom weak pointers must be ignored by the GC but not other
  // visitors. They're used for e.g., lists that are recreated after GC. The
  // default implementation treats them as strong pointers. Visitors who want to
  // ignore them must override this function with empty.
  virtual void VisitCustomWeakPointers(Tagged<HeapObject> host,
                                       ObjectSlot start, ObjectSlot end) {
    VisitPointers(host, start, end);
  }

  // Handy shorthand for visiting a single pointer.
  virtual void VisitPointer(Tagged<HeapObject> host, ObjectSlot p) {
    VisitPointers(host, p, p + 1);
  }
  virtual void VisitPointer(Tagged<HeapObject> host, MaybeObjectSlot p) {
    VisitPointers(host, p, p + 1);
  }
  virtual void VisitCustomWeakPointer(Tagged<HeapObject> host, ObjectSlot p) {
    VisitCustomWeakPointers(host, p, p + 1);
  }

  virtual void VisitEphemeron(Tagged<HeapObject> host, int index,
                              ObjectSlot key, ObjectSlot value) {
    VisitPointer(host, key);
    VisitPointer(host, value);
  }

  // Visits the relocation info using the given iterator.
  void VisitRelocInfo(Tagged<InstructionStream> host, RelocIterator* it);

  virtual void VisitCodeTarget(Tagged<InstructionStream> host,
                               RelocInfo* rinfo) {}
  virtual void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                                    RelocInfo* rinfo) {}
  virtual void VisitExternalReference(Tagged<InstructionStream> host,
                                      RelocInfo* rinfo) {}
  virtual void VisitInternalReference(Tagged<InstructionStream> host,
                                      RelocInfo* rinfo) {}
  // TODO(ishell): rename to VisitBuiltinEntry.
  virtual void VisitOffHeapTarget(Tagged<InstructionStream> host,
                                  RelocInfo* rinfo) {}

  virtual void VisitExternalPointer(Tagged<HeapObject> host,
                                    ExternalPointerSlot slot) {}

  // Same as `VisitExternalPointer` with the difference that the slot's contents
  // are known to be managed by `CppHeap`.
  virtual void VisitCppHeapPointer(Tagged<HeapObject> host,
                                   CppHeapPointerSlot slot) {}

  virtual void VisitIndirectPointer(Tagged<HeapObject> host,
                                    IndirectPointerSlot slot,
                                    IndirectPointerMode mode) {}

  virtual void VisitProtectedPointer(Tagged<TrustedObject> host,
                                     ProtectedPointerSlot slot) {}
  virtual void VisitProtectedPointer(Tagged<TrustedObject> host,
                                     ProtectedMaybeObjectSlot slot) {}

  virtual void VisitTrustedPointerTableEntry(Tagged<HeapObject> host,
                                             IndirectPointerSlot slot) {}

  virtual void VisitJSDispatchTableEntry(Tagged<HeapObject> host,
                                         JSDispatchHandle handle) {}

  virtual void VisitMapPointer(Tagged<HeapObject> host) { UNREACHABLE(); }
};

// Helper version of ObjectVisitor that also takes care of caching base values
// of the main pointer compression cage and for the code cage.
class ObjectVisitorWithCageBases : public ObjectVisitor {
 public:
  inline ObjectVisitorWithCageBases(PtrComprCageBase cage_base,
                                    PtrComprCageBase code_cage_base);
  inline explicit ObjectVisitorWithCageBases(Isolate* isolate);
  inline explicit ObjectVisitorWithCageBases(Heap* heap);

  // The pointer compression cage base value used for decompression of all
  // tagged values except references to InstructionStream objects.
  PtrComprCageBase cage_base() const {
#ifdef V8_COMPRESS_POINTERS
    return cage_base_;
#else
    return PtrComprCageBase{};
#endif  // V8_COMPRESS_POINTERS
  }

  // The pointer compression cage base value used for decompression of
  // references to InstructionStream objects.
  PtrComprCageBase code_cage_base() const {
#ifdef V8_EXTERNAL_CODE_SPACE
    return code_cage_base_;
#else
    return cage_base();
#endif  // V8_EXTERNAL_CODE_SPACE
  }

 private:
#ifdef V8_COMPRESS_POINTERS
  const PtrComprCageBase cage_base_;
#ifdef V8_EXTERNAL_CODE_SPACE
  const PtrComprCageBase code_cage_base_;
#endif  // V8_EXTERNAL_CODE_SPACE
#endif  // V8_COMPRESS_POINTERS
};

// A wrapper class for root visitors that are used by client isolates during a
// shared garbage collection. The wrapped visitor only visits heap objects in
// the shared spaces and ignores everything else. The type parameter `Visitor`
// should be a subclass of `RootVisitor`, or a similar class that provides the
// required interface.
template <typename Visitor = RootVisitor>
class ClientRootVisitor final : public RootVisitor {
 public:
  explicit ClientRootVisitor(Visitor* actual_visitor)
      : actual_visitor_(actual_visitor) {}

  void VisitRootPointers(Root root, const char* description,
                         FullObjectSlot start, FullObjectSlot end) final {
    for (FullObjectSlot p = start; p < end; ++p) {
      Tagged<Object> object = *p;
#ifdef V8_ENABLE_DIRECT_HANDLE
      if (object.ptr() == ValueHelper::kTaggedNullAddress) continue;
#endif
      if (!IsSharedHeapObject(object)) continue;
      actual_visitor_->VisitRootPointer(root, description, p);
    }
  }

  void VisitRootPointers(Root root, const char* description,
                         OffHeapObjectSlot start, OffHeapObjectSlot end) final {
    actual_visitor_->VisitRootPointers(root, description, start, end);
  }

  inline void VisitRunningCode(FullObjectSlot code_slot,
                               FullObjectSlot maybe_istream_slot) final;

  void Synchronize(VisitorSynchronization::SyncTag tag) final {
    actual_visitor_->Synchronize(tag);
  }

 private:
  V8_INLINE static bool IsSharedHeapObject(Tagged<Object> object);

  Visitor* const actual_visitor_;
};

// A wrapper class for object visitors that are used by client isolates during a
// shared garbage collection. The wrapped visitor only visits heap objects in
// the shared spaces and ignores everything else. The type parameter `Visitor`
// should be a subclass of `ObjectVisitorWithCageBases`, or a similar class that
// provides the required interface.
template <typename Visitor = ObjectVisitorWithCageBases>
class ClientObjectVisitor final : public ObjectVisitorWithCageBases {
 public:
  explicit ClientObjectVisitor(Visitor* actual_visitor)
      : ObjectVisitorWithCageBases(actual_visitor->cage_base(),
                                   actual_visitor->code_cage_base()),
        actual_visitor_(actual_visitor) {}

  void VisitPointer(Tagged<HeapObject> host, ObjectSlot p) final {
    if (!IsSharedHeapObject(p.load(cage_base()))) return;
    actual_visitor_->VisitPointer(host, p);
  }

  inline void VisitMapPointer(Tagged<HeapObject> host) final;

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) final {
    for (ObjectSlot p = start; p < end; ++p) {
      // The map slot should be handled in VisitMapPointer.
      DCHECK_NE(host->map_slot(), p);
      DCHECK(!HasWeakHeapObjectTag(p.load(cage_base())));
      VisitPointer(host, p);
    }
  }

  inline void VisitInstructionStreamPointer(Tagged<Code> host,
                                            InstructionStreamSlot slot) final;

  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) final {
    // At the moment, custom roots cannot contain weak pointers.
    UNREACHABLE();
  }

  inline void VisitCodeTarget(Tagged<InstructionStream> host,
                              RelocInfo* rinfo) final;

  inline void VisitEmbeddedPointer(Tagged<InstructionStream> host,
                                   RelocInfo* rinfo) final;

 private:
  V8_INLINE static bool IsSharedHeapObject(Tagged<Object> object);

  Visitor* const actual_visitor_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_VISITORS_H_
