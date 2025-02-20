// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code for POSIX goes here. This is not a platform on its
// own, but contains the parts which are the same across the POSIX platforms
// Linux, MacOS, FreeBSD, OpenBSD, NetBSD and QNX.

#include <errno.h>
#include <limits.h>
#include <pthread.h>

#include "src/base/logging.h"
#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#include <pthread_np.h>  // for pthread_set_name_np
#endif
#include <fcntl.h>
#include <sched.h>  // for sched_yield
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__DragonFly__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
#include <sys/sysctl.h>  // for sysctl
#endif

#if defined(ANDROID) && !defined(V8_ANDROID_LOG_STDOUT)
#define LOG_TAG "v8"
#include <android/log.h>
#endif

#include <cmath>
#include <cstdlib>
#include <optional>

#include "src/base/lazy-instance.h"
#include "src/base/macros.h"
#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/utils/random-number-generator.h"

#ifdef V8_FAST_TLS_SUPPORTED
#include <atomic>
#endif

#if V8_OS_DARWIN || V8_OS_LINUX
#include <dlfcn.h>  // for dlsym
#endif

#if V8_OS_DARWIN
#include <mach/mach.h>
#include <malloc/malloc.h>
#elif V8_OS_OPENBSD
#include <sys/malloc.h>
#elif !V8_OS_ZOS
#include <malloc.h>
#endif

#if V8_OS_LINUX
#include <sys/prctl.h>  // for prctl
#endif

#if defined(V8_OS_FUCHSIA)
#include <zircon/process.h>
#else
#include <sys/resource.h>
#endif

#if !defined(_AIX) && !defined(V8_OS_FUCHSIA) && !V8_OS_ZOS
#include <sys/syscall.h>
#endif

#if V8_OS_FREEBSD || V8_OS_DARWIN || V8_OS_OPENBSD || V8_OS_SOLARIS
#define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(V8_OS_SOLARIS)
#if (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE > 2) || defined(__EXTENSIONS__)
extern "C" int madvise(caddr_t, size_t, int);
#else
extern int madvise(caddr_t, size_t, int);
#endif
#endif

#ifndef MADV_FREE
#define MADV_FREE MADV_DONTNEED
#endif

#if defined(V8_LIBC_GLIBC)
extern "C" void* __libc_stack_end;
#endif

