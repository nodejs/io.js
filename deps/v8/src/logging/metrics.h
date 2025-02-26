// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LOGGING_METRICS_H_
#define V8_LOGGING_METRICS_H_

#include <memory>
#include <queue>

#include "include/v8-metrics.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/init/v8.h"

namespace v8 {

class TaskRunner;

namespace internal {
namespace metrics {

class Recorder : public std::enable_shared_from_this<Recorder> {
 public:
  V8_EXPORT_PRIVATE void SetEmbedderRecorder(
      Isolate* isolate,
      const std::shared_ptr<v8::metrics::Recorder>& embedder_recorder);

  V8_EXPORT_PRIVATE bool HasEmbedderRecorder() const;

  V8_EXPORT_PRIVATE void NotifyIsolateDisposal();

  template <class T>
  void AddMainThreadEvent(const T& event,
                          v8::metrics::Recorder::ContextId id) {
    if (embedder_recorder_)
      embedder_recorder_->AddMainThreadEvent(event, id);
  }

  template <class T>
  void DelayMainThreadEvent(const T& event,
                            v8::metrics::Recorder::ContextId id) {
    if (!embedder_recorder_) return;
    Delay(std::make_unique<DelayedEvent<T>>(event, id));
  }

  template <class T>
  void AddThreadSafeEvent(const T& event) {
    if (embedder_recorder_) embedder_recorder_->AddThreadSafeEvent(event);
  }

 private:
  class DelayedEventBase {
   public:
    virtual ~DelayedEventBase() = default;

    virtual void Run(const std::shared_ptr<Recorder>& recorder) = 0;
  };

  template <class T>
  class DelayedEvent : public DelayedEventBase {
   public:
    DelayedEvent(const T& event, v8::metrics::Recorder::ContextId id)
        : event_(event), id_(id) {}

    void Run(const std::shared_ptr<Recorder>& recorder) override {
      recorder->AddMainThreadEvent(event_, id_);
    }

   protected:
    T event_;
    v8::metrics::Recorder::ContextId id_;
  };

  class Task;

  V8_EXPORT_PRIVATE void Delay(
      std::unique_ptr<Recorder::DelayedEventBase>&& event);

  base::SpinningMutex lock_;
  std::shared_ptr<v8::TaskRunner> foreground_task_runner_;
  std::shared_ptr<v8::metrics::Recorder> embedder_recorder_;
  std::queue<std::unique_ptr<DelayedEventBase>> delayed_events_;
};

template <class T, int64_t (base::TimeDelta::*precision)() const =
                       &base::TimeDelta::InMicroseconds>
class V8_NODISCARD TimedScope {
 public:
  explicit TimedScope(T* event) : event_(event) { Start(); }
  ~TimedScope() { Stop(); }

  void Start() { start_time_ = base::TimeTicks::Now(); }

  void Stop() {
    if (start_time_.IsMin()) return;
    base::TimeDelta duration = base::TimeTicks::Now() - start_time_;
    event_->wall_clock_duration_in_us = (duration.*precision)();
    start_time_ = base::TimeTicks::Min();
  }

 private:
  T* event_;
  base::TimeTicks start_time_;
};

}  // namespace metrics
}  // namespace internal
}  // namespace v8

#endif  // V8_LOGGING_METRICS_H_
