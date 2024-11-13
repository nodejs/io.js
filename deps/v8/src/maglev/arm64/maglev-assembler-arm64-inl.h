// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_
#define V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_

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

constexpr Condition ConditionForNaN() { return vs; }

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

class MaglevAssembler::TemporaryRegisterScope
    : public TemporaryRegisterScopeBase<TemporaryRegisterScope> {
  using Base = TemporaryRegisterScopeBase<TemporaryRegisterScope>;

 public:
  struct SavedData : public Base::SavedData {
    CPURegList available_scratch_;
    CPURegList available_fp_scratch_;
  };

  explicit TemporaryRegisterScope(MaglevAssembler* masm)
      : Base(masm), scratch_scope_(masm) {}
  explicit TemporaryRegisterScope(MaglevAssembler* masm,
                                  const SavedData& saved_data)
      : Base(masm, saved_data), scratch_scope_(masm) {
    scratch_scope_.SetAvailable(saved_data.available_scratch_);
    scratch_scope_.SetAvailableFP(saved_data.available_fp_scratch_);
  }

  Register AcquireScratch() {
    Register reg = scratch_scope_.AcquireX();
    CHECK(!available_.has(reg));
    return reg;
  }
  DoubleRegister AcquireScratchDouble() {
    DoubleRegister reg = scratch_scope_.AcquireD();
    CHECK(!available_double_.has(reg));
    return reg;
  }
  void IncludeScratch(Register reg) { scratch_scope_.Include(reg); }

  SavedData CopyForDefer() {
    return SavedData{
        CopyForDeferBase(),
        *scratch_scope_.Available(),
        *scratch_scope_.AvailableFP(),
    };
  }

  void ResetToDefaultImpl() {
    scratch_scope_.SetAvailable(masm_->DefaultTmpList());
    scratch_scope_.SetAvailableFP(masm_->DefaultFPTmpList());
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
  masm_->CmpTagged(map_, temp);
  masm_->JumpIf(cond, if_true, distance);
}

Register MapCompare::GetMap() {
  if (PointerCompressionIsEnabled()) {
    // Decompression is idempotent (UXTW operand is used), so this would return
    // a valid pointer even if called multiple times in a row.
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

template <typename... Args>
inline void PushAll(MaglevAssembler* masm, Args... args) {
  PushAllHelper<Args...>::Push(masm, args...);
}

template <typename... Args>
inline void PushAllReverse(MaglevAssembler* masm, Args... args) {
  PushAllHelper<Args...>::PushReverse(masm, args...);
}

template <>
struct PushAllHelper<> {
  static void Push(MaglevAssembler* masm) {}
  static void PushReverse(MaglevAssembler* masm) {}
};

template <typename T, typename... Args>
inline void PushIterator(MaglevAssembler* masm, base::iterator_range<T> range,
                         Args... args) {
  using value_type = typename base::iterator_range<T>::value_type;
  for (auto iter = range.begin(), end = range.end(); iter != end; ++iter) {
    value_type val1 = *iter;
    ++iter;
    if (iter == end) {
      PushAll(masm, val1, args...);
      return;
    }
    value_type val2 = *iter;
    masm->Push(val1, val2);
  }
  PushAll(masm, args...);
}

template <typename T, typename... Args>
inline void PushIteratorReverse(MaglevAssembler* masm,
                                base::iterator_range<T> range, Args... args) {
  using value_type = typename base::iterator_range<T>::value_type;
  using difference_type = typename base::iterator_range<T>::difference_type;
  difference_type count = std::distance(range.begin(), range.end());
  DCHECK_GE(count, 0);
  auto iter = range.rbegin();
  auto end = range.rend();
  if (count % 2 != 0) {
    PushAllReverse(masm, *iter, args...);
    ++iter;
  } else {
    PushAllReverse(masm, args...);
  }
  while (iter != end) {
    value_type val1 = *iter;
    ++iter;
    value_type val2 = *iter;
    ++iter;
    masm->Push(val1, val2);
  }
}

template <typename Arg1, typename Arg2>
inline void PushAligned(MaglevAssembler* masm, Arg1 arg1, Arg2 arg2) {
  if (AlreadyInARegister(arg1) || AlreadyInARegister(arg2)) {
    // If one of the operands is already in a register, there is no need
    // to reuse scratch registers, so two arguments can be pushed together.
    MaglevAssembler::TemporaryRegisterScope temps(masm);
    masm->MacroAssembler::Push(ToRegister(masm, &temps, arg1),
                               ToRegister(masm, &temps, arg2));
    return;
  }
  {
    // Push the first argument together with padding to ensure alignment.
    // The second argument is not pushed together with the first so we can
    // re-use any scratch registers used to materialise the first argument for
    // the second one.
    MaglevAssembler::TemporaryRegisterScope temps(masm);
    masm->MacroAssembler::Push(ToRegister(masm, &temps, arg1), padreg);
  }
  {
    MaglevAssembler::TemporaryRegisterScope temps(masm);
    masm->MacroAssembler::str(ToRegister(masm, &temps, arg2), MemOperand(sp));
  }
}

template <typename Arg>
struct PushAllHelper<Arg> {
  static void Push(MaglevAssembler* masm, Arg arg) {
    if constexpr (is_iterator_range<Arg>::value) {
      PushIterator(masm, arg);
    } else {
      FATAL("Unaligned push");
    }
  }
  static void PushReverse(MaglevAssembler* masm, Arg arg) {
    if constexpr (is_iterator_range<Arg>::value) {
      PushIteratorReverse(masm, arg);
    } else {
      PushAllReverse(masm, arg, padreg);
    }
  }
};

template <typename Arg1, typename Arg2, typename... Args>
struct PushAllHelper<Arg1, Arg2, Args...> {
  static void Push(MaglevAssembler* masm, Arg1 arg1, Arg2 arg2, Args... args) {
    if constexpr (is_iterator_range<Arg1>::value) {
      PushIterator(masm, arg1, arg2, args...);
    } else if constexpr (is_iterator_range<Arg2>::value) {
      if (arg2.begin() != arg2.end()) {
        auto val = *arg2.begin();
        PushAligned(masm, arg1, val);
        PushAll(masm,
                base::make_iterator_range(std::next(arg2.begin()), arg2.end()),
                args...);
      } else {
        PushAll(masm, arg1, args...);
      }
    } else {
      PushAligned(masm, arg1, arg2);
      PushAll(masm, args...);
    }
  }
  static void PushReverse(MaglevAssembler* masm, Arg1 arg1, Arg2 arg2,
                          Args... args) {
    if constexpr (is_iterator_range<Arg1>::value) {
      PushIteratorReverse(masm, arg1, arg2, args...);
    } else if constexpr (is_iterator_range<Arg2>::value) {
      if (arg2.begin() != arg2.end()) {
        auto val = *arg2.begin();
        PushAllReverse(
            masm,
            base::make_iterator_range(std::next(arg2.begin()), arg2.end()),
            args...);
        PushAligned(masm, val, arg1);
      } else {
        PushAllReverse(masm, arg1, args...);
      }
    } else {
      PushAllReverse(masm, args...);
      PushAligned(masm, arg2, arg1);
    }
  }
};

}  // namespace detail

template <typename... T>
void MaglevAssembler::Push(T... vals) {
  const int push_count = detail::CountPushHelper<T...>::Count(vals...);
  if (push_count % 2 == 0) {
    detail::PushAll(this, vals...);
  } else {
    detail::PushAll(this, padreg, vals...);
  }
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
    Bind(block->label());
  }
}

inline void MaglevAssembler::SmiTagInt32AndSetFlags(Register dst,
                                                    Register src) {
  if (SmiValuesAre31Bits()) {
    Adds(dst.W(), src.W(), src.W());
  } else {
    SmiTag(dst, src);
  }
}

inline void MaglevAssembler::CheckInt32IsSmi(Register obj, Label* fail,
                                             Register scratch) {
  DCHECK(!SmiValuesAre32Bits());

  Adds(wzr, obj.W(), obj.W());
  JumpIf(kOverflow, fail);
}

inline void MaglevAssembler::SmiAddConstant(Register dst, Register src,
                                            int value, Label* fail,
                                            Label::Distance distance) {
  AssertSmi(src);
  if (value != 0) {
    if (SmiValuesAre31Bits()) {
      Adds(dst.W(), src.W(), Immediate(Smi::FromInt(value)));
    } else {
      DCHECK(dst.IsX());
      Adds(dst.X(), src.X(), Immediate(Smi::FromInt(value)));
    }
    JumpIf(kOverflow, fail, distance);
  } else {
    Move(dst, src);
  }
}

inline void MaglevAssembler::SmiSubConstant(Register dst, Register src,
                                            int value, Label* fail,
                                            Label::Distance distance) {
  AssertSmi(src);
  if (value != 0) {
    if (SmiValuesAre31Bits()) {
      Subs(dst.W(), src.W(), Immediate(Smi::FromInt(value)));
    } else {
      DCHECK(dst.IsX());
      Subs(dst.X(), src.X(), Immediate(Smi::FromInt(value)));
    }
    JumpIf(kOverflow, fail, distance);
  } else {
    Move(dst, src);
  }
}

inline void MaglevAssembler::MoveHeapNumber(Register dst, double value) {
  Mov(dst, Operand::EmbeddedHeapNumber(value));
}

inline Condition MaglevAssembler::IsRootConstant(Input input,
                                                 RootIndex root_index) {
  if (input.operand().IsRegister()) {
    CompareRoot(ToRegister(input), root_index);
  } else {
    DCHECK(input.operand().IsStackSlot());
    TemporaryRegisterScope temps(this);
    Register scratch = temps.AcquireScratch();
    Ldr(scratch, ToMemOperand(input));
    CompareRoot(scratch, root_index);
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
  TemporaryRegisterScope scope(this);
  Register base = scope.AcquireScratch();
  if (COMPRESS_POINTERS_BOOL) {
    Ldr(base.W(), FieldMemOperand(object, JSTypedArray::kBasePointerOffset));
  } else {
    Ldr(base, FieldMemOperand(object, JSTypedArray::kBasePointerOffset));
  }
  Add(data_pointer, data_pointer, base);
}

inline MemOperand MaglevAssembler::TypedArrayElementOperand(
    Register data_pointer, Register index, int element_size) {
  Add(data_pointer, data_pointer,
      Operand(index, LSL, ShiftFromScale(element_size)));
  return MemOperand(data_pointer);
}

inline MemOperand MaglevAssembler::DataViewElementOperand(Register data_pointer,
                                                          Register index) {
  return MemOperand(data_pointer, index);
}

inline void MaglevAssembler::LoadTaggedFieldByIndex(Register result,
                                                    Register object,
                                                    Register index, int scale,
                                                    int offset) {
  Add(result, object, Operand(index, LSL, ShiftFromScale(scale)));
  MacroAssembler::LoadTaggedField(result, FieldMemOperand(result, offset));
}

inline void MaglevAssembler::LoadBoundedSizeFromObject(Register result,
                                                       Register object,
                                                       int offset) {
  Move(result, FieldMemOperand(object, offset));
#ifdef V8_ENABLE_SANDBOX
  Lsr(result, result, kBoundedSizeShift);
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
                         FixedArray::kHeaderSize);
}

void MaglevAssembler::LoadFixedArrayElementWithoutDecompressing(
    Register result, Register array, Register index) {
  if (v8_flags.debug_code) {
    AssertObjectType(array, FIXED_ARRAY_TYPE, AbortReason::kUnexpectedValue);
    CompareInt32AndAssert(index, 0, kUnsignedGreaterThanEqual,
                          AbortReason::kUnexpectedNegativeValue);
  }
  Add(result, array, Operand(index, LSL, kTaggedSizeLog2));
  MacroAssembler::LoadTaggedFieldWithoutDecompressing(
      result, FieldMemOperand(result, FixedArray::kHeaderSize));
}

void MaglevAssembler::LoadFixedDoubleArrayElement(DoubleRegister result,
                                                  Register array,
                                                  Register index) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  if (v8_flags.debug_code) {
    AssertObjectType(array, FIXED_DOUBLE_ARRAY_TYPE,
                     AbortReason::kUnexpectedValue);
    CompareInt32AndAssert(index, 0, kUnsignedGreaterThanEqual,
                          AbortReason::kUnexpectedNegativeValue);
  }
  Add(scratch, array, Operand(index, LSL, kDoubleSizeLog2));
  Ldr(result, FieldMemOperand(scratch, FixedArray::kHeaderSize));
}