namespace v8 {
namespace base {

namespace {

// 0 is never a valid thread id.
#if V8_OS_ZOS
const pthread_t kNoThread = {0};  // pthread_t is a struct on z/OS
#else
const pthread_t kNoThread = static_cast<pthread_t>(0);
#endif

const char* g_gc_fake_mmap = nullptr;

DEFINE_LAZY_LEAKY_OBJECT_GETTER(RandomNumberGenerator,
                                GetPlatformRandomNumberGenerator)
static LazyMutex rng_mutex = LAZY_MUTEX_INITIALIZER;

#if !V8_OS_FUCHSIA && !V8_OS_ZOS
#if V8_OS_DARWIN
// kMmapFd is used to pass vm_alloc flags to tag the region with the user
// defined tag 255 This helps identify V8-allocated regions in memory analysis
// tools like vmmap(1).
const int kMmapFd = VM_MAKE_TAG(255);
#else   // !V8_OS_DARWIN
const int kMmapFd = -1;
#endif  // !V8_OS_DARWIN

#if defined(V8_TARGET_OS_MACOS) && V8_HOST_ARCH_ARM64
// During snapshot generation in cross builds, sysconf() runs on the Intel
// host and returns host page size, while the snapshot needs to use the
// target page size.
constexpr int kAppleArmPageSize = 1 << 14;
#endif

const int kMmapFdOffset = 0;

enum class PageType { kShared, kPrivate };

int GetFlagsForMemoryPermission(OS::MemoryPermission access,
                                PageType page_type) {
  int flags = MAP_ANONYMOUS;
  flags |= (page_type == PageType::kShared) ? MAP_SHARED : MAP_PRIVATE;
  if (access == OS::MemoryPermission::kNoAccess ||
      access == OS::MemoryPermission::kNoAccessWillJitLater) {
#if !V8_OS_AIX && !V8_OS_FREEBSD && !V8_OS_QNX
    flags |= MAP_NORESERVE;
#endif  // !V8_OS_AIX && !V8_OS_FREEBSD && !V8_OS_QNX
#if V8_OS_QNX
    flags |= MAP_LAZY;
#endif  // V8_OS_QNX
  }
#if V8_OS_DARWIN
  // MAP_JIT is required to obtain writable and executable pages when the
  // hardened runtime/memory protection is enabled, which is optional (via code
  // signing) on Intel-based Macs but mandatory on Apple silicon ones. See also
  // https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon.
  if (access == OS::MemoryPermission::kNoAccessWillJitLater ||
      access == OS::MemoryPermission::kReadWriteExecute) {
    flags |= MAP_JIT;
  }
#endif  // V8_OS_DARWIN
  return flags;
}

void* Allocate(void* hint, size_t size, OS::MemoryPermission access,
               PageType page_type) {
  int prot = GetProtectionFromMemoryPermission(access);
  int flags = GetFlagsForMemoryPermission(access, page_type);
  void* result = mmap(hint, size, prot, flags, kMmapFd, kMmapFdOffset);
  if (result == MAP_FAILED) return nullptr;

#if V8_OS_LINUX && V8_ENABLE_PRIVATE_MAPPING_FORK_OPTIMIZATION
  // This is advisory, so we ignore errors.
  madvise(result, size, MADV_DONTFORK);
#endif

#if ENABLE_HUGEPAGE
  if (result != nullptr && size >= kHugePageSize) {
    const uintptr_t huge_start =
        RoundUp(reinterpret_cast<uintptr_t>(result), kHugePageSize);
    const uintptr_t huge_end =
        RoundDown(reinterpret_cast<uintptr_t>(result) + size, kHugePageSize);
    if (huge_end > huge_start) {
      // Bail out in case the aligned addresses do not provide a block of at
      // least kHugePageSize size.
      madvise(reinterpret_cast<void*>(huge_start), huge_end - huge_start,
              MADV_HUGEPAGE);
    }
  }
#endif

  return result;
}

#endif  // !V8_OS_FUCHSIA && !V8_OS_ZOS

}  // namespace

// TODO(v8:10026): Add the right permission flag to make executable pages
// guarded.
int GetProtectionFromMemoryPermission(OS::MemoryPermission access) {
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
    case OS::MemoryPermission::kNoAccessWillJitLater:
      return PROT_NONE;
    case OS::MemoryPermission::kRead:
      return PROT_READ;
    case OS::MemoryPermission::kReadWrite:
      return PROT_READ | PROT_WRITE;
    case OS::MemoryPermission::kReadWriteExecute:
      return PROT_READ | PROT_WRITE | PROT_EXEC;
    case OS::MemoryPermission::kReadExecute:
      return PROT_READ | PROT_EXEC;
  }
  UNREACHABLE();
}

#if V8_OS_LINUX || V8_OS_FREEBSD
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
#endif

void PosixInitializeCommon(AbortMode abort_mode,
                           const char* const gc_fake_mmap) {
  g_abort_mode = abort_mode;
  g_gc_fake_mmap = gc_fake_mmap;
}

#if !V8_OS_FUCHSIA
void OS::Initialize(AbortMode abort_mode, const char* const gc_fake_mmap) {
  PosixInitializeCommon(abort_mode, gc_fake_mmap);
}
#endif  // !V8_OS_FUCHSIA

bool OS::IsHardwareEnforcedShadowStacksEnabled() { return false; }

int OS::ActivationFrameAlignment() {
#if V8_TARGET_ARCH_ARM
  // On EABI ARM targets this is required for fp correctness in the
  // runtime system.
  return 8;
#elif V8_TARGET_ARCH_MIPS
  return 8;
#elif V8_TARGET_ARCH_S390X
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
size_t OS::AllocatePageSize() {
#if defined(V8_TARGET_OS_MACOS) && V8_HOST_ARCH_ARM64
  return kAppleArmPageSize;
#else
  static size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  return page_size;
#endif
}

// static
size_t OS::CommitPageSize() {
  // Commit and allocate page size are the same on posix.
  return OS::AllocatePageSize();
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
  uintptr_t raw_addr;
  {
    MutexGuard guard(rng_mutex.Pointer());
    GetPlatformRandomNumberGenerator()->NextBytes(&raw_addr, sizeof(raw_addr));
  }
#if V8_HOST_ARCH_ARM64
#if defined(V8_TARGET_OS_MACOS)
  DCHECK_EQ(1 << 14, AllocatePageSize());
#endif
  // Keep the address page-aligned, AArch64 supports 4K, 16K and 64K
  // configurations.
  raw_addr = RoundDown(raw_addr, AllocatePageSize());
#endif
#if defined(V8_USE_ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(THREAD_SANITIZER) || defined(LEAK_SANITIZER)
  // If random hint addresses interfere with address ranges hard coded in
  // sanitizers, bad things happen. This address range is copied from TSAN
  // source but works with all tools.
  // See crbug.com/539863.
  raw_addr &= 0x007fffff0000ULL;
  raw_addr += 0x7e8000000000ULL;
#else
#if V8_TARGET_ARCH_X64
  // Currently available CPUs have 48 bits of virtual addressing.  Truncate
  // the hint address to 46 bits to give the kernel a fighting chance of
  // fulfilling our placement request.
  raw_addr &= uint64_t{0x3FFFFFFFF000};
#elif V8_TARGET_ARCH_ARM64
#if defined(V8_TARGET_OS_LINUX) || defined(V8_TARGET_OS_ANDROID)
  // On Linux, the default virtual address space is limited to 39 bits when
  // using 4KB pages, see arch/arm64/Kconfig. We truncate to 38 bits.
  raw_addr &= uint64_t{0x3FFFFFF000};
#else
  // On macOS and elsewhere, we use 46 bits, same as on x64.
  raw_addr &= uint64_t{0x3FFFFFFFF000};
#endif
#elif V8_TARGET_ARCH_PPC64
#if V8_OS_AIX
  // AIX: 64 bits of virtual addressing, but we limit address range to minimize
  // Segment Lookaside Buffer (SLB) misses.
  raw_addr &= uint64_t{0x3FFFF000};
  // Use extra address space to isolate the mmap regions.
  raw_addr += uint64_t{0x400000000000};
#else
  // Little-endian Linux: 46 bits of virtual addressing.
  raw_addr &= uint64_t{0x3FFFFFFF0000};
#endif
#elif V8_TARGET_ARCH_S390X
  // Linux on Z uses bits 22-32 for Region Indexing, which translates to 42 bits
  // of virtual addressing.  Truncate to 40 bits to allow kernel chance to
  // fulfill request.
  raw_addr &= uint64_t{0xFFFFFFF000};
#elif V8_TARGET_ARCH_MIPS64
  // 42 bits of virtual addressing. Truncate to 40 bits to allow kernel chance
  // to fulfill request.
  raw_addr &= uint64_t{0xFFFFFF0000};
#elif V8_TARGET_ARCH_RISCV64
  // TODO(RISCV): We need more information from the kernel to correctly mask
  // this address for RISC-V. https://github.com/v8-riscv/v8/issues/375
  raw_addr &= uint64_t{0xFFFFFF0000};
#elif V8_TARGET_ARCH_RISCV32
  // TODO(RISCV): We need more information from the kernel to correctly mask
  // this address for RISC-V. https://github.com/v8-riscv/v8/issues/375
  raw_addr &= 0x3FFFF000;
#elif V8_TARGET_ARCH_LOONG64
  // 40 or 47 bits of virtual addressing. Truncate to 38 bits to allow kernel
  // chance to fulfill request.
  raw_addr &= uint64_t{0x3FFFFF0000};
#else
  raw_addr &= 0x3FFFF000;

#ifdef __sun
  // For our Solaris/illumos mmap hint, we pick a random address in the bottom
  // half of the top half of the address space (that is, the third quarter).
  // Because we do not MAP_FIXED, this will be treated only as a hint -- the
  // system will not fail to mmap() because something else happens to already
  // be mapped at our random address. We deliberately set the hint high enough
  // to get well above the system's break (that is, the heap); Solaris and
  // illumos will try the hint and if that fails allocate as if there were
  // no hint at all. The high hint prevents the break from getting hemmed in
  // at low values, ceding half of the address space to the system heap.
  raw_addr += 0x80000000;
#elif V8_OS_AIX
  // The range 0x30000000 - 0xD0000000 is available on AIX;
  // choose the upper range.
  raw_addr += 0x90000000;
#else
  // The range 0x20000000 - 0x60000000 is relatively unpopulated across a
  // variety of ASLR modes (PAE kernel, NX compat mode, etc) and on macos
  // 10.6 and 10.7.
  raw_addr += 0x20000000;
#endif
#endif
#endif
  return reinterpret_cast<void*>(raw_addr);
}

// TODO(bbudge) Move Cygwin and Fuchsia stuff into platform-specific files.
#if !V8_OS_CYGWIN && !V8_OS_FUCHSIA
#if !V8_OS_ZOS
// static
void* OS::Allocate(void* hint, size_t size, size_t alignment,
                   MemoryPermission access) {
  size_t page_size = AllocatePageSize();
  DCHECK_EQ(0, size % page_size);
  DCHECK_EQ(0, alignment % page_size);
  hint = AlignedAddress(hint, alignment);
  // Add the maximum misalignment so we are guaranteed an aligned base address.
  size_t request_size = size + (alignment - page_size);
  request_size = RoundUp(request_size, OS::AllocatePageSize());
  void* result = base::Allocate(hint, request_size, access, PageType::kPrivate);
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
void* OS::AllocateShared(size_t size, MemoryPermission access) {
  DCHECK_EQ(0, size % AllocatePageSize());
  return base::Allocate(nullptr, size, access, PageType::kShared);
}

// static
void OS::Free(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % AllocatePageSize());
  DCHECK_EQ(0, size % AllocatePageSize());
  CHECK_EQ(0, munmap(address, size));
}

// Darwin specific implementation in platform-darwin.cc.
#if !defined(V8_OS_DARWIN)
// static
void* OS::AllocateShared(void* hint, size_t size, MemoryPermission access,
                         PlatformSharedMemoryHandle handle, uint64_t offset) {
  DCHECK_EQ(0, size % AllocatePageSize());
  int prot = GetProtectionFromMemoryPermission(access);
  int fd = FileDescriptorFromSharedMemoryHandle(handle);
  void* result = mmap(hint, size, prot, MAP_SHARED, fd, offset);
  if (result == MAP_FAILED) return nullptr;
  return result;
}
#endif  // !defined(V8_OS_DARWIN)

// static
void OS::FreeShared(void* address, size_t size) {
  DCHECK_EQ(0, size % AllocatePageSize());
  CHECK_EQ(0, munmap(address, size));
}

// static
void OS::Release(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  CHECK_EQ(0, munmap(address, size));
}

// static
bool OS::SetPermissions(void* address, size_t size, MemoryPermission access) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());

