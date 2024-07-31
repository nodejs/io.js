// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for Win32.

// Secure API functions are not available using MinGW with msvcrt.dll
// on Windows XP. Make sure MINGW_HAS_SECURE_API is not defined to
// disable definition of secure API functions in standard headers that
// would conflict with our own implementation.
#ifdef __MINGW32__
#include <_mingw.h>
#ifdef MINGW_HAS_SECURE_API
#undef MINGW_HAS_SECURE_API
#endif  // MINGW_HAS_SECURE_API
#endif  // __MINGW32__

#include <windows.h>

// This has to come after windows.h.
#include <VersionHelpers.h>
#include <dbghelp.h>  // For SymLoadModule64 and al.
#include <malloc.h>   // For _msize()
#include <mmsystem.h>  // For timeGetTime().
#include <psapi.h>     // For GetProcessmMemoryInfo().
#include <tlhelp32.h>  // For Module32First and al.

#include <limits>

#include "src/base/bits.h"
#include "src/base/lazy-instance.h"
#include "src/base/macros.h"
#include "src/base/platform/platform-win32.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/timezone-cache.h"
#include "src/base/utils/random-number-generator.h"
#include "src/base/win32-headers.h"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif               // defined(_MSC_VER)

// Check that type sizes and alignments match.
static_assert(sizeof(V8_CONDITION_VARIABLE) == sizeof(CONDITION_VARIABLE));
static_assert(alignof(V8_CONDITION_VARIABLE) == alignof(CONDITION_VARIABLE));
static_assert(sizeof(V8_SRWLOCK) == sizeof(SRWLOCK));
static_assert(alignof(V8_SRWLOCK) == alignof(SRWLOCK));
static_assert(sizeof(V8_CRITICAL_SECTION) == sizeof(CRITICAL_SECTION));
static_assert(alignof(V8_CRITICAL_SECTION) == alignof(CRITICAL_SECTION));

// Check that CRITICAL_SECTION offsets match.
static_assert(offsetof(V8_CRITICAL_SECTION, DebugInfo) ==
              offsetof(CRITICAL_SECTION, DebugInfo));
static_assert(offsetof(V8_CRITICAL_SECTION, LockCount) ==
              offsetof(CRITICAL_SECTION, LockCount));
static_assert(offsetof(V8_CRITICAL_SECTION, RecursionCount) ==
              offsetof(CRITICAL_SECTION, RecursionCount));
static_assert(offsetof(V8_CRITICAL_SECTION, OwningThread) ==
              offsetof(CRITICAL_SECTION, OwningThread));
static_assert(offsetof(V8_CRITICAL_SECTION, LockSemaphore) ==
              offsetof(CRITICAL_SECTION, LockSemaphore));
static_assert(offsetof(V8_CRITICAL_SECTION, SpinCount) ==
              offsetof(CRITICAL_SECTION, SpinCount));

// Extra functions for MinGW. Most of these are the _s functions which are in
// the Microsoft Visual Studio C++ CRT.
#ifdef __MINGW32__


#ifndef __MINGW64_VERSION_MAJOR

#define _TRUNCATE 0
#define STRUNCATE 80

inline void MemoryFence() {
  int barrier = 0;
  __asm__ __volatile__("xchgl %%eax,%0 ":"=r" (barrier));
}

#endif  // __MINGW64_VERSION_MAJOR


int localtime_s(tm* out_tm, const time_t* time) {
  tm* posix_local_time_struct = localtime_r(time, out_tm);
  if (posix_local_time_struct == nullptr) return 1;
  return 0;
}


int fopen_s(FILE** pFile, const char* filename, const char* mode) {
  *pFile = fopen(filename, mode);
  return *pFile != nullptr ? 0 : 1;
}

int _wfopen_s(FILE** pFile, const wchar_t* filename, const wchar_t* mode) {
  *pFile = _wfopen(filename, mode);
  return *pFile != nullptr ? 0 : 1;
}

int _vsnprintf_s(char* buffer, size_t sizeOfBuffer, size_t count,
                 const char* format, va_list argptr) {
  DCHECK(count == _TRUNCATE);
  return _vsnprintf(buffer, sizeOfBuffer, format, argptr);
}


int strncpy_s(char* dest, size_t dest_size, const char* source, size_t count) {
  CHECK(source != nullptr);
  CHECK(dest != nullptr);
  CHECK_GT(dest_size, 0);

  if (count == _TRUNCATE) {
    while (dest_size > 0 && *source != 0) {
      *(dest++) = *(source++);
      --dest_size;
    }
    if (dest_size == 0) {
      *(dest - 1) = 0;
      return STRUNCATE;
    }
  } else {
    while (dest_size > 0 && count > 0 && *source != 0) {
      *(dest++) = *(source++);
      --dest_size;
      --count;
    }
  }
  CHECK_GT(dest_size, 0);
  *dest = 0;
  return 0;
}

#endif  // __MINGW32__

namespace v8 {
namespace base {

namespace {

}  // namespace

class WindowsTimezoneCache : public TimezoneCache {
 public:
  WindowsTimezoneCache() : initialized_(false) {}

  ~WindowsTimezoneCache() override {}

  void Clear(TimeZoneDetection) override { initialized_ = false; }

  const char* LocalTimezone(double time) override;

  double LocalTimeOffset(double time, bool is_utc) override;

  double DaylightSavingsOffset(double time) override;

  // Initialize timezone information. The timezone information is obtained from
  // windows. If we cannot get the timezone information we fall back to CET.
  void InitializeIfNeeded() {
    // Just return if timezone information has already been initialized.
    if (initialized_) return;

    // Initialize POSIX time zone data.
    _tzset();
    // Obtain timezone information from operating system.
    memset(&tzinfo_, 0, sizeof(tzinfo_));
    if (GetTimeZoneInformation(&tzinfo_) == TIME_ZONE_ID_INVALID) {
      // If we cannot get timezone information we fall back to CET.
      tzinfo_.Bias = -60;
      tzinfo_.StandardDate.wMonth = 10;
      tzinfo_.StandardDate.wDay = 5;
      tzinfo_.StandardDate.wHour = 3;
      tzinfo_.StandardBias = 0;
      tzinfo_.DaylightDate.wMonth = 3;
      tzinfo_.DaylightDate.wDay = 5;
      tzinfo_.DaylightDate.wHour = 2;
      tzinfo_.DaylightBias = -60;
    }

    // Make standard and DST timezone names.
    WideCharToMultiByte(CP_UTF8, 0, tzinfo_.StandardName, -1, std_tz_name_,
                        kTzNameSize, nullptr, nullptr);
    std_tz_name_[kTzNameSize - 1] = '\0';
    WideCharToMultiByte(CP_UTF8, 0, tzinfo_.DaylightName, -1, dst_tz_name_,
                        kTzNameSize, nullptr, nullptr);
    dst_tz_name_[kTzNameSize - 1] = '\0';

    // If OS returned empty string or resource id (like "@tzres.dll,-211")
    // simply guess the name from the UTC bias of the timezone.
    // To properly resolve the resource identifier requires a library load,
    // which is not possible in a sandbox.
    if (std_tz_name_[0] == '\0' || std_tz_name_[0] == '@') {
      OS::SNPrintF(std_tz_name_, kTzNameSize - 1,
                   "%s Standard Time",
                   GuessTimezoneNameFromBias(tzinfo_.Bias));
    }
    if (dst_tz_name_[0] == '\0' || dst_tz_name_[0] == '@') {
      OS::SNPrintF(dst_tz_name_, kTzNameSize - 1,
                   "%s Daylight Time",
                   GuessTimezoneNameFromBias(tzinfo_.Bias));
    }
    // Timezone information initialized.
    initialized_ = true;
  }

  // Guess the name of the timezone from the bias.
  // The guess is very biased towards the northern hemisphere.
  const char* GuessTimezoneNameFromBias(int bias) {
    static const int kHour = 60;
    switch (-bias) {
      case -9*kHour: return "Alaska";
      case -8*kHour: return "Pacific";
      case -7*kHour: return "Mountain";
      case -6*kHour: return "Central";
      case -5*kHour: return "Eastern";
      case -4*kHour: return "Atlantic";
      case  0*kHour: return "GMT";
      case +1*kHour: return "Central Europe";
      case +2*kHour: return "Eastern Europe";
      case +3*kHour: return "Russia";
      case +5*kHour + 30: return "India";
      case +8*kHour: return "China";
      case +9*kHour: return "Japan";
      case +12*kHour: return "New Zealand";
      default: return "Local";
    }
  }


