// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-inlining.h"

#include <cinttypes>

#include "src/compiler/all-nodes.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/wasm-compiler.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/graph-builder-interface.h"
#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8 {
namespace internal {
namespace compiler {

Reduction WasmInliner::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kCall:
    case IrOpcode::kTailCall:
      return ReduceCall(node);
    default:
      return NoChange();
  }
}

#define TRACE(...) \
  if (v8_flags.trace_wasm_inlining) PrintF(__VA_ARGS__)

void WasmInliner::Trace(Node* call, int inlinee, const char* decision) {
  TRACE("[function %d: considering node %d, call to %d: %s]\n",
        data_.func_index, call->id(), inlinee, decision);
}

int WasmInliner::GetCallCount(Node* call) {
  if (!env_->enabled_features.has_inlining() && !env_->module->is_wasm_gc) {
    return 0;
  }
  return mcgraph()->GetCallCount(call->id());
}

// TODO(12166): Save inlined frames for trap/--trace-wasm purposes.
Reduction WasmInliner::ReduceCall(Node* call) {
  DCHECK(call->opcode() == IrOpcode::kCall ||
         call->opcode() == IrOpcode::kTailCall);

  if (seen_.find(call) != seen_.end()) {
    TRACE("[function %d: have already seen node %d, skipping]\n",
          data_.func_index, call->id());
    return NoChange();
  }
  seen_.insert(call);

  Node* callee = NodeProperties::GetValueInput(call, 0);
  IrOpcode::Value reloc_opcode = mcgraph_->machine()->Is32()
                                     ? IrOpcode::kRelocatableInt32Constant
                                     : IrOpcode::kRelocatableInt64Constant;
  if (callee->opcode() != reloc_opcode) {
    TRACE("[function %d: node %d: not a relocatable constant]\n",
          data_.func_index, call->id());
    return NoChange();
  }
  auto info = OpParameter<RelocatablePtrConstantInfo>(callee->op());
  uint32_t inlinee_index = static_cast<uint32_t>(info.value());
  if (info.rmode() != RelocInfo::WASM_CALL) {
    Trace(call, inlinee_index, "not a wasm call");
    return NoChange();
  }
  if (inlinee_index < module()->num_imported_functions) {
    Trace(call, inlinee_index, "imported function");
    return NoChange();
  }

  // We limit the times a function can be inlined to avoid repeatedly inlining
  // recursive calls. Since we only check here (and not in {Finalize}), it is
  // possible to exceed this limit if we find a large number of calls in a
  // single pass.
  constexpr int kMaximumInlinedCallsPerFunction = 3;
  if (function_inlining_count_[inlinee_index] >=
      kMaximumInlinedCallsPerFunction) {
    Trace(call, inlinee_index,
          "too many inlined calls to (recursive?) function");
    return NoChange();
  }

  CHECK_LT(inlinee_index, module()->functions.size());
  const wasm::WasmFunction* inlinee = &module()->functions[inlinee_index];
  base::Vector<const uint8_t> function_bytes =
      data_.wire_bytes_storage->GetCode(inlinee->code);

  int call_count = GetCallCount(call);

  int wire_byte_size = static_cast<int>(function_bytes.size());
  int min_count_for_inlining =
      v8_flags.wasm_inlining_ignore_call_counts ? 0 : wire_byte_size / 2;

  // If liftoff ran and collected call counts, only inline calls that have been
  // invoked often, except for truly tiny functions.
  if (v8_flags.liftoff &&
      (env_->enabled_features.has_inlining() || env_->module->is_wasm_gc) &&
      wire_byte_size >= 12 && call_count < min_count_for_inlining) {
    Trace(call, inlinee_index, "not called often enough");
    return NoChange();
  }

  Trace(call, inlinee_index, "adding to inlining candidates");

  CandidateInfo candidate{call, inlinee_index, call_count,
                          function_bytes.length()};

  inlining_candidates_.push(candidate);
  return NoChange();
}

bool SmallEnoughToInline(const wasm::WasmModule* module,
                         size_t current_graph_size, uint32_t candidate_size,
                         size_t initial_graph_size) {
  if (candidate_size > v8_flags.wasm_inlining_max_size) {
    return false;
  }
  if (WasmInliner::graph_size_allows_inlining(
          module, current_graph_size + candidate_size, initial_graph_size)) {
    return true;
  }
  // For truly tiny functions, let's be a bit more generous.
  return candidate_size <= 12 &&
         WasmInliner::graph_size_allows_inlining(
             module, current_graph_size - 100, initial_graph_size);
}

