// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_RISCV_MAGLEV_ASSEMBLER_RISCV_INL_H_
#define V8_MAGLEV_RISCV_MAGLEV_ASSEMBLER_RISCV_INL_H_

#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/common/globals.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/maglev/maglev-assembler.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-code-gen-state.h"
#include "src/maglev/maglev-ir.h"
#include "src/roots/static-roots.h"

namespace v8 {
namespace internal {
namespace maglev {

constexpr Condition ConditionForFloat64(Operation operation) {
  return ConditionFor(operation);
}

inline int ShiftFromScale(int n) {
  switch (n) {
    case 1:
      return 0;
    case 2:
      return 1;
    case 4:
      return 2;
    case 8:
      return 3;
    default:
      UNREACHABLE();
  }
}

inline FPUCondition ConditionToConditionCmpFPU(Condition condition) {
  switch (condition) {
    case kEqual:
      return EQ;
    case kNotEqual:
      return NE;
    case kUnsignedLessThan:
    case kLessThan:
      return LT;
    case kUnsignedGreaterThanEqual:
    case kGreaterThanEqual:
      return GE;
    case kUnsignedLessThanEqual:
    case kLessThanEqual:
      return LE;
    case kUnsignedGreaterThan:
    case kGreaterThan:
      return GT;
    default:
      break;
  }
  UNREACHABLE();
}

class MaglevAssembler::TemporaryRegisterScope
    : public TemporaryRegisterScopeBase<TemporaryRegisterScope> {
  using Base = TemporaryRegisterScopeBase<TemporaryRegisterScope>;

 public:
  struct SavedData : public Base::SavedData {
    RegList available_scratch_;
    DoubleRegList available_fp_scratch_;
  };

  explicit TemporaryRegisterScope(MaglevAssembler* masm)
      : Base(masm), scratch_scope_(masm) {
    if (prev_scope_ == nullptr) {
      // Add extra scratch register if no previous scope.
      scratch_scope_.Include(kMaglevExtraScratchRegister);
    }
  }
  explicit TemporaryRegisterScope(MaglevAssembler* masm,
                                  const SavedData& saved_data)
      : Base(masm, saved_data), scratch_scope_(masm) {
    scratch_scope_.SetAvailable(saved_data.available_scratch_);
    scratch_scope_.SetAvailableDouble(saved_data.available_fp_scratch_);
  }

  Register AcquireScratch() {
    Register reg = scratch_scope_.Acquire();
    CHECK(!available_.has(reg));
    return reg;
  }
  DoubleRegister AcquireScratchDouble() {
    DoubleRegister reg = scratch_scope_.AcquireDouble();
    CHECK(!available_double_.has(reg));
    return reg;
  }
  void IncludeScratch(Register reg) { scratch_scope_.Include(reg); }

  SavedData CopyForDefer() {
    return SavedData{
        CopyForDeferBase(),
        scratch_scope_.Available(),
        scratch_scope_.AvailableDouble(),
    };
  }

  void ResetToDefaultImpl() {
    scratch_scope_.SetAvailable(Assembler::DefaultTmpList() |
                                kMaglevExtraScratchRegister);
    scratch_scope_.SetAvailableDouble(Assembler::DefaultFPTmpList());
  }

 private:
  UseScratchRegisterScope scratch_scope_;
};

inline MapCompare::MapCompare(MaglevAssembler* masm, Register object,
                              size_t map_count)
    : masm_(masm), object_(object), map_count_(map_count) {
  map_ = masm_->scratch_register_scope()->AcquireScratch();
  if (PointerCompressionIsEnabled()) {
    masm_->LoadCompressedMap(map_, object_);
  } else {
    masm_->LoadMap(map_, object_);
  }
  USE(map_count_);
}

void MapCompare::Generate(Handle<Map> map, Condition cond, Label* if_true,
                          Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(masm_);
  Register temp = temps.AcquireScratch();
  masm_->Move(temp, map);
  // FIXME: reimplement with CmpTagged/JumpIf
  if (COMPRESS_POINTERS_BOOL) {
    masm_->Sub32(temp, map_, temp);
  } else {
    masm_->SubWord(temp, map_, temp);
  }
  masm_->MacroAssembler::Branch(if_true, cond, temp, Operand(zero_reg),
                                distance);
}

Register MapCompare::GetMap() {
  if (PointerCompressionIsEnabled()) {
    masm_->DecompressTagged(map_, map_);
  }
  return map_;
}

int MapCompare::TemporaryCount(size_t map_count) { return 1; }

namespace detail {

// Check if the argument is already in a register and doesn't need any
// scratches to reload. This should be in sync with `ToRegister` function below.
template <typename Arg>
inline bool AlreadyInARegister(Arg arg) {
  return false;
}

inline bool AlreadyInARegister(Register reg) { return true; }

inline bool AlreadyInARegister(const Input& input) {
  if (input.operand().IsConstant()) {
    return false;
  }
  const compiler::AllocatedOperand& operand =
      compiler::AllocatedOperand::cast(input.operand());
  if (operand.IsRegister()) {
    return true;
  }
  DCHECK(operand.IsStackSlot());
  return false;
}

template <typename Arg>
inline Register ToRegister(MaglevAssembler* masm,
                           MaglevAssembler::TemporaryRegisterScope* scratch,
                           Arg arg) {
  Register reg = scratch->AcquireScratch();
  masm->Move(reg, arg);
  return reg;
}
inline Register ToRegister(MaglevAssembler* masm,
                           MaglevAssembler::TemporaryRegisterScope* scratch,
                           Register reg) {
  return reg;
}
inline Register ToRegister(MaglevAssembler* masm,
                           MaglevAssembler::TemporaryRegisterScope* scratch,
                           const Input& input) {
  if (input.operand().IsConstant()) {
    Register reg = scratch->AcquireScratch();
    input.node()->LoadToRegister(masm, reg);
    return reg;
  }
  const compiler::AllocatedOperand& operand =
      compiler::AllocatedOperand::cast(input.operand());
  if (operand.IsRegister()) {
    return ToRegister(input);
  } else {
    DCHECK(operand.IsStackSlot());
    Register reg = scratch->AcquireScratch();
    masm->Move(reg, masm->ToMemOperand(input));
    return reg;
  }
}

template <typename... Args>
struct CountPushHelper;

template <>
struct CountPushHelper<> {
  static int Count() { return 0; }
};

template <typename Arg, typename... Args>
struct CountPushHelper<Arg, Args...> {
  static int Count(Arg arg, Args... args) {
    int arg_count = 1;
    if constexpr (is_iterator_range<Arg>::value) {
      arg_count = static_cast<int>(std::distance(arg.begin(), arg.end()));
    }
    return arg_count + CountPushHelper<Args...>::Count(args...);
  }
};

template <typename... Args>
struct PushAllHelper;

template <>
struct PushAllHelper<> {
  static void Push(MaglevAssembler* masm) {}
  static void PushReverse(MaglevAssembler* masm) {}
};

template <typename... Args>
inline void PushAll(MaglevAssembler* masm, Args... args) {
  PushAllHelper<Args...>::Push(masm, args...);
}

template <typename... Args>
inline void PushAllReverse(MaglevAssembler* masm, Args... args) {
  PushAllHelper<Args...>::PushReverse(masm, args...);
}

inline void PushInput(MaglevAssembler* masm, const Input& input) {
  if (input.operand().IsConstant()) {
    MaglevAssembler::TemporaryRegisterScope temps(masm);
    Register scratch = temps.AcquireScratch();
    input.node()->LoadToRegister(masm, scratch);
    masm->Push(scratch);
  } else {
    // TODO(leszeks): Consider special casing the value. (Toon: could possibly
    // be done through Input directly?)
    const compiler::AllocatedOperand& operand =
        compiler::AllocatedOperand::cast(input.operand());
    if (operand.IsRegister()) {
      masm->Push(operand.GetRegister());
    } else {
      DCHECK(operand.IsStackSlot());
      MaglevAssembler::TemporaryRegisterScope temps(masm);
      Register scratch = temps.AcquireScratch();
      masm->LoadWord(scratch, masm->GetStackSlot(operand));
      masm->Push(scratch);
    }
  }
}

template <typename T, typename... Args>
inline void PushIterator(MaglevAssembler* masm, base::iterator_range<T> range,
                         Args... args) {
  for (auto iter = range.begin(), end = range.end(); iter != end; ++iter) {
    masm->Push(*iter);
  }
  PushAllHelper<Args...>::Push(masm, args...);
}

template <typename T, typename... Args>
inline void PushIteratorReverse(MaglevAssembler* masm,
                                base::iterator_range<T> range, Args... args) {
  PushAllHelper<Args...>::PushReverse(masm, args...);
  for (auto iter = range.rbegin(), end = range.rend(); iter != end; ++iter) {
    masm->Push(*iter);
  }
}

template <typename... Args>
struct PushAllHelper<Input, Args...> {
  static void Push(MaglevAssembler* masm, const Input& arg, Args... args) {
    PushInput(masm, arg);
    PushAllHelper<Args...>::Push(masm, args...);
  }
  static void PushReverse(MaglevAssembler* masm, const Input& arg,
                          Args... args) {
    PushAllHelper<Args...>::PushReverse(masm, args...);
    PushInput(masm, arg);
  }
};
template <typename Arg, typename... Args>
struct PushAllHelper<Arg, Args...> {
  static void Push(MaglevAssembler* masm, Arg arg, Args... args) {
    if constexpr (is_iterator_range<Arg>::value) {
      PushIterator(masm, arg, args...);
    } else {
      masm->MacroAssembler::Push(arg);
      PushAllHelper<Args...>::Push(masm, args...);
    }
  }
  static void PushReverse(MaglevAssembler* masm, Arg arg, Args... args) {
    if constexpr (is_iterator_range<Arg>::value) {
      PushIteratorReverse(masm, arg, args...);
    } else {
      PushAllHelper<Args...>::PushReverse(masm, args...);
      masm->Push(arg);
    }
  }
};

}  // namespace detail

template <typename... T>
void MaglevAssembler::Push(T... vals) {
  detail::PushAll(this, vals...);
}

template <typename... T>
void MaglevAssembler::PushReverse(T... vals) {
  detail::PushAllReverse(this, vals...);
}

inline void MaglevAssembler::BindJumpTarget(Label* label) {
  MacroAssembler::BindJumpTarget(label);
}

inline void MaglevAssembler::BindBlock(BasicBlock* block) {
  if (block->is_start_block_of_switch_case()) {
    BindJumpTarget(block->label());
  } else {
    bind(block->label());
  }
}

inline Condition MaglevAssembler::CheckSmi(Register src) {
  Register cmp_flag = MaglevAssembler::GetFlagsRegister();
  // Pointers to heap objects have a 1 set for the bottom bit,
  // so cmp_flag is set to 0 if src is Smi.
  MacroAssembler::SmiTst(src, cmp_flag);
  return eq;
}

#ifdef V8_ENABLE_DEBUG_CODE
inline void MaglevAssembler::AssertMap(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  AssertNotSmi(object, AbortReason::kOperandIsNotAMap);

  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register temp = temps.AcquireScratch();
  Label ConditionMet, Done;
  MacroAssembler::JumpIfObjectType(&Done, Condition::kEqual, object, MAP_TYPE,
                                   temp);
  Abort(AbortReason::kOperandIsNotAMap);
  bind(&Done);
}
#endif

inline void MaglevAssembler::SmiTagInt32AndSetFlags(Register dst,
                                                    Register src) {
  // FIXME check callsites and subsequent calls to Assert!
  ASM_CODE_COMMENT(this);
  static_assert(kSmiTag == 0);
  // NB: JumpIf expects the result in dedicated "flag" register
  Register overflow_flag = MaglevAssembler::GetFlagsRegister();
  if (SmiValuesAre31Bits()) {
    // Smi is shifted left by 1, so double incoming integer using 64- and 32-bit
    // addition operations and then compare the results to detect overflow. The
    // order does matter cuz in common way dst != src is NOT guarantied
    Add64(overflow_flag, src, src);
    Add32(dst, src, src);
    Sne(overflow_flag, overflow_flag, Operand(dst));
  } else {
    // Smi goes to upper 32
    slli(dst, src, 32);
    // no overflow happens (check!)
    Move(overflow_flag, zero_reg);
  }
}

inline void MaglevAssembler::CheckInt32IsSmi(Register maybeSmi, Label* fail,
                                             Register scratch) {
  DCHECK(!SmiValuesAre32Bits());
  // Smi is shifted left by 1
  MaglevAssembler::TemporaryRegisterScope temps(this);
  if (scratch == Register::no_reg()) {
    scratch = temps.AcquireScratch();
  }
  Register sum32 = scratch;
  Register sum64 = temps.AcquireScratch();
  Add32(sum32, maybeSmi, Operand(maybeSmi));
  Add64(sum64, maybeSmi, Operand(maybeSmi));
  // overflow happened if sum64 != sum32
  MacroAssembler::Branch(fail, ne, sum64, Operand(sum32));
}

inline void MaglevAssembler::SmiAddConstant(Register dst, Register src,
                                            int value, Label* fail,
                                            Label::Distance distance) {
  AssertSmi(src);
  if (value != 0) {
    MaglevAssembler::TemporaryRegisterScope temps(this);
    Register overflow = temps.AcquireScratch();
    Operand addend = Operand(Smi::FromInt(value));
    if (SmiValuesAre31Bits()) {
      Add64(overflow, src, addend);
      Add32(dst, src, addend);
      Sub64(overflow, dst, overflow);
      MacroAssembler::Branch(fail, ne, overflow, Operand(zero_reg), distance);
    } else {
      AddOverflow64(dst, src, addend, overflow);
      MacroAssembler::Branch(fail, lt, overflow, Operand(zero_reg), distance);
    }
  } else {
    Move(dst, src);
  }
}

inline void MaglevAssembler::SmiSubConstant(Register dst, Register src,
                                            int value, Label* fail,
                                            Label::Distance distance) {
  AssertSmi(src);
  if (value != 0) {
    MaglevAssembler::TemporaryRegisterScope temps(this);
    Register overflow = temps.AcquireScratch();
    Operand subtrahend = Operand(Smi::FromInt(value));
    if (SmiValuesAre31Bits()) {
      Sub64(overflow, src, subtrahend);
      Sub32(dst, src, subtrahend);
      Sub64(overflow, dst, overflow);
      MacroAssembler::Branch(fail, ne, overflow, Operand(zero_reg), distance);
    } else {
      SubOverflow64(dst, src, subtrahend, overflow);
      MacroAssembler::Branch(fail, lt, overflow, Operand(zero_reg), distance);
    }
  } else {
    Move(dst, src);
  }
}

inline void MaglevAssembler::MoveHeapNumber(Register dst, double value) {
  li(dst, Operand::EmbeddedNumber(value));
}

// Compare the object in a register to a value from the root list.
inline void MaglevAssembler::CompareRoot(const Register& obj, RootIndex index,
                                         ComparisonMode mode) {
  constexpr Register aflag = MaglevAssembler::GetFlagsRegister();
  MacroAssembler::CompareRoot(obj, index, aflag, mode);
}

inline void MaglevAssembler::CompareTaggedRoot(const Register& obj,
                                               RootIndex index) {
  constexpr Register cmp_result = MaglevAssembler::GetFlagsRegister();
  MacroAssembler::CompareTaggedRoot(obj, index, cmp_result);
}

inline void MaglevAssembler::CmpTagged(const Register& rs1,
                                       const Register& rs2) {
  constexpr Register aflag = MaglevAssembler::GetFlagsRegister();
  MacroAssembler::CmpTagged(aflag, rs1, rs2);
}

// Cmp and Assert are only used in maglev unittests, so to make them happy.
// It's only used with subsequent Assert kEqual,
// so pseudo flag should be 0 if rn equals imm
inline void MaglevAssembler::Cmp(const Register& rn, int imm) {
  constexpr Register aflag = MaglevAssembler::GetFlagsRegister();
  SubWord(aflag, rn, Operand(imm));
}

inline void MaglevAssembler::Assert(Condition cond, AbortReason reason) {
  constexpr Register aflag = MaglevAssembler::GetFlagsRegister();
  MacroAssembler::Assert(cond, reason, aflag, Operand(zero_reg));
}

inline Condition MaglevAssembler::IsRootConstant(Input input,
                                                 RootIndex root_index) {
  constexpr Register aflag = MaglevAssembler::GetFlagsRegister();

  if (input.operand().IsRegister()) {
    MacroAssembler::CompareRoot(ToRegister(input), root_index, aflag);
  } else {
    DCHECK(input.operand().IsStackSlot());
    MaglevAssembler::TemporaryRegisterScope temps(this);
    Register scratch = temps.AcquireScratch();
    LoadWord(scratch, ToMemOperand(input));
    MacroAssembler::CompareRoot(scratch, root_index, aflag);
  }
  return eq;
}

inline MemOperand MaglevAssembler::StackSlotOperand(StackSlot slot) {
  return MemOperand(fp, slot.index);
}

inline Register MaglevAssembler::GetFramePointer() { return fp; }

// TODO(Victorgomes): Unify this to use StackSlot struct.
inline MemOperand MaglevAssembler::GetStackSlot(
    const compiler::AllocatedOperand& operand) {
  return MemOperand(fp, GetFramePointerOffsetForStackSlot(operand));
}

inline MemOperand MaglevAssembler::ToMemOperand(
    const compiler::InstructionOperand& operand) {
  return GetStackSlot(compiler::AllocatedOperand::cast(operand));
}

inline MemOperand MaglevAssembler::ToMemOperand(const ValueLocation& location) {
  return ToMemOperand(location.operand());
}

inline void MaglevAssembler::BuildTypedArrayDataPointer(Register data_pointer,
                                                        Register object) {
  DCHECK_NE(data_pointer, object);
  LoadExternalPointerField(
      data_pointer,
      FieldMemOperand(object, JSTypedArray::kExternalPointerOffset));
  if (JSTypedArray::kMaxSizeInHeap == 0) return;
  MaglevAssembler::TemporaryRegisterScope scope(this);
  Register base = scope.AcquireScratch();
  if (COMPRESS_POINTERS_BOOL) {
    Load32U(base, FieldMemOperand(object, JSTypedArray::kBasePointerOffset));
  } else {
    LoadWord(base, FieldMemOperand(object, JSTypedArray::kBasePointerOffset));
  }
  Add64(data_pointer, data_pointer, base);
}

inline MemOperand MaglevAssembler::TypedArrayElementOperand(
    Register data_pointer, Register index, int element_size) {
  const int shift = ShiftFromScale(element_size);
  if (shift == 0) {
    AddWord(data_pointer, data_pointer, index);
  } else {
    CalcScaledAddress(data_pointer, data_pointer, index, shift);
  }
  return MemOperand(data_pointer);
}

inline MemOperand MaglevAssembler::DataViewElementOperand(Register data_pointer,
                                                          Register index) {
  Add64(data_pointer, data_pointer,
        index);  // FIXME: should we check for COMPRESSED PTRS enabled here ?
  return MemOperand(data_pointer);
}

inline void MaglevAssembler::LoadTaggedFieldByIndex(Register result,
                                                    Register object,
                                                    Register index, int scale,
                                                    int offset) {
  const int shift = ShiftFromScale(scale);
  if (shift == 0) {
    AddWord(result, object, index);
  } else {
    CalcScaledAddress(result, object, index, shift);
  }
  LoadTaggedField(result, FieldMemOperand(result, offset));
}

inline void MaglevAssembler::LoadBoundedSizeFromObject(Register result,
                                                       Register object,
                                                       int offset) {
  Move(result, FieldMemOperand(object, offset));
#ifdef V8_ENABLE_SANDBOX
  SrlWord(result, result, Operand(kBoundedSizeShift));
#endif  // V8_ENABLE_SANDBOX
}

inline void MaglevAssembler::LoadExternalPointerField(Register result,
                                                      MemOperand operand) {
#ifdef V8_ENABLE_SANDBOX
  LoadSandboxedPointerField(result, operand);
#else
  Move(result, operand);
#endif
}

void MaglevAssembler::LoadFixedArrayElement(Register result, Register array,
                                            Register index) {
  if (v8_flags.debug_code) {
    AssertObjectType(array, FIXED_ARRAY_TYPE, AbortReason::kUnexpectedValue);
    CompareInt32AndAssert(index, 0, kUnsignedGreaterThanEqual,
                          AbortReason::kUnexpectedNegativeValue);
  }
  LoadTaggedFieldByIndex(result, array, index, kTaggedSize,
                         OFFSET_OF_DATA_START(FixedArray));
}

inline void MaglevAssembler::LoadTaggedFieldWithoutDecompressing(
    Register result, Register object, int offset) {
  MacroAssembler::LoadTaggedFieldWithoutDecompressing(
      result, FieldMemOperand(object, offset));
}

void MaglevAssembler::LoadFixedArrayElementWithoutDecompressing(
    Register result, Register array, Register index) {
  if (v8_flags.debug_code) {
    AssertObjectType(array, FIXED_ARRAY_TYPE, AbortReason::kUnexpectedValue);
    CompareInt32AndAssert(index, 0, kUnsignedGreaterThanEqual,
                          AbortReason::kUnexpectedNegativeValue);
  }
  CalcScaledAddress(result, array, index, kTaggedSizeLog2);
  MacroAssembler::LoadTaggedFieldWithoutDecompressing(
      result, FieldMemOperand(result, OFFSET_OF_DATA_START(FixedArray)));
}

void MaglevAssembler::LoadFixedDoubleArrayElement(DoubleRegister result,
                                                  Register array,
                                                  Register index) {
  if (v8_flags.debug_code) {
    AssertObjectType(array, FIXED_DOUBLE_ARRAY_TYPE,
                     AbortReason::kUnexpectedValue);
    CompareInt32AndAssert(index, 0, kUnsignedGreaterThanEqual,
                          AbortReason::kUnexpectedNegativeValue);
  }
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  CalcScaledAddress(scratch, array, index, kDoubleSizeLog2);
  LoadDouble(result,
             FieldMemOperand(scratch, OFFSET_OF_DATA_START(FixedArray)));
}

inline void MaglevAssembler::StoreFixedDoubleArrayElement(
    Register array, Register index, DoubleRegister value) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  CalcScaledAddress(scratch, array, index, kDoubleSizeLog2);
  StoreDouble(value,
              FieldMemOperand(scratch, OFFSET_OF_DATA_START(FixedArray)));
}

inline void MaglevAssembler::LoadSignedField(Register result,
                                             MemOperand operand, int size) {
  if (size == 1) {
    Lb(result, operand);
  } else if (size == 2) {
    Lh(result, operand);
  } else {
    DCHECK_EQ(size, 4);
    Lw(result, operand);
  }
}

inline void MaglevAssembler::LoadUnsignedField(Register result,
                                               MemOperand operand, int size) {
  if (size == 1) {
    Lbu(result, operand);
  } else if (size == 2) {
    Lhu(result, operand);
  } else {
    DCHECK_EQ(size, 4);
    Lwu(result, operand);
  }
}

inline void MaglevAssembler::SetSlotAddressForTaggedField(Register slot_reg,
                                                          Register object,
                                                          int offset) {
  Add64(slot_reg, object, offset - kHeapObjectTag);
}

inline void MaglevAssembler::SetSlotAddressForFixedArrayElement(
    Register slot_reg, Register object, Register index) {
  Add64(slot_reg, object, OFFSET_OF_DATA_START(FixedArray) - kHeapObjectTag);
  CalcScaledAddress(slot_reg, slot_reg, index, kTaggedSizeLog2);
}

inline void MaglevAssembler::StoreTaggedFieldNoWriteBarrier(Register object,
                                                            int offset,
                                                            Register value) {
  MacroAssembler::StoreTaggedField(value, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::StoreFixedArrayElementNoWriteBarrier(
    Register array, Register index, Register value) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  CalcScaledAddress(scratch, array, index, kTaggedSizeLog2);
  MacroAssembler::StoreTaggedField(
      value, FieldMemOperand(scratch, OFFSET_OF_DATA_START(FixedArray)));
}

inline void MaglevAssembler::StoreTaggedSignedField(Register object, int offset,
                                                    Register value) {
  AssertSmi(value);
  MacroAssembler::StoreTaggedField(value, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::StoreTaggedSignedField(Register object, int offset,
                                                    Tagged<Smi> value) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Move(scratch, value);
  MacroAssembler::StoreTaggedField(scratch, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::StoreInt32Field(Register object, int offset,
                                             int32_t value) {
  if (value == 0) {
    Sw(zero_reg, FieldMemOperand(object, offset));
    return;
  }
  MaglevAssembler::TemporaryRegisterScope scope(this);
  Register scratch = scope.AcquireScratch();
  Move(scratch, value);
  Sw(scratch, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::StoreField(MemOperand operand, Register value,
                                        int size) {
  DCHECK(size == 1 || size == 2 || size == 4);
  if (size == 1) {
    Sb(value, operand);
  } else if (size == 2) {
    Sh(value, operand);
  } else {
    DCHECK_EQ(size, 4);
    Sw(value, operand);
  }
}

inline void MaglevAssembler::ReverseByteOrder(Register value, int size) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  if (size == 2) {
    ByteSwap(value, value, 4, scratch);
    srai(value, value, 16);
  } else if (size == 4) {
    ByteSwap(value, value, 4, scratch);
  } else {
    DCHECK_EQ(size, 1);
  }
}

inline void MaglevAssembler::IncrementInt32(Register reg) {
  Add32(reg, reg, Operand(1));
}

inline void MaglevAssembler::DecrementInt32(Register reg) {
  Sub32(reg, reg, Operand(1));
}

inline void MaglevAssembler::AddInt32(Register reg, int amount) {
  Add32(reg, reg, Operand(amount));
}

inline void MaglevAssembler::AndInt32(Register reg, int mask) {
  // check if size of immediate exceeds 32 bits
  if constexpr (sizeof(intptr_t) > sizeof(mask)) {
    // set the upper bits of the immediate and so make sure that AND operation
    // won't touch the upper part of target register
    static constexpr intptr_t lsb_mask = 0xFFFFFFFF;
    And(reg, reg, Operand(~lsb_mask | mask));
  } else {
    And(reg, reg, Operand(mask));
  }
}

inline void MaglevAssembler::OrInt32(Register reg, int mask) {
  // OR won't touch the upper part of target register
  Or(reg, reg, Operand(mask));
}

inline void MaglevAssembler::ShiftLeft(Register reg, int amount) {
  Sll32(reg, reg, Operand(amount));
}

inline void MaglevAssembler::IncrementAddress(Register reg, int32_t delta) {
  Add64(reg, reg, Operand(delta));
}

inline void MaglevAssembler::LoadAddress(Register dst, MemOperand location) {
  DCHECK(location.is_reg());
  Add64(dst, location.rm(), location.offset());
}

inline void MaglevAssembler::Call(Label* target) {
  MacroAssembler::Call(target);
}

inline void MaglevAssembler::EmitEnterExitFrame(int extra_slots,
                                                StackFrame::Type frame_type,
                                                Register c_function,
                                                Register scratch) {
  EnterExitFrame(scratch, extra_slots, frame_type);
}

inline void MaglevAssembler::Move(StackSlot dst, Register src) {
  StoreWord(src, StackSlotOperand(dst));
}
inline void MaglevAssembler::Move(StackSlot dst, DoubleRegister src) {
  StoreDouble(src, StackSlotOperand(dst));
}
inline void MaglevAssembler::Move(Register dst, StackSlot src) {
  LoadWord(dst, StackSlotOperand(src));
}
inline void MaglevAssembler::Move(DoubleRegister dst, StackSlot src) {
  LoadDouble(dst, StackSlotOperand(src));
}
inline void MaglevAssembler::Move(MemOperand dst, Register src) {
  StoreWord(src, dst);
}
inline void MaglevAssembler::Move(Register dst, MemOperand src) {
  LoadWord(dst, src);
}
inline void MaglevAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  MoveDouble(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Tagged<Smi> src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(Register dst, ExternalReference src) {
  li(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Register src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Tagged<TaggedIndex> i) {
  li(dst, Operand(i.ptr()));
}
inline void MaglevAssembler::Move(Register dst, int32_t i) {
  li(dst, Operand(i));
}
inline void MaglevAssembler::Move(Register dst, uint32_t i) {
  li(dst, Operand(i));
}
inline void MaglevAssembler::Move(DoubleRegister dst, double n) {
  LoadFPRImmediate(dst, n);
}
inline void MaglevAssembler::Move(DoubleRegister dst, Float64 n) {
  LoadFPRImmediate(dst, n.get_scalar());
}
inline void MaglevAssembler::Move(Register dst, Handle<HeapObject> obj) {
  li(dst, obj);
}
void MaglevAssembler::MoveTagged(Register dst, Handle<HeapObject> obj) {
#ifdef V8_COMPRESS_POINTERS
  li(dst, obj, RelocInfo::COMPRESSED_EMBEDDED_OBJECT);
#else
  ASM_CODE_COMMENT_STRING(this, "MaglevAsm::MoveTagged");
  Move(dst, obj);
#endif
}

inline void MaglevAssembler::LoadInt32(Register dst, MemOperand src) {
  Load32U(dst, src);
}
inline void MaglevAssembler::StoreInt32(MemOperand dst, Register src) {
  Sw(src, dst);
}

inline void MaglevAssembler::LoadFloat32(DoubleRegister dst, MemOperand src) {
  LoadFloat(dst, src);
  // Convert Float32 to double(Float64)
  fcvt_d_s(dst, dst);
}
inline void MaglevAssembler::StoreFloat32(MemOperand dst, DoubleRegister src) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  DoubleRegister scratch = temps.AcquireScratchDouble();
  // Convert double(Float64) to Float32
  fcvt_s_d(scratch, src);
  StoreFloat(scratch, dst);
}
inline void MaglevAssembler::LoadFloat64(DoubleRegister dst, MemOperand src) {
  LoadDouble(dst, src);
}
inline void MaglevAssembler::StoreFloat64(MemOperand dst, DoubleRegister src) {
  StoreDouble(src, dst);
}

inline void MaglevAssembler::LoadUnalignedFloat64(DoubleRegister dst,
                                                  Register base,
                                                  Register index) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register address = temps.AcquireScratch();
  Add64(address, base, index);
  ULoadDouble(dst, MemOperand(address));
}
inline void MaglevAssembler::LoadUnalignedFloat64AndReverseByteOrder(
    DoubleRegister dst, Register base, Register index) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register address = temps.AcquireScratch();
  Add64(address, base, index);
  Register scratch = base;  // reuse base as scratch register
  Uld(scratch, MemOperand(address));
  ByteSwap(scratch, scratch, 8, address);
  MacroAssembler::Move(dst, scratch);
}
inline void MaglevAssembler::StoreUnalignedFloat64(Register base,
                                                   Register index,
                                                   DoubleRegister src) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register address = temps.AcquireScratch();
  Add64(address, base, index);
  UStoreDouble(src, MemOperand(address));
}
inline void MaglevAssembler::ReverseByteOrderAndStoreUnalignedFloat64(
    Register base, Register index, DoubleRegister src) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Register address = temps.AcquireScratch();
  MacroAssembler::Move(scratch, src);
  ByteSwap(scratch, scratch, 8, address);  // reuse address as scratch register
  Add64(address, base, index);
  Usd(scratch, MemOperand(address));
}

inline void MaglevAssembler::SignExtend32To64Bits(Register dst, Register src) {
  SignExtendWord(dst, src);
}

inline void MaglevAssembler::NegateInt32(Register val) {
  SignExtendWord(val, val);
  Neg(val, val);
}

inline void MaglevAssembler::ToUint8Clamped(Register result,
                                            DoubleRegister value, Label* min,
                                            Label* max, Label* done) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Register scratch2 = temps.AcquireScratch();
  DoubleRegister ftmp1 = temps.AcquireScratchDouble();
  DCHECK(ftmp1 != value);

  // if value is NOT in (0.0, 255.0), then fallback to min or max.
  fclass_d(scratch, value);
  constexpr int32_t nan_neg_mask =
      (kNegativeInfinity | kNegativeNormalNumber | kNegativeSubnormalNumber |
       kNegativeZero | kPositiveZero | kSignalingNaN | kQuietNaN);
  constexpr int32_t pos_inf_mask = kPositiveInfinity;
  And(scratch2, scratch, Operand(nan_neg_mask));
  MacroAssembler::Branch(min,  // value is NaN or value <= 0.0
                         not_equal, scratch2, Operand(zero_reg));
  And(scratch2, scratch, Operand(pos_inf_mask));
  MacroAssembler::Branch(max,  // value is +Infinity
                         not_equal, scratch2, Operand(zero_reg));
  // 255.0 is 0x406F_E000_0000_0000 in IEEE-754 floating point format
  Add32(scratch, zero_reg, Operand(0x406FE));
  Sll64(scratch, scratch, Operand(44));
  fmv_d_x(ftmp1, scratch);
  MacroAssembler::CompareF64(scratch, GE, value, ftmp1);
  MacroAssembler::Branch(max,  // value >= 255.0
                         not_equal, scratch, Operand(zero_reg));

  // value in (0.0, 255.0)
  fmv_x_d(result, value);
  // check if fractional part in result is absent
  Label has_fraction;
  Mv(scratch, result);
  SllWord(scratch, scratch, Operand(64 - kFloat64MantissaBits));
  MacroAssembler::Branch(&has_fraction, not_equal, scratch, Operand(zero_reg));
  // no fractional part, compute exponent part taking bias into account.
  SrlWord(result, result, Operand(kFloat64MantissaBits));
  SubWord(result, result, kFloat64ExponentBias);
  MacroAssembler::Branch(done);

  bind(&has_fraction);
  // Actual rounding is here. Notice that ToUint8Clamp does “round half to even”
  // tie-breaking and that differs from Math.round which does “round half up”
  // tie-breaking.
  fcvt_l_d(scratch, value, RNE);
  fcvt_d_l(ftmp1, scratch, RNE);
  // A special handling is needed if the result is a very small positive number
  // that rounds to zero. JS semantics requires that the rounded result retains
  // the sign of the input, so a very small positive floating-point number
  // should be rounded to positive 0.
  fsgnj_d(ftmp1, ftmp1, value);
  fmv_x_d(result, ftmp1);
  MacroAssembler::Branch(done);
}

template <typename NodeT>
inline void MaglevAssembler::DeoptIfBufferDetached(Register array,
                                                   Register scratch,
                                                   NodeT* node) {
  // A detached buffer leads to megamorphic feedback, so we won't have a deopt
  // loop if we deopt here.
  LoadTaggedField(scratch,
                  FieldMemOperand(array, JSArrayBufferView::kBufferOffset));
  LoadTaggedField(scratch,
                  FieldMemOperand(scratch, JSArrayBuffer::kBitFieldOffset));
  ZeroExtendWord(scratch, scratch);
  And(scratch, scratch, Operand(JSArrayBuffer::WasDetachedBit::kMask));
  Label* deopt_label =
      GetDeoptLabel(node, DeoptimizeReason::kArrayBufferWasDetached);
  RecordComment("-- Jump to eager deopt");
  MacroAssembler::Branch(deopt_label, not_equal, scratch, Operand(zero_reg));
}

inline void MaglevAssembler::LoadByte(Register dst, MemOperand src) {
  Lbu(dst, src);
}

inline Condition MaglevAssembler::IsCallableAndNotUndetectable(
    Register map, Register scratch) {
  Load32U(scratch, FieldMemOperand(map, Map::kBitFieldOffset));
  And(scratch, scratch,
      Operand(Map::Bits1::IsUndetectableBit::kMask |
              Map::Bits1::IsCallableBit::kMask));
  // NB: TestTypeOf=>Branch=>JumpIf expects the result of a comparison
  // in dedicated "flag" register
  constexpr Register bit_set_flag = MaglevAssembler::GetFlagsRegister();
  Sub32(bit_set_flag, scratch, Operand(Map::Bits1::IsCallableBit::kMask));
  return kEqual;
}

inline Condition MaglevAssembler::IsNotCallableNorUndetactable(
    Register map, Register scratch) {
  Load32U(scratch, FieldMemOperand(map, Map::kBitFieldOffset));

  // NB: TestTypeOf=>Branch=>JumpIf expects the result of a comparison
  // in dedicated "flag" register
  constexpr Register bits_unset_flag = MaglevAssembler::GetFlagsRegister();
  And(bits_unset_flag, scratch,
      Operand(Map::Bits1::IsUndetectableBit::kMask |
              Map::Bits1::IsCallableBit::kMask));
  return kEqual;
}

inline void MaglevAssembler::LoadInstanceType(Register instance_type,
                                              Register heap_object) {
  LoadMap(instance_type, heap_object);
  Lhu(instance_type, FieldMemOperand(instance_type, Map::kInstanceTypeOffset));
}

inline void MaglevAssembler::CompareInstanceTypeAndJumpIf(
    Register map, InstanceType type, Condition cond, Label* target,
    Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Lhu(scratch, FieldMemOperand(map, Map::kInstanceTypeOffset));
  // we could be out of scratch registers by this moment already, and Branch
  // with Immediate operand require one so use Sub and compare against x0 later,
  // saves a register. can be done only for signed comparisons
  bool can_sub = false;
  switch (cond) {
    case Condition::kEqual:
    case Condition::kNotEqual:
    case Condition::kLessThan:
    case Condition::kLessThanEqual:
    case Condition::kGreaterThan:
    case Condition::kGreaterThanEqual:
      can_sub = true;
      break;
    default:
      break;
  }
  if (can_sub) {
    SubWord(scratch, scratch, Operand(type));
    type = static_cast<InstanceType>(0);
  }
  MacroAssembler::Branch(target, cond, scratch, Operand(type), distance);
}

inline Condition MaglevAssembler::CompareInstanceTypeRange(
    Register map, Register instance_type_out, InstanceType lower_limit,
    InstanceType higher_limit) {
  DCHECK_LT(lower_limit, higher_limit);
  Lhu(instance_type_out, FieldMemOperand(map, Map::kInstanceTypeOffset));
  Sub32(instance_type_out, instance_type_out, Operand(lower_limit));
  Register aflag = MaglevAssembler::GetFlagsRegister();
  // NB: JumpIf expects the result in dedicated "flag" register
  Sleu(aflag, instance_type_out, Operand(higher_limit - lower_limit));
  return kNotZero;
}

inline void MaglevAssembler::JumpIfNotObjectType(Register heap_object,
                                                 InstanceType type,
                                                 Label* target,
                                                 Label::Distance distance) {
  constexpr Register flag = MaglevAssembler::GetFlagsRegister();
  IsObjectType(heap_object, flag, flag, type);
  // NB: JumpIf expects the result in dedicated "flag" register
  JumpIf(kNotEqual, target, distance);
}

inline void MaglevAssembler::AssertObjectType(Register heap_object,
                                              InstanceType type,
                                              AbortReason reason) {
  AssertNotSmi(heap_object);
  constexpr Register flag = MaglevAssembler::GetFlagsRegister();
  IsObjectType(heap_object, flag, flag, type);
  // NB: Assert expects the result in dedicated "flag" register
  Assert(Condition::kEqual, reason);
}

inline void MaglevAssembler::BranchOnObjectType(
    Register heap_object, InstanceType type, Label* if_true,
    Label::Distance true_distance, bool fallthrough_when_true, Label* if_false,
    Label::Distance false_distance, bool fallthrough_when_false) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  IsObjectType(heap_object, scratch, scratch, type);
  Branch(kEqual, if_true, true_distance, fallthrough_when_true, if_false,
         false_distance, fallthrough_when_false);
}

inline void MaglevAssembler::JumpIfObjectTypeInRange(Register heap_object,
                                                     InstanceType lower_limit,
                                                     InstanceType higher_limit,
                                                     Label* target,
                                                     Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  LoadMap(scratch, heap_object);
  Lhu(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  Sub32(scratch, scratch, Operand(lower_limit));
  MacroAssembler::Branch(target, kUnsignedLessThanEqual, scratch,
                         Operand(higher_limit - lower_limit));
}

inline void MaglevAssembler::JumpIfObjectTypeNotInRange(
    Register heap_object, InstanceType lower_limit, InstanceType higher_limit,
    Label* target, Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  LoadMap(scratch, heap_object);
  Lhu(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  Sub32(scratch, scratch, Operand(lower_limit));
  MacroAssembler::Branch(target, kUnsignedGreaterThan, scratch,
                         Operand(higher_limit - lower_limit));
}

inline void MaglevAssembler::AssertObjectTypeInRange(Register heap_object,
                                                     InstanceType lower_limit,
                                                     InstanceType higher_limit,
                                                     AbortReason reason) {
  AssertNotSmi(heap_object);
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  LoadMap(scratch, heap_object);
  Lhu(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  Sub32(scratch, scratch, Operand(lower_limit));
  MacroAssembler::Assert(kUnsignedLessThanEqual, reason, scratch,
                         Operand(higher_limit - lower_limit));
}

inline void MaglevAssembler::BranchOnObjectTypeInRange(
    Register heap_object, InstanceType lower_limit, InstanceType higher_limit,
    Label* if_true, Label::Distance true_distance, bool fallthrough_when_true,
    Label* if_false, Label::Distance false_distance,
    bool fallthrough_when_false) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  LoadMap(scratch, heap_object);
  Lhu(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  Sub32(scratch, scratch, Operand(lower_limit));
  constexpr Register flags_reg = MaglevAssembler::GetFlagsRegister();
  // if scratch <= (higher_limit - lower_limit) then flags_reg = 0 else
  // flags_reg = 1
  CompareI(flags_reg, scratch, Operand(higher_limit - lower_limit),
           Condition::kUnsignedGreaterThan);
  // now compare against 0 witj kEqual
  Branch(Condition::kEqual, if_true, true_distance, fallthrough_when_true,
         if_false, false_distance, fallthrough_when_false);
}

#if V8_STATIC_ROOTS_BOOL
// FIXME: not tested
inline void MaglevAssembler::JumpIfObjectInRange(Register heap_object,
                                                 Tagged_t lower_limit,
                                                 Tagged_t higher_limit,
                                                 Label* target,
                                                 Label::Distance distance) {
  // Only allowed for comparisons against RORoots.
  DCHECK_LE(lower_limit, StaticReadOnlyRoot::kLastAllocatedRoot);
  DCHECK_LE(higher_limit, StaticReadOnlyRoot::kLastAllocatedRoot);
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  AssertNotSmi(heap_object);
  CompareRange(heap_object, scratch, lower_limit, higher_limit);
  JumpIf(kUnsignedLessThanEqual, target, distance);
}

inline void MaglevAssembler::JumpIfObjectNotInRange(Register heap_object,
                                                    Tagged_t lower_limit,
                                                    Tagged_t higher_limit,
                                                    Label* target,
                                                    Label::Distance distance) {
  // Only allowed for comparisons against RORoots.
  DCHECK_LE(lower_limit, StaticReadOnlyRoot::kLastAllocatedRoot);
  DCHECK_LE(higher_limit, StaticReadOnlyRoot::kLastAllocatedRoot);
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  AssertNotSmi(heap_object);
  CompareRange(heap_object, scratch, lower_limit, higher_limit);
  JumpIf(kUnsignedGreaterThan, target, distance);
}

inline void MaglevAssembler::AssertObjectInRange(Register heap_object,
                                                 Tagged_t lower_limit,
                                                 Tagged_t higher_limit,
                                                 AbortReason reason) {
  // Only allowed for comparisons against RORoots.
  DCHECK_LE(lower_limit, StaticReadOnlyRoot::kLastAllocatedRoot);
  DCHECK_LE(higher_limit, StaticReadOnlyRoot::kLastAllocatedRoot);
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  AssertNotSmi(heap_object);
  CompareRange(heap_object, scratch, lower_limit, higher_limit);
  Assert(kUnsignedLessThanEqual, reason);
}
#endif

inline void MaglevAssembler::JumpIfJSAnyIsNotPrimitive(
    Register heap_object, Label* target, Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  MacroAssembler::JumpIfJSAnyIsNotPrimitive(heap_object, scratch, target,
                                            distance);
}

template <typename NodeT>
inline void MaglevAssembler::CompareRootAndEmitEagerDeoptIf(
    Register reg, RootIndex index, Condition cond, DeoptimizeReason reason,
    NodeT* node) {
  Label Deopt, Done;

  CompareRootAndBranch(reg, index, cond, &Deopt);
  Jump(&Done, Label::kNear);
  bind(&Deopt);
  EmitEagerDeopt(node, reason);
  bind(&Done);
}

template <typename NodeT>
inline void MaglevAssembler::CompareMapWithRootAndEmitEagerDeoptIf(
    Register reg, RootIndex index, Register scratch, Condition cond,
    DeoptimizeReason reason, NodeT* node) {
  CompareMapWithRoot(reg, index, scratch);
  DCHECK_EQ(cond, kNotEqual);  // so far we only support kNotEqual, flag reg is
                               // 0 for equal, 1 for not equal
  EmitEagerDeoptIf(cond, reason,
                   node);  // Jump to deopt only if flag reg is not equal 0
}

template <typename NodeT>
inline void MaglevAssembler::CompareTaggedRootAndEmitEagerDeoptIf(
    Register reg, RootIndex index, Condition cond, DeoptimizeReason reason,
    NodeT* node) {
  DCHECK_EQ(cond, kNotEqual);
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register cmp_result = temps.AcquireScratch();
  MacroAssembler::CompareTaggedRoot(reg, index, cmp_result);
  Label* deopt_label = GetDeoptLabel(node, reason);
  RecordComment("-- Jump to eager deopt");
  MacroAssembler::Branch(deopt_label, cond, cmp_result, Operand(zero_reg));
}

template <typename NodeT>
inline void MaglevAssembler::CompareUInt32AndEmitEagerDeoptIf(
    Register reg, int imm, Condition cond, DeoptimizeReason reason,
    NodeT* node) {
  Label Deopt, Done;

  MacroAssembler::Branch(&Deopt, cond, reg, Operand(imm), Label::kNear);
  Jump(&Done, Label::kNear);
  bind(&Deopt);
  EmitEagerDeopt(node, reason);
  bind(&Done);
}

inline void MaglevAssembler::CompareMapWithRoot(Register object,
                                                RootIndex index,
                                                Register scratch) {
  constexpr Register Jump_flag = MaglevAssembler::GetFlagsRegister();

  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index)) {
    LoadCompressedMap(scratch, object);
    MaglevAssembler::TemporaryRegisterScope temps(this);
    Register index_reg = temps.AcquireScratch();
    Li(index_reg, ReadOnlyRootPtr(index));
    MacroAssembler::CmpTagged(Jump_flag, scratch, index_reg);
    return;
  }
  LoadMap(scratch, object);
  MacroAssembler::CompareRoot(scratch, index,
                              Jump_flag);  // so 0 if equal, 1 if not
}

template <typename NodeT>
inline void MaglevAssembler::CompareInstanceTypeRangeAndEagerDeoptIf(
    Register map, Register instance_type_out, InstanceType lower_limit,
    InstanceType higher_limit, Condition cond, DeoptimizeReason reason,
    NodeT* node) {
  DCHECK_LT(lower_limit, higher_limit);
  Lhu(instance_type_out, FieldMemOperand(map, Map::kInstanceTypeOffset));
  Sub32(instance_type_out, instance_type_out, Operand(lower_limit));
  DCHECK_EQ(cond, kUnsignedGreaterThan);
  Label* deopt_label = GetDeoptLabel(node, reason);
  RecordComment("-- Jump to eager deopt");
  MacroAssembler::Branch(deopt_label, Ugreater, instance_type_out,
                         Operand(higher_limit - lower_limit));
}

inline void MaglevAssembler::CompareFloat64AndJumpIf(
    DoubleRegister src1, DoubleRegister src2, Condition cond, Label* target,
    Label* nan_failed, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Register scratch2 = temps.AcquireScratch();
  Register cmp = temps.AcquireScratch();
  // FIXME: check, which condition can be on input
  // If cond - condition for overflow, skip check for NaN,
  // such check implemented below, using fclass results
  if (cond != kOverflow && cond != kNoOverflow) {
    feq_d(scratch, src1, src1);
    feq_d(scratch2, src2, src2);
    And(scratch2, scratch, scratch2);
    MacroAssembler::Branch(nan_failed, equal, scratch2, Operand(zero_reg));
    // actual comparison
    FPUCondition fcond = ConditionToConditionCmpFPU(cond);
    MacroAssembler::CompareF64(cmp, fcond, src1, src2);
    MacroAssembler::Branch(target, not_equal, cmp, Operand(zero_reg), distance);
  } else {
    // Case for conditions connected with overflow should be checked,
    // and, maybe, removed in future (FPUCondition does not implement overflow
    // cases)
    fclass_d(scratch, src1);
    fclass_d(scratch2, src2);
    Or(scratch2, scratch, scratch2);
    And(cmp, scratch2, FClassFlag::kQuietNaN | FClassFlag::kSignalingNaN);
    MacroAssembler::Branch(nan_failed, not_equal, cmp, Operand(zero_reg));
    And(cmp, scratch2,
        FClassFlag::kNegativeInfinity | FClassFlag::kPositiveInfinity);
    MacroAssembler::Branch(target, not_equal, cmp, Operand(zero_reg), distance);
  }
}

inline void MaglevAssembler::CompareFloat64AndBranch(
    DoubleRegister src1, DoubleRegister src2, Condition cond,
    BasicBlock* if_true, BasicBlock* if_false, BasicBlock* next_block,
    BasicBlock* nan_failed) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch1 = temps.AcquireScratch();
  Register scratch2 = temps.AcquireScratch();
  // emit check for NaN
  feq_d(scratch1, src1, src1);
  feq_d(scratch2, src2, src2);
  Register any_nan = scratch2;
  And(any_nan, scratch1, scratch2);
  MacroAssembler::Branch(nan_failed->label(), equal, any_nan,
                         Operand(zero_reg));
  // actual comparison
  Register cmp = temps.AcquireScratch();
  bool fallthrough_when_true = (if_true == next_block);
  bool fallthrough_when_false = (if_false == next_block);
  Label* if_true_label = if_true->label();
  Label* if_false_label = if_false->label();
  if (fallthrough_when_false) {
    if (fallthrough_when_true) {
      // If both paths are a fallthrough, do nothing.
      DCHECK_EQ(if_true_label, if_false_label);
      return;
    }
    FPUCondition fcond = ConditionToConditionCmpFPU(cond);
    MacroAssembler::CompareF64(cmp, fcond, src1, src2);
    // Jump over the false block if true, otherwise fall through into it.
    MacroAssembler::Branch(if_true_label, ne, cmp, Operand(zero_reg),
                           Label::kFar);
  } else {
    FPUCondition neg_fcond = ConditionToConditionCmpFPU(NegateCondition(cond));
    MacroAssembler::CompareF64(cmp, neg_fcond, src1, src2);
    // Jump to the false block if true.
    MacroAssembler::Branch(if_false_label, ne, cmp, Operand(zero_reg),
                           Label::kFar);
    // Jump to the true block if it's not the next block.
    if (!fallthrough_when_true) {
      MacroAssembler::Branch(if_true_label, Label::kFar);
    }
  }
}

inline void MaglevAssembler::PrepareCallCFunction(int num_reg_arguments,
                                                  int num_double_registers) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  MacroAssembler::PrepareCallCFunction(num_reg_arguments, num_double_registers,
                                       scratch);
}

inline void MaglevAssembler::CallSelf() {
  DCHECK(allow_call());
  DCHECK(code_gen_state()->entry_label()->is_bound());
  MacroAssembler::Branch(code_gen_state()->entry_label());
}

inline void MaglevAssembler::Jump(Label* target, Label::Distance distance) {
  DCHECK(!IsDeoptLabel(target));
  MacroAssembler::Branch(target, distance);
}

inline void MaglevAssembler::JumpToDeopt(Label* target) {
  DCHECK(IsDeoptLabel(target));
  MacroAssembler::Branch(target);
}

inline void MaglevAssembler::EmitEagerDeoptStress(Label* target) {
  // TODO(olivf): On arm `--deopt-every-n-times` is currently not supported.
  // Supporting it would require to implement this method, additionally handle
  // deopt branches in Cbz, and handle all cases where we fall through to the
  // deopt branch (like Int32Divide).
}

inline void MaglevAssembler::JumpIf(Condition cond, Label* target,
                                    Label::Distance distance) {
  // NOTE: for now keep in mind that we always put the result of a comparison
  // into dedicated register ("set flag"), and then compare it with x0.
  constexpr Register aflag = MaglevAssembler::GetFlagsRegister();
  MacroAssembler::Branch(target, cond, aflag, Operand(zero_reg), distance);
}

inline void MaglevAssembler::JumpIfRoot(Register with, RootIndex index,
                                        Label* if_equal,
                                        Label::Distance distance) {
  MacroAssembler::JumpIfRoot(with, index, if_equal, distance);
}

inline void MaglevAssembler::JumpIfNotRoot(Register with, RootIndex index,
                                           Label* if_not_equal,
                                           Label::Distance distance) {
  MacroAssembler::JumpIfNotRoot(with, index, if_not_equal, distance);
}

inline void MaglevAssembler::JumpIfSmi(Register src, Label* on_smi,
                                       Label::Distance distance) {
  MacroAssembler::JumpIfSmi(src, on_smi, distance);
}

inline void MaglevAssembler::JumpIfNotSmi(Register src, Label* on_smi,
                                          Label::Distance distance) {
  MacroAssembler::JumpIfNotSmi(src, on_smi, distance);
}

void MaglevAssembler::JumpIfByte(Condition cc, Register value, int32_t byte,
                                 Label* target, Label::Distance distance) {
  MacroAssembler::Branch(target, cc, value, Operand(byte), distance);
}

void MaglevAssembler::JumpIfHoleNan(DoubleRegister value, Register scratch,
                                    Label* target, Label::Distance distance) {
  // TODO(leszeks): Right now this only accepts Zone-allocated target labels.
  // This works because all callsites are jumping to either a deopt, deferred
  // code, or a basic block. If we ever need to jump to an on-stack label, we
  // have to add support for it here change the caller to pass a ZoneLabelRef.
  DCHECK(compilation_info()->zone()->Contains(target));
  ZoneLabelRef is_hole = ZoneLabelRef::UnsafeFromLabelPointer(target);
  ZoneLabelRef is_not_hole(this);
  MaglevAssembler::TemporaryRegisterScope temps(this);

  Label* deferred_code = MakeDeferredCode(
      [](MaglevAssembler* masm, DoubleRegister value, Register scratch,
         ZoneLabelRef is_hole, ZoneLabelRef is_not_hole) {
        masm->ExtractHighWordFromF64(scratch, value);
        masm->CompareInt32AndJumpIf(scratch, kHoleNanUpper32, kEqual, *is_hole);
        masm->MacroAssembler::Branch(*is_not_hole);
      },
      value, scratch, is_hole, is_not_hole);
  Register scratch2 = temps.AcquireScratch();
  feq_d(scratch2, value, value);  // 0 if value is NaN
  MacroAssembler::Branch(deferred_code, equal, scratch2, Operand(zero_reg));
  bind(*is_not_hole);
}

void MaglevAssembler::JumpIfNotHoleNan(DoubleRegister value, Register scratch,
                                       Label* target,
                                       Label::Distance distance) {
  JumpIfNotNan(value, target, distance);
  ExtractHighWordFromF64(scratch, value);
  CompareInt32AndJumpIf(scratch, kHoleNanUpper32, kNotEqual, target, distance);
}

void MaglevAssembler::JumpIfNotHoleNan(MemOperand operand, Label* target,
                                       Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register upper_bits = temps.AcquireScratch();
  Load32U(upper_bits,
          MemOperand(operand.rm(), operand.offset() + (kDoubleSize / 2)));
  CompareInt32AndJumpIf(upper_bits, kHoleNanUpper32, kNotEqual, target,
                        distance);
}

void MaglevAssembler::JumpIfNan(DoubleRegister value, Label* target,
                                Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  feq_d(scratch, value, value);  // 0 if value is NaN
  MacroAssembler::Branch(target, equal, scratch, Operand(zero_reg), distance);
}

void MaglevAssembler::JumpIfNotNan(DoubleRegister value, Label* target,
                                   Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  feq_d(scratch, value, value);  // 1 if value is not NaN
  MacroAssembler::Branch(target, not_equal, scratch, Operand(zero_reg),
                         distance);
}

inline void MaglevAssembler::CompareInt32AndJumpIf(Register r1, Register r2,
                                                   Condition cond,
                                                   Label* target,
                                                   Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register r1w = temps.AcquireScratch();
  Register r2w = temps.AcquireScratch();
  // TODO(Yuri Gaevsky): is zero/sign extension really needed here?
  switch (cond) {
    case ult:
    case uge:
    case ule:
    case ugt:
      ZeroExtendWord(r1w, r1);
      ZeroExtendWord(r2w, r2);
      break;
    default:
      SignExtend32To64Bits(r1w, r1);
      SignExtend32To64Bits(r2w, r2);
  }
  MacroAssembler::Branch(target, cond, r1w, Operand(r2w), distance);
}

inline void MaglevAssembler::CompareIntPtrAndJumpIf(Register r1, int32_t value,
                                                    Condition cond,
                                                    Label* target,
                                                    Label::Distance distance) {
  MacroAssembler::Branch(target, cond, r1, Operand(value), distance);
}

void MaglevAssembler::CompareIntPtrAndJumpIf(Register r1, Register r2,
                                             Condition cond, Label* target,
                                             Label::Distance distance) {
  MacroAssembler::Branch(target, cond, r1, Operand(r2), distance);
}

inline void MaglevAssembler::CompareIntPtrAndBranch(
    Register r1, int32_t value, Condition cond, Label* if_true,
    Label::Distance true_distance, bool fallthrough_when_true, Label* if_false,
    Label::Distance false_distance, bool fallthrough_when_false) {
  // expect only specific conditions
  switch (cond) {
    case eq:
    case ne:
    case greater:
    case greater_equal:
    case less:
    case less_equal:
    case Ugreater:
    case Ugreater_equal:
    case Uless:
    case Uless_equal:
      break;  // expected
    case cc_always:
    default:
      UNREACHABLE();  // not expected
  }

  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register lhs = temps.AcquireScratch();
  if (fallthrough_when_false) {
    if (fallthrough_when_true) {
      // If both paths are a fallthrough, do nothing.
      DCHECK_EQ(if_true, if_false);
      return;
    }
    // Jump over the false block if true, otherwise fall through into it.
    MacroAssembler::Branch(if_true, cond, lhs, Operand(value), true_distance);
  } else {
    // Jump to the false block if true.
    MacroAssembler::Branch(if_false, NegateCondition(cond), lhs, Operand(value),
                           false_distance);
    // Jump to the true block if it's not the next block.
    if (!fallthrough_when_true) {
      MacroAssembler::Branch(if_true, true_distance);
    }
  }
}

inline void MaglevAssembler::CompareInt32AndJumpIf(Register r1, int32_t value,
                                                   Condition cond,
                                                   Label* target,
                                                   Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register r1w = temps.AcquireScratch();
  // TODO(Yuri Gaevsky): is zero/sign extension really needed here?
  switch (cond) {
    case ult:
    case uge:
    case ule:
    case ugt:
      ZeroExtendWord(r1w, r1);
      break;
    default:
      SignExtend32To64Bits(r1w, r1);
  }
  MacroAssembler::Branch(target, cond, r1w, Operand(value), distance);
}

inline void MaglevAssembler::CompareInt32AndAssert(Register r1, Register r2,
                                                   Condition cond,
                                                   AbortReason reason) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register r1w = temps.AcquireScratch();
  Register r2w = temps.AcquireScratch();
  // TODO(Yuri Gaevsky): is zero/sign extension really needed here?
  switch (cond) {
    case ult:
    case uge:
    case ule:
    case ugt:
      ZeroExtendWord(r1w, r1);
      ZeroExtendWord(r2w, r2);
      break;
    default:
      SignExtend32To64Bits(r1w, r1);
      SignExtend32To64Bits(r2w, r2);
  }
  MacroAssembler::Assert(cond, reason, r1w, Operand(r2w));
}

