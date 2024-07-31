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

#include "absl/synchronization/mutex.h"

#ifdef _WIN32
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#else
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/time.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <thread>  // NOLINT(build/c++11)

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/config.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/atomic_hook.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/base/internal/hide_ptr.h"
#include "absl/base/internal/low_level_alloc.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/internal/tsan_mutex_interface.h"
#include "absl/base/optimization.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/synchronization/internal/graphcycles.h"
#include "absl/synchronization/internal/per_thread_sem.h"
#include "absl/time/time.h"

using absl::base_internal::CurrentThreadIdentityIfPresent;
using absl::base_internal::CycleClock;
using absl::base_internal::PerThreadSynch;
using absl::base_internal::SchedulingGuard;
using absl::base_internal::ThreadIdentity;
using absl::synchronization_internal::GetOrCreateCurrentThreadIdentity;
using absl::synchronization_internal::GraphCycles;
using absl::synchronization_internal::GraphId;
using absl::synchronization_internal::InvalidGraphId;
using absl::synchronization_internal::KernelTimeout;
using absl::synchronization_internal::PerThreadSem;

extern "C" {
ABSL_ATTRIBUTE_WEAK void ABSL_INTERNAL_C_SYMBOL(AbslInternalMutexYield)() {
  std::this_thread::yield();
}
}  // extern "C"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace {

#if defined(ABSL_HAVE_THREAD_SANITIZER)
constexpr OnDeadlockCycle kDeadlockDetectionDefault = OnDeadlockCycle::kIgnore;
#else
constexpr OnDeadlockCycle kDeadlockDetectionDefault = OnDeadlockCycle::kAbort;
#endif

ABSL_CONST_INIT std::atomic<OnDeadlockCycle> synch_deadlock_detection(
    kDeadlockDetectionDefault);
ABSL_CONST_INIT std::atomic<bool> synch_check_invariants(false);

ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES
absl::base_internal::AtomicHook<void (*)(int64_t wait_cycles)>
    submit_profile_data;
ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES absl::base_internal::AtomicHook<void (*)(
    const char* msg, const void* obj, int64_t wait_cycles)>
    mutex_tracer;
ABSL_INTERNAL_ATOMIC_HOOK_ATTRIBUTES
absl::base_internal::AtomicHook<void (*)(const char* msg, const void* cv)>
    cond_var_tracer;

}  // namespace

static inline bool EvalConditionAnnotated(const Condition* cond, Mutex* mu,
                                          bool locking, bool trylock,
                                          bool read_lock);

void RegisterMutexProfiler(void (*fn)(int64_t wait_cycles)) {
  submit_profile_data.Store(fn);
}

void RegisterMutexTracer(void (*fn)(const char* msg, const void* obj,
                                    int64_t wait_cycles)) {
  mutex_tracer.Store(fn);
}

void RegisterCondVarTracer(void (*fn)(const char* msg, const void* cv)) {
  cond_var_tracer.Store(fn);
}

namespace {
// Represents the strategy for spin and yield.
// See the comment in GetMutexGlobals() for more information.
enum DelayMode { AGGRESSIVE, GENTLE };

struct ABSL_CACHELINE_ALIGNED MutexGlobals {
  absl::once_flag once;
  // Note: this variable is initialized separately in Mutex::LockSlow,
  // so that Mutex::Lock does not have a stack frame in optimized build.
  std::atomic<int> spinloop_iterations{0};
  int32_t mutex_sleep_spins[2] = {};
  absl::Duration mutex_sleep_time;
};

ABSL_CONST_INIT static MutexGlobals globals;

absl::Duration MeasureTimeToYield() {
  absl::Time before = absl::Now();
  ABSL_INTERNAL_C_SYMBOL(AbslInternalMutexYield)();
  return absl::Now() - before;
}

const MutexGlobals& GetMutexGlobals() {
  absl::base_internal::LowLevelCallOnce(&globals.once, [&]() {
    if (absl::base_internal::NumCPUs() > 1) {
      // If the mode is aggressive then spin many times before yielding.
      // If the mode is gentle then spin only a few times before yielding.
      // Aggressive spinning is used to ensure that an Unlock() call,
      // which must get the spin lock for any thread to make progress gets it
      // without undue delay.
      globals.mutex_sleep_spins[AGGRESSIVE] = 5000;
      globals.mutex_sleep_spins[GENTLE] = 250;
      globals.mutex_sleep_time = absl::Microseconds(10);
    } else {
      // If this a uniprocessor, only yield/sleep. Real-time threads are often
      // unable to yield, so the sleep time needs to be long enough to keep
      // the calling thread asleep until scheduling happens.
      globals.mutex_sleep_spins[AGGRESSIVE] = 0;
      globals.mutex_sleep_spins[GENTLE] = 0;
      globals.mutex_sleep_time = MeasureTimeToYield() * 5;
      globals.mutex_sleep_time =
          std::min(globals.mutex_sleep_time, absl::Milliseconds(1));
      globals.mutex_sleep_time =
          std::max(globals.mutex_sleep_time, absl::Microseconds(10));
    }
  });
  return globals;
}
}  // namespace

namespace synchronization_internal {
// Returns the Mutex delay on iteration `c` depending on the given `mode`.
// The returned value should be used as `c` for the next call to `MutexDelay`.
int MutexDelay(int32_t c, int mode) {
  const int32_t limit = GetMutexGlobals().mutex_sleep_spins[mode];
  const absl::Duration sleep_time = GetMutexGlobals().mutex_sleep_time;
  if (c < limit) {
    // Spin.
    c++;
  } else {
    SchedulingGuard::ScopedEnable enable_rescheduling;
    ABSL_TSAN_MUTEX_PRE_DIVERT(nullptr, 0);
    if (c == limit) {
      // Yield once.
      ABSL_INTERNAL_C_SYMBOL(AbslInternalMutexYield)();
      c++;
    } else {
      // Then wait.
      absl::SleepFor(sleep_time);
      c = 0;
    }
    ABSL_TSAN_MUTEX_POST_DIVERT(nullptr, 0);
  }
  return c;
}
}  // namespace synchronization_internal

// --------------------------Generic atomic ops
// Ensure that "(*pv & bits) == bits" by doing an atomic update of "*pv" to
// "*pv | bits" if necessary.  Wait until (*pv & wait_until_clear)==0
// before making any change.
// Returns true if bits were previously unset and set by the call.
// This is used to set flags in mutex and condition variable words.
static bool AtomicSetBits(std::atomic<intptr_t>* pv, intptr_t bits,
                          intptr_t wait_until_clear) {
  for (;;) {
    intptr_t v = pv->load(std::memory_order_relaxed);
    if ((v & bits) == bits) {
      return false;
    }
    if ((v & wait_until_clear) != 0) {
      continue;
    }
    if (pv->compare_exchange_weak(v, v | bits, std::memory_order_release,
                                  std::memory_order_relaxed)) {
      return true;
    }
  }
}

//------------------------------------------------------------------

// Data for doing deadlock detection.
ABSL_CONST_INIT static absl::base_internal::SpinLock deadlock_graph_mu(
    absl::kConstInit, base_internal::SCHEDULE_KERNEL_ONLY);

// Graph used to detect deadlocks.
ABSL_CONST_INIT static GraphCycles* deadlock_graph
    ABSL_GUARDED_BY(deadlock_graph_mu) ABSL_PT_GUARDED_BY(deadlock_graph_mu);

//------------------------------------------------------------------
// An event mechanism for debugging mutex use.
// It also allows mutexes to be given names for those who can't handle
// addresses, and instead like to give their data structures names like
// "Henry", "Fido", or "Rupert IV, King of Yondavia".

namespace {  // to prevent name pollution
enum {       // Mutex and CondVar events passed as "ev" to PostSynchEvent
             // Mutex events
  SYNCH_EV_TRYLOCK_SUCCESS,
  SYNCH_EV_TRYLOCK_FAILED,
  SYNCH_EV_READERTRYLOCK_SUCCESS,
  SYNCH_EV_READERTRYLOCK_FAILED,
  SYNCH_EV_LOCK,
  SYNCH_EV_LOCK_RETURNING,
  SYNCH_EV_READERLOCK,
  SYNCH_EV_READERLOCK_RETURNING,
  SYNCH_EV_UNLOCK,
  SYNCH_EV_READERUNLOCK,

  // CondVar events
  SYNCH_EV_WAIT,
  SYNCH_EV_WAIT_RETURNING,
  SYNCH_EV_SIGNAL,
  SYNCH_EV_SIGNALALL,
};

enum {                    // Event flags
  SYNCH_F_R = 0x01,       // reader event
  SYNCH_F_LCK = 0x02,     // PostSynchEvent called with mutex held
  SYNCH_F_TRY = 0x04,     // TryLock or ReaderTryLock
  SYNCH_F_UNLOCK = 0x08,  // Unlock or ReaderUnlock

  SYNCH_F_LCK_W = SYNCH_F_LCK,
  SYNCH_F_LCK_R = SYNCH_F_LCK | SYNCH_F_R,
};
}  // anonymous namespace

// Properties of the events.
static const struct {
  int flags;
  const char* msg;
} event_properties[] = {
    {SYNCH_F_LCK_W | SYNCH_F_TRY, "TryLock succeeded "},
    {0, "TryLock failed "},
    {SYNCH_F_LCK_R | SYNCH_F_TRY, "ReaderTryLock succeeded "},
    {0, "ReaderTryLock failed "},
    {0, "Lock blocking "},
    {SYNCH_F_LCK_W, "Lock returning "},
    {0, "ReaderLock blocking "},
    {SYNCH_F_LCK_R, "ReaderLock returning "},
    {SYNCH_F_LCK_W | SYNCH_F_UNLOCK, "Unlock "},
    {SYNCH_F_LCK_R | SYNCH_F_UNLOCK, "ReaderUnlock "},
    {0, "Wait on "},
    {0, "Wait unblocked "},
    {0, "Signal on "},
    {0, "SignalAll on "},
};

ABSL_CONST_INIT static absl::base_internal::SpinLock synch_event_mu(
    absl::kConstInit, base_internal::SCHEDULE_KERNEL_ONLY);

// Hash table size; should be prime > 2.
// Can't be too small, as it's used for deadlock detection information.
static constexpr uint32_t kNSynchEvent = 1031;

static struct SynchEvent {  // this is a trivial hash table for the events
  // struct is freed when refcount reaches 0
  int refcount ABSL_GUARDED_BY(synch_event_mu);

  // buckets have linear, 0-terminated  chains
  SynchEvent* next ABSL_GUARDED_BY(synch_event_mu);

  // Constant after initialization
  uintptr_t masked_addr;  // object at this address is called "name"

  // No explicit synchronization used.  Instead we assume that the
  // client who enables/disables invariants/logging on a Mutex does so
  // while the Mutex is not being concurrently accessed by others.
  void (*invariant)(void* arg);  // called on each event
  void* arg;                     // first arg to (*invariant)()
  bool log;                      // logging turned on

  // Constant after initialization
  char name[1];  // actually longer---NUL-terminated string
}* synch_event[kNSynchEvent] ABSL_GUARDED_BY(synch_event_mu);

// Ensure that the object at "addr" has a SynchEvent struct associated with it,
// set "bits" in the word there (waiting until lockbit is clear before doing
// so), and return a refcounted reference that will remain valid until
// UnrefSynchEvent() is called.  If a new SynchEvent is allocated,
// the string name is copied into it.
// When used with a mutex, the caller should also ensure that kMuEvent
// is set in the mutex word, and similarly for condition variables and kCVEvent.
static SynchEvent* EnsureSynchEvent(std::atomic<intptr_t>* addr,
                                    const char* name, intptr_t bits,
                                    intptr_t lockbit) {
  uint32_t h = reinterpret_cast<uintptr_t>(addr) % kNSynchEvent;
  synch_event_mu.Lock();
  // When a Mutex/CondVar is destroyed, we don't remove the associated
  // SynchEvent to keep destructors empty in release builds for performance
  // reasons. If the current call is the first to set bits (kMuEvent/kCVEvent),
  // we don't look up the existing even because (if it exists, it must be for
  // the previous Mutex/CondVar that existed at the same address).
  // The leaking events must not be a problem for tests, which should create
  // bounded amount of events. And debug logging is not supposed to be enabled
  // in production. However, if it's accidentally enabled, or briefly enabled
  // for some debugging, we don't want to crash the program. Instead we drop
  // all events, if we accumulated too many of them. Size of a single event
  // is ~48 bytes, so 100K events is ~5 MB.
  // Additionally we could delete the old event for the same address,
  // but it would require a better hashmap (if we accumulate too many events,
  // linked lists will grow and traversing them will be very slow).
  constexpr size_t kMaxSynchEventCount = 100 << 10;
  // Total number of live synch events.
  static size_t synch_event_count ABSL_GUARDED_BY(synch_event_mu);
  if (++synch_event_count > kMaxSynchEventCount) {
    synch_event_count = 0;
    ABSL_RAW_LOG(ERROR,
                 "Accumulated %zu Mutex debug objects. If you see this"
                 " in production, it may mean that the production code"
                 " accidentally calls "
                 "Mutex/CondVar::EnableDebugLog/EnableInvariantDebugging.",
                 kMaxSynchEventCount);
    for (auto*& head : synch_event) {
      for (auto* e = head; e != nullptr;) {
        SynchEvent* next = e->next;
        if (--(e->refcount) == 0) {
          base_internal::LowLevelAlloc::Free(e);
        }
        e = next;
      }
      head = nullptr;
    }
  }
  SynchEvent* e = nullptr;
  if (!AtomicSetBits(addr, bits, lockbit)) {
    for (e = synch_event[h];
         e != nullptr && e->masked_addr != base_internal::HidePtr(addr);
         e = e->next) {
    }
  }
  if (e == nullptr) {  // no SynchEvent struct found; make one.
    if (name == nullptr) {
      name = "";
    }
    size_t l = strlen(name);
    e = reinterpret_cast<SynchEvent*>(
        base_internal::LowLevelAlloc::Alloc(sizeof(*e) + l));
    e->refcount = 2;  // one for return value, one for linked list
    e->masked_addr = base_internal::HidePtr(addr);
    e->invariant = nullptr;
    e->arg = nullptr;
    e->log = false;
    strcpy(e->name, name);  // NOLINT(runtime/printf)
    e->next = synch_event[h];
    synch_event[h] = e;
  } else {
    e->refcount++;  // for return value
  }
  synch_event_mu.Unlock();
  return e;
}

// Decrement the reference count of *e, or do nothing if e==null.
static void UnrefSynchEvent(SynchEvent* e) {
  if (e != nullptr) {
    synch_event_mu.Lock();
    bool del = (--(e->refcount) == 0);
    synch_event_mu.Unlock();
    if (del) {
      base_internal::LowLevelAlloc::Free(e);
    }
  }
}

// Return a refcounted reference to the SynchEvent of the object at address
// "addr", if any.  The pointer returned is valid until the UnrefSynchEvent() is
// called.
static SynchEvent* GetSynchEvent(const void* addr) {
  uint32_t h = reinterpret_cast<uintptr_t>(addr) % kNSynchEvent;
  SynchEvent* e;
  synch_event_mu.Lock();
  for (e = synch_event[h];
       e != nullptr && e->masked_addr != base_internal::HidePtr(addr);
       e = e->next) {
  }
  if (e != nullptr) {
    e->refcount++;
  }
  synch_event_mu.Unlock();
  return e;
}

