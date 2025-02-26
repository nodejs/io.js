// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_PERSISTENT_HANDLES_H_
#define V8_HANDLES_PERSISTENT_HANDLES_H_

#include <vector>

#include "include/v8-internal.h"
#include "src/api/api.h"
#include "src/base/macros.h"
#include "src/execution/isolate.h"
#include "src/objects/visitors.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

class Heap;

// PersistentHandles serves as a container for handles that can be passed back
// and forth between threads. Allocation and deallocation of this class is
// thread-safe and the isolate tracks all PersistentHandles containers.
class PersistentHandles {
 public:
  V8_EXPORT_PRIVATE explicit PersistentHandles(Isolate* isolate);
  V8_EXPORT_PRIVATE ~PersistentHandles();

  PersistentHandles(const PersistentHandles&) = delete;
  PersistentHandles& operator=(const PersistentHandles&) = delete;

  V8_EXPORT_PRIVATE void Iterate(RootVisitor* visitor);

  template <typename T>
  IndirectHandle<T> NewHandle(Tagged<T> obj) {
#ifdef DEBUG
    CheckOwnerIsNotParked();
#endif
    return IndirectHandle<T>(GetHandle(obj.ptr()));
  }

  template <typename T>
  IndirectHandle<T> NewHandle(IndirectHandle<T> obj) {
    return NewHandle(*obj);
  }

  template <typename T>
  IndirectHandle<T> NewHandle(DirectHandle<T> obj) {
    return NewHandle(*obj);
  }

  template <typename T>
  IndirectHandle<T> NewHandle(T obj) {
    static_assert(kTaggedCanConvertToRawObjects);
    return NewHandle(Tagged<T>(obj));
  }

  Isolate* isolate() const { return isolate_; }

#ifdef DEBUG
  V8_EXPORT_PRIVATE bool Contains(Address* location);
#endif

 private:
  void AddBlock();
  V8_EXPORT_PRIVATE Address* GetHandle(Address value);

#ifdef DEBUG
  void Attach(LocalHeap* local_heap);
  void Detach();
  V8_EXPORT_PRIVATE void CheckOwnerIsNotParked();

  LocalHeap* owner_ = nullptr;

#else
  void Attach(LocalHeap*) {}
  void Detach() {}
#endif

  Isolate* isolate_;
  std::vector<Address*> blocks_;

  Address* block_next_;
  Address* block_limit_;

  PersistentHandles* prev_;
  PersistentHandles* next_;

#ifdef DEBUG
  std::set<Address*> ordered_blocks_;
#endif

  friend class HandleScopeImplementer;
  friend class LocalHeap;
  friend class PersistentHandlesList;

  FRIEND_TEST(PersistentHandlesTest, OrderOfBlocks);
};

class PersistentHandlesList {
 public:
  PersistentHandlesList() : persistent_handles_head_(nullptr) {}

  void Iterate(RootVisitor* visitor, Isolate* isolate);

 private:
  void Add(PersistentHandles* persistent_handles);
  void Remove(PersistentHandles* persistent_handles);

  base::SpinningMutex persistent_handles_mutex_;
  PersistentHandles* persistent_handles_head_;

  friend class PersistentHandles;
};

// PersistentHandlesScope sets up a scope in which all created main thread
// handles become persistent handles that can be sent to another thread.
class V8_NODISCARD PersistentHandlesScope {
 public:
  V8_EXPORT_PRIVATE explicit PersistentHandlesScope(Isolate* isolate);
  V8_EXPORT_PRIVATE ~PersistentHandlesScope();

  // Moves all blocks of this scope into PersistentHandles and returns it.
  V8_EXPORT_PRIVATE std::unique_ptr<PersistentHandles> Detach();

  // Returns true if the current active handle scope is a persistent handle
  // scope, thus all handles created become persistent handles.
  V8_EXPORT_PRIVATE static bool IsActive(Isolate* isolate);

 private:
  Address* first_block_;
  Address* prev_limit_;
  Address* prev_next_;
  HandleScopeImplementer* const impl_;

#ifdef DEBUG
  bool handles_detached_ = false;
  int prev_level_;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_PERSISTENT_HANDLES_H_
