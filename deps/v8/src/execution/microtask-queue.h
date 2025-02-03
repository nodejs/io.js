// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_MICROTASK_QUEUE_H_
#define V8_EXECUTION_MICROTASK_QUEUE_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "include/v8-internal.h"  // For Address.
#include "include/v8-microtask-queue.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

class Isolate;
class Microtask;
class Object;
class RootVisitor;
template <typename T>
class Tagged;

class V8_EXPORT_PRIVATE MicrotaskQueue final : public v8::MicrotaskQueue {
 public:
  static void SetUpDefaultMicrotaskQueue(Isolate* isolate);
  static std::unique_ptr<MicrotaskQueue> New(Isolate* isolate);

  ~MicrotaskQueue() override;

  // Uses raw Address values because it's called via ExternalReference.
  // {raw_microtask} is a tagged Microtask pointer.
  // Returns Smi::kZero due to CallCFunction.
  static Address CallEnqueueMicrotask(Isolate* isolate,
                                      intptr_t microtask_queue_pointer,
                                      Address raw_microtask);

  // v8::MicrotaskQueue implementations.
  void EnqueueMicrotask(v8::Isolate* isolate,
                        v8::Local<Function> microtask) override;
  void EnqueueMicrotask(v8::Isolate* isolate, v8::MicrotaskCallback callback,
                        void* data) override;
  void PerformCheckpoint(v8::Isolate* isolate) override {
    if (!ShouldPerfomCheckpoint()) return;
    PerformCheckpointInternal(isolate);
  }

  bool ShouldPerfomCheckpoint() const {
    return !IsRunningMicrotasks() && !GetMicrotasksScopeDepth() &&
           !HasMicrotasksSuppressions();
  }

  void EnqueueMicrotask(Tagged<Microtask> microtask);
  void AddMicrotasksCompletedCallback(
      MicrotasksCompletedCallbackWithData callback, void* data) override;
  void RemoveMicrotasksCompletedCallback(
      MicrotasksCompletedCallbackWithData callback, void* data) override;
  bool IsRunningMicrotasks() const override { return is_running_microtasks_; }

  // Runs all queued Microtasks.
  // Returns -1 if the execution is terminating, otherwise, returns the number
  // of microtasks that ran in this round.
  int RunMicrotasks(Isolate* isolate);

  // Iterate all pending Microtasks in this queue as strong roots, so that
  // builtins can update the queue directly without the write barrier.
  void IterateMicrotasks(RootVisitor* visitor);

  // Microtasks scope depth represents nested scopes controlling microtasks
  // invocation, which happens when depth reaches zero.
  void IncrementMicrotasksScopeDepth() { ++microtasks_depth_; }
  void DecrementMicrotasksScopeDepth() { --microtasks_depth_; }
  int GetMicrotasksScopeDepth() const override { return microtasks_depth_; }

  // Possibly nested microtasks suppression scopes prevent microtasks
  // from running.
  void IncrementMicrotasksSuppressions() { ++microtasks_suppressions_; }
  void DecrementMicrotasksSuppressions() { --microtasks_suppressions_; }
  bool HasMicrotasksSuppressions() const {
    return microtasks_suppressions_ != 0;
  }

#ifdef DEBUG
  // In debug we check that calls not intended to invoke microtasks are
  // still correctly wrapped with microtask scopes.
  void IncrementDebugMicrotasksScopeDepth() { ++debug_microtasks_depth_; }
  void DecrementDebugMicrotasksScopeDepth() { --debug_microtasks_depth_; }
  bool DebugMicrotasksScopeDepthIsZero() const {
    return debug_microtasks_depth_ == 0;
  }
#endif

  void set_microtasks_policy(v8::MicrotasksPolicy microtasks_policy) {
    microtasks_policy_ = microtasks_policy;
  }
  v8::MicrotasksPolicy microtasks_policy() const { return microtasks_policy_; }

  intptr_t capacity() const { return capacity_; }
  intptr_t size() const { return size_; }
  intptr_t start() const { return start_; }

  Tagged<Microtask> get(intptr_t index) const;

  MicrotaskQueue* next() const { return next_; }
  MicrotaskQueue* prev() const { return prev_; }

  static const size_t kRingBufferOffset;
  static const size_t kCapacityOffset;
  static const size_t kSizeOffset;
  static const size_t kStartOffset;
  static const size_t kFinishedMicrotaskCountOffset;

  static const intptr_t kMinimumCapacity;

 private:
  void PerformCheckpointInternal(v8::Isolate* v8_isolate);

  void OnCompleted(Isolate* isolate);

  MicrotaskQueue();
  void ResizeBuffer(intptr_t new_capacity);

  // A ring buffer to hold Microtask instances.
  // ring_buffer_[(start_ + i) % capacity_] contains |i|th Microtask for each
  // |i| in [0, size_).
  intptr_t size_ = 0;
  intptr_t capacity_ = 0;
  intptr_t start_ = 0;
  Address* ring_buffer_ = nullptr;

  // The number of finished microtask.
  intptr_t finished_microtask_count_ = 0;

  // MicrotaskQueue instances form a doubly linked list loop, so that all
  // instances are reachable through |next_|.
  MicrotaskQueue* next_ = nullptr;
  MicrotaskQueue* prev_ = nullptr;

  int microtasks_depth_ = 0;
  int microtasks_suppressions_ = 0;
#ifdef DEBUG
  int debug_microtasks_depth_ = 0;
#endif

  v8::MicrotasksPolicy microtasks_policy_ = v8::MicrotasksPolicy::kAuto;

  bool is_running_microtasks_ = false;
  bool is_running_completed_callbacks_ = false;
  using CallbackWithData =
      std::pair<MicrotasksCompletedCallbackWithData, void*>;
  std::vector<CallbackWithData> microtasks_completed_callbacks_;
  std::vector<CallbackWithData> microtasks_completed_callbacks_cow_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_MICROTASK_QUEUE_H_
