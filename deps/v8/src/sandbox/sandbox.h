// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_SANDBOX_H_
#define V8_SANDBOX_SANDBOX_H_

#include "include/v8-internal.h"
#include "include/v8-platform.h"
#include "include/v8config.h"
#include "src/base/bounds.h"
#include "src/common/globals.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX

/**
 * The V8 Sandbox.
 *
 * When enabled, V8 reserves a large region of virtual address space - the
 * sandbox - and places most of its objects inside of it. It is then assumed
 * that an attacker can, by exploiting a vulnerability in V8, corrupt memory
 * inside the sandbox arbitrarily and from different threads. The sandbox
 * attempts to stop an attacker from corrupting other memory in the process.
 *
 * The sandbox relies on a number of different mechanisms to achieve its goal.
 * For example, objects inside the sandbox can reference each other through
 * offsets from the start of the sandbox ("sandboxed pointers") instead of raw
 * pointers, and external objects can be referenced through indices into a
 * per-Isolate table of external pointers ("sandboxed external pointers").
 *
 * The pointer compression region, which contains most V8 objects, and inside
 * of which compressed (32-bit) pointers are used, is located at the start of
 * the sandbox. The remainder of the sandbox is mostly used for memory
 * buffers, in particular ArrayBuffer backing stores and WASM memory cages.
 *
 * As the embedder is responsible for providing ArrayBuffer allocators, V8
 * exposes the virtual address space backing the sandbox to the embedder.
 */
class V8_EXPORT_PRIVATE Sandbox {
 public:
  // +-  ~~~  -+----------------------------------------  ~~~  -+-  ~~~  -+
  // |  32 GB  |                 (Ideally) 1 TB                 |  32 GB  |
  // |         |                                                |         |
  // | Guard   |      4 GB      :  ArrayBuffer backing stores,  | Guard   |
  // | Region  |    V8 Heap     :  WASM memory buffers, and     | Region  |
  // | (front) |     Region     :  any other sandboxed objects. | (back)  |
  // +-  ~~~  -+----------------+-----------------------  ~~~  -+-  ~~~  -+
  //           ^                                                ^
  //           base                                             end
  //           < - - - - - - - - - - - size - - - - - - - - - - >
  // < - - - - - - - - - - - - - reservation_size - - - - - - - - - - - - >

  Sandbox() = default;

  Sandbox(const Sandbox&) = delete;
  Sandbox& operator=(Sandbox&) = delete;

  /**
   * Initializes this sandbox.
   *
   * This will allocate the virtual address subspace for the sandbox inside the
   * provided virtual address space. If a subspace of the required size cannot
   * be allocated, this method will insted initialize this sandbox as a
   * partially-reserved sandbox. In that case, a smaller virtual address space
   * reservation will be used and an EmulatedVirtualAddressSubspace instance
   * will be created on top of it to back the sandbox. If not enough virtual
   * address space can be allocated for even a partially-reserved sandbox, then
   * this method will fail with an OOM crash.
   */
  void Initialize(v8::VirtualAddressSpace* vas);

  /**
   * Tear down this sandbox.
   *
   * This will free the virtual address subspace backing this sandbox.
   */
  void TearDown();

  /**
   * Returns true if this sandbox has been initialized successfully.
   */
  bool is_initialized() const { return initialized_; }

  /**
   * Returns true if this sandbox is a partially-reserved sandbox.
   *
   * A partially-reserved sandbox is backed by a virtual address space
   * reservation that is smaller than its size. It also does not have guard
   * regions surrounding it. A partially-reserved sandbox is usually created if
   * not enough virtual address space could be reserved for the sandbox during
   * initialization. In such a configuration, unrelated memory mappings may end
   * up inside the sandbox, which affects its security properties.
   */
  bool is_partially_reserved() const { return reservation_size_ < size_; }

  /**
   * The base address of the sandbox.
   *
   * This is the start of the address space region that is directly addressable
   * by V8. In practice, this means the start of the part of the sandbox
   * address space between the surrounding guard regions.
   */
  Address base() const { return base_; }

  /**
   * The address right after the end of the sandbox.
   *
   * This is equal to |base| + |size|.
   */
  Address end() const { return end_; }

  /**
   * The size of the sandbox in bytes.
   */
  size_t size() const { return size_; }

  /**
   * The size of the virtual address space reservation backing the sandbox.
   *
   * This can be larger than |size| as it contains the surrounding guard
   * regions as well, or can be smaller than |size| in the case of a
   * partially-reserved sandbox.
   */
  size_t reservation_size() const { return reservation_size_; }

  /**
   * The virtual address subspace backing this sandbox.
   *
   * This can be used to allocate and manage memory pages inside the sandbox.
   */
  v8::VirtualAddressSpace* address_space() const {
    return address_space_.get();
  }

  /**
   * Returns a PageAllocator instance that allocates pages inside the sandbox.
   */
  v8::PageAllocator* page_allocator() const {
    return sandbox_page_allocator_.get();
  }

