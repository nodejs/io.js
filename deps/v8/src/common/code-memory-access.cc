// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/code-memory-access.h"

#include <optional>

#include "src/common/code-memory-access-inl.h"
#include "src/objects/instruction-stream-inl.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

ThreadIsolation::TrustedData ThreadIsolation::trusted_data_;

#if V8_HAS_PKU_JIT_WRITE_PROTECT

// static
int RwxMemoryWriteScope::memory_protection_key() {
  return ThreadIsolation::pkey();
}

bool RwxMemoryWriteScope::IsPKUWritable() {
  DCHECK(ThreadIsolation::initialized());
  return base::MemoryProtectionKey::GetKeyPermission(ThreadIsolation::pkey()) ==
         base::MemoryProtectionKey::kNoRestrictions;
}

void RwxMemoryWriteScope::SetDefaultPermissionsForSignalHandler() {
  DCHECK(ThreadIsolation::initialized());
  if (!RwxMemoryWriteScope::IsSupported()) return;
  base::MemoryProtectionKey::SetPermissionsForKey(
      ThreadIsolation::pkey(), base::MemoryProtectionKey::kDisableWrite);
}

#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

RwxMemoryWriteScopeForTesting::RwxMemoryWriteScopeForTesting()
    : RwxMemoryWriteScope("For Testing") {}

RwxMemoryWriteScopeForTesting::~RwxMemoryWriteScopeForTesting() {}

// static
bool ThreadIsolation::Enabled() {
#if V8_HEAP_USE_PKU_JIT_WRITE_PROTECT
  return allocator() != nullptr;
#else
  return false;
#endif
}

// static
template <typename T, typename... Args>
void ThreadIsolation::ConstructNew(T** ptr, Args&&... args) {
  if (Enabled()) {
    *ptr = reinterpret_cast<T*>(trusted_data_.allocator->Allocate(sizeof(T)));
    if (!*ptr) return;
    new (*ptr) T(std::forward<Args>(args)...);
  } else {
    *ptr = new T(std::forward<Args>(args)...);
  }
}

// static
template <typename T>
void ThreadIsolation::Delete(T* ptr) {
  if (Enabled()) {
    ptr->~T();
    trusted_data_.allocator->Free(ptr);
  } else {
    delete ptr;
  }
}

// static
void ThreadIsolation::Initialize(
    ThreadIsolatedAllocator* thread_isolated_allocator) {
#if DEBUG
  trusted_data_.initialized = true;
#endif

  bool enable = thread_isolated_allocator != nullptr && !v8_flags.jitless;

#ifdef THREAD_SANITIZER
  // TODO(sroettger): with TSAN enabled, we get crashes because
  // SetDefaultPermissionsForSignalHandler gets called while a
  // RwxMemoryWriteScope is active. It seems that tsan's ProcessPendingSignals
  // doesn't restore the pkru value after executing the signal handler.
  enable = false;
#endif

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  if (enable && !base::MemoryProtectionKey::HasMemoryProtectionKeySupport()) {
    enable = false;
  }
#endif

  if (enable) {
    trusted_data_.allocator = thread_isolated_allocator;
#if V8_HAS_PKU_JIT_WRITE_PROTECT
    trusted_data_.pkey = trusted_data_.allocator->Pkey();
#endif
  }

  {
    // We need to allocate the memory for jit page tracking even if we don't
    // enable the ThreadIsolation protections.
    CFIMetadataWriteScope write_scope("Initialize thread isolation.");
    ConstructNew(&trusted_data_.jit_pages_mutex_);
    ConstructNew(&trusted_data_.jit_pages_);
  }

  if (!enable) {
    return;
  }

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  // Check that our compile time assumed page size that we use for padding was
  // large enough.
  CHECK_GE(THREAD_ISOLATION_ALIGN_SZ,
           GetPlatformPageAllocator()->CommitPageSize());

  // TODO(sroettger): make this immutable once there's OS support.
  base::MemoryProtectionKey::SetPermissionsAndKey(
      {reinterpret_cast<Address>(&trusted_data_), sizeof(trusted_data_)},
      v8::PageAllocator::Permission::kRead,
      base::MemoryProtectionKey::kDefaultProtectionKey);
#endif
}

// static
ThreadIsolation::JitPageReference ThreadIsolation::LookupJitPageLocked(
    Address addr, size_t size) {
  trusted_data_.jit_pages_mutex_->AssertHeld();
  std::optional<JitPageReference> jit_page = TryLookupJitPageLocked(addr, size);
  CHECK(jit_page.has_value());
  return std::move(jit_page.value());
}

