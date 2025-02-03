// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_FRAME_STATES_H_
#define V8_COMPILER_FRAME_STATES_H_

#include "src/builtins/builtins.h"
#include "src/compiler/node.h"
#include "src/handles/handles.h"
#include "src/objects/shared-function-info.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

namespace wasm {
class ValueType;
using FunctionSig = Signature<ValueType>;
}  // namespace wasm

namespace compiler {

class JSGraph;
class Node;
class SharedFunctionInfoRef;

// Flag that describes how to combine the current environment with
// the output of a node to obtain a framestate for lazy bailout.
class OutputFrameStateCombine {
 public:
  static const size_t kInvalidIndex = SIZE_MAX;

  static OutputFrameStateCombine Ignore() {
    return OutputFrameStateCombine(kInvalidIndex);
  }
  static OutputFrameStateCombine PokeAt(size_t index) {
    return OutputFrameStateCombine(index);
  }

  size_t GetOffsetToPokeAt() const {
    DCHECK_NE(parameter_, kInvalidIndex);
    return parameter_;
  }

  bool IsOutputIgnored() const { return parameter_ == kInvalidIndex; }

  size_t ConsumedOutputCount() const { return IsOutputIgnored() ? 0 : 1; }

  bool operator==(OutputFrameStateCombine const& other) const {
    return parameter_ == other.parameter_;
  }
  bool operator!=(OutputFrameStateCombine const& other) const {
    return !(*this == other);
  }

  friend size_t hash_value(OutputFrameStateCombine const&);
  friend std::ostream& operator<<(std::ostream&,
                                  OutputFrameStateCombine const&);

 private:
  explicit OutputFrameStateCombine(size_t parameter) : parameter_(parameter) {}

  size_t const parameter_;
};

// The type of stack frame that a FrameState node represents.
enum class FrameStateType {
  kUnoptimizedFunction,    // Represents an UnoptimizedFrame.
  kInlinedExtraArguments,  // Represents inlined extra arguments.
  kConstructCreateStub,    // Represents a frame created before creating a new
                           // object in the construct stub.
  kConstructInvokeStub,    // Represents a frame created before invoking the
                           // constructor in the construct stub.
  kBuiltinContinuation,    // Represents a continuation to a stub.
#if V8_ENABLE_WEBASSEMBLY  // ↓ WebAssembly only
  kJSToWasmBuiltinContinuation,    // Represents a lazy deopt continuation for a
                                   // JS to Wasm call.
  kWasmInlinedIntoJS,              // Represents a Wasm function inlined into a
                                   // JS function.
  kLiftoffFunction,                // Represents an unoptimized (liftoff) wasm
                                   // function.
#endif                             // ↑ WebAssembly only
  kJavaScriptBuiltinContinuation,  // Represents a continuation to a JavaScipt
                                   // builtin.
  kJavaScriptBuiltinContinuationWithCatch  // Represents a continuation to a
                                           // JavaScipt builtin with a catch
                                           // handler.
};

class FrameStateFunctionInfo {
 public:
  FrameStateFunctionInfo(FrameStateType type, uint16_t parameter_count,
                         uint16_t max_arguments, int local_count,
                         Handle<SharedFunctionInfo> shared_info,
                         uint32_t wasm_liftoff_frame_size = 0,
                         uint32_t wasm_function_index = -1)
      : type_(type),
        parameter_count_(parameter_count),
        max_arguments_(max_arguments),
        local_count_(local_count),
#if V8_ENABLE_WEBASSEMBLY
        wasm_liftoff_frame_size_(wasm_liftoff_frame_size),
        wasm_function_index_(wasm_function_index),
#endif
        shared_info_(shared_info) {
  }

  int local_count() const { return local_count_; }
  uint16_t parameter_count() const { return parameter_count_; }
  uint16_t max_arguments() const { return max_arguments_; }
  Handle<SharedFunctionInfo> shared_info() const { return shared_info_; }
  FrameStateType type() const { return type_; }
  uint32_t wasm_liftoff_frame_size() const {
    return wasm_liftoff_frame_size_;
  }
  uint32_t wasm_function_index() const { return wasm_function_index_; }

  static bool IsJSFunctionType(FrameStateType type) {
    // This must be in sync with TRANSLATION_JS_FRAME_OPCODE_LIST in
    // translation-opcode.h or bad things happen.
    return type == FrameStateType::kUnoptimizedFunction ||
           type == FrameStateType::kJavaScriptBuiltinContinuation ||
           type == FrameStateType::kJavaScriptBuiltinContinuationWithCatch;
  }

