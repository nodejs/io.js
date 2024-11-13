// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TRANSITIONS_H_
#define V8_OBJECTS_TRANSITIONS_H_

#include <optional>

#include "src/common/checks.h"
#include "src/execution/isolate.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/elements-kind.h"
#include "src/objects/map.h"
#include "src/objects/maybe-object.h"
#include "src/objects/name.h"
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

namespace third_party_heap {
class Impl;
}  // namespace third_party_heap

// Find all transitions with given name and calls the callback.
using ForEachTransitionCallback = std::function<void(Tagged<Map>)>;

// Descriptor for the contents of special side-step transition arrays.
// Side-step transitions are accessed through the TransitionsAccessor which
// enforces adherence to this format. The entries are either weak, Empty, or
// Unreachable.
struct SideStepTransition {
  enum class Kind : uint32_t {
    kCloneObject,
    kObjectAssign,
    kObjectAssignValidityCell,
  };
  static constexpr uint32_t kSize =
      static_cast<uint32_t>(Kind::kObjectAssignValidityCell) + 1;

  static constexpr Tagged<Smi> Empty = Smi::FromInt(0);
  static constexpr Tagged<Smi> Unreachable = Smi::FromInt(1);

 private:
  static constexpr int index_of(Kind kind) {
    return static_cast<uint32_t>(kind);
  }
  static constexpr uint32_t kFirstMapIdx =
      static_cast<uint32_t>(Kind::kCloneObject);
  static constexpr uint32_t kLastMapIdx =
      static_cast<uint32_t>(Kind::kObjectAssign);
  friend class TransitionsAccessor;
  friend class TransitionArray;
  friend class ObjectAssignAssembler;
};

std::ostream& operator<<(std::ostream& os, SideStepTransition::Kind sidestep);

// TransitionsAccessor is a helper class to encapsulate access to the various
// ways a Map can store transitions to other maps in its respective field at
// Map::kTransitionsOrPrototypeInfo.
// It caches state information internally, which becomes stale when a Map's
// transitions storage changes or when a GC cycle clears dead transitions;
// so while a TransitionsAccessor instance can be used for several read-only
// operations in a row (provided no GC happens between them), it must be
// discarded and recreated after "Insert" and "UpdateHandler" operations.
//
// Internal details: a Map's field either holds an in-place weak reference to a
// transition target, or a StoreIC handler for a transitioning store (which in
// turn points to its target map), or a TransitionArray for several target maps
// and/or handlers as well as prototype and ElementsKind transitions.  Property
// details (and in case of inline target storage, the key) are retrieved from
// the target map's descriptor array.  Stored transitions are weak in the GC
// sense: both single transitions stored inline and TransitionArray fields are
// cleared when the map they refer to is not otherwise reachable.
class V8_EXPORT_PRIVATE TransitionsAccessor {
 public:
  // {concurrent_access} signals that the TransitionsAccessor will only be used
  // in background threads. It acquires a reader lock for critical paths, as
  // well as blocking the accessor from modifying the TransitionsArray.
  inline TransitionsAccessor(Isolate* isolate, Tagged<Map> map,
                             bool concurrent_access = false);

  // Insert a new transition into |map|'s transition array, extending it
  // as necessary. This can trigger GC.
  static void Insert(Isolate* isolate, DirectHandle<Map> map,
                     DirectHandle<Name> name, DirectHandle<Map> target,
                     TransitionKindFlag flag) {
    InsertHelper(isolate, map, name, DirectHandle<Map>(target), flag);
  }
  static void InsertNoneSentinel(Isolate* isolate, DirectHandle<Map> map,
                                 DirectHandle<Name> name) {
    InsertHelper(isolate, map, name, DirectHandle<Map>(),
                 TransitionKindFlag::SPECIAL_TRANSITION);
  }

  Tagged<Map> SearchTransition(Tagged<Name> name, PropertyKind kind,
                               PropertyAttributes attributes);
  static inline MaybeHandle<Map> SearchTransition(
      Isolate* isolate, DirectHandle<Map> map, Tagged<Name> name,
      PropertyKind kind, PropertyAttributes attributes);

