// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_INL_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_INL_H_

#include "src/wasm/baseline/liftoff-assembler.h"

// Include platform specific implementation.
#if V8_TARGET_ARCH_IA32
#include "src/wasm/baseline/ia32/liftoff-assembler-ia32-inl.h"
#elif V8_TARGET_ARCH_X64
#include "src/wasm/baseline/x64/liftoff-assembler-x64-inl.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/wasm/baseline/arm64/liftoff-assembler-arm64-inl.h"
#elif V8_TARGET_ARCH_ARM
#include "src/wasm/baseline/arm/liftoff-assembler-arm-inl.h"
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
#include "src/wasm/baseline/ppc/liftoff-assembler-ppc-inl.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/wasm/baseline/mips64/liftoff-assembler-mips64-inl.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/wasm/baseline/loong64/liftoff-assembler-loong64-inl.h"
#elif V8_TARGET_ARCH_S390
#include "src/wasm/baseline/s390/liftoff-assembler-s390-inl.h"
#elif V8_TARGET_ARCH_RISCV64
#include "src/wasm/baseline/riscv/liftoff-assembler-riscv64-inl.h"
#elif V8_TARGET_ARCH_RISCV32
#include "src/wasm/baseline/riscv/liftoff-assembler-riscv32-inl.h"
#else
#error Unsupported architecture.
#endif

