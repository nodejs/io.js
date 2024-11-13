// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV32_INL_H_
#define V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV32_INL_H_

#include "src/heap/mutable-page-metadata.h"
#include "src/wasm/baseline/liftoff-assembler.h"
#include "src/wasm/baseline/riscv/liftoff-assembler-riscv-inl.h"
#include "src/wasm/wasm-objects.h"
namespace v8::internal::wasm {

namespace liftoff {

// Liftoff Frames.
//
//  slot      Frame
//       +--------------------+---------------------------
//  n+4  | optional padding slot to keep the stack 16 byte aligned.
//  n+3  |   parameter n      |
//  ...  |       ...          |
//   4   |   parameter 1      | or parameter 2
//   3   |   parameter 0      | or parameter 1
//   2   |  (result address)  | or parameter 0
//  -----+--------------------+---------------------------
//   1   | return addr (ra)   |
//   0   | previous frame (fp)|
//  -----+--------------------+  <-- frame ptr (fp)
//  -1   | StackFrame::WASM   |
//  -2   |     instance       |
//  -3   |     feedback vector|
//  -----+--------------------+---------------------------
//  -4   |     slot 0         |   ^
//  -5   |     slot 1         |   |
//       |                    | Frame slots
//       |                    |   |
//       |                    |   v
//       | optional padding slot to keep the stack 16 byte aligned.
//  -----+--------------------+  <-- stack ptr (sp)
//

#if defined(V8_TARGET_BIG_ENDIAN)
constexpr int32_t kLowWordOffset = 4;
constexpr int32_t kHighWordOffset = 0;
#else
constexpr int32_t kLowWordOffset = 0;
constexpr int32_t kHighWordOffset = 4;
#endif

inline MemOperand GetHalfStackSlot(int offset, RegPairHalf half) {
  int32_t half_offset =
      half == kLowWord ? 0 : LiftoffAssembler::kStackSlotSize / 2;
  return MemOperand(offset > 0 ? fp : sp, -offset + half_offset);
}

inline MemOperand GetMemOp(LiftoffAssembler* assm, Register addr,
                           Register offset, uintptr_t offset_imm,
                           unsigned shift_amount = 0) {
  DCHECK_NE(addr, kScratchReg2);
  DCHECK_NE(offset, kScratchReg2);
  if (is_uint31(offset_imm)) {
    int32_t offset_imm32 = static_cast<int32_t>(offset_imm);
    if (offset == no_reg) return MemOperand(addr, offset_imm32);
    if (shift_amount != 0) {
      assm->CalcScaledAddress(kScratchReg2, addr, offset, shift_amount);
    } else {
      assm->AddWord(kScratchReg2, offset, addr);
    }
    return MemOperand(kScratchReg2, offset_imm32);
  }
  // Offset immediate does not fit in 31 bits.
  assm->li(kScratchReg2, offset_imm);
  assm->AddWord(kScratchReg2, kScratchReg2, addr);
  if (offset != no_reg) {
    if (shift_amount != 0) {
      assm->CalcScaledAddress(kScratchReg2, kScratchReg2, offset, shift_amount);
    } else {
      assm->AddWord(kScratchReg2, kScratchReg2, offset);
    }
  }
  return MemOperand(kScratchReg2, 0);
}

inline void Load(LiftoffAssembler* assm, LiftoffRegister dst, Register base,
                 int32_t offset, ValueKind kind) {
  MemOperand src(base, offset);

  switch (kind) {
    case kI32:
    case kRef:
    case kRefNull:
    case kRtt:
      assm->Lw(dst.gp(), src);
      break;
    case kI64:
      assm->Lw(dst.low_gp(),
               MemOperand(base, offset + liftoff::kLowWordOffset));
      assm->Lw(dst.high_gp(),
               MemOperand(base, offset + liftoff::kHighWordOffset));
      break;
    case kF32:
      assm->LoadFloat(dst.fp(), src);
      break;
    case kF64:
      assm->LoadDouble(dst.fp(), src);
      break;
    case kS128:{
      assm->VU.set(kScratchReg, E8, m1);
      Register src_reg = src.offset() == 0 ? src.rm() : kScratchReg;
      if (src.offset() != 0) {
        assm->AddWord(src_reg, src.rm(), src.offset());
      }
      assm->vl(dst.fp().toV(), src_reg, 0, E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

inline void Store(LiftoffAssembler* assm, Register base, int32_t offset,
                  LiftoffRegister src, ValueKind kind) {
  MemOperand dst(base, offset);
  switch (kind) {
    case kI32:
    case kRefNull:
    case kRef:
    case kRtt:
      assm->Sw(src.gp(), dst);
      break;
    case kI64:
      assm->Sw(src.low_gp(),
               MemOperand(base, offset + liftoff::kLowWordOffset));
      assm->Sw(src.high_gp(),
               MemOperand(base, offset + liftoff::kHighWordOffset));
      break;
    case kF32:
      assm->StoreFloat(src.fp(), dst);
      break;
    case kF64:
      assm->StoreDouble(src.fp(), dst);
      break;
    case kS128:{
      assm->VU.set(kScratchReg, E8, m1);
      Register dst_reg = dst.offset() == 0 ? dst.rm() : kScratchReg;
      if (dst.offset() != 0) {
        assm->AddWord(kScratchReg, dst.rm(), dst.offset());
      }
      assm->vs(src.fp().toV(), dst_reg, 0, VSew::E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

inline void push(LiftoffAssembler* assm, LiftoffRegister reg, ValueKind kind) {
  switch (kind) {
    case kI32:
    case kRefNull:
    case kRef:
    case kRtt:
      assm->addi(sp, sp, -kSystemPointerSize);
      assm->Sw(reg.gp(), MemOperand(sp, 0));
      break;
    case kI64:
      assm->Push(reg.high_gp(), reg.low_gp());
      break;
    case kF32:
      assm->addi(sp, sp, -kSystemPointerSize);
      assm->StoreFloat(reg.fp(), MemOperand(sp, 0));
      break;
    case kF64:
      assm->addi(sp, sp, -kDoubleSize);
      assm->StoreDouble(reg.fp(), MemOperand(sp, 0));
      break;
    case kS128:{
      assm->VU.set(kScratchReg, E8, m1);
      assm->addi(sp, sp, -kSystemPointerSize * 4);
      assm->vs(reg.fp().toV(), sp, 0, VSew::E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

inline Register EnsureNoAlias(Assembler* assm, Register reg,
                              LiftoffRegister must_not_alias,
                              UseScratchRegisterScope* temps) {
  if (reg != must_not_alias.low_gp() && reg != must_not_alias.high_gp())
    return reg;
  Register tmp = temps->Acquire();
  DCHECK_NE(must_not_alias.low_gp(), tmp);
  DCHECK_NE(must_not_alias.high_gp(), tmp);
  assm->mv(tmp, reg);
  return tmp;
}
}  // namespace liftoff

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value) {
  switch (value.type().kind()) {
    case kI32:
      MacroAssembler::li(reg.gp(), Operand(value.to_i32()));
      break;
    case kI64: {
      int32_t low_word = value.to_i64();
      int32_t high_word = value.to_i64() >> 32;
      MacroAssembler::li(reg.low_gp(), Operand(low_word));
      MacroAssembler::li(reg.high_gp(), Operand(high_word));
      break;
    }
    case kF32:
      MacroAssembler::LoadFPRImmediate(reg.fp(),
                                       value.to_f32_boxed().get_bits());
      break;
    case kF64:
      MacroAssembler::LoadFPRImmediate(reg.fp(),
                                       value.to_f64_boxed().get_bits());
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::LoadTaggedPointer(Register dst, Register src_addr,
                                         Register offset_reg,
                                         int32_t offset_imm,
                                         uint32_t* protected_load_pc,
                                         bool needs_shift) {
  static_assert(kTaggedSize == kSystemPointerSize);
  Load(LiftoffRegister(dst), src_addr, offset_reg,
       static_cast<uint32_t>(offset_imm), LoadType::kI32Load, protected_load_pc,
       false, false, needs_shift);
}

void LiftoffAssembler::LoadProtectedPointer(Register dst, Register src_addr,
                                            int32_t offset) {
  static_assert(!V8_ENABLE_SANDBOX_BOOL);
  LoadTaggedPointer(dst, src_addr, no_reg, offset);
}

void LiftoffAssembler::LoadFullPointer(Register dst, Register src_addr,
                                       int32_t offset_imm) {
  MemOperand src_op = MemOperand(src_addr, offset_imm);
  LoadWord(dst, src_op);
}

void LiftoffAssembler::StoreTaggedPointer(Register dst_addr,
                                          Register offset_reg,
                                          int32_t offset_imm, Register src,
                                          LiftoffRegList pinned,
                                          uint32_t* protected_store_pc,
                                          SkipWriteBarrier skip_write_barrier) {
  static_assert(kTaggedSize == kInt32Size);
  UseScratchRegisterScope temps{this};
  Register actual_offset_reg = offset_reg;
  if (offset_reg != no_reg && offset_imm != 0) {
    if (cache_state()->is_used(LiftoffRegister(offset_reg))) {
      // The code below only needs a scratch register if the {MemOperand} given
      // to {str} has an offset outside the uint12 range. After doing the
      // addition below we will not pass an immediate offset to {str} though, so
      // we can use the scratch register here.
      actual_offset_reg = temps.Acquire();
    }
    Add32(actual_offset_reg, offset_reg, Operand(offset_imm));
  }
  MemOperand dst_op = MemOperand(kScratchReg, 0);
  if (actual_offset_reg == no_reg) {
    dst_op = MemOperand(dst_addr, offset_imm);
  } else {
    AddWord(kScratchReg, dst_addr, actual_offset_reg);
    dst_op = MemOperand(kScratchReg, 0);
  }

  if (protected_store_pc) *protected_store_pc = pc_offset();

  StoreWord(src, dst_op);

  if (skip_write_barrier || v8_flags.disable_write_barriers) return;

  // The write barrier.
  Label exit;
  CheckPageFlag(dst_addr, MemoryChunk::kPointersFromHereAreInterestingMask,
                kZero, &exit);
  JumpIfSmi(src, &exit);
  CheckPageFlag(src, MemoryChunk::kPointersToHereAreInterestingMask, eq, &exit);
  CallRecordWriteStubSaveRegisters(
      dst_addr,
      actual_offset_reg == no_reg ? Operand(offset_imm)
                                  : Operand(actual_offset_reg),
      SaveFPRegsMode::kSave, StubCallMode::kCallWasmRuntimeStub);
  bind(&exit);
}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uintptr_t offset_imm,
                            LoadType type, uint32_t* protected_load_pc,
                            bool /* is_load_mem */, bool /* i64_offset */,
                            bool needs_shift) {
  unsigned shift_amount = needs_shift ? type.size_log_2() : 0;
  MemOperand src_op =
      liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm, shift_amount);

  if (protected_load_pc) *protected_load_pc = pc_offset();
  switch (type.value()) {
    case LoadType::kI32Load8U:
      Lbu(dst.gp(), src_op);
      break;
    case LoadType::kI64Load8U:
      Lbu(dst.low_gp(), src_op);
      MacroAssembler::mv(dst.high_gp(), zero_reg);
      break;
    case LoadType::kI32Load8S:
      Lb(dst.gp(), src_op);
      break;
    case LoadType::kI64Load8S:
      Lb(dst.low_gp(), src_op);
      MacroAssembler::srai(dst.high_gp(), dst.low_gp(), 31);
      break;
    case LoadType::kI32Load16U:
      MacroAssembler::Lhu(dst.gp(), src_op);
      break;
    case LoadType::kI64Load16U:
      MacroAssembler::Lhu(dst.low_gp(), src_op);
      MacroAssembler::mv(dst.high_gp(), zero_reg);
      break;
    case LoadType::kI32Load16S:
      MacroAssembler::Lh(dst.gp(), src_op);
      break;
    case LoadType::kI64Load16S:
      MacroAssembler::Lh(dst.low_gp(), src_op);
      MacroAssembler::srai(dst.high_gp(), dst.low_gp(), 31);
      break;
    case LoadType::kI64Load32U:
      MacroAssembler::Lw(dst.low_gp(), src_op);
      MacroAssembler::mv(dst.high_gp(), zero_reg);
      break;
    case LoadType::kI64Load32S:
      MacroAssembler::Lw(dst.low_gp(), src_op);
      MacroAssembler::srai(dst.high_gp(), dst.low_gp(), 31);
      break;
    case LoadType::kI32Load:
      MacroAssembler::Lw(dst.gp(), src_op);
      break;
    case LoadType::kI64Load: {
      Lw(dst.low_gp(), src_op);
      src_op = liftoff::GetMemOp(this, src_addr, offset_reg,
                                 offset_imm + kSystemPointerSize);
      Lw(dst.high_gp(), src_op);
    } break;
    case LoadType::kF32Load:
      MacroAssembler::LoadFloat(dst.fp(), src_op);
      break;
    case LoadType::kF64Load:
      MacroAssembler::LoadDouble(dst.fp(), src_op);
      break;
    case LoadType::kS128Load: {
      VU.set(kScratchReg, E8, m1);
      Register src_reg = src_op.offset() == 0 ? src_op.rm() : kScratchReg;
      if (src_op.offset() != 0) {
        MacroAssembler::AddWord(src_reg, src_op.rm(), src_op.offset());
      }
      vl(dst.fp().toV(), src_reg, 0, E8);
      break;
    }
    case LoadType::kF32LoadF16:
      UNIMPLEMENTED();
      break;
    default:
      UNREACHABLE();
  }

#if defined(V8_TARGET_BIG_ENDIAN)
  if (is_load_mem) {
    pinned.set(src_op.rm());
    liftoff::ChangeEndiannessLoad(this, dst, type, pinned);
  }
#endif
}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uintptr_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList pinned,
                             uint32_t* protected_store_pc, bool is_store_mem,
                             bool i64_offset) {
  MemOperand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);

#if defined(V8_TARGET_BIG_ENDIAN)
  if (is_store_mem) {
    pinned.set(dst_op.rm());
    LiftoffRegister tmp = GetUnusedRegister(src.reg_class(), pinned);
    // Save original value.
    Move(tmp, src, type.value_type());

    src = tmp;
    pinned.set(tmp);
    liftoff::ChangeEndiannessStore(this, src, type, pinned);
  }
#endif

  if (protected_store_pc) *protected_store_pc = pc_offset();

  switch (type.value()) {
    case StoreType::kI32Store8:
      Sb(src.gp(), dst_op);
      break;
    case StoreType::kI64Store8:
      Sb(src.low_gp(), dst_op);
      break;
    case StoreType::kI32Store16:
      MacroAssembler::Sh(src.gp(), dst_op);
      break;
    case StoreType::kI64Store16:
      MacroAssembler::Sh(src.low_gp(), dst_op);
      break;
    case StoreType::kI32Store:
      MacroAssembler::Sw(src.gp(), dst_op);
      break;
    case StoreType::kI64Store32:
      MacroAssembler::Sw(src.low_gp(), dst_op);
      break;
    case StoreType::kI64Store: {
      MacroAssembler::Sw(src.low_gp(), dst_op);
      dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg,
                                 offset_imm + kSystemPointerSize);
      MacroAssembler::Sw(src.high_gp(), dst_op);
      break;
    }
    case StoreType::kF32Store:
      MacroAssembler::StoreFloat(src.fp(), dst_op);
      break;
    case StoreType::kF64Store:
      MacroAssembler::StoreDouble(src.fp(), dst_op);
      break;
    case StoreType::kS128Store: {
      VU.set(kScratchReg, E8, m1);
      Register dst_reg = dst_op.offset() == 0 ? dst_op.rm() : kScratchReg;
      if (dst_op.offset() != 0) {
        AddWord(kScratchReg, dst_op.rm(), dst_op.offset());
      }
      vs(src.fp().toV(), dst_reg, 0, VSew::E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

namespace liftoff {
#define __ lasm->

inline Register CalculateActualAddress(LiftoffAssembler* lasm,
                                       UseScratchRegisterScope& temps,
                                       Register addr_reg, Register offset_reg,
                                       uintptr_t offset_imm,
                                       Register result_reg = no_reg) {
  if (offset_reg == no_reg && offset_imm == 0) {
    if (result_reg == addr_reg || result_reg == no_reg) return addr_reg;
    lasm->mv(result_reg, addr_reg);
    return result_reg;
  }
  if (result_reg == no_reg) result_reg = temps.Acquire();
  if (offset_reg == no_reg) {
    lasm->AddWord(result_reg, addr_reg, Operand(offset_imm));
  } else {
    lasm->AddWord(result_reg, addr_reg, Operand(offset_reg));
    if (offset_imm != 0)
      lasm->AddWord(result_reg, result_reg, Operand(offset_imm));
  }
  return result_reg;
}

enum class Binop { kAdd, kSub, kAnd, kOr, kXor, kExchange };
inline void AtomicBinop64(LiftoffAssembler* lasm, Register dst_addr,
                          Register offset_reg, uintptr_t offset_imm,
                          LiftoffRegister value, LiftoffRegister result,
                          StoreType type, Binop op) {
  ASM_CODE_COMMENT(lasm);
  FrameScope scope(lasm, StackFrame::MANUAL);
  RegList c_params = {kCArgRegs[0], kCArgRegs[1], kCArgRegs[2]};
  RegList result_list = {result.low_gp(), result.high_gp()};
  // Result registers does not need to be pushed.
  __ MultiPush(c_params - result_list);
  UseScratchRegisterScope temps(lasm);
  liftoff::CalculateActualAddress(lasm, temps, dst_addr, offset_reg, offset_imm,
                                  kScratchReg);
  __ Mv(kCArgRegs[1], value.low_gp());
  __ Mv(kCArgRegs[2], value.high_gp());
  __ Mv(kCArgRegs[0], kScratchReg);
  __ MultiPush(kJSCallerSaved - c_params - result_list);
  __ PrepareCallCFunction(3, 0, kScratchReg);
  ExternalReference extern_func_ref;
  switch (op) {
    case Binop::kAdd:
      extern_func_ref = ExternalReference::atomic_pair_add_function();
      break;
    case Binop::kSub:
      extern_func_ref = ExternalReference::atomic_pair_sub_function();
      break;
    case Binop::kAnd:
      extern_func_ref = ExternalReference::atomic_pair_and_function();
      break;
    case Binop::kOr:
      extern_func_ref = ExternalReference::atomic_pair_or_function();
      break;
    case Binop::kXor:
      extern_func_ref = ExternalReference::atomic_pair_xor_function();
      break;
    case Binop::kExchange:
      extern_func_ref = ExternalReference::atomic_pair_exchange_function();
      break;
    default:
      UNREACHABLE();
  }
  __ CallCFunction(extern_func_ref, 3, 0);
  __ MultiPop(kJSCallerSaved - c_params - result_list);
  __ Mv(result.low_gp(), kReturnRegister0);
  __ Mv(result.high_gp(), kReturnRegister1);
  __ MultiPop(c_params - result_list);
  return;
}

inline void AtomicBinop(LiftoffAssembler* lasm, Register dst_addr,
                        Register offset_reg, uintptr_t offset_imm,
                        LiftoffRegister value, LiftoffRegister result,
                        StoreType type, Binop op) {
  LiftoffRegList pinned{dst_addr, value, result};
  if (offset_reg != no_reg) pinned.set(offset_reg);
  Register store_result = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();

  // Make sure that {result} is unique.
  Register result_reg = no_reg;
  Register value_reg = no_reg;
  bool change_result = false;
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI64Store16:
      __ LoadConstant(result.high(), WasmValue(0));
      result_reg = result.low_gp();
      value_reg = value.low_gp();
      break;
    case StoreType::kI32Store8:
    case StoreType::kI32Store16:
      result_reg = result.gp();
      value_reg = value.gp();
      break;
    default:
      UNREACHABLE();
  }
  if (result_reg == value_reg || result_reg == dst_addr ||
      result_reg == offset_reg) {
    result_reg = __ GetUnusedRegister(kGpReg, pinned).gp();
    change_result = true;
  }

  UseScratchRegisterScope temps(lasm);
  Register actual_addr = liftoff::CalculateActualAddress(
      lasm, temps, dst_addr, offset_reg, offset_imm);

  // Allocate an additional {temp} register to hold the result that should be
  // stored to memory. Note that {temp} and {store_result} are not allowed to be
  // the same register.
  Register temp = temps.Acquire();

  Label retry;
  __ bind(&retry);
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI32Store8:
      __ lbu(result_reg, actual_addr, 0);
      __ sync();
      break;
    case StoreType::kI64Store16:
    case StoreType::kI32Store16:
      __ lhu(result_reg, actual_addr, 0);
      __ sync();
      break;
    case StoreType::kI64Store32:
    case StoreType::kI32Store:
      __ lr_w(true, false, result_reg, actual_addr);
      break;
    default:
      UNREACHABLE();
  }

  switch (op) {
    case Binop::kAdd:
      __ add(temp, result_reg, value_reg);
      break;
    case Binop::kSub:
      __ sub(temp, result_reg, value_reg);
      break;
    case Binop::kAnd:
      __ and_(temp, result_reg, value_reg);
      break;
    case Binop::kOr:
      __ or_(temp, result_reg, value_reg);
      break;
    case Binop::kXor:
      __ xor_(temp, result_reg, value_reg);
      break;
    case Binop::kExchange:
      __ mv(temp, value_reg);
      break;
  }
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI32Store8:
      __ sync();
      __ sb(temp, actual_addr, 0);
      __ sync();
      __ mv(store_result, zero_reg);
      break;
    case StoreType::kI64Store16:
    case StoreType::kI32Store16:
      __ sync();
      __ sh(temp, actual_addr, 0);
      __ sync();
      __ mv(store_result, zero_reg);
      break;
    case StoreType::kI64Store32:
    case StoreType::kI32Store:
      __ sc_w(false, true, store_result, actual_addr, temp);
      break;
    default:
      UNREACHABLE();
  }

  __ bnez(store_result, &retry);
  if (change_result) {
    switch (type.value()) {
      case StoreType::kI64Store8:
      case StoreType::kI64Store16:
      case StoreType::kI64Store32:
        __ mv(result.low_gp(), result_reg);
        break;
      case StoreType::kI32Store8:
      case StoreType::kI32Store16:
      case StoreType::kI32Store:
        __ mv(result.gp(), result_reg);
        break;
      default:
        UNREACHABLE();
    }
  }
}

#undef __
}  // namespace liftoff

void LiftoffAssembler::AtomicLoad(LiftoffRegister dst, Register src_addr,
                                  Register offset_reg, uintptr_t offset_imm,
                                  LoadType type, LiftoffRegList pinned,
                                  bool i64_offset) {
  UseScratchRegisterScope temps(this);
  Register src_reg = liftoff::CalculateActualAddress(this, temps, src_addr,
                                                     offset_reg, offset_imm);
  Register dst_reg = no_reg;
  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI32Load16U:
    case LoadType::kI32Load:
      dst_reg = dst.gp();
      break;
    case LoadType::kI64Load8U:
    case LoadType::kI64Load16U:
    case LoadType::kI64Load32U:
      dst_reg = dst.low_gp();
      LoadConstant(dst.high(), WasmValue(0));
      break;
    default:
      break;
  }
  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI64Load8U:
      fence(PSR | PSW, PSR | PSW);
      lbu(dst_reg, src_reg, 0);
      fence(PSR, PSR | PSW);
      return;
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U:
      fence(PSR | PSW, PSR | PSW);
      lhu(dst_reg, src_reg, 0);
      fence(PSR, PSR | PSW);
      return;
    case LoadType::kI32Load:
    case LoadType::kI64Load32U:
      fence(PSR | PSW, PSR | PSW);
      lw(dst_reg, src_reg, 0);
      fence(PSR, PSR | PSW);
      return;
    case LoadType::kI64Load:
      fence(PSR | PSW, PSR | PSW);
      lw(dst.low_gp(), src_reg, liftoff::kLowWordOffset);
      lw(dst.high_gp(), src_reg, liftoff::kHighWordOffset);
      fence(PSR, PSR | PSW);
      return;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicStore(Register dst_addr, Register offset_reg,
                                   uintptr_t offset_imm, LiftoffRegister src,
                                   StoreType type, LiftoffRegList pinned,
                                   bool i64_offset) {
  UseScratchRegisterScope temps(this);
  Register dst_reg = liftoff::CalculateActualAddress(this, temps, dst_addr,
                                                     offset_reg, offset_imm);
  Register src_reg = no_reg;
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI32Store16:
    case StoreType::kI32Store:
      src_reg = src.gp();
      break;
    case StoreType::kI64Store8:
    case StoreType::kI64Store16:
    case StoreType::kI64Store32:
      src_reg = src.low_gp();
      break;
    default:
      break;
  }
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI32Store8:
      fence(PSR | PSW, PSW);
      sb(src_reg, dst_reg, 0);
      return;
    case StoreType::kI64Store16:
    case StoreType::kI32Store16:
      fence(PSR | PSW, PSW);
      sh(src_reg, dst_reg, 0);
      return;
    case StoreType::kI64Store32:
    case StoreType::kI32Store:
      fence(PSR | PSW, PSW);
      sw(src_reg, dst_reg, 0);
      return;
    case StoreType::kI64Store:
      fence(PSR | PSW, PSW);
      sw(src.low_gp(), dst_reg, liftoff::kLowWordOffset);
      sw(src.high_gp(), dst_reg, liftoff::kHighWordOffset);
      return;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicAdd(Register dst_addr, Register offset_reg,
                                 uint32_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  if (type.value() == StoreType::kI64Store) {
    liftoff::AtomicBinop64(this, dst_addr, offset_reg, offset_imm, value,
                           result, type, liftoff::Binop::kAdd);
    return;
  }
  if (type.value() == StoreType::kI32Store ||
      type.value() == StoreType::kI64Store32) {
    UseScratchRegisterScope temps(this);
    Register actual_addr = liftoff::CalculateActualAddress(
        this, temps, dst_addr, offset_reg, offset_imm);
    if (type.value() == StoreType::kI64Store32) {
      mv(result.high_gp(), zero_reg);  // High word of result is always 0.
      result = result.low();
      value = value.low();
    }
    amoadd_w(true, true, result.gp(), actual_addr, value.gp());
    return;
  }

  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kAdd);
}

void LiftoffAssembler::AtomicSub(Register dst_addr, Register offset_reg,
                                 uint32_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  if (type.value() == StoreType::kI64Store) {
    liftoff::AtomicBinop64(this, dst_addr, offset_reg, offset_imm, value,
                           result, type, liftoff::Binop::kSub);
    return;
  }
  if (type.value() == StoreType::kI32Store ||
      type.value() == StoreType::kI64Store32) {
    UseScratchRegisterScope temps(this);
    Register actual_addr = liftoff::CalculateActualAddress(
        this, temps, dst_addr, offset_reg, offset_imm);
    if (type.value() == StoreType::kI64Store32) {
      mv(result.high_gp(), zero_reg);
      result = result.low();
      value = value.low();
    }
    sub(kScratchReg, zero_reg, value.gp());
    amoadd_w(true, true, result.gp(), actual_addr, kScratchReg);
    return;
  }
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kSub);
}

void LiftoffAssembler::AtomicAnd(Register dst_addr, Register offset_reg,
                                 uint32_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  if (type.value() == StoreType::kI64Store) {
    liftoff::AtomicBinop64(this, dst_addr, offset_reg, offset_imm, value,
                           result, type, liftoff::Binop::kAnd);
    return;
  }
  if (type.value() == StoreType::kI32Store ||
      type.value() == StoreType::kI64Store32) {
    UseScratchRegisterScope temps(this);
    Register actual_addr = liftoff::CalculateActualAddress(
        this, temps, dst_addr, offset_reg, offset_imm);
    if (type.value() == StoreType::kI64Store32) {
      mv(result.high_gp(), zero_reg);
      result = result.low();
      value = value.low();
    }
    amoand_w(true, true, result.gp(), actual_addr, value.gp());
    return;
  }
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kAnd);
}

void LiftoffAssembler::AtomicOr(Register dst_addr, Register offset_reg,
                                uint32_t offset_imm, LiftoffRegister value,
                                LiftoffRegister result, StoreType type,
                                bool i64_offset) {
  if (type.value() == StoreType::kI64Store) {
    liftoff::AtomicBinop64(this, dst_addr, offset_reg, offset_imm, value,
                           result, type, liftoff::Binop::kOr);
    return;
  }
  if (type.value() == StoreType::kI32Store ||
      type.value() == StoreType::kI64Store32) {
    UseScratchRegisterScope temps(this);
    Register actual_addr = liftoff::CalculateActualAddress(
        this, temps, dst_addr, offset_reg, offset_imm);
    if (type.value() == StoreType::kI64Store32) {
      mv(result.high_gp(), zero_reg);
      result = result.low();
      value = value.low();
    }
    amoor_w(true, true, result.gp(), actual_addr, value.gp());
    return;
  }
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kOr);
}

void LiftoffAssembler::AtomicXor(Register dst_addr, Register offset_reg,
                                 uint32_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  if (type.value() == StoreType::kI64Store) {
    liftoff::AtomicBinop64(this, dst_addr, offset_reg, offset_imm, value,
                           result, type, liftoff::Binop::kXor);
    return;
  }
  if (type.value() == StoreType::kI32Store ||
      type.value() == StoreType::kI64Store32) {
    UseScratchRegisterScope temps(this);
    Register actual_addr = liftoff::CalculateActualAddress(
        this, temps, dst_addr, offset_reg, offset_imm);
    if (type.value() == StoreType::kI64Store32) {
      mv(result.high_gp(), zero_reg);
      result = result.low();
      value = value.low();
    }
    amoxor_w(true, true, result.gp(), actual_addr, value.gp());
    return;
  }
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kXor);
}

void LiftoffAssembler::AtomicExchange(Register dst_addr, Register offset_reg,
                                      uint32_t offset_imm,
                                      LiftoffRegister value,
                                      LiftoffRegister result, StoreType type,
                                      bool i64_offset) {
  if (type.value() == StoreType::kI64Store) {
    liftoff::AtomicBinop64(this, dst_addr, offset_reg, offset_imm, value,
                           result, type, liftoff::Binop::kExchange);
    return;
  }
  if (type.value() == StoreType::kI32Store ||
      type.value() == StoreType::kI64Store32) {
    UseScratchRegisterScope temps(this);
    Register actual_addr = liftoff::CalculateActualAddress(
        this, temps, dst_addr, offset_reg, offset_imm);
    if (type.value() == StoreType::kI64Store32) {
      mv(result.high_gp(), zero_reg);
      result = result.low();
      value = value.low();
    }
    amoswap_w(true, true, result.gp(), actual_addr, value.gp());
    return;
  }
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kExchange);
}

void LiftoffAssembler::AtomicCompareExchange(
    Register dst_addr, Register offset_reg, uintptr_t offset_imm,
    LiftoffRegister expected, LiftoffRegister new_value, LiftoffRegister result,
    StoreType type, bool i64_offset) {
  ASM_CODE_COMMENT(this);
  LiftoffRegList pinned{dst_addr, expected, new_value, result};
  if (offset_reg != no_reg) pinned.set(offset_reg);

  if (type.value() == StoreType::kI64Store) {
    UseScratchRegisterScope temps(this);
    Register actual_addr = liftoff::CalculateActualAddress(
        this, temps, dst_addr, offset_reg, offset_imm, kScratchReg);
    FrameScope scope(this, StackFrame::MANUAL);
    // NOTE:
    // a0~a4 are caller-saved registers and also used
    // to pass parameters for C functions.
    RegList c_params = {kCArgRegs[0], kCArgRegs[1], kCArgRegs[2], kCArgRegs[3],
                        a4};
    RegList result_list = {result.low_gp(), result.high_gp()};
    MultiPush(c_params - result_list);

    Mv(a1, expected.low_gp());
    Mv(a2, expected.high_gp());
    Mv(a3, new_value.low_gp());
    Mv(a4, new_value.high_gp());
    Mv(a0, actual_addr);

    MultiPush(kJSCallerSaved - c_params - result_list);
    PrepareCallCFunction(5, 0, kScratchReg);
    CallCFunction(ExternalReference::atomic_pair_compare_exchange_function(), 5,
                  0);
    MultiPop(kJSCallerSaved - c_params - result_list);
    Mv(kScratchReg, kReturnRegister1);
    Mv(result.low_gp(), kReturnRegister0);
    Mv(result.high_gp(), kScratchReg);
    MultiPop(c_params - result_list);
    return;
  }
  // Make sure that {result} is unique.
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI64Store16:
    case StoreType::kI64Store32:
      LoadConstant(result.high(), WasmValue(0));
      result = result.low();
      new_value = new_value.low();
      expected = expected.low();
      break;
    case StoreType::kI32Store8:
    case StoreType::kI32Store16:
    case StoreType::kI32Store:
      break;
    default:
      UNREACHABLE();
  }

  UseScratchRegisterScope temps(this);
  Register actual_addr = liftoff::CalculateActualAddress(
      this, temps, dst_addr, offset_reg, offset_imm, kScratchReg);

  Register temp0 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp1 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp2 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();

  if (type.value() != StoreType::kI32Store &&
      type.value() != StoreType::kI64Store32) {
    And(temp1, actual_addr, 0x3);
    SubWord(temp0, actual_addr, Operand(temp1));
    SllWord(temp1, temp1, 3);
  }
  Label retry;
  Label done;
  bind(&retry);
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI32Store8:
      lr_w(true, true, temp2, temp0);
      ExtractBits(result.gp(), temp2, temp1, 8, false);
      ExtractBits(temp2, expected.gp(), zero_reg, 8, false);
      Branch(&done, ne, temp2, Operand(result.gp()));
      InsertBits(temp2, new_value.gp(), temp1, 8);
      sc_w(true, true, temp2, temp0, temp2);
      break;
    case StoreType::kI64Store16:
    case StoreType::kI32Store16:
      lr_w(true, true, temp2, temp0);
      ExtractBits(result.gp(), temp2, temp1, 16, false);
      ExtractBits(temp2, expected.gp(), zero_reg, 16, false);
      Branch(&done, ne, temp2, Operand(result.gp()));
      InsertBits(temp2, new_value.gp(), temp1, 16);
      sc_w(true, true, temp2, temp0, temp2);
      break;
    case StoreType::kI64Store32:
    case StoreType::kI32Store:
      lr_w(true, true, result.gp(), actual_addr);
      Branch(&done, ne, result.gp(), Operand(expected.gp()));
      sc_w(true, true, temp2, actual_addr, new_value.gp());
      break;
    default:
      UNREACHABLE();
  }
  bnez(temp2, &retry);
  bind(&done);
}

void LiftoffAssembler::AtomicFence() { sync(); }

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx,
                                           ValueKind kind) {
  int32_t offset = kSystemPointerSize * (caller_slot_idx + 1);
  liftoff::Load(this, dst, fp, offset, kind);
}

void LiftoffAssembler::StoreCallerFrameSlot(LiftoffRegister src,
                                            uint32_t caller_slot_idx,
                                            ValueKind kind) {
  int32_t offset = kSystemPointerSize * (caller_slot_idx + 1);
  liftoff::Store(this, fp, offset, src, kind);
}

void LiftoffAssembler::LoadReturnStackSlot(LiftoffRegister dst, int offset,
                                           ValueKind kind) {
  liftoff::Load(this, dst, sp, offset, kind);
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_offset, uint32_t src_offset,
                                      ValueKind kind) {
  DCHECK_NE(dst_offset, src_offset);

  MemOperand src = liftoff::GetStackSlot(src_offset);
  MemOperand dst = liftoff::GetStackSlot(dst_offset);
  switch (kind) {
    case kI32:
      Lw(kScratchReg, src);
      Sw(kScratchReg, dst);
      break;
    case kI64:
    case kRef:
    case kRefNull:
    case kRtt:
      Lw(kScratchReg, src);
      Sw(kScratchReg, dst);
      src = liftoff::GetStackSlot(src_offset - 4);
      dst = liftoff::GetStackSlot(dst_offset - 4);
      Lw(kScratchReg, src);
      Sw(kScratchReg, dst);
      break;
    case kF32:
      LoadFloat(kScratchDoubleReg, src);
      StoreFloat(kScratchDoubleReg, dst);
      break;
    case kF64:
      MacroAssembler::LoadDouble(kScratchDoubleReg, src);
      MacroAssembler::StoreDouble(kScratchDoubleReg, dst);
      break;
    case kS128: {
      VU.set(kScratchReg, E8, m1);
      Register src_reg = src.offset() == 0 ? src.rm() : kScratchReg;
      if (src.offset() != 0) {
        MacroAssembler::AddWord(src_reg, src.rm(), src.offset());
      }
      vl(kSimd128ScratchReg, src_reg, 0, E8);
      Register dst_reg = dst.offset() == 0 ? dst.rm() : kScratchReg;
      if (dst.offset() != 0) {
        AddWord(kScratchReg, dst.rm(), dst.offset());
      }
      vs(kSimd128ScratchReg, dst_reg, 0, VSew::E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Move(Register dst, Register src, ValueKind kind) {
  DCHECK_NE(dst, src);
  // TODO(ksreten): Handle different sizes here.
  MacroAssembler::Move(dst, src);
}

void LiftoffAssembler::Move(DoubleRegister dst, DoubleRegister src,
                            ValueKind kind) {
  DCHECK_NE(dst, src);
  if (kind != kS128) {
    MacroAssembler::Move(dst, src);
  } else {
    VU.set(kScratchReg, E8, m1);
    MacroAssembler::vmv_vv(dst.toV(), src.toV());
  }
}

void LiftoffAssembler::Spill(int offset, LiftoffRegister reg, ValueKind kind) {
  RecordUsedSpillOffset(offset);
  MemOperand dst = liftoff::GetStackSlot(offset);
  switch (kind) {
    case kI32:
    case kRef:
    case kRefNull:
    case kRtt:
      Sw(reg.gp(), dst);
      break;
    case kI64:
      Sw(reg.low_gp(), liftoff::GetHalfStackSlot(offset, kLowWord));
      Sw(reg.high_gp(), liftoff::GetHalfStackSlot(offset, kHighWord));
      break;
    case kF32:
      StoreFloat(reg.fp(), dst);
      break;
    case kF64:
      MacroAssembler::StoreDouble(reg.fp(), dst);
      break;
    case kS128: {
      VU.set(kScratchReg, E8, m1);
      Register dst_reg = dst.offset() == 0 ? dst.rm() : kScratchReg;
      if (dst.offset() != 0) {
        AddWord(kScratchReg, dst.rm(), dst.offset());
      }
      vs(reg.fp().toV(), dst_reg, 0, VSew::E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Spill(int offset, WasmValue value) {
  RecordUsedSpillOffset(offset);
  MemOperand dst = liftoff::GetStackSlot(offset);
  UseScratchRegisterScope assembler_temps(this);
  Register tmp = assembler_temps.Acquire();
  switch (value.type().kind()) {
    case kI32:
    case kRef:
    case kRefNull: {
      MacroAssembler::li(tmp, Operand(value.to_i32()));
      Sw(tmp, dst);
      break;
    }
    case kI64: {
      int32_t low_word = value.to_i64();
      int32_t high_word = value.to_i64() >> 32;
      MacroAssembler::li(tmp, Operand(low_word));
      Sw(tmp, liftoff::GetHalfStackSlot(offset, kLowWord));
      MacroAssembler::li(tmp, Operand(high_word));
      Sw(tmp, liftoff::GetHalfStackSlot(offset, kHighWord));
      break;
      break;
    }
    default:
      // kWasmF32 and kWasmF64 are unreachable, since those
      // constants are not tracked.
      UNREACHABLE();
  }
}

void LiftoffAssembler::Fill(LiftoffRegister reg, int offset, ValueKind kind) {
  MemOperand src = liftoff::GetStackSlot(offset);
  switch (kind) {
    case kI32:
    case kRef:
    case kRefNull:
      Lw(reg.gp(), src);
      break;
    case kI64:
      Lw(reg.low_gp(), liftoff::GetHalfStackSlot(offset, kLowWord));
      Lw(reg.high_gp(), liftoff::GetHalfStackSlot(offset, kHighWord));
      break;
    case kF32:
      LoadFloat(reg.fp(), src);
      break;
    case kF64:
      MacroAssembler::LoadDouble(reg.fp(), src);
      break;
    case kS128: {
      VU.set(kScratchReg, E8, m1);
      Register src_reg = src.offset() == 0 ? src.rm() : kScratchReg;
      if (src.offset() != 0) {
        MacroAssembler::AddWord(src_reg, src.rm(), src.offset());
      }
      vl(reg.fp().toV(), src_reg, 0, E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::FillI64Half(Register reg, int offset, RegPairHalf half) {
  Lw(reg, liftoff::GetHalfStackSlot(offset, half));
}

void LiftoffAssembler::FillStackSlotsWithZero(int start, int size) {
  DCHECK_LT(0, size);
  RecordUsedSpillOffset(start + size);

  // TODO(riscv32): check

  if (size <= 12 * kStackSlotSize) {
    // Special straight-line code for up to 12 slots. Generates one
    // instruction per slot (<= 12 instructions total).
    uint32_t remainder = size;
    for (; remainder >= kStackSlotSize; remainder -= kStackSlotSize) {
      Sw(zero_reg, liftoff::GetStackSlot(start + remainder));
      Sw(zero_reg, liftoff::GetStackSlot(start + remainder - 4));
    }
    DCHECK(remainder == 4 || remainder == 0);
    if (remainder) {
      Sw(zero_reg, liftoff::GetStackSlot(start + remainder));
    }
  } else {
    // General case for bigger counts (12 instructions).
    // Use a0 for start address (inclusive), a1 for end address (exclusive).
    Push(a1, a0);
    AddWord(a0, fp, Operand(-start - size));
    AddWord(a1, fp, Operand(-start));

    Label loop;
    bind(&loop);
    Sw(zero_reg, MemOperand(a0));
    addi(a0, a0, kSystemPointerSize);
    BranchShort(&loop, ne, a0, Operand(a1));

    Pop(a1, a0);
  }
}

void LiftoffAssembler::emit_i64_clz(LiftoffRegister dst, LiftoffRegister src) {
  // return high == 0 ? 32 + CLZ32(low) : CLZ32(high);
  Label done;
  Label high_is_zero;
  Branch(&high_is_zero, eq, src.high_gp(), Operand(zero_reg));

  Clz32(dst.low_gp(), src.high_gp());
  jmp(&done);

  bind(&high_is_zero);
  Clz32(dst.low_gp(), src.low_gp());
  AddWord(dst.low_gp(), dst.low_gp(), Operand(32));

  bind(&done);
  mv(dst.high_gp(), zero_reg);  // High word of result is always 0.
}

void LiftoffAssembler::emit_i64_ctz(LiftoffRegister dst, LiftoffRegister src) {
  // return low == 0 ? 32 + CTZ32(high) : CTZ32(low);
  Label done;
  Label low_is_zero;
  Branch(&low_is_zero, eq, src.low_gp(), Operand(zero_reg));

  Ctz32(dst.low_gp(), src.low_gp());
  jmp(&done);

  bind(&low_is_zero);
  Ctz32(dst.low_gp(), src.high_gp());
  AddWord(dst.low_gp(), dst.low_gp(), Operand(32));

  bind(&done);
  mv(dst.high_gp(), zero_reg);  // High word of result is always 0.
}

bool LiftoffAssembler::emit_i64_popcnt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  // Produce partial popcnts in the two dst registers.
  Register src1 = src.high_gp() == dst.low_gp() ? src.high_gp() : src.low_gp();
  Register src2 = src.high_gp() == dst.low_gp() ? src.low_gp() : src.high_gp();
  MacroAssembler::Popcnt32(dst.low_gp(), src1, kScratchReg);
  MacroAssembler::Popcnt32(dst.high_gp(), src2, kScratchReg);
  // Now add the two into the lower dst reg and clear the higher dst reg.
  AddWord(dst.low_gp(), dst.low_gp(), dst.high_gp());
  mv(dst.high_gp(), zero_reg);
  return true;
}

void LiftoffAssembler::emit_i32_mul(Register dst, Register lhs, Register rhs) {
  MacroAssembler::Mul(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_muli(Register dst, Register lhs, int32_t imm) {
  if (base::bits::IsPowerOfTwo(imm)) {
    emit_i32_shli(dst, lhs, base::bits::WhichPowerOfTwo(imm));
    return;
  }
  UseScratchRegisterScope temps{this};
  Register scratch = temps.Acquire();
  li(scratch, Operand{imm});
  MacroAssembler::Mul(dst, lhs, scratch);
}

void LiftoffAssembler::emit_i32_divs(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));

  // Check if lhs == kMinInt and rhs == -1, since this case is unrepresentable.
  MacroAssembler::CompareI(kScratchReg, lhs, Operand(kMinInt), ne);
  MacroAssembler::CompareI(kScratchReg2, rhs, Operand(-1), ne);
  add(kScratchReg, kScratchReg, kScratchReg2);
  MacroAssembler::Branch(trap_div_unrepresentable, eq, kScratchReg,
                         Operand(zero_reg));

  MacroAssembler::Div(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_divu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Divu(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_rems(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Mod(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_remu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Modu(dst, lhs, rhs);
}

#define I32_BINOP(name, instruction)                                 \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register lhs, \
                                         Register rhs) {             \
    instruction(dst, lhs, rhs);                                      \
  }

// clang-format off
I32_BINOP(add, add)
I32_BINOP(sub, sub)
I32_BINOP(and, and_)
I32_BINOP(or, or_)
I32_BINOP(xor, xor_)
// clang-format on

#undef I32_BINOP

#define I32_BINOP_I(name, instruction)                                  \
  void LiftoffAssembler::emit_i32_##name##i(Register dst, Register lhs, \
                                            int32_t imm) {              \
    instruction(dst, lhs, Operand(imm));                                \
  }

// clang-format off
I32_BINOP_I(add, AddWord)
I32_BINOP_I(sub, SubWord)
I32_BINOP_I(and, And)
I32_BINOP_I(or, Or)
I32_BINOP_I(xor, Xor)
// clang-format on

#undef I32_BINOP_I

void LiftoffAssembler::emit_i32_clz(Register dst, Register src) {
  MacroAssembler::Clz32(dst, src);
}

void LiftoffAssembler::emit_i32_ctz(Register dst, Register src) {
  MacroAssembler::Ctz32(dst, src);
}

bool LiftoffAssembler::emit_i32_popcnt(Register dst, Register src) {
  MacroAssembler::Popcnt32(dst, src, kScratchReg);
  return true;
}

#define I32_SHIFTOP(name, instruction)                               \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register src, \
                                         Register amount) {          \
    instruction(dst, src, amount);                                   \
  }
#define I32_SHIFTOP_I(name, instruction)                                \
  void LiftoffAssembler::emit_i32_##name##i(Register dst, Register src, \
                                            int amount) {               \
    instruction(dst, src, amount & 31);                                 \
  }

I32_SHIFTOP(shl, sll)
I32_SHIFTOP(sar, sra)
I32_SHIFTOP(shr, srl)

I32_SHIFTOP_I(shl, slli)
I32_SHIFTOP_I(sar, srai)
I32_SHIFTOP_I(shr, srli)

#undef I32_SHIFTOP
#undef I32_SHIFTOP_I

void LiftoffAssembler::emit_i64_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  MacroAssembler::MulPair(dst.low_gp(), dst.high_gp(), lhs.low_gp(),
                          lhs.high_gp(), rhs.low_gp(), rhs.high_gp(),
                          kScratchReg, kScratchReg2);
}

// Implemented by the host function in external-reference.h(Call to host
// function wasm::xxx).
bool LiftoffAssembler::emit_i64_divs(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  return false;
}

bool LiftoffAssembler::emit_i64_divu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  return false;
}

bool LiftoffAssembler::emit_i64_rems(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  return false;
}

bool LiftoffAssembler::emit_i64_remu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  return false;
}

namespace liftoff {

inline bool IsRegInRegPair(LiftoffRegister pair, Register reg) {
  DCHECK(pair.is_gp_pair());
  return pair.low_gp() == reg || pair.high_gp() == reg;
}

inline void Emit64BitShiftOperation(
    LiftoffAssembler* assm, LiftoffRegister dst, LiftoffRegister src,
    Register amount,
    void (MacroAssembler::*emit_shift)(Register, Register, Register, Register,
                                       Register, Register, Register)) {
  LiftoffRegList pinned{dst, src, amount};

  // If some of destination registers are in use, get another, unused pair.
  // That way we prevent overwriting some input registers while shifting.
  // Do this before any branch so that the cache state will be correct for
  // all conditions.
  Register amount_capped =
      pinned.set(assm->GetUnusedRegister(kGpReg, pinned).gp());
  assm->And(amount_capped, amount, Operand(63));
  if (liftoff::IsRegInRegPair(dst, amount) || dst.overlaps(src)) {
    // Do the actual shift.
    LiftoffRegister tmp = assm->GetUnusedRegister(kGpRegPair, pinned);
    (assm->*emit_shift)(tmp.low_gp(), tmp.high_gp(), src.low_gp(),
                        src.high_gp(), amount_capped, kScratchReg,
                        kScratchReg2);

    // Place result in destination register.
    assm->MacroAssembler::Move(dst.high_gp(), tmp.high_gp());
    assm->MacroAssembler::Move(dst.low_gp(), tmp.low_gp());
  } else {
    (assm->*emit_shift)(dst.low_gp(), dst.high_gp(), src.low_gp(),
                        src.high_gp(), amount_capped, kScratchReg,
                        kScratchReg2);
  }
}
}  // namespace liftoff

void LiftoffAssembler::emit_i64_add(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  MacroAssembler::AddPair(dst.low_gp(), dst.high_gp(), lhs.low_gp(),
                          lhs.high_gp(), rhs.low_gp(), rhs.high_gp(),
                          kScratchReg, kScratchReg2);
}

void LiftoffAssembler::emit_i64_addi(LiftoffRegister dst, LiftoffRegister lhs,
                                     int64_t imm) {
  LiftoffRegister imm_reg =
      GetUnusedRegister(kGpRegPair, LiftoffRegList{dst, lhs});
  int32_t imm_low_word = static_cast<int32_t>(imm);
  int32_t imm_high_word = static_cast<int32_t>(imm >> 32);

  // TODO(riscv32): are there some optimization we can make without
  // materializing?
  MacroAssembler::li(imm_reg.low_gp(), imm_low_word);
  MacroAssembler::li(imm_reg.high_gp(), imm_high_word);
  MacroAssembler::AddPair(dst.low_gp(), dst.high_gp(), lhs.low_gp(),
                          lhs.high_gp(), imm_reg.low_gp(), imm_reg.high_gp(),
                          kScratchReg, kScratchReg2);
}

void LiftoffAssembler::emit_i64_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  MacroAssembler::SubPair(dst.low_gp(), dst.high_gp(), lhs.low_gp(),
                          lhs.high_gp(), rhs.low_gp(), rhs.high_gp(),
                          kScratchReg, kScratchReg2);
}

void LiftoffAssembler::emit_i64_shl(LiftoffRegister dst, LiftoffRegister src,
                                    Register amount) {
  ASM_CODE_COMMENT(this);
  liftoff::Emit64BitShiftOperation(this, dst, src, amount,
                                   &MacroAssembler::ShlPair);
}

void LiftoffAssembler::emit_i64_shli(LiftoffRegister dst, LiftoffRegister src,
                                     int amount) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  LiftoffRegister temp = GetUnusedRegister(kGpReg, LiftoffRegList{dst, src});
  temps.Include(temp.gp());
  // {src.low_gp()} will still be needed after writing {dst.high_gp()} and
  // {dst.low_gp()}.
  Register src_low = liftoff::EnsureNoAlias(this, src.low_gp(), dst, &temps);
  Register src_high = liftoff::EnsureNoAlias(this, src.high_gp(), dst, &temps);
  // {src.high_gp()} will still be needed after writing {dst.high_gp()}.
  DCHECK_NE(dst.low_gp(), kScratchReg);
  DCHECK_NE(dst.high_gp(), kScratchReg);

  MacroAssembler::ShlPair(dst.low_gp(), dst.high_gp(), src_low, src_high,
                          amount & 63, kScratchReg, kScratchReg2);
}

void LiftoffAssembler::emit_i64_sar(LiftoffRegister dst, LiftoffRegister src,
                                    Register amount) {
  liftoff::Emit64BitShiftOperation(this, dst, src, amount,
                                   &MacroAssembler::SarPair);
}

void LiftoffAssembler::emit_i64_sari(LiftoffRegister dst, LiftoffRegister src,
                                     int amount) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  LiftoffRegister temp = GetUnusedRegister(kGpReg, LiftoffRegList{dst, src});
  temps.Include(temp.gp());
  // {src.low_gp()} will still be needed after writing {dst.high_gp()} and
  // {dst.low_gp()}.
  Register src_low = liftoff::EnsureNoAlias(this, src.low_gp(), dst, &temps);
  Register src_high = liftoff::EnsureNoAlias(this, src.high_gp(), dst, &temps);
  DCHECK_NE(dst.low_gp(), kScratchReg);
  DCHECK_NE(dst.high_gp(), kScratchReg);

  MacroAssembler::SarPair(dst.low_gp(), dst.high_gp(), src_low, src_high,
                          amount & 63, kScratchReg, kScratchReg2);
}

void LiftoffAssembler::emit_i64_shr(LiftoffRegister dst, LiftoffRegister src,
                                    Register amount) {
  liftoff::Emit64BitShiftOperation(this, dst, src, amount,
                                   &MacroAssembler::ShrPair);
}

void LiftoffAssembler::emit_i64_shri(LiftoffRegister dst, LiftoffRegister src,
                                     int amount) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  LiftoffRegister temp = GetUnusedRegister(kGpReg, LiftoffRegList{dst, src});
  temps.Include(temp.gp());
  // {src.low_gp()} will still be needed after writing {dst.high_gp()} and
  // {dst.low_gp()}.
  Register src_low = liftoff::EnsureNoAlias(this, src.low_gp(), dst, &temps);
  Register src_high = liftoff::EnsureNoAlias(this, src.high_gp(), dst, &temps);
  DCHECK_NE(dst.low_gp(), kScratchReg);
  DCHECK_NE(dst.high_gp(), kScratchReg);

  MacroAssembler::ShrPair(dst.low_gp(), dst.high_gp(), src_low, src_high,
                          amount & 63, kScratchReg, kScratchReg2);
}

#define FP_UNOP_RETURN_FALSE(name)                                             \
  bool LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister src) { \
    return false;                                                              \
  }

FP_UNOP_RETURN_FALSE(f64_ceil)
FP_UNOP_RETURN_FALSE(f64_floor)
FP_UNOP_RETURN_FALSE(f64_trunc)
FP_UNOP_RETURN_FALSE(f64_nearest_int)

#undef FP_UNOP_RETURN_FALSE

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  switch (opcode) {
    case kExprI32ConvertI64:
      MacroAssembler::Move(dst.gp(), src.low_gp());
      return true;
    case kExprI32SConvertF32:
    case kExprI32UConvertF32:
    case kExprI32SConvertF64:
    case kExprI32UConvertF64:
    case kExprI64SConvertF32:
    case kExprI64UConvertF32:
    case kExprI64SConvertF64:
    case kExprI64UConvertF64:
    case kExprF32ConvertF64: {
      // real conversion, if src is out-of-bound of target integer types,
      // kScratchReg is set to 0
      switch (opcode) {
        case kExprI32SConvertF32:
          Trunc_w_s(dst.gp(), src.fp(), kScratchReg);
          break;
        case kExprI32UConvertF32:
          Trunc_uw_s(dst.gp(), src.fp(), kScratchReg);
          break;
        case kExprI32SConvertF64:
          Trunc_w_d(dst.gp(), src.fp(), kScratchReg);
          break;
        case kExprI32UConvertF64:
          Trunc_uw_d(dst.gp(), src.fp(), kScratchReg);
          break;
        case kExprF32ConvertF64:
          fcvt_s_d(dst.fp(), src.fp());
          break;
        case kExprI64SConvertF32:
        case kExprI64UConvertF32:
        case kExprI64SConvertF64:
        case kExprI64UConvertF64:
          return false;
        default:
          UNREACHABLE();
      }

      // Checking if trap.
      if (trap != nullptr) {
        MacroAssembler::Branch(trap, eq, kScratchReg, Operand(zero_reg));
      }

      return true;
    }
    case kExprI32ReinterpretF32:
      MacroAssembler::ExtractLowWordFromF64(dst.gp(), src.fp());
      return true;
    case kExprI64SConvertI32:
      MacroAssembler::Move(dst.low_gp(), src.gp());
      MacroAssembler::Move(dst.high_gp(), src.gp());
      srai(dst.high_gp(), dst.high_gp(), 31);
      return true;
    case kExprI64UConvertI32:
      MacroAssembler::Move(dst.low_gp(), src.gp());
      MacroAssembler::Move(dst.high_gp(), zero_reg);
      return true;
    case kExprI64ReinterpretF64:
      SubWord(sp, sp, kDoubleSize);
      StoreDouble(src.fp(), MemOperand(sp, 0));
      Lw(dst.low_gp(), MemOperand(sp, 0));
      Lw(dst.high_gp(), MemOperand(sp, 4));
      AddWord(sp, sp, kDoubleSize);
      return true;
    case kExprF32SConvertI32: {
      MacroAssembler::Cvt_s_w(dst.fp(), src.gp());
      return true;
    }
    case kExprF32UConvertI32:
      MacroAssembler::Cvt_s_uw(dst.fp(), src.gp());
      return true;
    case kExprF32ReinterpretI32:
      fmv_w_x(dst.fp(), src.gp());
      return true;
    case kExprF64SConvertI32: {
      MacroAssembler::Cvt_d_w(dst.fp(), src.gp());
      return true;
    }
    case kExprF64UConvertI32:
      MacroAssembler::Cvt_d_uw(dst.fp(), src.gp());
      return true;
    case kExprF64ConvertF32:
      fcvt_d_s(dst.fp(), src.fp());
      return true;
    case kExprF64ReinterpretI64:
      SubWord(sp, sp, kDoubleSize);
      Sw(src.low_gp(), MemOperand(sp, 0));
      Sw(src.high_gp(), MemOperand(sp, 4));
      LoadDouble(dst.fp(), MemOperand(sp, 0));
      AddWord(sp, sp, kDoubleSize);
      return true;
    case kExprI32SConvertSatF32: {
      fcvt_w_s(dst.gp(), src.fp(), RTZ);
      Clear_if_nan_s(dst.gp(), src.fp());
      return true;
    }
    case kExprI32UConvertSatF32: {
      fcvt_wu_s(dst.gp(), src.fp(), RTZ);
      Clear_if_nan_s(dst.gp(), src.fp());
      return true;
    }
    case kExprI32SConvertSatF64: {
      fcvt_w_d(dst.gp(), src.fp(), RTZ);
      Clear_if_nan_d(dst.gp(), src.fp());
      return true;
    }
    case kExprI32UConvertSatF64: {
      fcvt_wu_d(dst.gp(), src.fp(), RTZ);
      Clear_if_nan_d(dst.gp(), src.fp());
      return true;
    }
    case kExprI64SConvertSatF32:
    case kExprI64UConvertSatF32:
    case kExprI64SConvertSatF64:
    case kExprI64UConvertSatF64:
      return false;
    default:
      return false;
  }
}

void LiftoffAssembler::emit_i64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E32, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), (imm_lane_idx << 0x1) + 1);
  vmv_xs(dst.high_gp(), kSimd128ScratchReg);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx << 0x1);
  vmv_xs(dst.low_gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32_signextend_i8(Register dst, Register src) {
  slli(dst, src, 32 - 8);
  srai(dst, dst, 32 - 8);
}

void LiftoffAssembler::emit_i32_signextend_i16(Register dst, Register src) {
  slli(dst, src, 32 - 16);
  srai(dst, dst, 32 - 16);
}

void LiftoffAssembler::emit_i64_signextend_i8(LiftoffRegister dst,
                                              LiftoffRegister src) {
  emit_i32_signextend_i8(dst.low_gp(), src.low_gp());
  srai(dst.high_gp(), dst.low_gp(), 31);
}

void LiftoffAssembler::emit_i64_signextend_i16(LiftoffRegister dst,
                                               LiftoffRegister src) {
  emit_i32_signextend_i16(dst.low_gp(), src.low_gp());
  srai(dst.high_gp(), dst.low_gp(), 31);
}

void LiftoffAssembler::emit_i64_signextend_i32(LiftoffRegister dst,
                                               LiftoffRegister src) {
  mv(dst.low_gp(), src.low_gp());
  srai(dst.high_gp(), src.low_gp(), 31);
}

void LiftoffAssembler::emit_jump(Label* label) {
  MacroAssembler::Branch(label);
}

void LiftoffAssembler::emit_jump(Register target) {
  MacroAssembler::Jump(target);
}

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label,
                                      ValueKind kind, Register lhs,
                                      Register rhs,
                                      const FreezeCacheState& frozen) {
  if (rhs == no_reg) {
    DCHECK(kind == kI32);
    MacroAssembler::Branch(label, cond, lhs, Operand(zero_reg));
  } else {
    DCHECK((kind == kI32) ||
           (is_reference(kind) && (cond == kEqual || cond == kNotEqual)));
    MacroAssembler::Branch(label, cond, lhs, Operand(rhs));
  }
}

