// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_FUTEX_EMULATION_H_
#define V8_EXECUTION_FUTEX_EMULATION_H_

#include <stdint.h>

#include "include/v8-persistent-handle.h"
#include "src/base/atomicops.h"
#include "src/base/macros.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/time.h"
#include "src/tasks/cancelable-task.h"
#include "src/utils/allocation.h"

// Support for emulating futexes, a low-level synchronization primitive. They
// are natively supported by Linux, but must be emulated for other platforms.
// This library emulates them on all platforms using mutexes and condition
// variables for consistency.
//
// This is used by the Futex API defined in the SharedArrayBuffer draft spec,
// found here: https://github.com/tc39/ecmascript_sharedmem

namespace v8 {

class Promise;

namespace base {
class TimeDelta;
}  // namespace base

namespace internal {

class BackingStore;
class FutexWaitList;

class Isolate;
class JSArrayBuffer;

class AtomicsWaitWakeHandle {
 public:
  explicit AtomicsWaitWakeHandle(Isolate* isolate) : isolate_(isolate) {}

  void Wake();
  inline bool has_stopped() const { return stopped_; }

 private:
  Isolate* isolate_;
  bool stopped_ = false;
};

class FutexWaitListNode {
 public:
  // Create a sync FutexWaitListNode.
  FutexWaitListNode() = default;

  // Create an async FutexWaitListNode.
  FutexWaitListNode(std::weak_ptr<BackingStore> backing_store,
                    void* wait_location, Handle<JSObject> promise_capability,
                    Isolate* isolate);

  // Disallow copying nodes.
  FutexWaitListNode(const FutexWaitListNode&) = delete;
  FutexWaitListNode& operator=(const FutexWaitListNode&) = delete;

  void NotifyWake();

  bool IsAsync() const { return async_state_ != nullptr; }

  // Returns false if the cancelling failed, true otherwise.
  bool CancelTimeoutTask();

 private:
  friend class FutexEmulation;
  friend class FutexWaitList;

  // Async wait requires substantially more information than synchronous wait.
  // Hence store that additional information in a heap-allocated struct to make
  // it more obvious that this will only be needed for the async case.
  struct AsyncState {
    AsyncState(Isolate* isolate, std::shared_ptr<TaskRunner> task_runner,
               std::weak_ptr<BackingStore> backing_store,
               v8::Global<v8::Promise> promise,
               v8::Global<v8::Context> native_context)
        : isolate_for_async_waiters(isolate),
          task_runner(std::move(task_runner)),
          backing_store(std::move(backing_store)),
          promise(std::move(promise)),
          native_context(std::move(native_context)) {
      DCHECK(this->promise.IsWeak());
      DCHECK(this->native_context.IsWeak());
    }

    ~AsyncState() {
      // Assert that the timeout task was cancelled.
      DCHECK_EQ(CancelableTaskManager::kInvalidTaskId, timeout_task_id);
    }

    Isolate* const isolate_for_async_waiters;
    std::shared_ptr<TaskRunner> const task_runner;

    // The backing store on which we are waiting might die in an async wait.
    // We keep a weak_ptr to verify during a wake operation that the original
    // backing store is still mapped to that address.
    std::weak_ptr<BackingStore> const backing_store;

    // Weak Global handle. Must not be synchronously resolved by a non-owner
    // Isolate.
    v8::Global<v8::Promise> const promise;

    // Weak Global handle.
    v8::Global<v8::Context> const native_context;

    // If timeout_time_ is base::TimeTicks(), this async waiter doesn't have a
    // timeout or has already been notified. Values other than base::TimeTicks()
    // are used for async waiters with an active timeout.
    base::TimeTicks timeout_time;

    // The task ID of the timeout task.
    CancelableTaskManager::Id timeout_task_id =
        CancelableTaskManager::kInvalidTaskId;
  };

  base::ConditionVariable cond_;
  // prev_ and next_ are protected by FutexEmulationGlobalState::mutex.
  FutexWaitListNode* prev_ = nullptr;
  FutexWaitListNode* next_ = nullptr;

  // The memory location the FutexWaitListNode is waiting on. Equals
  // backing_store_->buffer_start() + wait_addr at FutexWaitListNode creation
  // time. This address is used find the node in the per-location list, or to
  // remove it.
  // Note that during an async wait the BackingStore might get deleted while
  // this node is alive.
  void* wait_location_ = nullptr;

  // waiting_ and interrupted_ are protected by FutexEmulationGlobalState::mutex
  // if this node is currently contained in FutexEmulationGlobalState::wait_list
  // or an AtomicsWaitWakeHandle has access to it.
  bool waiting_ = false;
  bool interrupted_ = false;

  // State used for an async wait; nullptr on sync waits.
  const std::unique_ptr<AsyncState> async_state_;
};

class FutexEmulation : public AllStatic {
 public:
  enum WaitMode { kSync = 0, kAsync };
  enum class CallType { kIsNotWasm = 0, kIsWasm };

