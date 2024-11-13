// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV_INL_H_
#define V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV_INL_H_

#include "src/heap/mutable-page-metadata.h"
#include "src/wasm/baseline/liftoff-assembler.h"
#include "src/wasm/baseline/parallel-move-inl.h"
#include "src/wasm/object-access.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"

namespace v8::internal::wasm {

namespace liftoff {

inline MemOperand GetStackSlot(int offset) { return MemOperand(fp, -offset); }

inline MemOperand GetInstanceDataOperand() {
  return GetStackSlot(WasmLiftoffFrameConstants::kInstanceDataOffset);
}

}  // namespace liftoff
int LiftoffAssembler::PrepareStackFrame() {
  int offset = pc_offset();
  // When the frame size is bigger than 4KB, we need two instructions for
  // stack checking, so we reserve space for this case.
  addi(sp, sp, 0);
  nop();
  nop();
  return offset;
}

void LiftoffAssembler::PrepareTailCall(int num_callee_stack_params,
                                       int stack_param_delta) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Push the return address and frame pointer to complete the stack frame.
  LoadWord(scratch, MemOperand(fp, kSystemPointerSize));
  Push(scratch);
  LoadWord(scratch, MemOperand(fp, 0));
  Push(scratch);

  // Shift the whole frame upwards.
  int slot_count = num_callee_stack_params + 2;
  for (int i = slot_count - 1; i >= 0; --i) {
    LoadWord(scratch, MemOperand(sp, i * kSystemPointerSize));
    StoreWord(scratch,
              MemOperand(fp, (i - stack_param_delta) * kSystemPointerSize));
  }

  // Set the new stack and frame pointer.
  AddWord(sp, fp, -stack_param_delta * kSystemPointerSize);
  Pop(ra, fp);
}

void LiftoffAssembler::AlignFrameSize() {}

void LiftoffAssembler::CheckTierUp(int declared_func_index, int budget_used,
                                   Label* ool_label,
                                   const FreezeCacheState& frozen) {
  UseScratchRegisterScope temps(this);
  Register budget_array = temps.Acquire();
  Register instance_data = cache_state_.cached_instance_data;
  if (instance_data == no_reg) {
    instance_data = budget_array;  // Reuse the scratch register.
    LoadInstanceDataFromFrame(instance_data);
  }

  constexpr int kArrayOffset = wasm::ObjectAccess::ToTagged(
      WasmTrustedInstanceData::kTieringBudgetArrayOffset);
  LoadWord(budget_array, MemOperand(instance_data, kArrayOffset));

  int budget_arr_offset = kInt32Size * declared_func_index;
  // Pick a random register from kLiftoffAssemblerGpCacheRegs.
  // TODO(miladfarca): Use ScratchRegisterScope when available.
  Register budget = kScratchReg;
  MemOperand budget_addr(budget_array, budget_arr_offset);
  Lw(budget, budget_addr);
  Sub32(budget, budget, Operand{budget_used});
  Sw(budget, budget_addr);
  Branch(ool_label, lt, budget, Operand{0});
}

void LiftoffAssembler::PatchPrepareStackFrame(
    int offset, SafepointTableBuilder* safepoint_table_builder,
    bool feedback_vector_slot) {
  // The frame_size includes the frame marker and the instance slot. Both are
  // pushed as part of frame construction, so we don't need to allocate memory
  // for them anymore.
  int frame_size = GetTotalFrameSize() - 2 * kSystemPointerSize;
  // The frame setup builtin also pushes the feedback vector.
  if (feedback_vector_slot) {
    frame_size -= kSystemPointerSize;
  }
  // We can't run out of space, just pass anything big enough to not cause the
  // assembler to try to grow the buffer.
  constexpr int kAvailableSpace = 256;
  MacroAssembler patching_assembler(
      nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
      ExternalAssemblerBuffer(buffer_start_ + offset, kAvailableSpace));

  if (V8_LIKELY(frame_size < 4 * KB)) {
    // This is the standard case for small frames: just subtract from SP and be
    // done with it.
    patching_assembler.AddWord(sp, sp, Operand(-frame_size));
    return;
  }

  // The frame size is bigger than 4KB, so we might overflow the available stack
  // space if we first allocate the frame and then do the stack check (we will
  // need some remaining stack space for throwing the exception). That's why we
  // check the available stack space before we allocate the frame. To do this we
  // replace the {__ AddWord(sp, sp, -frame_size)} with a jump to OOL code that
  // does this "extended stack check".
  //
  // The OOL code can simply be generated here with the normal assembler,
  // because all other code generation, including OOL code, has already finished
  // when {PatchPrepareStackFrame} is called. The function prologue then jumps
  // to the current {pc_offset()} to execute the OOL code for allocating the
  // large frame.
  // Emit the unconditional branch in the function prologue (from {offset} to
  // {pc_offset()}).

  int imm32 = pc_offset() - offset;
  patching_assembler.GenPCRelativeJump(kScratchReg, imm32);

  // If the frame is bigger than the stack, we throw the stack overflow
  // exception unconditionally. Thereby we can avoid the integer overflow
  // check in the condition code.
  RecordComment("OOL: stack check for large frame");
  Label continuation;
  if (frame_size < v8_flags.stack_size * 1024) {
    Register stack_limit = kScratchReg;
    LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit);
    AddWord(stack_limit, stack_limit, Operand(frame_size));
    Branch(&continuation, uge, sp, Operand(stack_limit));
  }

  Call(static_cast<Address>(Builtin::kWasmStackOverflow),
       RelocInfo::WASM_STUB_CALL);
  // The call will not return; just define an empty safepoint.
  safepoint_table_builder->DefineSafepoint(this);
  if (v8_flags.debug_code) stop();

  bind(&continuation);

  // Now allocate the stack space. Note that this might do more than just
  // decrementing the SP;
  AddWord(sp, sp, Operand(-frame_size));

  // Jump back to the start of the function, from {pc_offset()} to
  // right after the reserved space for the {__ AddWord(sp, sp, -framesize)}
  // (which is a Branch now).
  int func_start_offset = offset + 2 * kInstrSize;
  imm32 = func_start_offset - pc_offset();
  GenPCRelativeJump(kScratchReg, imm32);
}

void LiftoffAssembler::LoadSpillAddress(Register dst, int offset,
                                        ValueKind /* kind */) {
  SubWord(dst, fp, offset);
}

void LiftoffAssembler::FinishCode() { ForceConstantPoolEmissionWithoutJump(); }

void LiftoffAssembler::AbortCompilation() { AbortedCodeGeneration(); }

// static
constexpr int LiftoffAssembler::StaticStackFrameSize() {
  return WasmLiftoffFrameConstants::kFeedbackVectorOffset;
}

int LiftoffAssembler::SlotSizeForType(ValueKind kind) {
  switch (kind) {
    case kS128:
      return value_kind_size(kind);
    default:
      return kStackSlotSize;
  }
}

bool LiftoffAssembler::NeedsAlignment(ValueKind kind) {
  switch (kind) {
    case kS128:
      return true;
    default:
      // No alignment because all other types are kStackSlotSize.
      return false;
  }
}

void LiftoffAssembler::LoadInstanceDataFromFrame(Register dst) {
  LoadWord(dst, liftoff::GetInstanceDataOperand());
}

void LiftoffAssembler::LoadTrustedPointer(Register dst, Register src_addr,
                                          int offset, IndirectPointerTag tag) {
  MemOperand src{src_addr, offset};
  LoadTrustedPointerField(dst, src, tag);
}

void LiftoffAssembler::LoadFromInstance(Register dst, Register instance,
                                        int offset, int size) {
  DCHECK_LE(0, offset);
  MemOperand src{instance, offset};
  switch (size) {
    case 1:
      Lb(dst, MemOperand(src));
      break;
    case 4:
      Lw(dst, MemOperand(src));
      break;
    case 8:
      LoadWord(dst, MemOperand(src));
      break;
    default:
      UNIMPLEMENTED();
  }
}

void LiftoffAssembler::LoadTaggedPointerFromInstance(Register dst,
                                                     Register instance,
                                                     int offset) {
  DCHECK_LE(0, offset);
  LoadTaggedField(dst, MemOperand{instance, offset});
}


