// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/microtask-queue.h"

#include <algorithm>
#include <cstddef>
#include <optional>

#include "src/api/api-inl.h"
#include "src/base/logging.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/objects/microtask-inl.h"
#include "src/objects/visitors.h"
#include "src/roots/roots-inl.h"
#include "src/tracing/trace-event.h"

namespace v8 {
namespace internal {

const size_t MicrotaskQueue::kRingBufferOffset =
    OFFSET_OF(MicrotaskQueue, ring_buffer_);
const size_t MicrotaskQueue::kCapacityOffset =
    OFFSET_OF(MicrotaskQueue, capacity_);
const size_t MicrotaskQueue::kSizeOffset = OFFSET_OF(MicrotaskQueue, size_);
const size_t MicrotaskQueue::kStartOffset = OFFSET_OF(MicrotaskQueue, start_);
const size_t MicrotaskQueue::kFinishedMicrotaskCountOffset =
    OFFSET_OF(MicrotaskQueue, finished_microtask_count_);

const intptr_t MicrotaskQueue::kMinimumCapacity = 8;

// static
void MicrotaskQueue::SetUpDefaultMicrotaskQueue(Isolate* isolate) {
  DCHECK_NULL(isolate->default_microtask_queue());

  MicrotaskQueue* microtask_queue = new MicrotaskQueue;
  microtask_queue->next_ = microtask_queue;
  microtask_queue->prev_ = microtask_queue;
  isolate->set_default_microtask_queue(microtask_queue);
}

// static
std::unique_ptr<MicrotaskQueue> MicrotaskQueue::New(Isolate* isolate) {
  DCHECK_NOT_NULL(isolate->default_microtask_queue());

  std::unique_ptr<MicrotaskQueue> microtask_queue(new MicrotaskQueue);

  // Insert the new instance to the next of last MicrotaskQueue instance.
  MicrotaskQueue* last = isolate->default_microtask_queue()->prev_;
  microtask_queue->next_ = last->next_;
  microtask_queue->prev_ = last;
  last->next_->prev_ = microtask_queue.get();
  last->next_ = microtask_queue.get();

  return microtask_queue;
}

MicrotaskQueue::MicrotaskQueue() = default;

MicrotaskQueue::~MicrotaskQueue() {
  if (next_ != this) {
    DCHECK_NE(prev_, this);
    next_->prev_ = prev_;
    prev_->next_ = next_;
  }
  delete[] ring_buffer_;
}

// static
Address MicrotaskQueue::CallEnqueueMicrotask(Isolate* isolate,
                                             intptr_t microtask_queue_pointer,
                                             Address raw_microtask) {
  Tagged<Microtask> microtask = Cast<Microtask>(Tagged<Object>(raw_microtask));
  reinterpret_cast<MicrotaskQueue*>(microtask_queue_pointer)
      ->EnqueueMicrotask(microtask);
  return Smi::zero().ptr();
}

void MicrotaskQueue::EnqueueMicrotask(v8::Isolate* v8_isolate,
                                      v8::Local<Function> function) {
  Isolate* isolate = reinterpret_cast<Isolate*>(v8_isolate);
  HandleScope scope(isolate);
  DirectHandle<CallableTask> microtask = isolate->factory()->NewCallableTask(
      Utils::OpenDirectHandle(*function), isolate->native_context());
  EnqueueMicrotask(*microtask);
}

void MicrotaskQueue::EnqueueMicrotask(v8::Isolate* v8_isolate,
                                      v8::MicrotaskCallback callback,
                                      void* data) {
  Isolate* isolate = reinterpret_cast<Isolate*>(v8_isolate);
  HandleScope scope(isolate);
  DirectHandle<CallbackTask> microtask = isolate->factory()->NewCallbackTask(
      isolate->factory()->NewForeign<kMicrotaskCallbackTag>(
          reinterpret_cast<Address>(callback)),
      isolate->factory()->NewForeign<kMicrotaskCallbackDataTag>(
          reinterpret_cast<Address>(data)));
  EnqueueMicrotask(*microtask);
}

void MicrotaskQueue::EnqueueMicrotask(Tagged<Microtask> microtask) {
  if (size_ == capacity_) {
    // Keep the capacity of |ring_buffer_| power of 2, so that the JIT
    // implementation can calculate the modulo easily.
    intptr_t new_capacity = std::max(kMinimumCapacity, capacity_ << 1);
    ResizeBuffer(new_capacity);
  }

  DCHECK_LT(size_, capacity_);
  ring_buffer_[(start_ + size_) % capacity_] = microtask.ptr();
  ++size_;
}

void MicrotaskQueue::PerformCheckpointInternal(v8::Isolate* v8_isolate) {
  DCHECK(ShouldPerfomCheckpoint());
  std::optional<MicrotasksScope> microtasks_scope;
  if (microtasks_policy_ == v8::MicrotasksPolicy::kScoped) {
    // If we're using microtask scopes to schedule microtask execution, V8
    // API calls will check that there's always a microtask scope on the
    // stack. As the microtasks we're about to execute could invoke embedder
    // callbacks which then calls back into V8, we create an artificial
    // microtask scope here to avoid running into the CallDepthScope check.
    microtasks_scope.emplace(v8_isolate, this,
                             v8::MicrotasksScope::kDoNotRunMicrotasks);
  }
  Isolate* isolate = reinterpret_cast<Isolate*>(v8_isolate);
  RunMicrotasks(isolate);
  isolate->ClearKeptObjects();
}

namespace {

class SetIsRunningMicrotasks {
 public:
  explicit SetIsRunningMicrotasks(bool* flag) : flag_(flag) {
    DCHECK(!*flag_);
    *flag_ = true;
  }

