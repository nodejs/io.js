// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INIT_ISOLATE_ALLOCATOR_H_
#define V8_INIT_ISOLATE_ALLOCATOR_H_

#include <memory>

#include "src/base/page-allocator.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

// TODO(v8:13788): remove IsolateAllocator, as it's no longer needed.
//
// IsolateAllocator manages the lifetime of any virtual memory resources
// associated with an Isolate.  When pointer compression is enabled, this is
// either the process-wide shared pointer compression cage
// (V8_COMPRESS_POINTERS_IN_SHARED_CAGE), or the per-isolate pointer compression
// cage (V8_COMPRESS_POINTERS_IN_ISOLATE_CAGE).  When the sandbox is enabled
// (V8_ENABLE_SANDBOX), this additionally includes the process-wide sandbox
// reservation, as well as the trusted pointer cages.
class V8_EXPORT_PRIVATE IsolateAllocator final {
 public:
  IsolateAllocator();
  IsolateAllocator(const IsolateAllocator&) = delete;
  IsolateAllocator& operator=(const IsolateAllocator&) = delete;

  v8::PageAllocator* page_allocator() const { return page_allocator_; }

  Address GetPtrComprCageBase() const {
    return COMPRESS_POINTERS_BOOL ? GetPtrComprCage()->base() : kNullAddress;
  }

  Address GetTrustedPtrComprCageBase() const {
    return COMPRESS_POINTERS_BOOL ? GetTrustedPtrComprCage()->base()
                                  : kNullAddress;
  }

  // When pointer compression is on, return the pointer compression
  // cage. Otherwise return nullptr.
  VirtualMemoryCage* GetPtrComprCage();
  const VirtualMemoryCage* GetPtrComprCage() const;

  const VirtualMemoryCage* GetTrustedPtrComprCage() const;

  static void InitializeOncePerProcess();

 private:
  friend class PoolTest;
  // Only used for testing.
  static void FreeProcessWidePtrComprCageForTesting();

  v8::PageAllocator* page_allocator_ = nullptr;
#ifdef V8_COMPRESS_POINTERS_IN_ISOLATE_CAGE
  VirtualMemoryCage isolate_ptr_compr_cage_;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_INIT_ISOLATE_ALLOCATOR_H_
