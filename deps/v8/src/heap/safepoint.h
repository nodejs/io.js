// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SAFEPOINT_H_
#define V8_HEAP_SAFEPOINT_H_

#include <optional>
#include <vector>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/local-heap.h"
#include "src/objects/visitors.h"

namespace v8 {
namespace internal {

class Heap;
class LocalHeap;
class PerClientSafepointData;
class RootVisitor;

// Used to bring all threads with heap access in an isolate to a safepoint such
// that e.g. a garbage collection can be performed.
class IsolateSafepoint final {
 public:
  explicit IsolateSafepoint(Heap* heap);

  // Iterate handles in local heaps
  void Iterate(RootVisitor* visitor);

  // Iterate local heaps
  template <typename Callback>
  void IterateLocalHeaps(Callback callback) {
    AssertActive();
    for (LocalHeap* current = local_heaps_head_; current;
         current = current->next_) {
      callback(current);
    }
  }

  void AssertActive() { local_heaps_mutex_.AssertHeld(); }

  V8_EXPORT_PRIVATE void AssertMainThreadIsOnlyThread();

 private:
  struct RunningLocalHeap {
    LocalHeap* local_heap;
#if V8_OS_DARWIN
    pthread_override_t qos_override;
#endif
  };

  using RunningLocalHeaps = base::SmallVector<RunningLocalHeap, 4>;

  class Barrier {
    base::Mutex mutex_;
    base::ConditionVariable cv_resume_;
    base::ConditionVariable cv_stopped_;
    bool armed_;

    size_t stopped_ = 0;

    bool IsArmed() { return armed_; }

   public:
    Barrier() : armed_(false), stopped_(0) {}

    void Arm();
    void Disarm();
    void WaitUntilRunningThreadsInSafepoint(
        const IsolateSafepoint::RunningLocalHeaps& running_threads);

    void WaitInSafepoint();
    void WaitInUnpark();
    void NotifyPark();
  };

  enum class IncludeMainThread { kYes, kNo };

  // Wait until unpark operation is safe again.
  void WaitInUnpark();

  // Enter the safepoint from a running thread.
  void WaitInSafepoint();

  // Running thread reached a safepoint by parking itself.
  void NotifyPark();

  // Methods for entering/leaving local safepoint scopes.
  void EnterLocalSafepointScope();
  void LeaveLocalSafepointScope();

  // Methods for entering/leaving global safepoint scopes.
  void TryInitiateGlobalSafepointScope(Isolate* initiator,
                                       PerClientSafepointData* client_data);
  void InitiateGlobalSafepointScope(Isolate* initiator,
                                    PerClientSafepointData* client_data);
  void InitiateGlobalSafepointScopeRaw(Isolate* initiator,
                                       PerClientSafepointData* client_data);
  void LeaveGlobalSafepointScope(Isolate* initiator);

  // Blocks until all running threads reached a safepoint.
  void WaitUntilRunningThreadsInSafepoint(
      const PerClientSafepointData* client_data);

  IncludeMainThread ShouldIncludeMainThread(Isolate* initiator);

  void LockMutex(LocalHeap* local_heap);

  void SetSafepointRequestedFlags(
      IncludeMainThread include_main_thread,
      IsolateSafepoint::RunningLocalHeaps& running_local_heaps);
  void ClearSafepointRequestedFlags(IncludeMainThread include_main_thread);

  template <typename Callback>
  void AddLocalHeap(LocalHeap* local_heap, Callback callback) {
    // Safepoint holds this lock in order to stop threads from starting or
    // stopping.
    base::RecursiveMutexGuard guard(&local_heaps_mutex_);

    // Additional code protected from safepoint
    callback();

    // Add list to doubly-linked list
    if (local_heaps_head_) local_heaps_head_->prev_ = local_heap;
    local_heap->prev_ = nullptr;
    local_heap->next_ = local_heaps_head_;
    local_heaps_head_ = local_heap;
  }

