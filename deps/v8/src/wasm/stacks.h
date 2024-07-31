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
  static StackMemory* New(Isolate* isolate) { return new StackMemory(isolate); }

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

  // Insert a stack in the linked list after this stack.
  void Add(StackMemory* stack);

  StackMemory* next() { return next_; }

  // Track external memory usage for Managed<StackMemory> objects.
  size_t owned_size() { return sizeof(StackMemory) + (owned_ ? size_ : 0); }
  bool IsActive() { return jmpbuf_.state == JumpBuffer::Active; }

#ifdef DEBUG
  static constexpr int kJSLimitOffsetKB = 80;
#else
  static constexpr int kJSLimitOffsetKB = 40;
#endif

 private:
  // This constructor allocates a new stack segment.
  explicit StackMemory(Isolate* isolate);

  // Overload to represent a view of the libc stack.
  StackMemory(Isolate* isolate, uint8_t* limit, size_t size);

  Isolate* isolate_;
  uint8_t* limit_;
  size_t size_;
  bool owned_;
  JumpBuffer jmpbuf_;
  int id_;
  // Stacks form a circular doubly linked list per isolate.
  StackMemory* next_ = this;
  StackMemory* prev_ = this;
};

}  // namespace v8::internal::wasm

#endif  // V8_WASM_STACKS_H_
