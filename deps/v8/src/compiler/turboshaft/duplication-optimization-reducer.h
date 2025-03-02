// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_DUPLICATION_OPTIMIZATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_DUPLICATION_OPTIMIZATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"

namespace v8::internal::compiler::turboshaft {

// DuplicationOptimizationReducer introduces duplication where this can be
// beneficial for generated code. It should run late in the pipeline so that the
// duplication isn't optimized away by some other phases (such as GVN).
//
// In particular, it introduces duplication in 2 places:
//
// 1. Branch condition duplication: it tries to ensure that the condition nodes
// of branches are used only once (under some conditions). When it finds a
// branch node whose condition has multiples uses, this condition is duplicated.
//
// Doing this enables the InstructionSelector to generate more efficient code
// for branches. For instance, consider this code:
//
//     c = a + b;
//     if (c == 0) { /* some code */ }
//     if (c == 0) { /* more code */ }
//
// Then the generated code will be something like (using registers "ra" for "a"
// and "rb" for "b", and "rt" a temporary register):
//
//     add ra, rb  ; a + b
//     cmp ra, 0   ; a + b == 0
//     sete rt     ; rt = (a + b == 0)
//     cmp rt, 0   ; rt == 0
//     jz
//     ...
//     cmp rt, 0   ; rt == 0
//     jz
//
// As you can see, TurboFan materialized the == bit into a temporary register.
// However, since the "add" instruction sets the ZF flag (on x64), it can be
// used to determine wether the jump should be taken or not. The code we'd like
// to generate instead if thus:
//
//     add ra, rb
//     jnz
//     ...
//     add ra, rb
//     jnz
//
// However, this requires to generate twice the instruction "add ra, rb". Due to
// how virtual registers are assigned in TurboFan (there is a map from node ID
// to virtual registers), both "add" instructions will use the same virtual
// register as output, which will break SSA.
//
// In order to overcome this issue, BranchConditionDuplicator duplicates branch
// conditions that are used more than once, so that they can be generated right
// before each branch without worrying about breaking SSA.
//
// 2. Load/Store flexible second operand duplication: on Arm64, it tries to
// duplicate the "index" input of Loads/Stores when it's a shift by a constant.
// This allows the Instruction Selector to compute said shift using a flexible
// second operand, which in most cases on recent Arm64 CPUs should be for free.

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class DuplicationOptimizationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(DuplucationOptimization)

  V<None> REDUCE_INPUT_GRAPH(Branch)(V<None> ig_index, const BranchOp& branch) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphBranch(ig_index, branch);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const Operation& cond = __ input_graph().Get(branch.condition());
    V<Word32> new_cond;
    if (!MaybeDuplicateCond(cond, branch.condition(), &new_cond)) {
      goto no_change;
    }

    DCHECK(new_cond.valid());
    __ Branch(new_cond, __ MapToNewGraph(branch.if_true),
              __ MapToNewGraph(branch.if_false), branch.hint);
    return V<None>::Invalid();
  }

  V<Any> REDUCE_INPUT_GRAPH(Select)(V<Any> ig_index, const SelectOp& select) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphSelect(ig_index, select);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const Operation& cond = __ input_graph().Get(select.cond());
    V<Word32> new_cond;
    if (!MaybeDuplicateCond(cond, select.cond(), &new_cond)) goto no_change;

    DCHECK(new_cond.valid());
    return __ Select(new_cond, __ MapToNewGraph(select.vtrue()),
                     __ MapToNewGraph(select.vfalse()), select.rep, select.hint,
                     select.implem);
  }

#if V8_TARGET_ARCH_ARM64
  // TODO(dmercadier): duplicating a shift to use a flexible second operand is
  // not always worth it; this depends mostly on the CPU, the kind of shift, and
  // the size of the loaded/stored data. Ideally, we would have cost models for
  // all the CPUs we target, and use those to decide to duplicate shifts or not.
  OpIndex REDUCE(Load)(OpIndex base, OptionalOpIndex index, LoadOp::Kind kind,
                       MemoryRepresentation loaded_rep,
                       RegisterRepresentation result_rep, int32_t offset,
                       uint8_t element_size_log2) {
    if (offset == 0 && element_size_log2 == 0 && index.valid()) {
      index = MaybeDuplicateOutputGraphShift(index.value());
    }
    return Next::ReduceLoad(base, index, kind, loaded_rep, result_rep, offset,
                            element_size_log2);
  }

  OpIndex REDUCE(Store)(OpIndex base, OptionalOpIndex index, OpIndex value,
                        StoreOp::Kind kind, MemoryRepresentation stored_rep,
                        WriteBarrierKind write_barrier, int32_t offset,
                        uint8_t element_size_log2,
                        bool maybe_initializing_or_transitioning,
                        IndirectPointerTag maybe_indirect_pointer_tag) {
    if (offset == 0 && element_size_log2 == 0 && index.valid()) {
      index = MaybeDuplicateOutputGraphShift(index.value());
    }
    return Next::ReduceStore(base, index, value, kind, stored_rep,
                             write_barrier, offset, element_size_log2,
                             maybe_initializing_or_transitioning,
                             maybe_indirect_pointer_tag);
  }