void LiftoffAssembler::emit_i32_cond_jumpi(Condition cond, Label* label,
                                           Register lhs, int32_t imm,
                                           const FreezeCacheState& frozen) {
  MacroAssembler::Branch(label, cond, lhs, Operand(imm));
}

void LiftoffAssembler::emit_i32_eqz(Register dst, Register src) {
  MacroAssembler::Sltu(dst, src, 1);
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  MacroAssembler::CompareI(dst, lhs, Operand(rhs), cond);
}

void LiftoffAssembler::emit_i64_eqz(Register dst, LiftoffRegister src) {
  Register tmp = GetUnusedRegister(kGpReg, LiftoffRegList{src, dst}).gp();
  Sltu(tmp, src.low_gp(), 1);
  Sltu(dst, src.high_gp(), 1);
  and_(dst, dst, tmp);
}

namespace liftoff {
inline Condition cond_make_unsigned(Condition cond) {
  switch (cond) {
    case kLessThan:
      return kUnsignedLessThan;
    case kLessThanEqual:
      return kUnsignedLessThanEqual;
    case kGreaterThan:
      return kUnsignedGreaterThan;
    case kGreaterThanEqual:
      return kUnsignedGreaterThanEqual;
    default:
      return cond;
  }
}
}  // namespace liftoff