inline void MaglevAssembler::CompareInt32AndAssert(Register r1, int32_t value,
                                                   Condition cond,
                                                   AbortReason reason) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register r1w = temps.AcquireScratch();
  // TODO(Yuri Gaevsky): is zero/sign extension really needed here?
  switch (cond) {
    case ult:
    case uge:
    case ule:
    case ugt:
      ZeroExtendWord(r1w, r1);
      break;
    default:
      SignExtend32To64Bits(r1w, r1);
  }
  MacroAssembler::Assert(cond, reason, r1w, Operand(value));
}

inline void MaglevAssembler::CompareInt32AndBranch(
    Register r1, int32_t value, Condition cond, Label* if_true,
    Label::Distance true_distance, bool fallthrough_when_true, Label* if_false,
    Label::Distance false_distance, bool fallthrough_when_false) {
  // expect only specific conditions
  switch (cond) {
    case eq:
    case ne:
    case greater:
    case greater_equal:
    case less:
    case less_equal:
    case Ugreater:
    case Ugreater_equal:
    case Uless:
    case Uless_equal:
      break;  // expected
    case cc_always:
    default:
      UNREACHABLE();  // not expected
  }

  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register lhs = temps.AcquireScratch();
  if (fallthrough_when_false) {
    if (fallthrough_when_true) {
      // If both paths are a fallthrough, do nothing.
      DCHECK_EQ(if_true, if_false);
      return;
    }
    // TODO(Yuri Gaevsky): is zero/sign extension really needed here?
    switch (cond) {
      case ult:
      case uge:
      case ule:
      case ugt:
        ZeroExtendWord(lhs, r1);
        break;
      default:
        SignExtend32To64Bits(lhs, r1);
    }
    // Jump over the false block if true, otherwise fall through into it.
    MacroAssembler::Branch(if_true, cond, lhs, Operand(value), true_distance);
  } else {
    // TODO(Yuri Gaevsky): is zero/sign extension really needed here?
    switch (cond) {
      case ult:
      case uge:
      case ule:
      case ugt:
        ZeroExtendWord(lhs, r1);
        break;
      default:
        SignExtend32To64Bits(lhs, r1);
    }
    // Jump to the false block if true.
    MacroAssembler::Branch(if_false, NegateCondition(cond), lhs, Operand(value),
                           false_distance);
    // Jump to the true block if it's not the next block.
    if (!fallthrough_when_true) {
      MacroAssembler::Branch(if_true, true_distance);
    }
  }
}

