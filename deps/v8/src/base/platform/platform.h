// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This module contains the platform-specific code. This make the rest of the
// code less dependent on operating system, compilers and runtime libraries.
// This module does specifically not deal with differences between different
// processor architecture.
// The platform classes have the same definition for all platforms. The
// implementation for a particular platform is put in platform_<os>.cc.
// The build system then uses the implementation for the target platform.
//
// This design has been chosen because it is simple and fast. Alternatively,
// the platform dependent classes could have been implemented using abstract
// superclasses with virtual methods and having specializations for each
// platform. This design was rejected because it was more complicated and
// slower. It would require factory methods for selecting the right
// implementation and the overhead of virtual methods for performance
// sensitive like mutex locking/unlocking.

#ifndef V8_BASE_PLATFORM_PLATFORM_H_
#define V8_BASE_PLATFORM_PLATFORM_H_

#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>

#include "include/v8-platform.h"
#include "src/base/abort-mode.h"
#include "src/base/base-export.h"
#include "src/base/build_config.h"
#include "src/base/compiler-specific.h"
#include "src/base/macros.h"
#include "src/base/optional.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/semaphore.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

#if V8_OS_QNX
#include "src/base/qnx-math.h"
#endif

#if V8_CC_MSVC
#include <intrin.h>
#endif  // V8_CC_MSVC

#if V8_OS_FUCHSIA
#include <zircon/types.h>
#endif  // V8_OS_FUCHSIA

#ifdef V8_USE_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif  // V8_USE_ADDRESS_SANITIZER

#ifndef V8_NO_FAST_TLS
#if V8_CC_MSVC && V8_HOST_ARCH_IA32
// __readfsdword is supposed to be declared in intrin.h but it is missing from
// some versions of that file. See https://bugs.llvm.org/show_bug.cgi?id=51188
// And, intrin.h is a very expensive header that we want to avoid here, and
// the cheaper intrin0.h is not available for all build configurations. That is
// why we declare this intrinsic.
extern "C" unsigned long __readfsdword(unsigned long);  // NOLINT(runtime/int)
#endif                                       // V8_CC_MSVC && V8_HOST_ARCH_IA32
#endif                                       // V8_NO_FAST_TLS

namespace v8 {

namespace internal {
class HandleHelper;
}

namespace base {

// ----------------------------------------------------------------------------
// Fast TLS support

#ifndef V8_NO_FAST_TLS

#if V8_CC_MSVC && V8_HOST_ARCH_IA32

#define V8_FAST_TLS_SUPPORTED 1

V8_INLINE intptr_t InternalGetExistingThreadLocal(intptr_t index) {
  const intptr_t kTibInlineTlsOffset = 0xE10;
  const intptr_t kTibExtraTlsOffset = 0xF94;
  const intptr_t kMaxInlineSlots = 64;
  const intptr_t kMaxSlots = kMaxInlineSlots + 1024;
  const intptr_t kSystemPointerSize = sizeof(void*);
  DCHECK(0 <= index && index < kMaxSlots);
  USE(kMaxSlots);
  if (index < kMaxInlineSlots) {
    return static_cast<intptr_t>(
        __readfsdword(kTibInlineTlsOffset + kSystemPointerSize * index));
  }
  intptr_t extra = static_cast<intptr_t>(__readfsdword(kTibExtraTlsOffset));
  if (!extra) return 0;
  return *reinterpret_cast<intptr_t*>(extra + kSystemPointerSize *
                                                  (index - kMaxInlineSlots));
}

// Not possible on ARM64, the register holding the base pointer is not stable
// across major releases.
#elif defined(__APPLE__) && (V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64)

// tvOS simulator does not use intptr_t as TLS key.
#if !defined(V8_OS_STARBOARD) || !defined(TARGET_OS_SIMULATOR)

#define V8_FAST_TLS_SUPPORTED 1

V8_INLINE intptr_t InternalGetExistingThreadLocal(intptr_t index) {
  intptr_t result;
#if V8_HOST_ARCH_IA32
  asm("movl %%gs:(,%1,4), %0;"
      : "=r"(result)  // Output must be a writable register.
      : "r"(index));
#else
  asm("movq %%gs:(,%1,8), %0;" : "=r"(result) : "r"(index));
#endif
  return result;
}

#endif  // !defined(V8_OS_STARBOARD) || !defined(TARGET_OS_SIMULATOR)

#endif

#endif  // V8_NO_FAST_TLS

class AddressSpaceReservation;
class PageAllocator;
class TimezoneCache;
class VirtualAddressSpace;
class VirtualAddressSubspace;

// ----------------------------------------------------------------------------
// OS
//
// This class has static methods for the different platform specific
// functions. Add methods here to cope with differences between the
// supported platforms.

class V8_BASE_EXPORT OS {
 public:
  // Initialize the OS class.
  // - abort_mode: see src/base/abort-mode.h for details.
  // - gc_fake_mmap: Name of the file for fake gc mmap used in ll_prof.
  static void Initialize(AbortMode abort_mode, const char* const gc_fake_mmap);

#if V8_OS_WIN
  // On Windows, ensure the newer memory API is loaded if available.  This
  // includes function like VirtualAlloc2 and MapViewOfFile3.
  // TODO(chromium:1218005) this should probably happen as part of Initialize,
  // but that is currently invoked too late, after the sandbox is initialized.
  // However, eventually the sandbox initialization will probably happen as
  // part of V8::Initialize, at which point this function can probably be
  // merged into OS::Initialize.
  static void EnsureWin32MemoryAPILoaded();
#endif