inline void MaglevAssembler::StoreFixedDoubleArrayElement(
    Register array, Register index, DoubleRegister value) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Add(scratch, array, Operand(index, LSL, kDoubleSizeLog2));
  Str(value, FieldMemOperand(scratch, FixedArray::kHeaderSize));
}

inline void MaglevAssembler::LoadSignedField(Register result,
                                             MemOperand operand, int size) {
  if (size == 1) {
    Ldrsb(result, operand);
  } else if (size == 2) {
    Ldrsh(result, operand);
  } else {
    DCHECK_EQ(size, 4);
    Ldr(result.W(), operand);
  }
}

inline void MaglevAssembler::LoadUnsignedField(Register result,
                                               MemOperand operand, int size) {
  if (size == 1) {
    Ldrb(result.W(), operand);
  } else if (size == 2) {
    Ldrh(result.W(), operand);
  } else {
    DCHECK_EQ(size, 4);
    Ldr(result.W(), operand);
  }
}

inline void MaglevAssembler::SetSlotAddressForTaggedField(Register slot_reg,
                                                          Register object,
                                                          int offset) {
  Add(slot_reg, object, offset - kHeapObjectTag);
}
inline void MaglevAssembler::SetSlotAddressForFixedArrayElement(
    Register slot_reg, Register object, Register index) {
  Add(slot_reg, object, FixedArray::kHeaderSize - kHeapObjectTag);
  Add(slot_reg, slot_reg, Operand(index, LSL, kTaggedSizeLog2));
}

