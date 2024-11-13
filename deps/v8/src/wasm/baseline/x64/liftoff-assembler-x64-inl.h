// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_X64_LIFTOFF_ASSEMBLER_X64_INL_H_
#define V8_WASM_BASELINE_X64_LIFTOFF_ASSEMBLER_X64_INL_H_

#include <optional>

#include "src/codegen/assembler.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/x64/assembler-x64.h"
#include "src/codegen/x64/register-x64.h"
#include "src/flags/flags.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/wasm/baseline/liftoff-assembler.h"
#include "src/wasm/baseline/parallel-move-inl.h"
#include "src/wasm/baseline/parallel-move.h"
#include "src/wasm/object-access.h"
#include "src/wasm/simd-shuffle.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"

namespace v8::internal::wasm {

#define RETURN_FALSE_IF_MISSING_CPU_FEATURE(name)    \
  if (!CpuFeatures::IsSupported(name)) return false; \
  CpuFeatureScope feature(this, name);

namespace liftoff {

constexpr Register kScratchRegister2 = r11;
static_assert(kScratchRegister != kScratchRegister2, "collision");
static_assert((kLiftoffAssemblerGpCacheRegs &
               RegList{kScratchRegister, kScratchRegister2})
                  .is_empty(),
              "scratch registers must not be used as cache registers");

constexpr DoubleRegister kScratchDoubleReg2 = xmm14;
static_assert(kScratchDoubleReg != kScratchDoubleReg2, "collision");
static_assert((kLiftoffAssemblerFpCacheRegs &
               DoubleRegList{kScratchDoubleReg, kScratchDoubleReg2})
                  .is_empty(),
              "scratch registers must not be used as cache registers");

inline constexpr Operand GetStackSlot(int offset) {
  return Operand(rbp, -offset);
}

constexpr Operand kInstanceDataOperand =
    GetStackSlot(WasmLiftoffFrameConstants::kInstanceDataOffset);

constexpr Operand kOSRTargetSlot = GetStackSlot(kOSRTargetOffset);

inline Operand GetMemOp(LiftoffAssembler* assm, Register addr,
                        Register offset_reg, uintptr_t offset_imm,
                        ScaleFactor scale_factor = times_1) {
  if (is_uint31(offset_imm)) {
    int32_t offset_imm32 = static_cast<int32_t>(offset_imm);
    return offset_reg == no_reg
               ? Operand(addr, offset_imm32)
               : Operand(addr, offset_reg, scale_factor, offset_imm32);
  }
  // Offset immediate does not fit in 31 bits.
  Register scratch = kScratchRegister;
  assm->MacroAssembler::Move(scratch, offset_imm);
  if (offset_reg != no_reg) assm->addq(scratch, offset_reg);
  return Operand(addr, scratch, scale_factor, 0);
}

inline void LoadFromStack(LiftoffAssembler* assm, LiftoffRegister dst,
                          Operand src, ValueKind kind) {
  switch (kind) {
    case kI16:
      assm->movw(dst.gp(), src);
      break;
    case kI32:
      assm->movl(dst.gp(), src);
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      // Stack slots are uncompressed even when heap pointers are compressed.
      assm->movq(dst.gp(), src);
      break;
    case kF32:
      assm->Movss(dst.fp(), src);
      break;
    case kF64:
      assm->Movsd(dst.fp(), src);
      break;
    case kS128:
      assm->Movdqu(dst.fp(), src);
      break;
    default:
      UNREACHABLE();
  }
}

inline void StoreToMemory(LiftoffAssembler* assm, Operand dst,
                          LiftoffRegister src, ValueKind kind) {
  switch (kind) {
    case kI16:
      assm->movw(dst, src.gp());
      break;
    case kI32:
      assm->movl(dst, src.gp());
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      // Stack slots are uncompressed even when heap pointers are compressed.
      assm->movq(dst, src.gp());
      break;
    case kF32:
      assm->Movss(dst, src.fp());
      break;
    case kF64:
      assm->Movsd(dst, src.fp());
      break;
    case kS128:
      assm->Movdqu(dst, src.fp());
      break;
    default:
      UNREACHABLE();
  }
}

inline void StoreToMemory(LiftoffAssembler* assm, Operand dst,
                          const LiftoffAssembler::VarState& src) {
  if (src.is_reg()) {
    liftoff::StoreToMemory(assm, dst, src.reg(), src.kind());
  } else if (src.is_const()) {
    if (src.kind() == kI32) {
      assm->movl(dst, Immediate(src.i32_const()));
    } else {
      assm->MacroAssembler::Move(dst, static_cast<int64_t>(src.i32_const()));
    }
  } else if (value_kind_size(src.kind()) == 4) {
    DCHECK(src.is_stack());
    assm->movl(kScratchRegister, liftoff::GetStackSlot(src.offset()));
    assm->movl(dst, kScratchRegister);
  } else {
    DCHECK(src.is_stack());
    DCHECK_EQ(8, value_kind_size(src.kind()));
    assm->movq(kScratchRegister, liftoff::GetStackSlot(src.offset()));
    assm->movq(dst, kScratchRegister);
  }
}

inline void push(LiftoffAssembler* assm, LiftoffRegister reg, ValueKind kind,
                 int padding = 0) {
  switch (kind) {
    case kI32:
    case kI64:
    case kRef:
    case kRefNull:
    case kRtt:
      assm->AllocateStackSpace(padding);
      assm->pushq(reg.gp());
      break;
    case kF32:
      assm->AllocateStackSpace(kSystemPointerSize + padding);
      assm->Movss(Operand(rsp, 0), reg.fp());
      break;
    case kF64:
      assm->AllocateStackSpace(kSystemPointerSize + padding);
      assm->Movsd(Operand(rsp, 0), reg.fp());
      break;
    case kS128:
      assm->AllocateStackSpace(kSystemPointerSize * 2 + padding);
      assm->Movdqu(Operand(rsp, 0), reg.fp());
      break;
    default:
      UNREACHABLE();
  }
}

constexpr int kSubSpSize = 7;  // 7 bytes for "subq rsp, <imm32>"

}  // namespace liftoff

int LiftoffAssembler::PrepareStackFrame() {
  int offset = pc_offset();
  // Next we reserve the memory for the whole stack frame. We do not know yet
  // how big the stack frame will be so we just emit a placeholder instruction.
  // PatchPrepareStackFrame will patch this in order to increase the stack
  // appropriately.
  sub_sp_32(0);
  DCHECK_EQ(liftoff::kSubSpSize, pc_offset() - offset);
  return offset;
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

  LoadConstant(LiftoffRegister(kLiftoffFrameSetupFunctionReg),
               WasmValue(declared_function_index));
  CallBuiltin(Builtin::kWasmLiftoffFrameSetup);
}

void LiftoffAssembler::PrepareTailCall(int num_callee_stack_params,
                                       int stack_param_delta) {
  // Push the return address and frame pointer to complete the stack frame.
  pushq(Operand(rbp, 8));
  pushq(Operand(rbp, 0));

  // Shift the whole frame upwards.
  const int slot_count = num_callee_stack_params + 2;
  for (int i = slot_count - 1; i >= 0; --i) {
    movq(kScratchRegister, Operand(rsp, i * 8));
    movq(Operand(rbp, (i - stack_param_delta) * 8), kScratchRegister);
  }

  // Set the new stack and frame pointer.
  leaq(rsp, Operand(rbp, -stack_param_delta * 8));
  popq(rbp);
}

void LiftoffAssembler::AlignFrameSize() {
  max_used_spill_offset_ = RoundUp(max_used_spill_offset_, kSystemPointerSize);
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
  DCHECK_EQ(0, frame_size % kSystemPointerSize);

  // We can't run out of space when patching, just pass anything big enough to
  // not cause the assembler to try to grow the buffer.
  constexpr int kAvailableSpace = 64;
  Assembler patching_assembler(
      AssemblerOptions{},
      ExternalAssemblerBuffer(buffer_start_ + offset, kAvailableSpace));

  if (V8_LIKELY(frame_size < 4 * KB)) {
    // This is the standard case for small frames: just subtract from SP and be
    // done with it.
    patching_assembler.sub_sp_32(frame_size);
    DCHECK_EQ(liftoff::kSubSpSize, patching_assembler.pc_offset());
    return;
  }

  // The frame size is bigger than 4KB, so we might overflow the available stack
  // space if we first allocate the frame and then do the stack check (we will
  // need some remaining stack space for throwing the exception). That's why we
  // check the available stack space before we allocate the frame. To do this we
  // replace the {__ sub(sp, framesize)} with a jump to OOL code that does this
  // "extended stack check".
  //
  // The OOL code can simply be generated here with the normal assembler,
  // because all other code generation, including OOL code, has already finished
  // when {PatchPrepareStackFrame} is called. The function prologue then jumps
  // to the current {pc_offset()} to execute the OOL code for allocating the
  // large frame.

  // Emit the unconditional branch in the function prologue (from {offset} to
  // {pc_offset()}).
  patching_assembler.jmp_rel(pc_offset() - offset);
  DCHECK_GE(liftoff::kSubSpSize, patching_assembler.pc_offset());
  patching_assembler.Nop(liftoff::kSubSpSize - patching_assembler.pc_offset());

  // If the frame is bigger than the stack, we throw the stack overflow
  // exception unconditionally. Thereby we can avoid the integer overflow
  // check in the condition code.
  RecordComment("OOL: stack check for large frame");
  Label continuation;
  if (frame_size < v8_flags.stack_size * 1024) {
    movq(kScratchRegister,
         StackLimitAsOperand(StackLimitKind::kRealStackLimit));
    addq(kScratchRegister, Immediate(frame_size));
    cmpq(rsp, kScratchRegister);
    j(above_equal, &continuation, Label::kNear);
  }

  near_call(static_cast<intptr_t>(Builtin::kWasmStackOverflow),
            RelocInfo::WASM_STUB_CALL);
  // The call will not return; just define an empty safepoint.
  safepoint_table_builder->DefineSafepoint(this);
  AssertUnreachable(AbortReason::kUnexpectedReturnFromWasmTrap);

  bind(&continuation);

  // Now allocate the stack space. Note that this might do more than just
  // decrementing the SP; consult {MacroAssembler::AllocateStackSpace}.
  AllocateStackSpace(frame_size);

  // Jump back to the start of the function, from {pc_offset()} to
  // right after the reserved space for the {__ sub(sp, sp, framesize)} (which
  // is a branch now).
  int func_start_offset = offset + liftoff::kSubSpSize;
  jmp_rel(func_start_offset - pc_offset());
}

void LiftoffAssembler::FinishCode() {}

void LiftoffAssembler::AbortCompilation() {}

// static
constexpr int LiftoffAssembler::StaticStackFrameSize() {
  return kOSRTargetOffset;
}

int LiftoffAssembler::SlotSizeForType(ValueKind kind) {
  return value_kind_full_size(kind);
}

bool LiftoffAssembler::NeedsAlignment(ValueKind kind) {
  return is_reference(kind);
}

void LiftoffAssembler::CheckTierUp(int declared_func_index, int budget_used,
                                   Label* ool_label,
                                   const FreezeCacheState& frozen) {
  Register instance_data = cache_state_.cached_instance_data;
  if (instance_data == no_reg) {
    instance_data = kScratchRegister;
    LoadInstanceDataFromFrame(instance_data);
  }

  Register budget_array = kScratchRegister;  // Overwriting {instance_data}.
  constexpr int kArrayOffset = wasm::ObjectAccess::ToTagged(
      WasmTrustedInstanceData::kTieringBudgetArrayOffset);
  movq(budget_array, Operand{instance_data, kArrayOffset});

  int offset = kInt32Size * declared_func_index;
  subl(Operand{budget_array, offset}, Immediate(budget_used));
  j(negative, ool_label);
}

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value) {
  switch (value.type().kind()) {
    case kI32:
      if (value.to_i32() == 0) {
        xorl(reg.gp(), reg.gp());
      } else {
        movl(reg.gp(), Immediate(value.to_i32()));
      }
      break;
    case kI64:
      MacroAssembler::Move(reg.gp(), value.to_i64());
      break;
    case kF32:
      MacroAssembler::Move(reg.fp(), value.to_f32_boxed().get_bits());
      break;
    case kF64:
      MacroAssembler::Move(reg.fp(), value.to_f64_boxed().get_bits());
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::LoadInstanceDataFromFrame(Register dst) {
  movq(dst, liftoff::kInstanceDataOperand);
}

void LiftoffAssembler::LoadTrustedPointer(Register dst, Register src_addr,
                                          int offset, IndirectPointerTag tag) {
  LoadTrustedPointerField(dst, Operand{src_addr, offset}, tag,
                          kScratchRegister);
}

void LiftoffAssembler::LoadFromInstance(Register dst, Register instance,
                                        int offset, int size) {
  DCHECK_LE(0, offset);
  Operand src{instance, offset};
  switch (size) {
    case 1:
      movzxbl(dst, src);
      break;
    case 4:
      movl(dst, src);
      break;
    case 8:
      movq(dst, src);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void LiftoffAssembler::LoadTaggedPointerFromInstance(Register dst,
                                                     Register instance,
                                                     int offset) {
  DCHECK_LE(0, offset);
  LoadTaggedField(dst, Operand(instance, offset));
}

void LiftoffAssembler::SpillInstanceData(Register instance) {
  movq(liftoff::kInstanceDataOperand, instance);
}

void LiftoffAssembler::ResetOSRTarget() {
  movq(liftoff::kOSRTargetSlot, Immediate(0));
}

void LiftoffAssembler::LoadTaggedPointer(Register dst, Register src_addr,
                                         Register offset_reg,
                                         int32_t offset_imm,
                                         uint32_t* protected_load_pc,
                                         bool needs_shift) {
  DCHECK_GE(offset_imm, 0);
  if (offset_reg != no_reg) AssertZeroExtended(offset_reg);
  ScaleFactor scale_factor = !needs_shift             ? times_1
                             : COMPRESS_POINTERS_BOOL ? times_4
                                                      : times_8;
  Operand src_op =
      liftoff::GetMemOp(this, src_addr, offset_reg,
                        static_cast<uint32_t>(offset_imm), scale_factor);
  if (protected_load_pc) *protected_load_pc = pc_offset();
  LoadTaggedField(dst, src_op);
}

void LiftoffAssembler::LoadProtectedPointer(Register dst, Register src_addr,
                                            int32_t offset_imm) {
  DCHECK_LE(0, offset_imm);
  LoadProtectedPointerField(dst, Operand{src_addr, offset_imm});
}

void LiftoffAssembler::LoadFullPointer(Register dst, Register src_addr,
                                       int32_t offset_imm) {
  Operand src_op = liftoff::GetMemOp(this, src_addr, no_reg,
                                     static_cast<uint32_t>(offset_imm));
  movq(dst, src_op);
}

#ifdef V8_ENABLE_SANDBOX
void LiftoffAssembler::LoadCodeEntrypointViaCodePointer(Register dst,
                                                        Register src_addr,
                                                        int offset_imm) {
  Operand src_op = liftoff::GetMemOp(this, src_addr, no_reg,
                                     static_cast<uint32_t>(offset_imm));
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
  DCHECK_GE(offset_imm, 0);
  Operand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg,
                                     static_cast<uint32_t>(offset_imm));
  if (protected_store_pc) *protected_store_pc = pc_offset();
  StoreTaggedField(dst_op, src);

  if (skip_write_barrier || v8_flags.disable_write_barriers) return;

  // None of the code below uses the {kScratchRegister} (in particular the
  // {CallRecordWriteStubSaveRegisters} just emits a near call). Hence we can
  // use it as scratch register here.
  Label exit;
  CheckPageFlag(dst_addr, kScratchRegister,
                MemoryChunk::kPointersFromHereAreInterestingMask, zero, &exit,
                Label::kNear);
  JumpIfSmi(src, &exit, Label::kNear);
  CheckPageFlag(src, kScratchRegister,
                MemoryChunk::kPointersToHereAreInterestingMask, zero, &exit,
                Label::kNear);
  leaq(kScratchRegister, dst_op);

  CallRecordWriteStubSaveRegisters(dst_addr, kScratchRegister,
                                   SaveFPRegsMode::kSave,
                                   StubCallMode::kCallWasmRuntimeStub);
  bind(&exit);
}

void LiftoffAssembler::AtomicLoad(LiftoffRegister dst, Register src_addr,
                                  Register offset_reg, uintptr_t offset_imm,
                                  LoadType type, LiftoffRegList /* pinned */,
                                  bool i64_offset) {
  Load(dst, src_addr, offset_reg, offset_imm, type, nullptr, true, i64_offset);
}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uintptr_t offset_imm,
                            LoadType type, uint32_t* protected_load_pc,
                            bool /* is_load_mem */, bool i64_offset,
                            bool needs_shift) {
  if (offset_reg != no_reg && !i64_offset) AssertZeroExtended(offset_reg);
  static_assert(times_4 == 2);
  ScaleFactor scale_factor =
      needs_shift ? static_cast<ScaleFactor>(type.size_log_2()) : times_1;
  Operand src_op =
      liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm, scale_factor);
  if (protected_load_pc) *protected_load_pc = pc_offset();
  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI64Load8U:
      movzxbl(dst.gp(), src_op);
      break;
    case LoadType::kI32Load8S:
      movsxbl(dst.gp(), src_op);
      break;
    case LoadType::kI64Load8S:
      movsxbq(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U:
      movzxwl(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16S:
      movsxwl(dst.gp(), src_op);
      break;
    case LoadType::kI64Load16S:
      movsxwq(dst.gp(), src_op);
      break;
    case LoadType::kI32Load:
    case LoadType::kI64Load32U:
      movl(dst.gp(), src_op);
      break;
    case LoadType::kI64Load32S:
      movsxlq(dst.gp(), src_op);
      break;
    case LoadType::kI64Load:
      movq(dst.gp(), src_op);
      break;
    case LoadType::kF32Load:
      Movss(dst.fp(), src_op);
      break;
    case LoadType::kF32LoadF16: {
      CpuFeatureScope f16c_scope(this, F16C);
      CpuFeatureScope avx2_scope(this, AVX2);
      vpbroadcastw(dst.fp(), src_op);
      vcvtph2ps(dst.fp(), dst.fp());
      break;
    }
    case LoadType::kF64Load:
      Movsd(dst.fp(), src_op);
      break;
    case LoadType::kS128Load:
      Movdqu(dst.fp(), src_op);
      break;
  }
}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uintptr_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList /* pinned */,
                             uint32_t* protected_store_pc,
                             bool /* is_store_mem */, bool i64_offset) {
  if (offset_reg != no_reg && !i64_offset) AssertZeroExtended(offset_reg);
  Operand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);
  if (protected_store_pc) *protected_store_pc = pc_offset();
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      movb(dst_op, src.gp());
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      movw(dst_op, src.gp());
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      movl(dst_op, src.gp());
      break;
    case StoreType::kI64Store:
      movq(dst_op, src.gp());
      break;
    case StoreType::kF32Store:
      Movss(dst_op, src.fp());
      break;
    case StoreType::kF32StoreF16: {
      CpuFeatureScope fscope(this, F16C);
      vcvtps2ph(kScratchDoubleReg, src.fp(), 0);
      Pextrw(dst_op, kScratchDoubleReg, static_cast<uint8_t>(0));
      break;
    }
    case StoreType::kF64Store:
      Movsd(dst_op, src.fp());
      break;
    case StoreType::kS128Store:
      Movdqu(dst_op, src.fp());
      break;
  }
}

