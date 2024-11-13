// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/time.h"

#if V8_OS_POSIX
#include <fcntl.h>  // for O_RDONLY
#include <sys/time.h>
#include <unistd.h>
#endif

#if V8_OS_DARWIN
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#endif

#if V8_OS_FUCHSIA
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>
#endif

#if V8_OS_STARBOARD
#include <sys/time.h>
#endif  // V8_OS_STARBOARD

#include <cstring>
#include <ostream>

#if V8_OS_WIN
#include <windows.h>

// This has to come after windows.h.
#include <mmsystem.h>  // For timeGetTime().

#include <atomic>

#include "src/base/lazy-instance.h"
#include "src/base/win32-headers.h"
#endif
#include "src/base/cpu.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"

#if V8_OS_STARBOARD
#include "starboard/common/time.h"
#endif

namespace {

#if V8_OS_DARWIN
int64_t ComputeThreadTicks() {
  mach_msg_type_number_t thread_info_count = THREAD_BASIC_INFO_COUNT;
  thread_basic_info_data_t thread_info_data;
  kern_return_t kr = thread_info(
      pthread_mach_thread_np(pthread_self()),
      THREAD_BASIC_INFO,
      reinterpret_cast<thread_info_t>(&thread_info_data),
      &thread_info_count);
  CHECK_EQ(kr, KERN_SUCCESS);

  // We can add the seconds into a {int64_t} without overflow.
  CHECK_LE(thread_info_data.user_time.seconds,
           std::numeric_limits<int64_t>::max() -
               thread_info_data.system_time.seconds);
  int64_t seconds =
      thread_info_data.user_time.seconds + thread_info_data.system_time.seconds;
  // Multiplying the seconds by {kMicrosecondsPerSecond}, and adding something
  // in [0, 2 * kMicrosecondsPerSecond) must result in a valid {int64_t}.
  static constexpr int64_t kSecondsLimit =
      (std::numeric_limits<int64_t>::max() /
       v8::base::Time::kMicrosecondsPerSecond) -
      2;
  CHECK_GT(kSecondsLimit, seconds);
  int64_t micros = seconds * v8::base::Time::kMicrosecondsPerSecond;
  micros += (thread_info_data.user_time.microseconds +
             thread_info_data.system_time.microseconds);
  return micros;
}
#elif V8_OS_FUCHSIA
V8_INLINE int64_t GetFuchsiaThreadTicks() {
  zx_info_thread_stats_t info;
  zx_status_t status = zx_object_get_info(thrd_get_zx_handle(thrd_current()),
                                          ZX_INFO_THREAD_STATS, &info,
                                          sizeof(info), nullptr, nullptr);
  CHECK_EQ(status, ZX_OK);
  return info.total_runtime / v8::base::Time::kNanosecondsPerMicrosecond;
}
#elif V8_OS_POSIX
// Helper function to get results from clock_gettime() and convert to a
// microsecond timebase. Minimum requirement is MONOTONIC_CLOCK to be supported
// on the system. FreeBSD 6 has CLOCK_MONOTONIC but defines
// _POSIX_MONOTONIC_CLOCK to -1.
V8_INLINE int64_t ClockNow(clockid_t clk_id) {
#if (defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0) || \
    defined(V8_OS_BSD) || defined(V8_OS_ANDROID) || defined(V8_OS_ZOS)
#if defined(V8_OS_AIX)
  // On AIX clock_gettime for CLOCK_THREAD_CPUTIME_ID outputs time with
  // resolution of 10ms. thread_cputime API provides the time in ns.
  if (clk_id == CLOCK_THREAD_CPUTIME_ID) {
#if defined(__PASE__)  // CLOCK_THREAD_CPUTIME_ID clock not supported on IBMi
    return 0;
#else
    thread_cputime_t tc;
    if (thread_cputime(-1, &tc) != 0) {
      UNREACHABLE();
    }
    return (tc.stime / v8::base::Time::kNanosecondsPerMicrosecond)
           + (tc.utime / v8::base::Time::kNanosecondsPerMicrosecond);
#endif  // defined(__PASE__)
  }
#endif  // defined(V8_OS_AIX)
  struct timespec ts;
  if (clock_gettime(clk_id, &ts) != 0) {
    UNREACHABLE();
  }
  // Multiplying the seconds by {kMicrosecondsPerSecond}, and adding something
  // in [0, kMicrosecondsPerSecond) must result in a valid {int64_t}.
  static constexpr int64_t kSecondsLimit =
      (std::numeric_limits<int64_t>::max() /
       v8::base::Time::kMicrosecondsPerSecond) -
      1;
  CHECK_GT(kSecondsLimit, ts.tv_sec);
  int64_t result = int64_t{ts.tv_sec} * v8::base::Time::kMicrosecondsPerSecond;
  result += (ts.tv_nsec / v8::base::Time::kNanosecondsPerMicrosecond);
  return result;
#else  // Monotonic clock not supported.
  return 0;
#endif
}

V8_INLINE int64_t NanosecondsNow() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return int64_t{ts.tv_sec} * v8::base::Time::kNanosecondsPerSecond +
         ts.tv_nsec;
}

