// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV64_INL_H_
#define V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV64_INL_H_

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

inline MemOperand GetMemOp(LiftoffAssembler* assm, Register addr,
                           Register offset, uintptr_t offset_imm,
                           bool i64_offset = false, unsigned shift_amount = 0) {
  if (offset != no_reg) {
    if (!i64_offset) {
      // extract bit[0:31] without sign extend
      assm->ExtractBits(kScratchReg2, offset, 0, 32, false);
      offset = kScratchReg2;
    }
    if (shift_amount != 0) {
      assm->CalcScaledAddress(kScratchReg2, addr, offset, shift_amount);
    } else {
      assm->Add64(kScratchReg2, offset, addr);
    }
    addr = kScratchReg2;
  }
  if (is_int31(offset_imm)) {
    int32_t offset_imm32 = static_cast<int32_t>(offset_imm);
    return MemOperand(addr, offset_imm32);
  } else {
    assm->li(kScratchReg, Operand(offset_imm));
    assm->Add64(kScratchReg2, addr, kScratchReg);
    return MemOperand(kScratchReg2, 0);
  }
}

inline void Load(LiftoffAssembler* assm, LiftoffRegister dst, MemOperand src,
                 ValueKind kind) {
  switch (kind) {
    case kI32:
      assm->Lw(dst.gp(), src);
      break;
    case kI64:
    case kRef:
    case kRefNull:
    case kRtt:
      assm->Ld(dst.gp(), src);
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
      assm->Sw(src.gp(), dst);
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      assm->Sd(src.gp(), dst);
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
        assm->Add64(kScratchReg, dst.rm(), dst.offset());
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
      assm->addi(sp, sp, -kSystemPointerSize);
      assm->Sw(reg.gp(), MemOperand(sp, 0));
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      assm->push(reg.gp());
      break;
    case kF32:
      assm->addi(sp, sp, -kSystemPointerSize);
      assm->StoreFloat(reg.fp(), MemOperand(sp, 0));
      break;
    case kF64:
      assm->addi(sp, sp, -kSystemPointerSize);
      assm->StoreDouble(reg.fp(), MemOperand(sp, 0));
      break;
    case kS128:{
      assm->VU.set(kScratchReg, E8, m1);
      assm->addi(sp, sp, -kSystemPointerSize * 2);
      assm->vs(reg.fp().toV(), sp, 0, VSew::E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

inline void StoreToMemory(LiftoffAssembler* assm, MemOperand dst,
                          const LiftoffAssembler::VarState& src) {
  UseScratchRegisterScope temps(assm);
  if (src.is_const()) {
    Register src_reg = no_reg;
    if (src.i32_const() == 0) {
      src_reg = zero_reg;
    } else {
      src_reg = temps.Acquire();
      assm->li(src_reg, src.i32_const());
    }
    assm->StoreWord(src_reg, dst);
  } else if (src.is_reg()) {
    switch (src.kind()) {
      case kI32:
        return assm->Sw(src.reg().gp(), dst);
      case kI64:
      case kRef:
      case kRefNull:
      case kRtt:
        return assm->Sd(src.reg().gp(), dst);
      case kF32:
        return assm->StoreFloat(src.reg().fp(), dst);
      case kF64:
        return assm->StoreDouble(src.reg().fp(), dst);
      case kS128: {
        assm->VU.set(kScratchReg, E8, m1);
        Register dst_reg = temps.Acquire();
        assm->Add64(dst_reg, dst.rm(), dst.offset());
        assm->vs(src.reg().fp().toV(), dst_reg, 0, VSew::E8);
        return;
      }
      default:
        UNREACHABLE();
    }
  } else {
    DCHECK(src.is_stack());
    Register temp = temps.Acquire();
    switch (src.kind()) {
      case kI32:
        assm->Lw(temp, GetStackSlot(src.offset()));
        assm->Sw(temp, dst);
        return;
      case kI64:
      case kRef:
      case kRefNull:
        assm->Ld(temp, GetStackSlot(src.offset()));
        assm->Sd(temp, dst);
        return;
      case kF32:
        assm->LoadFloat(kScratchDoubleReg, GetStackSlot(src.offset()));
        assm->StoreFloat(kScratchDoubleReg, dst);
        return;
      case kF64:
        assm->LoadDouble(kScratchDoubleReg, GetStackSlot(src.offset()));
        assm->StoreDouble(kScratchDoubleReg, dst);
        return;
      case kS128: {
        assm->VU.set(kScratchReg, E8, m1);
        Register src_reg = temp;
        assm->Add64(src_reg, sp, src.offset());
        assm->vl(kScratchDoubleReg.toV(), src_reg, 0, VSew::E8);
        Register dst_reg = temp;
        assm->Add64(dst_reg, dst.rm(), dst.offset());
        assm->vs(kScratchDoubleReg.toV(), dst_reg, 0, VSew::E8);
        return;
      }
      default:
        UNREACHABLE();
    }
  }
}

}  // namespace liftoff

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value) {
  switch (value.type().kind()) {
    case kI32:
      MacroAssembler::li(reg.gp(), Operand(value.to_i32()));
      break;
    case kI64:
      MacroAssembler::li(reg.gp(), Operand(value.to_i64()));
      break;
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
  unsigned shift_amount = !needs_shift ? 0 : COMPRESS_POINTERS_BOOL ? 2 : 3;
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm,
                                        false, shift_amount);
  Assembler::BlockPoolsScope blocked_pools_scope_(this, 4 * kInstrSize);
  LoadTaggedField(dst, src_op);

  // Since LoadTaggedField might start with an instruction loading an immediate
  // argument to a register, we have to compute the {protected_load_pc} after
  // calling it.
  // In case of compressed pointers, there is an additional instruction
  // (pointer decompression) after the load.
  uint8_t protected_instruction_offset_bias =
      COMPRESS_POINTERS_BOOL ? 2 * kInstrSize : kInstrSize;
  if (protected_load_pc) {
    *protected_load_pc = pc_offset() - protected_instruction_offset_bias;
    DCHECK(InstructionAt(*protected_load_pc)->IsLoad());
  }
}

void LiftoffAssembler::LoadProtectedPointer(Register dst, Register src_addr,
                                            int32_t offset_imm) {
  LoadProtectedPointerField(dst, MemOperand{src_addr, offset_imm});
}

void LiftoffAssembler::LoadFullPointer(Register dst, Register src_addr,
                                       int32_t offset_imm) {
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, no_reg, offset_imm);
  LoadWord(dst, src_op);
}

#ifdef V8_ENABLE_SANDBOX
void LiftoffAssembler::LoadCodeEntrypointViaCodePointer(Register dst,
                                                        Register src_addr,
                                                        int32_t offset_imm) {
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, no_reg, offset_imm);
  MacroAssembler::LoadCodeEntrypointViaCodePointer(dst, src_op,
                                                   kWasmEntrypointTag);
}
#endif