void LiftoffAssembler::emit_i64_set_cond(Condition cond, Register dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  ASM_CODE_COMMENT(this);
  Label low, cont;

  // For signed i64 comparisons, we still need to use unsigned comparison for
  // the low word (the only bit carrying signedness information is the MSB in
  // the high word).
  Condition unsigned_cond = liftoff::cond_make_unsigned(cond);

  Register tmp = dst;
  if (liftoff::IsRegInRegPair(lhs, dst) || liftoff::IsRegInRegPair(rhs, dst)) {
    tmp = GetUnusedRegister(kGpReg, LiftoffRegList{dst, lhs, rhs}).gp();
  }

  // Write 1 initially in tmp register.
  MacroAssembler::li(tmp, 1);

  // If high words are equal, then compare low words, else compare high.
  Branch(&low, eq, lhs.high_gp(), Operand(rhs.high_gp()));

  Branch(&cont, cond, lhs.high_gp(), Operand(rhs.high_gp()));
  mv(tmp, zero_reg);
  Branch(&cont);

  bind(&low);
  if (unsigned_cond == cond) {
    Branch(&cont, cond, lhs.low_gp(), Operand(rhs.low_gp()));
    mv(tmp, zero_reg);
  } else {
    Label lt_zero;
    Branch(&lt_zero, lt, lhs.high_gp(), Operand(zero_reg));
    Branch(&cont, unsigned_cond, lhs.low_gp(), Operand(rhs.low_gp()));
    mv(tmp, zero_reg);
    Branch(&cont);
    bind(&lt_zero);
    Branch(&cont, unsigned_cond, rhs.low_gp(), Operand(lhs.low_gp()));
    mv(tmp, zero_reg);
    Branch(&cont);
  }
  bind(&cont);
  // Move result to dst register if needed.
  MacroAssembler::Move(dst, tmp);
}

