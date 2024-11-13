// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/frames.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>

#include "src/api/api-arguments.h"
#include "src/api/api-natives.h"
#include "src/base/bits.h"
#include "src/codegen/interface-descriptors.h"
#include "src/codegen/linkage-location.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/maglev-safepoint-table.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/safepoint-table.h"
#include "src/common/globals.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/arguments.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames-inl.h"
#include "src/execution/vm-state-inl.h"
#include "src/ic/ic-stats.h"
#include "src/logging/counters.h"
#include "src/objects/code.h"
#include "src/objects/slots.h"
#include "src/objects/smi.h"
#include "src/objects/visitors.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/strings/string-stream.h"
#include "src/zone/zone-containers.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/debug/debug-wasm-objects.h"
#include "src/wasm/serialized-signature-inl.h"
#include "src/wasm/stacks.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects-inl.h"
#if V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter-runtime.h"
#endif  // V8_ENABLE_DRUMBRAKE
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

ReturnAddressLocationResolver StackFrame::return_address_location_resolver_ =
    nullptr;

namespace {

Address AddressOf(const StackHandler* handler) {
  Address raw = handler->address();
#ifdef V8_USE_ADDRESS_SANITIZER
  // ASan puts C++-allocated StackHandler markers onto its fake stack.
  // We work around that by storing the real stack address in the "padding"
  // field. StackHandlers allocated from generated code have 0 as padding.
  Address padding =
      base::Memory<Address>(raw + StackHandlerConstants::kPaddingOffset);
  if (padding != 0) return padding;
#endif
  return raw;
}

}  // namespace

// Iterator that supports traversing the stack handlers of a
// particular frame. Needs to know the top of the handler chain.
class StackHandlerIterator {
 public:
  StackHandlerIterator(const StackFrame* frame, StackHandler* handler)
      : limit_(frame->fp()), handler_(handler) {
#if V8_ENABLE_WEBASSEMBLY
#if !V8_ENABLE_DRUMBRAKE || !USE_SIMULATOR
    // Make sure the handler has already been unwound to this frame. With stack
    // switching this is not equivalent to the inequality below, because the
    // frame and the handler could be in different stacks.
    DCHECK_IMPLIES(frame->isolate()->wasm_stacks().empty(),
                   frame->InFastCCall() || frame->sp() <= AddressOf(handler));
#endif  // !V8_ENABLE_DRUMBRAKE || !USE_SIMULATOR

    // For CWasmEntry frames, the handler was registered by the last C++
    // frame (Execution::CallWasm), so even though its address is already
    // beyond the limit, we know we always want to unwind one handler.
    if (frame->is_c_wasm_entry()) handler_ = handler_->next();
#if V8_ENABLE_DRUMBRAKE
    // Do the same for GenericWasmToJsInterpreterWrapper frames.
    else if (v8_flags.wasm_jitless && frame->is_wasm_to_js()) {
      handler_ = handler_->next();
#ifdef USE_SIMULATOR
      // If we are running in the simulator, the handler_ address here will
      // refer to the 'actual' stack, not to the 'simulated' stack, so we need
      // to fix 'limit_' to make sure that the StackHandlerIterator won't skip
      // any handler.
      limit_ = 0;
#endif  // USE_SIMULATOR
    }
#endif  // V8_ENABLE_DRUMBRAKE
#else
    // Make sure the handler has already been unwound to this frame.
    DCHECK_LE(frame->sp(), AddressOf(handler));
#endif  // V8_ENABLE_WEBASSEMBLY
  }

  StackHandler* handler() const { return handler_; }

  bool done() { return handler_ == nullptr || AddressOf(handler_) > limit_; }
  void Advance() {
    DCHECK(!done());
    handler_ = handler_->next();
  }

 private:
#if V8_ENABLE_DRUMBRAKE && USE_SIMULATOR
  Address limit_;
#else
  const Address limit_;
#endif  // V8_ENABLE_DRUMBRAKE && USE_SIMULATOR

  StackHandler* handler_;
};

// -------------------------------------------------------------------------

StackFrameIteratorBase::StackFrameIteratorBase(Isolate* isolate)
    : isolate_(isolate), frame_(nullptr), handler_(nullptr) {}

StackFrameIterator::StackFrameIterator(Isolate* isolate)
    : StackFrameIterator(isolate, isolate->thread_local_top()) {}

StackFrameIterator::StackFrameIterator(Isolate* isolate, ThreadLocalTop* t)
    : StackFrameIteratorBase(isolate) {
  Reset(t);
}
#if V8_ENABLE_WEBASSEMBLY
StackFrameIterator::StackFrameIterator(Isolate* isolate,
                                       wasm::StackMemory* stack)
    : StackFrameIteratorBase(isolate) {
  Reset(isolate->thread_local_top(), stack);
}
#endif

void StackFrameIterator::Advance() {
  DCHECK(!done());
  // Compute the state of the calling frame before restoring
  // callee-saved registers and unwinding handlers. This allows the
  // frame code that computes the caller state to access the top
  // handler and the value of any callee-saved register if needed.
  StackFrame::State state;
  StackFrame::Type type = frame_->GetCallerState(&state);

  // Unwind handlers corresponding to the current frame.
  StackHandlerIterator it(frame_, handler_);
  while (!it.done()) it.Advance();
  handler_ = it.handler();

  // Advance to the calling frame.
  SetNewFrame(type, &state);
  // When we're done iterating over the stack frames, the handler
  // chain must have been completely unwound. Except for wasm stack-switching:
  // we stop at the end of the current segment.
#if V8_ENABLE_WEBASSEMBLY
  DCHECK_IMPLIES(done() && isolate()->wasm_stacks().empty(),
                 handler_ == nullptr);
#else
  DCHECK_IMPLIES(done(), handler_ == nullptr);
#endif
}

StackFrame* StackFrameIterator::Reframe() {
  StackFrame::State state = frame_->state_;
  StackFrame::Type type = ComputeStackFrameType(&state);
  SetNewFrame(type, &state);
  return frame();
}

