// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_CODE_MEMORY_ACCESS_H_
#define V8_COMMON_CODE_MEMORY_ACCESS_H_

#include <map>
#include <optional>

#include "include/v8-internal.h"
#include "include/v8-platform.h"
#include "src/base/build_config.h"
#include "src/base/macros.h"
#include "src/base/memory.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

// We protect writes to executable memory in some configurations and whenever
// we write to it, we need to explicitely allow it first.
//
// For this purposed, there are a few scope objects with different semantics:
//
// - CodePageMemoryModificationScopeForDebugging:
//     A scope only used in non-release builds, e.g. for code zapping.
// - wasm::CodeSpaceWriteScope:
//     Allows access to Wasm code
//
// - RwxMemoryWriteScope:
//     A scope that uses per-thread permissions to allow access. Should not be
//     used directly, but rather is the implementation of one of the above.
// - RwxMemoryWriteScopeForTesting:
//     Same, but for use in testing.

class RwxMemoryWriteScopeForTesting;
namespace wasm {
class CodeSpaceWriteScope;
}

#if V8_HAS_PKU_JIT_WRITE_PROTECT

// Alignment macros.
// Adapted from partition_allocator/thread_isolation/alignment.h.

// Page size is not a compile time constant, but we need it for alignment and
// padding of our global memory.
// We use the maximum expected value here (currently x64 only) and test in
// ThreadIsolation::Initialize() that it's a multiple of the real pagesize.
#define THREAD_ISOLATION_ALIGN_SZ 0x1000
#define THREAD_ISOLATION_ALIGN alignas(THREAD_ISOLATION_ALIGN_SZ)
#define THREAD_ISOLATION_ALIGN_OFFSET_MASK (THREAD_ISOLATION_ALIGN_SZ - 1)
#define THREAD_ISOLATION_FILL_PAGE_SZ(size)          \
  ((THREAD_ISOLATION_ALIGN_SZ -                      \
    ((size) & THREAD_ISOLATION_ALIGN_OFFSET_MASK)) % \
   THREAD_ISOLATION_ALIGN_SZ)

#else  // V8_HAS_PKU_JIT_WRITE_PROTECT

#define THREAD_ISOLATION_ALIGN_SZ 0
#define THREAD_ISOLATION_ALIGN
#define THREAD_ISOLATION_FILL_PAGE_SZ(size) 0

#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

// This scope is a wrapper for APRR/MAP_JIT machinery on MacOS on ARM64
// ("Apple M1"/Apple Silicon) or Intel PKU (aka. memory protection keys)
// with respective low-level semantics.
//
// The semantics on MacOS on ARM64 is the following:
// The scope switches permissions between writable and executable for all the
// pages allocated with RWX permissions. Only current thread is affected.
// This achieves "real" W^X and it's fast (see pthread_jit_write_protect_np()
// for details).
// By default it is assumed that the state is executable.
// It's also assumed that the process has the "com.apple.security.cs.allow-jit"
// entitlement.
//
// The semantics on Intel with PKU support is the following:
// When Intel PKU is available, the scope switches the protection key's
// permission between writable and not writable. The executable permission
// cannot be retracted with PKU. That is, this "only" achieves write
// protection, but is similarly thread-local and fast.
//
// On other platforms the scope is a no-op and thus it's allowed to be used.
//
// The scope is reentrant and thread safe.
class V8_NODISCARD RwxMemoryWriteScope {
 public:
  // The comment argument is used only for ensuring that explanation about why
  // the scope is needed is given at particular use case.
  V8_INLINE explicit RwxMemoryWriteScope(const char* comment);
  V8_INLINE ~RwxMemoryWriteScope();

  // Disable copy constructor and copy-assignment operator, since this manages
  // a resource and implicit copying of the scope can yield surprising errors.
  RwxMemoryWriteScope(const RwxMemoryWriteScope&) = delete;
  RwxMemoryWriteScope& operator=(const RwxMemoryWriteScope&) = delete;

  // Returns true if current configuration supports fast write-protection of
  // executable pages.
  V8_INLINE static bool IsSupported();

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  static int memory_protection_key();

  static bool IsPKUWritable();

  // Linux resets key's permissions to kDisableAccess before executing signal
  // handlers. If the handler requires access to code page bodies it should take
  // care of changing permissions to the default state (kDisableWrite).
  static V8_EXPORT void SetDefaultPermissionsForSignalHandler();
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

 private:
  friend class RwxMemoryWriteScopeForTesting;
  friend class wasm::CodeSpaceWriteScope;

