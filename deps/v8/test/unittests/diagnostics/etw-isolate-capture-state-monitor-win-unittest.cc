// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/diagnostics/etw-isolate-capture-state-monitor-win.h"

#include <thread>  // NOLINT(build/c++11)

#include "src/base/platform/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace ETWJITInterface {

TEST(EtwIsolateCaptureStateMonitorTest, Timeout) {
  base::Mutex mutex;
  base::MutexGuard guard(&mutex);

  auto monitor = std::make_shared<EtwIsolateCaptureStateMonitor>(
      &mutex, 1 /*isolate_count*/);
  bool completed = monitor->WaitFor(base::TimeDelta::FromSeconds(1));

  ASSERT_FALSE(completed);
}

TEST(EtwIsolateCaptureStateMonitorTest, TimeoutWithOneNotify) {
  base::Mutex mutex;
  base::MutexGuard guard(&mutex);

  auto monitor = std::make_shared<EtwIsolateCaptureStateMonitor>(
      &mutex, 2 /*isolate_count*/);
  std::thread t1([monitor]() { monitor->Notify(); });

  bool completed = monitor->WaitFor(base::TimeDelta::FromSeconds(1));

  t1.join();

  ASSERT_FALSE(completed);
}

TEST(EtwIsolateCaptureStateMonitorTest, Completed) {
  base::Mutex mutex;
  base::MutexGuard guard(&mutex);

  auto monitor = std::make_shared<EtwIsolateCaptureStateMonitor>(
      &mutex, 2 /*isolate_count*/);
  std::thread t1([monitor]() { monitor->Notify(); });
  std::thread t2([monitor]() { monitor->Notify(); });

  bool completed = monitor->WaitFor(base::TimeDelta::FromSeconds(10));

  t1.join();
  t2.join();

  ASSERT_TRUE(completed);
}

TEST(EtwIsolateCaptureStateMonitorTest, DontBlockOnZeroIsolateCount) {
  base::Mutex mutex;
  base::MutexGuard guard(&mutex);

  auto monitor = std::make_shared<EtwIsolateCaptureStateMonitor>(
      &mutex, 0 /*isolate_count*/);
  bool completed = monitor->WaitFor(base::TimeDelta::FromSeconds(1));

  ASSERT_TRUE(completed);
}

}  // namespace ETWJITInterface
}  // namespace internal
}  // namespace v8
