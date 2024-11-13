// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_ASSEMBLER_H_
#define V8_MAGLEV_MAGLEV_ASSEMBLER_H_

#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/maglev/maglev-code-gen-state.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

class Graph;
class MaglevAssembler;

inline ExternalReference SpaceAllocationTopAddress(Isolate* isolate,
                                                   AllocationType alloc_type) {
  if (alloc_type == AllocationType::kYoung) {
    return ExternalReference::new_space_allocation_top_address(isolate);
  }
  DCHECK_EQ(alloc_type, AllocationType::kOld);
  return ExternalReference::old_space_allocation_top_address(isolate);
}

inline ExternalReference SpaceAllocationLimitAddress(
    Isolate* isolate, AllocationType alloc_type) {
  if (alloc_type == AllocationType::kYoung) {
    return ExternalReference::new_space_allocation_limit_address(isolate);
  }
  DCHECK_EQ(alloc_type, AllocationType::kOld);
  return ExternalReference::old_space_allocation_limit_address(isolate);
}

inline Builtin AllocateBuiltin(AllocationType alloc_type) {
  if (alloc_type == AllocationType::kYoung) {
    return Builtin::kAllocateInYoungGeneration;
  }
  DCHECK_EQ(alloc_type, AllocationType::kOld);
  return Builtin::kAllocateInOldGeneration;
}

// Label allowed to be passed to deferred code.
class ZoneLabelRef {
 public:
  explicit ZoneLabelRef(Zone* zone) : label_(zone->New<Label>()) {}
  explicit inline ZoneLabelRef(MaglevAssembler* masm);

  static ZoneLabelRef UnsafeFromLabelPointer(Label* label) {
    // This is an unsafe operation, {label} must be zone allocated.
    return ZoneLabelRef(label);
  }

  Label* operator*() { return label_; }

 private:
  Label* label_;

  // Unsafe constructor. {label} must be zone allocated.
  explicit ZoneLabelRef(Label* label) : label_(label) {}
};

// The slot index is the offset from the frame pointer.
struct StackSlot {
  int32_t index;
};

// Helper for generating the platform-specific parts of map comparison
// operations.
class MapCompare {
 public:
  inline explicit MapCompare(MaglevAssembler* masm, Register object,
                             size_t map_count);

  inline void Generate(Handle<Map> map, Condition cond, Label* if_true,
                       Label::Distance distance = Label::kFar);
  inline Register GetObject() const { return object_; }
  inline Register GetMap();

  // For counting the temporaries needed by the above operations:
  static inline int TemporaryCount(size_t map_count);

 private:
  MaglevAssembler* masm_;
  const Register object_;
  const size_t map_count_;
  Register map_ = Register::no_reg();
};

class V8_EXPORT_PRIVATE MaglevAssembler : public MacroAssembler {
 public:
  class TemporaryRegisterScope;

  explicit MaglevAssembler(Isolate* isolate, MaglevCodeGenState* code_gen_state)
      : MacroAssembler(isolate, CodeObjectRequired::kNo),
        code_gen_state_(code_gen_state) {}

  static constexpr RegList GetAllocatableRegisters() {
#ifdef V8_TARGET_ARCH_ARM
    return kAllocatableGeneralRegisters - kMaglevExtraScratchRegister;
#else
    return kAllocatableGeneralRegisters;
#endif  // V8_TARGET_ARCH_ARM
  }

  static constexpr DoubleRegList GetAllocatableDoubleRegisters() {
    return kAllocatableDoubleRegisters;
  }

  inline MemOperand GetStackSlot(const compiler::AllocatedOperand& operand);
  inline MemOperand ToMemOperand(const compiler::InstructionOperand& operand);
  inline MemOperand ToMemOperand(const ValueLocation& location);

  inline Register GetFramePointer();

  inline int GetFramePointerOffsetForStackSlot(
      const compiler::AllocatedOperand& operand) {
    int index = operand.index();
    if (operand.representation() != MachineRepresentation::kTagged) {
      index += code_gen_state()->tagged_slots();
    }
    return GetFramePointerOffsetForStackSlot(index);
  }

  template <typename Dest, typename Source>
  inline void MoveRepr(MachineRepresentation repr, Dest dst, Source src);

  void Allocate(RegisterSnapshot register_snapshot, Register result,
                int size_in_bytes,
                AllocationType alloc_type = AllocationType::kYoung,
                AllocationAlignment alignment = kTaggedAligned);

  void Allocate(RegisterSnapshot register_snapshot, Register result,
                Register size_in_bytes,
                AllocationType alloc_type = AllocationType::kYoung,
                AllocationAlignment alignment = kTaggedAligned);

  void AllocateHeapNumber(RegisterSnapshot register_snapshot, Register result,
                          DoubleRegister value);

  void AllocateTwoByteString(RegisterSnapshot register_snapshot,
                             Register result, int length);

  void LoadSingleCharacterString(Register result, int char_code);
  void LoadSingleCharacterString(Register result, Register char_code,
                                 Register scratch);

  void EnsureWritableFastElements(RegisterSnapshot register_snapshot,
                                  Register elements, Register object,
                                  Register scratch);

  inline void BindJumpTarget(Label* label);
  inline void BindBlock(BasicBlock* block);

  inline Condition IsRootConstant(Input input, RootIndex root_index);