  int prot = GetProtectionFromMemoryPermission(access);
  int ret = mprotect(address, size, prot);

  // Setting permissions can fail if the limit of VMAs is exceeded.
  // Any failure that's not OOM likely indicates a bug in the caller (e.g.
  // using an invalid mapping) so attempt to catch that here to facilitate
  // debugging of these failures.
  if (ret != 0) CHECK_EQ(ENOMEM, errno);

  // MacOS 11.2 on Apple Silicon refuses to switch permissions from
  // rwx to none. Just use madvise instead.
#if defined(V8_OS_DARWIN)
  if (ret != 0 && access == OS::MemoryPermission::kNoAccess) {
    ret = madvise(address, size, MADV_FREE_REUSABLE);
    return ret == 0;
  }
#endif

  if (ret == 0 && access == OS::MemoryPermission::kNoAccess) {
    // This is advisory; ignore errors and continue execution.
    USE(DiscardSystemPages(address, size));
  }

// For accounting purposes, we want to call MADV_FREE_REUSE on macOS after
// changing permissions away from OS::MemoryPermission::kNoAccess. Since this
// state is not kept at this layer, we always call this if access != kNoAccess.
// The cost is a syscall that effectively no-ops.
// TODO(erikchen): Fix this to only call MADV_FREE_REUSE when necessary.
// https://crbug.com/823915
#if defined(V8_OS_DARWIN)
  if (access != OS::MemoryPermission::kNoAccess) {
    madvise(address, size, MADV_FREE_REUSE);
  }
#endif

  return ret == 0;
}

// static
void OS::SetDataReadOnly(void* address, size_t size) {
  CHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  CHECK_EQ(0, size % CommitPageSize());

  if (mprotect(address, size, PROT_READ) != 0) {
    FATAL("Failed to protect data memory at %p +%zu; error %d\n", address, size,
          errno);
  }
}