  // Returns the accumulated user time for thread. This routine
  // can be used for profiling. The implementation should
  // strive for high-precision timer resolution, preferable
  // micro-second resolution.
  static int GetUserTime(uint32_t* secs,  uint32_t* usecs);

  // Obtain the peak memory usage in kilobytes
  static int GetPeakMemoryUsageKb();

  // Returns current time as the number of milliseconds since
  // 00:00:00 UTC, January 1, 1970.
  static double TimeCurrentMillis();

  static TimezoneCache* CreateTimezoneCache();

  // Returns last OS error.
  static int GetLastError();

  static FILE* FOpen(const char* path, const char* mode);
  static bool Remove(const char* path);

  static char DirectorySeparator();
  static bool isDirectorySeparator(const char ch);

  // Opens a temporary file, the file is auto removed on close.
  static FILE* OpenTemporaryFile();

  // Log file open mode is platform-dependent due to line ends issues.
  static const char* const LogFileOpenMode;

  // Print output to console. This is mostly used for debugging output.
  // On platforms that has standard terminal output, the output
  // should go to stdout.
  static PRINTF_FORMAT(1, 2) void Print(const char* format, ...);
  static PRINTF_FORMAT(1, 0) void VPrint(const char* format, va_list args);

  // Print output to a file. This is mostly used for debugging output.
  static PRINTF_FORMAT(2, 3) void FPrint(FILE* out, const char* format, ...);
  static PRINTF_FORMAT(2, 0) void VFPrint(FILE* out, const char* format,
                                          va_list args);

  // Print error output to console. This is mostly used for error message
  // output. On platforms that has standard terminal output, the output
  // should go to stderr.
  static PRINTF_FORMAT(1, 2) void PrintError(const char* format, ...);
  static PRINTF_FORMAT(1, 0) void VPrintError(const char* format, va_list args);

  // Memory permissions. These should be kept in sync with the ones in
  // v8::PageAllocator and v8::PagePermissions.
  enum class MemoryPermission {
    kNoAccess,
    kRead,
    kReadWrite,
    kReadWriteExecute,
    kReadExecute,
    // TODO(jkummerow): Remove this when Wasm has a platform-independent
    // w^x implementation.
    kNoAccessWillJitLater
  };

  // Helpers to create shared memory objects. Currently only used for testing.
  static PlatformSharedMemoryHandle CreateSharedMemoryHandleForTesting(
      size_t size);
  static void DestroySharedMemoryHandle(PlatformSharedMemoryHandle handle);

  static bool HasLazyCommits();

  // Sleep for a specified time interval.
  static void Sleep(TimeDelta interval);