void LiftoffAssembler::AtomicStore(Register dst_addr, Register offset_reg,
                                   uintptr_t offset_imm, LiftoffRegister src,
                                   StoreType type, LiftoffRegList /* pinned */,
                                   bool i64_offset) {
  if (offset_reg != no_reg && !i64_offset) AssertZeroExtended(offset_reg);
  Operand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);
  Register src_reg = src.gp();
  if (cache_state()->is_used(src)) {
    movq(kScratchRegister, src_reg);
    src_reg = kScratchRegister;
  }
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      xchgb(src_reg, dst_op);
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      xchgw(src_reg, dst_op);
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      xchgl(src_reg, dst_op);
      break;
    case StoreType::kI64Store:
      xchgq(src_reg, dst_op);
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicAdd(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  if (offset_reg != no_reg && !i64_offset) AssertZeroExtended(offset_reg);
  DCHECK(!cache_state()->is_used(result));
  if (cache_state()->is_used(value)) {
    // We cannot overwrite {value}, but the {value} register is changed in the
    // code we generate. Therefore we copy {value} to {result} and use the
    // {result} register in the code below.
    movq(result.gp(), value.gp());
    value = result;
  }
  Operand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);
  lock();
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      xaddb(dst_op, value.gp());
      movzxbq(result.gp(), value.gp());
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      xaddw(dst_op, value.gp());
      movzxwq(result.gp(), value.gp());
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      xaddl(dst_op, value.gp());
      if (value != result) {
        movq(result.gp(), value.gp());
      }
      break;
    case StoreType::kI64Store:
      xaddq(dst_op, value.gp());
      if (value != result) {
        movq(result.gp(), value.gp());
      }
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicSub(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  if (offset_reg != no_reg && !i64_offset) AssertZeroExtended(offset_reg);
  LiftoffRegList dont_overwrite =
      cache_state()->used_registers | LiftoffRegList{dst_addr};
  if (offset_reg != no_reg) dont_overwrite.set(offset_reg);
  DCHECK(!dont_overwrite.has(result));
  if (dont_overwrite.has(value)) {
    // We cannot overwrite {value}, but the {value} register is changed in the
    // code we generate. Therefore we copy {value} to {result} and use the
    // {result} register in the code below.
    movq(result.gp(), value.gp());
    value = result;
  }
  Operand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      negb(value.gp());
      lock();
      xaddb(dst_op, value.gp());
      movzxbq(result.gp(), value.gp());
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      negw(value.gp());
      lock();
      xaddw(dst_op, value.gp());
      movzxwq(result.gp(), value.gp());
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      negl(value.gp());
      lock();
      xaddl(dst_op, value.gp());
      if (value != result) {
        movq(result.gp(), value.gp());
      }
      break;
    case StoreType::kI64Store:
      negq(value.gp());
      lock();
      xaddq(dst_op, value.gp());
      if (value != result) {
        movq(result.gp(), value.gp());
      }
      break;
    default:
      UNREACHABLE();
  }
}

namespace liftoff {
#define __ lasm->

inline void AtomicBinop(LiftoffAssembler* lasm,
                        void (Assembler::*opl)(Register, Register),
                        void (Assembler::*opq)(Register, Register),
                        Register dst_addr, Register offset_reg,
                        uintptr_t offset_imm, LiftoffRegister value,
                        LiftoffRegister result, StoreType type,
                        bool i64_offset) {
  if (offset_reg != no_reg && !i64_offset) __ AssertZeroExtended(offset_reg);
  DCHECK(!__ cache_state()->is_used(result));
  Register value_reg = value.gp();
  // The cmpxchg instruction uses rax to store the old value of the
  // compare-exchange primitive. Therefore we have to spill the register and
  // move any use to another register.
  LiftoffRegList pinned = LiftoffRegList{dst_addr, value_reg};
  if (offset_reg != no_reg) pinned.set(offset_reg);
  __ ClearRegister(rax, {&dst_addr, &offset_reg, &value_reg}, pinned);
  Operand dst_op = liftoff::GetMemOp(lasm, dst_addr, offset_reg, offset_imm);

  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8: {
      Label binop;
      __ xorq(rax, rax);
      __ movb(rax, dst_op);
      __ bind(&binop);
      __ movl(kScratchRegister, rax);
      (lasm->*opl)(kScratchRegister, value_reg);
      __ lock();
      __ cmpxchgb(dst_op, kScratchRegister);
      __ j(not_equal, &binop);
      break;
    }
    case StoreType::kI32Store16:
    case StoreType::kI64Store16: {
      Label binop;
      __ xorq(rax, rax);
      __ movw(rax, dst_op);
      __ bind(&binop);
      __ movl(kScratchRegister, rax);
      (lasm->*opl)(kScratchRegister, value_reg);
      __ lock();
      __ cmpxchgw(dst_op, kScratchRegister);
      __ j(not_equal, &binop);
      break;
    }
    case StoreType::kI32Store:
    case StoreType::kI64Store32: {
      Label binop;
      __ movl(rax, dst_op);
      __ bind(&binop);
      __ movl(kScratchRegister, rax);
      (lasm->*opl)(kScratchRegister, value_reg);
      __ lock();
      __ cmpxchgl(dst_op, kScratchRegister);
      __ j(not_equal, &binop);
      break;
    }
    case StoreType::kI64Store: {
      Label binop;
      __ movq(rax, dst_op);
      __ bind(&binop);
      __ movq(kScratchRegister, rax);
      (lasm->*opq)(kScratchRegister, value_reg);
      __ lock();
      __ cmpxchgq(dst_op, kScratchRegister);
      __ j(not_equal, &binop);
      break;
    }
    default:
      UNREACHABLE();
  }

  if (result.gp() != rax) {
    __ movq(result.gp(), rax);
  }
}
#undef __
}  // namespace liftoff

void LiftoffAssembler::AtomicAnd(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  liftoff::AtomicBinop(this, &Assembler::andl, &Assembler::andq, dst_addr,
                       offset_reg, offset_imm, value, result, type, i64_offset);
}

void LiftoffAssembler::AtomicOr(Register dst_addr, Register offset_reg,
                                uintptr_t offset_imm, LiftoffRegister value,
                                LiftoffRegister result, StoreType type,
                                bool i64_offset) {
  liftoff::AtomicBinop(this, &Assembler::orl, &Assembler::orq, dst_addr,
                       offset_reg, offset_imm, value, result, type, i64_offset);
}

void LiftoffAssembler::AtomicXor(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  liftoff::AtomicBinop(this, &Assembler::xorl, &Assembler::xorq, dst_addr,
                       offset_reg, offset_imm, value, result, type, i64_offset);
}