  inline void Branch(Condition condition, BasicBlock* if_true,
                     BasicBlock* if_false, BasicBlock* next_block);
  inline void Branch(Condition condition, Label* if_true,
                     Label::Distance true_distance, bool fallthrough_when_true,
                     Label* if_false, Label::Distance false_distance,
                     bool fallthrough_when_false);

  Register FromAnyToRegister(const Input& input, Register scratch);

  inline void LoadTaggedField(Register result, MemOperand operand);
  inline void LoadTaggedField(Register result, Register object, int offset);
  inline void LoadTaggedFieldWithoutDecompressing(Register result,
                                                  Register object, int offset);
  inline void LoadTaggedSignedField(Register result, MemOperand operand);
  inline void LoadTaggedSignedField(Register result, Register object,
                                    int offset);
  inline void LoadAndUntagTaggedSignedField(Register result, Register object,
                                            int offset);
  inline void LoadTaggedFieldByIndex(Register result, Register object,
                                     Register index, int scale, int offset);
  inline void LoadBoundedSizeFromObject(Register result, Register object,
                                        int offset);
  inline void LoadExternalPointerField(Register result, MemOperand operand);

  inline void LoadFixedArrayElement(Register result, Register array,
                                    Register index);
  inline void LoadFixedArrayElementWithoutDecompressing(Register result,
                                                        Register array,
                                                        Register index);
  inline void LoadFixedDoubleArrayElement(DoubleRegister result, Register array,
                                          Register index);
  inline void StoreFixedDoubleArrayElement(Register array, Register index,
                                           DoubleRegister value);

  inline void LoadSignedField(Register result, MemOperand operand,
                              int element_size);
  inline void LoadUnsignedField(Register result, MemOperand operand,
                                int element_size);
  template <typename BitField>
  inline void LoadBitField(Register result, MemOperand operand) {
    // Pick a load with the right size, which makes sure to read the whole
    // field.
    static constexpr int load_size =
        RoundUp<8>(BitField::kSize + BitField::kShift) / 8;
    // TODO(leszeks): If the shift is 8 or 16, we could have loaded from a
    // shifted address instead.
    LoadUnsignedField(result, operand, load_size);
    DecodeField<BitField>(result);
  }

  enum StoreMode { kField, kElement };
  enum ValueIsCompressed { kValueIsDecompressed, kValueIsCompressed };
  enum ValueCanBeSmi { kValueCannotBeSmi, kValueCanBeSmi };

  inline void SetSlotAddressForTaggedField(Register slot_reg, Register object,
                                           int offset);
  inline void SetSlotAddressForFixedArrayElement(Register slot_reg,
                                                 Register object,
                                                 Register index);

  template <StoreMode store_mode>
  using OffsetTypeFor = std::conditional_t<store_mode == kField, int, Register>;

  template <StoreMode store_mode>
  void CheckAndEmitDeferredWriteBarrier(Register object,
                                        OffsetTypeFor<store_mode> offset,
                                        Register value,
                                        RegisterSnapshot register_snapshot,
                                        ValueIsCompressed value_is_compressed,
                                        ValueCanBeSmi value_can_be_smi);

  void CheckAndEmitDeferredIndirectPointerWriteBarrier(
      Register object, int offset, Register value,
      RegisterSnapshot register_snapshot, IndirectPointerTag tag);

  // Preserves all registers that are in the register snapshot, but is otherwise
  // allowed to clobber both input registers if they are not in the snapshot.
  //
  // For maximum efficiency, prefer:
  //   * Having `object` == WriteBarrierDescriptor::ObjectRegister(),
  //   * Not having WriteBarrierDescriptor::SlotAddressRegister() in the
  //     register snapshot,
  //   * Not having `value` in the register snapshot, allowing it to be
  //     clobbered.
  void StoreTaggedFieldWithWriteBarrier(Register object, int offset,
                                        Register value,
                                        RegisterSnapshot register_snapshot,
                                        ValueIsCompressed value_is_compressed,
                                        ValueCanBeSmi value_can_be_smi);
  inline void StoreTaggedFieldNoWriteBarrier(Register object, int offset,
                                             Register value);
  inline void StoreTaggedSignedField(Register object, int offset,
                                     Register value);
  inline void StoreTaggedSignedField(Register object, int offset,
                                     Tagged<Smi> value);

  inline void StoreInt32Field(Register object, int offset, int32_t value);

#ifdef V8_ENABLE_SANDBOX

  void StoreTrustedPointerFieldWithWriteBarrier(
      Register object, int offset, Register value,
      RegisterSnapshot register_snapshot, IndirectPointerTag tag);
  inline void StoreTrustedPointerFieldNoWriteBarrier(Register object,
                                                     int offset,
                                                     Register value);
#endif  // V8_ENABLE_SANDBOX

  inline void StoreField(MemOperand operand, Register value, int element_size);
  inline void ReverseByteOrder(Register value, int element_size);

  inline void BuildTypedArrayDataPointer(Register data_pointer,
                                         Register object);
  inline MemOperand TypedArrayElementOperand(Register data_pointer,
                                             Register index, int element_size);
  inline MemOperand DataViewElementOperand(Register data_pointer,
                                           Register index);

  enum class CharCodeMaskMode { kValueIsInRange, kMustApplyMask };