namespace v8::internal::wasm {

// static
int LiftoffAssembler::NextSpillOffset(ValueKind kind, int top_spill_offset) {
  int offset = top_spill_offset + SlotSizeForType(kind);
  if (NeedsAlignment(kind)) {
    offset = RoundUp(offset, SlotSizeForType(kind));
  }
  return offset;
}

int LiftoffAssembler::NextSpillOffset(ValueKind kind) {
  return NextSpillOffset(kind, TopSpillOffset());
}

int LiftoffAssembler::TopSpillOffset() const {
  return cache_state_.stack_state.empty()
             ? StaticStackFrameSize()
             : cache_state_.stack_state.back().offset();
}

void LiftoffAssembler::PushRegister(ValueKind kind, LiftoffRegister reg) {
  DCHECK_EQ(reg_class_for(kind), reg.reg_class());
  cache_state_.inc_used(reg);
  cache_state_.stack_state.emplace_back(kind, reg, NextSpillOffset(kind));
}

// Assumes that the exception is in {kReturnRegister0}. This is where the
// exception is stored by the unwinder after a throwing call.
void LiftoffAssembler::PushException() {
  LiftoffRegister reg{kReturnRegister0};
  // This is used after a call, so {kReturnRegister0} is not used yet.
  DCHECK(cache_state_.is_free(reg));
  cache_state_.inc_used(reg);
  cache_state_.stack_state.emplace_back(kRef, reg, NextSpillOffset(kRef));
}

void LiftoffAssembler::PushConstant(ValueKind kind, int32_t i32_const) {
  V8_ASSUME(kind == kI32 || kind == kI64);
  cache_state_.stack_state.emplace_back(kind, i32_const, NextSpillOffset(kind));
}

void LiftoffAssembler::PushStack(ValueKind kind) {
  cache_state_.stack_state.emplace_back(kind, NextSpillOffset(kind));
}

void LiftoffAssembler::LoadToFixedRegister(VarState slot, LiftoffRegister reg) {
  DCHECK(slot.is_const() || slot.is_stack());
  if (slot.is_const()) {
    LoadConstant(reg, slot.constant());
  } else {
    Fill(reg, slot.offset(), slot.kind());
  }
}

void LiftoffAssembler::PopToFixedRegister(LiftoffRegister reg) {
  DCHECK(!cache_state_.stack_state.empty());
  VarState slot = cache_state_.stack_state.back();
  cache_state_.stack_state.pop_back();
  if (V8_LIKELY(slot.is_reg())) {
    cache_state_.dec_used(slot.reg());
    if (slot.reg() == reg) return;
    if (cache_state_.is_used(reg)) SpillRegister(reg);
    Move(reg, slot.reg(), slot.kind());
    return;
  }
  if (cache_state_.is_used(reg)) SpillRegister(reg);
  LoadToFixedRegister(slot, reg);
}

void LiftoffAssembler::LoadFixedArrayLengthAsInt32(LiftoffRegister dst,
                                                   Register array,
                                                   LiftoffRegList pinned) {
  int offset = FixedArray::kLengthOffset - kHeapObjectTag;
  LoadSmiAsInt32(dst, array, offset);
}

void LiftoffAssembler::LoadSmiAsInt32(LiftoffRegister dst, Register src_addr,
                                      int32_t offset) {
  if constexpr (SmiValuesAre32Bits()) {
#if V8_TARGET_LITTLE_ENDIAN
    DCHECK_EQ(kSmiShiftSize + kSmiTagSize, 4 * kBitsPerByte);
    offset += 4;
#endif
    Load(dst, src_addr, no_reg, offset, LoadType::kI32Load);
  } else {
    DCHECK(SmiValuesAre31Bits());
    Load(dst, src_addr, no_reg, offset, LoadType::kI32Load);
    emit_i32_sari(dst.gp(), dst.gp(), kSmiTagSize);
  }
}

void LiftoffAssembler::emit_ptrsize_add(Register dst, Register lhs,
                                        Register rhs) {
  if constexpr (kSystemPointerSize == 8) {
    emit_i64_add(LiftoffRegister(dst), LiftoffRegister(lhs),
                 LiftoffRegister(rhs));
  } else {
    emit_i32_add(dst, lhs, rhs);
  }
}

void LiftoffAssembler::emit_ptrsize_sub(Register dst, Register lhs,
                                        Register rhs) {
  if constexpr (kSystemPointerSize == 8) {
    emit_i64_sub(LiftoffRegister(dst), LiftoffRegister(lhs),
                 LiftoffRegister(rhs));
  } else {
    emit_i32_sub(dst, lhs, rhs);
  }
}

void LiftoffAssembler::emit_ptrsize_and(Register dst, Register lhs,
                                        Register rhs) {
  if constexpr (kSystemPointerSize == 8) {
    emit_i64_and(LiftoffRegister(dst), LiftoffRegister(lhs),
                 LiftoffRegister(rhs));
  } else {
    emit_i32_and(dst, lhs, rhs);
  }
}

void LiftoffAssembler::emit_ptrsize_shri(Register dst, Register src,
                                         int amount) {
  if constexpr (kSystemPointerSize == 8) {
    emit_i64_shri(LiftoffRegister(dst), LiftoffRegister(src), amount);
  } else {
    emit_i32_shri(dst, src, amount);
  }
}

void LiftoffAssembler::emit_ptrsize_addi(Register dst, Register lhs,
                                         intptr_t imm) {
  if constexpr (kSystemPointerSize == 8) {
    emit_i64_addi(LiftoffRegister(dst), LiftoffRegister(lhs), imm);
  } else {
    emit_i32_addi(dst, lhs, static_cast<int32_t>(imm));
  }
}

void LiftoffAssembler::emit_ptrsize_muli(Register dst, Register lhs,
                                         int32_t imm) {
  if constexpr (kSystemPointerSize == 8) {
    emit_i64_muli(LiftoffRegister(dst), LiftoffRegister(lhs), imm);
  } else {
    emit_i32_muli(dst, lhs, imm);
  }
}

void LiftoffAssembler::emit_ptrsize_set_cond(Condition condition, Register dst,
                                             LiftoffRegister lhs,
                                             LiftoffRegister rhs) {
  if constexpr (kSystemPointerSize == 8) {
    emit_i64_set_cond(condition, dst, lhs, rhs);
  } else {
    emit_i32_set_cond(condition, dst, lhs.gp(), rhs.gp());
  }
}

void LiftoffAssembler::bailout(LiftoffBailoutReason reason,
                               const char* detail) {
  DCHECK_NE(kSuccess, reason);
  if (bailout_reason_ != kSuccess) return;
  AbortCompilation();
  bailout_reason_ = reason;
  bailout_detail_ = detail;
}

// =======================================================================
// Partially platform-independent implementations of the platform-dependent
// part.

#ifdef V8_TARGET_ARCH_32_BIT

void LiftoffAssembler::emit_ptrsize_cond_jumpi(Condition cond, Label* label,
                                               Register lhs, int32_t imm,
                                               const FreezeCacheState& frozen) {
  emit_i32_cond_jumpi(cond, label, lhs, imm, frozen);
}

namespace liftoff {
template <void (LiftoffAssembler::*op)(Register, Register, Register)>
void EmitI64IndependentHalfOperation(LiftoffAssembler* assm,
                                     LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  // If {dst.low_gp()} does not overlap with {lhs.high_gp()} or {rhs.high_gp()},
  // just first compute the lower half, then the upper half.
  if (dst.low() != lhs.high() && dst.low() != rhs.high()) {
    (assm->*op)(dst.low_gp(), lhs.low_gp(), rhs.low_gp());
    (assm->*op)(dst.high_gp(), lhs.high_gp(), rhs.high_gp());
    return;
  }
  // If {dst.high_gp()} does not overlap with {lhs.low_gp()} or {rhs.low_gp()},
  // we can compute this the other way around.
  if (dst.high() != lhs.low() && dst.high() != rhs.low()) {
    (assm->*op)(dst.high_gp(), lhs.high_gp(), rhs.high_gp());
    (assm->*op)(dst.low_gp(), lhs.low_gp(), rhs.low_gp());
    return;
  }
  // Otherwise, we need a temporary register.
  Register tmp = assm->GetUnusedRegister(kGpReg, LiftoffRegList{lhs, rhs}).gp();
  (assm->*op)(tmp, lhs.low_gp(), rhs.low_gp());
  (assm->*op)(dst.high_gp(), lhs.high_gp(), rhs.high_gp());
  assm->Move(dst.low_gp(), tmp, kI32);
}

template <void (LiftoffAssembler::*op)(Register, Register, int32_t)>
void EmitI64IndependentHalfOperationImm(LiftoffAssembler* assm,
                                        LiftoffRegister dst,
                                        LiftoffRegister lhs, int64_t imm) {
  int32_t low_word = static_cast<int32_t>(imm);
  int32_t high_word = static_cast<int32_t>(imm >> 32);
  // If {dst.low_gp()} does not overlap with {lhs.high_gp()},
  // just first compute the lower half, then the upper half.
  if (dst.low() != lhs.high()) {
    (assm->*op)(dst.low_gp(), lhs.low_gp(), low_word);
    (assm->*op)(dst.high_gp(), lhs.high_gp(), high_word);
    return;
  }
  // If {dst.high_gp()} does not overlap with {lhs.low_gp()},
  // we can compute this the other way around.
  if (dst.high() != lhs.low()) {
    (assm->*op)(dst.high_gp(), lhs.high_gp(), high_word);
    (assm->*op)(dst.low_gp(), lhs.low_gp(), low_word);
    return;
  }
  // Otherwise, we need a temporary register.
  Register tmp = assm->GetUnusedRegister(kGpReg, LiftoffRegList{lhs}).gp();
  (assm->*op)(tmp, lhs.low_gp(), low_word);
  (assm->*op)(dst.high_gp(), lhs.high_gp(), high_word);
  assm->Move(dst.low_gp(), tmp, kI32);
}
}  // namespace liftoff

void LiftoffAssembler::emit_i64_and(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  liftoff::EmitI64IndependentHalfOperation<&LiftoffAssembler::emit_i32_and>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i64_andi(LiftoffRegister dst, LiftoffRegister lhs,
                                     int32_t imm) {
  liftoff::EmitI64IndependentHalfOperationImm<&LiftoffAssembler::emit_i32_andi>(
      this, dst, lhs, imm);
}

void LiftoffAssembler::emit_i64_or(LiftoffRegister dst, LiftoffRegister lhs,
                                   LiftoffRegister rhs) {
  liftoff::EmitI64IndependentHalfOperation<&LiftoffAssembler::emit_i32_or>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i64_ori(LiftoffRegister dst, LiftoffRegister lhs,
                                    int32_t imm) {
  liftoff::EmitI64IndependentHalfOperationImm<&LiftoffAssembler::emit_i32_ori>(
      this, dst, lhs, imm);
}

void LiftoffAssembler::emit_i64_xor(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  liftoff::EmitI64IndependentHalfOperation<&LiftoffAssembler::emit_i32_xor>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i64_xori(LiftoffRegister dst, LiftoffRegister lhs,
                                     int32_t imm) {
  liftoff::EmitI64IndependentHalfOperationImm<&LiftoffAssembler::emit_i32_xori>(
      this, dst, lhs, imm);
}

void LiftoffAssembler::emit_u32_to_uintptr(Register dst, Register src) {
  if (dst != src) Move(dst, src, kI32);
}

void LiftoffAssembler::clear_i32_upper_half(Register dst) { UNREACHABLE(); }

#endif  // V8_TARGET_ARCH_32_BIT

// End of the partially platform-independent implementations of the
// platform-dependent part.
// =======================================================================

}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_INL_H_