namespace {
StackFrame::Type GetStateForFastCCallCallerFP(Isolate* isolate, Address fp,
                                              Address pc, Address pc_address,
                                              StackFrame::State* state) {
  // 'Fast C calls' are a special type of C call where we call directly from
  // JS to C without an exit frame inbetween. The CEntryStub is responsible
  // for setting Isolate::c_entry_fp, meaning that it won't be set for fast C
  // calls. To keep the stack iterable, we store the FP and PC of the caller
  // of the fast C call on the isolate. This is guaranteed to be the topmost
  // JS frame, because fast C calls cannot call back into JS. We start
  // iterating the stack from this topmost JS frame.
  DCHECK_NE(kNullAddress, pc);
  state->fp = fp;
  state->sp = kNullAddress;
  state->pc_address = reinterpret_cast<Address*>(pc_address);
  state->callee_pc = kNullAddress;
  state->constant_pool_address = nullptr;
#if V8_ENABLE_WEBASSEMBLY
  if (wasm::WasmCode* code =
          wasm::GetWasmCodeManager()->LookupCode(isolate, pc)) {
    if (code->kind() == wasm::WasmCode::kWasmToJsWrapper) {
      return StackFrame::WASM_TO_JS;
    }
    DCHECK_EQ(code->kind(), wasm::WasmCode::kWasmFunction);
    return StackFrame::WASM;
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return StackFrame::TURBOFAN;
}
}  // namespace

void StackFrameIterator::Reset(ThreadLocalTop* top) {
  StackFrame::State state;
  StackFrame::Type type;

  const Address fast_c_call_caller_fp =
      isolate_->isolate_data()->fast_c_call_caller_fp();
  if (fast_c_call_caller_fp != kNullAddress) {
    const Address caller_pc = isolate_->isolate_data()->fast_c_call_caller_pc();
    const Address caller_pc_address =
        isolate_->isolate_data()->fast_c_call_caller_pc_address();
    type = GetStateForFastCCallCallerFP(isolate_, fast_c_call_caller_fp,
                                        caller_pc, caller_pc_address, &state);
  } else {
    type = ExitFrame::GetStateForFramePointer(Isolate::c_entry_fp(top), &state);
  }
    handler_ = StackHandler::FromAddress(Isolate::handler(top));
    SetNewFrame(type, &state);
}

#if V8_ENABLE_WEBASSEMBLY
void StackFrameIterator::Reset(ThreadLocalTop* top, wasm::StackMemory* stack) {
  if (stack->jmpbuf()->state == wasm::JumpBuffer::Retired) {
    return;
  }
  StackFrame::State state;
  StackSwitchFrame::GetStateForJumpBuffer(stack->jmpbuf(), &state);
  handler_ = StackHandler::FromAddress(Isolate::handler(top));
  SetNewFrame(StackFrame::STACK_SWITCH, &state);
}
#endif

void StackFrameIteratorBase::SetNewFrame(StackFrame::Type type,
                                         StackFrame::State* state) {
  SetNewFrame(type);
  DCHECK_EQ(!frame_, type == StackFrame::NO_FRAME_TYPE);
  if (frame_) frame_->state_ = *state;
}

void StackFrameIteratorBase::SetNewFrame(StackFrame::Type type) {
  switch (type) {
#define FRAME_TYPE_CASE(type, class)      \
  case StackFrame::type:                  \
    frame_ = new (&class##_) class(this); \
    return;
    STACK_FRAME_TYPE_LIST(FRAME_TYPE_CASE)
#undef FRAME_TYPE_CASE

    case StackFrame::NO_FRAME_TYPE:
    // We don't expect to see NUMBER_OF_TYPES or MANUAL, but stay robust against
    // them rather than being UNREACHABLE in case stack frame iteration gets
    // wonky.
    case StackFrame::NUMBER_OF_TYPES:
    case StackFrame::MANUAL:
      break;
  }
  frame_ = nullptr;
}

// -------------------------------------------------------------------------

void TypedFrameWithJSLinkage::Iterate(RootVisitor* v) const {
  IterateExpressions(v);
  IteratePc(v, constant_pool_address(), GcSafeLookupCode());
}

// -------------------------------------------------------------------------

void ConstructFrame::Iterate(RootVisitor* v) const {
  // The frame contains the actual argument count (intptr) that should not
  // be visited.
  FullObjectSlot argc(
      &Memory<Address>(fp() + ConstructFrameConstants::kLengthOffset));
  const int last_object_offset = ConstructFrameConstants::kLastObjectOffset;
  FullObjectSlot base(&Memory<Address>(sp()));
  FullObjectSlot limit(&Memory<Address>(fp() + last_object_offset) + 1);
  v->VisitRootPointers(Root::kStackRoots, nullptr, base, argc);
  v->VisitRootPointers(Root::kStackRoots, nullptr, argc + 1, limit);
  IteratePc(v, constant_pool_address(), GcSafeLookupCode());
}

// -------------------------------------------------------------------------

void JavaScriptStackFrameIterator::Advance() {
  do {
    iterator_.Advance();
  } while (!iterator_.done() && !iterator_.frame()->is_java_script());
}

// -------------------------------------------------------------------------

DebuggableStackFrameIterator::DebuggableStackFrameIterator(Isolate* isolate)
    : iterator_(isolate) {
  if (!done() && !IsValidFrame(iterator_.frame())) Advance();
}

DebuggableStackFrameIterator::DebuggableStackFrameIterator(Isolate* isolate,
                                                           StackFrameId id)
    : DebuggableStackFrameIterator(isolate) {
  while (!done() && frame()->id() != id) Advance();
}

void DebuggableStackFrameIterator::Advance() {
  do {
    iterator_.Advance();
  } while (!done() && !IsValidFrame(iterator_.frame()));
}

int DebuggableStackFrameIterator::FrameFunctionCount() const {
  DCHECK(!done());
  if (!iterator_.frame()->is_optimized()) return 1;
  std::vector<Tagged<SharedFunctionInfo>> infos;
  TurbofanFrame::cast(iterator_.frame())->GetFunctions(&infos);
  return static_cast<int>(infos.size());
}

FrameSummary DebuggableStackFrameIterator::GetTopValidFrame() const {
  DCHECK(!done());
  // Like FrameSummary::GetTop, but additionally observes
  // DebuggableStackFrameIterator filtering semantics.
  std::vector<FrameSummary> frames;
  frame()->Summarize(&frames);
  if (is_javascript()) {
    for (int i = static_cast<int>(frames.size()) - 1; i >= 0; i--) {
      const FrameSummary& summary = frames[i];
      if (summary.is_subject_to_debugging()) {
        return summary;
      }
    }
    UNREACHABLE();
  }
#if V8_ENABLE_WEBASSEMBLY
  if (is_wasm()) return frames.back();
#endif  // V8_ENABLE_WEBASSEMBLY
  UNREACHABLE();
}

// static
bool DebuggableStackFrameIterator::IsValidFrame(StackFrame* frame) {
  if (frame->is_java_script()) {
    Tagged<JSFunction> function =
        static_cast<JavaScriptFrame*>(frame)->function();
    return function->shared()->IsSubjectToDebugging();
  }
#if V8_ENABLE_WEBASSEMBLY
  if (frame->is_wasm()) return true;
#endif  // V8_ENABLE_WEBASSEMBLY
  return false;
}

// -------------------------------------------------------------------------

namespace {

std::optional<bool> IsInterpreterFramePc(Isolate* isolate, Address pc,
                                         StackFrame::State* state) {
  Builtin builtin = OffHeapInstructionStream::TryLookupCode(isolate, pc);
  if (builtin != Builtin::kNoBuiltinId &&
      (builtin == Builtin::kInterpreterEntryTrampoline ||
       builtin == Builtin::kInterpreterEnterAtBytecode ||
       builtin == Builtin::kInterpreterEnterAtNextBytecode ||
       builtin == Builtin::kBaselineOrInterpreterEnterAtBytecode ||
       builtin == Builtin::kBaselineOrInterpreterEnterAtNextBytecode)) {
    return true;
  } else if (v8_flags.interpreted_frames_native_stack) {
    intptr_t marker = Memory<intptr_t>(
        state->fp + CommonFrameConstants::kContextOrFrameTypeOffset);
    MSAN_MEMORY_IS_INITIALIZED(
        state->fp + StandardFrameConstants::kFunctionOffset,
        kSystemPointerSize);
    Tagged<Object> maybe_function = Tagged<Object>(
        Memory<Address>(state->fp + StandardFrameConstants::kFunctionOffset));
    // There's no need to run a full ContainsSlow if we know the frame can't be
    // an InterpretedFrame,  so we do these fast checks first
    if (StackFrame::IsTypeMarker(marker) || IsSmi(maybe_function)) {
      return false;
    } else if (!isolate->heap()->InSpaceSlow(pc, CODE_SPACE)) {
      return false;
    }
    if (!ThreadIsolation::CanLookupStartOfJitAllocationAt(pc)) {
      return {};
    }
    Tagged<Code> interpreter_entry_trampoline =
        isolate->heap()->FindCodeForInnerPointer(pc);
    return interpreter_entry_trampoline->is_interpreter_trampoline_builtin();
  } else {
    return false;
  }
}

}  // namespace

bool StackFrameIteratorForProfiler::IsNoFrameBytecodeHandlerPc(
    Isolate* isolate, Address pc, Address fp) const {
  EmbeddedData d = EmbeddedData::FromBlob(isolate);
  if (pc < d.InstructionStartOfBytecodeHandlers() ||
      pc >= d.InstructionEndOfBytecodeHandlers()) {
    return false;
  }

  Address frame_type_address =
      fp + CommonFrameConstants::kContextOrFrameTypeOffset;
  if (!IsValidStackAddress(frame_type_address)) {
    return false;
  }

  // Check if top stack frame is a bytecode handler stub frame.
  MSAN_MEMORY_IS_INITIALIZED(frame_type_address, kSystemPointerSize);
  intptr_t marker = Memory<intptr_t>(frame_type_address);
  if (StackFrame::IsTypeMarker(marker) &&
      StackFrame::MarkerToType(marker) == StackFrame::STUB) {
    // Bytecode handler built a frame.
    return false;
  }
  return true;
}

StackFrameIteratorForProfiler::StackFrameIteratorForProfiler(
    Isolate* isolate, Address pc, Address fp, Address sp, Address lr,
    Address js_entry_sp)
    : StackFrameIteratorBase(isolate),
      low_bound_(sp),
      high_bound_(js_entry_sp),
      top_frame_type_(StackFrame::NO_FRAME_TYPE),
      external_callback_scope_(isolate->external_callback_scope()),
      top_link_register_(lr)
#if V8_ENABLE_WEBASSEMBLY
      ,
      wasm_stacks_(isolate->wasm_stacks())
#endif
{
  if (!isolate->isolate_data()->stack_is_iterable()) {
    // The stack is not iterable in a short time interval during deoptimization.
    // See also: ExternalReference::stack_is_iterable_address.
    DCHECK(done());
    return;
  }

  // For Advance below, we need frame_ to be set; and that only happens if the
  // type is not NO_FRAME_TYPE.
  // TODO(jgruber): Clean this up.
  static constexpr StackFrame::Type kTypeForAdvance = StackFrame::TURBOFAN;

  StackFrame::State state;
  state.is_profiler_entry_frame = true;
  StackFrame::Type type;
  ThreadLocalTop* const top = isolate->thread_local_top();
  bool advance_frame = true;
  const Address fast_c_fp = isolate->isolate_data()->fast_c_call_caller_fp();
  if (fast_c_fp != kNullAddress) {
    // 'Fast C calls' are a special type of C call where we call directly from
    // JS to C without an exit frame inbetween. The CEntryStub is responsible
    // for setting Isolate::c_entry_fp, meaning that it won't be set for fast C
    // calls. To keep the stack iterable, we store the FP and PC of the caller
    // of the fast C call on the isolate. This is guaranteed to be the topmost
    // JS frame, because fast C calls cannot call back into JS. We start
    // iterating the stack from this topmost JS frame.
    DCHECK_NE(kNullAddress, isolate->isolate_data()->fast_c_call_caller_pc());
    state.fp = fast_c_fp;
    state.sp = sp;
    state.pc_address = reinterpret_cast<Address*>(
        isolate->isolate_data()->fast_c_call_caller_pc_address());

    // ComputeStackFrameType will read both kContextOffset and
    // kFunctionOffset, we check only that kFunctionOffset is within the stack
    // bounds and do a compile time check that kContextOffset slot is pushed on
    // the stack before kFunctionOffset.
    static_assert(StandardFrameConstants::kFunctionOffset <
                  StandardFrameConstants::kContextOffset);
    if (IsValidStackAddress(state.fp +
                            StandardFrameConstants::kFunctionOffset)) {
      type = ComputeStackFrameType(&state);
      if (IsValidFrameType(type)) {
        top_frame_type_ = type;
        advance_frame = false;
      }
    } else {
      // Cannot determine the actual type; the frame will be skipped below.
      type = kTypeForAdvance;
    }
  } else if (IsValidTop(top)) {
    type = ExitFrame::GetStateForFramePointer(Isolate::c_entry_fp(top), &state);
    top_frame_type_ = type;
  } else if (IsValidStackAddress(fp)) {
    DCHECK_NE(fp, kNullAddress);
    state.fp = fp;
    state.sp = sp;
    state.pc_address =
        StackFrame::ResolveReturnAddressLocation(reinterpret_cast<Address*>(
            fp + StandardFrameConstants::kCallerPCOffset));

    // If the current PC is in a bytecode handler, the top stack frame isn't
    // the bytecode handler's frame and the top of stack or link register is a
    // return address into the interpreter entry trampoline, then we are likely
    // in a bytecode handler with elided frame. In that case, set the PC
    // properly and make sure we do not drop the frame.
    bool is_no_frame_bytecode_handler = false;
    bool cant_lookup_frame_type = false;
    if (IsNoFrameBytecodeHandlerPc(isolate, pc, fp)) {
      Address* top_location = nullptr;
      if (top_link_register_) {
        top_location = &top_link_register_;
      } else if (IsValidStackAddress(sp)) {
        MSAN_MEMORY_IS_INITIALIZED(sp, kSystemPointerSize);
        top_location = reinterpret_cast<Address*>(sp);
      }

      std::optional<bool> is_interpreter_frame_pc =
          IsInterpreterFramePc(isolate, *top_location, &state);
      // Since we're in a signal handler, the pc lookup might not be possible
      // since the required locks are taken by the same thread.
      if (!is_interpreter_frame_pc.has_value()) {
        cant_lookup_frame_type = true;
      } else if (is_interpreter_frame_pc.value()) {
        state.pc_address = top_location;
        is_no_frame_bytecode_handler = true;
        advance_frame = false;
      }
    }

    // ComputeStackFrameType will read both kContextOffset and
    // kFunctionOffset, we check only that kFunctionOffset is within the stack
    // bounds and do a compile time check that kContextOffset slot is pushed on
    // the stack before kFunctionOffset.
    static_assert(StandardFrameConstants::kFunctionOffset <
                  StandardFrameConstants::kContextOffset);
    Address function_slot = fp + StandardFrameConstants::kFunctionOffset;
    if (cant_lookup_frame_type) {
      type = StackFrame::NO_FRAME_TYPE;
    } else if (IsValidStackAddress(function_slot)) {
      if (is_no_frame_bytecode_handler) {
        type = StackFrame::INTERPRETED;
      } else {
        type = ComputeStackFrameType(&state);
      }
      top_frame_type_ = type;
    } else {
      // Cannot determine the actual type; the frame will be skipped below.
      type = kTypeForAdvance;
    }
  } else {
    // Not iterable.
    DCHECK(done());
    return;
  }

  SetNewFrame(type, &state);
  if (advance_frame && !done()) {
    Advance();
  }
}

bool StackFrameIteratorForProfiler::IsValidTop(ThreadLocalTop* top) const {
  Address c_entry_fp = Isolate::c_entry_fp(top);
  if (!IsValidExitFrame(c_entry_fp)) return false;
  // There should be at least one JS_ENTRY stack handler.
  Address handler = Isolate::handler(top);
  if (handler == kNullAddress) return false;
  // Check that there are no js frames on top of the native frames.
  return c_entry_fp < handler;
}

void StackFrameIteratorForProfiler::AdvanceOneFrame() {
  DCHECK(!done());
  StackFrame* last_frame = frame_;
  Address last_sp = last_frame->sp(), last_fp = last_frame->fp();

  // Before advancing to the next stack frame, perform pointer validity tests.
  if (!IsValidState(last_frame->state_) ||
      !HasValidExitIfEntryFrame(last_frame)) {
    frame_ = nullptr;
    return;
  }

  // Advance to the previous frame, and perform pointer validity tests there
  // too.
  StackFrame::State state;
  last_frame->ComputeCallerState(&state);
  if (!IsValidState(state)) {
    frame_ = nullptr;
    return;
  }

  StackFrame::Type type = ComputeStackFrameType(&state);
  SetNewFrame(type, &state);
  if (!frame_) return;

  // Check that we have actually moved to the previous frame in the stack.
  if (frame_->sp() <= last_sp || frame_->fp() <= last_fp) {
    frame_ = nullptr;
  }
}

bool StackFrameIteratorForProfiler::IsValidState(
    const StackFrame::State& state) const {
  return IsValidStackAddress(state.sp) && IsValidStackAddress(state.fp);
}

bool StackFrameIteratorForProfiler::HasValidExitIfEntryFrame(
    const StackFrame* frame) const {
  if (!frame->is_entry() && !frame->is_construct_entry()) return true;

  // See EntryFrame::GetCallerState. It computes the caller FP address
  // and calls ExitFrame::GetStateForFramePointer on it. We need to be
  // sure that caller FP address is valid.
  Address next_exit_frame_fp_address =
      frame->fp() + EntryFrameConstants::kNextExitFrameFPOffset;
  // Profiling tick might be triggered in the middle of JSEntry builtin
  // before the next_exit_frame_fp value is initialized. IsValidExitFrame()
  // is able to deal with such a case, so just suppress the MSan warning.
  MSAN_MEMORY_IS_INITIALIZED(next_exit_frame_fp_address, kSystemPointerSize);
  Address next_exit_frame_fp = Memory<Address>(next_exit_frame_fp_address);
  return IsValidExitFrame(next_exit_frame_fp);
}

bool StackFrameIteratorForProfiler::IsValidExitFrame(Address fp) const {
  if (!IsValidStackAddress(fp)) return false;
  Address sp = ExitFrame::ComputeStackPointer(fp);
  if (!IsValidStackAddress(sp)) return false;
  StackFrame::State state;
  ExitFrame::FillState(fp, sp, &state);
  MSAN_MEMORY_IS_INITIALIZED(state.pc_address, sizeof(state.pc_address));
  return *state.pc_address != kNullAddress;
}

void StackFrameIteratorForProfiler::Advance() {
  while (true) {
    AdvanceOneFrame();
    if (done()) break;
    ExternalCallbackScope* last_callback_scope = nullptr;
    while (external_callback_scope_ != nullptr &&
           external_callback_scope_->scope_address() < frame_->fp()) {
      // As long as the setup of a frame is not atomic, we may happen to be
      // in an interval where an ExternalCallbackScope is already created,
      // but the frame is not yet entered. So we are actually observing
      // the previous frame.
      // Skip all the ExternalCallbackScope's that are below the current fp.
      last_callback_scope = external_callback_scope_;
      external_callback_scope_ = external_callback_scope_->previous();
    }
    if (frame_->is_java_script()) break;
#if V8_ENABLE_WEBASSEMBLY
    if (frame_->is_wasm() || frame_->is_wasm_to_js() ||
        frame_->is_js_to_wasm()) {
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    if (frame_->is_exit() || frame_->is_builtin_exit() ||
        frame_->is_api_accessor_exit() || frame_->is_api_callback_exit()) {
      // Some of the EXIT frames may have ExternalCallbackScope allocated on
      // top of them. In that case the scope corresponds to the first EXIT
      // frame beneath it. There may be other EXIT frames on top of the
      // ExternalCallbackScope, just skip them as we cannot collect any useful
      // information about them.
      if (last_callback_scope) {
        frame_->state_.pc_address =
            last_callback_scope->callback_entrypoint_address();
      }
      break;
    }
  }
}

StackFrameIteratorForProfilerForTesting::
    StackFrameIteratorForProfilerForTesting(Isolate* isolate, Address pc,
                                            Address fp, Address sp, Address lr,
                                            Address js_entry_sp)
    : StackFrameIteratorForProfiler(isolate, pc, fp, sp, lr, js_entry_sp) {}

void StackFrameIteratorForProfilerForTesting::Advance() {
  StackFrameIteratorForProfiler::Advance();
}

// -------------------------------------------------------------------------

namespace {

std::optional<Tagged<GcSafeCode>> GetContainingCode(Isolate* isolate,
                                                    Address pc) {
  return isolate->inner_pointer_to_code_cache()->GetCacheEntry(pc)->code;
}

}  // namespace

Tagged<GcSafeCode> StackFrame::GcSafeLookupCode() const {
  const Address pc = maybe_unauthenticated_pc();
  std::optional<Tagged<GcSafeCode>> result = GetContainingCode(isolate(), pc);
  DCHECK_GE(pc, result.value()->InstructionStart(isolate(), pc));
  DCHECK_LT(pc, result.value()->InstructionEnd(isolate(), pc));
  return result.value();
}

Tagged<Code> StackFrame::LookupCode() const {
  DCHECK_NE(isolate()->heap()->gc_state(), Heap::MARK_COMPACT);
  return GcSafeLookupCode()->UnsafeCastToCode();
}

void StackFrame::IteratePc(RootVisitor* v, Address* constant_pool_address,
                           Tagged<GcSafeCode> holder) const {
  const Address old_pc = maybe_unauthenticated_pc();
  DCHECK_GE(old_pc, holder->InstructionStart(isolate(), old_pc));
  DCHECK_LT(old_pc, holder->InstructionEnd(isolate(), old_pc));

  // Keep the old pc offset before visiting the code since we need it to
  // calculate the new pc after a potential InstructionStream move.
  const uintptr_t pc_offset_from_start = old_pc - holder->instruction_start();

  // Visit.
  Tagged<GcSafeCode> visited_holder = holder;
  PtrComprCageBase code_cage_base{isolate()->code_cage_base()};
  const Tagged<Object> old_istream =
      holder->raw_instruction_stream(code_cage_base);
  Tagged<Object> visited_istream = old_istream;
  v->VisitRunningCode(FullObjectSlot{&visited_holder},
                      FullObjectSlot{&visited_istream});
  if (visited_istream == old_istream) {
    // Note this covers two important cases:
    // 1. the associated InstructionStream object did not move, and
    // 2. `holder` is an embedded builtin and has no InstructionStream.
    return;
  }

  DCHECK(visited_holder->has_instruction_stream());
  // We can only relocate the InstructionStream object when we are able to patch
  // the return address. We only know the location of the return address if the
  // stack pointer is known. This means we cannot relocate InstructionStreams
  // for fast c calls.
  DCHECK(!InFastCCall());
  // Currently we turn off code space compaction fully when performing a GC in a
  // fast C call.
  DCHECK(!isolate()->InFastCCall());

  Tagged<InstructionStream> istream =
      UncheckedCast<InstructionStream>(visited_istream);
  const Address new_pc = istream->instruction_start() + pc_offset_from_start;
  // TODO(v8:10026): avoid replacing a signed pointer.
  PointerAuthentication::ReplacePC(pc_address(), new_pc, kSystemPointerSize);
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL && constant_pool_address != nullptr) {
    *constant_pool_address = istream->constant_pool();
  }
}

void StackFrame::SetReturnAddressLocationResolver(
    ReturnAddressLocationResolver resolver) {
  DCHECK_NULL(return_address_location_resolver_);
  return_address_location_resolver_ = resolver;
}

namespace {

StackFrame::Type ComputeBuiltinFrameType(Tagged<GcSafeCode> code) {
  if (code->is_interpreter_trampoline_builtin() ||
      code->is_baseline_trampoline_builtin()) {
    // Frames for baseline entry trampolines on the stack are still interpreted
    // frames.
    return StackFrame::INTERPRETED;
  } else if (code->is_baseline_leave_frame_builtin()) {
    return StackFrame::BASELINE;
  } else if (code->is_turbofanned()) {
    // TODO(bmeurer): We treat frames for BUILTIN Code objects as
    // OptimizedFrame for now (all the builtins with JavaScript linkage are
    // actually generated with TurboFan currently, so this is sound).
    return StackFrame::TURBOFAN;
  }
  return StackFrame::BUILTIN;
}

StackFrame::Type SafeStackFrameType(StackFrame::Type candidate) {
  DCHECK_LE(static_cast<uintptr_t>(candidate), StackFrame::NUMBER_OF_TYPES);
  switch (candidate) {
    case StackFrame::API_ACCESSOR_EXIT:
    case StackFrame::API_CALLBACK_EXIT:
    case StackFrame::BUILTIN_CONTINUATION:
    case StackFrame::BUILTIN_EXIT:
    case StackFrame::CONSTRUCT:
    case StackFrame::FAST_CONSTRUCT:
    case StackFrame::CONSTRUCT_ENTRY:
    case StackFrame::ENTRY:
    case StackFrame::EXIT:
    case StackFrame::INTERNAL:
    case StackFrame::IRREGEXP:
    case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION:
    case StackFrame::JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH:
    case StackFrame::STUB:
      return candidate;

#if V8_ENABLE_WEBASSEMBLY
    case StackFrame::JS_TO_WASM:
    case StackFrame::STACK_SWITCH:
    case StackFrame::WASM:
    case StackFrame::WASM_DEBUG_BREAK:
    case StackFrame::WASM_EXIT:
    case StackFrame::WASM_LIFTOFF_SETUP:
    case StackFrame::WASM_TO_JS:
#if V8_ENABLE_DRUMBRAKE
    case StackFrame::C_WASM_ENTRY:
    case StackFrame::WASM_INTERPRETER_ENTRY:
#endif  // V8_ENABLE_DRUMBRAKE
      return candidate;
#endif  // V8_ENABLE_WEBASSEMBLY

    // Any other marker value is likely to be a bogus stack frame when being
    // called from the profiler (in particular, JavaScript frames, including
    // interpreted frames, should never have a StackFrame::Type marker).
    // Consider these frames "native".
    // TODO(jgruber): For the StackFrameIterator, I'm not sure this fallback
    // makes sense. Shouldn't we know how to handle all frames we encounter
    // there?
    case StackFrame::BASELINE:
    case StackFrame::BUILTIN:
    case StackFrame::INTERPRETED:
    case StackFrame::MAGLEV:
    case StackFrame::MANUAL:
    case StackFrame::NATIVE:
    case StackFrame::NO_FRAME_TYPE:
    case StackFrame::NUMBER_OF_TYPES:
    case StackFrame::TURBOFAN:
    case StackFrame::TURBOFAN_STUB_WITH_CONTEXT:
#if V8_ENABLE_WEBASSEMBLY
#if !V8_ENABLE_DRUMBRAKE
    case StackFrame::C_WASM_ENTRY:
#endif  // !V8_ENABLE_DRUMBRAKE
    case StackFrame::WASM_TO_JS_FUNCTION:
#endif  // V8_ENABLE_WEBASSEMBLY
      return StackFrame::NATIVE;
  }
  UNREACHABLE();
}

}  // namespace

StackFrame::Type StackFrameIterator::ComputeStackFrameType(
    StackFrame::State* state) const {
#if V8_ENABLE_WEBASSEMBLY
  if (state->fp == kNullAddress) {
    DCHECK(!isolate_->wasm_stacks().empty());  // I.e., JSPI active
    return StackFrame::NO_FRAME_TYPE;
  }
#endif

  const Address pc = StackFrame::ReadPC(state->pc_address);

#if V8_ENABLE_WEBASSEMBLY
  // If the {pc} does not point into WebAssembly code we can rely on the
  // returned {wasm_code} to be null and fall back to {GetContainingCode}.
  if (wasm::WasmCode* wasm_code =
          wasm::GetWasmCodeManager()->LookupCode(isolate(), pc)) {
    switch (wasm_code->kind()) {
      case wasm::WasmCode::kWasmFunction:
        return StackFrame::WASM;
      case wasm::WasmCode::kWasmToCapiWrapper:
        return StackFrame::WASM_EXIT;
      case wasm::WasmCode::kWasmToJsWrapper:
        return StackFrame::WASM_TO_JS;
#if V8_ENABLE_DRUMBRAKE
      case wasm::WasmCode::kInterpreterEntry:
        return StackFrame::WASM_INTERPRETER_ENTRY;
#endif  // V8_ENABLE_DRUMBRAKE
      default:
        UNREACHABLE();
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // Look up the code object to figure out the type of the stack frame.
  std::optional<Tagged<GcSafeCode>> lookup_result =
      GetContainingCode(isolate(), pc);
  if (!lookup_result.has_value()) return StackFrame::NATIVE;

  MSAN_MEMORY_IS_INITIALIZED(
      state->fp + CommonFrameConstants::kContextOrFrameTypeOffset,
      kSystemPointerSize);
  const intptr_t marker = Memory<intptr_t>(
      state->fp + CommonFrameConstants::kContextOrFrameTypeOffset);
  switch (lookup_result.value()->kind()) {
    case CodeKind::BUILTIN: {
      if (StackFrame::IsTypeMarker(marker)) break;
      return ComputeBuiltinFrameType(lookup_result.value());
    }
    case CodeKind::BASELINE:
      return StackFrame::BASELINE;
    case CodeKind::MAGLEV:
      if (StackFrame::IsTypeMarker(marker)) {
        // An INTERNAL frame can be set up with an associated Maglev code
        // object when calling into runtime to handle tiering. In this case,
        // all stack slots are tagged pointers and should be visited through
        // the usual logic.
        DCHECK_EQ(StackFrame::MarkerToType(marker), StackFrame::INTERNAL);
        return StackFrame::INTERNAL;
      }
      return StackFrame::MAGLEV;
    case CodeKind::TURBOFAN:
      return StackFrame::TURBOFAN;
#if V8_ENABLE_WEBASSEMBLY
    case CodeKind::JS_TO_WASM_FUNCTION:
      if (lookup_result.value()->builtin_id() == Builtin::kJSToWasmWrapperAsm) {
        return StackFrame::JS_TO_WASM;
      }
#if V8_ENABLE_DRUMBRAKE
      if (lookup_result.value()->builtin_id() ==
          Builtin::kGenericJSToWasmInterpreterWrapper) {
        return StackFrame::JS_TO_WASM;
      }
#endif  // V8_ENABLE_DRUMBRAKE
      return StackFrame::TURBOFAN_STUB_WITH_CONTEXT;
    case CodeKind::C_WASM_ENTRY:
      return StackFrame::C_WASM_ENTRY;
    case CodeKind::WASM_TO_JS_FUNCTION:
      return StackFrame::WASM_TO_JS_FUNCTION;
    case CodeKind::WASM_FUNCTION:
    case CodeKind::WASM_TO_CAPI_FUNCTION:
      // These never appear as on-heap Code objects.
      UNREACHABLE();
#else
    case CodeKind::C_WASM_ENTRY:
    case CodeKind::JS_TO_WASM_FUNCTION:
    case CodeKind::WASM_FUNCTION:
    case CodeKind::WASM_TO_CAPI_FUNCTION:
    case CodeKind::WASM_TO_JS_FUNCTION:
      UNREACHABLE();
#endif  // V8_ENABLE_WEBASSEMBLY
    case CodeKind::BYTECODE_HANDLER:
    case CodeKind::FOR_TESTING:
    case CodeKind::REGEXP:
    case CodeKind::INTERPRETED_FUNCTION:
      // Fall back to the marker.
      break;
  }

  return SafeStackFrameType(StackFrame::MarkerToType(marker));
}

StackFrame::Type StackFrameIteratorForProfiler::ComputeStackFrameType(
    StackFrame::State* state) const {
#if V8_ENABLE_WEBASSEMBLY
  if (state->fp == kNullAddress) {
    DCHECK(!isolate_->wasm_stacks().empty());  // I.e., JSPI active
    return StackFrame::NO_FRAME_TYPE;
  }
#endif

  // We use unauthenticated_pc because it may come from
  // fast_c_call_caller_pc_address, for which authentication does not work.
  const Address pc = StackFrame::unauthenticated_pc(state->pc_address);
#if V8_ENABLE_WEBASSEMBLY
  Tagged<Code> wrapper =
      isolate()->builtins()->code(Builtin::kWasmToJsWrapperCSA);
  if (pc >= wrapper->instruction_start() && pc <= wrapper->instruction_end()) {
    return StackFrame::WASM_TO_JS;
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  MSAN_MEMORY_IS_INITIALIZED(
      state->fp + CommonFrameConstants::kContextOrFrameTypeOffset,
      kSystemPointerSize);
  const intptr_t marker = Memory<intptr_t>(
      state->fp + CommonFrameConstants::kContextOrFrameTypeOffset);
  if (StackFrame::IsTypeMarker(marker)) {
    if (static_cast<uintptr_t>(marker) > StackFrame::NUMBER_OF_TYPES) {
      // We've read some bogus value from the stack.
      return StackFrame::NATIVE;
    }
    return SafeStackFrameType(StackFrame::MarkerToType(marker));
  }

  MSAN_MEMORY_IS_INITIALIZED(
      state->fp + StandardFrameConstants::kFunctionOffset, kSystemPointerSize);
  Tagged<Object> maybe_function = Tagged<Object>(
      Memory<Address>(state->fp + StandardFrameConstants::kFunctionOffset));
  if (IsSmi(maybe_function)) {
    return StackFrame::NATIVE;
  }

  std::optional<bool> is_interpreter_frame =
      IsInterpreterFramePc(isolate(), pc, state);

  // We might not be able to lookup the frame type since we're inside a signal
  // handler and the required locks are taken.
  if (!is_interpreter_frame.has_value()) {
    return StackFrame::NO_FRAME_TYPE;
  }

  if (is_interpreter_frame.value()) {
    return StackFrame::INTERPRETED;
  }

  return StackFrame::TURBOFAN;
}

StackFrame::Type StackFrame::GetCallerState(State* state) const {
  ComputeCallerState(state);
  return iterator_->ComputeStackFrameType(state);
}

Address CommonFrame::GetCallerStackPointer() const {
  return fp() + CommonFrameConstants::kCallerSPOffset;
}

void NativeFrame::ComputeCallerState(State* state) const {
  state->sp = caller_sp();
  state->fp = Memory<Address>(fp() + CommonFrameConstants::kCallerFPOffset);
  state->pc_address = ResolveReturnAddressLocation(
      reinterpret_cast<Address*>(fp() + CommonFrameConstants::kCallerPCOffset));
  state->callee_pc = kNullAddress;
  state->constant_pool_address = nullptr;
}

Tagged<HeapObject> EntryFrame::unchecked_code() const {
  return isolate()->builtins()->code(Builtin::kJSEntry);
}

void EntryFrame::ComputeCallerState(State* state) const {
  GetCallerState(state);
}

StackFrame::Type EntryFrame::GetCallerState(State* state) const {
  const Address fast_c_call_caller_fp =
      Memory<Address>(fp() + EntryFrameConstants::kNextFastCallFrameFPOffset);
  if (fast_c_call_caller_fp != kNullAddress) {
    Address caller_pc_address =
        fp() + EntryFrameConstants::kNextFastCallFramePCOffset;
    Address caller_pc = Memory<Address>(caller_pc_address);
    return GetStateForFastCCallCallerFP(isolate(), fast_c_call_caller_fp,
                                        caller_pc, caller_pc_address, state);
  }
  Address next_exit_frame_fp =
      Memory<Address>(fp() + EntryFrameConstants::kNextExitFrameFPOffset);
  return ExitFrame::GetStateForFramePointer(next_exit_frame_fp, state);
}

#if V8_ENABLE_WEBASSEMBLY
StackFrame::Type CWasmEntryFrame::GetCallerState(State* state) const {
  const int offset = CWasmEntryFrameConstants::kCEntryFPOffset;
  Address fp = Memory<Address>(this->fp() + offset);
  return ExitFrame::GetStateForFramePointer(fp, state);
}

#if V8_ENABLE_DRUMBRAKE
void CWasmEntryFrame::Iterate(RootVisitor* v) const {
  if (!v8_flags.wasm_jitless) {
    StubFrame::Iterate(v);
  }
}
#endif  // V8_ENABLE_DRUMBRAKE

#endif  // V8_ENABLE_WEBASSEMBLY

Tagged<HeapObject> ConstructEntryFrame::unchecked_code() const {
  return isolate()->builtins()->code(Builtin::kJSConstructEntry);
}

void ExitFrame::ComputeCallerState(State* state) const {
  // Set up the caller state.
  state->sp = caller_sp();
  state->fp = Memory<Address>(fp() + ExitFrameConstants::kCallerFPOffset);
  state->pc_address = ResolveReturnAddressLocation(
      reinterpret_cast<Address*>(fp() + ExitFrameConstants::kCallerPCOffset));
  state->callee_pc = kNullAddress;
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    state->constant_pool_address = reinterpret_cast<Address*>(
        fp() + ExitFrameConstants::kConstantPoolOffset);
  }
}

void ExitFrame::Iterate(RootVisitor* v) const {
  // The arguments are traversed as part of the expression stack of
  // the calling frame.
  IteratePc(v, constant_pool_address(), GcSafeLookupCode());
}

StackFrame::Type ExitFrame::GetStateForFramePointer(Address fp, State* state) {
  if (fp == 0) return NO_FRAME_TYPE;
  StackFrame::Type type = ComputeFrameType(fp);
#if V8_ENABLE_WEBASSEMBLY
  Address sp = type == WASM_EXIT ? WasmExitFrame::ComputeStackPointer(fp)
                                 : ExitFrame::ComputeStackPointer(fp);
#else
  Address sp = ExitFrame::ComputeStackPointer(fp);
#endif  // V8_ENABLE_WEBASSEMBLY
  FillState(fp, sp, state);
  DCHECK_NE(*state->pc_address, kNullAddress);
  return type;
}

StackFrame::Type ExitFrame::ComputeFrameType(Address fp) {
  // Distinguish between different exit frame types.
  // Default to EXIT in all hairy cases (e.g., when called from profiler).
  const int offset = ExitFrameConstants::kFrameTypeOffset;
  Tagged<Object> marker(Memory<Address>(fp + offset));

  if (!IsSmi(marker)) {
    return EXIT;
  }

  intptr_t marker_int = base::bit_cast<intptr_t>(marker);

  StackFrame::Type frame_type = static_cast<StackFrame::Type>(marker_int >> 1);
  switch (frame_type) {
    case BUILTIN_EXIT:
    case API_ACCESSOR_EXIT:
    case API_CALLBACK_EXIT:
#if V8_ENABLE_WEBASSEMBLY
    case WASM_EXIT:
    case STACK_SWITCH:
#endif  // V8_ENABLE_WEBASSEMBLY
      return frame_type;
    default:
      return EXIT;
  }
}

Address ExitFrame::ComputeStackPointer(Address fp) {
  MSAN_MEMORY_IS_INITIALIZED(fp + ExitFrameConstants::kSPOffset,
                             kSystemPointerSize);
  return Memory<Address>(fp + ExitFrameConstants::kSPOffset);
}

#if V8_ENABLE_WEBASSEMBLY
Address WasmExitFrame::ComputeStackPointer(Address fp) {
  // For WASM_EXIT frames, {sp} is only needed for finding the PC slot,
  // everything else is handled via safepoint information.
  Address sp = fp + WasmExitFrameConstants::kWasmInstanceOffset;
  DCHECK_EQ(sp - 1 * kPCOnStackSize,
            fp + WasmExitFrameConstants::kCallingPCOffset);
  return sp;
}
#endif  // V8_ENABLE_WEBASSEMBLY

void ExitFrame::FillState(Address fp, Address sp, State* state) {
  state->sp = sp;
  state->fp = fp;
  state->pc_address = ResolveReturnAddressLocation(
      reinterpret_cast<Address*>(sp - 1 * kPCOnStackSize));
  state->callee_pc = kNullAddress;
  // The constant pool recorded in the exit frame is not associated
  // with the pc in this state (the return address into a C entry
  // stub).  ComputeCallerState will retrieve the constant pool
  // together with the associated caller pc.
  state->constant_pool_address = nullptr;
}

void BuiltinExitFrame::Summarize(std::vector<FrameSummary>* frames) const {
  DCHECK(frames->empty());
  DirectHandle<FixedArray> parameters = GetParameters();
  DisallowGarbageCollection no_gc;
  Tagged<Code> code = LookupCode();
  int code_offset = code->GetOffsetFromInstructionStart(isolate(), pc());
  FrameSummary::JavaScriptFrameSummary summary(
      isolate(), receiver(), function(), Cast<AbstractCode>(code), code_offset,
      IsConstructor(), *parameters);
  frames->push_back(summary);
}

Tagged<JSFunction> BuiltinExitFrame::function() const {
  return Cast<JSFunction>(target_slot_object());
}

Tagged<Object> BuiltinExitFrame::receiver() const {
  return receiver_slot_object();
}

Tagged<Object> BuiltinExitFrame::GetParameter(int i) const {
  DCHECK(i >= 0 && i < ComputeParametersCount());
  int offset =
      BuiltinExitFrameConstants::kFirstArgumentOffset + i * kSystemPointerSize;
  return Tagged<Object>(Memory<Address>(fp() + offset));
}

int BuiltinExitFrame::ComputeParametersCount() const {
  Tagged<Object> argc_slot = argc_slot_object();
  DCHECK(IsSmi(argc_slot));
  // Argc also counts the receiver, target, new target, and argc itself as args,
  // therefore the real argument count is argc - 4.
  int argc = Smi::ToInt(argc_slot) - 4;
  DCHECK_GE(argc, 0);
  return argc;
}

Handle<FixedArray> BuiltinExitFrame::GetParameters() const {
  if (V8_LIKELY(!v8_flags.detailed_error_stack_trace)) {
    return isolate()->factory()->empty_fixed_array();
  }
  int param_count = ComputeParametersCount();
  auto parameters = isolate()->factory()->NewFixedArray(param_count);
  for (int i = 0; i < param_count; i++) {
    parameters->set(i, GetParameter(i));
  }
  return parameters;
}

bool BuiltinExitFrame::IsConstructor() const {
  return !IsUndefined(new_target_slot_object(), isolate());
}

// Ensure layout of v8::FunctionCallbackInfo is in sync with
// ApiCallbackExitFrameConstants.
namespace ensure_layout {
using FC = ApiCallbackExitFrameConstants;
using FCA = FunctionCallbackArguments;
static_assert(FC::kFunctionCallbackInfoContextIndex == FCA::kContextIndex);
static_assert(FC::kFunctionCallbackInfoReturnValueIndex ==
              FCA::kReturnValueIndex);
static_assert(FC::kFunctionCallbackInfoTargetIndex == FCA::kTargetIndex);
static_assert(FC::kFunctionCallbackInfoNewTargetIndex == FCA::kNewTargetIndex);
static_assert(FC::kFunctionCallbackInfoArgsLength == FCA::kArgsLength);
}  // namespace ensure_layout

Handle<JSFunction> ApiCallbackExitFrame::GetFunction() const {
  Tagged<HeapObject> maybe_function = target();
  if (IsJSFunction(maybe_function)) {
    return Handle<JSFunction>(target_slot().location());
  }
  DCHECK(IsFunctionTemplateInfo(maybe_function));
  Handle<FunctionTemplateInfo> function_template_info(
      Cast<FunctionTemplateInfo>(maybe_function), isolate());

  // Instantiate function for the correct context.
  DCHECK(IsContext(context()));
  Handle<NativeContext> native_context(
      Cast<Context>(context())->native_context(), isolate());

  Handle<JSFunction> function =
      ApiNatives::InstantiateFunction(isolate(), native_context,
                                      function_template_info)
          .ToHandleChecked();

  set_target(*function);
  return function;
}

Handle<FunctionTemplateInfo> ApiCallbackExitFrame::GetFunctionTemplateInfo()
    const {
  Tagged<HeapObject> maybe_function = target();
  if (IsJSFunction(maybe_function)) {
    Tagged<SharedFunctionInfo> shared_info =
        Cast<JSFunction>(maybe_function)->shared();
    DCHECK(shared_info->IsApiFunction());
    return handle(shared_info->api_func_data(), isolate());
  }
  DCHECK(IsFunctionTemplateInfo(maybe_function));
  return handle(Cast<FunctionTemplateInfo>(maybe_function), isolate());
}

Handle<FixedArray> ApiCallbackExitFrame::GetParameters() const {
  if (V8_LIKELY(!v8_flags.detailed_error_stack_trace)) {
    return isolate()->factory()->empty_fixed_array();
  }
  int param_count = ComputeParametersCount();
  auto parameters = isolate()->factory()->NewFixedArray(param_count);
  for (int i = 0; i < param_count; i++) {
    parameters->set(i, GetParameter(i));
  }
  return parameters;
}

void ApiCallbackExitFrame::Summarize(std::vector<FrameSummary>* frames) const {
  DCHECK(frames->empty());
  DirectHandle<FixedArray> parameters = GetParameters();
  DirectHandle<JSFunction> function = GetFunction();
  DisallowGarbageCollection no_gc;
  Tagged<Code> code = LookupCode();
  int code_offset = code->GetOffsetFromInstructionStart(isolate(), pc());
  FrameSummary::JavaScriptFrameSummary summary(
      isolate(), receiver(), *function, Cast<AbstractCode>(code), code_offset,
      IsConstructor(), *parameters);
  frames->push_back(summary);
}

// Ensure layout of v8::PropertyCallbackInfo is in sync with
// ApiAccessorExitFrameConstants.
static_assert(
    ApiAccessorExitFrameConstants::kPropertyCallbackInfoPropertyKeyIndex ==
    PropertyCallbackArguments::kPropertyKeyIndex);
static_assert(
    ApiAccessorExitFrameConstants::kPropertyCallbackInfoReturnValueIndex ==
    PropertyCallbackArguments::kReturnValueIndex);
static_assert(
    ApiAccessorExitFrameConstants::kPropertyCallbackInfoReceiverIndex ==
    PropertyCallbackArguments::kThisIndex);
static_assert(ApiAccessorExitFrameConstants::kPropertyCallbackInfoHolderIndex ==
              PropertyCallbackArguments::kHolderIndex);
static_assert(ApiAccessorExitFrameConstants::kPropertyCallbackInfoArgsLength ==
              PropertyCallbackArguments::kArgsLength);

void ApiAccessorExitFrame::Summarize(std::vector<FrameSummary>* frames) const {
  // This frame is not supposed to appear in exception stack traces.
  DCHECK(IsName(property_name()));
  DCHECK(IsJSReceiver(receiver()));
  DCHECK(IsJSReceiver(holder()));
}

namespace {
void PrintIndex(StringStream* accumulator, StackFrame::PrintMode mode,
                int index) {
  accumulator->Add((mode == StackFrame::OVERVIEW) ? "%5d: " : "[%d]: ", index);
}

const char* StringForStackFrameType(StackFrame::Type type) {
  switch (type) {
#define CASE(value, name) \
  case StackFrame::value: \
    return #name;
    STACK_FRAME_TYPE_LIST(CASE)
#undef CASE
    default:
      UNREACHABLE();
  }
}
}  // namespace

void StackFrame::Print(StringStream* accumulator, PrintMode mode,
                       int index) const {
  DisallowGarbageCollection no_gc;
  PrintIndex(accumulator, mode, index);
  accumulator->Add(StringForStackFrameType(type()));
  accumulator->Add(" [pc: %p]\n", reinterpret_cast<void*>(pc()));
}

void BuiltinExitFrame::Print(StringStream* accumulator, PrintMode mode,
                             int index) const {
  DisallowGarbageCollection no_gc;
  Tagged<Object> receiver = this->receiver();
  Tagged<JSFunction> function = this->function();

  accumulator->PrintSecurityTokenIfChanged(function);
  PrintIndex(accumulator, mode, index);
  accumulator->Add("builtin exit frame: ");
  if (IsConstructor()) accumulator->Add("new ");
  accumulator->PrintFunction(function, receiver);

  accumulator->Add("(this=%o", receiver);

  // Print the parameters.
  int parameters_count = ComputeParametersCount();
  for (int i = 0; i < parameters_count; i++) {
    accumulator->Add(",%o", GetParameter(i));
  }

  accumulator->Add(")\n\n");
}

void ApiCallbackExitFrame::Print(StringStream* accumulator, PrintMode mode,
                                 int index) const {
  DirectHandle<JSFunction> function = GetFunction();
  DisallowGarbageCollection no_gc;
  Tagged<Object> receiver = this->receiver();

  accumulator->PrintSecurityTokenIfChanged(*function);
  PrintIndex(accumulator, mode, index);
  accumulator->Add("api callback exit frame: ");
  if (IsConstructor()) accumulator->Add("new ");
  accumulator->PrintFunction(*function, receiver);

  accumulator->Add("(this=%o", receiver);

  // Print the parameters.
  int parameters_count = ComputeParametersCount();
  for (int i = 0; i < parameters_count; i++) {
    accumulator->Add(",%o", GetParameter(i));
  }

  accumulator->Add(")\n\n");
}

void ApiAccessorExitFrame::Print(StringStream* accumulator, PrintMode mode,
                                 int index) const {
  DisallowGarbageCollection no_gc;

  PrintIndex(accumulator, mode, index);
  accumulator->Add("api accessor exit frame: ");

  Tagged<Name> name = property_name();
  Tagged<Object> receiver = this->receiver();
  Tagged<Object> holder = this->holder();
  accumulator->Add("(this=%o, holder=%o, name=%o)\n", receiver, holder, name);
}

Address CommonFrame::GetExpressionAddress(int n) const {
  const int offset = StandardFrameConstants::kExpressionsOffset;
  return fp() + offset - n * kSystemPointerSize;
}

Address UnoptimizedFrame::GetExpressionAddress(int n) const {
  const int offset = UnoptimizedFrameConstants::kExpressionsOffset;
  return fp() + offset - n * kSystemPointerSize;
}

Tagged<Object> CommonFrame::context() const {
  return ReadOnlyRoots(isolate()).undefined_value();
}

int CommonFrame::position() const {
  Tagged<Code> code = LookupCode();
  int code_offset = code->GetOffsetFromInstructionStart(isolate(), pc());
  return code->SourcePosition(code_offset);
}

int CommonFrame::ComputeExpressionsCount() const {
  Address base = GetExpressionAddress(0);
  Address limit = sp() - kSystemPointerSize;
  DCHECK(base >= limit);  // stack grows downwards
  // Include register-allocated locals in number of expressions.
  return static_cast<int>((base - limit) / kSystemPointerSize);
}

void CommonFrame::ComputeCallerState(State* state) const {
  state->fp = caller_fp();
#if V8_ENABLE_WEBASSEMBLY
  if (state->fp == kNullAddress) {
    // An empty FP signals the first frame of a stack segment. The caller is
    // on a different stack, or is unbound (suspended stack).
    // DCHECK(isolate_->wasm_stacks() != nullptr); // I.e., JSPI active
    return;
  }
#endif
  state->sp = caller_sp();
  state->pc_address = ResolveReturnAddressLocation(reinterpret_cast<Address*>(
      fp() + StandardFrameConstants::kCallerPCOffset));
  state->callee_fp = fp();
  state->callee_pc = maybe_unauthenticated_pc();
  state->constant_pool_address = reinterpret_cast<Address*>(
      fp() + StandardFrameConstants::kConstantPoolOffset);
}

void CommonFrame::Summarize(std::vector<FrameSummary>* functions) const {
  // This should only be called on frames which override this method.
  UNREACHABLE();
}

namespace {
void VisitSpillSlot(Isolate* isolate, RootVisitor* v,
                    FullObjectSlot spill_slot) {
#ifdef V8_COMPRESS_POINTERS
  PtrComprCageBase cage_base(isolate);
  bool was_compressed = false;

  // Spill slots may contain compressed values in which case the upper
  // 32-bits will contain zeros. In order to simplify handling of such
  // slots in GC we ensure that the slot always contains full value.

  // The spill slot may actually contain weak references so we load/store
  // values using spill_slot.location() in order to avoid dealing with
  // FullMaybeObjectSlots here.
  if (V8_EXTERNAL_CODE_SPACE_BOOL) {
    // When external code space is enabled the spill slot could contain both
    // InstructionStream and non-InstructionStream references, which have
    // different cage bases. So unconditional decompression of the value might
    // corrupt InstructionStream pointers. However, given that 1) the
    // InstructionStream pointers are never compressed by design (because
    //    otherwise we wouldn't know which cage base to apply for
    //    decompression, see respective DCHECKs in
    //    RelocInfo::target_object()),
    // 2) there's no need to update the upper part of the full pointer
    //    because if it was there then it'll stay the same,
    // we can avoid updating upper part of the spill slot if it already
    // contains full value.
    // TODO(v8:11880): Remove this special handling by enforcing builtins
    // to use CodeTs instead of InstructionStream objects.
    Address value = *spill_slot.location();
    if (!HAS_SMI_TAG(value) && value <= 0xffffffff) {
      // We don't need to update smi values or full pointers.
      was_compressed = true;
      *spill_slot.location() = V8HeapCompressionScheme::DecompressTagged(
          cage_base, static_cast<Tagged_t>(value));
      if (DEBUG_BOOL) {
        // Ensure that the spill slot contains correct heap object.
        Tagged<HeapObject> raw =
            Cast<HeapObject>(Tagged<Object>(*spill_slot.location()));
        MapWord map_word = raw->map_word(cage_base, kRelaxedLoad);
        Tagged<HeapObject> forwarded = map_word.IsForwardingAddress()
                                           ? map_word.ToForwardingAddress(raw)
                                           : raw;
        bool is_self_forwarded =
            forwarded->map_word(cage_base, kRelaxedLoad) ==
            MapWord::FromForwardingAddress(forwarded, forwarded);
        if (is_self_forwarded) {
          // The object might be in a self-forwarding state if it's located
          // in new large object space. GC will fix this at a later stage.
          CHECK(
              MemoryChunk::FromHeapObject(forwarded)->InNewLargeObjectSpace());
        } else {
          Tagged<HeapObject> forwarded_map = forwarded->map(cage_base);
          // The map might be forwarded as well.
          MapWord fwd_map_map_word =
              forwarded_map->map_word(cage_base, kRelaxedLoad);
          if (fwd_map_map_word.IsForwardingAddress()) {
            forwarded_map = fwd_map_map_word.ToForwardingAddress(forwarded_map);
          }
          CHECK(IsMap(forwarded_map, cage_base));
        }
      }
    }
  } else {
    Address slot_contents = *spill_slot.location();
    Tagged_t compressed_value = static_cast<Tagged_t>(slot_contents);
    if (!HAS_SMI_TAG(compressed_value)) {
      was_compressed = slot_contents <= 0xFFFFFFFF;
      // We don't need to update smi values.
      *spill_slot.location() = V8HeapCompressionScheme::DecompressTagged(
          cage_base, compressed_value);
    }
  }
#endif
  v->VisitRootPointer(Root::kStackRoots, nullptr, spill_slot);
#if V8_COMPRESS_POINTERS
  if (was_compressed) {
    // Restore compression. Generated code should be able to trust that
    // compressed spill slots remain compressed.
    *spill_slot.location() =
        V8HeapCompressionScheme::CompressObject(*spill_slot.location());
  }
#endif
}

void VisitSpillSlots(Isolate* isolate, RootVisitor* v,
                     FullObjectSlot first_slot_offset,
                     base::Vector<const uint8_t> tagged_slots) {
  FullObjectSlot slot_offset = first_slot_offset;
  for (uint8_t bits : tagged_slots) {
    while (bits) {
      const int bit = base::bits::CountTrailingZeros(bits);
      bits &= ~(1 << bit);
      FullObjectSlot spill_slot = slot_offset + bit;
      VisitSpillSlot(isolate, v, spill_slot);
    }
    slot_offset += kBitsPerByte;
  }
}

SafepointEntry GetSafepointEntryFromCodeCache(
    Isolate* isolate, Address inner_pointer,
    InnerPointerToCodeCache::InnerPointerToCodeCacheEntry* entry) {
  if (!entry->safepoint_entry.is_initialized()) {
    entry->safepoint_entry =
        SafepointTable::FindEntry(isolate, entry->code.value(), inner_pointer);
    DCHECK(entry->safepoint_entry.is_initialized());
  } else {
    DCHECK_EQ(
        entry->safepoint_entry,
        SafepointTable::FindEntry(isolate, entry->code.value(), inner_pointer));
  }
  return entry->safepoint_entry;
}

MaglevSafepointEntry GetMaglevSafepointEntryFromCodeCache(
    Isolate* isolate, Address inner_pointer,
    InnerPointerToCodeCache::InnerPointerToCodeCacheEntry* entry) {
  if (!entry->maglev_safepoint_entry.is_initialized()) {
    entry->maglev_safepoint_entry = MaglevSafepointTable::FindEntry(
        isolate, entry->code.value(), inner_pointer);
    DCHECK(entry->maglev_safepoint_entry.is_initialized());
  } else {
    DCHECK_EQ(entry->maglev_safepoint_entry,
              MaglevSafepointTable::FindEntry(isolate, entry->code.value(),
                                              inner_pointer));
  }
  return entry->maglev_safepoint_entry;
}

}  // namespace

#ifdef V8_ENABLE_WEBASSEMBLY
#if V8_ENABLE_DRUMBRAKE
// Class DrumBrakeWasmCode is an adapter class that exposes just the accessors
// of the original WasmCode class that are used in WasmFrame::Iterate. For non
// DrumBrake frames, the class calls the corresponding accessor in a contained
// WasmCode object, while for DrumBrake frames it returns dummy values. This is
// useful to minimize the merge issues in WasmFrame::Iterate.
class DrumBrakeWasmCode {
 public:
  explicit DrumBrakeWasmCode(wasm::WasmCode* wasm_code)
      : wasm_code_(wasm_code) {}

  static std::unique_ptr<DrumBrakeWasmCode> Interpreted() {
    return std::make_unique<DrumBrakeWasmCode>(nullptr);
  }
  static std::unique_ptr<DrumBrakeWasmCode> Compiled(
      wasm::WasmCode* wasm_code) {
    return std::make_unique<DrumBrakeWasmCode>(wasm_code);
  }

  bool is_liftoff() const {
    return wasm_code_ ? wasm_code_->is_liftoff() : false;
  }
  bool frame_has_feedback_slot() const {
    return wasm_code_ ? wasm_code_->frame_has_feedback_slot() : false;
  }
  int stack_slots() const { return wasm_code_ ? wasm_code_->stack_slots() : 0; }
  wasm::WasmCode::Kind kind() const {
    return wasm_code_ ? wasm_code_->kind() : wasm::WasmCode::kInterpreterEntry;
  }
  uint16_t first_tagged_parameter_slot() const {
    return wasm_code_ ? wasm_code_->first_tagged_parameter_slot() : 0;
  }
  uint16_t num_tagged_parameter_slots() const {
    return wasm_code_ ? wasm_code_->num_tagged_parameter_slots() : 0;
  }

 private:
  const wasm::WasmCode* wasm_code_;
};
#endif  // V8_ENABLE_DRUMBRAKE

void WasmFrame::Iterate(RootVisitor* v) const {
  DCHECK(!iterator_->IsStackFrameIteratorForProfiler());

  //  ===  WasmFrame ===
  //  +-------------------------+-----------------------------------------
  //  |   out_param n           |  <-- parameters_base / sp
  //  |       ...               |
  //  |   out_param 0           |  (these can be tagged or untagged)
  //  +-------------------------+-----------------------------------------
  //  |   spill_slot n          |  <-- parameters_limit                  ^
  //  |       ...               |                               spill_slot_space
  //  |   spill_slot 0          |                                        v
  //  +-------------------------+-----------------------------------------
  //  | WasmFeedback(*)         |  <-- frame_header_base                 ^
  //  |- - - - - - - - - - - - -|                                        |
  //  | WasmTrustedInstanceData |                                        |
  //  |- - - - - - - - - - - - -|                                        |
  //  |   Type Marker           |                                        |
  //  |- - - - - - - - - - - - -|                              frame_header_size
  //  | [Constant Pool]         |                                        |
  //  |- - - - - - - - - - - - -|                                        |
  //  | saved frame ptr         |  <-- fp                                |
  //  |- - - - - - - - - - - - -|                                        |
  //  |  return addr            |  <-- tagged_parameter_limit            v
  //  +-------------------------+-----------------------------------------
  //  |    in_param n           |
  //  |       ...               |
  //  |    in_param 0           |  <-- first_tagged_parameter_slot
  //  +-------------------------+-----------------------------------------
  //
  // (*) Only if compiled by Liftoff and with --experimental-wasm-inlining.

#if !V8_ENABLE_DRUMBRAKE
  auto pair = wasm::GetWasmCodeManager()->LookupCodeAndSafepoint(
      isolate(), maybe_unauthenticated_pc());
  wasm::WasmCode* wasm_code = pair.first;
  SafepointEntry safepoint_entry = pair.second;
#else   // !V8_ENABLE_DRUMBRAKE
  std::unique_ptr<DrumBrakeWasmCode> interpreter_wasm_code;
  SafepointEntry safepoint_entry;
  bool is_wasm_interpreter_frame =
      v8_flags.wasm_jitless &&
      (type() == WASM_INTERPRETER_ENTRY || type() == C_WASM_ENTRY);
  if (is_wasm_interpreter_frame) {
    interpreter_wasm_code = DrumBrakeWasmCode::Interpreted();
  } else {
    auto pair =
        wasm::GetWasmCodeManager()->LookupCodeAndSafepoint(isolate(), pc());
    wasm::WasmCode* wasm_code = pair.first;
    safepoint_entry = pair.second;
    DCHECK(wasm_code);
    interpreter_wasm_code = DrumBrakeWasmCode::Compiled(wasm_code);
  }

  // Reuse the same name "wasm_code" for this variable, to use the
  // DrumBrakeWasmCode adapter and minimize merge issues in the following code.
  DrumBrakeWasmCode* wasm_code = interpreter_wasm_code.get();
#endif  // !V8_ENABLE_DRUMBRAKE

  intptr_t marker =
      Memory<intptr_t>(fp() + CommonFrameConstants::kContextOrFrameTypeOffset);
  DCHECK(StackFrame::IsTypeMarker(marker));
  StackFrame::Type type = StackFrame::MarkerToType(marker);
  DCHECK(type == WASM_TO_JS || type == WASM || type == WASM_EXIT);

  // Determine the fixed header and spill slot area size.
  // The last value in the frame header is the calling PC, which should
  // not be visited.
  static_assert(WasmExitFrameConstants::kFixedSlotCountFromFp ==
                    WasmFrameConstants::kFixedSlotCountFromFp + 1,
                "WasmExitFrame has one slot more than WasmFrame");

  int frame_header_size = WasmFrameConstants::kFixedFrameSizeFromFp;
  if (wasm_code->is_liftoff() && wasm_code->frame_has_feedback_slot()) {
    // Frame has Wasm feedback slot.
    frame_header_size += kSystemPointerSize;
  }
  int spill_slot_space =
      wasm_code->stack_slots() * kSystemPointerSize -
      (frame_header_size + StandardFrameConstants::kFixedFrameSizeAboveFp);
  // Fixed frame slots.
  FullObjectSlot frame_header_base(&Memory<Address>(fp() - frame_header_size));
  FullObjectSlot frame_header_limit(
      &Memory<Address>(fp() - StandardFrameConstants::kCPSlotSize));

  // Parameters passed to the callee.
  Address central_stack_sp = Memory<Address>(
      fp() + WasmImportWrapperFrameConstants::kCentralStackSPOffset);
  FullObjectSlot parameters_limit(
      type == WASM_TO_JS && central_stack_sp != kNullAddress
          ? central_stack_sp
          : frame_header_base.address() - spill_slot_space);
  FullObjectSlot spill_space_end =
      FullObjectSlot(frame_header_base.address() - spill_slot_space);

  // Visit the rest of the parameters if they are tagged.
  bool has_tagged_outgoing_params =
      wasm_code->kind() != wasm::WasmCode::kWasmFunction &&
      wasm_code->kind() != wasm::WasmCode::kWasmToCapiWrapper;
  if (!InFastCCall() && has_tagged_outgoing_params) {
    FullObjectSlot parameters_base(&Memory<Address>(sp()));
    v->VisitRootPointers(Root::kStackRoots, nullptr, parameters_base,
                         parameters_limit);
  }

  // Visit pointer spill slots and locals.
  if (safepoint_entry.is_initialized()) {
    DCHECK_GE((wasm_code->stack_slots() + kBitsPerByte) / kBitsPerByte,
              safepoint_entry.tagged_slots().size());
    VisitSpillSlots(isolate(), v, spill_space_end,
                    safepoint_entry.tagged_slots());
  }

  // Visit tagged parameters that have been passed to the function of this
  // frame. Conceptionally these parameters belong to the parent frame. However,
  // the exact count is only known by this frame (in the presence of tail calls,
  // this information cannot be derived from the call site).
  if (wasm_code->num_tagged_parameter_slots() > 0) {
    FullObjectSlot tagged_parameter_base(&Memory<Address>(caller_sp()));
    tagged_parameter_base += wasm_code->first_tagged_parameter_slot();
    FullObjectSlot tagged_parameter_limit =
        tagged_parameter_base + wasm_code->num_tagged_parameter_slots();

    v->VisitRootPointers(Root::kStackRoots, nullptr, tagged_parameter_base,
                         tagged_parameter_limit);
  }

  // Visit the instance object.
  v->VisitRootPointers(Root::kStackRoots, nullptr, frame_header_base,
                       frame_header_limit);
}

void TypedFrame::IterateParamsOfGenericWasmToJSWrapper(RootVisitor* v) const {
  Tagged<Object> maybe_signature = Tagged<Object>(
      Memory<Address>(fp() + WasmToJSWrapperConstants::kSignatureOffset));
  if (IsSmi(maybe_signature)) {
    // The signature slot contains a Smi and not a signature. This means all
    // incoming parameters have been processed, and we don't have to keep them
    // alive anymore.
    return;
  }

  FullObjectSlot sig_slot(fp() + WasmToJSWrapperConstants::kSignatureOffset);
  VisitSpillSlot(isolate(), v, sig_slot);

  // Load the signature, considering forward pointers.
  PtrComprCageBase cage_base(isolate());
  Tagged<HeapObject> raw = Cast<HeapObject>(maybe_signature);
  MapWord map_word = raw->map_word(cage_base, kRelaxedLoad);
  Tagged<HeapObject> forwarded =
      map_word.IsForwardingAddress() ? map_word.ToForwardingAddress(raw) : raw;
  Tagged<PodArray<wasm::ValueType>> sig =
      Cast<PodArray<wasm::ValueType>>(forwarded);

  size_t parameter_count = wasm::SerializedSignatureHelper::ParamCount(sig);
  wasm::LinkageLocationAllocator allocator(wasm::kGpParamRegisters,
                                           wasm::kFpParamRegisters, 0);
  // The first parameter is the instance, which we don't have to scan. We have
  // to tell the LinkageLocationAllocator about it though.
  allocator.Next(MachineRepresentation::kTaggedPointer);

  // Parameters are separated into two groups (first all untagged, then all
  // tagged parameters). Therefore we first have to iterate over the signature
  // first to process all untagged parameters, and afterwards we can scan the
  // tagged parameters.
  bool has_tagged_param = false;
  for (size_t i = 0; i < parameter_count; i++) {
    wasm::ValueType type = wasm::SerializedSignatureHelper::GetParam(sig, i);
    MachineRepresentation param = type.machine_representation();
    // Skip tagged parameters (e.g. any-ref).
    if (IsAnyTagged(param)) {
      has_tagged_param = true;
      continue;
    }
    if (kSystemPointerSize == 8 || param != MachineRepresentation::kWord64) {
      allocator.Next(param);
    } else {
      allocator.Next(MachineRepresentation::kWord32);
      allocator.Next(MachineRepresentation::kWord32);
    }
  }

  // End the untagged area, so tagged slots come after. This means, especially,
  // that tagged parameters should not fill holes in the untagged area.
  allocator.EndSlotArea();

  if (!has_tagged_param) return;

#if V8_TARGET_ARCH_ARM64
  constexpr size_t size_of_sig = 2;
#else
  constexpr size_t size_of_sig = 1;
#endif

  for (size_t i = 0; i < parameter_count; i++) {
    wasm::ValueType type = wasm::SerializedSignatureHelper::GetParam(sig, i);
    MachineRepresentation param = type.machine_representation();
    // Skip untagged parameters.
    if (!IsAnyTagged(param)) continue;
    LinkageLocation l = allocator.Next(param);
    if (l.IsRegister()) {
      // Calculate the slot offset.
      int slot_offset = 0;
      // We have to do a reverse lookup in the kGPParamRegisters array. This
      // can be optimized if necessary.
      for (size_t i = 1; i < arraysize(wasm::kGpParamRegisters); ++i) {
        if (wasm::kGpParamRegisters[i].code() == l.AsRegister()) {
          // The first register (the instance) does not get spilled.
          slot_offset = static_cast<int>(i) - 1;
          break;
        }
      }
      // Caller FP + return address + signature + two stack-switching slots.
      size_t param_start_offset = 2 + size_of_sig + 2;
      FullObjectSlot param_start(fp() +
                                 param_start_offset * kSystemPointerSize);
      FullObjectSlot tagged_slot = param_start + slot_offset;
      VisitSpillSlot(isolate(), v, tagged_slot);
    } else {
      // Caller frame slots have negative indices and start at -1. Flip it
      // back to a positive offset (to be added to the frame's FP to find the
      // slot).
      int slot_offset = -l.GetLocation() - 1;
      // Caller FP + return address + signature + two stack-switching slots +
      // spilled registers (without the instance register).
      size_t slots_per_float64 = kDoubleSize / kSystemPointerSize;
      size_t param_start_offset =
          arraysize(wasm::kGpParamRegisters) - 1 +
          (arraysize(wasm::kFpParamRegisters) * slots_per_float64) + 2 +
          size_of_sig + 2;

      // The wasm-to-js wrapper pushes all but the first gp parameter register
      // on the stack, so if the number of gp parameter registers is even, this
      // means that the wrapper pushed an odd number. In that case, and when the
      // size of a double on the stack is two words, then there is an alignment
      // word between the pushed gp registers and the pushed fp registers, so
      // that the whole spill area is double-size aligned.
      if (arraysize(wasm::kGpParamRegisters) % 2 == (0) &&
          kSystemPointerSize != kDoubleSize) {
        param_start_offset++;
      }
      FullObjectSlot param_start(fp() +
                                 param_start_offset * kSystemPointerSize);
      FullObjectSlot tagged_slot = param_start + slot_offset;
      VisitSpillSlot(isolate(), v, tagged_slot);
    }
  }
}

void TypedFrame::IterateParamsOfOptimizedWasmToJSWrapper(RootVisitor* v) const {
  Tagged<GcSafeCode> code = GcSafeLookupCode();
  if (code->wasm_js_tagged_parameter_count() > 0) {
    FullObjectSlot tagged_parameter_base(&Memory<Address>(caller_sp()));
    tagged_parameter_base += code->wasm_js_first_tagged_parameter();
    FullObjectSlot tagged_parameter_limit =
        tagged_parameter_base + code->wasm_js_tagged_parameter_count();
    v->VisitRootPointers(Root::kStackRoots, nullptr, tagged_parameter_base,
                         tagged_parameter_limit);
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

void TypedFrame::Iterate(RootVisitor* v) const {
  DCHECK(!iterator_->IsStackFrameIteratorForProfiler());

  //  ===  TypedFrame ===
  //  +-----------------+-----------------------------------------
  //  |   out_param n   |  <-- parameters_base / sp
  //  |       ...       |
  //  |   out_param 0   |
  //  +-----------------+-----------------------------------------
  //  |   spill_slot n  |  <-- parameters_limit          ^
  //  |       ...       |                          spill_slot_count
  //  |   spill_slot 0  |                                v
  //  +-----------------+-----------------------------------------
  //  |   Type Marker   |  <-- frame_header_base         ^
  //  |- - - - - - - - -|                                |
  //  | [Constant Pool] |                                |
  //  |- - - - - - - - -|                           kFixedSlotCount
  //  | saved frame ptr |  <-- fp                        |
  //  |- - - - - - - - -|                                |
  //  |  return addr    |                                v
  //  +-----------------+-----------------------------------------

  // Find the code and compute the safepoint information.
  Address inner_pointer = pc();
  InnerPointerToCodeCache::InnerPointerToCodeCacheEntry* entry =
      isolate()->inner_pointer_to_code_cache()->GetCacheEntry(inner_pointer);
  CHECK(entry->code.has_value());
  Tagged<GcSafeCode> code = entry->code.value();
#if V8_ENABLE_WEBASSEMBLY
  bool is_generic_wasm_to_js =
      code->is_builtin() && code->builtin_id() == Builtin::kWasmToJsWrapperCSA;
  bool is_optimized_wasm_to_js = this->type() == WASM_TO_JS_FUNCTION;
  if (is_generic_wasm_to_js) {
    IterateParamsOfGenericWasmToJSWrapper(v);
  } else if (is_optimized_wasm_to_js) {
    IterateParamsOfOptimizedWasmToJSWrapper(v);
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  DCHECK(code->is_turbofanned());
  SafepointEntry safepoint_entry =
      GetSafepointEntryFromCodeCache(isolate(), inner_pointer, entry);

#ifdef DEBUG
  intptr_t marker =
      Memory<intptr_t>(fp() + CommonFrameConstants::kContextOrFrameTypeOffset);
  DCHECK(StackFrame::IsTypeMarker(marker));
#endif  // DEBUG

  // Determine the fixed header and spill slot area size.
  int frame_header_size = TypedFrameConstants::kFixedFrameSizeFromFp;
  int spill_slots_size =
      code->stack_slots() * kSystemPointerSize -
      (frame_header_size + StandardFrameConstants::kFixedFrameSizeAboveFp);

  // Fixed frame slots.
  FullObjectSlot frame_header_base(&Memory<Address>(fp() - frame_header_size));
  FullObjectSlot frame_header_limit(
      &Memory<Address>(fp() - StandardFrameConstants::kCPSlotSize));
  // Parameters passed to the callee.
#if V8_ENABLE_WEBASSEMBLY
  // Load the central stack SP value from the fixed slot.
  // If it is null, the import wrapper didn't switch and the layout is the same
  // as regular typed frames: the outgoing stack parameters end where the spill
  // area begins.
  // Otherwise, it holds the address in the central stack where the import
  // wrapper switched to before pushing the outgoing stack parameters and
  // calling the target. It marks the limit of the stack param area, and is
  // distinct from the beginning of the spill area.
  int central_stack_sp_offset =
      is_generic_wasm_to_js
          ? WasmToJSWrapperConstants::kCentralStackSPOffset
          : WasmImportWrapperFrameConstants::kCentralStackSPOffset;
  Address central_stack_sp = Memory<Address>(fp() + central_stack_sp_offset);
  FullObjectSlot parameters_limit(
      (is_generic_wasm_to_js || is_optimized_wasm_to_js) &&
              central_stack_sp != kNullAddress
          ? central_stack_sp
          : frame_header_base.address() - spill_slots_size);
#else
  FullObjectSlot parameters_limit(frame_header_base.address() -
                                  spill_slots_size);
#endif
  FullObjectSlot parameters_base(&Memory<Address>(sp()));
  FullObjectSlot spill_slots_end(frame_header_base.address() -
                                 spill_slots_size);

  // Visit the rest of the parameters.
  if (HasTaggedOutgoingParams(code)) {
    v->VisitRootPointers(Root::kStackRoots, nullptr, parameters_base,
                         parameters_limit);
  }

  // Visit pointer spill slots and locals.
  DCHECK_GE((code->stack_slots() + kBitsPerByte) / kBitsPerByte,
            safepoint_entry.tagged_slots().size());
  VisitSpillSlots(isolate(), v, spill_slots_end,
                  safepoint_entry.tagged_slots());

  // Visit fixed header region.
  v->VisitRootPointers(Root::kStackRoots, nullptr, frame_header_base,
                       frame_header_limit);

  // Visit the return address in the callee and incoming arguments.
  IteratePc(v, constant_pool_address(), code);
}

void MaglevFrame::Iterate(RootVisitor* v) const {
  DCHECK(!iterator_->IsStackFrameIteratorForProfiler());

  //  ===  MaglevFrame ===
  //  +-----------------+-----------------------------------------
  //  |   out_param n   |  <-- parameters_base / sp
  //  |       ...       |
  //  |   out_param 0   |
  //  +-----------------+-----------------------------------------
  //  | pushed_double n |  <-- parameters_limit          ^
  //  |       ...       |                                |
  //  | pushed_double 0 |                                |
  //  +- - - - - - - - -+                     num_extra_spill_slots
  //  |   pushed_reg n  |                                |
  //  |       ...       |                                |
  //  |   pushed_reg 0  |  <-- pushed_register_base      v
  //  +-----------------+-----------------------------------------
  //  | untagged_slot n |                                ^
  //  |       ...       |                                |
  //  | untagged_slot 0 |                                |
  //  +- - - - - - - - -+                         spill_slot_count
  //  |  tagged_slot n  |                                |
  //  |       ...       |                                |
  //  |  tagged_slot 0  |                                v
  //  +-----------------+-----------------------------------------
  //  |      argc       |  <-- frame_header_base         ^
  //  |- - - - - - - - -|                                |
  //  |   JSFunction    |                                |
  //  |- - - - - - - - -|                                |
  //  |    Context      |                                |
  //  |- - - - - - - - -|                          kFixedSlotCount
  //  | [Constant Pool] |                                |
  //  |- - - - - - - - -|                                |
  //  | saved frame ptr |  <-- fp                        |
  //  |- - - - - - - - -|                                |
  //  |  return addr    |                                v
  //  +-----------------+-----------------------------------------

  // Find the code and compute the safepoint information.
  Address inner_pointer = pc();
  InnerPointerToCodeCache::InnerPointerToCodeCacheEntry* entry =
      isolate()->inner_pointer_to_code_cache()->GetCacheEntry(inner_pointer);
  CHECK(entry->code.has_value());
  Tagged<GcSafeCode> code = entry->code.value();
  DCHECK(code->is_maglevved());
  MaglevSafepointEntry maglev_safepoint_entry =
      GetMaglevSafepointEntryFromCodeCache(isolate(), inner_pointer, entry);

#ifdef DEBUG
  // Assert that it is a JS frame and it has a context.
  intptr_t marker =
      Memory<intptr_t>(fp() + CommonFrameConstants::kContextOrFrameTypeOffset);
  DCHECK(!StackFrame::IsTypeMarker(marker));
#endif  // DEBUG

  // Fixed frame slots.
  FullObjectSlot frame_header_base(
      &Memory<Address>(fp() - StandardFrameConstants::kFixedFrameSizeFromFp));
  FullObjectSlot frame_header_limit(
      &Memory<Address>(fp() - StandardFrameConstants::kCPSlotSize));

  // Determine spill slot area count.
  uint32_t tagged_slot_count = maglev_safepoint_entry.num_tagged_slots();
  uint32_t spill_slot_count =
      tagged_slot_count + maglev_safepoint_entry.num_untagged_slots();
  DCHECK_EQ(code->stack_slots(),
            StandardFrameConstants::kFixedSlotCount +
                maglev_safepoint_entry.num_tagged_slots() +
                maglev_safepoint_entry.num_untagged_slots());

  // Visit the outgoing parameters if they are tagged.
  DCHECK(code->has_tagged_outgoing_params());
  FullObjectSlot parameters_base(&Memory<Address>(sp()));
  FullObjectSlot parameters_limit =
      frame_header_base - spill_slot_count -
      maglev_safepoint_entry.num_extra_spill_slots();
  v->VisitRootPointers(Root::kStackRoots, nullptr, parameters_base,
                       parameters_limit);

  // Maglev can also spill registers, tagged and untagged, just before making
  // a call. These are distinct from normal spill slots and live between the
  // normal spill slots and the pushed parameters. Some of these are tagged,
  // as indicated by the tagged register indexes, and should be visited too.
  if (maglev_safepoint_entry.num_extra_spill_slots() > 0) {
    FullObjectSlot pushed_register_base =
        frame_header_base - spill_slot_count - 1;
    uint32_t tagged_register_indexes =
        maglev_safepoint_entry.tagged_register_indexes();
    while (tagged_register_indexes != 0) {
      int index = base::bits::CountTrailingZeros(tagged_register_indexes);
      tagged_register_indexes &= ~(1 << index);
      FullObjectSlot spill_slot = pushed_register_base - index;
      VisitSpillSlot(isolate(), v, spill_slot);
    }
  }

  // Visit tagged spill slots.
  for (uint32_t i = 0; i < tagged_slot_count; ++i) {
    FullObjectSlot spill_slot = frame_header_base - 1 - i;
    VisitSpillSlot(isolate(), v, spill_slot);
  }

  // Visit fixed header region (the context and JSFunction), skipping the
  // argument count since it is stored untagged.
  v->VisitRootPointers(Root::kStackRoots, nullptr, frame_header_base + 1,
                       frame_header_limit);

  // Visit the return address in the callee and incoming arguments.
  IteratePc(v, constant_pool_address(), code);
}

Handle<JSFunction> MaglevFrame::GetInnermostFunction() const {
  std::vector<FrameSummary> frames;
  Summarize(&frames);
  return frames.back().AsJavaScript().function();
}

BytecodeOffset MaglevFrame::GetBytecodeOffsetForOSR() const {
  int deopt_index = SafepointEntry::kNoDeoptIndex;
  Tagged<Code> code = LookupCode();
  const Tagged<DeoptimizationData> data =
      GetDeoptimizationData(code, &deopt_index);
  if (deopt_index == SafepointEntry::kNoDeoptIndex) {
    CHECK(data.is_null());
    FATAL("Missing deoptimization information for OptimizedFrame::Summarize.");
  }

  DeoptimizationFrameTranslation::Iterator it(
      data->FrameTranslation(), data->TranslationIndex(deopt_index).value());
  // Search the innermost interpreter frame and get its bailout id. The
  // translation stores frames bottom up.
  int js_frames = it.EnterBeginOpcode().js_frame_count;
  DCHECK_GT(js_frames, 0);
  BytecodeOffset offset = BytecodeOffset::None();
  while (js_frames > 0) {
    TranslationOpcode frame = it.SeekNextJSFrame();
    --js_frames;
    if (IsTranslationInterpreterFrameOpcode(frame)) {
      offset = BytecodeOffset(it.NextOperand());
      it.SkipOperands(TranslationOpcodeOperandCount(frame) - 1);
    } else {
      it.SkipOperands(TranslationOpcodeOperandCount(frame));
    }
  }

  return offset;
}

bool CommonFrame::HasTaggedOutgoingParams(
    Tagged<GcSafeCode> code_lookup) const {
#if V8_ENABLE_WEBASSEMBLY
  // With inlined JS-to-Wasm calls, we can be in an OptimizedFrame and
  // directly call a Wasm function from JavaScript. In this case the Wasm frame
  // is responsible for visiting incoming potentially tagged parameters.
  // (This is required for tail-call support: If the direct callee tail-called
  // another function which then caused a GC, the caller would not be able to
  // determine where there might be tagged parameters.)
  wasm::WasmCode* wasm_callee =
      wasm::GetWasmCodeManager()->LookupCode(isolate(), callee_pc());
  if (wasm_callee) return false;

  Tagged<Code> wrapper =
      isolate()->builtins()->code(Builtin::kWasmToJsWrapperCSA);
  if (callee_pc() >= wrapper->instruction_start() &&
      callee_pc() <= wrapper->instruction_end()) {
    return false;
  }
  return code_lookup->has_tagged_outgoing_params();
#else
  return code_lookup->has_tagged_outgoing_params();
#endif  // V8_ENABLE_WEBASSEMBLY
}

Tagged<HeapObject> TurbofanStubWithContextFrame::unchecked_code() const {
  std::optional<Tagged<GcSafeCode>> code_lookup =
      isolate()->heap()->GcSafeTryFindCodeForInnerPointer(pc());
  if (!code_lookup.has_value()) return {};
  return code_lookup.value();
}

void CommonFrame::IterateTurbofanOptimizedFrame(RootVisitor* v) const {
  DCHECK(!iterator_->IsStackFrameIteratorForProfiler());

  //  ===  TurbofanFrame ===
  //  +-----------------+-----------------------------------------
  //  |   out_param n   |  <-- parameters_base / sp
  //  |       ...       |
  //  |   out_param 0   |
  //  +-----------------+-----------------------------------------
  //  |   spill_slot n  | <-- parameters_limit           ^
  //  |       ...       |                          spill_slot_count
  //  |   spill_slot 0  |                                v
  //  +-----------------+-----------------------------------------
  //  |      argc       |  <-- frame_header_base         ^
  //  |- - - - - - - - -|                                |
  //  |   JSFunction    |                                |
  //  |- - - - - - - - -|                                |
  //  |    Context      |                                |
  //  |- - - - - - - - -|                           kFixedSlotCount
  //  | [Constant Pool] |                                |
  //  |- - - - - - - - -|                                |
  //  | saved frame ptr |  <-- fp                        |
  //  |- - - - - - - - -|                                |
  //  |  return addr    |                                v
  //  +-----------------+-----------------------------------------

  // Find the code and compute the safepoint information.
  const Address inner_pointer = maybe_unauthenticated_pc();
  InnerPointerToCodeCache::InnerPointerToCodeCacheEntry* entry =
      isolate()->inner_pointer_to_code_cache()->GetCacheEntry(inner_pointer);
  CHECK(entry->code.has_value());
  Tagged<GcSafeCode> code = entry->code.value();
  DCHECK(code->is_turbofanned());
  SafepointEntry safepoint_entry =
      GetSafepointEntryFromCodeCache(isolate(), inner_pointer, entry);

#ifdef DEBUG
  // Assert that it is a JS frame and it has a context.
  intptr_t marker =
      Memory<intptr_t>(fp() + CommonFrameConstants::kContextOrFrameTypeOffset);
  DCHECK(!StackFrame::IsTypeMarker(marker));
#endif  // DEBUG

  // Determine the fixed header and spill slot area size.
  int frame_header_size = StandardFrameConstants::kFixedFrameSizeFromFp;
  int spill_slot_count =
      code->stack_slots() - StandardFrameConstants::kFixedSlotCount;

  // Fixed frame slots.
  FullObjectSlot frame_header_base(&Memory<Address>(fp() - frame_header_size));
  FullObjectSlot frame_header_limit(
      &Memory<Address>(fp() - StandardFrameConstants::kCPSlotSize));

  FullObjectSlot parameters_limit = frame_header_base - spill_slot_count;

  if (!InFastCCall()) {
    // Parameters passed to the callee.
    FullObjectSlot parameters_base(&Memory<Address>(sp()));

    // Visit the outgoing parameters if they are tagged.
    if (HasTaggedOutgoingParams(code)) {
      v->VisitRootPointers(Root::kStackRoots, nullptr, parameters_base,
                           parameters_limit);
    }
  } else {
    // There are no outgoing parameters to visit for fast C calls.
  }

  // Spill slots are in the region ]frame_header_base, parameters_limit];
  // Visit pointer spill slots and locals.
  DCHECK_GE((code->stack_slots() + kBitsPerByte) / kBitsPerByte,
            safepoint_entry.tagged_slots().size());
  VisitSpillSlots(isolate(), v, parameters_limit,
                  safepoint_entry.tagged_slots());

  // Visit fixed header region (the context and JSFunction), skipping the
  // argument count since it is stored untagged.
  v->VisitRootPointers(Root::kStackRoots, nullptr, frame_header_base + 1,
                       frame_header_limit);

  // Visit the return address in the callee and incoming arguments.
  IteratePc(v, constant_pool_address(), code);
}

void TurbofanStubWithContextFrame::Iterate(RootVisitor* v) const {
  return IterateTurbofanOptimizedFrame(v);
}

void TurbofanFrame::Iterate(RootVisitor* v) const {
  return IterateTurbofanOptimizedFrame(v);
}

Tagged<HeapObject> StubFrame::unchecked_code() const {
  std::optional<Tagged<GcSafeCode>> code_lookup =
      isolate()->heap()->GcSafeTryFindCodeForInnerPointer(pc());
  if (!code_lookup.has_value()) return {};
  return code_lookup.value();
}

int StubFrame::LookupExceptionHandlerInTable() {
  Tagged<Code> code = LookupCode();
  DCHECK(code->is_turbofanned());
  DCHECK(code->has_handler_table());
  HandlerTable table(code);
  int pc_offset = code->GetOffsetFromInstructionStart(isolate(), pc());
  return table.LookupReturn(pc_offset);
}

void StubFrame::Summarize(std::vector<FrameSummary>* frames) const {
#if V8_ENABLE_WEBASSEMBLY
  Tagged<Code> code = LookupCode();
  if (code->kind() != CodeKind::BUILTIN) return;
  // We skip most stub frames from stack traces, but a few builtins
  // specifically exist to pretend to be another builtin throwing an
  // exception.
  switch (code->builtin_id()) {
    case Builtin::kThrowDataViewTypeError:
    case Builtin::kThrowDataViewDetachedError:
    case Builtin::kThrowDataViewOutOfBounds:
    case Builtin::kThrowIndexOfCalledOnNull:
    case Builtin::kThrowToLowerCaseCalledOnNull:
    case Builtin::kWasmIntToString: {
      // When adding builtins here, also implement naming support for them.
      DCHECK_NE(nullptr,
                Builtins::NameForStackTrace(isolate(), code->builtin_id()));
      FrameSummary::BuiltinFrameSummary summary(isolate(), code->builtin_id());
      frames->push_back(summary);
      break;
    }
    default:
      break;
  }
#endif  // V8_ENABLE_WEBASSEMBLY
}

void JavaScriptFrame::SetParameterValue(int index, Tagged<Object> value) const {
  Memory<Address>(GetParameterSlot(index)) = value.ptr();
}

bool JavaScriptFrame::IsConstructor() const {
  return IsConstructFrame(caller_fp());
}

Tagged<HeapObject> CommonFrameWithJSLinkage::unchecked_code() const {
  return function()->code(isolate());
}

int TurbofanFrame::ComputeParametersCount() const {
  if (GcSafeLookupCode()->kind() == CodeKind::BUILTIN) {
    return static_cast<int>(
               Memory<intptr_t>(fp() + StandardFrameConstants::kArgCOffset)) -
           kJSArgcReceiverSlots;
  } else {
    return JavaScriptFrame::ComputeParametersCount();
  }
}

Address JavaScriptFrame::GetCallerStackPointer() const {
  return fp() + StandardFrameConstants::kCallerSPOffset;
}

void JavaScriptFrame::GetFunctions(
    std::vector<Tagged<SharedFunctionInfo>>* functions) const {
  DCHECK(functions->empty());
  functions->push_back(function()->shared());
}

void JavaScriptFrame::GetFunctions(
    std::vector<Handle<SharedFunctionInfo>>* functions) const {
  DCHECK(functions->empty());
  std::vector<Tagged<SharedFunctionInfo>> raw_functions;
  GetFunctions(&raw_functions);
  for (const auto& raw_function : raw_functions) {
    functions->push_back(
        Handle<SharedFunctionInfo>(raw_function, function()->GetIsolate()));
  }
}

bool CommonFrameWithJSLinkage::IsConstructor() const {
  return IsConstructFrame(caller_fp());
}

void CommonFrameWithJSLinkage::Summarize(
    std::vector<FrameSummary>* functions) const {
  DCHECK(functions->empty());
  Tagged<GcSafeCode> code = GcSafeLookupCode();
  int offset = code->GetOffsetFromInstructionStart(isolate(), pc());
  DirectHandle<AbstractCode> abstract_code(
      Cast<AbstractCode>(code->UnsafeCastToCode()), isolate());
  DirectHandle<FixedArray> params = GetParameters();
  FrameSummary::JavaScriptFrameSummary summary(
      isolate(), receiver(), function(), *abstract_code, offset,
      IsConstructor(), *params);
  functions->push_back(summary);
}

Tagged<JSFunction> JavaScriptFrame::function() const {
  return Cast<JSFunction>(function_slot_object());
}

Tagged<Object> JavaScriptFrame::unchecked_function() const {
  // During deoptimization of an optimized function, we may have yet to
  // materialize some closures on the stack. The arguments marker object
  // marks this case.
  DCHECK(IsJSFunction(function_slot_object()) ||
         ReadOnlyRoots(isolate()).arguments_marker() == function_slot_object());
  return function_slot_object();
}

Tagged<Object> CommonFrameWithJSLinkage::receiver() const {
  // TODO(cbruni): document this better
  return GetParameter(-1);
}

Tagged<Object> JavaScriptFrame::context() const {
  const int offset = StandardFrameConstants::kContextOffset;
  Tagged<Object> maybe_result(Memory<Address>(fp() + offset));
  DCHECK(!IsSmi(maybe_result));
  return maybe_result;
}

Tagged<Script> JavaScriptFrame::script() const {
  return Cast<Script>(function()->shared()->script());
}

int CommonFrameWithJSLinkage::LookupExceptionHandlerInTable(
    int* stack_depth, HandlerTable::CatchPrediction* prediction) {
  if (DEBUG_BOOL) {
    Tagged<Code> code_lookup_result = LookupCode();
    CHECK(!code_lookup_result->has_handler_table());
    CHECK(!code_lookup_result->is_optimized_code() ||
          code_lookup_result->kind() == CodeKind::BASELINE);
  }
  return -1;
}

void JavaScriptFrame::PrintFunctionAndOffset(Isolate* isolate,
                                             Tagged<JSFunction> function,
                                             Tagged<AbstractCode> code,
                                             int code_offset, FILE* file,
                                             bool print_line_number) {
  PtrComprCageBase cage_base = GetPtrComprCageBase(function);
  PrintF(file, "%s", CodeKindToMarker(code->kind(cage_base)));
  function->PrintName(file);
  PrintF(file, "+%d", code_offset);
  if (print_line_number) {
    Tagged<SharedFunctionInfo> shared = function->shared();
    int source_pos = code->SourcePosition(isolate, code_offset);
    Tagged<Object> maybe_script = shared->script();
    if (IsScript(maybe_script)) {
      Tagged<Script> script = Cast<Script>(maybe_script);
      int line = script->GetLineNumber(source_pos) + 1;
      Tagged<Object> script_name_raw = script->name();
      if (IsString(script_name_raw)) {
        Tagged<String> script_name = Cast<String>(script->name());
        std::unique_ptr<char[]> c_script_name =
            script_name->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
        PrintF(file, " at %s:%d", c_script_name.get(), line);
      } else {
        PrintF(file, " at <unknown>:%d", line);
      }
    } else {
      PrintF(file, " at <unknown>:<unknown>");
    }
  }
}

void JavaScriptFrame::PrintTop(Isolate* isolate, FILE* file, bool print_args,
                               bool print_line_number) {
  // constructor calls
  DisallowGarbageCollection no_gc;
  JavaScriptStackFrameIterator it(isolate);
  while (!it.done()) {
    if (it.frame()->is_java_script()) {
      JavaScriptFrame* frame = it.frame();
      if (frame->IsConstructor()) PrintF(file, "new ");
      Tagged<JSFunction> function = frame->function();
      int code_offset = 0;
      Tagged<AbstractCode> abstract_code = function->abstract_code(isolate);
      if (frame->is_interpreted()) {
        InterpretedFrame* iframe = reinterpret_cast<InterpretedFrame*>(frame);
        code_offset = iframe->GetBytecodeOffset();
      } else if (frame->is_baseline()) {
        // TODO(pthier): AbstractCode should fully support Baseline code.
        BaselineFrame* baseline_frame = BaselineFrame::cast(frame);
        code_offset = baseline_frame->GetBytecodeOffset();
        abstract_code = Cast<AbstractCode>(baseline_frame->GetBytecodeArray());
      } else {
        code_offset = frame->LookupCode()->GetOffsetFromInstructionStart(
            isolate, frame->pc());
      }
      PrintFunctionAndOffset(isolate, function, abstract_code, code_offset,
                             file, print_line_number);
      if (print_args) {
        // function arguments
        // (we are intentionally only printing the actually
        // supplied parameters, not all parameters required)
        PrintF(file, "(this=");
        ShortPrint(frame->receiver(), file);
        const int length = frame->ComputeParametersCount();
        for (int i = 0; i < length; i++) {
          PrintF(file, ", ");
          ShortPrint(frame->GetParameter(i), file);
        }
        PrintF(file, ")");
      }
      break;
    }
    it.Advance();
  }
}

// static
void JavaScriptFrame::CollectFunctionAndOffsetForICStats(
    Isolate* isolate, Tagged<JSFunction> function, Tagged<AbstractCode> code,
    int code_offset) {
  auto ic_stats = ICStats::instance();
  ICInfo& ic_info = ic_stats->Current();
  PtrComprCageBase cage_base = GetPtrComprCageBase(function);
  Tagged<SharedFunctionInfo> shared = function->shared(cage_base);

  ic_info.function_name = ic_stats->GetOrCacheFunctionName(isolate, function);
  ic_info.script_offset = code_offset;

  int source_pos = code->SourcePosition(isolate, code_offset);
  Tagged<Object> maybe_script = shared->script(cage_base, kAcquireLoad);
  if (IsScript(maybe_script, cage_base)) {
    Tagged<Script> script = Cast<Script>(maybe_script);
    Script::PositionInfo info;
    script->GetPositionInfo(source_pos, &info);
    ic_info.line_num = info.line + 1;
    ic_info.column_num = info.column + 1;
    ic_info.script_name = ic_stats->GetOrCacheScriptName(script);
  }
}

Tagged<Object> CommonFrameWithJSLinkage::GetParameter(int index) const {
  return Tagged<Object>(Memory<Address>(GetParameterSlot(index)));
}

int CommonFrameWithJSLinkage::ComputeParametersCount() const {
  DCHECK(!iterator_->IsStackFrameIteratorForProfiler() &&
         isolate()->heap()->gc_state() == Heap::NOT_IN_GC);
  return function()
      ->shared()
      ->internal_formal_parameter_count_without_receiver();
}

int JavaScriptFrame::GetActualArgumentCount() const {
  return static_cast<int>(
             Memory<intptr_t>(fp() + StandardFrameConstants::kArgCOffset)) -
         kJSArgcReceiverSlots;
}

Handle<FixedArray> CommonFrameWithJSLinkage::GetParameters() const {
  if (V8_LIKELY(!v8_flags.detailed_error_stack_trace)) {
    return isolate()->factory()->empty_fixed_array();
  }
  int param_count = ComputeParametersCount();
  Handle<FixedArray> parameters =
      isolate()->factory()->NewFixedArray(param_count);
  for (int i = 0; i < param_count; i++) {
    parameters->set(i, GetParameter(i));
  }

  return parameters;
}

Tagged<JSFunction> JavaScriptBuiltinContinuationFrame::function() const {
  const int offset = BuiltinContinuationFrameConstants::kFunctionOffset;
  return Cast<JSFunction>(Tagged<Object>(base::Memory<Address>(fp() + offset)));
}

int JavaScriptBuiltinContinuationFrame::ComputeParametersCount() const {
  // Assert that the first allocatable register is also the argument count
  // register.
  DCHECK_EQ(RegisterConfiguration::Default()->GetAllocatableGeneralCode(0),
            kJavaScriptCallArgCountRegister.code());
  Tagged<Object> argc_object(
      Memory<Address>(fp() + BuiltinContinuationFrameConstants::kArgCOffset));
  return Smi::ToInt(argc_object) - kJSArgcReceiverSlots;
}

intptr_t JavaScriptBuiltinContinuationFrame::GetSPToFPDelta() const {
  Address height_slot =
      fp() + BuiltinContinuationFrameConstants::kFrameSPtoFPDeltaAtDeoptimize;
  intptr_t height = Smi::ToInt(Tagged<Smi>(Memory<Address>(height_slot)));
  return height;
}

Tagged<Object> JavaScriptBuiltinContinuationFrame::context() const {
  return Tagged<Object>(Memory<Address>(
      fp() + BuiltinContinuationFrameConstants::kBuiltinContextOffset));
}

void JavaScriptBuiltinContinuationWithCatchFrame::SetException(
    Tagged<Object> exception) {
  int argc = ComputeParametersCount();
  Address exception_argument_slot =
      fp() + BuiltinContinuationFrameConstants::kFixedFrameSizeAboveFp +
      (argc - 1) * kSystemPointerSize;

  // Only allow setting exception if previous value was the hole.
  CHECK_EQ(ReadOnlyRoots(isolate()).the_hole_value(),
           Tagged<Object>(Memory<Address>(exception_argument_slot)));
  Memory<Address>(exception_argument_slot) = exception.ptr();
}

FrameSummary::JavaScriptFrameSummary::JavaScriptFrameSummary(
    Isolate* isolate, Tagged<Object> receiver, Tagged<JSFunction> function,
    Tagged<AbstractCode> abstract_code, int code_offset, bool is_constructor,
    Tagged<FixedArray> parameters)
    : FrameSummaryBase(isolate, FrameSummary::JAVA_SCRIPT),
      receiver_(receiver, isolate),
      function_(function, isolate),
      abstract_code_(abstract_code, isolate),
      code_offset_(code_offset),
      is_constructor_(is_constructor),
      parameters_(parameters, isolate) {
  DCHECK_IMPLIES(CodeKindIsOptimizedJSFunction(abstract_code->kind(isolate)),
                 // It might be an ApiCallbackBuiltin inlined into optimized
                 // code generated by Maglev.
                 (v8_flags.maglev_inline_api_calls &&
                  abstract_code->kind(isolate) == CodeKind::MAGLEV &&
                  function->shared()->IsApiFunction()));
}

void FrameSummary::EnsureSourcePositionsAvailable() {
  if (IsJavaScript()) {
    java_script_summary_.EnsureSourcePositionsAvailable();
  }
}

bool FrameSummary::AreSourcePositionsAvailable() const {
  if (IsJavaScript()) {
    return java_script_summary_.AreSourcePositionsAvailable();
  }
  return true;
}

void FrameSummary::JavaScriptFrameSummary::EnsureSourcePositionsAvailable() {
  Handle<SharedFunctionInfo> shared(function()->shared(), isolate());
  SharedFunctionInfo::EnsureSourcePositionsAvailable(isolate(), shared);
}

bool FrameSummary::JavaScriptFrameSummary::AreSourcePositionsAvailable() const {
  return !v8_flags.enable_lazy_source_positions ||
         function()
             ->shared()
             ->GetBytecodeArray(isolate())
             ->HasSourcePositionTable();
}

bool FrameSummary::JavaScriptFrameSummary::is_subject_to_debugging() const {
  return function()->shared()->IsSubjectToDebugging();
}

int FrameSummary::JavaScriptFrameSummary::SourcePosition() const {
  return abstract_code()->SourcePosition(isolate(), code_offset());
}

int FrameSummary::JavaScriptFrameSummary::SourceStatementPosition() const {
  return abstract_code()->SourceStatementPosition(isolate(), code_offset());
}

Handle<Object> FrameSummary::JavaScriptFrameSummary::script() const {
  return handle(function_->shared()->script(), isolate());
}

Handle<Context> FrameSummary::JavaScriptFrameSummary::native_context() const {
  return handle(function_->native_context(), isolate());
}

Handle<StackFrameInfo>
FrameSummary::JavaScriptFrameSummary::CreateStackFrameInfo() const {
  Handle<SharedFunctionInfo> shared(function_->shared(), isolate());
  DirectHandle<Script> script(Cast<Script>(shared->script()), isolate());
  DirectHandle<String> function_name = JSFunction::GetDebugName(function_);
  if (function_name->length() == 0 &&
      script->compilation_type() == Script::CompilationType::kEval) {
    function_name = isolate()->factory()->eval_string();
  }
  int bytecode_offset = code_offset();
  if (bytecode_offset == kFunctionEntryBytecodeOffset) {
    // For the special function entry bytecode offset (-1), which signals
    // that the stack trace was captured while the function entry was
    // executing (i.e. during the interrupt check), we cannot store this
    // sentinel in the bit field, so we just eagerly lookup the source
    // position within the script.
    SharedFunctionInfo::EnsureSourcePositionsAvailable(isolate(), shared);
    int source_position =
        abstract_code()->SourcePosition(isolate(), bytecode_offset);
    return isolate()->factory()->NewStackFrameInfo(
        script, source_position, function_name, is_constructor());
  }
  return isolate()->factory()->NewStackFrameInfo(
      shared, bytecode_offset, function_name, is_constructor());
}

#if V8_ENABLE_WEBASSEMBLY
FrameSummary::WasmFrameSummary::WasmFrameSummary(
    Isolate* isolate, Handle<WasmTrustedInstanceData> instance_data,
    wasm::WasmCode* code, int byte_offset, int function_index,
    bool at_to_number_conversion)
    : FrameSummaryBase(isolate, WASM),
      instance_data_(instance_data),
      at_to_number_conversion_(at_to_number_conversion),
      code_(code),
      byte_offset_(byte_offset),
      function_index_(function_index) {}

Handle<Object> FrameSummary::WasmFrameSummary::receiver() const {
  return isolate()->global_proxy();
}

uint32_t FrameSummary::WasmFrameSummary::function_index() const {
  return function_index_;
}

int FrameSummary::WasmFrameSummary::SourcePosition() const {
  const wasm::WasmModule* module = wasm_trusted_instance_data()->module();
  return GetSourcePosition(module, function_index(), code_offset(),
                           at_to_number_conversion());
}

Handle<Script> FrameSummary::WasmFrameSummary::script() const {
  return handle(wasm_instance()->module_object()->script(), isolate());
}

Handle<WasmInstanceObject> FrameSummary::WasmFrameSummary::wasm_instance()
    const {
  // TODO(42204563): Avoid crashing if the instance object is not available.
  CHECK(instance_data_->has_instance_object());
  return handle(instance_data_->instance_object(), isolate());
}

Handle<Context> FrameSummary::WasmFrameSummary::native_context() const {
  return handle(wasm_trusted_instance_data()->native_context(), isolate());
}

Handle<StackFrameInfo> FrameSummary::WasmFrameSummary::CreateStackFrameInfo()
    const {
  DirectHandle<String> function_name =
      GetWasmFunctionDebugName(isolate(), instance_data_, function_index());
  return isolate()->factory()->NewStackFrameInfo(script(), SourcePosition(),
                                                 function_name, false);
}

FrameSummary::WasmInlinedFrameSummary::WasmInlinedFrameSummary(
    Isolate* isolate, Handle<WasmTrustedInstanceData> instance_data,
    int function_index, int op_wire_bytes_offset)
    : FrameSummaryBase(isolate, WASM_INLINED),
      instance_data_(instance_data),
      function_index_(function_index),
      op_wire_bytes_offset_(op_wire_bytes_offset) {}

Handle<WasmInstanceObject>
FrameSummary::WasmInlinedFrameSummary::wasm_instance() const {
  // TODO(42204563): Avoid crashing if the instance object is not available.
  CHECK(instance_data_->has_instance_object());
  return handle(instance_data_->instance_object(), isolate());
}

Handle<Object> FrameSummary::WasmInlinedFrameSummary::receiver() const {
  return isolate()->global_proxy();
}

uint32_t FrameSummary::WasmInlinedFrameSummary::function_index() const {
  return function_index_;
}

int FrameSummary::WasmInlinedFrameSummary::SourcePosition() const {
  const wasm::WasmModule* module = instance_data_->module();
  return GetSourcePosition(module, function_index(), code_offset(), false);
}

Handle<Script> FrameSummary::WasmInlinedFrameSummary::script() const {
  return handle(wasm_instance()->module_object()->script(), isolate());
}

Handle<Context> FrameSummary::WasmInlinedFrameSummary::native_context() const {
  return handle(wasm_trusted_instance_data()->native_context(), isolate());
}

Handle<StackFrameInfo>
FrameSummary::WasmInlinedFrameSummary::CreateStackFrameInfo() const {
  DirectHandle<String> function_name =
      GetWasmFunctionDebugName(isolate(), instance_data_, function_index());
  return isolate()->factory()->NewStackFrameInfo(script(), SourcePosition(),
                                                 function_name, false);
}

#if V8_ENABLE_DRUMBRAKE
FrameSummary::WasmInterpretedFrameSummary::WasmInterpretedFrameSummary(
    Isolate* isolate, Handle<WasmInstanceObject> instance,
    uint32_t function_index, int byte_offset)
    : FrameSummaryBase(isolate, WASM_INTERPRETED),
      wasm_instance_(instance),
      function_index_(function_index),
      byte_offset_(byte_offset) {}

Handle<Object> FrameSummary::WasmInterpretedFrameSummary::receiver() const {
  return wasm_instance_->GetIsolate()->global_proxy();
}

int FrameSummary::WasmInterpretedFrameSummary::SourcePosition() const {
  const wasm::WasmModule* module = wasm_instance()->module_object()->module();
  return GetSourcePosition(module, function_index(), byte_offset(),
                           false /*at_to_number_conversion*/);
}

Handle<WasmTrustedInstanceData>
FrameSummary::WasmInterpretedFrameSummary::instance_data() const {
  return handle(wasm_instance_->trusted_data(isolate()), isolate());
}

Handle<Script> FrameSummary::WasmInterpretedFrameSummary::script() const {
  return handle(wasm_instance()->module_object()->script(),
                wasm_instance()->GetIsolate());
}

Handle<Context> FrameSummary::WasmInterpretedFrameSummary::native_context()
    const {
  return handle(wasm_instance_->trusted_data(isolate())->native_context(),
                isolate());
}

Handle<StackFrameInfo>
FrameSummary::WasmInterpretedFrameSummary::CreateStackFrameInfo() const {
  Handle<String> function_name =
      GetWasmFunctionDebugName(isolate(), instance_data(), function_index());
  return isolate()->factory()->NewStackFrameInfo(script(), SourcePosition(),
                                                 function_name, false);
}
#endif  // V8_ENABLE_DRUMBRAKE

FrameSummary::BuiltinFrameSummary::BuiltinFrameSummary(Isolate* isolate,
                                                       Builtin builtin)
    : FrameSummaryBase(isolate, FrameSummary::BUILTIN), builtin_(builtin) {}

Handle<Object> FrameSummary::BuiltinFrameSummary::receiver() const {
  return isolate()->factory()->undefined_value();
}

Handle<Object> FrameSummary::BuiltinFrameSummary::script() const {
  return isolate()->factory()->undefined_value();
}

Handle<Context> FrameSummary::BuiltinFrameSummary::native_context() const {
  return isolate()->native_context();
}

Handle<StackFrameInfo> FrameSummary::BuiltinFrameSummary::CreateStackFrameInfo()
    const {
  DirectHandle<String> name_str =
      isolate()->factory()->NewStringFromAsciiChecked(
          Builtins::NameForStackTrace(isolate(), builtin_));
  return isolate()->factory()->NewStackFrameInfo(
      Cast<Script>(script()), SourcePosition(), name_str, false);
}

#endif  // V8_ENABLE_WEBASSEMBLY

FrameSummary::~FrameSummary() {
#define FRAME_SUMMARY_DESTR(kind, type, field, desc) \
  case kind:                                         \
    field.~type();                                   \
    break;
  switch (base_.kind()) {
    FRAME_SUMMARY_VARIANTS(FRAME_SUMMARY_DESTR)
    default:
      UNREACHABLE();
  }
#undef FRAME_SUMMARY_DESTR
}

FrameSummary FrameSummary::GetTop(const CommonFrame* frame) {
  std::vector<FrameSummary> frames;
  frame->Summarize(&frames);
  DCHECK_LT(0, frames.size());
  return frames.back();
}

FrameSummary FrameSummary::GetBottom(const CommonFrame* frame) {
  return Get(frame, 0);
}

FrameSummary FrameSummary::GetSingle(const CommonFrame* frame) {
  std::vector<FrameSummary> frames;
  frame->Summarize(&frames);
  DCHECK_EQ(1, frames.size());
  return frames.front();
}

FrameSummary FrameSummary::Get(const CommonFrame* frame, int index) {
  DCHECK_LE(0, index);
  std::vector<FrameSummary> frames;
  frame->Summarize(&frames);
  DCHECK_GT(frames.size(), index);
  return frames[index];
}

#if V8_ENABLE_WEBASSEMBLY
#ifdef V8_ENABLE_DRUMBRAKE
#define CASE_WASM_INTERPRETED(name) \
  case WASM_INTERPRETED:            \
    return wasm_interpreted_summary_.name();
#else  // V8_ENABLE_DRUMBRAKE
#define CASE_WASM_INTERPRETED(name)
#endif  // V8_ENABLE_DRUMBRAKE
#define FRAME_SUMMARY_DISPATCH(ret, name)    \
  ret FrameSummary::name() const {           \
    switch (base_.kind()) {                  \
      case JAVA_SCRIPT:                      \
        return java_script_summary_.name();  \
      case WASM:                             \
        return wasm_summary_.name();         \
      case WASM_INLINED:                     \
        return wasm_inlined_summary_.name(); \
      case BUILTIN:                          \
        return builtin_summary_.name();      \
        CASE_WASM_INTERPRETED(name)          \
      default:                               \
        UNREACHABLE();                       \
    }                                        \
  }
#else
#define FRAME_SUMMARY_DISPATCH(ret, name) \
  ret FrameSummary::name() const {        \
    DCHECK_EQ(JAVA_SCRIPT, base_.kind()); \
    return java_script_summary_.name();   \
  }
#endif  // V8_ENABLE_WEBASSEMBLY

FRAME_SUMMARY_DISPATCH(Handle<Object>, receiver)
FRAME_SUMMARY_DISPATCH(int, code_offset)
FRAME_SUMMARY_DISPATCH(bool, is_constructor)
FRAME_SUMMARY_DISPATCH(bool, is_subject_to_debugging)
FRAME_SUMMARY_DISPATCH(Handle<Object>, script)
FRAME_SUMMARY_DISPATCH(int, SourcePosition)
FRAME_SUMMARY_DISPATCH(int, SourceStatementPosition)
FRAME_SUMMARY_DISPATCH(Handle<Context>, native_context)
FRAME_SUMMARY_DISPATCH(Handle<StackFrameInfo>, CreateStackFrameInfo)

#undef CASE_WASM_INTERPRETED
#undef FRAME_SUMMARY_DISPATCH

void OptimizedFrame::Summarize(std::vector<FrameSummary>* frames) const {
  DCHECK(frames->empty());
  DCHECK(is_optimized());

  // Delegate to JS frame in absence of deoptimization info.
  // TODO(turbofan): Revisit once we support deoptimization across the board.
  DirectHandle<Code> code(LookupCode(), isolate());
  if (code->kind() == CodeKind::BUILTIN) {
    return JavaScriptFrame::Summarize(frames);
  }

  int deopt_index = SafepointEntry::kNoDeoptIndex;
  Tagged<DeoptimizationData> const data =
      GetDeoptimizationData(*code, &deopt_index);
  if (deopt_index == SafepointEntry::kNoDeoptIndex) {
    // Hack: For maglevved function entry, we don't emit lazy deopt information,
    // so create an extra special summary here.
    //
    // TODO(leszeks): Remove this hack, by having a maglev-specific frame
    // summary which is a bit more aware of maglev behaviour and can e.g. handle
    // more compact safepointed frame information for both function entry and
    // loop stack checks.
    if (code->is_maglevved()) {
      DCHECK(frames->empty());
      DirectHandle<AbstractCode> abstract_code(
          Cast<AbstractCode>(function()->shared()->GetBytecodeArray(isolate())),
          isolate());
      DirectHandle<FixedArray> params = GetParameters();
      FrameSummary::JavaScriptFrameSummary summary(
          isolate(), receiver(), function(), *abstract_code,
          kFunctionEntryBytecodeOffset, IsConstructor(), *params);
      frames->push_back(summary);
      return;
    }

    CHECK(data.is_null());
    FATAL("Missing deoptimization information for OptimizedFrame::Summarize.");
  }

  // Prepare iteration over translation. We must not materialize values here
  // because we do not deoptimize the function.
  TranslatedState translated(this);
  translated.Prepare(fp());

  // We create the summary in reverse order because the frames
  // in the deoptimization translation are ordered bottom-to-top.
  bool is_constructor = IsConstructor();
  for (auto it = translated.begin(); it != translated.end(); it++) {
    if (it->kind() == TranslatedFrame::kUnoptimizedFunction ||
        it->kind() == TranslatedFrame::kJavaScriptBuiltinContinuation ||
        it->kind() ==
            TranslatedFrame::kJavaScriptBuiltinContinuationWithCatch) {
      DirectHandle<SharedFunctionInfo> shared_info = it->shared_info();

      // The translation commands are ordered and the function is always
      // at the first position, and the receiver is next.
      TranslatedFrame::iterator translated_values = it->begin();

      // Get the correct function in the optimized frame.
      CHECK(!translated_values->IsMaterializedObject());
      DirectHandle<JSFunction> function =
          Cast<JSFunction>(translated_values->GetValue());
      translated_values++;

      // Get the correct receiver in the optimized frame.
      CHECK(!translated_values->IsMaterializedObject());
      DirectHandle<Object> receiver = translated_values->GetValue();
      translated_values++;

      // Determine the underlying code object and the position within it from
      // the translation corresponding to the frame type in question.
      DirectHandle<AbstractCode> abstract_code;
      unsigned code_offset;
      if (it->kind() == TranslatedFrame::kJavaScriptBuiltinContinuation ||
          it->kind() ==
              TranslatedFrame::kJavaScriptBuiltinContinuationWithCatch) {
        code_offset = 0;
        abstract_code = Cast<AbstractCode>(isolate()->builtins()->code_handle(
            Builtins::GetBuiltinFromBytecodeOffset(it->bytecode_offset())));
      } else {
        DCHECK_EQ(it->kind(), TranslatedFrame::kUnoptimizedFunction);
        code_offset = it->bytecode_offset().ToInt();
        abstract_code =
            direct_handle(shared_info->abstract_code(isolate()), isolate());
      }

      // Append full summary of the encountered JS frame.
      DirectHandle<FixedArray> params = GetParameters();
      FrameSummary::JavaScriptFrameSummary summary(
          isolate(), *receiver, *function, *abstract_code, code_offset,
          is_constructor, *params);
      frames->push_back(summary);
      is_constructor = false;
    } else if (it->kind() == TranslatedFrame::kConstructCreateStub ||
               it->kind() == TranslatedFrame::kConstructInvokeStub) {
      // The next encountered JS frame will be marked as a constructor call.
      DCHECK(!is_constructor);
      is_constructor = true;
#if V8_ENABLE_WEBASSEMBLY
    } else if (it->kind() == TranslatedFrame::kWasmInlinedIntoJS) {
      DirectHandle<SharedFunctionInfo> shared_info = it->shared_info();
      DCHECK_NE(isolate()->heap()->gc_state(), Heap::MARK_COMPACT);

      Tagged<WasmExportedFunctionData> function_data =
          shared_info->wasm_exported_function_data();
      Handle<WasmTrustedInstanceData> instance{function_data->instance_data(),
                                               isolate()};
      int func_index = function_data->function_index();
      FrameSummary::WasmInlinedFrameSummary summary(
          isolate(), instance, func_index, it->bytecode_offset().ToInt());
      frames->push_back(summary);
#endif  // V8_ENABLE_WEBASSEMBLY
    }
  }
}

int OptimizedFrame::LookupExceptionHandlerInTable(
    int* data, HandlerTable::CatchPrediction* prediction) {
  // We cannot perform exception prediction on optimized code. Instead, we need
  // to use FrameSummary to find the corresponding code offset in unoptimized
  // code to perform prediction there.
  DCHECK_NULL(prediction);
  Tagged<Code> code = LookupCode();

  HandlerTable table(code);
  if (table.NumberOfReturnEntries() == 0) return -1;

  int pc_offset = code->GetOffsetFromInstructionStart(isolate(), pc());
  DCHECK_NULL(data);  // Data is not used and will not return a value.

  // When the return pc has been replaced by a trampoline there won't be
  // a handler for this trampoline. Thus we need to use the return pc that
  // _used to be_ on the stack to get the right ExceptionHandler.
  if (CodeKindCanDeoptimize(code->kind())) {
    if (!code->marked_for_deoptimization()) {
      // Lazy deoptimize the function in case the handler table entry flags that
      // it wants to be lazily deoptimized on throw. This allows the optimizing
      // compiler to omit catch blocks that were never reached in practice.
      int optimized_exception_handler = table.LookupReturn(pc_offset);
      if (optimized_exception_handler != HandlerTable::kLazyDeopt) {
        return optimized_exception_handler;
      }
      Deoptimizer::DeoptimizeFunction(function(), code);
    }
    DCHECK(code->marked_for_deoptimization());
    pc_offset = FindReturnPCForTrampoline(code, pc_offset);
  }
  return table.LookupReturn(pc_offset);
}

int MaglevFrame::FindReturnPCForTrampoline(Tagged<Code> code,
                                           int trampoline_pc) const {
  DCHECK_EQ(code->kind(), CodeKind::MAGLEV);
  DCHECK(code->marked_for_deoptimization());
  MaglevSafepointTable safepoints(isolate(), pc(), code);
  return safepoints.find_return_pc(trampoline_pc);
}

int TurbofanFrame::FindReturnPCForTrampoline(Tagged<Code> code,
                                             int trampoline_pc) const {
  DCHECK_EQ(code->kind(), CodeKind::TURBOFAN);
  DCHECK(code->marked_for_deoptimization());
  SafepointTable safepoints(isolate(), pc(), code);
  return safepoints.find_return_pc(trampoline_pc);
}

Tagged<DeoptimizationData> OptimizedFrame::GetDeoptimizationData(
    Tagged<Code> code, int* deopt_index) const {
  DCHECK(is_optimized());

  Address pc = maybe_unauthenticated_pc();

  DCHECK(code->contains(isolate(), pc));
  DCHECK(CodeKindCanDeoptimize(code->kind()));

  if (code->is_maglevved()) {
    MaglevSafepointEntry safepoint_entry =
        code->GetMaglevSafepointEntry(isolate(), pc);
    if (safepoint_entry.has_deoptimization_index()) {
      *deopt_index = safepoint_entry.deoptimization_index();
      return Cast<DeoptimizationData>(code->deoptimization_data());
    }
  } else {
    SafepointEntry safepoint_entry = code->GetSafepointEntry(isolate(), pc);
    if (safepoint_entry.has_deoptimization_index()) {
      *deopt_index = safepoint_entry.deoptimization_index();
      return Cast<DeoptimizationData>(code->deoptimization_data());
    }
  }
  *deopt_index = SafepointEntry::kNoDeoptIndex;
  return DeoptimizationData();
}

void OptimizedFrame::GetFunctions(
    std::vector<Tagged<SharedFunctionInfo>>* functions) const {
  DCHECK(functions->empty());
  DCHECK(is_optimized());

  // Delegate to JS frame in absence of turbofan deoptimization.
  // TODO(turbofan): Revisit once we support deoptimization across the board.
  Tagged<Code> code = LookupCode();
  if (code->kind() == CodeKind::BUILTIN) {
    return JavaScriptFrame::GetFunctions(functions);
  }

  DisallowGarbageCollection no_gc;
  int deopt_index = SafepointEntry::kNoDeoptIndex;
  Tagged<DeoptimizationData> const data =
      GetDeoptimizationData(code, &deopt_index);
  DCHECK(!data.is_null());
  DCHECK_NE(SafepointEntry::kNoDeoptIndex, deopt_index);
  Tagged<DeoptimizationLiteralArray> const literal_array = data->LiteralArray();

  DeoptimizationFrameTranslation::Iterator it(
      data->FrameTranslation(), data->TranslationIndex(deopt_index).value());
  int jsframe_count = it.EnterBeginOpcode().js_frame_count;

  // We insert the frames in reverse order because the frames
  // in the deoptimization translation are ordered bottom-to-top.
  while (jsframe_count != 0) {
    TranslationOpcode opcode = it.SeekNextJSFrame();
    it.NextOperand();  // Skip bailout id.
    jsframe_count--;

    // The second operand of the frame points to the function.
    Tagged<Object> shared = literal_array->get(it.NextOperand());
    functions->push_back(Cast<SharedFunctionInfo>(shared));

    // Skip over remaining operands to advance to the next opcode.
    it.SkipOperands(TranslationOpcodeOperandCount(opcode) - 2);
  }
}

int OptimizedFrame::StackSlotOffsetRelativeToFp(int slot_index) {
  return StandardFrameConstants::kCallerSPOffset -
         ((slot_index + 1) * kSystemPointerSize);
}

int UnoptimizedFrame::position() const {
  Tagged<BytecodeArray> code = GetBytecodeArray();
  int code_offset = GetBytecodeOffset();
  return code->SourcePosition(code_offset);
}

int UnoptimizedFrame::LookupExceptionHandlerInTable(
    int* context_register, HandlerTable::CatchPrediction* prediction) {
  HandlerTable table(GetBytecodeArray());
  int handler_index = table.LookupHandlerIndexForRange(GetBytecodeOffset());
  if (handler_index != HandlerTable::kNoHandlerFound) {
    if (context_register) *context_register = table.GetRangeData(handler_index);
    if (prediction) *prediction = table.GetRangePrediction(handler_index);
    table.MarkHandlerUsed(handler_index);
    return table.GetRangeHandler(handler_index);
  }
  return handler_index;
}

Tagged<BytecodeArray> UnoptimizedFrame::GetBytecodeArray() const {
  const int index = UnoptimizedFrameConstants::kBytecodeArrayExpressionIndex;
  DCHECK_EQ(UnoptimizedFrameConstants::kBytecodeArrayFromFp,
            UnoptimizedFrameConstants::kExpressionsOffset -
                index * kSystemPointerSize);
  return Cast<BytecodeArray>(GetExpression(index));
}

Tagged<Object> UnoptimizedFrame::ReadInterpreterRegister(
    int register_index) const {
  const int index = UnoptimizedFrameConstants::kRegisterFileExpressionIndex;
  DCHECK_EQ(UnoptimizedFrameConstants::kRegisterFileFromFp,
            UnoptimizedFrameConstants::kExpressionsOffset -
                index * kSystemPointerSize);
  return GetExpression(index + register_index);
}

void UnoptimizedFrame::Summarize(std::vector<FrameSummary>* functions) const {
  DCHECK(functions->empty());
  DirectHandle<AbstractCode> abstract_code(
      Cast<AbstractCode>(GetBytecodeArray()), isolate());
  DirectHandle<FixedArray> params = GetParameters();
  FrameSummary::JavaScriptFrameSummary summary(
      isolate(), receiver(), function(), *abstract_code, GetBytecodeOffset(),
      IsConstructor(), *params);
  functions->push_back(summary);
}

int InterpretedFrame::GetBytecodeOffset() const {
  const int index = InterpreterFrameConstants::kBytecodeOffsetExpressionIndex;
  DCHECK_EQ(InterpreterFrameConstants::kBytecodeOffsetFromFp,
            InterpreterFrameConstants::kExpressionsOffset -
                index * kSystemPointerSize);
  int raw_offset = Smi::ToInt(GetExpression(index));
  return raw_offset - BytecodeArray::kHeaderSize + kHeapObjectTag;
}

// static
int InterpretedFrame::GetBytecodeOffset(Address fp) {
  const int offset = InterpreterFrameConstants::kExpressionsOffset;
  const int index = InterpreterFrameConstants::kBytecodeOffsetExpressionIndex;
  DCHECK_EQ(InterpreterFrameConstants::kBytecodeOffsetFromFp,
            InterpreterFrameConstants::kExpressionsOffset -
                index * kSystemPointerSize);
  Address expression_offset = fp + offset - index * kSystemPointerSize;
  int raw_offset =
      Smi::ToInt(Tagged<Object>(Memory<Address>(expression_offset)));
  return raw_offset - BytecodeArray::kHeaderSize + kHeapObjectTag;
}

void InterpretedFrame::PatchBytecodeOffset(int new_offset) {
  const int index = InterpreterFrameConstants::kBytecodeOffsetExpressionIndex;
  DCHECK_EQ(InterpreterFrameConstants::kBytecodeOffsetFromFp,
            InterpreterFrameConstants::kExpressionsOffset -
                index * kSystemPointerSize);
  int raw_offset = BytecodeArray::kHeaderSize - kHeapObjectTag + new_offset;
  SetExpression(index, Smi::FromInt(raw_offset));
}

void InterpretedFrame::PatchBytecodeArray(
    Tagged<BytecodeArray> bytecode_array) {
  const int index = InterpreterFrameConstants::kBytecodeArrayExpressionIndex;
  DCHECK_EQ(InterpreterFrameConstants::kBytecodeArrayFromFp,
            InterpreterFrameConstants::kExpressionsOffset -
                index * kSystemPointerSize);
  SetExpression(index, bytecode_array);
}

int BaselineFrame::GetBytecodeOffset() const {
  Tagged<Code> code = LookupCode();
  return code->GetBytecodeOffsetForBaselinePC(this->pc(), GetBytecodeArray());
}

intptr_t BaselineFrame::GetPCForBytecodeOffset(int bytecode_offset) const {
  Tagged<Code> code = LookupCode();
  return code->GetBaselineStartPCForBytecodeOffset(bytecode_offset,
                                                   GetBytecodeArray());
}

void BaselineFrame::PatchContext(Tagged<Context> value) {
  base::Memory<Address>(fp() + BaselineFrameConstants::kContextOffset) =
      value.ptr();
}

Tagged<JSFunction> BuiltinFrame::function() const {
  const int offset = BuiltinFrameConstants::kFunctionOffset;
  return Cast<JSFunction>(Tagged<Object>(base::Memory<Address>(fp() + offset)));
}

int BuiltinFrame::ComputeParametersCount() const {
  const int offset = BuiltinFrameConstants::kLengthOffset;
  return Smi::ToInt(Tagged<Object>(base::Memory<Address>(fp() + offset))) -
         kJSArgcReceiverSlots;
}

#if V8_ENABLE_WEBASSEMBLY
void WasmFrame::Print(StringStream* accumulator, PrintMode mode,
                      int index) const {
  PrintIndex(accumulator, mode, index);

#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    DCHECK(is_wasm_to_js());
    accumulator->Add("Wasm-to-JS");
    if (mode != OVERVIEW) accumulator->Add("\n");
    return;
  }
#endif  // V8_ENABLE_DRUMBRAKE

  if (function_index() == wasm::kAnonymousFuncIndex) {
    accumulator->Add("Anonymous wasm wrapper [pc: %p]\n",
                     reinterpret_cast<void*>(pc()));
    return;
  }
  wasm::WasmCodeRefScope code_ref_scope;
  accumulator->Add(is_wasm_to_js() ? "Wasm-to-JS [" : "Wasm [");
  accumulator->PrintName(script()->name());
  Address instruction_start = wasm_code()->instruction_start();
  base::Vector<const uint8_t> raw_func_name =
      module_object()->GetRawFunctionName(function_index());
  const int kMaxPrintedFunctionName = 64;
  char func_name[kMaxPrintedFunctionName + 1];
  int func_name_len = std::min(kMaxPrintedFunctionName, raw_func_name.length());
  memcpy(func_name, raw_func_name.begin(), func_name_len);
  func_name[func_name_len] = '\0';
  int pos = position();
  const wasm::WasmModule* module = trusted_instance_data()->module();
  int func_index = function_index();
  int func_code_offset = module->functions[func_index].code.offset();
  accumulator->Add("], function #%u ('%s'), pc=%p (+0x%x), pos=%d (+%d)\n",
                   func_index, func_name, reinterpret_cast<void*>(pc()),
                   static_cast<int>(pc() - instruction_start), pos,
                   pos - func_code_offset);
  if (mode != OVERVIEW) accumulator->Add("\n");
}

wasm::WasmCode* WasmFrame::wasm_code() const {
  return wasm::GetWasmCodeManager()->LookupCode(isolate(),
                                                maybe_unauthenticated_pc());
}

Tagged<WasmInstanceObject> WasmFrame::wasm_instance() const {
  // TODO(42204563): Avoid crashing if the instance object is not available.
  CHECK(trusted_instance_data()->has_instance_object());
  return trusted_instance_data()->instance_object();
}

Tagged<WasmTrustedInstanceData> WasmFrame::trusted_instance_data() const {
  const int offset = WasmFrameConstants::kWasmInstanceOffset;
  Tagged<Object> trusted_data(Memory<Address>(fp() + offset));
  return Cast<WasmTrustedInstanceData>(trusted_data);
}

wasm::NativeModule* WasmFrame::native_module() const {
  return trusted_instance_data()->native_module();
}

Tagged<WasmModuleObject> WasmFrame::module_object() const {
  return trusted_instance_data()->module_object();
}

int WasmFrame::function_index() const { return wasm_code()->index(); }

Tagged<Script> WasmFrame::script() const { return module_object()->script(); }

int WasmFrame::position() const {
  const wasm::WasmModule* module = trusted_instance_data()->module();
  return GetSourcePosition(module, function_index(), generated_code_offset(),
                           at_to_number_conversion());
}

int WasmFrame::generated_code_offset() const {
  wasm::WasmCode* code = wasm_code();
  int offset = static_cast<int>(pc() - code->instruction_start());
  return code->GetSourceOffsetBefore(offset);
}

bool WasmFrame::is_inspectable() const { return wasm_code()->is_inspectable(); }

Tagged<Object> WasmFrame::context() const {
  return trusted_instance_data()->native_context();
}

void WasmFrame::Summarize(std::vector<FrameSummary>* functions) const {
  DCHECK(functions->empty());
  // The {WasmCode*} escapes this scope via the {FrameSummary}, which is fine,
  // since this code object is part of our stack.
  wasm::WasmCode* code = wasm_code();
  int offset =
      static_cast<int>(maybe_unauthenticated_pc() - code->instruction_start());
  Handle<WasmTrustedInstanceData> instance_data{trusted_instance_data(),
                                                isolate()};
  // Push regular non-inlined summary.
  SourcePosition pos = code->GetSourcePositionBefore(offset);
  bool at_conversion = at_to_number_conversion();
  bool child_was_tail_call = false;
  // Add summaries for each inlined function at the current location.
  while (pos.isInlined()) {
    // Use current pc offset as the code offset for inlined functions.
    // This is not fully correct but there isn't a real code offset of a stack
    // frame for an inlined function as the inlined function is not a true
    // function with a defined start and end in the generated code.
    const auto [func_index, was_tail_call, caller_pos] =
        code->GetInliningPosition(pos.InliningId());
    if (!child_was_tail_call) {
      FrameSummary::WasmFrameSummary summary(isolate(), instance_data, code,
                                             pos.ScriptOffset(), func_index,
                                             at_conversion);
      functions->push_back(summary);
    }
    pos = caller_pos;
    at_conversion = false;
    child_was_tail_call = was_tail_call;
  }

  if (!child_was_tail_call) {
    int func_index = code->index();
    FrameSummary::WasmFrameSummary summary(isolate(), instance_data, code,
                                           pos.ScriptOffset(), func_index,
                                           at_conversion);
    functions->push_back(summary);
  }

  // The caller has to be on top.
  std::reverse(functions->begin(), functions->end());
}

bool WasmFrame::at_to_number_conversion() const {
  if (callee_pc() == kNullAddress) return false;
  // Check whether our callee is a WASM_TO_JS frame, and this frame is at the
  // ToNumber conversion call.
  wasm::WasmCode* wasm_code =
      wasm::GetWasmCodeManager()->LookupCode(isolate(), callee_pc());

  if (wasm_code) {
    if (wasm_code->kind() != wasm::WasmCode::kWasmToJsWrapper) return false;
    int offset = static_cast<int>(callee_pc() - wasm_code->instruction_start());
    int pos = wasm_code->GetSourceOffsetBefore(offset);
    // The imported call has position 0, ToNumber has position 1.
    // If there is no source position available, this is also not a ToNumber
    // call.
    DCHECK(pos == wasm::kNoCodePosition || pos == 0 || pos == 1);
    return pos == 1;
  }

  InnerPointerToCodeCache::InnerPointerToCodeCacheEntry* entry =
      isolate()->inner_pointer_to_code_cache()->GetCacheEntry(callee_pc());
  CHECK(entry->code.has_value());
  Tagged<GcSafeCode> code = entry->code.value();
  if (code->builtin_id() != Builtin::kWasmToJsWrapperCSA) {
    return false;
  }

  // The generic wasm-to-js wrapper maintains a slot on the stack to indicate
  // its state. Initially this slot contains the signature, so that incoming
  // parameters can be scanned. After all parameters have been processed, the
  // number of parameters gets stored in the stack slot, so that outgoing
  // parameters can be scanned. After returning from JavaScript, Smi(-1) is
  // stored in the slot to indicate that any call from now on is a ToNumber
  // conversion.
  Tagged<Object> maybe_sig = Tagged<Object>(Memory<Address>(
      callee_fp() + WasmToJSWrapperConstants::kSignatureOffset));

  return IsSmi(maybe_sig) && Smi::ToInt(maybe_sig) < 0;
}

int WasmFrame::LookupExceptionHandlerInTable() {
  wasm::WasmCode* code =
      wasm::GetWasmCodeManager()->LookupCode(isolate(), pc());
  if (!code->IsAnonymous() && code->handler_table_size() > 0) {
    HandlerTable table(code);
    int pc_offset = static_cast<int>(pc() - code->instruction_start());
    return table.LookupReturn(pc_offset);
  }
  return -1;
}

void WasmDebugBreakFrame::Iterate(RootVisitor* v) const {
  DCHECK(caller_pc());
  auto pair = wasm::GetWasmCodeManager()->LookupCodeAndSafepoint(isolate(),
                                                                 caller_pc());
  SafepointEntry safepoint_entry = pair.second;
  uint32_t tagged_register_indexes = safepoint_entry.tagged_register_indexes();

  while (tagged_register_indexes != 0) {
    int reg_code = base::bits::CountTrailingZeros(tagged_register_indexes);
    tagged_register_indexes &= ~(1 << reg_code);
    FullObjectSlot spill_slot(&Memory<Address>(
        fp() +
        WasmDebugBreakFrameConstants::GetPushedGpRegisterOffset(reg_code)));

    v->VisitRootPointer(Root::kStackRoots, nullptr, spill_slot);
  }
}

void WasmDebugBreakFrame::Print(StringStream* accumulator, PrintMode mode,
                                int index) const {
  PrintIndex(accumulator, mode, index);
  accumulator->Add("WasmDebugBreak");
  if (mode != OVERVIEW) accumulator->Add("\n");
}

Tagged<WasmInstanceObject> WasmToJsFrame::wasm_instance() const {
  // WasmToJsFrames hold the {WasmImportData} object in the instance slot.
  // Load the instance from there.
  const int offset = WasmFrameConstants::kWasmInstanceOffset;
  Tagged<Object> func_ref_obj(Memory<Address>(fp() + offset));
  Tagged<WasmImportData> func_ref = Cast<WasmImportData>(func_ref_obj);
  // TODO(42204563): Avoid crashing if the instance object is not available.
  CHECK(func_ref->instance_data()->has_instance_object());
  return func_ref->instance_data()->instance_object();
}

Tagged<WasmTrustedInstanceData> WasmToJsFrame::trusted_instance_data() const {
  return wasm_instance()->trusted_data(isolate());
}

void JsToWasmFrame::Iterate(RootVisitor* v) const {
  // WrapperBuffer slot is RawPtr pointing to a stack.
  // Wasm instance and JS result array are passed as stack params.
  // So there is no need to visit them.

#if V8_ENABLE_DRUMBRAKE
  // Please reference GenericJSToWasmInterpreterWrapper for stack layout.
  if (v8_flags.wasm_jitless) {
    DCHECK(GetContainingCode(isolate(), pc()).value()->builtin_id() ==
           Builtin::kGenericJSToWasmInterpreterWrapper);

    // In a GenericJSToWasmInterpreterWrapper stack layout
    //  ------+-----------------+----------------------
    //        |  return addr    |
    //    fp  |- - - - - - - - -|  -------------------|
    //        |     old fp      |                     |
    //   fp-p |- - - - - - - - -|                     |
    //        |  frame marker   |                     | no GC scan
    //  fp-2p |- - - - - - - - -|                     |
    //        |   scan_count    |                     |
    //  fp-3p |- - - - - - - - -|  -------------------|
    //        |      ....       |                     |
    //        |      ....       | <- spill_slot_limit |
    //        |   spill slots   |                     | GC scan scan_count slots
    //    sp  |      ....       | <- spill_slot_base--|
    //        |                 |                     |
    // The [fp + BuiltinFrameConstants::kGCScanSlotCount] on the stack is a
    // value indicating how many values should be scanned from the top.
    intptr_t scan_count = *reinterpret_cast<intptr_t*>(
        fp() + BuiltinWasmInterpreterWrapperConstants::kGCScanSlotCountOffset);

    FullObjectSlot spill_slot_base(&Memory<Address>(sp()));
    FullObjectSlot spill_slot_limit(
        &Memory<Address>(sp() + scan_count * kSystemPointerSize));
    v->VisitRootPointers(Root::kStackRoots, nullptr, spill_slot_base,
                         spill_slot_limit);

    // We should scan the arg/return values array which may hold heap pointers
    // for reference type of parameter/return values.
    uint32_t signature_data = *reinterpret_cast<uint32_t*>(
        fp() + BuiltinWasmInterpreterWrapperConstants::kSignatureDataOffset);
    bool has_ref_args =
        signature_data & wasm::WasmInterpreterRuntime::HasRefArgsField::kMask;
    bool has_ref_rets =
        signature_data & wasm::WasmInterpreterRuntime::HasRefRetsField::kMask;

    // This value indicates the array is currently used as args array. If false,
    // it's an array for return values.
    bool is_args = *reinterpret_cast<intptr_t*>(
        fp() + BuiltinWasmInterpreterWrapperConstants::kArgRetsIsArgsOffset);
    if ((is_args && !has_ref_args) || (!is_args && !has_ref_rets)) return;

    // Retrieve function signature.
    size_t return_count = *reinterpret_cast<size_t*>(
        fp() + BuiltinWasmInterpreterWrapperConstants::kReturnCountOffset);
    size_t param_count = *reinterpret_cast<size_t*>(
        fp() + BuiltinWasmInterpreterWrapperConstants::kParamCountOffset);
    const wasm::ValueType* reps = *reinterpret_cast<const wasm::ValueType**>(
        fp() +
        BuiltinWasmInterpreterWrapperConstants::kValueTypesArrayStartOffset);
    wasm::FunctionSig sig(return_count, param_count, reps);

    intptr_t slot_ptr = *reinterpret_cast<intptr_t*>(
        fp() + BuiltinWasmInterpreterWrapperConstants::kArgRetsAddressOffset);

    if (is_args) {
      size_t current_index = *reinterpret_cast<size_t*>(
          fp() + BuiltinWasmInterpreterWrapperConstants::kCurrentIndexOffset);
      DCHECK_LE(current_index, param_count);
      for (size_t i = 0; i < current_index; i++) {
        wasm::ValueType type = sig.GetParam(i);
        if (type.is_reference()) {
          // Make sure slot for ref args are 64-bit aligned.
          slot_ptr += (slot_ptr & 0x04);  // Branchless.
          FullObjectSlot array_slot(&Memory<Address>(slot_ptr));
          v->VisitRootPointer(Root::kStackRoots, nullptr, array_slot);
          slot_ptr += kSystemPointerSize;
        } else {
          switch (type.kind()) {
            case wasm::kI32:
            case wasm::kF32:
              slot_ptr += sizeof(int32_t);
              break;
            case wasm::kI64:
            case wasm::kF64:
              slot_ptr += sizeof(int64_t);
              break;
            case wasm::kS128:
            default:
              UNREACHABLE();
          }
        }
      }
    } else {
      // When converting return values, all results are already in the array.
      for (size_t i = 0; i < return_count; i++) {
        wasm::ValueType type = sig.GetReturn(i);
        if (type.is_reference()) {
          // Make sure slot for ref args are 64-bit aligned.
          slot_ptr += (slot_ptr & 0x04);  // Branchless.
          FullObjectSlot array_slot(&Memory<Address>(slot_ptr));
          v->VisitRootPointer(Root::kStackRoots, nullptr, array_slot);
          slot_ptr += kSystemPointerSize;
        } else {
          switch (type.kind()) {
            case wasm::kI32:
            case wasm::kF32:
              slot_ptr += sizeof(int32_t);
              break;
            case wasm::kI64:
            case wasm::kF64:
              slot_ptr += sizeof(int64_t);
              break;
            case wasm::kS128:
            default:
              UNREACHABLE();
          }
        }
      }
    }
  }
#endif  // V8_ENABLE_DRUMBRAKE
}

#if V8_ENABLE_DRUMBRAKE
void WasmToJsFrame::Iterate(RootVisitor* v) const {
  if (v8_flags.wasm_jitless) {
    // Called from GenericWasmToJSInterpreterWrapper.
    CHECK(v8_flags.jitless);
    // The [fp + BuiltinFrameConstants::kGCScanSlotCount] on the stack is a
    // value indicating how many values should be scanned from the top.
    intptr_t scan_count = *reinterpret_cast<intptr_t*>(
        fp() + WasmToJSInterpreterFrameConstants::kGCScanSlotCountOffset);

    Address original_sp = *reinterpret_cast<Address*>(
        fp() + WasmToJSInterpreterFrameConstants::kGCSPOffset);

    // The original sp is not assigned yet if GC is triggered in the middle of
    // param conversion loop. In this case, we just need to scan arguments from
    // the current sp.
    if (original_sp == 0) original_sp = sp();

    if (sp() != original_sp) {
      // The actual frame sp can be different from the sp we had at the moment
      // of the call to Call_ReceiverIsAny for two reasons:
      // 1. Call_ReceiverIsAny might call AdaptorWithBuiltinExitFrame, which
      // adds BuiltinExitFrameConstants::kNumExtraArgsWithoutReceiver additional
      // tagged arguments to the stack.
      // 2. If there is arity mismatch and the imported Wasm function declares
      // fewer arguments then the arguments expected by the JS function,
      // Call_ReceiverIsAny passes additional Undefined args.
      FullObjectSlot additional_spill_slot_base(&Memory<Address>(sp()));
      FullObjectSlot additional_spill_slot_limit(original_sp);
      v->VisitRootPointers(Root::kStackRoots, nullptr,
                           additional_spill_slot_base,
                           additional_spill_slot_limit);
    }
    FullObjectSlot spill_slot_base(&Memory<Address>(original_sp));
    FullObjectSlot spill_slot_limit(
        &Memory<Address>(original_sp + scan_count * kSystemPointerSize));
    v->VisitRootPointers(Root::kStackRoots, nullptr, spill_slot_base,
                         spill_slot_limit);
    return;
  }
  WasmFrame::Iterate(v);
}
#endif  // V8_ENABLE_DRUMBRAKE

void StackSwitchFrame::Iterate(RootVisitor* v) const {
  //  See JsToWasmFrame layout.
  //  We cannot DCHECK that the pc matches the expected builtin code here,
  //  because the return address is on a different stack.
  // The [fp + BuiltinFrameConstants::kGCScanSlotCountOffset] on the stack is a
  // value indicating how many values should be scanned from the top.
  intptr_t scan_count = Memory<intptr_t>(
      fp() + StackSwitchFrameConstants::kGCScanSlotCountOffset);

  FullObjectSlot spill_slot_base(&Memory<Address>(sp()));
  FullObjectSlot spill_slot_limit(
      &Memory<Address>(sp() + scan_count * kSystemPointerSize));
  v->VisitRootPointers(Root::kStackRoots, nullptr, spill_slot_base,
                       spill_slot_limit);
  // Also visit fixed spill slots that contain references.
  FullObjectSlot instance_slot(
      &Memory<Address>(fp() + StackSwitchFrameConstants::kRefOffset));
  v->VisitRootPointer(Root::kStackRoots, nullptr, instance_slot);
  FullObjectSlot result_array_slot(
      &Memory<Address>(fp() + StackSwitchFrameConstants::kResultArrayOffset));
  v->VisitRootPointer(Root::kStackRoots, nullptr, result_array_slot);
}

#if V8_ENABLE_DRUMBRAKE
void WasmInterpreterEntryFrame::Iterate(RootVisitor* v) const {
  //  WasmInterpreterEntryFrame stack layout
  //  ------+-----------------+----------------------
  //        |  return addr    |                     |
  //    fp  |- - - - - - - - -|  -------------------|
  //        |    prev fp      |                     |
  //   fp-p |- - - - - - - - -|                     | no GC scan
  //        |  frame marker   |                     |
  //  fp-2p |- - - - - - - - -|  -------------------|-------------
  //        | WasmInstanceObj |                     | GC scan
  //  fp-3p |- - - - - - - - -|  -------------------|-------------
  //        | function_index  |                     |
  //  fp-4p |- - - - - - - - -|  -------------------| no GC scan
  //        |   array_start   |                     |
  //  fp-5p |- - - - - - - - -|  -------------------|

  static constexpr int kWasmInstanceObjOffset = -2 * kSystemPointerSize;
  FullObjectSlot slot_base(&Memory<Address>(fp() + kWasmInstanceObjOffset));
  FullObjectSlot slot_limit(
      &Memory<Address>(fp() + kWasmInstanceObjOffset + kSystemPointerSize));
  v->VisitRootPointers(Root::kStackRoots, nullptr, slot_base, slot_limit);
}

void WasmInterpreterEntryFrame::Print(StringStream* accumulator, PrintMode mode,
                                      int index) const {
  PrintIndex(accumulator, mode, index);
  accumulator->Add("WASM INTERPRETER ENTRY [");
  Tagged<Script> script = this->script();
  accumulator->PrintName(script->name());
  accumulator->Add("]");
  if (mode != OVERVIEW) accumulator->Add("\n");
}

void WasmInterpreterEntryFrame::Summarize(
    std::vector<FrameSummary>* functions) const {
  Handle<WasmInstanceObject> instance(wasm_instance(), isolate());
  std::vector<WasmInterpreterStackEntry> interpreted_stack =
      WasmInterpreterObject::GetInterpretedStack(
          trusted_instance_data()->interpreter_object(), fp());

  for (auto& e : interpreted_stack) {
    FrameSummary::WasmInterpretedFrameSummary summary(
        isolate(), instance, e.function_index, e.byte_offset);
    functions->push_back(summary);
  }
}

Tagged<HeapObject> WasmInterpreterEntryFrame::unchecked_code() const {
  return InstructionStream();
}

Tagged<WasmInstanceObject> WasmInterpreterEntryFrame::wasm_instance() const {
  const int offset = WasmInterpreterFrameConstants::kWasmInstanceOffset;
  Tagged<Object> instance(Memory<Address>(fp() + offset));
  return Cast<WasmInstanceObject>(instance);
}

Tagged<WasmTrustedInstanceData>
WasmInterpreterEntryFrame::trusted_instance_data() const {
  return wasm_instance()->trusted_data(isolate());
}

Tagged<Tuple2> WasmInterpreterEntryFrame::interpreter_object() const {
  return trusted_instance_data()->interpreter_object();
}

Tagged<WasmModuleObject> WasmInterpreterEntryFrame::module_object() const {
  return trusted_instance_data()->module_object();
}

int WasmInterpreterEntryFrame::function_index(
    int inlined_function_index) const {
  return WasmInterpreterObject::GetFunctionIndex(
      trusted_instance_data()->interpreter_object(), fp(),
      inlined_function_index);
}

int WasmInterpreterEntryFrame::position() const {
  return FrameSummary::GetBottom(this).AsWasmInterpreted().SourcePosition();
}

Tagged<Object> WasmInterpreterEntryFrame::context() const {
  return trusted_instance_data()->native_context();
}

Address WasmInterpreterEntryFrame::GetCallerStackPointer() const {
  return fp() + CommonFrameConstants::kCallerSPOffset;
}
#endif  // V8_ENABLE_DRUMBRAKE

// static
void StackSwitchFrame::GetStateForJumpBuffer(wasm::JumpBuffer* jmpbuf,
                                             State* state) {
  DCHECK_NE(jmpbuf->fp, kNullAddress);
  DCHECK_EQ(ComputeFrameType(jmpbuf->fp), STACK_SWITCH);
  FillState(jmpbuf->fp, jmpbuf->sp, state);
  state->pc_address = &jmpbuf->pc;
  DCHECK_NE(*state->pc_address, kNullAddress);
}

int WasmLiftoffSetupFrame::GetDeclaredFunctionIndex() const {
  Tagged<Object> func_index(Memory<Address>(
      sp() + WasmLiftoffSetupFrameConstants::kDeclaredFunctionIndexOffset));
  return Smi::ToInt(func_index);
}

wasm::NativeModule* WasmLiftoffSetupFrame::GetNativeModule() const {
  return Memory<wasm::NativeModule*>(
      sp() + WasmLiftoffSetupFrameConstants::kNativeModuleOffset);
}

FullObjectSlot WasmLiftoffSetupFrame::wasm_instance_slot() const {
  return FullObjectSlot(&Memory<Address>(
      sp() + WasmLiftoffSetupFrameConstants::kWasmInstanceOffset));
}

void WasmLiftoffSetupFrame::Iterate(RootVisitor* v) const {
  FullObjectSlot spilled_instance_slot(&Memory<Address>(
      fp() + WasmLiftoffSetupFrameConstants::kInstanceSpillOffset));
  v->VisitRootPointer(Root::kStackRoots, "spilled wasm instance",
                      spilled_instance_slot);
  v->VisitRootPointer(Root::kStackRoots, "wasm instance parameter",
                      wasm_instance_slot());

  wasm::NativeModule* native_module = GetNativeModule();
  int func_index = GetDeclaredFunctionIndex() +
                   native_module->module()->num_imported_functions;

  // Scan the spill slots of the parameter registers. Parameters in WebAssembly
  // get reordered such that first all value parameters get put into registers.
  // If there are more registers than value parameters, the remaining registers
  // are used for reference parameters. Therefore we can determine which
  // registers get used for which parameters by counting the number of value
  // parameters and the number of reference parameters.
  int num_int_params = 0;
  int num_ref_params = 0;
  const wasm::FunctionSig* sig =
      native_module->module()->functions[func_index].sig;
  for (auto param : sig->parameters()) {
    if (param == wasm::kWasmI32) {
      num_int_params++;
    } else if (param == wasm::kWasmI64) {
      num_int_params += kSystemPointerSize == 8 ? 1 : 2;
    } else if (param.is_reference()) {
      num_ref_params++;
    }
  }

  // There are no reference parameters, there is nothing to scan.
  if (num_ref_params == 0) return;

  int num_int_params_in_registers =
      std::min(num_int_params,
               WasmLiftoffSetupFrameConstants::kNumberOfSavedGpParamRegs);
  int num_ref_params_in_registers =
      std::min(num_ref_params,
               WasmLiftoffSetupFrameConstants::kNumberOfSavedGpParamRegs -
                   num_int_params_in_registers);

  for (int i = 0; i < num_ref_params_in_registers; ++i) {
    FullObjectSlot spill_slot(
        fp() + WasmLiftoffSetupFrameConstants::kParameterSpillsOffset
                   [num_int_params_in_registers + i]);

    v->VisitRootPointer(Root::kStackRoots, "register parameter", spill_slot);
  }

  // Next we scan the slots of stack parameters.
  wasm::WasmCode* wasm_code = native_module->GetCode(func_index);
  uint32_t first_tagged_stack_slot = wasm_code->first_tagged_parameter_slot();
  uint32_t num_tagged_stack_slots = wasm_code->num_tagged_parameter_slots();

  // Visit tagged parameters that have been passed to the function of this
  // frame. Conceptionally these parameters belong to the parent frame.
  // However, the exact count is only known by this frame (in the presence of
  // tail calls, this information cannot be derived from the call site).
  if (num_tagged_stack_slots > 0) {
    FullObjectSlot tagged_parameter_base(&Memory<Address>(caller_sp()));
    tagged_parameter_base += first_tagged_stack_slot;
    FullObjectSlot tagged_parameter_limit =
        tagged_parameter_base + num_tagged_stack_slots;

    v->VisitRootPointers(Root::kStackRoots, "stack parameter",
                         tagged_parameter_base, tagged_parameter_limit);
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

namespace {

void PrintFunctionSource(StringStream* accumulator,
                         Tagged<SharedFunctionInfo> shared) {
  if (v8_flags.max_stack_trace_source_length != 0) {
    std::ostringstream os;
    os << "--------- s o u r c e   c o d e ---------\n"
       << SourceCodeOf(shared, v8_flags.max_stack_trace_source_length)
       << "\n-----------------------------------------\n";
    accumulator->Add(os.str().c_str());
  }
}

}  // namespace

void JavaScriptFrame::Print(StringStream* accumulator, PrintMode mode,
                            int index) const {
  Handle<SharedFunctionInfo> shared = handle(function()->shared(), isolate());
  SharedFunctionInfo::EnsureSourcePositionsAvailable(isolate(), shared);

  DisallowGarbageCollection no_gc;
  Tagged<Object> receiver = this->receiver();
  Tagged<JSFunction> function = this->function();

  accumulator->PrintSecurityTokenIfChanged(function);
  PrintIndex(accumulator, mode, index);
  PrintFrameKind(accumulator);
  if (IsConstructor()) accumulator->Add("new ");
  accumulator->PrintFunction(function, receiver);
  accumulator->Add(" [%p]", function);

  // Get scope information for nicer output, if possible. If code is nullptr, or
  // doesn't contain scope info, scope_info will return 0 for the number of
  // parameters, stack local variables, context local variables, stack slots,
  // or context slots.
  Tagged<ScopeInfo> scope_info = shared->scope_info();
  Tagged<Object> script_obj = shared->script();
  if (IsScript(script_obj)) {
    Tagged<Script> script = Cast<Script>(script_obj);
    accumulator->Add(" [");
    accumulator->PrintName(script->name());

    if (is_interpreted()) {
      const InterpretedFrame* iframe = InterpretedFrame::cast(this);
      Tagged<BytecodeArray> bytecodes = iframe->GetBytecodeArray();
      int offset = iframe->GetBytecodeOffset();
      int source_pos = bytecodes->SourcePosition(offset);
      int line = script->GetLineNumber(source_pos) + 1;
      accumulator->Add(":%d] [bytecode=%p offset=%d]", line,
                       reinterpret_cast<void*>(bytecodes.ptr()), offset);
    } else {
      int function_start_pos = shared->StartPosition();
      int line = script->GetLineNumber(function_start_pos) + 1;
      accumulator->Add(":~%d] [pc=%p]", line, reinterpret_cast<void*>(pc()));
    }
  }

  accumulator->Add("(this=%o", receiver);

  // Print the parameters.
  int parameters_count = ComputeParametersCount();
  for (int i = 0; i < parameters_count; i++) {
    accumulator->Add(",");
    accumulator->Add("%o", GetParameter(i));
  }

  accumulator->Add(")");
  if (mode == OVERVIEW) {
    accumulator->Add("\n");
    return;
  }
  if (is_optimized()) {
    accumulator->Add(" {\n// optimized frame\n");
    PrintFunctionSource(accumulator, *shared);
    accumulator->Add("}\n");
    return;
  }
  accumulator->Add(" {\n");

  // Compute the number of locals and expression stack elements.
  int heap_locals_count = scope_info->ContextLocalCount();
  int expressions_count = ComputeExpressionsCount();

  // Try to get hold of the context of this frame.
  Tagged<Context> context;
  if (IsContext(this->context())) {
    context = Cast<Context>(this->context());
    while (context->IsWithContext()) {
      context = context->previous();
      DCHECK(!context.is_null());
    }
  }

  // Print heap-allocated local variables.
  if (heap_locals_count > 0) {
    accumulator->Add("  // heap-allocated locals\n");
  }
  for (auto it : ScopeInfo::IterateLocalNames(scope_info, no_gc)) {
    accumulator->Add("  var ");
    accumulator->PrintName(it->name());
    accumulator->Add(" = ");
    if (!context.is_null()) {
      int slot_index = Context::MIN_CONTEXT_SLOTS + it->index();
      if (slot_index < context->length()) {
        accumulator->Add("%o", context->get(slot_index));
      } else {
        accumulator->Add(
            "// warning: missing context slot - inconsistent frame?");
      }
    } else {
      accumulator->Add("// warning: no context found - inconsistent frame?");
    }
    accumulator->Add("\n");
  }

  // Print the expression stack.
  if (0 < expressions_count) {
    accumulator->Add("  // expression stack (top to bottom)\n");
  }
  for (int i = expressions_count - 1; i >= 0; i--) {
    accumulator->Add("  [%02d] : %o\n", i, GetExpression(i));
  }

  PrintFunctionSource(accumulator, *shared);

  accumulator->Add("}\n\n");
}

void EntryFrame::Iterate(RootVisitor* v) const {
  IteratePc(v, constant_pool_address(), GcSafeLookupCode());
}

void CommonFrame::IterateExpressions(RootVisitor* v) const {
  const int last_object_offset = StandardFrameConstants::kLastObjectOffset;
  intptr_t marker =
      Memory<intptr_t>(fp() + CommonFrameConstants::kContextOrFrameTypeOffset);
  FullObjectSlot base(&Memory<Address>(sp()));
  FullObjectSlot limit(&Memory<Address>(fp() + last_object_offset) + 1);
  CHECK(StackFrame::IsTypeMarker(marker));
  v->VisitRootPointers(Root::kStackRoots, nullptr, base, limit);
  IteratePc(v, constant_pool_address(), GcSafeLookupCode());
}

void JavaScriptFrame::Iterate(RootVisitor* v) const {
  // The frame contains the actual argument count (intptr) that should not be
  // visited.
  FullObjectSlot argc(
      &Memory<Address>(fp() + StandardFrameConstants::kArgCOffset));
  const int last_object_offset = StandardFrameConstants::kLastObjectOffset;
  FullObjectSlot base(&Memory<Address>(sp()));
  FullObjectSlot limit(&Memory<Address>(fp() + last_object_offset) + 1);
  v->VisitRootPointers(Root::kStackRoots, nullptr, base, argc);
  v->VisitRootPointers(Root::kStackRoots, nullptr, argc + 1, limit);
  IteratePc(v, constant_pool_address(), GcSafeLookupCode());
}

void InternalFrame::Iterate(RootVisitor* v) const {
  Tagged<GcSafeCode> code = GcSafeLookupCode();
  IteratePc(v, constant_pool_address(), code);
  // Internal frames typically do not receive any arguments, hence their stack
  // only contains tagged pointers.
  // We are misusing the has_tagged_outgoing_params flag here to tell us whether
  // the full stack frame contains only tagged pointers or only raw values.
  // This is used for the WasmCompileLazy builtin, where we actually pass
  // untagged arguments and also store untagged values on the stack.
  if (code->has_tagged_outgoing_params()) IterateExpressions(v);
}

// -------------------------------------------------------------------------

namespace {

// Predictably converts PC to uint32 by calculating offset of the PC in
// from the embedded builtins start or from respective MemoryChunk.
uint32_t PcAddressForHashing(Isolate* isolate, Address address) {
  uint32_t hashable_address;
  if (OffHeapInstructionStream::TryGetAddressForHashing(isolate, address,
                                                        &hashable_address)) {
    return hashable_address;
  }
  return ObjectAddressForHashing(address);
}

}  // namespace

InnerPointerToCodeCache::InnerPointerToCodeCacheEntry*
InnerPointerToCodeCache::GetCacheEntry(Address inner_pointer) {
  DCHECK(base::bits::IsPowerOfTwo(kInnerPointerToCodeCacheSize));
  uint32_t hash =
      ComputeUnseededHash(PcAddressForHashing(isolate_, inner_pointer));
  uint32_t index = hash & (kInnerPointerToCodeCacheSize - 1);
  InnerPointerToCodeCacheEntry* entry = cache(index);
  if (entry->inner_pointer == inner_pointer) {
    // Why this DCHECK holds is nontrivial:
    //
    // - the cache is filled lazily on calls to this function.
    // - this function may be called while GC, and in particular
    //   MarkCompactCollector::UpdatePointersAfterEvacuation, is in progress.
    // - the cache is cleared at the end of UpdatePointersAfterEvacuation.
    // - now, why does pointer equality hold even during moving GC?
    // - .. because GcSafeFindCodeForInnerPointer does not follow forwarding
    //   pointers and always returns the old object (which is still valid,
    //   *except* for the map_word).
    DCHECK_EQ(entry->code,
              isolate_->heap()->GcSafeFindCodeForInnerPointer(inner_pointer));
  } else {
    // Because this code may be interrupted by a profiling signal that
    // also queries the cache, we cannot update inner_pointer before the code
    // has been set. Otherwise, we risk trying to use a cache entry before
    // the code has been computed.
    entry->code =
        isolate_->heap()->GcSafeFindCodeForInnerPointer(inner_pointer);
    if (entry->code.value()->is_maglevved()) {
      entry->maglev_safepoint_entry.Reset();
    } else {
      entry->safepoint_entry.Reset();
    }
    entry->inner_pointer = inner_pointer;
  }
  return entry;
}

// Frame layout helper class implementation.
// -------------------------------------------------------------------------

namespace {

// Some architectures need to push padding together with the TOS register
// in order to maintain stack alignment.
constexpr int TopOfStackRegisterPaddingSlots() {
  return ArgumentPaddingSlots(1);
}

bool BuiltinContinuationModeIsWithCatch(BuiltinContinuationMode mode) {
  switch (mode) {
    case BuiltinContinuationMode::STUB:
    case BuiltinContinuationMode::JAVASCRIPT:
      return false;
    case BuiltinContinuationMode::JAVASCRIPT_WITH_CATCH:
    case BuiltinContinuationMode::JAVASCRIPT_HANDLE_EXCEPTION:
      return true;
  }
  UNREACHABLE();
}

}  // namespace

UnoptimizedFrameInfo::UnoptimizedFrameInfo(int parameters_count_with_receiver,
                                           int translation_height,
                                           bool is_topmost, bool pad_arguments,
                                           FrameInfoKind frame_info_kind) {
  const int locals_count = translation_height;

  register_stack_slot_count_ =
      UnoptimizedFrameConstants::RegisterStackSlotCount(locals_count);

  static constexpr int kTheAccumulator = 1;
  static constexpr int kTopOfStackPadding = TopOfStackRegisterPaddingSlots();
  int maybe_additional_slots =
      (is_topmost || frame_info_kind == FrameInfoKind::kConservative)
          ? (kTheAccumulator + kTopOfStackPadding)
          : 0;
  frame_size_in_bytes_without_fixed_ =
      (register_stack_slot_count_ + maybe_additional_slots) *
      kSystemPointerSize;

  // The 'fixed' part of the frame consists of the incoming parameters and
  // the part described by InterpreterFrameConstants. This will include
  // argument padding, when needed.
  const int parameter_padding_slots =
      pad_arguments ? ArgumentPaddingSlots(parameters_count_with_receiver) : 0;
  const int fixed_frame_size =
      InterpreterFrameConstants::kFixedFrameSize +
      (parameters_count_with_receiver + parameter_padding_slots) *
          kSystemPointerSize;
  frame_size_in_bytes_ = frame_size_in_bytes_without_fixed_ + fixed_frame_size;
}

// static
uint32_t UnoptimizedFrameInfo::GetStackSizeForAdditionalArguments(
    int parameters_count) {
  return (parameters_count + ArgumentPaddingSlots(parameters_count)) *
         kSystemPointerSize;
}

ConstructStubFrameInfo::ConstructStubFrameInfo(int translation_height,
                                               bool is_topmost,
                                               FrameInfoKind frame_info_kind) {
  // Note: This is according to the Translation's notion of 'parameters' which
  // differs to that of the SharedFunctionInfo, e.g. by including the receiver.
  const int parameters_count = translation_height;

  // If the construct frame appears to be topmost we should ensure that the
  // value of result register is preserved during continuation execution.
  // We do this here by "pushing" the result of the constructor function to
  // the top of the reconstructed stack and popping it in
  // {Builtin::kNotifyDeoptimized}.

  static constexpr int kTopOfStackPadding = TopOfStackRegisterPaddingSlots();
  static constexpr int kTheResult = 1;
  const int argument_padding = ArgumentPaddingSlots(parameters_count);

  const int adjusted_height =
      (is_topmost || frame_info_kind == FrameInfoKind::kConservative)
          ? parameters_count + argument_padding + kTheResult +
                kTopOfStackPadding
          : parameters_count + argument_padding;
  frame_size_in_bytes_without_fixed_ = adjusted_height * kSystemPointerSize;
  frame_size_in_bytes_ = frame_size_in_bytes_without_fixed_ +
                         ConstructFrameConstants::kFixedFrameSize;
}

FastConstructStubFrameInfo::FastConstructStubFrameInfo(bool is_topmost) {
  // If the construct frame appears to be topmost we should ensure that the
  // value of result register is preserved during continuation execution.
  // We do this here by "pushing" the result of the constructor function to
  // the top of the reconstructed stack and popping it in
  // {Builtin::kNotifyDeoptimized}.

  static constexpr int kTopOfStackPadding = TopOfStackRegisterPaddingSlots();
  static constexpr int kTheResult = 1;
  const int adjusted_height =
      ArgumentPaddingSlots(1) +
      (is_topmost ? kTheResult + kTopOfStackPadding : 0);
  frame_size_in_bytes_without_fixed_ = adjusted_height * kSystemPointerSize;
  frame_size_in_bytes_ = frame_size_in_bytes_without_fixed_ +
                         FastConstructFrameConstants::kFixedFrameSize;
}

BuiltinContinuationFrameInfo::BuiltinContinuationFrameInfo(
    int translation_height,
    const CallInterfaceDescriptor& continuation_descriptor,
    const RegisterConfiguration* register_config, bool is_topmost,
    DeoptimizeKind deopt_kind, BuiltinContinuationMode continuation_mode,
    FrameInfoKind frame_info_kind) {
  const bool is_conservative = frame_info_kind == FrameInfoKind::kConservative;

  // Note: This is according to the Translation's notion of 'parameters' which
  // differs to that of the SharedFunctionInfo, e.g. by including the receiver.
  const int parameters_count = translation_height;
  frame_has_result_stack_slot_ =
      !is_topmost || deopt_kind == DeoptimizeKind::kLazy;
  const int result_slot_count =
      (frame_has_result_stack_slot_ || is_conservative) ? 1 : 0;

  const int exception_slot_count =
      (BuiltinContinuationModeIsWithCatch(continuation_mode) || is_conservative)
          ? 1
          : 0;

  const int allocatable_register_count =
      register_config->num_allocatable_general_registers();
  const int padding_slot_count =
      BuiltinContinuationFrameConstants::PaddingSlotCount(
          allocatable_register_count);

  const int register_parameter_count =
      continuation_descriptor.GetRegisterParameterCount();
  translated_stack_parameter_count_ =
      parameters_count - register_parameter_count;
  stack_parameter_count_ = translated_stack_parameter_count_ +
                           result_slot_count + exception_slot_count;
  const int stack_param_pad_count =
      ArgumentPaddingSlots(stack_parameter_count_);

  // If the builtins frame appears to be topmost we should ensure that the
  // value of result register is preserved during continuation execution.
  // We do this here by "pushing" the result of callback function to the
  // top of the reconstructed stack and popping it in
  // {Builtin::kNotifyDeoptimized}.
  static constexpr int kTopOfStackPadding = TopOfStackRegisterPaddingSlots();
  static constexpr int kTheResult = 1;
  const int push_result_count =
      (is_topmost || is_conservative) ? kTheResult + kTopOfStackPadding : 0;

  frame_size_in_bytes_ =
      kSystemPointerSize * (stack_parameter_count_ + stack_param_pad_count +
                            allocatable_register_count + padding_slot_count +
                            push_result_count) +
      BuiltinContinuationFrameConstants::kFixedFrameSize;

  frame_size_in_bytes_above_fp_ =
      kSystemPointerSize * (allocatable_register_count + padding_slot_count +
                            push_result_count) +
      (BuiltinContinuationFrameConstants::kFixedFrameSize -
       BuiltinContinuationFrameConstants::kFixedFrameSizeAboveFp);
}

}  // namespace internal
}  // namespace v8
