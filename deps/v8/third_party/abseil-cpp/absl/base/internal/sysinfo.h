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
// This file includes routines to find out characteristics
// of the machine a program is running on.  It is undoubtedly
// system-dependent.

// Functions listed here that accept a pid_t as an argument act on the
// current process if the pid_t argument is 0
// All functions here are thread-hostile due to file caching unless
// commented otherwise.

#ifndef ABSL_BASE_INTERNAL_SYSINFO_H_
#define ABSL_BASE_INTERNAL_SYSINFO_H_

#ifndef _WIN32
#include <sys/types.h>
#endif

#include <cstdint>

#include "absl/base/config.h"
#include "absl/base/port.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// Nominal core processor cycles per second of each processor.   This is _not_
// necessarily the frequency of the CycleClock counter (see cycleclock.h)
// Thread-safe.
double NominalCPUFrequency();

// Number of logical processors (hyperthreads) in system. Thread-safe.
int NumCPUs();

// Return the thread id of the current thread, as told by the system.
// No two currently-live threads implemented by the OS shall have the same ID.
// Thread ids of exited threads may be reused.   Multiple user-level threads
// may have the same thread ID if multiplexed on the same OS thread.
//
// On Linux, you may send a signal to the resulting ID with kill().  However,
// it is recommended for portability that you use pthread_kill() instead.
#ifdef _WIN32
// On Windows, process id and thread id are of the same type according to the
// return types of GetProcessId() and GetThreadId() are both DWORD, an unsigned
// 32-bit type.
using pid_t = uint32_t;
#endif
pid_t GetTID();

// Like GetTID(), but caches the result in thread-local storage in order
// to avoid unnecessary system calls. Note that there are some cases where
// one must call through to GetTID directly, which is why this exists as a
// separate function. For example, GetCachedTID() is not safe to call in
// an asynchronous signal-handling context nor right after a call to fork().
pid_t GetCachedTID();

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_SYSINFO_H_