  // {SetWritable} and {SetExecutable} implicitly enters/exits the scope.
  // These methods are exposed only for the purpose of implementing other
  // scope classes that affect executable pages permissions.
  V8_INLINE static void SetWritable();
  V8_INLINE static void SetExecutable();
};

class WritableJitPage;
class WritableJitAllocation;
class WritableJumpTablePair;

// The ThreadIsolation API is used to protect executable memory using per-thread
// memory permissions and perform validation for any writes into it.
//
// It keeps metadata about all JIT regions in write-protected memory and will
// use it to validate that the writes are safe from a CFI perspective.
// Its tasks are:
// * track JIT pages and allocations and check for validity
// * check for dangling pointers on the shadow stack (not implemented)
// * validate code writes like code creation, relocation, etc. (not implemented)
class V8_EXPORT ThreadIsolation {
 public:
  static bool Enabled();
  static void Initialize(ThreadIsolatedAllocator* allocator);

  enum class JitAllocationType {
    kInstructionStream,
    kWasmCode,
    kWasmJumpTable,
    kWasmFarJumpTable,
    kWasmLazyCompileTable,
  };

  // Register a new JIT region.
  static void RegisterJitPage(Address address, size_t size);
  // Unregister a JIT region that is about to be unmpapped.
  static void UnregisterJitPage(Address address, size_t size);
  // Make a page executable. Needs to be registered first. Should only be called
  // if Enabled() is true.
  V8_NODISCARD static bool MakeExecutable(Address address, size_t size);

  // Register a new JIT allocation for tracking and return a writable reference
  // to it. All writes should go through the returned WritableJitAllocation
  // object since it will perform additional validation required for CFI.
  static WritableJitAllocation RegisterJitAllocation(Address addr, size_t size,
                                                     JitAllocationType type);
  // TODO(sroettger): remove this overwrite and use RegisterJitAllocation
  // instead.
  static WritableJitAllocation RegisterInstructionStreamAllocation(Address addr,
                                                                   size_t size);
  // Register multiple consecutive allocations together.
  static void RegisterJitAllocations(Address start,
                                     const std::vector<size_t>& sizes,
                                     JitAllocationType type);

  // Get writable reference to a previously registered allocation. All writes to
  // executable memory need to go through one of these Writable* objects since
  // this is where we perform CFI validation.
  static WritableJitAllocation LookupJitAllocation(Address addr, size_t size,
                                                   JitAllocationType type);
  // A special case of LookupJitAllocation since in Wasm, we sometimes have to
  // unlock two allocations (jump tables) together.
  static WritableJumpTablePair LookupJumpTableAllocations(
      Address jump_table_address, size_t jump_table_size,
      Address far_jump_table_address, size_t far_jump_table_size);
  // Unlock a larger region. This allowsV us to lookup allocations in this
  // region more quickly without switching the write permissions all the time.
  static WritableJitPage LookupWritableJitPage(Address addr, size_t size);

  static void UnregisterWasmAllocation(Address addr, size_t size);

  // Check for a potential dead lock in case we want to lookup the jit
  // allocation from inside a signal handler.
  static bool CanLookupStartOfJitAllocationAt(Address inner_pointer);
  static std::optional<Address> StartOfJitAllocationAt(Address inner_pointer);

  // Write-protect a given range of memory. Address and size need to be page
  // aligned.
  V8_NODISCARD static bool WriteProtectMemory(
      Address addr, size_t size, PageAllocator::Permission page_permissions);

  static void RegisterJitAllocationForTesting(Address obj, size_t size);
  static void UnregisterJitAllocationForTesting(Address addr, size_t size);

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  static int pkey() { return trusted_data_.pkey; }
#endif

#if DEBUG
  static bool initialized() { return trusted_data_.initialized; }
  static void CheckTrackedMemoryEmpty();
#endif

  // A std::allocator implementation that wraps the ThreadIsolated allocator.
  // This is needed to create STL containers backed by ThreadIsolated memory.
  template <class T>
  struct StlAllocator {
    typedef T value_type;

    StlAllocator() = default;
    template <class U>
    explicit StlAllocator(const StlAllocator<U>&) noexcept {}

    value_type* allocate(size_t n) {
      if (Enabled()) {
        return static_cast<value_type*>(
            ThreadIsolation::allocator()->Allocate(n * sizeof(value_type)));
      } else {
        return static_cast<value_type*>(::operator new(n * sizeof(T)));
      }
    }