void LiftoffAssembler::SpillInstanceData(Register instance) {
  StoreWord(instance, liftoff::GetInstanceDataOperand());
}

void LiftoffAssembler::ResetOSRTarget() {}

void LiftoffAssembler::emit_f32_neg(DoubleRegister dst, DoubleRegister src) {
  MacroAssembler::Neg_s(dst, src);
}

void LiftoffAssembler::emit_f64_neg(DoubleRegister dst, DoubleRegister src) {
  MacroAssembler::Neg_d(dst, src);
}

void LiftoffAssembler::emit_f32_min(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  MacroAssembler::Float32Min(dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32_max(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  MacroAssembler::Float32Max(dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32_copysign(DoubleRegister dst, DoubleRegister lhs,
                                         DoubleRegister rhs) {
  fsgnj_s(dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64_min(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  MacroAssembler::Float64Min(dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64_max(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  MacroAssembler::Float64Max(dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64_copysign(DoubleRegister dst, DoubleRegister lhs,
                                         DoubleRegister rhs) {
  fsgnj_d(dst, lhs, rhs);
}

#define FP_BINOP(name, instruction)                                          \
  void LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister lhs, \
                                     DoubleRegister rhs) {                   \
    instruction(dst, lhs, rhs);                                              \
  }
#define FP_UNOP(name, instruction)                                             \
  void LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister src) { \
    instruction(dst, src);                                                     \
  }
#define FP_UNOP_RETURN_TRUE(name, instruction)                                 \
  bool LiftoffAssembler::emit_##name(DoubleRegister dst, DoubleRegister src) { \
    instruction(dst, src, kScratchDoubleReg);                                  \
    return true;                                                               \
  }

FP_BINOP(f32_add, fadd_s)
FP_BINOP(f32_sub, fsub_s)
FP_BINOP(f32_mul, fmul_s)
FP_BINOP(f32_div, fdiv_s)
FP_UNOP(f32_abs, fabs_s)
FP_UNOP_RETURN_TRUE(f32_ceil, Ceil_s_s)
FP_UNOP_RETURN_TRUE(f32_floor, Floor_s_s)
FP_UNOP_RETURN_TRUE(f32_trunc, Trunc_s_s)
FP_UNOP_RETURN_TRUE(f32_nearest_int, Round_s_s)
FP_UNOP(f32_sqrt, fsqrt_s)
FP_BINOP(f64_add, fadd_d)
FP_BINOP(f64_sub, fsub_d)
FP_BINOP(f64_mul, fmul_d)
FP_BINOP(f64_div, fdiv_d)
FP_UNOP(f64_abs, fabs_d)
FP_UNOP(f64_sqrt, fsqrt_d)
#undef FP_BINOP
#undef FP_UNOP
#undef FP_UNOP_RETURN_TRUE

static FPUCondition ConditionToConditionCmpFPU(Condition condition) {
  switch (condition) {
    case kEqual:
      return EQ;
    case kNotEqual:
      return NE;
    case kUnsignedLessThan:
      return LT;
    case kUnsignedGreaterThanEqual:
      return GE;
    case kUnsignedLessThanEqual:
      return LE;
    case kUnsignedGreaterThan:
      return GT;
    default:
      break;
  }
  UNREACHABLE();
}

void LiftoffAssembler::emit_f32_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  FPUCondition fcond = ConditionToConditionCmpFPU(cond);
  MacroAssembler::CompareF32(dst, fcond, lhs, rhs);
}

void LiftoffAssembler::emit_f64_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  FPUCondition fcond = ConditionToConditionCmpFPU(cond);
  MacroAssembler::CompareF64(dst, fcond, lhs, rhs);
}

bool LiftoffAssembler::emit_select(LiftoffRegister dst, Register condition,
                                   LiftoffRegister true_value,
                                   LiftoffRegister false_value,
                                   ValueKind kind) {
  return false;
}

void LiftoffAssembler::emit_smi_check(Register obj, Label* target,
                                      SmiCheckMode mode,
                                      const FreezeCacheState& frozen) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  And(scratch, obj, Operand(kSmiTagMask));
  Condition condition = mode == kJumpOnSmi ? eq : ne;
  Branch(target, condition, scratch, Operand(zero_reg));
}

// Implemente vector popcnt refer dense_popcnt
//  int dense_popcnt(uint32_t n)
//  {
//      int count = 32;  // sizeof(uint32_t) * CHAR_BIT;
//      n ^= 0xFF'FF'FF'FF;
//      while(n)
//      {
//          --count;
//          n &= n - 1;
//      }
//      return count;
//  }
void LiftoffAssembler::emit_i8x16_popcnt(LiftoffRegister dst,
                                         LiftoffRegister src) {
  VRegister src_v = src.fp().toV();
  VRegister dst_v = dst.fp().toV();
  Label t, done;
  VU.set(kScratchReg, E8, m1);
  vmv_vv(kSimd128ScratchReg, src_v);
  li(kScratchReg, 0xFF);
  vxor_vx(kSimd128ScratchReg, kSimd128ScratchReg, kScratchReg);
  vmv_vi(dst_v, 8);
  vmv_vi(kSimd128RegZero, 0);
  bind(&t);
  vmsne_vi(v0, kSimd128ScratchReg, 0);
  VU.set(kScratchReg, E16, m1);
  vmv_xs(kScratchReg, v0);
  beqz(kScratchReg, &done);
  VU.set(kScratchReg, E8, m1);
  vadd_vi(dst_v, dst_v, -1, MaskType::Mask);
  vadd_vi(kSimd128ScratchReg2, kSimd128ScratchReg, -1, MaskType::Mask);
  vand_vv(kSimd128ScratchReg, kSimd128ScratchReg2, kSimd128ScratchReg,
          MaskType::Mask);
  Branch(&t);
  bind(&done);
}

void LiftoffAssembler::emit_i8x16_shuffle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs,
                                          const uint8_t shuffle[16],
                                          bool is_swizzle) {
  VRegister dst_v = dst.fp().toV();
  VRegister lhs_v = lhs.fp().toV();
  VRegister rhs_v = rhs.fp().toV();

  WasmRvvS128const(kSimd128ScratchReg2, shuffle);

  VU.set(kScratchReg, E8, m1);
  VRegister temp =
      GetUnusedRegister(kFpReg, LiftoffRegList{lhs, rhs}).fp().toV();
  if (dst_v == lhs_v) {
    vmv_vv(temp, lhs_v);
    lhs_v = temp;
  } else if (dst_v == rhs_v) {
    vmv_vv(temp, rhs_v);
    rhs_v = temp;
  }
  vrgather_vv(dst_v, lhs_v, kSimd128ScratchReg2);
  vadd_vi(kSimd128ScratchReg2, kSimd128ScratchReg2,
          -16);  // The indices in range [16, 31] select the i - 16-th element
                 // of rhs
  vrgather_vv(kSimd128ScratchReg, rhs_v, kSimd128ScratchReg2);
  vor_vv(dst_v, dst_v, kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_swizzle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  if (dst == lhs) {
    vrgather_vv(kSimd128ScratchReg, lhs.fp().toV(), rhs.fp().toV());
    vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
  } else {
    vrgather_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
  }
}

void LiftoffAssembler::emit_i8x16_relaxed_swizzle(LiftoffRegister dst,
                                                  LiftoffRegister lhs,
                                                  LiftoffRegister rhs) {
  emit_i8x16_swizzle(dst, lhs, rhs);
}

void LiftoffAssembler::emit_s128_relaxed_laneselect(LiftoffRegister dst,
                                                    LiftoffRegister src1,
                                                    LiftoffRegister src2,
                                                    LiftoffRegister mask,
                                                    int lane_width) {
  // RISC-V uses bytewise selection for all lane widths.
  emit_s128_select(dst, src1, src2, mask);
}

void LiftoffAssembler::emit_i8x16_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  vmv_vx(dst.fp().toV(), src.gp());
}

void LiftoffAssembler::emit_i16x8_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  vmv_vx(dst.fp().toV(), src.gp());
}

void LiftoffAssembler::emit_i32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vmv_vx(dst.fp().toV(), src.gp());
}

void LiftoffAssembler::emit_i64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  WasmRvvEq(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E64, m1);
}

