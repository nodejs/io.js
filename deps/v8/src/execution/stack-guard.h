// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_STACK_GUARD_H_
#define V8_EXECUTION_STACK_GUARD_H_

#include "include/v8-internal.h"
#include "src/base/atomicops.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class ExecutionAccess;
class InterruptsScope;
class Isolate;
class Object;
class RootVisitor;

// StackGuard contains the handling of the limits that are used to limit the
// number of nested invocations of JavaScript and the stack size used in each
// invocation.
class V8_EXPORT_PRIVATE V8_NODISCARD StackGuard final {
 public:
  StackGuard(const StackGuard&) = delete;
  StackGuard& operator=(const StackGuard&) = delete;

  explicit StackGuard(Isolate* isolate) : isolate_(isolate) {}

  // Pass the address beyond which the stack should not grow. The stack
  // is assumed to grow downwards.
  // When executing on the simulator, we set the stack limits to the limits of
  // the simulator's stack instead of using {limit}.
  void SetStackLimit(uintptr_t limit);

  // Try to compare and swap the given jslimit without the ExecutionAccess lock.
  // Expects potential concurrent writes of the interrupt limit, and of the
  // interrupt limit only.
  void SetStackLimitForStackSwitching(uintptr_t limit);

#ifdef USE_SIMULATOR
  // The simulator uses a separate JS stack. Limits on the JS stack might have
  // to be adjusted in order to reflect overflows of the C stack, because we
  // cannot rely on the interleaving of frames on the simulator.
  void AdjustStackLimitForSimulator();
#endif

  // Threading support.
  char* ArchiveStackGuard(char* to);
  char* RestoreStackGuard(char* from);
  static int ArchiveSpacePerThread() { return sizeof(ThreadLocal); }
  void FreeThreadResources();
  // Sets up the default stack guard for this thread.
  void InitThread(const ExecutionAccess& lock);

  // Code locations that check for interrupts might only handle a subset of the
  // available interrupts, expressed as an `InterruptLevel`. These levels are
  // also associated with side effects that are allowed for the respective
  // level. The levels are inclusive, which is specified using the order in the
  // enum. For example, a site that handles `kAnyEffect` will also handle the
  // preceding levels.
  enum class InterruptLevel { kNoGC, kNoHeapWrites, kAnyEffect };
  static constexpr int kNumberOfInterruptLevels = 3;

#define INTERRUPT_LIST(V)                                                      \
  V(TERMINATE_EXECUTION, TerminateExecution, 0, InterruptLevel::kNoGC)         \
  V(GC_REQUEST, GC, 1, InterruptLevel::kNoHeapWrites)                          \
  V(INSTALL_CODE, InstallCode, 2, InterruptLevel::kAnyEffect)                  \
  V(INSTALL_BASELINE_CODE, InstallBaselineCode, 3, InterruptLevel::kAnyEffect) \
  V(API_INTERRUPT, ApiInterrupt, 4, InterruptLevel::kNoHeapWrites)             \
  V(DEOPT_MARKED_ALLOCATION_SITES, DeoptMarkedAllocationSites, 5,              \
    InterruptLevel::kNoHeapWrites)                                             \
  V(GROW_SHARED_MEMORY, GrowSharedMemory, 6, InterruptLevel::kAnyEffect)       \
  V(LOG_WASM_CODE, LogWasmCode, 7, InterruptLevel::kAnyEffect)                 \
  V(WASM_CODE_GC, WasmCodeGC, 8, InterruptLevel::kNoHeapWrites)                \
  V(INSTALL_MAGLEV_CODE, InstallMaglevCode, 9, InterruptLevel::kAnyEffect)     \
  V(GLOBAL_SAFEPOINT, GlobalSafepoint, 10, InterruptLevel::kNoHeapWrites)      \
  V(START_INCREMENTAL_MARKING, StartIncrementalMarking, 11,                    \
    InterruptLevel::kNoHeapWrites)

#define V(NAME, Name, id, interrupt_level)                   \
  inline bool Check##Name() { return CheckInterrupt(NAME); } \
  inline void Request##Name() { RequestInterrupt(NAME); }    \
  inline void Clear##Name() { ClearInterrupt(NAME); }
  INTERRUPT_LIST(V)
#undef V