void LiftoffAssembler::AtomicExchange(Register dst_addr, Register offset_reg,
                                      uintptr_t offset_imm,
                                      LiftoffRegister value,
                                      LiftoffRegister result, StoreType type,
                                      bool i64_offset) {
  if (offset_reg != no_reg && !i64_offset) AssertZeroExtended(offset_reg);
  DCHECK(!cache_state()->is_used(result));
  if (cache_state()->is_used(value)) {
    // We cannot overwrite {value}, but the {value} register is changed in the
    // code we generate. Therefore we copy {value} to {result} and use the
    // {result} register in the code below.
    movq(result.gp(), value.gp());
    value = result;
  }
  Operand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      xchgb(value.gp(), dst_op);
      movzxbq(result.gp(), value.gp());
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      xchgw(value.gp(), dst_op);
      movzxwq(result.gp(), value.gp());
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      xchgl(value.gp(), dst_op);
      if (value != result) {
        movq(result.gp(), value.gp());
      }
      break;
    case StoreType::kI64Store:
      xchgq(value.gp(), dst_op);
      if (value != result) {
        movq(result.gp(), value.gp());
      }
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicCompareExchange(
    Register dst_addr, Register offset_reg, uintptr_t offset_imm,
    LiftoffRegister expected, LiftoffRegister new_value, LiftoffRegister result,
    StoreType type, bool i64_offset) {
  if (offset_reg != no_reg && !i64_offset) AssertZeroExtended(offset_reg);
  Register value_reg = new_value.gp();
  // The cmpxchg instruction uses rax to store the old value of the
  // compare-exchange primitive. Therefore we have to spill the register and
  // move any use to another register.
  LiftoffRegList pinned = LiftoffRegList{dst_addr, expected, value_reg};
  if (offset_reg != no_reg) pinned.set(offset_reg);
  ClearRegister(rax, {&dst_addr, &offset_reg, &value_reg}, pinned);
  if (expected.gp() != rax) {
    movq(rax, expected.gp());
  }

  Operand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);

  lock();
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8: {
      cmpxchgb(dst_op, value_reg);
      movzxbq(result.gp(), rax);
      break;
    }
    case StoreType::kI32Store16:
    case StoreType::kI64Store16: {
      cmpxchgw(dst_op, value_reg);
      movzxwq(result.gp(), rax);
      break;
    }
    case StoreType::kI32Store: {
      cmpxchgl(dst_op, value_reg);
      if (result.gp() != rax) {
        movl(result.gp(), rax);
      }
      break;
    }
    case StoreType::kI64Store32: {
      cmpxchgl(dst_op, value_reg);
      // Zero extension.
      movl(result.gp(), rax);
      break;
    }
    case StoreType::kI64Store: {
      cmpxchgq(dst_op, value_reg);
      if (result.gp() != rax) {
        movq(result.gp(), rax);
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicFence() { mfence(); }

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx,
                                           ValueKind kind) {
  Operand src(rbp, kSystemPointerSize * (caller_slot_idx + 1));
  liftoff::LoadFromStack(this, dst, src, kind);
}

void LiftoffAssembler::StoreCallerFrameSlot(LiftoffRegister src,
                                            uint32_t caller_slot_idx,
                                            ValueKind kind) {
  Operand dst(rbp, kSystemPointerSize * (caller_slot_idx + 1));
  liftoff::StoreToMemory(this, dst, src, kind);
}

void LiftoffAssembler::LoadReturnStackSlot(LiftoffRegister reg, int offset,
                                           ValueKind kind) {
  Operand src(rsp, offset);
  liftoff::LoadFromStack(this, reg, src, kind);
}

void LiftoffAssembler::MoveStackValue(uint32_t dst_offset, uint32_t src_offset,
                                      ValueKind kind) {
  DCHECK_NE(dst_offset, src_offset);
  Operand dst = liftoff::GetStackSlot(dst_offset);
  Operand src = liftoff::GetStackSlot(src_offset);
  switch (SlotSizeForType(kind)) {
    case 4:
      movl(kScratchRegister, src);
      movl(dst, kScratchRegister);
      break;
    case 8:
      movq(kScratchRegister, src);
      movq(dst, kScratchRegister);
      break;
    case 16:
      Movdqu(kScratchDoubleReg, src);
      Movdqu(dst, kScratchDoubleReg);
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Move(Register dst, Register src, ValueKind kind) {
  DCHECK_NE(dst, src);
  if (kind == kI32) {
    movl(dst, src);
  } else {
    DCHECK(kI64 == kind || is_reference(kind));
    movq(dst, src);
  }
}

void LiftoffAssembler::Move(DoubleRegister dst, DoubleRegister src,
                            ValueKind kind) {
  DCHECK_NE(dst, src);
  if (kind == kF32) {
    Movss(dst, src);
  } else if (kind == kF64) {
    Movsd(dst, src);
  } else {
    DCHECK_EQ(kS128, kind);
    Movapd(dst, src);
  }
}

void LiftoffAssembler::Spill(int offset, LiftoffRegister reg, ValueKind kind) {
  RecordUsedSpillOffset(offset);
  Operand dst = liftoff::GetStackSlot(offset);
  switch (kind) {
    case kI32:
      movl(dst, reg.gp());
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      movq(dst, reg.gp());
      break;
    case kF32:
      Movss(dst, reg.fp());
      break;
    case kF64:
      Movsd(dst, reg.fp());
      break;
    case kS128:
      Movdqu(dst, reg.fp());
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Spill(int offset, WasmValue value) {
  RecordUsedSpillOffset(offset);
  Operand dst = liftoff::GetStackSlot(offset);
  switch (value.type().kind()) {
    case kI32:
      movl(dst, Immediate(value.to_i32()));
      break;
    case kI64: {
      if (is_int32(value.to_i64())) {
        // Sign extend low word.
        movq(dst, Immediate(static_cast<int32_t>(value.to_i64())));
      } else if (is_uint32(value.to_i64())) {
        // Zero extend low word.
        movl(kScratchRegister, Immediate(static_cast<int32_t>(value.to_i64())));
        movq(dst, kScratchRegister);
      } else {
        movq(kScratchRegister, value.to_i64());
        movq(dst, kScratchRegister);
      }
      break;
    }
    default:
      // We do not track f32 and f64 constants, hence they are unreachable.
      UNREACHABLE();
  }
}

void LiftoffAssembler::Fill(LiftoffRegister reg, int offset, ValueKind kind) {
  liftoff::LoadFromStack(this, reg, liftoff::GetStackSlot(offset), kind);
}

void LiftoffAssembler::FillI64Half(Register, int offset, RegPairHalf) {
  UNREACHABLE();
}

void LiftoffAssembler::FillStackSlotsWithZero(int start, int size) {
  DCHECK_LT(0, size);
  RecordUsedSpillOffset(start + size);

  if (size <= 3 * kStackSlotSize) {
    // Special straight-line code for up to three slots
    // (7-10 bytes per slot: REX C7 <1-4 bytes op> <4 bytes imm>),
    // And a movd (6-9 byte) when size % 8 != 0;
    uint32_t remainder = size;
    for (; remainder >= kStackSlotSize; remainder -= kStackSlotSize) {
      movq(liftoff::GetStackSlot(start + remainder), Immediate(0));
    }
    DCHECK(remainder == 4 || remainder == 0);
    if (remainder) {
      movl(liftoff::GetStackSlot(start + remainder), Immediate(0));
    }
  } else {
    // General case for bigger counts.
    // This sequence takes 19-22 bytes (3 for pushes, 4-7 for lea, 2 for xor, 5
    // for mov, 2 for repstosl, 3 for pops).
    pushq(rax);
    pushq(rcx);
    pushq(rdi);
    leaq(rdi, liftoff::GetStackSlot(start + size));
    xorl(rax, rax);
    // Convert size (bytes) to doublewords (4-bytes).
    movl(rcx, Immediate(size / 4));
    repstosl();
    popq(rdi);
    popq(rcx);
    popq(rax);
  }
}

void LiftoffAssembler::LoadSpillAddress(Register dst, int offset,
                                        ValueKind /* kind */) {
  leaq(dst, liftoff::GetStackSlot(offset));
}

void LiftoffAssembler::emit_trace_instruction(uint32_t markid) {
  Assembler::emit_trace_instruction(Immediate(markid));
}

void LiftoffAssembler::emit_i32_add(Register dst, Register lhs, Register rhs) {
  if (lhs != dst) {
    leal(dst, Operand(lhs, rhs, times_1, 0));
  } else {
    addl(dst, rhs);
  }
}

void LiftoffAssembler::emit_i32_addi(Register dst, Register lhs, int32_t imm) {
  if (lhs != dst) {
    leal(dst, Operand(lhs, imm));
  } else {
    addl(dst, Immediate(imm));
  }
}

void LiftoffAssembler::emit_i32_sub(Register dst, Register lhs, Register rhs) {
  if (dst != rhs) {
    // Default path.
    if (dst != lhs) movl(dst, lhs);
    subl(dst, rhs);
  } else if (lhs == rhs) {
    // Degenerate case.
    xorl(dst, dst);
  } else {
    // Emit {dst = lhs + -rhs} if dst == rhs.
    negl(dst);
    addl(dst, lhs);
  }
}

void LiftoffAssembler::emit_i32_subi(Register dst, Register lhs, int32_t imm) {
  if (dst != lhs) {
    // We'll have to implement an UB-safe version if we need this corner case.
    DCHECK_NE(imm, kMinInt);
    leal(dst, Operand(lhs, -imm));
  } else {
    subl(dst, Immediate(imm));
  }
}

namespace liftoff {
template <void (Assembler::*op)(Register, Register),
          void (Assembler::*mov)(Register, Register)>
void EmitCommutativeBinOp(LiftoffAssembler* assm, Register dst, Register lhs,
                          Register rhs) {
  if (dst == rhs) {
    (assm->*op)(dst, lhs);
  } else {
    if (dst != lhs) (assm->*mov)(dst, lhs);
    (assm->*op)(dst, rhs);
  }
}

template <void (Assembler::*op)(Register, Immediate),
          void (Assembler::*mov)(Register, Register)>
void EmitCommutativeBinOpImm(LiftoffAssembler* assm, Register dst, Register lhs,
                             int32_t imm) {
  if (dst != lhs) (assm->*mov)(dst, lhs);
  (assm->*op)(dst, Immediate(imm));
}

}  // namespace liftoff

void LiftoffAssembler::emit_i32_mul(Register dst, Register lhs, Register rhs) {
  liftoff::EmitCommutativeBinOp<&Assembler::imull, &Assembler::movl>(this, dst,
                                                                     lhs, rhs);
}

namespace liftoff {
enum class DivOrRem : uint8_t { kDiv, kRem };
template <typename type, DivOrRem div_or_rem>
void EmitIntDivOrRem(LiftoffAssembler* assm, Register dst, Register lhs,
                     Register rhs, Label* trap_div_by_zero,
                     Label* trap_div_unrepresentable) {
  constexpr bool needs_unrepresentable_check =
      std::is_signed<type>::value && div_or_rem == DivOrRem::kDiv;
  constexpr bool special_case_minus_1 =
      std::is_signed<type>::value && div_or_rem == DivOrRem::kRem;
  DCHECK_EQ(needs_unrepresentable_check, trap_div_unrepresentable != nullptr);

#define iop(name, ...)            \
  do {                            \
    if (sizeof(type) == 4) {      \
      assm->name##l(__VA_ARGS__); \
    } else {                      \
      assm->name##q(__VA_ARGS__); \
    }                             \
  } while (false)

  // For division, the lhs is always taken from {edx:eax}. Thus, make sure that
  // these registers are unused. If {rhs} is stored in one of them, move it to
  // another temporary register.
  // Do all this before any branch, such that the code is executed
  // unconditionally, as the cache state will also be modified unconditionally.
  assm->SpillRegisters(rdx, rax);
  if (rhs == rax || rhs == rdx) {
    iop(mov, kScratchRegister, rhs);
    rhs = kScratchRegister;
  }

  // Check for division by zero.
  iop(test, rhs, rhs);
  assm->j(zero, trap_div_by_zero);

  Label done;
  if (needs_unrepresentable_check) {
    // Check for {kMinInt / -1}. This is unrepresentable.
    Label do_div;
    iop(cmp, rhs, Immediate(-1));
    assm->j(not_equal, &do_div);
    // {lhs} is min int if {lhs - 1} overflows.
    iop(cmp, lhs, Immediate(1));
    assm->j(overflow, trap_div_unrepresentable);
    assm->bind(&do_div);
  } else if (special_case_minus_1) {
    // {lhs % -1} is always 0 (needs to be special cased because {kMinInt / -1}
    // cannot be computed).
    Label do_rem;
    iop(cmp, rhs, Immediate(-1));
    assm->j(not_equal, &do_rem);
    // clang-format off
    // (conflicts with presubmit checks because it is confused about "xor")
    iop(xor, dst, dst);
    // clang-format on
    assm->jmp(&done);
    assm->bind(&do_rem);
  }

  // Now move {lhs} into {eax}, then zero-extend or sign-extend into {edx}, then
  // do the division.
  if (lhs != rax) iop(mov, rax, lhs);
  if (std::is_same<int32_t, type>::value) {  // i32
    assm->cdq();
    assm->idivl(rhs);
  } else if (std::is_same<uint32_t, type>::value) {  // u32
    assm->xorl(rdx, rdx);
    assm->divl(rhs);
  } else if (std::is_same<int64_t, type>::value) {  // i64
    assm->cqo();
    assm->idivq(rhs);
  } else {  // u64
    assm->xorq(rdx, rdx);
    assm->divq(rhs);
  }

  // Move back the result (in {eax} or {edx}) into the {dst} register.
  constexpr Register kResultReg = div_or_rem == DivOrRem::kDiv ? rax : rdx;
  if (dst != kResultReg) {
    iop(mov, dst, kResultReg);
  }
  if (special_case_minus_1) assm->bind(&done);
}
}  // namespace liftoff

void LiftoffAssembler::emit_i32_divs(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  liftoff::EmitIntDivOrRem<int32_t, liftoff::DivOrRem::kDiv>(
      this, dst, lhs, rhs, trap_div_by_zero, trap_div_unrepresentable);
}

void LiftoffAssembler::emit_i32_divu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  liftoff::EmitIntDivOrRem<uint32_t, liftoff::DivOrRem::kDiv>(
      this, dst, lhs, rhs, trap_div_by_zero, nullptr);
}

void LiftoffAssembler::emit_i32_rems(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  liftoff::EmitIntDivOrRem<int32_t, liftoff::DivOrRem::kRem>(
      this, dst, lhs, rhs, trap_div_by_zero, nullptr);
}

void LiftoffAssembler::emit_i32_remu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  liftoff::EmitIntDivOrRem<uint32_t, liftoff::DivOrRem::kRem>(
      this, dst, lhs, rhs, trap_div_by_zero, nullptr);
}

void LiftoffAssembler::emit_i32_and(Register dst, Register lhs, Register rhs) {
  liftoff::EmitCommutativeBinOp<&Assembler::andl, &Assembler::movl>(this, dst,
                                                                    lhs, rhs);
}

void LiftoffAssembler::emit_i32_andi(Register dst, Register lhs, int32_t imm) {
  liftoff::EmitCommutativeBinOpImm<&Assembler::andl, &Assembler::movl>(
      this, dst, lhs, imm);
}

void LiftoffAssembler::emit_i32_or(Register dst, Register lhs, Register rhs) {
  liftoff::EmitCommutativeBinOp<&Assembler::orl, &Assembler::movl>(this, dst,
                                                                   lhs, rhs);
}

void LiftoffAssembler::emit_i32_ori(Register dst, Register lhs, int32_t imm) {
  liftoff::EmitCommutativeBinOpImm<&Assembler::orl, &Assembler::movl>(this, dst,
                                                                      lhs, imm);
}

void LiftoffAssembler::emit_i32_xor(Register dst, Register lhs, Register rhs) {
  liftoff::EmitCommutativeBinOp<&Assembler::xorl, &Assembler::movl>(this, dst,
                                                                    lhs, rhs);
}

void LiftoffAssembler::emit_i32_xori(Register dst, Register lhs, int32_t imm) {
  liftoff::EmitCommutativeBinOpImm<&Assembler::xorl, &Assembler::movl>(
      this, dst, lhs, imm);
}

namespace liftoff {
template <ValueKind kind>
inline void EmitShiftOperation(LiftoffAssembler* assm, Register dst,
                               Register src, Register amount,
                               void (Assembler::*emit_shift)(Register)) {
  // If dst is rcx, compute into the scratch register first, then move to rcx.
  if (dst == rcx) {
    assm->Move(kScratchRegister, src, kind);
    if (amount != rcx) assm->Move(rcx, amount, kind);
    (assm->*emit_shift)(kScratchRegister);
    assm->Move(rcx, kScratchRegister, kind);
    return;
  }

  // Move amount into rcx. If rcx is in use, move its content into the scratch
  // register. If src is rcx, src is now the scratch register.
  bool use_scratch = false;
  if (amount != rcx) {
    use_scratch =
        src == rcx || assm->cache_state()->is_used(LiftoffRegister(rcx));
    if (use_scratch) assm->movq(kScratchRegister, rcx);
    if (src == rcx) src = kScratchRegister;
    assm->Move(rcx, amount, kind);
  }

  // Do the actual shift.
  if (dst != src) assm->Move(dst, src, kind);
  (assm->*emit_shift)(dst);

  // Restore rcx if needed.
  if (use_scratch) assm->movq(rcx, kScratchRegister);
}
}  // namespace liftoff

void LiftoffAssembler::emit_i32_shl(Register dst, Register src,
                                    Register amount) {
  liftoff::EmitShiftOperation<kI32>(this, dst, src, amount,
                                    &Assembler::shll_cl);
}

void LiftoffAssembler::emit_i32_shli(Register dst, Register src,
                                     int32_t amount) {
  if (dst != src) movl(dst, src);
  shll(dst, Immediate(amount & 31));
}

void LiftoffAssembler::emit_i32_sar(Register dst, Register src,
                                    Register amount) {
  liftoff::EmitShiftOperation<kI32>(this, dst, src, amount,
                                    &Assembler::sarl_cl);
}

void LiftoffAssembler::emit_i32_sari(Register dst, Register src,
                                     int32_t amount) {
  if (dst != src) movl(dst, src);
  sarl(dst, Immediate(amount & 31));
}

void LiftoffAssembler::emit_i32_shr(Register dst, Register src,
                                    Register amount) {
  liftoff::EmitShiftOperation<kI32>(this, dst, src, amount,
                                    &Assembler::shrl_cl);
}

void LiftoffAssembler::emit_i32_shri(Register dst, Register src,
                                     int32_t amount) {
  if (dst != src) movl(dst, src);
  shrl(dst, Immediate(amount & 31));
}

void LiftoffAssembler::emit_i32_clz(Register dst, Register src) {
  Lzcntl(dst, src);
}

void LiftoffAssembler::emit_i32_ctz(Register dst, Register src) {
  Tzcntl(dst, src);
}

bool LiftoffAssembler::emit_i32_popcnt(Register dst, Register src) {
  if (!CpuFeatures::IsSupported(POPCNT)) return false;
  CpuFeatureScope scope(this, POPCNT);
  popcntl(dst, src);
  return true;
}

void LiftoffAssembler::emit_i64_add(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  if (lhs.gp() != dst.gp()) {
    leaq(dst.gp(), Operand(lhs.gp(), rhs.gp(), times_1, 0));
  } else {
    addq(dst.gp(), rhs.gp());
  }
}

void LiftoffAssembler::emit_i64_addi(LiftoffRegister dst, LiftoffRegister lhs,
                                     int64_t imm) {
  if (!is_int32(imm)) {
    MacroAssembler::Move(kScratchRegister, imm);
    if (lhs.gp() == dst.gp()) {
      addq(dst.gp(), kScratchRegister);
    } else {
      leaq(dst.gp(), Operand(lhs.gp(), kScratchRegister, times_1, 0));
    }
  } else if (lhs.gp() == dst.gp()) {
    addq(dst.gp(), Immediate(static_cast<int32_t>(imm)));
  } else {
    leaq(dst.gp(), Operand(lhs.gp(), static_cast<int32_t>(imm)));
  }
}

void LiftoffAssembler::emit_i64_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  if (lhs.gp() == rhs.gp()) {
    xorq(dst.gp(), dst.gp());
  } else if (dst.gp() == rhs.gp()) {
    negq(dst.gp());
    addq(dst.gp(), lhs.gp());
  } else {
    if (dst.gp() != lhs.gp()) movq(dst.gp(), lhs.gp());
    subq(dst.gp(), rhs.gp());
  }
}

void LiftoffAssembler::emit_i64_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  liftoff::EmitCommutativeBinOp<&Assembler::imulq, &Assembler::movq>(
      this, dst.gp(), lhs.gp(), rhs.gp());
}

void LiftoffAssembler::emit_i64_muli(LiftoffRegister dst, LiftoffRegister lhs,
                                     int32_t imm) {
  if (base::bits::IsPowerOfTwo(imm)) {
    emit_i64_shli(dst, lhs, base::bits::WhichPowerOfTwo(imm));
  } else {
    imulq(dst.gp(), lhs.gp(), Immediate{imm});
  }
}

bool LiftoffAssembler::emit_i64_divs(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  liftoff::EmitIntDivOrRem<int64_t, liftoff::DivOrRem::kDiv>(
      this, dst.gp(), lhs.gp(), rhs.gp(), trap_div_by_zero,
      trap_div_unrepresentable);
  return true;
}

bool LiftoffAssembler::emit_i64_divu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  liftoff::EmitIntDivOrRem<uint64_t, liftoff::DivOrRem::kDiv>(
      this, dst.gp(), lhs.gp(), rhs.gp(), trap_div_by_zero, nullptr);
  return true;
}

bool LiftoffAssembler::emit_i64_rems(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  liftoff::EmitIntDivOrRem<int64_t, liftoff::DivOrRem::kRem>(
      this, dst.gp(), lhs.gp(), rhs.gp(), trap_div_by_zero, nullptr);
  return true;
}

bool LiftoffAssembler::emit_i64_remu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  liftoff::EmitIntDivOrRem<uint64_t, liftoff::DivOrRem::kRem>(
      this, dst.gp(), lhs.gp(), rhs.gp(), trap_div_by_zero, nullptr);
  return true;
}

void LiftoffAssembler::emit_i64_and(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  liftoff::EmitCommutativeBinOp<&Assembler::andq, &Assembler::movq>(
      this, dst.gp(), lhs.gp(), rhs.gp());
}

void LiftoffAssembler::emit_i64_andi(LiftoffRegister dst, LiftoffRegister lhs,
                                     int32_t imm) {
  liftoff::EmitCommutativeBinOpImm<&Assembler::andq, &Assembler::movq>(
      this, dst.gp(), lhs.gp(), imm);
}

void LiftoffAssembler::emit_i64_or(LiftoffRegister dst, LiftoffRegister lhs,
                                   LiftoffRegister rhs) {
  liftoff::EmitCommutativeBinOp<&Assembler::orq, &Assembler::movq>(
      this, dst.gp(), lhs.gp(), rhs.gp());
}

void LiftoffAssembler::emit_i64_ori(LiftoffRegister dst, LiftoffRegister lhs,
                                    int32_t imm) {
  liftoff::EmitCommutativeBinOpImm<&Assembler::orq, &Assembler::movq>(
      this, dst.gp(), lhs.gp(), imm);
}

void LiftoffAssembler::emit_i64_xor(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  liftoff::EmitCommutativeBinOp<&Assembler::xorq, &Assembler::movq>(
      this, dst.gp(), lhs.gp(), rhs.gp());
}

void LiftoffAssembler::emit_i64_xori(LiftoffRegister dst, LiftoffRegister lhs,
                                     int32_t imm) {
  liftoff::EmitCommutativeBinOpImm<&Assembler::xorq, &Assembler::movq>(
      this, dst.gp(), lhs.gp(), imm);
}

void LiftoffAssembler::emit_i64_shl(LiftoffRegister dst, LiftoffRegister src,
                                    Register amount) {
  liftoff::EmitShiftOperation<kI64>(this, dst.gp(), src.gp(), amount,
                                    &Assembler::shlq_cl);
}

void LiftoffAssembler::emit_i64_shli(LiftoffRegister dst, LiftoffRegister src,
                                     int32_t amount) {
  if (dst.gp() != src.gp()) movq(dst.gp(), src.gp());
  shlq(dst.gp(), Immediate(amount & 63));
}

void LiftoffAssembler::emit_i64_sar(LiftoffRegister dst, LiftoffRegister src,
                                    Register amount) {
  liftoff::EmitShiftOperation<kI64>(this, dst.gp(), src.gp(), amount,
                                    &Assembler::sarq_cl);
}

void LiftoffAssembler::emit_i64_sari(LiftoffRegister dst, LiftoffRegister src,
                                     int32_t amount) {
  if (dst.gp() != src.gp()) movq(dst.gp(), src.gp());
  sarq(dst.gp(), Immediate(amount & 63));
}

void LiftoffAssembler::emit_i64_shr(LiftoffRegister dst, LiftoffRegister src,
                                    Register amount) {
  liftoff::EmitShiftOperation<kI64>(this, dst.gp(), src.gp(), amount,
                                    &Assembler::shrq_cl);
}

void LiftoffAssembler::emit_i64_shri(LiftoffRegister dst, LiftoffRegister src,
                                     int32_t amount) {
  if (dst != src) movq(dst.gp(), src.gp());
  shrq(dst.gp(), Immediate(amount & 63));
}

void LiftoffAssembler::emit_i64_clz(LiftoffRegister dst, LiftoffRegister src) {
  Lzcntq(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_ctz(LiftoffRegister dst, LiftoffRegister src) {
  Tzcntq(dst.gp(), src.gp());
}

bool LiftoffAssembler::emit_i64_popcnt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(POPCNT)) return false;
  CpuFeatureScope scope(this, POPCNT);
  popcntq(dst.gp(), src.gp());
  return true;
}

void LiftoffAssembler::IncrementSmi(LiftoffRegister dst, int offset) {
  SmiAddConstant(Operand(dst.gp(), offset), Smi::FromInt(1));
}

void LiftoffAssembler::emit_u32_to_uintptr(Register dst, Register src) {
  AssertZeroExtended(src);
  if (dst != src) movl(dst, src);
}

void LiftoffAssembler::clear_i32_upper_half(Register dst) { movl(dst, dst); }

void LiftoffAssembler::emit_f32_add(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vaddss(dst, lhs, rhs);
  } else if (dst == rhs) {
    addss(dst, lhs);
  } else {
    if (dst != lhs) movss(dst, lhs);
    addss(dst, rhs);
  }
}

void LiftoffAssembler::emit_f32_sub(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vsubss(dst, lhs, rhs);
  } else if (dst == rhs) {
    movss(kScratchDoubleReg, rhs);
    movss(dst, lhs);
    subss(dst, kScratchDoubleReg);
  } else {
    if (dst != lhs) movss(dst, lhs);
    subss(dst, rhs);
  }
}

void LiftoffAssembler::emit_f32_mul(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vmulss(dst, lhs, rhs);
  } else if (dst == rhs) {
    mulss(dst, lhs);
  } else {
    if (dst != lhs) movss(dst, lhs);
    mulss(dst, rhs);
  }
}

void LiftoffAssembler::emit_f32_div(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vdivss(dst, lhs, rhs);
  } else if (dst == rhs) {
    movss(kScratchDoubleReg, rhs);
    movss(dst, lhs);
    divss(dst, kScratchDoubleReg);
  } else {
    if (dst != lhs) movss(dst, lhs);
    divss(dst, rhs);
  }
}