  /**
   * Returns true if the given address lies within the sandbox address space.
   */
  bool Contains(Address addr) const {
    return base::IsInHalfOpenRange(addr, base_, base_ + size_);
  }

  /**
   * Returns true if the given pointer points into the sandbox address space.
   */
  bool Contains(void* ptr) const {
    return Contains(reinterpret_cast<Address>(ptr));
  }

  /**
   * Returns true if the given address lies within the sandbox reservation.
   *
   * This is a variant of Contains that checks whether the address lies within
   * the virtual address space reserved for the sandbox. In the case of a
   * fully-reserved sandbox (the default) this is essentially the same as
   * Contains but also includes the guard region. In the case of a
   * partially-reserved sandbox, this will only test against the address region
   * that was actually reserved.
   * This can be useful when checking that objects are *not* located within the
   * sandbox, as in the case of a partially-reserved sandbox, they may still
   * end up in the unreserved part.
   */
  bool ReservationContains(Address addr) const {
    return base::IsInHalfOpenRange(addr, reservation_base_,
                                   reservation_base_ + reservation_size_);
  }

  class SandboxedPointerConstants final {
   public:
    Address empty_backing_store_buffer() const {
      return empty_backing_store_buffer_;
    }
    Address empty_backing_store_buffer_address() const {
      return reinterpret_cast<Address>(&empty_backing_store_buffer_);
    }
    void set_empty_backing_store_buffer(Address value) {
      empty_backing_store_buffer_ = value;
    }

    void Reset() { empty_backing_store_buffer_ = 0; }

   private:
    Address empty_backing_store_buffer_ = 0;
  };
  const SandboxedPointerConstants& constants() const { return constants_; }

  Address base_address() const { return reinterpret_cast<Address>(&base_); }
  Address end_address() const { return reinterpret_cast<Address>(&end_); }
  Address size_address() const { return reinterpret_cast<Address>(&size_); }

 private:
  // The SequentialUnmapperTest calls the private Initialize method to create a
  // sandbox without guard regions, which would consume too much memory.
  friend class SequentialUnmapperTest;

  // These tests call the private Initialize methods below.
  FRIEND_TEST(SandboxTest, InitializationWithSize);
  FRIEND_TEST(SandboxTest, PartiallyReservedSandbox);

  // We allow tests to disable the guard regions around the sandbox. This is
  // useful for example for tests like the SequentialUnmapperTest which track
  // page allocations and so would incur a large overhead from the guard
  // regions. The provided virtual address space must be able to allocate
  // subspaces. The size must be a multiple of the allocation granularity of the
  // virtual memory space.
  bool Initialize(v8::VirtualAddressSpace* vas, size_t size,
                  bool use_guard_regions);

  // Used when reserving virtual memory is too expensive. A partially reserved
  // sandbox does not reserve all of its virtual memory and so doesn't have the
  // desired security properties as unrelated mappings could end up inside of
  // it and be corrupted. The size and size_to_reserve parameters must be
  // multiples of the allocation granularity of the virtual address space.
  bool InitializeAsPartiallyReservedSandbox(v8::VirtualAddressSpace* vas,
                                            size_t size,
                                            size_t size_to_reserve);

  // Performs final initialization steps after the sandbox address space has
  // been initialized. Called from the two Initialize variants above.
  void FinishInitialization();

  // Initialize the constant objects for this sandbox.
  void InitializeConstants();

  Address base_ = kNullAddress;
  Address end_ = kNullAddress;
  size_t size_ = 0;

  // Base and size of the virtual memory reservation backing this sandbox.
  // These can be different from the sandbox base and size due to guard regions
  // or when a partially-reserved sandbox is used.
  Address reservation_base_ = kNullAddress;
  size_t reservation_size_ = 0;

  bool initialized_ = false;

  // The virtual address subspace backing the sandbox.
  std::unique_ptr<v8::VirtualAddressSpace> address_space_;

  // The page allocator instance for this sandbox.
  std::unique_ptr<v8::PageAllocator> sandbox_page_allocator_;

  // Constant objects inside this sandbox.
  SandboxedPointerConstants constants_;
};

V8_EXPORT_PRIVATE Sandbox* GetProcessWideSandbox();

#endif  // V8_ENABLE_SANDBOX

// Helper function that can be used to ensure that certain objects are not
// located inside the sandbox. Typically used for trusted objects.
// Will always return false when the sandbox is disabled or partially reserved.
V8_INLINE bool InsideSandbox(uintptr_t address) {
#ifdef V8_ENABLE_SANDBOX
  Sandbox* sandbox = GetProcessWideSandbox();
  // Use ReservationContains (instead of just Contains) to correctly handle the
  // case of partially-reserved sandboxes.
  return sandbox->ReservationContains(address);
#else
  return false;
#endif
}

V8_INLINE void* EmptyBackingStoreBuffer() {
#ifdef V8_ENABLE_SANDBOX
  return reinterpret_cast<void*>(
      GetProcessWideSandbox()->constants().empty_backing_store_buffer());
#else
  return nullptr;
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_SANDBOX_H_