  // Abort the current process.
  [[noreturn]] static void Abort();

  // Debug break.
  static void DebugBreak();

  // Walk the stack.
  static const int kStackWalkError = -1;
  static const int kStackWalkMaxNameLen = 256;
  static const int kStackWalkMaxTextLen = 256;
  struct StackFrame {
    void* address;
    char text[kStackWalkMaxTextLen];
  };

  class V8_BASE_EXPORT MemoryMappedFile {
   public:
    enum class FileMode { kReadOnly, kReadWrite };

    virtual ~MemoryMappedFile() = default;
    virtual void* memory() const = 0;
    virtual size_t size() const = 0;

    static MemoryMappedFile* open(const char* name,
                                  FileMode mode = FileMode::kReadWrite);
    static MemoryMappedFile* create(const char* name, size_t size,
                                    void* initial);
  };

  // Safe formatting print. Ensures that str is always null-terminated.
  // Returns the number of chars written, or -1 if output was truncated.
  static PRINTF_FORMAT(3, 4) int SNPrintF(char* str, int length,
                                          const char* format, ...);
  static PRINTF_FORMAT(3, 0) int VSNPrintF(char* str, int length,
                                           const char* format, va_list args);

  static void StrNCpy(char* dest, int length, const char* src, size_t n);

  // Support for the profiler.  Can do nothing, in which case ticks
  // occurring in shared libraries will not be properly accounted for.
  struct SharedLibraryAddress {
    SharedLibraryAddress(const std::string& library_path, uintptr_t start,
                         uintptr_t end)
        : library_path(library_path), start(start), end(end), aslr_slide(0) {}
    SharedLibraryAddress(const std::string& library_path, uintptr_t start,
                         uintptr_t end, intptr_t aslr_slide)
        : library_path(library_path),
          start(start),
          end(end),
          aslr_slide(aslr_slide) {}

    std::string library_path;
    uintptr_t start;
    uintptr_t end;
    intptr_t aslr_slide;
  };

  static std::vector<SharedLibraryAddress> GetSharedLibraryAddresses();

  // Support for the profiler.  Notifies the external profiling
  // process that a code moving garbage collection starts.  Can do
  // nothing, in which case the code objects must not move (e.g., by
  // using --never-compact) if accurate profiling is desired.
  static void SignalCodeMovingGC();

  // Support runtime detection of whether the hard float option of the
  // EABI is used.
  static bool ArmUsingHardFloat();

  // Returns the activation frame alignment constraint or zero if
  // the platform doesn't care. Guaranteed to be a power of two.
  static int ActivationFrameAlignment();

  static int GetCurrentProcessId();

  static int GetCurrentThreadId();

  static void AdjustSchedulingParams();

  using Address = uintptr_t;

  struct MemoryRange {
    uintptr_t start = 0;
    uintptr_t end = 0;
  };

  // Find gaps between existing virtual memory ranges that have enough space
  // to place a region with minimum_size within (boundary_start, boundary_end)
  static std::vector<MemoryRange> GetFreeMemoryRangesWithin(
      Address boundary_start, Address boundary_end, size_t minimum_size,
      size_t alignment);

  [[noreturn]] static void ExitProcess(int exit_code);

  // Whether the platform supports mapping a given address in another location
  // in the address space.
  V8_WARN_UNUSED_RESULT static constexpr bool IsRemapPageSupported() {
#if (defined(V8_OS_DARWIN) || defined(V8_OS_LINUX)) && \
    !(defined(V8_TARGET_ARCH_PPC64) || defined(V8_TARGET_ARCH_S390X))
    return true;
#else
    return false;
#endif
  }

  // Remaps already-mapped memory at |new_address| with |access| permissions.
  //
  // Both the source and target addresses must be page-aligned, and |size| must
  // be a multiple of the system page size.  If there is already memory mapped
  // at the target address, it is replaced by the new mapping.
  //
  // In addition, this is only meant to remap memory which is file-backed, and
  // mapped from a file which is still accessible.
  //
  // Must not be called if |IsRemapPagesSupported()| return false.
  // Returns true for success.
  V8_WARN_UNUSED_RESULT static bool RemapPages(const void* address, size_t size,
                                               void* new_address,
                                               MemoryPermission access);

