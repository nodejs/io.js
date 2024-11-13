// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/frame-states.h"

#include <optional>

#include "src/base/functional.h"
#include "src/codegen/callable.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node.h"
#include "src/handles/handles-inl.h"
#include "src/objects/objects-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/value-type.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

size_t hash_value(OutputFrameStateCombine const& sc) {
  return base::hash_value(sc.parameter_);
}

std::ostream& operator<<(std::ostream& os, OutputFrameStateCombine const& sc) {
  if (sc.parameter_ == OutputFrameStateCombine::kInvalidIndex)
    return os << "Ignore";
  return os << "PokeAt(" << sc.parameter_ << ")";
}

bool operator==(FrameStateInfo const& lhs, FrameStateInfo const& rhs) {
  return lhs.type() == rhs.type() && lhs.bailout_id() == rhs.bailout_id() &&
         lhs.state_combine() == rhs.state_combine() &&
         lhs.function_info() == rhs.function_info();
}

bool operator!=(FrameStateInfo const& lhs, FrameStateInfo const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(FrameStateInfo const& info) {
  return base::hash_combine(static_cast<int>(info.type()), info.bailout_id(),
                            info.state_combine());
}

std::ostream& operator<<(std::ostream& os, FrameStateType type) {
  switch (type) {
    case FrameStateType::kUnoptimizedFunction:
      os << "UNOPTIMIZED_FRAME";
      break;
    case FrameStateType::kInlinedExtraArguments:
      os << "INLINED_EXTRA_ARGUMENTS";
      break;
    case FrameStateType::kConstructCreateStub:
      os << "CONSTRUCT_CREATE_STUB";
      break;
    case FrameStateType::kConstructInvokeStub:
      os << "CONSTRUCT_INVOKE_STUB";
      break;
    case FrameStateType::kBuiltinContinuation:
      os << "BUILTIN_CONTINUATION_FRAME";
      break;
#if V8_ENABLE_WEBASSEMBLY
    case FrameStateType::kWasmInlinedIntoJS:
      os << "WASM_INLINED_INTO_JS_FRAME";
      break;
    case FrameStateType::kJSToWasmBuiltinContinuation:
      os << "JS_TO_WASM_BUILTIN_CONTINUATION_FRAME";
      break;
    case FrameStateType::kLiftoffFunction:
      os << "LIFTOFF_FRAME";
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case FrameStateType::kJavaScriptBuiltinContinuation:
      os << "JAVA_SCRIPT_BUILTIN_CONTINUATION_FRAME";
      break;
    case FrameStateType::kJavaScriptBuiltinContinuationWithCatch:
      os << "JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME";
      break;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, FrameStateInfo const& info) {
  os << info.type() << ", " << info.bailout_id() << ", "
     << info.state_combine();
  Handle<SharedFunctionInfo> shared_info;
  if (info.shared_info().ToHandle(&shared_info)) {
    os << ", " << Brief(*shared_info);
  }
  return os;
}

namespace {

// Lazy deopt points where the frame state is associated with a call get an
// additional parameter for the return result from the call. The return result
// is added by the deoptimizer and not explicitly specified in the frame state.
// Lazy deopt points which can catch exceptions further get an additional
// parameter, namely the exception thrown. The exception is also added by the
// deoptimizer.
uint8_t DeoptimizerParameterCountFor(ContinuationFrameStateMode mode) {
  switch (mode) {
    case ContinuationFrameStateMode::EAGER:
      return 0;
    case ContinuationFrameStateMode::LAZY:
      return 1;
    case ContinuationFrameStateMode::LAZY_WITH_CATCH:
      return 2;
  }
  UNREACHABLE();
}

FrameState CreateBuiltinContinuationFrameStateCommon(
    JSGraph* jsgraph, FrameStateType frame_type, Builtin name, Node* closure,
    Node* context, Node** parameters, int parameter_count,
    Node* outer_frame_state,
    Handle<SharedFunctionInfo> shared = Handle<SharedFunctionInfo>(),
    const wasm::FunctionSig* signature = nullptr) {
  Graph* const graph = jsgraph->graph();
  CommonOperatorBuilder* const common = jsgraph->common();

  const Operator* op_param =
      common->StateValues(parameter_count, SparseInputMask::Dense());
  Node* params_node = graph->NewNode(op_param, parameter_count, parameters);

  BytecodeOffset bailout_id = Builtins::GetContinuationBytecodeOffset(name);
#if V8_ENABLE_WEBASSEMBLY
  const FrameStateFunctionInfo* state_info =
      signature ? common->CreateJSToWasmFrameStateFunctionInfo(
                      frame_type, parameter_count, 0, shared, signature)
                : common->CreateFrameStateFunctionInfo(
                      frame_type, parameter_count, 0, 0, shared);
#else
  DCHECK_NULL(signature);
  const FrameStateFunctionInfo* state_info =
      common->CreateFrameStateFunctionInfo(frame_type, parameter_count, 0, 0,
                                           shared);
#endif  // V8_ENABLE_WEBASSEMBLY

  const Operator* op = common->FrameState(
      bailout_id, OutputFrameStateCombine::Ignore(), state_info);
  return FrameState(graph->NewNode(op, params_node, jsgraph->EmptyStateValues(),
                                   jsgraph->EmptyStateValues(), context,
                                   closure, outer_frame_state));
}

}  // namespace

FrameState CreateStubBuiltinContinuationFrameState(
    JSGraph* jsgraph, Builtin name, Node* context, Node* const* parameters,
    int parameter_count, Node* outer_frame_state,
    ContinuationFrameStateMode mode, const wasm::FunctionSig* signature) {
  Callable callable = Builtins::CallableFor(jsgraph->isolate(), name);
  CallInterfaceDescriptor descriptor = callable.descriptor();

  std::vector<Node*> actual_parameters;
  // Stack parameters first. Depending on {mode}, final parameters are added
  // by the deoptimizer and aren't explicitly passed in the frame state.
  int stack_parameter_count =
      descriptor.GetStackParameterCount() - DeoptimizerParameterCountFor(mode);

  // Ensure the parameters added by the deoptimizer are passed on the stack.
  // This check prevents using TFS builtins as continuations while doing the
  // lazy deopt. Use TFC or TFJ builtin as a lazy deopt continuation which
  // would pass the result parameter on the stack.
  DCHECK_GE(stack_parameter_count, 0);

  // Reserving space in the vector.
  actual_parameters.reserve(stack_parameter_count +
                            descriptor.GetRegisterParameterCount());
  for (int i = 0; i < stack_parameter_count; ++i) {
    actual_parameters.push_back(
        parameters[descriptor.GetRegisterParameterCount() + i]);
  }
  // Register parameters follow, context will be added by instruction selector
  // during FrameState translation.
  for (int i = 0; i < descriptor.GetRegisterParameterCount(); ++i) {
    actual_parameters.push_back(parameters[i]);
  }

  FrameStateType frame_state_type = FrameStateType::kBuiltinContinuation;
#if V8_ENABLE_WEBASSEMBLY
  if (name == Builtin::kJSToWasmLazyDeoptContinuation) {
    CHECK_NOT_NULL(signature);
    frame_state_type = FrameStateType::kJSToWasmBuiltinContinuation;
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return CreateBuiltinContinuationFrameStateCommon(
      jsgraph, frame_state_type, name, jsgraph->UndefinedConstant(), context,
      actual_parameters.data(), static_cast<int>(actual_parameters.size()),
      outer_frame_state, Handle<SharedFunctionInfo>(), signature);
}

#if V8_ENABLE_WEBASSEMBLY
FrameState CreateJSWasmCallBuiltinContinuationFrameState(
    JSGraph* jsgraph, Node* context, Node* outer_frame_state,
    const wasm::FunctionSig* signature) {
  std::optional<wasm::ValueKind> wasm_return_kind =
      wasm::WasmReturnTypeFromSignature(signature);
  Node* node_return_type =
      jsgraph->SmiConstant(wasm_return_kind ? wasm_return_kind.value() : -1);
  Node* lazy_deopt_parameters[] = {node_return_type};
  return CreateStubBuiltinContinuationFrameState(
      jsgraph, Builtin::kJSToWasmLazyDeoptContinuation, context,
      lazy_deopt_parameters, arraysize(lazy_deopt_parameters),
      outer_frame_state, ContinuationFrameStateMode::LAZY, signature);
}
#endif  // V8_ENABLE_WEBASSEMBLY

FrameState CreateJavaScriptBuiltinContinuationFrameState(
    JSGraph* jsgraph, SharedFunctionInfoRef shared, Builtin name, Node* target,
    Node* context, Node* const* stack_parameters, int stack_parameter_count,
    Node* outer_frame_state, ContinuationFrameStateMode mode) {
  // Depending on {mode}, final parameters are added by the deoptimizer
  // and aren't explicitly passed in the frame state.
  DCHECK_EQ(Builtins::GetStackParameterCount(name),
            stack_parameter_count + DeoptimizerParameterCountFor(mode));

  Node* argc = jsgraph->ConstantNoHole(Builtins::GetStackParameterCount(name));

  // Stack parameters first. They must be first because the receiver is expected
  // to be the second value in the translation when creating stack crawls
  // (e.g. Error.stack) of optimized JavaScript frames.
  std::vector<Node*> actual_parameters;
  actual_parameters.reserve(stack_parameter_count);
  for (int i = 0; i < stack_parameter_count; ++i) {
    actual_parameters.push_back(stack_parameters[i]);
  }

  Node* new_target = jsgraph->UndefinedConstant();

  // Register parameters follow stack parameters. The context will be added by
  // instruction selector during FrameState translation.
  actual_parameters.push_back(target);      // kJavaScriptCallTargetRegister
  actual_parameters.push_back(new_target);  // kJavaScriptCallNewTargetRegister
  actual_parameters.push_back(argc);        // kJavaScriptCallArgCountRegister

  return CreateBuiltinContinuationFrameStateCommon(
      jsgraph,
      mode == ContinuationFrameStateMode::LAZY_WITH_CATCH
          ? FrameStateType::kJavaScriptBuiltinContinuationWithCatch
          : FrameStateType::kJavaScriptBuiltinContinuation,
      name, target, context, &actual_parameters[0],
      static_cast<int>(actual_parameters.size()), outer_frame_state,
      shared.object());
}

FrameState CreateGenericLazyDeoptContinuationFrameState(
    JSGraph* graph, SharedFunctionInfoRef shared, Node* target, Node* context,
    Node* receiver, Node* outer_frame_state) {
  Node* stack_parameters[]{receiver};
  const int stack_parameter_count = arraysize(stack_parameters);
  return CreateJavaScriptBuiltinContinuationFrameState(
      graph, shared, Builtin::kGenericLazyDeoptContinuation, target, context,
      stack_parameters, stack_parameter_count, outer_frame_state,
      ContinuationFrameStateMode::LAZY);
}

Node* CreateInlinedApiFunctionFrameState(JSGraph* graph,
                                         SharedFunctionInfoRef shared,
                                         Node* target, Node* context,
                                         Node* receiver,
                                         Node* outer_frame_state) {
  return outer_frame_state;
}

FrameState CloneFrameState(JSGraph* jsgraph, FrameState frame_state,
                           OutputFrameStateCombine changed_state_combine) {
  Graph* graph = jsgraph->graph();
  CommonOperatorBuilder* common = jsgraph->common();

  DCHECK_EQ(IrOpcode::kFrameState, frame_state->op()->opcode());

  const Operator* op = common->FrameState(
      frame_state.frame_state_info().bailout_id(), changed_state_combine,
      frame_state.frame_state_info().function_info());
  return FrameState(
      graph->NewNode(op, frame_state.parameters(), frame_state.locals(),
                     frame_state.stack(), frame_state.context(),
                     frame_state.function(), frame_state.outer_frame_state()));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