// Called when an event "ev" occurs on a Mutex of CondVar "obj"
// if event recording is on
static void PostSynchEvent(void* obj, int ev) {
  SynchEvent* e = GetSynchEvent(obj);
  // logging is on if event recording is on and either there's no event struct,
  // or it explicitly says to log
  if (e == nullptr || e->log) {
    void* pcs[40];
    int n = absl::GetStackTrace(pcs, ABSL_ARRAYSIZE(pcs), 1);
    // A buffer with enough space for the ASCII for all the PCs, even on a
    // 64-bit machine.
    char buffer[ABSL_ARRAYSIZE(pcs) * 24];
    int pos = snprintf(buffer, sizeof(buffer), " @");
    for (int i = 0; i != n; i++) {
      int b = snprintf(&buffer[pos], sizeof(buffer) - static_cast<size_t>(pos),
                       " %p", pcs[i]);
      if (b < 0 ||
          static_cast<size_t>(b) >= sizeof(buffer) - static_cast<size_t>(pos)) {
        break;
      }
      pos += b;
    }
    ABSL_RAW_LOG(INFO, "%s%p %s %s", event_properties[ev].msg, obj,
                 (e == nullptr ? "" : e->name), buffer);
  }
  const int flags = event_properties[ev].flags;
  if ((flags & SYNCH_F_LCK) != 0 && e != nullptr && e->invariant != nullptr) {
    // Calling the invariant as is causes problems under ThreadSanitizer.
    // We are currently inside of Mutex Lock/Unlock and are ignoring all
    // memory accesses and synchronization. If the invariant transitively
    // synchronizes something else and we ignore the synchronization, we will
    // get false positive race reports later.
    // Reuse EvalConditionAnnotated to properly call into user code.
    struct local {
      static bool pred(SynchEvent* ev) {
        (*ev->invariant)(ev->arg);
        return false;
      }
    };
    Condition cond(&local::pred, e);
    Mutex* mu = static_cast<Mutex*>(obj);
    const bool locking = (flags & SYNCH_F_UNLOCK) == 0;
    const bool trylock = (flags & SYNCH_F_TRY) != 0;
    const bool read_lock = (flags & SYNCH_F_R) != 0;
    EvalConditionAnnotated(&cond, mu, locking, trylock, read_lock);
  }
  UnrefSynchEvent(e);
}

//------------------------------------------------------------------

// The SynchWaitParams struct encapsulates the way in which a thread is waiting:
// whether it has a timeout, the condition, exclusive/shared, and whether a
// condition variable wait has an associated Mutex (as opposed to another
// type of lock).  It also points to the PerThreadSynch struct of its thread.
// cv_word tells Enqueue() to enqueue on a CondVar using CondVarEnqueue().
//
// This structure is held on the stack rather than directly in
// PerThreadSynch because a thread can be waiting on multiple Mutexes if,
// while waiting on one Mutex, the implementation calls a client callback
// (such as a Condition function) that acquires another Mutex. We don't
// strictly need to allow this, but programmers become confused if we do not
// allow them to use functions such a LOG() within Condition functions.  The
// PerThreadSynch struct points at the most recent SynchWaitParams struct when
// the thread is on a Mutex's waiter queue.
struct SynchWaitParams {
  SynchWaitParams(Mutex::MuHow how_arg, const Condition* cond_arg,
                  KernelTimeout timeout_arg, Mutex* cvmu_arg,
                  PerThreadSynch* thread_arg,
                  std::atomic<intptr_t>* cv_word_arg)
      : how(how_arg),
        cond(cond_arg),
        timeout(timeout_arg),
        cvmu(cvmu_arg),
        thread(thread_arg),
        cv_word(cv_word_arg),
        contention_start_cycles(CycleClock::Now()),
        should_submit_contention_data(false) {}

  const Mutex::MuHow how;  // How this thread needs to wait.
  const Condition* cond;   // The condition that this thread is waiting for.
                           // In Mutex, this field is set to zero if a timeout
                           // expires.
  KernelTimeout timeout;  // timeout expiry---absolute time
                          // In Mutex, this field is set to zero if a timeout
                          // expires.
  Mutex* const cvmu;      // used for transfer from cond var to mutex
  PerThreadSynch* const thread;  // thread that is waiting

  // If not null, thread should be enqueued on the CondVar whose state
  // word is cv_word instead of queueing normally on the Mutex.
  std::atomic<intptr_t>* cv_word;

  int64_t contention_start_cycles;  // Time (in cycles) when this thread started
                                    // to contend for the mutex.
  bool should_submit_contention_data;
};

struct SynchLocksHeld {
  int n;          // number of valid entries in locks[]
  bool overflow;  // true iff we overflowed the array at some point
  struct {
    Mutex* mu;      // lock acquired
    int32_t count;  // times acquired
    GraphId id;     // deadlock_graph id of acquired lock
  } locks[40];
  // If a thread overfills the array during deadlock detection, we
  // continue, discarding information as needed.  If no overflow has
  // taken place, we can provide more error checking, such as
  // detecting when a thread releases a lock it does not hold.
};

// A sentinel value in lists that is not 0.
// A 0 value is used to mean "not on a list".
static PerThreadSynch* const kPerThreadSynchNull =
    reinterpret_cast<PerThreadSynch*>(1);

static SynchLocksHeld* LocksHeldAlloc() {
  SynchLocksHeld* ret = reinterpret_cast<SynchLocksHeld*>(
      base_internal::LowLevelAlloc::Alloc(sizeof(SynchLocksHeld)));
  ret->n = 0;
  ret->overflow = false;
  return ret;
}

// Return the PerThreadSynch-struct for this thread.
static PerThreadSynch* Synch_GetPerThread() {
  ThreadIdentity* identity = GetOrCreateCurrentThreadIdentity();
  return &identity->per_thread_synch;
}

static PerThreadSynch* Synch_GetPerThreadAnnotated(Mutex* mu) {
  if (mu) {
    ABSL_TSAN_MUTEX_PRE_DIVERT(mu, 0);
  }
  PerThreadSynch* w = Synch_GetPerThread();
  if (mu) {
    ABSL_TSAN_MUTEX_POST_DIVERT(mu, 0);
  }
  return w;
}

static SynchLocksHeld* Synch_GetAllLocks() {
  PerThreadSynch* s = Synch_GetPerThread();
  if (s->all_locks == nullptr) {
    s->all_locks = LocksHeldAlloc();  // Freed by ReclaimThreadIdentity.
  }
  return s->all_locks;
}

// Post on "w"'s associated PerThreadSem.
void Mutex::IncrementSynchSem(Mutex* mu, PerThreadSynch* w) {
  static_cast<void>(mu);  // Prevent unused param warning in non-TSAN builds.
  ABSL_TSAN_MUTEX_PRE_DIVERT(mu, 0);
  // We miss synchronization around passing PerThreadSynch between threads
  // since it happens inside of the Mutex code, so we need to ignore all
  // accesses to the object.
  ABSL_ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN();
  PerThreadSem::Post(w->thread_identity());
  ABSL_ANNOTATE_IGNORE_READS_AND_WRITES_END();
  ABSL_TSAN_MUTEX_POST_DIVERT(mu, 0);
}

// Wait on "w"'s associated PerThreadSem; returns false if timeout expired.
bool Mutex::DecrementSynchSem(Mutex* mu, PerThreadSynch* w, KernelTimeout t) {
  static_cast<void>(mu);  // Prevent unused param warning in non-TSAN builds.
  ABSL_TSAN_MUTEX_PRE_DIVERT(mu, 0);
  assert(w == Synch_GetPerThread());
  static_cast<void>(w);
  bool res = PerThreadSem::Wait(t);
  ABSL_TSAN_MUTEX_POST_DIVERT(mu, 0);
  return res;
}

// We're in a fatal signal handler that hopes to use Mutex and to get
// lucky by not deadlocking.  We try to improve its chances of success
// by effectively disabling some of the consistency checks.  This will
// prevent certain ABSL_RAW_CHECK() statements from being triggered when
// re-rentry is detected.  The ABSL_RAW_CHECK() statements are those in the
// Mutex code checking that the "waitp" field has not been reused.
void Mutex::InternalAttemptToUseMutexInFatalSignalHandler() {
  // Fix the per-thread state only if it exists.
  ThreadIdentity* identity = CurrentThreadIdentityIfPresent();
  if (identity != nullptr) {
    identity->per_thread_synch.suppress_fatal_errors = true;
  }
  // Don't do deadlock detection when we are already failing.
  synch_deadlock_detection.store(OnDeadlockCycle::kIgnore,
                                 std::memory_order_release);
}

// --------------------------Mutexes

// In the layout below, the msb of the bottom byte is currently unused.  Also,
// the following constraints were considered in choosing the layout:
//  o Both the debug allocator's "uninitialized" and "freed" patterns (0xab and
//    0xcd) are illegal: reader and writer lock both held.
//  o kMuWriter and kMuEvent should exceed kMuDesig and kMuWait, to enable the
//    bit-twiddling trick in Mutex::Unlock().
//  o kMuWriter / kMuReader == kMuWrWait / kMuWait,
//    to enable the bit-twiddling trick in CheckForMutexCorruption().
static const intptr_t kMuReader = 0x0001L;  // a reader holds the lock
// There's a designated waker.
// INVARIANT1:  there's a thread that was blocked on the mutex, is
// no longer, yet has not yet acquired the mutex.  If there's a
// designated waker, all threads can avoid taking the slow path in
// unlock because the designated waker will subsequently acquire
// the lock and wake someone.  To maintain INVARIANT1 the bit is
// set when a thread is unblocked(INV1a), and threads that were
// unblocked reset the bit when they either acquire or re-block (INV1b).
static const intptr_t kMuDesig = 0x0002L;
static const intptr_t kMuWait = 0x0004L;    // threads are waiting
static const intptr_t kMuWriter = 0x0008L;  // a writer holds the lock
static const intptr_t kMuEvent = 0x0010L;   // record this mutex's events
// Runnable writer is waiting for a reader.
// If set, new readers will not lock the mutex to avoid writer starvation.
// Note: if a reader has higher priority than the writer, it will still lock
// the mutex ahead of the waiting writer, but in a very inefficient manner:
// the reader will first queue itself and block, but then the last unlocking
// reader will wake it.
static const intptr_t kMuWrWait = 0x0020L;
static const intptr_t kMuSpin = 0x0040L;  // spinlock protects wait list
static const intptr_t kMuLow = 0x00ffL;   // mask all mutex bits
static const intptr_t kMuHigh = ~kMuLow;  // mask pointer/reader count

// Hack to make constant values available to gdb pretty printer
enum {
  kGdbMuSpin = kMuSpin,
  kGdbMuEvent = kMuEvent,
  kGdbMuWait = kMuWait,
  kGdbMuWriter = kMuWriter,
  kGdbMuDesig = kMuDesig,
  kGdbMuWrWait = kMuWrWait,
  kGdbMuReader = kMuReader,
  kGdbMuLow = kMuLow,
};

// kMuWrWait implies kMuWait.
// kMuReader and kMuWriter are mutually exclusive.
// If kMuReader is zero, there are no readers.
// Otherwise, if kMuWait is zero, the high order bits contain a count of the
// number of readers.  Otherwise, the reader count is held in
// PerThreadSynch::readers of the most recently queued waiter, again in the
// bits above kMuLow.
static const intptr_t kMuOne = 0x0100;  // a count of one reader

// flags passed to Enqueue and LockSlow{,WithTimeout,Loop}
static const int kMuHasBlocked = 0x01;  // already blocked (MUST == 1)
static const int kMuIsCond = 0x02;      // conditional waiter (CV or Condition)
static const int kMuIsFer = 0x04;       // wait morphing from a CondVar

static_assert(PerThreadSynch::kAlignment > kMuLow,
              "PerThreadSynch::kAlignment must be greater than kMuLow");

// This struct contains various bitmasks to be used in
// acquiring and releasing a mutex in a particular mode.
struct MuHowS {
  // if all the bits in fast_need_zero are zero, the lock can be acquired by
  // adding fast_add and oring fast_or.  The bit kMuDesig should be reset iff
  // this is the designated waker.
  intptr_t fast_need_zero;
  intptr_t fast_or;
  intptr_t fast_add;

  intptr_t slow_need_zero;  // fast_need_zero with events (e.g. logging)

  intptr_t slow_inc_need_zero;  // if all the bits in slow_inc_need_zero are
                                // zero a reader can acquire a read share by
                                // setting the reader bit and incrementing
                                // the reader count (in last waiter since
                                // we're now slow-path).  kMuWrWait be may
                                // be ignored if we already waited once.
};

static const MuHowS kSharedS = {
    // shared or read lock
    kMuWriter | kMuWait | kMuEvent,   // fast_need_zero
    kMuReader,                        // fast_or
    kMuOne,                           // fast_add
    kMuWriter | kMuWait,              // slow_need_zero
    kMuSpin | kMuWriter | kMuWrWait,  // slow_inc_need_zero
};
static const MuHowS kExclusiveS = {
    // exclusive or write lock
    kMuWriter | kMuReader | kMuEvent,  // fast_need_zero
    kMuWriter,                         // fast_or
    0,                                 // fast_add
    kMuWriter | kMuReader,             // slow_need_zero
    ~static_cast<intptr_t>(0),         // slow_inc_need_zero
};
static const Mutex::MuHow kShared = &kSharedS;        // shared lock
static const Mutex::MuHow kExclusive = &kExclusiveS;  // exclusive lock

#ifdef NDEBUG
static constexpr bool kDebugMode = false;
#else
static constexpr bool kDebugMode = true;
#endif

#ifdef ABSL_INTERNAL_HAVE_TSAN_INTERFACE
static unsigned TsanFlags(Mutex::MuHow how) {
  return how == kShared ? __tsan_mutex_read_lock : 0;
}
#endif

#if defined(__APPLE__) || defined(ABSL_BUILD_DLL)
// When building a dll symbol export lists may reference the destructor
// and want it to be an exported symbol rather than an inline function.
// Some apple builds also do dynamic library build but don't say it explicitly.
Mutex::~Mutex() { Dtor(); }
#endif

#if !defined(NDEBUG) || defined(ABSL_HAVE_THREAD_SANITIZER)
void Mutex::Dtor() {
  if (kDebugMode) {
    this->ForgetDeadlockInfo();
  }
  ABSL_TSAN_MUTEX_DESTROY(this, __tsan_mutex_not_static);
}
#endif