 private:
  static const int kTzNameSize = 128;
  bool initialized_;
  char std_tz_name_[kTzNameSize];
  char dst_tz_name_[kTzNameSize];
  TIME_ZONE_INFORMATION tzinfo_;
  friend class Win32Time;
};


// ----------------------------------------------------------------------------
// The Time class represents time on win32. A timestamp is represented as
// a 64-bit integer in 100 nanoseconds since January 1, 1601 (UTC). JavaScript
// timestamps are represented as a doubles in milliseconds since 00:00:00 UTC,
// January 1, 1970.

class Win32Time {
 public:
  // Constructors.
  Win32Time();
  explicit Win32Time(double jstime);
  Win32Time(int year, int mon, int day, int hour, int min, int sec);

  // Convert timestamp to JavaScript representation.
  double ToJSTime();

  // Set timestamp to current time.
  void SetToCurrentTime();

  // Returns the local timezone offset in milliseconds east of UTC. This is
  // the number of milliseconds you must add to UTC to get local time, i.e.
  // LocalOffset(CET) = 3600000 and LocalOffset(PST) = -28800000. This
  // routine also takes into account whether daylight saving is effect
  // at the time.
  int64_t LocalOffset(WindowsTimezoneCache* cache);

  // Returns the daylight savings time offset for the time in milliseconds.
  int64_t DaylightSavingsOffset(WindowsTimezoneCache* cache);

  // Returns a string identifying the current timezone for the
  // timestamp taking into account daylight saving.
  char* LocalTimezone(WindowsTimezoneCache* cache);

 private:
  // Constants for time conversion.
  static const int64_t kTimeEpoc = 116444736000000000LL;
  static const int64_t kTimeScaler = 10000;
  static const int64_t kMsPerMinute = 60000;

  // Constants for timezone information.
  static const bool kShortTzNames = false;

  // Return whether or not daylight savings time is in effect at this time.
  bool InDST(WindowsTimezoneCache* cache);

  // Accessor for FILETIME representation.
  FILETIME& ft() { return time_.ft_; }

  // Accessor for integer representation.
  int64_t& t() { return time_.t_; }

  // Although win32 uses 64-bit integers for representing timestamps,
  // these are packed into a FILETIME structure. The FILETIME structure
  // is just a struct representing a 64-bit integer. The TimeStamp union
  // allows access to both a FILETIME and an integer representation of
  // the timestamp.
  union TimeStamp {
    FILETIME ft_;
    int64_t t_;
  };

