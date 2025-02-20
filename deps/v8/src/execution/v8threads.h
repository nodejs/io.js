// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_V8THREADS_H_
#define V8_EXECUTION_V8THREADS_H_

#include <atomic>

#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

class RootVisitor;
class ThreadLocalTop;

class ThreadState {
 public:
  // Returns nullptr after the last one.
  ThreadState* Next();

  enum List { FREE_LIST, IN_USE_LIST };

  void LinkInto(List list);
  void Unlink();

  // Id of thread.
  void set_id(ThreadId id) { id_ = id; }
  ThreadId id() { return id_; }

  // Get data area for archiving a thread.
  char* data() { return data_; }

 private:
  explicit ThreadState(ThreadManager* thread_manager);
  ~ThreadState();

  void AllocateSpace();

  ThreadId id_;
  char* data_;
  ThreadState* next_;
  ThreadState* previous_;

  ThreadManager* thread_manager_;

  friend class ThreadManager;
};

class ThreadVisitor {
 public:
  // ThreadLocalTop may be only available during this call.
  virtual void VisitThread(Isolate* isolate, ThreadLocalTop* top) = 0;

 protected:
  virtual ~ThreadVisitor() = default;
};

class ThreadManager {
 public:
  void Lock();
  V8_EXPORT_PRIVATE void Unlock();

  void InitThread(const ExecutionAccess&);
  void ArchiveThread();
  bool RestoreThread();
  void FreeThreadResources();
  bool IsArchived();

  void Iterate(RootVisitor* v);
  void IterateArchivedThreads(ThreadVisitor* v);
  bool IsLockedByCurrentThread() const {
    return mutex_owner_.load(std::memory_order_relaxed) == ThreadId::Current();
  }
  bool IsLockedByThread(ThreadId id) const {
    return mutex_owner_.load(std::memory_order_relaxed) == id;
  }

  ThreadId CurrentId();

  // Iterate over in-use states.
  ThreadState* FirstThreadStateInUse();
  ThreadState* GetFreeThreadState();

 private:
  explicit ThreadManager(Isolate* isolate);
  ~ThreadManager();

  void DeleteThreadStateList(ThreadState* anchor);

  void EagerlyArchiveThread();

  base::SpinningMutex mutex_;
  // {ThreadId} must be trivially copyable to be stored in {std::atomic}.
  ASSERT_TRIVIALLY_COPYABLE(i::ThreadId);
  std::atomic<ThreadId> mutex_owner_;
  ThreadId lazily_archived_thread_;
  ThreadState* lazily_archived_thread_state_;

  // In the following two lists there is always at least one object on the list.
  // The first object is a flying anchor that is only there to simplify linking
  // and unlinking.
  // Head of linked list of free states.
  ThreadState* free_anchor_;
  // Head of linked list of states in use.
  ThreadState* in_use_anchor_;

  Isolate* isolate_;

  friend class Isolate;
  friend class ThreadState;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_V8THREADS_H_