void LiftoffAssembler::emit_i64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  WasmRvvNe(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E64, m1);
}

void LiftoffAssembler::emit_i64x2_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGtS(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E64, m1);
}

void LiftoffAssembler::emit_i64x2_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGeS(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E64, m1);
}

void LiftoffAssembler::emit_f32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vfmv_vf(dst.fp().toV(), src.fp());
}

void LiftoffAssembler::emit_f64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vfmv_vf(dst.fp().toV(), src.fp());
}

void LiftoffAssembler::emit_i64x2_extmul_low_i32x4_s(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  VU.set(kScratchReg, E32, mf2);
  VRegister dst_v = dst.fp().toV();
  if (dst == src1 || dst == src2) {
    dst_v = kSimd128ScratchReg3;
  }
  vwmul_vv(dst_v, src2.fp().toV(), src1.fp().toV());
  if (dst == src1 || dst == src2) {
    VU.set(kScratchReg, E64, m1);
    vmv_vv(dst.fp().toV(), dst_v);
  }
}

void LiftoffAssembler::emit_i64x2_extmul_low_i32x4_u(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  VU.set(kScratchReg, E32, mf2);
  VRegister dst_v = dst.fp().toV();
  if (dst == src1 || dst == src2) {
    dst_v = kSimd128ScratchReg3;
  }
  vwmulu_vv(dst_v, src2.fp().toV(), src1.fp().toV());
  if (dst == src1 || dst == src2) {
    VU.set(kScratchReg, E64, m1);
    vmv_vv(dst.fp().toV(), dst_v);
  }
}

void LiftoffAssembler::emit_i64x2_extmul_high_i32x4_s(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  VU.set(kScratchReg, E32, m1);
  vslidedown_vi(kSimd128ScratchReg, src1.fp().toV(), 2);
  vslidedown_vi(kSimd128ScratchReg2, src2.fp().toV(), 2);
  VU.set(kScratchReg, E32, mf2);
  vwmul_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i64x2_extmul_high_i32x4_u(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  VU.set(kScratchReg, E32, m1);
  vslidedown_vi(kSimd128ScratchReg, src1.fp().toV(), 2);
  vslidedown_vi(kSimd128ScratchReg2, src2.fp().toV(), 2);
  VU.set(kScratchReg, E32, mf2);
  vwmulu_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i32x4_extmul_low_i16x8_s(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  VU.set(kScratchReg, E16, mf2);
  VRegister dst_v = dst.fp().toV();
  if (dst == src1 || dst == src2) {
    dst_v = kSimd128ScratchReg3;
  }
  vwmul_vv(dst_v, src2.fp().toV(), src1.fp().toV());
  if (dst == src1 || dst == src2) {
    VU.set(kScratchReg, E16, m1);
    vmv_vv(dst.fp().toV(), dst_v);
  }
}

void LiftoffAssembler::emit_i32x4_extmul_low_i16x8_u(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  VU.set(kScratchReg, E16, mf2);
  VRegister dst_v = dst.fp().toV();
  if (dst == src1 || dst == src2) {
    dst_v = kSimd128ScratchReg3;
  }
  vwmulu_vv(dst_v, src2.fp().toV(), src1.fp().toV());
  if (dst == src1 || dst == src2) {
    VU.set(kScratchReg, E16, m1);
    vmv_vv(dst.fp().toV(), dst_v);
  }
}

void LiftoffAssembler::emit_i32x4_extmul_high_i16x8_s(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  VU.set(kScratchReg, E16, m1);
  vslidedown_vi(kSimd128ScratchReg, src1.fp().toV(), 4);
  vslidedown_vi(kSimd128ScratchReg2, src2.fp().toV(), 4);
  VU.set(kScratchReg, E16, mf2);
  vwmul_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i32x4_extmul_high_i16x8_u(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  VU.set(kScratchReg, E16, m1);
  vslidedown_vi(kSimd128ScratchReg, src1.fp().toV(), 4);
  vslidedown_vi(kSimd128ScratchReg2, src2.fp().toV(), 4);
  VU.set(kScratchReg, E16, mf2);
  vwmulu_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i16x8_extmul_low_i8x16_s(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  VU.set(kScratchReg, E8, mf2);
  VRegister dst_v = dst.fp().toV();
  if (dst == src1 || dst == src2) {
    dst_v = kSimd128ScratchReg3;
  }
  vwmul_vv(dst_v, src2.fp().toV(), src1.fp().toV());
  if (dst == src1 || dst == src2) {
    VU.set(kScratchReg, E8, m1);
    vmv_vv(dst.fp().toV(), dst_v);
  }
}

void LiftoffAssembler::emit_i16x8_extmul_low_i8x16_u(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  VU.set(kScratchReg, E8, mf2);
  VRegister dst_v = dst.fp().toV();
  if (dst == src1 || dst == src2) {
    dst_v = kSimd128ScratchReg3;
  }
  vwmulu_vv(dst_v, src2.fp().toV(), src1.fp().toV());
  if (dst == src1 || dst == src2) {
    VU.set(kScratchReg, E8, m1);
    vmv_vv(dst.fp().toV(), dst_v);
  }
}

void LiftoffAssembler::emit_i16x8_extmul_high_i8x16_s(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  VU.set(kScratchReg, E8, m1);
  vslidedown_vi(kSimd128ScratchReg, src1.fp().toV(), 8);
  vslidedown_vi(kSimd128ScratchReg2, src2.fp().toV(), 8);
  VU.set(kScratchReg, E8, mf2);
  vwmul_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i16x8_extmul_high_i8x16_u(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  VU.set(kScratchReg, E8, m1);
  vslidedown_vi(kSimd128ScratchReg, src1.fp().toV(), 8);
  vslidedown_vi(kSimd128ScratchReg2, src2.fp().toV(), 8);
  VU.set(kScratchReg, E8, mf2);
  vwmulu_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

#undef SIMD_BINOP

void LiftoffAssembler::emit_i16x8_q15mulr_sat_s(LiftoffRegister dst,
                                                LiftoffRegister src1,
                                                LiftoffRegister src2) {
  VU.set(kScratchReg, E16, m1);
  vsmul_vv(dst.fp().toV(), src1.fp().toV(), src2.fp().toV());
}

void LiftoffAssembler::emit_i16x8_relaxed_q15mulr_s(LiftoffRegister dst,
                                                    LiftoffRegister src1,
                                                    LiftoffRegister src2) {
  VU.set(kScratchReg, E16, m1);
  vsmul_vv(dst.fp().toV(), src1.fp().toV(), src2.fp().toV());
}

void LiftoffAssembler::emit_i64x2_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vx(kSimd128RegZero, zero_reg);
  vmv_vx(kSimd128ScratchReg, zero_reg);
  vmslt_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128RegZero);
  VU.set(kScratchReg, E32, m1);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i64x2_sconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vsext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i64x2_sconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), 2);
  VU.set(kScratchReg, E64, m1);
  vsext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vzext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), 2);
  VU.set(kScratchReg, E64, m1);
  vzext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  WasmRvvEq(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E8, m1);
}

void LiftoffAssembler::emit_i8x16_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  WasmRvvNe(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E8, m1);
}

void LiftoffAssembler::emit_i8x16_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGtS(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E8, m1);
}

void LiftoffAssembler::emit_i8x16_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGtU(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E8, m1);
}

void LiftoffAssembler::emit_i8x16_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGeS(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E8, m1);
}

void LiftoffAssembler::emit_i8x16_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGeU(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E8, m1);
}

void LiftoffAssembler::emit_i16x8_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  WasmRvvEq(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E16, m1);
}

void LiftoffAssembler::emit_i16x8_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  WasmRvvNe(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E16, m1);
}

void LiftoffAssembler::emit_i16x8_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGtS(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E16, m1);
}

void LiftoffAssembler::emit_i16x8_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGtU(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E16, m1);
}

void LiftoffAssembler::emit_i16x8_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGeS(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E16, m1);
}