  TimeStamp time_;
};


// Initialize timestamp to start of epoc.
Win32Time::Win32Time() {
  t() = 0;
}


// Initialize timestamp from a JavaScript timestamp.
Win32Time::Win32Time(double jstime) {
  t() = static_cast<int64_t>(jstime) * kTimeScaler + kTimeEpoc;
}


// Initialize timestamp from date/time components.
Win32Time::Win32Time(int year, int mon, int day, int hour, int min, int sec) {
  SYSTEMTIME st;
  st.wYear = year;
  st.wMonth = mon;
  st.wDay = day;
  st.wHour = hour;
  st.wMinute = min;
  st.wSecond = sec;
  st.wMilliseconds = 0;
  SystemTimeToFileTime(&st, &ft());
}


// Convert timestamp to JavaScript timestamp.
double Win32Time::ToJSTime() {
  return static_cast<double>((t() - kTimeEpoc) / kTimeScaler);
}


// Set timestamp to current time.
void Win32Time::SetToCurrentTime() {
  // The default GetSystemTimeAsFileTime has a ~15.5ms resolution.
  // Because we're fast, we like fast timers which have at least a
  // 1ms resolution.
  //
  // timeGetTime() provides 1ms granularity when combined with
  // timeBeginPeriod().  If the host application for v8 wants fast
  // timers, it can use timeBeginPeriod to increase the resolution.
  //
  // Using timeGetTime() has a drawback because it is a 32bit value
  // and hence rolls-over every ~49days.
  //
  // To use the clock, we use GetSystemTimeAsFileTime as our base;
  // and then use timeGetTime to extrapolate current time from the
  // start time.  To deal with rollovers, we resync the clock
  // any time when more than kMaxClockElapsedTime has passed or
  // whenever timeGetTime creates a rollover.

  static bool initialized = false;
  static TimeStamp init_time;
  static DWORD init_ticks;
  static const int64_t kHundredNanosecondsPerSecond = 10000000;
  static const int64_t kMaxClockElapsedTime =
      60*kHundredNanosecondsPerSecond;  // 1 minute

  // If we are uninitialized, we need to resync the clock.
  bool needs_resync = !initialized;

  // Get the current time.
  TimeStamp time_now;
  GetSystemTimeAsFileTime(&time_now.ft_);
  DWORD ticks_now = timeGetTime();

  // Check if we need to resync due to clock rollover.
  needs_resync |= ticks_now < init_ticks;

  // Check if we need to resync due to elapsed time.
  needs_resync |= (time_now.t_ - init_time.t_) > kMaxClockElapsedTime;

  // Check if we need to resync due to backwards time change.
  needs_resync |= time_now.t_ < init_time.t_;

  // Resync the clock if necessary.
  if (needs_resync) {
    GetSystemTimeAsFileTime(&init_time.ft_);
    init_ticks = ticks_now = timeGetTime();
    initialized = true;
  }

  // Finally, compute the actual time.  Why is this so hard.
  DWORD elapsed = ticks_now - init_ticks;
  this->time_.t_ = init_time.t_ + (static_cast<int64_t>(elapsed) * 10000);
}


// Return the local timezone offset in milliseconds east of UTC. This
// takes into account whether daylight saving is in effect at the time.
// Only times in the 32-bit Unix range may be passed to this function.
// Also, adding the time-zone offset to the input must not overflow.
// The function EquivalentTime() in date.js guarantees this.
int64_t Win32Time::LocalOffset(WindowsTimezoneCache* cache) {
  cache->InitializeIfNeeded();

  Win32Time rounded_to_second(*this);
  rounded_to_second.t() =
      rounded_to_second.t() / 1000 / kTimeScaler * 1000 * kTimeScaler;
  // Convert to local time using POSIX localtime function.
  // Windows XP Service Pack 3 made SystemTimeToTzSpecificLocalTime()
  // very slow.  Other browsers use localtime().

  // Convert from JavaScript milliseconds past 1/1/1970 0:00:00 to
  // POSIX seconds past 1/1/1970 0:00:00.
  double unchecked_posix_time = rounded_to_second.ToJSTime() / 1000;
  if (unchecked_posix_time > INT_MAX || unchecked_posix_time < 0) {
    return 0;
  }
  // Because _USE_32BIT_TIME_T is defined, time_t is a 32-bit int.
  time_t posix_time = static_cast<time_t>(unchecked_posix_time);

  // Convert to local time, as struct with fields for day, hour, year, etc.
  tm posix_local_time_struct;
  if (localtime_s(&posix_local_time_struct, &posix_time)) return 0;

  if (posix_local_time_struct.tm_isdst > 0) {
    return (cache->tzinfo_.Bias + cache->tzinfo_.DaylightBias) * -kMsPerMinute;
  } else if (posix_local_time_struct.tm_isdst == 0) {
    return (cache->tzinfo_.Bias + cache->tzinfo_.StandardBias) * -kMsPerMinute;
  } else {
    return cache->tzinfo_.Bias * -kMsPerMinute;
  }
}


// Return whether or not daylight savings time is in effect at this time.
bool Win32Time::InDST(WindowsTimezoneCache* cache) {
  cache->InitializeIfNeeded();

  // Determine if DST is in effect at the specified time.
  bool in_dst = false;
  if (cache->tzinfo_.StandardDate.wMonth != 0 ||
      cache->tzinfo_.DaylightDate.wMonth != 0) {
    // Get the local timezone offset for the timestamp in milliseconds.
    int64_t offset = LocalOffset(cache);

    // Compute the offset for DST. The bias parameters in the timezone info
    // are specified in minutes. These must be converted to milliseconds.
    int64_t dstofs =
        -(cache->tzinfo_.Bias + cache->tzinfo_.DaylightBias) * kMsPerMinute;

    // If the local time offset equals the timezone bias plus the daylight
    // bias then DST is in effect.
    in_dst = offset == dstofs;
  }

  return in_dst;
}


// Return the daylight savings time offset for this time.
int64_t Win32Time::DaylightSavingsOffset(WindowsTimezoneCache* cache) {
  return InDST(cache) ? 60 * kMsPerMinute : 0;
}


// Returns a string identifying the current timezone for the
// timestamp taking into account daylight saving.
char* Win32Time::LocalTimezone(WindowsTimezoneCache* cache) {
  // Return the standard or DST time zone name based on whether daylight
  // saving is in effect at the given time.
  return InDST(cache) ? cache->dst_tz_name_ : cache->std_tz_name_;
}


// Returns the accumulated user time for thread.
int OS::GetUserTime(uint32_t* secs,  uint32_t* usecs) {
  FILETIME dummy;
  uint64_t usertime;

  // Get the amount of time that the thread has executed in user mode.
  if (!GetThreadTimes(GetCurrentThread(), &dummy, &dummy, &dummy,
                      reinterpret_cast<FILETIME*>(&usertime))) return -1;

  // Adjust the resolution to micro-seconds.
  usertime /= 10;

  // Convert to seconds and microseconds
  *secs = static_cast<uint32_t>(usertime / 1000000);
  *usecs = static_cast<uint32_t>(usertime % 1000000);
  return 0;
}

int OS::GetPeakMemoryUsageKb() {
  constexpr int KB = 1024;

  PROCESS_MEMORY_COUNTERS mem_counters;
  int ret;

  ret = GetProcessMemoryInfo(GetCurrentProcess(), &mem_counters,
                             sizeof(mem_counters));
  if (ret == 0) return -1;

  return static_cast<int>(mem_counters.PeakWorkingSetSize / KB);
}

// Returns current time as the number of milliseconds since
// 00:00:00 UTC, January 1, 1970.
double OS::TimeCurrentMillis() {
  return Time::Now().ToJsTime();
}

// Returns a string identifying the current timezone taking into
// account daylight saving.
const char* WindowsTimezoneCache::LocalTimezone(double time) {
  return Win32Time(time).LocalTimezone(this);
}

// Returns the local time offset in milliseconds east of UTC without
// taking daylight savings time into account.
double WindowsTimezoneCache::LocalTimeOffset(double time_ms, bool is_utc) {
  // Ignore is_utc and time_ms for now. That way, the behavior wouldn't
  // change with icu_timezone_data disabled.
  // Use current time, rounded to the millisecond.
  Win32Time t(OS::TimeCurrentMillis());
  // Time::LocalOffset inlcudes any daylight savings offset, so subtract it.
  return static_cast<double>(t.LocalOffset(this) -
                             t.DaylightSavingsOffset(this));
}

// Returns the daylight savings offset in milliseconds for the given
// time.
double WindowsTimezoneCache::DaylightSavingsOffset(double time) {
  int64_t offset = Win32Time(time).DaylightSavingsOffset(this);
  return static_cast<double>(offset);
}

TimezoneCache* OS::CreateTimezoneCache() { return new WindowsTimezoneCache(); }

int OS::GetLastError() {
  return ::GetLastError();
}


int OS::GetCurrentProcessId() {
  return static_cast<int>(::GetCurrentProcessId());
}


int OS::GetCurrentThreadId() {
  return static_cast<int>(::GetCurrentThreadId());
}

void OS::ExitProcess(int exit_code) {
  // Use TerminateProcess to avoid races between isolate threads and
  // static destructors.
  fflush(stdout);
  fflush(stderr);
  TerminateProcess(GetCurrentProcess(), exit_code);
  // Termination the current process does not return. {TerminateProcess} is not
  // marked [[noreturn]] though, since it can also be used to terminate another
  // process.
  UNREACHABLE();
}

// ----------------------------------------------------------------------------
// Win32 console output.
//
// If a Win32 application is linked as a console application it has a normal
// standard output and standard error. In this case normal printf works fine
// for output. However, if the application is linked as a GUI application,
// the process doesn't have a console, and therefore (debugging) output is lost.
// This is the case if we are embedded in a windows program (like a browser).
// In order to be able to get debug output in this case the the debugging
// facility using OutputDebugString. This output goes to the active debugger
// for the process (if any). Else the output can be monitored using DBMON.EXE.

enum OutputMode {
  UNKNOWN,  // Output method has not yet been determined.
  CONSOLE,  // Output is written to stdout.
  ODS       // Output is written to debug facility.
};

static OutputMode output_mode = UNKNOWN;  // Current output mode.


// Determine if the process has a console for output.
static bool HasConsole() {
  // Only check the first time. Eventual race conditions are not a problem,
  // because all threads will eventually determine the same mode.
  if (output_mode == UNKNOWN) {
    // We cannot just check that the standard output is attached to a console
    // because this would fail if output is redirected to a file. Therefore we
    // say that a process does not have an output console if either the
    // standard output handle is invalid or its file type is unknown.
    if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE &&
        GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN)
      output_mode = CONSOLE;
    else
      output_mode = ODS;
  }
  return output_mode == CONSOLE;
}


static void VPrintHelper(FILE* stream, const char* format, va_list args) {
  if ((stream == stdout || stream == stderr) && !HasConsole()) {
    // It is important to use safe print here in order to avoid
    // overflowing the buffer. We might truncate the output, but this
    // does not crash.
    char buffer[4096];
    OS::VSNPrintF(buffer, sizeof(buffer), format, args);
    OutputDebugStringA(buffer);
  } else {
    vfprintf(stream, format, args);
  }
}

// Convert utf-8 encoded string to utf-16 encoded.
static std::wstring ConvertUtf8StringToUtf16(const char* str) {
  // On Windows wchar_t must be a 16-bit value.
  static_assert(sizeof(wchar_t) == 2, "wrong wchar_t size");
  std::wstring utf16_str;
  int name_length = static_cast<int>(strlen(str));
  int len = MultiByteToWideChar(CP_UTF8, 0, str, name_length, nullptr, 0);
  if (len > 0) {
    utf16_str.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, str, name_length, &utf16_str[0], len);
  }
  return utf16_str;
}

FILE* OS::FOpen(const char* path, const char* mode) {
  FILE* result;
  std::wstring utf16_path = ConvertUtf8StringToUtf16(path);
  std::wstring utf16_mode = ConvertUtf8StringToUtf16(mode);
  if (_wfopen_s(&result, utf16_path.c_str(), utf16_mode.c_str()) == 0) {
    return result;
  } else {
    return nullptr;
  }
}


bool OS::Remove(const char* path) {
  return (DeleteFileA(path) != 0);
}

char OS::DirectorySeparator() { return '\\'; }

bool OS::isDirectorySeparator(const char ch) {
  return ch == '/' || ch == '\\';
}


FILE* OS::OpenTemporaryFile() {
  // tmpfile_s tries to use the root dir, don't use it.
  char tempPathBuffer[MAX_PATH];
  DWORD path_result = 0;
  path_result = GetTempPathA(MAX_PATH, tempPathBuffer);
  if (path_result > MAX_PATH || path_result == 0) return nullptr;
  UINT name_result = 0;
  char tempNameBuffer[MAX_PATH];
  name_result = GetTempFileNameA(tempPathBuffer, "", 0, tempNameBuffer);
  if (name_result == 0) return nullptr;
  FILE* result = FOpen(tempNameBuffer, "w+");  // Same mode as tmpfile uses.
  if (result != nullptr) {
    Remove(tempNameBuffer);  // Delete on close.
  }
  return result;
}


// Open log file in binary mode to avoid /n -> /r/n conversion.
const char* const OS::LogFileOpenMode = "wb+";

// Print (debug) message to console.
void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrint(format, args);
  va_end(args);
}


void OS::VPrint(const char* format, va_list args) {
  VPrintHelper(stdout, format, args);
}


void OS::FPrint(FILE* out, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(out, format, args);
  va_end(args);
}


void OS::VFPrint(FILE* out, const char* format, va_list args) {
  VPrintHelper(out, format, args);
}


// Print error message to console.
void OS::PrintError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
  fflush(stderr);
}


void OS::VPrintError(const char* format, va_list args) {
  VPrintHelper(stderr, format, args);
}


int OS::SNPrintF(char* str, int length, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = VSNPrintF(str, length, format, args);
  va_end(args);
  return result;
}