inline void MaglevAssembler::CompareInt32AndBranch(
    Register r1, Register value, Condition cond, Label* if_true,
    Label::Distance true_distance, bool fallthrough_when_true, Label* if_false,
    Label::Distance false_distance, bool fallthrough_when_false) {
  // expect only specific conditions
  switch (cond) {
    case eq:
    case ne:
    case greater:
    case greater_equal:
    case less:
    case less_equal:
    case Ugreater:
    case Ugreater_equal:
    case Uless:
    case Uless_equal:
      break;  // expected
    case cc_always:
    default:
      UNREACHABLE();  // not expected
  }

  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register lhs = temps.AcquireScratch();
  Register rhs = temps.AcquireScratch();
  if (fallthrough_when_false) {
    if (fallthrough_when_true) {
      // If both paths are a fallthrough, do nothing.
      DCHECK_EQ(if_true, if_false);
      return;
    }
    // TODO(Yuri Gaevsky): is zero/sign extension really needed here?
    switch (cond) {
      case ult:
      case uge:
      case ule:
      case ugt:
        ZeroExtendWord(lhs, r1);
        ZeroExtendWord(rhs, value);
        break;
      default:
        SignExtend32To64Bits(lhs, r1);
        SignExtend32To64Bits(rhs, value);
    }
    // Jump over the false block if true, otherwise fall through into it.
    MacroAssembler::Branch(if_true, cond, lhs, Operand(rhs), true_distance);
  } else {
    switch (cond) {
      // TODO(Yuri Gaevsky): is zero/sign extension really needed here?
      case ult:
      case uge:
      case ule:
      case ugt:
        ZeroExtendWord(lhs, r1);
        ZeroExtendWord(rhs, value);
        break;
      default:
        SignExtend32To64Bits(lhs, r1);
        SignExtend32To64Bits(rhs, value);
    }
    // Jump to the false block if true.
    MacroAssembler::Branch(if_false, NegateCondition(cond), lhs, Operand(rhs),
                           false_distance);
    // Jump to the true block if it's not the next block.
    if (!fallthrough_when_true) {
      MacroAssembler::Branch(if_true, true_distance);
    }
  }
}