    void deallocate(value_type* ptr, size_t n) {
      if (Enabled()) {
        ThreadIsolation::allocator()->Free(ptr);
      } else {
        ::operator delete(ptr);
      }
    }
  };

  class JitAllocation {
   public:
    explicit JitAllocation(size_t size, JitAllocationType type)
        : size_(size), type_(type) {}
    size_t Size() const { return size_; }
    JitAllocationType Type() const { return type_; }

   private:
    size_t size_;
    JitAllocationType type_;
  };

  class JitPage;

  // All accesses to the JitPage go through the JitPageReference class, which
  // will guard it against concurrent access.
  class V8_EXPORT JitPageReference {
   public:
    JitPageReference(class JitPage* page, Address address);
    JitPageReference(JitPageReference&&) V8_NOEXCEPT = default;
    JitPageReference(const JitPageReference&) = delete;
    JitPageReference& operator=(const JitPageReference&) = delete;

    base::Address Address() const { return address_; }
    size_t Size() const;
    base::Address End() const { return Address() + Size(); }
    JitAllocation& RegisterAllocation(base::Address addr, size_t size,
                                      JitAllocationType type);
    JitAllocation& LookupAllocation(base::Address addr, size_t size,
                                    JitAllocationType type);
    void UnregisterAllocation(base::Address addr);
    void UnregisterAllocationsExcept(base::Address start, size_t size,
                                     const std::vector<base::Address>& addr);
    void UnregisterRange(base::Address addr, size_t size);

    base::Address StartOfAllocationAt(base::Address inner_pointer);
    std::pair<base::Address, JitAllocation&> AllocationContaining(
        base::Address addr);

    bool Empty() const { return jit_page_->allocations_.empty(); }
    void Shrink(class JitPage* tail);
    void Expand(size_t offset);
    void Merge(JitPageReference& next);
    class JitPage* JitPage() { return jit_page_; }

   private:
    base::MutexGuard page_lock_;
    class JitPage* jit_page_;
    // We get the address from the key of the map when we do a JitPage lookup.
    // We can save some memory by storing it as part of the reference instead.
    base::Address address_;
  };

  class JitPage {
   public:
    explicit JitPage(size_t size) : size_(size) {}
    ~JitPage();

   private:
    base::Mutex mutex_;
    typedef std::map<Address, JitAllocation, std::less<Address>,
                     StlAllocator<std::pair<const Address, JitAllocation>>>
        AllocationMap;
    AllocationMap allocations_;
    size_t size_;

    friend class JitPageReference;
    // Allow CanLookupStartOfJitAllocationAt to check if the mutex is locked.
    friend bool ThreadIsolation::CanLookupStartOfJitAllocationAt(Address);
  };

 private:
  static ThreadIsolatedAllocator* allocator() {
    return trusted_data_.allocator;
  }

  // We store pointers in the map since we want to use the entries without
  // keeping the map locked.
  typedef std::map<Address, JitPage*, std::less<Address>,
                   StlAllocator<std::pair<const Address, JitPage*>>>
      JitPageMap;

  // The TrustedData needs to be page aligned so that we can protect it using
  // per-thread memory permissions (e.g. pkeys on x64).
  struct THREAD_ISOLATION_ALIGN TrustedData {
    ThreadIsolatedAllocator* allocator = nullptr;

#if V8_HAS_PKU_JIT_WRITE_PROTECT
    int pkey = -1;
#endif

    base::Mutex* jit_pages_mutex_;
    JitPageMap* jit_pages_;

#if DEBUG
    bool initialized = false;
#endif
  };

  static struct TrustedData trusted_data_;

  static_assert(THREAD_ISOLATION_ALIGN_SZ == 0 ||
                sizeof(trusted_data_) == THREAD_ISOLATION_ALIGN_SZ);

  // Allocate and construct C++ objects using memory backed by the
  // ThreadIsolated allocator.
  template <typename T, typename... Args>
  static void ConstructNew(T** ptr, Args&&... args);
  template <typename T>
  static void Delete(T* ptr);