namespace liftoff {
enum class MinOrMax : uint8_t { kMin, kMax };
template <typename type>
inline void EmitFloatMinOrMax(LiftoffAssembler* assm, DoubleRegister dst,
                              DoubleRegister lhs, DoubleRegister rhs,
                              MinOrMax min_or_max) {
  Label is_nan;
  Label lhs_below_rhs;
  Label lhs_above_rhs;
  Label done;

#define dop(name, ...)            \
  do {                            \
    if (sizeof(type) == 4) {      \
      assm->name##s(__VA_ARGS__); \
    } else {                      \
      assm->name##d(__VA_ARGS__); \
    }                             \
  } while (false)

  // Check the easy cases first: nan (e.g. unordered), smaller and greater.
  // NaN has to be checked first, because PF=1 implies CF=1.
  dop(Ucomis, lhs, rhs);
  assm->j(parity_even, &is_nan, Label::kNear);   // PF=1
  assm->j(below, &lhs_below_rhs, Label::kNear);  // CF=1
  assm->j(above, &lhs_above_rhs, Label::kNear);  // CF=0 && ZF=0

  // If we get here, then either
  // a) {lhs == rhs},
  // b) {lhs == -0.0} and {rhs == 0.0}, or
  // c) {lhs == 0.0} and {rhs == -0.0}.
  // For a), it does not matter whether we return {lhs} or {rhs}. Check the sign
  // bit of {rhs} to differentiate b) and c).
  dop(Movmskp, kScratchRegister, rhs);
  assm->testl(kScratchRegister, Immediate(1));
  assm->j(zero, &lhs_below_rhs, Label::kNear);
  assm->jmp(&lhs_above_rhs, Label::kNear);

  assm->bind(&is_nan);
  // Create a NaN output.
  dop(Xorp, dst, dst);
  dop(Divs, dst, dst);
  assm->jmp(&done, Label::kNear);

  assm->bind(&lhs_below_rhs);
  DoubleRegister lhs_below_rhs_src = min_or_max == MinOrMax::kMin ? lhs : rhs;
  if (dst != lhs_below_rhs_src) dop(Movs, dst, lhs_below_rhs_src);
  assm->jmp(&done, Label::kNear);

  assm->bind(&lhs_above_rhs);
  DoubleRegister lhs_above_rhs_src = min_or_max == MinOrMax::kMin ? rhs : lhs;
  if (dst != lhs_above_rhs_src) dop(Movs, dst, lhs_above_rhs_src);

  assm->bind(&done);
}
}  // namespace liftoff

void LiftoffAssembler::emit_f32_min(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  liftoff::EmitFloatMinOrMax<float>(this, dst, lhs, rhs,
                                    liftoff::MinOrMax::kMin);
}

void LiftoffAssembler::emit_f32_max(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  liftoff::EmitFloatMinOrMax<float>(this, dst, lhs, rhs,
                                    liftoff::MinOrMax::kMax);
}

void LiftoffAssembler::emit_f32_copysign(DoubleRegister dst, DoubleRegister lhs,
                                         DoubleRegister rhs) {
  static constexpr int kF32SignBit = 1 << 31;
  Movd(kScratchRegister, lhs);
  andl(kScratchRegister, Immediate(~kF32SignBit));
  Movd(liftoff::kScratchRegister2, rhs);
  andl(liftoff::kScratchRegister2, Immediate(kF32SignBit));
  orl(kScratchRegister, liftoff::kScratchRegister2);
  Movd(dst, kScratchRegister);
}

void LiftoffAssembler::emit_f32_abs(DoubleRegister dst, DoubleRegister src) {
  static constexpr uint32_t kSignBit = uint32_t{1} << 31;
  if (dst == src) {
    MacroAssembler::Move(kScratchDoubleReg, kSignBit - 1);
    Andps(dst, kScratchDoubleReg);
  } else {
    MacroAssembler::Move(dst, kSignBit - 1);
    Andps(dst, src);
  }
}

void LiftoffAssembler::emit_f32_neg(DoubleRegister dst, DoubleRegister src) {
  static constexpr uint32_t kSignBit = uint32_t{1} << 31;
  if (dst == src) {
    MacroAssembler::Move(kScratchDoubleReg, kSignBit);
    Xorps(dst, kScratchDoubleReg);
  } else {
    MacroAssembler::Move(dst, kSignBit);
    Xorps(dst, src);
  }
}

bool LiftoffAssembler::emit_f32_ceil(DoubleRegister dst, DoubleRegister src) {
  RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
  Roundss(dst, src, kRoundUp);
  return true;
}

bool LiftoffAssembler::emit_f32_floor(DoubleRegister dst, DoubleRegister src) {
  RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
  Roundss(dst, src, kRoundDown);
  return true;
}

bool LiftoffAssembler::emit_f32_trunc(DoubleRegister dst, DoubleRegister src) {
  RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
  Roundss(dst, src, kRoundToZero);
  return true;
}

bool LiftoffAssembler::emit_f32_nearest_int(DoubleRegister dst,
                                            DoubleRegister src) {
  RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
  Roundss(dst, src, kRoundToNearest);
  return true;
}

void LiftoffAssembler::emit_f32_sqrt(DoubleRegister dst, DoubleRegister src) {
  Sqrtss(dst, src);
}

void LiftoffAssembler::emit_f64_add(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vaddsd(dst, lhs, rhs);
  } else if (dst == rhs) {
    addsd(dst, lhs);
  } else {
    if (dst != lhs) movsd(dst, lhs);
    addsd(dst, rhs);
  }
}

void LiftoffAssembler::emit_f64_sub(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vsubsd(dst, lhs, rhs);
  } else if (dst == rhs) {
    movsd(kScratchDoubleReg, rhs);
    movsd(dst, lhs);
    subsd(dst, kScratchDoubleReg);
  } else {
    if (dst != lhs) movsd(dst, lhs);
    subsd(dst, rhs);
  }
}

void LiftoffAssembler::emit_f64_mul(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vmulsd(dst, lhs, rhs);
  } else if (dst == rhs) {
    mulsd(dst, lhs);
  } else {
    if (dst != lhs) movsd(dst, lhs);
    mulsd(dst, rhs);
  }
}

void LiftoffAssembler::emit_f64_div(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vdivsd(dst, lhs, rhs);
  } else if (dst == rhs) {
    movsd(kScratchDoubleReg, rhs);
    movsd(dst, lhs);
    divsd(dst, kScratchDoubleReg);
  } else {
    if (dst != lhs) movsd(dst, lhs);
    divsd(dst, rhs);
  }
}

void LiftoffAssembler::emit_f64_min(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  liftoff::EmitFloatMinOrMax<double>(this, dst, lhs, rhs,
                                     liftoff::MinOrMax::kMin);
}

void LiftoffAssembler::emit_f64_copysign(DoubleRegister dst, DoubleRegister lhs,
                                         DoubleRegister rhs) {
  // Extract sign bit from {rhs} into {kScratchRegister2}.
  Movq(liftoff::kScratchRegister2, rhs);
  shrq(liftoff::kScratchRegister2, Immediate(63));
  shlq(liftoff::kScratchRegister2, Immediate(63));
  // Reset sign bit of {lhs} (in {kScratchRegister}).
  Movq(kScratchRegister, lhs);
  btrq(kScratchRegister, Immediate(63));
  // Combine both values into {kScratchRegister} and move into {dst}.
  orq(kScratchRegister, liftoff::kScratchRegister2);
  Movq(dst, kScratchRegister);
}

void LiftoffAssembler::emit_f64_max(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  liftoff::EmitFloatMinOrMax<double>(this, dst, lhs, rhs,
                                     liftoff::MinOrMax::kMax);
}

void LiftoffAssembler::emit_f64_abs(DoubleRegister dst, DoubleRegister src) {
  static constexpr uint64_t kSignBit = uint64_t{1} << 63;
  if (dst == src) {
    MacroAssembler::Move(kScratchDoubleReg, kSignBit - 1);
    Andpd(dst, kScratchDoubleReg);
  } else {
    MacroAssembler::Move(dst, kSignBit - 1);
    Andpd(dst, src);
  }
}

void LiftoffAssembler::emit_f64_neg(DoubleRegister dst, DoubleRegister src) {
  static constexpr uint64_t kSignBit = uint64_t{1} << 63;
  if (dst == src) {
    MacroAssembler::Move(kScratchDoubleReg, kSignBit);
    Xorpd(dst, kScratchDoubleReg);
  } else {
    MacroAssembler::Move(dst, kSignBit);
    Xorpd(dst, src);
  }
}

bool LiftoffAssembler::emit_f64_ceil(DoubleRegister dst, DoubleRegister src) {
  RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
  Roundsd(dst, src, kRoundUp);
  return true;
}

bool LiftoffAssembler::emit_f64_floor(DoubleRegister dst, DoubleRegister src) {
  RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
  Roundsd(dst, src, kRoundDown);
  return true;
}

bool LiftoffAssembler::emit_f64_trunc(DoubleRegister dst, DoubleRegister src) {
  RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
  Roundsd(dst, src, kRoundToZero);
  return true;
}

bool LiftoffAssembler::emit_f64_nearest_int(DoubleRegister dst,
                                            DoubleRegister src) {
  RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
  Roundsd(dst, src, kRoundToNearest);
  return true;
}

void LiftoffAssembler::emit_f64_sqrt(DoubleRegister dst, DoubleRegister src) {
  Sqrtsd(dst, src);
}