  // Warning: Input registers {string} and {index} will be scratched.
  // {result} is allowed to alias with one the other 3 input registers.
  // {result} is an int32.
  void StringCharCodeOrCodePointAt(
      BuiltinStringPrototypeCharCodeOrCodePointAt::Mode mode,
      RegisterSnapshot& register_snapshot, Register result, Register string,
      Register index, Register scratch1, Register scratch2,
      Label* result_fits_one_byte);
  // Warning: Input {char_code} will be scratched.
  void StringFromCharCode(RegisterSnapshot register_snapshot,
                          Label* char_code_fits_one_byte, Register result,
                          Register char_code, Register scratch,
                          CharCodeMaskMode mask_mode);

  void ToBoolean(Register value, CheckType check_type, ZoneLabelRef is_true,
                 ZoneLabelRef is_false, bool fallthrough_when_true);

  void TestTypeOf(Register object,
                  interpreter::TestTypeOfFlags::LiteralFlag literal,
                  Label* if_true, Label::Distance true_distance,
                  bool fallthrough_when_true, Label* if_false,
                  Label::Distance false_distance, bool fallthrough_when_false);

  inline void SmiTagInt32AndJumpIfFail(Register dst, Register src, Label* fail,
                                       Label::Distance distance = Label::kFar);
  inline void SmiTagInt32AndJumpIfFail(Register reg, Label* fail,
                                       Label::Distance distance = Label::kFar);
  inline void SmiTagInt32AndJumpIfSuccess(
      Register dst, Register src, Label* success,
      Label::Distance distance = Label::kFar);
  inline void SmiTagInt32AndJumpIfSuccess(
      Register reg, Label* success, Label::Distance distance = Label::kFar);
  inline void UncheckedSmiTagInt32(Register dst, Register src);
  inline void UncheckedSmiTagInt32(Register reg);

  inline void SmiTagUint32AndJumpIfFail(Register dst, Register src, Label* fail,
                                        Label::Distance distance = Label::kFar);
  inline void SmiTagUint32AndJumpIfFail(Register reg, Label* fail,
                                        Label::Distance distance = Label::kFar);
  inline void SmiTagUint32AndJumpIfSuccess(
      Register dst, Register src, Label* success,
      Label::Distance distance = Label::kFar);
  inline void SmiTagUint32AndJumpIfSuccess(
      Register reg, Label* success, Label::Distance distance = Label::kFar);
  inline void UncheckedSmiTagUint32(Register dst, Register src);
  inline void UncheckedSmiTagUint32(Register reg);

  // Try to smi-tag {obj}. Result is thrown away.
  inline void CheckInt32IsSmi(Register obj, Label* fail,
                              Register scratch = Register::no_reg());

  // Add/Subtract a constant (not smi tagged) to a smi. Jump to {fail} if the
  // result doesn't fit.
  inline void SmiAddConstant(Register dst, Register src, int value, Label* fail,
                             Label::Distance distance = Label::kFar);
  inline void SmiAddConstant(Register reg, int value, Label* fail,
                             Label::Distance distance = Label::kFar);
  inline void SmiSubConstant(Register dst, Register src, int value, Label* fail,
                             Label::Distance distance = Label::kFar);
  inline void SmiSubConstant(Register reg, int value, Label* fail,
                             Label::Distance distance = Label::kFar);

  inline void MoveHeapNumber(Register dst, double value);

  void TruncateDoubleToInt32(Register dst, DoubleRegister src);
  void TryTruncateDoubleToInt32(Register dst, DoubleRegister src, Label* fail);
  void TryTruncateDoubleToUint32(Register dst, DoubleRegister src, Label* fail);

  void TryChangeFloat64ToIndex(Register result, DoubleRegister value,
                               Label* success, Label* fail);

  inline void DefineLazyDeoptPoint(LazyDeoptInfo* info);
  inline void DefineExceptionHandlerPoint(NodeBase* node);
  inline void DefineExceptionHandlerAndLazyDeoptPoint(NodeBase* node);

  template <typename Function, typename... Args>
  inline Label* MakeDeferredCode(Function&& deferred_code_gen, Args&&... args);
  template <typename Function, typename... Args>
  inline void JumpToDeferredIf(Condition cond, Function&& deferred_code_gen,
                               Args&&... args);
  void JumpIfNotCallable(Register object, Register scratch,
                         CheckType check_type, Label* target,
                         Label::Distance distance = Label::kFar);
  void JumpIfUndetectable(Register object, Register scratch,
                          CheckType check_type, Label* target,
                          Label::Distance distance = Label::kFar);
  void JumpIfNotUndetectable(Register object, Register scratch, CheckType,
                             Label* target,
                             Label::Distance distance = Label::kFar);
  template <typename NodeT>
  inline Label* GetDeoptLabel(NodeT* node, DeoptimizeReason reason);
  inline bool IsDeoptLabel(Label* label);
  inline void EmitEagerDeoptStress(Label* label);
  template <typename NodeT>
  inline void EmitEagerDeopt(NodeT* node, DeoptimizeReason reason);
  template <typename NodeT>
  inline void EmitEagerDeoptIf(Condition cond, DeoptimizeReason reason,
                               NodeT* node);
  template <typename NodeT>
  inline void EmitEagerDeoptIfNotEqual(DeoptimizeReason reason, NodeT* node);
  template <typename NodeT>
  inline void EmitEagerDeoptIfSmi(NodeT* node, Register object,
                                  DeoptimizeReason reason);
  template <typename NodeT>
  inline void EmitEagerDeoptIfNotSmi(NodeT* node, Register object,
                                     DeoptimizeReason reason);

