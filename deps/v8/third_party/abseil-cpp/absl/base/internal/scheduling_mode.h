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

#ifndef ABSL_BASE_INTERNAL_SCHEDULING_MODE_H_
#define ABSL_BASE_INTERNAL_SCHEDULING_MODE_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// Used to describe how a thread may be scheduled.  Typically associated with
// the declaration of a resource supporting synchronized access.
//
// SCHEDULE_COOPERATIVE_AND_KERNEL:
// Specifies that when waiting, a cooperative thread (e.g. a Fiber) may
// reschedule (using base::scheduling semantics); allowing other cooperative
// threads to proceed.
//
// SCHEDULE_KERNEL_ONLY: (Also described as "non-cooperative")
// Specifies that no cooperative scheduling semantics may be used, even if the
// current thread is itself cooperatively scheduled.  This means that
// cooperative threads will NOT allow other cooperative threads to execute in
// their place while waiting for a resource of this type.  Host operating system
// semantics (e.g. a futex) may still be used.
//
// When optional, clients should strongly prefer SCHEDULE_COOPERATIVE_AND_KERNEL
// by default.  SCHEDULE_KERNEL_ONLY should only be used for resources on which
// base::scheduling (e.g. the implementation of a Scheduler) may depend.
//
// NOTE: Cooperative resources may not be nested below non-cooperative ones.
// This means that it is invalid to to acquire a SCHEDULE_COOPERATIVE_AND_KERNEL
// resource if a SCHEDULE_KERNEL_ONLY resource is already held.
enum SchedulingMode {
  SCHEDULE_KERNEL_ONLY = 0,         // Allow scheduling only the host OS.
  SCHEDULE_COOPERATIVE_AND_KERNEL,  // Also allow cooperative scheduling.
};

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_SCHEDULING_MODE_H_