namespace liftoff {
#define __ assm->
// Used for float to int conversions. If the value in {converted_back} equals
// {src} afterwards, the conversion succeeded.
template <typename dst_type, typename src_type>
inline void ConvertFloatToIntAndBack(LiftoffAssembler* assm, Register dst,
                                     DoubleRegister src,
                                     DoubleRegister converted_back) {
  if (std::is_same<double, src_type>::value) {     // f64
    if (std::is_same<int32_t, dst_type>::value) {  // f64 -> i32
      __ Cvttsd2si(dst, src);
      __ Cvtlsi2sd(converted_back, dst);
    } else if (std::is_same<uint32_t, dst_type>::value) {  // f64 -> u32
      __ Cvttsd2siq(dst, src);
      __ movl(dst, dst);
      __ Cvtqsi2sd(converted_back, dst);
    } else if (std::is_same<int64_t, dst_type>::value) {  // f64 -> i64
      __ Cvttsd2siq(dst, src);
      __ Cvtqsi2sd(converted_back, dst);
    } else {
      UNREACHABLE();
    }
  } else {                                         // f32
    if (std::is_same<int32_t, dst_type>::value) {  // f32 -> i32
      __ Cvttss2si(dst, src);
      __ Cvtlsi2ss(converted_back, dst);
    } else if (std::is_same<uint32_t, dst_type>::value) {  // f32 -> u32
      __ Cvttss2siq(dst, src);
      __ movl(dst, dst);
      __ Cvtqsi2ss(converted_back, dst);
    } else if (std::is_same<int64_t, dst_type>::value) {  // f32 -> i64
      __ Cvttss2siq(dst, src);
      __ Cvtqsi2ss(converted_back, dst);
    } else {
      UNREACHABLE();
    }
  }
}

template <typename dst_type, typename src_type>
inline bool EmitTruncateFloatToInt(LiftoffAssembler* assm, Register dst,
                                   DoubleRegister src, Label* trap) {
  if (!CpuFeatures::IsSupported(SSE4_1)) {
    __ bailout(kMissingCPUFeature, "no SSE4.1");
    return true;
  }
  CpuFeatureScope feature(assm, SSE4_1);

  DoubleRegister rounded = kScratchDoubleReg;
  DoubleRegister converted_back = kScratchDoubleReg2;

  if (std::is_same<double, src_type>::value) {  // f64
    __ Roundsd(rounded, src, kRoundToZero);
  } else {  // f32
    __ Roundss(rounded, src, kRoundToZero);
  }
  ConvertFloatToIntAndBack<dst_type, src_type>(assm, dst, rounded,
                                               converted_back);
  if (std::is_same<double, src_type>::value) {  // f64
    __ Ucomisd(converted_back, rounded);
  } else {  // f32
    __ Ucomiss(converted_back, rounded);
  }

  // Jump to trap if PF is 0 (one of the operands was NaN) or they are not
  // equal.
  __ j(parity_even, trap);
  __ j(not_equal, trap);
  return true;
}

template <typename dst_type, typename src_type>
inline bool EmitSatTruncateFloatToInt(LiftoffAssembler* assm, Register dst,
                                      DoubleRegister src) {
  if (!CpuFeatures::IsSupported(SSE4_1)) {
    __ bailout(kMissingCPUFeature, "no SSE4.1");
    return true;
  }
  CpuFeatureScope feature(assm, SSE4_1);

  Label done;
  Label not_nan;
  Label src_positive;

  DoubleRegister rounded = kScratchDoubleReg;
  DoubleRegister converted_back = kScratchDoubleReg2;
  DoubleRegister zero_reg = kScratchDoubleReg;

  if (std::is_same<double, src_type>::value) {  // f64
    __ Roundsd(rounded, src, kRoundToZero);
  } else {  // f32
    __ Roundss(rounded, src, kRoundToZero);
  }

  ConvertFloatToIntAndBack<dst_type, src_type>(assm, dst, rounded,
                                               converted_back);
  if (std::is_same<double, src_type>::value) {  // f64
    __ Ucomisd(converted_back, rounded);
  } else {  // f32
    __ Ucomiss(converted_back, rounded);
  }

  // Return 0 if PF is 0 (one of the operands was NaN)
  __ j(parity_odd, &not_nan);
  __ xorl(dst, dst);
  __ jmp(&done);

  __ bind(&not_nan);
  // If rounding is as expected, return result
  __ j(equal, &done);

  __ xorpd(zero_reg, zero_reg);

  // if out-of-bounds, check if src is positive
  if (std::is_same<double, src_type>::value) {  // f64
    __ Ucomisd(src, zero_reg);
  } else {  // f32
    __ Ucomiss(src, zero_reg);
  }
  __ j(above, &src_positive);
  if (std::is_same<int32_t, dst_type>::value ||
      std::is_same<uint32_t, dst_type>::value) {  // i32
    __ movl(
        dst,
        Immediate(static_cast<int32_t>(std::numeric_limits<dst_type>::min())));
  } else if (std::is_same<int64_t, dst_type>::value) {  // i64s
    __ movq(dst, Immediate64(std::numeric_limits<dst_type>::min()));
  } else {
    UNREACHABLE();
  }
  __ jmp(&done);

  __ bind(&src_positive);
  if (std::is_same<int32_t, dst_type>::value ||
      std::is_same<uint32_t, dst_type>::value) {  // i32
    __ movl(
        dst,
        Immediate(static_cast<int32_t>(std::numeric_limits<dst_type>::max())));
  } else if (std::is_same<int64_t, dst_type>::value) {  // i64s
    __ movq(dst, Immediate64(std::numeric_limits<dst_type>::max()));
  } else {
    UNREACHABLE();
  }

  __ bind(&done);
  return true;
}

template <typename src_type>
inline bool EmitSatTruncateFloatToUInt64(LiftoffAssembler* assm, Register dst,
                                         DoubleRegister src) {
  if (!CpuFeatures::IsSupported(SSE4_1)) {
    __ bailout(kMissingCPUFeature, "no SSE4.1");
    return true;
  }
  CpuFeatureScope feature(assm, SSE4_1);

  Label done;
  Label neg_or_nan;
  Label overflow;

  DoubleRegister zero_reg = kScratchDoubleReg;

  __ xorpd(zero_reg, zero_reg);
  if (std::is_same<double, src_type>::value) {  // f64
    __ Ucomisd(src, zero_reg);
  } else {  // f32
    __ Ucomiss(src, zero_reg);
  }
  // Check if NaN
  __ j(parity_even, &neg_or_nan);
  __ j(below, &neg_or_nan);
  if (std::is_same<double, src_type>::value) {  // f64
    __ Cvttsd2uiq(dst, src, &overflow);
  } else {  // f32
    __ Cvttss2uiq(dst, src, &overflow);
  }
  __ jmp(&done);

  __ bind(&neg_or_nan);
  __ movq(dst, zero_reg);
  __ jmp(&done);

  __ bind(&overflow);
  __ movq(dst, Immediate64(std::numeric_limits<uint64_t>::max()));
  __ bind(&done);
  return true;
}
#undef __
}  // namespace liftoff

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  switch (opcode) {
    case kExprI32ConvertI64:
      movl(dst.gp(), src.gp());
      return true;
    case kExprI32SConvertF32:
      return liftoff::EmitTruncateFloatToInt<int32_t, float>(this, dst.gp(),
                                                             src.fp(), trap);
    case kExprI32UConvertF32:
      return liftoff::EmitTruncateFloatToInt<uint32_t, float>(this, dst.gp(),
                                                              src.fp(), trap);
    case kExprI32SConvertF64:
      return liftoff::EmitTruncateFloatToInt<int32_t, double>(this, dst.gp(),
                                                              src.fp(), trap);
    case kExprI32UConvertF64:
      return liftoff::EmitTruncateFloatToInt<uint32_t, double>(this, dst.gp(),
                                                               src.fp(), trap);
    case kExprI32SConvertSatF32:
      return liftoff::EmitSatTruncateFloatToInt<int32_t, float>(this, dst.gp(),
                                                                src.fp());
    case kExprI32UConvertSatF32:
      return liftoff::EmitSatTruncateFloatToInt<uint32_t, float>(this, dst.gp(),
                                                                 src.fp());
    case kExprI32SConvertSatF64:
      return liftoff::EmitSatTruncateFloatToInt<int32_t, double>(this, dst.gp(),
                                                                 src.fp());
    case kExprI32UConvertSatF64:
      return liftoff::EmitSatTruncateFloatToInt<uint32_t, double>(
          this, dst.gp(), src.fp());
    case kExprI32ReinterpretF32:
      Movd(dst.gp(), src.fp());
      return true;
    case kExprI64SConvertI32:
      movsxlq(dst.gp(), src.gp());
      return true;
    case kExprI64SConvertF32:
      return liftoff::EmitTruncateFloatToInt<int64_t, float>(this, dst.gp(),
                                                             src.fp(), trap);
    case kExprI64UConvertF32: {
      RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
      Cvttss2uiq(dst.gp(), src.fp(), trap);
      return true;
    }
    case kExprI64SConvertF64:
      return liftoff::EmitTruncateFloatToInt<int64_t, double>(this, dst.gp(),
                                                              src.fp(), trap);
    case kExprI64UConvertF64: {
      RETURN_FALSE_IF_MISSING_CPU_FEATURE(SSE4_1);
      Cvttsd2uiq(dst.gp(), src.fp(), trap);
      return true;
    }
    case kExprI64SConvertSatF32:
      return liftoff::EmitSatTruncateFloatToInt<int64_t, float>(this, dst.gp(),
                                                                src.fp());
    case kExprI64UConvertSatF32: {
      return liftoff::EmitSatTruncateFloatToUInt64<float>(this, dst.gp(),
                                                          src.fp());
    }
    case kExprI64SConvertSatF64:
      return liftoff::EmitSatTruncateFloatToInt<int64_t, double>(this, dst.gp(),
                                                                 src.fp());
    case kExprI64UConvertSatF64: {
      return liftoff::EmitSatTruncateFloatToUInt64<double>(this, dst.gp(),
                                                           src.fp());
    }
    case kExprI64UConvertI32:
      emit_u32_to_uintptr(dst.gp(), src.gp());
      return true;
    case kExprI64ReinterpretF64:
      Movq(dst.gp(), src.fp());
      return true;
    case kExprF32SConvertI32:
      Cvtlsi2ss(dst.fp(), src.gp());
      return true;
    case kExprF32UConvertI32:
      movl(kScratchRegister, src.gp());
      Cvtqsi2ss(dst.fp(), kScratchRegister);
      return true;
    case kExprF32SConvertI64:
      Cvtqsi2ss(dst.fp(), src.gp());
      return true;
    case kExprF32UConvertI64:
      Cvtqui2ss(dst.fp(), src.gp());
      return true;
    case kExprF32ConvertF64:
      Cvtsd2ss(dst.fp(), src.fp());
      return true;
    case kExprF32ReinterpretI32:
      Movd(dst.fp(), src.gp());
      return true;
    case kExprF64SConvertI32:
      Cvtlsi2sd(dst.fp(), src.gp());
      return true;
    case kExprF64UConvertI32:
      movl(kScratchRegister, src.gp());
      Cvtqsi2sd(dst.fp(), kScratchRegister);
      return true;
    case kExprF64SConvertI64:
      Cvtqsi2sd(dst.fp(), src.gp());
      return true;
    case kExprF64UConvertI64:
      Cvtqui2sd(dst.fp(), src.gp());
      return true;
    case kExprF64ConvertF32:
      Cvtss2sd(dst.fp(), src.fp());
      return true;
    case kExprF64ReinterpretI64:
      Movq(dst.fp(), src.gp());
      return true;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::emit_i32_signextend_i8(Register dst, Register src) {
  movsxbl(dst, src);
}

void LiftoffAssembler::emit_i32_signextend_i16(Register dst, Register src) {
  movsxwl(dst, src);
}

void LiftoffAssembler::emit_i64_signextend_i8(LiftoffRegister dst,
                                              LiftoffRegister src) {
  movsxbq(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_signextend_i16(LiftoffRegister dst,
                                               LiftoffRegister src) {
  movsxwq(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_signextend_i32(LiftoffRegister dst,
                                               LiftoffRegister src) {
  movsxlq(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_jump(Label* label) { jmp(label); }

void LiftoffAssembler::emit_jump(Register target) { jmp(target); }

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label,
                                      ValueKind kind, Register lhs,
                                      Register rhs,
                                      const FreezeCacheState& frozen) {
  if (rhs != no_reg) {
    switch (kind) {
      case kI32:
        cmpl(lhs, rhs);
        break;
      case kRef:
      case kRefNull:
      case kRtt:
        DCHECK(cond == kEqual || cond == kNotEqual);
#if defined(V8_COMPRESS_POINTERS)
        // It's enough to do a 32-bit comparison. This is also necessary for
        // null checks which only compare against a 32 bit value, not a full
        // pointer.
        cmpl(lhs, rhs);
#else
        cmpq(lhs, rhs);
#endif
        break;
      case kI64:
        cmpq(lhs, rhs);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    DCHECK_EQ(kind, kI32);
    testl(lhs, lhs);
  }

  j(cond, label);
}

void LiftoffAssembler::emit_i32_cond_jumpi(Condition cond, Label* label,
                                           Register lhs, int imm,
                                           const FreezeCacheState& frozen) {
  cmpl(lhs, Immediate(imm));
  j(cond, label);
}

void LiftoffAssembler::emit_ptrsize_cond_jumpi(Condition cond, Label* label,
                                               Register lhs, int32_t imm,
                                               const FreezeCacheState& frozen) {
  cmpq(lhs, Immediate(imm));
  j(cond, label);
}

void LiftoffAssembler::emit_i32_eqz(Register dst, Register src) {
  testl(src, src);
  setcc(equal, dst);
  movzxbl(dst, dst);
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  cmpl(lhs, rhs);
  setcc(cond, dst);
  movzxbl(dst, dst);
}

void LiftoffAssembler::emit_i64_eqz(Register dst, LiftoffRegister src) {
  testq(src.gp(), src.gp());
  setcc(equal, dst);
  movzxbl(dst, dst);
}

void LiftoffAssembler::emit_i64_set_cond(Condition cond, Register dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  cmpq(lhs.gp(), rhs.gp());
  setcc(cond, dst);
  movzxbl(dst, dst);
}

namespace liftoff {
template <void (SharedMacroAssemblerBase::*cmp_op)(DoubleRegister,
                                                   DoubleRegister)>
void EmitFloatSetCond(LiftoffAssembler* assm, Condition cond, Register dst,
                      DoubleRegister lhs, DoubleRegister rhs) {
  Label cont;
  Label not_nan;

  (assm->*cmp_op)(lhs, rhs);
  // If PF is one, one of the operands was NaN. This needs special handling.
  assm->j(parity_odd, &not_nan, Label::kNear);
  // Return 1 for f32.ne, 0 for all other cases.
  if (cond == not_equal) {
    assm->movl(dst, Immediate(1));
  } else {
    assm->xorl(dst, dst);
  }
  assm->jmp(&cont, Label::kNear);
  assm->bind(&not_nan);

  assm->setcc(cond, dst);
  assm->movzxbl(dst, dst);
  assm->bind(&cont);
}
}  // namespace liftoff

void LiftoffAssembler::emit_f32_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  liftoff::EmitFloatSetCond<&MacroAssembler::Ucomiss>(this, cond, dst, lhs,
                                                      rhs);
}

void LiftoffAssembler::emit_f64_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  liftoff::EmitFloatSetCond<&MacroAssembler::Ucomisd>(this, cond, dst, lhs,
                                                      rhs);
}

bool LiftoffAssembler::emit_select(LiftoffRegister dst, Register condition,
                                   LiftoffRegister true_value,
                                   LiftoffRegister false_value,
                                   ValueKind kind) {
  if (kind != kI32 && kind != kI64) return false;

  testl(condition, condition);

  if (kind == kI32) {
    if (dst == false_value) {
      cmovl(not_zero, dst.gp(), true_value.gp());
    } else {
      if (dst != true_value) movl(dst.gp(), true_value.gp());
      cmovl(zero, dst.gp(), false_value.gp());
    }
  } else {
    if (dst == false_value) {
      cmovq(not_zero, dst.gp(), true_value.gp());
    } else {
      if (dst != true_value) movq(dst.gp(), true_value.gp());
      cmovq(zero, dst.gp(), false_value.gp());
    }
  }

  return true;
}

void LiftoffAssembler::emit_smi_check(Register obj, Label* target,
                                      SmiCheckMode mode,
                                      const FreezeCacheState& frozen) {
  testb(obj, Immediate(kSmiTagMask));
  Condition condition = mode == kJumpOnSmi ? zero : not_zero;
  j(condition, target);
}

// TODO(fanchenk): Distinguish mov* if data bypass delay matter.
namespace liftoff {
template <void (Assembler::*avx_op)(XMMRegister, XMMRegister, XMMRegister),
          void (Assembler::*sse_op)(XMMRegister, XMMRegister)>
void EmitSimdCommutativeBinOp(
    LiftoffAssembler* assm, LiftoffRegister dst, LiftoffRegister lhs,
    LiftoffRegister rhs, std::optional<CpuFeature> feature = std::nullopt) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(assm, AVX);
    (assm->*avx_op)(dst.fp(), lhs.fp(), rhs.fp());
    return;
  }

  std::optional<CpuFeatureScope> sse_scope;
  if (feature.has_value()) sse_scope.emplace(assm, *feature);

  if (dst.fp() == rhs.fp()) {
    (assm->*sse_op)(dst.fp(), lhs.fp());
  } else {
    if (dst.fp() != lhs.fp()) (assm->movaps)(dst.fp(), lhs.fp());
    (assm->*sse_op)(dst.fp(), rhs.fp());
  }
}

template <void (Assembler::*avx_op)(XMMRegister, XMMRegister, XMMRegister),
          void (Assembler::*sse_op)(XMMRegister, XMMRegister)>
void EmitSimdNonCommutativeBinOp(
    LiftoffAssembler* assm, LiftoffRegister dst, LiftoffRegister lhs,
    LiftoffRegister rhs, std::optional<CpuFeature> feature = std::nullopt) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(assm, AVX);
    (assm->*avx_op)(dst.fp(), lhs.fp(), rhs.fp());
    return;
  }

  std::optional<CpuFeatureScope> sse_scope;
  if (feature.has_value()) sse_scope.emplace(assm, *feature);

  if (dst.fp() == rhs.fp()) {
    assm->movaps(kScratchDoubleReg, rhs.fp());
    assm->movaps(dst.fp(), lhs.fp());
    (assm->*sse_op)(dst.fp(), kScratchDoubleReg);
  } else {
    if (dst.fp() != lhs.fp()) assm->movaps(dst.fp(), lhs.fp());
    (assm->*sse_op)(dst.fp(), rhs.fp());
  }
}

template <void (Assembler::*avx_op)(XMMRegister, XMMRegister, XMMRegister),
          void (Assembler::*sse_op)(XMMRegister, XMMRegister), uint8_t width>
void EmitSimdShiftOp(LiftoffAssembler* assm, LiftoffRegister dst,
                     LiftoffRegister operand, LiftoffRegister count) {
  constexpr int mask = (1 << width) - 1;
  assm->movq(kScratchRegister, count.gp());
  assm->andq(kScratchRegister, Immediate(mask));
  assm->Movq(kScratchDoubleReg, kScratchRegister);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(assm, AVX);
    (assm->*avx_op)(dst.fp(), operand.fp(), kScratchDoubleReg);
  } else {
    if (dst.fp() != operand.fp()) assm->movaps(dst.fp(), operand.fp());
    (assm->*sse_op)(dst.fp(), kScratchDoubleReg);
  }
}

template <void (Assembler::*avx_op)(XMMRegister, XMMRegister, uint8_t),
          void (Assembler::*sse_op)(XMMRegister, uint8_t), uint8_t width>
void EmitSimdShiftOpImm(LiftoffAssembler* assm, LiftoffRegister dst,
                        LiftoffRegister operand, int32_t count) {
  constexpr int mask = (1 << width) - 1;
  uint8_t shift = static_cast<uint8_t>(count & mask);
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(assm, AVX);
    (assm->*avx_op)(dst.fp(), operand.fp(), shift);
  } else {
    if (dst.fp() != operand.fp()) assm->movaps(dst.fp(), operand.fp());
    (assm->*sse_op)(dst.fp(), shift);
  }
}

inline void EmitAnyTrue(LiftoffAssembler* assm, LiftoffRegister dst,
                        LiftoffRegister src) {
  assm->xorq(dst.gp(), dst.gp());
  assm->Ptest(src.fp(), src.fp());
  assm->setcc(not_equal, dst.gp());
}

template <void (SharedMacroAssemblerBase::*pcmp)(XMMRegister, XMMRegister)>
inline void EmitAllTrue(LiftoffAssembler* assm, LiftoffRegister dst,
                        LiftoffRegister src,
                        std::optional<CpuFeature> feature = std::nullopt) {
  std::optional<CpuFeatureScope> sse_scope;
  if (feature.has_value()) sse_scope.emplace(assm, *feature);

  XMMRegister tmp = kScratchDoubleReg;
  assm->xorq(dst.gp(), dst.gp());
  assm->Pxor(tmp, tmp);
  (assm->*pcmp)(tmp, src.fp());
  assm->Ptest(tmp, tmp);
  assm->setcc(equal, dst.gp());
}

}  // namespace liftoff

void LiftoffAssembler::LoadTransform(LiftoffRegister dst, Register src_addr,
                                     Register offset_reg, uintptr_t offset_imm,
                                     LoadType type,
                                     LoadTransformationKind transform,
                                     uint32_t* protected_load_pc) {
  Operand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm);
  *protected_load_pc = pc_offset();
  MachineType memtype = type.mem_type();
  if (transform == LoadTransformationKind::kExtend) {
    if (memtype == MachineType::Int8()) {
      Pmovsxbw(dst.fp(), src_op);
    } else if (memtype == MachineType::Uint8()) {
      Pmovzxbw(dst.fp(), src_op);
    } else if (memtype == MachineType::Int16()) {
      Pmovsxwd(dst.fp(), src_op);
    } else if (memtype == MachineType::Uint16()) {
      Pmovzxwd(dst.fp(), src_op);
    } else if (memtype == MachineType::Int32()) {
      Pmovsxdq(dst.fp(), src_op);
    } else if (memtype == MachineType::Uint32()) {
      Pmovzxdq(dst.fp(), src_op);
    }
  } else if (transform == LoadTransformationKind::kZeroExtend) {
    if (memtype == MachineType::Int32()) {
      Movss(dst.fp(), src_op);
    } else {
      DCHECK_EQ(MachineType::Int64(), memtype);
      Movsd(dst.fp(), src_op);
    }
  } else {
    DCHECK_EQ(LoadTransformationKind::kSplat, transform);
    if (memtype == MachineType::Int8()) {
      S128Load8Splat(dst.fp(), src_op, kScratchDoubleReg);
    } else if (memtype == MachineType::Int16()) {
      S128Load16Splat(dst.fp(), src_op, kScratchDoubleReg);
    } else if (memtype == MachineType::Int32()) {
      S128Load32Splat(dst.fp(), src_op);
    } else if (memtype == MachineType::Int64()) {
      Movddup(dst.fp(), src_op);
    }
  }
}

void LiftoffAssembler::LoadLane(LiftoffRegister dst, LiftoffRegister src,
                                Register addr, Register offset_reg,
                                uintptr_t offset_imm, LoadType type,
                                uint8_t laneidx, uint32_t* protected_load_pc,
                                bool i64_offset) {
  if (offset_reg != no_reg && !i64_offset) AssertZeroExtended(offset_reg);
  Operand src_op = liftoff::GetMemOp(this, addr, offset_reg, offset_imm);

  MachineType mem_type = type.mem_type();
  if (mem_type == MachineType::Int8()) {
    Pinsrb(dst.fp(), src.fp(), src_op, laneidx, protected_load_pc);
  } else if (mem_type == MachineType::Int16()) {
    Pinsrw(dst.fp(), src.fp(), src_op, laneidx, protected_load_pc);
  } else if (mem_type == MachineType::Int32()) {
    Pinsrd(dst.fp(), src.fp(), src_op, laneidx, protected_load_pc);
  } else {
    DCHECK_EQ(MachineType::Int64(), mem_type);
    Pinsrq(dst.fp(), src.fp(), src_op, laneidx, protected_load_pc);
  }
}

void LiftoffAssembler::StoreLane(Register dst, Register offset,
                                 uintptr_t offset_imm, LiftoffRegister src,
                                 StoreType type, uint8_t lane,
                                 uint32_t* protected_store_pc,
                                 bool i64_offset) {
  if (offset != no_reg && !i64_offset) AssertZeroExtended(offset);
  Operand dst_op = liftoff::GetMemOp(this, dst, offset, offset_imm);
  if (protected_store_pc) *protected_store_pc = pc_offset();
  MachineRepresentation rep = type.mem_rep();
  if (rep == MachineRepresentation::kWord8) {
    Pextrb(dst_op, src.fp(), lane);
  } else if (rep == MachineRepresentation::kWord16) {
    Pextrw(dst_op, src.fp(), lane);
  } else if (rep == MachineRepresentation::kWord32) {
    S128Store32Lane(dst_op, src.fp(), lane);
  } else {
    DCHECK_EQ(MachineRepresentation::kWord64, rep);
    S128Store64Lane(dst_op, src.fp(), lane);
  }
}