// static
ThreadIsolation::JitPageReference ThreadIsolation::LookupJitPage(Address addr,
                                                                 size_t size) {
  base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
  return LookupJitPageLocked(addr, size);
}

// static
WritableJitPage ThreadIsolation::LookupWritableJitPage(Address addr,
                                                       size_t size) {
  return WritableJitPage(addr, size);
}

// static
std::optional<ThreadIsolation::JitPageReference>
ThreadIsolation::TryLookupJitPage(Address addr, size_t size) {
  base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
  return TryLookupJitPageLocked(addr, size);
}

// static
std::optional<ThreadIsolation::JitPageReference>
ThreadIsolation::TryLookupJitPageLocked(Address addr, size_t size) {
  trusted_data_.jit_pages_mutex_->AssertHeld();

  Address end = addr + size;
  CHECK_GT(end, addr);

  // upper_bound gives us an iterator to the position after address.
  auto it = trusted_data_.jit_pages_->upper_bound(addr);

  // The previous page should be the one we're looking for.
  if (it == trusted_data_.jit_pages_->begin()) {
    return {};
  }

  it--;

  JitPageReference jit_page(it->second, it->first);

  // If the address is not in the range of the jit page, return.
  if (jit_page.End() <= addr) {
    return {};
  }

  if (jit_page.End() >= end) {
    return jit_page;
  }

  // It's possible that the allocation spans multiple pages, merge them.
  auto to_delete_start = ++it;
  for (; jit_page.End() < end && it != trusted_data_.jit_pages_->end(); it++) {
    {
      JitPageReference next_page(it->second, it->first);
      CHECK_EQ(next_page.Address(), jit_page.End());
      jit_page.Merge(next_page);
    }
    Delete(it->second);
  }

  trusted_data_.jit_pages_->erase(to_delete_start, it);

  if (jit_page.End() < end) {
    return {};
  }

  return jit_page;
}

namespace {

size_t GetSize(ThreadIsolation::JitPage* jit_page) {
  return ThreadIsolation::JitPageReference(jit_page, 0).Size();
}

size_t GetSize(ThreadIsolation::JitAllocation allocation) {
  return allocation.Size();
}

template <class T>
void CheckForRegionOverlap(const T& map, Address addr, size_t size) {
  // The data is untrusted from the pov of CFI, so we check that there's no
  // overlaps with existing regions etc.
  CHECK_GE(addr + size, addr);

  // Find an entry in the map with key > addr
  auto it = map.upper_bound(addr);
  bool is_begin = it == map.begin();
  bool is_end = it == map.end();

  // Check for overlap with the next entry
  if (!is_end) {
    Address next_addr = it->first;
    Address offset = next_addr - addr;
    CHECK_LE(size, offset);
  }

  // Check the previous entry for overlap
  if (!is_begin) {
    it--;
    Address prev_addr = it->first;
    const typename T::value_type::second_type& prev_entry = it->second;
    Address offset = addr - prev_addr;
    CHECK_LE(GetSize(prev_entry), offset);
  }
}

template <typename Iterator>
bool AllocationIsBehindRange(Address range_start, Address range_size,
                             const Iterator& it) {
  Address range_end = range_start + range_size;
  Address allocation_start = it->first;
  Address allocation_size = it->second.Size();
  Address allocation_end = allocation_start + allocation_size;

  if (allocation_start >= range_end) return true;

  CHECK_LE(allocation_end, range_end);
  return false;
}

}  // namespace

ThreadIsolation::JitPageReference::JitPageReference(class JitPage* jit_page,
                                                    base::Address address)
    : page_lock_(&jit_page->mutex_), jit_page_(jit_page), address_(address) {}

ThreadIsolation::JitPage::~JitPage() {
  // TODO(sroettger): check that the page is not in use (scan shadow stacks).
}

size_t ThreadIsolation::JitPageReference::Size() const {
  return jit_page_->size_;
}

void ThreadIsolation::JitPageReference::Shrink(class JitPage* tail) {
  jit_page_->size_ -= tail->size_;
  // Move all allocations that are out of bounds.
  auto it = jit_page_->allocations_.lower_bound(End());
  tail->allocations_.insert(it, jit_page_->allocations_.end());
  jit_page_->allocations_.erase(it, jit_page_->allocations_.end());
}

void ThreadIsolation::JitPageReference::Expand(size_t offset) {
  jit_page_->size_ += offset;
}

void ThreadIsolation::JitPageReference::Merge(JitPageReference& next) {
  DCHECK_EQ(End(), next.Address());
  jit_page_->size_ += next.jit_page_->size_;
  next.jit_page_->size_ = 0;
  jit_page_->allocations_.merge(next.jit_page_->allocations_);
  DCHECK(next.jit_page_->allocations_.empty());
}