  void MaterialiseValueNode(Register dst, ValueNode* value);

  inline void IncrementInt32(Register reg);
  inline void DecrementInt32(Register reg);
  inline void AddInt32(Register reg, int amount);
  inline void AndInt32(Register reg, int mask);
  inline void OrInt32(Register reg, int mask);
  inline void ShiftLeft(Register reg, int amount);
  inline void IncrementAddress(Register reg, int32_t delta);
  inline void LoadAddress(Register dst, MemOperand location);

  inline void Call(Label* target);

  inline void EmitEnterExitFrame(int extra_slots, StackFrame::Type frame_type,
                                 Register c_function, Register scratch);

  inline MemOperand StackSlotOperand(StackSlot slot);
  inline void Move(StackSlot dst, Register src);
  inline void Move(StackSlot dst, DoubleRegister src);
  inline void Move(Register dst, StackSlot src);
  inline void Move(DoubleRegister dst, StackSlot src);
  inline void Move(MemOperand dst, Register src);
  inline void Move(Register dst, MemOperand src);
  inline void Move(DoubleRegister dst, DoubleRegister src);
  inline void Move(Register dst, Tagged<Smi> src);
  inline void Move(Register dst, ExternalReference src);
  inline void Move(Register dst, Register src);
  inline void Move(Register dst, Tagged<TaggedIndex> i);
  inline void Move(Register dst, int32_t i);
  inline void Move(Register dst, uint32_t i);
  inline void Move(Register dst, IndirectPointerTag i);
  inline void Move(DoubleRegister dst, double n);
  inline void Move(DoubleRegister dst, Float64 n);
  inline void Move(Register dst, Handle<HeapObject> obj);

  inline void MoveTagged(Register dst, Handle<HeapObject> obj);

  inline void LoadMapForCompare(Register dst, Register obj);

  inline void LoadByte(Register dst, MemOperand src);

  inline void LoadFloat32(DoubleRegister dst, MemOperand src);
  inline void StoreFloat32(MemOperand dst, DoubleRegister src);
  inline void LoadFloat64(DoubleRegister dst, MemOperand src);
  inline void StoreFloat64(MemOperand dst, DoubleRegister src);

  inline void LoadUnalignedFloat64(DoubleRegister dst, Register base,
                                   Register index);
  inline void LoadUnalignedFloat64AndReverseByteOrder(DoubleRegister dst,
                                                      Register base,
                                                      Register index);
  inline void StoreUnalignedFloat64(Register base, Register index,
                                    DoubleRegister src);
  inline void ReverseByteOrderAndStoreUnalignedFloat64(Register base,
                                                       Register index,
                                                       DoubleRegister src);

  inline void SignExtend32To64Bits(Register dst, Register src);
  inline void NegateInt32(Register val);

  inline void ToUint8Clamped(Register result, DoubleRegister value, Label* min,
                             Label* max, Label* done);

  template <typename NodeT>
  inline void DeoptIfBufferDetached(Register array, Register scratch,
                                    NodeT* node);

  inline Condition IsCallableAndNotUndetectable(Register map, Register scratch);
  inline Condition IsNotCallableNorUndetactable(Register map, Register scratch);

  inline void LoadInstanceType(Register instance_type, Register heap_object);
  inline void JumpIfObjectType(Register heap_object, InstanceType type,
                               Label* target,
                               Label::Distance distance = Label::kFar);
  inline void JumpIfNotObjectType(Register heap_object, InstanceType type,
                                  Label* target,
                                  Label::Distance distance = Label::kFar);
  inline void AssertObjectType(Register heap_object, InstanceType type,
                               AbortReason reason);
  inline void BranchOnObjectType(Register heap_object, InstanceType type,
                                 Label* if_true, Label::Distance true_distance,
                                 bool fallthrough_when_true, Label* if_false,
                                 Label::Distance false_distance,
                                 bool fallthrough_when_false);

  inline void JumpIfObjectTypeInRange(Register heap_object,
                                      InstanceType lower_limit,
                                      InstanceType higher_limit, Label* target,
                                      Label::Distance distance = Label::kFar);
  inline void JumpIfObjectTypeNotInRange(
      Register heap_object, InstanceType lower_limit, InstanceType higher_limit,
      Label* target, Label::Distance distance = Label::kFar);
  inline void AssertObjectTypeInRange(Register heap_object,
                                      InstanceType lower_limit,
                                      InstanceType higher_limit,
                                      AbortReason reason);
  inline void BranchOnObjectTypeInRange(
      Register heap_object, InstanceType lower_limit, InstanceType higher_limit,
      Label* if_true, Label::Distance true_distance, bool fallthrough_when_true,
      Label* if_false, Label::Distance false_distance,
      bool fallthrough_when_false);

#if V8_STATIC_ROOTS_BOOL
  inline void JumpIfObjectInRange(Register heap_object, Tagged_t lower_limit,
                                  Tagged_t higher_limit, Label* target,
                                  Label::Distance distance = Label::kFar);
  inline void JumpIfObjectNotInRange(Register heap_object, Tagged_t lower_limit,
                                     Tagged_t higher_limit, Label* target,
                                     Label::Distance distance = Label::kFar);
  inline void AssertObjectInRange(Register heap_object, Tagged_t lower_limit,
                                  Tagged_t higher_limit, AbortReason reason);
#endif