 private:
  const FrameStateType type_;
  const uint16_t parameter_count_;
  const uint16_t max_arguments_;
  const int local_count_;
#if V8_ENABLE_WEBASSEMBLY
  const uint32_t wasm_liftoff_frame_size_ = 0;
  const uint32_t wasm_function_index_ = -1;
#else
  static constexpr uint32_t wasm_liftoff_frame_size_ = 0;
  static constexpr uint32_t wasm_function_index_ = -1;
#endif
  const Handle<SharedFunctionInfo> shared_info_;
};

#if V8_ENABLE_WEBASSEMBLY
class JSToWasmFrameStateFunctionInfo : public FrameStateFunctionInfo {
 public:
  JSToWasmFrameStateFunctionInfo(FrameStateType type, uint16_t parameter_count,
                                 int local_count,
                                 Handle<SharedFunctionInfo> shared_info,
                                 const wasm::FunctionSig* signature)
      : FrameStateFunctionInfo(type, parameter_count, 0, local_count,
                               shared_info),
        signature_(signature) {
    DCHECK_NOT_NULL(signature);
  }

  const wasm::FunctionSig* signature() const { return signature_; }

 private:
  const wasm::FunctionSig* const signature_;
};
#endif  // V8_ENABLE_WEBASSEMBLY

class FrameStateInfo final {
 public:
  FrameStateInfo(BytecodeOffset bailout_id,
                 OutputFrameStateCombine state_combine,
                 const FrameStateFunctionInfo* info)
      : bailout_id_(bailout_id),
        frame_state_combine_(state_combine),
        info_(info) {}

  FrameStateType type() const {
    return info_ == nullptr ? FrameStateType::kUnoptimizedFunction
                            : info_->type();
  }
  BytecodeOffset bailout_id() const { return bailout_id_; }
  OutputFrameStateCombine state_combine() const { return frame_state_combine_; }
  MaybeHandle<SharedFunctionInfo> shared_info() const {
    return info_ == nullptr ? MaybeHandle<SharedFunctionInfo>()
                            : info_->shared_info();
  }
  uint16_t parameter_count() const {
    return info_ == nullptr ? 0 : info_->parameter_count();
  }
  uint16_t max_arguments() const {
    return info_ == nullptr ? 0 : info_->max_arguments();
  }
  int local_count() const {
    return info_ == nullptr ? 0 : info_->local_count();
  }
  int stack_count() const {
    return type() == FrameStateType::kUnoptimizedFunction ? 1 : 0;
  }
  const FrameStateFunctionInfo* function_info() const { return info_; }

 private:
  BytecodeOffset const bailout_id_;
  OutputFrameStateCombine const frame_state_combine_;
  const FrameStateFunctionInfo* const info_;
};

bool operator==(FrameStateInfo const&, FrameStateInfo const&);
bool operator!=(FrameStateInfo const&, FrameStateInfo const&);

size_t hash_value(FrameStateInfo const&);

std::ostream& operator<<(std::ostream&, FrameStateInfo const&);

enum class ContinuationFrameStateMode { EAGER, LAZY, LAZY_WITH_CATCH };

class FrameState;

FrameState CreateStubBuiltinContinuationFrameState(
    JSGraph* graph, Builtin name, Node* context, Node* const* parameters,
    int parameter_count, Node* outer_frame_state,
    ContinuationFrameStateMode mode,
    const wasm::FunctionSig* signature = nullptr);

#if V8_ENABLE_WEBASSEMBLY
FrameState CreateJSWasmCallBuiltinContinuationFrameState(
    JSGraph* jsgraph, Node* context, Node* outer_frame_state,
    const wasm::FunctionSig* signature);
#endif  // V8_ENABLE_WEBASSEMBLY

FrameState CreateJavaScriptBuiltinContinuationFrameState(
    JSGraph* graph, SharedFunctionInfoRef shared, Builtin name, Node* target,
    Node* context, Node* const* stack_parameters, int stack_parameter_count,
    Node* outer_frame_state, ContinuationFrameStateMode mode);

FrameState CreateGenericLazyDeoptContinuationFrameState(
    JSGraph* graph, SharedFunctionInfoRef shared, Node* target, Node* context,
    Node* receiver, Node* outer_frame_state);

// Creates GenericLazyDeoptContinuationFrameState if
// --experimental-stack-trace-frames is enabled, returns outer_frame_state
// otherwise.
Node* CreateInlinedApiFunctionFrameState(JSGraph* graph,
                                         SharedFunctionInfoRef shared,
                                         Node* target, Node* context,
                                         Node* receiver,
                                         Node* outer_frame_state);

// Creates a FrameState otherwise identical to `frame_state` except the
// OutputFrameStateCombine is changed.
FrameState CloneFrameState(JSGraph* jsgraph, FrameState frame_state,
                           OutputFrameStateCombine changed_state_combine);

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_FRAME_STATES_H_
