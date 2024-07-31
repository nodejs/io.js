// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Core interfaces and definitions used by by low-level interfaces such as
// SpinLock.

#ifndef ABSL_BASE_INTERNAL_LOW_LEVEL_SCHEDULING_H_
#define ABSL_BASE_INTERNAL_LOW_LEVEL_SCHEDULING_H_

#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/scheduling_mode.h"
#include "absl/base/macros.h"

// The following two declarations exist so SchedulingGuard may friend them with
// the appropriate language linkage.  These callbacks allow libc internals, such
// as function level statics, to schedule cooperatively when locking.
extern "C" bool __google_disable_rescheduling(void);
extern "C" void __google_enable_rescheduling(bool disable_result);

namespace absl {
ABSL_NAMESPACE_BEGIN
class CondVar;
class Mutex;

namespace synchronization_internal {
int MutexDelay(int32_t c, int mode);
}  // namespace synchronization_internal

namespace base_internal {

class SchedulingHelper;  // To allow use of SchedulingGuard.
class SpinLock;          // To allow use of SchedulingGuard.

// SchedulingGuard
// Provides guard semantics that may be used to disable cooperative rescheduling
// of the calling thread within specific program blocks.  This is used to
// protect resources (e.g. low-level SpinLocks or Domain code) that cooperative
// scheduling depends on.
//
// Domain implementations capable of rescheduling in reaction to involuntary
// kernel thread actions (e.g blocking due to a pagefault or syscall) must
// guarantee that an annotated thread is not allowed to (cooperatively)
// reschedule until the annotated region is complete.
//
// It is an error to attempt to use a cooperatively scheduled resource (e.g.
// Mutex) within a rescheduling-disabled region.
//
// All methods are async-signal safe.
class SchedulingGuard {
 public:
  // Returns true iff the calling thread may be cooperatively rescheduled.
  static bool ReschedulingIsAllowed();
  SchedulingGuard(const SchedulingGuard&) = delete;
  SchedulingGuard& operator=(const SchedulingGuard&) = delete;

 private:
  // Disable cooperative rescheduling of the calling thread.  It may still
  // initiate scheduling operations (e.g. wake-ups), however, it may not itself
  // reschedule.  Nestable.  The returned result is opaque, clients should not
  // attempt to interpret it.
  // REQUIRES: Result must be passed to a pairing EnableScheduling().
  static bool DisableRescheduling();

  // Marks the end of a rescheduling disabled region, previously started by
  // DisableRescheduling().
  // REQUIRES: Pairs with innermost call (and result) of DisableRescheduling().
  static void EnableRescheduling(bool disable_result);

  // A scoped helper for {Disable, Enable}Rescheduling().
  // REQUIRES: destructor must run in same thread as constructor.
  struct ScopedDisable {
    ScopedDisable() { disabled = SchedulingGuard::DisableRescheduling(); }
    ~ScopedDisable() { SchedulingGuard::EnableRescheduling(disabled); }

    bool disabled;
  };

  // A scoped helper to enable rescheduling temporarily.
  // REQUIRES: destructor must run in same thread as constructor.
  class ScopedEnable {
   public:
    ScopedEnable();
    ~ScopedEnable();

   private:
    int scheduling_disabled_depth_;
  };

  // Access to SchedulingGuard is explicitly permitted.
  friend class absl::CondVar;
  friend class absl::Mutex;
  friend class SchedulingHelper;
  friend class SpinLock;
  friend int absl::synchronization_internal::MutexDelay(int32_t c, int mode);
};

//------------------------------------------------------------------------------
// End of public interfaces.
//------------------------------------------------------------------------------

inline bool SchedulingGuard::ReschedulingIsAllowed() {
  return false;
}

inline bool SchedulingGuard::DisableRescheduling() {
  return false;
}

inline void SchedulingGuard::EnableRescheduling(bool /* disable_result */) {
  return;
}

inline SchedulingGuard::ScopedEnable::ScopedEnable()
    : scheduling_disabled_depth_(0) {}
inline SchedulingGuard::ScopedEnable::~ScopedEnable() {
  ABSL_RAW_CHECK(scheduling_disabled_depth_ == 0, "disable unused warning");
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_LOW_LEVEL_SCHEDULING_H_
