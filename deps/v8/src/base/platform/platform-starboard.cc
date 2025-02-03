// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for Starboard goes here. Starboard is the platform
// abstraction layer for Cobalt, an HTML5 container used mainly by YouTube
// apps in the living room.

#include <stdio.h>
#include <sys/mman.h>

#include "src/base/lazy-instance.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/timezone-cache.h"
#include "src/base/utils/random-number-generator.h"
#include "starboard/client_porting/eztime/eztime.h"
#include "starboard/common/condition_variable.h"
#include "starboard/common/log.h"
#include "starboard/common/string.h"
#include "starboard/common/time.h"
#include "starboard/configuration.h"
#include "starboard/configuration_constants.h"
#include "starboard/time_zone.h"

namespace v8 {
namespace base {

#ifdef __arm__
bool OS::ArmUsingHardFloat() {
  // GCC versions 4.6 and above define __ARM_PCS or __ARM_PCS_VFP to specify
  // the Floating Point ABI used (PCS stands for Procedure Call Standard).
  // We use these as well as a couple of other defines to statically determine
  // what FP ABI used.
  // GCC versions 4.4 and below don't support hard-fp.
  // GCC versions 4.5 may support hard-fp without defining __ARM_PCS or
  // __ARM_PCS_VFP.

#define GCC_VERSION \
  (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#if GCC_VERSION >= 40600 && !defined(__clang__)
#if defined(__ARM_PCS_VFP)
  return true;
#else
  return false;
#endif

#elif GCC_VERSION < 40500 && !defined(__clang__)
  return false;

#else
#if defined(__ARM_PCS_VFP)
  return true;
#elif defined(__ARM_PCS) || defined(__SOFTFP__) || defined(__SOFTFP) || \
    !defined(__VFP_FP__)
  return false;
#else
#error \
    "Your version of compiler does not report the FP ABI compiled for."     \
       "Please report it on this issue"                                        \
       "http://code.google.com/p/v8/issues/detail?id=2140"

#endif
#endif
#undef GCC_VERSION
}
#endif  // def __arm__

namespace {

static LazyInstance<RandomNumberGenerator>::type
    platform_random_number_generator = LAZY_INSTANCE_INITIALIZER;
static LazyMutex rng_mutex = LAZY_MUTEX_INITIALIZER;

// We only use this stack size to get the topmost stack frame.
const int kStackSize = 1;

}  // namespace

void OS::Initialize(AbortMode abort_mode, const char* const gc_fake_mmap) {
  g_abort_mode = abort_mode;
  // This is only used on Posix, we don't need to use it for anything.
}

int OS::GetUserTime(uint32_t* secs, uint32_t* usecs) {
  const int64_t us_time = starboard::CurrentMonotonicThreadTime();
  if (us_time == 0) return -1;
  *secs = us_time / TimeConstants::kMicroSecondsPerSecond;
  *usecs = us_time % TimeConstants::kMicroSecondsPerSecond;
  return 0;
}

double OS::TimeCurrentMillis() { return Time::Now().ToJsTime(); }

int OS::ActivationFrameAlignment() {
#if V8_TARGET_ARCH_ARM
  // On EABI ARM targets this is required for fp correctness in the
  // runtime system.
  return 8;
#elif V8_TARGET_ARCH_MIPS
  return 8;
#elif V8_TARGET_ARCH_S390
  return 8;
#else
  // Otherwise we just assume 16 byte alignment, i.e.:
  // - With gcc 4.4 the tree vectorization optimizer can generate code
  //   that requires 16 byte alignment such as movdqa on x86.
  // - Mac OS X, PPC and Solaris (64-bit) activation frames must
  //   be 16 byte-aligned;  see "Mac OS X ABI Function Call Guide"
  return 16;
#endif
}

// static
size_t OS::AllocatePageSize() { return kSbMemoryPageSize; }

// static
size_t OS::CommitPageSize() { return kSbMemoryPageSize; }

// static
void OS::SetRandomMmapSeed(int64_t seed) { SB_NOTIMPLEMENTED(); }

// static
void* OS::GetRandomMmapAddr() { return nullptr; }

void* Allocate(void* address, size_t size, OS::MemoryPermission access) {
  int prot_flags;
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
      prot_flags = PROT_NONE;
      break;
    case OS::MemoryPermission::kReadWrite:
      prot_flags = PROT_READ | PROT_WRITE;
      break;
    default:
      SB_LOG(ERROR) << "The requested memory allocation access is not"
                       " implemented for Starboard: "
                    << static_cast<int>(access);
      return nullptr;
  }
  void* result = mmap(nullptr, size, prot_flags, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (result == MAP_FAILED) {
    return nullptr;
  }
  return result;
}

// static
void* OS::Allocate(void* address, size_t size, size_t alignment,
                   MemoryPermission access) {
  size_t page_size = AllocatePageSize();
  DCHECK_EQ(0, size % page_size);
  DCHECK_EQ(0, alignment % page_size);
  address = AlignedAddress(address, alignment);
  // Add the maximum misalignment so we are guaranteed an aligned base address.
  size_t request_size = size + (alignment - page_size);
  request_size = RoundUp(request_size, OS::AllocatePageSize());
  void* result = base::Allocate(address, request_size, access);
  if (result == nullptr) return nullptr;

  // Unmap memory allocated before the aligned base address.
  uint8_t* base = static_cast<uint8_t*>(result);
  uint8_t* aligned_base = reinterpret_cast<uint8_t*>(
      RoundUp(reinterpret_cast<uintptr_t>(base), alignment));
  if (aligned_base != base) {
    DCHECK_LT(base, aligned_base);
    size_t prefix_size = static_cast<size_t>(aligned_base - base);
    Free(base, prefix_size);
    request_size -= prefix_size;
  }
  // Unmap memory allocated after the potentially unaligned end.
  if (size != request_size) {
    DCHECK_LT(size, request_size);
    size_t suffix_size = request_size - size;
    Free(aligned_base + size, suffix_size);
    request_size -= suffix_size;
  }

  DCHECK_EQ(size, request_size);
  return static_cast<void*>(aligned_base);
}

// static
void OS::Free(void* address, const size_t size) {
  CHECK_EQ(munmap(address, size), 0);
}

// static
void OS::Release(void* address, size_t size) {
  CHECK_EQ(munmap(address, size), 0);
}

// static
bool OS::SetPermissions(void* address, size_t size, MemoryPermission access) {
  int new_protection;
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
      new_protection = PROT_NONE;
      break;
    case OS::MemoryPermission::kRead:
      new_protection = PROT_READ;
    case OS::MemoryPermission::kReadWrite:
      new_protection = PROT_READ | PROT_WRITE;
      break;
    case OS::MemoryPermission::kReadExecute:
#if SB_CAN(MAP_EXECUTABLE_MEMORY)
      new_protection = PROT_READ | PROT_EXEC;
#else
      UNREACHABLE();
#endif
      break;
    default:
      // All other types are not supported by Starboard.
      return false;
  }
  return mprotect(address, size, new_protection) == 0;
}

// static
bool OS::RecommitPages(void* address, size_t size, MemoryPermission access) {
  return SetPermissions(address, size, access);
}

// static
bool OS::HasLazyCommits() {
  SB_NOTIMPLEMENTED();
  return false;
}

void OS::Sleep(TimeDelta interval) { SbThreadSleep(interval.InMicroseconds()); }

void OS::Abort() { SbSystemBreakIntoDebugger(); }

void OS::DebugBreak() { SbSystemBreakIntoDebugger(); }

class StarboardMemoryMappedFile final : public OS::MemoryMappedFile {
 public:
  ~StarboardMemoryMappedFile() final;
  void* memory() const final {
    SB_NOTIMPLEMENTED();
    return nullptr;
  }
  size_t size() const final {
    SB_NOTIMPLEMENTED();
    return 0u;
  }
};

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::open(const char* name,
                                                 FileMode mode) {
  SB_NOTIMPLEMENTED();
  return nullptr;
}

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name,
                                                   size_t size, void* initial) {
  SB_NOTIMPLEMENTED();
  return nullptr;
}

StarboardMemoryMappedFile::~StarboardMemoryMappedFile() { SB_NOTIMPLEMENTED(); }

int OS::GetCurrentProcessId() {
  SB_NOTIMPLEMENTED();
  return 0;
}

int OS::GetCurrentThreadId() { return SbThreadGetId(); }

int OS::GetLastError() { return SbSystemGetLastError(); }

// ----------------------------------------------------------------------------
// POSIX stdio support.
//

FILE* OS::FOpen(const char* path, const char* mode) {
  SB_NOTIMPLEMENTED();
  return nullptr;
}

bool OS::Remove(const char* path) {
  SB_NOTIMPLEMENTED();
  return false;
}

char OS::DirectorySeparator() { return kSbFileSepChar; }

bool OS::isDirectorySeparator(const char ch) {
  return ch == DirectorySeparator();
}

FILE* OS::OpenTemporaryFile() {
  SB_NOTIMPLEMENTED();
  return nullptr;
}

const char* const OS::LogFileOpenMode = "\0";

void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrint(format, args);
  va_end(args);
}

