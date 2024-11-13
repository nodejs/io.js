// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_BASELINE_COMPILER_H_
#define V8_BASELINE_BASELINE_COMPILER_H_

#include "src/base/logging.h"
#include "src/base/pointer-with-payload.h"
#include "src/base/threaded-list.h"
#include "src/base/vlq.h"
#include "src/baseline/baseline-assembler.h"
#include "src/execution/local-isolate.h"
#include "src/handles/handles.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/interpreter-intrinsics.h"
#include "src/logging/counters.h"
#include "src/objects/map.h"
#include "src/objects/tagged-index.h"
#include "src/utils/bit-vector.h"

namespace v8 {
namespace internal {

class BytecodeArray;

namespace baseline {

class BytecodeOffsetTableBuilder {
 public:
  void AddPosition(size_t pc_offset) {
    size_t pc_diff = pc_offset - previous_pc_;
    DCHECK_GE(pc_diff, 0);
    DCHECK_LE(pc_diff, std::numeric_limits<uint32_t>::max());
    base::VLQEncodeUnsigned(&bytes_, static_cast<uint32_t>(pc_diff));
    previous_pc_ = pc_offset;
  }

  template <typename IsolateT>
  Handle<TrustedByteArray> ToBytecodeOffsetTable(IsolateT* isolate);

  void Reserve(size_t size) { bytes_.reserve(size); }

 private:
  size_t previous_pc_ = 0;
  std::vector<uint8_t> bytes_;
};

class BaselineCompiler {
 public:
  explicit BaselineCompiler(LocalIsolate* local_isolate,
                            Handle<SharedFunctionInfo> shared_function_info,
                            Handle<BytecodeArray> bytecode);

  void GenerateCode();
  MaybeHandle<Code> Build();
  static int EstimateInstructionSize(Tagged<BytecodeArray> bytecode);

 private:
  void Prologue();
  void PrologueFillFrame();
  void PrologueHandleOptimizationState(Register feedback_vector);

  void PreVisitSingleBytecode();
  void VisitSingleBytecode();

  void VerifyFrame();
  void VerifyFrameSize();

  // Register operands.
  interpreter::Register RegisterOperand(int operand_index);
  void LoadRegister(Register output, int operand_index);
  void StoreRegister(int operand_index, Register value);
  void StoreRegisterPair(int operand_index, Register val0, Register val1);

  // Constant pool operands.
  template <typename Type>
  Handle<Type> Constant(int operand_index);
  Tagged<Smi> ConstantSmi(int operand_index);
  template <typename Type>
  void LoadConstant(Register output, int operand_index);

  // Immediate value operands.
  uint32_t Uint(int operand_index);
  int32_t Int(int operand_index);
  uint32_t Index(int operand_index);
  uint32_t Flag8(int operand_index);
  uint32_t Flag16(int operand_index);
  uint32_t RegisterCount(int operand_index);
  Tagged<TaggedIndex> IndexAsTagged(int operand_index);
  Tagged<TaggedIndex> UintAsTagged(int operand_index);
  Tagged<Smi> IndexAsSmi(int operand_index);
  Tagged<Smi> IntAsSmi(int operand_index);
  Tagged<Smi> UintAsSmi(int operand_index);
  Tagged<Smi> Flag8AsSmi(int operand_index);
  Tagged<Smi> Flag16AsSmi(int operand_index);

  // Jump helpers.
  Label* NewLabel();
  Label* BuildForwardJumpLabel();
  enum StackCheckBehavior {
    kEnableStackCheck,
    kDisableStackCheck,
  };
  void UpdateInterruptBudgetAndJumpToLabel(
      int weight, Label* label, Label* skip_interrupt_label,
      StackCheckBehavior stack_check_behavior);
  void JumpIfRoot(RootIndex root);
  void JumpIfNotRoot(RootIndex root);

  // Feedback vector.
  MemOperand FeedbackVector();
  void LoadFeedbackVector(Register output);
  void LoadClosureFeedbackArray(Register output);

  // Position mapping.
  void AddPosition();

  // Misc. helpers.

  // Select the root boolean constant based on the jump in the given
  // `jump_func` -- the function should jump to the given label if we want to
  // select "true", otherwise it should fall through.
  void SelectBooleanConstant(
      Register output, std::function<void(Label*, Label::Distance)> jump_func);

  // Jumps based on calling ToBoolean on kInterpreterAccumulatorRegister.
  void JumpIfToBoolean(bool do_jump_if_true, Label* label,
                       Label::Distance distance = Label::kFar);

  // Call helpers.
  template <Builtin kBuiltin, typename... Args>
  void CallBuiltin(Args... args);
  template <typename... Args>
  void CallRuntime(Runtime::FunctionId function, Args... args);

  template <Builtin kBuiltin, typename... Args>
  void TailCallBuiltin(Args... args);

  template <ConvertReceiverMode kMode, typename... Args>
  void BuildCall(uint32_t slot, uint32_t arg_count, Args... args);

#ifdef V8_TRACE_UNOPTIMIZED
  void TraceBytecode(Runtime::FunctionId function_id);
#endif

  // Single bytecode visitors.
#define DECLARE_VISITOR(name, ...) void Visit##name();
  BYTECODE_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

  // Intrinsic call visitors.
#define DECLARE_VISITOR(name, ...) \
  void VisitIntrinsic##name(interpreter::RegisterList args);
  INTRINSICS_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

  const interpreter::BytecodeArrayIterator& iterator() { return iterator_; }

  LocalIsolate* local_isolate_;
  RuntimeCallStats* stats_;
  Handle<SharedFunctionInfo> shared_function_info_;
  Handle<HeapObject> interpreter_data_;
  Handle<BytecodeArray> bytecode_;
  MacroAssembler masm_;
  BaselineAssembler basm_;
  interpreter::BytecodeArrayIterator iterator_;
  BytecodeOffsetTableBuilder bytecode_offset_table_builder_;
  Zone zone_;

  // Mark location as a jump target reachable via indirect branches, required
  // for CFI.
  enum class MarkAsIndirectJumpTarget { kNo, kYes };

  Label* EnsureLabel(int offset, MarkAsIndirectJumpTarget mark =
                                     MarkAsIndirectJumpTarget::kNo) {
    Label* label = &labels_[offset];
    if (!label_tags_.Contains(offset * 2)) {
      label_tags_.Add(offset * 2);
      new (label) Label();
    }
    if (mark == MarkAsIndirectJumpTarget::kYes) {
      MarkIndirectJumpTarget(offset);
    }
    return label;
  }
  bool IsJumpTarget(int offset) const {
    return label_tags_.Contains(offset * 2);
  }
  bool IsIndirectJumpTarget(int offset) const {
    return label_tags_.Contains(offset * 2 + 1);
  }
  void MarkIndirectJumpTarget(int offset) { label_tags_.Add(offset * 2 + 1); }

  Label* labels_;
  BitVector label_tags_;

#ifdef DEBUG
  friend class SaveAccumulatorScope;

  struct EffectState {
    bool may_have_deopted = false;
    bool accumulator_on_stack = false;
    bool safe_to_skip = false;

    void MayDeopt() {
      // If this check fails, you might need to update `BuiltinMayDeopt` if
      // applicable.
      DCHECK(!accumulator_on_stack);
      may_have_deopted = true;
    }

    void CheckEffect() { DCHECK(!may_have_deopted || safe_to_skip); }

    void clear() {
      DCHECK(!accumulator_on_stack);
      *this = EffectState();
    }
  } effect_state_;
#endif
};

class SaveAccumulatorScope final {
 public:
  SaveAccumulatorScope(BaselineCompiler* compiler,
                       BaselineAssembler* assembler);

  ~SaveAccumulatorScope();

 private:
#ifdef DEBUG
  BaselineCompiler* compiler_;
#endif
  BaselineAssembler* assembler_;
};

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_COMPILER_H_
