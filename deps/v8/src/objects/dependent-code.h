// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DEPENDENT_CODE_H_
#define V8_OBJECTS_DEPENDENT_CODE_H_

#include "src/objects/fixed-array.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

enum class LazyDeoptimizeReason : uint8_t;

// Dependent code is conceptually the list of {Code, DependencyGroup} tuples
// associated with an object, where the dependency group is a reason that could
// lead to a deopt of the corresponding code.
//
// Implementation details: DependentCode is a weak array list containing
// entries, where each entry consists of a (weak) Code object and the
// DependencyGroups bitset as a Smi.
//
// Note the underlying weak array list currently never shrinks physically (the
// contents may shrink).
// TODO(jgruber): Consider adding physical shrinking.
class DependentCode : public WeakArrayList {
 public:
  enum DependencyGroup {
    // Group of code objects that embed a transition to this map, and depend on
    // being deoptimized when the transition is replaced by a new version.
    kTransitionGroup = 1 << 0,
    // Group of code objects that omit run-time prototype checks for prototypes
    // described by this map. The group is deoptimized whenever the following
    // conditions hold, possibly invalidating the assumptions embedded in the
    // code:
    // a) A fast-mode object described by this map changes shape (and
    // transitions to a new map), or
    // b) A dictionary-mode prototype described by this map changes shape, the
    // const-ness of one of its properties changes, or its [[Prototype]]
    // changes (only the latter causes a transition).
    kPrototypeCheckGroup = 1 << 1,
    // Group of code objects that depends on global property values in property
    // cells not being changed.
    kPropertyCellChangedGroup = 1 << 2,
    // Group of code objects that omit run-time checks for field(s) introduced
    // by this map, i.e. for the field type.
    kFieldTypeGroup = 1 << 3,
    kFieldConstGroup = 1 << 4,
    kFieldRepresentationGroup = 1 << 5,
    // Group of code objects that omit run-time type checks for initial maps of
    // constructors.
    kInitialMapChangedGroup = 1 << 6,
    // Group of code objects that depend on tenuring information in
    // AllocationSites not being changed.
    kAllocationSiteTenuringChangedGroup = 1 << 7,
    // Group of code objects that depend on element transition information in
    // AllocationSites not being changed.
    kAllocationSiteTransitionChangedGroup = 1 << 8,
    // Group of code objects that depend on a slot side table property of
    // a ScriptContext not being changed.
    kScriptContextSlotPropertyChangedGroup = 1 << 9,
    // Group of code objects that depend on particular context's extension
    // slot to be empty.
    kEmptyContextExtensionGroup = 1 << 10,
    // IMPORTANT: The last bit must fit into a Smi, i.e. into 31 bits.
  };
  using DependencyGroups = base::Flags<DependencyGroup, uint32_t>;

  static const char* DependencyGroupName(DependencyGroup group);
  static LazyDeoptimizeReason DependencyGroupToLazyDeoptReason(
      DependencyGroup group);

  // Register a dependency of {code} on {object}, of the kinds given by
  // {groups}.
  V8_EXPORT_PRIVATE static void InstallDependency(Isolate* isolate,
                                                  Handle<Code> code,
                                                  Handle<HeapObject> object,
                                                  DependencyGroups groups);

  template <typename ObjectT>
  static void DeoptimizeDependencyGroups(Isolate* isolate, ObjectT object,
                                         DependencyGroups groups);

  template <typename ObjectT>
  static void DeoptimizeDependencyGroups(Isolate* isolate,
                                         Tagged<ObjectT> object,
                                         DependencyGroups groups);

  template <typename ObjectT>
  static bool MarkCodeForDeoptimization(Isolate* isolate,
                                        Tagged<ObjectT> object,
                                        DependencyGroups groups);

  V8_EXPORT_PRIVATE static Tagged<DependentCode> empty_dependent_code(
      const ReadOnlyRoots& roots);
  static constexpr RootIndex kEmptyDependentCode =
      RootIndex::kEmptyWeakArrayList;

  // Constants exposed for tests.
  static constexpr int kSlotsPerEntry =
      2;  // {code: weak InstructionStream, groups: Smi}.
  static constexpr int kCodeSlotOffset = 0;
  static constexpr int kGroupsSlotOffset = 1;

 private:
  // Get/Set {object}'s {DependentCode}.
  static Tagged<DependentCode> GetDependentCode(Tagged<HeapObject> object);
  static void SetDependentCode(DirectHandle<HeapObject> object,
                               DirectHandle<DependentCode> dep);

  static DirectHandle<DependentCode> InsertWeakCode(
      Isolate* isolate, Handle<DependentCode> entries, DependencyGroups groups,
      DirectHandle<Code> code);

  bool MarkCodeForDeoptimization(Isolate* isolate,
                                 DependencyGroups deopt_groups);

  void DeoptimizeDependencyGroups(Isolate* isolate, DependencyGroups groups);

  // The callback is called for all non-cleared entries, and should return true
  // iff the current entry should be cleared. The Function template argument
  // must be of type: bool (Tagged<Code>, DependencyGroups).
  template <typename Function>
  void IterateAndCompact(IsolateForSandbox isolate, const Function& fn);

  // Fills the given entry with the last non-cleared entry in this list, and
  // returns the new length after the last non-cleared entry has been moved.
  int FillEntryFromBack(int index, int length);

  static constexpr int LengthFor(int number_of_entries) {
    return number_of_entries * kSlotsPerEntry;
  }

  OBJECT_CONSTRUCTORS(DependentCode, WeakArrayList);
};

DEFINE_OPERATORS_FOR_FLAGS(DependentCode::DependencyGroups)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DEPENDENT_CODE_H_