void LiftoffAssembler::StoreTaggedPointer(Register dst_addr,
                                          Register offset_reg,
                                          int32_t offset_imm, Register src,
                                          LiftoffRegList pinned,
                                          uint32_t* protected_store_pc,
                                          SkipWriteBarrier skip_write_barrier) {
  UseScratchRegisterScope temps(this);
  Operand offset_op =
      offset_reg.is_valid() ? Operand(offset_reg) : Operand(offset_imm);
  // For the write barrier (below), we cannot have both an offset register and
  // an immediate offset. Add them to a 32-bit offset initially, but in a 64-bit
  // register, because that's needed in the MemOperand below.
  if (offset_reg.is_valid() && offset_imm) {
    Register effective_offset = temps.Acquire();
    AddWord(effective_offset, offset_reg, Operand(offset_imm));
    offset_op = Operand(effective_offset);
  }

  if (offset_op.is_reg()) {
    AddWord(kScratchReg, dst_addr, offset_op.rm());
    StoreTaggedField(src, MemOperand(kScratchReg, 0));
  } else {
    StoreTaggedField(src, MemOperand(dst_addr, offset_imm));
  }

  // Since StoreTaggedField might start with an instruction loading an immediate
  // argument to a register, we have to compute the {protected_load_pc} after
  // calling it.
  if (protected_store_pc) {
    *protected_store_pc = pc_offset() - kInstrSize;
  }

  if (skip_write_barrier || v8_flags.disable_write_barriers) return;

  Label exit;
  CheckPageFlag(dst_addr, MemoryChunk::kPointersFromHereAreInterestingMask,
                kZero, &exit);
  JumpIfSmi(src, &exit);
  CheckPageFlag(src, MemoryChunk::kPointersToHereAreInterestingMask, eq, &exit);
  CallRecordWriteStubSaveRegisters(dst_addr, offset_op, SaveFPRegsMode::kSave,
                                   StubCallMode::kCallWasmRuntimeStub);
  bind(&exit);
}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uintptr_t offset_imm,
                            LoadType type, uint32_t* protected_load_pc,
                            bool is_load_mem, bool i64_offset,
                            bool needs_shift) {
  unsigned shift_amount = needs_shift ? type.size_log_2() : 0;
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm,
                                        i64_offset, shift_amount);
  Assembler::BlockPoolsScope blocked_pools_scope_(this, 4 * kInstrSize);
  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI64Load8U:
      Lbu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load8S:
    case LoadType::kI64Load8S:
      Lb(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U:
      MacroAssembler::Lhu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16S:
    case LoadType::kI64Load16S:
      MacroAssembler::Lh(dst.gp(), src_op);
      break;
    case LoadType::kI64Load32U:
      MacroAssembler::Lwu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load:
    case LoadType::kI64Load32S:
      MacroAssembler::Lw(dst.gp(), src_op);
      break;
    case LoadType::kI64Load:
      MacroAssembler::Ld(dst.gp(), src_op);
      break;
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
  // Since {Ldr*} macros might start with an instruction loading an immediate
  // argument to a register, we have to compute the {protected_load_pc} after
  // calling them.
  if (protected_load_pc) {
    *protected_load_pc = pc_offset() - kInstrSize;
    DCHECK(InstructionAt(*protected_load_pc)->IsLoad());
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
  MemOperand dst_op =
      liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm, i64_offset);

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

  Assembler::BlockPoolsScope blocked_pools_scope_(this, 4 * kInstrSize);
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      Sb(src.gp(), dst_op);
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      MacroAssembler::Sh(src.gp(), dst_op);
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      MacroAssembler::Sw(src.gp(), dst_op);
      break;
    case StoreType::kI64Store:
      MacroAssembler::Sd(src.gp(), dst_op);
      break;
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
        Add64(kScratchReg, dst_op.rm(), dst_op.offset());
      }
      vs(src.fp().toV(), dst_reg, 0, VSew::E8);
      break;
    }
    default:
      UNREACHABLE();
  }
  // Since {Str*} macros might start with an instruction loading an immediate
  // argument to a register, we have to compute the {protected_load_pc} after
  // calling them.
  if (protected_store_pc) {
    *protected_store_pc = pc_offset() - kInstrSize;
    DCHECK(InstructionAt(*protected_store_pc)->IsStore());
  }
}