  template <typename Callback>
  void RemoveLocalHeap(LocalHeap* local_heap, Callback callback) {
    base::RecursiveMutexGuard guard(&local_heaps_mutex_);

    // Additional code protected from safepoint
    callback();

    // Remove list from doubly-linked list
    if (local_heap->next_) local_heap->next_->prev_ = local_heap->prev_;
    if (local_heap->prev_)
      local_heap->prev_->next_ = local_heap->next_;
    else
      local_heaps_head_ = local_heap->next_;
  }

  Isolate* isolate() const;
  Isolate* shared_space_isolate() const;

  Barrier barrier_;
  Heap* heap_;

  // Mutex is used both for safepointing and adding/removing threads. A
  // RecursiveMutex is needed since we need to support nested SafepointScopes.
  base::RecursiveMutex local_heaps_mutex_;
  LocalHeap* local_heaps_head_ = nullptr;

  int active_safepoint_scopes_ = 0;

  friend class GlobalSafepoint;
  friend class GlobalSafepointScope;
  friend class Isolate;
  friend class IsolateSafepointScope;
  friend class LocalHeap;
  friend class PerClientSafepointData;
};

class V8_NODISCARD IsolateSafepointScope {
 public:
  V8_EXPORT_PRIVATE explicit IsolateSafepointScope(Heap* heap);
  V8_EXPORT_PRIVATE ~IsolateSafepointScope();

 private:
  IsolateSafepoint* safepoint_;
};

// Used for reaching a global safepoint, a safepoint across all client isolates
// of the shared isolate.
class GlobalSafepoint final {
 public:
  explicit GlobalSafepoint(Isolate* isolate);

  void AppendClient(Isolate* client);
  void RemoveClient(Isolate* client);

  template <typename Callback>
  void IterateClientIsolates(Callback callback) {
    AssertActive();
    for (Isolate* current = clients_head_; current;
         current = current->global_safepoint_next_client_isolate_) {
      DCHECK(!current->is_shared_space_isolate());
      callback(current);
    }
  }

  template <typename Callback>
  void IterateSharedSpaceAndClientIsolates(Callback callback) {
    callback(shared_space_isolate_);
    IterateClientIsolates(callback);
  }

  void AssertNoClientsOnTearDown();

  void AssertActive() { clients_mutex_.AssertHeld(); }

  V8_EXPORT_PRIVATE bool IsRequestedForTesting();

 private:
  void EnterGlobalSafepointScope(Isolate* initiator);
  void LeaveGlobalSafepointScope(Isolate* initiator);

  Isolate* const shared_space_isolate_;
  // RecursiveMutex is needed since we need to support nested
  // GlobalSafepointScopes.
  base::RecursiveMutex clients_mutex_;
  Isolate* clients_head_ = nullptr;
  int active_safepoint_scopes_ = 0;

  friend class GlobalSafepointScope;
  friend class Isolate;
};

class V8_NODISCARD GlobalSafepointScope {
 public:
  V8_EXPORT_PRIVATE explicit GlobalSafepointScope(Isolate* initiator);
  V8_EXPORT_PRIVATE ~GlobalSafepointScope();

 private:
  Isolate* const initiator_;
  Isolate* const shared_space_isolate_;
};

enum class SafepointKind { kIsolate, kGlobal };
struct GlobalSafepointForSharedSpaceIsolateTag {};

static constexpr GlobalSafepointForSharedSpaceIsolateTag
    kGlobalSafepointForSharedSpaceIsolate;

class V8_NODISCARD SafepointScope {
 public:
  V8_EXPORT_PRIVATE explicit SafepointScope(Isolate* initiator,
                                            SafepointKind kind);
  V8_EXPORT_PRIVATE explicit SafepointScope(
      Isolate* initiator, GlobalSafepointForSharedSpaceIsolateTag);

 private:
  std::optional<IsolateSafepointScope> isolate_safepoint_;
  std::optional<GlobalSafepointScope> global_safepoint_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SAFEPOINT_H_