  // Make part of the process's data memory read-only.
  static void SetDataReadOnly(void* address, size_t size);

 private:
  // These classes use the private memory management API below.
  friend class AddressSpaceReservation;
  friend class MemoryMappedFile;
  friend class PosixMemoryMappedFile;
  friend class v8::base::PageAllocator;
  friend class v8::base::VirtualAddressSpace;
  friend class v8::base::VirtualAddressSubspace;
  FRIEND_TEST(OS, RemapPages);

  static size_t AllocatePageSize();

  static size_t CommitPageSize();

  static void SetRandomMmapSeed(int64_t seed);

  static void* GetRandomMmapAddr();

  V8_WARN_UNUSED_RESULT static void* Allocate(void* address, size_t size,
                                              size_t alignment,
                                              MemoryPermission access);

  V8_WARN_UNUSED_RESULT static void* AllocateShared(size_t size,
                                                    MemoryPermission access);

  V8_WARN_UNUSED_RESULT static void* RemapShared(void* old_address,
                                                 void* new_address,
                                                 size_t size);

  static void Free(void* address, size_t size);

  V8_WARN_UNUSED_RESULT static void* AllocateShared(
      void* address, size_t size, OS::MemoryPermission access,
      PlatformSharedMemoryHandle handle, uint64_t offset);

  static void FreeShared(void* address, size_t size);

  static void Release(void* address, size_t size);

  V8_WARN_UNUSED_RESULT static bool SetPermissions(void* address, size_t size,
                                                   MemoryPermission access);

  V8_WARN_UNUSED_RESULT static bool RecommitPages(void* address, size_t size,
                                                  MemoryPermission access);

  V8_WARN_UNUSED_RESULT static bool DiscardSystemPages(void* address,
                                                       size_t size);

  V8_WARN_UNUSED_RESULT static bool DecommitPages(void* address, size_t size);

  V8_WARN_UNUSED_RESULT static bool CanReserveAddressSpace();

  V8_WARN_UNUSED_RESULT static Optional<AddressSpaceReservation>
  CreateAddressSpaceReservation(void* hint, size_t size, size_t alignment,
                                MemoryPermission max_permission);

  static void FreeAddressSpaceReservation(AddressSpaceReservation reservation);

  static const int msPerSecond = 1000;

#if V8_OS_POSIX
  static const char* GetGCFakeMMapFile();
#endif

  DISALLOW_IMPLICIT_CONSTRUCTORS(OS);
};

#if defined(V8_OS_WIN)
V8_BASE_EXPORT void EnsureConsoleOutputWin32();
#endif  // defined(V8_OS_WIN)

inline void EnsureConsoleOutput() {
#if defined(V8_OS_WIN)
  // Windows requires extra calls to send assert output to the console
  // rather than a dialog box.
  EnsureConsoleOutputWin32();
#endif  // defined(V8_OS_WIN)
}

// ----------------------------------------------------------------------------
// AddressSpaceReservation
//
// This class provides the same memory management functions as OS but operates
// inside a previously reserved contiguous region of virtual address space.
//
// Reserved address space in which no pages have been allocated is guaranteed
// to be inaccessible and cause a fault on access. As such, creating guard
// regions requires no further action.
class V8_BASE_EXPORT AddressSpaceReservation {
 public:
  using Address = uintptr_t;

  void* base() const { return base_; }
  size_t size() const { return size_; }

  bool Contains(void* region_addr, size_t region_size) const {
    Address base = reinterpret_cast<Address>(base_);
    Address region_base = reinterpret_cast<Address>(region_addr);
    return (region_base >= base) &&
           ((region_base + region_size) <= (base + size_));
  }

  V8_WARN_UNUSED_RESULT bool Allocate(void* address, size_t size,
                                      OS::MemoryPermission access);

  V8_WARN_UNUSED_RESULT bool Free(void* address, size_t size);