void Mutex::EnableDebugLog(const char* name) {
  // Need to disable writes here and in EnableInvariantDebugging to prevent
  // false race reports on SynchEvent objects. TSan ignores synchronization
  // on synch_event_mu in Lock/Unlock/etc methods due to mutex annotations,
  // but it sees few accesses to SynchEvent in EvalConditionAnnotated.
  // If we don't ignore accesses here, it can result in false races
  // between EvalConditionAnnotated and SynchEvent reuse in EnsureSynchEvent.
  ABSL_ANNOTATE_IGNORE_WRITES_BEGIN();
  SynchEvent* e = EnsureSynchEvent(&this->mu_, name, kMuEvent, kMuSpin);
  e->log = true;
  UnrefSynchEvent(e);
  // This prevents "error: undefined symbol: absl::Mutex::~Mutex()"
  // in a release build (NDEBUG defined) when a test does "#undef NDEBUG"
  // to use assert macro. In such case, the test does not get the dtor
  // definition because it's supposed to be outline when NDEBUG is not defined,
  // and this source file does not define one either because NDEBUG is defined.
  // Since it's not possible to take address of a destructor, we move the
  // actual destructor code into the separate Dtor function and force the
  // compiler to emit this function even if it's inline by taking its address.
  ABSL_ATTRIBUTE_UNUSED volatile auto dtor = &Mutex::Dtor;
  ABSL_ANNOTATE_IGNORE_WRITES_END();
}

void EnableMutexInvariantDebugging(bool enabled) {
  synch_check_invariants.store(enabled, std::memory_order_release);
}

void Mutex::EnableInvariantDebugging(void (*invariant)(void*), void* arg) {
  ABSL_ANNOTATE_IGNORE_WRITES_BEGIN();
  if (synch_check_invariants.load(std::memory_order_acquire) &&
      invariant != nullptr) {
    SynchEvent* e = EnsureSynchEvent(&this->mu_, nullptr, kMuEvent, kMuSpin);
    e->invariant = invariant;
    e->arg = arg;
    UnrefSynchEvent(e);
  }
  ABSL_ANNOTATE_IGNORE_WRITES_END();
}

void SetMutexDeadlockDetectionMode(OnDeadlockCycle mode) {
  synch_deadlock_detection.store(mode, std::memory_order_release);
}

// Return true iff threads x and y are part of the same equivalence
// class of waiters. An equivalence class is defined as the set of
// waiters with the same condition, type of lock, and thread priority.
//
// Requires that x and y be waiting on the same Mutex queue.
static bool MuEquivalentWaiter(PerThreadSynch* x, PerThreadSynch* y) {
  return x->waitp->how == y->waitp->how && x->priority == y->priority &&
         Condition::GuaranteedEqual(x->waitp->cond, y->waitp->cond);
}

// Given the contents of a mutex word containing a PerThreadSynch pointer,
// return the pointer.
static inline PerThreadSynch* GetPerThreadSynch(intptr_t v) {
  return reinterpret_cast<PerThreadSynch*>(v & kMuHigh);
}

// The next several routines maintain the per-thread next and skip fields
// used in the Mutex waiter queue.
// The queue is a circular singly-linked list, of which the "head" is the
// last element, and head->next if the first element.
// The skip field has the invariant:
//   For thread x, x->skip is one of:
//     - invalid (iff x is not in a Mutex wait queue),
//     - null, or
//     - a pointer to a distinct thread waiting later in the same Mutex queue
//       such that all threads in [x, x->skip] have the same condition, priority
//       and lock type (MuEquivalentWaiter() is true for all pairs in [x,
//       x->skip]).
// In addition, if x->skip is  valid, (x->may_skip || x->skip == null)
//
// By the spec of MuEquivalentWaiter(), it is not necessary when removing the
// first runnable thread y from the front a Mutex queue to adjust the skip
// field of another thread x because if x->skip==y, x->skip must (have) become
// invalid before y is removed.  The function TryRemove can remove a specified
// thread from an arbitrary position in the queue whether runnable or not, so
// it fixes up skip fields that would otherwise be left dangling.
// The statement
//     if (x->may_skip && MuEquivalentWaiter(x, x->next)) { x->skip = x->next; }
// maintains the invariant provided x is not the last waiter in a Mutex queue
// The statement
//          if (x->skip != null) { x->skip = x->skip->skip; }
// maintains the invariant.

// Returns the last thread y in a mutex waiter queue such that all threads in
// [x, y] inclusive share the same condition.  Sets skip fields of some threads
// in that range to optimize future evaluation of Skip() on x values in
// the range.  Requires thread x is in a mutex waiter queue.
// The locking is unusual.  Skip() is called under these conditions:
//   - spinlock is held in call from Enqueue(), with maybe_unlocking == false
//   - Mutex is held in call from UnlockSlow() by last unlocker, with
//     maybe_unlocking == true
//   - both Mutex and spinlock are held in call from DequeueAllWakeable() (from
//     UnlockSlow()) and TryRemove()
// These cases are mutually exclusive, so Skip() never runs concurrently
// with itself on the same Mutex.   The skip chain is used in these other places
// that cannot occur concurrently:
//   - FixSkip() (from TryRemove()) - spinlock and Mutex are held)
//   - Dequeue() (with spinlock and Mutex held)
//   - UnlockSlow() (with spinlock and Mutex held)
// A more complex case is Enqueue()
//   - Enqueue() (with spinlock held and maybe_unlocking == false)
//               This is the first case in which Skip is called, above.
//   - Enqueue() (without spinlock held; but queue is empty and being freshly
//                formed)
//   - Enqueue() (with spinlock held and maybe_unlocking == true)
// The first case has mutual exclusion, and the second isolation through
// working on an otherwise unreachable data structure.
// In the last case, Enqueue() is required to change no skip/next pointers
// except those in the added node and the former "head" node.  This implies
// that the new node is added after head, and so must be the new head or the
// new front of the queue.
static PerThreadSynch* Skip(PerThreadSynch* x) {
  PerThreadSynch* x0 = nullptr;
  PerThreadSynch* x1 = x;
  PerThreadSynch* x2 = x->skip;
  if (x2 != nullptr) {
    // Each iteration attempts to advance sequence (x0,x1,x2) to next sequence
    // such that   x1 == x0->skip && x2 == x1->skip
    while ((x0 = x1, x1 = x2, x2 = x2->skip) != nullptr) {
      x0->skip = x2;  // short-circuit skip from x0 to x2
    }
    x->skip = x1;  // short-circuit skip from x to result
  }
  return x1;
}

// "ancestor" appears before "to_be_removed" in the same Mutex waiter queue.
// The latter is going to be removed out of order, because of a timeout.
// Check whether "ancestor" has a skip field pointing to "to_be_removed",
// and fix it if it does.
static void FixSkip(PerThreadSynch* ancestor, PerThreadSynch* to_be_removed) {
  if (ancestor->skip == to_be_removed) {  // ancestor->skip left dangling
    if (to_be_removed->skip != nullptr) {
      ancestor->skip = to_be_removed->skip;  // can skip past to_be_removed
    } else if (ancestor->next != to_be_removed) {  // they are not adjacent
      ancestor->skip = ancestor->next;             // can skip one past ancestor
    } else {
      ancestor->skip = nullptr;  // can't skip at all
    }
  }
}

static void CondVarEnqueue(SynchWaitParams* waitp);

// Enqueue thread "waitp->thread" on a waiter queue.
// Called with mutex spinlock held if head != nullptr
// If head==nullptr and waitp->cv_word==nullptr, then Enqueue() is
// idempotent; it alters no state associated with the existing (empty)
// queue.
//
// If waitp->cv_word == nullptr, queue the thread at either the front or
// the end (according to its priority) of the circular mutex waiter queue whose
// head is "head", and return the new head.  mu is the previous mutex state,
// which contains the reader count (perhaps adjusted for the operation in
// progress) if the list was empty and a read lock held, and the holder hint if
// the list was empty and a write lock held.  (flags & kMuIsCond) indicates
// whether this thread was transferred from a CondVar or is waiting for a
// non-trivial condition.  In this case, Enqueue() never returns nullptr
//
// If waitp->cv_word != nullptr, CondVarEnqueue() is called, and "head" is
// returned. This mechanism is used by CondVar to queue a thread on the
// condition variable queue instead of the mutex queue in implementing Wait().
// In this case, Enqueue() can return nullptr (if head==nullptr).
static PerThreadSynch* Enqueue(PerThreadSynch* head, SynchWaitParams* waitp,
                               intptr_t mu, int flags) {
  // If we have been given a cv_word, call CondVarEnqueue() and return
  // the previous head of the Mutex waiter queue.
  if (waitp->cv_word != nullptr) {
    CondVarEnqueue(waitp);
    return head;
  }

  PerThreadSynch* s = waitp->thread;
  ABSL_RAW_CHECK(
      s->waitp == nullptr ||    // normal case
          s->waitp == waitp ||  // Fer()---transfer from condition variable
          s->suppress_fatal_errors,
      "detected illegal recursion into Mutex code");
  s->waitp = waitp;
  s->skip = nullptr;   // maintain skip invariant (see above)
  s->may_skip = true;  // always true on entering queue
  s->wake = false;     // not being woken
  s->cond_waiter = ((flags & kMuIsCond) != 0);
#ifdef ABSL_HAVE_PTHREAD_GETSCHEDPARAM
  if ((flags & kMuIsFer) == 0) {
    assert(s == Synch_GetPerThread());
    int64_t now_cycles = CycleClock::Now();
    if (s->next_priority_read_cycles < now_cycles) {
      // Every so often, update our idea of the thread's priority.
      // pthread_getschedparam() is 5% of the block/wakeup time;
      // CycleClock::Now() is 0.5%.
      int policy;
      struct sched_param param;
      const int err = pthread_getschedparam(pthread_self(), &policy, &param);
      if (err != 0) {
        ABSL_RAW_LOG(ERROR, "pthread_getschedparam failed: %d", err);
      } else {
        s->priority = param.sched_priority;
        s->next_priority_read_cycles =
            now_cycles + static_cast<int64_t>(CycleClock::Frequency());
      }
    }
  }
#endif
  if (head == nullptr) {         // s is the only waiter
    s->next = s;                 // it's the only entry in the cycle
    s->readers = mu;             // reader count is from mu word
    s->maybe_unlocking = false;  // no one is searching an empty list
    head = s;                    // s is new head
  } else {
    PerThreadSynch* enqueue_after = nullptr;  // we'll put s after this element
#ifdef ABSL_HAVE_PTHREAD_GETSCHEDPARAM
    if (s->priority > head->priority) {  // s's priority is above head's
      // try to put s in priority-fifo order, or failing that at the front.
      if (!head->maybe_unlocking) {
        // No unlocker can be scanning the queue, so we can insert into the
        // middle of the queue.
        //
        // Within a skip chain, all waiters have the same priority, so we can
        // skip forward through the chains until we find one with a lower
        // priority than the waiter to be enqueued.
        PerThreadSynch* advance_to = head;  // next value of enqueue_after
        do {
          enqueue_after = advance_to;
          // (side-effect: optimizes skip chain)
          advance_to = Skip(enqueue_after->next);
        } while (s->priority <= advance_to->priority);
        // termination guaranteed because s->priority > head->priority
        // and head is the end of a skip chain
      } else if (waitp->how == kExclusive && waitp->cond == nullptr) {
        // An unlocker could be scanning the queue, but we know it will recheck
        // the queue front for writers that have no condition, which is what s
        // is, so an insert at front is safe.
        enqueue_after = head;  // add after head, at front
      }
    }
#endif
    if (enqueue_after != nullptr) {
      s->next = enqueue_after->next;
      enqueue_after->next = s;

      // enqueue_after can be: head, Skip(...), or cur.
      // The first two imply enqueue_after->skip == nullptr, and
      // the last is used only if MuEquivalentWaiter(s, cur).
      // We require this because clearing enqueue_after->skip
      // is impossible; enqueue_after's predecessors might also
      // incorrectly skip over s if we were to allow other
      // insertion points.
      ABSL_RAW_CHECK(enqueue_after->skip == nullptr ||
                         MuEquivalentWaiter(enqueue_after, s),
                     "Mutex Enqueue failure");

      if (enqueue_after != head && enqueue_after->may_skip &&
          MuEquivalentWaiter(enqueue_after, enqueue_after->next)) {
        // enqueue_after can skip to its new successor, s
        enqueue_after->skip = enqueue_after->next;
      }
      if (MuEquivalentWaiter(s, s->next)) {  // s->may_skip is known to be true
        s->skip = s->next;                   // s may skip to its successor
      }
    } else if ((flags & kMuHasBlocked) &&
               (s->priority >= head->next->priority) &&
               (!head->maybe_unlocking ||
                (waitp->how == kExclusive &&
                 Condition::GuaranteedEqual(waitp->cond, nullptr)))) {
      // This thread has already waited, then was woken, then failed to acquire
      // the mutex and now tries to requeue. Try to requeue it at head,
      // otherwise it can suffer bad latency (wait whole queue several times).
      // However, we need to be conservative. First, we need to ensure that we
      // respect priorities. Then, we need to be careful to not break wait
      // queue invariants: we require either that unlocker is not scanning
      // the queue or that the current thread is a writer with no condition
      // (unlocker will recheck the queue for such waiters).
      s->next = head->next;
      head->next = s;
      if (MuEquivalentWaiter(s, s->next)) {  // s->may_skip is known to be true
        s->skip = s->next;                   // s may skip to its successor
      }
    } else {  // enqueue not done any other way, so
              // we're inserting s at the back
      // s will become new head; copy data from head into it
      s->next = head->next;  // add s after head
      head->next = s;
      s->readers = head->readers;  // reader count is from previous head
      s->maybe_unlocking = head->maybe_unlocking;  // same for unlock hint
      if (head->may_skip && MuEquivalentWaiter(head, s)) {
        // head now has successor; may skip
        head->skip = s;
      }
      head = s;  // s is new head
    }
  }
  s->state.store(PerThreadSynch::kQueued, std::memory_order_relaxed);
  return head;
}

// Dequeue the successor pw->next of thread pw from the Mutex waiter queue
// whose last element is head.  The new head element is returned, or null
// if the list is made empty.
// Dequeue is called with both spinlock and Mutex held.
static PerThreadSynch* Dequeue(PerThreadSynch* head, PerThreadSynch* pw) {
  PerThreadSynch* w = pw->next;
  pw->next = w->next;                 // snip w out of list
  if (head == w) {                    // we removed the head
    head = (pw == w) ? nullptr : pw;  // either emptied list, or pw is new head
  } else if (pw != head && MuEquivalentWaiter(pw, pw->next)) {
    // pw can skip to its new successor
    if (pw->next->skip !=
        nullptr) {  // either skip to its successors skip target
      pw->skip = pw->next->skip;
    } else {  // or to pw's successor
      pw->skip = pw->next;
    }
  }
  return head;
}

