// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/interpreter-assembler.h"

#include <limits>
#include <ostream>

#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/interpreter/bytecodes.h"
#include "src/interpreter/interpreter.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

using compiler::CodeAssemblerState;

InterpreterAssembler::InterpreterAssembler(CodeAssemblerState* state,
                                           Bytecode bytecode,
                                           OperandScale operand_scale)
    : CodeStubAssembler(state),
      bytecode_(bytecode),
      operand_scale_(operand_scale),
      TVARIABLE_CONSTRUCTOR(interpreted_frame_pointer_),
      TVARIABLE_CONSTRUCTOR(bytecode_array_,
                            Parameter<BytecodeArray>(
                                InterpreterDispatchDescriptor::kBytecodeArray)),
      TVARIABLE_CONSTRUCTOR(
          bytecode_offset_,
          UncheckedParameter<IntPtrT>(
              InterpreterDispatchDescriptor::kBytecodeOffset)),
      TVARIABLE_CONSTRUCTOR(dispatch_table_,
                            UncheckedParameter<ExternalReference>(
                                InterpreterDispatchDescriptor::kDispatchTable)),
      TVARIABLE_CONSTRUCTOR(
          accumulator_,
          Parameter<Object>(InterpreterDispatchDescriptor::kAccumulator)),
      implicit_register_use_(ImplicitRegisterUse::kNone),
      made_call_(false),
      reloaded_frame_ptr_(false),
      bytecode_array_valid_(true) {
#ifdef V8_TRACE_UNOPTIMIZED
  TraceBytecode(Runtime::kTraceUnoptimizedBytecodeEntry);
#endif
  RegisterCallGenerationCallbacks([this] { CallPrologue(); },
                                  [this] { CallEpilogue(); });

  // Save the bytecode offset immediately if bytecode will make a call along
  // the critical path, or it is a return bytecode.
  if (Bytecodes::MakesCallAlongCriticalPath(bytecode) ||
      Bytecodes::Returns(bytecode)) {
    SaveBytecodeOffset();
  }
}

InterpreterAssembler::~InterpreterAssembler() {
  // If the following check fails the handler does not use the
  // accumulator in the way described in the bytecode definitions in
  // bytecodes.h.
  DCHECK_EQ(implicit_register_use_,
            Bytecodes::GetImplicitRegisterUse(bytecode_));
  UnregisterCallGenerationCallbacks();
}

TNode<RawPtrT> InterpreterAssembler::GetInterpretedFramePointer() {
  if (!interpreted_frame_pointer_.IsBound()) {
    interpreted_frame_pointer_ = LoadParentFramePointer();
  } else if (Bytecodes::MakesCallAlongCriticalPath(bytecode_) && made_call_ &&
             !reloaded_frame_ptr_) {
    interpreted_frame_pointer_ = LoadParentFramePointer();
    reloaded_frame_ptr_ = true;
  }
  return interpreted_frame_pointer_.value();
}

TNode<IntPtrT> InterpreterAssembler::BytecodeOffset() {
  if (Bytecodes::MakesCallAlongCriticalPath(bytecode_) && made_call_ &&
      (bytecode_offset_.value() ==
       UncheckedParameter<IntPtrT>(
           InterpreterDispatchDescriptor::kBytecodeOffset))) {
    bytecode_offset_ = ReloadBytecodeOffset();
  }
  return bytecode_offset_.value();
}

TNode<IntPtrT> InterpreterAssembler::ReloadBytecodeOffset() {
  TNode<IntPtrT> offset = LoadAndUntagRegister(Register::bytecode_offset());
  if (operand_scale() != OperandScale::kSingle) {
    // Add one to the offset such that it points to the actual bytecode rather
    // than the Wide / ExtraWide prefix bytecode.
    offset = IntPtrAdd(offset, IntPtrConstant(1));
  }
  return offset;
}

void InterpreterAssembler::SaveBytecodeOffset() {
  TNode<IntPtrT> bytecode_offset = BytecodeOffset();
  if (operand_scale() != OperandScale::kSingle) {
    // Subtract one from the bytecode_offset such that it points to the Wide /
    // ExtraWide prefix bytecode.
    bytecode_offset = IntPtrSub(BytecodeOffset(), IntPtrConstant(1));
  }
  int store_offset =
      Register::bytecode_offset().ToOperand() * kSystemPointerSize;
  TNode<RawPtrT> base = GetInterpretedFramePointer();

  if (SmiValuesAre32Bits()) {
    int zero_offset = store_offset + 4;
    int payload_offset = store_offset;
#if V8_TARGET_LITTLE_ENDIAN
    std::swap(zero_offset, payload_offset);
#endif
    StoreNoWriteBarrier(MachineRepresentation::kWord32, base,
                        IntPtrConstant(zero_offset), Int32Constant(0));
    StoreNoWriteBarrier(MachineRepresentation::kWord32, base,
                        IntPtrConstant(payload_offset),
                        TruncateIntPtrToInt32(bytecode_offset));
  } else {
    StoreFullTaggedNoWriteBarrier(base, IntPtrConstant(store_offset),
                                  SmiTag(bytecode_offset));
  }
}

TNode<BytecodeArray> InterpreterAssembler::BytecodeArrayTaggedPointer() {
  // Force a re-load of the bytecode array after every call in case the debugger
  // has been activated.
  if (!bytecode_array_valid_) {
    bytecode_array_ = CAST(LoadRegister(Register::bytecode_array()));
    bytecode_array_valid_ = true;
  }
  return bytecode_array_.value();
}

TNode<ExternalReference> InterpreterAssembler::DispatchTablePointer() {
  if (Bytecodes::MakesCallAlongCriticalPath(bytecode_) && made_call_ &&
      (dispatch_table_.value() ==
       UncheckedParameter<ExternalReference>(
           InterpreterDispatchDescriptor::kDispatchTable))) {
    dispatch_table_ = ExternalConstant(
        ExternalReference::interpreter_dispatch_table_address(isolate()));
  }
  return dispatch_table_.value();
}

TNode<Object> InterpreterAssembler::GetAccumulatorUnchecked() {
  return accumulator_.value();
}

TNode<Object> InterpreterAssembler::GetAccumulator() {
  DCHECK(Bytecodes::ReadsAccumulator(bytecode_));
  implicit_register_use_ =
      implicit_register_use_ | ImplicitRegisterUse::kReadAccumulator;
  return GetAccumulatorUnchecked();
}

void InterpreterAssembler::SetAccumulator(TNode<Object> value) {
  DCHECK(Bytecodes::WritesAccumulator(bytecode_));
  implicit_register_use_ =
      implicit_register_use_ | ImplicitRegisterUse::kWriteAccumulator;
  accumulator_ = value;
}

void InterpreterAssembler::ClobberAccumulator(TNode<Object> clobber_value) {
  DCHECK(Bytecodes::ClobbersAccumulator(bytecode_));
  implicit_register_use_ =
      implicit_register_use_ | ImplicitRegisterUse::kClobberAccumulator;
  accumulator_ = clobber_value;
}

TNode<Context> InterpreterAssembler::GetContext() {
  return CAST(LoadRegister(Register::current_context()));
}

void InterpreterAssembler::SetContext(TNode<Context> value) {
  StoreRegister(value, Register::current_context());
}

TNode<Context> InterpreterAssembler::GetContextAtDepth(TNode<Context> context,
                                                       TNode<Uint32T> depth) {
  TVARIABLE(Context, cur_context, context);
  TVARIABLE(Uint32T, cur_depth, depth);

  Label context_found(this);

  Label context_search(this, {&cur_depth, &cur_context});

  // Fast path if the depth is 0.
  Branch(Word32Equal(depth, Int32Constant(0)), &context_found, &context_search);

  // Loop until the depth is 0.
  BIND(&context_search);
  {
    cur_depth = Unsigned(Int32Sub(cur_depth.value(), Int32Constant(1)));
    cur_context =
        CAST(LoadContextElement(cur_context.value(), Context::PREVIOUS_INDEX));

    Branch(Word32Equal(cur_depth.value(), Int32Constant(0)), &context_found,
           &context_search);
  }

  BIND(&context_found);
  return cur_context.value();
}

TNode<IntPtrT> InterpreterAssembler::RegisterLocation(
    TNode<IntPtrT> reg_index) {
  return Signed(
      IntPtrAdd(GetInterpretedFramePointer(), RegisterFrameOffset(reg_index)));
}

TNode<IntPtrT> InterpreterAssembler::RegisterLocation(Register reg) {
  return RegisterLocation(IntPtrConstant(reg.ToOperand()));
}

TNode<IntPtrT> InterpreterAssembler::RegisterFrameOffset(TNode<IntPtrT> index) {
  return TimesSystemPointerSize(index);
}

TNode<Object> InterpreterAssembler::LoadRegister(TNode<IntPtrT> reg_index) {
  return LoadFullTagged(GetInterpretedFramePointer(),
                        RegisterFrameOffset(reg_index));
}