int OS::VSNPrintF(char* str, int length, const char* format, va_list args) {
  int n = _vsnprintf_s(str, length, _TRUNCATE, format, args);
  // Make sure to zero-terminate the string if the output was
  // truncated or if there was an error.
  if (n < 0 || n >= length) {
    if (length > 0)
      str[length - 1] = '\0';
    return -1;
  } else {
    return n;
  }
}


void OS::StrNCpy(char* dest, int length, const char* src, size_t n) {
  // Use _TRUNCATE or strncpy_s crashes (by design) if buffer is too small.
  size_t buffer_size = static_cast<size_t>(length);
  if (n + 1 > buffer_size)  // count for trailing '\0'
    n = _TRUNCATE;
  int result = strncpy_s(dest, length, src, n);
  USE(result);
  DCHECK(result == 0 || (n == _TRUNCATE && result == STRUNCATE));
}


#undef _TRUNCATE
#undef STRUNCATE

DEFINE_LAZY_LEAKY_OBJECT_GETTER(RandomNumberGenerator,
                                GetPlatformRandomNumberGenerator)
static LazyMutex rng_mutex = LAZY_MUTEX_INITIALIZER;

void OS::Initialize(AbortMode abort_mode, const char* const gc_fake_mmap) {
  g_abort_mode = abort_mode;
}

typedef PVOID(__stdcall* VirtualAlloc2_t)(HANDLE, PVOID, SIZE_T, ULONG, ULONG,
                                          MEM_EXTENDED_PARAMETER*, ULONG);
VirtualAlloc2_t VirtualAlloc2 = nullptr;

typedef PVOID(__stdcall* MapViewOfFile3_t)(HANDLE, HANDLE, PVOID, ULONG64,
                                           SIZE_T, ULONG, ULONG,
                                           MEM_EXTENDED_PARAMETER*, ULONG);
MapViewOfFile3_t MapViewOfFile3 = nullptr;

typedef PVOID(__stdcall* UnmapViewOfFile2_t)(HANDLE, PVOID, ULONG);
UnmapViewOfFile2_t UnmapViewOfFile2 = nullptr;

void OS::EnsureWin32MemoryAPILoaded() {
  static bool loaded = false;
  if (!loaded) {
    VirtualAlloc2 = (VirtualAlloc2_t)GetProcAddress(
        GetModuleHandle(L"kernelbase.dll"), "VirtualAlloc2");

    MapViewOfFile3 = (MapViewOfFile3_t)GetProcAddress(
        GetModuleHandle(L"kernelbase.dll"), "MapViewOfFile3");

    UnmapViewOfFile2 = (UnmapViewOfFile2_t)GetProcAddress(
        GetModuleHandle(L"kernelbase.dll"), "UnmapViewOfFile2");

    loaded = true;
  }
}

// static
size_t OS::AllocatePageSize() {
  static size_t allocate_alignment = 0;
  if (allocate_alignment == 0) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    allocate_alignment = info.dwAllocationGranularity;
  }
  return allocate_alignment;
}

// static
size_t OS::CommitPageSize() {
  static size_t page_size = 0;
  if (page_size == 0) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    page_size = info.dwPageSize;
    DCHECK_EQ(4096, page_size);
  }
  return page_size;
}

// static
void OS::SetRandomMmapSeed(int64_t seed) {
  if (seed) {
    MutexGuard guard(rng_mutex.Pointer());
    GetPlatformRandomNumberGenerator()->SetSeed(seed);
  }
}

// static
void* OS::GetRandomMmapAddr() {
// The address range used to randomize RWX allocations in OS::Allocate
// Try not to map pages into the default range that windows loads DLLs
// Use a multiple of 64k to prevent committing unused memory.
// Note: This does not guarantee RWX regions will be within the
// range kAllocationRandomAddressMin to kAllocationRandomAddressMax
#ifdef V8_HOST_ARCH_64_BIT
  static const uintptr_t kAllocationRandomAddressMin = 0x0000000080000000;
  static const uintptr_t kAllocationRandomAddressMax = 0x000003FFFFFF0000;
#else
  static const uintptr_t kAllocationRandomAddressMin = 0x04000000;
  static const uintptr_t kAllocationRandomAddressMax = 0x3FFF0000;
#endif
  uintptr_t address;
  {
    MutexGuard guard(rng_mutex.Pointer());
    GetPlatformRandomNumberGenerator()->NextBytes(&address, sizeof(address));
  }
  address <<= kPageSizeBits;
  address += kAllocationRandomAddressMin;
  address &= kAllocationRandomAddressMax;
  return reinterpret_cast<void*>(address);
}

namespace {

DWORD GetProtectionFromMemoryPermission(OS::MemoryPermission access) {
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
    case OS::MemoryPermission::kNoAccessWillJitLater:
      return PAGE_NOACCESS;
    case OS::MemoryPermission::kRead:
      return PAGE_READONLY;
    case OS::MemoryPermission::kReadWrite:
      return PAGE_READWRITE;
    case OS::MemoryPermission::kReadWriteExecute:
      if (IsWindows10OrGreater())
        return PAGE_EXECUTE_READWRITE | PAGE_TARGETS_INVALID;
      return PAGE_EXECUTE_READWRITE;
    case OS::MemoryPermission::kReadExecute:
      if (IsWindows10OrGreater())
        return PAGE_EXECUTE_READ | PAGE_TARGETS_INVALID;
      return PAGE_EXECUTE_READ;
  }
  UNREACHABLE();
}

// Desired access parameter for MapViewOfFile
DWORD GetFileViewAccessFromMemoryPermission(OS::MemoryPermission access) {
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
    case OS::MemoryPermission::kNoAccessWillJitLater:
    case OS::MemoryPermission::kRead:
      return FILE_MAP_READ;
    case OS::MemoryPermission::kReadWrite:
      return FILE_MAP_READ | FILE_MAP_WRITE;
    default:
      // Execute access is not supported
      break;
  }
  UNREACHABLE();
}

void* VirtualAllocWrapper(void* address, size_t size, DWORD flags,
                          DWORD protect) {
  if (VirtualAlloc2) {
    return VirtualAlloc2(GetCurrentProcess(), address, size, flags, protect,
                         NULL, 0);
  } else {
    return VirtualAlloc(address, size, flags, protect);
  }
}

uint8_t* VirtualAllocWithHint(size_t size, DWORD flags, DWORD protect,
                              void* hint) {
  LPVOID base = VirtualAllocWrapper(hint, size, flags, protect);

  // On failure, let the OS find an address to use.
  if (hint && base == nullptr) {
    base = VirtualAllocWrapper(nullptr, size, flags, protect);
  }

  return reinterpret_cast<uint8_t*>(base);
}

void* AllocateInternal(void* hint, size_t size, size_t alignment,
                       size_t page_size, DWORD flags, DWORD protect) {
  // First, try an exact size aligned allocation.
  uint8_t* base = VirtualAllocWithHint(size, flags, protect, hint);
  if (base == nullptr) return nullptr;  // Can't allocate, we're OOM.

  // If address is suitably aligned, we're done.
  uint8_t* aligned_base = reinterpret_cast<uint8_t*>(
      RoundUp(reinterpret_cast<uintptr_t>(base), alignment));
  if (base == aligned_base) return reinterpret_cast<void*>(base);

  // Otherwise, free it and try a larger allocation.
  CHECK(VirtualFree(base, 0, MEM_RELEASE));

  // Clear the hint. It's unlikely we can allocate at this address.
  hint = nullptr;

  // Add the maximum misalignment so we are guaranteed an aligned base address
  // in the allocated region.
  size_t padded_size = size + (alignment - page_size);
  const int kMaxAttempts = 3;
  aligned_base = nullptr;
  for (int i = 0; i < kMaxAttempts; ++i) {
    base = VirtualAllocWithHint(padded_size, flags, protect, hint);
    if (base == nullptr) return nullptr;  // Can't allocate, we're OOM.

    // Try to trim the allocation by freeing the padded allocation and then
    // calling VirtualAlloc at the aligned base.
    CHECK(VirtualFree(base, 0, MEM_RELEASE));
    aligned_base = reinterpret_cast<uint8_t*>(
        RoundUp(reinterpret_cast<uintptr_t>(base), alignment));
    base = reinterpret_cast<uint8_t*>(
        VirtualAllocWrapper(aligned_base, size, flags, protect));
    // We might not get the reduced allocation due to a race. In that case,
    // base will be nullptr.
    if (base != nullptr) break;
  }
  DCHECK_IMPLIES(base, base == aligned_base);
  return reinterpret_cast<void*>(base);
}

