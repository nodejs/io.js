// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_GLOBAL_HANDLES_H_
#define V8_HANDLES_GLOBAL_HANDLES_H_

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "include/v8-callbacks.h"
#include "include/v8-persistent-handle.h"
#include "include/v8-profiler.h"
#include "src/handles/handles.h"
#include "src/heap/heap.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

class HeapStats;
class RootVisitor;

// Global handles hold handles that are independent of stack-state and can have
// callbacks and finalizers attached to them.
class V8_EXPORT_PRIVATE GlobalHandles final {
 public:
  GlobalHandles(const GlobalHandles&) = delete;
  GlobalHandles& operator=(const GlobalHandles&) = delete;

  template <class NodeType>
  class NodeBlock;

  //
  // API for regular handles.
  //

  static void MoveGlobal(Address** from, Address** to);

  static IndirectHandle<Object> CopyGlobal(Address* location);

  static void Destroy(Address* location);

  // Make the global handle weak and set the callback parameter for the
  // handle.  When the garbage collector recognizes that only weak global
  // handles point to an object the callback function is invoked (for each
  // handle) with the handle and corresponding parameter as arguments.  By
  // default the handle still contains a pointer to the object that is being
  // collected.  For this reason the object is not collected until the next
  // GC.  For a phantom weak handle the handle is cleared (set to a Smi)
  // before the callback is invoked, but the handle can still be identified
  // in the callback by using the location() of the handle.
  static void MakeWeak(Address* location, void* parameter,
                       WeakCallbackInfo<void>::Callback weak_callback,
                       v8::WeakCallbackType type);
  static void MakeWeak(Address** location_addr);

  static void AnnotateStrongRetainer(Address* location, const char* label);

  // Clear the weakness of a global handle.
  static void* ClearWeakness(Address* location);

  // Tells whether global handle is weak.
  static bool IsWeak(Address* location);

  explicit GlobalHandles(Isolate* isolate);
  ~GlobalHandles();

  // Creates a new global handle that is alive until Destroy is called.
  IndirectHandle<Object> Create(Tagged<Object> value);
  IndirectHandle<Object> Create(Address value);

  template <typename T>
  inline IndirectHandle<T> Create(Tagged<T> value);

  void RecordStats(HeapStats* stats);

  size_t InvokeFirstPassWeakCallbacks();
  void InvokeSecondPassPhantomCallbacks();

  // Schedule or invoke second pass weak callbacks.
  void PostGarbageCollectionProcessing(v8::GCCallbackFlags gc_callback_flags);

  void IterateStrongRoots(RootVisitor* v);
  void IterateWeakRoots(RootVisitor* v);
  void IterateAllRoots(RootVisitor* v);
  void IterateAllYoungRoots(RootVisitor* v);

  // Marks handles that are phantom or have callbacks based on the predicate
  // |should_reset_handle| as pending.
  void IterateWeakRootsForPhantomHandles(
      WeakSlotCallbackWithHeap should_reset_handle);

  //  Note: The following *Young* methods are used for the Scavenger to
  //  identify and process handles in the young generation. The set of young
  //  handles is complete but the methods may encounter handles that are
  //  already in old space.

  // Iterates over strong and dependent handles. See the note above.
  void IterateYoungStrongAndDependentRoots(RootVisitor* v);

  // Processes all young weak objects:
  // - Weak objects for which `should_reset_handle()` returns true are reset;
  // - Others are passed to `v` iff `v` is not null.
  void ProcessWeakYoungObjects(RootVisitor* v,
                               WeakSlotCallbackWithHeap should_reset_handle);

  // Updates the list of young nodes that is maintained separately.
  void UpdateListOfYoungNodes();
  // Clears the list of young nodes, assuming that the young generation is
  // empty.
  void ClearListOfYoungNodes();

  Isolate* isolate() const { return isolate_; }

  size_t TotalSize() const;
  size_t UsedSize() const;
  // Number of global handles.
  size_t handles_count() const;
  size_t last_gc_custom_callbacks() const { return last_gc_custom_callbacks_; }

  void IterateAllRootsForTesting(v8::PersistentHandleVisitor* v);

#ifdef DEBUG
  void PrintStats();
  void Print();
#endif  // DEBUG

  bool HasYoung() const { return !young_nodes_.empty(); }

 private:
  // Internal node structures.
  class Node;
  template <class BlockType>
  class NodeIterator;
  template <class NodeType>
  class NodeSpace;
  class PendingPhantomCallback;

  void ApplyPersistentHandleVisitor(v8::PersistentHandleVisitor* visitor,
                                    Node* node);

  // Clears a weak `node` for which `should_reset_node()` returns true.
  //
  // Returns false if a node is weak and alive which requires further
  // processing, and true in all other cases (e.g. also strong nodes).
  bool ResetWeakNodeIfDead(Node* node,
                           WeakSlotCallbackWithHeap should_reset_node);