void LiftoffAssembler::emit_i8x16_shuffle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs,
                                          const uint8_t shuffle[16],
                                          bool is_swizzle) {
  if (is_swizzle) {
    uint32_t imms[4];
    // Shuffles that use just 1 operand are called swizzles, rhs can be ignored.
    wasm::SimdShuffle::Pack16Lanes(imms, shuffle);
    MacroAssembler::Move(kScratchDoubleReg, make_uint64(imms[3], imms[2]),
                         make_uint64(imms[1], imms[0]));
    Pshufb(dst.fp(), lhs.fp(), kScratchDoubleReg);
    return;
  }

  uint64_t mask1[2] = {};
  for (int i = 15; i >= 0; i--) {
    uint8_t lane = shuffle[i];
    int j = i >> 3;
    mask1[j] <<= 8;
    mask1[j] |= lane < kSimd128Size ? lane : 0x80;
  }
  MacroAssembler::Move(liftoff::kScratchDoubleReg2, mask1[1], mask1[0]);
  Pshufb(kScratchDoubleReg, lhs.fp(), liftoff::kScratchDoubleReg2);

  uint64_t mask2[2] = {};
  for (int i = 15; i >= 0; i--) {
    uint8_t lane = shuffle[i];
    int j = i >> 3;
    mask2[j] <<= 8;
    mask2[j] |= lane >= kSimd128Size ? (lane & 0x0F) : 0x80;
  }
  MacroAssembler::Move(liftoff::kScratchDoubleReg2, mask2[1], mask2[0]);

  Pshufb(dst.fp(), rhs.fp(), liftoff::kScratchDoubleReg2);
  Por(dst.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i8x16_swizzle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs) {
  I8x16Swizzle(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg,
               kScratchRegister);
}

void LiftoffAssembler::emit_i8x16_relaxed_swizzle(LiftoffRegister dst,
                                                  LiftoffRegister lhs,
                                                  LiftoffRegister rhs) {
  I8x16Swizzle(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg,
               kScratchRegister, true);
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f32x4_s(LiftoffRegister dst,
                                                        LiftoffRegister src) {
  Cvttps2dq(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f32x4_u(LiftoffRegister dst,
                                                        LiftoffRegister src) {
  emit_i32x4_uconvert_f32x4(dst, src);
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f64x2_s_zero(
    LiftoffRegister dst, LiftoffRegister src) {
  Cvttpd2dq(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f64x2_u_zero(
    LiftoffRegister dst, LiftoffRegister src) {
  emit_i32x4_trunc_sat_f64x2_u_zero(dst, src);
}

void LiftoffAssembler::emit_s128_relaxed_laneselect(LiftoffRegister dst,
                                                    LiftoffRegister src1,
                                                    LiftoffRegister src2,
                                                    LiftoffRegister mask,
                                                    int lane_width) {
  // Passing {src2} first is not a typo: the x86 instructions copy from the
  // second operand when the mask is 1, contrary to the Wasm instruction.
  if (lane_width == 8) {
    Pblendvb(dst.fp(), src2.fp(), src1.fp(), mask.fp());
  } else if (lane_width == 32) {
    Blendvps(dst.fp(), src2.fp(), src1.fp(), mask.fp());
  } else if (lane_width == 64) {
    Blendvpd(dst.fp(), src2.fp(), src1.fp(), mask.fp());
  } else {
    UNREACHABLE();
  }
}

void LiftoffAssembler::emit_i8x16_popcnt(LiftoffRegister dst,
                                         LiftoffRegister src) {
  I8x16Popcnt(dst.fp(), src.fp(), kScratchDoubleReg,
              liftoff::kScratchDoubleReg2, kScratchRegister);
}

void LiftoffAssembler::emit_i8x16_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  I8x16Splat(dst.fp(), src.gp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i16x8_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  I16x8Splat(dst.fp(), src.gp());
}

void LiftoffAssembler::emit_i32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  Movd(dst.fp(), src.gp());
  Pshufd(dst.fp(), dst.fp(), static_cast<uint8_t>(0));
}

void LiftoffAssembler::emit_i64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  Movq(dst.fp(), src.gp());
  Movddup(dst.fp(), dst.fp());
}

void LiftoffAssembler::emit_f32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  F32x4Splat(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_f64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  Movddup(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i8x16_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpcmpeqb, &Assembler::pcmpeqb>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i8x16_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpcmpeqb, &Assembler::pcmpeqb>(
      this, dst, lhs, rhs);
  Pcmpeqb(kScratchDoubleReg, kScratchDoubleReg);
  Pxor(dst.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i8x16_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpcmpgtb,
                                       &Assembler::pcmpgtb>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_i8x16_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxub, &Assembler::pmaxub>(
      this, dst, lhs, rhs, SSE4_1);
  Pcmpeqb(dst.fp(), ref);
  Pcmpeqb(kScratchDoubleReg, kScratchDoubleReg);
  Pxor(dst.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i8x16_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminsb, &Assembler::pminsb>(
      this, dst, lhs, rhs, SSE4_1);
  Pcmpeqb(dst.fp(), ref);
}

void LiftoffAssembler::emit_i8x16_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminub, &Assembler::pminub>(
      this, dst, lhs, rhs);
  Pcmpeqb(dst.fp(), ref);
}

void LiftoffAssembler::emit_i16x8_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpcmpeqw, &Assembler::pcmpeqw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpcmpeqw, &Assembler::pcmpeqw>(
      this, dst, lhs, rhs);
  Pcmpeqw(kScratchDoubleReg, kScratchDoubleReg);
  Pxor(dst.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i16x8_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpcmpgtw,
                                       &Assembler::pcmpgtw>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_i16x8_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxuw, &Assembler::pmaxuw>(
      this, dst, lhs, rhs);
  Pcmpeqw(dst.fp(), ref);
  Pcmpeqw(kScratchDoubleReg, kScratchDoubleReg);
  Pxor(dst.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i16x8_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminsw, &Assembler::pminsw>(
      this, dst, lhs, rhs);
  Pcmpeqw(dst.fp(), ref);
}

void LiftoffAssembler::emit_i16x8_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminuw, &Assembler::pminuw>(
      this, dst, lhs, rhs, SSE4_1);
  Pcmpeqw(dst.fp(), ref);
}

void LiftoffAssembler::emit_i32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpcmpeqd, &Assembler::pcmpeqd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpcmpeqd, &Assembler::pcmpeqd>(
      this, dst, lhs, rhs);
  Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
  Pxor(dst.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i32x4_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpcmpgtd,
                                       &Assembler::pcmpgtd>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_i32x4_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxud, &Assembler::pmaxud>(
      this, dst, lhs, rhs, SSE4_1);
  Pcmpeqd(dst.fp(), ref);
  Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
  Pxor(dst.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i32x4_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminsd, &Assembler::pminsd>(
      this, dst, lhs, rhs, SSE4_1);
  Pcmpeqd(dst.fp(), ref);
}

void LiftoffAssembler::emit_i32x4_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  DoubleRegister ref = rhs.fp();
  if (dst == rhs) {
    Movaps(kScratchDoubleReg, rhs.fp());
    ref = kScratchDoubleReg;
  }
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminud, &Assembler::pminud>(
      this, dst, lhs, rhs, SSE4_1);
  Pcmpeqd(dst.fp(), ref);
}

void LiftoffAssembler::emit_i64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpcmpeqq, &Assembler::pcmpeqq>(
      this, dst, lhs, rhs, SSE4_1);
}

void LiftoffAssembler::emit_i64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpcmpeqq, &Assembler::pcmpeqq>(
      this, dst, lhs, rhs, SSE4_1);
  Pcmpeqq(kScratchDoubleReg, kScratchDoubleReg);
  Pxor(dst.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i64x2_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  // Different register alias requirements depending on CpuFeatures supported:
  if (CpuFeatures::IsSupported(AVX) || CpuFeatures::IsSupported(SSE4_2)) {
    // 1. AVX, or SSE4_2 no requirements (I64x2GtS takes care of aliasing).
    I64x2GtS(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
  } else {
    // 2. Else, dst != lhs && dst != rhs (lhs == rhs is ok).
    if (dst == lhs || dst == rhs) {
      I64x2GtS(liftoff::kScratchDoubleReg2, lhs.fp(), rhs.fp(),
               kScratchDoubleReg);
      movaps(dst.fp(), liftoff::kScratchDoubleReg2);
    } else {
      I64x2GtS(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
    }
  }
}

void LiftoffAssembler::emit_i64x2_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  // Different register alias requirements depending on CpuFeatures supported:
  if (CpuFeatures::IsSupported(AVX)) {
    // 1. AVX, no requirements.
    I64x2GeS(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
  } else if (CpuFeatures::IsSupported(SSE4_2)) {
    // 2. SSE4_2, dst != lhs.
    if (dst == lhs) {
      I64x2GeS(liftoff::kScratchDoubleReg2, lhs.fp(), rhs.fp(),
               kScratchDoubleReg);
      movaps(dst.fp(), liftoff::kScratchDoubleReg2);
    } else {
      I64x2GeS(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
    }
  } else {
    // 3. Else, dst != lhs && dst != rhs (lhs == rhs is ok).
    if (dst == lhs || dst == rhs) {
      I64x2GeS(liftoff::kScratchDoubleReg2, lhs.fp(), rhs.fp(),
               kScratchDoubleReg);
      movaps(dst.fp(), liftoff::kScratchDoubleReg2);
    } else {
      I64x2GeS(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
    }
  }
}

void LiftoffAssembler::emit_f32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vcmpeqps, &Assembler::cmpeqps>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vcmpneqps,
                                    &Assembler::cmpneqps>(this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32x4_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vcmpltps,
                                       &Assembler::cmpltps>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_f32x4_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vcmpleps,
                                       &Assembler::cmpleps>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_f64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vcmpeqpd, &Assembler::cmpeqpd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vcmpneqpd,
                                    &Assembler::cmpneqpd>(this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vcmpltpd,
                                       &Assembler::cmpltpd>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_f64x2_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vcmplepd,
                                       &Assembler::cmplepd>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_s128_const(LiftoffRegister dst,
                                       const uint8_t imms[16]) {
  uint64_t vals[2];
  memcpy(vals, imms, sizeof(vals));
  MacroAssembler::Move(dst.fp(), vals[1], vals[0]);
}

void LiftoffAssembler::emit_s128_not(LiftoffRegister dst, LiftoffRegister src) {
  S128Not(dst.fp(), src.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_s128_and(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpand, &Assembler::pand>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_s128_or(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpor, &Assembler::por>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_s128_xor(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpxor, &Assembler::pxor>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_s128_select(LiftoffRegister dst,
                                        LiftoffRegister src1,
                                        LiftoffRegister src2,
                                        LiftoffRegister mask) {
  // Ensure that we don't overwrite any inputs with the movaps below.
  DCHECK_NE(dst, src1);
  DCHECK_NE(dst, src2);
  if (!CpuFeatures::IsSupported(AVX) && dst != mask) {
    movaps(dst.fp(), mask.fp());
    S128Select(dst.fp(), dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg);
  } else {
    S128Select(dst.fp(), mask.fp(), src1.fp(), src2.fp(), kScratchDoubleReg);
  }
}

void LiftoffAssembler::emit_i8x16_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  if (dst.fp() == src.fp()) {
    Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
    Psignb(dst.fp(), kScratchDoubleReg);
  } else {
    Pxor(dst.fp(), dst.fp());
    Psubb(dst.fp(), src.fp());
  }
}

void LiftoffAssembler::emit_v128_anytrue(LiftoffRegister dst,
                                         LiftoffRegister src) {
  liftoff::EmitAnyTrue(this, dst, src);
}

void LiftoffAssembler::emit_i8x16_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  liftoff::EmitAllTrue<&MacroAssembler::Pcmpeqb>(this, dst, src);
}

void LiftoffAssembler::emit_i8x16_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  Pmovmskb(dst.gp(), src.fp());
}

void LiftoffAssembler::emit_i8x16_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  I8x16Shl(dst.fp(), lhs.fp(), rhs.gp(), kScratchRegister, kScratchDoubleReg,
           liftoff::kScratchDoubleReg2);
}

void LiftoffAssembler::emit_i8x16_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  I8x16Shl(dst.fp(), lhs.fp(), rhs, kScratchRegister, kScratchDoubleReg);
}

void LiftoffAssembler::emit_i8x16_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  I8x16ShrS(dst.fp(), lhs.fp(), rhs.gp(), kScratchRegister, kScratchDoubleReg,
            liftoff::kScratchDoubleReg2);
}

void LiftoffAssembler::emit_i8x16_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  I8x16ShrS(dst.fp(), lhs.fp(), rhs, kScratchDoubleReg);
}

void LiftoffAssembler::emit_i8x16_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  I8x16ShrU(dst.fp(), lhs.fp(), rhs.gp(), kScratchRegister, kScratchDoubleReg,
            liftoff::kScratchDoubleReg2);
}

void LiftoffAssembler::emit_i8x16_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  I8x16ShrU(dst.fp(), lhs.fp(), rhs, kScratchRegister, kScratchDoubleReg);
}

void LiftoffAssembler::emit_i8x16_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpaddb, &Assembler::paddb>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i8x16_add_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpaddsb, &Assembler::paddsb>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i8x16_add_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpaddusb, &Assembler::paddusb>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i8x16_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpsubb, &Assembler::psubb>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i8x16_sub_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpsubsb, &Assembler::psubsb>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i8x16_sub_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpsubusb,
                                       &Assembler::psubusb>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_i8x16_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminsb, &Assembler::pminsb>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i8x16_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminub, &Assembler::pminub>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i8x16_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxsb, &Assembler::pmaxsb>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i8x16_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxub, &Assembler::pmaxub>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  if (dst.fp() == src.fp()) {
    Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
    Psignw(dst.fp(), kScratchDoubleReg);
  } else {
    Pxor(dst.fp(), dst.fp());
    Psubw(dst.fp(), src.fp());
  }
}

void LiftoffAssembler::emit_i16x8_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  liftoff::EmitAllTrue<&MacroAssembler::Pcmpeqw>(this, dst, src);
}

void LiftoffAssembler::emit_i16x8_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  XMMRegister tmp = kScratchDoubleReg;
  Packsswb(tmp, src.fp());
  Pmovmskb(dst.gp(), tmp);
  shrq(dst.gp(), Immediate(8));
}

void LiftoffAssembler::emit_i16x8_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdShiftOp<&Assembler::vpsllw, &Assembler::psllw, 4>(this, dst,
                                                                     lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  liftoff::EmitSimdShiftOpImm<&Assembler::vpsllw, &Assembler::psllw, 4>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdShiftOp<&Assembler::vpsraw, &Assembler::psraw, 4>(this, dst,
                                                                     lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  liftoff::EmitSimdShiftOpImm<&Assembler::vpsraw, &Assembler::psraw, 4>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdShiftOp<&Assembler::vpsrlw, &Assembler::psrlw, 4>(this, dst,
                                                                     lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  liftoff::EmitSimdShiftOpImm<&Assembler::vpsrlw, &Assembler::psrlw, 4>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpaddw, &Assembler::paddw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_add_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpaddsw, &Assembler::paddsw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_add_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpaddusw, &Assembler::paddusw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpsubw, &Assembler::psubw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_sub_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpsubsw, &Assembler::psubsw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_sub_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpsubusw,
                                       &Assembler::psubusw>(this, dst, lhs,
                                                            rhs);
}

void LiftoffAssembler::emit_i16x8_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmullw, &Assembler::pmullw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminsw, &Assembler::pminsw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminuw, &Assembler::pminuw>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i16x8_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxsw, &Assembler::pmaxsw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxuw, &Assembler::pmaxuw>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i16x8_extadd_pairwise_i8x16_s(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  I16x8ExtAddPairwiseI8x16S(dst.fp(), src.fp(), kScratchDoubleReg,
                            kScratchRegister);
}

void LiftoffAssembler::emit_i16x8_extadd_pairwise_i8x16_u(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  I16x8ExtAddPairwiseI8x16U(dst.fp(), src.fp(), kScratchRegister);
}

void LiftoffAssembler::emit_i16x8_extmul_low_i8x16_s(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  I16x8ExtMulLow(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg,
                 /*is_signed=*/true);
}

void LiftoffAssembler::emit_i16x8_extmul_low_i8x16_u(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  I16x8ExtMulLow(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg,
                 /*is_signed=*/false);
}

void LiftoffAssembler::emit_i16x8_extmul_high_i8x16_s(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  I16x8ExtMulHighS(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i16x8_extmul_high_i8x16_u(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  I16x8ExtMulHighU(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i16x8_q15mulr_sat_s(LiftoffRegister dst,
                                                LiftoffRegister src1,
                                                LiftoffRegister src2) {
  I16x8Q15MulRSatS(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i16x8_relaxed_q15mulr_s(LiftoffRegister dst,
                                                    LiftoffRegister src1,
                                                    LiftoffRegister src2) {
  if (CpuFeatures::IsSupported(AVX) || dst == src1) {
    Pmulhrsw(dst.fp(), src1.fp(), src2.fp());
  } else {
    movdqa(dst.fp(), src1.fp());
    pmulhrsw(dst.fp(), src2.fp());
  }
}

void LiftoffAssembler::emit_i16x8_dot_i8x16_i7x16_s(LiftoffRegister dst,
                                                    LiftoffRegister lhs,
                                                    LiftoffRegister rhs) {
  I16x8DotI8x16I7x16S(dst.fp(), lhs.fp(), rhs.fp());
}

void LiftoffAssembler::emit_i32x4_dot_i8x16_i7x16_add_s(LiftoffRegister dst,
                                                        LiftoffRegister lhs,
                                                        LiftoffRegister rhs,
                                                        LiftoffRegister acc) {
  if (CpuFeatures::IsSupported(AVX_VNNI)) {
    I32x4DotI8x16I7x16AddS(dst.fp(), lhs.fp(), rhs.fp(), acc.fp(),
                           kScratchDoubleReg, kScratchDoubleReg);
  } else {
    static constexpr RegClass tmp_rc = reg_class_for(kS128);
    LiftoffRegister tmp1 =
        GetUnusedRegister(tmp_rc, LiftoffRegList{dst, lhs, rhs, acc});
    LiftoffRegister tmp2 =
        GetUnusedRegister(tmp_rc, LiftoffRegList{dst, lhs, rhs, acc, tmp1});
    I32x4DotI8x16I7x16AddS(dst.fp(), lhs.fp(), rhs.fp(), acc.fp(), tmp1.fp(),
                           tmp2.fp());
  }
}

void LiftoffAssembler::emit_i32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  if (dst.fp() == src.fp()) {
    Pcmpeqd(kScratchDoubleReg, kScratchDoubleReg);
    Psignd(dst.fp(), kScratchDoubleReg);
  } else {
    Pxor(dst.fp(), dst.fp());
    Psubd(dst.fp(), src.fp());
  }
}

void LiftoffAssembler::emit_i32x4_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  liftoff::EmitAllTrue<&MacroAssembler::Pcmpeqd>(this, dst, src);
}

void LiftoffAssembler::emit_i32x4_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  Movmskps(dst.gp(), src.fp());
}

void LiftoffAssembler::emit_i32x4_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdShiftOp<&Assembler::vpslld, &Assembler::pslld, 5>(this, dst,
                                                                     lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  liftoff::EmitSimdShiftOpImm<&Assembler::vpslld, &Assembler::pslld, 5>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdShiftOp<&Assembler::vpsrad, &Assembler::psrad, 5>(this, dst,
                                                                     lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  liftoff::EmitSimdShiftOpImm<&Assembler::vpsrad, &Assembler::psrad, 5>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdShiftOp<&Assembler::vpsrld, &Assembler::psrld, 5>(this, dst,
                                                                     lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  liftoff::EmitSimdShiftOpImm<&Assembler::vpsrld, &Assembler::psrld, 5>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpaddd, &Assembler::paddd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpsubd, &Assembler::psubd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmulld, &Assembler::pmulld>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i32x4_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminsd, &Assembler::pminsd>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i32x4_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpminud, &Assembler::pminud>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i32x4_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxsd, &Assembler::pmaxsd>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i32x4_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaxud, &Assembler::pmaxud>(
      this, dst, lhs, rhs, std::optional<CpuFeature>(SSE4_1));
}

void LiftoffAssembler::emit_i32x4_dot_i16x8_s(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpmaddwd, &Assembler::pmaddwd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32x4_extadd_pairwise_i16x8_s(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  I32x4ExtAddPairwiseI16x8S(dst.fp(), src.fp(), kScratchRegister);
}

void LiftoffAssembler::emit_i32x4_extadd_pairwise_i16x8_u(LiftoffRegister dst,
                                                          LiftoffRegister src) {
  I32x4ExtAddPairwiseI16x8U(dst.fp(), src.fp(), kScratchDoubleReg);
}

namespace liftoff {
// Helper function to check for register aliasing, AVX support, and moves
// registers around before calling the actual macro-assembler function.
inline void I32x4ExtMulHelper(LiftoffAssembler* assm, XMMRegister dst,
                              XMMRegister src1, XMMRegister src2, bool low,
                              bool is_signed) {
  // I32x4ExtMul requires dst == src1 if AVX is not supported.
  if (CpuFeatures::IsSupported(AVX) || dst == src1) {
    assm->I32x4ExtMul(dst, src1, src2, kScratchDoubleReg, low, is_signed);
  } else if (dst != src2) {
    // dst != src1 && dst != src2
    assm->movaps(dst, src1);
    assm->I32x4ExtMul(dst, dst, src2, kScratchDoubleReg, low, is_signed);
  } else {
    // dst == src2
    // Extended multiplication is commutative,
    assm->movaps(dst, src2);
    assm->I32x4ExtMul(dst, dst, src1, kScratchDoubleReg, low, is_signed);
  }
}
}  // namespace liftoff

void LiftoffAssembler::emit_i32x4_extmul_low_i16x8_s(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  liftoff::I32x4ExtMulHelper(this, dst.fp(), src1.fp(), src2.fp(), /*low=*/true,
                             /*is_signed=*/true);
}

void LiftoffAssembler::emit_i32x4_extmul_low_i16x8_u(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  liftoff::I32x4ExtMulHelper(this, dst.fp(), src1.fp(), src2.fp(), /*low=*/true,
                             /*is_signed=*/false);
}

void LiftoffAssembler::emit_i32x4_extmul_high_i16x8_s(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  liftoff::I32x4ExtMulHelper(this, dst.fp(), src1.fp(), src2.fp(),
                             /*low=*/false,
                             /*is_signed=*/true);
}

void LiftoffAssembler::emit_i32x4_extmul_high_i16x8_u(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  liftoff::I32x4ExtMulHelper(this, dst.fp(), src1.fp(), src2.fp(),
                             /*low=*/false,
                             /*is_signed=*/false);
}

void LiftoffAssembler::emit_i64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  I64x2Neg(dst.fp(), src.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i64x2_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  liftoff::EmitAllTrue<&MacroAssembler::Pcmpeqq>(this, dst, src, SSE4_1);
}

void LiftoffAssembler::emit_i64x2_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdShiftOp<&Assembler::vpsllq, &Assembler::psllq, 6>(this, dst,
                                                                     lhs, rhs);
}

void LiftoffAssembler::emit_i64x2_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  liftoff::EmitSimdShiftOpImm<&Assembler::vpsllq, &Assembler::psllq, 6>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i64x2_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  I64x2ShrS(dst.fp(), lhs.fp(), rhs.gp(), kScratchDoubleReg,
            liftoff::kScratchDoubleReg2, kScratchRegister);
}

void LiftoffAssembler::emit_i64x2_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  I64x2ShrS(dst.fp(), lhs.fp(), rhs & 0x3F, kScratchDoubleReg);
}

void LiftoffAssembler::emit_i64x2_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  liftoff::EmitSimdShiftOp<&Assembler::vpsrlq, &Assembler::psrlq, 6>(this, dst,
                                                                     lhs, rhs);
}

void LiftoffAssembler::emit_i64x2_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  liftoff::EmitSimdShiftOpImm<&Assembler::vpsrlq, &Assembler::psrlq, 6>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpaddq, &Assembler::paddq>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpsubq, &Assembler::psubq>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  static constexpr RegClass tmp_rc = reg_class_for(kS128);
  LiftoffRegister tmp1 =
      GetUnusedRegister(tmp_rc, LiftoffRegList{dst, lhs, rhs});
  LiftoffRegister tmp2 =
      GetUnusedRegister(tmp_rc, LiftoffRegList{dst, lhs, rhs, tmp1});
  I64x2Mul(dst.fp(), lhs.fp(), rhs.fp(), tmp1.fp(), tmp2.fp());
}

void LiftoffAssembler::emit_i64x2_extmul_low_i32x4_s(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  I64x2ExtMul(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg, /*low=*/true,
              /*is_signed=*/true);
}

void LiftoffAssembler::emit_i64x2_extmul_low_i32x4_u(LiftoffRegister dst,
                                                     LiftoffRegister src1,
                                                     LiftoffRegister src2) {
  I64x2ExtMul(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg, /*low=*/true,
              /*is_signed=*/false);
}

void LiftoffAssembler::emit_i64x2_extmul_high_i32x4_s(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  I64x2ExtMul(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg, /*low=*/false,
              /*is_signed=*/true);
}

void LiftoffAssembler::emit_i64x2_extmul_high_i32x4_u(LiftoffRegister dst,
                                                      LiftoffRegister src1,
                                                      LiftoffRegister src2) {
  I64x2ExtMul(dst.fp(), src1.fp(), src2.fp(), kScratchDoubleReg, /*low=*/false,
              /*is_signed=*/false);
}

void LiftoffAssembler::emit_i64x2_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  Movmskpd(dst.gp(), src.fp());
}

void LiftoffAssembler::emit_i64x2_sconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  Pmovsxdq(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i64x2_sconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I64x2SConvertI32x4High(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  Pmovzxdq(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I64x2UConvertI32x4High(dst.fp(), src.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_f32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  Absps(dst.fp(), src.fp(), kScratchRegister);
}

void LiftoffAssembler::emit_f32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  Negps(dst.fp(), src.fp(), kScratchRegister);
}

void LiftoffAssembler::emit_f32x4_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  Sqrtps(dst.fp(), src.fp());
}

bool LiftoffAssembler::emit_f32x4_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  DCHECK(CpuFeatures::IsSupported(SSE4_1));
  Roundps(dst.fp(), src.fp(), kRoundUp);
  return true;
}

bool LiftoffAssembler::emit_f32x4_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  DCHECK(CpuFeatures::IsSupported(SSE4_1));
  Roundps(dst.fp(), src.fp(), kRoundDown);
  return true;
}

bool LiftoffAssembler::emit_f32x4_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  DCHECK(CpuFeatures::IsSupported(SSE4_1));
  Roundps(dst.fp(), src.fp(), kRoundToZero);
  return true;
}

bool LiftoffAssembler::emit_f32x4_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  DCHECK(CpuFeatures::IsSupported(SSE4_1));
  Roundps(dst.fp(), src.fp(), kRoundToNearest);
  return true;
}

void LiftoffAssembler::emit_f32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vaddps, &Assembler::addps>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vsubps, &Assembler::subps>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vmulps, &Assembler::mulps>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32x4_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vdivps, &Assembler::divps>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32x4_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  F32x4Min(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_f32x4_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  F32x4Max(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_f32x4_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  // Due to the way minps works, pmin(a, b) = minps(b, a).
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vminps, &Assembler::minps>(
      this, dst, rhs, lhs);
}

void LiftoffAssembler::emit_f32x4_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  // Due to the way maxps works, pmax(a, b) = maxps(b, a).
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vmaxps, &Assembler::maxps>(
      this, dst, rhs, lhs);
}

void LiftoffAssembler::emit_f32x4_relaxed_min(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vminps, &Assembler::minps>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f32x4_relaxed_max(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vmaxps, &Assembler::maxps>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  Abspd(dst.fp(), src.fp(), kScratchRegister);
}

void LiftoffAssembler::emit_f64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  Negpd(dst.fp(), src.fp(), kScratchRegister);
}

void LiftoffAssembler::emit_f64x2_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  Sqrtpd(dst.fp(), src.fp());
}

bool LiftoffAssembler::emit_f64x2_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  DCHECK(CpuFeatures::IsSupported(SSE4_1));
  Roundpd(dst.fp(), src.fp(), kRoundUp);
  return true;
}

bool LiftoffAssembler::emit_f64x2_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  DCHECK(CpuFeatures::IsSupported(SSE4_1));
  Roundpd(dst.fp(), src.fp(), kRoundDown);
  return true;
}

bool LiftoffAssembler::emit_f64x2_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  DCHECK(CpuFeatures::IsSupported(SSE4_1));
  Roundpd(dst.fp(), src.fp(), kRoundToZero);
  return true;
}