void CheckIsOOMError(int error) {
  // We expect one of ERROR_NOT_ENOUGH_MEMORY or ERROR_COMMITMENT_LIMIT. We'd
  // still like to get the actual error code when its not one of the expected
  // errors, so use the construct below to achieve that.
  if (error != ERROR_NOT_ENOUGH_MEMORY) CHECK_EQ(ERROR_COMMITMENT_LIMIT, error);
}

}  // namespace

// static
void* OS::Allocate(void* hint, size_t size, size_t alignment,
                   MemoryPermission access) {
  size_t page_size = AllocatePageSize();
  DCHECK_EQ(0, size % page_size);
  DCHECK_EQ(0, alignment % page_size);
  DCHECK_LE(page_size, alignment);
  hint = AlignedAddress(hint, alignment);

  DWORD flags = (access == OS::MemoryPermission::kNoAccess)
                    ? MEM_RESERVE
                    : MEM_RESERVE | MEM_COMMIT;
  DWORD protect = GetProtectionFromMemoryPermission(access);

  return AllocateInternal(hint, size, alignment, page_size, flags, protect);
}

// static
void OS::Free(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % AllocatePageSize());
  DCHECK_EQ(0, size % AllocatePageSize());
  USE(size);
  CHECK_NE(0, VirtualFree(address, 0, MEM_RELEASE));
}

// static
void* OS::AllocateShared(void* hint, size_t size, MemoryPermission permission,
                         PlatformSharedMemoryHandle handle, uint64_t offset) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(hint) % AllocatePageSize());
  DCHECK_EQ(0, size % AllocatePageSize());
  DCHECK_EQ(0, offset % AllocatePageSize());

  DWORD off_hi = static_cast<DWORD>(offset >> 32);
  DWORD off_lo = static_cast<DWORD>(offset);
  DWORD access = GetFileViewAccessFromMemoryPermission(permission);

  HANDLE file_mapping = FileMappingFromSharedMemoryHandle(handle);
  void* result =
      MapViewOfFileEx(file_mapping, access, off_hi, off_lo, size, hint);

  if (!result) {
    // Retry without hint.
    result = MapViewOfFile(file_mapping, access, off_hi, off_lo, size);
  }

  return result;
}

// static
void OS::FreeShared(void* address, size_t size) {
  CHECK(UnmapViewOfFile(address));
}

// static
void OS::Release(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  CHECK_NE(0, VirtualFree(address, size, MEM_DECOMMIT));
}

// static
bool OS::SetPermissions(void* address, size_t size, MemoryPermission access) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  if (access == MemoryPermission::kNoAccess) {
    return VirtualFree(address, size, MEM_DECOMMIT) != 0;
  }
  DWORD protect = GetProtectionFromMemoryPermission(access);
  void* result = VirtualAllocWrapper(address, size, MEM_COMMIT, protect);

  // Any failure that's not OOM likely indicates a bug in the caller (e.g.
  // using an invalid mapping) so attempt to catch that here to facilitate
  // debugging of these failures.
  if (!result) CheckIsOOMError(GetLastError());

  return result != nullptr;
}

void OS::SetDataReadOnly(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());

  DWORD old_protection;
  CHECK(VirtualProtect(address, size, PAGE_READONLY, &old_protection));
  CHECK(old_protection == PAGE_READWRITE || old_protection == PAGE_WRITECOPY);
}

// static
bool OS::RecommitPages(void* address, size_t size, MemoryPermission access) {
  return SetPermissions(address, size, access);
}

// static
bool OS::DiscardSystemPages(void* address, size_t size) {
  // On Windows, discarded pages are not returned to the system immediately and
  // not guaranteed to be zeroed when returned to the application.
  using DiscardVirtualMemoryFunction =
      DWORD(WINAPI*)(PVOID virtualAddress, SIZE_T size);
  static std::atomic<DiscardVirtualMemoryFunction> discard_virtual_memory(
      reinterpret_cast<DiscardVirtualMemoryFunction>(-1));
  if (discard_virtual_memory ==
      reinterpret_cast<DiscardVirtualMemoryFunction>(-1))
    discard_virtual_memory =
        reinterpret_cast<DiscardVirtualMemoryFunction>(GetProcAddress(
            GetModuleHandle(L"Kernel32.dll"), "DiscardVirtualMemory"));
  // Use DiscardVirtualMemory when available because it releases faster than
  // MEM_RESET.
  DiscardVirtualMemoryFunction discard_function = discard_virtual_memory.load();
  if (discard_function) {
    DWORD ret = discard_function(address, size);
    if (!ret) return true;
  }
  // DiscardVirtualMemory is buggy in Win10 SP0, so fall back to MEM_RESET on
  // failure.
  void* ptr = VirtualAllocWrapper(address, size, MEM_RESET, PAGE_READWRITE);
  CHECK(ptr);
  return ptr;
}

// static
bool OS::DecommitPages(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  // https://docs.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree:
  // "If a page is decommitted but not released, its state changes to reserved.
  // Subsequently, you can call VirtualAlloc to commit it, or VirtualFree to
  // release it. Attempts to read from or write to a reserved page results in an
  // access violation exception."
  // https://docs.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc
  // for MEM_COMMIT: "The function also guarantees that when the caller later
  // initially accesses the memory, the contents will be zero."
  return VirtualFree(address, size, MEM_DECOMMIT) != 0;
}

// static
bool OS::CanReserveAddressSpace() {
  return VirtualAlloc2 != nullptr && MapViewOfFile3 != nullptr &&
         UnmapViewOfFile2 != nullptr;
}

// static
Optional<AddressSpaceReservation> OS::CreateAddressSpaceReservation(
    void* hint, size_t size, size_t alignment,
    MemoryPermission max_permission) {
  CHECK(CanReserveAddressSpace());

  size_t page_size = AllocatePageSize();
  DCHECK_EQ(0, size % page_size);
  DCHECK_EQ(0, alignment % page_size);
  DCHECK_LE(page_size, alignment);
  hint = AlignedAddress(hint, alignment);

  // On Windows, address space reservations are backed by placeholder mappings.
  void* reservation =
      AllocateInternal(hint, size, alignment, page_size,
                       MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS);
  if (!reservation) return {};

  return AddressSpaceReservation(reservation, size);
}

// static
void OS::FreeAddressSpaceReservation(AddressSpaceReservation reservation) {
  OS::Free(reservation.base(), reservation.size());
}

// static
PlatformSharedMemoryHandle OS::CreateSharedMemoryHandleForTesting(size_t size) {
  HANDLE handle = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr,
                                    PAGE_READWRITE, 0, size, nullptr);
  if (!handle) return kInvalidSharedMemoryHandle;
  return SharedMemoryHandleFromFileMapping(handle);
}

// static
void OS::DestroySharedMemoryHandle(PlatformSharedMemoryHandle handle) {
  DCHECK_NE(kInvalidSharedMemoryHandle, handle);
  HANDLE file_mapping = FileMappingFromSharedMemoryHandle(handle);
  CHECK(CloseHandle(file_mapping));
}

// static
bool OS::HasLazyCommits() {
  // TODO(alph): implement for the platform.
  return false;
}

void OS::Sleep(TimeDelta interval) {
  ::Sleep(static_cast<DWORD>(interval.InMilliseconds()));
}

PreciseSleepTimer::PreciseSleepTimer() : timer_(NULL) {}
PreciseSleepTimer::~PreciseSleepTimer() { Close(); }
PreciseSleepTimer::PreciseSleepTimer(PreciseSleepTimer&& other) V8_NOEXCEPT {
  Close();
  timer_ = other.timer_;
  other.timer_ = NULL;
}
PreciseSleepTimer& PreciseSleepTimer::operator=(PreciseSleepTimer&& other)
    V8_NOEXCEPT {
  Close();
  timer_ = other.timer_;
  other.timer_ = NULL;
  return *this;
}
bool PreciseSleepTimer::IsInitialized() const { return timer_ != NULL; }
void PreciseSleepTimer::Close() {
  if (timer_ != NULL) {
    CloseHandle(timer_);
    timer_ = NULL;
  }
}