inline bool IsHighResolutionTimer(clockid_t clk_id) {
  // Currently this is only needed for CLOCK_MONOTONIC. If other clocks need
  // to be checked, care must be taken to support all platforms correctly;
  // see ClockNow() above for precedent.
  DCHECK_EQ(clk_id, CLOCK_MONOTONIC);
  int64_t previous = NanosecondsNow();
  // There should be enough attempts to make the loop run for more than one
  // microsecond if the early return is not taken -- the elapsed time can't
  // be measured in that situation, so we have to estimate it offline.
  constexpr int kAttempts = 100;
  for (int i = 0; i < kAttempts; i++) {
    int64_t next = NanosecondsNow();
    int64_t delta = next - previous;
    if (delta == 0) continue;
    // We expect most systems to take this branch on the first iteration.
    if (delta <= v8::base::Time::kNanosecondsPerMicrosecond) {
      return true;
    }
    previous = next;
  }
  // As of 2022, we expect that the loop above has taken at least 2 μs (on
  // a fast desktop). If we still haven't seen a non-zero clock increment
  // in sub-microsecond range, assume a low resolution timer.
  return false;
}

#elif V8_OS_WIN
// Returns the current value of the performance counter.
V8_INLINE uint64_t QPCNowRaw() {
  LARGE_INTEGER perf_counter_now = {};
  // According to the MSDN documentation for QueryPerformanceCounter(), this
  // will never fail on systems that run XP or later.
  // https://msdn.microsoft.com/library/windows/desktop/ms644904.aspx
  BOOL result = ::QueryPerformanceCounter(&perf_counter_now);
  DCHECK(result);
  USE(result);
  return perf_counter_now.QuadPart;
}
#endif  // V8_OS_DARWIN

}  // namespace

namespace v8 {
namespace base {

int TimeDelta::InDays() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(delta_ / Time::kMicrosecondsPerDay);
}

int TimeDelta::InHours() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(delta_ / Time::kMicrosecondsPerHour);
}

int TimeDelta::InMinutes() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(delta_ / Time::kMicrosecondsPerMinute);
}

double TimeDelta::InSecondsF() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<double>::infinity();
  }
  return static_cast<double>(delta_) / Time::kMicrosecondsPerSecond;
}

int64_t TimeDelta::InSeconds() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return delta_ / Time::kMicrosecondsPerSecond;
}

double TimeDelta::InMillisecondsF() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<double>::infinity();
  }
  return static_cast<double>(delta_) / Time::kMicrosecondsPerMillisecond;
}

int64_t TimeDelta::InMilliseconds() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return delta_ / Time::kMicrosecondsPerMillisecond;
}

int64_t TimeDelta::InMillisecondsRoundedUp() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return (delta_ + Time::kMicrosecondsPerMillisecond - 1) /
         Time::kMicrosecondsPerMillisecond;
}

int64_t TimeDelta::InMicroseconds() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return delta_;
}

int64_t TimeDelta::InNanoseconds() const {
  if (IsMax()) {
    // Preserve max to prevent overflow.
    return std::numeric_limits<int64_t>::max();
  }
  return delta_ * Time::kNanosecondsPerMicrosecond;
}

#if V8_OS_DARWIN

TimeDelta TimeDelta::FromMachTimespec(struct mach_timespec ts) {
  DCHECK_GE(ts.tv_nsec, 0);
  DCHECK_LT(ts.tv_nsec,
            static_cast<long>(Time::kNanosecondsPerSecond));  // NOLINT
  return TimeDelta(ts.tv_sec * Time::kMicrosecondsPerSecond +
                   ts.tv_nsec / Time::kNanosecondsPerMicrosecond);
}


