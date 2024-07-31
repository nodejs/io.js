// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/basic-block-instrumentor.h"

#include <sstream>

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/schedule.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

// Find the first place to insert new nodes in a block that's already been
// scheduled that won't upset the register allocator.
static NodeVector::iterator FindInsertionPoint(BasicBlock* block) {
  NodeVector::iterator i = block->begin();
  for (; i != block->end(); ++i) {
    const Operator* op = (*i)->op();
    if (OperatorProperties::IsBasicBlockBegin(op)) continue;
    switch (op->opcode()) {
      case IrOpcode::kParameter:
      case IrOpcode::kPhi:
      case IrOpcode::kEffectPhi:
        continue;
    }
    break;
  }
  return i;
}

static const Operator* IntPtrConstant(CommonOperatorBuilder* common,
                                      intptr_t value) {
  return kSystemPointerSize == 8
             ? common->Int64Constant(value)
             : common->Int32Constant(static_cast<int32_t>(value));
}

// TODO(dcarney): need to mark code as non-serializable.
static const Operator* PointerConstant(CommonOperatorBuilder* common,
                                       const void* ptr) {
  intptr_t ptr_as_int = reinterpret_cast<intptr_t>(ptr);
  return IntPtrConstant(common, ptr_as_int);
}

BasicBlockProfilerData* BasicBlockInstrumentor::Instrument(
    OptimizedCompilationInfo* info, Graph* graph, Schedule* schedule,
    Isolate* isolate) {
  // Basic block profiling disables concurrent compilation, so handle deref is
  // fine.
  AllowHandleDereference allow_handle_dereference;
  // Skip the exit block in profiles, since the register allocator can't handle
  // it and entry into it means falling off the end of the function anyway.
  size_t n_blocks = schedule->RpoBlockCount();
  BasicBlockProfilerData* data = BasicBlockProfiler::Get()->NewData(n_blocks);
  // Set the function name.
  data->SetFunctionName(info->GetDebugName());
  // Capture the schedule string before instrumentation.
  if (v8_flags.turbo_profiling_verbose) {
    std::ostringstream os;
    os << *schedule;
    data->SetSchedule(os);
  }
  // Check whether we should write counts to a JS heap object or to the
  // BasicBlockProfilerData directly. The JS heap object is only used for
  // builtins.
  bool on_heap_counters = isolate && isolate->IsGeneratingEmbeddedBuiltins();
  // Add the increment instructions to the start of every block.
  CommonOperatorBuilder common(graph->zone());
  MachineOperatorBuilder machine(graph->zone());
  Node* counters_array = nullptr;
  if (on_heap_counters) {
    // Allocation is disallowed here, so rather than referring to an actual
    // counters array, create a reference to a special marker object. This
    // object will get fixed up later in the constants table (see
    // PatchBasicBlockCountersReference). An important and subtle point: we
    // cannot use the root handle basic_block_counters_marker_handle() and must
    // create a new separate handle. Otherwise
    // MacroAssemblerBase::IndirectLoadConstant would helpfully emit a
    // root-relative load rather than putting this value in the constants table
    // where we expect it to be for patching.
    counters_array = graph->NewNode(common.HeapConstant(Handle<HeapObject>::New(
        ReadOnlyRoots(isolate).basic_block_counters_marker(), isolate)));
  } else {
    counters_array = graph->NewNode(PointerConstant(&common, data->counts()));
  }
  Node* zero = graph->NewNode(common.Int32Constant(0));
  Node* one = graph->NewNode(common.Int32Constant(1));
  BasicBlockVector* blocks = schedule->rpo_order();
  size_t block_number = 0;
  for (BasicBlockVector::iterator it = blocks->begin(); block_number < n_blocks;
       ++it, ++block_number) {
    BasicBlock* block = (*it);
    if (block == schedule->end()) continue;
    // Iteration is already in reverse post-order.
    DCHECK_EQ(block->rpo_number(), block_number);
    data->SetBlockId(block_number, block->id().ToInt());
    // It is unnecessary to wire effect and control deps for load and store
    // since this happens after scheduling.
    // Construct increment operation.
    int offset_to_counter_value = static_cast<int>(block_number) * kInt32Size;
    if (on_heap_counters) {
      offset_to_counter_value += ByteArray::kHeaderSize - kHeapObjectTag;
    }
    Node* offset_to_counter =
        graph->NewNode(IntPtrConstant(&common, offset_to_counter_value));
    Node* load =
        graph->NewNode(machine.Load(MachineType::Uint32()), counters_array,
                       offset_to_counter, graph->start(), graph->start());
    Node* inc = graph->NewNode(machine.Int32Add(), load, one);

    // Branchless saturation, because we've already run the scheduler, so
    // introducing extra control flow here would be surprising.
    Node* overflow = graph->NewNode(machine.Uint32LessThan(), inc, load);
    Node* overflow_mask = graph->NewNode(machine.Int32Sub(), zero, overflow);
    Node* saturated_inc =
        graph->NewNode(machine.Word32Or(), inc, overflow_mask);

    Node* store =
        graph->NewNode(machine.Store(StoreRepresentation(
                           MachineRepresentation::kWord32, kNoWriteBarrier)),
                       counters_array, offset_to_counter, saturated_inc,
                       graph->start(), graph->start());
    // Insert the new nodes.
    static const int kArraySize = 10;
    Node* to_insert[kArraySize] = {
        counters_array, zero, one,      offset_to_counter,
        load,           inc,  overflow, overflow_mask,
        saturated_inc,  store};
    // The first three Nodes are constant across all blocks.
    int insertion_start = block_number == 0 ? 0 : 3;
    NodeVector::iterator insertion_point = FindInsertionPoint(block);
    block->InsertNodes(insertion_point, &to_insert[insertion_start],
                       &to_insert[kArraySize]);
    // Tell the scheduler about the new nodes.
    for (int i = insertion_start; i < kArraySize; ++i) {
      schedule->SetBlockForNode(block, to_insert[i]);
    }
    // The exit block is not instrumented and so we must ignore that block
    // count.
    if (block->control() == BasicBlock::kBranch &&
        block->successors()[0] != schedule->end() &&
        block->successors()[1] != schedule->end()) {
      data->AddBranch(block->successors()[0]->id().ToInt(),
                      block->successors()[1]->id().ToInt());
    }
  }
  return data;
}

