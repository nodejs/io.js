// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logging/metrics.h"

#include "include/v8-platform.h"

namespace v8 {
namespace internal {
namespace metrics {

class Recorder::Task : public v8::Task {
 public:
  explicit Task(const std::shared_ptr<Recorder>& recorder)
      : recorder_(recorder) {}

  void Run() override {
    std::queue<std::unique_ptr<Recorder::DelayedEventBase>> delayed_events;
    {
      base::SpinningMutexGuard lock_scope(&recorder_->lock_);
      delayed_events.swap(recorder_->delayed_events_);
    }
    while (!delayed_events.empty()) {
      delayed_events.front()->Run(recorder_);
      delayed_events.pop();
    }
  }

 private:
  std::shared_ptr<Recorder> recorder_;
};

void Recorder::SetEmbedderRecorder(
    Isolate* isolate,
    const std::shared_ptr<v8::metrics::Recorder>& embedder_recorder) {
  foreground_task_runner_ = V8::GetCurrentPlatform()->GetForegroundTaskRunner(
      reinterpret_cast<v8::Isolate*>(isolate));
  CHECK_NULL(embedder_recorder_);
  embedder_recorder_ = embedder_recorder;
}

bool Recorder::HasEmbedderRecorder() const { return embedder_recorder_.get(); }

void Recorder::NotifyIsolateDisposal() {
  if (embedder_recorder_) {
    embedder_recorder_->NotifyIsolateDisposal();
  }
}

void Recorder::Delay(std::unique_ptr<Recorder::DelayedEventBase>&& event) {
  base::SpinningMutexGuard lock_scope(&lock_);
  bool was_empty = delayed_events_.empty();
  delayed_events_.push(std::move(event));
  if (was_empty) {
    foreground_task_runner_->PostDelayedTask(
        std::make_unique<Task>(shared_from_this()), 1.0);
  }
}

}  // namespace metrics
}  // namespace internal
}  // namespace v8