struct mach_timespec TimeDelta::ToMachTimespec() const {
  struct mach_timespec ts;
  DCHECK_GE(delta_, 0);
  ts.tv_sec = static_cast<unsigned>(delta_ / Time::kMicrosecondsPerSecond);
  ts.tv_nsec = (delta_ % Time::kMicrosecondsPerSecond) *
      Time::kNanosecondsPerMicrosecond;
  return ts;
}

#endif  // V8_OS_DARWIN

#if V8_OS_POSIX

TimeDelta TimeDelta::FromTimespec(struct timespec ts) {
  DCHECK_GE(ts.tv_nsec, 0);
  DCHECK_LT(ts.tv_nsec,
            static_cast<long>(Time::kNanosecondsPerSecond));  // NOLINT
  return TimeDelta(ts.tv_sec * Time::kMicrosecondsPerSecond +
                   ts.tv_nsec / Time::kNanosecondsPerMicrosecond);
}


struct timespec TimeDelta::ToTimespec() const {
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(delta_ / Time::kMicrosecondsPerSecond);
  ts.tv_nsec = (delta_ % Time::kMicrosecondsPerSecond) *
      Time::kNanosecondsPerMicrosecond;
  return ts;
}

#endif  // V8_OS_POSIX


#if V8_OS_WIN

// We implement time using the high-resolution timers so that we can get
// timeouts which are smaller than 10-15ms. To avoid any drift, we
// periodically resync the internal clock to the system clock.
class Clock final {
 public:
  Clock() : initial_ticks_(GetSystemTicks()), initial_time_(GetSystemTime()) {}

  Time Now() {
    // Time between resampling the un-granular clock for this API (1 minute).
    const TimeDelta kMaxElapsedTime = TimeDelta::FromMinutes(1);

    MutexGuard lock_guard(&mutex_);

    // Determine current time and ticks.
    TimeTicks ticks = GetSystemTicks();
    Time time = GetSystemTime();

    // Check if we need to synchronize with the system clock due to a backwards
    // time change or the amount of time elapsed.
    TimeDelta elapsed = ticks - initial_ticks_;
    if (time < initial_time_ || elapsed > kMaxElapsedTime) {
      initial_ticks_ = ticks;
      initial_time_ = time;
      return time;
    }

    return initial_time_ + elapsed;
  }

  Time NowFromSystemTime() {
    MutexGuard lock_guard(&mutex_);
    initial_ticks_ = GetSystemTicks();
    initial_time_ = GetSystemTime();
    return initial_time_;
  }

 private:
  static TimeTicks GetSystemTicks() {
    return TimeTicks::Now();
  }

  static Time GetSystemTime() {
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    return Time::FromFiletime(ft);
  }

  TimeTicks initial_ticks_;
  Time initial_time_;
  Mutex mutex_;
};

namespace {
DEFINE_LAZY_LEAKY_OBJECT_GETTER(Clock, GetClock)
}  // namespace

Time Time::Now() { return GetClock()->Now(); }

Time Time::NowFromSystemTime() { return GetClock()->NowFromSystemTime(); }

// Time between windows epoch and standard epoch.
static const int64_t kTimeToEpochInMicroseconds = int64_t{11644473600000000};

Time Time::FromFiletime(FILETIME ft) {
  if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) {
    return Time();
  }
  if (ft.dwLowDateTime == std::numeric_limits<DWORD>::max() &&
      ft.dwHighDateTime == std::numeric_limits<DWORD>::max()) {
    return Max();
  }
  int64_t us = (static_cast<uint64_t>(ft.dwLowDateTime) +
                (static_cast<uint64_t>(ft.dwHighDateTime) << 32)) / 10;
  return Time(us - kTimeToEpochInMicroseconds);
}


FILETIME Time::ToFiletime() const {
  DCHECK_GE(us_, 0);
  FILETIME ft;
  if (IsNull()) {
    ft.dwLowDateTime = 0;
    ft.dwHighDateTime = 0;
    return ft;
  }
  if (IsMax()) {
    ft.dwLowDateTime = std::numeric_limits<DWORD>::max();
    ft.dwHighDateTime = std::numeric_limits<DWORD>::max();
    return ft;
  }
  uint64_t us = static_cast<uint64_t>(us_ + kTimeToEpochInMicroseconds) * 10;
  ft.dwLowDateTime = static_cast<DWORD>(us);
  ft.dwHighDateTime = static_cast<DWORD>(us >> 32);
  return ft;
}