  // Lookup a JitPage that spans a given range. Note that JitPages are not
  // required to align with OS pages. There are no minimum size requirements and
  // we can split and merge them under the hood for performance optimizations.
  // IOW, the returned JitPage is guaranteed to span the given range, but
  // doesn't need to be the exact previously registered JitPage.
  static JitPageReference LookupJitPage(Address addr, size_t size);
  static JitPageReference LookupJitPageLocked(Address addr, size_t size);
  static std::optional<JitPageReference> TryLookupJitPage(Address addr,
                                                          size_t size);
  // The caller needs to hold a lock of the jit_pages_mutex_
  static std::optional<JitPageReference> TryLookupJitPageLocked(Address addr,
                                                                size_t size);
  static JitPageReference SplitJitPageLocked(Address addr, size_t size);
  static JitPageReference SplitJitPage(Address addr, size_t size);
  static std::pair<JitPageReference, JitPageReference> SplitJitPages(
      Address addr1, size_t size1, Address addr2, size_t size2);

  template <class T>
  friend struct StlAllocator;
  friend class WritableJitPage;
  friend class WritableJitAllocation;
  friend class WritableJumpTablePair;
};

// A scope class that temporarily makes the JitAllocation writable. All writes
// to executable memory should go through this object since it adds validation
// that the writes are safe for CFI.
class WritableJitAllocation {
 public:
  WritableJitAllocation(const WritableJitAllocation&) = delete;
  WritableJitAllocation& operator=(const WritableJitAllocation&) = delete;
  V8_INLINE ~WritableJitAllocation();

  static WritableJitAllocation ForInstructionStream(
      Tagged<InstructionStream> istream);

  // WritableJitAllocations are used during reloc iteration. But in some
  // cases, we relocate code off-heap, e.g. when growing AssemblerBuffers.
  // This function creates a WritableJitAllocation that doesn't unlock the
  // executable memory.
  static V8_INLINE WritableJitAllocation ForNonExecutableMemory(
      Address addr, size_t size, ThreadIsolation::JitAllocationType type);

  // Writes a header slot either as a primitive or as a Tagged value.
  // Important: this function will not trigger a write barrier by itself,
  // since we want to keep the code running with write access to executable
  // memory to a minimum. You should trigger the write barriers after this
  // function goes out of scope.
  template <typename T, size_t offset>
  V8_INLINE void WriteHeaderSlot(T value);
  template <typename T, size_t offset>
  V8_INLINE void WriteHeaderSlot(Tagged<T> value, ReleaseStoreTag);
  template <typename T, size_t offset>
  V8_INLINE void WriteHeaderSlot(Tagged<T> value, RelaxedStoreTag);
  template <typename T, size_t offset>
  V8_INLINE void WriteProtectedPointerHeaderSlot(Tagged<T> value,
                                                 RelaxedStoreTag);
  template <typename T>
  V8_INLINE void WriteHeaderSlot(Address address, T value, RelaxedStoreTag);

  // CopyCode and CopyData have the same implementation at the moment, but
  // they will diverge once we implement validation.
  V8_INLINE void CopyCode(size_t dst_offset, const uint8_t* src,
                          size_t num_bytes);
  V8_INLINE void CopyData(size_t dst_offset, const uint8_t* src,
                          size_t num_bytes);

  V8_INLINE void ClearBytes(size_t offset, size_t len);

  Address address() const { return address_; }
  size_t size() const { return allocation_.Size(); }

 private:
  enum class JitAllocationSource {
    kRegister,
    kLookup,
  };
  V8_INLINE WritableJitAllocation(Address addr, size_t size,
                                  ThreadIsolation::JitAllocationType type,
                                  JitAllocationSource source);
  // Used for non-executable memory.
  V8_INLINE WritableJitAllocation(Address addr, size_t size,
                                  ThreadIsolation::JitAllocationType type);

  ThreadIsolation::JitPageReference& page_ref() { return page_ref_.value(); }

  const Address address_;
  // TODO(sroettger): we can move the memory write scopes into the Write*
  // functions in debug builds. This would allow us to ensure that all writes
  // go through this object.
  // The scope and page reference are optional in case we're creating a
  // WritableJitAllocation for off-heap memory. See ForNonExecutableMemory
  // above.
  std::optional<RwxMemoryWriteScope> write_scope_;
  std::optional<ThreadIsolation::JitPageReference> page_ref_;
  const ThreadIsolation::JitAllocation allocation_;

  friend class ThreadIsolation;
  friend class WritableJitPage;
};

// Similar to the WritableJitAllocation, all writes to free space should go
// through this object since it adds validation that the writes are safe for
// CFI.
// For convenience, it can also be used for writes to non-executable memory for
// which it will skip the CFI checks.
class WritableFreeSpace {
 public:
  // This function can be used to create a WritableFreeSpace object for
  // non-executable memory only, i.e. it won't perform CFI validation and
  // doesn't unlock the code space.
  // For executable memory, use the WritableJitPage::FreeRange function.
  static V8_INLINE WritableFreeSpace ForNonExecutableMemory(base::Address addr,
                                                            size_t size);