void PreciseSleepTimer::TryInit() {
  Close();
  // This flag allows precise sleep times, but is only available since Windows
  // 10 version 1803.
  DWORD flags = CREATE_WAITABLE_TIMER_HIGH_RESOLUTION;
  // The TIMER_MODIFY_STATE permission allows setting the timer, and SYNCHRONIZE
  // allows waiting for it.
  DWORD desired_access = TIMER_MODIFY_STATE | SYNCHRONIZE;
  timer_ =
      CreateWaitableTimerExW(NULL,  // Cannot be inherited by child processes
                             NULL,  // Cannot be looked up by name
                             flags, desired_access);
}

void PreciseSleepTimer::Sleep(TimeDelta interval) const {
  // Time is specified in 100 nanosecond intervals. Negative values indicate
  // relative time.
  LARGE_INTEGER due_time;
  due_time.QuadPart = -interval.InMicroseconds() * 10;
  LONG period = 0;  // Not periodic; wake only once
  PTIMERAPCROUTINE completion_routine = NULL;
  LPVOID arg_to_completion_routine = NULL;
  BOOL resume = false;  // No need to wake system from sleep
  CHECK(SetWaitableTimer(timer_, &due_time, period, completion_routine,
                         arg_to_completion_routine, resume));

  DWORD timeout_interval = INFINITE;  // Return only when the object is signaled
  CHECK_EQ(WAIT_OBJECT_0, WaitForSingleObject(timer_, timeout_interval));
}

void OS::Abort() {
  // Give a chance to debug the failure.
  if (IsDebuggerPresent()) {
    DebugBreak();
  }

  // Before aborting, make sure to flush output buffers.
  fflush(stdout);
  fflush(stderr);

  switch (g_abort_mode) {
    case AbortMode::kSoft:
      _exit(-1);
    case AbortMode::kHard:
      IMMEDIATE_CRASH();
    case AbortMode::kDefault:
      break;
  }

  // Make the MSVCRT do a silent abort.
  raise(SIGABRT);

  // Make sure function doesn't return.
  abort();
}


void OS::DebugBreak() {
#if V8_CC_MSVC
  // To avoid Visual Studio runtime support the following code can be used
  // instead
  // __asm { int 3 }
  __debugbreak();
#else
  ::DebugBreak();
#endif
}


class Win32MemoryMappedFile final : public OS::MemoryMappedFile {
 public:
  Win32MemoryMappedFile(HANDLE file, HANDLE file_mapping, void* memory,
                        size_t size)
      : file_(file),
        file_mapping_(file_mapping),
        memory_(memory),
        size_(size) {}
  ~Win32MemoryMappedFile() final;
  void* memory() const final { return memory_; }
  size_t size() const final { return size_; }

 private:
  HANDLE const file_;
  HANDLE const file_mapping_;
  void* const memory_;
  size_t const size_;
};