  V8_WARN_UNUSED_RESULT bool AllocateShared(void* address, size_t size,
                                            OS::MemoryPermission access,
                                            PlatformSharedMemoryHandle handle,
                                            uint64_t offset);

  V8_WARN_UNUSED_RESULT bool FreeShared(void* address, size_t size);

  V8_WARN_UNUSED_RESULT bool SetPermissions(void* address, size_t size,
                                            OS::MemoryPermission access);

  V8_WARN_UNUSED_RESULT bool RecommitPages(void* address, size_t size,
                                           OS::MemoryPermission access);

  V8_WARN_UNUSED_RESULT bool DiscardSystemPages(void* address, size_t size);

  V8_WARN_UNUSED_RESULT bool DecommitPages(void* address, size_t size);

  V8_WARN_UNUSED_RESULT Optional<AddressSpaceReservation> CreateSubReservation(
      void* address, size_t size, OS::MemoryPermission max_permission);

  V8_WARN_UNUSED_RESULT static bool FreeSubReservation(
      AddressSpaceReservation reservation);

#if V8_OS_WIN
  // On Windows, the placeholder mappings backing address space reservations
  // need to be split and merged as page allocations can only replace an entire
  // placeholder mapping, not parts of it. This must be done by the users of
  // this API as it requires a RegionAllocator (or equivalent) to keep track of
  // sub-regions and decide when to split and when to coalesce multiple free
  // regions into a single one.
  V8_WARN_UNUSED_RESULT bool SplitPlaceholder(void* address, size_t size);
  V8_WARN_UNUSED_RESULT bool MergePlaceholders(void* address, size_t size);
#endif  // V8_OS_WIN

 private:
  friend class OS;

#if V8_OS_FUCHSIA
  AddressSpaceReservation(void* base, size_t size, zx_handle_t vmar)
      : base_(base), size_(size), vmar_(vmar) {}
#else
  AddressSpaceReservation(void* base, size_t size) : base_(base), size_(size) {}
#endif  // V8_OS_FUCHSIA

  void* base_ = nullptr;
  size_t size_ = 0;

#if V8_OS_FUCHSIA
  // On Fuchsia, address space reservations are backed by VMARs.
  zx_handle_t vmar_ = ZX_HANDLE_INVALID;
#endif  // V8_OS_FUCHSIA
};

// ----------------------------------------------------------------------------
// Thread
//
// Thread objects are used for creating and running threads. When the start()
// method is called the new thread starts running the run() method in the new
// thread. The Thread object should not be deallocated before the thread has
// terminated.

class V8_BASE_EXPORT Thread {
 public:
  // Opaque data type for thread-local storage keys.
#if V8_OS_STARBOARD
  using LocalStorageKey = SbThreadLocalKey;
#else
  using LocalStorageKey = int32_t;
#endif

  // Priority class for the thread. Use kDefault to keep the priority
  // unchanged.
  enum class Priority { kBestEffort, kUserVisible, kUserBlocking, kDefault };

  class Options {
   public:
    Options() : Options("v8:<unknown>") {}
    explicit Options(const char* name, int stack_size = 0)
        : Options(name, Priority::kDefault, stack_size) {}
    Options(const char* name, Priority priority, int stack_size = 0)
        : name_(name), priority_(priority), stack_size_(stack_size) {}

    const char* name() const { return name_; }
    int stack_size() const { return stack_size_; }
    Priority priority() const { return priority_; }

   private:
    const char* name_;
    const Priority priority_;
    const int stack_size_;
  };

  // Create new thread.
  explicit Thread(const Options& options);
  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;
  virtual ~Thread();

  // Start new thread by calling the Run() method on the new thread.
  V8_WARN_UNUSED_RESULT bool Start();

  // Start new thread and wait until Run() method is called on the new thread.
  bool StartSynchronously() {
    start_semaphore_ = new Semaphore(0);
    if (!Start()) return false;
    start_semaphore_->Wait();
    delete start_semaphore_;
    start_semaphore_ = nullptr;
    return true;
  }

  // Wait until thread terminates.
  void Join();

  inline const char* name() const {
    return name_;
  }

  // Abstract method for run handler.
  virtual void Run() = 0;