#elif V8_OS_POSIX || V8_OS_STARBOARD

Time Time::Now() {
  struct timeval tv;
  int result = gettimeofday(&tv, nullptr);
  DCHECK_EQ(0, result);
  USE(result);
  return FromTimeval(tv);
}


Time Time::NowFromSystemTime() {
  return Now();
}


Time Time::FromTimespec(struct timespec ts) {
  DCHECK_GE(ts.tv_nsec, 0);
  DCHECK_LT(ts.tv_nsec, kNanosecondsPerSecond);
  if (ts.tv_nsec == 0 && ts.tv_sec == 0) {
    return Time();
  }
  if (ts.tv_nsec == static_cast<long>(kNanosecondsPerSecond - 1) &&  // NOLINT
      ts.tv_sec == std::numeric_limits<time_t>::max()) {
    return Max();
  }
  return Time(ts.tv_sec * kMicrosecondsPerSecond +
              ts.tv_nsec / kNanosecondsPerMicrosecond);
}


struct timespec Time::ToTimespec() const {
  struct timespec ts;
  if (IsNull()) {
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    return ts;
  }
  if (IsMax()) {
    ts.tv_sec = std::numeric_limits<time_t>::max();
    ts.tv_nsec = static_cast<long>(kNanosecondsPerSecond - 1);  // NOLINT
    return ts;
  }
  ts.tv_sec = static_cast<time_t>(us_ / kMicrosecondsPerSecond);
  ts.tv_nsec = (us_ % kMicrosecondsPerSecond) * kNanosecondsPerMicrosecond;
  return ts;
}


Time Time::FromTimeval(struct timeval tv) {
  DCHECK_GE(tv.tv_usec, 0);
  DCHECK(tv.tv_usec < static_cast<suseconds_t>(kMicrosecondsPerSecond));
  if (tv.tv_usec == 0 && tv.tv_sec == 0) {
    return Time();
  }
  if (tv.tv_usec == static_cast<suseconds_t>(kMicrosecondsPerSecond - 1) &&
      tv.tv_sec == std::numeric_limits<time_t>::max()) {
    return Max();
  }
  return Time(tv.tv_sec * kMicrosecondsPerSecond + tv.tv_usec);
}


struct timeval Time::ToTimeval() const {
  struct timeval tv;
  if (IsNull()) {
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return tv;
  }
  if (IsMax()) {
    tv.tv_sec = std::numeric_limits<time_t>::max();
    tv.tv_usec = static_cast<suseconds_t>(kMicrosecondsPerSecond - 1);
    return tv;
  }
  tv.tv_sec = static_cast<time_t>(us_ / kMicrosecondsPerSecond);
  tv.tv_usec = us_ % kMicrosecondsPerSecond;
  return tv;
}

#endif  // V8_OS_POSIX || V8_OS_STARBOARD

Time Time::FromJsTime(double ms_since_epoch) {
  // The epoch is a valid time, so this constructor doesn't interpret
  // 0 as the null time.
  if (ms_since_epoch == std::numeric_limits<double>::max()) {
    return Max();
  }
  return Time(
      static_cast<int64_t>(ms_since_epoch * kMicrosecondsPerMillisecond));
}


double Time::ToJsTime() const {
  if (IsNull()) {
    // Preserve 0 so the invalid result doesn't depend on the platform.
    return 0;
  }
  if (IsMax()) {
    // Preserve max without offset to prevent overflow.
    return std::numeric_limits<double>::max();
  }
  return static_cast<double>(us_) / kMicrosecondsPerMillisecond;
}


std::ostream& operator<<(std::ostream& os, const Time& time) {
  return os << time.ToJsTime();
}


#if V8_OS_WIN

namespace {

// We define a wrapper to adapt between the __stdcall and __cdecl call of the
// mock function, and to avoid a static constructor.  Assigning an import to a
// function pointer directly would require setup code to fetch from the IAT.
DWORD timeGetTimeWrapper() { return timeGetTime(); }

DWORD (*g_tick_function)(void) = &timeGetTimeWrapper;

// A structure holding the most significant bits of "last seen" and a
// "rollover" counter.
union LastTimeAndRolloversState {
  // The state as a single 32-bit opaque value.
  int32_t as_opaque_32;

