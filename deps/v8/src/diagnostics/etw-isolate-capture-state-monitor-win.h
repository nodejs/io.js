// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DIAGNOSTICS_ETW_ISOLATE_CAPTURE_STATE_MONITOR_WIN_H_
#define V8_DIAGNOSTICS_ETW_ISOLATE_CAPTURE_STATE_MONITOR_WIN_H_

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

class V8_EXPORT_PRIVATE EtwIsolateCaptureStateMonitor {
 public:
  EtwIsolateCaptureStateMonitor(base::Mutex* mutex,
                                size_t pending_isolate_count);
  EtwIsolateCaptureStateMonitor(const EtwIsolateCaptureStateMonitor&) = delete;
  EtwIsolateCaptureStateMonitor& operator=(
      const EtwIsolateCaptureStateMonitor&) = delete;

  // Call from ETW callback thread to wait for the specified time or until
  // Notify is called pending_isolate_count times.
  bool WaitFor(const base::TimeDelta& delta);

  // Called from isolate thread to unblock WaitFor.
  void Notify();

 private:
  // Must be held prior to calling WaitFor.
  // Also used to sychronize access when reading/writing the isolate_count_.
  base::Mutex* mutex_;
  size_t pending_isolate_count_;
  base::ConditionVariable isolates_ready_cv_;
  // Used to track when WaitFor started and how much of the original timeout
  // remains when recovering from spurious wakeups.
  base::TimeTicks wait_started_;
};

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8

#endif  // V8_DIAGNOSTICS_ETW_ISOLATE_CAPTURE_STATE_MONITOR_WIN_H_