void LiftoffAssembler::emit_i16x8_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGeU(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E16, m1);
}

void LiftoffAssembler::emit_i32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  WasmRvvEq(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E32, m1);
}

void LiftoffAssembler::emit_i32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  WasmRvvNe(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E32, m1);
}

void LiftoffAssembler::emit_i32x4_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGtS(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E32, m1);
}

void LiftoffAssembler::emit_i32x4_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGtU(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E32, m1);
}

void LiftoffAssembler::emit_i32x4_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGeS(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E32, m1);
}

void LiftoffAssembler::emit_i32x4_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  WasmRvvGeU(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV(), E32, m1);
}

void LiftoffAssembler::emit_f32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmfeq_vv(v0, rhs.fp().toV(), lhs.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vmerge_vi(dst.fp().toV(), -1, dst.fp().toV());
}

void LiftoffAssembler::emit_f32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmfne_vv(v0, rhs.fp().toV(), lhs.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vmerge_vi(dst.fp().toV(), -1, dst.fp().toV());
}

void LiftoffAssembler::emit_f32x4_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmflt_vv(v0, lhs.fp().toV(), rhs.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vmerge_vi(dst.fp().toV(), -1, dst.fp().toV());
}

void LiftoffAssembler::emit_f32x4_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmfle_vv(v0, lhs.fp().toV(), rhs.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vmerge_vi(dst.fp().toV(), -1, dst.fp().toV());
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_s(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  VU.set(kScratchReg, E32, mf2);
  if (dst.fp().toV() != src.fp().toV()) {
    vfwcvt_f_x_v(dst.fp().toV(), src.fp().toV());
  } else {
    vfwcvt_f_x_v(kSimd128ScratchReg3, src.fp().toV());
    VU.set(kScratchReg, E64, m1);
    vmv_vv(dst.fp().toV(), kSimd128ScratchReg3);
  }
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_u(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  VU.set(kScratchReg, E32, mf2);
  if (dst.fp().toV() != src.fp().toV()) {
    vfwcvt_f_xu_v(dst.fp().toV(), src.fp().toV());
  } else {
    vfwcvt_f_xu_v(kSimd128ScratchReg3, src.fp().toV());
    VU.set(kScratchReg, E64, m1);
    vmv_vv(dst.fp().toV(), kSimd128ScratchReg3);
  }
}

void LiftoffAssembler::emit_f64x2_promote_low_f32x4(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  VU.set(kScratchReg, E32, mf2);
  if (dst.fp().toV() != src.fp().toV()) {
    vfwcvt_f_f_v(dst.fp().toV(), src.fp().toV());
  } else {
    vfwcvt_f_f_v(kSimd128ScratchReg3, src.fp().toV());
    VU.set(kScratchReg, E64, m1);
    vmv_vv(dst.fp().toV(), kSimd128ScratchReg3);
  }
}

void LiftoffAssembler::emit_f32x4_demote_f64x2_zero(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  VU.set(kScratchReg, E32, mf2);
  vfncvt_f_f_w(dst.fp().toV(), src.fp().toV());
  VU.set(kScratchReg, E32, m1);
  vmv_vi(v0, 12);
  vmerge_vx(dst.fp().toV(), zero_reg, dst.fp().toV());
}

void LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_s_zero(LiftoffRegister dst,
                                                         LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vx(kSimd128ScratchReg, zero_reg);
  vmfeq_vv(v0, src.fp().toV(), src.fp().toV());
  vmv_vv(kSimd128ScratchReg3, src.fp().toV());
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vfncvt_x_f_w(kSimd128ScratchReg, kSimd128ScratchReg3, MaskType::Mask);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_u_zero(LiftoffRegister dst,
                                                         LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vx(kSimd128ScratchReg, zero_reg);
  vmfeq_vv(v0, src.fp().toV(), src.fp().toV());
  vmv_vv(kSimd128ScratchReg3, src.fp().toV());
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vfncvt_xu_f_w(kSimd128ScratchReg, kSimd128ScratchReg3, MaskType::Mask);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f32x4_s(LiftoffRegister dst,
                                                        LiftoffRegister src) {
  VU.set(FPURoundingMode::RTZ);
  VU.set(kScratchReg, E32, m1);
  vfcvt_x_f_v(dst.fp().toV(), src.fp().toV());
}
void LiftoffAssembler::emit_i32x4_relaxed_trunc_f32x4_u(LiftoffRegister dst,
                                                        LiftoffRegister src) {
  VU.set(FPURoundingMode::RTZ);
  VU.set(kScratchReg, E32, m1);
  vfcvt_xu_f_v(dst.fp().toV(), src.fp().toV());
}
void LiftoffAssembler::emit_i32x4_relaxed_trunc_f64x2_s_zero(
    LiftoffRegister dst, LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vfncvt_x_f_w(kSimd128ScratchReg, kSimd128ScratchReg);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}
void LiftoffAssembler::emit_i32x4_relaxed_trunc_f64x2_u_zero(
    LiftoffRegister dst, LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vfncvt_xu_f_w(kSimd128ScratchReg, kSimd128ScratchReg);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_f64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vmfeq_vv(v0, rhs.fp().toV(), lhs.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vmerge_vi(dst.fp().toV(), -1, dst.fp().toV());
}

void LiftoffAssembler::emit_f64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vmfne_vv(v0, rhs.fp().toV(), lhs.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vmerge_vi(dst.fp().toV(), -1, dst.fp().toV());
}

void LiftoffAssembler::emit_f64x2_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vmflt_vv(v0, lhs.fp().toV(), rhs.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vmerge_vi(dst.fp().toV(), -1, dst.fp().toV());
}

void LiftoffAssembler::emit_f64x2_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vmfle_vv(v0, lhs.fp().toV(), rhs.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vmerge_vi(dst.fp().toV(), -1, dst.fp().toV());
}

void LiftoffAssembler::emit_s128_const(LiftoffRegister dst,
                                       const uint8_t imms[16]) {
  WasmRvvS128const(dst.fp().toV(), imms);
}

void LiftoffAssembler::emit_s128_not(LiftoffRegister dst, LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  vnot_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_s128_and(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vand_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_s128_or(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vor_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_s128_xor(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vxor_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_s128_and_not(LiftoffRegister dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vnot_vv(kSimd128ScratchReg, rhs.fp().toV());
  vand_vv(dst.fp().toV(), lhs.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_s128_select(LiftoffRegister dst,
                                        LiftoffRegister src1,
                                        LiftoffRegister src2,
                                        LiftoffRegister mask) {
  VU.set(kScratchReg, E8, m1);
  vand_vv(kSimd128ScratchReg, src1.fp().toV(), mask.fp().toV());
  vnot_vv(kSimd128ScratchReg2, mask.fp().toV());
  vand_vv(kSimd128ScratchReg2, src2.fp().toV(), kSimd128ScratchReg2);
  vor_vv(dst.fp().toV(), kSimd128ScratchReg, kSimd128ScratchReg2);
}

void LiftoffAssembler::emit_i8x16_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  vneg_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_v128_anytrue(LiftoffRegister dst,
                                         LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  Label t;
  vmv_sx(kSimd128ScratchReg, zero_reg);
  vredmaxu_vs(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
  beq(dst.gp(), zero_reg, &t);
  li(dst.gp(), 1);
  bind(&t);
}

void LiftoffAssembler::emit_i8x16_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  Label notalltrue;
  vmv_vi(kSimd128ScratchReg, -1);
  vredminu_vs(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
  beqz(dst.gp(), &notalltrue);
  li(dst.gp(), 1);
  bind(&notalltrue);
}

void LiftoffAssembler::emit_i8x16_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  vmv_vx(kSimd128RegZero, zero_reg);
  vmv_vx(kSimd128ScratchReg, zero_reg);
  vmslt_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128RegZero);
  VU.set(kScratchReg, E32, m1);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  andi(rhs.gp(), rhs.gp(), 8 - 1);
  vsll_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i8x16_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  DCHECK(is_uint5(rhs));
  VU.set(kScratchReg, E8, m1);
  vsll_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 8);
}

void LiftoffAssembler::emit_i8x16_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  andi(rhs.gp(), rhs.gp(), 8 - 1);
  vsra_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i8x16_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  VU.set(kScratchReg, E8, m1);
  vsra_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 8);
}

void LiftoffAssembler::emit_i8x16_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  andi(rhs.gp(), rhs.gp(), 8 - 1);
  vsrl_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i8x16_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  VU.set(kScratchReg, E8, m1);
  vsrl_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 8);
}

void LiftoffAssembler::emit_i8x16_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vadd_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_add_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vsadd_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_add_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vsaddu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vsub_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_sub_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vssub_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_sub_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vssubu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vmin_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vminu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vmax_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i8x16_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vmaxu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  vneg_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_i16x8_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  Label notalltrue;
  vmv_vi(kSimd128ScratchReg, -1);
  vredminu_vs(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
  beqz(dst.gp(), &notalltrue);
  li(dst.gp(), 1);
  bind(&notalltrue);
}

void LiftoffAssembler::emit_i16x8_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  vmv_vx(kSimd128RegZero, zero_reg);
  vmv_vx(kSimd128ScratchReg, zero_reg);
  vmslt_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128RegZero);
  VU.set(kScratchReg, E32, m1);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  andi(rhs.gp(), rhs.gp(), 16 - 1);
  vsll_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i16x8_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  VU.set(kScratchReg, E16, m1);
  vsll_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 16);
}

void LiftoffAssembler::emit_i16x8_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  andi(rhs.gp(), rhs.gp(), 16 - 1);
  vsra_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i16x8_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  VU.set(kScratchReg, E16, m1);
  vsra_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 16);
}

