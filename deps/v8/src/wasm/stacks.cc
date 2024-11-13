// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/stacks.h"

#include "src/base/platform/platform.h"
#include "src/execution/simulator.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::wasm {

// static
StackMemory* StackMemory::GetCurrentStackView(Isolate* isolate) {
  base::Vector<uint8_t> view = SimulatorStack::GetCurrentStackView(isolate);
  return new StackMemory(view.begin(), view.size());
}

StackMemory::~StackMemory() {
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Delete stack #%d\n", id_);
  }
  PageAllocator* allocator = GetPlatformPageAllocator();
  if (owned_ && !allocator->DecommitPages(limit_, size_)) {
    V8::FatalProcessOutOfMemory(nullptr, "Decommit stack memory");
  }
}

StackMemory::StackMemory() : owned_(true) {
  static std::atomic<int> next_id(1);
  id_ = next_id.fetch_add(1);
  PageAllocator* allocator = GetPlatformPageAllocator();
  int kJsStackSizeKB = v8_flags.wasm_stack_switching_stack_size;
  size_ = (kJsStackSizeKB + kJSLimitOffsetKB) * KB;
  size_ = RoundUp(size_, allocator->AllocatePageSize());
  limit_ = static_cast<uint8_t*>(
      allocator->AllocatePages(nullptr, size_, allocator->AllocatePageSize(),
                               PageAllocator::kReadWrite));
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Allocate stack #%d (limit: %p, base: %p)\n", id_, limit_,
           limit_ + size_);
  }
}

// Overload to represent a view of the libc stack.
StackMemory::StackMemory(uint8_t* limit, size_t size)
    : limit_(limit), size_(size), owned_(false) {
  id_ = 0;
}

std::unique_ptr<StackMemory> StackPool::GetOrAllocate() {
  std::unique_ptr<StackMemory> stack;
  if (freelist_.empty()) {
    stack = StackMemory::New();
  } else {
    stack = std::move(freelist_.back());
    freelist_.pop_back();
    size_ -= stack->size_;
  }
  return stack;
}

void StackPool::Add(std::unique_ptr<StackMemory> stack) {
  if (size_ + stack->size_ > kMaxSize) {
    return;
  }
  size_ += stack->size_;
  freelist_.push_back(std::move(stack));
}

void StackPool::ReleaseFinishedStacks() { freelist_.clear(); }

size_t StackPool::Size() const {
  return freelist_.size() * sizeof(decltype(freelist_)::value_type) + size_;
}

}  // namespace v8::internal::wasm
