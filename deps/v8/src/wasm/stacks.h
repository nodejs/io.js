// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_STACKS_H_
#define V8_WASM_STACKS_H_

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#include "src/common/globals.h"
#include "src/utils/allocation.h"

namespace v8 {
class Isolate;
}

namespace v8::internal::wasm {

struct JumpBuffer {
  Address sp;
  Address fp;
  Address pc;
  void* stack_limit;
  enum StackState : int32_t { Active, Inactive, Retired };
  StackState state;
};

constexpr int kJmpBufSpOffset = offsetof(JumpBuffer, sp);
constexpr int kJmpBufFpOffset = offsetof(JumpBuffer, fp);
constexpr int kJmpBufPcOffset = offsetof(JumpBuffer, pc);
constexpr int kJmpBufStackLimitOffset = offsetof(JumpBuffer, stack_limit);
constexpr int kJmpBufStateOffset = offsetof(JumpBuffer, state);

class StackMemory {
 public:
  static constexpr ExternalPointerTag kManagedTag = kWasmStackMemoryTag;

  static std::unique_ptr<StackMemory> New() {
    return std::unique_ptr<StackMemory>(new StackMemory());
  }

  // Returns a non-owning view of the current (main) stack. This may be
  // the simulator's stack when running on the simulator.
  static StackMemory* GetCurrentStackView(Isolate* isolate);

  ~StackMemory();
  void* jslimit() const { return limit_ + kJSLimitOffsetKB * KB; }
  Address base() const { return reinterpret_cast<Address>(limit_ + size_); }
  JumpBuffer* jmpbuf() { return &jmpbuf_; }
  bool Contains(Address addr) {
    return reinterpret_cast<Address>(jslimit()) <= addr && addr < base();
  }
  int id() { return id_; }
  bool IsActive() { return jmpbuf_.state == JumpBuffer::Active; }
  void set_index(size_t index) { index_ = index; }
  size_t index() { return index_; }

#ifdef DEBUG
  static constexpr int kJSLimitOffsetKB = 80;
#else
  static constexpr int kJSLimitOffsetKB = 40;
#endif

  friend class StackPool;

 private:
  // This constructor allocates a new stack segment.
  StackMemory();

  // Overload to represent a view of the libc stack.
  StackMemory(uint8_t* limit, size_t size);

  uint8_t* limit_;
  size_t size_;
  bool owned_;
  JumpBuffer jmpbuf_;
  // Stable ID.
  int id_;
  // Index of this stack in the global Isolate::wasm_stacks() vector. This
  // allows us to add and remove from the vector in constant time (see
  // return_switch()).
  size_t index_;
};

// A pool of "finished" stacks, i.e. stacks whose last frame have returned and
// whose memory can be reused for new suspendable computations.
class StackPool {
 public:
  // Gets a stack from the free list if one exists, else allocates it.
  std::unique_ptr<StackMemory> GetOrAllocate();
  // Adds a finished stack to the free list.
  void Add(std::unique_ptr<StackMemory> stack);
  // Decommit the stack memories and empty the freelist.
  void ReleaseFinishedStacks();
  size_t Size() const;

 private:
  std::vector<std::unique_ptr<StackMemory>> freelist_;
  size_t size_ = 0;
  // If the next finished stack would move the total size above this limit, the
  // stack is freed instead of being added to the free list.
  static constexpr int kMaxSize = 4 * MB;
};

}  // namespace v8::internal::wasm

#endif  // V8_WASM_STACKS_H_