void OS::VPrint(const char* format, va_list args) {
  SbLogRawFormat(format, args);
}

void OS::FPrint(FILE* out, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
}

void OS::VFPrint(FILE* out, const char* format, va_list args) {
  SbLogRawFormat(format, args);
}

void OS::PrintError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
}

void OS::VPrintError(const char* format, va_list args) {
  // Starboard has no concept of stderr vs stdout.
  SbLogRawFormat(format, args);
}

int OS::SNPrintF(char* str, int length, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = VSNPrintF(str, length, format, args);
  va_end(args);
  return result;
}

int OS::VSNPrintF(char* str, int length, const char* format, va_list args) {
  int n = vsnprintf(str, length, format, args);
  if (n < 0 || n >= length) {
    // If the length is zero, the assignment fails.
    if (length > 0) str[length - 1] = '\0';
    return -1;
  } else {
    return n;
  }
}

// ----------------------------------------------------------------------------
// POSIX string support.
//

void OS::StrNCpy(char* dest, int length, const char* src, size_t n) {
  strncpy(dest, src, n);
}

// ----------------------------------------------------------------------------
// POSIX thread support.
//

class Thread::PlatformData {
 public:
  PlatformData() : thread_(kSbThreadInvalid) {}
  SbThread thread_;  // Thread handle for pthread.
  // Synchronizes thread creation
  Mutex thread_creation_mutex_;
};