  // Thread-local storage.
  static LocalStorageKey CreateThreadLocalKey();
  static void DeleteThreadLocalKey(LocalStorageKey key);
  static void* GetThreadLocal(LocalStorageKey key);
  static void SetThreadLocal(LocalStorageKey key, void* value);
  static bool HasThreadLocal(LocalStorageKey key) {
    return GetThreadLocal(key) != nullptr;
  }

#ifdef V8_FAST_TLS_SUPPORTED
  static inline void* GetExistingThreadLocal(LocalStorageKey key) {
    void* result = reinterpret_cast<void*>(
        InternalGetExistingThreadLocal(static_cast<intptr_t>(key)));
    DCHECK(result == GetThreadLocal(key));
    return result;
  }
#else
  static inline void* GetExistingThreadLocal(LocalStorageKey key) {
    return GetThreadLocal(key);
  }
#endif

  // The thread name length is limited to 16 based on Linux's implementation of
  // prctl().
  static const int kMaxThreadNameLength = 16;

  class PlatformData;
  PlatformData* data() { return data_; }
  Priority priority() const { return priority_; }

  void NotifyStartedAndRun() {
    if (start_semaphore_) start_semaphore_->Signal();
    Run();
  }

 private:
  void set_name(const char* name);

  PlatformData* data_;

  char name_[kMaxThreadNameLength];
  int stack_size_;
  Priority priority_;
  Semaphore* start_semaphore_;
};

// TODO(v8:10354): Make use of the stack utilities here in V8.
class V8_BASE_EXPORT Stack {
 public:
  // Convenience wrapper to use stack slots as unsigned values or void*
  // pointers.
  struct StackSlot {
    // NOLINTNEXTLINE
    StackSlot(void* value) : value(reinterpret_cast<uintptr_t>(value)) {}
    StackSlot(uintptr_t value) : value(value) {}  // NOLINT

    // NOLINTNEXTLINE
    operator void*() const { return reinterpret_cast<void*>(value); }
    operator uintptr_t() const { return value; }  // NOLINT

    uintptr_t value;
  };

  // Gets the start of the stack of the current thread.
  static StackSlot GetStackStart();

  // Returns the current stack top. Works correctly with ASAN and SafeStack.
  //
  // GetCurrentStackPosition() should not be inlined, because it works on stack
  // frames if it were inlined into a function with a huge stack frame it would
  // return an address significantly above the actual current stack position.
  static V8_NOINLINE StackSlot GetCurrentStackPosition();

  // Same as `GetCurrentStackPosition()` with the difference that it is always
  // inlined and thus always returns the current frame's stack top.
  static V8_INLINE StackSlot GetCurrentFrameAddress() {
#if V8_CC_MSVC
    return _AddressOfReturnAddress();
#else
    return __builtin_frame_address(0);
#endif
  }

  // Returns the real stack frame if slot is part of a fake frame, and slot
  // otherwise.
  static StackSlot GetRealStackAddressForSlot(StackSlot slot) {
#ifdef V8_USE_ADDRESS_SANITIZER
    // ASAN fetches the real stack deeper in the __asan_addr_is_in_fake_stack()
    // call (precisely, deeper in __asan_stack_malloc_()), which results in a
    // real frame that could be outside of stack bounds. Adjust for this
    // impreciseness here.
    constexpr size_t kAsanRealFrameOffsetBytes = 32;
    void* real_frame = __asan_addr_is_in_fake_stack(
        __asan_get_current_fake_stack(), slot, nullptr, nullptr);
    return real_frame ? StackSlot(static_cast<char*>(real_frame) +
                                  kAsanRealFrameOffsetBytes)
                      : slot;
#endif  // V8_USE_ADDRESS_SANITIZER
    return slot;
  }

 private:
  // Return the current thread stack start pointer.
  static StackSlot GetStackStartUnchecked();
  static Stack::StackSlot ObtainCurrentThreadStackStart();

  friend v8::internal::HandleHelper;
};

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT
V8_BASE_EXPORT void SetJitWriteProtected(int enable);
#endif

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_PLATFORM_PLATFORM_H_
