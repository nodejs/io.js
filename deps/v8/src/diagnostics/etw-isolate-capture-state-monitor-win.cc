// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/etw-isolate-capture-state-monitor-win.h"

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/diagnostics/etw-debug-win.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

EtwIsolateCaptureStateMonitor::EtwIsolateCaptureStateMonitor(
    base::Mutex* mutex, size_t pending_isolate_count)
    : mutex_(mutex), pending_isolate_count_(pending_isolate_count) {}

bool EtwIsolateCaptureStateMonitor::WaitFor(const base::TimeDelta& delta) {
  wait_started_ = base::TimeTicks::Now();
  base::TimeDelta remaining = delta;

  if (pending_isolate_count_ == 0) {
    return true;
  }

  ETWTRACEDBG << "Waiting for " << pending_isolate_count_
              << " isolates for up to " << remaining.InMilliseconds()
              << std::endl;
  while (isolates_ready_cv_.WaitFor(mutex_, remaining)) {
    ETWTRACEDBG << "WaitFor woke up: " << pending_isolate_count_
                << " isolates remaining " << std::endl;
    if (pending_isolate_count_ == 0) {
      return true;
    }

    // If the timeout has expired, return false.
    auto elapsed = base::TimeTicks::Now() - wait_started_;
    if (elapsed >= remaining) {
      ETWTRACEDBG << "Elapsed is " << elapsed.InMilliseconds()
                  << " greater than reminaing " << remaining.InMilliseconds()
                  << std::endl;
      return false;
    }

    // If the condition variable was woken up spuriously, adjust the timeout.
    remaining -= elapsed;
    ETWTRACEDBG << "New remaining " << remaining.InMilliseconds()
                << " resuming waiting" << std::endl;
  }

  // Propagate the WaitFor false return value (timeout before being notified) to
  // the caller.
  return false;
}

void EtwIsolateCaptureStateMonitor::Notify() {
  {
    ETWTRACEDBG << "Notify taking mutex" << std::endl;
    base::MutexGuard lock(mutex_);
    pending_isolate_count_--;
    ETWTRACEDBG << "Got mutex and isolate count reduced to "
                << pending_isolate_count_ << std::endl;
  }
  ETWTRACEDBG << "Released mutex preparing to notifyOne " << std::endl;
  isolates_ready_cv_.NotifyOne();
  ETWTRACEDBG << "Finished notifyOne " << std::endl;
}

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8