inline void MaglevAssembler::StoreTaggedFieldNoWriteBarrier(Register object,
                                                            int offset,
                                                            Register value) {
  MacroAssembler::StoreTaggedField(value, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::StoreFixedArrayElementNoWriteBarrier(
    Register array, Register index, Register value) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Add(scratch, array, Operand(index, LSL, kTaggedSizeLog2));
  MacroAssembler::StoreTaggedField(
      value, FieldMemOperand(scratch, FixedArray::kHeaderSize));
}

inline void MaglevAssembler::StoreTaggedSignedField(Register object, int offset,
                                                    Register value) {
  AssertSmi(value);
  MacroAssembler::StoreTaggedField(value, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::StoreTaggedSignedField(Register object, int offset,
                                                    Tagged<Smi> value) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Mov(scratch, value);
  MacroAssembler::StoreTaggedField(scratch, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::StoreInt32Field(Register object, int offset,
                                             int32_t value) {
  if (value == 0) {
    Str(wzr, FieldMemOperand(object, offset));
    return;
  }
  TemporaryRegisterScope scope(this);
  Register scratch = scope.AcquireScratch().W();
  Move(scratch, value);
  Str(scratch, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::StoreField(MemOperand operand, Register value,
                                        int size) {
  DCHECK(size == 1 || size == 2 || size == 4);
  if (size == 1) {
    Strb(value.W(), operand);
  } else if (size == 2) {
    Strh(value.W(), operand);
  } else {
    DCHECK_EQ(size, 4);
    Str(value.W(), operand);
  }
}

#ifdef V8_ENABLE_SANDBOX

inline void MaglevAssembler::StoreTrustedPointerFieldNoWriteBarrier(
    Register object, int offset, Register value) {
  MacroAssembler::StoreTrustedPointerField(value,
                                           FieldMemOperand(object, offset));
}

#endif  // V8_ENABLE_SANDBOX

inline void MaglevAssembler::ReverseByteOrder(Register value, int size) {
  if (size == 2) {
    Rev16(value, value);
    Sxth(value, value);
  } else if (size == 4) {
    Rev32(value, value);
  } else {
    DCHECK_EQ(size, 1);
  }
}

inline void MaglevAssembler::IncrementInt32(Register reg) {
  Add(reg.W(), reg.W(), Immediate(1));
}

inline void MaglevAssembler::DecrementInt32(Register reg) {
  Sub(reg.W(), reg.W(), Immediate(1));
}

inline void MaglevAssembler::AddInt32(Register reg, int amount) {
  Add(reg.W(), reg.W(), Immediate(amount));
}

inline void MaglevAssembler::AndInt32(Register reg, int mask) {
  And(reg.W(), reg.W(), Immediate(mask));
}

inline void MaglevAssembler::OrInt32(Register reg, int mask) {
  Orr(reg.W(), reg.W(), Immediate(mask));
}

inline void MaglevAssembler::ShiftLeft(Register reg, int amount) {
  Lsl(reg.W(), reg.W(), amount);
}

inline void MaglevAssembler::IncrementAddress(Register reg, int32_t delta) {
  Add(reg.X(), reg.X(), Immediate(delta));
}

inline void MaglevAssembler::LoadAddress(Register dst, MemOperand location) {
  DCHECK(location.IsImmediateOffset());
  Add(dst.X(), location.base(), Immediate(location.offset()));
}

inline void MaglevAssembler::Call(Label* target) { bl(target); }

inline void MaglevAssembler::EmitEnterExitFrame(int extra_slots,
                                                StackFrame::Type frame_type,
                                                Register c_function,
                                                Register scratch) {
  EnterExitFrame(scratch, extra_slots, frame_type);
}

inline void MaglevAssembler::Move(StackSlot dst, Register src) {
  Str(src, StackSlotOperand(dst));
}
inline void MaglevAssembler::Move(StackSlot dst, DoubleRegister src) {
  Str(src, StackSlotOperand(dst));
}
inline void MaglevAssembler::Move(Register dst, StackSlot src) {
  Ldr(dst, StackSlotOperand(src));
}
inline void MaglevAssembler::Move(DoubleRegister dst, StackSlot src) {
  Ldr(dst, StackSlotOperand(src));
}
inline void MaglevAssembler::Move(MemOperand dst, Register src) {
  Str(src, dst);
}
inline void MaglevAssembler::Move(Register dst, MemOperand src) {
  Ldr(dst, src);
}
inline void MaglevAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  Fmov(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Tagged<Smi> src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(Register dst, ExternalReference src) {
  Mov(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Register src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Tagged<TaggedIndex> i) {
  Mov(dst, i.ptr());
}
inline void MaglevAssembler::Move(Register dst, int32_t i) {
  Mov(dst.W(), Immediate(i));
}
inline void MaglevAssembler::Move(Register dst, uint32_t i) {
  Mov(dst.W(), Immediate(i));
}
inline void MaglevAssembler::Move(Register dst, IndirectPointerTag i) {
  Mov(dst, Immediate(i));
}
inline void MaglevAssembler::Move(DoubleRegister dst, double n) {
  Fmov(dst, n);
}
inline void MaglevAssembler::Move(DoubleRegister dst, Float64 n) {
  Fmov(dst, n.get_scalar());
}
inline void MaglevAssembler::Move(Register dst, Handle<HeapObject> obj) {
  Mov(dst, Operand(obj));
}
void MaglevAssembler::MoveTagged(Register dst, Handle<HeapObject> obj) {
#ifdef V8_COMPRESS_POINTERS
  Mov(dst.W(), Operand(obj, RelocInfo::COMPRESSED_EMBEDDED_OBJECT));
#else
  Mov(dst, Operand(obj));
#endif
}

inline void MaglevAssembler::LoadFloat32(DoubleRegister dst, MemOperand src) {
  Ldr(dst.S(), src);
  Fcvt(dst, dst.S());
}
inline void MaglevAssembler::StoreFloat32(MemOperand dst, DoubleRegister src) {
  TemporaryRegisterScope temps(this);
  DoubleRegister scratch = temps.AcquireScratchDouble();
  Fcvt(scratch.S(), src);
  Str(scratch.S(), dst);
}
inline void MaglevAssembler::LoadFloat64(DoubleRegister dst, MemOperand src) {
  Ldr(dst, src);
}
inline void MaglevAssembler::StoreFloat64(MemOperand dst, DoubleRegister src) {
  Str(src, dst);
}

inline void MaglevAssembler::LoadUnalignedFloat64(DoubleRegister dst,
                                                  Register base,
                                                  Register index) {
  LoadFloat64(dst, MemOperand(base, index));
}
inline void MaglevAssembler::LoadUnalignedFloat64AndReverseByteOrder(
    DoubleRegister dst, Register base, Register index) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Ldr(scratch, MemOperand(base, index));
  Rev(scratch, scratch);
  Fmov(dst, scratch);
}
inline void MaglevAssembler::StoreUnalignedFloat64(Register base,
                                                   Register index,
                                                   DoubleRegister src) {
  StoreFloat64(MemOperand(base, index), src);
}
inline void MaglevAssembler::ReverseByteOrderAndStoreUnalignedFloat64(
    Register base, Register index, DoubleRegister src) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  Fmov(scratch, src);
  Rev(scratch, scratch);
  Str(scratch, MemOperand(base, index));
}

inline void MaglevAssembler::SignExtend32To64Bits(Register dst, Register src) {
  Mov(dst, Operand(src.W(), SXTW));
}
inline void MaglevAssembler::NegateInt32(Register val) {
  Neg(val.W(), val.W());
}

inline void MaglevAssembler::ToUint8Clamped(Register result,
                                            DoubleRegister value, Label* min,
                                            Label* max, Label* done) {
  TemporaryRegisterScope temps(this);
  DoubleRegister scratch = temps.AcquireScratchDouble();
  Move(scratch, 0.0);
  Fcmp(scratch, value);
  // Set to 0 if NaN.
  B(vs, min);
  B(ge, min);
  Move(scratch, 255.0);
  Fcmp(value, scratch);
  B(ge, max);
  // if value in [0, 255], then round up to the nearest.
  Frintn(scratch, value);
  TruncateDoubleToInt32(result, scratch);
  B(done);
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
    Tst(scratch.W(), Immediate(JSArrayBuffer::WasDetachedBit::kMask));
    EmitEagerDeoptIf(ne, DeoptimizeReason::kArrayBufferWasDetached, node);
}

inline void MaglevAssembler::LoadByte(Register dst, MemOperand src) {
  Ldrb(dst, src);
}

inline Condition MaglevAssembler::IsCallableAndNotUndetectable(
    Register map, Register scratch) {
  Ldr(scratch.W(), FieldMemOperand(map, Map::kBitFieldOffset));
  And(scratch.W(), scratch.W(),
      Map::Bits1::IsUndetectableBit::kMask | Map::Bits1::IsCallableBit::kMask);
  Cmp(scratch.W(), Map::Bits1::IsCallableBit::kMask);
  return kEqual;
}

inline Condition MaglevAssembler::IsNotCallableNorUndetactable(
    Register map, Register scratch) {
  Ldr(scratch.W(), FieldMemOperand(map, Map::kBitFieldOffset));
  Tst(scratch.W(), Immediate(Map::Bits1::IsUndetectableBit::kMask |
                             Map::Bits1::IsCallableBit::kMask));
  return kEqual;
}

inline void MaglevAssembler::LoadInstanceType(Register instance_type,
                                              Register heap_object) {
  LoadMap(instance_type, heap_object);
  Ldrh(instance_type.W(),
       FieldMemOperand(instance_type, Map::kInstanceTypeOffset));
}

inline void MaglevAssembler::JumpIfObjectType(Register heap_object,
                                              InstanceType type, Label* target,
                                              Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  IsObjectType(heap_object, scratch, scratch, type);
  JumpIf(kEqual, target, distance);
}

inline void MaglevAssembler::JumpIfNotObjectType(Register heap_object,
                                                 InstanceType type,
                                                 Label* target,
                                                 Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  IsObjectType(heap_object, scratch, scratch, type);
  JumpIf(kNotEqual, target, distance);
}

inline void MaglevAssembler::AssertObjectType(Register heap_object,
                                              InstanceType type,
                                              AbortReason reason) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  AssertNotSmi(heap_object);
  IsObjectType(heap_object, scratch, scratch, type);
  Assert(kEqual, reason);
}

inline void MaglevAssembler::BranchOnObjectType(
    Register heap_object, InstanceType type, Label* if_true,
    Label::Distance true_distance, bool fallthrough_when_true, Label* if_false,
    Label::Distance false_distance, bool fallthrough_when_false) {
  TemporaryRegisterScope temps(this);
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
  IsObjectTypeInRange(heap_object, scratch, lower_limit, higher_limit);
  JumpIf(kUnsignedLessThanEqual, target, distance);
}

inline void MaglevAssembler::JumpIfObjectTypeNotInRange(
    Register heap_object, InstanceType lower_limit, InstanceType higher_limit,
    Label* target, Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  IsObjectTypeInRange(heap_object, scratch, lower_limit, higher_limit);
  JumpIf(kUnsignedGreaterThan, target, distance);
}

inline void MaglevAssembler::AssertObjectTypeInRange(Register heap_object,
                                                     InstanceType lower_limit,
                                                     InstanceType higher_limit,
                                                     AbortReason reason) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  AssertNotSmi(heap_object);
  IsObjectTypeInRange(heap_object, scratch, lower_limit, higher_limit);
  Assert(kUnsignedLessThanEqual, reason);
}

inline void MaglevAssembler::BranchOnObjectTypeInRange(
    Register heap_object, InstanceType lower_limit, InstanceType higher_limit,
    Label* if_true, Label::Distance true_distance, bool fallthrough_when_true,
    Label* if_false, Label::Distance false_distance,
    bool fallthrough_when_false) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  IsObjectTypeInRange(heap_object, scratch, lower_limit, higher_limit);
  Branch(kUnsignedLessThanEqual, if_true, true_distance, fallthrough_when_true,
         if_false, false_distance, fallthrough_when_false);
}

#if V8_STATIC_ROOTS_BOOL
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

inline void MaglevAssembler::CompareMapWithRoot(Register object,
                                                RootIndex index,
                                                Register scratch) {
  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index)) {
    LoadCompressedMap(scratch, object);
    CmpTagged(scratch, Immediate(ReadOnlyRootPtr(index)));
    return;
  }
  LoadMap(scratch, object);
  CompareRoot(scratch, index);
}

inline void MaglevAssembler::CompareInstanceType(Register map,
                                                 InstanceType instance_type) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  MacroAssembler::CompareInstanceType(map, scratch, instance_type);
}