bool WasmInliner::graph_size_allows_inlining(const wasm::WasmModule* module,
                                             size_t graph_size,
                                             size_t initial_graph_size) {
  size_t budget =
      std::max<size_t>(v8_flags.wasm_inlining_min_budget,
                       v8_flags.wasm_inlining_factor * initial_graph_size);
  // For large-ish functions, the inlining budget is mainly defined by the
  // wasm_inlining_budget.
  size_t upper_budget = v8_flags.wasm_inlining_budget;
  double small_function_percentage =
      module->num_small_functions * 100.0 / module->num_declared_functions;
  if (small_function_percentage < 50) {
    // If there are few small functions, it indicates that the toolchain already
    // performed significant inlining. Reduce the budget significantly as
    // inlining has a diminishing ROI.

    // We also apply a linear progression of the budget in the interval [25, 50]
    // for the small_function_percentage. This progression is just added to
    // prevent performance cliffs (e.g. when just performing a sharp cutoff at
    // the 50% point) and not based on actual data.
    double smallishness = std::max(25.0, small_function_percentage) - 25.0;
    size_t lower_budget = upper_budget / 10;
    double step = (upper_budget - lower_budget) / 25.0;
    upper_budget = lower_budget + smallishness * step;
  }
  // Independent of the wasm_inlining_budget, for large functions we should
  // still allow some inlining.
  size_t full_budget = std::max<size_t>(upper_budget, initial_graph_size * 1.1);
  budget = std::min<size_t>(full_budget, budget);
  return graph_size < budget;
}

void WasmInliner::Trace(const CandidateInfo& candidate, const char* decision) {
  TRACE(
      "  [function %d: considering candidate {@%d, index=%d, count=%d, "
      "size=%d, score=%" PRId64 "}: %s]\n",
      data_.func_index, candidate.node->id(), candidate.inlinee_index,
      candidate.call_count, candidate.wire_byte_size, candidate.score(),
      decision);
}