ThreadIsolation::JitAllocation&
ThreadIsolation::JitPageReference::RegisterAllocation(base::Address addr,
                                                      size_t size,
                                                      JitAllocationType type) {
  // The data is untrusted from the pov of CFI, so the checks are security
  // sensitive.
  CHECK_GE(addr, address_);
  base::Address offset = addr - address_;
  base::Address end_offset = offset + size;
  CHECK_GT(end_offset, offset);
  CHECK_GT(jit_page_->size_, offset);
  CHECK_GE(jit_page_->size_, end_offset);

  CheckForRegionOverlap(jit_page_->allocations_, addr, size);
  return jit_page_->allocations_.emplace(addr, JitAllocation(size, type))
      .first->second;
}

ThreadIsolation::JitAllocation&
ThreadIsolation::JitPageReference::LookupAllocation(base::Address addr,
                                                    size_t size,
                                                    JitAllocationType type) {
  auto it = jit_page_->allocations_.find(addr);
  CHECK_NE(it, jit_page_->allocations_.end());
  CHECK_EQ(it->second.Size(), size);
  CHECK_EQ(it->second.Type(), type);
  return it->second;
}

void ThreadIsolation::JitPageReference::UnregisterAllocation(
    base::Address addr) {
  // TODO(sroettger): check that the memory is not in use (scan shadow stacks).
  CHECK_EQ(jit_page_->allocations_.erase(addr), 1);
}

void ThreadIsolation::JitPageReference::UnregisterRange(base::Address start,
                                                        size_t size) {
  auto begin = jit_page_->allocations_.lower_bound(start);
  auto end = begin;
  while (end != jit_page_->allocations_.end() &&
         !AllocationIsBehindRange(start, size, end)) {
    end++;
  }

  // TODO(sroettger): check that the memory is not in use (scan shadow stacks).
  jit_page_->allocations_.erase(begin, end);
}

void ThreadIsolation::JitPageReference::UnregisterAllocationsExcept(
    base::Address start, size_t size, const std::vector<base::Address>& keep) {
  // TODO(sroettger): check that the page is not in use (scan shadow stacks).
  JitPage::AllocationMap keep_allocations;

  auto keep_before = jit_page_->allocations_.lower_bound(start);
  auto keep_after = jit_page_->allocations_.lower_bound(start + size);

  // keep all allocations before the start address.
  if (keep_before != jit_page_->allocations_.begin()) {
    keep_before--;
    keep_allocations.insert(jit_page_->allocations_.begin(), keep_before);
  }

  // from the start address, keep only allocations passed in the vector
  auto keep_iterator = keep.begin();
  for (auto it = keep_before; it != keep_after; it++) {
    if (keep_iterator == keep.end()) break;
    if (it->first == *keep_iterator) {
      keep_allocations.emplace_hint(keep_allocations.end(), it->first,
                                    it->second);
      keep_iterator++;
    }
  }
  CHECK_EQ(keep_iterator, keep.end());

  // keep all allocations after the region
  keep_allocations.insert(keep_after, jit_page_->allocations_.end());

  jit_page_->allocations_.swap(keep_allocations);
}

base::Address ThreadIsolation::JitPageReference::StartOfAllocationAt(
    base::Address inner_pointer) {
  return AllocationContaining(inner_pointer).first;
}

std::pair<base::Address, ThreadIsolation::JitAllocation&>
ThreadIsolation::JitPageReference::AllocationContaining(
    base::Address inner_pointer) {
  auto it = jit_page_->allocations_.upper_bound(inner_pointer);
  CHECK_NE(it, jit_page_->allocations_.begin());
  it--;
  size_t offset = inner_pointer - it->first;
  CHECK_GT(it->second.Size(), offset);
  return {it->first, it->second};
}

// static
void ThreadIsolation::RegisterJitPage(Address address, size_t size) {
  CFIMetadataWriteScope write_scope("Adding new executable memory.");

  base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
  CheckForRegionOverlap(*trusted_data_.jit_pages_, address, size);
  JitPage* jit_page;
  ConstructNew(&jit_page, size);
  trusted_data_.jit_pages_->emplace(address, jit_page);
}