// Traverse the elements [ pw->next, h] of the circular list whose last element
// is head.
// Remove all elements with wake==true and place them in the
// singly-linked list wake_list in the order found.   Assumes that
// there is only one such element if the element has how == kExclusive.
// Return the new head.
static PerThreadSynch* DequeueAllWakeable(PerThreadSynch* head,
                                          PerThreadSynch* pw,
                                          PerThreadSynch** wake_tail) {
  PerThreadSynch* orig_h = head;
  PerThreadSynch* w = pw->next;
  bool skipped = false;
  do {
    if (w->wake) {  // remove this element
      ABSL_RAW_CHECK(pw->skip == nullptr, "bad skip in DequeueAllWakeable");
      // we're removing pw's successor so either pw->skip is zero or we should
      // already have removed pw since if pw->skip!=null, pw has the same
      // condition as w.
      head = Dequeue(head, pw);
      w->next = *wake_tail;               // keep list terminated
      *wake_tail = w;                     // add w to wake_list;
      wake_tail = &w->next;               // next addition to end
      if (w->waitp->how == kExclusive) {  // wake at most 1 writer
        break;
      }
    } else {         // not waking this one; skip
      pw = Skip(w);  // skip as much as possible
      skipped = true;
    }
    w = pw->next;
    // We want to stop processing after we've considered the original head,
    // orig_h.  We can't test for w==orig_h in the loop because w may skip over
    // it; we are guaranteed only that w's predecessor will not skip over
    // orig_h.  When we've considered orig_h, either we've processed it and
    // removed it (so orig_h != head), or we considered it and skipped it (so
    // skipped==true && pw == head because skipping from head always skips by
    // just one, leaving pw pointing at head).  So we want to
    // continue the loop with the negation of that expression.
  } while (orig_h == head && (pw != head || !skipped));
  return head;
}

// Try to remove thread s from the list of waiters on this mutex.
// Does nothing if s is not on the waiter list.
void Mutex::TryRemove(PerThreadSynch* s) {
  SchedulingGuard::ScopedDisable disable_rescheduling;
  intptr_t v = mu_.load(std::memory_order_relaxed);
  // acquire spinlock & lock
  if ((v & (kMuWait | kMuSpin | kMuWriter | kMuReader)) == kMuWait &&
      mu_.compare_exchange_strong(v, v | kMuSpin | kMuWriter,
                                  std::memory_order_acquire,
                                  std::memory_order_relaxed)) {
    PerThreadSynch* h = GetPerThreadSynch(v);
    if (h != nullptr) {
      PerThreadSynch* pw = h;  // pw is w's predecessor
      PerThreadSynch* w;
      if ((w = pw->next) != s) {  // search for thread,
        do {                      // processing at least one element
          // If the current element isn't equivalent to the waiter to be
          // removed, we can skip the entire chain.
          if (!MuEquivalentWaiter(s, w)) {
            pw = Skip(w);  // so skip all that won't match
            // we don't have to worry about dangling skip fields
            // in the threads we skipped; none can point to s
            // because they are in a different equivalence class.
          } else {          // seeking same condition
            FixSkip(w, s);  // fix up any skip pointer from w to s
            pw = w;
          }
          // don't search further if we found the thread, or we're about to
          // process the first thread again.
        } while ((w = pw->next) != s && pw != h);
      }
      if (w == s) {  // found thread; remove it
        // pw->skip may be non-zero here; the loop above ensured that
        // no ancestor of s can skip to s, so removal is safe anyway.
        h = Dequeue(h, pw);
        s->next = nullptr;
        s->state.store(PerThreadSynch::kAvailable, std::memory_order_release);
      }
    }
    intptr_t nv;
    do {  // release spinlock and lock
      v = mu_.load(std::memory_order_relaxed);
      nv = v & (kMuDesig | kMuEvent);
      if (h != nullptr) {
        nv |= kMuWait | reinterpret_cast<intptr_t>(h);
        h->readers = 0;              // we hold writer lock
        h->maybe_unlocking = false;  // finished unlocking
      }
    } while (!mu_.compare_exchange_weak(v, nv, std::memory_order_release,
                                        std::memory_order_relaxed));
  }
}

// Wait until thread "s", which must be the current thread, is removed from the
// this mutex's waiter queue.  If "s->waitp->timeout" has a timeout, wake up
// if the wait extends past the absolute time specified, even if "s" is still
// on the mutex queue.  In this case, remove "s" from the queue and return
// true, otherwise return false.
void Mutex::Block(PerThreadSynch* s) {
  while (s->state.load(std::memory_order_acquire) == PerThreadSynch::kQueued) {
    if (!DecrementSynchSem(this, s, s->waitp->timeout)) {
      // After a timeout, we go into a spin loop until we remove ourselves
      // from the queue, or someone else removes us.  We can't be sure to be
      // able to remove ourselves in a single lock acquisition because this
      // mutex may be held, and the holder has the right to read the centre
      // of the waiter queue without holding the spinlock.
      this->TryRemove(s);
      int c = 0;
      while (s->next != nullptr) {
        c = synchronization_internal::MutexDelay(c, GENTLE);
        this->TryRemove(s);
      }
      if (kDebugMode) {
        // This ensures that we test the case that TryRemove() is called when s
        // is not on the queue.
        this->TryRemove(s);
      }
      s->waitp->timeout = KernelTimeout::Never();  // timeout is satisfied
      s->waitp->cond = nullptr;  // condition no longer relevant for wakeups
    }
  }
  ABSL_RAW_CHECK(s->waitp != nullptr || s->suppress_fatal_errors,
                 "detected illegal recursion in Mutex code");
  s->waitp = nullptr;
}

// Wake thread w, and return the next thread in the list.
PerThreadSynch* Mutex::Wakeup(PerThreadSynch* w) {
  PerThreadSynch* next = w->next;
  w->next = nullptr;
  w->state.store(PerThreadSynch::kAvailable, std::memory_order_release);
  IncrementSynchSem(this, w);

  return next;
}

static GraphId GetGraphIdLocked(Mutex* mu)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(deadlock_graph_mu) {
  if (!deadlock_graph) {  // (re)create the deadlock graph.
    deadlock_graph =
        new (base_internal::LowLevelAlloc::Alloc(sizeof(*deadlock_graph)))
            GraphCycles;
  }
  return deadlock_graph->GetId(mu);
}

static GraphId GetGraphId(Mutex* mu) ABSL_LOCKS_EXCLUDED(deadlock_graph_mu) {
  deadlock_graph_mu.Lock();
  GraphId id = GetGraphIdLocked(mu);
  deadlock_graph_mu.Unlock();
  return id;
}

// Record a lock acquisition.  This is used in debug mode for deadlock
// detection.  The held_locks pointer points to the relevant data
// structure for each case.
static void LockEnter(Mutex* mu, GraphId id, SynchLocksHeld* held_locks) {
  int n = held_locks->n;
  int i = 0;
  while (i != n && held_locks->locks[i].id != id) {
    i++;
  }
  if (i == n) {
    if (n == ABSL_ARRAYSIZE(held_locks->locks)) {
      held_locks->overflow = true;  // lost some data
    } else {                        // we have room for lock
      held_locks->locks[i].mu = mu;
      held_locks->locks[i].count = 1;
      held_locks->locks[i].id = id;
      held_locks->n = n + 1;
    }
  } else {
    held_locks->locks[i].count++;
  }
}

// Record a lock release.  Each call to LockEnter(mu, id, x) should be
// eventually followed by a call to LockLeave(mu, id, x) by the same thread.
// It does not process the event if is not needed when deadlock detection is
// disabled.
static void LockLeave(Mutex* mu, GraphId id, SynchLocksHeld* held_locks) {
  int n = held_locks->n;
  int i = 0;
  while (i != n && held_locks->locks[i].id != id) {
    i++;
  }
  if (i == n) {
    if (!held_locks->overflow) {
      // The deadlock id may have been reassigned after ForgetDeadlockInfo,
      // but in that case mu should still be present.
      i = 0;
      while (i != n && held_locks->locks[i].mu != mu) {
        i++;
      }
      if (i == n) {  // mu missing means releasing unheld lock
        SynchEvent* mu_events = GetSynchEvent(mu);
        ABSL_RAW_LOG(FATAL,
                     "thread releasing lock it does not hold: %p %s; "
                     ,
                     static_cast<void*>(mu),
                     mu_events == nullptr ? "" : mu_events->name);
      }
    }
  } else if (held_locks->locks[i].count == 1) {
    held_locks->n = n - 1;
    held_locks->locks[i] = held_locks->locks[n - 1];
    held_locks->locks[n - 1].id = InvalidGraphId();
    held_locks->locks[n - 1].mu =
        nullptr;  // clear mu to please the leak detector.
  } else {
    assert(held_locks->locks[i].count > 0);
    held_locks->locks[i].count--;
  }
}

// Call LockEnter() if in debug mode and deadlock detection is enabled.
static inline void DebugOnlyLockEnter(Mutex* mu) {
  if (kDebugMode) {
    if (synch_deadlock_detection.load(std::memory_order_acquire) !=
        OnDeadlockCycle::kIgnore) {
      LockEnter(mu, GetGraphId(mu), Synch_GetAllLocks());
    }
  }
}

// Call LockEnter() if in debug mode and deadlock detection is enabled.
static inline void DebugOnlyLockEnter(Mutex* mu, GraphId id) {
  if (kDebugMode) {
    if (synch_deadlock_detection.load(std::memory_order_acquire) !=
        OnDeadlockCycle::kIgnore) {
      LockEnter(mu, id, Synch_GetAllLocks());
    }
  }
}

// Call LockLeave() if in debug mode and deadlock detection is enabled.
static inline void DebugOnlyLockLeave(Mutex* mu) {
  if (kDebugMode) {
    if (synch_deadlock_detection.load(std::memory_order_acquire) !=
        OnDeadlockCycle::kIgnore) {
      LockLeave(mu, GetGraphId(mu), Synch_GetAllLocks());
    }
  }
}

static char* StackString(void** pcs, int n, char* buf, int maxlen,
                         bool symbolize) {
  static constexpr int kSymLen = 200;
  char sym[kSymLen];
  int len = 0;
  for (int i = 0; i != n; i++) {
    if (len >= maxlen)
      return buf;
    size_t count = static_cast<size_t>(maxlen - len);
    if (symbolize) {
      if (!absl::Symbolize(pcs[i], sym, kSymLen)) {
        sym[0] = '\0';
      }
      snprintf(buf + len, count, "%s\t@ %p %s\n", (i == 0 ? "\n" : ""), pcs[i],
               sym);
    } else {
      snprintf(buf + len, count, " %p", pcs[i]);
    }
    len += strlen(&buf[len]);
  }
  return buf;
}

static char* CurrentStackString(char* buf, int maxlen, bool symbolize) {
  void* pcs[40];
  return StackString(pcs, absl::GetStackTrace(pcs, ABSL_ARRAYSIZE(pcs), 2), buf,
                     maxlen, symbolize);
}

namespace {
enum {
  kMaxDeadlockPathLen = 10
};  // maximum length of a deadlock cycle;
    // a path this long would be remarkable
// Buffers required to report a deadlock.
// We do not allocate them on stack to avoid large stack frame.
struct DeadlockReportBuffers {
  char buf[6100];
  GraphId path[kMaxDeadlockPathLen];
};

struct ScopedDeadlockReportBuffers {
  ScopedDeadlockReportBuffers() {
    b = reinterpret_cast<DeadlockReportBuffers*>(
        base_internal::LowLevelAlloc::Alloc(sizeof(*b)));
  }
  ~ScopedDeadlockReportBuffers() { base_internal::LowLevelAlloc::Free(b); }
  DeadlockReportBuffers* b;
};

// Helper to pass to GraphCycles::UpdateStackTrace.
int GetStack(void** stack, int max_depth) {
  return absl::GetStackTrace(stack, max_depth, 3);
}
}  // anonymous namespace

// Called in debug mode when a thread is about to acquire a lock in a way that
// may block.
static GraphId DeadlockCheck(Mutex* mu) {
  if (synch_deadlock_detection.load(std::memory_order_acquire) ==
      OnDeadlockCycle::kIgnore) {
    return InvalidGraphId();
  }

  SynchLocksHeld* all_locks = Synch_GetAllLocks();

  absl::base_internal::SpinLockHolder lock(&deadlock_graph_mu);
  const GraphId mu_id = GetGraphIdLocked(mu);

  if (all_locks->n == 0) {
    // There are no other locks held. Return now so that we don't need to
    // call GetSynchEvent(). This way we do not record the stack trace
    // for this Mutex. It's ok, since if this Mutex is involved in a deadlock,
    // it can't always be the first lock acquired by a thread.
    return mu_id;
  }

  // We prefer to keep stack traces that show a thread holding and acquiring
  // as many locks as possible.  This increases the chances that a given edge
  // in the acquires-before graph will be represented in the stack traces
  // recorded for the locks.
  deadlock_graph->UpdateStackTrace(mu_id, all_locks->n + 1, GetStack);

  // For each other mutex already held by this thread:
  for (int i = 0; i != all_locks->n; i++) {
    const GraphId other_node_id = all_locks->locks[i].id;
    const Mutex* other =
        static_cast<const Mutex*>(deadlock_graph->Ptr(other_node_id));
    if (other == nullptr) {
      // Ignore stale lock
      continue;
    }

    // Add the acquired-before edge to the graph.
    if (!deadlock_graph->InsertEdge(other_node_id, mu_id)) {
      ScopedDeadlockReportBuffers scoped_buffers;
      DeadlockReportBuffers* b = scoped_buffers.b;
      static int number_of_reported_deadlocks = 0;
      number_of_reported_deadlocks++;
      // Symbolize only 2 first deadlock report to avoid huge slowdowns.
      bool symbolize = number_of_reported_deadlocks <= 2;
      ABSL_RAW_LOG(ERROR, "Potential Mutex deadlock: %s",
                   CurrentStackString(b->buf, sizeof (b->buf), symbolize));
      size_t len = 0;
      for (int j = 0; j != all_locks->n; j++) {
        void* pr = deadlock_graph->Ptr(all_locks->locks[j].id);
        if (pr != nullptr) {
          snprintf(b->buf + len, sizeof(b->buf) - len, " %p", pr);
          len += strlen(&b->buf[len]);
        }
      }
      ABSL_RAW_LOG(ERROR,
                   "Acquiring absl::Mutex %p while holding %s; a cycle in the "
                   "historical lock ordering graph has been observed",
                   static_cast<void*>(mu), b->buf);
      ABSL_RAW_LOG(ERROR, "Cycle: ");
      int path_len = deadlock_graph->FindPath(mu_id, other_node_id,
                                              ABSL_ARRAYSIZE(b->path), b->path);
      for (int j = 0; j != path_len && j != ABSL_ARRAYSIZE(b->path); j++) {
        GraphId id = b->path[j];
        Mutex* path_mu = static_cast<Mutex*>(deadlock_graph->Ptr(id));
        if (path_mu == nullptr) continue;
        void** stack;
        int depth = deadlock_graph->GetStackTrace(id, &stack);
        snprintf(b->buf, sizeof(b->buf),
                 "mutex@%p stack: ", static_cast<void*>(path_mu));
        StackString(stack, depth, b->buf + strlen(b->buf),
                    static_cast<int>(sizeof(b->buf) - strlen(b->buf)),
                    symbolize);
        ABSL_RAW_LOG(ERROR, "%s", b->buf);
      }
      if (path_len > static_cast<int>(ABSL_ARRAYSIZE(b->path))) {
        ABSL_RAW_LOG(ERROR, "(long cycle; list truncated)");
      }
      if (synch_deadlock_detection.load(std::memory_order_acquire) ==
          OnDeadlockCycle::kAbort) {
        deadlock_graph_mu.Unlock();  // avoid deadlock in fatal sighandler
        ABSL_RAW_LOG(FATAL, "dying due to potential deadlock");
        return mu_id;
      }
      break;  // report at most one potential deadlock per acquisition
    }
  }

  return mu_id;
}