bool LiftoffAssembler::emit_f64x2_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  DCHECK(CpuFeatures::IsSupported(SSE4_1));
  Roundpd(dst.fp(), src.fp(), kRoundToNearest);
  return true;
}

void LiftoffAssembler::emit_f64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vaddpd, &Assembler::addpd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vsubpd, &Assembler::subpd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vmulpd, &Assembler::mulpd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vdivpd, &Assembler::divpd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  F64x2Min(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_f64x2_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  F64x2Max(dst.fp(), lhs.fp(), rhs.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_f64x2_relaxed_min(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vminpd, &Assembler::minpd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_relaxed_max(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vmaxpd, &Assembler::maxpd>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_f64x2_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  // Due to the way minpd works, pmin(a, b) = minpd(b, a).
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vminpd, &Assembler::minpd>(
      this, dst, rhs, lhs);
}

void LiftoffAssembler::emit_f64x2_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  // Due to the way maxpd works, pmax(a, b) = maxpd(b, a).
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vmaxpd, &Assembler::maxpd>(
      this, dst, rhs, lhs);
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_s(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  Cvtdq2pd(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_u(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  F64x2ConvertLowI32x4U(dst.fp(), src.fp(), kScratchRegister);
}

void LiftoffAssembler::emit_f64x2_promote_low_f32x4(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  Cvtps2pd(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i32x4_sconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  I32x4SConvertF32x4(dst.fp(), src.fp(), kScratchDoubleReg, kScratchRegister);
}

void LiftoffAssembler::emit_i32x4_uconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  I32x4TruncF32x4U(dst.fp(), src.fp(), kScratchDoubleReg,
                   liftoff::kScratchDoubleReg2);
}

void LiftoffAssembler::emit_f32x4_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  Cvtdq2ps(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_f32x4_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  Pxor(kScratchDoubleReg, kScratchDoubleReg);           // Zeros.
  Pblendw(kScratchDoubleReg, src.fp(), uint8_t{0x55});  // Get lo 16 bits.
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vpsubd(dst.fp(), src.fp(), kScratchDoubleReg);  // Get hi 16 bits.
  } else {
    if (dst.fp() != src.fp()) movaps(dst.fp(), src.fp());
    psubd(dst.fp(), kScratchDoubleReg);
  }
  Cvtdq2ps(kScratchDoubleReg, kScratchDoubleReg);  // Convert lo exactly.
  Psrld(dst.fp(), uint8_t{1});         // Divide by 2 to get in unsigned range.
  Cvtdq2ps(dst.fp(), dst.fp());        // Convert hi, exactly.
  Addps(dst.fp(), dst.fp());           // Double hi, exactly.
  Addps(dst.fp(), kScratchDoubleReg);  // Add hi and lo, may round.
}

void LiftoffAssembler::emit_f32x4_demote_f64x2_zero(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  Cvtpd2ps(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i8x16_sconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpacksswb,
                                       &Assembler::packsswb>(this, dst, lhs,
                                                             rhs);
}

void LiftoffAssembler::emit_i8x16_uconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpackuswb,
                                       &Assembler::packuswb>(this, dst, lhs,
                                                             rhs);
}

void LiftoffAssembler::emit_i16x8_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpackssdw,
                                       &Assembler::packssdw>(this, dst, lhs,
                                                             rhs);
}

void LiftoffAssembler::emit_i16x8_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vpackusdw,
                                       &Assembler::packusdw>(this, dst, lhs,
                                                             rhs, SSE4_1);
}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  Pmovsxbw(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I16x8SConvertI8x16High(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  Pmovzxbw(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I16x8UConvertI8x16High(dst.fp(), src.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  Pmovsxwd(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I32x4SConvertI16x8High(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  Pmovzxwd(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I32x4UConvertI16x8High(dst.fp(), src.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_s_zero(LiftoffRegister dst,
                                                         LiftoffRegister src) {
  I32x4TruncSatF64x2SZero(dst.fp(), src.fp(), kScratchDoubleReg,
                          kScratchRegister);
}

void LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_u_zero(LiftoffRegister dst,
                                                         LiftoffRegister src) {
  I32x4TruncSatF64x2UZero(dst.fp(), src.fp(), kScratchDoubleReg,
                          kScratchRegister);
}

void LiftoffAssembler::emit_s128_and_not(LiftoffRegister dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  liftoff::EmitSimdNonCommutativeBinOp<&Assembler::vandnps, &Assembler::andnps>(
      this, dst, rhs, lhs);
}

void LiftoffAssembler::emit_i8x16_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpavgb, &Assembler::pavgb>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i16x8_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
  liftoff::EmitSimdCommutativeBinOp<&Assembler::vpavgw, &Assembler::pavgw>(
      this, dst, lhs, rhs);
}

void LiftoffAssembler::emit_i8x16_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  Pabsb(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i16x8_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  Pabsw(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  Pabsd(dst.fp(), src.fp());
}

void LiftoffAssembler::emit_i64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  I64x2Abs(dst.fp(), src.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_i8x16_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  Pextrb(dst.gp(), lhs.fp(), imm_lane_idx);
  movsxbl(dst.gp(), dst.gp());
}

void LiftoffAssembler::emit_i8x16_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  Pextrb(dst.gp(), lhs.fp(), imm_lane_idx);
}

void LiftoffAssembler::emit_i16x8_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  Pextrw(dst.gp(), lhs.fp(), imm_lane_idx);
  movsxwl(dst.gp(), dst.gp());
}

void LiftoffAssembler::emit_i16x8_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  Pextrw(dst.gp(), lhs.fp(), imm_lane_idx);
}

void LiftoffAssembler::emit_i32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  Pextrd(dst.gp(), lhs.fp(), imm_lane_idx);
}

void LiftoffAssembler::emit_i64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  Pextrq(dst.gp(), lhs.fp(), static_cast<int8_t>(imm_lane_idx));
}

void LiftoffAssembler::emit_f32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  F32x4ExtractLane(dst.fp(), lhs.fp(), imm_lane_idx);
}

void LiftoffAssembler::emit_f64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  F64x2ExtractLane(dst.fp(), lhs.fp(), imm_lane_idx);
}

void LiftoffAssembler::emit_i8x16_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vpinsrb(dst.fp(), src1.fp(), src2.gp(), imm_lane_idx);
  } else {
    CpuFeatureScope scope(this, SSE4_1);
    if (dst.fp() != src1.fp()) movaps(dst.fp(), src1.fp());
    pinsrb(dst.fp(), src2.gp(), imm_lane_idx);
  }
}

void LiftoffAssembler::emit_i16x8_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vpinsrw(dst.fp(), src1.fp(), src2.gp(), imm_lane_idx);
  } else {
    if (dst.fp() != src1.fp()) movaps(dst.fp(), src1.fp());
    pinsrw(dst.fp(), src2.gp(), imm_lane_idx);
  }
}

void LiftoffAssembler::emit_i32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vpinsrd(dst.fp(), src1.fp(), src2.gp(), imm_lane_idx);
  } else {
    CpuFeatureScope scope(this, SSE4_1);
    if (dst.fp() != src1.fp()) movaps(dst.fp(), src1.fp());
    pinsrd(dst.fp(), src2.gp(), imm_lane_idx);
  }
}

void LiftoffAssembler::emit_i64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vpinsrq(dst.fp(), src1.fp(), src2.gp(), imm_lane_idx);
  } else {
    CpuFeatureScope scope(this, SSE4_1);
    if (dst.fp() != src1.fp()) movaps(dst.fp(), src1.fp());
    pinsrq(dst.fp(), src2.gp(), imm_lane_idx);
  }
}

void LiftoffAssembler::emit_f32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (CpuFeatures::IsSupported(AVX)) {
    CpuFeatureScope scope(this, AVX);
    vinsertps(dst.fp(), src1.fp(), src2.fp(), (imm_lane_idx << 4) & 0x30);
  } else {
    CpuFeatureScope scope(this, SSE4_1);
    if (dst.fp() != src1.fp()) movaps(dst.fp(), src1.fp());
    insertps(dst.fp(), src2.fp(), (imm_lane_idx << 4) & 0x30);
  }
}

void LiftoffAssembler::emit_f64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  F64x2ReplaceLane(dst.fp(), src1.fp(), src2.fp(), imm_lane_idx);
}

void LiftoffAssembler::emit_f32x4_qfma(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  F32x4Qfma(dst.fp(), src1.fp(), src2.fp(), src3.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_f32x4_qfms(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  F32x4Qfms(dst.fp(), src1.fp(), src2.fp(), src3.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_f64x2_qfma(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  F64x2Qfma(dst.fp(), src1.fp(), src2.fp(), src3.fp(), kScratchDoubleReg);
}

void LiftoffAssembler::emit_f64x2_qfms(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  F64x2Qfms(dst.fp(), src1.fp(), src2.fp(), src3.fp(), kScratchDoubleReg);
}

bool LiftoffAssembler::emit_f16x8_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX2)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx2_scope(this, AVX2);
  vcvtps2ph(dst.fp(), src.fp(), 0);
  vpbroadcastw(dst.fp(), dst.fp());
  return true;
}

bool LiftoffAssembler::emit_f16x8_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  Pextrw(kScratchRegister, lhs.fp(), imm_lane_idx);
  vmovd(dst.fp(), kScratchRegister);
  vcvtph2ps(dst.fp(), dst.fp());
  return true;
}

bool LiftoffAssembler::emit_f16x8_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  vcvtps2ph(kScratchDoubleReg, src2.fp(), 0);
  vmovd(kScratchRegister, kScratchDoubleReg);
  vpinsrw(dst.fp(), src1.fp(), kScratchRegister, imm_lane_idx);
  return true;
}

bool LiftoffAssembler::emit_f16x8_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope avx_scope(this, AVX);
  Absph(dst.fp(), src.fp(), kScratchRegister);
  return true;
}

bool LiftoffAssembler::emit_f16x8_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope avx_scope(this, AVX);
  Negph(dst.fp(), src.fp(), kScratchRegister);
  return true;
}