Thread::Thread(const Options& options)
    : data_(new PlatformData),
      stack_size_(options.stack_size()),
      start_semaphore_(nullptr) {
  set_name(options.name());
}

Thread::~Thread() { delete data_; }

static void SetThreadName(const char* name) { SbThreadSetName(name); }

static void* ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);
  // We take the lock here to make sure that pthread_create finished first since
  // we don't know which thread will run first (the original thread or the new
  // one).
  { LockGuard<Mutex> lock_guard(&thread->data()->thread_creation_mutex_); }
  SetThreadName(thread->name());
  // DCHECK_NE(thread->data()->thread_, kNoThread);
  thread->NotifyStartedAndRun();

  return nullptr;
}

void Thread::set_name(const char* name) {
  strncpy(name_, name, sizeof(name_));
  name_[sizeof(name_) - 1] = '\0';
}

bool Thread::Start() {
  data_->thread_ =
      SbThreadCreate(stack_size_, kSbThreadNoPriority, kSbThreadNoAffinity,
                     true, name_, ThreadEntry, this);
  return SbThreadIsValid(data_->thread_);
}

void Thread::Join() { SbThreadJoin(data_->thread_, nullptr); }

Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  return SbThreadCreateLocalKey(nullptr);
}

void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  SbThreadDestroyLocalKey(key);
}

void* Thread::GetThreadLocal(LocalStorageKey key) {
  return SbThreadGetLocalValue(key);
}

void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  bool result = SbThreadSetLocalValue(key, value);
  DCHECK(result);
}

class StarboardTimezoneCache : public TimezoneCache {
 public:
  void Clear(TimeZoneDetection time_zone_detection) override {}
  ~StarboardTimezoneCache() override {}

 protected:
  static const int msPerSecond = 1000;
};

class StarboardDefaultTimezoneCache : public StarboardTimezoneCache {
 public:
  const char* LocalTimezone(double time_ms) override {
    return SbTimeZoneGetName();
  }
  double LocalTimeOffset(double time_ms, bool is_utc) override {
    // SbTimeZoneGetCurrent returns an offset west of Greenwich, which has the
    // opposite sign V8 expects.
    // The starboard function returns offset in minutes. We convert to return
    // value in milliseconds.
    return SbTimeZoneGetCurrent() * 60.0 * msPerSecond * (-1);
  }
  double DaylightSavingsOffset(double time_ms) override {
    int64_t posix_microseconds = starboard::CurrentPosixTime();
    EzTimeValue value = {
        posix_microseconds / TimeConstants::kMicroSecondsPerSecond,
        (int32_t)(posix_microseconds % TimeConstants::kMicroSecondsPerSecond)
    };
    EzTimeExploded ez_exploded;
    bool result =
        EzTimeValueExplode(&value, kEzTimeZoneLocal, &ez_exploded, NULL);
    return ez_exploded.tm_isdst > 0 ? 3600 * msPerSecond : 0;
  }

  ~StarboardDefaultTimezoneCache() override {}
};

TimezoneCache* OS::CreateTimezoneCache() {
  return new StarboardDefaultTimezoneCache();
}

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  SB_NOTIMPLEMENTED();
  return {};
}

void OS::SignalCodeMovingGC() { SB_NOTIMPLEMENTED(); }

void OS::AdjustSchedulingParams() {}

std::optional<OS::MemoryRange> OS::GetFirstFreeMemoryRangeWithin(
    OS::Address boundary_start, OS::Address boundary_end, size_t minimum_size,
    size_t alignment) {
  return std::nullopt;
}

bool OS::DiscardSystemPages(void* address, size_t size) {
  // Starboard API does not support this function yet.
  return true;
}

// static
Stack::StackSlot Stack::GetStackStart() {
  SB_NOTIMPLEMENTED();
  return nullptr;
}

// static
Stack::StackSlot Stack::GetCurrentStackPosition() {
  void* addresses[kStackSize];
  const size_t count = SbSystemGetStack(addresses, kStackSize);
  if (count > 0) {
    return addresses[0];
  } else {
    return nullptr;
  }
}

}  // namespace base
}  // namespace v8