inline void MaglevAssembler::CompareInstanceTypeRange(
    Register map, Register instance_type_out, InstanceType lower_limit,
    InstanceType higher_limit) {
  MacroAssembler::CompareInstanceTypeRange(map, instance_type_out, lower_limit,
                                           higher_limit);
}

inline void MaglevAssembler::CompareFloat64AndJumpIf(
    DoubleRegister src1, DoubleRegister src2, Condition cond, Label* target,
    Label* nan_failed, Label::Distance distance) {
  Fcmp(src1, src2);
  JumpIf(ConditionForNaN(), nan_failed);
  JumpIf(cond, target, distance);
}

inline void MaglevAssembler::CompareFloat64AndBranch(
    DoubleRegister src1, DoubleRegister src2, Condition cond,
    BasicBlock* if_true, BasicBlock* if_false, BasicBlock* next_block,
    BasicBlock* nan_failed) {
  Fcmp(src1, src2);
  JumpIf(ConditionForNaN(), nan_failed->label());
  Branch(cond, if_true, if_false, next_block);
}

inline void MaglevAssembler::PrepareCallCFunction(int num_reg_arguments,
                                                  int num_double_registers) {}

inline void MaglevAssembler::CallSelf() {
  DCHECK(allow_call());
  DCHECK(code_gen_state()->entry_label()->is_bound());
  Bl(code_gen_state()->entry_label());
}