bool LiftoffAssembler::emit_f16x8_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  vcvtph2ps(ydst, src.fp());
  vsqrtps(ydst, ydst);
  vcvtps2ph(dst.fp(), ydst, 0);
  return true;
}

bool LiftoffAssembler::emit_f16x8_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  vcvtph2ps(ydst, src.fp());
  vroundps(ydst, ydst, kRoundUp);
  vcvtps2ph(dst.fp(), ydst, 0);
  return true;
}

bool LiftoffAssembler::emit_f16x8_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  vcvtph2ps(ydst, src.fp());
  vroundps(ydst, ydst, kRoundDown);
  vcvtps2ph(dst.fp(), ydst, 0);
  return true;
}

bool LiftoffAssembler::emit_f16x8_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  vcvtph2ps(ydst, src.fp());
  vroundps(ydst, ydst, kRoundToZero);
  vcvtps2ph(dst.fp(), ydst, 0);
  return true;
}

bool LiftoffAssembler::emit_f16x8_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  vcvtph2ps(ydst, src.fp());
  vroundps(ydst, ydst, kRoundToNearest);
  vcvtps2ph(dst.fp(), ydst, 0);
  return true;
}

template <void (Assembler::*avx_op)(YMMRegister, YMMRegister, YMMRegister)>
bool F16x8CmpOpViaF32(LiftoffAssembler* assm, LiftoffRegister dst,
                      LiftoffRegister lhs, LiftoffRegister rhs) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX) ||
      !CpuFeatures::IsSupported(AVX2)) {
    return false;
  }
  CpuFeatureScope f16c_scope(assm, F16C);
  CpuFeatureScope avx_scope(assm, AVX);
  CpuFeatureScope avx2_scope(assm, AVX2);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  assm->vcvtph2ps(ydst, lhs.fp());
  assm->vcvtph2ps(kScratchSimd256Reg, rhs.fp());
  (assm->*avx_op)(ydst, ydst, kScratchSimd256Reg);
  assm->vpackssdw(ydst, ydst, ydst);
  return true;
}

bool LiftoffAssembler::emit_f16x8_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  return F16x8CmpOpViaF32<&Assembler::vcmpeqps>(this, dst, lhs, rhs);
}

bool LiftoffAssembler::emit_f16x8_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  return F16x8CmpOpViaF32<&Assembler::vcmpneqps>(this, dst, lhs, rhs);
}

bool LiftoffAssembler::emit_f16x8_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  return F16x8CmpOpViaF32<&Assembler::vcmpltps>(this, dst, lhs, rhs);
}

bool LiftoffAssembler::emit_f16x8_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  return F16x8CmpOpViaF32<&Assembler::vcmpleps>(this, dst, lhs, rhs);
}

template <void (Assembler::*avx_op)(YMMRegister, YMMRegister, YMMRegister)>
bool F16x8BinOpViaF32(LiftoffAssembler* assm, LiftoffRegister dst,
                      LiftoffRegister lhs, LiftoffRegister rhs) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX)) {
    return false;
  }
  CpuFeatureScope f16c_scope(assm, F16C);
  CpuFeatureScope avx_scope(assm, AVX);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  assm->vcvtph2ps(ydst, lhs.fp());
  assm->vcvtph2ps(kScratchSimd256Reg, rhs.fp());
  (assm->*avx_op)(ydst, ydst, kScratchSimd256Reg);
  assm->vcvtps2ph(dst.fp(), ydst, 0);
  return true;
}

bool LiftoffAssembler::emit_f16x8_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return F16x8BinOpViaF32<&Assembler::vaddps>(this, dst, lhs, rhs);
}

bool LiftoffAssembler::emit_f16x8_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return F16x8BinOpViaF32<&Assembler::vsubps>(this, dst, lhs, rhs);
}

bool LiftoffAssembler::emit_f16x8_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return F16x8BinOpViaF32<&Assembler::vmulps>(this, dst, lhs, rhs);
}

bool LiftoffAssembler::emit_f16x8_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  return F16x8BinOpViaF32<&Assembler::vdivps>(this, dst, lhs, rhs);
}

bool LiftoffAssembler::emit_f16x8_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX2)) {
    return false;
  }
  static constexpr RegClass res_rc = reg_class_for(kS128);
  LiftoffRegister tmp =
      GetUnusedRegister(res_rc, LiftoffRegList{dst, lhs, rhs});
  YMMRegister ytmp = YMMRegister::from_code(tmp.fp().code());
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  F16x8Min(ydst, lhs.fp(), rhs.fp(), kScratchSimd256Reg, ytmp);
  return true;
}

bool LiftoffAssembler::emit_f16x8_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX2)) {
    return false;
  }
  static constexpr RegClass res_rc = reg_class_for(kS128);
  LiftoffRegister tmp =
      GetUnusedRegister(res_rc, LiftoffRegList{dst, lhs, rhs});
  YMMRegister ytmp = YMMRegister::from_code(tmp.fp().code());
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  F16x8Max(ydst, lhs.fp(), rhs.fp(), kScratchSimd256Reg, ytmp);
  return true;
}

bool LiftoffAssembler::emit_f16x8_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  // Due to the way minps works, pmin(a, b) = minps(b, a).
  return F16x8BinOpViaF32<&Assembler::vminps>(this, dst, rhs, lhs);
}

bool LiftoffAssembler::emit_f16x8_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  // Due to the way maxps works, pmax(a, b) = maxps(b, a).
  return F16x8BinOpViaF32<&Assembler::vmaxps>(this, dst, rhs, lhs);
}

bool LiftoffAssembler::emit_i16x8_sconvert_f16x8(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX) ||
      !CpuFeatures::IsSupported(AVX2)) {
    return false;
  }

  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  CpuFeatureScope avx2_scope(this, AVX2);

  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  I16x8SConvertF16x8(ydst, src.fp(), kScratchSimd256Reg, kScratchRegister);
  return true;
}

bool LiftoffAssembler::emit_i16x8_uconvert_f16x8(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX) ||
      !CpuFeatures::IsSupported(AVX2)) {
    return false;
  }

  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  CpuFeatureScope avx2_scope(this, AVX2);

  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  I16x8TruncF16x8U(ydst, src.fp(), kScratchSimd256Reg);
  return true;
}

bool LiftoffAssembler::emit_f16x8_sconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX) ||
      !CpuFeatures::IsSupported(AVX2)) {
    return false;
  }

  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  CpuFeatureScope avx2_scope(this, AVX2);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  vpmovsxwd(ydst, src.fp());
  vcvtdq2ps(ydst, ydst);
  vcvtps2ph(dst.fp(), ydst, 0);
  return true;
}

bool LiftoffAssembler::emit_f16x8_uconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(AVX) ||
      !CpuFeatures::IsSupported(AVX2)) {
    return false;
  }

  CpuFeatureScope f16c_scope(this, F16C);
  CpuFeatureScope avx_scope(this, AVX);
  CpuFeatureScope avx2_scope(this, AVX2);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  vpmovzxwd(ydst, src.fp());
  vcvtdq2ps(ydst, ydst);
  vcvtps2ph(dst.fp(), ydst, 0);
  return true;
}

bool LiftoffAssembler::emit_f16x8_demote_f32x4_zero(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  YMMRegister ysrc = YMMRegister::from_code(src.fp().code());
  vcvtps2ph(dst.fp(), ysrc, 0);
  return true;
}

bool LiftoffAssembler::emit_f16x8_demote_f64x2_zero(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  return false;
}

bool LiftoffAssembler::emit_f32x4_promote_low_f16x8(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  if (!CpuFeatures::IsSupported(F16C)) {
    return false;
  }
  CpuFeatureScope f16c_scope(this, F16C);
  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  vcvtph2ps(ydst, src.fp());
  return true;
}

bool LiftoffAssembler::emit_f16x8_qfma(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(FMA3)) {
    return false;
  }

  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  YMMRegister tmp = YMMRegister::from_code(kScratchDoubleReg.code());
  YMMRegister tmp2 = YMMRegister::from_code(liftoff::kScratchDoubleReg2.code());
  F16x8Qfma(ydst, src1.fp(), src2.fp(), src3.fp(), tmp, tmp2);
  return true;
}

bool LiftoffAssembler::emit_f16x8_qfms(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  if (!CpuFeatures::IsSupported(F16C) || !CpuFeatures::IsSupported(FMA3)) {
    return false;
  }

  YMMRegister ydst = YMMRegister::from_code(dst.fp().code());
  YMMRegister tmp = YMMRegister::from_code(kScratchDoubleReg.code());
  YMMRegister tmp2 = YMMRegister::from_code(liftoff::kScratchDoubleReg2.code());
  F16x8Qfms(ydst, src1.fp(), src2.fp(), src3.fp(), tmp, tmp2);
  return true;
}

bool LiftoffAssembler::supports_f16_mem_access() {
  return CpuFeatures::IsSupported(F16C) && CpuFeatures::IsSupported(AVX2);
}

void LiftoffAssembler::set_trap_on_oob_mem64(Register index, uint64_t oob_size,
                                             uint64_t oob_index) {
  Label done;
  movq(kScratchRegister, Immediate64(oob_size));
  cmpq(index, kScratchRegister);
  j(below, &done);
  movq(index, Immediate64(oob_index));
  bind(&done);
}

void LiftoffAssembler::StackCheck(Label* ool_code) {
  cmpq(rsp, StackLimitAsOperand(StackLimitKind::kInterruptStackLimit));
  j(below_equal, ool_code);
}

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  MacroAssembler::AssertUnreachable(reason);
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  while (!gp_regs.is_empty()) {
    LiftoffRegister reg = gp_regs.GetFirstRegSet();
    pushq(reg.gp());
    gp_regs.clear(reg);
  }
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  unsigned num_fp_regs = fp_regs.GetNumRegsSet();
  if (num_fp_regs) {
    AllocateStackSpace(num_fp_regs * kSimd128Size);
    unsigned offset = 0;
    while (!fp_regs.is_empty()) {
      LiftoffRegister reg = fp_regs.GetFirstRegSet();
      Movdqu(Operand(rsp, offset), reg.fp());
      fp_regs.clear(reg);
      offset += kSimd128Size;
    }
    DCHECK_EQ(offset, num_fp_regs * kSimd128Size);
  }
}

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) {
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  unsigned fp_offset = 0;
  while (!fp_regs.is_empty()) {
    LiftoffRegister reg = fp_regs.GetFirstRegSet();
    Movdqu(reg.fp(), Operand(rsp, fp_offset));
    fp_regs.clear(reg);
    fp_offset += kSimd128Size;
  }
  if (fp_offset) addq(rsp, Immediate(fp_offset));
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  while (!gp_regs.is_empty()) {
    LiftoffRegister reg = gp_regs.GetLastRegSet();
    popq(reg.gp());
    gp_regs.clear(reg);
  }
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
  DCHECK_LT(num_stack_slots,
            (1 << 16) / kSystemPointerSize);  // 16 bit immediate
  ret(static_cast<int>(num_stack_slots * kSystemPointerSize));
}

void LiftoffAssembler::CallCWithStackBuffer(
    const std::initializer_list<VarState> args, const LiftoffRegister* rets,
    ValueKind return_kind, ValueKind out_argument_kind, int stack_bytes,
    ExternalReference ext_ref) {
  AllocateStackSpace(stack_bytes);

  int arg_offset = 0;
  for (const VarState& arg : args) {
    Operand dst{rsp, arg_offset};
    liftoff::StoreToMemory(this, dst, arg);
    arg_offset += value_kind_size(arg.kind());
  }
  DCHECK_LE(arg_offset, stack_bytes);

  // Pass a pointer to the buffer with the arguments to the C function.
  movq(kCArgRegs[0], rsp);

  constexpr int kNumCCallArgs = 1;

  // Now call the C function.
  PrepareCallCFunction(kNumCCallArgs);
  CallCFunction(ext_ref, kNumCCallArgs);

  // Move return value to the right register.
  const LiftoffRegister* next_result_reg = rets;
  if (return_kind != kVoid) {
    constexpr Register kReturnReg = rax;
    if (kReturnReg != next_result_reg->gp()) {
      Move(*next_result_reg, LiftoffRegister(kReturnReg), return_kind);
    }
    ++next_result_reg;
  }

  // Load potential output value from the buffer on the stack.
  if (out_argument_kind != kVoid) {
    liftoff::LoadFromStack(this, *next_result_reg, Operand(rsp, 0),
                           out_argument_kind);
  }

  addq(rsp, Immediate(stack_bytes));
}

void LiftoffAssembler::CallC(const std::initializer_list<VarState> args,
                             ExternalReference ext_ref) {
  // First, prepare the stack for the C call.
  int num_args = static_cast<int>(args.size());
  PrepareCallCFunction(num_args);

  // Then execute the parallel register move and also move values to parameter
  // stack slots.
  int reg_args = 0;
#ifdef V8_TARGET_OS_WIN
  // See comment on {kWindowsHomeStackSlots}.
  int stack_args = kWindowsHomeStackSlots;
#else
  int stack_args = 0;
#endif
  ParallelMove parallel_move{this};
  for (const VarState& arg : args) {
    if (reg_args < int{arraysize(kCArgRegs)}) {
      parallel_move.LoadIntoRegister(LiftoffRegister{kCArgRegs[reg_args]}, arg);
      ++reg_args;
    } else {
      Operand dst{rsp, stack_args * kSystemPointerSize};
      liftoff::StoreToMemory(this, dst, arg);
      ++stack_args;
    }
  }
  parallel_move.Execute();

  // Now call the C function.
  CallCFunction(ext_ref, num_args);
}

void LiftoffAssembler::CallNativeWasmCode(Address addr) {
  near_call(addr, RelocInfo::WASM_CALL);
}

void LiftoffAssembler::TailCallNativeWasmCode(Address addr) {
  near_jmp(addr, RelocInfo::WASM_CALL);
}

void LiftoffAssembler::CallIndirect(const ValueKindSig* sig,
                                    compiler::CallDescriptor* call_descriptor,
                                    Register target) {
  if (target == no_reg) {
    popq(kScratchRegister);
    target = kScratchRegister;
  }
  call(target);
}

void LiftoffAssembler::TailCallIndirect(Register target) {
  if (target == no_reg) {
    popq(kScratchRegister);
    target = kScratchRegister;
  }
  jmp(target);
}

void LiftoffAssembler::CallBuiltin(Builtin builtin) {
  // A direct call to a builtin. Just encode the builtin index. This will be
  // patched at relocation.
  near_call(static_cast<Address>(builtin), RelocInfo::WASM_STUB_CALL);
}

void LiftoffAssembler::AllocateStackSlot(Register addr, uint32_t size) {
  AllocateStackSpace(size);
  movq(addr, rsp);
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) {
  addq(rsp, Immediate(size));
}

void LiftoffAssembler::MaybeOSR() {
  cmpq(liftoff::kOSRTargetSlot, Immediate(0));
  j(not_equal, static_cast<Address>(Builtin::kWasmOnStackReplace),
    RelocInfo::WASM_STUB_CALL);
}

void LiftoffAssembler::emit_set_if_nan(Register dst, DoubleRegister src,
                                       ValueKind kind) {
  if (kind == kF32) {
    Ucomiss(src, src);
  } else {
    DCHECK_EQ(kind, kF64);
    Ucomisd(src, src);
  }
  Label ret;
  j(parity_odd, &ret);
  movl(Operand(dst, 0), Immediate(1));
  bind(&ret);
}

void LiftoffAssembler::emit_s128_set_if_nan(Register dst, LiftoffRegister src,
                                            Register tmp_gp,
                                            LiftoffRegister tmp_s128,
                                            ValueKind lane_kind) {
  if (lane_kind == kF32) {
    movaps(tmp_s128.fp(), src.fp());
    cmpunordps(tmp_s128.fp(), tmp_s128.fp());
  } else {
    DCHECK_EQ(lane_kind, kF64);
    movapd(tmp_s128.fp(), src.fp());
    cmpunordpd(tmp_s128.fp(), tmp_s128.fp());
  }
  pmovmskb(tmp_gp, tmp_s128.fp());
  orl(Operand(dst, 0), tmp_gp);
}

void LiftoffStackSlots::Construct(int param_slots) {
  DCHECK_LT(0, slots_.size());
  SortInPushOrder();
  int last_stack_slot = param_slots;
  for (auto& slot : slots_) {
    const int stack_slot = slot.dst_slot_;
    int stack_decrement = (last_stack_slot - stack_slot) * kSystemPointerSize;
    last_stack_slot = stack_slot;
    const LiftoffAssembler::VarState& src = slot.src_;
    DCHECK_LT(0, stack_decrement);
    switch (src.loc()) {
      case LiftoffAssembler::VarState::kStack:
        if (src.kind() == kI32) {
          asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
          // Load i32 values to a register first to ensure they are zero
          // extended.
          asm_->movl(kScratchRegister, liftoff::GetStackSlot(slot.src_offset_));
          asm_->pushq(kScratchRegister);
        } else if (src.kind() == kS128) {
          asm_->AllocateStackSpace(stack_decrement - kSimd128Size);
          // Since offsets are subtracted from sp, we need a smaller offset to
          // push the top of a s128 value.
          asm_->pushq(liftoff::GetStackSlot(slot.src_offset_ - 8));
          asm_->pushq(liftoff::GetStackSlot(slot.src_offset_));
        } else {
          asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
          // For all other types, just push the whole (8-byte) stack slot.
          // This is also ok for f32 values (even though we copy 4 uninitialized
          // bytes), because f32 and f64 values are clearly distinguished in
          // Turbofan, so the uninitialized bytes are never accessed.
          asm_->pushq(liftoff::GetStackSlot(slot.src_offset_));
        }
        break;
      case LiftoffAssembler::VarState::kRegister: {
        int pushed = src.kind() == kS128 ? kSimd128Size : kSystemPointerSize;
        liftoff::push(asm_, src.reg(), src.kind(), stack_decrement - pushed);
        break;
      }
      case LiftoffAssembler::VarState::kIntConst:
        asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
        asm_->pushq(Immediate(src.i32_const()));
        break;
    }
  }
}

#undef RETURN_FALSE_IF_MISSING_CPU_FEATURE

}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_X64_LIFTOFF_ASSEMBLER_X64_INL_H_
