// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_FRAMES_INL_H_
#define V8_EXECUTION_FRAMES_INL_H_

#include <optional>

#include "src/base/memory.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames.h"
#include "src/execution/isolate.h"
#include "src/execution/pointer-authentication.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

class InnerPointerToCodeCache final {
 public:
  struct InnerPointerToCodeCacheEntry {
    Address inner_pointer;
    std::optional<Tagged<GcSafeCode>> code;
    union {
      SafepointEntry safepoint_entry;
      MaglevSafepointEntry maglev_safepoint_entry;
    };
    InnerPointerToCodeCacheEntry() : safepoint_entry() {}
  };

  explicit InnerPointerToCodeCache(Isolate* isolate) : isolate_(isolate) {
    Flush();
  }

  InnerPointerToCodeCache(const InnerPointerToCodeCache&) = delete;
  InnerPointerToCodeCache& operator=(const InnerPointerToCodeCache&) = delete;

  void Flush() { memset(static_cast<void*>(&cache_[0]), 0, sizeof(cache_)); }

  InnerPointerToCodeCacheEntry* GetCacheEntry(Address inner_pointer);

 private:
  InnerPointerToCodeCacheEntry* cache(int index) { return &cache_[index]; }

  Isolate* const isolate_;

  static const int kInnerPointerToCodeCacheSize = 1024;
  InnerPointerToCodeCacheEntry cache_[kInnerPointerToCodeCacheSize];
};

inline Address StackHandler::address() const {
  return reinterpret_cast<Address>(const_cast<StackHandler*>(this));
}

inline StackHandler* StackHandler::next() const {
  const int offset = StackHandlerConstants::kNextOffset;
  return FromAddress(base::Memory<Address>(address() + offset));
}

inline Address StackHandler::next_address() const {
  return base::Memory<Address>(address() + StackHandlerConstants::kNextOffset);
}

inline StackHandler* StackHandler::FromAddress(Address address) {
  return reinterpret_cast<StackHandler*>(address);
}

inline StackFrame::StackFrame(StackFrameIteratorBase* iterator)
    : iterator_(iterator), isolate_(iterator_->isolate()) {}

inline StackHandler* StackFrame::top_handler() const {
  return iterator_->handler();
}

inline Address StackFrame::pc() const { return ReadPC(pc_address()); }

inline Address StackFrame::unauthenticated_pc() const {
  return unauthenticated_pc(pc_address());
}

// static
inline Address StackFrame::unauthenticated_pc(Address* pc_address) {
  return PointerAuthentication::StripPAC(*pc_address);
}

inline Address StackFrame::maybe_unauthenticated_pc() const {
  if (!InFastCCall() && !is_profiler_entry_frame()) {
    // Here the pc_address() is on the stack and properly authenticated.
    return pc();
  } else {
    // For fast C calls pc_address() points into IsolateData and the pc in there
    // is unauthenticated. For the profiler, the pc_address of the first visited
    // frame is also not written by a call instruction.
    return unauthenticated_pc(pc_address());
  }
}

inline Address StackFrame::ReadPC(Address* pc_address) {
  return PointerAuthentication::AuthenticatePC(pc_address, kSystemPointerSize);
}

inline Address* StackFrame::ResolveReturnAddressLocation(Address* pc_address) {
  if (return_address_location_resolver_ == nullptr) {
    return pc_address;
  } else {
    return reinterpret_cast<Address*>(return_address_location_resolver_(
        reinterpret_cast<uintptr_t>(pc_address)));
  }
}

inline TypedFrame::TypedFrame(StackFrameIteratorBase* iterator)
    : CommonFrame(iterator) {}

inline CommonFrameWithJSLinkage::CommonFrameWithJSLinkage(
    StackFrameIteratorBase* iterator)
    : CommonFrame(iterator) {}

inline TypedFrameWithJSLinkage::TypedFrameWithJSLinkage(
    StackFrameIteratorBase* iterator)
    : CommonFrameWithJSLinkage(iterator) {}