inline void MaglevAssembler::Jump(Label* target, Label::Distance) {
  // Any eager deopts should go through JumpIf to enable us to support the
  // `--deopt-every-n-times` stress mode. See EmitEagerDeoptStress.
  DCHECK(!IsDeoptLabel(target));
  B(target);
}

inline void MaglevAssembler::JumpToDeopt(Label* target) {
  DCHECK(IsDeoptLabel(target));
  B(target);
}

inline void MaglevAssembler::EmitEagerDeoptStress(Label* target) {
  // TODO(olivf): On arm `--deopt-every-n-times` is currently not supported.
  // Supporting it would require to implement this method, additionally handle
  // deopt branches in Cbz, and handle all cases where we fall through to the
  // deopt branch (like Int32Divide).
}

inline void MaglevAssembler::JumpIf(Condition cond, Label* target,
                                    Label::Distance) {
  B(target, cond);
}

inline void MaglevAssembler::JumpIfRoot(Register with, RootIndex index,
                                        Label* if_equal,
                                        Label::Distance distance) {
  MacroAssembler::JumpIfRoot(with, index, if_equal);
}

inline void MaglevAssembler::JumpIfNotRoot(Register with, RootIndex index,
                                           Label* if_not_equal,
                                           Label::Distance distance) {
  MacroAssembler::JumpIfNotRoot(with, index, if_not_equal);
}