void ThreadIsolation::UnregisterJitPage(Address address, size_t size) {
  // TODO(sroettger): merge the write scopes higher up.
  CFIMetadataWriteScope write_scope("Removing executable memory.");

  JitPage* to_delete;
  {
    base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
    JitPageReference jit_page = LookupJitPageLocked(address, size);

    // We're merging jit pages together, so potentially split them back up
    // if we're only freeing a subrange.

    Address to_free_end = address + size;
    Address jit_page_end = jit_page.Address() + jit_page.Size();

    if (to_free_end < jit_page_end) {
      // There's a tail after the page that we release. Shrink the page and
      // add the tail to the map.
      size_t tail_size = jit_page_end - to_free_end;
      JitPage* tail;
      ConstructNew(&tail, tail_size);
      jit_page.Shrink(tail);
      trusted_data_.jit_pages_->emplace(to_free_end, tail);
    }

    DCHECK_EQ(to_free_end, jit_page.Address() + jit_page.Size());

    if (address == jit_page.Address()) {
      // We remove the start of the region, just remove it from the map.
      to_delete = jit_page.JitPage();
      trusted_data_.jit_pages_->erase(address);
    } else {
      // Otherwise, we need to shrink the region.
      DCHECK_GT(address, jit_page.Address());
      JitPage* tail;
      ConstructNew(&tail, size);
      jit_page.Shrink(tail);
      to_delete = tail;
    }
  }
  Delete(to_delete);
}

// static
bool ThreadIsolation::MakeExecutable(Address address, size_t size) {
  DCHECK(Enabled());

  // TODO(sroettger): ensure that this can only happen at prcoess startup.

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  return base::MemoryProtectionKey::SetPermissionsAndKey(
      {address, size}, PageAllocator::Permission::kReadWriteExecute, pkey());
#else   // V8_HAS_PKU_JIT_WRITE_PROTECT
  UNREACHABLE();
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT
}

// static
WritableJitAllocation ThreadIsolation::RegisterJitAllocation(
    Address obj, size_t size, JitAllocationType type) {
  return WritableJitAllocation(
      obj, size, type, WritableJitAllocation::JitAllocationSource::kRegister);
}

// static
WritableJitAllocation ThreadIsolation::RegisterInstructionStreamAllocation(
    Address addr, size_t size) {
  return RegisterJitAllocation(addr, size,
                               JitAllocationType::kInstructionStream);
}

// static
WritableJitAllocation ThreadIsolation::LookupJitAllocation(
    Address addr, size_t size, JitAllocationType type) {
  return WritableJitAllocation(
      addr, size, type, WritableJitAllocation::JitAllocationSource::kLookup);
}

// static
WritableJumpTablePair ThreadIsolation::LookupJumpTableAllocations(
    Address jump_table_address, size_t jump_table_size,
    Address far_jump_table_address, size_t far_jump_table_size) {
  return WritableJumpTablePair(jump_table_address, jump_table_size,
                               far_jump_table_address, far_jump_table_size);
}

// static
void ThreadIsolation::RegisterJitAllocations(Address start,
                                             const std::vector<size_t>& sizes,
                                             JitAllocationType type) {
  CFIMetadataWriteScope write_scope("Register bulk allocations.");

  size_t total_size = 0;
  for (auto size : sizes) {
    total_size += size;
  }

  constexpr size_t kSplitThreshold = 0x40000;
  JitPageReference page_ref = total_size >= kSplitThreshold
                                  ? SplitJitPage(start, total_size)
                                  : LookupJitPage(start, total_size);

  for (auto size : sizes) {
    page_ref.RegisterAllocation(start, size, type);
    start += size;
  }
}

void ThreadIsolation::RegisterJitAllocationForTesting(Address obj,
                                                      size_t size) {
  RegisterJitAllocation(obj, size, JitAllocationType::kInstructionStream);
}

// static
void ThreadIsolation::UnregisterJitAllocationForTesting(Address addr,
                                                        size_t size) {
  LookupJitPage(addr, size).UnregisterAllocation(addr);
}

// static
void ThreadIsolation::UnregisterWasmAllocation(Address addr, size_t size) {
  CFIMetadataWriteScope write_scope("UnregisterWasmAllocation");
  LookupJitPage(addr, size).UnregisterAllocation(addr);
}

ThreadIsolation::JitPageReference ThreadIsolation::SplitJitPage(Address addr,
                                                                size_t size) {
  base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
  return SplitJitPageLocked(addr, size);
}

ThreadIsolation::JitPageReference ThreadIsolation::SplitJitPageLocked(
    Address addr, size_t size) {
  trusted_data_.jit_pages_mutex_->AssertHeld();

  JitPageReference jit_page = LookupJitPageLocked(addr, size);

  // Split the JitPage into upto three pages.
  size_t head_size = addr - jit_page.Address();
  size_t tail_size = jit_page.Size() - size - head_size;
  if (tail_size > 0) {
    JitPage* tail;
    ConstructNew(&tail, tail_size);
    jit_page.Shrink(tail);
    trusted_data_.jit_pages_->emplace(addr + size, tail);
  }
  if (head_size > 0) {
    JitPage* mid;
    ConstructNew(&mid, size);
    jit_page.Shrink(mid);
    trusted_data_.jit_pages_->emplace(addr, mid);
    return JitPageReference(mid, addr);
  }

  return jit_page;
}