inline void MaglevAssembler::CompareSmiAndJumpIf(Register r1, Tagged<Smi> value,
                                                 Condition cond, Label* target,
                                                 Label::Distance distance) {
  AssertSmi(r1);
  CompareTaggedAndBranch(target, cond, r1, Operand(value), distance);
}

inline void MaglevAssembler::CompareByteAndJumpIf(MemOperand left, int8_t right,
                                                  Condition cond,
                                                  Register scratch,
                                                  Label* target,
                                                  Label::Distance distance) {
  LoadByte(scratch, left);
  MacroAssembler::Branch(target, cond, scratch, Operand(right), distance);
}

inline void MaglevAssembler::CompareTaggedAndJumpIf(Register r1,
                                                    Tagged<Smi> value,
                                                    Condition cond,
                                                    Label* target,
                                                    Label::Distance distance) {
  CompareTaggedAndBranch(target, cond, r1, Operand(value), distance);
}

inline void MaglevAssembler::CompareTaggedAndJumpIf(Register r1,
                                                    Handle<HeapObject> obj,
                                                    Condition cond,
                                                    Label* target,
                                                    Label::Distance distance) {
  CompareTaggedAndBranch(target, cond, r1, Operand(obj), distance);
}

inline void MaglevAssembler::CompareTaggedAndJumpIf(Register src1,
                                                    Register src2,
                                                    Condition cond,
                                                    Label* target,
                                                    Label::Distance distance) {
  CompareTaggedAndBranch(target, cond, src1, Operand(src2), distance);
}