// static
OS::MemoryMappedFile* OS::MemoryMappedFile::open(const char* name,
                                                 FileMode mode) {
  // Open a physical file.
  DWORD access = GENERIC_READ;
  if (mode == FileMode::kReadWrite) {
    access |= GENERIC_WRITE;
  }

  std::wstring utf16_name = ConvertUtf8StringToUtf16(name);
  HANDLE file = CreateFileW(utf16_name.c_str(), access,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return nullptr;

  DWORD size = GetFileSize(file, nullptr);
  if (size == 0) return new Win32MemoryMappedFile(file, nullptr, nullptr, 0);

  DWORD protection =
      (mode == FileMode::kReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
  // Create a file mapping for the physical file.
  HANDLE file_mapping =
      CreateFileMapping(file, nullptr, protection, 0, size, nullptr);
  if (file_mapping == nullptr) return nullptr;

  // Map a view of the file into memory.
  DWORD view_access =
      (mode == FileMode::kReadOnly) ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;
  void* memory = MapViewOfFile(file_mapping, view_access, 0, 0, size);
  return new Win32MemoryMappedFile(file, file_mapping, memory, size);
}

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name,
                                                   size_t size, void* initial) {
  std::wstring utf16_name = ConvertUtf8StringToUtf16(name);
  // Open a physical file.
  HANDLE file = CreateFileW(utf16_name.c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, 0, nullptr);
  if (file == nullptr) return nullptr;
  if (size == 0) return new Win32MemoryMappedFile(file, nullptr, nullptr, 0);
  // Create a file mapping for the physical file.
  HANDLE file_mapping = CreateFileMapping(file, nullptr, PAGE_READWRITE, 0,
                                          static_cast<DWORD>(size), nullptr);
  if (file_mapping == nullptr) return nullptr;
  // Map a view of the file into memory.
  void* memory = MapViewOfFile(file_mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (memory) memmove(memory, initial, size);
  return new Win32MemoryMappedFile(file, file_mapping, memory, size);
}


Win32MemoryMappedFile::~Win32MemoryMappedFile() {
  if (memory_) UnmapViewOfFile(memory_);
  if (file_mapping_) CloseHandle(file_mapping_);
  CloseHandle(file_);
}

Optional<AddressSpaceReservation> AddressSpaceReservation::CreateSubReservation(
    void* address, size_t size, OS::MemoryPermission max_permission) {
  // Nothing to do, the sub reservation must already have been split by now.
  DCHECK(Contains(address, size));
  DCHECK_EQ(0, size % OS::AllocatePageSize());
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % OS::AllocatePageSize());

  return AddressSpaceReservation(address, size);
}

bool AddressSpaceReservation::FreeSubReservation(
    AddressSpaceReservation reservation) {
  // Nothing to do.
  // Pages allocated inside the reservation must've already been freed.
  return true;
}

bool AddressSpaceReservation::SplitPlaceholder(void* address, size_t size) {
  DCHECK(Contains(address, size));
  return VirtualFree(address, size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
}

bool AddressSpaceReservation::MergePlaceholders(void* address, size_t size) {
  DCHECK(Contains(address, size));
  return VirtualFree(address, size, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS);
}

bool AddressSpaceReservation::Allocate(void* address, size_t size,
                                       OS::MemoryPermission access) {
  DCHECK(Contains(address, size));
  CHECK(VirtualAlloc2);
  DWORD flags = (access == OS::MemoryPermission::kNoAccess)
                    ? MEM_RESERVE | MEM_REPLACE_PLACEHOLDER
                    : MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER;
  DWORD protect = GetProtectionFromMemoryPermission(access);
  return VirtualAlloc2(GetCurrentProcess(), address, size, flags, protect,
                       nullptr, 0);
}

bool AddressSpaceReservation::Free(void* address, size_t size) {
  DCHECK(Contains(address, size));
  return VirtualFree(address, size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
}

bool AddressSpaceReservation::AllocateShared(void* address, size_t size,
                                             OS::MemoryPermission access,
                                             PlatformSharedMemoryHandle handle,
                                             uint64_t offset) {
  DCHECK(Contains(address, size));
  CHECK(MapViewOfFile3);

  DWORD protect = GetProtectionFromMemoryPermission(access);
  HANDLE file_mapping = FileMappingFromSharedMemoryHandle(handle);
  return MapViewOfFile3(file_mapping, GetCurrentProcess(), address, offset,
                        size, MEM_REPLACE_PLACEHOLDER, protect, nullptr, 0);
}

bool AddressSpaceReservation::FreeShared(void* address, size_t size) {
  DCHECK(Contains(address, size));
  CHECK(UnmapViewOfFile2);

  return UnmapViewOfFile2(GetCurrentProcess(), address,
                          MEM_PRESERVE_PLACEHOLDER);
}

bool AddressSpaceReservation::SetPermissions(void* address, size_t size,
                                             OS::MemoryPermission access) {
  DCHECK(Contains(address, size));
  return OS::SetPermissions(address, size, access);
}

bool AddressSpaceReservation::RecommitPages(void* address, size_t size,
                                            OS::MemoryPermission access) {
  DCHECK(Contains(address, size));
  return OS::RecommitPages(address, size, access);
}

bool AddressSpaceReservation::DiscardSystemPages(void* address, size_t size) {
  DCHECK(Contains(address, size));
  return OS::DiscardSystemPages(address, size);
}

bool AddressSpaceReservation::DecommitPages(void* address, size_t size) {
  DCHECK(Contains(address, size));
  return OS::DecommitPages(address, size);
}

// The following code loads functions defined in DbhHelp.h and TlHelp32.h
// dynamically. This is to avoid being depending on dbghelp.dll and
// tlhelp32.dll when running (the functions in tlhelp32.dll have been moved to
// kernel32.dll at some point so loading functions defines in TlHelp32.h
// dynamically might not be necessary any more - for some versions of Windows?).

// Function pointers to functions dynamically loaded from dbghelp.dll.
#define DBGHELP_FUNCTION_LIST(V)  \
  V(SymInitialize)                \
  V(SymGetOptions)                \
  V(SymSetOptions)                \
  V(SymGetSearchPath)             \
  V(SymLoadModule64)              \
  V(StackWalk64)                  \
  V(SymGetSymFromAddr64)          \
  V(SymGetLineFromAddr64)         \
  V(SymFunctionTableAccess64)     \
  V(SymGetModuleBase64)

// Function pointers to functions dynamically loaded from dbghelp.dll.
#define TLHELP32_FUNCTION_LIST(V)  \
  V(CreateToolhelp32Snapshot)      \
  V(Module32FirstW)                \
  V(Module32NextW)

// Define the decoration to use for the type and variable name used for
// dynamically loaded DLL function..
#define DLL_FUNC_TYPE(name) _##name##_
#define DLL_FUNC_VAR(name) _##name

// Define the type for each dynamically loaded DLL function. The function
// definitions are copied from DbgHelp.h and TlHelp32.h. The IN and VOID macros
// from the Windows include files are redefined here to have the function
// definitions to be as close to the ones in the original .h files as possible.
#ifndef IN
#define IN
#endif
#ifndef VOID
#define VOID void
#endif

// DbgHelp isn't supported on MinGW yet
#ifndef __MINGW32__
// DbgHelp.h functions.
using DLL_FUNC_TYPE(SymInitialize) = BOOL(__stdcall*)(IN HANDLE hProcess,
                                                      IN PSTR UserSearchPath,
                                                      IN BOOL fInvadeProcess);
using DLL_FUNC_TYPE(SymGetOptions) = DWORD(__stdcall*)(VOID);
using DLL_FUNC_TYPE(SymSetOptions) = DWORD(__stdcall*)(IN DWORD SymOptions);
using DLL_FUNC_TYPE(SymGetSearchPath) = BOOL(__stdcall*)(
    IN HANDLE hProcess, OUT PSTR SearchPath, IN DWORD SearchPathLength);
using DLL_FUNC_TYPE(SymLoadModule64) = DWORD64(__stdcall*)(
    IN HANDLE hProcess, IN HANDLE hFile, IN PSTR ImageName, IN PSTR ModuleName,
    IN DWORD64 BaseOfDll, IN DWORD SizeOfDll);
using DLL_FUNC_TYPE(StackWalk64) = BOOL(__stdcall*)(
    DWORD MachineType, HANDLE hProcess, HANDLE hThread,
    LPSTACKFRAME64 StackFrame, PVOID ContextRecord,
    PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
    PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
    PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
    PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);
using DLL_FUNC_TYPE(SymGetSymFromAddr64) = BOOL(__stdcall*)(
    IN HANDLE hProcess, IN DWORD64 qwAddr, OUT PDWORD64 pdwDisplacement,
    OUT PIMAGEHLP_SYMBOL64 Symbol);
using DLL_FUNC_TYPE(SymGetLineFromAddr64) =
    BOOL(__stdcall*)(IN HANDLE hProcess, IN DWORD64 qwAddr,
                     OUT PDWORD pdwDisplacement, OUT PIMAGEHLP_LINE64 Line64);
// DbgHelp.h typedefs. Implementation found in dbghelp.dll.
using DLL_FUNC_TYPE(SymFunctionTableAccess64) = PVOID(__stdcall*)(
    HANDLE hProcess,
    DWORD64 AddrBase);  // DbgHelp.h typedef PFUNCTION_TABLE_ACCESS_ROUTINE64
using DLL_FUNC_TYPE(SymGetModuleBase64) = DWORD64(__stdcall*)(
    HANDLE hProcess,
    DWORD64 AddrBase);  // DbgHelp.h typedef PGET_MODULE_BASE_ROUTINE64

// TlHelp32.h functions.
using DLL_FUNC_TYPE(CreateToolhelp32Snapshot) =
    HANDLE(__stdcall*)(DWORD dwFlags, DWORD th32ProcessID);
using DLL_FUNC_TYPE(Module32FirstW) = BOOL(__stdcall*)(HANDLE hSnapshot,
                                                       LPMODULEENTRY32W lpme);
using DLL_FUNC_TYPE(Module32NextW) = BOOL(__stdcall*)(HANDLE hSnapshot,
                                                      LPMODULEENTRY32W lpme);

#undef IN
#undef VOID

// Declare a variable for each dynamically loaded DLL function.
#define DEF_DLL_FUNCTION(name) DLL_FUNC_TYPE(name) DLL_FUNC_VAR(name) = nullptr;
DBGHELP_FUNCTION_LIST(DEF_DLL_FUNCTION)
TLHELP32_FUNCTION_LIST(DEF_DLL_FUNCTION)
#undef DEF_DLL_FUNCTION

// Load the functions. This function has a lot of "ugly" macros in order to
// keep down code duplication.

static bool LoadDbgHelpAndTlHelp32() {
  static bool dbghelp_loaded = false;

  if (dbghelp_loaded) return true;

  HMODULE module;

  // Load functions from the dbghelp.dll module.
  module = LoadLibrary(TEXT("dbghelp.dll"));
  if (module == nullptr) {
    return false;
  }

#define LOAD_DLL_FUNC(name)                                                 \
  DLL_FUNC_VAR(name) =                                                      \
      reinterpret_cast<DLL_FUNC_TYPE(name)>(GetProcAddress(module, #name));

DBGHELP_FUNCTION_LIST(LOAD_DLL_FUNC)

#undef LOAD_DLL_FUNC

  // Load functions from the kernel32.dll module (the TlHelp32.h function used
  // to be in tlhelp32.dll but are now moved to kernel32.dll).
  module = LoadLibrary(TEXT("kernel32.dll"));
  if (module == nullptr) {
    return false;
  }

#define LOAD_DLL_FUNC(name)                                                 \
  DLL_FUNC_VAR(name) =                                                      \
      reinterpret_cast<DLL_FUNC_TYPE(name)>(GetProcAddress(module, #name));

TLHELP32_FUNCTION_LIST(LOAD_DLL_FUNC)

#undef LOAD_DLL_FUNC

  // Check that all functions where loaded.
bool result =
#define DLL_FUNC_LOADED(name) (DLL_FUNC_VAR(name) != nullptr)&&

    DBGHELP_FUNCTION_LIST(DLL_FUNC_LOADED)
        TLHELP32_FUNCTION_LIST(DLL_FUNC_LOADED)

#undef DLL_FUNC_LOADED
            true;

  dbghelp_loaded = result;
  return result;
  // NOTE: The modules are never unloaded and will stay around until the
  // application is closed.
}

#undef DBGHELP_FUNCTION_LIST
#undef TLHELP32_FUNCTION_LIST
#undef DLL_FUNC_VAR
#undef DLL_FUNC_TYPE


// Load the symbols for generating stack traces.
static std::vector<OS::SharedLibraryAddress> LoadSymbols(
    HANDLE process_handle) {
  static std::vector<OS::SharedLibraryAddress> result;

  static bool symbols_loaded = false;

  if (symbols_loaded) return result;

  BOOL ok;

  // Initialize the symbol engine.
  ok = _SymInitialize(process_handle,  // hProcess
                      nullptr,         // UserSearchPath
                      false);          // fInvadeProcess
  if (!ok) return result;

  DWORD options = _SymGetOptions();
  options |= SYMOPT_LOAD_LINES;
  options |= SYMOPT_FAIL_CRITICAL_ERRORS;
  options = _SymSetOptions(options);

  char buf[OS::kStackWalkMaxNameLen] = {0};
  ok = _SymGetSearchPath(process_handle, buf, OS::kStackWalkMaxNameLen);
  if (!ok) {
    int err = GetLastError();
    OS::Print("%d\n", err);
    return result;
  }

  HANDLE snapshot = _CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE,       // dwFlags
      GetCurrentProcessId());  // th32ProcessId
  if (snapshot == INVALID_HANDLE_VALUE) return result;
  MODULEENTRY32W module_entry;
  module_entry.dwSize = sizeof(module_entry);  // Set the size of the structure.
  BOOL cont = _Module32FirstW(snapshot, &module_entry);
  while (cont) {
    DWORD64 base;
    // NOTE the SymLoadModule64 function has the peculiarity of accepting a
    // both unicode and ASCII strings even though the parameter is PSTR.
    base = _SymLoadModule64(
        process_handle,                                       // hProcess
        0,                                                    // hFile
        reinterpret_cast<PSTR>(module_entry.szExePath),       // ImageName
        reinterpret_cast<PSTR>(module_entry.szModule),        // ModuleName
        reinterpret_cast<DWORD64>(module_entry.modBaseAddr),  // BaseOfDll
        module_entry.modBaseSize);                            // SizeOfDll
    if (base == 0) {
      int err = GetLastError();
      if (err != ERROR_MOD_NOT_FOUND &&
          err != ERROR_INVALID_HANDLE) {
        result.clear();
        return result;
      }
    }
    int lib_name_length = WideCharToMultiByte(
        CP_UTF8, 0, module_entry.szExePath, -1, nullptr, 0, nullptr, nullptr);
    std::string lib_name(lib_name_length, 0);
    WideCharToMultiByte(CP_UTF8, 0, module_entry.szExePath, -1, &lib_name[0],
                        lib_name_length, nullptr, nullptr);
    result.push_back(OS::SharedLibraryAddress(
        lib_name, reinterpret_cast<uintptr_t>(module_entry.modBaseAddr),
        reinterpret_cast<uintptr_t>(module_entry.modBaseAddr +
                                    module_entry.modBaseSize)));
    cont = _Module32NextW(snapshot, &module_entry);
  }
  CloseHandle(snapshot);

  symbols_loaded = true;
  return result;
}


std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  // SharedLibraryEvents are logged when loading symbol information.
  // Only the shared libraries loaded at the time of the call to
  // GetSharedLibraryAddresses are logged.  DLLs loaded after
  // initialization are not accounted for.
  if (!LoadDbgHelpAndTlHelp32()) return std::vector<OS::SharedLibraryAddress>();
  HANDLE process_handle = GetCurrentProcess();
  return LoadSymbols(process_handle);
}

void OS::SignalCodeMovingGC() {}

#else  // __MINGW32__
std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  return std::vector<OS::SharedLibraryAddress>();
}

void OS::SignalCodeMovingGC() {}
#endif  // __MINGW32__


int OS::ActivationFrameAlignment() {
#ifdef _WIN64
  return 16;  // Windows 64-bit ABI requires the stack to be 16-byte aligned.
#elif defined(__MINGW32__)
  // With gcc 4.4 the tree vectorization optimizer can generate code
  // that requires 16 byte alignment such as movdqa on x86.
  return 16;
#else
  return 8;  // Floating-point math runs faster with 8-byte alignment.
#endif
}

#if defined(V8_OS_WIN)
void EnsureConsoleOutputWin32() {
  UINT new_flags =
      SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
  UINT existing_flags = SetErrorMode(new_flags);
  SetErrorMode(existing_flags | new_flags);
#if defined(_MSC_VER)
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _set_error_mode(_OUT_TO_STDERR);
#endif  // defined(_MSC_VER)
}
#endif  // defined(V8_OS_WIN)

// ----------------------------------------------------------------------------
// Win32 thread support.

// Definition of invalid thread handle and id.
static const HANDLE kNoThread = INVALID_HANDLE_VALUE;

// Entry point for threads. The supplied argument is a pointer to the thread
// object. The entry function dispatches to the run method in the thread
// object. It is important that this function has __stdcall calling
// convention.
static unsigned int __stdcall ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);
  thread->NotifyStartedAndRun();
  return 0;
}


class Thread::PlatformData {
 public:
  explicit PlatformData(HANDLE thread) : thread_(thread) {}
  HANDLE thread_;
  unsigned thread_id_;
};


// Initialize a Win32 thread object. The thread has an invalid thread
// handle until it is started.

Thread::Thread(const Options& options)
    : stack_size_(options.stack_size()), start_semaphore_(nullptr) {
  data_ = new PlatformData(kNoThread);
  set_name(options.name());
}


void Thread::set_name(const char* name) {
  OS::StrNCpy(name_, sizeof(name_), name, strlen(name));
  name_[sizeof(name_) - 1] = '\0';
}


// Close our own handle for the thread.
Thread::~Thread() {
  if (data_->thread_ != kNoThread) CloseHandle(data_->thread_);
  delete data_;
}


// Create a new thread. It is important to use _beginthreadex() instead of
// the Win32 function CreateThread(), because the CreateThread() does not
// initialize thread specific structures in the C runtime library.
bool Thread::Start() {
  uintptr_t result = _beginthreadex(nullptr, static_cast<unsigned>(stack_size_),
                                    ThreadEntry, this, 0, &data_->thread_id_);
  data_->thread_ = reinterpret_cast<HANDLE>(result);
  return result != 0;
}

// Wait for thread to terminate.
void Thread::Join() {
  if (data_->thread_id_ != GetCurrentThreadId()) {
    WaitForSingleObject(data_->thread_, INFINITE);
  }
}


Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  DWORD result = TlsAlloc();
  DCHECK(result != TLS_OUT_OF_INDEXES);
  return static_cast<LocalStorageKey>(result);
}


void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  BOOL result = TlsFree(static_cast<DWORD>(key));
  USE(result);
  DCHECK(result);
}