  WritableFreeSpace(const WritableFreeSpace&) = delete;
  WritableFreeSpace& operator=(const WritableFreeSpace&) = delete;
  V8_INLINE ~WritableFreeSpace();

  template <typename T, size_t offset>
  V8_INLINE void WriteHeaderSlot(Tagged<T> value, RelaxedStoreTag) const;
  template <size_t offset>
  void ClearTagged(size_t count) const;

  base::Address Address() const { return address_; }
  int Size() const { return size_; }
  bool Executable() const { return executable_; }

 private:
  WritableFreeSpace(base::Address addr, size_t size, bool executable);

  const base::Address address_;
  const int size_;
  const bool executable_;

  friend class WritableJitPage;
};

extern template void WritableFreeSpace::ClearTagged<kTaggedSize>(
    size_t count) const;
extern template void WritableFreeSpace::ClearTagged<2 * kTaggedSize>(
    size_t count) const;

class WritableJitPage {
 public:
  V8_INLINE WritableJitPage(Address addr, size_t size);

  WritableJitPage(const WritableJitPage&) = delete;
  WritableJitPage& operator=(const WritableJitPage&) = delete;
  V8_INLINE ~WritableJitPage();
  friend class ThreadIsolation;

  V8_INLINE WritableJitAllocation LookupAllocationContaining(Address addr);

  V8_INLINE WritableFreeSpace FreeRange(Address addr, size_t size);

  bool Empty() const { return page_ref_.Empty(); }

 private:
  RwxMemoryWriteScope write_scope_;
  ThreadIsolation::JitPageReference page_ref_;
};

class WritableJumpTablePair {
 public:
  // TODO(sroettger): add functions to write to the jump tables.
 private:
  V8_INLINE WritableJumpTablePair(Address jump_table_address,
                                  size_t jump_table_size,
                                  Address far_jump_table_address,
                                  size_t far_jump_table_size);
  RwxMemoryWriteScope write_scope_;
  std::pair<ThreadIsolation::JitPageReference,
            ThreadIsolation::JitPageReference>
      jump_table_pages_;
  const ThreadIsolation::JitAllocation& jump_table_;
  const ThreadIsolation::JitAllocation& far_jump_table_;

  friend class ThreadIsolation;
};

template <class T>
bool operator==(const ThreadIsolation::StlAllocator<T>&,
                const ThreadIsolation::StlAllocator<T>&) {
  return true;
}

template <class T>
bool operator!=(const ThreadIsolation::StlAllocator<T>&,
                const ThreadIsolation::StlAllocator<T>&) {
  return false;
}

// This class is a no-op version of the RwxMemoryWriteScope class above.
// It's used as a target type for other scope type definitions when a no-op
// semantics is required.
class V8_NODISCARD V8_ALLOW_UNUSED NopRwxMemoryWriteScope final {
 public:
  V8_INLINE NopRwxMemoryWriteScope() = default;
  V8_INLINE explicit NopRwxMemoryWriteScope(const char* comment) {
    // Define a constructor to avoid unused variable warnings.
  }
};

// Same as the RwxMemoryWriteScope but without inlining the code.
// This is a workaround for component build issue (crbug/1316800), when
// a thread_local value can't be properly exported.
class V8_NODISCARD RwxMemoryWriteScopeForTesting final
    : public RwxMemoryWriteScope {
 public:
  V8_EXPORT_PRIVATE RwxMemoryWriteScopeForTesting();
  V8_EXPORT_PRIVATE ~RwxMemoryWriteScopeForTesting();

  // Disable copy constructor and copy-assignment operator, since this manages
  // a resource and implicit copying of the scope can yield surprising errors.
  RwxMemoryWriteScopeForTesting(const RwxMemoryWriteScopeForTesting&) = delete;
  RwxMemoryWriteScopeForTesting& operator=(
      const RwxMemoryWriteScopeForTesting&) = delete;
};

#if V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT
// Metadata are not protected yet with PTHREAD_JIT_WRITE_PROTECT
using CFIMetadataWriteScope = NopRwxMemoryWriteScope;
#else
using CFIMetadataWriteScope = RwxMemoryWriteScope;
#endif

}  // namespace internal
}  // namespace v8

#endif  // V8_COMMON_CODE_MEMORY_ACCESS_H_