inline void MaglevAssembler::CompareDoubleAndJumpIfZeroOrNaN(
    DoubleRegister reg, Label* target, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  fclass_d(scratch, reg);
  And(scratch, scratch,
      Operand(kNegativeZero | kPositiveZero | kSignalingNaN | kQuietNaN));
  MacroAssembler::Branch(target, not_equal, scratch, Operand(zero_reg),
                         distance);
}

inline void MaglevAssembler::CompareDoubleAndJumpIfZeroOrNaN(
    MemOperand operand, Label* target, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  DoubleRegister value_double = temps.AcquireScratchDouble();
  LoadDouble(value_double, operand);
  CompareDoubleAndJumpIfZeroOrNaN(value_double, target, distance);
}

inline void MaglevAssembler::TestInt32AndJumpIfAnySet(
    Register r1, int32_t mask, Label* target, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  // TODO(Yuri Gaevsky): is zero extension really needed here?
  if (mask < 0) {                      // high-bits are all 1s due to
    And(scratch, r1, Operand(mask));   // sign-promotion, so we need
    ZeroExtendWord(scratch, scratch);  // to clear them all
  } else {
    And(scratch, r1, Operand(mask));
  }
  MacroAssembler::Branch(target, kNotZero, scratch, Operand(zero_reg),
                         distance);
}

