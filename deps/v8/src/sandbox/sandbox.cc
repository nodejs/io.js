// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/sandbox.h"

#include "include/v8-internal.h"
#include "src/base/bits.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/cpu.h"
#include "src/base/emulated-virtual-address-subspace.h"
#include "src/base/lazy-instance.h"
#include "src/base/sys-info.h"
#include "src/base/utils/random-number-generator.h"
#include "src/base/virtual-address-space-page-allocator.h"
#include "src/base/virtual-address-space.h"
#include "src/flags/flags.h"
#include "src/sandbox/hardware-support.h"
#include "src/sandbox/sandboxed-pointer.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX

bool Sandbox::first_four_gb_of_address_space_are_reserved_ = false;

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
thread_local Sandbox* Sandbox::current_ = nullptr;
// static
Sandbox* Sandbox::current_non_inlined() { return current_; }
// static
void Sandbox::set_current_non_inlined(Sandbox* sandbox) { current_ = sandbox; }
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES

Sandbox* Sandbox::default_sandbox_ = nullptr;

// Best-effort function to determine the approximate size of the virtual
// address space that can be addressed by this process. Used to determine
// appropriate sandbox size and placement.
// The value returned by this function will always be a power of two.
static Address DetermineAddressSpaceLimit() {
#ifndef V8_TARGET_ARCH_64_BIT
#error Unsupported target architecture.
#endif

  // Assume 48 bits by default, which seems to be the most common configuration.
  constexpr unsigned kDefaultVirtualAddressBits = 48;
  // 36 bits should realistically be the lowest value we could ever see.
  constexpr unsigned kMinVirtualAddressBits = 36;
  constexpr unsigned kMaxVirtualAddressBits = 64;

  unsigned hardware_virtual_address_bits = kDefaultVirtualAddressBits;
#if defined(V8_TARGET_ARCH_X64)
  base::CPU cpu;
  if (cpu.exposes_num_virtual_address_bits()) {
    hardware_virtual_address_bits = cpu.num_virtual_address_bits();
  }
#endif  // V8_TARGET_ARCH_X64

#if defined(V8_TARGET_ARCH_ARM64) && defined(V8_TARGET_OS_ANDROID)
  // On Arm64 Android assume a 40-bit virtual address space (39 bits for
  // userspace and kernel each) as that appears to be the most common
  // configuration and there seems to be no easy way to retrieve the actual
  // number of virtual address bits from the CPU in userspace.
  hardware_virtual_address_bits = 40;
#endif

  // Assume virtual address space is split 50/50 between userspace and kernel.
  hardware_virtual_address_bits -= 1;

  // Check if there is a software-imposed limits on the size of the address
  // space. For example, older Windows versions limit the address space to 8TB:
  // https://learn.microsoft.com/en-us/windows/win32/memory/memory-limits-for-windows-releases).
  Address software_limit = base::SysInfo::AddressSpaceEnd();
  // Compute the next power of two that is larger or equal to the limit.
  unsigned software_virtual_address_bits =
      64 - base::bits::CountLeadingZeros(software_limit - 1);

  // The available address space is the smaller of the two limits.
  unsigned virtual_address_bits =
      std::min(hardware_virtual_address_bits, software_virtual_address_bits);

  // Guard against nonsensical values.
  if (virtual_address_bits < kMinVirtualAddressBits ||
      virtual_address_bits > kMaxVirtualAddressBits) {
    virtual_address_bits = kDefaultVirtualAddressBits;
  }

  return 1ULL << virtual_address_bits;
}

