// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_STACK_H_
#define V8_REGEXP_REGEXP_STACK_H_

#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class RegExpStack;

// Maintains a per-v8thread stack area that can be used by irregexp
// implementation for its backtracking stack.
class V8_NODISCARD RegExpStackScope final {
 public:
  // Create and delete an instance to control the life-time of a growing stack.

  // Initializes the stack memory area if necessary.
  explicit RegExpStackScope(Isolate* isolate);
  ~RegExpStackScope();  // Releases the stack if it has grown.
  RegExpStackScope(const RegExpStackScope&) = delete;
  RegExpStackScope& operator=(const RegExpStackScope&) = delete;

  RegExpStack* stack() const { return regexp_stack_; }

 private:
  RegExpStack* const regexp_stack_;
  const ptrdiff_t old_sp_top_delta_;
};

class RegExpStack final {
 public:
  RegExpStack();
  ~RegExpStack();
  RegExpStack(const RegExpStack&) = delete;
  RegExpStack& operator=(const RegExpStack&) = delete;

#if defined(V8_TARGET_ARCH_PPC64) || defined(V8_TARGET_ARCH_S390X)
  static constexpr int kSlotSize = kSystemPointerSize;
#else
  static constexpr int kSlotSize = kInt32Size;
#endif
  // Number of allocated locations on the stack below the limit. No sequence of
  // pushes must be longer than this without doing a stack-limit check.
  static constexpr int kStackLimitSlackSlotCount = 32;
  static constexpr int kStackLimitSlackSize =
      kStackLimitSlackSlotCount * kSlotSize;

  Address begin() const {
    return reinterpret_cast<Address>(thread_local_.memory_);
  }
  Address end() const {
    DCHECK_NE(0, thread_local_.memory_size_);
    DCHECK_EQ(thread_local_.memory_top_,
              thread_local_.memory_ + thread_local_.memory_size_);
    return reinterpret_cast<Address>(thread_local_.memory_top_);
  }
  Address memory_top() const { return end(); }

  Address stack_pointer() const {
    return reinterpret_cast<Address>(thread_local_.stack_pointer_);
  }

  size_t memory_size() const { return thread_local_.memory_size_; }

  // If the stack pointer gets below the limit, we should react and
  // either grow the stack or report an out-of-stack exception.
  // There is only a limited number of locations below the stack limit,
  // so users of the stack should check the stack limit during any
  // sequence of pushes longer that this.
  Address* limit_address_address() { return &thread_local_.limit_; }

  // Ensures that there is a memory area with at least the specified size.
  // If passing zero, the default/minimum size buffer is allocated.
  Address EnsureCapacity(size_t size);

  // Thread local archiving.
  static constexpr int ArchiveSpacePerThread() {
    return static_cast<int>(kThreadLocalSize);
  }
  char* ArchiveStack(char* to);
  char* RestoreStack(char* from);
  void FreeThreadResources() { thread_local_.ResetToStaticStack(this); }

  // Maximal size of allocated stack area.
  static constexpr size_t kMaximumStackSize = 64 * MB;

 private:
  // Artificial limit used when the thread-local state has been destroyed.
  static const Address kMemoryTop =
      static_cast<Address>(static_cast<uintptr_t>(-1));

  // In addition to dynamically-allocated, variable-sized stacks, we also have
  // a statically allocated and sized area that is used whenever no dynamic
  // stack is allocated. This guarantees that a stack is always available and
  // we can skip availability-checks later on.
  static constexpr size_t kStaticStackSize = 1 * KB;
  // It's at least double the slack size to ensure that we have a bit of
  // breathing room before NativeRegExpMacroAssembler::GrowStack must be
  // called.
  static_assert(kStaticStackSize >= 2 * kStackLimitSlackSize);
  static_assert(kStaticStackSize <= kMaximumStackSize);
  uint8_t static_stack_[kStaticStackSize] = {0};

  // Minimal size of dynamically-allocated stack area.
  static constexpr size_t kMinimumDynamicStackSize = 2 * KB;
  static_assert(kMinimumDynamicStackSize == 2 * kStaticStackSize);

  // Structure holding the allocated memory, size and limit. Thread switching
  // archives and restores this struct.
  struct ThreadLocal {
    explicit ThreadLocal(RegExpStack* regexp_stack) {
      ResetToStaticStack(regexp_stack);
    }

    // If memory_size_ > 0 then
    //  - memory_, memory_top_, stack_pointer_ must be non-nullptr
    //  - memory_top_ = memory_ + memory_size_
    //  - memory_ <= stack_pointer_ <= memory_top_
    uint8_t* memory_ = nullptr;
    uint8_t* memory_top_ = nullptr;
    size_t memory_size_ = 0;
    uint8_t* stack_pointer_ = nullptr;
    Address limit_ = kNullAddress;
    bool owns_memory_ = false;  // Whether memory_ is owned and must be freed.

    void ResetToStaticStack(RegExpStack* regexp_stack);
    void ResetToStaticStackIfEmpty(RegExpStack* regexp_stack) {
      if (stack_pointer_ == memory_top_) ResetToStaticStack(regexp_stack);
    }
    void FreeAndInvalidate();
  };
  static constexpr size_t kThreadLocalSize = sizeof(ThreadLocal);

  Address memory_top_address_address() {
    return reinterpret_cast<Address>(&thread_local_.memory_top_);
  }

  Address stack_pointer_address() {
    return reinterpret_cast<Address>(&thread_local_.stack_pointer_);
  }

  // A position-independent representation of the stack pointer.
  ptrdiff_t sp_top_delta() const {
    ptrdiff_t result =
        reinterpret_cast<intptr_t>(thread_local_.stack_pointer_) -
        reinterpret_cast<intptr_t>(thread_local_.memory_top_);
    DCHECK_LE(result, 0);
    return result;
  }

  // Resets the buffer if it has grown beyond the default/minimum size and is
  // empty.
  void ResetIfEmpty() { thread_local_.ResetToStaticStackIfEmpty(this); }

  // Whether the ThreadLocal storage has been invalidated.
  bool IsValid() const { return thread_local_.memory_ != nullptr; }

  ThreadLocal thread_local_;

  friend class ExternalReference;
  friend class RegExpStackScope;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_STACK_H_