  inline void JumpIfJSAnyIsNotPrimitive(Register heap_object, Label* target,
                                        Label::Distance distance = Label::kFar);

  inline void JumpIfStringMap(Register map, Label* target,
                              Label::Distance distance = Label::kFar,
                              bool jump_if_true = true);
  inline void JumpIfString(Register heap_object, Label* target,
                           Label::Distance distance = Label::kFar);
  inline void JumpIfNotString(Register heap_object, Label* target,
                              Label::Distance distance = Label::kFar);
  inline void CheckJSAnyIsStringAndBranch(Register heap_object, Label* if_true,
                                          Label::Distance true_distance,
                                          bool fallthrough_when_true,
                                          Label* if_false,
                                          Label::Distance false_distance,
                                          bool fallthrough_when_false);

  inline void CompareMapWithRoot(Register object, RootIndex index,
                                 Register scratch);

  inline void CompareInstanceType(Register map, InstanceType instance_type);
  inline void CompareInstanceTypeRange(Register map, InstanceType lower_limit,
                                       InstanceType higher_limit);
  inline void CompareInstanceTypeRange(Register map, Register instance_type_out,
                                       InstanceType lower_limit,
                                       InstanceType higher_limit);

  inline void CompareTaggedAndJumpIf(Register reg, Tagged<Smi> smi,
                                     Condition cond, Label* target,
                                     Label::Distance distance = Label::kFar);
  inline void CompareTaggedAndJumpIf(Register reg, Handle<HeapObject> obj,
                                     Condition cond, Label* target,
                                     Label::Distance distance = Label::kFar);
  inline void CompareTaggedAndJumpIf(Register src1, Register src2,
                                     Condition cond, Label* target,
                                     Label::Distance distance = Label::kFar);

  inline void CompareFloat64AndJumpIf(DoubleRegister src1, DoubleRegister src2,
                                      Condition cond, Label* target,
                                      Label* nan_failed,
                                      Label::Distance distance = Label::kFar);
  inline void CompareFloat64AndBranch(DoubleRegister src1, DoubleRegister src2,
                                      Condition cond, BasicBlock* if_true,
                                      BasicBlock* if_false,
                                      BasicBlock* next_block,
                                      BasicBlock* nan_failed);
  inline void PrepareCallCFunction(int num_reg_arguments,
                                   int num_double_registers = 0);

  inline void CallSelf();
  inline void CallBuiltin(Builtin builtin);
  template <Builtin kBuiltin, typename... Args>
  inline void CallBuiltin(Args&&... args);
  inline void CallRuntime(Runtime::FunctionId fid);
  inline void CallRuntime(Runtime::FunctionId fid, int num_args);

  inline void Jump(Label* target, Label::Distance distance = Label::kFar);
  inline void JumpToDeopt(Label* target);
  inline void JumpIf(Condition cond, Label* target,
                     Label::Distance distance = Label::kFar);

  inline void JumpIfRoot(Register with, RootIndex index, Label* if_equal,
                         Label::Distance distance = Label::kFar);
  inline void JumpIfNotRoot(Register with, RootIndex index, Label* if_not_equal,
                            Label::Distance distance = Label::kFar);
  inline void JumpIfSmi(Register src, Label* on_smi,
                        Label::Distance near_jump = Label::kFar);
  inline void JumpIfNotSmi(Register src, Label* on_not_smi,
                           Label::Distance near_jump = Label::kFar);
  inline void JumpIfByte(Condition cc, Register value, int32_t byte,
                         Label* target, Label::Distance distance = Label::kFar);

  inline void JumpIfHoleNan(DoubleRegister value, Register scratch,
                            Label* target,
                            Label::Distance distance = Label::kFar);
  inline void JumpIfNotHoleNan(DoubleRegister value, Register scratch,
                               Label* target,
                               Label::Distance distance = Label::kFar);
  inline void JumpIfNan(DoubleRegister value, Label* target,
                        Label::Distance distance = Label::kFar);
  inline void JumpIfNotNan(DoubleRegister value, Label* target,
                           Label::Distance distance = Label::kFar);
  inline void JumpIfNotHoleNan(MemOperand operand, Label* target,
                               Label::Distance distance = Label::kFar);

  inline void CompareInt32AndJumpIf(Register r1, Register r2, Condition cond,
                                    Label* target,
                                    Label::Distance distance = Label::kFar);
  inline void CompareIntPtrAndJumpIf(Register r1, Register r2, Condition cond,
                                     Label* target,
                                     Label::Distance distance = Label::kFar);
  inline void CompareInt32AndJumpIf(Register r1, int32_t value, Condition cond,
                                    Label* target,
                                    Label::Distance distance = Label::kFar);
  inline void CompareInt32AndBranch(Register r1, int32_t value, Condition cond,
                                    BasicBlock* if_true, BasicBlock* if_false,
                                    BasicBlock* next_block);
  inline void CompareInt32AndBranch(Register r1, Register r2, Condition cond,
                                    BasicBlock* if_true, BasicBlock* if_false,
                                    BasicBlock* next_block);
  inline void CompareInt32AndBranch(Register r1, int32_t value, Condition cond,
                                    Label* if_true,
                                    Label::Distance true_distance,
                                    bool fallthrough_when_true, Label* if_false,
                                    Label::Distance false_distance,
                                    bool fallthrough_when_false);
  inline void CompareInt32AndBranch(Register r1, Register r2, Condition cond,
                                    Label* if_true,
                                    Label::Distance true_distance,
                                    bool fallthrough_when_true, Label* if_false,
                                    Label::Distance false_distance,
                                    bool fallthrough_when_false);
  inline void CompareInt32AndAssert(Register r1, Register r2, Condition cond,
                                    AbortReason reason);
  inline void CompareInt32AndAssert(Register r1, int32_t value, Condition cond,
                                    AbortReason reason);
  inline void CompareSmiAndJumpIf(Register r1, Tagged<Smi> value,
                                  Condition cond, Label* target,
                                  Label::Distance distance = Label::kFar);
  inline void CompareSmiAndAssert(Register r1, Tagged<Smi> value,
                                  Condition cond, AbortReason reason);
  inline void CompareByteAndJumpIf(MemOperand left, int8_t right,
                                   Condition cond, Register scratch,
                                   Label* target,
                                   Label::Distance distance = Label::kFar);