// static
bool OS::RecommitPages(void* address, size_t size, MemoryPermission access) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());

#if defined(V8_OS_DARWIN)
  while (madvise(address, size, MADV_FREE_REUSE) == -1 && errno == EAGAIN) {
  }
#endif  // defined(V8_OS_DARWIN)
  return true;
}

// static
bool OS::DiscardSystemPages(void* address, size_t size) {
  // Roughly based on PartitionAlloc's DiscardSystemPagesInternal
  // (base/allocator/partition_allocator/page_allocator_internals_posix.h)
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
#if defined(V8_OS_DARWIN)
  // On OSX, MADV_FREE_REUSABLE has comparable behavior to MADV_FREE, but also
  // marks the pages with the reusable bit, which allows both Activity Monitor
  // and memory-infra to correctly track the pages.
  int ret;
  do {
    ret = madvise(address, size, MADV_FREE_REUSABLE);
  } while (ret != 0 && errno == EAGAIN);
  if (ret) {
    // MADV_FREE_REUSABLE sometimes fails, so fall back to MADV_DONTNEED.
    ret = madvise(address, size, MADV_DONTNEED);
  }
#elif defined(_AIX) || defined(V8_OS_SOLARIS)
  int ret = madvise(reinterpret_cast<caddr_t>(address), size, MADV_FREE);
  if (ret != 0 && errno == ENOSYS) {
    return true;  // madvise is not available on all systems.
  }
  if (ret != 0 && errno == EINVAL) {
    ret = madvise(reinterpret_cast<caddr_t>(address), size, MADV_DONTNEED);
  }
#else
  int ret = madvise(address, size, MADV_DONTNEED);
#endif
  // madvise with MADV_DONTNEED only fails on illegal parameters. That's a bug
  // in the caller.
  CHECK_EQ(0, ret);
  return true;
}

#if !defined(_AIX)
// See AIX version for details.
// static
bool OS::DecommitPages(void* address, size_t size) {
  DCHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());
  DCHECK_EQ(0, size % CommitPageSize());
  // From https://pubs.opengroup.org/onlinepubs/9699919799/functions/mmap.html:
  // "If a MAP_FIXED request is successful, then any previous mappings [...] for
  // those whole pages containing any part of the address range [pa,pa+len)
  // shall be removed, as if by an appropriate call to munmap(), before the new
  // mapping is established." As a consequence, the memory will be
  // zero-initialized on next access.
  void* ret = mmap(address, size, PROT_NONE,
                   MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (V8_UNLIKELY(ret == MAP_FAILED)) {
    // Decommitting pages can fail if the limit of VMAs is exceeded.
    CHECK_EQ(ENOMEM, errno);
    return false;
  }
  CHECK_EQ(ret, address);
  return true;
}
#endif  // !defined(_AIX)
#endif  // !V8_OS_ZOS

// static
bool OS::SealPages(void* address, size_t size) {
#ifdef V8_ENABLE_MEMORY_SEALING
#if V8_OS_LINUX && defined(__NR_mseal)
  long ret = syscall(__NR_mseal, address, size, 0);
  return ret == 0;
#else
  return false;
#endif
#else  // V8_ENABLE_MEMORY_SEALING
  return false;
#endif
}

// static
bool OS::CanReserveAddressSpace() { return true; }

// static
std::optional<AddressSpaceReservation> OS::CreateAddressSpaceReservation(
    void* hint, size_t size, size_t alignment,
    MemoryPermission max_permission) {
  // On POSIX, address space reservations are backed by private memory mappings.
  MemoryPermission permission = MemoryPermission::kNoAccess;
  if (max_permission == MemoryPermission::kReadWriteExecute) {
    permission = MemoryPermission::kNoAccessWillJitLater;
  }

  void* reservation = Allocate(hint, size, alignment, permission);
  if (!reservation && permission == MemoryPermission::kNoAccessWillJitLater) {
    // Retry without MAP_JIT, for example in case we are running on an old OS X.
    permission = MemoryPermission::kNoAccess;
    reservation = Allocate(hint, size, alignment, permission);
  }

  if (!reservation) return {};

  return AddressSpaceReservation(reservation, size);
}

// static
void OS::FreeAddressSpaceReservation(AddressSpaceReservation reservation) {
  Free(reservation.base(), reservation.size());
}

// Darwin specific implementation in platform-darwin.cc.
#if !defined(V8_OS_DARWIN)
// static
// Need to disable CFI_ICALL due to the indirect call to memfd_create.
DISABLE_CFI_ICALL
PlatformSharedMemoryHandle OS::CreateSharedMemoryHandleForTesting(size_t size) {
#if V8_OS_LINUX && !V8_OS_ANDROID
  // Use memfd_create if available, otherwise mkstemp.
  using memfd_create_t = int (*)(const char*, unsigned int);
  memfd_create_t memfd_create =
      reinterpret_cast<memfd_create_t>(dlsym(RTLD_DEFAULT, "memfd_create"));
  int fd = -1;
  if (memfd_create) {
    fd = memfd_create("V8MemFDForTesting", 0);
  }
  if (fd == -1) {
    char filename[] = "/tmp/v8_tmp_file_for_testing_XXXXXX";
    fd = mkstemp(filename);
    if (fd != -1) CHECK_EQ(0, unlink(filename));
  }
  if (fd == -1) return kInvalidSharedMemoryHandle;
  CHECK_EQ(0, ftruncate(fd, size));
  return SharedMemoryHandleFromFileDescriptor(fd);
#else
  return kInvalidSharedMemoryHandle;
#endif
}