inline void MaglevAssembler::JumpIfSmi(Register src, Label* on_smi,
                                       Label::Distance distance) {
  MacroAssembler::JumpIfSmi(src, on_smi);
}

inline void MaglevAssembler::JumpIfNotSmi(Register src, Label* on_smi,
                                          Label::Distance distance) {
  MacroAssembler::JumpIfNotSmi(src, on_smi);
}

void MaglevAssembler::JumpIfByte(Condition cc, Register value, int32_t byte,
                                 Label* target, Label::Distance) {
  CompareAndBranch(value, Immediate(byte), cc, target);
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
  Fcmp(value, value);
  JumpIf(ConditionForNaN(),
         MakeDeferredCode(
             [](MaglevAssembler* masm, DoubleRegister value, Register scratch,
                ZoneLabelRef is_hole, ZoneLabelRef is_not_hole) {
               masm->Umov(scratch.W(), value.V2S(), 1);
               masm->CompareInt32AndJumpIf(scratch.W(), kHoleNanUpper32, kEqual,
                                           *is_hole);
               masm->Jump(*is_not_hole);
             },
             value, scratch, is_hole, is_not_hole));
  bind(*is_not_hole);
}

void MaglevAssembler::JumpIfNotHoleNan(DoubleRegister value, Register scratch,
                                       Label* target,
                                       Label::Distance distance) {
  JumpIfNotNan(value, target, distance);
  Umov(scratch.W(), value.V2S(), 1);
  CompareInt32AndJumpIf(scratch.W(), kHoleNanUpper32, kNotEqual, target,
                        distance);
}

