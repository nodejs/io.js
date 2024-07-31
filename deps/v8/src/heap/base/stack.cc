// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/base/stack.h"

#include <limits>

#include "src/base/sanitizer/asan.h"
#include "src/base/sanitizer/msan.h"
#include "src/heap/base/memory-tagging.h"
#include "src/base/sanitizer/tsan.h"

namespace heap::base {

// Function with architecture-specific implementation:
// Pushes all callee-saved registers to the stack and invokes the callback,
// passing the supplied pointers (stack and argument) and the intended stack
// marker.
extern "C" void PushAllRegistersAndIterateStack(
    Stack* stack, void* argument, Stack::IterateStackCallback callback);

bool Stack::IsOnStack(const void* slot) const {
  DCHECK_NOT_NULL(stack_start_);
#ifdef V8_USE_ADDRESS_SANITIZER
  // If the slot is part of a fake frame, then it is definitely on the stack.
  if (__asan_addr_is_in_fake_stack(__asan_get_current_fake_stack(),
                                   const_cast<void*>(slot), nullptr, nullptr)) {
    return true;
  }
  // Fall through as there is still a regular stack present even when running
  // with ASAN fake stacks.
#endif  // V8_USE_ADDRESS_SANITIZER
#if defined(__has_feature)
#if __has_feature(safe_stack)
  if (__builtin___get_unsafe_stack_top() >= slot &&
      slot >= __builtin___get_unsafe_stack_ptr()) {
    return true;
  }
#endif  // __has_feature(safe_stack)
#endif  // defined(__has_feature)
  return v8::base::Stack::GetCurrentStackPosition() <= slot &&
         slot <= stack_start_;
}

namespace {

#ifdef V8_USE_ADDRESS_SANITIZER

// No ASAN support as accessing fake frames otherwise results in
// "stack-use-after-scope" warnings.
DISABLE_ASAN
// No TSAN support as the stack may not be exclusively owned by the current
// thread, e.g., for interrupt handling. Atomic reads are not enough as the
// other thread may use a lock to synchronize the access.
DISABLE_TSAN
void IterateAsanFakeFrameIfNecessary(StackVisitor* visitor,
                                     const void* asan_fake_stack,
                                     const void* stack_start,
                                     const void* stack_end,
                                     const void* address) {
  // When using ASAN fake stack a pointer to the fake frame is kept on the
  // native frame. In case |addr| points to a fake frame of the current stack
  // iterate the fake frame. Frame layout see
  // https://github.com/google/sanitizers/wiki/AddressSanitizerUseAfterReturn
  if (!asan_fake_stack) return;
  void* fake_frame_begin;
  void* fake_frame_end;
  void* real_stack_frame = __asan_addr_is_in_fake_stack(
      const_cast<void*>(asan_fake_stack), const_cast<void*>(address),
      &fake_frame_begin, &fake_frame_end);
  if (real_stack_frame) {
    // |address| points to a fake frame. Check that the fake frame is part
    // of this stack.
    if (stack_start >= real_stack_frame && real_stack_frame >= stack_end) {
      // Iterate the fake frame.
      for (const void* const* current =
               reinterpret_cast<const void* const*>(fake_frame_begin);
           current < fake_frame_end; ++current) {
        const void* address = *current;
        if (address == nullptr) continue;
        visitor->VisitPointer(address);
      }
    }
  }
}
#else
void IterateAsanFakeFrameIfNecessary(StackVisitor* visitor,
                                     const void* asan_fake_stack,
                                     const void* stack_start,
                                     const void* stack_end,
                                     const void* address) {}
#endif  // V8_USE_ADDRESS_SANITIZER

void IterateUnsafeStackIfNecessary(StackVisitor* visitor) {
#if defined(__has_feature)
#if __has_feature(safe_stack)
  // Source:
  // https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/safestack/safestack.cpp
  constexpr size_t kSafeStackAlignmentBytes = 16;
  const void* stack_end = __builtin___get_unsafe_stack_ptr();
  const void* stack_start = __builtin___get_unsafe_stack_top();
  CHECK_GT(stack_start, stack_end);
  CHECK_EQ(0u, reinterpret_cast<uintptr_t>(stack_end) &
                   (kSafeStackAlignmentBytes - 1));
  CHECK_EQ(0u, reinterpret_cast<uintptr_t>(stack_start) &
                   (kSafeStackAlignmentBytes - 1));
  for (const void* const* current =
           reinterpret_cast<const void* const*>(stack_end);
       current < stack_start; ++current) {
    const void* address = *current;
    if (address == nullptr) continue;
    visitor->VisitPointer(address);
  }
#endif  // __has_feature(safe_stack)
#endif  // defined(__has_feature)
}

// This method should never be inlined to ensure that a possible redzone cannot
// contain any data that needs to be scanned.
V8_NOINLINE
// No ASAN support as method accesses redzones while walking the stack.
DISABLE_ASAN
// No TSAN support as the stack may not be exclusively owned by the current
// thread, e.g., for interrupt handling. Atomic reads are not enough as the
// other thread may use a lock to synchronize the access.
DISABLE_TSAN
void IteratePointersInStack(StackVisitor* visitor, const void* top,
                            const void* start, const void* asan_fake_stack) {
  for (const void* const* current = reinterpret_cast<const void* const*>(top);
       current < start; ++current) {
    // MSAN: Instead of unpoisoning the whole stack, the slot's value is copied
    // into a local which is unpoisoned.
    const void* address = *current;
    MSAN_MEMORY_IS_INITIALIZED(&address, sizeof(address));
    if (address == nullptr) continue;
    visitor->VisitPointer(address);
    IterateAsanFakeFrameIfNecessary(visitor, asan_fake_stack, start, top,
                                    address);
  }
}

}  // namespace

void Stack::IteratePointersForTesting(StackVisitor* visitor) {
  SetMarkerAndCallback([this, visitor]() { IteratePointers(visitor); });
}

void Stack::IteratePointersUntilMarker(StackVisitor* visitor) const {
  DCHECK_NOT_NULL(stack_start_);
  DCHECK_NOT_NULL(stack_marker_);
  DCHECK_GE(stack_start_, stack_marker_);

#ifdef V8_USE_ADDRESS_SANITIZER
  const void* asan_fake_stack = __asan_get_current_fake_stack();
#else
  const void* asan_fake_stack = nullptr;
#endif  // V8_USE_ADDRESS_SANITIZER

  // Iterate through the stack.
  // All supported platforms should have their stack aligned to at least
  // sizeof(void*).
  constexpr size_t kMinStackAlignment = sizeof(void*);
  CHECK_EQ(0u, reinterpret_cast<uintptr_t>(stack_marker_) &
                   (kMinStackAlignment - 1));
  {
    // Temporarily stop checking MTE tags whilst scanning the stack (whilst V8
    // may not be tagging its portion of the stack, higher frames from the OS or
    // libc could be using stack tagging.)
    SuspendTagCheckingScope s;
    IteratePointersInStack(visitor,
                           reinterpret_cast<const void* const*>(stack_marker_),
                           stack_start_, asan_fake_stack);

    for (const auto& segment : inactive_stacks_) {
      CHECK_EQ(0u, reinterpret_cast<uintptr_t>(segment.top) &
                       (kMinStackAlignment - 1));
      IteratePointersInStack(visitor, segment.top, segment.start,
                             asan_fake_stack);
    }

    IterateUnsafeStackIfNecessary(visitor);
  }

  IterateBackgroundStacks(visitor);
}

void Stack::IterateBackgroundStacks(StackVisitor* visitor) const {
#ifdef V8_USE_ADDRESS_SANITIZER
  const void* asan_fake_stack = __asan_get_current_fake_stack();
#else
  const void* asan_fake_stack = nullptr;
#endif  // V8_USE_ADDRESS_SANITIZER

  {
    // Temporarily stop checking MTE tags whilst scanning the stack (whilst V8
    // may not be tagging its portion of the stack, higher frames from the OS or
    // libc could be using stack tagging.)
    SuspendTagCheckingScope s;

    for (const auto& [_, segment] : background_stacks_) {
      // All supported platforms should have their stack aligned to at least
      // sizeof(void*).
      constexpr size_t kMinStackAlignment = sizeof(void*);
      CHECK_EQ(0u, reinterpret_cast<uintptr_t>(segment.top) &
                       (kMinStackAlignment - 1));
      IteratePointersInStack(visitor, segment.top, segment.start,
                             asan_fake_stack);
    }
  }
}

#ifdef DEBUG
// static
bool Stack::IsOnCurrentStack(const void* ptr) {
  DCHECK_NOT_NULL(ptr);
  const void* current_stack_start = v8::base::Stack::GetStackStart();
  const void* current_stack_top = v8::base::Stack::GetCurrentStackPosition();
  return ptr <= current_stack_start && ptr >= current_stack_top;
}
#endif  // DEBUG

void Stack::AddStackSegment(const void* start, const void* top) {
  DCHECK_LE(top, start);
  inactive_stacks_.push_back({start, top});
}

void Stack::ClearStackSegments() { inactive_stacks_.clear(); }

void Stack::TrampolineCallbackHelper(void* argument,
                                     IterateStackCallback callback) {
  PushAllRegistersAndIterateStack(this, argument, callback);
  // TODO(chromium:1056170): Add support for SIMD and/or filtering.
}

}  // namespace heap::base