  // Flag used to set the interrupt causes.
  enum InterruptFlag : uint32_t {
#define V(NAME, Name, id, interrupt_level) NAME = (1 << id),
    INTERRUPT_LIST(V)
#undef V
#define V(NAME, Name, id, interrupt_level) NAME |
        ALL_INTERRUPTS = INTERRUPT_LIST(V) 0
#undef V
  };
  static_assert(InterruptFlag::ALL_INTERRUPTS <
                std::numeric_limits<uint32_t>::max());

  static constexpr InterruptFlag InterruptLevelMask(InterruptLevel level) {
#define V(NAME, Name, id, interrupt_level) \
  | (interrupt_level <= level ? NAME : 0)
    return static_cast<InterruptFlag>(0 INTERRUPT_LIST(V));
#undef V
  }

  uintptr_t climit() {
#ifdef USE_SIMULATOR
    return thread_local_.climit();
#else
    return thread_local_.jslimit();
#endif
  }
  uintptr_t jslimit() { return thread_local_.jslimit(); }
  // This provides an asynchronous read of the stack limits for the current
  // thread.  There are no locks protecting this, but it is assumed that you
  // have the global V8 lock if you are using multiple V8 threads.
  uintptr_t real_climit() {
#ifdef USE_SIMULATOR
    return thread_local_.real_climit_;
#else
    return thread_local_.real_jslimit_;
#endif
  }
  uintptr_t real_jslimit() { return thread_local_.real_jslimit_; }
  Address address_of_jslimit() {
    return reinterpret_cast<Address>(&thread_local_.jslimit_);
  }
  Address address_of_real_jslimit() {
    return reinterpret_cast<Address>(&thread_local_.real_jslimit_);
  }
  Address address_of_interrupt_request(InterruptLevel level) {
    return reinterpret_cast<Address>(
        &thread_local_.interrupt_requested_[static_cast<int>(level)]);
  }

  static constexpr int jslimit_offset() {
    return offsetof(StackGuard, thread_local_) +
           offsetof(ThreadLocal, jslimit_);
  }

  static constexpr int real_jslimit_offset() {
    return offsetof(StackGuard, thread_local_) +
           offsetof(ThreadLocal, real_jslimit_);
  }

  // If the stack guard is triggered, but it is not an actual
  // stack overflow, then handle the interruption accordingly.
  // Only interrupts that match the given `InterruptLevel` will be handled,
  // leaving other interrupts pending as if this method had not been called.
  Tagged<Object> HandleInterrupts(
      InterruptLevel level = InterruptLevel::kAnyEffect);

  // Special case of {HandleInterrupts}: checks for termination requests only.
  // This is guaranteed to never cause GC, so can be used to interrupt
  // long-running computations that are not GC-safe.
  bool HasTerminationRequest();

  static constexpr int kSizeInBytes = 8 * kSystemPointerSize;

  static char* Iterate(RootVisitor* v, char* thread_storage) {
    return thread_storage + ArchiveSpacePerThread();
  }

 private:
  bool CheckInterrupt(InterruptFlag flag);
  void RequestInterrupt(InterruptFlag flag);
  void ClearInterrupt(InterruptFlag flag);
  int FetchAndClearInterrupts(InterruptLevel level);

  void SetStackLimitInternal(const ExecutionAccess& lock, uintptr_t limit,
                             uintptr_t jslimit);

  // You should hold the ExecutionAccess lock when calling this method.
  bool has_pending_interrupts(const ExecutionAccess& lock) {
    return thread_local_.interrupt_flags_ != 0;
  }

