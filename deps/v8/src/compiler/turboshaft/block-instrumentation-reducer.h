// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_BLOCK_INSTRUMENTATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_BLOCK_INSTRUMENTATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"

namespace v8::internal::compiler::turboshaft {

#include "define-assembler-macros.inc"

namespace detail {
Handle<HeapObject> CreateCountersArray(Isolate* isolate);
}  // namespace detail

template <typename Next>
class BlockInstrumentationReducer
    : public UniformReducerAdapter<BlockInstrumentationReducer, Next> {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(BlockInstrumentation)
  using Adapter = UniformReducerAdapter<BlockInstrumentationReducer, Next>;

  BlockInstrumentationReducer() {
    DCHECK_NOT_NULL(data_);
    if (on_heap_counters_) {
      counters_array_handle_ = detail::CreateCountersArray(isolate_);
    }
  }

  void Bind(Block* new_block) {
    Next::Bind(new_block);

    const int block_number = new_block->index().id();
    data_->SetBlockId(block_number, block_number);

    // Reset counter.
    operations_emitted_in_current_block_ = 0;
  }

  template <Opcode opcode, typename Continuation, typename... Args>
  OpIndex ReduceOperation(Args... args) {
    // Those operations must be skipped here because we want to keep them at the
    // beginning of their blocks.
    static_assert(opcode != Opcode::kCatchBlockBegin);
    static_assert(opcode != Opcode::kDidntThrow);
    static_assert(opcode != Opcode::kParameter);

    if (0 == operations_emitted_in_current_block_++) {
      // If this is the first (non-skipped) operation in this block, emit
      // instrumentation.
      const int block_number = __ current_block() -> index().id();
      EmitBlockInstrumentation(block_number);
    }
    return Continuation{this}.Reduce(args...);
  }

  V<Object> REDUCE(Parameter)(int32_t parameter_index,
                              RegisterRepresentation rep,
                              const char* debug_name) {
    // Skip generic callback as we don't want to emit instrumentation BEFORE
    // this operation.
    return Next::ReduceParameter(parameter_index, rep, debug_name);
  }

  V<Any> REDUCE(CatchBlockBegin)() {
    // Skip generic callback as we don't want to emit instrumentation BEFORE
    // this operation.
    return Next::ReduceCatchBlockBegin();
  }

  V<Any> REDUCE(DidntThrow)(
      V<Any> throwing_operation, bool has_catch_block,
      const base::Vector<const RegisterRepresentation>* results_rep,
      OpEffects throwing_op_effects) {
    // Skip generic callback as we don't want to emit instrumentation BEFORE
    // this operation.
    return Next::ReduceDidntThrow(throwing_operation, has_catch_block,
                                  results_rep, throwing_op_effects);
  }

  V<Word32> LoadCounterValue(int block_number) {
    int offset_to_counter_value = block_number * kInt32Size;
    if (on_heap_counters_) {
      offset_to_counter_value += ByteArray::kHeaderSize;
      // Allocation is disallowed here, so rather than referring to an actual
      // counters array, create a reference to a special marker object. This
      // object will get fixed up later in the constants table (see
      // PatchBasicBlockCountersReference). An important and subtle point: we
      // cannot use the root handle basic_block_counters_marker_handle() and
      // must create a new separate handle. Otherwise
      // MacroAssemblerBase::IndirectLoadConstant would helpfully emit a
      // root-relative load rather than putting this value in the constants
      // table where we expect it to be for patching.
      V<HeapObject> counter_array = __ HeapConstant(counters_array_handle_);
      return __ Load(counter_array, LoadOp::Kind::TaggedBase(),
                     MemoryRepresentation::Uint32(), offset_to_counter_value);
    } else {
      V<WordPtr> counter_array =
          __ WordPtrConstant(reinterpret_cast<uintptr_t>(data_->counts()));
      return __ LoadOffHeap(counter_array, offset_to_counter_value,
                            MemoryRepresentation::Uint32());
    }
  }

  void StoreCounterValue(int block_number, V<Word32> value) {
    int offset_to_counter_value = block_number * kInt32Size;
    if (on_heap_counters_) {
      offset_to_counter_value += ByteArray::kHeaderSize;
      // Allocation is disallowed here, so rather than referring to an actual
      // counters array, create a reference to a special marker object. This
      // object will get fixed up later in the constants table (see
      // PatchBasicBlockCountersReference). An important and subtle point: we
      // cannot use the root handle basic_block_counters_marker_handle() and
      // must create a new separate handle. Otherwise
      // MacroAssemblerBase::IndirectLoadConstant would helpfully emit a
      // root-relative load rather than putting this value in the constants
      // table where we expect it to be for patching.
      V<HeapObject> counter_array = __ HeapConstant(counters_array_handle_);
      __ Store(counter_array, value, StoreOp::Kind::TaggedBase(),
               MemoryRepresentation::Uint32(),
               WriteBarrierKind::kNoWriteBarrier, offset_to_counter_value);
    } else {
      V<WordPtr> counter_array =
          __ WordPtrConstant(reinterpret_cast<uintptr_t>(data_->counts()));
      __ StoreOffHeap(counter_array, value, MemoryRepresentation::Uint32(),
                      offset_to_counter_value);
    }
  }

  void EmitBlockInstrumentation(int block_number) {
    // Load the current counter value from the array.
    V<Word32> value = LoadCounterValue(block_number);

    // Increment the counter value.
    V<Word32> incremented_value = __ Word32Add(value, 1);

    // Branchless saturation, because we don't want to introduce additional
    // control flow here.
    V<Word32> overflow = __ Uint32LessThan(incremented_value, value);
    V<Word32> overflow_mask = __ Word32Sub(0, overflow);
    V<Word32> saturated_value =
        __ Word32BitwiseOr(incremented_value, overflow_mask);

    // Store the incremented counter value back into the array.
    StoreCounterValue(block_number, saturated_value);
  }

  V<None> REDUCE_INPUT_GRAPH(Branch)(V<None> ig_index, const BranchOp& branch) {
    const int true_id = branch.if_true->index().id();
    const int false_id = branch.if_false->index().id();
    data_->AddBranch(true_id, false_id);
    return Next::ReduceInputGraphBranch(ig_index, branch);
  }

 private:
  Isolate* isolate_ = __ data() -> isolate();
  BasicBlockProfilerData* data_ = __ data() -> info()->profiler_data();
  const bool on_heap_counters_ =
      isolate_ && isolate_->IsGeneratingEmbeddedBuiltins();
  size_t operations_emitted_in_current_block_ = 0;
  Handle<HeapObject> counters_array_handle_;
};

#include "undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_BLOCK_INSTRUMENTATION_REDUCER_H_
