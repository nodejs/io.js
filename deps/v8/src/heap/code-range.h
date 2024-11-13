// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CODE_RANGE_H_
#define V8_HEAP_CODE_RANGE_H_

#include <unordered_map>
#include <vector>

#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/utils/allocation.h"
#include "v8-internal.h"

namespace v8 {
namespace internal {

// The process-wide singleton that keeps track of code range regions with the
// intention to reuse free code range regions as a workaround for CFG memory
// leaks (see crbug.com/870054).
class CodeRangeAddressHint {
 public:
  // When near code range is enabled, an address within
  // kMaxPCRelativeCodeRangeInMB to the embedded blob is returned if
  // there is enough space. Otherwise a random address is returned.
  // When near code range is disabled, returns the most recently freed code
  // range start address for the given size. If there is no such entry, then a
  // random address is returned.
  V8_EXPORT_PRIVATE Address GetAddressHint(size_t code_range_size,
                                           size_t alignment);

  V8_EXPORT_PRIVATE void NotifyFreedCodeRange(Address code_range_start,
                                              size_t code_range_size);

 private:
  base::Mutex mutex_;
  // A map from code range size to an array of recently freed code range
  // addresses. There should be O(1) different code range sizes.
  // The length of each array is limited by the peak number of code ranges,
  // which should be also O(1).
  std::unordered_map<size_t, std::vector<Address>> recently_freed_;
};

// A code range is a virtual memory cage that may contain executable code. It
// has the following layout.
//
// +---------+-----+-----------------  ~~~  -+
// |   RW    | ... |     ...                 |
// +---------+-----+------------------ ~~~  -+
// ^               ^
// base            allocatable base
//
// <-------->      <------------------------->
//  reserved            allocatable region
// <----------------------------------------->
//                 CodeRange
//
// The start of the reservation may include reserved page with read-write access
// as required by some platforms (Win64) followed by an unmapped region which
// make allocatable base MemoryChunk::kAlignment-aligned. The cage's page
// allocator explicitly marks the optional reserved page as occupied, so it's
// excluded from further allocations.
//
// The following conditions hold:
// 1) |reservation()->region()| == [base(), base() + size()[,
// 2) if optional RW pages are not necessary, then |base| == |allocatable base|,
// 3) both |base| and |allocatable base| are MemoryChunk::kAlignment-aligned.
class CodeRange final : public VirtualMemoryCage {
 public:
  V8_EXPORT_PRIVATE ~CodeRange() override;

  // Returns the size of the initial area of a code range, which is marked
  // writable and reserved to contain unwind information.
  static size_t GetWritableReservedAreaSize();

  uint8_t* embedded_blob_code_copy() const {
    // remap_embedded_builtins_mutex_ is designed to protect write contention to
    // embedded_blob_code_copy_. It is safe to be read without taking the
    // mutex. It is read to check if short builtins ought to be enabled because
    // a shared CodeRange has already remapped builtins and to find where the
    // instruction stream for a builtin is.
    //
    // For the first, this racing with an Isolate calling RemapEmbeddedBuiltins
    // may result in disabling short builtins, which is not a correctness issue.
    //
    // For the second, this racing with an Isolate calling RemapEmbeddedBuiltins
    // may result in an already running Isolate that did not have short builtins
    // enabled (due to max old generation size) to switch over to using remapped
    // builtins, which is also not a correctness issue as the remapped builtins
    // are byte-equivalent.
    //
    // Both these scenarios should be rare. The initial Isolate is usually
    // created by itself, i.e. without contention. Additionally, the first
    // Isolate usually remaps builtins on machines with enough memory, not
    // subsequent Isolates in the same process.
    return embedded_blob_code_copy_.load(std::memory_order_acquire);
  }

  bool InitReservation(v8::PageAllocator* page_allocator, size_t requested);

  V8_EXPORT_PRIVATE void Free();

  // Remap and copy the embedded builtins into this CodeRange. This method is
  // idempotent and only performs the copy once. This property is so that this
  // method can be used uniformly regardless of whether there is a single global
  // pointer address space or multiple pointer cages. Returns the address of
  // the copy.
  //
  // The builtins code region will be freed with the code range at tear down.
  //
  // When ENABLE_SLOW_DCHECKS is on, the contents of the embedded_blob_code are
  // compared against the already copied version.
  uint8_t* RemapEmbeddedBuiltins(Isolate* isolate,
                                 const uint8_t* embedded_blob_code,
                                 size_t embedded_blob_code_size);

 private:
  static base::AddressRegion GetPreferredRegion(size_t radius_in_megabytes,
                                                size_t allocate_page_size);

  // Used when short builtin calls are enabled, where embedded builtins are
  // copied into the CodeRange so calls can be nearer.
  std::atomic<uint8_t*> embedded_blob_code_copy_{nullptr};

  // When sharing a CodeRange among Isolates, calls to RemapEmbeddedBuiltins may
  // race during Isolate::Init.
  base::Mutex remap_embedded_builtins_mutex_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CODE_RANGE_H_