namespace liftoff {
#define __ lasm->

inline Register CalculateActualAddress(LiftoffAssembler* lasm,
                                       UseScratchRegisterScope& temps,
                                       Register addr_reg, Register offset_reg,
                                       uintptr_t offset_imm) {
  DCHECK_NE(addr_reg, no_reg);
  if (offset_reg == no_reg && offset_imm == 0) return addr_reg;
  Register result = temps.Acquire();
  if (offset_reg == no_reg) {
    __ AddWord(result, addr_reg, Operand(offset_imm));
  } else {
    __ AddWord(result, addr_reg, Operand(offset_reg));
    if (offset_imm != 0) __ AddWord(result, result, Operand(offset_imm));
  }
  return result;
}

enum class Binop { kAdd, kSub, kAnd, kOr, kXor, kExchange };

inline void AtomicBinop(LiftoffAssembler* lasm, Register dst_addr,
                        Register offset_reg, uintptr_t offset_imm,
                        LiftoffRegister value, LiftoffRegister result,
                        StoreType type, Binop op) {
  LiftoffRegList pinned{dst_addr, value, result};
  if (offset_reg != no_reg) pinned.set(offset_reg);
  Register store_result = pinned.set(__ GetUnusedRegister(kGpReg, pinned)).gp();

  // Make sure that {result} is unique.
  Register result_reg = result.gp();
  if (result_reg == value.gp() || result_reg == dst_addr ||
      result_reg == offset_reg) {
    result_reg = __ GetUnusedRegister(kGpReg, pinned).gp();
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
      __ lr_w(true, false, result_reg, actual_addr);
      __ ZeroExtendWord(result_reg, result_reg);
      break;
    case StoreType::kI32Store:
      __ lr_w(true, false, result_reg, actual_addr);
      break;
    case StoreType::kI64Store:
      __ lr_d(true, false, result_reg, actual_addr);
      break;
    default:
      UNREACHABLE();
  }

  switch (op) {
    case Binop::kAdd:
      __ add(temp, result_reg, value.gp());
      break;
    case Binop::kSub:
      __ sub(temp, result_reg, value.gp());
      break;
    case Binop::kAnd:
      __ and_(temp, result_reg, value.gp());
      break;
    case Binop::kOr:
      __ or_(temp, result_reg, value.gp());
      break;
    case Binop::kXor:
      __ xor_(temp, result_reg, value.gp());
      break;
    case Binop::kExchange:
      __ mv(temp, value.gp());
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
    case StoreType::kI64Store:
      __ sc_d(false, true, store_result, actual_addr, temp);
      break;
    default:
      UNREACHABLE();
  }

  __ bnez(store_result, &retry);
  if (result_reg != result.gp()) {
    __ mv(result.gp(), result_reg);
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
  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI64Load8U:
      lbu(dst.gp(), src_reg, 0);
      sync();
      return;
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U:
      lhu(dst.gp(), src_reg, 0);
      sync();
      return;
    case LoadType::kI32Load:
      lw(dst.gp(), src_reg, 0);
      sync();
      return;
    case LoadType::kI64Load32U:
      lwu(dst.gp(), src_reg, 0);
      sync();
      return;
    case LoadType::kI64Load:
      ld(dst.gp(), src_reg, 0);
      sync();
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
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI32Store8:
      sync();
      sb(src.gp(), dst_reg, 0);
      return;
    case StoreType::kI64Store16:
    case StoreType::kI32Store16:
      sync();
      sh(src.gp(), dst_reg, 0);
      return;
    case StoreType::kI64Store32:
    case StoreType::kI32Store:
      sync();
      sw(src.gp(), dst_reg, 0);
      return;
    case StoreType::kI64Store:
      sync();
      sd(src.gp(), dst_reg, 0);
      return;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicAdd(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kAdd);
}

void LiftoffAssembler::AtomicSub(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kSub);
}

void LiftoffAssembler::AtomicAnd(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kAnd);
}

void LiftoffAssembler::AtomicOr(Register dst_addr, Register offset_reg,
                                uintptr_t offset_imm, LiftoffRegister value,
                                LiftoffRegister result, StoreType type,
                                bool i64_offset) {
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kOr);
}

void LiftoffAssembler::AtomicXor(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kXor);
}

void LiftoffAssembler::AtomicExchange(Register dst_addr, Register offset_reg,
                                      uintptr_t offset_imm,
                                      LiftoffRegister value,
                                      LiftoffRegister result, StoreType type,
                                      bool i64_offset) {
  liftoff::AtomicBinop(this, dst_addr, offset_reg, offset_imm, value, result,
                       type, liftoff::Binop::kExchange);
}

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(load_linked,       \
                                                 store_conditional) \
  do {                                                              \
    Label compareExchange;                                          \
    Label exit;                                                     \
    sync();                                                         \
    bind(&compareExchange);                                         \
    load_linked(result.gp(), MemOperand(temp0, 0));                 \
    BranchShort(&exit, ne, expected.gp(), Operand(result.gp()));    \
    mv(temp2, new_value.gp());                                      \
    store_conditional(temp2, MemOperand(temp0, 0));                 \
    BranchShort(&compareExchange, ne, temp2, Operand(zero_reg));    \
    bind(&exit);                                                    \
    sync();                                                         \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(            \
    load_linked, store_conditional, size, aligned)               \
  do {                                                           \
    Label compareExchange;                                       \
    Label exit;                                                  \
    andi(temp1, temp0, aligned);                                 \
    Sub64(temp0, temp0, Operand(temp1));                         \
    Sll32(temp1, temp1, 3);                                      \
    sync();                                                      \
    bind(&compareExchange);                                      \
    load_linked(temp2, MemOperand(temp0, 0));                    \
    ExtractBits(result.gp(), temp2, temp1, size, false);         \
    ExtractBits(temp2, expected.gp(), zero_reg, size, false);    \
    BranchShort(&exit, ne, temp2, Operand(result.gp()));         \
    InsertBits(temp2, new_value.gp(), temp1, size);              \
    store_conditional(temp2, MemOperand(temp0, 0));              \
    BranchShort(&compareExchange, ne, temp2, Operand(zero_reg)); \
    bind(&exit);                                                 \
    sync();                                                      \
  } while (0)

void LiftoffAssembler::AtomicCompareExchange(
    Register dst_addr, Register offset_reg, uintptr_t offset_imm,
    LiftoffRegister expected, LiftoffRegister new_value, LiftoffRegister result,
    StoreType type, bool i64_offset) {
  LiftoffRegList pinned{dst_addr, expected, new_value, result};
  if (offset_reg != no_reg) pinned.set(offset_reg);

  Register temp0 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp1 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp2 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  MemOperand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);
  Add64(temp0, dst_op.rm(), dst_op.offset());
  switch (type.value()) {
    case StoreType::kI64Store8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, 8, 7);
      break;
    case StoreType::kI32Store8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, 8, 3);
      break;
    case StoreType::kI64Store16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, 16, 7);
      break;
    case StoreType::kI32Store16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, 16, 3);
      break;
    case StoreType::kI64Store32:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, 32, 7);
      break;
    case StoreType::kI32Store:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll, Sc);
      break;
    case StoreType::kI64Store:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Lld, Scd);
      break;
    default:
      UNREACHABLE();
  }
}
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT

