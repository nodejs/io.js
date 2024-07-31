// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/gc-invoker.h"

#include <memory>

#include "include/cppgc/common.h"
#include "include/cppgc/platform.h"
#include "src/heap/cppgc/task-handle.h"

namespace cppgc {
namespace internal {

class GCInvoker::GCInvokerImpl final : public GarbageCollector {
 public:
  GCInvokerImpl(GarbageCollector*, cppgc::Platform*, cppgc::Heap::StackSupport);
  ~GCInvokerImpl();

  GCInvokerImpl(const GCInvokerImpl&) = delete;
  GCInvokerImpl& operator=(const GCInvokerImpl&) = delete;

  void CollectGarbage(GCConfig) final;
  void StartIncrementalGarbageCollection(GCConfig) final;
  size_t epoch() const final { return collector_->epoch(); }
  std::optional<EmbedderStackState> overridden_stack_state() const final {
    return collector_->overridden_stack_state();
  }
  void set_override_stack_state(EmbedderStackState state) final {
    collector_->set_override_stack_state(state);
  }
  void clear_overridden_stack_state() final {
    collector_->clear_overridden_stack_state();
  }
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  v8::base::Optional<int> UpdateAllocationTimeout() final {
    return v8::base::nullopt;
  }
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

 private:
  class GCTask final : public cppgc::Task {
   public:
    using Handle = SingleThreadedHandle;

    static Handle Post(GarbageCollector* collector, cppgc::TaskRunner* runner,
                       GCConfig config) {
      auto task =
          std::make_unique<GCInvoker::GCInvokerImpl::GCTask>(collector, config);
      auto handle = task->GetHandle();
      runner->PostNonNestableTask(std::move(task));
      return handle;
    }

    explicit GCTask(GarbageCollector* collector, GCConfig config)
        : collector_(collector),
          config_(config),
          handle_(Handle::NonEmptyTag{}),
          saved_epoch_(collector->epoch()) {}

   private:
    void Run() final {
      if (handle_.IsCanceled() || (collector_->epoch() != saved_epoch_)) return;

      collector_->set_override_stack_state(EmbedderStackState::kNoHeapPointers);
      collector_->CollectGarbage(config_);
      collector_->clear_overridden_stack_state();
      handle_.Cancel();
    }

    Handle GetHandle() { return handle_; }

    GarbageCollector* collector_;
    GCConfig config_;
    Handle handle_;
    size_t saved_epoch_;
  };

  GarbageCollector* collector_;
  cppgc::Platform* platform_;
  cppgc::Heap::StackSupport stack_support_;
  GCTask::Handle gc_task_handle_;
};

GCInvoker::GCInvokerImpl::GCInvokerImpl(GarbageCollector* collector,
                                        cppgc::Platform* platform,
                                        cppgc::Heap::StackSupport stack_support)
    : collector_(collector),
      platform_(platform),
      stack_support_(stack_support) {}

GCInvoker::GCInvokerImpl::~GCInvokerImpl() {
  if (gc_task_handle_) {
    gc_task_handle_.Cancel();
  }
}

void GCInvoker::GCInvokerImpl::CollectGarbage(GCConfig config) {
  DCHECK_EQ(config.marking_type, cppgc::Heap::MarkingType::kAtomic);
  if ((config.stack_state == StackState::kNoHeapPointers) ||
      (stack_support_ ==
       cppgc::Heap::StackSupport::kSupportsConservativeStackScan)) {
    collector_->CollectGarbage(config);
  } else if (platform_->GetForegroundTaskRunner() &&
             platform_->GetForegroundTaskRunner()->NonNestableTasksEnabled()) {
    if (!gc_task_handle_) {
      // Force a precise GC since it will run in a non-nestable task.
      config.stack_state = StackState::kNoHeapPointers;
      DCHECK_NE(cppgc::Heap::StackSupport::kSupportsConservativeStackScan,
                stack_support_);
      gc_task_handle_ = GCTask::Post(
          collector_, platform_->GetForegroundTaskRunner().get(), config);
    }
  }
}

void GCInvoker::GCInvokerImpl::StartIncrementalGarbageCollection(
    GCConfig config) {
  DCHECK_NE(config.marking_type, cppgc::Heap::MarkingType::kAtomic);
  if ((stack_support_ !=
       cppgc::Heap::StackSupport::kSupportsConservativeStackScan) &&
      (!platform_->GetForegroundTaskRunner() ||
       !platform_->GetForegroundTaskRunner()->NonNestableTasksEnabled())) {
    // In this configuration the GC finalization can only be triggered through
    // ForceGarbageCollectionSlow. If incremental GC is started, there is no
    // way to know how long it will remain enabled (and the write barrier with
    // it). For that reason, we do not support running incremental GCs in this
    // configuration.
    return;
  }
  // No need to postpone starting incremental GC since the stack is not scanned
  // until GC finalization.
  collector_->StartIncrementalGarbageCollection(config);
}

GCInvoker::GCInvoker(GarbageCollector* collector, cppgc::Platform* platform,
                     cppgc::Heap::StackSupport stack_support)
    : impl_(std::make_unique<GCInvoker::GCInvokerImpl>(collector, platform,
                                                       stack_support)) {}

GCInvoker::~GCInvoker() = default;

void GCInvoker::CollectGarbage(GCConfig config) {
  impl_->CollectGarbage(config);
}

void GCInvoker::StartIncrementalGarbageCollection(GCConfig config) {
  impl_->StartIncrementalGarbageCollection(config);
}

size_t GCInvoker::epoch() const { return impl_->epoch(); }

std::optional<EmbedderStackState> GCInvoker::overridden_stack_state() const {
  return impl_->overridden_stack_state();
}

void GCInvoker::set_override_stack_state(EmbedderStackState state) {
  impl_->set_override_stack_state(state);
}

void GCInvoker::clear_overridden_stack_state() {
  impl_->clear_overridden_stack_state();
}

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
v8::base::Optional<int> GCInvoker::UpdateAllocationTimeout() {
  return impl_->UpdateAllocationTimeout();
}
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

}  // namespace internal
}  // namespace cppgc