  // The state as usable values.
  struct {
    // The top 8-bits of the "last" time. This is enough to check for rollovers
    // and the small bit-size means fewer CompareAndSwap operations to store
    // changes in state, which in turn makes for fewer retries.
    uint8_t last_8;
    // A count of the number of detected rollovers. Using this as bits 47-32
    // of the upper half of a 64-bit value results in a 48-bit tick counter.
    // This extends the total rollover period from about 49 days to about 8800
    // years while still allowing it to be stored with last_8 in a single
    // 32-bit value.
    uint16_t rollovers;
  } as_values;
};
std::atomic<int32_t> g_last_time_and_rollovers{0};
static_assert(sizeof(LastTimeAndRolloversState) <=
                  sizeof(g_last_time_and_rollovers),
              "LastTimeAndRolloversState does not fit in a single atomic word");

// We use timeGetTime() to implement TimeTicks::Now().  This can be problematic
// because it returns the number of milliseconds since Windows has started,
// which will roll over the 32-bit value every ~49 days.  We try to track
// rollover ourselves, which works if TimeTicks::Now() is called at least every
// 48.8 days (not 49 days because only changes in the top 8 bits get noticed).
TimeTicks RolloverProtectedNow() {
  LastTimeAndRolloversState state;
  DWORD now;  // DWORD is always unsigned 32 bits.

  // Fetch the "now" and "last" tick values, updating "last" with "now" and
  // incrementing the "rollovers" counter if the tick-value has wrapped back
  // around. Atomic operations ensure that both "last" and "rollovers" are
  // always updated together.
  int32_t original = g_last_time_and_rollovers.load(std::memory_order_acquire);
  while (true) {
    state.as_opaque_32 = original;
    now = g_tick_function();
    uint8_t now_8 = static_cast<uint8_t>(now >> 24);
    if (now_8 < state.as_values.last_8) ++state.as_values.rollovers;
    state.as_values.last_8 = now_8;

    // If the state hasn't changed, exit the loop.
    if (state.as_opaque_32 == original) break;

    // Save the changed state. If the existing value is unchanged from the
    // original, exit the loop.
    if (g_last_time_and_rollovers.compare_exchange_weak(
            original, state.as_opaque_32, std::memory_order_acq_rel)) {
      break;
    }

    // Another thread has done something in between so retry from the top.
    // {original} has been updated by the {compare_exchange_weak}.
  }

  return TimeTicks() +
         TimeDelta::FromMilliseconds(
             now + (static_cast<uint64_t>(state.as_values.rollovers) << 32));
}

// Discussion of tick counter options on Windows:
//
// (1) CPU cycle counter. (Retrieved via RDTSC)
// The CPU counter provides the highest resolution time stamp and is the least
// expensive to retrieve. However, on older CPUs, two issues can affect its
// reliability: First it is maintained per processor and not synchronized
// between processors. Also, the counters will change frequency due to thermal
// and power changes, and stop in some states.
//
// (2) QueryPerformanceCounter (QPC). The QPC counter provides a high-
// resolution (<1 microsecond) time stamp. On most hardware running today, it
// auto-detects and uses the constant-rate RDTSC counter to provide extremely
// efficient and reliable time stamps.
//
// On older CPUs where RDTSC is unreliable, it falls back to using more
// expensive (20X to 40X more costly) alternate clocks, such as HPET or the ACPI
// PM timer, and can involve system calls; and all this is up to the HAL (with
// some help from ACPI). According to
// http://blogs.msdn.com/oldnewthing/archive/2005/09/02/459952.aspx, in the
// worst case, it gets the counter from the rollover interrupt on the
// programmable interrupt timer. In best cases, the HAL may conclude that the
// RDTSC counter runs at a constant frequency, then it uses that instead. On
// multiprocessor machines, it will try to verify the values returned from
// RDTSC on each processor are consistent with each other, and apply a handful
// of workarounds for known buggy hardware. In other words, QPC is supposed to
// give consistent results on a multiprocessor computer, but for older CPUs it
// can be unreliable due bugs in BIOS or HAL.
//
// (3) System time. The system time provides a low-resolution (from ~1 to ~15.6
// milliseconds) time stamp but is comparatively less expensive to retrieve and
// more reliable. Time::EnableHighResolutionTimer() and
// Time::ActivateHighResolutionTimer() can be called to alter the resolution of
// this timer; and also other Windows applications can alter it, affecting this
// one.

TimeTicks InitialTimeTicksNowFunction();

// See "threading notes" in InitializeNowFunctionPointer() for details on how
// concurrent reads/writes to these globals has been made safe.
using TimeTicksNowFunction = decltype(&TimeTicks::Now);
TimeTicksNowFunction g_time_ticks_now_function = &InitialTimeTicksNowFunction;
int64_t g_qpc_ticks_per_second = 0;

TimeDelta QPCValueToTimeDelta(LONGLONG qpc_value) {
  // Ensure that the assignment to |g_qpc_ticks_per_second|, made in
  // InitializeNowFunctionPointer(), has happened by this point.
  std::atomic_thread_fence(std::memory_order_acquire);

  DCHECK_GT(g_qpc_ticks_per_second, 0);

  // If the QPC Value is below the overflow threshold, we proceed with
  // simple multiply and divide.
  if (qpc_value < TimeTicks::kQPCOverflowThreshold) {
    return TimeDelta::FromMicroseconds(
        qpc_value * TimeTicks::kMicrosecondsPerSecond / g_qpc_ticks_per_second);
  }
  // Otherwise, calculate microseconds in a round about manner to avoid
  // overflow and precision issues.
  int64_t whole_seconds = qpc_value / g_qpc_ticks_per_second;
  int64_t leftover_ticks = qpc_value - (whole_seconds * g_qpc_ticks_per_second);
  return TimeDelta::FromMicroseconds(
      (whole_seconds * TimeTicks::kMicrosecondsPerSecond) +
      ((leftover_ticks * TimeTicks::kMicrosecondsPerSecond) /
       g_qpc_ticks_per_second));
}

TimeTicks QPCNow() { return TimeTicks() + QPCValueToTimeDelta(QPCNowRaw()); }

void InitializeTimeTicksNowFunctionPointer() {
  LARGE_INTEGER ticks_per_sec = {};
  if (!QueryPerformanceFrequency(&ticks_per_sec)) ticks_per_sec.QuadPart = 0;

  // If Windows cannot provide a QPC implementation, TimeTicks::Now() must use
  // the low-resolution clock.
  //
  // If the QPC implementation is expensive and/or unreliable, TimeTicks::Now()
  // will still use the low-resolution clock. A CPU lacking a non-stop time
  // counter will cause Windows to provide an alternate QPC implementation that
  // works, but is expensive to use. Certain Athlon CPUs are known to make the
  // QPC implementation unreliable.
  //
  // Otherwise, Now uses the high-resolution QPC clock. As of 21 August 2015,
  // ~72% of users fall within this category.
  TimeTicksNowFunction now_function;
  CPU cpu;
  if (ticks_per_sec.QuadPart <= 0 || !cpu.has_non_stop_time_stamp_counter()) {
    now_function = &RolloverProtectedNow;
  } else {
    now_function = &QPCNow;
  }

  // Threading note 1: In an unlikely race condition, it's possible for two or
  // more threads to enter InitializeNowFunctionPointer() in parallel. This is
  // not a problem since all threads should end up writing out the same values
  // to the global variables.
  //
  // Threading note 2: A release fence is placed here to ensure, from the
  // perspective of other threads using the function pointers, that the
  // assignment to |g_qpc_ticks_per_second| happens before the function pointers
  // are changed.
  g_qpc_ticks_per_second = ticks_per_sec.QuadPart;
  std::atomic_thread_fence(std::memory_order_release);
  g_time_ticks_now_function = now_function;
}

TimeTicks InitialTimeTicksNowFunction() {
  InitializeTimeTicksNowFunctionPointer();
  return g_time_ticks_now_function();
}

#if V8_HOST_ARCH_ARM64
// From MSDN, FILETIME "Contains a 64-bit value representing the number of
// 100-nanosecond intervals since January 1, 1601 (UTC)."
int64_t FileTimeToMicroseconds(const FILETIME& ft) {
  // Need to bit_cast to fix alignment, then divide by 10 to convert
  // 100-nanoseconds to microseconds. This only works on little-endian
  // machines.
  return bit_cast<int64_t, FILETIME>(ft) / 10;
}
#endif

}  // namespace