void LiftoffAssembler::emit_i16x8_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  andi(rhs.gp(), rhs.gp(), 16 - 1);
  vsrl_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i16x8_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  DCHECK(is_uint5(rhs));
  VU.set(kScratchReg, E16, m1);
  vsrl_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 16);
}

void LiftoffAssembler::emit_i16x8_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vadd_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_add_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vsadd_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_add_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vsaddu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vsub_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_sub_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vssub_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_sub_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vssubu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vmul_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vmin_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vminu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vmax_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i16x8_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vmaxu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vneg_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_i32x4_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  Label notalltrue;
  vmv_vi(kSimd128ScratchReg, -1);
  vredminu_vs(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
  beqz(dst.gp(), &notalltrue);
  li(dst.gp(), 1);
  bind(&notalltrue);
}

void LiftoffAssembler::emit_i32x4_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vmv_vx(kSimd128RegZero, zero_reg);
  vmv_vx(kSimd128ScratchReg, zero_reg);
  vmslt_vv(kSimd128ScratchReg, src.fp().toV(), kSimd128RegZero);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  andi(rhs.gp(), rhs.gp(), 32 - 1);
  vsll_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i32x4_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  if (is_uint5(rhs % 32)) {
    vsll_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 32);
  } else {
    li(kScratchReg, rhs % 32);
    vsll_vx(dst.fp().toV(), lhs.fp().toV(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i32x4_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  andi(rhs.gp(), rhs.gp(), 32 - 1);
  vsra_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i32x4_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  VU.set(kScratchReg, E32, m1);
  if (is_uint5(rhs % 32)) {
    vsra_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 32);
  } else {
    li(kScratchReg, rhs % 32);
    vsra_vx(dst.fp().toV(), lhs.fp().toV(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i32x4_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  andi(rhs.gp(), rhs.gp(), 32 - 1);
  vsrl_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i32x4_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  VU.set(kScratchReg, E32, m1);
  if (is_uint5(rhs % 32)) {
    vsrl_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 32);
  } else {
    li(kScratchReg, rhs % 32);
    vsrl_vx(dst.fp().toV(), lhs.fp().toV(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vadd_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vsub_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmul_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmin_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vminu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmax_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmaxu_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_dot_i16x8_s(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vwmul_vv(kSimd128ScratchReg3, lhs.fp().toV(), rhs.fp().toV());
  VU.set(kScratchReg, E32, m2);
  li(kScratchReg, 0b01010101);
  vmv_sx(v0, kScratchReg);
  vcompress_vv(kSimd128ScratchReg, kSimd128ScratchReg3, v0);

  li(kScratchReg, 0b10101010);
  vmv_sx(kSimd128ScratchReg2, kScratchReg);
  vcompress_vv(v0, kSimd128ScratchReg3, kSimd128ScratchReg2);
  VU.set(kScratchReg, E32, m1);
  vadd_vv(dst.fp().toV(), kSimd128ScratchReg, v0);
}

void LiftoffAssembler::emit_i16x8_dot_i8x16_i7x16_s(LiftoffRegister dst,
                                                    LiftoffRegister lhs,
                                                    LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vwmul_vv(kSimd128ScratchReg3, lhs.fp().toV(), rhs.fp().toV());
  VU.set(kScratchReg, E16, m2);

  constexpr int32_t FIRST_INDEX = 0b0101010101010101;
  constexpr int32_t SECOND_INDEX = 0b1010101010101010;
  li(kScratchReg, FIRST_INDEX);
  vmv_sx(v0, kScratchReg);
  vcompress_vv(kSimd128ScratchReg, kSimd128ScratchReg3, v0);

  li(kScratchReg, SECOND_INDEX);
  vmv_sx(kSimd128ScratchReg2, kScratchReg);
  vcompress_vv(v0, kSimd128ScratchReg3, kSimd128ScratchReg2);
  VU.set(kScratchReg, E16, m1);
  vadd_vv(dst.fp().toV(), kSimd128ScratchReg, v0);
}

void LiftoffAssembler::emit_i32x4_dot_i8x16_i7x16_add_s(LiftoffRegister dst,
                                                        LiftoffRegister lhs,
                                                        LiftoffRegister rhs,
                                                        LiftoffRegister acc) {
  DCHECK_NE(dst, acc);
  VU.set(kScratchReg, E8, m1);
  VRegister intermediate = kSimd128ScratchReg3;
  VRegister kSimd128ScratchReg4 =
      GetUnusedRegister(LiftoffRegList{LiftoffRegister(ft10)}).fp().toV();
  vwmul_vv(intermediate, lhs.fp().toV(), rhs.fp().toV());  // i16*16 v8 v9

  constexpr int32_t FIRST_INDEX = 0b0001000100010001;
  constexpr int32_t SECOND_INDEX = 0b0010001000100010;
  constexpr int32_t THIRD_INDEX = 0b0100010001000100;
  constexpr int32_t FOURTH_INDEX = 0b1000100010001000;

  VU.set(kScratchReg, E16, m2);
  li(kScratchReg, FIRST_INDEX);
  vmv_sx(v0, kScratchReg);
  vcompress_vv(kSimd128ScratchReg, intermediate, v0);  // i16*4 a
  li(kScratchReg, SECOND_INDEX);
  vmv_sx(kSimd128ScratchReg2, kScratchReg);
  vcompress_vv(v0, intermediate, kSimd128ScratchReg2);  // i16*4 b

  VU.set(kScratchReg, E16, m1);
  vwadd_vv(kSimd128ScratchReg4, kSimd128ScratchReg, v0);  // i32*4 c

  VU.set(kScratchReg, E16, m2);
  li(kScratchReg, THIRD_INDEX);
  vmv_sx(v0, kScratchReg);
  vcompress_vv(kSimd128ScratchReg, intermediate, v0);  // i16*4 a

  li(kScratchReg, FOURTH_INDEX);
  vmv_sx(kSimd128ScratchReg2, kScratchReg);
  vcompress_vv(v0, intermediate, kSimd128ScratchReg2);  // i16*4 b

  VU.set(kScratchReg, E16, m1);
  vwadd_vv(kSimd128ScratchReg3, kSimd128ScratchReg, v0);  // i32*4 c

  VU.set(kScratchReg, E32, m1);
  vadd_vv(dst.fp().toV(), kSimd128ScratchReg4, kSimd128ScratchReg3);
  vadd_vv(dst.fp().toV(), dst.fp().toV(), acc.fp().toV());
}

void LiftoffAssembler::emit_i64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vneg_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_i64x2_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  Label notalltrue;
  vmv_vi(kSimd128ScratchReg, -1);
  vredminu_vs(kSimd128ScratchReg, src.fp().toV(), kSimd128ScratchReg);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
  beqz(dst.gp(), &notalltrue);
  li(dst.gp(), 1);
  bind(&notalltrue);
}

void LiftoffAssembler::emit_i64x2_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  andi(rhs.gp(), rhs.gp(), 64 - 1);
  vsll_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i64x2_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  VU.set(kScratchReg, E64, m1);
  if (is_uint5(rhs % 64)) {
    vsll_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 64);
  } else {
    li(kScratchReg, rhs % 64);
    vsll_vx(dst.fp().toV(), lhs.fp().toV(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i64x2_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  andi(rhs.gp(), rhs.gp(), 64 - 1);
  vsra_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i64x2_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  VU.set(kScratchReg, E64, m1);
  if (is_uint5(rhs % 64)) {
    vsra_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 64);
  } else {
    li(kScratchReg, rhs % 64);
    vsra_vx(dst.fp().toV(), lhs.fp().toV(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i64x2_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  andi(rhs.gp(), rhs.gp(), 64 - 1);
  vsrl_vx(dst.fp().toV(), lhs.fp().toV(), rhs.gp());
}

void LiftoffAssembler::emit_i64x2_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  VU.set(kScratchReg, E64, m1);
  if (is_uint5(rhs % 64)) {
    vsrl_vi(dst.fp().toV(), lhs.fp().toV(), rhs % 64);
  } else {
    li(kScratchReg, rhs % 64);
    vsrl_vx(dst.fp().toV(), lhs.fp().toV(), kScratchReg);
  }
}

void LiftoffAssembler::emit_i64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vadd_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vsub_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_i64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vmul_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vfabs_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_f32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vfneg_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_f32x4_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vfsqrt_v(dst.fp().toV(), src.fp().toV());
}

bool LiftoffAssembler::emit_f32x4_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  Ceil_f(dst.fp().toV(), src.fp().toV(), kScratchReg, kSimd128ScratchReg);
  return true;
}

bool LiftoffAssembler::emit_f32x4_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  Floor_f(dst.fp().toV(), src.fp().toV(), kScratchReg, kSimd128ScratchReg);
  return true;
}

bool LiftoffAssembler::emit_f32x4_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  Trunc_f(dst.fp().toV(), src.fp().toV(), kScratchReg, kSimd128ScratchReg);
  return true;
}

bool LiftoffAssembler::emit_f32x4_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  Round_f(dst.fp().toV(), src.fp().toV(), kScratchReg, kSimd128ScratchReg);
  return true;
}

void LiftoffAssembler::emit_f32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vfadd_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vfsub_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vfmul_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f32x4_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vfdiv_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f32x4_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  const int32_t kNaN = 0x7FC00000;
  VU.set(kScratchReg, E32, m1);
  vmfeq_vv(v0, lhs.fp().toV(), lhs.fp().toV());
  vmfeq_vv(kSimd128ScratchReg, rhs.fp().toV(), rhs.fp().toV());
  vand_vv(v0, v0, kSimd128ScratchReg);
  li(kScratchReg, kNaN);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  vfmin_vv(kSimd128ScratchReg, rhs.fp().toV(), lhs.fp().toV(), Mask);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_f32x4_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  const int32_t kNaN = 0x7FC00000;
  VU.set(kScratchReg, E32, m1);
  vmfeq_vv(v0, lhs.fp().toV(), lhs.fp().toV());
  vmfeq_vv(kSimd128ScratchReg, rhs.fp().toV(), rhs.fp().toV());
  vand_vv(v0, v0, kSimd128ScratchReg);
  li(kScratchReg, kNaN);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  vfmax_vv(kSimd128ScratchReg, rhs.fp().toV(), lhs.fp().toV(), Mask);
  vmv_vv(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_f32x4_relaxed_min(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vfmin_vv(dst.fp().toV(), rhs.fp().toV(), lhs.fp().toV());
}

void LiftoffAssembler::emit_f32x4_relaxed_max(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vfmax_vv(dst.fp().toV(), rhs.fp().toV(), lhs.fp().toV());
}

void LiftoffAssembler::emit_f32x4_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  // b < a ? b : a
  vmflt_vv(v0, rhs.fp().toV(), lhs.fp().toV());
  vmerge_vv(dst.fp().toV(), rhs.fp().toV(), lhs.fp().toV());
}

void LiftoffAssembler::emit_f32x4_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  // a < b ? b : a
  vmflt_vv(v0, lhs.fp().toV(), rhs.fp().toV());
  vmerge_vv(dst.fp().toV(), rhs.fp().toV(), lhs.fp().toV());
}

void LiftoffAssembler::emit_f64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vfabs_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_f64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vfneg_vv(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_f64x2_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vfsqrt_v(dst.fp().toV(), src.fp().toV());
}

bool LiftoffAssembler::emit_f64x2_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  Ceil_d(dst.fp().toV(), src.fp().toV(), kScratchReg, kSimd128ScratchReg);
  return true;
}

bool LiftoffAssembler::emit_f64x2_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  Floor_d(dst.fp().toV(), src.fp().toV(), kScratchReg, kSimd128ScratchReg);
  return true;
}

