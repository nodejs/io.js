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

#ifndef ABSL_SYNCHRONIZATION_INTERNAL_WAITER_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_WAITER_H_

#include "absl/base/config.h"
#include "absl/synchronization/internal/futex_waiter.h"
#include "absl/synchronization/internal/pthread_waiter.h"
#include "absl/synchronization/internal/sem_waiter.h"
#include "absl/synchronization/internal/stdcpp_waiter.h"
#include "absl/synchronization/internal/win32_waiter.h"

// May be chosen at compile time via -DABSL_FORCE_WAITER_MODE=<index>
#define ABSL_WAITER_MODE_FUTEX 0
#define ABSL_WAITER_MODE_SEM 1
#define ABSL_WAITER_MODE_CONDVAR 2
#define ABSL_WAITER_MODE_WIN32 3
#define ABSL_WAITER_MODE_STDCPP 4

#if defined(ABSL_FORCE_WAITER_MODE)
#define ABSL_WAITER_MODE ABSL_FORCE_WAITER_MODE
#elif defined(ABSL_INTERNAL_HAVE_WIN32_WAITER)
#define ABSL_WAITER_MODE ABSL_WAITER_MODE_WIN32
#elif defined(ABSL_INTERNAL_HAVE_FUTEX_WAITER)
#define ABSL_WAITER_MODE ABSL_WAITER_MODE_FUTEX
#elif defined(ABSL_INTERNAL_HAVE_SEM_WAITER)
#define ABSL_WAITER_MODE ABSL_WAITER_MODE_SEM
#elif defined(ABSL_INTERNAL_HAVE_PTHREAD_WAITER)
#define ABSL_WAITER_MODE ABSL_WAITER_MODE_CONDVAR
#elif defined(ABSL_INTERNAL_HAVE_STDCPP_WAITER)
#define ABSL_WAITER_MODE ABSL_WAITER_MODE_STDCPP
#else
#error ABSL_WAITER_MODE is undefined
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

#if ABSL_WAITER_MODE == ABSL_WAITER_MODE_FUTEX
using Waiter = FutexWaiter;
#elif ABSL_WAITER_MODE == ABSL_WAITER_MODE_SEM
using Waiter = SemWaiter;
#elif ABSL_WAITER_MODE == ABSL_WAITER_MODE_CONDVAR
using Waiter = PthreadWaiter;
#elif ABSL_WAITER_MODE == ABSL_WAITER_MODE_WIN32
using Waiter = Win32Waiter;
#elif ABSL_WAITER_MODE == ABSL_WAITER_MODE_STDCPP
using Waiter = StdcppWaiter;
#endif

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_SYNCHRONIZATION_INTERNAL_WAITER_H_
