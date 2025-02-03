// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_ALLOCATION_SITE_H_
#define V8_OBJECTS_ALLOCATION_SITE_H_

#include "src/objects/objects.h"
#include "src/objects/struct.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

enum InstanceType : uint16_t;

#include "torque-generated/src/objects/allocation-site-tq.inc"

class AllocationSite : public Struct {
 public:
  NEVER_READ_ONLY_SPACE
  static const uint32_t kMaximumArrayBytesToPretransition = 8 * 1024;

  // Values for pretenure decision field.
  enum PretenureDecision {
    kUndecided = 0,
    kDontTenure = 1,
    kMaybeTenure = 2,
    kTenure = 3,
    kZombie = 4,  // See comment to IsZombie() for documentation.
    kLastPretenureDecisionValue = kZombie
  };

  const char* PretenureDecisionName(PretenureDecision decision);

  // Contains either a Smi-encoded bitfield or a boilerplate. If it's a Smi the
  // AllocationSite is for a constructed Array.
  DECL_ACCESSORS(transition_info_or_boilerplate, Tagged<Object>)
  DECL_RELEASE_ACQUIRE_ACCESSORS(transition_info_or_boilerplate, Tagged<Object>)
  DECL_GETTER(boilerplate, Tagged<JSObject>)
  DECL_RELEASE_ACQUIRE_ACCESSORS(boilerplate, Tagged<JSObject>)
  DECL_INT_ACCESSORS(transition_info)

  // nested_site threads a list of sites that represent nested literals
  // walked in a particular order. So [[1, 2], 1, 2] will have one
  // nested_site, but [[1, 2], 3, [4]] will have a list of two.
  DECL_ACCESSORS(nested_site, Tagged<Object>)

  // Bitfield containing pretenuring information.
  DECL_RELAXED_INT32_ACCESSORS(pretenure_data)

  DECL_INT32_ACCESSORS(pretenure_create_count)
  DECL_ACCESSORS(dependent_code, Tagged<DependentCode>)

  // heap->allocation_site_list() points to the last AllocationSite which form
  // a linked list through the weak_next property. The GC might remove elements
  // from the list by updateing weak_next.
  DECL_ACCESSORS(weak_next, Tagged<Object>)

  inline void Initialize();

  // Checks if the allocation site contain weak_next field;
  inline bool HasWeakNext() const;

  // This method is expensive, it should only be called for reporting.
  bool IsNested();

  // transition_info bitfields, for constructed array transition info.
  using ElementsKindBits = base::BitField<ElementsKind, 0, 6>;
  using DoNotInlineBit = base::BitField<bool, 6, 1>;
  // Unused bits 7-30.

  // Bitfields for pretenure_data
  using MementoFoundCountBits = base::BitField<int, 0, 26>;
  using PretenureDecisionBits = base::BitField<PretenureDecision, 26, 3>;
  using DeoptDependentCodeBit = base::BitField<bool, 29, 1>;
  static_assert(PretenureDecisionBits::kMax >= kLastPretenureDecisionValue);

  // Increments the mementos found counter and returns the new count.
  inline int IncrementMementoFoundCount(int increment = 1);

  inline void IncrementMementoCreateCount();

  AllocationType GetAllocationType() const;

  void ResetPretenureDecision();

  inline PretenureDecision pretenure_decision() const;
  inline void set_pretenure_decision(PretenureDecision decision);

  inline bool deopt_dependent_code() const;
  inline void set_deopt_dependent_code(bool deopt);

  inline int memento_found_count() const;
  inline void set_memento_found_count(int count);

  inline int memento_create_count() const;
  inline void set_memento_create_count(int count);

  // A "zombie" AllocationSite is one which has no more strong roots to
  // it, and yet must be maintained until the next GC. The reason is that
  // it may be that in new space there are AllocationMementos hanging around
  // which point to the AllocationSite. If we scavenge these AllocationSites
  // too soon, those AllocationMementos will end up pointing to garbage
  // addresses. The concrete case happens when evacuating new space in the full
  // GC which happens after sweeping has been started already. To mitigate this
  // problem the garbage collector marks such AllocationSites as zombies when it
  // discovers there are no roots, allowing the subsequent collection pass to
  // recognize zombies and discard them later.
  inline bool IsZombie() const;

  inline bool IsMaybeTenure() const;

  inline void MarkZombie();

  inline bool MakePretenureDecision(PretenureDecision current_decision,
                                    double ratio, bool maximum_size_scavenge);

  inline bool DigestPretenuringFeedback(bool maximum_size_scavenge);

  inline ElementsKind GetElementsKind() const;
  inline void SetElementsKind(ElementsKind kind);

  inline bool CanInlineCall() const;
  inline void SetDoNotInlineCall();

  inline bool PointsToLiteral() const;

  template <AllocationSiteUpdateMode update_or_check =
                AllocationSiteUpdateMode::kUpdate>
  static bool DigestTransitionFeedback(DirectHandle<AllocationSite> site,
                                       ElementsKind to_kind);

  DECL_PRINTER(AllocationSite)
  DECL_VERIFIER(AllocationSite)

  static inline bool ShouldTrack(ElementsKind boilerplate_elements_kind);
  static bool ShouldTrack(ElementsKind from, ElementsKind to);
  static inline bool CanTrack(InstanceType type);

  // Layout description.
  // AllocationSite has to start with TransitionInfoOrboilerPlateOffset
  // and end with WeakNext field.
  #define ALLOCATION_SITE_FIELDS(V)                     \
    V(kStartOffset, 0)                                  \
    V(kTransitionInfoOrBoilerplateOffset, kTaggedSize)  \
    V(kNestedSiteOffset, kTaggedSize)                   \
    V(kDependentCodeOffset, kTaggedSize)                \
    V(kCommonPointerFieldEndOffset, 0)                  \
    V(kPretenureDataOffset, kInt32Size)                 \
    V(kPretenureCreateCountOffset, kInt32Size)          \
    /* Size of AllocationSite without WeakNext field */ \
    V(kSizeWithoutWeakNext, 0)                          \
    V(kWeakNextOffset, kTaggedSize)                     \
    /* Size of AllocationSite with WeakNext field */    \
    V(kSizeWithWeakNext, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, ALLOCATION_SITE_FIELDS)
  #undef ALLOCATION_SITE_FIELDS

  class BodyDescriptor;

 private:
  inline bool PretenuringDecisionMade() const;

  OBJECT_CONSTRUCTORS(AllocationSite, Struct);
};

class AllocationMemento
    : public TorqueGeneratedAllocationMemento<AllocationMemento, Struct> {
 public:
  DECL_ACCESSORS(allocation_site, Tagged<Object>)

  inline bool IsValid() const;
  inline Tagged<AllocationSite> GetAllocationSite() const;
  inline Address GetAllocationSiteUnchecked() const;

  DECL_PRINTER(AllocationMemento)

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(AllocationMemento)
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_ALLOCATION_SITE_H_