void LiftoffAssembler::IncrementSmi(LiftoffRegister dst, int offset) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  SmiUntag(scratch, MemOperand(dst.gp(), offset));
  AddWord(scratch, scratch, Operand(1));
  SmiTag(scratch);
  Sw(scratch, MemOperand(dst.gp(), offset));
}

void LiftoffAssembler::LoadTransform(LiftoffRegister dst, Register src_addr,
                                     Register offset_reg, uintptr_t offset_imm,
                                     LoadType type,
                                     LoadTransformationKind transform,
                                     uint32_t* protected_load_pc) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm);
  VRegister dst_v = dst.fp().toV();
  *protected_load_pc = pc_offset();

  MachineType memtype = type.mem_type();
  if (transform == LoadTransformationKind::kExtend) {
    // TODO(RISCV): need to confirm the performance impact of using floating
    // point registers.
    LoadDouble(kScratchDoubleReg, src_op);
    if (memtype == MachineType::Int8()) {
      VU.set(kScratchReg, E64, m1);
      vfmv_vf(kSimd128ScratchReg, kScratchDoubleReg);
      VU.set(kScratchReg, E16, m1);
      vsext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Uint8()) {
      VU.set(kScratchReg, E64, m1);
      vfmv_vf(kSimd128ScratchReg, kScratchDoubleReg);
      VU.set(kScratchReg, E16, m1);
      vzext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Int16()) {
      VU.set(kScratchReg, E64, m1);
      vfmv_vf(kSimd128ScratchReg, kScratchDoubleReg);
      VU.set(kScratchReg, E32, m1);
      vsext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Uint16()) {
      VU.set(kScratchReg, E64, m1);
      vfmv_vf(kSimd128ScratchReg, kScratchDoubleReg);
      VU.set(kScratchReg, E32, m1);
      vzext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Int32()) {
      VU.set(kScratchReg, E64, m1);
      vfmv_vf(kSimd128ScratchReg, kScratchDoubleReg);
      vsext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Uint32()) {
      VU.set(kScratchReg, E64, m1);
      vfmv_vf(kSimd128ScratchReg, kScratchDoubleReg);
      vzext_vf2(dst_v, kSimd128ScratchReg);
    }
  } else if (transform == LoadTransformationKind::kZeroExtend) {
    vxor_vv(dst_v, dst_v, dst_v);
    if (memtype == MachineType::Int32()) {
      VU.set(kScratchReg, E32, m1);
      Lw(scratch, src_op);
      vmv_sx(dst_v, scratch);
    } else {
      DCHECK_EQ(MachineType::Int64(), memtype);
      VU.set(kScratchReg, E64, m1);
      LoadDouble(kScratchDoubleReg, src_op);
      vfmv_sf(dst_v, kScratchDoubleReg);
    }
  } else {
    DCHECK_EQ(LoadTransformationKind::kSplat, transform);
    if (memtype == MachineType::Int8()) {
      VU.set(kScratchReg, E8, m1);
      Lb(scratch, src_op);
      vmv_vx(dst_v, scratch);
    } else if (memtype == MachineType::Int16()) {
      VU.set(kScratchReg, E16, m1);
      Lh(scratch, src_op);
      vmv_vx(dst_v, scratch);
    } else if (memtype == MachineType::Int32()) {
      VU.set(kScratchReg, E32, m1);
      Lw(scratch, src_op);
      vmv_vx(dst_v, scratch);
    } else if (memtype == MachineType::Int64()) {
      VU.set(kScratchReg, E64, m1);
      LoadDouble(kScratchDoubleReg, src_op);
      vfmv_vf(dst_v, kScratchDoubleReg);
    }
  }
}

