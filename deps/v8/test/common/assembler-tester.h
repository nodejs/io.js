// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TEST_COMMON_ASSEMBLER_TESTER_H_
#define V8_TEST_COMMON_ASSEMBLER_TESTER_H_

#include <memory>

#include "src/codegen/assembler.h"
#include "src/codegen/flush-instruction-cache.h"
#include "src/common/code-memory-access.h"

namespace v8 {
namespace internal {

class TestingAssemblerBuffer : public AssemblerBuffer {
 public:
  TestingAssemblerBuffer(size_t requested, void* address,
                         JitPermission jit_permission = JitPermission::kNoJit)
      : protection_reconfiguration_is_allowed_(true) {
    size_t page_size = v8::internal::AllocatePageSize();
    size_t alloc_size = RoundUp(requested, page_size);
    CHECK_GE(kMaxInt, alloc_size);
    reservation_ = VirtualMemory(
        GetPlatformPageAllocator(), alloc_size, address, page_size,
        jit_permission == JitPermission::kNoJit
            ? v8::PageAllocator::Permission::kNoAccess
            : v8::PageAllocator::Permission::kNoAccessWillJitLater);
    CHECK(reservation_.IsReserved());
    MakeWritable();
  }

  ~TestingAssemblerBuffer() override { reservation_.Free(); }

  uint8_t* start() const override {
    return reinterpret_cast<uint8_t*>(reservation_.address());
  }

  int size() const override { return static_cast<int>(reservation_.size()); }

  std::unique_ptr<AssemblerBuffer> Grow(int new_size) override {
    FATAL("Cannot grow TestingAssemblerBuffer");
  }

  std::unique_ptr<AssemblerBuffer> CreateView() const {
    return ExternalAssemblerBuffer(start(), size());
  }

  void MakeExecutable() {
    // Flush the instruction cache as part of making the buffer executable.
    // Note: we do this before setting permissions to ReadExecute because on
    // some older ARM kernels there is a bug which causes an access error on
    // cache flush instructions to trigger access error on non-writable memory.
    // See https://bugs.chromium.org/p/v8/issues/detail?id=8157
    FlushInstructionCache(start(), size());

    if (protection_reconfiguration_is_allowed_) {
      bool result = SetPermissions(GetPlatformPageAllocator(), start(), size(),
                                   v8::PageAllocator::kReadExecute);
      CHECK(result);
    }
  }

  void MakeWritable() {
    if (protection_reconfiguration_is_allowed_) {
      bool result = SetPermissions(GetPlatformPageAllocator(), start(), size(),
                                   v8::PageAllocator::kReadWrite);
      CHECK(result);
    }
  }

  void MakeWritableAndExecutable() {
    bool result = SetPermissions(GetPlatformPageAllocator(), start(), size(),
                                 v8::PageAllocator::kReadWriteExecute);
    CHECK(result);
    // Once buffer protection is set to RWX it might not be allowed to be
    // changed anymore.
    protection_reconfiguration_is_allowed_ =
        !V8_HEAP_USE_PTHREAD_JIT_WRITE_PROTECT &&
        !V8_HEAP_USE_BECORE_JIT_WRITE_PROTECT &&
        protection_reconfiguration_is_allowed_;
  }

 private:
  VirtualMemory reservation_;
  bool protection_reconfiguration_is_allowed_;
};

// This scope class is mostly necesasry for arm64 tests running on Apple Silicon
// (M1) which prohibits reconfiguration of page permissions for RWX pages.
// Instead of altering the page permissions one must flip the X-W state by
// calling pthread_jit_write_protect_np() function.
// See RwxMemoryWriteScope for details.
class V8_NODISCARD AssemblerBufferWriteScope final {
 public:
  explicit AssemblerBufferWriteScope(TestingAssemblerBuffer& buffer)
      : buffer_(buffer) {
    buffer_.MakeWritable();
  }

  ~AssemblerBufferWriteScope() { buffer_.MakeExecutable(); }

  // Disable copy constructor and copy-assignment operator, since this manages
  // a resource and implicit copying of the scope can yield surprising errors.
  AssemblerBufferWriteScope(const AssemblerBufferWriteScope&) = delete;
  AssemblerBufferWriteScope& operator=(const AssemblerBufferWriteScope&) =
      delete;

 private:
  RwxMemoryWriteScopeForTesting rwx_write_scope_;
  TestingAssemblerBuffer& buffer_;
};

static inline std::unique_ptr<TestingAssemblerBuffer> AllocateAssemblerBuffer(
    size_t requested = v8::internal::AssemblerBase::kDefaultBufferSize,
    void* address = nullptr,
    JitPermission jit_permission = JitPermission::kMapAsJittable) {
  return std::make_unique<TestingAssemblerBuffer>(requested, address,
                                                  jit_permission);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TEST_COMMON_ASSEMBLER_TESTER_H_