void Sandbox::Initialize(v8::VirtualAddressSpace* vas) {
  // Take the size of the virtual address space into account when determining
  // the size of the address space reservation backing the sandbox. For
  // example, if we only have a 40-bit address space, split evenly between
  // userspace and kernel, then userspace can only address 512GB and so we use
  // a quarter of that, 128GB, as maximum reservation size.
  Address address_space_limit = DetermineAddressSpaceLimit();
  // Note: this is technically the maximum reservation size excluding the guard
  // regions (which are not created for partially-reserved sandboxes).
  size_t max_reservation_size = address_space_limit / 4;

  // In any case, the sandbox should be smaller than our address space since we
  // otherwise wouldn't always be able to allocate objects inside of it.
  CHECK_LT(kSandboxSize, address_space_limit);

  if (!vas->CanAllocateSubspaces()) {
    // If we cannot create virtual memory subspaces, we fall back to creating a
    // partially reserved sandbox. This will happen for example on older
    // Windows versions (before Windows 10) where the necessary memory
    // management APIs, in particular, VirtualAlloc2, are not available.
    // Since reserving virtual memory is an expensive operation on Windows
    // before version 8.1 (reserving 1TB of address space will increase private
    // memory usage by around 2GB), we only reserve the minimal amount of
    // address space here. This way, we don't incur the cost of reserving
    // virtual memory, but also don't get the desired security properties as
    // unrelated mappings may end up inside the sandbox.
    max_reservation_size = kSandboxMinimumReservationSize;
  }

  // If the maximum reservation size is less than the size of the sandbox, we
  // can only create a partially-reserved sandbox.
  bool success;
  size_t reservation_size = std::min(kSandboxSize, max_reservation_size);
  DCHECK(base::bits::IsPowerOfTwo(reservation_size));
  if (reservation_size < kSandboxSize) {
    DCHECK_GE(max_reservation_size, kSandboxMinimumReservationSize);
    success = InitializeAsPartiallyReservedSandbox(vas, kSandboxSize,
                                                   reservation_size);
  } else {
    DCHECK_EQ(kSandboxSize, reservation_size);
    constexpr bool use_guard_regions = true;
    success = Initialize(vas, kSandboxSize, use_guard_regions);
  }

  // Fall back to creating a (smaller) partially reserved sandbox.
  while (!success && reservation_size > kSandboxMinimumReservationSize) {
    static_assert(kFallbackToPartiallyReservedSandboxAllowed);
    reservation_size /= 2;
    DCHECK_GE(reservation_size, kSandboxMinimumReservationSize);
    success = InitializeAsPartiallyReservedSandbox(vas, kSandboxSize,
                                                   reservation_size);
  }

  if (!success) {
    V8::FatalProcessOutOfMemory(
        nullptr,
        "Failed to reserve the virtual address space for the V8 sandbox");
  }

#if V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED
  if (trap_handler::RegisterV8Sandbox(base(), size())) {
    trap_handler_initialized_ = true;
  } else {
    V8::FatalProcessOutOfMemory(
        nullptr, "Failed to allocate sandbox record for trap handling.");
  }
#endif  // V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED

  SandboxHardwareSupport::TryEnable(base(), size());

  DCHECK(initialized_);
}