void LiftoffAssembler::LoadLane(LiftoffRegister dst, LiftoffRegister src,
                                Register addr, Register offset_reg,
                                uintptr_t offset_imm, LoadType type,
                                uint8_t laneidx, uint32_t* protected_load_pc,
                                bool /* i64_offfset */) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MemOperand src_op = liftoff::GetMemOp(this, addr, offset_reg, offset_imm);
  MachineType mem_type = type.mem_type();
  *protected_load_pc = pc_offset();
  if (mem_type == MachineType::Int8()) {
    Lbu(scratch, src_op);
    VU.set(kScratchReg, E32, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    VU.set(kScratchReg, E8, m1);
    vmerge_vx(dst.fp().toV(), scratch, dst.fp().toV());
  } else if (mem_type == MachineType::Int16()) {
    Lhu(scratch, src_op);
    VU.set(kScratchReg, E16, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vmerge_vx(dst.fp().toV(), scratch, dst.fp().toV());
  } else if (mem_type == MachineType::Int32()) {
    Lw(scratch, src_op);
    VU.set(kScratchReg, E32, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vmerge_vx(dst.fp().toV(), scratch, dst.fp().toV());
  } else if (mem_type == MachineType::Int64()) {
    LoadDouble(kScratchDoubleReg, src_op);
    VU.set(kScratchReg, E64, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vfmerge_vf(dst.fp().toV(), kScratchDoubleReg, dst.fp().toV());
  } else {
    UNREACHABLE();
  }
}

void LiftoffAssembler::StoreLane(Register dst, Register offset,
                                 uintptr_t offset_imm, LiftoffRegister src,
                                 StoreType type, uint8_t lane,
                                 uint32_t* protected_store_pc,
                                 bool /* i64_offfset */) {
  MemOperand dst_op = liftoff::GetMemOp(this, dst, offset, offset_imm);
  if (protected_store_pc) *protected_store_pc = pc_offset();
  MachineRepresentation rep = type.mem_rep();
  if (rep == MachineRepresentation::kWord8) {
    VU.set(kScratchReg, E8, m1);
    vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), lane);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    Sb(kScratchReg, dst_op);
  } else if (rep == MachineRepresentation::kWord16) {
    VU.set(kScratchReg, E16, m1);
    vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), lane);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    Sh(kScratchReg, dst_op);
  } else if (rep == MachineRepresentation::kWord32) {
    VU.set(kScratchReg, E32, m1);
    vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), lane);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    Sw(kScratchReg, dst_op);
  } else {
    DCHECK_EQ(MachineRepresentation::kWord64, rep);
    VU.set(kScratchReg, E64, m1);
    vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), lane);
    vfmv_fs(kScratchDoubleReg, kSimd128ScratchReg);
    StoreDouble(kScratchDoubleReg, dst_op);
  }
}