void WasmInliner::Finalize() {
  TRACE("[function %d (%s): %s]\n", data_.func_index, debug_name_,
        inlining_candidates_.empty() ? "no inlining candidates"
                                     : "going through inlining candidates");
  if (inlining_candidates_.empty()) return;
  while (!inlining_candidates_.empty()) {
    CandidateInfo candidate = inlining_candidates_.top();
    inlining_candidates_.pop();
    Node* call = candidate.node;
    if (call->IsDead()) {
      Trace(candidate, "dead node");
      continue;
    }
    // We could build the candidate's graph first and consider its node count,
    // but it turns out that wire byte size and node count are quite strongly
    // correlated, at about 1.16 nodes per wire byte (measured for J2Wasm).
    if (!SmallEnoughToInline(module(), current_graph_size_,
                             candidate.wire_byte_size, initial_graph_size_)) {
      Trace(candidate, "not enough inlining budget");
      continue;
    }
    const wasm::WasmFunction* inlinee =
        &module()->functions[candidate.inlinee_index];

    DCHECK_EQ(inlinee->sig->parameter_count(),
              call->op()->ValueInputCount() - 2);
#if DEBUG
    // The two first parameters in the call are the function and instance, and
    // then come the wasm function parameters.
    for (uint32_t i = 0; i < inlinee->sig->parameter_count(); i++) {
      if (!NodeProperties::IsTyped(call->InputAt(i + 2))) continue;
      wasm::TypeInModule param_type =
          NodeProperties::GetType(call->InputAt(i + 2)).AsWasm();
      CHECK(IsSubtypeOf(param_type.type, inlinee->sig->GetParam(i),
                        param_type.module, module()));
    }
#endif

    base::Vector<const uint8_t> function_bytes =
        data_.wire_bytes_storage->GetCode(inlinee->code);

    bool is_shared = module()->types[inlinee->sig_index].is_shared;

    const wasm::FunctionBody inlinee_body{inlinee->sig, inlinee->code.offset(),
                                          function_bytes.begin(),
                                          function_bytes.end(), is_shared};

    // If the inlinee was not validated before, do that now.
    if (V8_UNLIKELY(
            !module()->function_was_validated(candidate.inlinee_index))) {
      if (ValidateFunctionBody(zone(), env_->enabled_features, module(),
                               detected_, inlinee_body)
              .failed()) {
        Trace(candidate, "function is invalid");
        // At this point we cannot easily raise a compilation error any more.
        // Since this situation is highly unlikely though, we just ignore this
        // inlinee and move on. The same validation error will be triggered
        // again when actually compiling the invalid function.
        continue;
      }
      module()->set_function_validated(candidate.inlinee_index);
    }

    std::vector<WasmLoopInfo> inlinee_loop_infos;
    wasm::DanglingExceptions dangling_exceptions;

    size_t subgraph_min_node_id = graph()->NodeCount();
    Node* inlinee_start;
    Node* inlinee_end;
    SourcePosition caller_pos =
        data_.source_positions->GetSourcePosition(candidate.node);
    inlining_positions_->push_back({static_cast<int>(candidate.inlinee_index),
                                    call->opcode() == IrOpcode::kTailCall,
                                    caller_pos});
    int inlining_position_id =
        static_cast<int>(inlining_positions_->size()) - 1;
    WasmGraphBuilder builder(env_, zone(), mcgraph_, inlinee_body.sig,
                             data_.source_positions,
                             WasmGraphBuilder::kInstanceParameterMode,
                             nullptr /* isolate */, env_->enabled_features);
    builder.set_inlining_id(inlining_position_id);
    {
      Graph::SubgraphScope scope(graph());
      wasm::BuildTFGraph(zone()->allocator(), env_->enabled_features, module(),
                         &builder, detected_, inlinee_body, &inlinee_loop_infos,
                         &dangling_exceptions, data_.node_origins,
                         candidate.inlinee_index, data_.assumptions,
                         NodeProperties::IsExceptionalCall(call)
                             ? wasm::kInlinedHandledCall
                             : wasm::kInlinedNonHandledCall);
      inlinee_start = graph()->start();
      inlinee_end = graph()->end();
    }

    size_t additional_nodes = graph()->NodeCount() - subgraph_min_node_id;
    Trace(candidate, "inlining");
    current_graph_size_ += additional_nodes;
    DCHECK_GE(function_inlining_count_[candidate.inlinee_index], 0);
    function_inlining_count_[candidate.inlinee_index]++;

    if (call->opcode() == IrOpcode::kCall) {
      InlineCall(call, inlinee_start, inlinee_end, inlinee->sig, caller_pos,
                 &dangling_exceptions);
    } else {
      InlineTailCall(call, inlinee_start, inlinee_end);
    }
    call->Kill();
    data_.loop_infos->insert(data_.loop_infos->end(),
                             inlinee_loop_infos.begin(),
                             inlinee_loop_infos.end());
    // Returning after inlining, so that new calls in the inlined body are added
    // to the candidates list and prioritized if they have a higher score.
    return;
  }
}

/* Rewire callee formal parameters to the call-site real parameters. Rewire
 * effect and control dependencies of callee's start node with the respective
 * inputs of the call node.
 */
void WasmInliner::RewireFunctionEntry(Node* call, Node* callee_start) {
  Node* control = NodeProperties::GetControlInput(call);
  Node* effect = NodeProperties::GetEffectInput(call);

  for (Edge edge : callee_start->use_edges()) {
    Node* use = edge.from();
    switch (use->opcode()) {
      case IrOpcode::kParameter: {
        // Index 0 is the callee node.
        int index = 1 + ParameterIndexOf(use->op());
        Replace(use, NodeProperties::GetValueInput(call, index));
        break;
      }
      default:
        if (NodeProperties::IsEffectEdge(edge)) {
          edge.UpdateTo(effect);
        } else if (NodeProperties::IsControlEdge(edge)) {
          // Projections pointing to the inlinee start are floating control.
          // They should point to the graph's start.
          edge.UpdateTo(use->opcode() == IrOpcode::kProjection
                            ? graph()->start()
                            : control);
        } else {
          UNREACHABLE();
        }
        Revisit(edge.from());
        break;
    }
  }
}