TNode<Object> InterpreterAssembler::LoadRegister(Register reg) {
  return LoadFullTagged(GetInterpretedFramePointer(),
                        IntPtrConstant(reg.ToOperand() * kSystemPointerSize));
}

TNode<IntPtrT> InterpreterAssembler::LoadAndUntagRegister(Register reg) {
  TNode<RawPtrT> base = GetInterpretedFramePointer();
  int index = reg.ToOperand() * kSystemPointerSize;
  if (SmiValuesAre32Bits()) {
#if V8_TARGET_LITTLE_ENDIAN
    index += 4;
#endif
    return ChangeInt32ToIntPtr(Load<Int32T>(base, IntPtrConstant(index)));
  } else {
    return SmiToIntPtr(CAST(LoadFullTagged(base, IntPtrConstant(index))));
  }
}

TNode<Object> InterpreterAssembler::LoadRegisterAtOperandIndex(
    int operand_index) {
  return LoadRegister(BytecodeOperandReg(operand_index));
}

std::pair<TNode<Object>, TNode<Object>>
InterpreterAssembler::LoadRegisterPairAtOperandIndex(int operand_index) {
  DCHECK_EQ(OperandType::kRegPair,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  TNode<IntPtrT> first_reg_index = BytecodeOperandReg(operand_index);
  TNode<IntPtrT> second_reg_index = NextRegister(first_reg_index);
  return std::make_pair(LoadRegister(first_reg_index),
                        LoadRegister(second_reg_index));
}

InterpreterAssembler::RegListNodePair
InterpreterAssembler::GetRegisterListAtOperandIndex(int operand_index) {
  DCHECK(Bytecodes::IsRegisterListOperandType(
      Bytecodes::GetOperandType(bytecode_, operand_index)));
  DCHECK_EQ(OperandType::kRegCount,
            Bytecodes::GetOperandType(bytecode_, operand_index + 1));
  TNode<IntPtrT> base_reg = RegisterLocation(BytecodeOperandReg(operand_index));
  TNode<Uint32T> reg_count = BytecodeOperandCount(operand_index + 1);
  return RegListNodePair(base_reg, reg_count);
}

TNode<Object> InterpreterAssembler::LoadRegisterFromRegisterList(
    const RegListNodePair& reg_list, int index) {
  TNode<IntPtrT> location = RegisterLocationInRegisterList(reg_list, index);
  return LoadFullTagged(location);
}

TNode<IntPtrT> InterpreterAssembler::RegisterLocationInRegisterList(
    const RegListNodePair& reg_list, int index) {
  CSA_DCHECK(this,
             Uint32GreaterThan(reg_list.reg_count(), Int32Constant(index)));
  TNode<IntPtrT> offset = RegisterFrameOffset(IntPtrConstant(index));
  // Register indexes are negative, so subtract index from base location to get
  // location.
  return Signed(IntPtrSub(reg_list.base_reg_location(), offset));
}

void InterpreterAssembler::StoreRegister(TNode<Object> value, Register reg) {
  StoreFullTaggedNoWriteBarrier(
      GetInterpretedFramePointer(),
      IntPtrConstant(reg.ToOperand() * kSystemPointerSize), value);
}

void InterpreterAssembler::StoreRegister(TNode<Object> value,
                                         TNode<IntPtrT> reg_index) {
  StoreFullTaggedNoWriteBarrier(GetInterpretedFramePointer(),
                                RegisterFrameOffset(reg_index), value);
}

void InterpreterAssembler::StoreRegisterForShortStar(TNode<Object> value,
                                                     TNode<WordT> opcode) {
  DCHECK(Bytecodes::IsShortStar(bytecode_));
  implicit_register_use_ =
      implicit_register_use_ | ImplicitRegisterUse::kWriteShortStar;

  CSA_DCHECK(
      this, UintPtrGreaterThanOrEqual(opcode, UintPtrConstant(static_cast<int>(
                                                  Bytecode::kFirstShortStar))));
  CSA_DCHECK(
      this,
      UintPtrLessThanOrEqual(
          opcode, UintPtrConstant(static_cast<int>(Bytecode::kLastShortStar))));

  // Compute the constant that we can add to a Bytecode value to map the range
  // [Bytecode::kStar15, Bytecode::kStar0] to the range
  // [Register(15).ToOperand(), Register(0).ToOperand()].
  constexpr int short_star_to_operand =
      Register(0).ToOperand() - static_cast<int>(Bytecode::kStar0);
  // Make sure the values count in the right direction.
  static_assert(short_star_to_operand ==
                Register(1).ToOperand() - static_cast<int>(Bytecode::kStar1));

  TNode<IntPtrT> offset =
      IntPtrAdd(RegisterFrameOffset(Signed(opcode)),
                IntPtrConstant(short_star_to_operand * kSystemPointerSize));
  StoreFullTaggedNoWriteBarrier(GetInterpretedFramePointer(), offset, value);
}

void InterpreterAssembler::StoreRegisterAtOperandIndex(TNode<Object> value,
                                                       int operand_index) {
  StoreRegister(value, BytecodeOperandReg(operand_index));
}

void InterpreterAssembler::StoreRegisterPairAtOperandIndex(TNode<Object> value1,
                                                           TNode<Object> value2,
                                                           int operand_index) {
  DCHECK_EQ(OperandType::kRegOutPair,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  TNode<IntPtrT> first_reg_index = BytecodeOperandReg(operand_index);
  StoreRegister(value1, first_reg_index);
  TNode<IntPtrT> second_reg_index = NextRegister(first_reg_index);
  StoreRegister(value2, second_reg_index);
}

void InterpreterAssembler::StoreRegisterTripleAtOperandIndex(
    TNode<Object> value1, TNode<Object> value2, TNode<Object> value3,
    int operand_index) {
  DCHECK_EQ(OperandType::kRegOutTriple,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  TNode<IntPtrT> first_reg_index = BytecodeOperandReg(operand_index);
  StoreRegister(value1, first_reg_index);
  TNode<IntPtrT> second_reg_index = NextRegister(first_reg_index);
  StoreRegister(value2, second_reg_index);
  TNode<IntPtrT> third_reg_index = NextRegister(second_reg_index);
  StoreRegister(value3, third_reg_index);
}

TNode<IntPtrT> InterpreterAssembler::NextRegister(TNode<IntPtrT> reg_index) {
  // Register indexes are negative, so the next index is minus one.
  return Signed(IntPtrAdd(reg_index, IntPtrConstant(-1)));
}

TNode<IntPtrT> InterpreterAssembler::OperandOffset(int operand_index) {
  return IntPtrConstant(
      Bytecodes::GetOperandOffset(bytecode_, operand_index, operand_scale()));
}

TNode<Uint8T> InterpreterAssembler::BytecodeOperandUnsignedByte(
    int operand_index) {
  DCHECK_LT(operand_index, Bytecodes::NumberOfOperands(bytecode_));
  DCHECK_EQ(OperandSize::kByte, Bytecodes::GetOperandSize(
                                    bytecode_, operand_index, operand_scale()));
  TNode<IntPtrT> operand_offset = OperandOffset(operand_index);
  return Load<Uint8T>(BytecodeArrayTaggedPointer(),
                      IntPtrAdd(BytecodeOffset(), operand_offset));
}

TNode<Int8T> InterpreterAssembler::BytecodeOperandSignedByte(
    int operand_index) {
  DCHECK_LT(operand_index, Bytecodes::NumberOfOperands(bytecode_));
  DCHECK_EQ(OperandSize::kByte, Bytecodes::GetOperandSize(
                                    bytecode_, operand_index, operand_scale()));
  TNode<IntPtrT> operand_offset = OperandOffset(operand_index);
  return Load<Int8T>(BytecodeArrayTaggedPointer(),
                     IntPtrAdd(BytecodeOffset(), operand_offset));
}

TNode<Word32T> InterpreterAssembler::BytecodeOperandReadUnaligned(
    int relative_offset, MachineType result_type) {
  static const int kMaxCount = 4;
  DCHECK(!TargetSupportsUnalignedAccess());

  int count;
  switch (result_type.representation()) {
    case MachineRepresentation::kWord16:
      count = 2;
      break;
    case MachineRepresentation::kWord32:
      count = 4;
      break;
    default:
      UNREACHABLE();
  }
  MachineType msb_type =
      result_type.IsSigned() ? MachineType::Int8() : MachineType::Uint8();

#if V8_TARGET_LITTLE_ENDIAN
  const int kStep = -1;
  int msb_offset = count - 1;
#elif V8_TARGET_BIG_ENDIAN
  const int kStep = 1;
  int msb_offset = 0;
#else
#error "Unknown Architecture"
#endif

  // Read the most signicant bytecode into bytes[0] and then in order
  // down to least significant in bytes[count - 1].
  DCHECK_LE(count, kMaxCount);
  TNode<Word32T> bytes[kMaxCount];
  for (int i = 0; i < count; i++) {
    MachineType machine_type = (i == 0) ? msb_type : MachineType::Uint8();
    TNode<IntPtrT> offset =
        IntPtrConstant(relative_offset + msb_offset + i * kStep);
    TNode<IntPtrT> array_offset = IntPtrAdd(BytecodeOffset(), offset);
    bytes[i] = UncheckedCast<Word32T>(
        Load(machine_type, BytecodeArrayTaggedPointer(), array_offset));
  }

  // Pack LSB to MSB.
  TNode<Word32T> result = bytes[--count];
  for (int i = 1; --count >= 0; i++) {
    TNode<Int32T> shift = Int32Constant(i * kBitsPerByte);
    TNode<Word32T> value = Word32Shl(bytes[count], shift);
    result = Word32Or(value, result);
  }
  return result;
}

TNode<Uint16T> InterpreterAssembler::BytecodeOperandUnsignedShort(
    int operand_index) {
  DCHECK_LT(operand_index, Bytecodes::NumberOfOperands(bytecode_));
  DCHECK_EQ(
      OperandSize::kShort,
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale()));
  int operand_offset =
      Bytecodes::GetOperandOffset(bytecode_, operand_index, operand_scale());
  if (TargetSupportsUnalignedAccess()) {
    return Load<Uint16T>(
        BytecodeArrayTaggedPointer(),
        IntPtrAdd(BytecodeOffset(), IntPtrConstant(operand_offset)));
  } else {
    return UncheckedCast<Uint16T>(
        BytecodeOperandReadUnaligned(operand_offset, MachineType::Uint16()));
  }
}

TNode<Int16T> InterpreterAssembler::BytecodeOperandSignedShort(
    int operand_index) {
  DCHECK_LT(operand_index, Bytecodes::NumberOfOperands(bytecode_));
  DCHECK_EQ(
      OperandSize::kShort,
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale()));
  int operand_offset =
      Bytecodes::GetOperandOffset(bytecode_, operand_index, operand_scale());
  if (TargetSupportsUnalignedAccess()) {
    return Load<Int16T>(
        BytecodeArrayTaggedPointer(),
        IntPtrAdd(BytecodeOffset(), IntPtrConstant(operand_offset)));
  } else {
    return UncheckedCast<Int16T>(
        BytecodeOperandReadUnaligned(operand_offset, MachineType::Int16()));
  }
}

TNode<Uint32T> InterpreterAssembler::BytecodeOperandUnsignedQuad(
    int operand_index) {
  DCHECK_LT(operand_index, Bytecodes::NumberOfOperands(bytecode_));
  DCHECK_EQ(OperandSize::kQuad, Bytecodes::GetOperandSize(
                                    bytecode_, operand_index, operand_scale()));
  int operand_offset =
      Bytecodes::GetOperandOffset(bytecode_, operand_index, operand_scale());
  if (TargetSupportsUnalignedAccess()) {
    return Load<Uint32T>(
        BytecodeArrayTaggedPointer(),
        IntPtrAdd(BytecodeOffset(), IntPtrConstant(operand_offset)));
  } else {
    return UncheckedCast<Uint32T>(
        BytecodeOperandReadUnaligned(operand_offset, MachineType::Uint32()));
  }
}

TNode<Int32T> InterpreterAssembler::BytecodeOperandSignedQuad(
    int operand_index) {
  DCHECK_LT(operand_index, Bytecodes::NumberOfOperands(bytecode_));
  DCHECK_EQ(OperandSize::kQuad, Bytecodes::GetOperandSize(
                                    bytecode_, operand_index, operand_scale()));
  int operand_offset =
      Bytecodes::GetOperandOffset(bytecode_, operand_index, operand_scale());
  if (TargetSupportsUnalignedAccess()) {
    return Load<Int32T>(
        BytecodeArrayTaggedPointer(),
        IntPtrAdd(BytecodeOffset(), IntPtrConstant(operand_offset)));
  } else {
    return UncheckedCast<Int32T>(
        BytecodeOperandReadUnaligned(operand_offset, MachineType::Int32()));
  }
}

TNode<Int32T> InterpreterAssembler::BytecodeSignedOperand(
    int operand_index, OperandSize operand_size) {
  DCHECK(!Bytecodes::IsUnsignedOperandType(
      Bytecodes::GetOperandType(bytecode_, operand_index)));
  switch (operand_size) {
    case OperandSize::kByte:
      return BytecodeOperandSignedByte(operand_index);
    case OperandSize::kShort:
      return BytecodeOperandSignedShort(operand_index);
    case OperandSize::kQuad:
      return BytecodeOperandSignedQuad(operand_index);
    case OperandSize::kNone:
      UNREACHABLE();
  }
}

TNode<Uint32T> InterpreterAssembler::BytecodeUnsignedOperand(
    int operand_index, OperandSize operand_size) {
  DCHECK(Bytecodes::IsUnsignedOperandType(
      Bytecodes::GetOperandType(bytecode_, operand_index)));
  switch (operand_size) {
    case OperandSize::kByte:
      return BytecodeOperandUnsignedByte(operand_index);
    case OperandSize::kShort:
      return BytecodeOperandUnsignedShort(operand_index);
    case OperandSize::kQuad:
      return BytecodeOperandUnsignedQuad(operand_index);
    case OperandSize::kNone:
      UNREACHABLE();
  }
}

TNode<Uint32T> InterpreterAssembler::BytecodeOperandCount(int operand_index) {
  DCHECK_EQ(OperandType::kRegCount,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  return BytecodeUnsignedOperand(operand_index, operand_size);
}

TNode<Uint32T> InterpreterAssembler::BytecodeOperandFlag8(int operand_index) {
  DCHECK_EQ(OperandType::kFlag8,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  DCHECK_EQ(operand_size, OperandSize::kByte);
  return BytecodeUnsignedOperand(operand_index, operand_size);
}

TNode<Uint32T> InterpreterAssembler::BytecodeOperandFlag16(int operand_index) {
  DCHECK_EQ(OperandType::kFlag16,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  DCHECK_EQ(operand_size, OperandSize::kShort);
  return BytecodeUnsignedOperand(operand_index, operand_size);
}

TNode<Uint32T> InterpreterAssembler::BytecodeOperandUImm(int operand_index) {
  DCHECK_EQ(OperandType::kUImm,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  return BytecodeUnsignedOperand(operand_index, operand_size);
}

TNode<UintPtrT> InterpreterAssembler::BytecodeOperandUImmWord(
    int operand_index) {
  return ChangeUint32ToWord(BytecodeOperandUImm(operand_index));
}

TNode<Smi> InterpreterAssembler::BytecodeOperandUImmSmi(int operand_index) {
  return SmiFromUint32(BytecodeOperandUImm(operand_index));
}

TNode<Int32T> InterpreterAssembler::BytecodeOperandImm(int operand_index) {
  DCHECK_EQ(OperandType::kImm,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  return BytecodeSignedOperand(operand_index, operand_size);
}

TNode<IntPtrT> InterpreterAssembler::BytecodeOperandImmIntPtr(
    int operand_index) {
  return ChangeInt32ToIntPtr(BytecodeOperandImm(operand_index));
}

TNode<Smi> InterpreterAssembler::BytecodeOperandImmSmi(int operand_index) {
  return SmiFromInt32(BytecodeOperandImm(operand_index));
}

TNode<Uint32T> InterpreterAssembler::BytecodeOperandIdxInt32(
    int operand_index) {
  DCHECK_EQ(OperandType::kIdx,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  return BytecodeUnsignedOperand(operand_index, operand_size);
}

TNode<UintPtrT> InterpreterAssembler::BytecodeOperandIdx(int operand_index) {
  return ChangeUint32ToWord(BytecodeOperandIdxInt32(operand_index));
}

TNode<Smi> InterpreterAssembler::BytecodeOperandIdxSmi(int operand_index) {
  return SmiTag(Signed(BytecodeOperandIdx(operand_index)));
}

TNode<TaggedIndex> InterpreterAssembler::BytecodeOperandIdxTaggedIndex(
    int operand_index) {
  TNode<IntPtrT> index =
      ChangeInt32ToIntPtr(Signed(BytecodeOperandIdxInt32(operand_index)));
  return IntPtrToTaggedIndex(index);
}

TNode<UintPtrT> InterpreterAssembler::BytecodeOperandConstantPoolIdx(
    int operand_index) {
  DCHECK_EQ(OperandType::kIdx,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  return ChangeUint32ToWord(
      BytecodeUnsignedOperand(operand_index, operand_size));
}

TNode<IntPtrT> InterpreterAssembler::BytecodeOperandReg(int operand_index) {
  DCHECK(Bytecodes::IsRegisterOperandType(
      Bytecodes::GetOperandType(bytecode_, operand_index)));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  return ChangeInt32ToIntPtr(
      BytecodeSignedOperand(operand_index, operand_size));
}

TNode<Uint32T> InterpreterAssembler::BytecodeOperandRuntimeId(
    int operand_index) {
  DCHECK_EQ(OperandType::kRuntimeId,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  DCHECK_EQ(operand_size, OperandSize::kShort);
  return BytecodeUnsignedOperand(operand_index, operand_size);
}

TNode<UintPtrT> InterpreterAssembler::BytecodeOperandNativeContextIndex(
    int operand_index) {
  DCHECK_EQ(OperandType::kNativeContextIndex,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  return ChangeUint32ToWord(
      BytecodeUnsignedOperand(operand_index, operand_size));
}

TNode<Uint32T> InterpreterAssembler::BytecodeOperandIntrinsicId(
    int operand_index) {
  DCHECK_EQ(OperandType::kIntrinsicId,
            Bytecodes::GetOperandType(bytecode_, operand_index));
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode_, operand_index, operand_scale());
  DCHECK_EQ(operand_size, OperandSize::kByte);
  return BytecodeUnsignedOperand(operand_index, operand_size);
}

TNode<Object> InterpreterAssembler::LoadConstantPoolEntry(TNode<WordT> index) {
  TNode<TrustedFixedArray> constant_pool = CAST(LoadProtectedPointerField(
      BytecodeArrayTaggedPointer(), BytecodeArray::kConstantPoolOffset));
  return CAST(LoadArrayElement(constant_pool, TrustedFixedArray::kHeaderSize,
                               UncheckedCast<IntPtrT>(index), 0));
}

TNode<IntPtrT> InterpreterAssembler::LoadAndUntagConstantPoolEntry(
    TNode<WordT> index) {
  return SmiUntag(CAST(LoadConstantPoolEntry(index)));
}

TNode<Object> InterpreterAssembler::LoadConstantPoolEntryAtOperandIndex(
    int operand_index) {
  TNode<UintPtrT> index = BytecodeOperandConstantPoolIdx(operand_index);
  return LoadConstantPoolEntry(index);
}

TNode<IntPtrT>
InterpreterAssembler::LoadAndUntagConstantPoolEntryAtOperandIndex(
    int operand_index) {
  return SmiUntag(CAST(LoadConstantPoolEntryAtOperandIndex(operand_index)));
}

TNode<JSFunction> InterpreterAssembler::LoadFunctionClosure() {
  return CAST(LoadRegister(Register::function_closure()));
}

TNode<HeapObject> InterpreterAssembler::LoadFeedbackVector() {
  return CAST(LoadRegister(Register::feedback_vector()));
}

void InterpreterAssembler::CallPrologue() {
  if (!Bytecodes::MakesCallAlongCriticalPath(bytecode_)) {
    // Bytecodes that make a call along the critical path save the bytecode
    // offset in the bytecode handler's prologue. For other bytecodes, if
    // there are multiple calls in the bytecode handler, you need to spill
    // before each of them, unless SaveBytecodeOffset has explicitly been called
    // in a path that dominates _all_ of those calls (which we don't track).
    SaveBytecodeOffset();
  }

  bytecode_array_valid_ = false;
  made_call_ = true;
}

void InterpreterAssembler::CallEpilogue() {}

void InterpreterAssembler::CallJSAndDispatch(
    TNode<Object> function, TNode<Context> context, const RegListNodePair& args,
    ConvertReceiverMode receiver_mode) {
  DCHECK(Bytecodes::MakesCallAlongCriticalPath(bytecode_));
  DCHECK(Bytecodes::IsCallOrConstruct(bytecode_) ||
         bytecode_ == Bytecode::kInvokeIntrinsic);
  DCHECK_EQ(Bytecodes::GetReceiverMode(bytecode_), receiver_mode);

  TNode<Word32T> args_count = args.reg_count();
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    // Add receiver. It is not included in args as it is implicit.
    args_count = Int32Add(args_count, Int32Constant(kJSArgcReceiverSlots));
  }

  Builtin builtin = Builtins::InterpreterPushArgsThenCall(
      receiver_mode, InterpreterPushArgsMode::kOther);

  TailCallBuiltinThenBytecodeDispatch(builtin, context, args_count,
                                      args.base_reg_location(), function);
  // TailCallStubThenDispatch updates accumulator with result.
  implicit_register_use_ =
      implicit_register_use_ | ImplicitRegisterUse::kWriteAccumulator;
}

template <class... TArgs>
void InterpreterAssembler::CallJSAndDispatch(TNode<Object> function,
                                             TNode<Context> context,
                                             TNode<Word32T> arg_count,
                                             ConvertReceiverMode receiver_mode,
                                             TArgs... args) {
  DCHECK(Bytecodes::MakesCallAlongCriticalPath(bytecode_));
  DCHECK(Bytecodes::IsCallOrConstruct(bytecode_) ||
         bytecode_ == Bytecode::kInvokeIntrinsic);
  DCHECK_EQ(Bytecodes::GetReceiverMode(bytecode_), receiver_mode);
  Builtin builtin = Builtins::Call();

  arg_count = JSParameterCount(arg_count);
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    // The first argument parameter (the receiver) is implied to be undefined.
    TailCallBuiltinThenBytecodeDispatch(builtin, context, function, arg_count,
                                        args..., UndefinedConstant());
  } else {
    TailCallBuiltinThenBytecodeDispatch(builtin, context, function, arg_count,
                                        args...);
  }
  // TailCallStubThenDispatch updates accumulator with result.
  implicit_register_use_ =
      implicit_register_use_ | ImplicitRegisterUse::kWriteAccumulator;
}

// Instantiate CallJSAndDispatch() for argument counts used by interpreter
// generator.
template V8_EXPORT_PRIVATE void InterpreterAssembler::CallJSAndDispatch(
    TNode<Object> function, TNode<Context> context, TNode<Word32T> arg_count,
    ConvertReceiverMode receiver_mode);
template V8_EXPORT_PRIVATE void InterpreterAssembler::CallJSAndDispatch(
    TNode<Object> function, TNode<Context> context, TNode<Word32T> arg_count,
    ConvertReceiverMode receiver_mode, TNode<Object>);
template V8_EXPORT_PRIVATE void InterpreterAssembler::CallJSAndDispatch(
    TNode<Object> function, TNode<Context> context, TNode<Word32T> arg_count,
    ConvertReceiverMode receiver_mode, TNode<Object>, TNode<Object>);
template V8_EXPORT_PRIVATE void InterpreterAssembler::CallJSAndDispatch(
    TNode<Object> function, TNode<Context> context, TNode<Word32T> arg_count,
    ConvertReceiverMode receiver_mode, TNode<Object>, TNode<Object>,
    TNode<Object>);

void InterpreterAssembler::CallJSWithSpreadAndDispatch(
    TNode<Object> function, TNode<Context> context, const RegListNodePair& args,
    TNode<UintPtrT> slot_id) {
  DCHECK(Bytecodes::MakesCallAlongCriticalPath(bytecode_));
  DCHECK_EQ(Bytecodes::GetReceiverMode(bytecode_), ConvertReceiverMode::kAny);

#ifndef V8_JITLESS
  TNode<HeapObject> maybe_feedback_vector = LoadFeedbackVector();
  LazyNode<Object> receiver = [=, this] {
    return LoadRegisterAtOperandIndex(1);
  };
  CollectCallFeedback(function, receiver, context, maybe_feedback_vector,
                      slot_id);
#endif  // !V8_JITLESS

  Comment("call using CallWithSpread builtin");
  Builtin builtin = Builtins::InterpreterPushArgsThenCall(
      ConvertReceiverMode::kAny, InterpreterPushArgsMode::kWithFinalSpread);

  TNode<Word32T> args_count = args.reg_count();
  TailCallBuiltinThenBytecodeDispatch(builtin, context, args_count,
                                      args.base_reg_location(), function);
  // TailCallStubThenDispatch updates accumulator with result.
  implicit_register_use_ =
      implicit_register_use_ | ImplicitRegisterUse::kWriteAccumulator;
}

TNode<Object> InterpreterAssembler::Construct(
    TNode<Object> target, TNode<Context> context, TNode<Object> new_target,
    const RegListNodePair& args, TNode<UintPtrT> slot_id,
    TNode<HeapObject> maybe_feedback_vector) {
  DCHECK(Bytecodes::MakesCallAlongCriticalPath(bytecode_));
  TVARIABLE(Object, var_result);
  TVARIABLE(AllocationSite, var_site);
  Label return_result(this), try_fast_construct(this), construct_generic(this),
      construct_array(this, &var_site);

  TNode<Word32T> args_count = JSParameterCount(args.reg_count());
  // TODO(42200059): Propagate TaggedIndex usage.
  CollectConstructFeedback(context, target, new_target, maybe_feedback_vector,
                           IntPtrToTaggedIndex(Signed(slot_id)),
                           UpdateFeedbackMode::kOptionalFeedback,
                           &try_fast_construct, &construct_array, &var_site);

  BIND(&try_fast_construct);
  {
    Comment("call using FastConstruct builtin");
    GotoIf(TaggedIsSmi(target), &construct_generic);
    GotoIfNot(IsJSFunction(CAST(target)), &construct_generic);
    var_result =
        CallBuiltin(Builtin::kInterpreterPushArgsThenFastConstructFunction,
                    context, args_count, args.base_reg_location(), target,
                    new_target, UndefinedConstant());
    Goto(&return_result);
  }

  BIND(&construct_generic);
  {
    // TODO(bmeurer): Remove the generic type_info parameter from the Construct.
    Comment("call using Construct builtin");
    Builtin builtin = Builtins::InterpreterPushArgsThenConstruct(
        InterpreterPushArgsMode::kOther);
    var_result =
        CallBuiltin(builtin, context, args_count, args.base_reg_location(),
                    target, new_target, UndefinedConstant());
    Goto(&return_result);
  }

  BIND(&construct_array);
  {
    // TODO(bmeurer): Introduce a dedicated builtin to deal with the Array
    // constructor feedback collection inside of Ignition.
    Comment("call using ConstructArray builtin");
    Builtin builtin = Builtins::InterpreterPushArgsThenConstruct(
        InterpreterPushArgsMode::kArrayFunction);
    var_result =
        CallBuiltin(builtin, context, args_count, args.base_reg_location(),
                    target, new_target, var_site.value());
    Goto(&return_result);
  }

  BIND(&return_result);
  return var_result.value();
}

TNode<Object> InterpreterAssembler::ConstructWithSpread(
    TNode<Object> target, TNode<Context> context, TNode<Object> new_target,
    const RegListNodePair& args, TNode<UintPtrT> slot_id) {
  // TODO(bmeurer): Unify this with the Construct bytecode feedback
  // above once we have a way to pass the AllocationSite to the Array
  // constructor _and_ spread the last argument at the same time.
  DCHECK(Bytecodes::MakesCallAlongCriticalPath(bytecode_));

#ifndef V8_JITLESS
  // TODO(syg): Is the feedback collection logic here the same as
  // CollectConstructFeedback?
  Label extra_checks(this, Label::kDeferred), construct(this);
  TNode<HeapObject> maybe_feedback_vector = LoadFeedbackVector();
  GotoIf(IsUndefined(maybe_feedback_vector), &construct);
  TNode<FeedbackVector> feedback_vector = CAST(maybe_feedback_vector);

  // Increment the call count.
  IncrementCallCount(feedback_vector, slot_id);

  // Check if we have monomorphic {new_target} feedback already.
  TNode<HeapObjectReference> feedback =
      CAST(LoadFeedbackVectorSlot(feedback_vector, slot_id));
  Branch(IsWeakReferenceToObject(feedback, new_target), &construct,
         &extra_checks);

  BIND(&extra_checks);
  {
    Label check_initialized(this), initialize(this), mark_megamorphic(this);

    // Check if it is a megamorphic {new_target}.
    Comment("check if megamorphic");
    TNode<BoolT> is_megamorphic = TaggedEqual(
        feedback,
        HeapConstantNoHole(FeedbackVector::MegamorphicSentinel(isolate())));
    GotoIf(is_megamorphic, &construct);

    Comment("check if weak reference");
    GotoIfNot(IsWeakOrCleared(feedback), &check_initialized);

    // If the weak reference is cleared, we have a new chance to become
    // monomorphic.
    Comment("check if weak reference is cleared");
    Branch(IsCleared(feedback), &initialize, &mark_megamorphic);

    BIND(&check_initialized);
    {
      // Check if it is uninitialized.
      Comment("check if uninitialized");
      TNode<BoolT> is_uninitialized =
          TaggedEqual(feedback, UninitializedSymbolConstant());
      Branch(is_uninitialized, &initialize, &mark_megamorphic);
    }

    BIND(&initialize);
    {
      Comment("check if function in same native context");
      GotoIf(TaggedIsSmi(new_target), &mark_megamorphic);
      // Check if the {new_target} is a JSFunction or JSBoundFunction
      // in the current native context.
      TVARIABLE(HeapObject, var_current, CAST(new_target));
      Label loop(this, &var_current), done_loop(this);
      Goto(&loop);
      BIND(&loop);
      {
        Label if_boundfunction(this), if_function(this);
        TNode<HeapObject> current = var_current.value();
        TNode<Uint16T> current_instance_type = LoadInstanceType(current);
        GotoIf(InstanceTypeEqual(current_instance_type, JS_BOUND_FUNCTION_TYPE),
               &if_boundfunction);
        Branch(IsJSFunctionInstanceType(current_instance_type), &if_function,
               &mark_megamorphic);

        BIND(&if_function);
        {
          // Check that the JSFunction {current} is in the current native
          // context.
          TNode<Context> current_context =
              CAST(LoadObjectField(current, JSFunction::kContextOffset));
          TNode<NativeContext> current_native_context =
              LoadNativeContext(current_context);
          Branch(
              TaggedEqual(LoadNativeContext(context), current_native_context),
              &done_loop, &mark_megamorphic);
        }

        BIND(&if_boundfunction);
        {
          // Continue with the [[BoundTargetFunction]] of {current}.
          var_current = LoadObjectField<HeapObject>(
              current, JSBoundFunction::kBoundTargetFunctionOffset);
          Goto(&loop);
        }
      }
      BIND(&done_loop);
      StoreWeakReferenceInFeedbackVector(feedback_vector, slot_id,
                                         CAST(new_target));
      ReportFeedbackUpdate(feedback_vector, slot_id,
                           "ConstructWithSpread:Initialize");
      Goto(&construct);
    }

    BIND(&mark_megamorphic);
    {
      // MegamorphicSentinel is an immortal immovable object so
      // write-barrier is not needed.
      Comment("transition to megamorphic");
      DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kmegamorphic_symbol));
      StoreFeedbackVectorSlot(
          feedback_vector, slot_id,
          HeapConstantNoHole(FeedbackVector::MegamorphicSentinel(isolate())),
          SKIP_WRITE_BARRIER);
      ReportFeedbackUpdate(feedback_vector, slot_id,
                           "ConstructWithSpread:TransitionMegamorphic");
      Goto(&construct);
    }
  }

  BIND(&construct);
#endif  // !V8_JITLESS
  Comment("call using ConstructWithSpread builtin");
  Builtin builtin = Builtins::InterpreterPushArgsThenConstruct(
      InterpreterPushArgsMode::kWithFinalSpread);
  TNode<Word32T> args_count = JSParameterCount(args.reg_count());
  return CallBuiltin(builtin, context, args_count, args.base_reg_location(),
                     target, new_target, UndefinedConstant());
}

// TODO(v8:13249): Add a FastConstruct variant to avoid pushing arguments twice
// (once here, and once again in construct stub).
TNode<Object> InterpreterAssembler::ConstructForwardAllArgs(
    TNode<Object> target, TNode<Context> context, TNode<Object> new_target,
    TNode<TaggedIndex> slot_id) {
  DCHECK(Bytecodes::MakesCallAlongCriticalPath(bytecode_));
  TVARIABLE(Object, var_result);
  TVARIABLE(AllocationSite, var_site);

#ifndef V8_JITLESS
  Label construct(this);

  TNode<HeapObject> maybe_feedback_vector = LoadFeedbackVector();
  GotoIf(IsUndefined(maybe_feedback_vector), &construct);

  CollectConstructFeedback(context, target, new_target, maybe_feedback_vector,
                           slot_id, UpdateFeedbackMode::kOptionalFeedback,
                           &construct, &construct, &var_site);
  BIND(&construct);
#endif  // !V8_JITLESS

  return CallBuiltin(Builtin::kInterpreterForwardAllArgsThenConstruct, context,
                     target, new_target);
}

template <class T>
TNode<T> InterpreterAssembler::CallRuntimeN(TNode<Uint32T> function_id,
                                            TNode<Context> context,
                                            const RegListNodePair& args,
                                            int return_count) {
  DCHECK(Bytecodes::MakesCallAlongCriticalPath(bytecode_));
  DCHECK(Bytecodes::IsCallRuntime(bytecode_));

  // Get the function entry from the function id.
  TNode<RawPtrT> function_table = ReinterpretCast<RawPtrT>(ExternalConstant(
      ExternalReference::runtime_function_table_address(isolate())));
  TNode<Word32T> function_offset =
      Int32Mul(function_id, Int32Constant(sizeof(Runtime::Function)));
  TNode<WordT> function =
      IntPtrAdd(function_table, ChangeUint32ToWord(function_offset));
  TNode<RawPtrT> function_entry = Load<RawPtrT>(
      function, IntPtrConstant(offsetof(Runtime::Function, entry)));

  Builtin centry = Builtins::InterpreterCEntry(return_count);
  return CallBuiltin<T>(centry, context, args.reg_count(),
                        args.base_reg_location(), function_entry);
}

template V8_EXPORT_PRIVATE TNode<Object> InterpreterAssembler::CallRuntimeN(
    TNode<Uint32T> function_id, TNode<Context> context,
    const RegListNodePair& args, int return_count);
template V8_EXPORT_PRIVATE TNode<PairT<Object, Object>>
InterpreterAssembler::CallRuntimeN(TNode<Uint32T> function_id,
                                   TNode<Context> context,
                                   const RegListNodePair& args,
                                   int return_count);

TNode<Int32T> InterpreterAssembler::UpdateInterruptBudget(
    TNode<Int32T> weight) {
  TNode<JSFunction> function = LoadFunctionClosure();
  TNode<FeedbackCell> feedback_cell =
      LoadObjectField<FeedbackCell>(function, JSFunction::kFeedbackCellOffset);
  TNode<Int32T> old_budget = LoadObjectField<Int32T>(
      feedback_cell, FeedbackCell::kInterruptBudgetOffset);

  // Update budget by |weight| and check if it reaches zero.
  TNode<Int32T> new_budget = Int32Sub(old_budget, weight);
  // Update budget.
  StoreObjectFieldNoWriteBarrier(
      feedback_cell, FeedbackCell::kInterruptBudgetOffset, new_budget);
  return new_budget;
}

void InterpreterAssembler::DecreaseInterruptBudget(
    TNode<Int32T> weight, StackCheckBehavior stack_check_behavior) {
  Comment("[ DecreaseInterruptBudget");
  Label done(this), interrupt_check(this);

  // Assert that the weight is positive.
  CSA_DCHECK(this, Int32GreaterThanOrEqual(weight, Int32Constant(0)));

  // Make sure we include the current bytecode in the budget calculation.
  TNode<Int32T> weight_after_bytecode =
      Int32Add(weight, Int32Constant(CurrentBytecodeSize()));
  TNode<Int32T> new_budget = UpdateInterruptBudget(weight_after_bytecode);
  Branch(Int32GreaterThanOrEqual(new_budget, Int32Constant(0)), &done,
         &interrupt_check);

  BIND(&interrupt_check);
  TNode<JSFunction> function = LoadFunctionClosure();
  CallRuntime(stack_check_behavior == kEnableStackCheck
                  ? Runtime::kBytecodeBudgetInterruptWithStackCheck_Ignition
                  : Runtime::kBytecodeBudgetInterrupt_Ignition,
              GetContext(), function);
  Goto(&done);

  BIND(&done);

  Comment("] DecreaseInterruptBudget");
}

TNode<IntPtrT> InterpreterAssembler::Advance() {
  return Advance(CurrentBytecodeSize());
}

TNode<IntPtrT> InterpreterAssembler::Advance(int delta) {
  return Advance(IntPtrConstant(delta));
}

TNode<IntPtrT> InterpreterAssembler::Advance(TNode<IntPtrT> delta) {
  TNode<IntPtrT> next_offset = IntPtrAdd(BytecodeOffset(), delta);
  bytecode_offset_ = next_offset;
  return next_offset;
}

void InterpreterAssembler::JumpToOffset(TNode<IntPtrT> new_bytecode_offset) {
  DCHECK(!Bytecodes::IsStarLookahead(bytecode_, operand_scale_));
#ifdef V8_TRACE_UNOPTIMIZED
  TraceBytecode(Runtime::kTraceUnoptimizedBytecodeExit);
#endif
  bytecode_offset_ = new_bytecode_offset;
  TNode<RawPtrT> target_bytecode =
      UncheckedCast<RawPtrT>(LoadBytecode(new_bytecode_offset));
  DispatchToBytecode(target_bytecode, new_bytecode_offset);
}

void InterpreterAssembler::Jump(TNode<IntPtrT> jump_offset) {
  JumpToOffset(IntPtrAdd(BytecodeOffset(), jump_offset));
}

void InterpreterAssembler::JumpBackward(TNode<IntPtrT> jump_offset) {
  DecreaseInterruptBudget(TruncateIntPtrToInt32(jump_offset),
                          kEnableStackCheck);
  JumpToOffset(IntPtrSub(BytecodeOffset(), jump_offset));
}

void InterpreterAssembler::JumpConditional(TNode<BoolT> condition,
                                           TNode<IntPtrT> jump_offset) {
  Label match(this), no_match(this);

  Branch(condition, &match, &no_match);
  BIND(&match);
  Jump(jump_offset);
  BIND(&no_match);
  Dispatch();
}

void InterpreterAssembler::JumpConditionalByImmediateOperand(
    TNode<BoolT> condition, int operand_index) {
  Label match(this), no_match(this);

  Branch(condition, &match, &no_match);
  BIND(&match);
  TNode<IntPtrT> jump_offset = Signed(BytecodeOperandUImmWord(operand_index));
  Jump(jump_offset);
  BIND(&no_match);
  Dispatch();
}

void InterpreterAssembler::JumpConditionalByConstantOperand(
    TNode<BoolT> condition, int operand_index) {
  Label match(this), no_match(this);

  Branch(condition, &match, &no_match);
  BIND(&match);
  TNode<IntPtrT> jump_offset =
      LoadAndUntagConstantPoolEntryAtOperandIndex(operand_index);
  Jump(jump_offset);
  BIND(&no_match);
  Dispatch();
}

void InterpreterAssembler::JumpIfTaggedEqual(TNode<Object> lhs,
                                             TNode<Object> rhs,
                                             TNode<IntPtrT> jump_offset) {
  JumpConditional(TaggedEqual(lhs, rhs), jump_offset);
}

void InterpreterAssembler::JumpIfTaggedEqual(TNode<Object> lhs,
                                             TNode<Object> rhs,
                                             int operand_index) {
  JumpConditionalByImmediateOperand(TaggedEqual(lhs, rhs), operand_index);
}

void InterpreterAssembler::JumpIfTaggedEqualConstant(TNode<Object> lhs,
                                                     TNode<Object> rhs,
                                                     int operand_index) {
  JumpConditionalByConstantOperand(TaggedEqual(lhs, rhs), operand_index);
}

void InterpreterAssembler::JumpIfTaggedNotEqual(TNode<Object> lhs,
                                                TNode<Object> rhs,
                                                TNode<IntPtrT> jump_offset) {
  JumpConditional(TaggedNotEqual(lhs, rhs), jump_offset);
}

void InterpreterAssembler::JumpIfTaggedNotEqual(TNode<Object> lhs,
                                                TNode<Object> rhs,
                                                int operand_index) {
  JumpConditionalByImmediateOperand(TaggedNotEqual(lhs, rhs), operand_index);
}

void InterpreterAssembler::JumpIfTaggedNotEqualConstant(TNode<Object> lhs,
                                                        TNode<Object> rhs,
                                                        int operand_index) {
  JumpConditionalByConstantOperand(TaggedNotEqual(lhs, rhs), operand_index);
}

TNode<WordT> InterpreterAssembler::LoadBytecode(
    TNode<IntPtrT> bytecode_offset) {
  TNode<Uint8T> bytecode =
      Load<Uint8T>(BytecodeArrayTaggedPointer(), bytecode_offset);
  return ChangeUint32ToWord(bytecode);
}

TNode<IntPtrT> InterpreterAssembler::LoadParameterCountWithoutReceiver() {
  TNode<Int32T> parameter_count =
      LoadBytecodeArrayParameterCountWithoutReceiver(
          BytecodeArrayTaggedPointer());
  return ChangeInt32ToIntPtr(parameter_count);
}

void InterpreterAssembler::StarDispatchLookahead(TNode<WordT> target_bytecode) {
  Label do_inline_star(this), done(this);

  // Check whether the following opcode is one of the short Star codes. All
  // opcodes higher than the short Star variants are invalid, and invalid
  // opcodes are never deliberately written, so we can use a one-sided check.
  // This is no less secure than the normal-length Star handler, which performs
  // no validation on its operand.
  static_assert(static_cast<int>(Bytecode::kLastShortStar) + 1 ==
                static_cast<int>(Bytecode::kIllegal));
  static_assert(Bytecode::kIllegal == Bytecode::kLast);
  TNode<Int32T> first_short_star_bytecode =
      Int32Constant(static_cast<int>(Bytecode::kFirstShortStar));
  TNode<BoolT> is_star = Uint32GreaterThanOrEqual(
      TruncateWordToInt32(target_bytecode), first_short_star_bytecode);
  Branch(is_star, &do_inline_star, &done);

  BIND(&do_inline_star);
  {
    InlineShortStar(target_bytecode);

    // Rather than merging control flow to a single indirect jump, we can get
    // better branch prediction by duplicating it. This is because the
    // instruction following a merged X + StarN is a bad predictor of the
    // instruction following a non-merged X, and vice versa.
    DispatchToBytecode(LoadBytecode(BytecodeOffset()), BytecodeOffset());
  }
  BIND(&done);
}

void InterpreterAssembler::InlineShortStar(TNode<WordT> target_bytecode) {
  Bytecode previous_bytecode = bytecode_;
  ImplicitRegisterUse previous_acc_use = implicit_register_use_;

  // At this point we don't know statically what bytecode we're executing, but
  // kStar0 has the right attributes (namely, no operands) for any of the short
  // Star codes.
  bytecode_ = Bytecode::kStar0;
  implicit_register_use_ = ImplicitRegisterUse::kNone;

#ifdef V8_TRACE_UNOPTIMIZED
  TraceBytecode(Runtime::kTraceUnoptimizedBytecodeEntry);
#endif

  StoreRegisterForShortStar(GetAccumulator(), target_bytecode);

  DCHECK_EQ(implicit_register_use_,
            Bytecodes::GetImplicitRegisterUse(bytecode_));

  Advance();
  bytecode_ = previous_bytecode;
  implicit_register_use_ = previous_acc_use;
}

void InterpreterAssembler::Dispatch() {
  Comment("========= Dispatch");
  DCHECK_IMPLIES(Bytecodes::MakesCallAlongCriticalPath(bytecode_), made_call_);
  TNode<IntPtrT> target_offset = Advance();
  TNode<WordT> target_bytecode = LoadBytecode(target_offset);
  DispatchToBytecodeWithOptionalStarLookahead(target_bytecode);
}

void InterpreterAssembler::DispatchToBytecodeWithOptionalStarLookahead(
    TNode<WordT> target_bytecode) {
  if (Bytecodes::IsStarLookahead(bytecode_, operand_scale_)) {
    StarDispatchLookahead(target_bytecode);
  }
  DispatchToBytecode(target_bytecode, BytecodeOffset());
}

void InterpreterAssembler::DispatchToBytecode(
    TNode<WordT> target_bytecode, TNode<IntPtrT> new_bytecode_offset) {
  if (V8_IGNITION_DISPATCH_COUNTING_BOOL) {
    TraceBytecodeDispatch(target_bytecode);
  }

  TNode<RawPtrT> target_code_entry = Load<RawPtrT>(
      DispatchTablePointer(), TimesSystemPointerSize(target_bytecode));

  DispatchToBytecodeHandlerEntry(target_code_entry, new_bytecode_offset);
}

void InterpreterAssembler::DispatchToBytecodeHandlerEntry(
    TNode<RawPtrT> handler_entry, TNode<IntPtrT> bytecode_offset) {
  TailCallBytecodeDispatch(
      InterpreterDispatchDescriptor{}, handler_entry, GetAccumulatorUnchecked(),
      bytecode_offset, BytecodeArrayTaggedPointer(), DispatchTablePointer());
}

void InterpreterAssembler::DispatchWide(OperandScale operand_scale) {
  // Dispatching a wide bytecode requires treating the prefix
  // bytecode a base pointer into the dispatch table and dispatching
  // the bytecode that follows relative to this base.
  //
  //   Indices 0-255 correspond to bytecodes with operand_scale == 0
  //   Indices 256-511 correspond to bytecodes with operand_scale == 1
  //   Indices 512-767 correspond to bytecodes with operand_scale == 2
  DCHECK_IMPLIES(Bytecodes::MakesCallAlongCriticalPath(bytecode_), made_call_);
  TNode<IntPtrT> next_bytecode_offset = Advance(1);
  TNode<WordT> next_bytecode = LoadBytecode(next_bytecode_offset);

  if (V8_IGNITION_DISPATCH_COUNTING_BOOL) {
    TraceBytecodeDispatch(next_bytecode);
  }

  TNode<IntPtrT> base_index;
  switch (operand_scale) {
    case OperandScale::kDouble:
      base_index = IntPtrConstant(1 << kBitsPerByte);
      break;
    case OperandScale::kQuadruple:
      base_index = IntPtrConstant(2 << kBitsPerByte);
      break;
    default:
      UNREACHABLE();
  }
  TNode<WordT> target_index = IntPtrAdd(base_index, next_bytecode);
  TNode<RawPtrT> target_code_entry = Load<RawPtrT>(
      DispatchTablePointer(), TimesSystemPointerSize(target_index));

  DispatchToBytecodeHandlerEntry(target_code_entry, next_bytecode_offset);
}

void InterpreterAssembler::UpdateInterruptBudgetOnReturn() {
  // TODO(rmcilroy): Investigate whether it is worth supporting self
  // optimization of primitive functions like FullCodegen.

  // Update profiling count by the number of bytes between the end of the
  // current bytecode and the start of the first one, to simulate backedge to
  // start of function.
  //
  // With headers and current offset, the bytecode array layout looks like:
  //
  //           <---------- simulated backedge ----------
  // | header | first bytecode | .... | return bytecode |
  //  |<------ current offset ------->
  //  ^ tagged bytecode array pointer
  //
  // UpdateInterruptBudget already handles adding the bytecode size to the
  // length of the back-edge, so we just have to correct for the non-zero offset
  // of the first bytecode.

  TNode<Int32T> profiling_weight =
      Int32Sub(TruncateIntPtrToInt32(BytecodeOffset()),
               Int32Constant(kFirstBytecodeOffset));
  DecreaseInterruptBudget(profiling_weight, kDisableStackCheck);
}

TNode<Int8T> InterpreterAssembler::LoadOsrState(
    TNode<FeedbackVector> feedback_vector) {
  // We're loading an 8-bit field, mask it.
  return UncheckedCast<Int8T>(Word32And(
      LoadObjectField<Int8T>(feedback_vector, FeedbackVector::kOsrStateOffset),
      0xFF));
}

void InterpreterAssembler::Abort(AbortReason abort_reason) {
  TNode<Smi> abort_id = SmiConstant(abort_reason);
  CallRuntime(Runtime::kAbort, GetContext(), abort_id);
}

void InterpreterAssembler::AbortIfWordNotEqual(TNode<WordT> lhs,
                                               TNode<WordT> rhs,
                                               AbortReason abort_reason) {
  Label ok(this), abort(this, Label::kDeferred);
  Branch(WordEqual(lhs, rhs), &ok, &abort);

  BIND(&abort);
  Abort(abort_reason);
  Goto(&ok);

  BIND(&ok);
}

void InterpreterAssembler::OnStackReplacement(
    TNode<Context> context, TNode<FeedbackVector> feedback_vector,
    TNode<IntPtrT> relative_jump, TNode<Int32T> loop_depth,
    TNode<IntPtrT> feedback_slot, TNode<Int8T> osr_state,
    OnStackReplacementParams params) {
  // Three cases may cause us to attempt OSR, in the following order:
  //
  // 1) Presence of cached OSR Turbofan/Maglev code.
  // 2) Presence of cached OSR Sparkplug code.
  // 3) The OSR urgency exceeds the current loop depth - in that case, trigger
  //    a Turbofan OSR compilation.

  TVARIABLE(Object, maybe_target_code, SmiConstant(0));
  Label osr_to_opt(this), osr_to_sparkplug(this);

  // Case 1).
  {
    Label next(this);
    TNode<MaybeObject> maybe_cached_osr_code =
        LoadFeedbackVectorSlot(feedback_vector, feedback_slot);
    GotoIf(IsCleared(maybe_cached_osr_code), &next);
    maybe_target_code = GetHeapObjectAssumeWeak(maybe_cached_osr_code);

    // Is it marked_for_deoptimization? If yes, clear the slot.
    TNode<CodeWrapper> code_wrapper = CAST(maybe_target_code.value());
    maybe_target_code =
        LoadCodePointerFromObject(code_wrapper, CodeWrapper::kCodeOffset);
    GotoIfNot(IsMarkedForDeoptimization(CAST(maybe_target_code.value())),
              &osr_to_opt);
    StoreFeedbackVectorSlot(feedback_vector, Unsigned(feedback_slot),
                            ClearedValue(), UNSAFE_SKIP_WRITE_BARRIER);
    maybe_target_code = SmiConstant(0);

    Goto(&next);
    BIND(&next);
  }

  // Case 2).
  if (params == OnStackReplacementParams::kBaselineCodeIsCached) {
    Goto(&osr_to_sparkplug);
  } else {
    DCHECK_EQ(params, OnStackReplacementParams::kDefault);
    TNode<SharedFunctionInfo> sfi = LoadObjectField<SharedFunctionInfo>(
        LoadFunctionClosure(), JSFunction::kSharedFunctionInfoOffset);
    GotoIf(SharedFunctionInfoHasBaselineCode(sfi), &osr_to_sparkplug);

    // Case 3).
    {
      static_assert(FeedbackVector::OsrUrgencyBits::kShift == 0);
      TNode<Int32T> osr_urgency = Word32And(
          osr_state, Int32Constant(FeedbackVector::OsrUrgencyBits::kMask));
      GotoIf(Uint32LessThan(loop_depth, osr_urgency), &osr_to_opt);
      JumpBackward(relative_jump);
    }
  }

  BIND(&osr_to_opt);
  {
    TNode<Uint32T> length =
        LoadAndUntagBytecodeArrayLength(BytecodeArrayTaggedPointer());
    TNode<Uint32T> weight =
        Uint32Mul(length, Uint32Constant(v8_flags.osr_to_tierup));
    DecreaseInterruptBudget(Signed(weight), kDisableStackCheck);
    CallBuiltin(Builtin::kInterpreterOnStackReplacement, context,
                maybe_target_code.value());
    UpdateInterruptBudget(Int32Mul(Signed(weight), Int32Constant(-1)));
    JumpBackward(relative_jump);
  }

  BIND(&osr_to_sparkplug);
  {
    // We already compiled the baseline code, so we don't need to handle failed
    // compilation as in the Ignition -> Turbofan case. Therefore we can just
    // tailcall to the OSR builtin.
    SaveBytecodeOffset();
    TailCallBuiltin(Builtin::kInterpreterOnStackReplacement_ToBaseline,
                    context);
  }
}

void InterpreterAssembler::TraceBytecode(Runtime::FunctionId function_id) {
  CallRuntime(function_id, GetContext(), BytecodeArrayTaggedPointer(),
              SmiTag(BytecodeOffset()), GetAccumulatorUnchecked());
}

void InterpreterAssembler::TraceBytecodeDispatch(TNode<WordT> target_bytecode) {
  TNode<ExternalReference> counters_table = ExternalConstant(
      ExternalReference::interpreter_dispatch_counters(isolate()));
  TNode<IntPtrT> source_bytecode_table_index = IntPtrConstant(
      static_cast<int>(bytecode_) * (static_cast<int>(Bytecode::kLast) + 1));

  TNode<WordT> counter_offset = TimesSystemPointerSize(
      IntPtrAdd(source_bytecode_table_index, target_bytecode));
  TNode<IntPtrT> old_counter = Load<IntPtrT>(counters_table, counter_offset);

  Label counter_ok(this), counter_saturated(this, Label::kDeferred);

  TNode<BoolT> counter_reached_max = WordEqual(
      old_counter, IntPtrConstant(std::numeric_limits<uintptr_t>::max()));
  Branch(counter_reached_max, &counter_saturated, &counter_ok);

  BIND(&counter_ok);
  {
    TNode<IntPtrT> new_counter = IntPtrAdd(old_counter, IntPtrConstant(1));
    StoreNoWriteBarrier(MachineType::PointerRepresentation(), counters_table,
                        counter_offset, new_counter);
    Goto(&counter_saturated);
  }

  BIND(&counter_saturated);
}

// static
bool InterpreterAssembler::TargetSupportsUnalignedAccess() {
#if V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_RISCV32
  return false;
#elif V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_S390 || \
    V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_PPC ||   \
    V8_TARGET_ARCH_PPC64 || V8_TARGET_ARCH_LOONG64
  return true;
#else
#error "Unknown Architecture"
#endif
}

void InterpreterAssembler::AbortIfRegisterCountInvalid(
    TNode<FixedArray> parameters_and_registers, TNode<IntPtrT> parameter_count,
    TNode<UintPtrT> register_count) {
  TNode<IntPtrT> array_size =
      LoadAndUntagFixedArrayBaseLength(parameters_and_registers);

  Label ok(this), abort(this, Label::kDeferred);
  Branch(UintPtrLessThanOrEqual(IntPtrAdd(parameter_count, register_count),
                                array_size),
         &ok, &abort);

  BIND(&abort);
  Abort(AbortReason::kInvalidParametersAndRegistersInGenerator);
  Goto(&ok);

  BIND(&ok);
}

TNode<FixedArray> InterpreterAssembler::ExportParametersAndRegisterFile(
    TNode<FixedArray> array, const RegListNodePair& registers) {
  // Store the formal parameters (without receiver) followed by the
  // registers into the generator's internal parameters_and_registers field.
  TNode<IntPtrT> parameter_count = LoadParameterCountWithoutReceiver();
  TNode<UintPtrT> register_count = ChangeUint32ToWord(registers.reg_count());
  if (v8_flags.debug_code) {
    CSA_DCHECK(this, IntPtrEqual(registers.base_reg_location(),
                                 RegisterLocation(Register(0))));
    AbortIfRegisterCountInvalid(array, parameter_count, register_count);
  }

  {
    TVARIABLE(IntPtrT, var_index);
    var_index = IntPtrConstant(0);

    // Iterate over parameters and write them into the array.
    Label loop(this, &var_index), done_loop(this);

    TNode<IntPtrT> reg_base =
        IntPtrConstant(Register::FromParameterIndex(0).ToOperand() + 1);

    Goto(&loop);
    BIND(&loop);
    {
      TNode<IntPtrT> index = var_index.value();
      GotoIfNot(UintPtrLessThan(index, parameter_count), &done_loop);

      TNode<IntPtrT> reg_index = IntPtrAdd(reg_base, index);
      TNode<Object> value = LoadRegister(reg_index);

      StoreFixedArrayElement(array, index, value);

      var_index = IntPtrAdd(index, IntPtrConstant(1));
      Goto(&loop);
    }
    BIND(&done_loop);
  }

  {
    // Iterate over register file and write values into array.
    // The mapping of register to array index must match that used in
    // BytecodeGraphBuilder::VisitResumeGenerator.
    TVARIABLE(IntPtrT, var_index);
    var_index = IntPtrConstant(0);

    Label loop(this, &var_index), done_loop(this);
    Goto(&loop);
    BIND(&loop);
    {
      TNode<IntPtrT> index = var_index.value();
      GotoIfNot(UintPtrLessThan(index, register_count), &done_loop);

      TNode<IntPtrT> reg_index =
          IntPtrSub(IntPtrConstant(Register(0).ToOperand()), index);
      TNode<Object> value = LoadRegister(reg_index);

      TNode<IntPtrT> array_index = IntPtrAdd(parameter_count, index);
      StoreFixedArrayElement(array, array_index, value);

      var_index = IntPtrAdd(index, IntPtrConstant(1));
      Goto(&loop);
    }
    BIND(&done_loop);
  }

  return array;
}

TNode<FixedArray> InterpreterAssembler::ImportRegisterFile(
    TNode<FixedArray> array, const RegListNodePair& registers) {
  TNode<IntPtrT> parameter_count = LoadParameterCountWithoutReceiver();
  TNode<UintPtrT> register_count = ChangeUint32ToWord(registers.reg_count());
  if (v8_flags.debug_code) {
    CSA_DCHECK(this, IntPtrEqual(registers.base_reg_location(),
                                 RegisterLocation(Register(0))));
    AbortIfRegisterCountInvalid(array, parameter_count, register_count);
  }

  TVARIABLE(IntPtrT, var_index, IntPtrConstant(0));

  // Iterate over array and write values into register file.  Also erase the
  // array contents to not keep them alive artificially.
  Label loop(this, &var_index), done_loop(this);
  Goto(&loop);
  BIND(&loop);
  {
    TNode<IntPtrT> index = var_index.value();
    GotoIfNot(UintPtrLessThan(index, register_count), &done_loop);

    TNode<IntPtrT> array_index = IntPtrAdd(parameter_count, index);
    TNode<Object> value = LoadFixedArrayElement(array, array_index);

    TNode<IntPtrT> reg_index =
        IntPtrSub(IntPtrConstant(Register(0).ToOperand()), index);
    StoreRegister(value, reg_index);

    StoreFixedArrayElement(array, array_index, StaleRegisterConstant());

    var_index = IntPtrAdd(index, IntPtrConstant(1));
    Goto(&loop);
  }
  BIND(&done_loop);

  return array;
}

int InterpreterAssembler::CurrentBytecodeSize() const {
  return Bytecodes::Size(bytecode_, operand_scale_);
}

void InterpreterAssembler::ToNumberOrNumeric(Object::Conversion mode) {
  TNode<Object> object = GetAccumulator();
  TNode<Context> context = GetContext();

  TVARIABLE(Smi, var_type_feedback);
  TVARIABLE(Numeric, var_result);
  Label if_done(this), if_objectissmi(this), if_objectisheapnumber(this),
      if_objectisother(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(object), &if_objectissmi);
  Branch(IsHeapNumber(CAST(object)), &if_objectisheapnumber, &if_objectisother);

  BIND(&if_objectissmi);
  {
    var_result = CAST(object);
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kSignedSmall);
    Goto(&if_done);
  }

  BIND(&if_objectisheapnumber);
  {
    var_result = CAST(object);
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kNumber);
    Goto(&if_done);
  }

  BIND(&if_objectisother);
  {
    auto builtin = Builtin::kNonNumberToNumber;
    if (mode == Object::Conversion::kToNumeric) {
      builtin = Builtin::kNonNumberToNumeric;
      // Special case for collecting BigInt feedback.
      Label not_bigint(this);
      GotoIfNot(IsBigInt(CAST(object)), &not_bigint);
      {
        var_result = CAST(object);
        var_type_feedback = SmiConstant(BinaryOperationFeedback::kBigInt);
        Goto(&if_done);
      }
      BIND(&not_bigint);
    }

    // Convert {object} by calling out to the appropriate builtin.
    var_result = CAST(CallBuiltin(builtin, context, object));
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kAny);
    Goto(&if_done);
  }

  BIND(&if_done);

  // Record the type feedback collected for {object}.
  TNode<UintPtrT> slot_index = BytecodeOperandIdx(0);
  TNode<HeapObject> maybe_feedback_vector = LoadFeedbackVector();

  MaybeUpdateFeedback(var_type_feedback.value(), maybe_feedback_vector,
                      slot_index);

  SetAccumulator(var_result.value());
  Dispatch();
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
