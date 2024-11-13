// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_JS_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_JS_LOWERING_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/wasm-graph-assembler.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// This reducer is part of the JavaScript pipeline and contains lowering of
// wasm nodes (from inlined wasm functions).
//
// The reducer replaces all TrapIf nodes with a conditional goto to deferred
// code containing a call to the trap builtin.
template <class Next>
class WasmJSLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(WasmJSLowering)

  V<None> REDUCE(TrapIf)(V<Word32> condition, OptionalV<FrameState> frame_state,
                         bool negated, TrapId trap_id) {
    // All TrapIf nodes in JS need to have a FrameState.
    DCHECK(frame_state.valid());
    Builtin trap = static_cast<Builtin>(trap_id);
    // The call is not marked as Operator::kNoDeopt. While it cannot actually
    // deopt, deopt info based on the provided FrameState is required for stack
    // trace creation of the wasm trap.
    const bool needs_frame_state = true;
    const CallDescriptor* tf_descriptor = GetBuiltinCallDescriptor(
        trap, Asm().graph_zone(), StubCallMode::kCallBuiltinPointer,
        needs_frame_state, Operator::kNoProperties);
    const TSCallDescriptor* ts_descriptor =
        TSCallDescriptor::Create(tf_descriptor, CanThrow::kYes,
                                 LazyDeoptOnThrow::kNo, Asm().graph_zone());

    V<FrameState> new_frame_state =
        CreateFrameStateWithUpdatedBailoutId(frame_state.value());
    V<Word32> should_trap = negated ? __ Word32Equal(condition, 0) : condition;
    IF (UNLIKELY(should_trap)) {
      OpIndex call_target = __ NumberConstant(static_cast<int>(trap));
      __ Call(call_target, new_frame_state, {}, ts_descriptor);
      __ Unreachable();  // The trap builtin never returns.
    }

    return V<None>::Invalid();
  }

 private:
  OpIndex CreateFrameStateWithUpdatedBailoutId(OpIndex frame_state) {
    // Create new FrameState with the correct source position (the position of
    // the trap location).
    const FrameStateOp& frame_state_op =
        Asm().output_graph().Get(frame_state).template Cast<FrameStateOp>();
    const FrameStateData* data = frame_state_op.data;
    const FrameStateInfo& info = data->frame_state_info;

    V<AnyOrNone> origin = Asm().current_operation_origin();
    DCHECK(origin.valid());
    int offset = __ input_graph().source_positions()[origin].ScriptOffset();

    const FrameStateInfo* new_info =
        Asm().graph_zone()->template New<FrameStateInfo>(
            BytecodeOffset(offset), info.state_combine(), info.function_info());
    FrameStateData* new_data = Asm().graph_zone()->template New<FrameStateData>(
        FrameStateData{*new_info, data->instructions, data->machine_types,
                       data->int_operands});
    return __ FrameState(frame_state_op.inputs(), frame_state_op.inlined,
                         new_data);
  }

  Isolate* isolate_ = __ data() -> isolate();
  SourcePositionTable* source_positions_ = __ data() -> source_positions();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_JS_LOWERING_REDUCER_H_