inline void MaglevAssembler::TestInt32AndJumpIfAnySet(
    MemOperand operand, int32_t mask, Label* target, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Lwu(scratch, operand);
  And(scratch, scratch, Operand(mask));
  MacroAssembler::Branch(target, kNotZero, scratch, Operand(zero_reg),
                         distance);
}

inline void MaglevAssembler::TestInt32AndJumpIfAllClear(
    Register r1, int32_t mask, Label* target, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  // TODO(Yuri Gaevsky): is zero extension really needed here?
  if (mask < 0) {                      // high-bits are all 1s due to
    And(scratch, r1, Operand(mask));   // sign-promotion, so we need
    ZeroExtendWord(scratch, scratch);  // to clear them all
  } else {
    And(scratch, r1, Operand(mask));
  }
  MacroAssembler::Branch(target, kZero, scratch, Operand(zero_reg), distance);
}

inline void MaglevAssembler::TestInt32AndJumpIfAllClear(
    MemOperand operand, int32_t mask, Label* target, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Lwu(scratch, operand);
  And(scratch, scratch, Operand(mask));
  MacroAssembler::Branch(target, kZero, scratch, Operand(zero_reg), distance);
}

inline void MaglevAssembler::TestUint8AndJumpIfAnySet(
    MemOperand operand, uint8_t mask, Label* target, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Lbu(scratch, operand);
  And(scratch, scratch, Operand(mask));
  MacroAssembler::Branch(target, kNotZero, scratch, Operand(zero_reg),
                         distance);
}