void LiftoffAssembler::AtomicFence() { sync(); }

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx,
                                           ValueKind kind) {
  MemOperand src(fp, kSystemPointerSize * (caller_slot_idx + 1));
  liftoff::Load(this, dst, src, kind);
}

void LiftoffAssembler::StoreCallerFrameSlot(LiftoffRegister src,
                                            uint32_t caller_slot_idx,
                                            ValueKind kind) {
  int32_t offset = kSystemPointerSize * (caller_slot_idx + 1);
  liftoff::Store(this, fp, offset, src, kind);
}

void LiftoffAssembler::LoadReturnStackSlot(LiftoffRegister dst, int offset,
                                           ValueKind kind) {
  liftoff::Load(this, dst, MemOperand(sp, offset), kind);
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
      Ld(kScratchReg, src);
      Sd(kScratchReg, dst);
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
        MacroAssembler::Add64(src_reg, src.rm(), src.offset());
      }
      vl(kSimd128ScratchReg, src_reg, 0, E8);
      Register dst_reg = dst.offset() == 0 ? dst.rm() : kScratchReg;
      if (dst.offset() != 0) {
        Add64(kScratchReg, dst.rm(), dst.offset());
      }
      vs(kSimd128ScratchReg, dst_reg, 0, VSew::E8);
      break;
    }
    case kVoid:
    case kI8:
    case kI16:
    case kBottom:
    case kF16:
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
      Sw(reg.gp(), dst);
      break;
    case kI64:
    case kRef:
    case kRefNull:
    case kRtt:
      Sd(reg.gp(), dst);
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
        Add64(kScratchReg, dst.rm(), dst.offset());
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
  switch (value.type().kind()) {
    case kI32: {
      UseScratchRegisterScope temps(this);
      Register tmp = temps.Acquire();
      MacroAssembler::li(tmp, Operand(value.to_i32()));
      Sw(tmp, dst);
      break;
    }
    case kI64:
    case kRef:
    case kRefNull: {
      UseScratchRegisterScope temps(this);
      Register tmp = temps.Acquire();
      MacroAssembler::li(tmp, value.to_i64());
      Sd(tmp, dst);
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
      Lw(reg.gp(), src);
      break;
    case kI64:
    case kRef:
    case kRefNull:
      Ld(reg.gp(), src);
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
        MacroAssembler::Add64(src_reg, src.rm(), src.offset());
      }
      vl(reg.fp().toV(), src_reg, 0, E8);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::FillI64Half(Register, int offset, RegPairHalf) {
  UNREACHABLE();
}

void LiftoffAssembler::FillStackSlotsWithZero(int start, int size) {
  DCHECK_LT(0, size);
  RecordUsedSpillOffset(start + size);

  if (size <= 12 * kStackSlotSize) {
    // Special straight-line code for up to 12 slots. Generates one
    // instruction per slot (<= 12 instructions total).
    uint32_t remainder = size;
    for (; remainder >= kStackSlotSize; remainder -= kStackSlotSize) {
      Sd(zero_reg, liftoff::GetStackSlot(start + remainder));
    }
    DCHECK(remainder == 4 || remainder == 0);
    if (remainder) {
      Sw(zero_reg, liftoff::GetStackSlot(start + remainder));
    }
  } else {
    // General case for bigger counts (12 instructions).
    // Use a0 for start address (inclusive), a1 for end address (exclusive).
    Push(a1, a0);
    Add64(a0, fp, Operand(-start - size));
    Add64(a1, fp, Operand(-start));

    Label loop;
    bind(&loop);
    Sd(zero_reg, MemOperand(a0));
    addi(a0, a0, kSystemPointerSize);
    BranchShort(&loop, ne, a0, Operand(a1));

    Pop(a1, a0);
  }
}

void LiftoffAssembler::emit_i64_clz(LiftoffRegister dst, LiftoffRegister src) {
  MacroAssembler::Clz64(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_ctz(LiftoffRegister dst, LiftoffRegister src) {
  MacroAssembler::Ctz64(dst.gp(), src.gp());
}

bool LiftoffAssembler::emit_i64_popcnt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  MacroAssembler::Popcnt64(dst.gp(), src.gp(), kScratchReg);
  return true;
}

void LiftoffAssembler::emit_i32_mul(Register dst, Register lhs, Register rhs) {
  MacroAssembler::Mul32(dst, lhs, rhs);
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

  MacroAssembler::Div32(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_divu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Divu32(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_rems(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Mod32(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_remu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Modu32(dst, lhs, rhs);
}

#define I32_BINOP(name, instruction)                                 \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register lhs, \
                                         Register rhs) {             \
    instruction(dst, lhs, rhs);                                      \
  }

// clang-format off
I32_BINOP(add, addw)
I32_BINOP(sub, subw)
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
I32_BINOP_I(add, Add32)
I32_BINOP_I(sub, Sub32)
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

I32_SHIFTOP(shl, sllw)
I32_SHIFTOP(sar, sraw)
I32_SHIFTOP(shr, srlw)

I32_SHIFTOP_I(shl, slliw)
I32_SHIFTOP_I(sar, sraiw)
I32_SHIFTOP_I(shr, srliw)

#undef I32_SHIFTOP
#undef I32_SHIFTOP_I

void LiftoffAssembler::emit_i64_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  MacroAssembler::Mul64(dst.gp(), lhs.gp(), rhs.gp());
}

void LiftoffAssembler::emit_i64_muli(LiftoffRegister dst, LiftoffRegister lhs,
                                     int32_t imm) {
  if (base::bits::IsPowerOfTwo(imm)) {
    emit_i64_shli(dst, lhs, base::bits::WhichPowerOfTwo(imm));
    return;
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, imm);
  Mul64(dst.gp(), lhs.gp(), scratch);
}

bool LiftoffAssembler::emit_i64_divs(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));

  // Check if lhs == MinInt64 and rhs == -1, since this case is unrepresentable.
  MacroAssembler::CompareI(kScratchReg, lhs.gp(),
                           Operand(std::numeric_limits<int64_t>::min()), ne);
  MacroAssembler::CompareI(kScratchReg2, rhs.gp(), Operand(-1), ne);
  add(kScratchReg, kScratchReg, kScratchReg2);
  MacroAssembler::Branch(trap_div_unrepresentable, eq, kScratchReg,
                         Operand(zero_reg));

  MacroAssembler::Div64(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_divu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Divu64(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_rems(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Mod64(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_remu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Modu64(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

#define I64_BINOP(name, instruction)                                   \
  void LiftoffAssembler::emit_i64_##name(                              \
      LiftoffRegister dst, LiftoffRegister lhs, LiftoffRegister rhs) { \
    instruction(dst.gp(), lhs.gp(), rhs.gp());                         \
  }

// clang-format off
I64_BINOP(add, add)
I64_BINOP(sub, sub)
I64_BINOP(and, and_)
I64_BINOP(or, or_)
I64_BINOP(xor, xor_)
// clang-format on

#undef I64_BINOP

#define I64_BINOP_I(name, instruction)                         \
  void LiftoffAssembler::emit_i64_##name##i(                   \
      LiftoffRegister dst, LiftoffRegister lhs, int32_t imm) { \
    instruction(dst.gp(), lhs.gp(), Operand(imm));             \
  }

// clang-format off
I64_BINOP_I(and, And)
I64_BINOP_I(or, Or)
I64_BINOP_I(xor, Xor)
// clang-format on

#undef I64_BINOP_I

#define I64_SHIFTOP(name, instruction)                             \
  void LiftoffAssembler::emit_i64_##name(                          \
      LiftoffRegister dst, LiftoffRegister src, Register amount) { \
    instruction(dst.gp(), src.gp(), amount);                       \
  }

I64_SHIFTOP(shl, sll)
I64_SHIFTOP(sar, sra)
I64_SHIFTOP(shr, srl)
#undef I64_SHIFTOP

void LiftoffAssembler::emit_i64_shli(LiftoffRegister dst, LiftoffRegister src,
                                     int amount) {
  if (is_uint6(amount)) {
    slli(dst.gp(), src.gp(), amount);
  } else {
    li(kScratchReg, amount);
    sll(dst.gp(), src.gp(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i64_sari(LiftoffRegister dst, LiftoffRegister src,
                                     int amount) {
  if (is_uint6(amount)) {
    srai(dst.gp(), src.gp(), amount);
  } else {
    li(kScratchReg, amount);
    sra(dst.gp(), src.gp(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i64_shri(LiftoffRegister dst, LiftoffRegister src,
                                     int amount) {
  if (is_uint6(amount)) {
    srli(dst.gp(), src.gp(), amount);
  } else {
    li(kScratchReg, amount);
    srl(dst.gp(), src.gp(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i64_addi(LiftoffRegister dst, LiftoffRegister lhs,
                                     int64_t imm) {
  MacroAssembler::Add64(dst.gp(), lhs.gp(), Operand(imm));
}
void LiftoffAssembler::emit_u32_to_uintptr(Register dst, Register src) {
  ZeroExtendWord(dst, src);
}

void LiftoffAssembler::clear_i32_upper_half(Register dst) {
  // Don't need to clear the upper halves of i32 values for sandbox on riscv64,
  // because we'll explicitly zero-extend their lower halves before using them
  // for memory accesses anyway.
}

#define FP_UNOP_RETURN_TRUE(name, instruction)                                 \
  bool LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister src) { \
    instruction(dst, src, kScratchDoubleReg);                                  \
    return true;                                                               \
  }

FP_UNOP_RETURN_TRUE(f64_ceil, Ceil_d_d)
FP_UNOP_RETURN_TRUE(f64_floor, Floor_d_d)
FP_UNOP_RETURN_TRUE(f64_trunc, Trunc_d_d)
FP_UNOP_RETURN_TRUE(f64_nearest_int, Round_d_d)

#undef FP_UNOP_RETURN_TRUE

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  switch (opcode) {
    case kExprI32ConvertI64:
      // According to WebAssembly spec, if I64 value does not fit the range of
      // I32, the value is undefined. Therefore, We use sign extension to
      // implement I64 to I32 truncation
      MacroAssembler::SignExtendWord(dst.gp(), src.gp());
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
        case kExprI64SConvertF32:
          Trunc_l_s(dst.gp(), src.fp(), kScratchReg);
          break;
        case kExprI64UConvertF32:
          Trunc_ul_s(dst.gp(), src.fp(), kScratchReg);
          break;
        case kExprI64SConvertF64:
          Trunc_l_d(dst.gp(), src.fp(), kScratchReg);
          break;
        case kExprI64UConvertF64:
          Trunc_ul_d(dst.gp(), src.fp(), kScratchReg);
          break;
        case kExprF32ConvertF64:
          fcvt_s_d(dst.fp(), src.fp());
          break;
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
      MacroAssembler::SignExtendWord(dst.gp(), src.gp());
      return true;
    case kExprI64UConvertI32:
      MacroAssembler::ZeroExtendWord(dst.gp(), src.gp());
      return true;
    case kExprI64ReinterpretF64:
      fmv_x_d(dst.gp(), src.fp());
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
      fmv_d_x(dst.fp(), src.gp());
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
    case kExprI64SConvertSatF32: {
      fcvt_l_s(dst.gp(), src.fp(), RTZ);
      Clear_if_nan_s(dst.gp(), src.fp());
      return true;
    }
    case kExprI64UConvertSatF32: {
      fcvt_lu_s(dst.gp(), src.fp(), RTZ);
      Clear_if_nan_s(dst.gp(), src.fp());
      return true;
    }
    case kExprI64SConvertSatF64: {
      fcvt_l_d(dst.gp(), src.fp(), RTZ);
      Clear_if_nan_d(dst.gp(), src.fp());
      return true;
    }
    case kExprI64UConvertSatF64: {
      fcvt_lu_d(dst.gp(), src.fp(), RTZ);
      Clear_if_nan_d(dst.gp(), src.fp());
      return true;
    }
    default:
      return false;
  }
}

void LiftoffAssembler::emit_i64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E64, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32_signextend_i8(Register dst, Register src) {
  slliw(dst, src, 32 - 8);
  sraiw(dst, dst, 32 - 8);
}

void LiftoffAssembler::emit_i32_signextend_i16(Register dst, Register src) {
  slliw(dst, src, 32 - 16);
  sraiw(dst, dst, 32 - 16);
}

void LiftoffAssembler::emit_i64_signextend_i8(LiftoffRegister dst,
                                              LiftoffRegister src) {
  slli(dst.gp(), src.gp(), 64 - 8);
  srai(dst.gp(), dst.gp(), 64 - 8);
}

void LiftoffAssembler::emit_i64_signextend_i16(LiftoffRegister dst,
                                               LiftoffRegister src) {
  slli(dst.gp(), src.gp(), 64 - 16);
  srai(dst.gp(), dst.gp(), 64 - 16);
}

void LiftoffAssembler::emit_i64_signextend_i32(LiftoffRegister dst,
                                               LiftoffRegister src) {
  slli(dst.gp(), src.gp(), 64 - 32);
  srai(dst.gp(), dst.gp(), 64 - 32);
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
    if (kind == kI32) {
      UseScratchRegisterScope temps(this);
      Register scratch0 = temps.Acquire();
      slliw(scratch0, lhs, 0);
      MacroAssembler::Branch(label, cond, scratch0, Operand(zero_reg));
    } else {
      DCHECK(kind == kI64);
      MacroAssembler::Branch(label, cond, lhs, Operand(zero_reg));
    }
  } else {
    if (kind == kI64) {
      MacroAssembler::Branch(label, cond, lhs, Operand(rhs));
    } else {
      DCHECK((kind == kI32) || (kind == kRtt) || (kind == kRef) ||
             (kind == kRefNull));
      MacroAssembler::CompareTaggedAndBranch(label, cond, lhs, Operand(rhs));
    }
  }
}

void LiftoffAssembler::emit_i32_cond_jumpi(Condition cond, Label* label,
                                           Register lhs, int32_t imm,
                                           const FreezeCacheState& frozen) {
  MacroAssembler::CompareTaggedAndBranch(label, cond, lhs, Operand(imm));
}

void LiftoffAssembler::emit_ptrsize_cond_jumpi(Condition cond, Label* label,
                                               Register lhs, int32_t imm,
                                               const FreezeCacheState& frozen) {
  MacroAssembler::Branch(label, cond, lhs, Operand(imm));
}

void LiftoffAssembler::emit_i32_eqz(Register dst, Register src) {
  MacroAssembler::slliw(dst, src, 0);
  MacroAssembler::Sltu(dst, src, 1);
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  UseScratchRegisterScope temps(this);
  Register scratch0 = temps.Acquire();
  Register scratch1 = kScratchReg;
  MacroAssembler::slliw(scratch0, lhs, 0);
  MacroAssembler::slliw(scratch1, rhs, 0);
  MacroAssembler::CompareI(dst, scratch0, Operand(scratch1), cond);
}

void LiftoffAssembler::emit_i64_eqz(Register dst, LiftoffRegister src) {
  MacroAssembler::Sltu(dst, src.gp(), 1);
}

void LiftoffAssembler::emit_i64_set_cond(Condition cond, Register dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  MacroAssembler::CompareI(dst, lhs.gp(), Operand(rhs.gp()), cond);
}

void LiftoffAssembler::IncrementSmi(LiftoffRegister dst, int offset) {
  UseScratchRegisterScope temps(this);
  if (COMPRESS_POINTERS_BOOL) {
    DCHECK(SmiValuesAre31Bits());
    Register scratch = temps.Acquire();
    Lw(scratch, MemOperand(dst.gp(), offset));
    Add32(scratch, scratch, Operand(Smi::FromInt(1)));
    Sw(scratch, MemOperand(dst.gp(), offset));
  } else {
    Register scratch = temps.Acquire();
    SmiUntag(scratch, MemOperand(dst.gp(), offset));
    Add64(scratch, scratch, Operand(1));
    SmiTag(scratch);
    Sd(scratch, MemOperand(dst.gp(), offset));
  }
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

  MachineType memtype = type.mem_type();
  if (transform == LoadTransformationKind::kExtend) {
    Ld(scratch, src_op);
    *protected_load_pc = pc_offset() - kInstrSize;
    if (memtype == MachineType::Int8()) {
      VU.set(kScratchReg, E64, m1);
      vmv_vx(kSimd128ScratchReg, scratch);
      VU.set(kScratchReg, E16, m1);
      vsext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Uint8()) {
      VU.set(kScratchReg, E64, m1);
      vmv_vx(kSimd128ScratchReg, scratch);
      VU.set(kScratchReg, E16, m1);
      vzext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Int16()) {
      VU.set(kScratchReg, E64, m1);
      vmv_vx(kSimd128ScratchReg, scratch);
      VU.set(kScratchReg, E32, m1);
      vsext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Uint16()) {
      VU.set(kScratchReg, E64, m1);
      vmv_vx(kSimd128ScratchReg, scratch);
      VU.set(kScratchReg, E32, m1);
      vzext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Int32()) {
      VU.set(kScratchReg, E64, m1);
      vmv_vx(kSimd128ScratchReg, scratch);
      vsext_vf2(dst_v, kSimd128ScratchReg);
    } else if (memtype == MachineType::Uint32()) {
      VU.set(kScratchReg, E64, m1);
      vmv_vx(kSimd128ScratchReg, scratch);
      vzext_vf2(dst_v, kSimd128ScratchReg);
    }
  } else if (transform == LoadTransformationKind::kZeroExtend) {
    vxor_vv(dst_v, dst_v, dst_v);
    if (memtype == MachineType::Int32()) {
      VU.set(kScratchReg, E32, m1);
      Lwu(scratch, src_op);
      *protected_load_pc = pc_offset() - kInstrSize;
      vmv_sx(dst_v, scratch);
    } else {
      DCHECK_EQ(MachineType::Int64(), memtype);
      VU.set(kScratchReg, E64, m1);
      Ld(scratch, src_op);
      *protected_load_pc = pc_offset() - kInstrSize;
      vmv_sx(dst_v, scratch);
    }
  } else {
    DCHECK_EQ(LoadTransformationKind::kSplat, transform);
    if (memtype == MachineType::Int8()) {
      VU.set(kScratchReg, E8, m1);
      Lb(scratch, src_op);
      *protected_load_pc = pc_offset() - kInstrSize;
      vmv_vx(dst_v, scratch);
    } else if (memtype == MachineType::Int16()) {
      VU.set(kScratchReg, E16, m1);
      Lh(scratch, src_op);
      *protected_load_pc = pc_offset() - kInstrSize;
      vmv_vx(dst_v, scratch);
    } else if (memtype == MachineType::Int32()) {
      VU.set(kScratchReg, E32, m1);
      Lw(scratch, src_op);
      *protected_load_pc = pc_offset() - kInstrSize;
      vmv_vx(dst_v, scratch);
    } else if (memtype == MachineType::Int64()) {
      VU.set(kScratchReg, E64, m1);
      Ld(scratch, src_op);
      *protected_load_pc = pc_offset() - kInstrSize;
      vmv_vx(dst_v, scratch);
    }
  }
}

void LiftoffAssembler::LoadLane(LiftoffRegister dst, LiftoffRegister src,
                                Register addr, Register offset_reg,
                                uintptr_t offset_imm, LoadType type,
                                uint8_t laneidx, uint32_t* protected_load_pc,
                                bool i64_offset) {
  MemOperand src_op =
      liftoff::GetMemOp(this, addr, offset_reg, offset_imm, i64_offset);
  MachineType mem_type = type.mem_type();
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  if (mem_type == MachineType::Int8()) {
    Lbu(scratch, src_op);
    *protected_load_pc = pc_offset() - kInstrSize;
    VU.set(kScratchReg, E64, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    VU.set(kScratchReg, E8, m1);
    vmerge_vx(dst.fp().toV(), scratch, dst.fp().toV());
  } else if (mem_type == MachineType::Int16()) {
    Lhu(scratch, src_op);
    *protected_load_pc = pc_offset() - kInstrSize;
    VU.set(kScratchReg, E16, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vmerge_vx(dst.fp().toV(), scratch, dst.fp().toV());
  } else if (mem_type == MachineType::Int32()) {
    Lwu(scratch, src_op);
    *protected_load_pc = pc_offset() - kInstrSize;
    VU.set(kScratchReg, E32, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vmerge_vx(dst.fp().toV(), scratch, dst.fp().toV());
  } else if (mem_type == MachineType::Int64()) {
    Ld(scratch, src_op);
    *protected_load_pc = pc_offset() - kInstrSize;
    VU.set(kScratchReg, E64, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vmerge_vx(dst.fp().toV(), scratch, dst.fp().toV());
  } else {
    UNREACHABLE();
  }
}

void LiftoffAssembler::StoreLane(Register dst, Register offset,
                                 uintptr_t offset_imm, LiftoffRegister src,
                                 StoreType type, uint8_t lane,
                                 uint32_t* protected_store_pc,
                                 bool i64_offset) {
  MemOperand dst_op =
      liftoff::GetMemOp(this, dst, offset, offset_imm, i64_offset);
  MachineRepresentation rep = type.mem_rep();
  if (rep == MachineRepresentation::kWord8) {
    VU.set(kScratchReg, E8, m1);
    vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), lane);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    if (protected_store_pc) *protected_store_pc = pc_offset();
    Sb(kScratchReg, dst_op);
  } else if (rep == MachineRepresentation::kWord16) {
    VU.set(kScratchReg, E16, m1);
    vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), lane);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    if (protected_store_pc) *protected_store_pc = pc_offset();
    Sh(kScratchReg, dst_op);
  } else if (rep == MachineRepresentation::kWord32) {
    VU.set(kScratchReg, E32, m1);
    vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), lane);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    if (protected_store_pc) *protected_store_pc = pc_offset();
    Sw(kScratchReg, dst_op);
  } else {
    DCHECK_EQ(MachineRepresentation::kWord64, rep);
    VU.set(kScratchReg, E64, m1);
    vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), lane);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    if (protected_store_pc) *protected_store_pc = pc_offset();
    Sd(kScratchReg, dst_op);
  }
}

void LiftoffAssembler::emit_i64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vx(dst.fp().toV(), src.gp());
}

void LiftoffAssembler::emit_i64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E64, m1);
  li(kScratchReg, 0x1 << imm_lane_idx);
  vmv_sx(v0, kScratchReg);
  vmerge_vx(dst.fp().toV(), src2.gp(), src1.fp().toV());
}

void LiftoffAssembler::emit_f64x2_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  const int64_t kNaN = 0x7ff8000000000000L;
  vmfeq_vv(v0, lhs.fp().toV(), lhs.fp().toV());
  vmfeq_vv(kSimd128ScratchReg, rhs.fp().toV(), rhs.fp().toV());
  vand_vv(v0, v0, kSimd128ScratchReg);
  li(kScratchReg, kNaN);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  vfmin_vv(kSimd128ScratchReg, rhs.fp().toV(), lhs.fp().toV(), Mask);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_f64x2_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  const int64_t kNaN = 0x7ff8000000000000L;
  vmfeq_vv(v0, lhs.fp().toV(), lhs.fp().toV());
  vmfeq_vv(kSimd128ScratchReg, rhs.fp().toV(), rhs.fp().toV());
  vand_vv(v0, v0, kSimd128ScratchReg);
  li(kScratchReg, kNaN);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  vfmax_vv(kSimd128ScratchReg, rhs.fp().toV(), lhs.fp().toV(), Mask);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_extadd_pairwise_i16x8_s(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vi(kSimd128ScratchReg, -1);
  vmv_vi(kSimd128ScratchReg3, -1);
  li(kScratchReg, 0x0006000400020000);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 0x0007000500030001);
  vmv_sx(kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg, E16, m1);
  vrgather_vv(kSimd128ScratchReg2, src.fp().toV(), kSimd128ScratchReg);
  vrgather_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg3);
  VU.set(kScratchReg, E16, mf2);
  vwadd_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i32x4_extadd_pairwise_i16x8_u(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vi(kSimd128ScratchReg, -1);
  vmv_vi(kSimd128ScratchReg3, -1);
  li(kScratchReg, 0x0006000400020000);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 0x0007000500030001);
  vmv_sx(kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg, E16, m1);
  vrgather_vv(kSimd128ScratchReg2, src.fp().toV(), kSimd128ScratchReg);
  vrgather_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg3);
  VU.set(kScratchReg, E16, mf2);
  vwaddu_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i16x8_extadd_pairwise_i8x16_s(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vi(kSimd128ScratchReg, -1);
  vmv_vi(kSimd128ScratchReg3, -1);
  li(kScratchReg, 0x0E0C0A0806040200);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 0x0F0D0B0907050301);
  vmv_sx(kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg, E8, m1);
  vrgather_vv(kSimd128ScratchReg2, src.fp().toV(), kSimd128ScratchReg);
  vrgather_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg3);
  VU.set(kScratchReg, E8, mf2);
  vwadd_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i16x8_extadd_pairwise_i8x16_u(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vi(kSimd128ScratchReg, -1);
  vmv_vi(kSimd128ScratchReg3, -1);
  li(kScratchReg, 0x0E0C0A0806040200);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 0x0F0D0B0907050301);
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
    liftoff::StoreToMemory(this, MemOperand{sp, arg_offset}, arg);
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
    liftoff::Load(this, *next_result_reg, MemOperand(sp, 0), out_argument_kind);
  }

  AddWord(sp, sp, Operand(stack_bytes));
}

void LiftoffAssembler::CallC(const std::initializer_list<VarState> args_list,
                             ExternalReference ext_ref) {
  const int num_args = static_cast<int>(args_list.size());
  const VarState* const args = args_list.begin();
  // Note: If we ever need more than eight arguments we would need to load the
  // stack arguments to registers (via LoadToRegister) in pairs of two, then use
  // Stp with MemOperand{sp, -2 * kSystemPointerSize, PreIndex} to push them to
  // the stack.
  // Execute the parallel register move for register parameters.
  DCHECK_GE(arraysize(kCArgRegs), num_args);
  ParallelMove parallel_move{this};
  for (int reg_arg = 0; reg_arg < num_args; ++reg_arg) {
    parallel_move.LoadIntoRegister(LiftoffRegister{kCArgRegs[reg_arg]},
                                   args[reg_arg]);
  }
  parallel_move.Execute();
  // Now call the C function.
  PrepareCallCFunction(num_args, kScratchReg);
  CallCFunction(ext_ref, num_args);
}

void LiftoffStackSlots::Construct(int param_slots) {
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
      case LiftoffAssembler::VarState::kStack:
        if (src.kind() != kS128) {
          asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
          asm_->Ld(kScratchReg, liftoff::GetStackSlot(slot.src_offset_));
          asm_->push(kScratchReg);
        } else {
          asm_->AllocateStackSpace(stack_decrement - kSimd128Size);
          asm_->Ld(kScratchReg, liftoff::GetStackSlot(slot.src_offset_ - 8));
          asm_->push(kScratchReg);
          asm_->Ld(kScratchReg, liftoff::GetStackSlot(slot.src_offset_));
          asm_->push(kScratchReg);
        }
        break;
      case LiftoffAssembler::VarState::kRegister: {
        int pushed_bytes = SlotSizeInBytes(slot);
        asm_->AllocateStackSpace(stack_decrement - pushed_bytes);
        liftoff::push(asm_, src.reg(), src.kind());
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

#endif  // V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV64_INL_H_