  Isolate* const isolate_;

  std::unique_ptr<NodeSpace<Node>> regular_nodes_;
  // Contains all nodes holding young objects. Note: when the list
  // is accessed, some of the objects may have been promoted already.
  std::vector<Node*> young_nodes_;
  std::vector<std::pair<Node*, PendingPhantomCallback>>
      pending_phantom_callbacks_;
  std::vector<PendingPhantomCallback> second_pass_callbacks_;
  bool second_pass_callbacks_task_posted_ = false;
  size_t last_gc_custom_callbacks_ = 0;
};

class GlobalHandles::PendingPhantomCallback final {
 public:
  using Data = v8::WeakCallbackInfo<void>;

  enum InvocationType { kFirstPass, kSecondPass };

  PendingPhantomCallback(
      Data::Callback callback, void* parameter,
      void* embedder_fields[v8::kEmbedderFieldsInWeakCallback])
      : callback_(callback), parameter_(parameter) {
    for (int i = 0; i < v8::kEmbedderFieldsInWeakCallback; ++i) {
      embedder_fields_[i] = embedder_fields[i];
    }
  }

  void Invoke(Isolate* isolate, InvocationType type);

  Data::Callback callback() const { return callback_; }

 private:
  Data::Callback callback_;
  void* parameter_;
  void* embedder_fields_[v8::kEmbedderFieldsInWeakCallback];
};

class EternalHandles final {
 public:
  EternalHandles() = default;
  ~EternalHandles();
  EternalHandles(const EternalHandles&) = delete;
  EternalHandles& operator=(const EternalHandles&) = delete;

  // Create an EternalHandle, overwriting the index.
  V8_EXPORT_PRIVATE void Create(Isolate* isolate, Tagged<Object> object,
                                int* index);

  // Grab the handle for an existing EternalHandle.
  inline IndirectHandle<Object> Get(int index) {
    return IndirectHandle<Object>(GetLocation(index));
  }

  // Iterates over all handles.
  void IterateAllRoots(RootVisitor* visitor);
  // Iterates over all handles which might be in the young generation.
  void IterateYoungRoots(RootVisitor* visitor);
  // Rebuilds new space list.
  void PostGarbageCollectionProcessing();

  size_t handles_count() const { return size_; }

 private:
  static const int kInvalidIndex = -1;
  static const int kShift = 8;
  static const int kSize = 1 << kShift;
  static const int kMask = 0xff;

  // Gets the slot for an index. This returns an Address* rather than an
  // ObjectSlot in order to avoid #including slots.h in this header file.
  inline Address* GetLocation(int index) {
    DCHECK(index >= 0 && index < size_);
    return &blocks_[index >> kShift][index & kMask];
  }

  int size_ = 0;
  std::vector<Address*> blocks_;
  std::vector<int> young_node_indices_;
};

// A vector of global Handles which automatically manages the backing of those
// Handles as a vector of strong-rooted addresses. Handles returned by the
// vector are valid as long as they are present in the vector.
template <typename T>
class GlobalHandleVector {
 public:
  class Iterator {
   public:
    explicit Iterator(
        std::vector<Address, StrongRootAllocator<Address>>::iterator it)
        : it_(it) {}
    Iterator& operator++() {
      ++it_;
      return *this;
    }
    IndirectHandle<T> operator*() { return IndirectHandle<T>(&*it_); }
    bool operator==(const Iterator& that) const { return it_ == that.it_; }
    bool operator!=(const Iterator& that) const { return it_ != that.it_; }

    Tagged<T> raw() { return Cast<T>(Tagged<Object>(*it_)); }

   private:
    std::vector<Address, StrongRootAllocator<Address>>::iterator it_;
  };

  explicit inline GlobalHandleVector(Heap* heap);
  // Usage with LocalHeap is safe.
  explicit inline GlobalHandleVector(LocalHeap* local_heap);

  IndirectHandle<T> operator[](size_t i) {
    return IndirectHandle<T>(&locations_[i]);
  }

  size_t size() const { return locations_.size(); }
  bool empty() const { return locations_.empty(); }

  void Reserve(size_t size) { locations_.reserve(size); }
  void Push(Tagged<T> val) { locations_.push_back(val.ptr()); }
  // Handles into the GlobalHandleVector become invalid when they are removed,
  // so "pop" returns a raw object rather than a handle.
  inline Tagged<T> Pop();

  Iterator begin() { return Iterator(locations_.begin()); }
  Iterator end() { return Iterator(locations_.end()); }

 private:
  std::vector<Address, StrongRootAllocator<Address>> locations_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_GLOBAL_HANDLES_H_