  // Searches for a transition with a special symbol.
  Tagged<Map> SearchSpecial(Tagged<Symbol> name);
  static inline MaybeHandle<Map> SearchSpecial(Isolate* isolate,
                                               DirectHandle<Map> map,
                                               Tagged<Symbol> name);

  // Returns true for non-property transitions like elements kind, or
  // or frozen/sealed transitions.
  static bool IsSpecialTransition(ReadOnlyRoots roots, Tagged<Name> name);

  MaybeHandle<Map> FindTransitionToField(DirectHandle<String> name);

  // Find all transitions with given name and calls the callback.
  // Neither GCs nor operations requiring Isolate::full_transition_array_access
  // lock are allowed inside the callback.
  // If any of the GC- or lock-requiring processing is necessary, it has to be
  // done outside of the callback.
  void ForEachTransitionTo(Tagged<Name> name,
                           const ForEachTransitionCallback& callback,
                           DisallowGarbageCollection* no_gc);

  template <typename Char>
  inline bool IsExpectedTransition(Tagged<Name> transition_name,
                                   Tagged<Map> transition_target,
                                   base::Vector<const Char> key_chars);

  template <typename Char>
  inline std::pair<Handle<String>, Handle<Map>> ExpectedTransition(
      base::Vector<const Char> key_chars);

  template <typename Callback, typename ProtoCallback,
            typename SideStepCallback>
  void ForEachTransition(DisallowGarbageCollection* no_gc, Callback callback,
                         ProtoCallback proto_transition_callback,
                         SideStepCallback side_step_transition_callback) {
    ForEachTransitionWithKey<Callback, ProtoCallback, SideStepCallback, false>(
        no_gc, callback, proto_transition_callback,
        side_step_transition_callback);
  }

  template <typename Callback, typename ProtoCallback,
            typename SideStepCallback, bool with_key = true>
  void ForEachTransitionWithKey(DisallowGarbageCollection* no_gc,
                                Callback callback,
                                ProtoCallback proto_transition_callback,
                                SideStepCallback side_step_transition_callback);

  int NumberOfTransitions();
  // The size of transition arrays are limited so they do not end up in large
  // object space. Otherwise ClearNonLiveReferences would leak memory while
  // applying in-place right trimming.
  static const int kMaxNumberOfTransitions = 1024 + 512;
  inline Tagged<Name> GetKey(int transition_number);
  inline Tagged<Map> GetTarget(int transition_number);
  static inline PropertyDetails GetTargetDetails(Tagged<Name> name,
                                                 Tagged<Map> target);

  static bool CanHaveMoreTransitions(Isolate* isolate, DirectHandle<Map> map);

  static bool IsMatchingMap(Tagged<Map> target, Tagged<Name> name,
                            PropertyKind kind, PropertyAttributes attributes);

  bool HasIntegrityLevelTransitionTo(
      Tagged<Map> to, Tagged<Symbol>* out_symbol = nullptr,
      PropertyAttributes* out_integrity_level = nullptr);

  // ===== ITERATION =====
  using TraverseCallback = std::function<void(Tagged<Map>)>;

  // Traverse the transition tree in preorder.
  void TraverseTransitionTree(const TraverseCallback& callback) {
    // Make sure that we do not allocate in the callback.
    DisallowGarbageCollection no_gc;
    base::SharedMutexGuardIf<base::kShared> scope(
        isolate_->full_transition_array_access(), concurrent_access_);
    TraverseTransitionTreeInternal(callback, &no_gc);
  }

  // ===== PROTOTYPE TRANSITIONS =====
  // When you set the prototype of an object using the __proto__ accessor you
  // need a new map for the object (the prototype is stored in the map).  In
  // order not to multiply maps unnecessarily we store these as transitions in
  // the original map.  That way we can transition to the same map if the same
  // prototype is set, rather than creating a new map every time.  The
  // transitions are in the form of a map where the keys are prototype objects
  // and the values are the maps they transition to.
  // PutPrototypeTransition can trigger GC.
  static bool PutPrototypeTransition(Isolate* isolate, DirectHandle<Map>,
                                     DirectHandle<Object> prototype,
                                     DirectHandle<Map> target_map);
  static std::optional<Tagged<Map>> GetPrototypeTransition(
      Isolate* isolate, Tagged<Map> map, Tagged<Object> prototype);
  bool HasPrototypeTransitions();