// static
TimeTicks TimeTicks::Now() {
  // Make sure we never return 0 here.
  TimeTicks ticks(g_time_ticks_now_function());
  DCHECK(!ticks.IsNull());
  return ticks;
}

// static
bool TimeTicks::IsHighResolution() {
  if (g_time_ticks_now_function == &InitialTimeTicksNowFunction)
    InitializeTimeTicksNowFunctionPointer();
  return g_time_ticks_now_function == &QPCNow;
}

#else  // V8_OS_WIN

TimeTicks TimeTicks::Now() {
  int64_t ticks;
#if V8_OS_DARWIN
  static struct mach_timebase_info info;
  if (info.denom == 0) {
    kern_return_t result = mach_timebase_info(&info);
    DCHECK_EQ(KERN_SUCCESS, result);
    USE(result);
  }
  ticks = (mach_absolute_time() / Time::kNanosecondsPerMicrosecond *
           info.numer / info.denom);
#elif V8_OS_SOLARIS
  ticks = (gethrtime() / Time::kNanosecondsPerMicrosecond);
#elif V8_OS_FUCHSIA
  ticks = zx_clock_get_monotonic() / Time::kNanosecondsPerMicrosecond;
#elif V8_OS_POSIX
  ticks = ClockNow(CLOCK_MONOTONIC);
#elif V8_OS_STARBOARD
  ticks = starboard::CurrentMonotonicTime();
#else
#error platform does not implement TimeTicks::Now.
#endif  // V8_OS_DARWIN
  // Make sure we never return 0 here.
  return TimeTicks(ticks + 1);
}