// static
void OS::DestroySharedMemoryHandle(PlatformSharedMemoryHandle handle) {
  DCHECK_NE(kInvalidSharedMemoryHandle, handle);
  int fd = FileDescriptorFromSharedMemoryHandle(handle);
  CHECK_EQ(0, close(fd));
}
#endif  // !defined(V8_OS_DARWIN)

#if !V8_OS_ZOS
// static
bool OS::HasLazyCommits() {
#if V8_OS_AIX || V8_OS_LINUX || V8_OS_DARWIN
  return true;
#else
  // TODO(bbudge) Return true for all POSIX platforms.
  return false;
#endif
}
#endif  // !V8_OS_ZOS
#endif  // !V8_OS_CYGWIN && !V8_OS_FUCHSIA

const char* OS::GetGCFakeMMapFile() {
  return g_gc_fake_mmap;
}


void OS::Sleep(TimeDelta interval) {
  usleep(static_cast<useconds_t>(interval.InMicroseconds()));
}


void OS::Abort() {
  switch (g_abort_mode) {
    case AbortMode::kExitWithSuccessAndIgnoreDcheckFailures:
      _exit(0);
    case AbortMode::kExitWithFailureAndIgnoreDcheckFailures:
      _exit(-1);
    case AbortMode::kImmediateCrash:
      IMMEDIATE_CRASH();
    case AbortMode::kDefault:
      break;
  }
  // Redirect to std abort to signal abnormal program termination.
  abort();
}


void OS::DebugBreak() {
#if V8_HOST_ARCH_ARM
  asm("bkpt 0");
#elif V8_HOST_ARCH_ARM64
  asm("brk 0");
#elif V8_HOST_ARCH_MIPS
  asm("break");
#elif V8_HOST_ARCH_MIPS64
  asm("break");
#elif V8_HOST_ARCH_LOONG64
  asm("break 0");
#elif V8_HOST_ARCH_PPC64
  asm("twge 2,2");
#elif V8_HOST_ARCH_IA32
  asm("int $3");
#elif V8_HOST_ARCH_X64
  asm("int $3");
#elif V8_OS_ZOS
  asm(" dc x'0001'");
#elif V8_HOST_ARCH_S390X
  // Software breakpoint instruction is 0x0001
  asm volatile(".word 0x0001");
#elif V8_HOST_ARCH_RISCV64
  asm("ebreak");
#elif V8_HOST_ARCH_RISCV32
  asm("ebreak");
#else
#error Unsupported host architecture.
#endif
}

#if !V8_OS_ZOS
class PosixMemoryMappedFile final : public OS::MemoryMappedFile {
 public:
  PosixMemoryMappedFile(FILE* file, void* memory, size_t size)
      : file_(file), memory_(memory), size_(size) {}
  ~PosixMemoryMappedFile() final;
  void* memory() const final { return memory_; }
  size_t size() const final { return size_; }

 private:
  FILE* const file_;
  void* const memory_;
  size_t const size_;
};


// static
OS::MemoryMappedFile* OS::MemoryMappedFile::open(const char* name,
                                                 FileMode mode) {
  const char* fopen_mode = (mode == FileMode::kReadOnly) ? "r" : "r+";
  struct stat statbuf;
  // Make sure path exists and is not a directory.
  if (stat(name, &statbuf) == 0 && !S_ISDIR(statbuf.st_mode)) {
    if (FILE* file = fopen(name, fopen_mode)) {
      if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);  // NOLINT(runtime/int)
        if (size == 0) return new PosixMemoryMappedFile(file, nullptr, 0);
        if (size > 0) {
          int prot = PROT_READ;
          int flags = MAP_PRIVATE;
          if (mode == FileMode::kReadWrite) {
            prot |= PROT_WRITE;
            flags = MAP_SHARED;
          }
          void* const memory =
              mmap(OS::GetRandomMmapAddr(), size, prot, flags, fileno(file), 0);
          if (memory != MAP_FAILED) {
            return new PosixMemoryMappedFile(file, memory, size);
          }
        }
      }
      fclose(file);
    }
  }
  return nullptr;
}

// static
OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name,
                                                   size_t size, void* initial) {
  if (FILE* file = fopen(name, "w+")) {
    if (size == 0) return new PosixMemoryMappedFile(file, nullptr, 0);
    size_t result = fwrite(initial, 1, size, file);
    if (result == size && !ferror(file)) {
      void* memory = mmap(OS::GetRandomMmapAddr(), result,
                          PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
      if (memory != MAP_FAILED) {
        return new PosixMemoryMappedFile(file, memory, result);
      }
    }
    fclose(file);
  }
  return nullptr;
}


PosixMemoryMappedFile::~PosixMemoryMappedFile() {
  if (memory_) OS::Free(memory_, RoundUp(size_, OS::AllocatePageSize()));
  fclose(file_);
}
#endif  // !V8_OS_ZOS

int OS::GetCurrentProcessId() {
  return static_cast<int>(getpid());
}


int OS::GetCurrentThreadId() {
#if V8_OS_DARWIN || (V8_OS_ANDROID && defined(__APPLE__))
  return static_cast<int>(pthread_mach_thread_np(pthread_self()));
#elif V8_OS_LINUX
  return static_cast<int>(syscall(__NR_gettid));
#elif V8_OS_ANDROID
  return static_cast<int>(gettid());
#elif V8_OS_AIX
  return static_cast<int>(thread_self());
#elif V8_OS_FUCHSIA
  return static_cast<int>(zx_thread_self());
#elif V8_OS_SOLARIS
  return static_cast<int>(pthread_self());
#elif V8_OS_ZOS
  return gettid();
#else
  return static_cast<int>(reinterpret_cast<intptr_t>(pthread_self()));
#endif
}

void OS::ExitProcess(int exit_code) {
  // Use _exit instead of exit to avoid races between isolate
  // threads and static destructors.
  fflush(stdout);
  fflush(stderr);
  _exit(exit_code);
}

// ----------------------------------------------------------------------------
// POSIX date/time support.
//

#if !defined(V8_OS_FUCHSIA)
int OS::GetUserTime(uint32_t* secs, uint32_t* usecs) {
  struct rusage usage;

  if (getrusage(RUSAGE_SELF, &usage) < 0) return -1;
  *secs = static_cast<uint32_t>(usage.ru_utime.tv_sec);
  *usecs = static_cast<uint32_t>(usage.ru_utime.tv_usec);
  return 0;
}
#endif

int OS::GetPeakMemoryUsageKb() {
#if defined(V8_OS_FUCHSIA)
  // Fuchsia does not implement getrusage()
  return -1;
#elif defined(V8_OS_ZOS)
  // TODO(v8:342445981): zos - rusage struct doesn't yet include ru_maxrss
  return -1;
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) < 0) return -1;