  inline void CompareDoubleAndJumpIfZeroOrNaN(
      DoubleRegister reg, Label* target,
      Label::Distance distance = Label::kFar);
  inline void CompareDoubleAndJumpIfZeroOrNaN(
      MemOperand operand, Label* target,
      Label::Distance distance = Label::kFar);

  inline void TestInt32AndJumpIfAnySet(Register r1, int32_t mask, Label* target,
                                       Label::Distance distance = Label::kFar);
  inline void TestInt32AndJumpIfAnySet(MemOperand operand, int32_t mask,
                                       Label* target,
                                       Label::Distance distance = Label::kFar);

  inline void TestInt32AndJumpIfAllClear(
      Register r1, int32_t mask, Label* target,
      Label::Distance distance = Label::kFar);
  inline void TestInt32AndJumpIfAllClear(
      MemOperand operand, int32_t mask, Label* target,
      Label::Distance distance = Label::kFar);

  inline void Int32ToDouble(DoubleRegister result, Register src);
  inline void Uint32ToDouble(DoubleRegister result, Register src);
  inline void SmiToDouble(DoubleRegister result, Register smi);

  inline void StringLength(Register result, Register string);

  // The registers WriteBarrierDescriptor::ObjectRegister and
  // WriteBarrierDescriptor::SlotAddressRegister can be clobbered.
  void StoreFixedArrayElementWithWriteBarrier(
      Register array, Register index, Register value,
      RegisterSnapshot register_snapshot);
  inline void StoreFixedArrayElementNoWriteBarrier(Register array,
                                                   Register index,
                                                   Register value);

  // TODO(victorgomes): Import baseline Pop(T...) methods.
  inline void Pop(Register dst);
  using MacroAssembler::Pop;

  template <typename... T>
  inline void Push(T... vals);
  template <typename... T>
  inline void PushReverse(T... vals);

  void OSRPrologue(Graph* graph);
  void Prologue(Graph* graph);

  inline void FinishCode();

  inline void AssertStackSizeCorrect();
  inline Condition FunctionEntryStackCheck(int stack_check_offset);

  inline void SetMapAsRoot(Register object, RootIndex map);

  inline void LoadHeapNumberValue(DoubleRegister result, Register heap_number);
  inline void LoadHeapNumberOrOddballValue(DoubleRegister result,
                                           Register object);

  void LoadDataField(const PolymorphicAccessInfo& access_info, Register result,
                     Register object, Register scratch);

  void MaybeEmitDeoptBuiltinsCall(size_t eager_deopt_count,
                                  Label* eager_deopt_entry,
                                  size_t lazy_deopt_count,
                                  Label* lazy_deopt_entry);

  void GenerateCheckConstTrackingLetCellFooter(Register context, Register data,
                                               int index, Label* done);

  void TryMigrateInstance(Register object, RegisterSnapshot& register_snapshot,
                          Label* fail);

  compiler::NativeContextRef native_context() const {
    return code_gen_state()->broker()->target_native_context();
  }

  MaglevCodeGenState* code_gen_state() const { return code_gen_state_; }
  MaglevSafepointTableBuilder* safepoint_table_builder() const {
    return code_gen_state()->safepoint_table_builder();
  }
  MaglevCompilationInfo* compilation_info() const {
    return code_gen_state()->compilation_info();
  }

  TemporaryRegisterScope* scratch_register_scope() const {
    return scratch_register_scope_;
  }

#ifdef DEBUG
  bool allow_allocate() const { return allow_allocate_; }
  void set_allow_allocate(bool value) { allow_allocate_ = value; }

  bool allow_call() const { return allow_call_; }
  void set_allow_call(bool value) { allow_call_ = value; }

  bool allow_deferred_call() const { return allow_deferred_call_; }
  void set_allow_deferred_call(bool value) { allow_deferred_call_ = value; }
#endif  // DEBUG

 private:
  template <typename Derived>
  class TemporaryRegisterScopeBase;

  inline constexpr int GetFramePointerOffsetForStackSlot(int index) {
    return StandardFrameConstants::kExpressionsOffset -
           index * kSystemPointerSize;
  }

  inline void SmiTagInt32AndSetFlags(Register dst, Register src);