// static
bool TimeTicks::IsHighResolution() {
#if V8_OS_DARWIN
  return true;
#elif V8_OS_FUCHSIA
  return true;
#elif V8_OS_POSIX
  static const bool is_high_resolution = IsHighResolutionTimer(CLOCK_MONOTONIC);
  return is_high_resolution;
#else
  return true;
#endif
}

#endif  // V8_OS_WIN


bool ThreadTicks::IsSupported() {
#if V8_OS_STARBOARD
  return starboard::CurrentMonotonicThreadTime() != 0;
#elif defined(__PASE__)
  // Thread CPU time accounting is unavailable in PASE
  return false;
#elif (defined(_POSIX_THREAD_CPUTIME) && (_POSIX_THREAD_CPUTIME >= 0)) || \
    defined(V8_OS_DARWIN) || defined(V8_OS_ANDROID) ||                    \
    defined(V8_OS_SOLARIS) || defined(V8_OS_ZOS)
  return true;
#elif defined(V8_OS_WIN)
  return IsSupportedWin();
#else
  return false;
#endif
}


ThreadTicks ThreadTicks::Now() {
#if V8_OS_STARBOARD
  const int64_t now = starboard::CurrentMonotonicThreadTime();
  if (now != 0)
    return ThreadTicks(now);
  UNREACHABLE();
#elif V8_OS_DARWIN
  return ThreadTicks(ComputeThreadTicks());
#elif V8_OS_FUCHSIA
  return ThreadTicks(GetFuchsiaThreadTicks());
#elif (defined(_POSIX_THREAD_CPUTIME) && (_POSIX_THREAD_CPUTIME >= 0)) || \
    defined(V8_OS_ANDROID) || defined(V8_OS_ZOS)
  return ThreadTicks(ClockNow(CLOCK_THREAD_CPUTIME_ID));
#elif V8_OS_SOLARIS
  return ThreadTicks(gethrvtime() / Time::kNanosecondsPerMicrosecond);
#elif V8_OS_WIN
  return ThreadTicks::GetForThread(::GetCurrentThread());
#else
  UNREACHABLE();
#endif
}