void LiftoffAssembler::emit_i64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vmv_vi(v0, 0b0101);
  vmv_vx(kSimd128ScratchReg, src.high_gp());
  vmerge_vx(dst.fp().toV(), src.low_gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E32, m1);
  vmv_vx(kSimd128ScratchReg, src2.high_gp());
  vmv_sx(kSimd128ScratchReg, src2.low_gp());
  VU.set(kScratchReg, E64, m1);
  li(kScratchReg, 0x1 << imm_lane_idx);
  vmv_sx(v0, kScratchReg);
  vfmv_fs(kScratchDoubleReg, kSimd128ScratchReg);
  vfmerge_vf(dst.fp().toV(), kScratchDoubleReg, src1.fp().toV());
}

void LiftoffAssembler::emit_f64x2_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  const int32_t kNaN = 0x7ff80000L, kNaNShift = 32;
  vmfeq_vv(v0, lhs.fp().toV(), lhs.fp().toV());
  vmfeq_vv(kSimd128ScratchReg, rhs.fp().toV(), rhs.fp().toV());
  vand_vv(v0, v0, kSimd128ScratchReg);
  li(kScratchReg, kNaN);
  li(kScratchReg2, kNaNShift);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  vsll_vx(kSimd128ScratchReg, kSimd128ScratchReg, kScratchReg2);
  vfmin_vv(kSimd128ScratchReg, rhs.fp().toV(), lhs.fp().toV(), Mask);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_f64x2_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  const int32_t kNaN = 0x7ff80000L, kNaNShift = 32;
  vmfeq_vv(v0, lhs.fp().toV(), lhs.fp().toV());
  vmfeq_vv(kSimd128ScratchReg, rhs.fp().toV(), rhs.fp().toV());
  vand_vv(v0, v0, kSimd128ScratchReg);
  li(kScratchReg, kNaN);
  li(kScratchReg2, kNaNShift);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  vsll_vx(kSimd128ScratchReg, kSimd128ScratchReg, kScratchReg2);
  vfmax_vv(kSimd128ScratchReg, rhs.fp().toV(), lhs.fp().toV(), Mask);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_extadd_pairwise_i16x8_s(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  // li(kScratchReg, 0x0006000400020000);
  li(kScratchReg, 0x00060004);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 0x00020000);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  // li(kScratchReg, 0x0007000500030001);
  li(kScratchReg, 0x00070005);
  vmv_vx(kSimd128ScratchReg3, kScratchReg);
  li(kScratchReg, 0x00030001);
  vmv_sx(kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg, E16, m1);
  vrgather_vv(kSimd128ScratchReg2, src.fp().toV(), kSimd128ScratchReg);
  vrgather_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg3);
  VU.set(kScratchReg, E16, mf2);
  vwadd_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i32x4_extadd_pairwise_i16x8_u(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  // li(kScratchReg, 0x0006000400020000);
  li(kScratchReg, 0x00060004);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 0x00020000);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  // li(kScratchReg, 0x0007000500030001);
  li(kScratchReg, 0x00070005);
  vmv_vx(kSimd128ScratchReg3, kScratchReg);
  li(kScratchReg, 0x00030001);
  vmv_sx(kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg, E16, m1);
  vrgather_vv(kSimd128ScratchReg2, src.fp().toV(), kSimd128ScratchReg);
  vrgather_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg3);
  VU.set(kScratchReg, E16, mf2);
  vwaddu_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i16x8_extadd_pairwise_i8x16_s(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  // li(kScratchReg, 0x0E0C0A0806040200);
  li(kScratchReg, 0x0E0C0A08);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 0x06040200);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  // li(kScratchReg, 0x0F0D0B0907050301);
  li(kScratchReg, 0x0F0D0B09);
  vmv_vx(kSimd128ScratchReg3, kScratchReg);
  li(kScratchReg, 0x07050301);
  vmv_sx(kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg, E8, m1);
  vrgather_vv(kSimd128ScratchReg2, src.fp().toV(), kSimd128ScratchReg);
  vrgather_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg3);
  VU.set(kScratchReg, E8, mf2);
  vwadd_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i16x8_extadd_pairwise_i8x16_u(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  // li(kScratchReg, 0x0E0C0A0806040200);
  li(kScratchReg, 0x0E0C0A08);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 0x06040200);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  // li(kScratchReg, 0x0F0D0B0907050301);
  li(kScratchReg, 0x0F0D0B09);
  vmv_vx(kSimd128ScratchReg3, kScratchReg);
  li(kScratchReg, 0x07050301);
  vmv_sx(kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg, E8, m1);
  vrgather_vv(kSimd128ScratchReg2, src.fp().toV(), kSimd128ScratchReg);
  vrgather_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg3);
  VU.set(kScratchReg, E8, mf2);
  vwaddu_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::CallCWithStackBuffer(
    const std::initializer_list<VarState> args, const LiftoffRegister* rets,
    ValueKind return_kind, ValueKind out_argument_kind, int stack_bytes,
    ExternalReference ext_ref) {
  AddWord(sp, sp, Operand(-stack_bytes));

  int arg_offset = 0;
  for (const VarState& arg : args) {
    UseScratchRegisterScope temps(this);
    Register src = no_reg;
    MemOperand dst{sp, arg_offset};
    if (arg.is_reg()) {
      liftoff::Store(this, sp, arg_offset, arg.reg(), arg.kind());
    } else if (arg.is_const()) {
      DCHECK_EQ(kI32, arg.kind());
      if (arg.i32_const() == 0) {
        src = zero_reg;
      } else {
        src = temps.Acquire();
        li(src, arg.i32_const());
      }
      StoreWord(src, dst);
    } else {
      DCHECK_EQ(value_kind_size(arg.kind()), 4);
      MemOperand src = liftoff::GetStackSlot(arg.offset());
      auto scratch = temps.Acquire();
      Lw(scratch, src);
      Sw(scratch, dst);
    }
    arg_offset += value_kind_size(arg.kind());
  }
  DCHECK_LE(arg_offset, stack_bytes);

  // Pass a pointer to the buffer with the arguments to the C function.
  // On RISC-V, the first argument is passed in {a0}.
  constexpr Register kFirstArgReg = a0;
  mv(kFirstArgReg, sp);

  // Now call the C function.
  constexpr int kNumCCallArgs = 1;
  PrepareCallCFunction(kNumCCallArgs, kScratchReg);
  CallCFunction(ext_ref, kNumCCallArgs);

  // Move return value to the right register.
  const LiftoffRegister* next_result_reg = rets;
  if (return_kind != kVoid) {
    constexpr Register kReturnReg = a0;
    if (kReturnReg != next_result_reg->gp()) {
      Move(*next_result_reg, LiftoffRegister(kReturnReg), return_kind);
    }
    ++next_result_reg;
  }

  // Load potential output value from the buffer on the stack.
  if (out_argument_kind != kVoid) {
    liftoff::Load(this, *next_result_reg, sp, 0, out_argument_kind);
  }

  AddWord(sp, sp, Operand(stack_bytes));
}