  // During the first-time Map::Update and Map::TryUpdate, the migration target
  // map could be cached in the raw_transitions slot of the old map that is
  // deprecated from the map transition tree. The next time old map is updated,
  // we will check this cache slot as a shortcut to get the migration target
  // map.
  static void SetMigrationTarget(Isolate* isolate, DirectHandle<Map> map,
                                 Tagged<Map> migration_target);
  Tagged<Map> GetMigrationTarget();

  inline bool HasSideStepTransitions();
  static void EnsureHasSideStepTransitions(Isolate* isolate,
                                           DirectHandle<Map> map);
  inline Tagged<Object> GetSideStepTransition(SideStepTransition::Kind i);
  inline void SetSideStepTransition(SideStepTransition::Kind i,
                                    Tagged<Object> target);

#if DEBUG || OBJECT_PRINT
  void PrintTransitions(std::ostream& os);
  static void PrintOneTransition(std::ostream& os, Tagged<Name> key,
                                 Tagged<Map> target);
  void PrintTransitionTree();
  void PrintTransitionTree(std::ostream& os, int level,
                           DisallowGarbageCollection* no_gc);
#endif
#if DEBUG
  static void CheckNewTransitionsAreConsistent(Isolate* isolate,
                                               DirectHandle<Map> map,
                                               Tagged<Object> transitions);
  bool IsConsistentWithBackPointers();
  bool IsSortedNoDuplicates();
#endif

 protected:
  // Allow tests to use inheritance to access internals.
  enum Encoding {
    kPrototypeInfo,
    kUninitialized,
    kMigrationTarget,
    kWeakRef,
    kFullTransitionArray,
  };

  inline Encoding encoding() { return encoding_; }

  inline int Capacity();

  inline Tagged<TransitionArray> transitions();

  DISALLOW_GARBAGE_COLLECTION(no_gc_)

 private:
  friend class MarkCompactCollector;  // For HasSimpleTransitionTo.
  friend class third_party_heap::Impl;
  friend class TransitionArray;

  static inline Encoding GetEncoding(Isolate* isolate,
                                     Tagged<MaybeObject> raw_transitions);
  static inline Encoding GetEncoding(Isolate* isolate,
                                     Tagged<TransitionArray> array);
  static inline Encoding GetEncoding(Isolate* isolate, DirectHandle<Map> map);

  static inline Tagged<TransitionArray> GetTransitionArray(
      Isolate* isolate, Tagged<MaybeObject> raw_transitions);
  static inline Tagged<TransitionArray> GetTransitionArray(
      Isolate* isolate, DirectHandle<Map> map);

  static inline Tagged<Map> GetSimpleTransition(Isolate* isolate,
                                                DirectHandle<Map> map);
  static inline Tagged<Name> GetSimpleTransitionKey(Tagged<Map> transition);
  inline PropertyDetails GetSimpleTargetDetails(Tagged<Map> transition);

  static inline Tagged<Map> GetTargetFromRaw(Tagged<MaybeObject> raw);

  static void EnsureHasFullTransitionArray(Isolate* isolate,
                                           DirectHandle<Map> map);
  static void SetPrototypeTransitions(
      Isolate* isolate, DirectHandle<Map> map,
      DirectHandle<WeakFixedArray> proto_transitions);
  static Tagged<WeakFixedArray> GetPrototypeTransitions(Isolate* isolate,
                                                        Tagged<Map> map);

  static void InsertHelper(Isolate* isolate, DirectHandle<Map> map,
                           DirectHandle<Name> name, DirectHandle<Map> target,
                           TransitionKindFlag flag);