std::pair<ThreadIsolation::JitPageReference, ThreadIsolation::JitPageReference>
ThreadIsolation::SplitJitPages(Address addr1, size_t size1, Address addr2,
                               size_t size2) {
  if (addr1 > addr2) {
    auto reversed_pair = SplitJitPages(addr2, size2, addr1, size1);
    return {std::move(reversed_pair.second), std::move(reversed_pair.first)};
  }
  // Make sure there's no overlap. SplitJitPageLocked will do additional checks
  // that the sizes don't overflow.
  CHECK_LE(addr1 + size1, addr2);

  base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
  return {SplitJitPageLocked(addr1, size1), SplitJitPageLocked(addr2, size2)};
}

// static
std::optional<Address> ThreadIsolation::StartOfJitAllocationAt(
    Address inner_pointer) {
  CFIMetadataWriteScope write_scope("StartOfJitAllocationAt");
  std::optional<JitPageReference> page = TryLookupJitPage(inner_pointer, 1);
  if (!page) {
    return {};
  }
  return page->StartOfAllocationAt(inner_pointer);
}

// static
bool ThreadIsolation::WriteProtectMemory(
    Address addr, size_t size, PageAllocator::Permission page_permissions) {
  if (!Enabled()) {
    return true;
  }

#if V8_HEAP_USE_PKU_JIT_WRITE_PROTECT
  return base::MemoryProtectionKey::SetPermissionsAndKey(
      {addr, size}, PageAllocator::Permission::kNoAccess,
      ThreadIsolation::pkey());
#else
  UNREACHABLE();
#endif
}

namespace {

class MutexUnlocker {
 public:
  explicit MutexUnlocker(base::Mutex& mutex) : mutex_(mutex) {
    mutex_.AssertHeld();
  }

  ~MutexUnlocker() {
    mutex_.AssertHeld();
    mutex_.Unlock();
  }

 private:
  base::Mutex& mutex_;
};

}  // namespace

// static
bool ThreadIsolation::CanLookupStartOfJitAllocationAt(Address inner_pointer) {
  CFIMetadataWriteScope write_scope("CanLookupStartOfJitAllocationAt");

  // Try to lock the pages mutex and the mutex of the page itself to prevent
  // potential dead locks. The profiler can try to do a lookup from a signal
  // handler. If that signal handler runs while the thread locked one of these
  // mutexes, it would result in a dead lock.
  bool pages_mutex_locked = trusted_data_.jit_pages_mutex_->TryLock();
  if (!pages_mutex_locked) {
    return false;
  }
  MutexUnlocker pages_mutex_unlocker(*trusted_data_.jit_pages_mutex_);

  // upper_bound gives us an iterator to the position after address.
  auto it = trusted_data_.jit_pages_->upper_bound(inner_pointer);

  // The previous page should be the one we're looking for.
  if (it == trusted_data_.jit_pages_->begin()) {
    return {};
  }
  it--;

  JitPage* jit_page = it->second;
  bool jit_page_locked = jit_page->mutex_.TryLock();
  if (!jit_page_locked) {
    return false;
  }
  jit_page->mutex_.Unlock();

  return true;
}

// static
WritableJitAllocation WritableJitAllocation::ForInstructionStream(
    Tagged<InstructionStream> istream) {
  return WritableJitAllocation(
      istream->address(), istream->Size(),
      ThreadIsolation::JitAllocationType::kInstructionStream,
      JitAllocationSource::kLookup);
}

template <size_t offset>
void WritableFreeSpace::ClearTagged(size_t count) const {
  base::Address start = address_ + offset;
  // TODO(v8:13355): add validation before the write.
  MemsetTagged(ObjectSlot(start), Tagged<Object>(kClearedFreeMemoryValue),
               count);
}

template void WritableFreeSpace::ClearTagged<kTaggedSize>(size_t count) const;
template void WritableFreeSpace::ClearTagged<2 * kTaggedSize>(
    size_t count) const;

#if DEBUG

// static
void ThreadIsolation::CheckTrackedMemoryEmpty() {
  DCHECK(trusted_data_.jit_pages_->empty());
}

#endif  // DEBUG

}  // namespace internal
}  // namespace v8
