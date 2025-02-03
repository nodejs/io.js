// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_WORKLIST_H_
#define V8_HEAP_MARKING_WORKLIST_H_

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

#include "src/heap/base/worklist.h"
#include "src/heap/cppgc-js/cpp-marking-state.h"
#include "src/objects/heap-object.h"
#include "src/utils/address-map.h"

namespace v8 {
namespace internal {

class CppMarkingState;
class JSObject;

// The index of the main thread task used by concurrent/parallel GC.
const int kMainThreadTask = 0;

using MarkingWorklist = ::heap::base::Worklist<Tagged<HeapObject>, 64>;

// We piggyback on marking to compute object sizes per native context that is
// needed for the new memory measurement API. The algorithm works as follows:
// 1) At the start of marking we create a marking worklist for each context.
//    The existing shared, on_hold, and embedder worklists continue to work
//    as they did before, but they hold objects that are not attributed to any
//    context yet.
// 2) Each marker has an active worklist where it pushes newly discovered
//    objects. Initially the shared worklist is set as active for all markers.
// 3) When a marker pops an object from the active worklist:
//    a) It checks if the object has a known context (e.g. JSObjects, Maps,
//       Contexts know the context they belong to). If that's the case, then
//       the marker changes its active worklist to the worklist corresponding
//       to the context of the object.
//    b) It account the size of object to the active context.
//    c) It visits all pointers in the object and pushes new objects onto the
//       active worklist.
// 4) When the active worklist becomes empty the marker selects any other
//    non-empty worklist as the active worklist.
// 5) The write barrier pushes onto the shared worklist.
//
// The main invariant for context worklists:
//    If object X is in the worklist of context C, then either
//    a) X has a context and that context is C.
//    b) X is retained by object Y that has context C.
//
// The algorithm allows us to attribute context-independent objects such as
// strings, numbers, FixedArrays to their retaining contexts. The algorithm is
// not precise for context-independent objects that are shared between multiple
// contexts. Such objects may be attributed to any retaining context.

// Named pair of native context address and its marking worklist.
// Since native contexts are allocated in the old generation, their addresses
// a stable across Scavenges and stay valid throughout the marking phase.
struct ContextWorklistPair {
  Address context;
  std::unique_ptr<MarkingWorklist> worklist;
};

// A helper class that owns all global marking worklists.
class V8_EXPORT_PRIVATE MarkingWorklists final {
 public:
  class Local;
  // Fake addresses of special contexts used for per-context accounting.
  // - kSharedContext is for objects that are not attributed to any context.
  // - kOtherContext is for objects that are attributed to contexts that are
  //   not being measured.
  static constexpr Address kSharedContext = 0;
  static constexpr Address kOtherContext = 8;

  MarkingWorklists() = default;

  // Worklists implicitly check for emptiness on destruction.
  ~MarkingWorklists() = default;

  // Calls the specified callback on each element of the deques and replaces
  // the element with the result of the callback. If the callback returns
  // nullptr then the element is removed from the deque.
  // The callback must accept HeapObject and return HeapObject.
  template <typename Callback>
  void Update(Callback callback);

  MarkingWorklist* shared() { return &shared_; }
  MarkingWorklist* on_hold() { return &on_hold_; }
  MarkingWorklist* other() { return &other_; }

  // A list of (context, worklist) pairs that was set up at the start of
  // marking by CreateContextWorklists.
  const std::vector<ContextWorklistPair>& context_worklists() const {
    return context_worklists_;
  }
  // This should be invoked at the start of marking with the list of contexts
  // that require object size accounting.
  void CreateContextWorklists(const std::vector<Address>& contexts);
  // This should be invoked at the end of marking. All worklists must be
  // empty at that point.
  void ReleaseContextWorklists();
  bool IsUsingContextWorklists() const { return !context_worklists_.empty(); }

  void Clear();
  void Print();

 private:
  // Prints the stats about the global pool of the worklist.
  void PrintWorklist(const char* worklist_name, MarkingWorklist* worklist);

  // Worklist used for most objects.
  // TODO(mlippautz): Rename to "default".
  MarkingWorklist shared_;

  // Concurrent marking uses this worklist to bail out of marking objects
  // in new space's linear allocation area. Used to avoid black allocation
  // for new space. This allow the compiler to remove write barriers
  // for freshly allocatd objects.
  MarkingWorklist on_hold_;

  // Per-context worklists. Objects are in the `shared_` worklist by default.
  std::vector<ContextWorklistPair> context_worklists_;
  // Worklist used for objects that are attributed to contexts that are
  // not being measured.
  MarkingWorklist other_;
};

// A thread-local view of the marking worklists. It owns all local marking
// worklists and keeps track of the currently active local marking worklist
// for per-context marking. In order to avoid additional indirections for
// pushing and popping entries, the active_ worklist is not a pointer to
// Local but an actual instance of Local with the following invariants:
// - active_owner == worlist_by_context[active_context_].get()
// - *active_owner is empty (all fields are null) because its content has
//   been moved to active_.
class V8_EXPORT_PRIVATE MarkingWorklists::Local final {
 public:
  static constexpr Address kSharedContext = MarkingWorklists::kSharedContext;
  static constexpr Address kOtherContext = MarkingWorklists::kOtherContext;
  static constexpr std::nullptr_t kNoCppMarkingState = nullptr;

  explicit Local(
      MarkingWorklists* global,
      std::unique_ptr<CppMarkingState> cpp_marking_state = kNoCppMarkingState);

  // Local worklists implicitly check for emptiness on destruction.
  ~Local() = default;

  inline void Push(Tagged<HeapObject> object);
  inline bool Pop(Tagged<HeapObject>* object);

  inline void PushOnHold(Tagged<HeapObject> object);
  inline bool PopOnHold(Tagged<HeapObject>* object);

  void Publish();
  bool IsEmpty();
  bool IsWrapperEmpty() const;
  // Publishes the local active marking worklist if its global worklist is
  // empty. In the per-context marking mode it also publishes the shared
  // worklist.
  void ShareWork();
  // Publishes the local active marking worklist. Assume per-context marking
  // mode is not used.
  void PublishWork();
  // Merges the on-hold worklist to the shared worklist.
  void MergeOnHold();

  // Publishes CppHeap objects.
  inline void PublishCppHeapObjects();

  // Returns the context of the active worklist.
  Address Context() const { return active_context_; }
  inline Address SwitchToContext(Address context);
  bool IsPerContextMode() const { return is_per_context_mode_; }

  CppMarkingState* cpp_marking_state() const {
    return cpp_marking_state_.get();
  }

  Address SwitchToSharedForTesting();

 private:
  inline void SwitchToContextImpl(Address context,
                                  MarkingWorklist::Local* worklist);

  bool PopContext(Tagged<HeapObject>* object);
  Address SwitchToContextSlow(Address context);

  // Points to either `shared_`, `other_` or to a per-context worklist.
  MarkingWorklist::Local* active_;
  MarkingWorklist::Local shared_;
  MarkingWorklist::Local on_hold_;
  Address active_context_;
  const bool is_per_context_mode_;

  std::vector<MarkingWorklist::Local> context_worklists_;
  AddressToIndexHashMap worklist_by_context_;
  MarkingWorklist::Local other_;
  std::unique_ptr<CppMarkingState> cpp_marking_state_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_WORKLIST_H_