void* Thread::GetThreadLocal(LocalStorageKey key) {
  return TlsGetValue(static_cast<DWORD>(key));
}


void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  BOOL result = TlsSetValue(static_cast<DWORD>(key), value);
  USE(result);
  DCHECK(result);
}

void OS::AdjustSchedulingParams() {}

std::vector<OS::MemoryRange> OS::GetFreeMemoryRangesWithin(
    OS::Address boundary_start, OS::Address boundary_end, size_t minimum_size,
    size_t alignment) {
  std::vector<OS::MemoryRange> result = {};

  // Search for the virtual memory (vm) ranges within the boundary.
  // If a range is free and larger than {minimum_size}, then push it to the
  // returned vector.
  uintptr_t vm_start = RoundUp(boundary_start, alignment);
  uintptr_t vm_end = 0;
  MEMORY_BASIC_INFORMATION mi;
  // This loop will terminate once the scanning reaches the higher address
  // to the end of boundary or the function VirtualQuery fails.
  while (vm_start < boundary_end &&
         VirtualQuery(reinterpret_cast<LPCVOID>(vm_start), &mi, sizeof(mi)) !=
             0) {
    vm_start = reinterpret_cast<uintptr_t>(mi.BaseAddress);
    vm_end = vm_start + mi.RegionSize;
    if (mi.State == MEM_FREE) {
      // The available area is the overlap of the virtual memory range and
      // boundary. Push the overlapped memory range to the vector if there is
      // enough space.
      const uintptr_t overlap_start =
          RoundUp(std::max(vm_start, boundary_start), alignment);
      const uintptr_t overlap_end =
          RoundDown(std::min(vm_end, boundary_end), alignment);
      if (overlap_start < overlap_end &&
          overlap_end - overlap_start >= minimum_size) {
        result.push_back({overlap_start, overlap_end});
      }
    }
    // Continue to visit the next virtual memory range.
    vm_start = vm_end;
  }

  return result;
}

// static
Stack::StackSlot Stack::ObtainCurrentThreadStackStart() {
#if defined(V8_TARGET_ARCH_X64)
  return reinterpret_cast<void*>(
      reinterpret_cast<NT_TIB64*>(NtCurrentTeb())->StackBase);
#elif defined(V8_TARGET_ARCH_32_BIT)
  return reinterpret_cast<void*>(
      reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
#elif defined(V8_TARGET_ARCH_ARM64)
  // Windows 8 and later, see
  // https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthreadstacklimits
  ULONG_PTR lowLimit, highLimit;
  ::GetCurrentThreadStackLimits(&lowLimit, &highLimit);
  return reinterpret_cast<void*>(highLimit);
#else
#error Unsupported ObtainCurrentThreadStackStart.
#endif
}

// static
Stack::StackSlot Stack::GetCurrentStackPosition() {
#if V8_CC_MSVC
  return _AddressOfReturnAddress();
#else
  return __builtin_frame_address(0);
#endif
}

}  // namespace base
}  // namespace v8