#endif

 private:
  bool MaybeDuplicateCond(const Operation& cond, OpIndex input_idx,
                          V<Word32>* new_cond) {
    if (cond.saturated_use_count.IsOne()) return false;

    switch (cond.opcode) {
      case Opcode::kComparison:
        *new_cond =
            MaybeDuplicateComparison(cond.Cast<ComparisonOp>(), input_idx);
        break;
      case Opcode::kWordBinop:
        *new_cond =
            MaybeDuplicateWordBinop(cond.Cast<WordBinopOp>(), input_idx);
        break;
      case Opcode::kShift:
        *new_cond = MaybeDuplicateShift(cond.Cast<ShiftOp>(), input_idx);
        break;
      default:
        return false;
    }
    return new_cond->valid();
  }

  bool MaybeCanDuplicateGenericBinop(OpIndex input_idx, OpIndex left,
                                     OpIndex right) {
    if (__ input_graph().Get(left).saturated_use_count.IsOne() &&
        __ input_graph().Get(right).saturated_use_count.IsOne()) {
      // We don't duplicate binops when all of their inputs are used a single
      // time (this would increase register pressure by keeping 2 values alive
      // instead of 1).
      return false;
    }
    OpIndex binop_output_idx = __ MapToNewGraph(input_idx);
    if (__ Get(binop_output_idx).saturated_use_count.IsZero()) {
      // This is the 1st use of {binop} in the output graph, so there is no need
      // to duplicate it just yet.
      return false;
    }
    return true;
  }

  OpIndex MaybeDuplicateWordBinop(const WordBinopOp& binop, OpIndex input_idx) {
    if (!MaybeCanDuplicateGenericBinop(input_idx, binop.left(),
                                       binop.right())) {
      return OpIndex::Invalid();
    }

    switch (binop.kind) {
      case WordBinopOp::Kind::kSignedDiv:
      case WordBinopOp::Kind::kUnsignedDiv:
      case WordBinopOp::Kind::kSignedMod:
      case WordBinopOp::Kind::kUnsignedMod:
        // These operations are somewhat expensive, and duplicating them is
        // probably not worth it.
        return OpIndex::Invalid();
      default:
        break;
    }

    DisableValueNumbering disable_gvn(this);
    return __ WordBinop(__ MapToNewGraph(binop.left()),
                        __ MapToNewGraph(binop.right()), binop.kind, binop.rep);
  }

  V<Word32> MaybeDuplicateComparison(const ComparisonOp& comp,
                                     OpIndex input_idx) {
    if (!MaybeCanDuplicateGenericBinop(input_idx, comp.left(), comp.right())) {
      return {};
    }

    DisableValueNumbering disable_gvn(this);
    return __ Comparison(__ MapToNewGraph(comp.left()),
                         __ MapToNewGraph(comp.right()), comp.kind, comp.rep);
  }

  OpIndex MaybeDuplicateShift(const ShiftOp& shift, OpIndex input_idx) {
    if (!MaybeCanDuplicateGenericBinop(input_idx, shift.left(),
                                       shift.right())) {
      return OpIndex::Invalid();
    }

    DisableValueNumbering disable_gvn(this);
    return __ Shift(__ MapToNewGraph(shift.left()),
                    __ MapToNewGraph(shift.right()), shift.kind, shift.rep);
  }

  OpIndex MaybeDuplicateOutputGraphShift(OpIndex index) {
    V<Word> shifted;
    int shifted_by;
    ShiftOp::Kind shift_kind;
    WordRepresentation shift_rep;
    if (__ matcher().MatchConstantShift(index, &shifted, &shift_kind,
                                        &shift_rep, &shifted_by) &&
        !__ matcher().Get(index).saturated_use_count.IsZero()) {
      // We don't check the use count of {shifted}, because it might have uses
      // in the future that haven't been emitted yet.
      DisableValueNumbering disable_gvn(this);
      return __ Shift(shifted, __ Word32Constant(shifted_by), shift_kind,
                      shift_rep);
    }
    return index;
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_DUPLICATION_OPTIMIZATION_REDUCER_H_