#if defined(V8_OS_MACOS) || defined(V8_OS_IOS)
  constexpr int KB = 1024;
  // MacOS and iOS ru_maxrss count bytes
  return static_cast<int>(usage.ru_maxrss / KB);
#else
  // Most other cases (at least Linux, IOS, return kilobytes)
  return static_cast<int>(usage.ru_maxrss);
#endif  // defined(V8_OS_MACOS) || defined(V8_OS_IOS)
#endif  // defined(V8_OS_FUCHSIA)
}

double OS::TimeCurrentMillis() {
  return Time::Now().ToJsTime();
}

double PosixTimezoneCache::DaylightSavingsOffset(double time) {
  if (std::isnan(time)) return std::numeric_limits<double>::quiet_NaN();
  time_t tv = static_cast<time_t>(std::floor(time/msPerSecond));
  struct tm tm;
  struct tm* t = localtime_r(&tv, &tm);
  if (nullptr == t) return std::numeric_limits<double>::quiet_NaN();
  return t->tm_isdst > 0 ? 3600 * msPerSecond : 0;
}


int OS::GetLastError() {
  return errno;
}


// ----------------------------------------------------------------------------
// POSIX stdio support.
//

FILE* OS::FOpen(const char* path, const char* mode) {
  FILE* file = fopen(path, mode);
  if (file == nullptr) return nullptr;
  struct stat file_stat;
  if (fstat(fileno(file), &file_stat) != 0) {
    fclose(file);
    return nullptr;
  }
  bool is_regular_file = ((file_stat.st_mode & S_IFREG) != 0);
  if (is_regular_file) return file;
  fclose(file);
  return nullptr;
}


bool OS::Remove(const char* path) {
  return (remove(path) == 0);
}

char OS::DirectorySeparator() { return '/'; }

bool OS::isDirectorySeparator(const char ch) {
  return ch == DirectorySeparator();
}


FILE* OS::OpenTemporaryFile() {
  return tmpfile();
}

const char* const OS::LogFileOpenMode = "w+";

void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrint(format, args);
  va_end(args);
}


void OS::VPrint(const char* format, va_list args) {
#if defined(ANDROID) && !defined(V8_ANDROID_LOG_STDOUT)
  __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, format, args);
#else
  vprintf(format, args);
#endif
}


void OS::FPrint(FILE* out, const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(out, format, args);
  va_end(args);
}


void OS::VFPrint(FILE* out, const char* format, va_list args) {
#if defined(ANDROID) && !defined(V8_ANDROID_LOG_STDOUT)
  if (out == stdout) {
    __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, format, args);
    return;
  }
#endif
  vfprintf(out, format, args);
}


void OS::PrintError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VPrintError(format, args);
  va_end(args);
  fflush(stderr);
}


void OS::VPrintError(const char* format, va_list args) {
#if defined(ANDROID) && !defined(V8_ANDROID_LOG_STDOUT)
  __android_log_vprint(ANDROID_LOG_ERROR, LOG_TAG, format, args);
#else
  vfprintf(stderr, format, args);
#endif
}


int OS::SNPrintF(char* str, int length, const char* format, ...) {
  va_list args;
  va_start(args, format);
  int result = VSNPrintF(str, length, format, args);
  va_end(args);
  return result;
}