// Invoke DeadlockCheck() iff we're in debug mode and
// deadlock checking has been enabled.
static inline GraphId DebugOnlyDeadlockCheck(Mutex* mu) {
  if (kDebugMode && synch_deadlock_detection.load(std::memory_order_acquire) !=
                        OnDeadlockCycle::kIgnore) {
    return DeadlockCheck(mu);
  } else {
    return InvalidGraphId();
  }
}

void Mutex::ForgetDeadlockInfo() {
  if (kDebugMode && synch_deadlock_detection.load(std::memory_order_acquire) !=
                        OnDeadlockCycle::kIgnore) {
    deadlock_graph_mu.Lock();
    if (deadlock_graph != nullptr) {
      deadlock_graph->RemoveNode(this);
    }
    deadlock_graph_mu.Unlock();
  }
}

void Mutex::AssertNotHeld() const {
  // We have the data to allow this check only if in debug mode and deadlock
  // detection is enabled.
  if (kDebugMode &&
      (mu_.load(std::memory_order_relaxed) & (kMuWriter | kMuReader)) != 0 &&
      synch_deadlock_detection.load(std::memory_order_acquire) !=
          OnDeadlockCycle::kIgnore) {
    GraphId id = GetGraphId(const_cast<Mutex*>(this));
    SynchLocksHeld* locks = Synch_GetAllLocks();
    for (int i = 0; i != locks->n; i++) {
      if (locks->locks[i].id == id) {
        SynchEvent* mu_events = GetSynchEvent(this);
        ABSL_RAW_LOG(FATAL, "thread should not hold mutex %p %s",
                     static_cast<const void*>(this),
                     (mu_events == nullptr ? "" : mu_events->name));
      }
    }
  }
}

// Attempt to acquire *mu, and return whether successful.  The implementation
// may spin for a short while if the lock cannot be acquired immediately.
static bool TryAcquireWithSpinning(std::atomic<intptr_t>* mu) {
  int c = globals.spinloop_iterations.load(std::memory_order_relaxed);
  do {  // do/while somewhat faster on AMD
    intptr_t v = mu->load(std::memory_order_relaxed);
    if ((v & (kMuReader | kMuEvent)) != 0) {
      return false;                       // a reader or tracing -> give up
    } else if (((v & kMuWriter) == 0) &&  // no holder -> try to acquire
               mu->compare_exchange_strong(v, kMuWriter | v,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
      return true;
    }
  } while (--c > 0);
  return false;
}

void Mutex::Lock() {
  ABSL_TSAN_MUTEX_PRE_LOCK(this, 0);
  GraphId id = DebugOnlyDeadlockCheck(this);
  intptr_t v = mu_.load(std::memory_order_relaxed);
  // try fast acquire, then spin loop
  if (ABSL_PREDICT_FALSE((v & (kMuWriter | kMuReader | kMuEvent)) != 0) ||
      ABSL_PREDICT_FALSE(!mu_.compare_exchange_strong(
          v, kMuWriter | v, std::memory_order_acquire,
          std::memory_order_relaxed))) {
    // try spin acquire, then slow loop
    if (ABSL_PREDICT_FALSE(!TryAcquireWithSpinning(&this->mu_))) {
      this->LockSlow(kExclusive, nullptr, 0);
    }
  }
  DebugOnlyLockEnter(this, id);
  ABSL_TSAN_MUTEX_POST_LOCK(this, 0, 0);
}

void Mutex::ReaderLock() {
  ABSL_TSAN_MUTEX_PRE_LOCK(this, __tsan_mutex_read_lock);
  GraphId id = DebugOnlyDeadlockCheck(this);
  intptr_t v = mu_.load(std::memory_order_relaxed);
  for (;;) {
    // If there are non-readers holding the lock, use the slow loop.
    if (ABSL_PREDICT_FALSE(v & (kMuWriter | kMuWait | kMuEvent)) != 0) {
      this->LockSlow(kShared, nullptr, 0);
      break;
    }
    // We can avoid the loop and only use the CAS when the lock is free or
    // only held by readers.
    if (ABSL_PREDICT_TRUE(mu_.compare_exchange_weak(
            v, (kMuReader | v) + kMuOne, std::memory_order_acquire,
            std::memory_order_relaxed))) {
      break;
    }
  }
  DebugOnlyLockEnter(this, id);
  ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_read_lock, 0);
}

bool Mutex::LockWhenCommon(const Condition& cond,
                           synchronization_internal::KernelTimeout t,
                           bool write) {
  MuHow how = write ? kExclusive : kShared;
  ABSL_TSAN_MUTEX_PRE_LOCK(this, TsanFlags(how));
  GraphId id = DebugOnlyDeadlockCheck(this);
  bool res = LockSlowWithDeadline(how, &cond, t, 0);
  DebugOnlyLockEnter(this, id);
  ABSL_TSAN_MUTEX_POST_LOCK(this, TsanFlags(how), 0);
  return res;
}

bool Mutex::AwaitCommon(const Condition& cond, KernelTimeout t) {
  if (kDebugMode) {
    this->AssertReaderHeld();
  }
  if (cond.Eval()) {  // condition already true; nothing to do
    return true;
  }
  MuHow how =
      (mu_.load(std::memory_order_relaxed) & kMuWriter) ? kExclusive : kShared;
  ABSL_TSAN_MUTEX_PRE_UNLOCK(this, TsanFlags(how));
  SynchWaitParams waitp(how, &cond, t, nullptr /*no cvmu*/,
                        Synch_GetPerThreadAnnotated(this),
                        nullptr /*no cv_word*/);
  this->UnlockSlow(&waitp);
  this->Block(waitp.thread);
  ABSL_TSAN_MUTEX_POST_UNLOCK(this, TsanFlags(how));
  ABSL_TSAN_MUTEX_PRE_LOCK(this, TsanFlags(how));
  this->LockSlowLoop(&waitp, kMuHasBlocked | kMuIsCond);
  bool res = waitp.cond != nullptr ||  // => cond known true from LockSlowLoop
             EvalConditionAnnotated(&cond, this, true, false, how == kShared);
  ABSL_TSAN_MUTEX_POST_LOCK(this, TsanFlags(how), 0);
  ABSL_RAW_CHECK(res || t.has_timeout(),
                 "condition untrue on return from Await");
  return res;
}

bool Mutex::TryLock() {
  ABSL_TSAN_MUTEX_PRE_LOCK(this, __tsan_mutex_try_lock);
  intptr_t v = mu_.load(std::memory_order_relaxed);
  // Try fast acquire.
  if (ABSL_PREDICT_TRUE((v & (kMuWriter | kMuReader | kMuEvent)) == 0)) {
    if (ABSL_PREDICT_TRUE(mu_.compare_exchange_strong(
            v, kMuWriter | v, std::memory_order_acquire,
            std::memory_order_relaxed))) {
      DebugOnlyLockEnter(this);
      ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_lock, 0);
      return true;
    }
  } else if (ABSL_PREDICT_FALSE((v & kMuEvent) != 0)) {
    // We're recording events.
    return TryLockSlow();
  }
  ABSL_TSAN_MUTEX_POST_LOCK(
      this, __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 0);
  return false;
}

ABSL_ATTRIBUTE_NOINLINE bool Mutex::TryLockSlow() {
  intptr_t v = mu_.load(std::memory_order_relaxed);
  if ((v & kExclusive->slow_need_zero) == 0 &&  // try fast acquire
      mu_.compare_exchange_strong(
          v, (kExclusive->fast_or | v) + kExclusive->fast_add,
          std::memory_order_acquire, std::memory_order_relaxed)) {
    DebugOnlyLockEnter(this);
    PostSynchEvent(this, SYNCH_EV_TRYLOCK_SUCCESS);
    ABSL_TSAN_MUTEX_POST_LOCK(this, __tsan_mutex_try_lock, 0);
    return true;
  }
  PostSynchEvent(this, SYNCH_EV_TRYLOCK_FAILED);
  ABSL_TSAN_MUTEX_POST_LOCK(
      this, __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 0);
  return false;
}

bool Mutex::ReaderTryLock() {
  ABSL_TSAN_MUTEX_PRE_LOCK(this,
                           __tsan_mutex_read_lock | __tsan_mutex_try_lock);
  intptr_t v = mu_.load(std::memory_order_relaxed);
  // Clang tends to unroll the loop when compiling with optimization.
  // But in this case it just unnecessary increases code size.
  // If CAS is failing due to contention, the jump cost is negligible.
#if defined(__clang__)
#pragma nounroll
#endif
  // The while-loops (here and below) iterate only if the mutex word keeps
  // changing (typically because the reader count changes) under the CAS.
  // We limit the number of attempts to avoid having to think about livelock.
  for (int loop_limit = 5; loop_limit != 0; loop_limit--) {
    if (ABSL_PREDICT_FALSE((v & (kMuWriter | kMuWait | kMuEvent)) != 0)) {
      break;
    }
    if (ABSL_PREDICT_TRUE(mu_.compare_exchange_strong(
            v, (kMuReader | v) + kMuOne, std::memory_order_acquire,
            std::memory_order_relaxed))) {
      DebugOnlyLockEnter(this);
      ABSL_TSAN_MUTEX_POST_LOCK(
          this, __tsan_mutex_read_lock | __tsan_mutex_try_lock, 0);
      return true;
    }
  }
  if (ABSL_PREDICT_TRUE((v & kMuEvent) == 0)) {
    ABSL_TSAN_MUTEX_POST_LOCK(this,
                              __tsan_mutex_read_lock | __tsan_mutex_try_lock |
                                  __tsan_mutex_try_lock_failed,
                              0);
    return false;
  }
  // we're recording events
  return ReaderTryLockSlow();
}

ABSL_ATTRIBUTE_NOINLINE bool Mutex::ReaderTryLockSlow() {
  intptr_t v = mu_.load(std::memory_order_relaxed);
#if defined(__clang__)
#pragma nounroll
#endif
  for (int loop_limit = 5; loop_limit != 0; loop_limit--) {
    if ((v & kShared->slow_need_zero) == 0 &&
        mu_.compare_exchange_strong(v, (kMuReader | v) + kMuOne,
                                    std::memory_order_acquire,
                                    std::memory_order_relaxed)) {
      DebugOnlyLockEnter(this);
      PostSynchEvent(this, SYNCH_EV_READERTRYLOCK_SUCCESS);
      ABSL_TSAN_MUTEX_POST_LOCK(
          this, __tsan_mutex_read_lock | __tsan_mutex_try_lock, 0);
      return true;
    }
  }
  PostSynchEvent(this, SYNCH_EV_READERTRYLOCK_FAILED);
  ABSL_TSAN_MUTEX_POST_LOCK(this,
                            __tsan_mutex_read_lock | __tsan_mutex_try_lock |
                                __tsan_mutex_try_lock_failed,
                            0);
  return false;
}