inline void MaglevAssembler::TestUint8AndJumpIfAllClear(
    MemOperand operand, uint8_t mask, Label* target, Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Lbu(scratch, operand);
  And(scratch, scratch, Operand(mask));
  MacroAssembler::Branch(target, kZero, scratch, Operand(zero_reg), distance);
}

inline void MaglevAssembler::LoadHeapNumberValue(DoubleRegister result,
                                                 Register heap_number) {
  LoadDouble(result,
             FieldMemOperand(heap_number, offsetof(HeapNumber, value_)));
}

inline void MaglevAssembler::LoadHeapInt32Value(Register result,
                                                Register heap_number) {
  Load32U(result, FieldMemOperand(heap_number, offsetof(HeapNumber, value_)));
}

inline void MaglevAssembler::StoreHeapInt32Value(Register value,
                                                 Register heap_number) {
  Sw(value, (FieldMemOperand(heap_number, offsetof(HeapNumber, value_))));
}

inline void MaglevAssembler::Int32ToDouble(DoubleRegister result,
                                           Register src) {
  Cvt_d_w(result, src);
}

inline void MaglevAssembler::IntPtrToDouble(DoubleRegister result,
                                            Register src) {
  fcvt_d_l(result, src);
}

inline void MaglevAssembler::Uint32ToDouble(DoubleRegister result,
                                            Register src) {
  Cvt_d_uw(result, src);
}