inline NativeFrame::NativeFrame(StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline EntryFrame::EntryFrame(StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline ConstructEntryFrame::ConstructEntryFrame(
    StackFrameIteratorBase* iterator)
    : EntryFrame(iterator) {}

inline ExitFrame::ExitFrame(StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline BuiltinExitFrame::BuiltinExitFrame(StackFrameIteratorBase* iterator)
    : ExitFrame(iterator) {}

inline Tagged<Object> BuiltinExitFrame::receiver_slot_object() const {
  // The receiver is the first argument on the frame.
  // fp[1]: return address.
  // ------- fixed extra builtin arguments -------
  // fp[2]: new target.
  // fp[3]: target.
  // fp[4]: argc.
  // fp[5]: hole.
  // ------- JS stack arguments ------
  // fp[6]: receiver
  const int receiverOffset = BuiltinExitFrameConstants::kFirstArgumentOffset;
  return Tagged<Object>(base::Memory<Address>(fp() + receiverOffset));
}

inline Tagged<Object> BuiltinExitFrame::argc_slot_object() const {
  return Tagged<Object>(
      base::Memory<Address>(fp() + BuiltinExitFrameConstants::kArgcOffset));
}

inline Tagged<Object> BuiltinExitFrame::target_slot_object() const {
  return Tagged<Object>(
      base::Memory<Address>(fp() + BuiltinExitFrameConstants::kTargetOffset));
}

inline Tagged<Object> BuiltinExitFrame::new_target_slot_object() const {
  return Tagged<Object>(base::Memory<Address>(
      fp() + BuiltinExitFrameConstants::kNewTargetOffset));
}

inline ApiCallbackExitFrame::ApiCallbackExitFrame(
    StackFrameIteratorBase* iterator)
    : ExitFrame(iterator) {}

inline Tagged<Object> ApiCallbackExitFrame::context() const {
  return Tagged<Object>(base::Memory<Address>(
      fp() + ApiCallbackExitFrameConstants::kContextOffset));
}

inline FullObjectSlot ApiCallbackExitFrame::target_slot() const {
  return FullObjectSlot(fp() + ApiCallbackExitFrameConstants::kTargetOffset);
}

Tagged<Object> ApiCallbackExitFrame::receiver() const {
  return Tagged<Object>(base::Memory<Address>(
      fp() + ApiCallbackExitFrameConstants::kReceiverOffset));
}

Tagged<HeapObject> ApiCallbackExitFrame::target() const {
  Tagged<Object> function = *target_slot();
  DCHECK(IsJSFunction(function) || IsFunctionTemplateInfo(function));
  return Cast<HeapObject>(function);
}

void ApiCallbackExitFrame::set_target(Tagged<HeapObject> function) const {
  DCHECK(IsJSFunction(function) || IsFunctionTemplateInfo(function));
  target_slot().store(function);
}

int ApiCallbackExitFrame::ComputeParametersCount() const {
  int argc = static_cast<int>(base::Memory<Address>(
      fp() + ApiCallbackExitFrameConstants::kFCIArgcOffset));
  DCHECK_GE(argc, 0);
  return argc;
}

Tagged<Object> ApiCallbackExitFrame::GetParameter(int i) const {
  DCHECK(i >= 0 && i < ComputeParametersCount());
  int offset = ApiCallbackExitFrameConstants::kFirstArgumentOffset +
               i * kSystemPointerSize;
  return Tagged<Object>(base::Memory<Address>(fp() + offset));
}

bool ApiCallbackExitFrame::IsConstructor() const {
  Tagged<Object> new_context(base::Memory<Address>(
      fp() + ApiCallbackExitFrameConstants::kNewTargetOffset));
  return !IsUndefined(new_context, isolate());
}

inline ApiAccessorExitFrame::ApiAccessorExitFrame(
    StackFrameIteratorBase* iterator)
    : ExitFrame(iterator) {}

inline FullObjectSlot ApiAccessorExitFrame::property_name_slot() const {
  return FullObjectSlot(fp() +
                        ApiAccessorExitFrameConstants::kPropertyNameOffset);
}

inline FullObjectSlot ApiAccessorExitFrame::receiver_slot() const {
  return FullObjectSlot(fp() + ApiAccessorExitFrameConstants::kReceiverOffset);
}

inline FullObjectSlot ApiAccessorExitFrame::holder_slot() const {
  return FullObjectSlot(fp() + ApiAccessorExitFrameConstants::kHolderOffset);
}

Tagged<Name> ApiAccessorExitFrame::property_name() const {
  return Cast<Name>(*property_name_slot());
}

Tagged<Object> ApiAccessorExitFrame::receiver() const {
  return *receiver_slot();
}

Tagged<Object> ApiAccessorExitFrame::holder() const { return *holder_slot(); }

inline CommonFrame::CommonFrame(StackFrameIteratorBase* iterator)
    : StackFrame(iterator) {}

inline Tagged<Object> CommonFrame::GetExpression(int index) const {
  return Tagged<Object>(base::Memory<Address>(GetExpressionAddress(index)));
}

inline void CommonFrame::SetExpression(int index, Tagged<Object> value) {
  base::Memory<Address>(GetExpressionAddress(index)) = value.ptr();
}

inline Address CommonFrame::caller_fp() const {
  return base::Memory<Address>(fp() + StandardFrameConstants::kCallerFPOffset);
}

inline Address CommonFrame::caller_pc() const {
  return ReadPC(reinterpret_cast<Address*>(
      fp() + StandardFrameConstants::kCallerPCOffset));
}

inline bool CommonFrameWithJSLinkage::IsConstructFrame(Address fp) {
  intptr_t frame_type =
      base::Memory<intptr_t>(fp + TypedFrameConstants::kFrameTypeOffset);
  return frame_type == StackFrame::TypeToMarker(StackFrame::CONSTRUCT) ||
         frame_type == StackFrame::TypeToMarker(StackFrame::FAST_CONSTRUCT);
}

inline JavaScriptFrame::JavaScriptFrame(StackFrameIteratorBase* iterator)
    : CommonFrameWithJSLinkage(iterator) {}

Address CommonFrameWithJSLinkage::GetParameterSlot(int index) const {
  DCHECK_LE(-1, index);
  DCHECK_LT(index,
            std::max(GetActualArgumentCount(), ComputeParametersCount()));
  int parameter_offset = (index + 1) * kSystemPointerSize;
  return caller_sp() + parameter_offset;
}

inline int CommonFrameWithJSLinkage::GetActualArgumentCount() const {
  return 0;
}

inline void JavaScriptFrame::set_receiver(Tagged<Object> value) {
  base::Memory<Address>(GetParameterSlot(-1)) = value.ptr();
}

inline void UnoptimizedFrame::SetFeedbackVector(
    Tagged<FeedbackVector> feedback_vector) {
  const int offset = InterpreterFrameConstants::kFeedbackVectorFromFp;
  base::Memory<Address>(fp() + offset) = feedback_vector.ptr();
}

inline Tagged<Object> JavaScriptFrame::function_slot_object() const {
  const int offset = StandardFrameConstants::kFunctionOffset;
  return Tagged<Object>(base::Memory<Address>(fp() + offset));
}

inline TurbofanStubWithContextFrame::TurbofanStubWithContextFrame(
    StackFrameIteratorBase* iterator)
    : CommonFrame(iterator) {}

inline StubFrame::StubFrame(StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline OptimizedFrame::OptimizedFrame(StackFrameIteratorBase* iterator)
    : JavaScriptFrame(iterator) {}

inline UnoptimizedFrame::UnoptimizedFrame(StackFrameIteratorBase* iterator)
    : JavaScriptFrame(iterator) {}

inline InterpretedFrame::InterpretedFrame(StackFrameIteratorBase* iterator)
    : UnoptimizedFrame(iterator) {}

inline BaselineFrame::BaselineFrame(StackFrameIteratorBase* iterator)
    : UnoptimizedFrame(iterator) {}

inline MaglevFrame::MaglevFrame(StackFrameIteratorBase* iterator)
    : OptimizedFrame(iterator) {}

inline TurbofanFrame::TurbofanFrame(StackFrameIteratorBase* iterator)
    : OptimizedFrame(iterator) {}

inline BuiltinFrame::BuiltinFrame(StackFrameIteratorBase* iterator)
    : TypedFrameWithJSLinkage(iterator) {}

#if V8_ENABLE_WEBASSEMBLY
inline WasmFrame::WasmFrame(StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline WasmExitFrame::WasmExitFrame(StackFrameIteratorBase* iterator)
    : WasmFrame(iterator) {}

#if V8_ENABLE_DRUMBRAKE
inline WasmInterpreterEntryFrame::WasmInterpreterEntryFrame(
    StackFrameIteratorBase* iterator)
    : WasmFrame(iterator) {}
#endif  // V8_ENABLE_DRUMBRAKE

inline WasmDebugBreakFrame::WasmDebugBreakFrame(
    StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline WasmToJsFrame::WasmToJsFrame(StackFrameIteratorBase* iterator)
    : WasmFrame(iterator) {}

inline WasmToJsFunctionFrame::WasmToJsFunctionFrame(
    StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline JsToWasmFrame::JsToWasmFrame(StackFrameIteratorBase* iterator)
    : StubFrame(iterator) {}

inline StackSwitchFrame::StackSwitchFrame(StackFrameIteratorBase* iterator)
    : ExitFrame(iterator) {}

inline CWasmEntryFrame::CWasmEntryFrame(StackFrameIteratorBase* iterator)
    : StubFrame(iterator) {}

inline WasmLiftoffSetupFrame::WasmLiftoffSetupFrame(
    StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}
#endif  // V8_ENABLE_WEBASSEMBLY

inline InternalFrame::InternalFrame(StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline ConstructFrame::ConstructFrame(StackFrameIteratorBase* iterator)
    : InternalFrame(iterator) {}

inline FastConstructFrame::FastConstructFrame(StackFrameIteratorBase* iterator)
    : InternalFrame(iterator) {}

inline BuiltinContinuationFrame::BuiltinContinuationFrame(
    StackFrameIteratorBase* iterator)
    : InternalFrame(iterator) {}

inline JavaScriptBuiltinContinuationFrame::JavaScriptBuiltinContinuationFrame(
    StackFrameIteratorBase* iterator)
    : TypedFrameWithJSLinkage(iterator) {}

inline JavaScriptBuiltinContinuationWithCatchFrame::
    JavaScriptBuiltinContinuationWithCatchFrame(
        StackFrameIteratorBase* iterator)
    : JavaScriptBuiltinContinuationFrame(iterator) {}

inline IrregexpFrame::IrregexpFrame(StackFrameIteratorBase* iterator)
    : TypedFrame(iterator) {}

inline CommonFrame* DebuggableStackFrameIterator::frame() const {
  StackFrame* frame = iterator_.frame();
#if V8_ENABLE_WEBASSEMBLY
  DCHECK(frame->is_java_script() || frame->is_wasm());
#else
  DCHECK(frame->is_java_script());
#endif  // V8_ENABLE_WEBASSEMBLY
  return static_cast<CommonFrame*>(frame);
}

inline CommonFrame* DebuggableStackFrameIterator::Reframe() {
  iterator_.Reframe();
  return frame();
}

bool DebuggableStackFrameIterator::is_javascript() const {
  return frame()->is_java_script();
}

#if V8_ENABLE_WEBASSEMBLY
bool DebuggableStackFrameIterator::is_wasm() const {
  return frame()->is_wasm();
}

#if V8_ENABLE_DRUMBRAKE
bool DebuggableStackFrameIterator::is_wasm_interpreter_entry() const {
  return frame()->is_wasm_interpreter_entry();
}
#endif  // V8_ENABLE_DRUMBRAKE

#endif  // V8_ENABLE_WEBASSEMBLY

JavaScriptFrame* DebuggableStackFrameIterator::javascript_frame() const {
  return JavaScriptFrame::cast(frame());
}

// static
inline bool StackFrameIteratorForProfiler::IsValidFrameType(
    StackFrame::Type type) {
#if V8_ENABLE_WEBASSEMBLY
  DCHECK_NE(type, StackFrame::C_WASM_ENTRY);
#endif  // V8_ENABLE_WEBASSEMBLY
  return StackFrame::IsJavaScript(type) || type == StackFrame::EXIT ||
         type == StackFrame::BUILTIN_EXIT ||
         type == StackFrame::API_ACCESSOR_EXIT ||
         type == StackFrame::API_CALLBACK_EXIT ||
#if V8_ENABLE_WEBASSEMBLY
         type == StackFrame::WASM || type == StackFrame::WASM_TO_JS ||
         type == StackFrame::JS_TO_WASM ||
#if V8_ENABLE_DRUMBRAKE
         type == StackFrame::WASM_INTERPRETER_ENTRY ||
#endif  // V8_ENABLE_DRUMBRAKE
#endif  // V8_ENABLE_WEBASSEMBLY
         false;
}

inline StackFrame* StackFrameIteratorForProfiler::frame() const {
  DCHECK(!done());
  DCHECK(IsValidFrameType(frame_->type()));
  return frame_;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_FRAMES_INL_H_
