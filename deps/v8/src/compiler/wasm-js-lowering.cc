// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-js-lowering.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"

namespace v8::internal::compiler {

WasmJSLowering::WasmJSLowering(Editor* editor, MachineGraph* mcgraph,
                               SourcePositionTable* source_position_table)
    : AdvancedReducer(editor),
      gasm_(mcgraph, mcgraph->zone()),
      mcgraph_(mcgraph),
      source_position_table_(source_position_table) {}

Reduction WasmJSLowering::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kTrapIf:
    case IrOpcode::kTrapUnless: {
      Node* effect = NodeProperties::GetEffectInput(node);
      Node* control = NodeProperties::GetControlInput(node);
      Node* trap_condition = NodeProperties::GetValueInput(node, 0);
      auto ool_trap = gasm_.MakeDeferredLabel();
      gasm_.InitializeEffectControl(effect, control);
      if (node->opcode() == IrOpcode::kTrapIf) {
        gasm_.GotoIf(trap_condition, &ool_trap);
      } else {
        DCHECK_EQ(node->opcode(), IrOpcode::kTrapUnless);
        gasm_.GotoIfNot(trap_condition, &ool_trap);
      }
      effect = gasm_.effect();
      control = gasm_.control();
      Node* goto_node = control;

      // Generate out of line code.
      gasm_.InitializeEffectControl(nullptr, nullptr);
      gasm_.Bind(&ool_trap);
      TrapId trap_id = TrapIdOf(node->op());
      Builtin trap = static_cast<Builtin>(trap_id);

      // Create new FrameState with the correct source position (the position
      // of the trap location).
      Node* frame_state = NodeProperties::GetValueInput(node, 1);
      const FrameStateInfo& info = FrameState(frame_state).frame_state_info();
      SourcePosition position = source_position_table_->GetSourcePosition(node);
      Node* new_frame_state = mcgraph_->graph()->CloneNode(frame_state);
      BytecodeOffset bailout_id(position.ScriptOffset());
      const Operator* frame_state_op = mcgraph_->common()->FrameState(
          bailout_id, info.state_combine(), info.function_info());
      NodeProperties::ChangeOp(new_frame_state, frame_state_op);

      gasm_.CallBuiltinWithFrameState(trap, Operator::kNoProperties,
                                      new_frame_state);
      Node* terminate = mcgraph_->graph()->NewNode(
          mcgraph_->common()->Throw(), gasm_.effect(), gasm_.control());
      MergeControlToEnd(mcgraph_->graph(), mcgraph_->common(), terminate);

      // Replace the trap node with the conditional branch.
      gasm_.InitializeEffectControl(effect, control);
      ReplaceWithValue(node, goto_node, gasm_.effect(), gasm_.control());
      node->Kill();
      return Replace(goto_node);
    }
    default:
      return NoChange();
  }
}

}  // namespace v8::internal::compiler
