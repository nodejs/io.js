// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/memory-protection-key.h"

#if V8_HAS_PKU_JIT_WRITE_PROTECT

#include <sys/mman.h>  // For {mprotect()} protection macros.
#undef MAP_TYPE  // Conflicts with MAP_TYPE in Torque-generated instance-types.h

#include "src/base/logging.h"
#include "src/base/macros.h"

// Declare all the pkey functions as weak to support older glibc versions where
// they don't exist yet.
int pkey_mprotect(void* addr, size_t len, int prot, int pkey) V8_WEAK;
int pkey_get(int key) V8_WEAK;
int pkey_set(int, unsigned) V8_WEAK;
int pkey_alloc(unsigned int, unsigned int) V8_WEAK;

namespace v8 {
namespace base {

namespace {

int GetProtectionFromMemoryPermission(PageAllocator::Permission permission) {
  // Mappings for PKU are either RWX (for code), no access (for uncommitted
  // memory), or RO for globals.
  switch (permission) {
    case PageAllocator::kNoAccess:
      return PROT_NONE;
    case PageAllocator::kRead:
      return PROT_READ;
    case PageAllocator::kReadWrite:
      return PROT_READ | PROT_WRITE;
    case PageAllocator::kReadWriteExecute:
      return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
      UNREACHABLE();
  }
}

}  // namespace

bool MemoryProtectionKey::HasMemoryProtectionKeySupport() {
  if (!pkey_mprotect) return false;
  // If {pkey_mprotect} is available, the others must also be available.
  CHECK(pkey_get && pkey_set && pkey_alloc);

  return true;
}

// static
int MemoryProtectionKey::AllocateKey() {
  if (!pkey_alloc) {
    return kNoMemoryProtectionKey;
  }

  return pkey_alloc(0, 0);
}

// static
bool MemoryProtectionKey::SetPermissionsAndKey(
    base::AddressRegion region, v8::PageAllocator::Permission page_permissions,
    int key) {
  DCHECK_NE(key, kNoMemoryProtectionKey);
  CHECK_NOT_NULL(pkey_mprotect);

  void* address = reinterpret_cast<void*>(region.begin());
  size_t size = region.size();

  int protection = GetProtectionFromMemoryPermission(page_permissions);

  return pkey_mprotect(address, size, protection, key) == 0;
}

// static
void MemoryProtectionKey::SetPermissionsForKey(int key,
                                               Permission permissions) {
  DCHECK_NE(kNoMemoryProtectionKey, key);

  // If a valid key was allocated, {pkey_set()} must also be available.
  DCHECK_NOT_NULL(pkey_set);

  CHECK_EQ(0 /* success */, pkey_set(key, permissions));
}

// static
MemoryProtectionKey::Permission MemoryProtectionKey::GetKeyPermission(int key) {
  DCHECK_NE(kNoMemoryProtectionKey, key);

  // If a valid key was allocated, {pkey_get()} must also be available.
  DCHECK_NOT_NULL(pkey_get);

  int permission = pkey_get(key);
  CHECK(permission == kNoRestrictions || permission == kDisableAccess ||
        permission == kDisableWrite);
  return static_cast<Permission>(permission);
}

}  // namespace base
}  // namespace v8

#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT
