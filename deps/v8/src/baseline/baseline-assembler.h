// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_BASELINE_ASSEMBLER_H_
#define V8_BASELINE_BASELINE_ASSEMBLER_H_

#include "src/codegen/macro-assembler.h"
#include "src/interpreter/bytecode-register.h"
#include "src/objects/tagged-index.h"

namespace v8 {
namespace internal {
namespace baseline {

class BaselineAssembler {
 public:
  class ScratchRegisterScope;

  explicit BaselineAssembler(MacroAssembler* masm) : masm_(masm) {}
  inline static MemOperand RegisterFrameOperand(
      interpreter::Register interpreter_register);
  inline void RegisterFrameAddress(interpreter::Register interpreter_register,
                                   Register rscratch);
  inline MemOperand ContextOperand();
  inline MemOperand FunctionOperand();
  inline MemOperand FeedbackVectorOperand();
  inline MemOperand FeedbackCellOperand();

  inline void GetCode(LocalIsolate* isolate, CodeDesc* desc);
  inline int pc_offset() const;
  inline void CodeEntry() const;
  inline void ExceptionHandler() const;
  V8_INLINE void RecordComment(const char* string);
  inline void Trap();
  inline void DebugBreak();

  template <typename Field>
  inline void DecodeField(Register reg);

  inline void Bind(Label* label);
  // Marks the current position as a valid jump target on CFI enabled
  // architectures.
  inline void JumpTarget();
  inline void Jump(Label* target, Label::Distance distance = Label::kFar);
  inline void JumpIfRoot(Register value, RootIndex index, Label* target,
                         Label::Distance distance = Label::kFar);
  inline void JumpIfNotRoot(Register value, RootIndex index, Label* target,
                            Label ::Distance distance = Label::kFar);
  inline void JumpIfSmi(Register value, Label* target,
                        Label::Distance distance = Label::kFar);
  inline void JumpIfNotSmi(Register value, Label* target,
                           Label::Distance distance = Label::kFar);

  inline void TestAndBranch(Register value, int mask, Condition cc,
                            Label* target,
                            Label::Distance distance = Label::kFar);

  inline void JumpIf(Condition cc, Register lhs, const Operand& rhs,
                     Label* target, Label::Distance distance = Label::kFar);
#if V8_STATIC_ROOTS_BOOL
  // Fast JS_RECEIVER test which assumes to receive either a primitive object or
  // a js receiver.
  inline void JumpIfJSAnyIsPrimitive(Register heap_object, Label* target,
                                     Label::Distance distance = Label::kFar);
#endif
  inline void JumpIfObjectType(Condition cc, Register object,
                               InstanceType instance_type, Register map,
                               Label* target,
                               Label::Distance distance = Label::kFar);
  // Might not load the map into the scratch register.
  inline void JumpIfObjectTypeFast(Condition cc, Register object,
                                   InstanceType instance_type, Label* target,
                                   Label::Distance distance = Label::kFar);
  inline void JumpIfInstanceType(Condition cc, Register map,
                                 InstanceType instance_type, Label* target,
                                 Label::Distance distance = Label::kFar);
  inline void JumpIfPointer(Condition cc, Register value, MemOperand operand,
                            Label* target,
                            Label::Distance distance = Label::kFar);
  inline Condition CheckSmi(Register value);
  inline void JumpIfSmi(Condition cc, Register value, Tagged<Smi> smi,
                        Label* target, Label::Distance distance = Label::kFar);
  inline void JumpIfSmi(Condition cc, Register lhs, Register rhs, Label* target,
                        Label::Distance distance = Label::kFar);
  inline void JumpIfImmediate(Condition cc, Register left, int right,
                              Label* target,
                              Label::Distance distance = Label::kFar);
  inline void JumpIfTagged(Condition cc, Register value, MemOperand operand,
                           Label* target,
                           Label::Distance distance = Label::kFar);
  inline void JumpIfTagged(Condition cc, MemOperand operand, Register value,
                           Label* target,
                           Label::Distance distance = Label::kFar);
  inline void JumpIfByte(Condition cc, Register value, int32_t byte,
                         Label* target, Label::Distance distance = Label::kFar);

  inline void LoadMap(Register output, Register value);
  inline void LoadRoot(Register output, RootIndex index);
  inline void LoadNativeContextSlot(Register output, uint32_t index);

  inline void Move(Register output, Register source);
  inline void Move(Register output, MemOperand operand);
  inline void Move(Register output, Tagged<Smi> value);
  inline void Move(Register output, Tagged<TaggedIndex> value);
  inline void Move(Register output, interpreter::Register source);
  inline void Move(interpreter::Register output, Register source);
  inline void Move(Register output, RootIndex source);
  inline void Move(MemOperand output, Register source);
  inline void Move(Register output, ExternalReference reference);
  inline void Move(Register output, Handle<HeapObject> value);
  inline void Move(Register output, int32_t immediate);
  inline void MoveMaybeSmi(Register output, Register source);
  inline void MoveSmi(Register output, Register source);

  // Push the given values, in the given order. If the stack needs alignment
  // (looking at you Arm64), the stack is padded from the front (i.e. before the
  // first value is pushed).
  //
  // This supports pushing a RegisterList as the last value -- the list is
  // iterated and each interpreter Register is pushed.
  //
  // The total number of values pushed is returned. Note that this might be
  // different from sizeof(T...), specifically if there was a RegisterList.
  template <typename... T>
  inline int Push(T... vals);

  // Like Push(vals...), but pushes in reverse order, to support our reversed
  // order argument JS calling convention. Doesn't return the number of
  // arguments pushed though.
  //
  // Note that padding is still inserted before the first pushed value (i.e. the
  // last value).
  template <typename... T>
  inline void PushReverse(T... vals);

  // Pop values off the stack into the given registers.
  //
  // Note that this inserts into registers in the given order, i.e. in reverse
  // order if the registers were pushed. This means that to spill registers,
  // push and pop have to be in reverse order, e.g.
  //
  //     Push(r1, r2, ..., rN);
  //     ClobberRegisters();
  //     Pop(rN, ..., r2, r1);
  //
  // On stack-alignment architectures, any padding is popped off after the last
  // register. This the behaviour of Push, which means that the above code still
  // works even if the number of registers doesn't match stack alignment.
  template <typename... T>
  inline void Pop(T... registers);

  inline void CallBuiltin(Builtin builtin);
  inline void TailCallBuiltin(Builtin builtin);
  inline void CallRuntime(Runtime::FunctionId function, int nargs);

  inline void LoadTaggedField(Register output, Register source, int offset);
  inline void LoadTaggedSignedField(Register output, Register source,
                                    int offset);
  inline void LoadTaggedSignedFieldAndUntag(Register output, Register source,
                                            int offset);
  inline void LoadWord16FieldZeroExtend(Register output, Register source,
                                        int offset);
  inline void LoadWord8Field(Register output, Register source, int offset);
  inline void StoreTaggedSignedField(Register target, int offset,
                                     Tagged<Smi> value);
  inline void StoreTaggedFieldWithWriteBarrier(Register target, int offset,
                                               Register value);
  inline void StoreTaggedFieldNoWriteBarrier(Register target, int offset,
                                             Register value);
  inline void LoadFixedArrayElement(Register output, Register array,
                                    int32_t index);
  inline void LoadPrototype(Register prototype, Register object);

// Loads compressed pointer or loads from compressed pointer. This is because
// X64 supports complex addressing mode, pointer decompression can be done by
// [%compressed_base + %r1 + K].
#if V8_TARGET_ARCH_X64
  inline void LoadTaggedField(TaggedRegister output, Register source,
                              int offset);
  inline void LoadTaggedField(TaggedRegister output, TaggedRegister source,
                              int offset);
  inline void LoadTaggedField(Register output, TaggedRegister source,
                              int offset);
  inline void LoadFixedArrayElement(Register output, TaggedRegister array,
                                    int32_t index);
  inline void LoadFixedArrayElement(TaggedRegister output, TaggedRegister array,
                                    int32_t index);
#endif

  // Falls through and sets scratch_and_result to 0 on failure, jumps to
  // on_result on success.
  inline void TryLoadOptimizedOsrCode(Register scratch_and_result,
                                      Register feedback_vector,
                                      FeedbackSlot slot, Label* on_result,
                                      Label::Distance distance);

  // Loads the feedback cell from the function, and sets flags on add so that
  // we can compare afterward.
  inline void AddToInterruptBudgetAndJumpIfNotExceeded(
      int32_t weight, Label* skip_interrupt_label);
  inline void AddToInterruptBudgetAndJumpIfNotExceeded(
      Register weight, Label* skip_interrupt_label);

  // By default, the output register may be compressed on 64-bit architectures
  // that support pointer compression.
  enum class CompressionMode {
    kDefault,
    kForceDecompression,
  };
  inline void LdaContextSlot(
      Register context, uint32_t index, uint32_t depth,
      CompressionMode compression_mode = CompressionMode::kDefault);
  inline void StaContextSlot(Register context, Register value, uint32_t index,
                             uint32_t depth);
  inline void LdaModuleVariable(Register context, int cell_index,
                                uint32_t depth);
  inline void StaModuleVariable(Register context, Register value,
                                int cell_index, uint32_t depth);

  inline void IncrementSmi(MemOperand lhs);
  inline void SmiUntag(Register value);
  inline void SmiUntag(Register output, Register value);

  inline void Word32And(Register output, Register lhs, int rhs);

  inline void Switch(Register reg, int case_value_base, Label** labels,
                     int num_labels);

  // Register operands.
  inline void LoadRegister(Register output, interpreter::Register source);
  inline void StoreRegister(interpreter::Register output, Register value);

  // Frame values
  inline void LoadFunction(Register output);
  inline void LoadContext(Register output);
  inline void StoreContext(Register context);

  inline void LoadFeedbackCell(Register output);
  inline void AssertFeedbackCell(Register object);

#ifdef V8_ENABLE_CET_SHADOW_STACK
  // If CET shadow stack is enabled, reserves a few bytes as NOP that can be
  // patched later.
  inline void MaybeEmitPlaceHolderForDeopt();
#endif  // V8_ENABLE_CET_SHADOW_STACK

  inline static void EmitReturn(MacroAssembler* masm);

  MacroAssembler* masm() { return masm_; }

 private:
  MacroAssembler* masm_;
  ScratchRegisterScope* scratch_register_scope_ = nullptr;
};

class EnsureAccumulatorPreservedScope final {
 public:
  inline explicit EnsureAccumulatorPreservedScope(BaselineAssembler* assembler);

  inline ~EnsureAccumulatorPreservedScope();

 private:
  inline void AssertEqualToAccumulator(Register reg);

  BaselineAssembler* assembler_;
#ifdef V8_CODE_COMMENTS
  Assembler::CodeComment comment_;
#endif
};

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_ASSEMBLER_H_