  MaglevCodeGenState* const code_gen_state_;
  TemporaryRegisterScope* scratch_register_scope_ = nullptr;
#ifdef DEBUG
  bool allow_allocate_ = false;
  bool allow_call_ = false;
  bool allow_deferred_call_ = false;
#endif  // DEBUG
};

// Shared logic for per-architecture TemporaryRegisterScope.
template <typename Derived>
class MaglevAssembler::TemporaryRegisterScopeBase {
 public:
  struct SavedData {
    RegList available_;
    DoubleRegList available_double_;
  };

  explicit TemporaryRegisterScopeBase(MaglevAssembler* masm)
      : masm_(masm),
        prev_scope_(masm->scratch_register_scope_),
        available_(masm->scratch_register_scope_
                       ? static_cast<TemporaryRegisterScopeBase*>(prev_scope_)
                             ->available_
                       : RegList()),
        available_double_(
            masm->scratch_register_scope_
                ? static_cast<TemporaryRegisterScopeBase*>(prev_scope_)
                      ->available_double_
                : DoubleRegList()) {
    masm_->scratch_register_scope_ = static_cast<Derived*>(this);
  }
  explicit TemporaryRegisterScopeBase(MaglevAssembler* masm,
                                      const SavedData& saved_data)
      : masm_(masm),
        prev_scope_(masm->scratch_register_scope_),
        available_(saved_data.available_),
        available_double_(saved_data.available_double_) {
    masm_->scratch_register_scope_ = static_cast<Derived*>(this);
  }
  ~TemporaryRegisterScopeBase() {
    masm_->scratch_register_scope_ = prev_scope_;
    // TODO(leszeks): Clear used registers.
  }

  void ResetToDefault() {
    available_ = {};
    available_double_ = {};
    static_cast<Derived*>(this)->ResetToDefaultImpl();
  }

  Register Acquire() {
    CHECK(!available_.is_empty());
    return available_.PopFirst();
  }
  void Include(const RegList list) {
    DCHECK((list - kAllocatableGeneralRegisters).is_empty());
    available_ = available_ | list;
  }

  DoubleRegister AcquireDouble() {
    CHECK(!available_double_.is_empty());
    return available_double_.PopFirst();
  }
  void IncludeDouble(const DoubleRegList list) {
    DCHECK((list - kAllocatableDoubleRegisters).is_empty());
    available_double_ = available_double_ | list;
  }

  RegList Available() { return available_; }
  void SetAvailable(RegList list) { available_ = list; }

  DoubleRegList AvailableDouble() { return available_double_; }
  void SetAvailableDouble(DoubleRegList list) { available_double_ = list; }

 protected:
  SavedData CopyForDeferBase() {
    return SavedData{available_, available_double_};
  }

  MaglevAssembler* masm_;
  Derived* prev_scope_;
  RegList available_;
  DoubleRegList available_double_;
};

class SaveRegisterStateForCall {
 public:
  SaveRegisterStateForCall(MaglevAssembler* masm, RegisterSnapshot snapshot)
      : masm(masm), snapshot_(snapshot) {
    masm->PushAll(snapshot_.live_registers);
    masm->PushAll(snapshot_.live_double_registers, kDoubleSize);
  }

  ~SaveRegisterStateForCall() {
    masm->PopAll(snapshot_.live_double_registers, kDoubleSize);
    masm->PopAll(snapshot_.live_registers);
  }

  MaglevSafepointTableBuilder::Safepoint DefineSafepoint() {
    // TODO(leszeks): Avoid emitting safepoints when there are no registers to
    // save.
    auto safepoint = masm->safepoint_table_builder()->DefineSafepoint(masm);
    int pushed_reg_index = 0;
    for (Register reg : snapshot_.live_registers) {
      if (snapshot_.live_tagged_registers.has(reg)) {
        safepoint.DefineTaggedRegister(pushed_reg_index);
      }
      pushed_reg_index++;
    }
#ifdef V8_TARGET_ARCH_ARM64
    pushed_reg_index = RoundUp<2>(pushed_reg_index);
#endif
    int num_double_slots = snapshot_.live_double_registers.Count() *
                           (kDoubleSize / kSystemPointerSize);
#ifdef V8_TARGET_ARCH_ARM64
    num_double_slots = RoundUp<2>(num_double_slots);
#endif
    safepoint.SetNumExtraSpillSlots(pushed_reg_index + num_double_slots);
    return safepoint;
  }

  MaglevSafepointTableBuilder::Safepoint DefineSafepointWithLazyDeopt(
      LazyDeoptInfo* lazy_deopt_info) {
    lazy_deopt_info->set_deopting_call_return_pc(
        masm->pc_offset_for_safepoint());
    masm->code_gen_state()->PushLazyDeopt(lazy_deopt_info);
    return DefineSafepoint();
  }

 private:
  MaglevAssembler* masm;
  RegisterSnapshot snapshot_;
};

ZoneLabelRef::ZoneLabelRef(MaglevAssembler* masm)
    : ZoneLabelRef(masm->compilation_info()->zone()) {}

// ---
// Deopt
// ---

inline bool MaglevAssembler::IsDeoptLabel(Label* label) {
  for (auto deopt : code_gen_state_->eager_deopts()) {
    if (deopt->deopt_entry_label() == label) {
      return true;
    }
  }
  return false;
}