#if V8_OS_WIN
ThreadTicks ThreadTicks::GetForThread(const HANDLE& thread_handle) {
  DCHECK(IsSupported());

#if V8_HOST_ARCH_ARM64
  // QueryThreadCycleTime versus TSCTicksPerSecond doesn't have much relation to
  // actual elapsed time on Windows on Arm, because QueryThreadCycleTime is
  // backed by the actual number of CPU cycles executed, rather than a
  // constant-rate timer like Intel. To work around this, use GetThreadTimes
  // (which isn't as accurate but is meaningful as a measure of elapsed
  // per-thread time).
  FILETIME creation_time, exit_time, kernel_time, user_time;
  ::GetThreadTimes(thread_handle, &creation_time, &exit_time, &kernel_time,
                   &user_time);

  int64_t us = FileTimeToMicroseconds(user_time);
  return ThreadTicks(us);
#else
  // Get the number of TSC ticks used by the current thread.
  ULONG64 thread_cycle_time = 0;
  ::QueryThreadCycleTime(thread_handle, &thread_cycle_time);

  // Get the frequency of the TSC.
  double tsc_ticks_per_second = TSCTicksPerSecond();
  if (tsc_ticks_per_second == 0)
    return ThreadTicks();

  // Return the CPU time of the current thread.
  double thread_time_seconds = thread_cycle_time / tsc_ticks_per_second;
  return ThreadTicks(
      static_cast<int64_t>(thread_time_seconds * Time::kMicrosecondsPerSecond));
#endif
}

// static
bool ThreadTicks::IsSupportedWin() {
  static bool is_supported = base::CPU().has_non_stop_time_stamp_counter();
  return is_supported;
}

// static
void ThreadTicks::WaitUntilInitializedWin() {
#ifndef V8_HOST_ARCH_ARM64
  while (TSCTicksPerSecond() == 0) ::Sleep(10);
#endif
}

#ifndef V8_HOST_ARCH_ARM64
double ThreadTicks::TSCTicksPerSecond() {
  DCHECK(IsSupported());

  // The value returned by QueryPerformanceFrequency() cannot be used as the TSC
  // frequency, because there is no guarantee that the TSC frequency is equal to
  // the performance counter frequency.

  // The TSC frequency is cached in a static variable because it takes some time
  // to compute it.
  static double tsc_ticks_per_second = 0;
  if (tsc_ticks_per_second != 0)
    return tsc_ticks_per_second;

  // Increase the thread priority to reduces the chances of having a context
  // switch during a reading of the TSC and the performance counter.
  int previous_priority = ::GetThreadPriority(::GetCurrentThread());
  ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

  // The first time that this function is called, make an initial reading of the
  // TSC and the performance counter.
  static const uint64_t tsc_initial = __rdtsc();
  static const uint64_t perf_counter_initial = QPCNowRaw();

  // Make a another reading of the TSC and the performance counter every time
  // that this function is called.
  uint64_t tsc_now = __rdtsc();
  uint64_t perf_counter_now = QPCNowRaw();

  // Reset the thread priority.
  ::SetThreadPriority(::GetCurrentThread(), previous_priority);

  // Make sure that at least 50 ms elapsed between the 2 readings. The first
  // time that this function is called, we don't expect this to be the case.
  // Note: The longer the elapsed time between the 2 readings is, the more
  //   accurate the computed TSC frequency will be. The 50 ms value was
  //   chosen because local benchmarks show that it allows us to get a
  //   stddev of less than 1 tick/us between multiple runs.
  // Note: According to the MSDN documentation for QueryPerformanceFrequency(),
  //   this will never fail on systems that run XP or later.
  //   https://msdn.microsoft.com/library/windows/desktop/ms644905.aspx
  LARGE_INTEGER perf_counter_frequency = {};
  ::QueryPerformanceFrequency(&perf_counter_frequency);
  DCHECK_GE(perf_counter_now, perf_counter_initial);
  uint64_t perf_counter_ticks = perf_counter_now - perf_counter_initial;
  double elapsed_time_seconds =
      perf_counter_ticks / static_cast<double>(perf_counter_frequency.QuadPart);

  const double kMinimumEvaluationPeriodSeconds = 0.05;
  if (elapsed_time_seconds < kMinimumEvaluationPeriodSeconds)
    return 0;

  // Compute the frequency of the TSC.
  DCHECK_GE(tsc_now, tsc_initial);
  uint64_t tsc_ticks = tsc_now - tsc_initial;
  tsc_ticks_per_second = tsc_ticks / elapsed_time_seconds;

  return tsc_ticks_per_second;
}
#endif  // !defined(V8_HOST_ARCH_ARM64)
#endif  // V8_OS_WIN

}  // namespace base
}  // namespace v8
