// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Platform-specific code shared between macOS and iOS goes here. The
// POSIX-compatible parts in platform-posix.cc.

#include <AvailabilityMacros.h>
#include <dlfcn.h>
#include <errno.h>
#include <libkern/OSAtomic.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/semaphore.h>
#include <mach/task.h>
#include <mach/vm_map.h>
#include <mach/vm_statistics.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <cmath>

#undef MAP_TYPE

#include "src/base/macros.h"
#include "src/base/platform/platform-posix-time.h"
#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"

#if defined(V8_TARGET_OS_IOS)
#include "src/base/ios-headers.h"
#else
#include <mach/mach_vm.h>
#endif

namespace v8 {
namespace base {

namespace {

vm_prot_t GetVMProtFromMemoryPermission(OS::MemoryPermission access) {
  switch (access) {
    case OS::MemoryPermission::kNoAccess:
    case OS::MemoryPermission::kNoAccessWillJitLater:
      return VM_PROT_NONE;
    case OS::MemoryPermission::kRead:
      return VM_PROT_READ;
    case OS::MemoryPermission::kReadWrite:
      return VM_PROT_READ | VM_PROT_WRITE;
    case OS::MemoryPermission::kReadWriteExecute:
      return VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
    case OS::MemoryPermission::kReadExecute:
      return VM_PROT_READ | VM_PROT_EXECUTE;
  }
  UNREACHABLE();
}

kern_return_t mach_vm_map_wrapper(mach_vm_address_t* address,
                                  mach_vm_size_t size, int flags,
                                  mach_port_t port,
                                  memory_object_offset_t offset,
                                  vm_prot_t prot) {
  vm_prot_t current_prot = prot;
  vm_prot_t maximum_prot = current_prot;
  return mach_vm_map(mach_task_self(), address, size, 0, flags, port, offset,
                     FALSE, current_prot, maximum_prot, VM_INHERIT_NONE);
}

}  // namespace

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  std::vector<SharedLibraryAddress> result;
  unsigned int images_count = _dyld_image_count();
  for (unsigned int i = 0; i < images_count; ++i) {
    const mach_header* header = _dyld_get_image_header(i);
    if (header == nullptr) continue;
    unsigned long size;
#if V8_HOST_ARCH_I32
    uint8_t* code_ptr = getsectiondata(header, SEG_TEXT, SECT_TEXT, &size);
#else
    const mach_header_64* header64 =
        reinterpret_cast<const mach_header_64*>(header);
    uint8_t* code_ptr = getsectiondata(header64, SEG_TEXT, SECT_TEXT, &size);
#endif
    if (code_ptr == nullptr) continue;
    const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
    const uintptr_t start = reinterpret_cast<uintptr_t>(code_ptr);
    result.push_back(SharedLibraryAddress(_dyld_get_image_name(i), start,
                                          start + size, slide));
  }
  return result;
}

void OS::SignalCodeMovingGC() {}

TimezoneCache* OS::CreateTimezoneCache() {
  return new PosixDefaultTimezoneCache();
}

void OS::AdjustSchedulingParams() {
#if V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32
  {
    // Check availability of scheduling params.
    uint32_t val = 0;
    size_t valSize = sizeof(val);
    int rc = sysctlbyname("kern.tcsm_available", &val, &valSize, NULL, 0);
    if (rc < 0 || !val) return;
  }

  {
    // Adjust scheduling params.
    uint32_t val = 1;
    int rc = sysctlbyname("kern.tcsm_enable", NULL, NULL, &val, sizeof(val));
    DCHECK_GE(rc, 0);
    USE(rc);
  }
#endif
}

std::optional<OS::MemoryRange> OS::GetFirstFreeMemoryRangeWithin(
    OS::Address boundary_start, OS::Address boundary_end, size_t minimum_size,
    size_t alignment) {
  return std::nullopt;
}

// static
Stack::StackSlot Stack::ObtainCurrentThreadStackStart() {
  return pthread_get_stackaddr_np(pthread_self());
}

// static
PlatformSharedMemoryHandle OS::CreateSharedMemoryHandleForTesting(size_t size) {
  mach_vm_size_t vm_size = size;
  mach_port_t port;
  kern_return_t kr = mach_make_memory_entry_64(
      mach_task_self(), &vm_size, 0,
      MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE, &port,
      MACH_PORT_NULL);
  if (kr != KERN_SUCCESS) return kInvalidSharedMemoryHandle;
  return SharedMemoryHandleFromMachMemoryEntry(port);
}

// static
void OS::DestroySharedMemoryHandle(PlatformSharedMemoryHandle handle) {
  DCHECK_NE(kInvalidSharedMemoryHandle, handle);
  mach_port_t port = MachMemoryEntryFromSharedMemoryHandle(handle);
  CHECK_EQ(KERN_SUCCESS, mach_port_deallocate(mach_task_self(), port));
}

// static
void* OS::AllocateShared(void* hint, size_t size, MemoryPermission access,
                         PlatformSharedMemoryHandle handle, uint64_t offset) {
  DCHECK_EQ(0, size % AllocatePageSize());

  mach_vm_address_t addr = reinterpret_cast<mach_vm_address_t>(hint);
  vm_prot_t prot = GetVMProtFromMemoryPermission(access);
  mach_port_t shared_mem_port = MachMemoryEntryFromSharedMemoryHandle(handle);
  kern_return_t kr = mach_vm_map_wrapper(&addr, size, VM_FLAGS_FIXED,
                                         shared_mem_port, offset, prot);

  if (kr != KERN_SUCCESS) {
    // Retry without hint.
    kr = mach_vm_map_wrapper(&addr, size, VM_FLAGS_ANYWHERE, shared_mem_port,
                             offset, prot);
  }

  if (kr != KERN_SUCCESS) return nullptr;
  return reinterpret_cast<void*>(addr);
}

// static
bool OS::RemapPages(const void* address, size_t size, void* new_address,
                    MemoryPermission access) {
  DCHECK(IsAligned(reinterpret_cast<uintptr_t>(address), AllocatePageSize()));
  DCHECK(
      IsAligned(reinterpret_cast<uintptr_t>(new_address), AllocatePageSize()));
  DCHECK(IsAligned(size, AllocatePageSize()));

  vm_prot_t cur_protection = GetVMProtFromMemoryPermission(access);
  vm_prot_t max_protection;
  // Asks the kernel to remap *on top* of an existing mapping, rather than
  // copying the data.
  int flags = VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE;
  mach_vm_address_t target = reinterpret_cast<mach_vm_address_t>(new_address);
  kern_return_t ret =
      mach_vm_remap(mach_task_self(), &target, size, 0, flags, mach_task_self(),
                    reinterpret_cast<mach_vm_address_t>(address), FALSE,
                    &cur_protection, &max_protection, VM_INHERIT_NONE);

  if (ret != KERN_SUCCESS) return false;

  // Did we get the address we wanted?
  CHECK_EQ(new_address, reinterpret_cast<void*>(target));

  return true;
}

bool AddressSpaceReservation::AllocateShared(void* address, size_t size,
                                             OS::MemoryPermission access,
                                             PlatformSharedMemoryHandle handle,
                                             uint64_t offset) {
  DCHECK(Contains(address, size));

  vm_prot_t prot = GetVMProtFromMemoryPermission(access);
  mach_vm_address_t addr = reinterpret_cast<mach_vm_address_t>(address);
  mach_port_t shared_mem_port = MachMemoryEntryFromSharedMemoryHandle(handle);
  kern_return_t kr =
      mach_vm_map_wrapper(&addr, size, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                          shared_mem_port, offset, prot);
  return kr == KERN_SUCCESS;
}

// See platform-ios.cc for the iOS implementation.
#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT && !defined(V8_OS_IOS)
// Ignoring this warning is considered better than relying on
// __builtin_available.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"

V8_BASE_EXPORT void SetJitWriteProtected(int enable) {
  pthread_jit_write_protect_np(enable);
}

#pragma clang diagnostic pop
#endif

}  // namespace base
}  // namespace v8
