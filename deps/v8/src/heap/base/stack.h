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
  explicit Stack(const void* stack_start = nullptr)
      : stack_start_(stack_start) {}

  // Sets the start of the stack.
  void SetStackStart(const void* stack_start) { stack_start_ = stack_start; }

  // Returns true if |slot| is part of the stack and false otherwise.
  bool IsOnStack(const void* slot) const;

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
    if (stack_marker_ == nullptr) {
      TrampolineCallbackHelper(static_cast<void*>(&callback),
                               &SetMarkerAndCallbackImpl<Callback>);
    } else {
      DCHECK(IsOnCurrentStack(stack_marker_));
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

  bool IsMarkerSet() const { return stack_marker_ != nullptr; }

  // This method is only safe to use in a safepoint, as it does not take the
  // mutex for background_stacks_.
  bool HasBackgroundStacks() const { return !background_stacks_.empty(); }

 private:
#ifdef DEBUG
  static bool IsOnCurrentStack(const void* ptr);
#endif

  V8_NOINLINE void TrampolineCallbackHelper(void* argument,
                                            IterateStackCallback callback);

  template <typename Callback>
  static void SetMarkerAndCallbackImpl(Stack* stack, void* argument,
                                       const void* stack_end) {
    DCHECK_NULL(stack->stack_marker_);
    stack->stack_marker_ = stack_end;
    Callback* callback = static_cast<Callback*>(argument);
    (*callback)();
    stack->stack_marker_ = nullptr;
  }

  template <typename Callback>
  static void SetMarkerForBackgroundThreadAndCallbackImpl(
      Stack* stack, void* argument, const void* stack_end) {
    DCHECK(IsOnCurrentStack(stack_end));
    auto [thread, callback] =
        *static_cast<std::pair<ThreadId, Callback*>*>(argument);
    auto& background_stacks = stack->background_stacks_;
    {
      v8::base::MutexGuard guard(&stack->lock_);
      DCHECK_EQ(background_stacks.end(), background_stacks.find(thread));
      background_stacks.emplace(
          thread, StackSegments{v8::base::Stack::GetStackStart(), stack_end});
    }
    (*callback)();
    {
      v8::base::MutexGuard guard(&stack->lock_);
      background_stacks.erase(thread);
    }
  }

  const void* stack_start_;

  // Marker that signals end of the interesting stack region in which on-heap
  // pointers can be found.
  const void* stack_marker_ = nullptr;

  // Stack segments that may also contain pointers and should be scanned.
  struct StackSegments {
    const void* start;
    const void* top;
  };
  std::vector<StackSegments> inactive_stacks_;

  mutable v8::base::Mutex lock_;
  std::map<ThreadId, const StackSegments> background_stacks_;
};

}  // namespace heap::base

#endif  // V8_HEAP_BASE_STACK_H_
