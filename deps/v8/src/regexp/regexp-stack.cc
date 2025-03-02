// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-stack.h"

#include "src/execution/isolate.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {

RegExpStackScope::RegExpStackScope(Isolate* isolate)
    : regexp_stack_(isolate->regexp_stack()),
      old_sp_top_delta_(regexp_stack_->sp_top_delta()) {
  DCHECK(regexp_stack_->IsValid());
}

RegExpStackScope::~RegExpStackScope() {
  CHECK_EQ(old_sp_top_delta_, regexp_stack_->sp_top_delta());
  regexp_stack_->ResetIfEmpty();
}

RegExpStack::RegExpStack() : thread_local_(this) {}

RegExpStack::~RegExpStack() { thread_local_.FreeAndInvalidate(); }

char* RegExpStack::ArchiveStack(char* to) {
  if (!thread_local_.owns_memory_) {
    // Force dynamic stacks prior to archiving. Any growth will do. A dynamic
    // stack is needed because stack archival & restoration rely on `memory_`
    // pointing at a fixed-location backing store, whereas the static stack is
    // tied to a RegExpStack instance.
    EnsureCapacity(thread_local_.memory_size_ + 1);
    DCHECK(thread_local_.owns_memory_);
  }

  MemCopy(reinterpret_cast<void*>(to), &thread_local_, kThreadLocalSize);
  thread_local_ = ThreadLocal(this);
  return to + kThreadLocalSize;
}


char* RegExpStack::RestoreStack(char* from) {
  MemCopy(&thread_local_, reinterpret_cast<void*>(from), kThreadLocalSize);
  return from + kThreadLocalSize;
}

void RegExpStack::ThreadLocal::ResetToStaticStack(RegExpStack* regexp_stack) {
  if (owns_memory_) DeleteArray(memory_);

  memory_ = regexp_stack->static_stack_;
  memory_top_ = regexp_stack->static_stack_ + kStaticStackSize;
  memory_size_ = kStaticStackSize;
  stack_pointer_ = memory_top_;
  limit_ = reinterpret_cast<Address>(regexp_stack->static_stack_) +
           kStackLimitSlackSize;
  owns_memory_ = false;
}

void RegExpStack::ThreadLocal::FreeAndInvalidate() {
  if (owns_memory_) DeleteArray(memory_);

  // This stack may not be used after being freed. Just reset to invalid values
  // to ensure we don't accidentally use old memory areas.
  memory_ = nullptr;
  memory_top_ = nullptr;
  memory_size_ = 0;
  stack_pointer_ = nullptr;
  limit_ = kMemoryTop;
}

Address RegExpStack::EnsureCapacity(size_t size) {
  if (size > kMaximumStackSize) return kNullAddress;
  if (thread_local_.memory_size_ < size) {
    if (size < kMinimumDynamicStackSize) size = kMinimumDynamicStackSize;
    uint8_t* new_memory = NewArray<uint8_t>(size);
    if (thread_local_.memory_size_ > 0) {
      // Copy original memory into top of new memory.
      MemCopy(new_memory + size - thread_local_.memory_size_,
              thread_local_.memory_, thread_local_.memory_size_);
      if (thread_local_.owns_memory_) DeleteArray(thread_local_.memory_);
    }
    ptrdiff_t delta = sp_top_delta();
    thread_local_.memory_ = new_memory;
    thread_local_.memory_top_ = new_memory + size;
    thread_local_.memory_size_ = size;
    thread_local_.stack_pointer_ = thread_local_.memory_top_ + delta;
    thread_local_.limit_ =
        reinterpret_cast<Address>(new_memory) + kStackLimitSlackSize;
    thread_local_.owns_memory_ = true;
  }
  return reinterpret_cast<Address>(thread_local_.memory_top_);
}


}  // namespace internal
}  // namespace v8