int OS::VSNPrintF(char* str,
                  int length,
                  const char* format,
                  va_list args) {
  int n = vsnprintf(str, length, format, args);
  if (n < 0 || n >= length) {
    // If the length is zero, the assignment fails.
    if (length > 0)
      str[length - 1] = '\0';
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
// POSIX Address space reservation support.
//

#if !V8_OS_CYGWIN && !V8_OS_FUCHSIA

std::optional<AddressSpaceReservation>
AddressSpaceReservation::CreateSubReservation(
    void* address, size_t size, OS::MemoryPermission max_permission) {
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

bool AddressSpaceReservation::Allocate(void* address, size_t size,
                                       OS::MemoryPermission access) {
  // The region is already mmap'ed, so it just has to be made accessible now.
  DCHECK(Contains(address, size));
  if (access == OS::MemoryPermission::kNoAccess) {
    // Nothing to do. We don't want to call SetPermissions with kNoAccess here
    // as that will for example mark the pages as discardable, which is
    // probably not desired here.
    return true;
  }
  return OS::SetPermissions(address, size, access);
}

bool AddressSpaceReservation::Free(void* address, size_t size) {
  DCHECK(Contains(address, size));
  return OS::DecommitPages(address, size);
}

// z/OS specific implementation in platform-zos.cc.
#if !defined(V8_OS_ZOS)
// Darwin specific implementation in platform-darwin.cc.
#if !defined(V8_OS_DARWIN)
bool AddressSpaceReservation::AllocateShared(void* address, size_t size,
                                             OS::MemoryPermission access,
                                             PlatformSharedMemoryHandle handle,
                                             uint64_t offset) {
  DCHECK(Contains(address, size));
  int prot = GetProtectionFromMemoryPermission(access);
  int fd = FileDescriptorFromSharedMemoryHandle(handle);
  return mmap(address, size, prot, MAP_SHARED | MAP_FIXED, fd, offset) !=
         MAP_FAILED;
}
#endif  // !defined(V8_OS_DARWIN)

bool AddressSpaceReservation::FreeShared(void* address, size_t size) {
  DCHECK(Contains(address, size));
  return mmap(address, size, PROT_NONE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE,
              -1, 0) == address;
}
#endif  // !V8_OS_ZOS

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

#endif  // !V8_OS_CYGWIN && !V8_OS_FUCHSIA

// ----------------------------------------------------------------------------
// POSIX thread support.
//

class Thread::PlatformData {
 public:
  PlatformData() : thread_(kNoThread) {}
  pthread_t thread_;  // Thread handle for pthread.
  // Synchronizes thread creation
  Mutex thread_creation_mutex_;
};

Thread::Thread(const Options& options)
    : data_(new PlatformData),
      stack_size_(options.stack_size()),
      priority_(options.priority()),
      start_semaphore_(nullptr) {
  const int min_stack_size = static_cast<int>(PTHREAD_STACK_MIN);
  if (stack_size_ > 0) stack_size_ = std::max(stack_size_, min_stack_size);
  set_name(options.name());
}

Thread::~Thread() {
  delete data_;
}


static void SetThreadName(const char* name) {
#if V8_OS_DRAGONFLYBSD || V8_OS_FREEBSD || V8_OS_OPENBSD
  pthread_set_name_np(pthread_self(), name);
#elif V8_OS_NETBSD
  static_assert(Thread::kMaxThreadNameLength <= PTHREAD_MAX_NAMELEN_NP);
  pthread_setname_np(pthread_self(), "%s", name);
#elif V8_OS_DARWIN
  // pthread_setname_np is only available in 10.6 or later, so test
  // for it at runtime.
  int (*dynamic_pthread_setname_np)(const char*);
  *reinterpret_cast<void**>(&dynamic_pthread_setname_np) =
    dlsym(RTLD_DEFAULT, "pthread_setname_np");
  if (dynamic_pthread_setname_np == nullptr) return;

  // Mac OS X does not expose the length limit of the name, so hardcode it.
  static const int kMaxNameLength = 63;
  static_assert(Thread::kMaxThreadNameLength <= kMaxNameLength);
  dynamic_pthread_setname_np(name);
#elif defined(PR_SET_NAME)
  prctl(PR_SET_NAME,
        reinterpret_cast<unsigned long>(name),  // NOLINT
        0, 0, 0);
#endif
}

static void* ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);
  // We take the lock here to make sure that pthread_create finished first since
  // we don't know which thread will run first (the original thread or the new
  // one).
  { MutexGuard lock_guard(&thread->data()->thread_creation_mutex_); }
  SetThreadName(thread->name());
#if V8_OS_DARWIN
  switch (thread->priority()) {
    case Thread::Priority::kBestEffort:
      pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
      break;
    case Thread::Priority::kUserVisible:
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, -1);
      break;
    case Thread::Priority::kUserBlocking:
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
      break;
    case Thread::Priority::kDefault:
      break;
  }
#elif V8_OS_LINUX || V8_OS_ZOS
  switch (thread->priority()) {
    case Thread::Priority::kBestEffort:
      setpriority(PRIO_PROCESS, 0, 10);
      break;
    case Thread::Priority::kUserVisible:
      setpriority(PRIO_PROCESS, 0, 1);
      break;
    case Thread::Priority::kUserBlocking:
      setpriority(PRIO_PROCESS, 0, 0);
      break;
    case Thread::Priority::kDefault:
      break;
  }
#endif
  DCHECK_NE(thread->data()->thread_, kNoThread);
  thread->NotifyStartedAndRun();
  return nullptr;
}


void Thread::set_name(const char* name) {
  strncpy(name_, name, sizeof(name_) - 1);
  name_[sizeof(name_) - 1] = '\0';
}

bool Thread::Start() {
  int result;
  pthread_attr_t attr;
  memset(&attr, 0, sizeof(attr));
  result = pthread_attr_init(&attr);
  if (result != 0) return false;
  size_t stack_size = stack_size_;
  if (stack_size == 0) {
#if V8_OS_DARWIN
    // Default on Mac OS X is 512kB -- bump up to 1MB
    stack_size = 1 * 1024 * 1024;
#elif V8_OS_AIX
    // Default on AIX is 96kB -- bump up to 2MB
    stack_size = 2 * 1024 * 1024;
#endif
  }
  if (stack_size > 0) {
    result = pthread_attr_setstacksize(&attr, stack_size);
    if (result != 0) return pthread_attr_destroy(&attr), false;
  }
  {
    MutexGuard lock_guard(&data_->thread_creation_mutex_);
    result = pthread_create(&data_->thread_, &attr, ThreadEntry, this);
    if (result != 0 || data_->thread_ == kNoThread) {
      return pthread_attr_destroy(&attr), false;
    }
  }
  result = pthread_attr_destroy(&attr);
  return result == 0;
}

void Thread::Join() { pthread_join(data_->thread_, nullptr); }