inline void MaglevAssembler::Pop(Register dst) { MacroAssembler::Pop(dst); }

inline void MaglevAssembler::AssertStackSizeCorrect() {
  if (v8_flags.slow_debug_code) {
    MaglevAssembler::TemporaryRegisterScope temps(this);
    Register scratch = temps.AcquireScratch();
    Add64(scratch, sp,
          Operand(code_gen_state()->stack_slots() * kSystemPointerSize +
                  StandardFrameConstants::kFixedFrameSizeFromFp));
    MacroAssembler::Assert(eq, AbortReason::kStackAccessBelowStackPointer,
                           scratch, Operand(fp));
  }
}

inline Condition MaglevAssembler::FunctionEntryStackCheck(
    int stack_check_offset) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register stack_cmp_reg = sp;
  if (stack_check_offset >= kStackLimitSlackForDeoptimizationInBytes) {
    stack_cmp_reg = temps.AcquireScratch();
    Sub64(stack_cmp_reg, sp, stack_check_offset);
  }
  Register interrupt_stack_limit = temps.AcquireScratch();
  LoadStackLimit(interrupt_stack_limit, StackLimitKind::kInterruptStackLimit);
  // Flags register is used in subsequent JumpIfs
  constexpr Register flags_reg = MaglevAssembler::GetFlagsRegister();
  // FLAGS = ( predicted stack pointer < stack limit ) ? 1 : 0
  //     0 - we're Ok
  //     1 - stack will be overflown
  CompareI(flags_reg, stack_cmp_reg, Operand(interrupt_stack_limit),
           Condition::kUnsignedLessThan);
  return kZero;
}

inline void MaglevAssembler::FinishCode() {
  ForceConstantPoolEmissionWithoutJump();
}

template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      Register src) {
  Move(dst, src);
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      MemOperand src) {
  switch (repr) {
    case MachineRepresentation::kWord32:
      return Lw(dst, src);
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kWord64:
      return LoadWord(dst, src);
    default:
      UNREACHABLE();
  }
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr,
                                      MemOperand dst, Register src) {
  switch (repr) {
    case MachineRepresentation::kWord32:
      return Sw(src, dst);
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kWord64:
      return StoreWord(src, dst);
    default:
      UNREACHABLE();
  }
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr,
                                      MemOperand dst, MemOperand src) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  MoveRepr(repr, scratch, src);
  MoveRepr(repr, dst, scratch);
}

inline void MaglevAssembler::MaybeEmitPlaceHolderForDeopt() {
  // Implemented only for x64.
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_RISCV_MAGLEV_ASSEMBLER_RISCV_INL_H_