bool Sandbox::Initialize(v8::VirtualAddressSpace* vas, size_t size,
                         bool use_guard_regions) {
  CHECK(!initialized_);
  CHECK(base::bits::IsPowerOfTwo(size));
  CHECK(vas->CanAllocateSubspaces());

  size_t reservation_size = size;
  // As a temporary workaround for crbug.com/40070746 we use larger guard
  // regions at the end of the sandbox.
  // TODO(40070746): remove this workaround again once we have a proper fix.
  size_t true_reservation_size = size;
#if defined(V8_TARGET_OS_ANDROID)
  // On Android, we often won't have sufficient virtual address space available.
  const size_t kAdditionalTrailingGuardRegionSize = 0;
#else
  // Worst-case, we currently need 8 (max element size) * 32GB (max ArrayBuffer
  // size) + 4GB (additional offset for TypedArray access).
  const size_t kTotalTrailingGuardRegionSize = 260ULL * GB;
  const size_t kAdditionalTrailingGuardRegionSize =
      kTotalTrailingGuardRegionSize - kSandboxGuardRegionSize;
#endif
  if (use_guard_regions) {
    reservation_size += 2 * kSandboxGuardRegionSize;
    true_reservation_size =
        reservation_size + kAdditionalTrailingGuardRegionSize;
  }

  Address hint = RoundDown(vas->RandomPageAddress(), kSandboxAlignment);

  // There should be no executable pages mapped inside the sandbox since
  // those could be corrupted by an attacker and therefore pose a security
  // risk. Furthermore, allowing executable mappings in the sandbox requires
  // MAP_JIT on macOS, which causes fork() to become excessively slow
  // (multiple seconds or even minutes for a 1TB sandbox on macOS 12.X), in
  // turn causing tests to time out. As such, the maximum page permission
  // inside the sandbox should be read + write.
  address_space_ =
      vas->AllocateSubspace(hint, true_reservation_size, kSandboxAlignment,
                            PagePermissions::kReadWrite);

  if (!address_space_) return false;

  reservation_base_ = address_space_->base();
  base_ = reservation_base_ + (use_guard_regions ? kSandboxGuardRegionSize : 0);
  size_ = size;
  end_ = base_ + size_;
  reservation_size_ = reservation_size;
  sandbox_page_allocator_ =
      std::make_unique<base::VirtualAddressSpacePageAllocator>(
          address_space_.get());

  if (use_guard_regions) {
    Address front = reservation_base_;
    Address back = end_;
    // These must succeed since nothing was allocated in the subspace yet.
    CHECK(address_space_->AllocateGuardRegion(front, kSandboxGuardRegionSize));
    CHECK(address_space_->AllocateGuardRegion(
        back, kSandboxGuardRegionSize + kAdditionalTrailingGuardRegionSize));
  }

  // Also try to reserve the first 4GB of the process' address space. This
  // mitigates Smi<->HeapObject confusion bugs in which we end up treating a
  // Smi value as a pointer.
  if (!first_four_gb_of_address_space_are_reserved_) {
    Address end = 4UL * GB;
    size_t step = address_space_->allocation_granularity();
    for (Address start = 0; start <= 1 * MB; start += step) {
      if (vas->AllocateGuardRegion(start, end - start)) {
        first_four_gb_of_address_space_are_reserved_ = true;
        break;
      }
    }
  }

  initialized_ = true;

  FinishInitialization();

  DCHECK(!is_partially_reserved());
  return true;
}

bool Sandbox::InitializeAsPartiallyReservedSandbox(v8::VirtualAddressSpace* vas,
                                                   size_t size,
                                                   size_t size_to_reserve) {
  CHECK(!initialized_);
  CHECK(base::bits::IsPowerOfTwo(size));
  CHECK(base::bits::IsPowerOfTwo(size_to_reserve));
  CHECK_LT(size_to_reserve, size);

  // Use a custom random number generator here to ensure that we get uniformly
  // distributed random numbers. We figure out the available address space
  // ourselves, and so are potentially better positioned to determine a good
  // base address for the sandbox than the embedder.
  base::RandomNumberGenerator rng;
  if (v8_flags.random_seed != 0) {
    rng.SetSeed(v8_flags.random_seed);
  }

  // We try to ensure that base + size is still (mostly) within the process'
  // address space, even though we only reserve a fraction of the memory. For
  // that, we attempt to map the sandbox into the first half of the usable
  // address space. This keeps the implementation simple and should, In any
  // realistic scenario, leave plenty of space after the actual reservation.
  Address address_space_end = DetermineAddressSpaceLimit();
  Address highest_allowed_address = address_space_end / 2;
  DCHECK(base::bits::IsPowerOfTwo(highest_allowed_address));
  constexpr int kMaxAttempts = 10;
  for (int i = 1; i <= kMaxAttempts; i++) {
    Address hint = rng.NextInt64() % highest_allowed_address;
    hint = RoundDown(hint, kSandboxAlignment);

    reservation_base_ = vas->AllocatePages(
        hint, size_to_reserve, kSandboxAlignment, PagePermissions::kNoAccess);

    if (!reservation_base_) return false;

    // Take this base if it meets the requirements or if this is the last
    // attempt.
    if (reservation_base_ <= highest_allowed_address || i == kMaxAttempts)
      break;

    // Can't use this base, so free the reservation and try again
    vas->FreePages(reservation_base_, size_to_reserve);
    reservation_base_ = kNullAddress;
  }
  DCHECK(reservation_base_);

  base_ = reservation_base_;
  size_ = size;
  end_ = base_ + size_;
  reservation_size_ = size_to_reserve;
  initialized_ = true;
  address_space_ = std::make_unique<base::EmulatedVirtualAddressSubspace>(
      vas, reservation_base_, reservation_size_, size_);
  sandbox_page_allocator_ =
      std::make_unique<base::VirtualAddressSpacePageAllocator>(
          address_space_.get());

  FinishInitialization();

  DCHECK(is_partially_reserved());
  return true;
}