void MaglevAssembler::JumpIfNotHoleNan(MemOperand operand, Label* target,
                                       Label::Distance distance) {
  MaglevAssembler::TemporaryRegisterScope temps(this);
  Register upper_bits = temps.AcquireScratch();
  DCHECK(operand.IsImmediateOffset() && operand.shift_amount() == 0);
  Ldr(upper_bits.W(),
      MemOperand(operand.base(), operand.offset() + (kDoubleSize / 2),
                 operand.addrmode()));
  CompareInt32AndJumpIf(upper_bits.W(), kHoleNanUpper32, kNotEqual, target,
                        distance);
}

void MaglevAssembler::JumpIfNan(DoubleRegister value, Label* target,
                                Label::Distance distance) {
  Fcmp(value, value);
  JumpIf(ConditionForNaN(), target, distance);
}

void MaglevAssembler::JumpIfNotNan(DoubleRegister value, Label* target,
                                   Label::Distance distance) {
  Fcmp(value, value);
  JumpIf(NegateCondition(ConditionForNaN()), target, distance);
}

inline void MaglevAssembler::CompareInt32AndJumpIf(Register r1, Register r2,
                                                   Condition cond,
                                                   Label* target,
                                                   Label::Distance distance) {
  CompareAndBranch(r1.W(), r2.W(), cond, target);
}

void MaglevAssembler::CompareIntPtrAndJumpIf(Register r1, Register r2,
                                             Condition cond, Label* target,
                                             Label::Distance distance) {
  CompareAndBranch(r1.X(), r2.X(), cond, target);
}

inline void MaglevAssembler::CompareInt32AndJumpIf(Register r1, int32_t value,
                                                   Condition cond,
                                                   Label* target,
                                                   Label::Distance distance) {
  CompareAndBranch(r1.W(), Immediate(value), cond, target);
}

inline void MaglevAssembler::CompareInt32AndAssert(Register r1, Register r2,
                                                   Condition cond,
                                                   AbortReason reason) {
  Cmp(r1.W(), r2.W());
  Assert(cond, reason);
}
inline void MaglevAssembler::CompareInt32AndAssert(Register r1, int32_t value,
                                                   Condition cond,
                                                   AbortReason reason) {
  Cmp(r1.W(), Immediate(value));
  Assert(cond, reason);
}

inline void MaglevAssembler::CompareInt32AndBranch(
    Register r1, int32_t value, Condition cond, Label* if_true,
    Label::Distance true_distance, bool fallthrough_when_true, Label* if_false,
    Label::Distance false_distance, bool fallthrough_when_false) {
  Cmp(r1.W(), Immediate(value));
  Branch(cond, if_true, true_distance, fallthrough_when_true, if_false,
         false_distance, fallthrough_when_false);
}

inline void MaglevAssembler::CompareInt32AndBranch(
    Register r1, Register value, Condition cond, Label* if_true,
    Label::Distance true_distance, bool fallthrough_when_true, Label* if_false,
    Label::Distance false_distance, bool fallthrough_when_false) {
  Cmp(r1.W(), value.W());
  Branch(cond, if_true, true_distance, fallthrough_when_true, if_false,
         false_distance, fallthrough_when_false);
}

inline void MaglevAssembler::CompareSmiAndJumpIf(Register r1, Tagged<Smi> value,
                                                 Condition cond, Label* target,
                                                 Label::Distance distance) {
  AssertSmi(r1);
  CompareTaggedAndBranch(r1, Immediate(value), cond, target);
}

inline void MaglevAssembler::CompareSmiAndAssert(Register r1, Tagged<Smi> value,
                                                 Condition cond,
                                                 AbortReason reason) {
  if (!v8_flags.debug_code) return;
  AssertSmi(r1);
  CmpTagged(r1, value);
  Assert(cond, reason);
}

inline void MaglevAssembler::CompareByteAndJumpIf(MemOperand left, int8_t right,
                                                  Condition cond,
                                                  Register scratch,
                                                  Label* target,
                                                  Label::Distance distance) {
  LoadByte(scratch.W(), left);
  CompareAndBranch(scratch.W(), Immediate(right), cond, target);
}

inline void MaglevAssembler::CompareTaggedAndJumpIf(Register r1,
                                                    Tagged<Smi> value,
                                                    Condition cond,
                                                    Label* target,
                                                    Label::Distance distance) {
  CompareTaggedAndBranch(r1, Immediate(value), cond, target);
}