  static inline void ReplaceTransitions(Isolate* isolate, DirectHandle<Map> map,
                                        Tagged<MaybeObject> new_transitions);
  static inline void ReplaceTransitions(
      Isolate* isolate, DirectHandle<Map> map,
      DirectHandle<TransitionArray> new_transitions);

  bool HasSimpleTransitionTo(Tagged<Map> map);

  inline Tagged<Map> GetTargetMapFromWeakRef();

  void TraverseTransitionTreeInternal(const TraverseCallback& callback,
                                      DisallowGarbageCollection* no_gc);

  Isolate* isolate_;
  Tagged<Map> map_;
  Tagged<MaybeObject> raw_transitions_;
  Encoding encoding_;
  bool concurrent_access_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(TransitionsAccessor);
};

// TransitionArrays are fixed arrays used to hold map transitions for property,
// constant, and element changes.
// The TransitionArray class exposes a very low-level interface. Most clients
// should use TransitionsAccessors.
// TransitionArrays have the following format:
// [0] Tagged<Smi>(0) or WeakFixedArray of prototype transitions (strong ref)
// [1] Tagged<Smi>(0) or WeakFixedArray of side-step transitions (strong ref)
// [2] Number of transitions (can be zero after trimming)
// [3] First transition key (strong ref)
// [4] First transition target (weak ref)
// ...
// [4 + number of transitions * kTransitionSize]: start of slack
// TODO(olivf): The slots for prototype transitions and side-steps could be
// shared.
class TransitionArray : public WeakFixedArray {
 public:
  inline Tagged<WeakFixedArray> GetPrototypeTransitions();
  inline bool HasPrototypeTransitions();

  // Accessors for fetching instance transition at transition number.
  inline void SetKey(int transition_number, Tagged<Name> value);
  inline Tagged<Name> GetKey(int transition_number);
  inline HeapObjectSlot GetKeySlot(int transition_number);

  inline Tagged<Map> GetTarget(int transition_number);
  inline void SetRawTarget(int transition_number, Tagged<MaybeObject> target);
  inline Tagged<MaybeObject> GetRawTarget(int transition_number);
  inline HeapObjectSlot GetTargetSlot(int transition_number);
  inline bool GetTargetIfExists(int transition_number, Isolate* isolate,
                                Tagged<Map>* target);

  // Required for templatized Search interface.
  inline Tagged<Name> GetKey(InternalIndex index);
  static constexpr int kNotFound = -1;

  inline Tagged<Name> GetSortedKey(int transition_number);
  int GetSortedKeyIndex(int transition_number) { return transition_number; }
  inline int number_of_entries() const;
#ifdef DEBUG
  V8_EXPORT_PRIVATE bool IsSortedNoDuplicates();
#endif

  V8_EXPORT_PRIVATE void Sort();

  void PrintInternal(std::ostream& os);

  DECL_PRINTER(TransitionArray)
  DECL_VERIFIER(TransitionArray)

  // Layout for full transition arrays.
  static const int kPrototypeTransitionsIndex = 0;
  static const int kSideStepTransitionsIndex = 1;
  static const int kTransitionLengthIndex = 2;
  static const int kFirstIndex = 3;

  // Layout of map transition entries in full transition arrays.
  static const int kEntryKeyIndex = 0;
  static const int kEntryTargetIndex = 1;
  static const int kEntrySize = 2;

  // Conversion from transition number to array indices.
  static int ToKeyIndex(int transition_number) {
    return kFirstIndex + (transition_number * kEntrySize) + kEntryKeyIndex;
  }

  static int ToTargetIndex(int transition_number) {
    return kFirstIndex + (transition_number * kEntrySize) + kEntryTargetIndex;
  }

  inline int SearchNameForTesting(Tagged<Name> name,
                                  int* out_insertion_index = nullptr);

  inline Tagged<Map> SearchAndGetTargetForTesting(
      PropertyKind kind, Tagged<Name> name, PropertyAttributes attributes);

  // Accessors for side-step transitions.
  inline bool HasSideStepTransitions();
  static void CreateSideStepTransitions(
      Isolate* isolate, DirectHandle<TransitionArray> transitions);