static Thread::LocalStorageKey PthreadKeyToLocalKey(pthread_key_t pthread_key) {
#if V8_OS_CYGWIN
  // We need to cast pthread_key_t to Thread::LocalStorageKey in two steps
  // because pthread_key_t is a pointer type on Cygwin. This will probably not
  // work on 64-bit platforms, but Cygwin doesn't support 64-bit anyway.
  static_assert(sizeof(Thread::LocalStorageKey) == sizeof(pthread_key_t));
  intptr_t ptr_key = reinterpret_cast<intptr_t>(pthread_key);
  return static_cast<Thread::LocalStorageKey>(ptr_key);
#else
  return static_cast<Thread::LocalStorageKey>(pthread_key);
#endif
}


static pthread_key_t LocalKeyToPthreadKey(Thread::LocalStorageKey local_key) {
#if V8_OS_CYGWIN
  static_assert(sizeof(Thread::LocalStorageKey) == sizeof(pthread_key_t));
  intptr_t ptr_key = static_cast<intptr_t>(local_key);
  return reinterpret_cast<pthread_key_t>(ptr_key);
#else
  return static_cast<pthread_key_t>(local_key);
#endif
}

#if defined(V8_FAST_TLS_SUPPORTED) && defined(DEBUG)

static void CheckFastTls(Thread::LocalStorageKey key) {
  void* expected = reinterpret_cast<void*>(0x1234CAFE);
  Thread::SetThreadLocal(key, expected);
  void* actual = Thread::GetExistingThreadLocal(key);
  if (expected != actual) {
    FATAL("V8 failed to initialize fast TLS on current kernel");
  }
  Thread::SetThreadLocal(key, nullptr);
}

#endif  // defined(V8_FAST_TLS_SUPPORTED) && defined(DEBUG)

Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  pthread_key_t key;
  int result = pthread_key_create(&key, nullptr);
  DCHECK_EQ(0, result);
  USE(result);
  LocalStorageKey local_key = PthreadKeyToLocalKey(key);
#if defined(V8_FAST_TLS_SUPPORTED) && defined(DEBUG)
  CheckFastTls(local_key);
#endif
  return local_key;
}

void Thread::DeleteThreadLocalKey(LocalStorageKey key) {
  pthread_key_t pthread_key = LocalKeyToPthreadKey(key);
  int result = pthread_key_delete(pthread_key);
  DCHECK_EQ(0, result);
  USE(result);
}


void* Thread::GetThreadLocal(LocalStorageKey key) {
  pthread_key_t pthread_key = LocalKeyToPthreadKey(key);
  return pthread_getspecific(pthread_key);
}


void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  pthread_key_t pthread_key = LocalKeyToPthreadKey(key);
  int result = pthread_setspecific(pthread_key, value);
  DCHECK_EQ(0, result);
  USE(result);
}

// pthread_getattr_np used below is non portable (hence the _np suffix). We
// keep this version in POSIX as most Linux-compatible derivatives will
// support it. MacOS and FreeBSD are different here.
#if !defined(V8_OS_FREEBSD) && !defined(V8_OS_DARWIN) && !defined(_AIX) && \
    !defined(V8_OS_SOLARIS)

namespace {
#if DEBUG
bool MainThreadIsCurrentThread() {
  // This method assumes the first time is called is from the main thread.
  // It returns true for subsequent calls only if they are called from the
  // same thread.
  static int main_thread_id = -1;
  if (main_thread_id == -1) {
    main_thread_id = OS::GetCurrentThreadId();
  }
  return main_thread_id == OS::GetCurrentThreadId();
}
#endif  // DEBUG
}  // namespace

// static
Stack::StackSlot Stack::ObtainCurrentThreadStackStart() {
#if V8_OS_ZOS
  return __get_stack_start();
#elif V8_OS_OPENBSD
  stack_t stack;
  int error = pthread_stackseg_np(pthread_self(), &stack);
  if(error) {
    DCHECK(MainThreadIsCurrentThread());
    return nullptr;
  }
  void* stack_start = reinterpret_cast<uint8_t*>(stack.ss_sp) + stack.ss_size;
  return stack_start;
#else
  pthread_attr_t attr;
  int error = pthread_getattr_np(pthread_self(), &attr);
  if (error) {
    DCHECK(MainThreadIsCurrentThread());
#if defined(V8_LIBC_GLIBC)
    // pthread_getattr_np can fail for the main thread.
    // For the main thread we prefer using __libc_stack_end (if it exists) since
    // it generally provides a tighter limit for CSS.
    return __libc_stack_end;
#else
    return nullptr;
#endif  // !defined(V8_LIBC_GLIBC)
  }
  void* base;
  size_t size;
  error = pthread_attr_getstack(&attr, &base, &size);
  CHECK(!error);
  pthread_attr_destroy(&attr);
  void* stack_start = reinterpret_cast<uint8_t*>(base) + size;
#if defined(V8_LIBC_GLIBC)
  // __libc_stack_end is process global and thus is only valid for
  // the main thread. Check whether this is the main thread by checking
  // __libc_stack_end is within the thread's stack.
  if ((base <= __libc_stack_end) && (__libc_stack_end <= stack_start)) {
    DCHECK(MainThreadIsCurrentThread());
    return __libc_stack_end;
  }
#endif  // !defined(V8_LIBC_GLIBC)
  return stack_start;
#endif  // V8_OS_ZOS
}

#endif  // !defined(V8_OS_FREEBSD) && !defined(V8_OS_DARWIN) &&
        // !defined(_AIX) && !defined(V8_OS_SOLARIS)

// static
Stack::StackSlot Stack::GetCurrentStackPosition() {
  return __builtin_frame_address(0);
}

#undef LOG_TAG
#undef MAP_ANONYMOUS
#undef MADV_FREE

}  // namespace base
}  // namespace v8
