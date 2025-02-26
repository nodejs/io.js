// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/string-escape-analysis-reducer.h"

#include <utility>

#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

void StringEscapeAnalyzer::Run() {
  for (uint32_t processed = graph_.block_count(); processed > 0; --processed) {
    BlockIndex block_index = static_cast<BlockIndex>(processed - 1);

    const Block& block = graph_.Get(block_index);
    ProcessBlock(block);
  }

  // Because of loop phis, some StringConcat could now be escaping even though
  // they weren't escaping on first use.
  ReprocessStringConcats();

  // Now that we know for a fact which StringConcat will be elided, we can
  // compute which FrameStates will need to be reconstructed in the reducer.
  ComputeFrameStatesToReconstruct();
}

void StringEscapeAnalyzer::MarkNextFrameStateInputAsEscaping(
    FrameStateData::Iterator* it) {
  switch (it->current_instr()) {
    using Instr = FrameStateData::Instr;
    case Instr::kInput: {
      MachineType type;
      OpIndex input;
      it->ConsumeInput(&type, &input);
      MarkAsEscaping(input);
      return;
    }
    case Instr::kArgumentsElements:
    case Instr::kArgumentsLength:
    case Instr::kRestLength:
    case Instr::kDematerializedObjectReference:
    case Instr::kDematerializedObject:
    case Instr::kDematerializedStringConcat:
    case Instr::kDematerializedStringConcatReference:
    case Instr::kUnusedRegister:
      return;
  }
}

void StringEscapeAnalyzer::ProcessFrameState(V<FrameState> index,
                                             const FrameStateOp& framestate) {
  max_frame_state_input_count_ =
      std::max<uint32_t>(max_frame_state_input_count_, framestate.input_count);
  for (V<Any> input_idx : framestate.inputs()) {
    if (graph_.Get(input_idx).Is<StringConcatOp>()) {
      // This FrameState has a StringConcat as input, so we might need to
      // recreate it in the reducer.
      maybe_to_reconstruct_frame_states_.push_back(index);
      break;
    }
  }

  // We need to mark the Function and the Receiver as escaping. See
  // https://crbug.com/40059369.
  auto it = framestate.data->iterator(framestate.state_values());
  // Function
  MarkNextFrameStateInputAsEscaping(&it);
  // 1st parameter = receiver
  MarkNextFrameStateInputAsEscaping(&it);

  // Other FrameState uses are not considered as escaping.
}

void StringEscapeAnalyzer::ProcessBlock(const Block& block) {
  for (V<Any> index : base::Reversed(graph_.OperationIndices(block))) {
    const Operation& op = graph_.Get(index);
    switch (op.opcode) {
      case Opcode::kFrameState:
        ProcessFrameState(V<FrameState>::Cast(index), op.Cast<FrameStateOp>());
        break;
      case Opcode::kStringConcat:
        // The inputs of a StringConcat are only escaping if the StringConcat
        // itself is already escaping itself.
        if (IsEscaping(index)) {
          MarkAllInputsAsEscaping(op);
        } else {
          maybe_non_escaping_string_concats_.push_back(V<String>::Cast(index));
        }
        break;
      case Opcode::kStringLength:
        // The first input to StringConcat is the length of the result, which
        // means that StringLength won't prevent eliding StringConcat:
        // StringLength(StringConcat(len, left, rigth)) == len
        break;
      default:
        // By default, all uses are considered as escaping their inputs.
        MarkAllInputsAsEscaping(op);
    }
  }
}

void StringEscapeAnalyzer::MarkAllInputsAsEscaping(const Operation& op) {
  for (V<Any> input : op.inputs()) {
    if (!graph_.Get(input).Is<FrameStateOp>()) {
      MarkAsEscaping(input);
    }
  }
}

void StringEscapeAnalyzer::RecursivelyMarkAllStringConcatInputsAsEscaping(
    const StringConcatOp* concat) {
  base::SmallVector<const StringConcatOp*, 16> to_mark;
  to_mark.push_back(concat);

  while (!to_mark.empty()) {
    const StringConcatOp* curr = to_mark.back();
    to_mark.pop_back();

    for (V<Any> input_index : curr->inputs()) {
      const Operation& input = graph_.Get(input_index);
      if (input.Is<StringConcatOp>() && !IsEscaping(input_index)) {
        MarkAsEscaping(input_index);
        to_mark.push_back(&input.Cast<StringConcatOp>());
      }
    }
  }
}

void StringEscapeAnalyzer::ReprocessStringConcats() {
  constexpr uint32_t kMaxOpInputCount = std::numeric_limits<
      decltype(std::declval<Operation>().input_count)>::max();
  if (maybe_non_escaping_string_concats_.size() + max_frame_state_input_count_ >
      kMaxOpInputCount) {
    // There is a risk that in order to elide some StringConcat, we end up
    // needing more inputs for a FrameState than the maximum number of possible
    // inputs. Note that this is a bit of an overapproximation, but it should
    // still happen very rarely since it would require a huge function with
    // thousand of local variables and/or parameters. When this happens, we mark
    // all operations as "escaping", so that the reducer doesn't try to elide
    // anything.
    for (V<String> index : maybe_non_escaping_string_concats_) {
      MarkAsEscaping(index);
    }
  }

  for (V<String> index : maybe_non_escaping_string_concats_) {
    if (IsEscaping(index)) {
      RecursivelyMarkAllStringConcatInputsAsEscaping(
          &graph_.Get(index).Cast<StringConcatOp>());
    }
  }
}

void StringEscapeAnalyzer::ComputeFrameStatesToReconstruct() {
  for (V<FrameState> frame_state_idx : maybe_to_reconstruct_frame_states_) {
    const FrameStateOp& frame_state =
        graph_.Get(frame_state_idx).Cast<FrameStateOp>();
    for (V<Any> input : frame_state.inputs()) {
      if (graph_.Get(input).Is<StringConcatOp>() && !IsEscaping(input)) {
        RecursiveMarkAsShouldReconstruct(frame_state_idx);
        break;
      }
    }
  }
}

}  // namespace v8::internal::compiler::turboshaft