void WasmInliner::InlineTailCall(Node* call, Node* callee_start,
                                 Node* callee_end) {
  DCHECK_EQ(call->opcode(), IrOpcode::kTailCall);
  // 1) Rewire function entry.
  RewireFunctionEntry(call, callee_start);
  // 2) For tail calls, all we have to do is rewire all terminators of the
  // inlined graph to the end of the caller graph.
  for (Node* const input : callee_end->inputs()) {
    DCHECK(IrOpcode::IsGraphTerminator(input->opcode()));
    MergeControlToEnd(graph(), common(), input);
  }
  for (Edge edge_to_end : call->use_edges()) {
    DCHECK_EQ(edge_to_end.from(), graph()->end());
    edge_to_end.UpdateTo(mcgraph()->Dead());
  }
  callee_end->Kill();
  call->Kill();
  Revisit(graph()->end());
}

void WasmInliner::InlineCall(Node* call, Node* callee_start, Node* callee_end,
                             const wasm::FunctionSig* inlinee_sig,
                             SourcePosition parent_pos,
                             wasm::DanglingExceptions* dangling_exceptions) {
  DCHECK_EQ(call->opcode(), IrOpcode::kCall);

  Node* handler = nullptr;
  bool is_exceptional_call = NodeProperties::IsExceptionalCall(call, &handler);

  // 1) Rewire function entry.
  RewireFunctionEntry(call, callee_start);

  // 2) Handle all graph terminators for the callee.
  NodeVector return_nodes(zone());
  for (Node* const input : callee_end->inputs()) {
    DCHECK(IrOpcode::IsGraphTerminator(input->opcode()));
    switch (input->opcode()) {
      case IrOpcode::kReturn:
        // Returns are collected to be rewired into the caller graph later.
        return_nodes.push_back(input);
        break;
      case IrOpcode::kDeoptimize:
      case IrOpcode::kTerminate:
      case IrOpcode::kThrow:
        MergeControlToEnd(graph(), common(), input);
        break;
      case IrOpcode::kTailCall: {
        // A tail call in the callee inlined in a regular call in the caller has
        // to be transformed into a regular call, and then returned from the
        // inlinee. It will then be handled like any other return.
        auto descriptor = CallDescriptorOf(input->op());
        NodeProperties::ChangeOp(input, common()->Call(descriptor));
        // Consider a function f which calls g which tail calls h. If h traps,
        // we need the stack trace to include h and f (g's frame is gone due to
        // the tail call). The way to achieve this is to set this call's
        // position to the position of g's call in f.
        data_.source_positions->SetSourcePosition(input, parent_pos);

        DCHECK_GT(input->op()->EffectOutputCount(), 0);
        DCHECK_GT(input->op()->ControlOutputCount(), 0);
        Node* effect = input;
        Node* control = input;
        if (is_exceptional_call) {
          // Remember dangling exception (will be connected later).
          Node* if_exception = graph()->NewNode(
              mcgraph()->common()->IfException(), input, control);
          dangling_exceptions->Add(if_exception, if_exception, if_exception);
          control = graph()->NewNode(mcgraph()->common()->IfSuccess(), input);
        }

        int return_arity = static_cast<int>(inlinee_sig->return_count());
        NodeVector return_inputs(zone());
        // The first input of a return node is always the 0 constant.
        return_inputs.push_back(graph()->NewNode(common()->Int32Constant(0)));
        if (return_arity == 1) {
          // Tail calls are untyped; we have to type the node here.
          // TODO(manoskouk): Try to compute a more precise type from the callee
          // node.
          NodeProperties::SetType(
              input, Type::Wasm({inlinee_sig->GetReturn(0), module()},
                                graph()->zone()));
          return_inputs.push_back(input);
        } else if (return_arity > 1) {
          for (int i = 0; i < return_arity; i++) {
            Node* ith_projection =
                graph()->NewNode(common()->Projection(i), input, control);
            // Similarly here we have to type the call's projections.
            NodeProperties::SetType(
                ith_projection,
                Type::Wasm({inlinee_sig->GetReturn(i), module()},
                           graph()->zone()));
            return_inputs.push_back(ith_projection);
          }
        }

        // Add effect and control inputs.
        return_inputs.push_back(effect);
        return_inputs.push_back(control);

        Node* ret = graph()->NewNode(common()->Return(return_arity),
                                     static_cast<int>(return_inputs.size()),
                                     return_inputs.data());
        return_nodes.push_back(ret);
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  callee_end->Kill();

  // 3) Rewire unhandled calls to the handler.
  if (is_exceptional_call) {
    int handler_count = static_cast<int>(dangling_exceptions->Size());
    if (handler_count > 0) {
      Node* control_output =
          graph()->NewNode(common()->Merge(handler_count), handler_count,
                           dangling_exceptions->controls.data());
      std::vector<Node*>& effects(dangling_exceptions->effects);
      std::vector<Node*>& values(dangling_exceptions->exception_values);

      effects.push_back(control_output);
      values.push_back(control_output);
      Node* value_output = graph()->NewNode(
          common()->Phi(MachineRepresentation::kTagged, handler_count),
          handler_count + 1, values.data());
      Node* effect_output = graph()->NewNode(common()->EffectPhi(handler_count),
                                             handler_count + 1, effects.data());
      ReplaceWithValue(handler, value_output, effect_output, control_output);
    } else {
      // Nothing in the inlined function can throw. Remove the handler.
      ReplaceWithValue(handler, mcgraph()->Dead(), mcgraph()->Dead(),
                       mcgraph()->Dead());
    }
  }

  if (!return_nodes.empty()) {
    /* 4) Collect all return site value, effect, and control inputs into phis
     * and merges. */
    int const return_count = static_cast<int>(return_nodes.size());
    NodeVector controls(zone());
    NodeVector effects(zone());
    for (Node* const return_node : return_nodes) {
      controls.push_back(NodeProperties::GetControlInput(return_node));
      effects.push_back(NodeProperties::GetEffectInput(return_node));
    }
    Node* control_output = graph()->NewNode(common()->Merge(return_count),
                                            return_count, &controls.front());
    effects.push_back(control_output);
    Node* effect_output =
        graph()->NewNode(common()->EffectPhi(return_count),
                         static_cast<int>(effects.size()), &effects.front());

    // The first input of a return node is discarded. This is because Wasm
    // functions always return an additional 0 constant as a first return value.
    DCHECK(
        Int32Matcher(NodeProperties::GetValueInput(return_nodes[0], 0)).Is(0));
    int const return_arity = return_nodes[0]->op()->ValueInputCount() - 1;
    NodeVector values(zone());
#if DEBUG
    for (Node* const return_node : return_nodes) {
      // 3 = effect, control, first 0 return value.
      CHECK_EQ(return_arity, return_node->InputCount() - 3);
    }
#endif
    for (int i = 0; i < return_arity; i++) {
      NodeVector ith_values(zone());
      for (Node* const return_node : return_nodes) {
        Node* value = NodeProperties::GetValueInput(return_node, i + 1);
        ith_values.push_back(value);
      }
      ith_values.push_back(control_output);
      // Find the correct machine representation for the return values from the
      // inlinee signature.
      MachineRepresentation repr =
          inlinee_sig->GetReturn(i).machine_representation();
      Node* ith_value_output = graph()->NewNode(
          common()->Phi(repr, return_count),
          static_cast<int>(ith_values.size()), &ith_values.front());
      values.push_back(ith_value_output);
    }
    for (Node* return_node : return_nodes) return_node->Kill();

    if (return_arity == 0) {
      // Void function, no value uses.
      ReplaceWithValue(call, mcgraph()->Dead(), effect_output, control_output);
    } else if (return_arity == 1) {
      // One return value. Just replace value uses of the call node with it.
      // Note: This will automatically detect and replace the IfSuccess node
      // correctly.
      ReplaceWithValue(call, values[0], effect_output, control_output);
    } else {
      // Multiple returns. We have to find the projections of the call node and
      // replace them with the returned values.
      for (Edge use_edge : call->use_edges()) {
        if (NodeProperties::IsValueEdge(use_edge)) {
          Node* use = use_edge.from();
          DCHECK_EQ(use->opcode(), IrOpcode::kProjection);
          ReplaceWithValue(use, values[ProjectionIndexOf(use->op())]);
        }
      }
      // All value inputs are replaced by the above loop, so it is ok to use
      // Dead() as a dummy for value replacement.
      ReplaceWithValue(call, mcgraph()->Dead(), effect_output, control_output);
    }
  } else {
    // The callee can never return. The call node and all its uses are dead.
    ReplaceWithValue(call, mcgraph()->Dead(), mcgraph()->Dead(),
                     mcgraph()->Dead());
  }
}

const wasm::WasmModule* WasmInliner::module() const { return env_->module; }

#undef TRACE

}  // namespace compiler
}  // namespace internal
}  // namespace v8