void LiftoffAssembler::CallC(const std::initializer_list<VarState> args,
                             ExternalReference ext_ref) {
  // First, prepare the stack for the C call.
  int num_args = static_cast<int>(args.size());
  PrepareCallCFunction(num_args, kScratchReg);
  // Then execute the parallel register move and also move values to parameter
  // stack slots.
  int reg_args = 0;
  int stack_args = 0;
  ParallelMove parallel_move{this};
  for (const VarState& arg : args) {
    if (needs_gp_reg_pair(arg.kind())) {
      // All i64 arguments (currently) fully fit in the register parameters.
      DCHECK_LE(reg_args + 2, arraysize(kCArgRegs));
      parallel_move.LoadIntoRegister(
          LiftoffRegister::ForPair(kCArgRegs[reg_args],
                                   kCArgRegs[reg_args + 1]),
          arg);
      reg_args += 2;
      continue;
    }
    if (reg_args < int{arraysize(kCArgRegs)}) {
      parallel_move.LoadIntoRegister(LiftoffRegister{kCArgRegs[reg_args]}, arg);
      ++reg_args;
      continue;
    }
    MemOperand dst{sp, stack_args * kSystemPointerSize};
    ++stack_args;
    if (arg.is_reg()) {
      liftoff::Store(this, dst.rm(), dst.offset(), arg.reg(), arg.kind());
      continue;
    }
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    if (arg.is_const()) {
      DCHECK_EQ(kI32, arg.kind());
      li(scratch, Operand(arg.i32_const()));
      Sw(scratch, dst);
    } else {
      // Stack to stack move.
      MemOperand src = liftoff::GetStackSlot(arg.offset());
      Lw(scratch, src);
      Sw(scratch, dst);
    }
  }
  parallel_move.Execute();
  // Now call the C function.
  PrepareCallCFunction(num_args, kScratchReg);
  CallCFunction(ext_ref, num_args);
}

