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

#include <stdint.h>

#include <new>

// This file is a no-op if the required LowLevelAlloc support is missing.
#include "absl/base/internal/low_level_alloc.h"
#include "absl/synchronization/internal/waiter.h"
#ifndef ABSL_LOW_LEVEL_ALLOC_MISSING

#include <string.h>

#include "absl/base/attributes.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/synchronization/internal/per_thread_sem.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

// ThreadIdentity storage is persistent, we maintain a free-list of previously
// released ThreadIdentity objects.
ABSL_CONST_INIT static base_internal::SpinLock freelist_lock(
    absl::kConstInit, base_internal::SCHEDULE_KERNEL_ONLY);
ABSL_CONST_INIT static base_internal::ThreadIdentity* thread_identity_freelist;

// A per-thread destructor for reclaiming associated ThreadIdentity objects.
// Since we must preserve their storage we cache them for re-use.
static void ReclaimThreadIdentity(void* v) {
  base_internal::ThreadIdentity* identity =
      static_cast<base_internal::ThreadIdentity*>(v);

  // all_locks might have been allocated by the Mutex implementation.
  // We free it here when we are notified that our thread is dying.
  if (identity->per_thread_synch.all_locks != nullptr) {
    base_internal::LowLevelAlloc::Free(identity->per_thread_synch.all_locks);
  }

  // We must explicitly clear the current thread's identity:
  // (a) Subsequent (unrelated) per-thread destructors may require an identity.
  //     We must guarantee a new identity is used in this case (this instructor
  //     will be reinvoked up to PTHREAD_DESTRUCTOR_ITERATIONS in this case).
  // (b) ThreadIdentity implementations may depend on memory that is not
  //     reinitialized before reuse.  We must allow explicit clearing of the
  //     association state in this case.
  base_internal::ClearCurrentThreadIdentity();
  {
    base_internal::SpinLockHolder l(&freelist_lock);
    identity->next = thread_identity_freelist;
    thread_identity_freelist = identity;
  }
}

// Return value rounded up to next multiple of align.
// Align must be a power of two.
static intptr_t RoundUp(intptr_t addr, intptr_t align) {
  return (addr + align - 1) & ~(align - 1);
}

void OneTimeInitThreadIdentity(base_internal::ThreadIdentity* identity) {
  PerThreadSem::Init(identity);
  identity->ticker.store(0, std::memory_order_relaxed);
  identity->wait_start.store(0, std::memory_order_relaxed);
  identity->is_idle.store(false, std::memory_order_relaxed);
}

static void ResetThreadIdentityBetweenReuse(
    base_internal::ThreadIdentity* identity) {
  base_internal::PerThreadSynch* pts = &identity->per_thread_synch;
  pts->next = nullptr;
  pts->skip = nullptr;
  pts->may_skip = false;
  pts->waitp = nullptr;
  pts->suppress_fatal_errors = false;
  pts->readers = 0;
  pts->priority = 0;
  pts->next_priority_read_cycles = 0;
  pts->state.store(base_internal::PerThreadSynch::State::kAvailable,
                   std::memory_order_relaxed);
  pts->maybe_unlocking = false;
  pts->wake = false;
  pts->cond_waiter = false;
  pts->all_locks = nullptr;
  identity->blocked_count_ptr = nullptr;
  identity->ticker.store(0, std::memory_order_relaxed);
  identity->wait_start.store(0, std::memory_order_relaxed);
  identity->is_idle.store(false, std::memory_order_relaxed);
  identity->next = nullptr;
}

static base_internal::ThreadIdentity* NewThreadIdentity() {
  base_internal::ThreadIdentity* identity = nullptr;

  {
    // Re-use a previously released object if possible.
    base_internal::SpinLockHolder l(&freelist_lock);
    if (thread_identity_freelist) {
      identity = thread_identity_freelist;  // Take list-head.
      thread_identity_freelist = thread_identity_freelist->next;
    }
  }

  if (identity == nullptr) {
    // Allocate enough space to align ThreadIdentity to a multiple of
    // PerThreadSynch::kAlignment. This space is never released (it is
    // added to a freelist by ReclaimThreadIdentity instead).
    void* allocation = base_internal::LowLevelAlloc::Alloc(
        sizeof(*identity) + base_internal::PerThreadSynch::kAlignment - 1);
    // Round up the address to the required alignment.
    identity = reinterpret_cast<base_internal::ThreadIdentity*>(
        RoundUp(reinterpret_cast<intptr_t>(allocation),
                base_internal::PerThreadSynch::kAlignment));
    OneTimeInitThreadIdentity(identity);
  }
  ResetThreadIdentityBetweenReuse(identity);

  return identity;
}

// Allocates and attaches ThreadIdentity object for the calling thread.  Returns
// the new identity.
// REQUIRES: CurrentThreadIdentity(false) == nullptr
base_internal::ThreadIdentity* CreateThreadIdentity() {
  base_internal::ThreadIdentity* identity = NewThreadIdentity();
  // Associate the value with the current thread, and attach our destructor.
  base_internal::SetCurrentThreadIdentity(identity, ReclaimThreadIdentity);
  return identity;
}

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_LOW_LEVEL_ALLOC_MISSING