  // You should hold the ExecutionAccess lock when calling this method.
  inline void update_interrupt_requests_and_stack_limits(
      const ExecutionAccess& lock);

#if V8_TARGET_ARCH_64_BIT
  static const uintptr_t kInterruptLimit = uintptr_t{0xfffffffffffffffe};
  static const uintptr_t kIllegalLimit = uintptr_t{0xfffffffffffffff8};
#else
  static const uintptr_t kInterruptLimit = 0xfffffffe;
  static const uintptr_t kIllegalLimit = 0xfffffff8;
#endif

  void PushInterruptsScope(InterruptsScope* scope);
  void PopInterruptsScope();

  class ThreadLocal final {
   public:
    ThreadLocal() {}

    void Initialize(Isolate* isolate, const ExecutionAccess& lock);

    // The stack limit has two values: the one with the real_ prefix is the
    // actual stack limit set for the VM.  The one without the real_ prefix has
    // the same value as the actual stack limit except when there is an
    // interruption (e.g. debug break or preemption) in which case it is lowered
    // to make stack checks fail. Both the generated code and the runtime system
    // check against the one without the real_ prefix.
    // For simulator builds, we also use a separate C++ stack limit.

    // Actual JavaScript stack limit set for the VM.
    uintptr_t real_jslimit_ = kIllegalLimit;
#ifdef USE_SIMULATOR
    // Actual C++ stack limit set for the VM.
    uintptr_t real_climit_ = kIllegalLimit;
#else
    // Padding to match the missing {real_climit_} field, renamed to make it
    // explicit that this field is unused in this configuration. But the padding
    // field is needed:
    // - To keep the isolate's LinearAllocationArea fields from crossing cache
    // lines (see Isolate::CheckIsolateLayout).
    // - To ensure that jslimit_offset() is the same in mksnapshot and in V8:
    // When cross-compiling V8, mksnapshot's host and target may be different
    // even if they are the same for V8, which results in a different value for
    // USE_SIMULATOR. Without this padding, this causes the builtins to use the
    // wrong jslimit_offset() for stack checks.
    uintptr_t padding1_;
#endif

    // jslimit_ and climit_ can be read without any lock.
    // Writing requires the ExecutionAccess lock, or may be updated with a
    // strong compare-and-swap (e.g. for stack-switching).
    base::AtomicWord jslimit_ = kIllegalLimit;
#ifdef USE_SIMULATOR
    base::AtomicWord climit_ = kIllegalLimit;
#else
    // See {padding1_}.
    uintptr_t padding2_;
#endif

    uintptr_t jslimit() {
      return base::bit_cast<uintptr_t>(base::Relaxed_Load(&jslimit_));
    }
    void set_jslimit(uintptr_t limit) {
      return base::Relaxed_Store(&jslimit_,
                                 static_cast<base::AtomicWord>(limit));
    }
#ifdef USE_SIMULATOR
    uintptr_t climit() {
      return base::bit_cast<uintptr_t>(base::Relaxed_Load(&climit_));
    }
    void set_climit(uintptr_t limit) {
      return base::Relaxed_Store(&climit_,
                                 static_cast<base::AtomicWord>(limit));
    }
#endif

    // Interrupt request bytes can be read without any lock.
    // Writing requires the ExecutionAccess lock.
    base::Atomic8 interrupt_requested_[kNumberOfInterruptLevels] = {
        false, false, false};

    void set_interrupt_requested(InterruptLevel level, bool requested) {
      base::Relaxed_Store(&interrupt_requested_[static_cast<int>(level)],
                          requested);
    }

    bool has_interrupt_requested(InterruptLevel level) {
      return base::Relaxed_Load(&interrupt_requested_[static_cast<int>(level)]);
    }

    InterruptsScope* interrupt_scopes_ = nullptr;
    uint32_t interrupt_flags_ = 0;
  };

  // TODO(isolates): Technically this could be calculated directly from a
  //                 pointer to StackGuard.
  Isolate* isolate_;
  ThreadLocal thread_local_;

  friend class Isolate;
  friend class StackLimitCheck;
  friend class InterruptsScope;

  static_assert(std::is_standard_layout<ThreadLocal>::value);
};

static_assert(StackGuard::kSizeInBytes == sizeof(StackGuard));

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_STACK_GUARD_H_
