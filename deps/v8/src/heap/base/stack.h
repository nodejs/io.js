// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_BASE_STACK_H_
#define V8_HEAP_BASE_STACK_H_

#include <map>
#include <vector>

#include "src/base/macros.h"
#include "src/base/platform/platform.h"

namespace heap::base {

class StackVisitor {
 public:
  virtual ~StackVisitor() = default;
  virtual void VisitPointer(const void* address) = 0;
};

#if defined(__has_feature)
#if __has_feature(safe_stack)
#define V8_USE_SAFE_STACK 1
#endif  // __has_feature(safe_stack)
#endif  // defined(__has_feature)

// Abstraction over the stack. Supports handling of:
// - native stack;
// - ASAN/MSAN;
// - SafeStack: https://releases.llvm.org/10.0.0/tools/clang/docs/SafeStack.html
//
// Stacks grow down, so throughout this class "start" refers the highest
// address of the stack, and top/marker the lowest.
//
// TODO(chromium:1056170): Consider adding a component that keeps track
// of relevant GC stack regions where interesting pointers can be found.
class V8_EXPORT_PRIVATE Stack final {
 public:
  // Sets the start of the stack to the current stack start.
  void SetStackStart() {
    current_segment_.start = v8::base::Stack::GetStackStart();
#ifdef V8_USE_ADDRESS_SANITIZER
    current_segment_.asan_fake_stack = __asan_get_current_fake_stack();
#endif  // V8_USE_ADDRESS_SANITIZER
#ifdef V8_USE_SAFE_STACK
    current_segment_.unsafe_stack_start = __builtin___get_unsafe_stack_top();
#endif  // V8_USE_SAFE_STACK
  }

  // Returns true if |slot| is part of the stack and false otherwise.
  // It is a static method, ignoring the set stack start and marker, but it
  // considers the ASAN stack and SafeStack.
  static bool IsOnStack(const void* slot);

  void IteratePointers(StackVisitor* visitor) const {
    IteratePointersUntilMarker(visitor);
    IterateBackgroundStacks(visitor);
  }

  // Word-aligned iteration of the stack, starting at the `stack_marker_`
  // and going to the stack start. Slot values are passed on to `visitor`.
  void IteratePointersUntilMarker(StackVisitor* visitor) const;

  void AddStackSegment(const void* start, const void* top);
  void ClearStackSegments();

  // Iterate just the background stacks, if any.
  void IterateBackgroundStacks(StackVisitor* visitor) const;

  // Push callee-saved registers to the stack, set the stack marker to the
  // current stack top and invoke the callback.
  template <typename Callback>
  V8_INLINE void SetMarkerAndCallback(Callback callback) {
    TrampolineCallbackHelper(static_cast<void*>(&callback),
                             &SetMarkerAndCallbackImpl<Callback>);
  }

  template <typename Callback>
  V8_INLINE void SetMarkerIfNeededAndCallback(Callback callback) {
    if (!IsMarkerSet()) {
      TrampolineCallbackHelper(static_cast<void*>(&callback),
                               &SetMarkerAndCallbackImpl<Callback>);
    } else {
      DCHECK(IsOnCurrentStack(current_segment_.top));
      callback();
    }
  }

  using ThreadId = int;

  template <typename Callback>
  V8_INLINE void SetMarkerForBackgroundThreadAndCallback(ThreadId thread,
                                                         Callback callback) {
    std::pair<ThreadId, Callback*> info{thread, &callback};
    TrampolineCallbackHelper(
        static_cast<void*>(&info),
        &SetMarkerForBackgroundThreadAndCallbackImpl<Callback>);
  }

  using IterateStackCallback = void (*)(Stack*, void*, const void*);

  // This method combines SetMarkerAndCallback with IteratePointers.
  // Callee-saved registers are pushed to the stack and then a word-aligned
  // iteration of the stack is performed. Slot values are passed on to
  // `visitor`. To be used for testing.
  void IteratePointersForTesting(StackVisitor* visitor);

  bool IsMarkerSet() const { return current_segment_.top != nullptr; }
  bool IsMarkerSetForBackgroundThread(ThreadId thread) const {
    v8::base::MutexGuard guard(&lock_);
    auto it = background_stacks_.find(thread);
    if (it == background_stacks_.end()) return false;
    DCHECK_NOT_NULL(it->second.top);
    return true;
  }