void Sandbox::FinishInitialization() {
  // Reserve the last page in the sandbox. This way, we can place inaccessible
  // "objects" (e.g. the empty backing store buffer) there that are guaranteed
  // to cause a fault on any accidental access.
  // Further, this also prevents the accidental construction of invalid
  // SandboxedPointers: if an ArrayBuffer is placed right at the end of the
  // sandbox, an ArrayBufferView could be constructed with byteLength=0 and
  // offset=buffer.byteLength, which would lead to a pointer that points just
  // outside of the sandbox.
  size_t allocation_granularity = address_space_->allocation_granularity();
  bool success = address_space_->AllocateGuardRegion(
      end_ - allocation_granularity, allocation_granularity);
  // If the sandbox is partially-reserved, this operation may fail, for example
  // if the last page is outside of the mappable address space of the process.
  CHECK(success || is_partially_reserved());

  InitializeConstants();
}

void Sandbox::InitializeConstants() {
  // Place the empty backing store buffer at the end of the sandbox, so that any
  // accidental access to it will most likely hit a guard page.
  constants_.set_empty_backing_store_buffer(end_ - 1);
}

void Sandbox::TearDown() {
  if (initialized_) {
#if V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED
    if (trap_handler_initialized_) {
      trap_handler::UnregisterV8Sandbox(base(), size());
      trap_handler_initialized_ = false;
    }
#endif  // V8_ENABLE_WEBASSEMBLY && V8_TRAP_HANDLER_SUPPORTED

    // This destroys the sub space and frees the underlying reservation.
    address_space_.reset();
    sandbox_page_allocator_.reset();
    base_ = kNullAddress;
    end_ = kNullAddress;
    size_ = 0;
    reservation_base_ = kNullAddress;
    reservation_size_ = 0;
    initialized_ = false;
    constants_.Reset();
  }
}

// static
void Sandbox::InitializeDefaultOncePerProcess(v8::VirtualAddressSpace* vas) {
  static base::LeakyObject<Sandbox> default_sandbox;
  default_sandbox_ = default_sandbox.get();

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  set_current(default_sandbox_);
#endif
  default_sandbox_->Initialize(vas);
}

// static
void Sandbox::TearDownDefault() {
  GetDefault()->TearDown();

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  set_current(nullptr);
#endif
}

// static
Sandbox* Sandbox::New(v8::VirtualAddressSpace* vas) {
  if (!COMPRESS_POINTERS_IN_MULTIPLE_CAGES_BOOL) {
    FATAL(
        "Creation of new sandboxes requires enabling "
        "multiple pointer compression cages at build-time");
  }
  Sandbox* sandbox = new Sandbox;
  sandbox->Initialize(vas);
  CHECK(!v8_flags.sandbox_testing && !v8_flags.sandbox_fuzzing);
  return sandbox;
}

#endif  // V8_ENABLE_SANDBOX

}  // namespace internal
}  // namespace v8