template <typename NodeT>
inline Label* MaglevAssembler::GetDeoptLabel(NodeT* node,
                                             DeoptimizeReason reason) {
  static_assert(NodeT::kProperties.can_eager_deopt());
  EagerDeoptInfo* deopt_info = node->eager_deopt_info();
  if (deopt_info->reason() != DeoptimizeReason::kUnknown) {
    DCHECK_EQ(deopt_info->reason(), reason);
  }
  if (deopt_info->deopt_entry_label()->is_unused()) {
    code_gen_state()->PushEagerDeopt(deopt_info);
    deopt_info->set_reason(reason);
  }
  return node->eager_deopt_info()->deopt_entry_label();
}

template <typename NodeT>
inline void MaglevAssembler::EmitEagerDeopt(NodeT* node,
                                            DeoptimizeReason reason) {
  RecordComment("-- jump to eager deopt");
  JumpToDeopt(GetDeoptLabel(node, reason));
}

template <typename NodeT>
inline void MaglevAssembler::EmitEagerDeoptIf(Condition cond,
                                              DeoptimizeReason reason,
                                              NodeT* node) {
  RecordComment("-- Jump to eager deopt");
  JumpIf(cond, GetDeoptLabel(node, reason));
}

template <typename NodeT>
void MaglevAssembler::EmitEagerDeoptIfSmi(NodeT* node, Register object,
                                          DeoptimizeReason reason) {
  RecordComment("-- Jump to eager deopt");
  JumpIfSmi(object, GetDeoptLabel(node, reason));
}

template <typename NodeT>
void MaglevAssembler::EmitEagerDeoptIfNotSmi(NodeT* node, Register object,
                                             DeoptimizeReason reason) {
  RecordComment("-- Jump to eager deopt");
  JumpIfNotSmi(object, GetDeoptLabel(node, reason));
}

inline void MaglevAssembler::DefineLazyDeoptPoint(LazyDeoptInfo* info) {
  info->set_deopting_call_return_pc(pc_offset_for_safepoint());
  code_gen_state()->PushLazyDeopt(info);
  safepoint_table_builder()->DefineSafepoint(this);
}

inline void MaglevAssembler::DefineExceptionHandlerPoint(NodeBase* node) {
  ExceptionHandlerInfo* info = node->exception_handler_info();
  if (!info->HasExceptionHandler()) return;
  info->pc_offset = pc_offset_for_safepoint();
  code_gen_state()->PushHandlerInfo(node);
}

inline void MaglevAssembler::DefineExceptionHandlerAndLazyDeoptPoint(
    NodeBase* node) {
  DefineExceptionHandlerPoint(node);
  DefineLazyDeoptPoint(node->lazy_deopt_info());
}

// Helpers for pushing arguments.
template <typename T>
class RepeatIterator {
 public:
  // Although we pretend to be a random access iterator, only methods that are
  // required for Push() are implemented right now.
  typedef std::random_access_iterator_tag iterator_category;
  typedef T value_type;
  typedef int difference_type;
  typedef T* pointer;
  typedef T reference;
  RepeatIterator(T val, int count) : val_(val), count_(count) {}
  reference operator*() const { return val_; }
  pointer operator->() { return &val_; }
  RepeatIterator& operator++() {
    ++count_;
    return *this;
  }
  RepeatIterator& operator--() {
    --count_;
    return *this;
  }
  RepeatIterator& operator+=(difference_type diff) {
    count_ += diff;
    return *this;
  }
  bool operator!=(const RepeatIterator<T>& that) const {
    return count_ != that.count_;
  }
  bool operator==(const RepeatIterator<T>& that) const {
    return count_ == that.count_;
  }
  difference_type operator-(const RepeatIterator<T>& it) const {
    return count_ - it.count_;
  }

 private:
  T val_;
  int count_;
};

template <typename T>
auto RepeatValue(T val, int count) {
  return base::make_iterator_range(RepeatIterator<T>(val, 0),
                                   RepeatIterator<T>(val, count));
}

namespace detail {

template <class T>
struct is_iterator_range : std::false_type {};
template <typename T>
struct is_iterator_range<base::iterator_range<T>> : std::true_type {};

}  // namespace detail

// General helpers.

inline Condition ToCondition(AssertCondition cond) {
  switch (cond) {
#define CASE(Name)               \
  case AssertCondition::k##Name: \
    return k##Name;
    ASSERT_CONDITION(CASE)
#undef CASE
  }
}

constexpr Condition ConditionFor(Operation operation) {
  switch (operation) {
    case Operation::kEqual:
    case Operation::kStrictEqual:
      return kEqual;
    case Operation::kLessThan:
      return kLessThan;
    case Operation::kLessThanOrEqual:
      return kLessThanEqual;
    case Operation::kGreaterThan:
      return kGreaterThan;
    case Operation::kGreaterThanOrEqual:
      return kGreaterThanEqual;
    default:
      UNREACHABLE();
  }
}

constexpr Condition UnsignedConditionFor(Operation operation) {
  switch (operation) {
    case Operation::kEqual:
    case Operation::kStrictEqual:
      return kEqual;
    case Operation::kLessThan:
      return kUnsignedLessThan;
    case Operation::kLessThanOrEqual:
      return kUnsignedLessThanEqual;
    case Operation::kGreaterThan:
      return kUnsignedGreaterThan;
    case Operation::kGreaterThanOrEqual:
      return kUnsignedGreaterThanEqual;
    default:
      UNREACHABLE();
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_ASSEMBLER_H_