  // This method is only safe to use in a safepoint, as it does not take the
  // mutex for background_stacks_.
  bool HasBackgroundStacks() const { return !background_stacks_.empty(); }

  // Stack segments that may contain pointers and should be scanned.
  struct Segment {
    // The start and top of the stack. It must be sp <= top <= start.
    // The top pointer is generally a marker that signals the end of the
    // interesting stack region in which on-heap pointers can be found.
    const void* start = nullptr;
    const void* top = nullptr;

#ifdef V8_USE_ADDRESS_SANITIZER
    // The start of ASAN's fake stack.
    const void* asan_fake_stack = nullptr;
#endif  // V8_USE_ADDRESS_SANITIZER

#ifdef V8_USE_SAFE_STACK
    // Start and top for the unsafe stack that is used in clang with
    // -fsanitizer=safe-stack.
    // It must be unsafe_sp <= unsafe_stack_top <= unsafe_stack_start.
    // Notice that the terms "start" and "top" have here a different meaning in
    // the terminology used in this feature's documentation.
    const void* unsafe_stack_start = nullptr;
    const void* unsafe_stack_top = nullptr;
#endif  // V8_USE_SAFE_STACK

    Segment() = default;
    Segment(const void* stack_start, const void* stack_top)
        : start(stack_start), top(stack_top) {
#ifdef V8_USE_ADDRESS_SANITIZER
      asan_fake_stack = __asan_get_current_fake_stack();
#endif  // V8_USE_ADDRESS_SANITIZER
#ifdef V8_USE_SAFE_STACK
      unsafe_stack_start = __builtin___get_unsafe_stack_top();
      unsafe_stack_top = __builtin___get_unsafe_stack_ptr();
#endif  // V8_USE_SAFE_STACK
    }
  };

 private:
#ifdef DEBUG
  static bool IsOnCurrentStack(const void* ptr);
#endif

  V8_NOINLINE void TrampolineCallbackHelper(void* argument,
                                            IterateStackCallback callback);

  template <typename Callback>
  static void SetMarkerAndCallbackImpl(Stack* stack, void* argument,
                                       const void* stack_end) {
    Segment previous_segment = stack->current_segment_;
    stack->current_segment_.top = stack_end;
#ifdef V8_USE_SAFE_STACK
    stack->current_segment_.unsafe_stack_top =
        __builtin___get_unsafe_stack_ptr();
#endif  // V8_USE_SAFE_STACK
    Callback* callback = static_cast<Callback*>(argument);
    (*callback)();
    stack->current_segment_ = previous_segment;
  }

  template <typename Callback>
  static void SetMarkerForBackgroundThreadAndCallbackImpl(
      Stack* stack, void* argument, const void* stack_end) {
    DCHECK(IsOnCurrentStack(stack_end));
    auto [thread, callback] =
        *static_cast<std::pair<ThreadId, Callback*>*>(argument);
    auto& background_stacks = stack->background_stacks_;
    Segment previous_segment;
    {
      v8::base::MutexGuard guard(&stack->lock_);
      if (auto it = background_stacks.find(thread);
          it != background_stacks.end()) {
        previous_segment = it->second;
        DCHECK_NOT_NULL(previous_segment.top);
      } else {
        DCHECK_NULL(previous_segment.top);
      }
      // This implicitly uses the current values (if applicable) for:
      // - asan_fake_start
      // - unsafe stack start
      // - unsafe stack top
      background_stacks[thread] =
          Segment(v8::base::Stack::GetStackStart(), stack_end);
    }
    (*callback)();
    {
      v8::base::MutexGuard guard(&stack->lock_);
      if (previous_segment.top)
        background_stacks[thread] = previous_segment;
      else
        background_stacks.erase(thread);
    }
  }

  Segment current_segment_;

  // TODO(v8:13493): If inactive stacks are not used anymore, clean this up.
  std::vector<Segment> inactive_stacks_;

  mutable v8::base::Mutex lock_;
  std::map<ThreadId, Segment> background_stacks_;
};

}  // namespace heap::base

#endif  // V8_HEAP_BASE_STACK_H_