inline void MaglevAssembler::CompareTaggedAndJumpIf(Register r1,
                                                    Handle<HeapObject> obj,
                                                    Condition cond,
                                                    Label* target,
                                                    Label::Distance distance) {
  CmpTagged(r1, Operand(obj, COMPRESS_POINTERS_BOOL
                                 ? RelocInfo::COMPRESSED_EMBEDDED_OBJECT
                                 : RelocInfo::FULL_EMBEDDED_OBJECT));
  JumpIf(cond, target, distance);
}

inline void MaglevAssembler::CompareTaggedAndJumpIf(Register src1,
                                                    Register src2,
                                                    Condition cond,
                                                    Label* target,
                                                    Label::Distance distance) {
  CmpTagged(src1, src2);
  JumpIf(cond, target, distance);
}

inline void MaglevAssembler::CompareDoubleAndJumpIfZeroOrNaN(
    DoubleRegister reg, Label* target, Label::Distance distance) {
  Fcmp(reg, 0.0);
  JumpIf(eq, target);
  JumpIf(vs, target);  // NaN check
}

inline void MaglevAssembler::CompareDoubleAndJumpIfZeroOrNaN(
    MemOperand operand, Label* target, Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  DoubleRegister value_double = temps.AcquireScratchDouble();
  Ldr(value_double, operand);
  CompareDoubleAndJumpIfZeroOrNaN(value_double, target, distance);
}

inline void MaglevAssembler::TestInt32AndJumpIfAnySet(
    Register r1, int32_t mask, Label* target, Label::Distance distance) {
  TestAndBranchIfAnySet(r1.W(), mask, target);
}

inline void MaglevAssembler::TestInt32AndJumpIfAnySet(
    MemOperand operand, int32_t mask, Label* target, Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  Register value = temps.AcquireScratch().W();
  Ldr(value, operand);
  TestAndBranchIfAnySet(value, mask, target);
}

inline void MaglevAssembler::TestInt32AndJumpIfAllClear(
    Register r1, int32_t mask, Label* target, Label::Distance distance) {
  TestAndBranchIfAllClear(r1.W(), mask, target);
}

inline void MaglevAssembler::TestInt32AndJumpIfAllClear(
    MemOperand operand, int32_t mask, Label* target, Label::Distance distance) {
  TemporaryRegisterScope temps(this);
  Register value = temps.AcquireScratch().W();
  Ldr(value, operand);
  TestAndBranchIfAllClear(value, mask, target);
}

inline void MaglevAssembler::LoadHeapNumberValue(DoubleRegister result,
                                                 Register heap_number) {
  Ldr(result, FieldMemOperand(heap_number, offsetof(HeapNumber, value_)));
}

inline void MaglevAssembler::Int32ToDouble(DoubleRegister result,
                                           Register src) {
  Scvtf(result, src.W());
}

inline void MaglevAssembler::Uint32ToDouble(DoubleRegister result,
                                            Register src) {
  Ucvtf(result, src.W());
}

inline void MaglevAssembler::Pop(Register dst) { Pop(dst, padreg); }

inline void MaglevAssembler::AssertStackSizeCorrect() {
  if (v8_flags.debug_code) {
    TemporaryRegisterScope temps(this);
    Register scratch = temps.AcquireScratch();
    Add(scratch, sp,
        RoundUp<2 * kSystemPointerSize>(
            code_gen_state()->stack_slots() * kSystemPointerSize +
            StandardFrameConstants::kFixedFrameSizeFromFp));
    Cmp(scratch, fp);
    Assert(eq, AbortReason::kStackAccessBelowStackPointer);
  }
}

inline Condition MaglevAssembler::FunctionEntryStackCheck(
    int stack_check_offset) {
  TemporaryRegisterScope temps(this);
  Register stack_cmp_reg = sp;
  if (stack_check_offset >= kStackLimitSlackForDeoptimizationInBytes) {
    stack_cmp_reg = temps.AcquireScratch();
    Sub(stack_cmp_reg, sp, stack_check_offset);
  }
  Register interrupt_stack_limit = temps.AcquireScratch();
  LoadStackLimit(interrupt_stack_limit, StackLimitKind::kInterruptStackLimit);
  Cmp(stack_cmp_reg, interrupt_stack_limit);
  return kUnsignedGreaterThanEqual;
}

inline void MaglevAssembler::FinishCode() {
  ForceConstantPoolEmissionWithoutJump();
}

template <typename NodeT>
inline void MaglevAssembler::EmitEagerDeoptIfNotEqual(DeoptimizeReason reason,
                                                      NodeT* node) {
  EmitEagerDeoptIf(ne, reason, node);
}

template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      Register src) {
  Mov(dst, src);
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      MemOperand src) {
  switch (repr) {
    case MachineRepresentation::kWord32:
      return Ldr(dst.W(), src);
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kWord64:
      return Ldr(dst, src);
    default:
      UNREACHABLE();
  }
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr,
                                      MemOperand dst, Register src) {
  switch (repr) {
    case MachineRepresentation::kWord32:
      return Str(src.W(), dst);
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kWord64:
      return Str(src, dst);
    default:
      UNREACHABLE();
  }
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr,
                                      MemOperand dst, MemOperand src) {
  TemporaryRegisterScope temps(this);
  Register scratch = temps.AcquireScratch();
  MoveRepr(repr, scratch, src);
  MoveRepr(repr, dst, scratch);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_