  ~SetIsRunningMicrotasks() {
    DCHECK(*flag_);
    *flag_ = false;
  }

 private:
  bool* flag_;
};

}  // namespace

int MicrotaskQueue::RunMicrotasks(Isolate* isolate) {
  SetIsRunningMicrotasks scope(&is_running_microtasks_);
  v8::Isolate::SuppressMicrotaskExecutionScope suppress(
      reinterpret_cast<v8::Isolate*>(isolate), this);

  if (!size()) {
    OnCompleted(isolate);
    return 0;
  }

  // We should not enter V8 if it's marked for termination.
  DCHECK_IMPLIES(v8_flags.strict_termination_checks,
                 !isolate->is_execution_terminating());

  intptr_t base_count = finished_microtask_count_;
  HandleScope handle_scope(isolate);
  MaybeDirectHandle<Object> maybe_result;

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  DirectHandle<Object> continuation_preserved_embedder_data(
      isolate->isolate_data()->continuation_preserved_embedder_data(), isolate);
  isolate->isolate_data()->set_continuation_preserved_embedder_data(
      ReadOnlyRoots(isolate).undefined_value());
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

  int processed_microtask_count;
  {
    HandleScopeImplementer::EnteredContextRewindScope rewind_scope(
        isolate->handle_scope_implementer());
    TRACE_EVENT_BEGIN0("v8.execute", "RunMicrotasks");
    {
      TRACE_EVENT_CALL_STATS_SCOPED(isolate, "v8", "V8.RunMicrotasks");
      maybe_result = Execution::TryRunMicrotasks(isolate, this);
      processed_microtask_count =
          static_cast<int>(finished_microtask_count_ - base_count);
    }
    TRACE_EVENT_END1("v8.execute", "RunMicrotasks", "microtask_count",
                     processed_microtask_count);
  }

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  isolate->isolate_data()->set_continuation_preserved_embedder_data(
      *continuation_preserved_embedder_data);
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

  if (isolate->is_execution_terminating()) {
    DCHECK(isolate->has_exception());
    DCHECK(maybe_result.is_null());
    delete[] ring_buffer_;
    ring_buffer_ = nullptr;
    capacity_ = 0;
    size_ = 0;
    start_ = 0;
    isolate->OnTerminationDuringRunMicrotasks();
    OnCompleted(isolate);
    return -1;
  }

  DCHECK_EQ(0, size());
  OnCompleted(isolate);

  return processed_microtask_count;
}

void MicrotaskQueue::IterateMicrotasks(RootVisitor* visitor) {
  if (size_) {
    // Iterate pending Microtasks as root objects to avoid the write barrier for
    // all single Microtask. If this hurts the GC performance, use a FixedArray.
    visitor->VisitRootPointers(
        Root::kMicroTasks, nullptr, FullObjectSlot(ring_buffer_ + start_),
        FullObjectSlot(ring_buffer_ + std::min(start_ + size_, capacity_)));
    visitor->VisitRootPointers(
        Root::kMicroTasks, nullptr, FullObjectSlot(ring_buffer_),
        FullObjectSlot(ring_buffer_ + std::max(start_ + size_ - capacity_,
                                               static_cast<intptr_t>(0))));
  }

  if (capacity_ <= kMinimumCapacity) {
    return;
  }

  intptr_t new_capacity = capacity_;
  while (new_capacity > 2 * size_) {
    new_capacity >>= 1;
  }
  new_capacity = std::max(new_capacity, kMinimumCapacity);
  if (new_capacity < capacity_) {
    ResizeBuffer(new_capacity);
  }
}

void MicrotaskQueue::AddMicrotasksCompletedCallback(
    MicrotasksCompletedCallbackWithData callback, void* data) {
  std::vector<CallbackWithData>* microtasks_completed_callbacks =
      &microtasks_completed_callbacks_;
  if (is_running_completed_callbacks_) {
    if (!microtasks_completed_callbacks_cow_.has_value()) {
      microtasks_completed_callbacks_cow_.emplace(
          microtasks_completed_callbacks_);
    }
    // Use the COW vector if we are iterating the callbacks right now.
    microtasks_completed_callbacks =
        &microtasks_completed_callbacks_cow_.value();
  }

  CallbackWithData callback_with_data(callback, data);
  const auto pos =
      std::find(microtasks_completed_callbacks->begin(),
                microtasks_completed_callbacks->end(), callback_with_data);
  if (pos != microtasks_completed_callbacks->end()) {
    return;
  }
  microtasks_completed_callbacks->push_back(callback_with_data);
}

void MicrotaskQueue::RemoveMicrotasksCompletedCallback(
    MicrotasksCompletedCallbackWithData callback, void* data) {
  std::vector<CallbackWithData>* microtasks_completed_callbacks =
      &microtasks_completed_callbacks_;
  if (is_running_completed_callbacks_) {
    if (!microtasks_completed_callbacks_cow_.has_value()) {
      microtasks_completed_callbacks_cow_.emplace(
          microtasks_completed_callbacks_);
    }
    // Use the COW vector if we are iterating the callbacks right now.
    microtasks_completed_callbacks =
        &microtasks_completed_callbacks_cow_.value();
  }

  CallbackWithData callback_with_data(callback, data);
  const auto pos =
      std::find(microtasks_completed_callbacks->begin(),
                microtasks_completed_callbacks->end(), callback_with_data);
  if (pos == microtasks_completed_callbacks->end()) {
    return;
  }
  microtasks_completed_callbacks->erase(pos);
}

void MicrotaskQueue::OnCompleted(Isolate* isolate) {
  is_running_completed_callbacks_ = true;
  for (auto& callback : microtasks_completed_callbacks_) {
    callback.first(reinterpret_cast<v8::Isolate*>(isolate), callback.second);
  }
  is_running_completed_callbacks_ = false;
  if (V8_UNLIKELY(microtasks_completed_callbacks_cow_.has_value())) {
    microtasks_completed_callbacks_ =
        std::move(microtasks_completed_callbacks_cow_.value());
    microtasks_completed_callbacks_cow_.reset();
  }
}

Tagged<Microtask> MicrotaskQueue::get(intptr_t index) const {
  DCHECK_LT(index, size_);
  Tagged<Object> microtask(ring_buffer_[(index + start_) % capacity_]);
  return Cast<Microtask>(microtask);
}

void MicrotaskQueue::ResizeBuffer(intptr_t new_capacity) {
  DCHECK_LE(size_, new_capacity);
  Address* new_ring_buffer = new Address[new_capacity];
  for (intptr_t i = 0; i < size_; ++i) {
    new_ring_buffer[i] = ring_buffer_[(start_ + i) % capacity_];
  }

  delete[] ring_buffer_;
  ring_buffer_ = new_ring_buffer;
  capacity_ = new_capacity;
  start_ = 0;
}

}  // namespace internal
}  // namespace v8
