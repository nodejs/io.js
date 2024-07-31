// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_THREAD_LOCAL_TOP_H_
#define V8_EXECUTION_THREAD_LOCAL_TOP_H_

#include "include/v8-callbacks.h"
#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-unwinder.h"
#include "src/common/globals.h"
#include "src/execution/thread-id.h"
#include "src/objects/contexts.h"
#include "src/utils/utils.h"

namespace v8 {

class TryCatch;

namespace internal {

class EmbedderState;
class ExternalCallbackScope;
class Isolate;
class Simulator;

class ThreadLocalTop {
 public:
  // TODO(all): This is not particularly beautiful. We should probably
  // refactor this to really consist of just Addresses and 32-bit
  // integer fields.
  static constexpr uint32_t kSizeInBytes = 30 * kSystemPointerSize;

  // Does early low-level initialization that does not depend on the
  // isolate being present.
  ThreadLocalTop() { Clear(); }

  void Clear();

  // Initialize the thread data.
  void Initialize(Isolate*);

  // Get the address of the top C++ try catch handler or nullptr if
  // none are registered.
  //
  // This method always returns an address that can be compared to
  // pointers into the JavaScript stack.  When running on actual
  // hardware, try_catch_handler_address and TryCatchHandler return
  // the same pointer.  When running on a simulator with a separate JS
  // stack, try_catch_handler_address returns a JS stack address that
  // corresponds to the place on the JS stack where the C++ handler
  // would have been if the stack were not separate.
  Address try_catch_handler_address() {
    if (try_catch_handler_) {
      return try_catch_handler_->JSStackComparableAddressPrivate();
    }
    return kNullAddress;
  }

  // Call depth represents nested v8 api calls. Instead of storing the nesting
  // level as an integer, we store the stack height of the last API entry. This
  // additional information is used when we decide whether to trigger a debug
  // break at a function entry.
  template <bool clear_exception, typename Scope>
  void IncrementCallDepth(Scope* stack_allocated_scope) {
    stack_allocated_scope->previous_stack_height_ = last_api_entry_;
#if defined(USE_SIMULATOR) || defined(V8_USE_ADDRESS_SANITIZER)
    StoreCurrentStackPosition();
#else
    last_api_entry_ = reinterpret_cast<i::Address>(stack_allocated_scope);
#endif
    if constexpr (clear_exception) {
      exception_ = Tagged<Object>(
          Internals::GetRoot(reinterpret_cast<v8::Isolate*>(isolate_),
                             Internals::kTheHoleValueRootIndex));
    }
  }

#if defined(USE_SIMULATOR) || defined(V8_USE_ADDRESS_SANITIZER)
  void StoreCurrentStackPosition();
#endif

  template <typename Scope>
  void DecrementCallDepth(Scope* stack_allocated_scope) {
    last_api_entry_ = stack_allocated_scope->previous_stack_height_;
  }

  bool CallDepthIsZero() const { return last_api_entry_ == kNullAddress; }

  void Free();

  // Group fields updated on every CEntry/CallApiCallback/CallApiGetter call
  // together. See MacroAssembler::EnterExitFram/LeaveExitFrame.
  // [ CEntry/CallApiCallback/CallApiGetter

  // The frame pointer of the top c entry frame.
  Address c_entry_fp_;
  // C function that was called at c entry.
  Address c_function_;
  // The context where the current execution method is created and for
  // variable lookups.
  // TODO(3770): This field is read/written from generated code, so it would
  // be cleaner to make it an "Address raw_context_", and construct a Context
  // object in the getter. Same for {pending_handler_context_} below. In the
  // meantime, assert that the memory layout is the same.
  static_assert(sizeof(Tagged<Context>) == kSystemPointerSize);
  Tagged<Context> context_;