void LiftoffStackSlots::Construct(int param_slots) {
  ASM_CODE_COMMENT(asm_);
  DCHECK_LT(0, slots_.size());
  SortInPushOrder();
  int last_stack_slot = param_slots;
  for (auto& slot : slots_) {
    const int stack_slot = slot.dst_slot_;
    int stack_decrement = (last_stack_slot - stack_slot) * kSystemPointerSize;
    DCHECK_LT(0, stack_decrement);
    last_stack_slot = stack_slot;
    const LiftoffAssembler::VarState& src = slot.src_;
    switch (src.loc()) {
      case LiftoffAssembler::VarState::kStack: {
        switch (src.kind()) {
          // i32 and i64 can be treated as similar cases, i64 being previously
          // split into two i32 registers
          case kI32:
          case kI64:
          case kF32:
          case kRef:
          case kRefNull: {
            asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
            UseScratchRegisterScope temps(asm_);
            Register scratch = temps.Acquire();
            asm_->Lw(scratch,
                     liftoff::GetHalfStackSlot(slot.src_offset_, slot.half_));
            asm_->Push(scratch);
          } break;
          case kF64: {
            asm_->AllocateStackSpace(stack_decrement - kDoubleSize);
            DCHECK_EQ(kLowWord, slot.half_);
            asm_->Lw(kScratchReg,
                     liftoff::GetHalfStackSlot(slot.src_offset_, kHighWord));
            asm_->push(kScratchReg);
            asm_->Lw(kScratchReg,
                     liftoff::GetHalfStackSlot(slot.src_offset_, kLowWord));
            asm_->push(kScratchReg);
          } break;
          case kS128: {
            asm_->AllocateStackSpace(stack_decrement - kSimd128Size);
            asm_->Lw(kScratchReg, liftoff::GetStackSlot(slot.src_offset_ - 8));
            asm_->push(kScratchReg);
            asm_->Lw(kScratchReg, liftoff::GetStackSlot(slot.src_offset_));
            asm_->push(kScratchReg);
          } break;
          default:
            UNREACHABLE();
        }
        break;
      }
      case LiftoffAssembler::VarState::kRegister: {
        int pushed_bytes = SlotSizeInBytes(slot);
        asm_->AllocateStackSpace(stack_decrement - pushed_bytes);
        if (src.kind() == kI64) {
          liftoff::push(
              asm_, slot.half_ == kLowWord ? src.reg().low() : src.reg().high(),
              kI32);
        } else {
          liftoff::push(asm_, src.reg(), src.kind());
        }
        break;
      }
      case LiftoffAssembler::VarState::kIntConst: {
        asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
        asm_->li(kScratchReg, Operand(src.i32_const()));
        asm_->push(kScratchReg);
        break;
      }
    }
  }
}

bool LiftoffAssembler::supports_f16_mem_access() { return false; }

}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV32_INL_H_