 private:
  friend class Factory;
  friend class MarkCompactCollector;
  friend class third_party_heap::Impl;
  friend class TransitionsAccessor;

  inline void SetNumberOfTransitions(int number_of_transitions);

  inline int Capacity();

  // ===== PROTOTYPE TRANSITIONS =====
  // Cache format:
  //    0: finger - index of the first free cell in the cache
  //    1 + i: target map
  static const int kProtoTransitionHeaderSize = 1;
  static const int kMaxCachedPrototypeTransitions = 256;

  inline void SetPrototypeTransitions(
      Tagged<WeakFixedArray> prototype_transitions);

  static inline int NumberOfPrototypeTransitions(
      Tagged<WeakFixedArray> proto_transitions);
  static void SetNumberOfPrototypeTransitions(
      Tagged<WeakFixedArray> proto_transitions, int value);

  static const int kProtoTransitionNumberOfEntriesOffset = 0;
  static_assert(kProtoTransitionHeaderSize == 1);

  // Returns the fixed array length required to hold number_of_transitions
  // transitions.
  static int LengthFor(int number_of_transitions) {
    return ToKeyIndex(number_of_transitions);
  }

  // Search a  transition for a given kind, property name and attributes.
  int Search(PropertyKind kind, Tagged<Name> name,
             PropertyAttributes attributes, int* out_insertion_index = nullptr);

  V8_EXPORT_PRIVATE Tagged<Map> SearchAndGetTarget(
      PropertyKind kind, Tagged<Name> name, PropertyAttributes attributes);

  // Search a non-property transition (like elements kind, observe or frozen
  // transitions).
  inline int SearchSpecial(Tagged<Symbol> symbol,
                           bool concurrent_search = false,
                           int* out_insertion_index = nullptr);
  // Search a first transition for a given property name.
  inline int SearchName(Tagged<Name> name, bool concurrent_search = false,
                        int* out_insertion_index = nullptr);
  int SearchDetails(int transition, PropertyKind kind,
                    PropertyAttributes attributes, int* out_insertion_index);
  Tagged<Map> SearchDetailsAndGetTarget(int transition, PropertyKind kind,
                                        PropertyAttributes attributes);

  // Find all transitions with given name and calls the callback.
  void ForEachTransitionTo(Tagged<Name> name,
                           const ForEachTransitionCallback& callback);

  inline int number_of_transitions() const;

  static bool CompactPrototypeTransitionArray(Isolate* isolate,
                                              Tagged<WeakFixedArray> array);

  static Handle<WeakFixedArray> GrowPrototypeTransitionArray(
      DirectHandle<WeakFixedArray> array, int new_capacity, Isolate* isolate);

  // Compares two tuples <key, kind, attributes>, returns -1 if
  // tuple1 is "less" than tuple2, 0 if tuple1 equal to tuple2 and 1 otherwise.
  static inline int CompareKeys(Tagged<Name> key1, uint32_t hash1,
                                PropertyKind kind1,
                                PropertyAttributes attributes1,
                                Tagged<Name> key2, uint32_t hash2,
                                PropertyKind kind2,
                                PropertyAttributes attributes2);

  // Compares keys, returns -1 if key1 is "less" than key2,
  // 0 if key1 equal to key2 and 1 otherwise.
  static inline int CompareNames(Tagged<Name> key1, uint32_t hash1,
                                 Tagged<Name> key2, uint32_t hash2);

  // Compares two details, returns -1 if details1 is "less" than details2,
  // 0 if details1 equal to details2 and 1 otherwise.
  static inline int CompareDetails(PropertyKind kind1,
                                   PropertyAttributes attributes1,
                                   PropertyKind kind2,
                                   PropertyAttributes attributes2);

  inline void Set(int transition_number, Tagged<Name> key,
                  Tagged<MaybeObject> target);

  inline Tagged<WeakFixedArray> GetSideStepTransitions();

  OBJECT_CONSTRUCTORS(TransitionArray, WeakFixedArray);
};

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TRANSITIONS_H_