  // The "topmost script-having execution context" from the Web IDL spec
  // (i.e. the context of the topmost user JavaScript code, see
  // https://html.spec.whatwg.org/multipage/webappapis.html#topmost-script-having-execution-context)
  // if known or Context::kNoContext otherwise. It's guaranteed to be valid
  // only when read from within Api function callback or Api getter/setter
  // callbacks. The caller context is set to the current context from generated
  // code/builtins right before calling the Api callback when it's guaraneed
  // that current context belongs to user JavaScript code:
  //  - when an Api getter/setter function callback is called by IC system
  //    from interpreter or baseline code,
  //  - when an Api callback is called from optimized code (Maglev or TurboFan).
  //
  // Once the caller context value becomes outdated it's reset to kNoContext
  // in order to enforce the slow mechanism involving stack iteration.
  // This happens in the following cases:
  //  - when an Api function is called as a regular JSFunction (it's not worth
  //    the efforts of properly propagating the topmost user script-having
  //    context through a potential sequence of builtin function calls),
  //  - when execution crosses C++ to JS boundary (Execution::Call*/New),
  //  - when execution crosses JS to Wasm boundary or Wasm to JS bounary
  //    (it's not worth the efforts of propagating the caller context
  //    through Wasm, especially with Wasm stack switching),
  //  - when an optimized function is deoptimized (for simplicity),
  //  - after stack unwinding because of thrown exception.
  //
  // GC treats this value as a weak reference and resets it back to kNoContext
  // if the context dies.
  Tagged<Context> topmost_script_having_context_;

  // This field is updated along with context_ on every operation triggered
  // via V8 Api.
  Address last_api_entry_;

  // ] CEntry/CallApiCallback/CallApiGetter fields.

  Tagged<Object> exception_ = Smi::zero();

  // Communication channel between Isolate::FindHandler and the CEntry.
  Tagged<Context> pending_handler_context_;
  Address pending_handler_entrypoint_;
  Address pending_handler_constant_pool_;
  Address pending_handler_fp_;
  Address pending_handler_sp_;

  // The top C++ try catch handler or nullptr if none are registered.
  //
  // This field is not guaranteed to hold an address that can be
  // used for comparison with addresses into the JS stack. If such
  // an address is needed, use try_catch_handler_address.
  v8::TryCatch* try_catch_handler_;

  // These two fields are updated rarely (on every thread restore).
  Isolate* isolate_;
  std::atomic<ThreadId> thread_id_;

  // TODO(all): Combine into a bitfield.
  uintptr_t num_frames_above_pending_handler_;
  // Wasm Stack Switching: The central stack.
  // If set, then we are currently executing code on the central stack.
  uint8_t is_on_central_stack_flag_;
  uint8_t rethrowing_message_;

  // Communication channel between Isolate::Throw and message consumers.
  Tagged<Object> pending_message_ = Smi::zero();

  // Try-blocks are chained through the stack.
  Address handler_;

  // Simulator field is always present to get predictable layout.
  Simulator* simulator_;

  // The stack pointer of the bottom JS entry frame.
  Address js_entry_sp_;
  // The external callback we're currently in.
  ExternalCallbackScope* external_callback_scope_;
  StateTag current_vm_state_;
  EmbedderState* current_embedder_state_;

  // The top entry of the v8::Context::BackupIncumbentScope stack.
  const v8::Context::BackupIncumbentScope* top_backup_incumbent_scope_;

  // Call back function to report unsafe JS accesses.
  v8::FailedAccessCheckCallback failed_access_check_callback_;

  // Address of the thread-local "thread in wasm" flag.
  Address thread_in_wasm_flag_address_;

  // On switching from the central stack these fields are set
  // to the central stack's SP and stack limit accordingly,
  // to use for switching from secondary stacks.
  Address central_stack_sp_;
  Address central_stack_limit_;
  // On switching to the central stack these fields are set
  // to the secondary stack's SP and stack limit accordingly.
  // It is used if we need to check for the stack overflow condition
  // on the secondary stack, during execution on the central stack.
  Address secondary_stack_sp_;
  Address secondary_stack_limit_;
};

static_assert(ThreadLocalTop::kSizeInBytes == sizeof(ThreadLocalTop));

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_THREAD_LOCAL_TOP_H_