bool LiftoffAssembler::emit_f64x2_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  Trunc_d(dst.fp().toV(), src.fp().toV(), kScratchReg, kSimd128ScratchReg);
  return true;
}

bool LiftoffAssembler::emit_f64x2_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  Round_d(dst.fp().toV(), src.fp().toV(), kScratchReg, kSimd128ScratchReg);
  return true;
}

void LiftoffAssembler::emit_f64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vfadd_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vfsub_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vfmul_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f64x2_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vfdiv_vv(dst.fp().toV(), lhs.fp().toV(), rhs.fp().toV());
}

void LiftoffAssembler::emit_f64x2_relaxed_min(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vfmin_vv(dst.fp().toV(), rhs.fp().toV(), lhs.fp().toV());
}

void LiftoffAssembler::emit_f64x2_relaxed_max(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  vfmax_vv(dst.fp().toV(), rhs.fp().toV(), lhs.fp().toV());
}

void LiftoffAssembler::emit_f64x2_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  // b < a ? b : a
  vmflt_vv(v0, rhs.fp().toV(), lhs.fp().toV());
  vmerge_vv(dst.fp().toV(), rhs.fp().toV(), lhs.fp().toV());
}

void LiftoffAssembler::emit_f64x2_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  VU.set(kScratchReg, E64, m1);
  // a < b ? b : a
  vmflt_vv(v0, lhs.fp().toV(), rhs.fp().toV());
  vmerge_vv(dst.fp().toV(), rhs.fp().toV(), lhs.fp().toV());
}

void LiftoffAssembler::emit_i32x4_sconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vmfeq_vv(v0, src.fp().toV(), src.fp().toV());
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vfcvt_x_f_v(dst.fp().toV(), kSimd128ScratchReg, Mask);
}

void LiftoffAssembler::emit_i32x4_uconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vmfeq_vv(v0, src.fp().toV(), src.fp().toV());
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vmv_vx(dst.fp().toV(), zero_reg);
  vfcvt_xu_f_v(dst.fp().toV(), kSimd128ScratchReg, Mask);
}

void LiftoffAssembler::emit_f32x4_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vfcvt_f_x_v(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_f32x4_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  VU.set(FPURoundingMode::RTZ);
  vfcvt_f_xu_v(dst.fp().toV(), src.fp().toV());
}

void LiftoffAssembler::emit_i8x16_sconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vmv_vv(kSimd128ScratchReg, lhs.fp().toV());  // kSimd128ScratchReg v24
  vmv_vv(v25, rhs.fp().toV());
  VU.set(kScratchReg, E8, m1);
  VU.set(FPURoundingMode::RNE);
  vnclip_vi(dst.fp().toV(), kSimd128ScratchReg, 0);
}