void Mutex::Unlock() {
  ABSL_TSAN_MUTEX_PRE_UNLOCK(this, 0);
  DebugOnlyLockLeave(this);
  intptr_t v = mu_.load(std::memory_order_relaxed);

  if (kDebugMode && ((v & (kMuWriter | kMuReader)) != kMuWriter)) {
    ABSL_RAW_LOG(FATAL, "Mutex unlocked when destroyed or not locked: v=0x%x",
                 static_cast<unsigned>(v));
  }

  // should_try_cas is whether we'll try a compare-and-swap immediately.
  // NOTE: optimized out when kDebugMode is false.
  bool should_try_cas = ((v & (kMuEvent | kMuWriter)) == kMuWriter &&
                         (v & (kMuWait | kMuDesig)) != kMuWait);
  // But, we can use an alternate computation of it, that compilers
  // currently don't find on their own.  When that changes, this function
  // can be simplified.
  intptr_t x = (v ^ (kMuWriter | kMuWait)) & (kMuWriter | kMuEvent);
  intptr_t y = (v ^ (kMuWriter | kMuWait)) & (kMuWait | kMuDesig);
  // Claim: "x == 0 && y > 0" is equal to should_try_cas.
  // Also, because kMuWriter and kMuEvent exceed kMuDesig and kMuWait,
  // all possible non-zero values for x exceed all possible values for y.
  // Therefore, (x == 0 && y > 0) == (x < y).
  if (kDebugMode && should_try_cas != (x < y)) {
    // We would usually use PRIdPTR here, but is not correctly implemented
    // within the android toolchain.
    ABSL_RAW_LOG(FATAL, "internal logic error %llx %llx %llx\n",
                 static_cast<long long>(v), static_cast<long long>(x),
                 static_cast<long long>(y));
  }
  if (x < y && mu_.compare_exchange_strong(v, v & ~(kMuWrWait | kMuWriter),
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
    // fast writer release (writer with no waiters or with designated waker)
  } else {
    this->UnlockSlow(nullptr /*no waitp*/);  // take slow path
  }
  ABSL_TSAN_MUTEX_POST_UNLOCK(this, 0);
}

// Requires v to represent a reader-locked state.
static bool ExactlyOneReader(intptr_t v) {
  assert((v & (kMuWriter | kMuReader)) == kMuReader);
  assert((v & kMuHigh) != 0);
  // The more straightforward "(v & kMuHigh) == kMuOne" also works, but
  // on some architectures the following generates slightly smaller code.
  // It may be faster too.
  constexpr intptr_t kMuMultipleWaitersMask = kMuHigh ^ kMuOne;
  return (v & kMuMultipleWaitersMask) == 0;
}

void Mutex::ReaderUnlock() {
  ABSL_TSAN_MUTEX_PRE_UNLOCK(this, __tsan_mutex_read_lock);
  DebugOnlyLockLeave(this);
  intptr_t v = mu_.load(std::memory_order_relaxed);
  assert((v & (kMuWriter | kMuReader)) == kMuReader);
  for (;;) {
    if (ABSL_PREDICT_FALSE((v & (kMuReader | kMuWait | kMuEvent)) !=
                           kMuReader)) {
      this->UnlockSlow(nullptr /*no waitp*/);  // take slow path
      break;
    }
    // fast reader release (reader with no waiters)
    intptr_t clear = ExactlyOneReader(v) ? kMuReader | kMuOne : kMuOne;
    if (ABSL_PREDICT_TRUE(
            mu_.compare_exchange_strong(v, v - clear, std::memory_order_release,
                                        std::memory_order_relaxed))) {
      break;
    }
  }
  ABSL_TSAN_MUTEX_POST_UNLOCK(this, __tsan_mutex_read_lock);
}

// Clears the designated waker flag in the mutex if this thread has blocked, and
// therefore may be the designated waker.
static intptr_t ClearDesignatedWakerMask(int flag) {
  assert(flag >= 0);
  assert(flag <= 1);
  switch (flag) {
    case 0:  // not blocked
      return ~static_cast<intptr_t>(0);
    case 1:  // blocked; turn off the designated waker bit
      return ~static_cast<intptr_t>(kMuDesig);
  }
  ABSL_UNREACHABLE();
}

// Conditionally ignores the existence of waiting writers if a reader that has
// already blocked once wakes up.
static intptr_t IgnoreWaitingWritersMask(int flag) {
  assert(flag >= 0);
  assert(flag <= 1);
  switch (flag) {
    case 0:  // not blocked
      return ~static_cast<intptr_t>(0);
    case 1:  // blocked; pretend there are no waiting writers
      return ~static_cast<intptr_t>(kMuWrWait);
  }
  ABSL_UNREACHABLE();
}

// Internal version of LockWhen().  See LockSlowWithDeadline()
ABSL_ATTRIBUTE_NOINLINE void Mutex::LockSlow(MuHow how, const Condition* cond,
                                             int flags) {
  // Note: we specifically initialize spinloop_iterations after the first use
  // in TryAcquireWithSpinning so that Lock function does not have any non-tail
  // calls and consequently a stack frame. It's fine to have spinloop_iterations
  // uninitialized (meaning no spinning) in all initial uncontended Lock calls
  // and in the first contended call. After that we will have
  // spinloop_iterations properly initialized.
  if (ABSL_PREDICT_FALSE(
          globals.spinloop_iterations.load(std::memory_order_relaxed) == 0)) {
    if (absl::base_internal::NumCPUs() > 1) {
      // If this is multiprocessor, allow spinning.
      globals.spinloop_iterations.store(1500, std::memory_order_relaxed);
    } else {
      // If this a uniprocessor, only yield/sleep.
      globals.spinloop_iterations.store(-1, std::memory_order_relaxed);
    }
  }
  ABSL_RAW_CHECK(
      this->LockSlowWithDeadline(how, cond, KernelTimeout::Never(), flags),
      "condition untrue on return from LockSlow");
}

// Compute cond->Eval() and tell race detectors that we do it under mutex mu.
static inline bool EvalConditionAnnotated(const Condition* cond, Mutex* mu,
                                          bool locking, bool trylock,
                                          bool read_lock) {
  // Delicate annotation dance.
  // We are currently inside of read/write lock/unlock operation.
  // All memory accesses are ignored inside of mutex operations + for unlock
  // operation tsan considers that we've already released the mutex.
  bool res = false;
#ifdef ABSL_INTERNAL_HAVE_TSAN_INTERFACE
  const uint32_t flags = read_lock ? __tsan_mutex_read_lock : 0;
  const uint32_t tryflags = flags | (trylock ? __tsan_mutex_try_lock : 0);
#endif
  if (locking) {
    // For lock we pretend that we have finished the operation,
    // evaluate the predicate, then unlock the mutex and start locking it again
    // to match the annotation at the end of outer lock operation.
    // Note: we can't simply do POST_LOCK, Eval, PRE_LOCK, because then tsan
    // will think the lock acquisition is recursive which will trigger
    // deadlock detector.
    ABSL_TSAN_MUTEX_POST_LOCK(mu, tryflags, 0);
    res = cond->Eval();
    // There is no "try" version of Unlock, so use flags instead of tryflags.
    ABSL_TSAN_MUTEX_PRE_UNLOCK(mu, flags);
    ABSL_TSAN_MUTEX_POST_UNLOCK(mu, flags);
    ABSL_TSAN_MUTEX_PRE_LOCK(mu, tryflags);
  } else {
    // Similarly, for unlock we pretend that we have unlocked the mutex,
    // lock the mutex, evaluate the predicate, and start unlocking it again
    // to match the annotation at the end of outer unlock operation.
    ABSL_TSAN_MUTEX_POST_UNLOCK(mu, flags);
    ABSL_TSAN_MUTEX_PRE_LOCK(mu, flags);
    ABSL_TSAN_MUTEX_POST_LOCK(mu, flags, 0);
    res = cond->Eval();
    ABSL_TSAN_MUTEX_PRE_UNLOCK(mu, flags);
  }
  // Prevent unused param warnings in non-TSAN builds.
  static_cast<void>(mu);
  static_cast<void>(trylock);
  static_cast<void>(read_lock);
  return res;
}

// Compute cond->Eval() hiding it from race detectors.
// We are hiding it because inside of UnlockSlow we can evaluate a predicate
// that was just added by a concurrent Lock operation; Lock adds the predicate
// to the internal Mutex list without actually acquiring the Mutex
// (it only acquires the internal spinlock, which is rightfully invisible for
// tsan). As the result there is no tsan-visible synchronization between the
// addition and this thread. So if we would enable race detection here,
// it would race with the predicate initialization.
static inline bool EvalConditionIgnored(Mutex* mu, const Condition* cond) {
  // Memory accesses are already ignored inside of lock/unlock operations,
  // but synchronization operations are also ignored. When we evaluate the
  // predicate we must ignore only memory accesses but not synchronization,
  // because missed synchronization can lead to false reports later.
  // So we "divert" (which un-ignores both memory accesses and synchronization)
  // and then separately turn on ignores of memory accesses.
  ABSL_TSAN_MUTEX_PRE_DIVERT(mu, 0);
  ABSL_ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN();
  bool res = cond->Eval();
  ABSL_ANNOTATE_IGNORE_READS_AND_WRITES_END();
  ABSL_TSAN_MUTEX_POST_DIVERT(mu, 0);
  static_cast<void>(mu);  // Prevent unused param warning in non-TSAN builds.
  return res;
}

// Internal equivalent of *LockWhenWithDeadline(), where
//   "t" represents the absolute timeout; !t.has_timeout() means "forever".
//   "how" is "kShared" (for ReaderLockWhen) or "kExclusive" (for LockWhen)
// In flags, bits are ored together:
// - kMuHasBlocked indicates that the client has already blocked on the call so
//   the designated waker bit must be cleared and waiting writers should not
//   obstruct this call
// - kMuIsCond indicates that this is a conditional acquire (condition variable,
//   Await,  LockWhen) so contention profiling should be suppressed.
bool Mutex::LockSlowWithDeadline(MuHow how, const Condition* cond,
                                 KernelTimeout t, int flags) {
  intptr_t v = mu_.load(std::memory_order_relaxed);
  bool unlock = false;
  if ((v & how->fast_need_zero) == 0 &&  // try fast acquire
      mu_.compare_exchange_strong(
          v,
          (how->fast_or |
           (v & ClearDesignatedWakerMask(flags & kMuHasBlocked))) +
              how->fast_add,
          std::memory_order_acquire, std::memory_order_relaxed)) {
    if (cond == nullptr ||
        EvalConditionAnnotated(cond, this, true, false, how == kShared)) {
      return true;
    }
    unlock = true;
  }
  SynchWaitParams waitp(how, cond, t, nullptr /*no cvmu*/,
                        Synch_GetPerThreadAnnotated(this),
                        nullptr /*no cv_word*/);
  if (cond != nullptr) {
    flags |= kMuIsCond;
  }
  if (unlock) {
    this->UnlockSlow(&waitp);
    this->Block(waitp.thread);
    flags |= kMuHasBlocked;
  }
  this->LockSlowLoop(&waitp, flags);
  return waitp.cond != nullptr ||  // => cond known true from LockSlowLoop
         cond == nullptr ||
         EvalConditionAnnotated(cond, this, true, false, how == kShared);
}

// RAW_CHECK_FMT() takes a condition, a printf-style format string, and
// the printf-style argument list.   The format string must be a literal.
// Arguments after the first are not evaluated unless the condition is true.
#define RAW_CHECK_FMT(cond, ...)                                   \
  do {                                                             \
    if (ABSL_PREDICT_FALSE(!(cond))) {                             \
      ABSL_RAW_LOG(FATAL, "Check " #cond " failed: " __VA_ARGS__); \
    }                                                              \
  } while (0)

static void CheckForMutexCorruption(intptr_t v, const char* label) {
  // Test for either of two situations that should not occur in v:
  //   kMuWriter and kMuReader
  //   kMuWrWait and !kMuWait
  const uintptr_t w = static_cast<uintptr_t>(v ^ kMuWait);
  // By flipping that bit, we can now test for:
  //   kMuWriter and kMuReader in w
  //   kMuWrWait and kMuWait in w
  // We've chosen these two pairs of values to be so that they will overlap,
  // respectively, when the word is left shifted by three.  This allows us to
  // save a branch in the common (correct) case of them not being coincident.
  static_assert(kMuReader << 3 == kMuWriter, "must match");
  static_assert(kMuWait << 3 == kMuWrWait, "must match");
  if (ABSL_PREDICT_TRUE((w & (w << 3) & (kMuWriter | kMuWrWait)) == 0)) return;
  RAW_CHECK_FMT((v & (kMuWriter | kMuReader)) != (kMuWriter | kMuReader),
                "%s: Mutex corrupt: both reader and writer lock held: %p",
                label, reinterpret_cast<void*>(v));
  RAW_CHECK_FMT((v & (kMuWait | kMuWrWait)) != kMuWrWait,
                "%s: Mutex corrupt: waiting writer with no waiters: %p", label,
                reinterpret_cast<void*>(v));
  assert(false);
}

void Mutex::LockSlowLoop(SynchWaitParams* waitp, int flags) {
  SchedulingGuard::ScopedDisable disable_rescheduling;
  int c = 0;
  intptr_t v = mu_.load(std::memory_order_relaxed);
  if ((v & kMuEvent) != 0) {
    PostSynchEvent(
        this, waitp->how == kExclusive ? SYNCH_EV_LOCK : SYNCH_EV_READERLOCK);
  }
  ABSL_RAW_CHECK(
      waitp->thread->waitp == nullptr || waitp->thread->suppress_fatal_errors,
      "detected illegal recursion into Mutex code");
  for (;;) {
    v = mu_.load(std::memory_order_relaxed);
    CheckForMutexCorruption(v, "Lock");
    if ((v & waitp->how->slow_need_zero) == 0) {
      if (mu_.compare_exchange_strong(
              v,
              (waitp->how->fast_or |
               (v & ClearDesignatedWakerMask(flags & kMuHasBlocked))) +
                  waitp->how->fast_add,
              std::memory_order_acquire, std::memory_order_relaxed)) {
        if (waitp->cond == nullptr ||
            EvalConditionAnnotated(waitp->cond, this, true, false,
                                   waitp->how == kShared)) {
          break;  // we timed out, or condition true, so return
        }
        this->UnlockSlow(waitp);  // got lock but condition false
        this->Block(waitp->thread);
        flags |= kMuHasBlocked;
        c = 0;
      }
    } else {  // need to access waiter list
      bool dowait = false;
      if ((v & (kMuSpin | kMuWait)) == 0) {  // no waiters
        // This thread tries to become the one and only waiter.
        PerThreadSynch* new_h = Enqueue(nullptr, waitp, v, flags);
        intptr_t nv =
            (v & ClearDesignatedWakerMask(flags & kMuHasBlocked) & kMuLow) |
            kMuWait;
        ABSL_RAW_CHECK(new_h != nullptr, "Enqueue to empty list failed");
        if (waitp->how == kExclusive && (v & kMuReader) != 0) {
          nv |= kMuWrWait;
        }
        if (mu_.compare_exchange_strong(
                v, reinterpret_cast<intptr_t>(new_h) | nv,
                std::memory_order_release, std::memory_order_relaxed)) {
          dowait = true;
        } else {  // attempted Enqueue() failed
          // zero out the waitp field set by Enqueue()
          waitp->thread->waitp = nullptr;
        }
      } else if ((v & waitp->how->slow_inc_need_zero &
                  IgnoreWaitingWritersMask(flags & kMuHasBlocked)) == 0) {
        // This is a reader that needs to increment the reader count,
        // but the count is currently held in the last waiter.
        if (mu_.compare_exchange_strong(
                v,
                (v & ClearDesignatedWakerMask(flags & kMuHasBlocked)) |
                    kMuSpin | kMuReader,
                std::memory_order_acquire, std::memory_order_relaxed)) {
          PerThreadSynch* h = GetPerThreadSynch(v);
          h->readers += kMuOne;  // inc reader count in waiter
          do {                   // release spinlock
            v = mu_.load(std::memory_order_relaxed);
          } while (!mu_.compare_exchange_weak(v, (v & ~kMuSpin) | kMuReader,
                                              std::memory_order_release,
                                              std::memory_order_relaxed));
          if (waitp->cond == nullptr ||
              EvalConditionAnnotated(waitp->cond, this, true, false,
                                     waitp->how == kShared)) {
            break;  // we timed out, or condition true, so return
          }
          this->UnlockSlow(waitp);  // got lock but condition false
          this->Block(waitp->thread);
          flags |= kMuHasBlocked;
          c = 0;
        }
      } else if ((v & kMuSpin) == 0 &&  // attempt to queue ourselves
                 mu_.compare_exchange_strong(
                     v,
                     (v & ClearDesignatedWakerMask(flags & kMuHasBlocked)) |
                         kMuSpin | kMuWait,
                     std::memory_order_acquire, std::memory_order_relaxed)) {
        PerThreadSynch* h = GetPerThreadSynch(v);
        PerThreadSynch* new_h = Enqueue(h, waitp, v, flags);
        intptr_t wr_wait = 0;
        ABSL_RAW_CHECK(new_h != nullptr, "Enqueue to list failed");
        if (waitp->how == kExclusive && (v & kMuReader) != 0) {
          wr_wait = kMuWrWait;  // give priority to a waiting writer
        }
        do {  // release spinlock
          v = mu_.load(std::memory_order_relaxed);
        } while (!mu_.compare_exchange_weak(
            v,
            (v & (kMuLow & ~kMuSpin)) | kMuWait | wr_wait |
                reinterpret_cast<intptr_t>(new_h),
            std::memory_order_release, std::memory_order_relaxed));
        dowait = true;
      }
      if (dowait) {
        this->Block(waitp->thread);  // wait until removed from list or timeout
        flags |= kMuHasBlocked;
        c = 0;
      }
    }
    ABSL_RAW_CHECK(
        waitp->thread->waitp == nullptr || waitp->thread->suppress_fatal_errors,
        "detected illegal recursion into Mutex code");
    // delay, then try again
    c = synchronization_internal::MutexDelay(c, GENTLE);
  }
  ABSL_RAW_CHECK(
      waitp->thread->waitp == nullptr || waitp->thread->suppress_fatal_errors,
      "detected illegal recursion into Mutex code");
  if ((v & kMuEvent) != 0) {
    PostSynchEvent(this, waitp->how == kExclusive
                             ? SYNCH_EV_LOCK_RETURNING
                             : SYNCH_EV_READERLOCK_RETURNING);
  }
}

// Unlock this mutex, which is held by the current thread.
// If waitp is non-zero, it must be the wait parameters for the current thread
// which holds the lock but is not runnable because its condition is false
// or it is in the process of blocking on a condition variable; it must requeue
// itself on the mutex/condvar to wait for its condition to become true.
ABSL_ATTRIBUTE_NOINLINE void Mutex::UnlockSlow(SynchWaitParams* waitp) {
  SchedulingGuard::ScopedDisable disable_rescheduling;
  intptr_t v = mu_.load(std::memory_order_relaxed);
  this->AssertReaderHeld();
  CheckForMutexCorruption(v, "Unlock");
  if ((v & kMuEvent) != 0) {
    PostSynchEvent(
        this, (v & kMuWriter) != 0 ? SYNCH_EV_UNLOCK : SYNCH_EV_READERUNLOCK);
  }
  int c = 0;
  // the waiter under consideration to wake, or zero
  PerThreadSynch* w = nullptr;
  // the predecessor to w or zero
  PerThreadSynch* pw = nullptr;
  // head of the list searched previously, or zero
  PerThreadSynch* old_h = nullptr;
  // a condition that's known to be false.
  PerThreadSynch* wake_list = kPerThreadSynchNull;  // list of threads to wake
  intptr_t wr_wait = 0;  // set to kMuWrWait if we wake a reader and a
                         // later writer could have acquired the lock
                         // (starvation avoidance)
  ABSL_RAW_CHECK(waitp == nullptr || waitp->thread->waitp == nullptr ||
                     waitp->thread->suppress_fatal_errors,
                 "detected illegal recursion into Mutex code");
  // This loop finds threads wake_list to wakeup if any, and removes them from
  // the list of waiters.  In addition, it places waitp.thread on the queue of
  // waiters if waitp is non-zero.
  for (;;) {
    v = mu_.load(std::memory_order_relaxed);
    if ((v & kMuWriter) != 0 && (v & (kMuWait | kMuDesig)) != kMuWait &&
        waitp == nullptr) {
      // fast writer release (writer with no waiters or with designated waker)
      if (mu_.compare_exchange_strong(v, v & ~(kMuWrWait | kMuWriter),
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
        return;
      }
    } else if ((v & (kMuReader | kMuWait)) == kMuReader && waitp == nullptr) {
      // fast reader release (reader with no waiters)
      intptr_t clear = ExactlyOneReader(v) ? kMuReader | kMuOne : kMuOne;
      if (mu_.compare_exchange_strong(v, v - clear, std::memory_order_release,
                                      std::memory_order_relaxed)) {
        return;
      }
    } else if ((v & kMuSpin) == 0 &&  // attempt to get spinlock
               mu_.compare_exchange_strong(v, v | kMuSpin,
                                           std::memory_order_acquire,
                                           std::memory_order_relaxed)) {
      if ((v & kMuWait) == 0) {  // no one to wake
        intptr_t nv;
        bool do_enqueue = true;  // always Enqueue() the first time
        ABSL_RAW_CHECK(waitp != nullptr,
                       "UnlockSlow is confused");  // about to sleep
        do {  // must loop to release spinlock as reader count may change
          v = mu_.load(std::memory_order_relaxed);
          // decrement reader count if there are readers
          intptr_t new_readers = (v >= kMuOne) ? v - kMuOne : v;
          PerThreadSynch* new_h = nullptr;
          if (do_enqueue) {
            // If we are enqueuing on a CondVar (waitp->cv_word != nullptr) then
            // we must not retry here.  The initial attempt will always have
            // succeeded, further attempts would enqueue us against *this due to
            // Fer() handling.
            do_enqueue = (waitp->cv_word == nullptr);
            new_h = Enqueue(nullptr, waitp, new_readers, kMuIsCond);
          }
          intptr_t clear = kMuWrWait | kMuWriter;  // by default clear write bit
          if ((v & kMuWriter) == 0 && ExactlyOneReader(v)) {  // last reader
            clear = kMuWrWait | kMuReader;                    // clear read bit
          }
          nv = (v & kMuLow & ~clear & ~kMuSpin);
          if (new_h != nullptr) {
            nv |= kMuWait | reinterpret_cast<intptr_t>(new_h);
          } else {  // new_h could be nullptr if we queued ourselves on a
                    // CondVar
            // In that case, we must place the reader count back in the mutex
            // word, as Enqueue() did not store it in the new waiter.
            nv |= new_readers & kMuHigh;
          }
          // release spinlock & our lock; retry if reader-count changed
          // (writer count cannot change since we hold lock)
        } while (!mu_.compare_exchange_weak(v, nv, std::memory_order_release,
                                            std::memory_order_relaxed));
        break;
      }

      // There are waiters.
      // Set h to the head of the circular waiter list.
      PerThreadSynch* h = GetPerThreadSynch(v);
      if ((v & kMuReader) != 0 && (h->readers & kMuHigh) > kMuOne) {
        // a reader but not the last
        h->readers -= kMuOne;    // release our lock
        intptr_t nv = v;         // normally just release spinlock
        if (waitp != nullptr) {  // but waitp!=nullptr => must queue ourselves
          PerThreadSynch* new_h = Enqueue(h, waitp, v, kMuIsCond);
          ABSL_RAW_CHECK(new_h != nullptr,
                         "waiters disappeared during Enqueue()!");
          nv &= kMuLow;
          nv |= kMuWait | reinterpret_cast<intptr_t>(new_h);
        }
        mu_.store(nv, std::memory_order_release);  // release spinlock
        // can release with a store because there were waiters
        break;
      }

      // Either we didn't search before, or we marked the queue
      // as "maybe_unlocking" and no one else should have changed it.
      ABSL_RAW_CHECK(old_h == nullptr || h->maybe_unlocking,
                     "Mutex queue changed beneath us");

      // The lock is becoming free, and there's a waiter
      if (old_h != nullptr &&
          !old_h->may_skip) {    // we used old_h as a terminator
        old_h->may_skip = true;  // allow old_h to skip once more
        ABSL_RAW_CHECK(old_h->skip == nullptr, "illegal skip from head");
        if (h != old_h && MuEquivalentWaiter(old_h, old_h->next)) {
          old_h->skip = old_h->next;  // old_h not head & can skip to successor
        }
      }
      if (h->next->waitp->how == kExclusive &&
          h->next->waitp->cond == nullptr) {
        // easy case: writer with no condition; no need to search
        pw = h;  // wake w, the successor of h (=pw)
        w = h->next;
        w->wake = true;
        // We are waking up a writer.  This writer may be racing against
        // an already awake reader for the lock.  We want the
        // writer to usually win this race,
        // because if it doesn't, we can potentially keep taking a reader
        // perpetually and writers will starve.  Worse than
        // that, this can also starve other readers if kMuWrWait gets set
        // later.
        wr_wait = kMuWrWait;
      } else if (w != nullptr && (w->waitp->how == kExclusive || h == old_h)) {
        // we found a waiter w to wake on a previous iteration and either it's
        // a writer, or we've searched the entire list so we have all the
        // readers.
        if (pw == nullptr) {  // if w's predecessor is unknown, it must be h
          pw = h;
        }
      } else {
        // At this point we don't know all the waiters to wake, and the first
        // waiter has a condition or is a reader.  We avoid searching over
        // waiters we've searched on previous iterations by starting at
        // old_h if it's set.  If old_h==h, there's no one to wakeup at all.
        if (old_h == h) {  // we've searched before, and nothing's new
                           // so there's no one to wake.
          intptr_t nv = (v & ~(kMuReader | kMuWriter | kMuWrWait));
          h->readers = 0;
          h->maybe_unlocking = false;  // finished unlocking
          if (waitp != nullptr) {      // we must queue ourselves and sleep
            PerThreadSynch* new_h = Enqueue(h, waitp, v, kMuIsCond);
            nv &= kMuLow;
            if (new_h != nullptr) {
              nv |= kMuWait | reinterpret_cast<intptr_t>(new_h);
            }  // else new_h could be nullptr if we queued ourselves on a
               // CondVar
          }
          // release spinlock & lock
          // can release with a store because there were waiters
          mu_.store(nv, std::memory_order_release);
          break;
        }

        // set up to walk the list
        PerThreadSynch* w_walk;   // current waiter during list walk
        PerThreadSynch* pw_walk;  // previous waiter during list walk
        if (old_h != nullptr) {  // we've searched up to old_h before
          pw_walk = old_h;
          w_walk = old_h->next;
        } else {  // no prior search, start at beginning
          pw_walk =
              nullptr;  // h->next's predecessor may change; don't record it
          w_walk = h->next;
        }

        h->may_skip = false;  // ensure we never skip past h in future searches
                              // even if other waiters are queued after it.
        ABSL_RAW_CHECK(h->skip == nullptr, "illegal skip from head");

        h->maybe_unlocking = true;  // we're about to scan the waiter list
                                    // without the spinlock held.
                                    // Enqueue must be conservative about
                                    // priority queuing.

        // We must release the spinlock to evaluate the conditions.
        mu_.store(v, std::memory_order_release);  // release just spinlock
        // can release with a store because there were waiters

        // h is the last waiter queued, and w_walk the first unsearched waiter.
        // Without the spinlock, the locations mu_ and h->next may now change
        // underneath us, but since we hold the lock itself, the only legal
        // change is to add waiters between h and w_walk.  Therefore, it's safe
        // to walk the path from w_walk to h inclusive. (TryRemove() can remove
        // a waiter anywhere, but it acquires both the spinlock and the Mutex)

        old_h = h;  // remember we searched to here

        // Walk the path upto and including h looking for waiters we can wake.
        while (pw_walk != h) {
          w_walk->wake = false;
          if (w_walk->waitp->cond ==
                  nullptr ||  // no condition => vacuously true OR
                              // this thread's condition is true
              EvalConditionIgnored(this, w_walk->waitp->cond)) {
            if (w == nullptr) {
              w_walk->wake = true;  // can wake this waiter
              w = w_walk;
              pw = pw_walk;
              if (w_walk->waitp->how == kExclusive) {
                wr_wait = kMuWrWait;
                break;  // bail if waking this writer
              }
            } else if (w_walk->waitp->how == kShared) {  // wake if a reader
              w_walk->wake = true;
            } else {  // writer with true condition
              wr_wait = kMuWrWait;
            }
          }
          if (w_walk->wake) {  // we're waking reader w_walk
            pw_walk = w_walk;  // don't skip similar waiters
          } else {             // not waking; skip as much as possible
            pw_walk = Skip(w_walk);
          }
          // If pw_walk == h, then load of pw_walk->next can race with
          // concurrent write in Enqueue(). However, at the same time
          // we do not need to do the load, because we will bail out
          // from the loop anyway.
          if (pw_walk != h) {
            w_walk = pw_walk->next;
          }
        }

        continue;  // restart for(;;)-loop to wakeup w or to find more waiters
      }
      ABSL_RAW_CHECK(pw->next == w, "pw not w's predecessor");
      // The first (and perhaps only) waiter we've chosen to wake is w, whose
      // predecessor is pw.  If w is a reader, we must wake all the other
      // waiters with wake==true as well.  We may also need to queue
      // ourselves if waitp != null.  The spinlock and the lock are still
      // held.

      // This traverses the list in [ pw->next, h ], where h is the head,
      // removing all elements with wake==true and placing them in the
      // singly-linked list wake_list.  Returns the new head.
      h = DequeueAllWakeable(h, pw, &wake_list);

      intptr_t nv = (v & kMuEvent) | kMuDesig;
      // assume no waiters left,
      // set kMuDesig for INV1a

      if (waitp != nullptr) {  // we must queue ourselves and sleep
        h = Enqueue(h, waitp, v, kMuIsCond);
        // h is new last waiter; could be null if we queued ourselves on a
        // CondVar
      }

      ABSL_RAW_CHECK(wake_list != kPerThreadSynchNull,
                     "unexpected empty wake list");

      if (h != nullptr) {  // there are waiters left
        h->readers = 0;
        h->maybe_unlocking = false;  // finished unlocking
        nv |= wr_wait | kMuWait | reinterpret_cast<intptr_t>(h);
      }

      // release both spinlock & lock
      // can release with a store because there were waiters
      mu_.store(nv, std::memory_order_release);
      break;  // out of for(;;)-loop
    }
    // aggressive here; no one can proceed till we do
    c = synchronization_internal::MutexDelay(c, AGGRESSIVE);
  }  // end of for(;;)-loop

  if (wake_list != kPerThreadSynchNull) {
    int64_t total_wait_cycles = 0;
    int64_t max_wait_cycles = 0;
    int64_t now = CycleClock::Now();
    do {
      // Profile lock contention events only if the waiter was trying to acquire
      // the lock, not waiting on a condition variable or Condition.
      if (!wake_list->cond_waiter) {
        int64_t cycles_waited =
            (now - wake_list->waitp->contention_start_cycles);
        total_wait_cycles += cycles_waited;
        if (max_wait_cycles == 0) max_wait_cycles = cycles_waited;
        wake_list->waitp->contention_start_cycles = now;
        wake_list->waitp->should_submit_contention_data = true;
      }
      wake_list = Wakeup(wake_list);  // wake waiters
    } while (wake_list != kPerThreadSynchNull);
    if (total_wait_cycles > 0) {
      mutex_tracer("slow release", this, total_wait_cycles);
      ABSL_TSAN_MUTEX_PRE_DIVERT(this, 0);
      submit_profile_data(total_wait_cycles);
      ABSL_TSAN_MUTEX_POST_DIVERT(this, 0);
    }
  }
}

// Used by CondVar implementation to reacquire mutex after waking from
// condition variable.  This routine is used instead of Lock() because the
// waiting thread may have been moved from the condition variable queue to the
// mutex queue without a wakeup, by Trans().  In that case, when the thread is
// finally woken, the woken thread will believe it has been woken from the
// condition variable (i.e. its PC will be in when in the CondVar code), when
// in fact it has just been woken from the mutex.  Thus, it must enter the slow
// path of the mutex in the same state as if it had just woken from the mutex.
// That is, it must ensure to clear kMuDesig (INV1b).
void Mutex::Trans(MuHow how) {
  this->LockSlow(how, nullptr, kMuHasBlocked | kMuIsCond);
}

// Used by CondVar implementation to effectively wake thread w from the
// condition variable.  If this mutex is free, we simply wake the thread.
// It will later acquire the mutex with high probability.  Otherwise, we
// enqueue thread w on this mutex.
void Mutex::Fer(PerThreadSynch* w) {
  SchedulingGuard::ScopedDisable disable_rescheduling;
  int c = 0;
  ABSL_RAW_CHECK(w->waitp->cond == nullptr,
                 "Mutex::Fer while waiting on Condition");
  ABSL_RAW_CHECK(w->waitp->cv_word == nullptr,
                 "Mutex::Fer with pending CondVar queueing");
  // The CondVar timeout is not relevant for the Mutex wait.
  w->waitp->timeout = {};
  for (;;) {
    intptr_t v = mu_.load(std::memory_order_relaxed);
    // Note: must not queue if the mutex is unlocked (nobody will wake it).
    // For example, we can have only kMuWait (conditional) or maybe
    // kMuWait|kMuWrWait.
    // conflicting != 0 implies that the waking thread cannot currently take
    // the mutex, which in turn implies that someone else has it and can wake
    // us if we queue.
    const intptr_t conflicting =
        kMuWriter | (w->waitp->how == kShared ? 0 : kMuReader);
    if ((v & conflicting) == 0) {
      w->next = nullptr;
      w->state.store(PerThreadSynch::kAvailable, std::memory_order_release);
      IncrementSynchSem(this, w);
      return;
    } else {
      if ((v & (kMuSpin | kMuWait)) == 0) {  // no waiters
        // This thread tries to become the one and only waiter.
        PerThreadSynch* new_h =
            Enqueue(nullptr, w->waitp, v, kMuIsCond | kMuIsFer);
        ABSL_RAW_CHECK(new_h != nullptr,
                       "Enqueue failed");  // we must queue ourselves
        if (mu_.compare_exchange_strong(
                v, reinterpret_cast<intptr_t>(new_h) | (v & kMuLow) | kMuWait,
                std::memory_order_release, std::memory_order_relaxed)) {
          return;
        }
      } else if ((v & kMuSpin) == 0 &&
                 mu_.compare_exchange_strong(v, v | kMuSpin | kMuWait)) {
        PerThreadSynch* h = GetPerThreadSynch(v);
        PerThreadSynch* new_h = Enqueue(h, w->waitp, v, kMuIsCond | kMuIsFer);
        ABSL_RAW_CHECK(new_h != nullptr,
                       "Enqueue failed");  // we must queue ourselves
        do {
          v = mu_.load(std::memory_order_relaxed);
        } while (!mu_.compare_exchange_weak(
            v,
            (v & kMuLow & ~kMuSpin) | kMuWait |
                reinterpret_cast<intptr_t>(new_h),
            std::memory_order_release, std::memory_order_relaxed));
        return;
      }
    }
    c = synchronization_internal::MutexDelay(c, GENTLE);
  }
}

void Mutex::AssertHeld() const {
  if ((mu_.load(std::memory_order_relaxed) & kMuWriter) == 0) {
    SynchEvent* e = GetSynchEvent(this);
    ABSL_RAW_LOG(FATAL, "thread should hold write lock on Mutex %p %s",
                 static_cast<const void*>(this), (e == nullptr ? "" : e->name));
  }
}

void Mutex::AssertReaderHeld() const {
  if ((mu_.load(std::memory_order_relaxed) & (kMuReader | kMuWriter)) == 0) {
    SynchEvent* e = GetSynchEvent(this);
    ABSL_RAW_LOG(FATAL,
                 "thread should hold at least a read lock on Mutex %p %s",
                 static_cast<const void*>(this), (e == nullptr ? "" : e->name));
  }
}

// -------------------------------- condition variables
static const intptr_t kCvSpin = 0x0001L;   // spinlock protects waiter list
static const intptr_t kCvEvent = 0x0002L;  // record events

static const intptr_t kCvLow = 0x0003L;  // low order bits of CV

// Hack to make constant values available to gdb pretty printer
enum {
  kGdbCvSpin = kCvSpin,
  kGdbCvEvent = kCvEvent,
  kGdbCvLow = kCvLow,
};

static_assert(PerThreadSynch::kAlignment > kCvLow,
              "PerThreadSynch::kAlignment must be greater than kCvLow");

void CondVar::EnableDebugLog(const char* name) {
  SynchEvent* e = EnsureSynchEvent(&this->cv_, name, kCvEvent, kCvSpin);
  e->log = true;
  UnrefSynchEvent(e);
}

// Remove thread s from the list of waiters on this condition variable.
void CondVar::Remove(PerThreadSynch* s) {
  SchedulingGuard::ScopedDisable disable_rescheduling;
  intptr_t v;
  int c = 0;
  for (v = cv_.load(std::memory_order_relaxed);;
       v = cv_.load(std::memory_order_relaxed)) {
    if ((v & kCvSpin) == 0 &&  // attempt to acquire spinlock
        cv_.compare_exchange_strong(v, v | kCvSpin, std::memory_order_acquire,
                                    std::memory_order_relaxed)) {
      PerThreadSynch* h = reinterpret_cast<PerThreadSynch*>(v & ~kCvLow);
      if (h != nullptr) {
        PerThreadSynch* w = h;
        while (w->next != s && w->next != h) {  // search for thread
          w = w->next;
        }
        if (w->next == s) {  // found thread; remove it
          w->next = s->next;
          if (h == s) {
            h = (w == s) ? nullptr : w;
          }
          s->next = nullptr;
          s->state.store(PerThreadSynch::kAvailable, std::memory_order_release);
        }
      }
      // release spinlock
      cv_.store((v & kCvEvent) | reinterpret_cast<intptr_t>(h),
                std::memory_order_release);
      return;
    } else {
      // try again after a delay
      c = synchronization_internal::MutexDelay(c, GENTLE);
    }
  }
}

// Queue thread waitp->thread on condition variable word cv_word using
// wait parameters waitp.
// We split this into a separate routine, rather than simply doing it as part
// of WaitCommon().  If we were to queue ourselves on the condition variable
// before calling Mutex::UnlockSlow(), the Mutex code might be re-entered (via
// the logging code, or via a Condition function) and might potentially attempt
// to block this thread.  That would be a problem if the thread were already on
// a condition variable waiter queue.  Thus, we use the waitp->cv_word to tell
// the unlock code to call CondVarEnqueue() to queue the thread on the condition
// variable queue just before the mutex is to be unlocked, and (most
// importantly) after any call to an external routine that might re-enter the
// mutex code.
static void CondVarEnqueue(SynchWaitParams* waitp) {
  // This thread might be transferred to the Mutex queue by Fer() when
  // we are woken.  To make sure that is what happens, Enqueue() doesn't
  // call CondVarEnqueue() again but instead uses its normal code.  We
  // must do this before we queue ourselves so that cv_word will be null
  // when seen by the dequeuer, who may wish immediately to requeue
  // this thread on another queue.
  std::atomic<intptr_t>* cv_word = waitp->cv_word;
  waitp->cv_word = nullptr;

  intptr_t v = cv_word->load(std::memory_order_relaxed);
  int c = 0;
  while ((v & kCvSpin) != 0 ||  // acquire spinlock
         !cv_word->compare_exchange_weak(v, v | kCvSpin,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
    c = synchronization_internal::MutexDelay(c, GENTLE);
    v = cv_word->load(std::memory_order_relaxed);
  }
  ABSL_RAW_CHECK(waitp->thread->waitp == nullptr, "waiting when shouldn't be");
  waitp->thread->waitp = waitp;  // prepare ourselves for waiting
  PerThreadSynch* h = reinterpret_cast<PerThreadSynch*>(v & ~kCvLow);
  if (h == nullptr) {  // add this thread to waiter list
    waitp->thread->next = waitp->thread;
  } else {
    waitp->thread->next = h->next;
    h->next = waitp->thread;
  }
  waitp->thread->state.store(PerThreadSynch::kQueued,
                             std::memory_order_relaxed);
  cv_word->store((v & kCvEvent) | reinterpret_cast<intptr_t>(waitp->thread),
                 std::memory_order_release);
}

bool CondVar::WaitCommon(Mutex* mutex, KernelTimeout t) {
  bool rc = false;  // return value; true iff we timed-out

  intptr_t mutex_v = mutex->mu_.load(std::memory_order_relaxed);
  Mutex::MuHow mutex_how = ((mutex_v & kMuWriter) != 0) ? kExclusive : kShared;
  ABSL_TSAN_MUTEX_PRE_UNLOCK(mutex, TsanFlags(mutex_how));

  // maybe trace this call
  intptr_t v = cv_.load(std::memory_order_relaxed);
  cond_var_tracer("Wait", this);
  if ((v & kCvEvent) != 0) {
    PostSynchEvent(this, SYNCH_EV_WAIT);
  }

  // Release mu and wait on condition variable.
  SynchWaitParams waitp(mutex_how, nullptr, t, mutex,
                        Synch_GetPerThreadAnnotated(mutex), &cv_);
  // UnlockSlow() will call CondVarEnqueue() just before releasing the
  // Mutex, thus queuing this thread on the condition variable.  See
  // CondVarEnqueue() for the reasons.
  mutex->UnlockSlow(&waitp);

  // wait for signal
  while (waitp.thread->state.load(std::memory_order_acquire) ==
         PerThreadSynch::kQueued) {
    if (!Mutex::DecrementSynchSem(mutex, waitp.thread, t)) {
      // DecrementSynchSem returned due to timeout.
      // Now we will either (1) remove ourselves from the wait list in Remove
      // below, in which case Remove will set thread.state = kAvailable and
      // we will not call DecrementSynchSem again; or (2) Signal/SignalAll
      // has removed us concurrently and is calling Wakeup, which will set
      // thread.state = kAvailable and post to the semaphore.
      // It's important to reset the timeout for the case (2) because otherwise
      // we can live-lock in this loop since DecrementSynchSem will always
      // return immediately due to timeout, but Signal/SignalAll is not
      // necessary set thread.state = kAvailable yet (and is not scheduled
      // due to thread priorities or other scheduler artifacts).
      // Note this could also be resolved if Signal/SignalAll would set
      // thread.state = kAvailable while holding the wait list spin lock.
      // But this can't be easily done for SignalAll since it grabs the whole
      // wait list with a single compare-exchange and does not really grab
      // the spin lock.
      t = KernelTimeout::Never();
      this->Remove(waitp.thread);
      rc = true;
    }
  }

  ABSL_RAW_CHECK(waitp.thread->waitp != nullptr, "not waiting when should be");
  waitp.thread->waitp = nullptr;  // cleanup

  // maybe trace this call
  cond_var_tracer("Unwait", this);
  if ((v & kCvEvent) != 0) {
    PostSynchEvent(this, SYNCH_EV_WAIT_RETURNING);
  }

  // From synchronization point of view Wait is unlock of the mutex followed
  // by lock of the mutex. We've annotated start of unlock in the beginning
  // of the function. Now, finish unlock and annotate lock of the mutex.
  // (Trans is effectively lock).
  ABSL_TSAN_MUTEX_POST_UNLOCK(mutex, TsanFlags(mutex_how));
  ABSL_TSAN_MUTEX_PRE_LOCK(mutex, TsanFlags(mutex_how));
  mutex->Trans(mutex_how);  // Reacquire mutex
  ABSL_TSAN_MUTEX_POST_LOCK(mutex, TsanFlags(mutex_how), 0);
  return rc;
}

void CondVar::Signal() {
  SchedulingGuard::ScopedDisable disable_rescheduling;
  ABSL_TSAN_MUTEX_PRE_SIGNAL(nullptr, 0);
  intptr_t v;
  int c = 0;
  for (v = cv_.load(std::memory_order_relaxed); v != 0;
       v = cv_.load(std::memory_order_relaxed)) {
    if ((v & kCvSpin) == 0 &&  // attempt to acquire spinlock
        cv_.compare_exchange_strong(v, v | kCvSpin, std::memory_order_acquire,
                                    std::memory_order_relaxed)) {
      PerThreadSynch* h = reinterpret_cast<PerThreadSynch*>(v & ~kCvLow);
      PerThreadSynch* w = nullptr;
      if (h != nullptr) {  // remove first waiter
        w = h->next;
        if (w == h) {
          h = nullptr;
        } else {
          h->next = w->next;
        }
      }
      // release spinlock
      cv_.store((v & kCvEvent) | reinterpret_cast<intptr_t>(h),
                std::memory_order_release);
      if (w != nullptr) {
        w->waitp->cvmu->Fer(w);  // wake waiter, if there was one
        cond_var_tracer("Signal wakeup", this);
      }
      if ((v & kCvEvent) != 0) {
        PostSynchEvent(this, SYNCH_EV_SIGNAL);
      }
      ABSL_TSAN_MUTEX_POST_SIGNAL(nullptr, 0);
      return;
    } else {
      c = synchronization_internal::MutexDelay(c, GENTLE);
    }
  }
  ABSL_TSAN_MUTEX_POST_SIGNAL(nullptr, 0);
}

void CondVar::SignalAll() {
  ABSL_TSAN_MUTEX_PRE_SIGNAL(nullptr, 0);
  intptr_t v;
  int c = 0;
  for (v = cv_.load(std::memory_order_relaxed); v != 0;
       v = cv_.load(std::memory_order_relaxed)) {
    // empty the list if spinlock free
    // We do this by simply setting the list to empty using
    // compare and swap.   We then have the entire list in our hands,
    // which cannot be changing since we grabbed it while no one
    // held the lock.
    if ((v & kCvSpin) == 0 &&
        cv_.compare_exchange_strong(v, v & kCvEvent, std::memory_order_acquire,
                                    std::memory_order_relaxed)) {
      PerThreadSynch* h = reinterpret_cast<PerThreadSynch*>(v & ~kCvLow);
      if (h != nullptr) {
        PerThreadSynch* w;
        PerThreadSynch* n = h->next;
        do {  // for every thread, wake it up
          w = n;
          n = n->next;
          w->waitp->cvmu->Fer(w);
        } while (w != h);
        cond_var_tracer("SignalAll wakeup", this);
      }
      if ((v & kCvEvent) != 0) {
        PostSynchEvent(this, SYNCH_EV_SIGNALALL);
      }
      ABSL_TSAN_MUTEX_POST_SIGNAL(nullptr, 0);
      return;
    } else {
      // try again after a delay
      c = synchronization_internal::MutexDelay(c, GENTLE);
    }
  }
  ABSL_TSAN_MUTEX_POST_SIGNAL(nullptr, 0);
}

void ReleasableMutexLock::Release() {
  ABSL_RAW_CHECK(this->mu_ != nullptr,
                 "ReleasableMutexLock::Release may only be called once");
  this->mu_->Unlock();
  this->mu_ = nullptr;
}

#ifdef ABSL_HAVE_THREAD_SANITIZER
extern "C" void __tsan_read1(void* addr);
#else
#define __tsan_read1(addr)  // do nothing if TSan not enabled
#endif

// A function that just returns its argument, dereferenced
static bool Dereference(void* arg) {
  // ThreadSanitizer does not instrument this file for memory accesses.
  // This function dereferences a user variable that can participate
  // in a data race, so we need to manually tell TSan about this memory access.
  __tsan_read1(arg);
  return *(static_cast<bool*>(arg));
}

ABSL_CONST_INIT const Condition Condition::kTrue;

Condition::Condition(bool (*func)(void*), void* arg)
    : eval_(&CallVoidPtrFunction), arg_(arg) {
  static_assert(sizeof(&func) <= sizeof(callback_),
                "An overlarge function pointer passed to Condition.");
  StoreCallback(func);
}

bool Condition::CallVoidPtrFunction(const Condition* c) {
  using FunctionPointer = bool (*)(void*);
  FunctionPointer function_pointer;
  std::memcpy(&function_pointer, c->callback_, sizeof(function_pointer));
  return (*function_pointer)(c->arg_);
}

Condition::Condition(const bool* cond)
    : eval_(CallVoidPtrFunction),
      // const_cast is safe since Dereference does not modify arg
      arg_(const_cast<bool*>(cond)) {
  using FunctionPointer = bool (*)(void*);
  const FunctionPointer dereference = Dereference;
  StoreCallback(dereference);
}

bool Condition::Eval() const { return (*this->eval_)(this); }

bool Condition::GuaranteedEqual(const Condition* a, const Condition* b) {
  if (a == nullptr || b == nullptr) {
    return a == b;
  }
  // Check equality of the representative fields.
  return a->eval_ == b->eval_ && a->arg_ == b->arg_ &&
         !memcmp(a->callback_, b->callback_, sizeof(a->callback_));
}

ABSL_NAMESPACE_END
}  // namespace absl
