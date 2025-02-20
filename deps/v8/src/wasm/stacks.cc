// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/stacks.h"

#include "src/base/platform/platform.h"
#include "src/execution/simulator.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::wasm {

// static
StackMemory* StackMemory::GetCentralStackView(Isolate* isolate) {
  base::Vector<uint8_t> view = SimulatorStack::GetCentralStackView(isolate);
  return new StackMemory(view.begin(), view.size());
}

StackMemory::~StackMemory() {
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Delete stack #%d\n", id_);
  }
  auto segment = first_segment_;
  while (segment) {
    auto next_segment = segment->next_segment_;
    delete segment;
    segment = next_segment;
  }
}

StackMemory::StackMemory() : owned_(true) {
  static std::atomic<int> next_id(1);
  id_ = next_id.fetch_add(1);
  size_t kJsStackSizeKB = v8_flags.wasm_stack_switching_stack_size;
  // v8_flags.stack_size is a size of the central stack and maximum
  // size of a secondary stack to grow.
  const size_t size_limit = v8_flags.stack_size;
  PageAllocator* allocator = GetPlatformPageAllocator();
  auto page_size = allocator->AllocatePageSize();
  size_t initial_size =
      std::min<size_t>(size_limit, kJsStackSizeKB + kJSLimitOffsetKB) * KB;
  first_segment_ =
      new StackSegment(RoundUp(initial_size, page_size) / page_size);
  active_segment_ = first_segment_;
  size_ = first_segment_->size_;
  limit_ = first_segment_->limit_;
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Allocate stack #%d (limit: %p, base: %p, size: %zu)\n", id_, limit_,
           limit_ + size_, size_);
  }
}

// Overload to represent a view of the libc stack.
StackMemory::StackMemory(uint8_t* limit, size_t size)
    : limit_(limit), size_(size), owned_(false) {
  id_ = 0;
}

StackMemory::StackSegment::StackSegment(size_t pages) {
  DCHECK_GE(pages, 1);
  PageAllocator* allocator = GetPlatformPageAllocator();
  size_ = pages * allocator->AllocatePageSize();
  limit_ = static_cast<uint8_t*>(
      allocator->AllocatePages(nullptr, size_, allocator->AllocatePageSize(),
                               PageAllocator::kReadWrite));
}

StackMemory::StackSegment::~StackSegment() {
  PageAllocator* allocator = GetPlatformPageAllocator();
  if (!allocator->DecommitPages(limit_, size_)) {
    V8::FatalProcessOutOfMemory(nullptr, "Decommit stack memory");
  }
}

bool StackMemory::Grow(Address current_fp) {
  DCHECK(owned_);
  if (active_segment_->next_segment_ != nullptr) {
    active_segment_ = active_segment_->next_segment_;
  } else {
    PageAllocator* allocator = GetPlatformPageAllocator();
    auto page_size = allocator->AllocatePageSize();
    const size_t size_limit = RoundUp(v8_flags.stack_size * KB, page_size);
    DCHECK_GE(size_limit, size_);
    size_t room_to_grow = size_limit - size_;
    size_t new_size = std::min(2 * active_segment_->size_, room_to_grow);
    if (new_size < page_size) {
      // We cannot grow less than page size.
      if (v8_flags.trace_wasm_stack_switching) {
        PrintF("Stack #%d reached the grow limit %zu bytes\n", id_, size_limit);
      }
      return false;
    }
    auto new_segment = new StackSegment(new_size / page_size);
    new_segment->prev_segment_ = active_segment_;
    active_segment_->next_segment_ = new_segment;
    active_segment_ = new_segment;
  }
  active_segment_->old_fp = current_fp;
  size_ += active_segment_->size_;
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Grow stack #%d by %zu bytes (limit: %p, base: %p)\n", id_,
           active_segment_->size_, active_segment_->limit_,
           active_segment_->limit_ + active_segment_->size_);
  }
  return true;
}

Address StackMemory::Shrink() {
  DCHECK(owned_);
  DCHECK_NE(active_segment_->prev_segment_, nullptr);
  Address old_fp = active_segment_->old_fp;
  size_ -= active_segment_->size_;
  active_segment_->old_fp = 0;
  active_segment_ = active_segment_->prev_segment_;
  if (v8_flags.trace_wasm_stack_switching) {
    PrintF("Shrink stack #%d (limit: %p, base: %p)\n", id_,
           active_segment_->limit_,
           active_segment_->limit_ + active_segment_->size_);
  }
  return old_fp;
}

void StackMemory::Reset() {
  active_segment_ = first_segment_;
  size_ = active_segment_->size_;
}

std::unique_ptr<StackMemory> StackPool::GetOrAllocate() {
  while (size_ > kMaxSize) {
    size_ -= freelist_.back()->allocated_size();
    freelist_.pop_back();
  }
  std::unique_ptr<StackMemory> stack;
  if (freelist_.empty()) {
    stack = StackMemory::New();
  } else {
    stack = std::move(freelist_.back());
    freelist_.pop_back();
    size_ -= stack->allocated_size();
  }
#if DEBUG
  constexpr uint8_t kZapValue = 0xab;
  stack->FillWith(kZapValue);
#endif
  return stack;
}

void StackPool::Add(std::unique_ptr<StackMemory> stack) {
  // Add the stack to the pool regardless of kMaxSize, because the stack might
  // still be in use by the unwinder.
  // Shrink the freelist lazily when we get the next stack instead.
  size_ += stack->allocated_size();
  stack->Reset();
  freelist_.push_back(std::move(stack));
}

void StackPool::ReleaseFinishedStacks() {
  size_ = 0;
  freelist_.clear();
}

size_t StackPool::Size() const {
  return freelist_.size() * sizeof(decltype(freelist_)::value_type) + size_;
}

}  // namespace v8::internal::wasm