void LiftoffAssembler::emit_i8x16_uconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  VU.set(kScratchReg, E16, m1);
  vmv_vv(kSimd128ScratchReg, lhs.fp().toV());  // kSimd128ScratchReg v24
  vmv_vv(v25, rhs.fp().toV());
  VU.set(kScratchReg, E16, m2);
  vmax_vx(kSimd128ScratchReg, kSimd128ScratchReg, zero_reg);
  VU.set(kScratchReg, E8, m1);
  VU.set(FPURoundingMode::RNE);
  vnclipu_vi(dst.fp().toV(), kSimd128ScratchReg, 0);
}

void LiftoffAssembler::emit_i16x8_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmv_vv(kSimd128ScratchReg, lhs.fp().toV());  // kSimd128ScratchReg v24
  vmv_vv(v25, rhs.fp().toV());
  VU.set(kScratchReg, E16, m1);
  VU.set(FPURoundingMode::RNE);
  vnclip_vi(dst.fp().toV(), kSimd128ScratchReg, 0);
}

void LiftoffAssembler::emit_i16x8_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  VU.set(kScratchReg, E32, m1);
  vmv_vv(kSimd128ScratchReg, lhs.fp().toV());  // kSimd128ScratchReg v24
  vmv_vv(v25, rhs.fp().toV());
  VU.set(kScratchReg, E32, m2);
  vmax_vx(kSimd128ScratchReg, kSimd128ScratchReg, zero_reg);
  VU.set(kScratchReg, E16, m1);
  VU.set(FPURoundingMode::RNE);
  vnclipu_vi(dst.fp().toV(), kSimd128ScratchReg, 0);
}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vsext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), 8);
  VU.set(kScratchReg, E16, m1);
  vsext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vzext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), 8);
  VU.set(kScratchReg, E16, m1);
  vzext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vsext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), 4);
  VU.set(kScratchReg, E32, m1);
  vsext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vmv_vv(kSimd128ScratchReg, src.fp().toV());
  vzext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  vslidedown_vi(kSimd128ScratchReg, src.fp().toV(), 4);
  VU.set(kScratchReg, E32, m1);
  vzext_vf2(dst.fp().toV(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
  VU.set(kScratchReg, E8, m1);
  vwaddu_vv(kSimd128ScratchReg, lhs.fp().toV(), rhs.fp().toV());
  li(kScratchReg, 1);
  vwaddu_wx(kSimd128ScratchReg3, kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 2);
  VU.set(kScratchReg2, E16, m2);
  vdivu_vx(kSimd128ScratchReg3, kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg2, E8, m1);
  vnclipu_vi(dst.fp().toV(), kSimd128ScratchReg3, 0);
}
void LiftoffAssembler::emit_i16x8_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
  VU.set(kScratchReg2, E16, m1);
  vwaddu_vv(kSimd128ScratchReg, lhs.fp().toV(), rhs.fp().toV());
  li(kScratchReg, 1);
  vwaddu_wx(kSimd128ScratchReg3, kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, 2);
  VU.set(kScratchReg2, E32, m2);
  vdivu_vx(kSimd128ScratchReg3, kSimd128ScratchReg3, kScratchReg);
  VU.set(kScratchReg2, E16, m1);
  vnclipu_vi(dst.fp().toV(), kSimd128ScratchReg3, 0);
}

void LiftoffAssembler::emit_i8x16_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E8, m1);
  vmv_vx(kSimd128RegZero, zero_reg);
  vmv_vv(dst.fp().toV(), src.fp().toV());
  vmv_vv(v0, kSimd128RegZero);
  vmslt_vv(v0, src.fp().toV(), kSimd128RegZero);
  vneg_vv(dst.fp().toV(), src.fp().toV(), MaskType::Mask);
}

void LiftoffAssembler::emit_i16x8_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E16, m1);
  vmv_vx(kSimd128RegZero, zero_reg);
  vmv_vv(dst.fp().toV(), src.fp().toV());
  vmv_vv(v0, kSimd128RegZero);
  vmslt_vv(v0, src.fp().toV(), kSimd128RegZero);
  vneg_vv(dst.fp().toV(), src.fp().toV(), MaskType::Mask);
}

void LiftoffAssembler::emit_i64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E64, m1);
  vmv_vx(kSimd128RegZero, zero_reg);
  vmv_vv(dst.fp().toV(), src.fp().toV());
  vmv_vv(v0, kSimd128RegZero);
  vmslt_vv(v0, src.fp().toV(), kSimd128RegZero);
  vneg_vv(dst.fp().toV(), src.fp().toV(), MaskType::Mask);
}

void LiftoffAssembler::emit_i32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  VU.set(kScratchReg, E32, m1);
  vmv_vx(kSimd128RegZero, zero_reg);
  vmv_vv(dst.fp().toV(), src.fp().toV());
  vmv_vv(v0, kSimd128RegZero);
  vmslt_vv(v0, src.fp().toV(), kSimd128RegZero);
  vneg_vv(dst.fp().toV(), src.fp().toV(), MaskType::Mask);
}

void LiftoffAssembler::emit_i8x16_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E8, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
  slli(dst.gp(), dst.gp(), sizeof(void*) * 8 - 8);
  srli(dst.gp(), dst.gp(), sizeof(void*) * 8 - 8);
}

void LiftoffAssembler::emit_i8x16_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E8, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E16, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
  slli(dst.gp(), dst.gp(), sizeof(void*) * 8 - 16);
  srli(dst.gp(), dst.gp(), sizeof(void*) * 8 - 16);
}

void LiftoffAssembler::emit_i16x8_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E16, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E32, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx);
  vmv_xs(dst.gp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_f32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E32, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx);
  vfmv_fs(dst.fp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_f64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E64, m1);
  vslidedown_vi(kSimd128ScratchReg, lhs.fp().toV(), imm_lane_idx);
  vfmv_fs(dst.fp(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E64, m1);
  li(kScratchReg, 0x1 << imm_lane_idx);
  vmv_sx(v0, kScratchReg);
  VU.set(kScratchReg, E8, m1);
  vmerge_vx(dst.fp().toV(), src2.gp(), src1.fp().toV());
}

void LiftoffAssembler::emit_i16x8_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E16, m1);
  li(kScratchReg, 0x1 << imm_lane_idx);
  vmv_sx(v0, kScratchReg);
  vmerge_vx(dst.fp().toV(), src2.gp(), src1.fp().toV());
}

void LiftoffAssembler::emit_i32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E32, m1);
  li(kScratchReg, 0x1 << imm_lane_idx);
  vmv_sx(v0, kScratchReg);
  vmerge_vx(dst.fp().toV(), src2.gp(), src1.fp().toV());
}

void LiftoffAssembler::emit_f32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E32, m1);
  li(kScratchReg, 0x1 << imm_lane_idx);
  vmv_sx(v0, kScratchReg);
  fmv_x_w(kScratchReg, src2.fp());
  vmerge_vx(dst.fp().toV(), kScratchReg, src1.fp().toV());
}

void LiftoffAssembler::emit_f64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  VU.set(kScratchReg, E64, m1);
  li(kScratchReg, 0x1 << imm_lane_idx);
  vmv_sx(v0, kScratchReg);
  vfmerge_vf(dst.fp().toV(), src2.fp(), src1.fp().toV());
}

void LiftoffAssembler::emit_s128_set_if_nan(Register dst, LiftoffRegister src,
                                            Register tmp_gp,
                                            LiftoffRegister tmp_s128,
                                            ValueKind lane_kind) {
  ASM_CODE_COMMENT(this);
  if (lane_kind == kF32) {
    VU.set(kScratchReg, E32, m1);
    vmfeq_vv(kSimd128ScratchReg, src.fp().toV(),
             src.fp().toV());  // scratch <- !IsNan(tmp_fp)
  } else {
    VU.set(kScratchReg, E64, m1);
    DCHECK_EQ(lane_kind, kF64);
    vmfeq_vv(kSimd128ScratchReg, src.fp().toV(),
             src.fp().toV());  // scratch <- !IsNan(tmp_fp)
  }
  vmv_xs(kScratchReg, kSimd128ScratchReg);
  not_(kScratchReg, kScratchReg);
  andi(kScratchReg, kScratchReg, int32_t(lane_kind == kF32 ? 0xF : 0x3));
  Sw(kScratchReg, MemOperand(dst));
}