  // Pass to Wake() to wake all waiters.
  static const uint32_t kWakeAll = UINT32_MAX;

  // Check that array_buffer[addr] == value, and return "not-equal" if not. If
  // they are equal, block execution on |isolate|'s thread until woken via
  // |Wake|, or when the time given in |rel_timeout_ms| elapses. Note that
  // |rel_timeout_ms| can be Infinity.
  // If woken, return "ok", otherwise return "timed-out". The initial check and
  // the decision to wait happen atomically.
  static Tagged<Object> WaitJs32(Isolate* isolate, WaitMode mode,
                                 Handle<JSArrayBuffer> array_buffer,
                                 size_t addr, int32_t value,
                                 double rel_timeout_ms);

  // An version of WaitJs32 for int64_t values.
  static Tagged<Object> WaitJs64(Isolate* isolate, WaitMode mode,
                                 Handle<JSArrayBuffer> array_buffer,
                                 size_t addr, int64_t value,
                                 double rel_timeout_ms);

  // Same as WaitJs above except it returns 0 (ok), 1 (not equal) and 2 (timed
  // out) as expected by Wasm.
  V8_EXPORT_PRIVATE static Tagged<Object> WaitWasm32(
      Isolate* isolate, Handle<JSArrayBuffer> array_buffer, size_t addr,
      int32_t value, int64_t rel_timeout_ns);

  // Same as Wait32 above except it checks for an int64_t value in the
  // array_buffer.
  V8_EXPORT_PRIVATE static Tagged<Object> WaitWasm64(
      Isolate* isolate, Handle<JSArrayBuffer> array_buffer, size_t addr,
      int64_t value, int64_t rel_timeout_ns);

  // Wake |num_waiters_to_wake| threads that are waiting on the given |addr|.
  // |num_waiters_to_wake| can be kWakeAll, in which case all waiters are
  // woken. The rest of the waiters will continue to wait. The return value is
  // the number of woken waiters.
  // Variant 1: Compute the wait address from the |array_buffer| and |addr|.
  V8_EXPORT_PRIVATE static int Wake(Tagged<JSArrayBuffer> array_buffer,
                                    size_t addr, uint32_t num_waiters_to_wake);
  // Variant 2: Pass raw |addr| (used for WebAssembly atomic.notify).
  static int Wake(void* addr, uint32_t num_waiters_to_wake);

  // Called before |isolate| dies. Removes async waiters owned by |isolate|.
  static void IsolateDeinit(Isolate* isolate);

  // Return the number of threads or async waiters waiting on |addr|. Should
  // only be used for testing.
  static int NumWaitersForTesting(Tagged<JSArrayBuffer> array_buffer,
                                  size_t addr);

  // Return the number of async waiters (which belong to |isolate|) waiting.
  // Should only be used for testing.
  static int NumAsyncWaitersForTesting(Isolate* isolate);

  // Return the number of async waiters which were waiting for |addr| and are
  // now waiting for the Promises to be resolved. Should only be used for
  // testing.
  static int NumUnresolvedAsyncPromisesForTesting(
      Tagged<JSArrayBuffer> array_buffer, size_t addr);

 private:
  friend class FutexWaitListNode;
  friend class AtomicsWaitWakeHandle;
  friend class ResolveAsyncWaiterPromisesTask;
  friend class AsyncWaiterTimeoutTask;

  template <typename T>
  static Tagged<Object> Wait(Isolate* isolate, WaitMode mode,
                             Handle<JSArrayBuffer> array_buffer, size_t addr,
                             T value, double rel_timeout_ms);

  template <typename T>
  static Tagged<Object> Wait(Isolate* isolate, WaitMode mode,
                             Handle<JSArrayBuffer> array_buffer, size_t addr,
                             T value, bool use_timeout, int64_t rel_timeout_ns,
                             CallType call_type = CallType::kIsNotWasm);

  template <typename T>
  static Tagged<Object> WaitSync(Isolate* isolate,
                                 Handle<JSArrayBuffer> array_buffer,
                                 size_t addr, T value, bool use_timeout,
                                 int64_t rel_timeout_ns, CallType call_type);

  template <typename T>
  static Tagged<Object> WaitAsync(Isolate* isolate,
                                  Handle<JSArrayBuffer> array_buffer,
                                  size_t addr, T value, bool use_timeout,
                                  int64_t rel_timeout_ns, CallType call_type);

  // Resolve the Promises of the async waiters which belong to |isolate|.
  static void ResolveAsyncWaiterPromises(Isolate* isolate);

  static void ResolveAsyncWaiterPromise(FutexWaitListNode* node);

  static void HandleAsyncWaiterTimeout(FutexWaitListNode* node);

  static void NotifyAsyncWaiter(FutexWaitListNode* node);

  // Remove the node's Promise from the NativeContext's Promise set.
  static void CleanupAsyncWaiterPromise(FutexWaitListNode* node);
};
}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_FUTEX_EMULATION_H_
