// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_MEMORY_PROTECTION_KEY_H_
#define V8_BASE_PLATFORM_MEMORY_PROTECTION_KEY_H_

#include "src/base/build_config.h"

#if V8_HAS_PKU_JIT_WRITE_PROTECT

#include "include/v8-platform.h"
#include "src/base/address-region.h"

namespace v8 {
namespace base {

// ----------------------------------------------------------------------------
// MemoryProtectionKey
//
// This class has static methods for the different platform specific
// functions related to memory protection key support.

// TODO(sroettger): Consider adding this to {base::PageAllocator} (higher-level,
// exported API) once the API is more stable and we have converged on a better
// design (e.g., typed class wrapper around int memory protection key).
class V8_BASE_EXPORT MemoryProtectionKey {
 public:
  // Sentinel value if there is no PKU support or allocation of a key failed.
  // This is also the return value on an error of pkey_alloc() and has the
  // benefit that calling pkey_mprotect() with -1 behaves the same as regular
  // mprotect().
  static constexpr int kNoMemoryProtectionKey = -1;

  // The default ProtectionKey can be used to remove pkey assignments.
  static constexpr int kDefaultProtectionKey = 0;

  // Permissions for memory protection keys on top of the page's permissions.
  // NOTE: Since there is no executable bit, the executable permission cannot be
  // withdrawn by memory protection keys.
  enum Permission {
    kNoRestrictions = 0,
    kDisableAccess = 1,
    kDisableWrite = 2,
  };

// If sys/mman.h has PKEY support (on newer Linux distributions), ensure that
// our definitions of the permissions is consistent with the ones in glibc.
#if defined(PKEY_DISABLE_ACCESS)
  static_assert(kDisableAccess == PKEY_DISABLE_ACCESS);
  static_assert(kDisableWrite == PKEY_DISABLE_WRITE);
#endif

  // Call exactly once per process to determine if PKU is supported on this
  // platform and initialize global data structures.
  static bool InitializeMemoryProtectionKeySupport();

  // Associates a memory protection {key} with the given {region}.
  // If {key} is {kNoMemoryProtectionKey} this behaves like "plain"
  // {SetPermissions()} and associates the default key to the region. That is,
  // explicitly calling with {kNoMemoryProtectionKey} can be used to
  // disassociate any protection key from a region. This also means "plain"
  // {SetPermissions()} disassociates the key from a region, making the key's
  // access restrictions irrelevant/inactive for that region. Returns true if
  // changing permissions and key was successful. (Returns a bool to be
  // consistent with {SetPermissions()}). The {page_permissions} are the
  // permissions of the page, not the key. For changing the permissions of the
  // key, use {SetPermissionsForKey()} instead.
  static bool SetPermissionsAndKey(
      base::AddressRegion region,
      v8::PageAllocator::Permission page_permissions, int key);

  // Set the key's permissions. {key} must be valid, i.e. not
  // {kNoMemoryProtectionKey}.
  static void SetPermissionsForKey(int key, Permission permissions);

  // Get the permissions of the protection key {key} for the current thread.
  static Permission GetKeyPermission(int key);
};

}  // namespace base
}  // namespace v8

#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

#endif  // V8_BASE_PLATFORM_MEMORY_PROTECTION_KEY_H_
