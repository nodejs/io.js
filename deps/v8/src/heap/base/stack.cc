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

// static
bool Stack::IsOnStack(const void* slot) {
#ifdef V8_USE_ADDRESS_SANITIZER
  // If the slot is part of a fake frame, then it is definitely on the stack.
  if (__asan_addr_is_in_fake_stack(__asan_get_current_fake_stack(),
                                   const_cast<void*>(slot), nullptr, nullptr)) {
    return true;
  }
  // Fall through as there is still a regular stack present even when running
  // with ASAN fake stacks.
#endif  // V8_USE_ADDRESS_SANITIZER
#ifdef V8_USE_SAFE_STACK
  if (__builtin___get_unsafe_stack_ptr() <= slot &&
      slot <= __builtin___get_unsafe_stack_top()) {
    return true;
  }
#endif  // V8_USE_SAFE_STACK
  return v8::base::Stack::GetCurrentStackPosition() <= slot &&
         slot <= v8::base::Stack::GetStackStartUnchecked();
}

namespace {

#ifdef V8_USE_ADDRESS_SANITIZER
// No ASAN support as accessing fake frames otherwise results in
// "stack-use-after-scope" warnings.
DISABLE_ASAN
// No HW ASAN support as stack iteration constructs pointers from arbitrary
// memory which may e.g. lead to tag mismatches.
DISABLE_HWASAN
// No TSAN support as the stack may not be exclusively owned by the current
// thread, e.g., for interrupt handling. Atomic reads are not enough as the
// other thread may use a lock to synchronize the access.
DISABLE_TSAN
void IterateAsanFakeFrameIfNecessary(StackVisitor* visitor,
                                     const Stack::Segment& segment,
                                     const void* address) {
  // When using ASAN fake stack a pointer to the fake frame is kept on the
  // native frame. In case |addr| points to a fake frame of the current stack
  // iterate the fake frame. Frame layout see
  // https://github.com/google/sanitizers/wiki/AddressSanitizerUseAfterReturn
  if (!segment.asan_fake_stack) return;
  void* fake_frame_begin;
  void* fake_frame_end;
  void* real_stack_frame = __asan_addr_is_in_fake_stack(
      const_cast<void*>(segment.asan_fake_stack), const_cast<void*>(address),
      &fake_frame_begin, &fake_frame_end);
  if (real_stack_frame) {
    // |address| points to a fake frame. Check that the fake frame is part
    // of this stack.
    if (segment.start >= real_stack_frame && real_stack_frame >= segment.top) {
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
                                     const Stack::Segment& segment,
                                     const void* address) {}
#endif  // V8_USE_ADDRESS_SANITIZER

void IteratePointersInUnsafeStackIfNecessary(StackVisitor* visitor,
                                             const Stack::Segment& segment) {
#ifdef V8_USE_SAFE_STACK
  CHECK_NOT_NULL(segment.unsafe_stack_start);
  CHECK_NOT_NULL(segment.unsafe_stack_top);
  // Source:
  // https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/safestack/safestack.cpp
  constexpr size_t kSafeStackAlignmentBytes = 16;
  CHECK_GE(segment.unsafe_stack_start, segment.unsafe_stack_top);
  CHECK_EQ(0u, reinterpret_cast<uintptr_t>(segment.unsafe_stack_top) &
                   (kSafeStackAlignmentBytes - 1));
  CHECK_EQ(0u, reinterpret_cast<uintptr_t>(segment.unsafe_stack_start) &
                   (kSafeStackAlignmentBytes - 1));

  for (const void* const* current =
           reinterpret_cast<const void* const*>(segment.unsafe_stack_top);
       current < segment.unsafe_stack_start; ++current) {
    const void* address = *current;
    if (address == nullptr) continue;
    visitor->VisitPointer(address);
  }
#endif  // V8_USE_SAFE_STACK
}

// This method should never be inlined to ensure that a possible redzone cannot
// contain any data that needs to be scanned.
V8_NOINLINE
// No ASAN support as method accesses redzones while walking the stack.
DISABLE_ASAN
// No HW ASAN support as stack iteration constructs pointers from arbitrary
// memory which may e.g. lead to tag mismatches.
DISABLE_HWASAN
// No TSAN support as the stack may not be exclusively owned by the current
// thread, e.g., for interrupt handling. Atomic reads are not enough as the
// other thread may use a lock to synchronize the access.
DISABLE_TSAN
void IteratePointersInStack(StackVisitor* visitor,
                            const Stack::Segment& segment) {
  CHECK_NOT_NULL(segment.top);
  CHECK_NOT_NULL(segment.start);
  CHECK_GE(segment.start, segment.top);
  // All supported platforms should have their stack aligned to at least
  // sizeof(void*).
  constexpr size_t kMinStackAlignment = sizeof(void*);
  CHECK_EQ(0u,
           reinterpret_cast<uintptr_t>(segment.top) & (kMinStackAlignment - 1));
  CHECK_EQ(0u, reinterpret_cast<uintptr_t>(segment.start) &
                   (kMinStackAlignment - 1));

  for (const void* const* current =
           reinterpret_cast<const void* const*>(segment.top);
       current < segment.start; ++current) {
    // MSAN: Instead of unpoisoning the whole stack, the slot's value is copied
    // into a local which is unpoisoned.
    const void* address = *current;
    MSAN_MEMORY_IS_INITIALIZED(&address, sizeof(address));
    if (address == nullptr) {
      continue;
    }
    visitor->VisitPointer(address);
    IterateAsanFakeFrameIfNecessary(visitor, segment, address);
  }
}

}  // namespace

void Stack::IteratePointersForTesting(StackVisitor* visitor) {
  SetMarkerAndCallback([this, visitor]() { IteratePointers(visitor); });
}

void Stack::IteratePointersUntilMarker(StackVisitor* visitor) const {
  // Temporarily stop checking MTE tags whilst scanning the stack (whilst V8
  // may not be tagging its portion of the stack, higher frames from the OS or
  // libc could be using stack tagging.)
  SuspendTagCheckingScope s;
  IteratePointersInStack(visitor, current_segment_);
  IteratePointersInUnsafeStackIfNecessary(visitor, current_segment_);

  for (const auto& segment : inactive_stacks_) {
    IteratePointersInStack(visitor, segment);
    // TODO(v8:13493): If inactive stacks are used again, consider iterating
    // pointers in the unsafe stack here.
  }
}

void Stack::IterateBackgroundStacks(StackVisitor* visitor) const {
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
    IteratePointersInStack(visitor, segment);
    IteratePointersInUnsafeStackIfNecessary(visitor, segment);
  }
}

#ifdef DEBUG
// static
bool Stack::IsOnCurrentStack(const void* ptr) {
  DCHECK_NOT_NULL(ptr);
  const void* current_stack_start = v8::base::Stack::GetStackStartUnchecked();
  const void* current_stack_top = v8::base::Stack::GetCurrentStackPosition();
  return ptr <= current_stack_start && ptr >= current_stack_top;
}
#endif  // DEBUG

void Stack::AddStackSegment(const void* start, const void* top) {
  DCHECK_LE(top, start);
  // TODO(v8:13493): If this method is used again, bear in mind that the
  // StackSegments constructor implicitly uses the current values (if
  // applicable) for:
  // - asan_fake_start
  // - unsafe stack start
  // - unsafe stack top
  inactive_stacks_.emplace_back(start, top);
}

void Stack::ClearStackSegments() { inactive_stacks_.clear(); }

void Stack::TrampolineCallbackHelper(void* argument,
                                     IterateStackCallback callback) {
  PushAllRegistersAndIterateStack(this, argument, callback);
  // TODO(chromium:1056170): Add support for SIMD and/or filtering.
}

}  // namespace heap::base