namespace {

void StoreBuiltinCallForNode(Node* n, Builtin builtin, int block_id,
                             BuiltinsCallGraph* bcc_profiler) {
  if (n == nullptr) return;
  IrOpcode::Value op = n->opcode();
  if (op == IrOpcode::kCall || op == IrOpcode::kTailCall) {
    const CallDescriptor* des = CallDescriptorOf(n->op());
    if (des->kind() == CallDescriptor::kCallCodeObject) {
      Node* callee = n->InputAt(0);
      Operator* op = const_cast<Operator*>(callee->op());
      if (op->opcode() == IrOpcode::kHeapConstant) {
        Handle<HeapObject> para = OpParameter<Handle<HeapObject>>(op);
        if (IsCode(*para)) {
          Handle<Code> code = Handle<Code>::cast(para);
          if (code->is_builtin()) {
            bcc_profiler->AddBuiltinCall(builtin, code->builtin_id(), block_id);
            return;
          }
        }
      }
    }
  }
}

}  // namespace

void BasicBlockCallGraphProfiler::StoreCallGraph(OptimizedCompilationInfo* info,
                                                 Schedule* schedule) {
  CHECK(Builtins::IsBuiltinId(info->builtin()));
  BasicBlockVector* blocks = schedule->rpo_order();
  size_t block_number = 0;
  size_t n_blocks = schedule->RpoBlockCount();
  for (BasicBlockVector::iterator it = blocks->begin(); block_number < n_blocks;
       ++it, ++block_number) {
    BasicBlock* block = (*it);
    if (block == schedule->end()) continue;
    // Iteration is already in reverse post-order.
    DCHECK_EQ(block->rpo_number(), block_number);
    int block_id = block->id().ToInt();

    BuiltinsCallGraph* profiler = BuiltinsCallGraph::Get();

    for (Node* node : *block) {
      StoreBuiltinCallForNode(node, info->builtin(), block_id, profiler);
    }

    BasicBlock::Control control = block->control();
    if (control != BasicBlock::kNone) {
      Node* cnt_node = block->control_input();
      StoreBuiltinCallForNode(cnt_node, info->builtin(), block_id, profiler);
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