void LiftoffAssembler::emit_f32x4_qfma(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  VU.set(kScratchReg, E32, m1);
  vfmadd_vv(src1.fp().toV(), src2.fp().toV(), src3.fp().toV());
  vmv_vv(dst.fp().toV(), src1.fp().toV());
}

void LiftoffAssembler::emit_f32x4_qfms(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  VU.set(kScratchReg, E32, m1);
  vfnmsub_vv(src1.fp().toV(), src2.fp().toV(), src3.fp().toV());
  vmv_vv(dst.fp().toV(), src1.fp().toV());
}

void LiftoffAssembler::emit_f64x2_qfma(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  VU.set(kScratchReg, E64, m1);
  vfmadd_vv(src1.fp().toV(), src2.fp().toV(), src3.fp().toV());
  vmv_vv(dst.fp().toV(), src1.fp().toV());
}

void LiftoffAssembler::emit_f64x2_qfms(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  VU.set(kScratchReg, E64, m1);
  vfnmsub_vv(src1.fp().toV(), src2.fp().toV(), src3.fp().toV());
  vmv_vv(dst.fp().toV(), src1.fp().toV());
}

void LiftoffAssembler::set_trap_on_oob_mem64(Register index, uint64_t oob_size,
                                             uint64_t oob_index) {
  UNREACHABLE();
}

void LiftoffAssembler::StackCheck(Label* ool_code) {
  UseScratchRegisterScope temps(this);
  Register limit_address = temps.Acquire();
  LoadStackLimit(limit_address, StackLimitKind::kInterruptStackLimit);
  MacroAssembler::Branch(ool_code, ule, sp, Operand(limit_address));
}

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  if (v8_flags.debug_code) Abort(reason);
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  int32_t num_gp_regs = gp_regs.GetNumRegsSet();
  if (num_gp_regs) {
    int32_t offset = num_gp_regs * kSystemPointerSize;
    AddWord(sp, sp, Operand(-offset));
    while (!gp_regs.is_empty()) {
      LiftoffRegister reg = gp_regs.GetFirstRegSet();
      offset -= kSystemPointerSize;
      StoreWord(reg.gp(), MemOperand(sp, offset));
      gp_regs.clear(reg);
    }
    DCHECK_EQ(offset, 0);
  }
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  int32_t num_fp_regs = fp_regs.GetNumRegsSet();
  if (num_fp_regs) {
    AddWord(sp, sp, Operand(-(num_fp_regs * kStackSlotSize)));
    int32_t offset = 0;
    while (!fp_regs.is_empty()) {
      LiftoffRegister reg = fp_regs.GetFirstRegSet();
      MacroAssembler::StoreDouble(reg.fp(), MemOperand(sp, offset));
      fp_regs.clear(reg);
      offset += sizeof(double);
    }
    DCHECK_EQ(offset, num_fp_regs * sizeof(double));
  }
}

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) {
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  int32_t fp_offset = 0;
  while (!fp_regs.is_empty()) {
    LiftoffRegister reg = fp_regs.GetFirstRegSet();
    MacroAssembler::LoadDouble(reg.fp(), MemOperand(sp, fp_offset));
    fp_regs.clear(reg);
    fp_offset += sizeof(double);
  }
  if (fp_offset) AddWord(sp, sp, Operand(fp_offset));
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  int32_t gp_offset = 0;
  while (!gp_regs.is_empty()) {
    LiftoffRegister reg = gp_regs.GetLastRegSet();
    LoadWord(reg.gp(), MemOperand(sp, gp_offset));
    gp_regs.clear(reg);
    gp_offset += kSystemPointerSize;
  }
  AddWord(sp, sp, Operand(gp_offset));
}

void LiftoffAssembler::RecordSpillsInSafepoint(
    SafepointTableBuilder::Safepoint& safepoint, LiftoffRegList all_spills,
    LiftoffRegList ref_spills, int spill_offset) {
  LiftoffRegList fp_spills = all_spills & kFpCacheRegList;
  int spill_space_size = fp_spills.GetNumRegsSet() * kSimd128Size;
  LiftoffRegList gp_spills = all_spills & kGpCacheRegList;
  while (!gp_spills.is_empty()) {
    LiftoffRegister reg = gp_spills.GetFirstRegSet();
    if (ref_spills.has(reg)) {
      safepoint.DefineTaggedStackSlot(spill_offset);
    }
    gp_spills.clear(reg);
    ++spill_offset;
    spill_space_size += kSystemPointerSize;
  }
  // Record the number of additional spill slots.
  RecordOolSpillSpaceSize(spill_space_size);
}

void LiftoffAssembler::DropStackSlotsAndRet(uint32_t num_stack_slots) {
  MacroAssembler::DropAndRet(static_cast<int>(num_stack_slots));
}

void LiftoffAssembler::CallNativeWasmCode(Address addr) {
  Call(addr, RelocInfo::WASM_CALL);
}

void LiftoffAssembler::TailCallNativeWasmCode(Address addr) {
  Jump(addr, RelocInfo::WASM_CALL);
}

void LiftoffAssembler::CallIndirect(const ValueKindSig* sig,
                                    compiler::CallDescriptor* call_descriptor,
                                    Register target) {
  if (target == no_reg) {
    pop(t6);
    Call(t6);
  } else {
    Call(target);
  }
}

void LiftoffAssembler::TailCallIndirect(Register target) {
  if (target == no_reg) {
    Pop(t6);
    Jump(t6);
  } else {
    Jump(target);
  }
}

void LiftoffAssembler::CallBuiltin(Builtin builtin) {
  // A direct call to a builtin. Just encode the builtin index. This will be
  // patched at relocation.
  Call(static_cast<Address>(builtin), RelocInfo::WASM_STUB_CALL);
}

void LiftoffAssembler::AllocateStackSlot(Register addr, uint32_t size) {
  AddWord(sp, sp, Operand(-size));
  MacroAssembler::Move(addr, sp);
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) {
  AddWord(sp, sp, Operand(size));
}

void LiftoffAssembler::MaybeOSR() {}

void LiftoffAssembler::emit_set_if_nan(Register dst, FPURegister src,
                                       ValueKind kind) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, 1);
  if (kind == kF32) {
    feq_s(scratch, src, src);  // rd <- !isNan(src)
  } else {
    DCHECK_EQ(kind, kF64);
    feq_d(scratch, src, src);  // rd <- !isNan(src)
  }
  seqz(scratch, scratch);
  Sw(scratch, MemOperand(dst));
}

void LiftoffAssembler::CallFrameSetupStub(int declared_function_index) {
// The standard library used by gcc tryjobs does not consider `std::find` to be
// `constexpr`, so wrap it in a `#ifdef __clang__` block.
#ifdef __clang__
  static_assert(std::find(std::begin(wasm::kGpParamRegisters),
                          std::end(wasm::kGpParamRegisters),
                          kLiftoffFrameSetupFunctionReg) ==
                std::end(wasm::kGpParamRegisters));
#endif

  // On MIPS64, we must push at least {ra} before calling the stub, otherwise
  // it would get clobbered with no possibility to recover it. So just set
  // up the frame here.
  EnterFrame(StackFrame::WASM);
  LoadConstant(LiftoffRegister(kLiftoffFrameSetupFunctionReg),
               WasmValue(declared_function_index));
  CallBuiltin(Builtin::kWasmLiftoffFrameSetup);
}

bool LiftoffAssembler::emit_f16x8_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  return false;
}

bool LiftoffAssembler::emit_f16x8_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  return false;
}

bool LiftoffAssembler::emit_f16x8_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  return false;
}

bool LiftoffAssembler::emit_f16x8_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  return false;
}

bool LiftoffAssembler::emit_i16x8_sconvert_f16x8(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_i16x8_uconvert_f16x8(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_sconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_uconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_demote_f32x4_zero(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f16x8_demote_f64x2_zero(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  return false;
}
bool LiftoffAssembler::emit_f32x4_promote_low_f16x8(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  return false;
}

bool LiftoffAssembler::emit_f16x8_qfma(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  return false;
}

bool LiftoffAssembler::emit_f16x8_qfms(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  return false;
}

}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_RISCV_LIFTOFF_ASSEMBLER_RISCV_INL_H_
