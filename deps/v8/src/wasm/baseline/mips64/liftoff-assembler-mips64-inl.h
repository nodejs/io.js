// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_MIPS64_LIFTOFF_ASSEMBLER_MIPS64_INL_H_
#define V8_WASM_BASELINE_MIPS64_LIFTOFF_ASSEMBLER_MIPS64_INL_H_

#include "src/codegen/machine-type.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/wasm/baseline/liftoff-assembler.h"
#include "src/wasm/baseline/parallel-move-inl.h"
#include "src/wasm/object-access.h"
#include "src/wasm/wasm-linkage.h"
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

inline MemOperand GetStackSlot(int offset) { return MemOperand(fp, -offset); }

inline MemOperand GetInstanceDataOperand() {
  return GetStackSlot(WasmLiftoffFrameConstants::kInstanceDataOffset);
}

template <typename T>
inline MemOperand GetMemOp(LiftoffAssembler* assm, Register addr,
                           Register offset, T offset_imm,
                           bool i64_offset = false, unsigned shift_amount = 0) {
  if (offset != no_reg) {
    if (!i64_offset) {
      assm->Dext(kScratchReg, offset, 0, 32);
      offset = kScratchReg;
    }
    if (shift_amount != 0) {
      assm->Dlsa(kScratchReg, addr, offset, shift_amount);
    } else {
      assm->daddu(kScratchReg, offset, addr);
    }
    addr = kScratchReg;
  }
  if (is_int31(offset_imm)) {
    int32_t offset_imm32 = static_cast<int32_t>(offset_imm);
    return MemOperand(addr, offset_imm32);
  } else {
    assm->li(kScratchReg2, Operand(offset_imm));
    assm->daddu(kScratchReg, addr, kScratchReg2);
    return MemOperand(kScratchReg, 0);
  }
}

inline void Load(LiftoffAssembler* assm, LiftoffRegister dst, MemOperand src,
                 ValueKind kind) {
  switch (kind) {
    case kI16:
      assm->Lh(dst.gp(), src);
      break;
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
      assm->Lwc1(dst.fp(), src);
      break;
    case kF64:
      assm->Ldc1(dst.fp(), src);
      break;
    case kS128:
      assm->ld_b(dst.fp().toW(), src);
      break;
    default:
      UNREACHABLE();
  }
}

inline void Store(LiftoffAssembler* assm, MemOperand dst, LiftoffRegister src,
                  ValueKind kind) {
  switch (kind) {
    case kI16:
      assm->Ush(src.gp(), dst, t8);
      break;
    case kI32:
      assm->Usw(src.gp(), dst);
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      assm->Usd(src.gp(), dst);
      break;
    case kF32:
      assm->Uswc1(src.fp(), dst, t8);
      break;
    case kF64:
      assm->Usdc1(src.fp(), dst, t8);
      break;
    case kS128:
      assm->st_b(src.fp().toW(), dst);
      break;
    default:
      UNREACHABLE();
  }
}

inline void Store(LiftoffAssembler* assm, Register base, int32_t offset,
                  LiftoffRegister src, ValueKind kind) {
  MemOperand dst(base, offset);
  Store(assm, dst, src, kind);
}

inline void push(LiftoffAssembler* assm, LiftoffRegister reg, ValueKind kind) {
  switch (kind) {
    case kI32:
      assm->daddiu(sp, sp, -kSystemPointerSize);
      assm->sw(reg.gp(), MemOperand(sp, 0));
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      assm->push(reg.gp());
      break;
    case kF32:
      assm->daddiu(sp, sp, -kSystemPointerSize);
      assm->swc1(reg.fp(), MemOperand(sp, 0));
      break;
    case kF64:
      assm->daddiu(sp, sp, -kSystemPointerSize);
      assm->Sdc1(reg.fp(), MemOperand(sp, 0));
      break;
    case kS128:
      assm->daddiu(sp, sp, -kSystemPointerSize * 2);
      assm->st_b(reg.fp().toW(), MemOperand(sp, 0));
      break;
    default:
      UNREACHABLE();
  }
}

#if defined(V8_TARGET_BIG_ENDIAN)
inline void ChangeEndiannessLoad(LiftoffAssembler* assm, LiftoffRegister dst,
                                 LoadType type, LiftoffRegList pinned) {
  bool is_float = false;
  LiftoffRegister tmp = dst;
  switch (type.value()) {
    case LoadType::kI64Load8U:
    case LoadType::kI64Load8S:
    case LoadType::kI32Load8U:
    case LoadType::kI32Load8S:
      // No need to change endianness for byte size.
      return;
    case LoadType::kF32Load:
      is_float = true;
      tmp = assm->GetUnusedRegister(kGpReg, pinned);
      assm->emit_type_conversion(kExprI32ReinterpretF32, tmp, dst);
      [[fallthrough]];
    case LoadType::kI64Load32U:
      assm->MacroAssembler::ByteSwapUnsigned(tmp.gp(), tmp.gp(), 4);
      break;
    case LoadType::kI32Load:
    case LoadType::kI64Load32S:
      assm->MacroAssembler::ByteSwapSigned(tmp.gp(), tmp.gp(), 4);
      break;
    case LoadType::kI32Load16S:
    case LoadType::kI64Load16S:
      assm->MacroAssembler::ByteSwapSigned(tmp.gp(), tmp.gp(), 2);
      break;
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U:
      assm->MacroAssembler::ByteSwapUnsigned(tmp.gp(), tmp.gp(), 2);
      break;
    case LoadType::kF64Load:
      is_float = true;
      tmp = assm->GetUnusedRegister(kGpReg, pinned);
      assm->emit_type_conversion(kExprI64ReinterpretF64, tmp, dst);
      [[fallthrough]];
    case LoadType::kI64Load:
      assm->MacroAssembler::ByteSwapSigned(tmp.gp(), tmp.gp(), 8);
      break;
    default:
      UNREACHABLE();
  }

  if (is_float) {
    switch (type.value()) {
      case LoadType::kF32Load:
        assm->emit_type_conversion(kExprF32ReinterpretI32, dst, tmp);
        break;
      case LoadType::kF64Load:
        assm->emit_type_conversion(kExprF64ReinterpretI64, dst, tmp);
        break;
      default:
        UNREACHABLE();
    }
  }
}

inline void ChangeEndiannessStore(LiftoffAssembler* assm, LiftoffRegister src,
                                  StoreType type, LiftoffRegList pinned) {
  bool is_float = false;
  LiftoffRegister tmp = src;
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI32Store8:
      // No need to change endianness for byte size.
      return;
    case StoreType::kF32Store:
      is_float = true;
      tmp = assm->GetUnusedRegister(kGpReg, pinned);
      assm->emit_type_conversion(kExprI32ReinterpretF32, tmp, src);
      [[fallthrough]];
    case StoreType::kI32Store:
      assm->MacroAssembler::ByteSwapSigned(tmp.gp(), tmp.gp(), 4);
      break;
    case StoreType::kI32Store16:
      assm->MacroAssembler::ByteSwapSigned(tmp.gp(), tmp.gp(), 2);
      break;
    case StoreType::kF64Store:
      is_float = true;
      tmp = assm->GetUnusedRegister(kGpReg, pinned);
      assm->emit_type_conversion(kExprI64ReinterpretF64, tmp, src);
      [[fallthrough]];
    case StoreType::kI64Store:
      assm->MacroAssembler::ByteSwapSigned(tmp.gp(), tmp.gp(), 8);
      break;
    case StoreType::kI64Store32:
      assm->MacroAssembler::ByteSwapSigned(tmp.gp(), tmp.gp(), 4);
      break;
    case StoreType::kI64Store16:
      assm->MacroAssembler::ByteSwapSigned(tmp.gp(), tmp.gp(), 2);
      break;
    default:
      UNREACHABLE();
  }

  if (is_float) {
    switch (type.value()) {
      case StoreType::kF32Store:
        assm->emit_type_conversion(kExprF32ReinterpretI32, src, tmp);
        break;
      case StoreType::kF64Store:
        assm->emit_type_conversion(kExprF64ReinterpretI64, src, tmp);
        break;
      default:
        UNREACHABLE();
    }
  }
}
#endif  // V8_TARGET_BIG_ENDIAN

}  // namespace liftoff

int LiftoffAssembler::PrepareStackFrame() {
  int offset = pc_offset();
  // When the frame size is bigger than 4KB, we need seven instructions for
  // stack checking, so we reserve space for this case.
  daddiu(sp, sp, 0);
  nop();
  nop();
  nop();
  nop();
  nop();
  nop();
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

  // On MIPS64, we must push at least {ra} before calling the stub, otherwise
  // it would get clobbered with no possibility to recover it. So just set
  // up the frame here.
  EnterFrame(StackFrame::WASM);
  LoadConstant(LiftoffRegister(kLiftoffFrameSetupFunctionReg),
               WasmValue(declared_function_index));
  CallBuiltin(Builtin::kWasmLiftoffFrameSetup);
}

void LiftoffAssembler::PrepareTailCall(int num_callee_stack_params,
                                       int stack_param_delta) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Push the return address and frame pointer to complete the stack frame.
  Ld(scratch, MemOperand(fp, 8));
  Push(scratch);
  Ld(scratch, MemOperand(fp, 0));
  Push(scratch);

  // Shift the whole frame upwards.
  int slot_count = num_callee_stack_params + 2;
  for (int i = slot_count - 1; i >= 0; --i) {
    Ld(scratch, MemOperand(sp, i * 8));
    Sd(scratch, MemOperand(fp, (i - stack_param_delta) * 8));
  }

  // Set the new stack and frame pointer.
  daddiu(sp, fp, -stack_param_delta * 8);
  Pop(ra, fp);
}

void LiftoffAssembler::AlignFrameSize() {}

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
    patching_assembler.Daddu(sp, sp, Operand(-frame_size));
    return;
  }

  // The frame size is bigger than 4KB, so we might overflow the available stack
  // space if we first allocate the frame and then do the stack check (we will
  // need some remaining stack space for throwing the exception). That's why we
  // check the available stack space before we allocate the frame. To do this we
  // replace the {__ Daddu(sp, sp, -frame_size)} with a jump to OOL code that
  // does this "extended stack check".
  //
  // The OOL code can simply be generated here with the normal assembler,
  // because all other code generation, including OOL code, has already finished
  // when {PatchPrepareStackFrame} is called. The function prologue then jumps
  // to the current {pc_offset()} to execute the OOL code for allocating the
  // large frame.
  // Emit the unconditional branch in the function prologue (from {offset} to
  // {pc_offset()}).

  int imm32 = pc_offset() - offset - 3 * kInstrSize;
  patching_assembler.BranchLong(imm32);

  // If the frame is bigger than the stack, we throw the stack overflow
  // exception unconditionally. Thereby we can avoid the integer overflow
  // check in the condition code.
  RecordComment("OOL: stack check for large frame");
  Label continuation;
  if (frame_size < v8_flags.stack_size * 1024) {
    Register stack_limit = kScratchReg;
    LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit);
    Daddu(stack_limit, stack_limit, Operand(frame_size));
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
  Daddu(sp, sp, Operand(-frame_size));

  // Jump back to the start of the function, from {pc_offset()} to
  // right after the reserved space for the {__ Daddu(sp, sp, -framesize)}
  // (which is a Branch now).
  int func_start_offset = offset + 7 * kInstrSize;
  imm32 = func_start_offset - pc_offset() - 3 * kInstrSize;
  BranchLong(imm32);
}

void LiftoffAssembler::FinishCode() {}

void LiftoffAssembler::AbortCompilation() {}

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
  return kind == kS128 || is_reference(kind);
}

void LiftoffAssembler::CheckTierUp(int declared_func_index, int budget_used,
                                   Label* ool_label,
                                   const FreezeCacheState& frozen) {
  Register budget_array = kScratchReg;

  Register instance_data = cache_state_.cached_instance_data;
  if (instance_data == no_reg) {
    instance_data = budget_array;  // Reuse the scratch register.
    LoadInstanceDataFromFrame(instance_data);
  }

  constexpr int kArrayOffset = wasm::ObjectAccess::ToTagged(
      WasmTrustedInstanceData::kTieringBudgetArrayOffset);
  Ld(budget_array, MemOperand(instance_data, kArrayOffset));

  int budget_arr_offset = kInt32Size * declared_func_index;

  Register budget = kScratchReg2;
  MemOperand budget_addr(budget_array, budget_arr_offset);
  Lw(budget, budget_addr);
  Subu(budget, budget, budget_used);
  Sw(budget, budget_addr);

  Branch(ool_label, less, budget, Operand(zero_reg));
}

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value) {
  switch (value.type().kind()) {
    case kI32:
      MacroAssembler::li(reg.gp(), Operand(value.to_i32()));
      break;
    case kI64:
      MacroAssembler::li(reg.gp(), Operand(value.to_i64()));
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
  Ld(dst, liftoff::GetInstanceDataOperand());
}

void LiftoffAssembler::LoadTrustedPointer(Register dst, Register src_addr,
                                          int offset, IndirectPointerTag tag) {
  static_assert(!V8_ENABLE_SANDBOX_BOOL);
  static_assert(!COMPRESS_POINTERS_BOOL);
  Ld(dst, MemOperand{src_addr, offset});
}

void LiftoffAssembler::LoadFromInstance(Register dst, Register instance,
                                        int offset, int size) {
  DCHECK_LE(0, offset);
  switch (size) {
    case 1:
      Lb(dst, MemOperand(instance, offset));
      break;
    case 4:
      Lw(dst, MemOperand(instance, offset));
      break;
    case 8:
      Ld(dst, MemOperand(instance, offset));
      break;
    default:
      UNIMPLEMENTED();
  }
}

void LiftoffAssembler::LoadTaggedPointerFromInstance(Register dst,
                                                     Register instance,
                                                     int32_t offset) {
  static_assert(kTaggedSize == kSystemPointerSize);
  Ld(dst, MemOperand(instance, offset));
}

void LiftoffAssembler::SpillInstanceData(Register instance) {
  Sd(instance, liftoff::GetInstanceDataOperand());
}

void LiftoffAssembler::ResetOSRTarget() {}

void LiftoffAssembler::LoadTaggedPointer(Register dst, Register src_addr,
                                         Register offset_reg,
                                         int32_t offset_imm,
                                         uint32_t* protected_load_pc,
                                         bool needs_shift) {
  static_assert(kTaggedSize == kInt64Size);
  unsigned shift_amount = !needs_shift ? 0 : 3;
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm,
                                        false, shift_amount);
  Ld(dst, src_op);

  // Since LoadTaggedField might start with an instruction loading an immediate
  // argument to a register, we have to compute the {protected_load_pc} after
  // calling it.
  if (protected_load_pc) {
    *protected_load_pc = pc_offset() - kInstrSize;
  }
}

void LiftoffAssembler::LoadProtectedPointer(Register dst, Register src_addr,
                                            int32_t offset_imm) {
  static_assert(!V8_ENABLE_SANDBOX_BOOL);
  LoadTaggedPointer(dst, src_addr, no_reg, offset_imm);
}

void LiftoffAssembler::LoadFullPointer(Register dst, Register src_addr,
                                       int32_t offset_imm) {
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, no_reg, offset_imm);
  Ld(dst, src_op);
}

void LiftoffAssembler::StoreTaggedPointer(Register dst_addr,
                                          Register offset_reg,
                                          int32_t offset_imm, Register src,
                                          LiftoffRegList pinned,
                                          uint32_t* protected_store_pc,
                                          SkipWriteBarrier skip_write_barrier) {
  static_assert(kTaggedSize == kInt64Size);
  Register scratch = kScratchReg2;
  MemOperand dst_op = liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm);
  Sd(src, dst_op);

  // Since StoreTaggedField might start with an instruction loading an immediate
  // argument to a register, we have to compute the {protected_load_pc} after
  // calling it.
  if (protected_store_pc) {
    *protected_store_pc = pc_offset() - kInstrSize;
  }

  if (skip_write_barrier || v8_flags.disable_write_barriers) return;

  Label exit;
  CheckPageFlag(dst_addr, scratch,
                MemoryChunk::kPointersFromHereAreInterestingMask, kZero, &exit);
  JumpIfSmi(src, &exit);
  CheckPageFlag(src, scratch, MemoryChunk::kPointersToHereAreInterestingMask,
                eq, &exit);
  Daddu(scratch, dst_op.rm(), dst_op.offset());
  CallRecordWriteStubSaveRegisters(dst_addr, scratch, SaveFPRegsMode::kSave,
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
      MacroAssembler::Ulhu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16S:
    case LoadType::kI64Load16S:
      MacroAssembler::Ulh(dst.gp(), src_op);
      break;
    case LoadType::kI64Load32U:
      MacroAssembler::Ulwu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load:
    case LoadType::kI64Load32S:
      MacroAssembler::Ulw(dst.gp(), src_op);
      break;
    case LoadType::kI64Load:
      MacroAssembler::Uld(dst.gp(), src_op);
      break;
    case LoadType::kF32Load:
      MacroAssembler::Ulwc1(dst.fp(), src_op, t8);
      break;
    case LoadType::kF32LoadF16:
      UNIMPLEMENTED();
      break;
    case LoadType::kF64Load:
      MacroAssembler::Uldc1(dst.fp(), src_op, t8);
      break;
    case LoadType::kS128Load:
      MacroAssembler::ld_b(dst.fp().toW(), src_op);
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
  // Since load macros might start with an instruction loading an immediate
  // argument to a register, we have to compute the {protected_load_pc} after
  // calling them.
  if (protected_load_pc) {
    *protected_load_pc = pc_offset() - kInstrSize;
  }
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
    LiftoffRegister tmp = kScratchReg2;
    // Save original value.
    Move(tmp, src, type.value_type());

    src = tmp;
    pinned.set(tmp);
    liftoff::ChangeEndiannessStore(this, src, type, pinned);
  }
#endif

  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      Sb(src.gp(), dst_op);
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      MacroAssembler::Ush(src.gp(), dst_op, t8);
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      MacroAssembler::Usw(src.gp(), dst_op);
      break;
    case StoreType::kI64Store:
      MacroAssembler::Usd(src.gp(), dst_op);
      break;
    case StoreType::kF32Store:
      MacroAssembler::Uswc1(src.fp(), dst_op, t8);
      break;
    case StoreType::kF32StoreF16:
      UNIMPLEMENTED();
      break;
    case StoreType::kF64Store:
      MacroAssembler::Usdc1(src.fp(), dst_op, t8);
      break;
    case StoreType::kS128Store:
      MacroAssembler::st_b(src.fp().toW(), dst_op);
      break;
    default:
      UNREACHABLE();
  }

  // Since store macros might start with an instruction loading an immediate
  // argument to a register, we have to compute the {protected_store_pc} after
  // calling them.
  if (protected_store_pc) {
    *protected_store_pc = pc_offset() - kInstrSize;
  }
}

void LiftoffAssembler::AtomicLoad(LiftoffRegister dst, Register src_addr,
                                  Register offset_reg, uintptr_t offset_imm,
                                  LoadType type, LiftoffRegList pinned,
                                  bool i64_offset) {
  UseScratchRegisterScope temps(this);
  MemOperand src_op =
      liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm, i64_offset);
  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI64Load8U: {
      Lbu(dst.gp(), src_op);
      sync();
      return;
    }
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U: {
      Lhu(dst.gp(), src_op);
      sync();
      return;
    }
    case LoadType::kI32Load: {
      Lw(dst.gp(), src_op);
      sync();
      return;
    }
    case LoadType::kI64Load32U: {
      Lwu(dst.gp(), src_op);
      sync();
      return;
    }
    case LoadType::kI64Load: {
      Ld(dst.gp(), src_op);
      sync();
      return;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicStore(Register dst_addr, Register offset_reg,
                                   uintptr_t offset_imm, LiftoffRegister src,
                                   StoreType type, LiftoffRegList pinned,
                                   bool i64_offset) {
  UseScratchRegisterScope temps(this);
  MemOperand dst_op =
      liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm, i64_offset);
  switch (type.value()) {
    case StoreType::kI64Store8:
    case StoreType::kI32Store8: {
      sync();
      Sb(src.gp(), dst_op);
      return;
    }
    case StoreType::kI64Store16:
    case StoreType::kI32Store16: {
      sync();
      Sh(src.gp(), dst_op);
      return;
    }
    case StoreType::kI64Store32:
    case StoreType::kI32Store: {
      sync();
      Sw(src.gp(), dst_op);
      return;
    }
    case StoreType::kI64Store: {
      sync();
      Sd(src.gp(), dst_op);
      return;
    }
    default:
      UNREACHABLE();
  }
}

#define ASSEMBLE_ATOMIC_BINOP(load_linked, store_conditional, bin_instr) \
  do {                                                                   \
    Label binop;                                                         \
    sync();                                                              \
    bind(&binop);                                                        \
    load_linked(result.gp(), MemOperand(temp0, 0));                      \
    bin_instr(temp1, result.gp(), Operand(value.gp()));                  \
    store_conditional(temp1, MemOperand(temp0, 0));                      \
    BranchShort(&binop, eq, temp1, Operand(zero_reg));                   \
    sync();                                                              \
  } while (0)

#define ASSEMBLE_ATOMIC_BINOP_EXT(load_linked, store_conditional, size, \
                                  bin_instr, aligned)                   \
  do {                                                                  \
    Label binop;                                                        \
    andi(temp3, temp0, aligned);                                        \
    Dsubu(temp0, temp0, Operand(temp3));                                \
    sll(temp3, temp3, 3);                                               \
    sync();                                                             \
    bind(&binop);                                                       \
    load_linked(temp1, MemOperand(temp0, 0));                           \
    ExtractBits(result.gp(), temp1, temp3, size, false);                \
    bin_instr(temp2, result.gp(), value.gp());                          \
    InsertBits(temp1, temp2, temp3, size);                              \
    store_conditional(temp1, MemOperand(temp0, 0));                     \
    BranchShort(&binop, eq, temp1, Operand(zero_reg));                  \
    sync();                                                             \
  } while (0)

#define ATOMIC_BINOP_CASE(name, inst32, inst64)                                \
  void LiftoffAssembler::Atomic##name(                                         \
      Register dst_addr, Register offset_reg, uintptr_t offset_imm,            \
      LiftoffRegister value, LiftoffRegister result, StoreType type,           \
      bool i64_offset) {                                                       \
    LiftoffRegList pinned{dst_addr, value, result};                            \
    if (offset_reg != no_reg) pinned.set(offset_reg);                          \
    Register temp0 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();       \
    Register temp1 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();       \
    Register temp2 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();       \
    Register temp3 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();       \
    MemOperand dst_op =                                                        \
        liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm, i64_offset); \
    Daddu(temp0, dst_op.rm(), dst_op.offset());                                \
    switch (type.value()) {                                                    \
      case StoreType::kI64Store8:                                              \
        ASSEMBLE_ATOMIC_BINOP_EXT(Lld, Scd, 8, inst64, 7);                     \
        break;                                                                 \
      case StoreType::kI32Store8:                                              \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, 8, inst32, 3);                       \
        break;                                                                 \
      case StoreType::kI64Store16:                                             \
        ASSEMBLE_ATOMIC_BINOP_EXT(Lld, Scd, 16, inst64, 7);                    \
        break;                                                                 \
      case StoreType::kI32Store16:                                             \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, 16, inst32, 3);                      \
        break;                                                                 \
      case StoreType::kI64Store32:                                             \
        ASSEMBLE_ATOMIC_BINOP_EXT(Lld, Scd, 32, inst64, 7);                    \
        break;                                                                 \
      case StoreType::kI32Store:                                               \
        ASSEMBLE_ATOMIC_BINOP(Ll, Sc, inst32);                                 \
        break;                                                                 \
      case StoreType::kI64Store:                                               \
        ASSEMBLE_ATOMIC_BINOP(Lld, Scd, inst64);                               \
        break;                                                                 \
      default:                                                                 \
        UNREACHABLE();                                                         \
    }                                                                          \
  }

ATOMIC_BINOP_CASE(Add, Addu, Daddu)
ATOMIC_BINOP_CASE(Sub, Subu, Dsubu)
ATOMIC_BINOP_CASE(And, And, And)
ATOMIC_BINOP_CASE(Or, Or, Or)
ATOMIC_BINOP_CASE(Xor, Xor, Xor)
#undef ASSEMBLE_ATOMIC_BINOP
#undef ASSEMBLE_ATOMIC_BINOP_EXT
#undef ATOMIC_BINOP_CASE

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(load_linked, store_conditional) \
  do {                                                                   \
    Label exchange;                                                      \
    sync();                                                              \
    bind(&exchange);                                                     \
    load_linked(result.gp(), MemOperand(temp0, 0));                      \
    mov(temp1, value.gp());                                              \
    store_conditional(temp1, MemOperand(temp0, 0));                      \
    BranchShort(&exchange, eq, temp1, Operand(zero_reg));                \
    sync();                                                              \
  } while (0)

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(load_linked, store_conditional, \
                                             size, aligned)                  \
  do {                                                                       \
    Label exchange;                                                          \
    andi(temp1, temp0, aligned);                                             \
    Dsubu(temp0, temp0, Operand(temp1));                                     \
    sll(temp1, temp1, 3);                                                    \
    sync();                                                                  \
    bind(&exchange);                                                         \
    load_linked(temp2, MemOperand(temp0, 0));                                \
    ExtractBits(result.gp(), temp2, temp1, size, false);                     \
    InsertBits(temp2, value.gp(), temp1, size);                              \
    store_conditional(temp2, MemOperand(temp0, 0));                          \
    BranchShort(&exchange, eq, temp2, Operand(zero_reg));                    \
    sync();                                                                  \
  } while (0)

void LiftoffAssembler::AtomicExchange(Register dst_addr, Register offset_reg,
                                      uintptr_t offset_imm,
                                      LiftoffRegister value,
                                      LiftoffRegister result, StoreType type,
                                      bool i64_offset) {
  LiftoffRegList pinned{dst_addr, value, result};
  if (offset_reg != no_reg) pinned.set(offset_reg);
  Register temp0 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp1 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp2 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  MemOperand dst_op =
      liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm, i64_offset);
  Daddu(temp0, dst_op.rm(), dst_op.offset());
  switch (type.value()) {
    case StoreType::kI64Store8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, 8, 7);
      break;
    case StoreType::kI32Store8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll, Sc, 8, 3);
      break;
    case StoreType::kI64Store16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, 16, 7);
      break;
    case StoreType::kI32Store16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll, Sc, 16, 3);
      break;
    case StoreType::kI64Store32:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, 32, 7);
      break;
    case StoreType::kI32Store:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(Ll, Sc);
      break;
    case StoreType::kI64Store:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(Lld, Scd);
      break;
    default:
      UNREACHABLE();
  }
}
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(load_linked,       \
                                                 store_conditional) \
  do {                                                              \
    Label compareExchange;                                          \
    Label exit;                                                     \
    sync();                                                         \
    bind(&compareExchange);                                         \
    load_linked(result.gp(), MemOperand(temp0, 0));                 \
    BranchShort(&exit, ne, expected.gp(), Operand(result.gp()));    \
    mov(temp2, new_value.gp());                                     \
    store_conditional(temp2, MemOperand(temp0, 0));                 \
    BranchShort(&compareExchange, eq, temp2, Operand(zero_reg));    \
    bind(&exit);                                                    \
    sync();                                                         \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(            \
    load_linked, store_conditional, size, aligned)               \
  do {                                                           \
    Label compareExchange;                                       \
    Label exit;                                                  \
    andi(temp1, temp0, aligned);                                 \
    Dsubu(temp0, temp0, Operand(temp1));                         \
    sll(temp1, temp1, 3);                                        \
    sync();                                                      \
    bind(&compareExchange);                                      \
    load_linked(temp2, MemOperand(temp0, 0));                    \
    ExtractBits(result.gp(), temp2, temp1, size, false);         \
    ExtractBits(temp2, expected.gp(), zero_reg, size, false);    \
    BranchShort(&exit, ne, temp2, Operand(result.gp()));         \
    InsertBits(temp2, new_value.gp(), temp1, size);              \
    store_conditional(temp2, MemOperand(temp0, 0));              \
    BranchShort(&compareExchange, eq, temp2, Operand(zero_reg)); \
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
  MemOperand dst_op =
      liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm, i64_offset);
  Daddu(temp0, dst_op.rm(), dst_op.offset());
  switch (type.value()) {
    case StoreType::kI64Store8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, 8, 7);
      break;
    case StoreType::kI32Store8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, 8, 3);
      break;
    case StoreType::kI64Store16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, 16, 7);
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
  Register scratch = kScratchReg;

  switch (kind) {
    case kI32:
    case kF32:
      Lw(scratch, liftoff::GetStackSlot(src_offset));
      Sw(scratch, liftoff::GetStackSlot(dst_offset));
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
    case kF64:
      Ld(scratch, liftoff::GetStackSlot(src_offset));
      Sd(scratch, liftoff::GetStackSlot(dst_offset));
      break;
    case kS128:
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
    MacroAssembler::move_v(dst.toW(), src.toW());
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
      Swc1(reg.fp(), dst);
      break;
    case kF64:
      MacroAssembler::Sdc1(reg.fp(), dst);
      break;
    case kS128:
      MacroAssembler::st_b(reg.fp().toW(), dst);
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Spill(int offset, WasmValue value) {
  RecordUsedSpillOffset(offset);
  MemOperand dst = liftoff::GetStackSlot(offset);
  switch (value.type().kind()) {
    case kI32: {
      MacroAssembler::li(kScratchReg, Operand(value.to_i32()));
      Sw(kScratchReg, dst);
      break;
    }
    case kI64:
    case kRef:
    case kRefNull: {
      MacroAssembler::li(kScratchReg, value.to_i64());
      Sd(kScratchReg, dst);
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
      Lwc1(reg.fp(), src);
      break;
    case kF64:
      MacroAssembler::Ldc1(reg.fp(), src);
      break;
    case kS128:
      MacroAssembler::ld_b(reg.fp().toW(), src);
      break;
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
    Daddu(a0, fp, Operand(-start - size));
    Daddu(a1, fp, Operand(-start));

    Label loop;
    bind(&loop);
    Sd(zero_reg, MemOperand(a0));
    daddiu(a0, a0, kSystemPointerSize);
    BranchShort(&loop, ne, a0, Operand(a1));

    Pop(a1, a0);
  }
}

void LiftoffAssembler::LoadSpillAddress(Register dst, int offset,
                                        ValueKind /* kind */) {
  Dsubu(dst, fp, Operand(offset));
}

void LiftoffAssembler::emit_i64_clz(LiftoffRegister dst, LiftoffRegister src) {
  MacroAssembler::Dclz(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_ctz(LiftoffRegister dst, LiftoffRegister src) {
  MacroAssembler::Dctz(dst.gp(), src.gp());
}

bool LiftoffAssembler::emit_i64_popcnt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  MacroAssembler::Dpopcnt(dst.gp(), src.gp());
  return true;
}

void LiftoffAssembler::IncrementSmi(LiftoffRegister dst, int offset) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  SmiUntag(scratch, MemOperand(dst.gp(), offset));
  Daddu(scratch, scratch, Operand(1));
  SmiTag(scratch);
  Sd(scratch, MemOperand(dst.gp(), offset));
}

void LiftoffAssembler::emit_i32_mul(Register dst, Register lhs, Register rhs) {
  MacroAssembler::Mul(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_divs(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));

  // Check if lhs == kMinInt and rhs == -1, since this case is unrepresentable.
  rotr(kScratchReg, lhs, 31);
  sltiu(kScratchReg2, kScratchReg, 2);
  movn(kScratchReg2, kScratchReg, kScratchReg2);
  addu(kScratchReg2, kScratchReg2, rhs);
  MacroAssembler::Branch(trap_div_unrepresentable, eq, kScratchReg2,
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
I32_BINOP(add, addu)
I32_BINOP(sub, subu)
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
I32_BINOP_I(add, Addu)
I32_BINOP_I(sub, Subu)
I32_BINOP_I(and, And)
I32_BINOP_I(or, Or)
I32_BINOP_I(xor, Xor)
// clang-format on

#undef I32_BINOP_I

void LiftoffAssembler::emit_i32_clz(Register dst, Register src) {
  MacroAssembler::Clz(dst, src);
}

void LiftoffAssembler::emit_i32_ctz(Register dst, Register src) {
  MacroAssembler::Ctz(dst, src);
}

bool LiftoffAssembler::emit_i32_popcnt(Register dst, Register src) {
  MacroAssembler::Popcnt(dst, src);
  return true;
}

#define I32_SHIFTOP(name, instruction)                               \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register src, \
                                         Register amount) {          \
    instruction(dst, src, amount);                                   \
  }
#define I32_SHIFTOP_I(name, instruction)                                \
  I32_SHIFTOP(name, instruction##v)                                     \
  void LiftoffAssembler::emit_i32_##name##i(Register dst, Register src, \
                                            int amount) {               \
    instruction(dst, src, amount & 31);                                 \
  }

I32_SHIFTOP_I(shl, sll)
I32_SHIFTOP_I(sar, sra)
I32_SHIFTOP_I(shr, srl)

#undef I32_SHIFTOP
#undef I32_SHIFTOP_I

void LiftoffAssembler::emit_i64_addi(LiftoffRegister dst, LiftoffRegister lhs,
                                     int64_t imm) {
  MacroAssembler::Daddu(dst.gp(), lhs.gp(), Operand(imm));
}

void LiftoffAssembler::emit_i64_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  MacroAssembler::Dmul(dst.gp(), lhs.gp(), rhs.gp());
}

void LiftoffAssembler::emit_i64_muli(LiftoffRegister dst, LiftoffRegister lhs,
                                     int32_t imm) {
  if (base::bits::IsPowerOfTwo(imm)) {
    emit_i64_shli(dst, lhs, base::bits::WhichPowerOfTwo(imm));
    return;
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MacroAssembler::li(scratch, Operand(imm));
  MacroAssembler::Dmul(dst.gp(), lhs.gp(), scratch);
}

bool LiftoffAssembler::emit_i64_divs(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));

  // Check if lhs == MinInt64 and rhs == -1, since this case is unrepresentable.
  drotr32(kScratchReg, lhs.gp(), 31);
  sltiu(kScratchReg2, kScratchReg, 2);
  movn(kScratchReg2, kScratchReg, kScratchReg2);
  daddu(kScratchReg2, kScratchReg2, rhs.gp());
  MacroAssembler::Branch(trap_div_unrepresentable, eq, kScratchReg2,
                         Operand(zero_reg));

  MacroAssembler::Ddiv(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_divu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Ddivu(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_rems(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Dmod(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_remu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Dmodu(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

#define I64_BINOP(name, instruction)                                   \
  void LiftoffAssembler::emit_i64_##name(                              \
      LiftoffRegister dst, LiftoffRegister lhs, LiftoffRegister rhs) { \
    instruction(dst.gp(), lhs.gp(), rhs.gp());                         \
  }

// clang-format off
I64_BINOP(add, daddu)
I64_BINOP(sub, dsubu)
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
#define I64_SHIFTOP_I(name, instruction)                                       \
  I64_SHIFTOP(name, instruction##v)                                            \
  void LiftoffAssembler::emit_i64_##name##i(LiftoffRegister dst,               \
                                            LiftoffRegister src, int amount) { \
    amount &= 63;                                                              \
    if (amount < 32)                                                           \
      instruction(dst.gp(), src.gp(), amount);                                 \
    else                                                                       \
      instruction##32(dst.gp(), src.gp(), amount - 32);                        \
  }

I64_SHIFTOP_I(shl, dsll)
I64_SHIFTOP_I(sar, dsra)
I64_SHIFTOP_I(shr, dsrl)

#undef I64_SHIFTOP
#undef I64_SHIFTOP_I

void LiftoffAssembler::emit_u32_to_uintptr(Register dst, Register src) {
  Dext(dst, src, 0, 32);
}

void LiftoffAssembler::clear_i32_upper_half(Register dst) {
  // Don't need to clear the upper halves of i32 values for sandbox on MIPS64,
  // because we'll explicitly zero-extend their lower halves before using them
  // for memory accesses anyway.
}

void LiftoffAssembler::emit_f32_neg(DoubleRegister dst, DoubleRegister src) {
  MacroAssembler::Neg_s(dst, src);
}

void LiftoffAssembler::emit_f64_neg(DoubleRegister dst, DoubleRegister src) {
  MacroAssembler::Neg_d(dst, src);
}

void LiftoffAssembler::emit_f32_min(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  Label ool, done;
  MacroAssembler::Float32Min(dst, lhs, rhs, &ool);
  Branch(&done);

  bind(&ool);
  MacroAssembler::Float32MinOutOfLine(dst, lhs, rhs);
  bind(&done);
}

void LiftoffAssembler::emit_f32_max(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  Label ool, done;
  MacroAssembler::Float32Max(dst, lhs, rhs, &ool);
  Branch(&done);

  bind(&ool);
  MacroAssembler::Float32MaxOutOfLine(dst, lhs, rhs);
  bind(&done);
}

void LiftoffAssembler::emit_f32_copysign(DoubleRegister dst, DoubleRegister lhs,
                                         DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(MIPS_SIMD)) {
    DoubleRegister scratch = rhs;
    if (dst == rhs) {
      scratch = kScratchDoubleReg;
      Move_d(scratch, rhs);
    }
    if (dst != lhs) {
      Move_d(dst, lhs);
    }
    binsli_w(dst.toW(), scratch.toW(), 0);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    Register scratch2 = temps.Acquire();
    mfc1(scratch1, lhs);
    mfc1(scratch2, rhs);
    srl(scratch2, scratch2, 31);
    Ins(scratch1, scratch2, 31, 1);
    mtc1(scratch1, dst);
  }
}

void LiftoffAssembler::emit_f64_min(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  Label ool, done;
  MacroAssembler::Float64Min(dst, lhs, rhs, &ool);
  Branch(&done);

  bind(&ool);
  MacroAssembler::Float64MinOutOfLine(dst, lhs, rhs);
  bind(&done);
}

void LiftoffAssembler::emit_f64_max(DoubleRegister dst, DoubleRegister lhs,
                                    DoubleRegister rhs) {
  Label ool, done;
  MacroAssembler::Float64Max(dst, lhs, rhs, &ool);
  Branch(&done);

  bind(&ool);
  MacroAssembler::Float64MaxOutOfLine(dst, lhs, rhs);
  bind(&done);
}

void LiftoffAssembler::emit_f64_copysign(DoubleRegister dst, DoubleRegister lhs,
                                         DoubleRegister rhs) {
  if (CpuFeatures::IsSupported(MIPS_SIMD)) {
    DoubleRegister scratch = rhs;
    if (dst == rhs) {
      scratch = kScratchDoubleReg;
      Move_d(scratch, rhs);
    }
    if (dst != lhs) {
      Move_d(dst, lhs);
    }
    binsli_d(dst.toW(), scratch.toW(), 0);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    Register scratch2 = temps.Acquire();
    dmfc1(scratch1, lhs);
    dmfc1(scratch2, rhs);
    dsrl32(scratch2, scratch2, 31);
    Dins(scratch1, scratch2, 63, 1);
    dmtc1(scratch1, dst);
  }
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
    instruction(dst, src);                                                     \
    return true;                                                               \
  }

FP_BINOP(f32_add, add_s)
FP_BINOP(f32_sub, sub_s)
FP_BINOP(f32_mul, mul_s)
FP_BINOP(f32_div, div_s)
FP_UNOP(f32_abs, abs_s)
FP_UNOP_RETURN_TRUE(f32_ceil, Ceil_s_s)
FP_UNOP_RETURN_TRUE(f32_floor, Floor_s_s)
FP_UNOP_RETURN_TRUE(f32_trunc, Trunc_s_s)
FP_UNOP_RETURN_TRUE(f32_nearest_int, Round_s_s)
FP_UNOP(f32_sqrt, sqrt_s)
FP_BINOP(f64_add, add_d)
FP_BINOP(f64_sub, sub_d)
FP_BINOP(f64_mul, mul_d)
FP_BINOP(f64_div, div_d)
FP_UNOP(f64_abs, abs_d)
FP_UNOP_RETURN_TRUE(f64_ceil, Ceil_d_d)
FP_UNOP_RETURN_TRUE(f64_floor, Floor_d_d)
FP_UNOP_RETURN_TRUE(f64_trunc, Trunc_d_d)
FP_UNOP_RETURN_TRUE(f64_nearest_int, Round_d_d)
FP_UNOP(f64_sqrt, sqrt_d)

#undef FP_BINOP
#undef FP_UNOP
#undef FP_UNOP_RETURN_TRUE

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  switch (opcode) {
    case kExprI32ConvertI64:
      MacroAssembler::Ext(dst.gp(), src.gp(), 0, 32);
      return true;
    case kExprI32SConvertF32: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_s_s(rounded.fp(), src.fp());
      trunc_w_s(kScratchDoubleReg, rounded.fp());
      mfc1(dst.gp(), kScratchDoubleReg);
      // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
      // because INT32_MIN allows easier out-of-bounds detection.
      MacroAssembler::Addu(kScratchReg, dst.gp(), 1);
      MacroAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      MacroAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      mtc1(dst.gp(), kScratchDoubleReg);
      cvt_s_w(converted_back.fp(), kScratchDoubleReg);
      MacroAssembler::CompareF32(EQ, rounded.fp(), converted_back.fp());
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32UConvertF32: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_s_s(rounded.fp(), src.fp());
      MacroAssembler::Trunc_uw_s(dst.gp(), rounded.fp(), kScratchDoubleReg);
      // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
      // because 0 allows easier out-of-bounds detection.
      MacroAssembler::Addu(kScratchReg, dst.gp(), 1);
      MacroAssembler::Movz(dst.gp(), zero_reg, kScratchReg);

      // Checking if trap.
      MacroAssembler::Cvt_d_uw(converted_back.fp(), dst.gp());
      cvt_s_d(converted_back.fp(), converted_back.fp());
      MacroAssembler::CompareF32(EQ, rounded.fp(), converted_back.fp());
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32SConvertF64: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_d_d(rounded.fp(), src.fp());
      trunc_w_d(kScratchDoubleReg, rounded.fp());
      mfc1(dst.gp(), kScratchDoubleReg);

      // Checking if trap.
      cvt_d_w(converted_back.fp(), kScratchDoubleReg);
      MacroAssembler::CompareF64(EQ, rounded.fp(), converted_back.fp());
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32UConvertF64: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_d_d(rounded.fp(), src.fp());
      MacroAssembler::Trunc_uw_d(dst.gp(), rounded.fp(), kScratchDoubleReg);

      // Checking if trap.
      MacroAssembler::Cvt_d_uw(converted_back.fp(), dst.gp());
      MacroAssembler::CompareF64(EQ, rounded.fp(), converted_back.fp());
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32ReinterpretF32:
      MacroAssembler::FmoveLow(dst.gp(), src.fp());
      return true;
    case kExprI64SConvertI32:
      sll(dst.gp(), src.gp(), 0);
      return true;
    case kExprI64UConvertI32:
      MacroAssembler::Dext(dst.gp(), src.gp(), 0, 32);
      return true;
    case kExprI64SConvertF32: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_s_s(rounded.fp(), src.fp());
      trunc_l_s(kScratchDoubleReg, rounded.fp());
      dmfc1(dst.gp(), kScratchDoubleReg);
      // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
      // because INT64_MIN allows easier out-of-bounds detection.
      MacroAssembler::Daddu(kScratchReg, dst.gp(), 1);
      MacroAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      MacroAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      dmtc1(dst.gp(), kScratchDoubleReg);
      cvt_s_l(converted_back.fp(), kScratchDoubleReg);
      MacroAssembler::CompareF32(EQ, rounded.fp(), converted_back.fp());
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI64UConvertF32: {
      // Real conversion.
      MacroAssembler::Trunc_ul_s(dst.gp(), src.fp(), kScratchDoubleReg,
                                 kScratchReg);

      // Checking if trap.
      MacroAssembler::Branch(trap, eq, kScratchReg, Operand(zero_reg));
      return true;
    }
    case kExprI64SConvertF64: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_d_d(rounded.fp(), src.fp());
      trunc_l_d(kScratchDoubleReg, rounded.fp());
      dmfc1(dst.gp(), kScratchDoubleReg);
      // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
      // because INT64_MIN allows easier out-of-bounds detection.
      MacroAssembler::Daddu(kScratchReg, dst.gp(), 1);
      MacroAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      MacroAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      dmtc1(dst.gp(), kScratchDoubleReg);
      cvt_d_l(converted_back.fp(), kScratchDoubleReg);
      MacroAssembler::CompareF64(EQ, rounded.fp(), converted_back.fp());
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI64UConvertF64: {
      // Real conversion.
      MacroAssembler::Trunc_ul_d(dst.gp(), src.fp(), kScratchDoubleReg,
                                 kScratchReg);

      // Checking if trap.
      MacroAssembler::Branch(trap, eq, kScratchReg, Operand(zero_reg));
      return true;
    }
    case kExprI64ReinterpretF64:
      dmfc1(dst.gp(), src.fp());
      return true;
    case kExprF32SConvertI32: {
      LiftoffRegister scratch = GetUnusedRegister(kFpReg, LiftoffRegList{dst});
      mtc1(src.gp(), scratch.fp());
      cvt_s_w(dst.fp(), scratch.fp());
      return true;
    }
    case kExprF32UConvertI32:
      MacroAssembler::Cvt_s_uw(dst.fp(), src.gp());
      return true;
    case kExprF32ConvertF64:
      cvt_s_d(dst.fp(), src.fp());
      return true;
    case kExprF32ReinterpretI32:
      MacroAssembler::FmoveLow(dst.fp(), src.gp());
      return true;
    case kExprF64SConvertI32: {
      LiftoffRegister scratch = GetUnusedRegister(kFpReg, LiftoffRegList{dst});
      mtc1(src.gp(), scratch.fp());
      cvt_d_w(dst.fp(), scratch.fp());
      return true;
    }
    case kExprF64UConvertI32:
      MacroAssembler::Cvt_d_uw(dst.fp(), src.gp());
      return true;
    case kExprF64ConvertF32:
      cvt_d_s(dst.fp(), src.fp());
      return true;
    case kExprF64ReinterpretI64:
      dmtc1(src.gp(), dst.fp());
      return true;
    case kExprI32SConvertSatF32: {
      // Other arches use round to zero here, so we follow.
      if (CpuFeatures::IsSupported(MIPS_SIMD)) {
        trunc_w_s(kScratchDoubleReg, src.fp());
        mfc1(dst.gp(), kScratchDoubleReg);
      } else {
        Label done;
        mov(dst.gp(), zero_reg);
        CompareIsNanF32(src.fp(), src.fp());
        BranchTrueShortF(&done);
        li(dst.gp(), static_cast<int32_t>(std::numeric_limits<int32_t>::min()));
        MacroAssembler::Move(
            kScratchDoubleReg,
            static_cast<float>(std::numeric_limits<int32_t>::min()));
        CompareF32(OLT, src.fp(), kScratchDoubleReg);
        BranchTrueShortF(&done);
        trunc_w_s(kScratchDoubleReg, src.fp());
        mfc1(dst.gp(), kScratchDoubleReg);
        bind(&done);
      }
      return true;
    }
    case kExprI32UConvertSatF32: {
      Label isnan_or_lessthan_or_equal_zero;
      mov(dst.gp(), zero_reg);
      MacroAssembler::Move(kScratchDoubleReg, static_cast<float>(0.0));
      CompareF32(ULE, src.fp(), kScratchDoubleReg);
      BranchTrueShortF(&isnan_or_lessthan_or_equal_zero);
      Trunc_uw_s(dst.gp(), src.fp(), kScratchDoubleReg);
      bind(&isnan_or_lessthan_or_equal_zero);
      return true;
    }
    case kExprI32SConvertSatF64: {
      if (CpuFeatures::IsSupported(MIPS_SIMD)) {
        trunc_w_d(kScratchDoubleReg, src.fp());
        mfc1(dst.gp(), kScratchDoubleReg);
      } else {
        Label done;
        mov(dst.gp(), zero_reg);
        CompareIsNanF64(src.fp(), src.fp());
        BranchTrueShortF(&done);
        li(dst.gp(), static_cast<int32_t>(std::numeric_limits<int32_t>::min()));
        MacroAssembler::Move(
            kScratchDoubleReg,
            static_cast<double>(std::numeric_limits<int32_t>::min()));
        CompareF64(OLT, src.fp(), kScratchDoubleReg);
        BranchTrueShortF(&done);
        trunc_w_d(kScratchDoubleReg, src.fp());
        mfc1(dst.gp(), kScratchDoubleReg);
        bind(&done);
      }
      return true;
    }
    case kExprI32UConvertSatF64: {
      Label isnan_or_lessthan_or_equal_zero;
      mov(dst.gp(), zero_reg);
      MacroAssembler::Move(kScratchDoubleReg, static_cast<double>(0.0));
      CompareF64(ULE, src.fp(), kScratchDoubleReg);
      BranchTrueShortF(&isnan_or_lessthan_or_equal_zero);
      Trunc_uw_d(dst.gp(), src.fp(), kScratchDoubleReg);
      bind(&isnan_or_lessthan_or_equal_zero);
      return true;
    }
    case kExprI64SConvertSatF32: {
      if (CpuFeatures::IsSupported(MIPS_SIMD)) {
        trunc_l_s(kScratchDoubleReg, src.fp());
        dmfc1(dst.gp(), kScratchDoubleReg);
      } else {
        Label done;
        mov(dst.gp(), zero_reg);
        CompareIsNanF32(src.fp(), src.fp());
        BranchTrueShortF(&done);
        li(dst.gp(), static_cast<int64_t>(std::numeric_limits<int64_t>::min()));
        MacroAssembler::Move(
            kScratchDoubleReg,
            static_cast<float>(std::numeric_limits<int64_t>::min()));
        CompareF32(OLT, src.fp(), kScratchDoubleReg);
        BranchTrueShortF(&done);
        trunc_l_s(kScratchDoubleReg, src.fp());
        dmfc1(dst.gp(), kScratchDoubleReg);
        bind(&done);
      }
      return true;
    }
    case kExprI64UConvertSatF32: {
      Label isnan_or_lessthan_or_equal_zero;
      mov(dst.gp(), zero_reg);
      MacroAssembler::Move(kScratchDoubleReg, static_cast<float>(0.0));
      CompareF32(ULE, src.fp(), kScratchDoubleReg);
      BranchTrueShortF(&isnan_or_lessthan_or_equal_zero);
      Trunc_ul_s(dst.gp(), src.fp(), kScratchDoubleReg, no_reg);
      bind(&isnan_or_lessthan_or_equal_zero);
      return true;
    }
    case kExprI64SConvertSatF64: {
      if (CpuFeatures::IsSupported(MIPS_SIMD)) {
        trunc_l_d(kScratchDoubleReg, src.fp());
        dmfc1(dst.gp(), kScratchDoubleReg);
      } else {
        Label done;
        mov(dst.gp(), zero_reg);
        CompareIsNanF64(src.fp(), src.fp());
        BranchTrueShortF(&done);
        li(dst.gp(), static_cast<int64_t>(std::numeric_limits<int64_t>::min()));
        MacroAssembler::Move(
            kScratchDoubleReg,
            static_cast<double>(std::numeric_limits<int64_t>::min()));
        CompareF64(OLT, src.fp(), kScratchDoubleReg);
        BranchTrueShortF(&done);
        trunc_l_d(kScratchDoubleReg, src.fp());
        dmfc1(dst.gp(), kScratchDoubleReg);
        bind(&done);
      }
      return true;
    }
    case kExprI64UConvertSatF64: {
      Label isnan_or_lessthan_or_equal_zero;
      mov(dst.gp(), zero_reg);
      MacroAssembler::Move(kScratchDoubleReg, static_cast<double>(0.0));
      CompareF64(ULE, src.fp(), kScratchDoubleReg);
      BranchTrueShortF(&isnan_or_lessthan_or_equal_zero);
      Trunc_ul_d(dst.gp(), src.fp(), kScratchDoubleReg, no_reg);
      bind(&isnan_or_lessthan_or_equal_zero);
      return true;
    }
    default:
      return false;
  }
}

void LiftoffAssembler::emit_i32_signextend_i8(Register dst, Register src) {
  seb(dst, src);
}

void LiftoffAssembler::emit_i32_signextend_i16(Register dst, Register src) {
  seh(dst, src);
}

void LiftoffAssembler::emit_i64_signextend_i8(LiftoffRegister dst,
                                              LiftoffRegister src) {
  seb(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_signextend_i16(LiftoffRegister dst,
                                               LiftoffRegister src) {
  seh(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_signextend_i32(LiftoffRegister dst,
                                               LiftoffRegister src) {
  sll(dst.gp(), src.gp(), 0);
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
    DCHECK(kind == kI32 || kind == kI64);
    MacroAssembler::Branch(label, cond, lhs, Operand(zero_reg));
  } else {
    DCHECK((kind == kI32 || kind == kI64) ||
           (is_reference(kind) && (cond == kEqual || cond == kNotEqual)));
    MacroAssembler::Branch(label, cond, lhs, Operand(rhs));
  }
}

void LiftoffAssembler::emit_i32_cond_jumpi(Condition cond, Label* label,
                                           Register lhs, int32_t imm,
                                           const FreezeCacheState& frozen) {
  MacroAssembler::Branch(label, cond, lhs, Operand(imm));
}

void LiftoffAssembler::emit_ptrsize_cond_jumpi(Condition cond, Label* label,
                                               Register lhs, int32_t imm,
                                               const FreezeCacheState& frozen) {
  MacroAssembler::Branch(label, cond, lhs, Operand(imm));
}

void LiftoffAssembler::emit_i32_eqz(Register dst, Register src) {
  sltiu(dst, src, 1);
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  CompareWord(cond, dst, lhs, Operand(rhs));
}

void LiftoffAssembler::emit_i64_eqz(Register dst, LiftoffRegister src) {
  sltiu(dst, src.gp(), 1);
}

void LiftoffAssembler::emit_i64_set_cond(Condition cond, Register dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  CompareWord(cond, dst, lhs.gp(), Operand(rhs.gp()));
}

namespace liftoff {

inline FPUCondition ConditionToConditionCmpFPU(Condition condition,
                                               bool* predicate) {
  switch (condition) {
    case kEqual:
      *predicate = true;
      return EQ;
    case kNotEqual:
      *predicate = false;
      return EQ;
    case kUnsignedLessThan:
      *predicate = true;
      return OLT;
    case kUnsignedGreaterThanEqual:
      *predicate = false;
      return OLT;
    case kUnsignedLessThanEqual:
      *predicate = true;
      return OLE;
    case kUnsignedGreaterThan:
      *predicate = false;
      return OLE;
    default:
      *predicate = true;
      break;
  }
  UNREACHABLE();
}

inline void EmitAnyTrue(LiftoffAssembler* assm, LiftoffRegister dst,
                        LiftoffRegister src) {
  Label all_false;
  assm->BranchMSA(&all_false, MSA_BRANCH_V, all_zero, src.fp().toW(),
                  USE_DELAY_SLOT);
  assm->li(dst.gp(), 0l);
  assm->li(dst.gp(), 1);
  assm->bind(&all_false);
}

inline void EmitAllTrue(LiftoffAssembler* assm, LiftoffRegister dst,
                        LiftoffRegister src, MSABranchDF msa_branch_df) {
  Label all_true;
  assm->BranchMSA(&all_true, msa_branch_df, all_not_zero, src.fp().toW(),
                  USE_DELAY_SLOT);
  assm->li(dst.gp(), 1);
  assm->li(dst.gp(), 0l);
  assm->bind(&all_true);
}

inline void StoreToMemory(LiftoffAssembler* assm, MemOperand dst,
                          const LiftoffAssembler::VarState& src) {
  if (src.is_reg()) {
    Store(assm, dst, src.reg(), src.kind());
    return;
  }

  UseScratchRegisterScope temps(assm);
  Register temp = temps.Acquire();
  if (src.is_const()) {
    if (src.i32_const() == 0) {
      temp = zero_reg;
    } else {
      assm->li(temp, src.i32_const());
    }
  } else {
    DCHECK(src.is_stack());
    if (value_kind_size(src.kind()) == 4) {
      assm->Lw(temp, liftoff::GetStackSlot(src.offset()));
    } else {
      assm->Ld(temp, liftoff::GetStackSlot(src.offset()));
    }
  }

  if (value_kind_size(src.kind()) == 4) {
    assm->Sw(temp, dst);
  } else {
    DCHECK_EQ(8, value_kind_size(src.kind()));
    assm->Sd(temp, dst);
  }
}

}  // namespace liftoff

void LiftoffAssembler::emit_f32_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  Label not_nan, cont;
  MacroAssembler::CompareIsNanF32(lhs, rhs);
  MacroAssembler::BranchFalseF(&not_nan);
  // If one of the operands is NaN, return 1 for f32.ne, else 0.
  if (cond == ne) {
    MacroAssembler::li(dst, 1);
  } else {
    MacroAssembler::Move(dst, zero_reg);
  }
  MacroAssembler::Branch(&cont);

  bind(&not_nan);

  MacroAssembler::li(dst, 1);
  bool predicate;
  FPUCondition fcond = liftoff::ConditionToConditionCmpFPU(cond, &predicate);
  MacroAssembler::CompareF32(fcond, lhs, rhs);
  if (predicate) {
    MacroAssembler::LoadZeroIfNotFPUCondition(dst);
  } else {
    MacroAssembler::LoadZeroIfFPUCondition(dst);
  }

  bind(&cont);
}

void LiftoffAssembler::emit_f64_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  Label not_nan, cont;
  MacroAssembler::CompareIsNanF64(lhs, rhs);
  MacroAssembler::BranchFalseF(&not_nan);
  // If one of the operands is NaN, return 1 for f64.ne, else 0.
  if (cond == ne) {
    MacroAssembler::li(dst, 1);
  } else {
    MacroAssembler::Move(dst, zero_reg);
  }
  MacroAssembler::Branch(&cont);

  bind(&not_nan);

  MacroAssembler::li(dst, 1);
  bool predicate;
  FPUCondition fcond = liftoff::ConditionToConditionCmpFPU(cond, &predicate);
  MacroAssembler::CompareF64(fcond, lhs, rhs);
  if (predicate) {
    MacroAssembler::LoadZeroIfNotFPUCondition(dst);
  } else {
    MacroAssembler::LoadZeroIfFPUCondition(dst);
  }

  bind(&cont);
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

void LiftoffAssembler::LoadTransform(LiftoffRegister dst, Register src_addr,
                                     Register offset_reg, uintptr_t offset_imm,
                                     LoadType type,
                                     LoadTransformationKind transform,
                                     uint32_t* protected_load_pc) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm);
  MSARegister dst_msa = dst.fp().toW();
  *protected_load_pc = pc_offset();
  MachineType memtype = type.mem_type();

  if (transform == LoadTransformationKind::kExtend) {
    Ld(scratch, src_op);
    if (memtype == MachineType::Int8()) {
      fill_d(dst_msa, scratch);
      clti_s_b(kSimd128ScratchReg, dst_msa, 0);
      ilvr_b(dst_msa, kSimd128ScratchReg, dst_msa);
    } else if (memtype == MachineType::Uint8()) {
      xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      fill_d(dst_msa, scratch);
      ilvr_b(dst_msa, kSimd128RegZero, dst_msa);
    } else if (memtype == MachineType::Int16()) {
      fill_d(dst_msa, scratch);
      clti_s_h(kSimd128ScratchReg, dst_msa, 0);
      ilvr_h(dst_msa, kSimd128ScratchReg, dst_msa);
    } else if (memtype == MachineType::Uint16()) {
      xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      fill_d(dst_msa, scratch);
      ilvr_h(dst_msa, kSimd128RegZero, dst_msa);
    } else if (memtype == MachineType::Int32()) {
      fill_d(dst_msa, scratch);
      clti_s_w(kSimd128ScratchReg, dst_msa, 0);
      ilvr_w(dst_msa, kSimd128ScratchReg, dst_msa);
    } else if (memtype == MachineType::Uint32()) {
      xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      fill_d(dst_msa, scratch);
      ilvr_w(dst_msa, kSimd128RegZero, dst_msa);
    }
  } else if (transform == LoadTransformationKind::kZeroExtend) {
    xor_v(dst_msa, dst_msa, dst_msa);
    if (memtype == MachineType::Int32()) {
      Lwu(scratch, src_op);
      insert_w(dst_msa, 0, scratch);
    } else {
      DCHECK_EQ(MachineType::Int64(), memtype);
      Ld(scratch, src_op);
      insert_d(dst_msa, 0, scratch);
    }
  } else {
    DCHECK_EQ(LoadTransformationKind::kSplat, transform);
    if (memtype == MachineType::Int8()) {
      Lb(scratch, src_op);
      fill_b(dst_msa, scratch);
    } else if (memtype == MachineType::Int16()) {
      Lh(scratch, src_op);
      fill_h(dst_msa, scratch);
    } else if (memtype == MachineType::Int32()) {
      Lw(scratch, src_op);
      fill_w(dst_msa, scratch);
    } else if (memtype == MachineType::Int64()) {
      Ld(scratch, src_op);
      fill_d(dst_msa, scratch);
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
  *protected_load_pc = pc_offset();
  LoadStoreLaneParams load_params(type.mem_type().representation(), laneidx);
  MacroAssembler::LoadLane(load_params.sz, dst.fp().toW(), laneidx, src_op);
}

void LiftoffAssembler::StoreLane(Register dst, Register offset,
                                 uintptr_t offset_imm, LiftoffRegister src,
                                 StoreType type, uint8_t lane,
                                 uint32_t* protected_store_pc,
                                 bool i64_offset) {
  MemOperand dst_op =
      liftoff::GetMemOp(this, dst, offset, offset_imm, i64_offset);
  if (protected_store_pc) *protected_store_pc = pc_offset();
  LoadStoreLaneParams store_params(type.mem_rep(), lane);
  MacroAssembler::StoreLane(store_params.sz, src.fp().toW(), lane, dst_op);
}

void LiftoffAssembler::emit_i8x16_shuffle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs,
                                          const uint8_t shuffle[16],
                                          bool is_swizzle) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();

  uint64_t control_hi = 0;
  uint64_t control_low = 0;
  for (int i = 7; i >= 0; i--) {
    control_hi <<= 8;
    control_hi |= shuffle[i + 8];
    control_low <<= 8;
    control_low |= shuffle[i];
  }

  if (dst_msa == lhs_msa) {
    move_v(kSimd128ScratchReg, lhs_msa);
    lhs_msa = kSimd128ScratchReg;
  } else if (dst_msa == rhs_msa) {
    move_v(kSimd128ScratchReg, rhs_msa);
    rhs_msa = kSimd128ScratchReg;
  }

  li(kScratchReg, control_low);
  insert_d(dst_msa, 0, kScratchReg);
  li(kScratchReg, control_hi);
  insert_d(dst_msa, 1, kScratchReg);
  vshf_b(dst_msa, rhs_msa, lhs_msa);
}

void LiftoffAssembler::emit_i8x16_swizzle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();

  if (dst == lhs) {
    move_v(kSimd128ScratchReg, lhs_msa);
    lhs_msa = kSimd128ScratchReg;
  }
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  move_v(dst_msa, rhs_msa);
  vshf_b(dst_msa, kSimd128RegZero, lhs_msa);
}

void LiftoffAssembler::emit_i8x16_relaxed_swizzle(LiftoffRegister dst,
                                                  LiftoffRegister lhs,
                                                  LiftoffRegister rhs) {
  bailout(kRelaxedSimd, "emit_i8x16_relaxed_swizzle");
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f32x4_s(LiftoffRegister dst,
                                                        LiftoffRegister src) {
  bailout(kRelaxedSimd, "emit_i32x4_relaxed_trunc_f32x4_s");
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f32x4_u(LiftoffRegister dst,
                                                        LiftoffRegister src) {
  bailout(kRelaxedSimd, "emit_i32x4_relaxed_trunc_f32x4_u");
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f64x2_s_zero(
    LiftoffRegister dst, LiftoffRegister src) {
  bailout(kRelaxedSimd, "emit_i32x4_relaxed_trunc_f64x2_s_zero");
}

void LiftoffAssembler::emit_i32x4_relaxed_trunc_f64x2_u_zero(
    LiftoffRegister dst, LiftoffRegister src) {
  bailout(kRelaxedSimd, "emit_i32x4_relaxed_trunc_f64x2_u_zero");
}

void LiftoffAssembler::emit_s128_relaxed_laneselect(LiftoffRegister dst,
                                                    LiftoffRegister src1,
                                                    LiftoffRegister src2,
                                                    LiftoffRegister mask,
                                                    int lane_width) {
  bailout(kRelaxedSimd, "emit_s128_relaxed_laneselect");
}

void LiftoffAssembler::emit_i8x16_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  fill_b(dst.fp().toW(), src.gp());
}

void LiftoffAssembler::emit_i16x8_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  fill_h(dst.fp().toW(), src.gp());
}

void LiftoffAssembler::emit_i32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  fill_w(dst.fp().toW(), src.gp());
}

void LiftoffAssembler::emit_i64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  fill_d(dst.fp().toW(), src.gp());
}

void LiftoffAssembler::emit_f32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  MacroAssembler::FmoveLow(kScratchReg, src.fp());
  fill_w(dst.fp().toW(), kScratchReg);
}

void LiftoffAssembler::emit_f64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  MacroAssembler::Move(kScratchReg, src.fp());
  fill_d(dst.fp().toW(), kScratchReg);
}

#define SIMD_BINOP(name1, name2, type)                                   \
  void LiftoffAssembler::emit_##name1##_extmul_low_##name2(              \
      LiftoffRegister dst, LiftoffRegister src1, LiftoffRegister src2) { \
    MacroAssembler::ExtMulLow(type, dst.fp().toW(), src1.fp().toW(),     \
                              src2.fp().toW());                          \
  }                                                                      \
  void LiftoffAssembler::emit_##name1##_extmul_high_##name2(             \
      LiftoffRegister dst, LiftoffRegister src1, LiftoffRegister src2) { \
    MacroAssembler::ExtMulHigh(type, dst.fp().toW(), src1.fp().toW(),    \
                               src2.fp().toW());                         \
  }

SIMD_BINOP(i16x8, i8x16_s, MSAS8)
SIMD_BINOP(i16x8, i8x16_u, MSAU8)

SIMD_BINOP(i32x4, i16x8_s, MSAS16)
SIMD_BINOP(i32x4, i16x8_u, MSAU16)

SIMD_BINOP(i64x2, i32x4_s, MSAS32)
SIMD_BINOP(i64x2, i32x4_u, MSAU32)

#undef SIMD_BINOP

#define SIMD_BINOP(name1, name2, type)                                    \
  void LiftoffAssembler::emit_##name1##_extadd_pairwise_##name2(          \
      LiftoffRegister dst, LiftoffRegister src) {                         \
    MacroAssembler::ExtAddPairwise(type, dst.fp().toW(), src.fp().toW()); \
  }

SIMD_BINOP(i16x8, i8x16_s, MSAS8)
SIMD_BINOP(i16x8, i8x16_u, MSAU8)
SIMD_BINOP(i32x4, i16x8_s, MSAS16)
SIMD_BINOP(i32x4, i16x8_u, MSAU16)
#undef SIMD_BINOP

void LiftoffAssembler::emit_i16x8_q15mulr_sat_s(LiftoffRegister dst,
                                                LiftoffRegister src1,
                                                LiftoffRegister src2) {
  mulr_q_h(dst.fp().toW(), src1.fp().toW(), src2.fp().toW());
}

void LiftoffAssembler::emit_i16x8_relaxed_q15mulr_s(LiftoffRegister dst,
                                                    LiftoffRegister src1,
                                                    LiftoffRegister src2) {
  bailout(kRelaxedSimd, "emit_i16x8_relaxed_q15mulr_s");
}

void LiftoffAssembler::emit_i16x8_dot_i8x16_i7x16_s(LiftoffRegister dst,
                                                    LiftoffRegister lhs,
                                                    LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_dot_i8x16_i7x16_s");
}

void LiftoffAssembler::emit_i32x4_dot_i8x16_i7x16_add_s(LiftoffRegister dst,
                                                        LiftoffRegister lhs,
                                                        LiftoffRegister rhs,
                                                        LiftoffRegister acc) {
  bailout(kSimd, "emit_i32x4_dot_i8x16_i7x16_add_s");
}

void LiftoffAssembler::emit_i8x16_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  ceq_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  ceq_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
  nor_v(dst.fp().toW(), dst.fp().toW(), dst.fp().toW());
}

void LiftoffAssembler::emit_i8x16_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  clt_s_b(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  clt_u_b(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  cle_s_b(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  cle_u_b(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  ceq_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  ceq_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
  nor_v(dst.fp().toW(), dst.fp().toW(), dst.fp().toW());
}

void LiftoffAssembler::emit_i16x8_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  clt_s_h(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  clt_u_h(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  cle_s_h(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  cle_u_h(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  ceq_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  ceq_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
  nor_v(dst.fp().toW(), dst.fp().toW(), dst.fp().toW());
}

void LiftoffAssembler::emit_i32x4_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  clt_s_w(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  clt_u_w(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  cle_s_w(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  cle_u_w(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  fceq_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  fcune_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  fclt_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  fcle_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  ceq_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  ceq_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
  nor_v(dst.fp().toW(), dst.fp().toW(), dst.fp().toW());
}

void LiftoffAssembler::emit_i64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  add_a_d(dst.fp().toW(), src.fp().toW(), kSimd128RegZero);
}

void LiftoffAssembler::emit_f64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  fceq_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  fcune_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f64x2_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  fclt_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f64x2_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  fcle_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_s128_const(LiftoffRegister dst,
                                       const uint8_t imms[16]) {
  MSARegister dst_msa = dst.fp().toW();
  uint64_t vals[2];
  memcpy(vals, imms, sizeof(vals));
  li(kScratchReg, vals[0]);
  insert_d(dst_msa, 0, kScratchReg);
  li(kScratchReg, vals[1]);
  insert_d(dst_msa, 1, kScratchReg);
}

void LiftoffAssembler::emit_s128_not(LiftoffRegister dst, LiftoffRegister src) {
  nor_v(dst.fp().toW(), src.fp().toW(), src.fp().toW());
}

void LiftoffAssembler::emit_s128_and(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  and_v(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_s128_or(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  or_v(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_s128_xor(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  xor_v(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_s128_and_not(LiftoffRegister dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  nor_v(kSimd128ScratchReg, rhs.fp().toW(), rhs.fp().toW());
  and_v(dst.fp().toW(), kSimd128ScratchReg, lhs.fp().toW());
}

void LiftoffAssembler::emit_s128_select(LiftoffRegister dst,
                                        LiftoffRegister src1,
                                        LiftoffRegister src2,
                                        LiftoffRegister mask) {
  if (dst == mask) {
    bsel_v(dst.fp().toW(), src2.fp().toW(), src1.fp().toW());
  } else {
    xor_v(kSimd128ScratchReg, src1.fp().toW(), src2.fp().toW());
    and_v(kSimd128ScratchReg, kSimd128ScratchReg, mask.fp().toW());
    xor_v(dst.fp().toW(), kSimd128ScratchReg, src2.fp().toW());
  }
}

void LiftoffAssembler::emit_i8x16_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  subv_b(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_v128_anytrue(LiftoffRegister dst,
                                         LiftoffRegister src) {
  liftoff::EmitAnyTrue(this, dst, src);
}

void LiftoffAssembler::emit_i8x16_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  liftoff::EmitAllTrue(this, dst, src, MSA_BRANCH_B);
}

void LiftoffAssembler::emit_i8x16_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  MSARegister scratch0 = kSimd128RegZero;
  MSARegister scratch1 = kSimd128ScratchReg;
  srli_b(scratch0, src.fp().toW(), 7);
  srli_h(scratch1, scratch0, 7);
  or_v(scratch0, scratch0, scratch1);
  srli_w(scratch1, scratch0, 14);
  or_v(scratch0, scratch0, scratch1);
  srli_d(scratch1, scratch0, 28);
  or_v(scratch0, scratch0, scratch1);
  shf_w(scratch1, scratch0, 0x0E);
  ilvev_b(scratch0, scratch1, scratch0);
  copy_u_h(dst.gp(), scratch0, 0);
}

void LiftoffAssembler::emit_i8x16_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fill_b(kSimd128ScratchReg, rhs.gp());
  sll_b(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  slli_b(dst.fp().toW(), lhs.fp().toW(), rhs & 7);
}

void LiftoffAssembler::emit_i8x16_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  fill_b(kSimd128ScratchReg, rhs.gp());
  sra_b(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  srai_b(dst.fp().toW(), lhs.fp().toW(), rhs & 7);
}

void LiftoffAssembler::emit_i8x16_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  fill_b(kSimd128ScratchReg, rhs.gp());
  srl_b(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  srli_b(dst.fp().toW(), lhs.fp().toW(), rhs & 7);
}

void LiftoffAssembler::emit_i8x16_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  addv_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_add_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  adds_s_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_add_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  adds_u_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  subv_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_sub_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  subs_s_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_sub_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  subs_u_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  min_s_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  min_u_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  max_s_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  max_u_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_popcnt(LiftoffRegister dst,
                                         LiftoffRegister src) {
  pcnt_b(dst.fp().toW(), src.fp().toW());
}

void LiftoffAssembler::emit_i16x8_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  subv_h(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i16x8_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  liftoff::EmitAllTrue(this, dst, src, MSA_BRANCH_H);
}

void LiftoffAssembler::emit_i16x8_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  MSARegister scratch0 = kSimd128RegZero;
  MSARegister scratch1 = kSimd128ScratchReg;
  srli_h(scratch0, src.fp().toW(), 15);
  srli_w(scratch1, scratch0, 15);
  or_v(scratch0, scratch0, scratch1);
  srli_d(scratch1, scratch0, 30);
  or_v(scratch0, scratch0, scratch1);
  shf_w(scratch1, scratch0, 0x0E);
  slli_d(scratch1, scratch1, 4);
  or_v(scratch0, scratch0, scratch1);
  copy_u_b(dst.gp(), scratch0, 0);
}

void LiftoffAssembler::emit_i16x8_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fill_h(kSimd128ScratchReg, rhs.gp());
  sll_h(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  slli_h(dst.fp().toW(), lhs.fp().toW(), rhs & 15);
}

void LiftoffAssembler::emit_i16x8_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  fill_h(kSimd128ScratchReg, rhs.gp());
  sra_h(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  srai_h(dst.fp().toW(), lhs.fp().toW(), rhs & 15);
}

void LiftoffAssembler::emit_i16x8_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  fill_h(kSimd128ScratchReg, rhs.gp());
  srl_h(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  srli_h(dst.fp().toW(), lhs.fp().toW(), rhs & 15);
}

void LiftoffAssembler::emit_i16x8_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  addv_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_add_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  adds_s_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_add_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  adds_u_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  subv_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_sub_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  subs_s_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_sub_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  subs_u_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  mulv_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  min_s_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  min_u_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  max_s_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  max_u_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  subv_w(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i32x4_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  liftoff::EmitAllTrue(this, dst, src, MSA_BRANCH_W);
}

void LiftoffAssembler::emit_i32x4_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  MSARegister scratch0 = kSimd128RegZero;
  MSARegister scratch1 = kSimd128ScratchReg;
  srli_w(scratch0, src.fp().toW(), 31);
  srli_d(scratch1, scratch0, 31);
  or_v(scratch0, scratch0, scratch1);
  shf_w(scratch1, scratch0, 0x0E);
  slli_d(scratch1, scratch1, 2);
  or_v(scratch0, scratch0, scratch1);
  copy_u_b(dst.gp(), scratch0, 0);
}

void LiftoffAssembler::emit_i32x4_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fill_w(kSimd128ScratchReg, rhs.gp());
  sll_w(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  slli_w(dst.fp().toW(), lhs.fp().toW(), rhs & 31);
}

void LiftoffAssembler::emit_i32x4_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  fill_w(kSimd128ScratchReg, rhs.gp());
  sra_w(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  srai_w(dst.fp().toW(), lhs.fp().toW(), rhs & 31);
}

void LiftoffAssembler::emit_i32x4_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  fill_w(kSimd128ScratchReg, rhs.gp());
  srl_w(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  srli_w(dst.fp().toW(), lhs.fp().toW(), rhs & 31);
}

void LiftoffAssembler::emit_i32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  addv_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  subv_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  mulv_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  min_s_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  min_u_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  max_s_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  max_u_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i32x4_dot_i16x8_s(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  dotp_s_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  subv_d(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i64x2_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  liftoff::EmitAllTrue(this, dst, src, MSA_BRANCH_D);
}

void LiftoffAssembler::emit_i64x2_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  srli_d(kSimd128RegZero, src.fp().toW(), 63);
  shf_w(kSimd128ScratchReg, kSimd128RegZero, 0x02);
  slli_d(kSimd128ScratchReg, kSimd128ScratchReg, 1);
  or_v(kSimd128RegZero, kSimd128RegZero, kSimd128ScratchReg);
  copy_u_b(dst.gp(), kSimd128RegZero, 0);
}

void LiftoffAssembler::emit_i64x2_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fill_d(kSimd128ScratchReg, rhs.gp());
  sll_d(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i64x2_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  slli_d(dst.fp().toW(), lhs.fp().toW(), rhs & 63);
}

void LiftoffAssembler::emit_i64x2_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  fill_d(kSimd128ScratchReg, rhs.gp());
  sra_d(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i64x2_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  srai_d(dst.fp().toW(), lhs.fp().toW(), rhs & 63);
}

void LiftoffAssembler::emit_i64x2_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  fill_d(kSimd128ScratchReg, rhs.gp());
  srl_d(dst.fp().toW(), lhs.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i64x2_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  srli_d(dst.fp().toW(), lhs.fp().toW(), rhs & 63);
}

void LiftoffAssembler::emit_i64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  addv_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  subv_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  mulv_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i64x2_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  clt_s_d(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_i64x2_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  cle_s_d(dst.fp().toW(), rhs.fp().toW(), lhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bclri_w(dst.fp().toW(), src.fp().toW(), 31);
}

void LiftoffAssembler::emit_f32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bnegi_w(dst.fp().toW(), src.fp().toW(), 31);
}

void LiftoffAssembler::emit_f32x4_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  fsqrt_w(dst.fp().toW(), src.fp().toW());
}

bool LiftoffAssembler::emit_f32x4_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  MSARoundW(dst.fp().toW(), src.fp().toW(), kRoundToPlusInf);
  return true;
}

bool LiftoffAssembler::emit_f32x4_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  MSARoundW(dst.fp().toW(), src.fp().toW(), kRoundToMinusInf);
  return true;
}

bool LiftoffAssembler::emit_f32x4_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  MSARoundW(dst.fp().toW(), src.fp().toW(), kRoundToZero);
  return true;
}

bool LiftoffAssembler::emit_f32x4_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  MSARoundW(dst.fp().toW(), src.fp().toW(), kRoundToNearest);
  return true;
}

void LiftoffAssembler::emit_f32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fadd_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fsub_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fmul_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fdiv_w(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f32x4_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();
  MSARegister scratch0 = kSimd128RegZero;
  MSARegister scratch1 = kSimd128ScratchReg;
  // If inputs are -0.0. and +0.0, then write -0.0 to scratch1.
  // scratch1 = (lhs == rhs) ?  (lhs | rhs) : (rhs | rhs).
  fseq_w(scratch0, lhs_msa, rhs_msa);
  bsel_v(scratch0, rhs_msa, lhs_msa);
  or_v(scratch1, scratch0, rhs_msa);
  // scratch0 = isNaN(scratch1) ? scratch1: lhs.
  fseq_w(scratch0, scratch1, scratch1);
  bsel_v(scratch0, scratch1, lhs_msa);
  // dst = (scratch1 <= scratch0) ? scratch1 : scratch0.
  fsle_w(dst_msa, scratch1, scratch0);
  bsel_v(dst_msa, scratch0, scratch1);
  // Canonicalize the result.
  fmin_w(dst_msa, dst_msa, dst_msa);
}

void LiftoffAssembler::emit_f32x4_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();
  MSARegister scratch0 = kSimd128RegZero;
  MSARegister scratch1 = kSimd128ScratchReg;
  // If inputs are -0.0. and +0.0, then write +0.0 to scratch1.
  // scratch1 = (lhs == rhs) ?  (lhs | rhs) : (rhs | rhs).
  fseq_w(scratch0, lhs_msa, rhs_msa);
  bsel_v(scratch0, rhs_msa, lhs_msa);
  and_v(scratch1, scratch0, rhs_msa);
  // scratch0 = isNaN(scratch1) ? scratch1: lhs.
  fseq_w(scratch0, scratch1, scratch1);
  bsel_v(scratch0, scratch1, lhs_msa);
  // dst = (scratch0 <= scratch1) ? scratch1 : scratch0.
  fsle_w(dst_msa, scratch0, scratch1);
  bsel_v(dst_msa, scratch0, scratch1);
  // Canonicalize the result.
  fmax_w(dst_msa, dst_msa, dst_msa);
}

void LiftoffAssembler::emit_f32x4_relaxed_min(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kRelaxedSimd, "emit_f32x4_relaxed_min");
}

void LiftoffAssembler::emit_f32x4_relaxed_max(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kRelaxedSimd, "emit_f32x4_relaxed_max");
}

void LiftoffAssembler::emit_f32x4_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();
  // dst = rhs < lhs ? rhs : lhs
  fclt_w(dst_msa, rhs_msa, lhs_msa);
  bsel_v(dst_msa, lhs_msa, rhs_msa);
}

void LiftoffAssembler::emit_f32x4_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();
  // dst = lhs < rhs ? rhs : lhs
  fclt_w(dst_msa, lhs_msa, rhs_msa);
  bsel_v(dst_msa, lhs_msa, rhs_msa);
}

void LiftoffAssembler::emit_f64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bclri_d(dst.fp().toW(), src.fp().toW(), 63);
}

void LiftoffAssembler::emit_f64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bnegi_d(dst.fp().toW(), src.fp().toW(), 63);
}

void LiftoffAssembler::emit_f64x2_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  fsqrt_d(dst.fp().toW(), src.fp().toW());
}

bool LiftoffAssembler::emit_f64x2_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  MSARoundD(dst.fp().toW(), src.fp().toW(), kRoundToPlusInf);
  return true;
}

bool LiftoffAssembler::emit_f64x2_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  MSARoundD(dst.fp().toW(), src.fp().toW(), kRoundToMinusInf);
  return true;
}

bool LiftoffAssembler::emit_f64x2_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  MSARoundD(dst.fp().toW(), src.fp().toW(), kRoundToZero);
  return true;
}

bool LiftoffAssembler::emit_f64x2_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  MSARoundD(dst.fp().toW(), src.fp().toW(), kRoundToNearest);
  return true;
}

void LiftoffAssembler::emit_f64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fadd_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fsub_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fmul_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f64x2_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  fdiv_d(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_f64x2_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();
  MSARegister scratch0 = kSimd128RegZero;
  MSARegister scratch1 = kSimd128ScratchReg;
  // If inputs are -0.0. and +0.0, then write -0.0 to scratch1.
  // scratch1 = (lhs == rhs) ?  (lhs | rhs) : (rhs | rhs).
  fseq_d(scratch0, lhs_msa, rhs_msa);
  bsel_v(scratch0, rhs_msa, lhs_msa);
  or_v(scratch1, scratch0, rhs_msa);
  // scratch0 = isNaN(scratch1) ? scratch1: lhs.
  fseq_d(scratch0, scratch1, scratch1);
  bsel_v(scratch0, scratch1, lhs_msa);
  // dst = (scratch1 <= scratch0) ? scratch1 : scratch0.
  fsle_d(dst_msa, scratch1, scratch0);
  bsel_v(dst_msa, scratch0, scratch1);
  // Canonicalize the result.
  fmin_d(dst_msa, dst_msa, dst_msa);
}

void LiftoffAssembler::emit_f64x2_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();
  MSARegister scratch0 = kSimd128RegZero;
  MSARegister scratch1 = kSimd128ScratchReg;
  // If inputs are -0.0. and +0.0, then write +0.0 to scratch1.
  // scratch1 = (lhs == rhs) ?  (lhs | rhs) : (rhs | rhs).
  fseq_d(scratch0, lhs_msa, rhs_msa);
  bsel_v(scratch0, rhs_msa, lhs_msa);
  and_v(scratch1, scratch0, rhs_msa);
  // scratch0 = isNaN(scratch1) ? scratch1: lhs.
  fseq_d(scratch0, scratch1, scratch1);
  bsel_v(scratch0, scratch1, lhs_msa);
  // dst = (scratch0 <= scratch1) ? scratch1 : scratch0.
  fsle_d(dst_msa, scratch0, scratch1);
  bsel_v(dst_msa, scratch0, scratch1);
  // Canonicalize the result.
  fmax_d(dst_msa, dst_msa, dst_msa);
}

void LiftoffAssembler::emit_f64x2_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();
  // dst = rhs < lhs ? rhs : lhs
  fclt_d(dst_msa, rhs_msa, lhs_msa);
  bsel_v(dst_msa, lhs_msa, rhs_msa);
}

void LiftoffAssembler::emit_f64x2_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  MSARegister dst_msa = dst.fp().toW();
  MSARegister lhs_msa = lhs.fp().toW();
  MSARegister rhs_msa = rhs.fp().toW();
  // dst = lhs < rhs ? rhs : lhs
  fclt_d(dst_msa, lhs_msa, rhs_msa);
  bsel_v(dst_msa, lhs_msa, rhs_msa);
}

void LiftoffAssembler::emit_f64x2_relaxed_min(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kRelaxedSimd, "emit_f64x2_relaxed_min");
}

void LiftoffAssembler::emit_f64x2_relaxed_max(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kRelaxedSimd, "emit_f64x2_relaxed_max");
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_s(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ilvr_w(kSimd128RegZero, kSimd128RegZero, src.fp().toW());
  slli_d(kSimd128RegZero, kSimd128RegZero, 32);
  srai_d(kSimd128RegZero, kSimd128RegZero, 32);
  ffint_s_d(dst.fp().toW(), kSimd128RegZero);
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_u(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ilvr_w(kSimd128RegZero, kSimd128RegZero, src.fp().toW());
  ffint_u_d(dst.fp().toW(), kSimd128RegZero);
}

void LiftoffAssembler::emit_f64x2_promote_low_f32x4(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  fexupr_d(dst.fp().toW(), src.fp().toW());
}

void LiftoffAssembler::emit_i32x4_sconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  ftrunc_s_w(dst.fp().toW(), src.fp().toW());
}

void LiftoffAssembler::emit_i32x4_uconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  ftrunc_u_w(dst.fp().toW(), src.fp().toW());
}

void LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_s_zero(LiftoffRegister dst,
                                                         LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ftrunc_s_d(kSimd128ScratchReg, src.fp().toW());
  sat_s_d(kSimd128ScratchReg, kSimd128ScratchReg, 31);
  pckev_w(dst.fp().toW(), kSimd128RegZero, kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_u_zero(LiftoffRegister dst,
                                                         LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ftrunc_u_d(kSimd128ScratchReg, src.fp().toW());
  sat_u_d(kSimd128ScratchReg, kSimd128ScratchReg, 31);
  pckev_w(dst.fp().toW(), kSimd128RegZero, kSimd128ScratchReg);
}

void LiftoffAssembler::emit_f32x4_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  ffint_s_w(dst.fp().toW(), src.fp().toW());
}

void LiftoffAssembler::emit_f32x4_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  ffint_u_w(dst.fp().toW(), src.fp().toW());
}

void LiftoffAssembler::emit_f32x4_demote_f64x2_zero(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  fexdo_w(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i8x16_sconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  sat_s_h(kSimd128ScratchReg, lhs.fp().toW(), 7);
  sat_s_h(dst.fp().toW(), lhs.fp().toW(), 7);
  pckev_b(dst.fp().toW(), dst.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i8x16_uconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  max_s_h(kSimd128ScratchReg, kSimd128RegZero, lhs.fp().toW());
  sat_u_h(kSimd128ScratchReg, kSimd128ScratchReg, 7);
  max_s_h(dst.fp().toW(), kSimd128RegZero, rhs.fp().toW());
  sat_u_h(dst.fp().toW(), dst.fp().toW(), 7);
  pckev_b(dst.fp().toW(), dst.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  sat_s_w(kSimd128ScratchReg, lhs.fp().toW(), 15);
  sat_s_w(dst.fp().toW(), lhs.fp().toW(), 15);
  pckev_h(dst.fp().toW(), dst.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  max_s_w(kSimd128ScratchReg, kSimd128RegZero, lhs.fp().toW());
  sat_u_w(kSimd128ScratchReg, kSimd128ScratchReg, 15);
  max_s_w(dst.fp().toW(), kSimd128RegZero, rhs.fp().toW());
  sat_u_w(dst.fp().toW(), dst.fp().toW(), 15);
  pckev_h(dst.fp().toW(), dst.fp().toW(), kSimd128ScratchReg);
}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  ilvr_b(kSimd128ScratchReg, src.fp().toW(), src.fp().toW());
  slli_h(dst.fp().toW(), kSimd128ScratchReg, 8);
  srai_h(dst.fp().toW(), dst.fp().toW(), 8);
}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  ilvl_b(kSimd128ScratchReg, src.fp().toW(), src.fp().toW());
  slli_h(dst.fp().toW(), kSimd128ScratchReg, 8);
  srai_h(dst.fp().toW(), dst.fp().toW(), 8);
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ilvr_b(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ilvl_b(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  ilvr_h(kSimd128ScratchReg, src.fp().toW(), src.fp().toW());
  slli_w(dst.fp().toW(), kSimd128ScratchReg, 16);
  srai_w(dst.fp().toW(), dst.fp().toW(), 16);
}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  ilvl_h(kSimd128ScratchReg, src.fp().toW(), src.fp().toW());
  slli_w(dst.fp().toW(), kSimd128ScratchReg, 16);
  srai_w(dst.fp().toW(), dst.fp().toW(), 16);
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ilvr_h(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ilvl_h(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i64x2_sconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  ilvr_w(kSimd128ScratchReg, src.fp().toW(), src.fp().toW());
  slli_d(dst.fp().toW(), kSimd128ScratchReg, 32);
  srai_d(dst.fp().toW(), dst.fp().toW(), 32);
}

void LiftoffAssembler::emit_i64x2_sconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  ilvl_w(kSimd128ScratchReg, src.fp().toW(), src.fp().toW());
  slli_d(dst.fp().toW(), kSimd128ScratchReg, 32);
  srai_d(dst.fp().toW(), dst.fp().toW(), 32);
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ilvr_w(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  ilvl_w(dst.fp().toW(), kSimd128RegZero, src.fp().toW());
}

void LiftoffAssembler::emit_i8x16_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
  aver_u_b(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i16x8_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
  aver_u_h(dst.fp().toW(), lhs.fp().toW(), rhs.fp().toW());
}

void LiftoffAssembler::emit_i8x16_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  asub_s_b(dst.fp().toW(), src.fp().toW(), kSimd128RegZero);
}

void LiftoffAssembler::emit_i16x8_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  asub_s_h(dst.fp().toW(), src.fp().toW(), kSimd128RegZero);
}

void LiftoffAssembler::emit_i32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  asub_s_w(dst.fp().toW(), src.fp().toW(), kSimd128RegZero);
}

void LiftoffAssembler::emit_i8x16_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  copy_s_b(dst.gp(), lhs.fp().toW(), imm_lane_idx);
}

void LiftoffAssembler::emit_i8x16_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  copy_u_b(dst.gp(), lhs.fp().toW(), imm_lane_idx);
}

void LiftoffAssembler::emit_i16x8_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  copy_s_h(dst.gp(), lhs.fp().toW(), imm_lane_idx);
}

void LiftoffAssembler::emit_i16x8_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  copy_u_h(dst.gp(), lhs.fp().toW(), imm_lane_idx);
}

void LiftoffAssembler::emit_i32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  copy_s_w(dst.gp(), lhs.fp().toW(), imm_lane_idx);
}

void LiftoffAssembler::emit_i64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  copy_s_d(dst.gp(), lhs.fp().toW(), imm_lane_idx);
}

void LiftoffAssembler::emit_f32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  copy_u_w(kScratchReg, lhs.fp().toW(), imm_lane_idx);
  MacroAssembler::FmoveLow(dst.fp(), kScratchReg);
}

void LiftoffAssembler::emit_f64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  copy_s_d(kScratchReg, lhs.fp().toW(), imm_lane_idx);
  MacroAssembler::Move(dst.fp(), kScratchReg);
}

void LiftoffAssembler::emit_i8x16_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (dst != src1) {
    move_v(dst.fp().toW(), src1.fp().toW());
  }
  insert_b(dst.fp().toW(), imm_lane_idx, src2.gp());
}

void LiftoffAssembler::emit_i16x8_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (dst != src1) {
    move_v(dst.fp().toW(), src1.fp().toW());
  }
  insert_h(dst.fp().toW(), imm_lane_idx, src2.gp());
}

void LiftoffAssembler::emit_i32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (dst != src1) {
    move_v(dst.fp().toW(), src1.fp().toW());
  }
  insert_w(dst.fp().toW(), imm_lane_idx, src2.gp());
}

void LiftoffAssembler::emit_i64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  if (dst != src1) {
    move_v(dst.fp().toW(), src1.fp().toW());
  }
  insert_d(dst.fp().toW(), imm_lane_idx, src2.gp());
}

void LiftoffAssembler::emit_f32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  MacroAssembler::FmoveLow(kScratchReg, src2.fp());
  if (dst != src1) {
    move_v(dst.fp().toW(), src1.fp().toW());
  }
  insert_w(dst.fp().toW(), imm_lane_idx, kScratchReg);
}

void LiftoffAssembler::emit_f64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  MacroAssembler::Move(kScratchReg, src2.fp());
  if (dst != src1) {
    move_v(dst.fp().toW(), src1.fp().toW());
  }
  insert_d(dst.fp().toW(), imm_lane_idx, kScratchReg);
}

void LiftoffAssembler::emit_f32x4_qfma(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  bailout(kRelaxedSimd, "emit_f32x4_qfma");
}

void LiftoffAssembler::emit_f32x4_qfms(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  bailout(kRelaxedSimd, "emit_f32x4_qfms");
}

void LiftoffAssembler::emit_f64x2_qfma(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  bailout(kRelaxedSimd, "emit_f64x2_qfma");
}

void LiftoffAssembler::emit_f64x2_qfms(LiftoffRegister dst,
                                       LiftoffRegister src1,
                                       LiftoffRegister src2,
                                       LiftoffRegister src3) {
  bailout(kRelaxedSimd, "emit_f64x2_qfms");
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

bool LiftoffAssembler::supports_f16_mem_access() { return false; }

void LiftoffAssembler::set_trap_on_oob_mem64(Register index, uint64_t oob_size,
                                             uint64_t oob_index) {
  UNREACHABLE();
}

void LiftoffAssembler::StackCheck(Label* ool_code) {
  Register limit_address = kScratchReg;
  LoadStackLimit(limit_address, StackLimitKind::kInterruptStackLimit);
  Branch(ool_code, ule, sp, Operand(limit_address));
}

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  if (v8_flags.debug_code) Abort(reason);
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  unsigned num_gp_regs = gp_regs.GetNumRegsSet();
  if (num_gp_regs) {
    unsigned offset = num_gp_regs * kSystemPointerSize;
    daddiu(sp, sp, -offset);
    while (!gp_regs.is_empty()) {
      LiftoffRegister reg = gp_regs.GetFirstRegSet();
      offset -= kSystemPointerSize;
      sd(reg.gp(), MemOperand(sp, offset));
      gp_regs.clear(reg);
    }
    DCHECK_EQ(offset, 0);
  }
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  unsigned num_fp_regs = fp_regs.GetNumRegsSet();
  if (num_fp_regs) {
    unsigned slot_size = IsEnabled(MIPS_SIMD) ? 16 : 8;
    daddiu(sp, sp, -(num_fp_regs * slot_size));
    unsigned offset = 0;
    while (!fp_regs.is_empty()) {
      LiftoffRegister reg = fp_regs.GetFirstRegSet();
      if (IsEnabled(MIPS_SIMD)) {
        MacroAssembler::st_d(reg.fp().toW(), MemOperand(sp, offset));
      } else {
        MacroAssembler::Sdc1(reg.fp(), MemOperand(sp, offset));
      }
      fp_regs.clear(reg);
      offset += slot_size;
    }
    DCHECK_EQ(offset, num_fp_regs * slot_size);
  }
}

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) {
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  unsigned fp_offset = 0;
  while (!fp_regs.is_empty()) {
    LiftoffRegister reg = fp_regs.GetFirstRegSet();
    if (IsEnabled(MIPS_SIMD)) {
      MacroAssembler::ld_d(reg.fp().toW(), MemOperand(sp, fp_offset));
    } else {
      MacroAssembler::Ldc1(reg.fp(), MemOperand(sp, fp_offset));
    }
    fp_regs.clear(reg);
    fp_offset += (IsEnabled(MIPS_SIMD) ? 16 : 8);
  }
  if (fp_offset) daddiu(sp, sp, fp_offset);
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  unsigned gp_offset = 0;
  while (!gp_regs.is_empty()) {
    LiftoffRegister reg = gp_regs.GetLastRegSet();
    ld(reg.gp(), MemOperand(sp, gp_offset));
    gp_regs.clear(reg);
    gp_offset += kSystemPointerSize;
  }
  daddiu(sp, sp, gp_offset);
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
  MacroAssembler::DropAndRet(static_cast<int>(num_stack_slots));
}

void LiftoffAssembler::CallCWithStackBuffer(
    const std::initializer_list<VarState> args, const LiftoffRegister* rets,
    ValueKind return_kind, ValueKind out_argument_kind, int stack_bytes,
    ExternalReference ext_ref) {
  Daddu(sp, sp, -stack_bytes);

  int arg_offset = 0;
  for (const VarState& arg : args) {
    liftoff::StoreToMemory(this, MemOperand{sp, arg_offset}, arg);
    arg_offset += value_kind_size(arg.kind());
  }
  DCHECK_LE(arg_offset, stack_bytes);

  // Pass a pointer to the buffer with the arguments to the C function.
  // On mips, the first argument is passed in {a0}.
  constexpr Register kFirstArgReg = a0;
  mov(kFirstArgReg, sp);

  // Now call the C function.
  constexpr int kNumCCallArgs = 1;
  PrepareCallCFunction(kNumCCallArgs, kScratchReg);
  CallCFunction(ext_ref, kNumCCallArgs);

  // Move return value to the right register.
  const LiftoffRegister* next_result_reg = rets;
  if (return_kind != kVoid) {
    constexpr Register kReturnReg = v0;
#ifdef USE_SIMULATOR
    // When call to a host function in simulator, if the function return an
    // int32 value, the simulator does not sign-extend it to int64 because
    // in simulator we do not know whether the function returns an int32 or
    // int64. so we need to sign extend it here.
    if (return_kind == kI32) {
      sll(next_result_reg->gp(), kReturnReg, 0);
    } else if (kReturnReg != next_result_reg->gp()) {
      Move(*next_result_reg, LiftoffRegister(kReturnReg), return_kind);
    }
#else
    if (kReturnReg != next_result_reg->gp()) {
      Move(*next_result_reg, LiftoffRegister(kReturnReg), return_kind);
    }
#endif
    ++next_result_reg;
  }

  // Load potential output value from the buffer on the stack.
  if (out_argument_kind != kVoid) {
    liftoff::Load(this, *next_result_reg, MemOperand(sp, 0), out_argument_kind);
  }

  Daddu(sp, sp, stack_bytes);
}

void LiftoffAssembler::CallC(const std::initializer_list<VarState> args_list,
                             ExternalReference ext_ref) {
  // First, prepare the stack for the C call.
  const int num_args = static_cast<int>(args_list.size());
  PrepareCallCFunction(num_args, kScratchReg);

  // Note: If we ever need more than eight arguments we would need to load the
  // stack arguments to registers (via LoadToRegister), then push them to the
  // stack.

  // Execute the parallel register move for register parameters.
  DCHECK_GE(arraysize(kCArgRegs), num_args);
  const VarState* const args = args_list.begin();
  ParallelMove parallel_move{this};
  for (int reg_arg = 0; reg_arg < num_args; ++reg_arg) {
    parallel_move.LoadIntoRegister(LiftoffRegister{kCArgRegs[reg_arg]},
                                   args[reg_arg]);
  }
  parallel_move.Execute();

  // Now call the C function.
  CallCFunction(ext_ref, num_args);
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
    pop(kScratchReg);
    Call(kScratchReg);
  } else {
    Call(target);
  }
}

void LiftoffAssembler::TailCallIndirect(Register target) {
  if (target == no_reg) {
    Pop(kScratchReg);
    Jump(kScratchReg);
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
  Daddu(sp, sp, -size);
  MacroAssembler::Move(addr, sp);
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) {
  Daddu(sp, sp, size);
}

void LiftoffAssembler::MaybeOSR() {}

void LiftoffAssembler::emit_set_if_nan(Register dst, FPURegister src,
                                       ValueKind kind) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Label not_nan;
  if (kind == kF32) {
    CompareIsNanF32(src, src);
  } else {
    DCHECK_EQ(kind, kF64);
    CompareIsNanF64(src, src);
  }
  BranchFalseShortF(&not_nan, USE_DELAY_SLOT);
  li(scratch, 1);
  Sw(dst, MemOperand(dst));
  bind(&not_nan);
}

void LiftoffAssembler::emit_s128_set_if_nan(Register dst, LiftoffRegister src,
                                            Register tmp_gp,
                                            LiftoffRegister tmp_s128,
                                            ValueKind lane_kind) {
  Label not_nan;
  if (lane_kind == kF32) {
    fcun_w(tmp_s128.fp().toW(), src.fp().toW(), src.fp().toW());
  } else {
    DCHECK_EQ(lane_kind, kF64);
    fcun_d(tmp_s128.fp().toW(), src.fp().toW(), src.fp().toW());
  }
  BranchMSA(&not_nan, MSA_BRANCH_V, all_zero, tmp_s128.fp().toW(),
            USE_DELAY_SLOT);
  li(tmp_gp, 1);
  Sw(tmp_gp, MemOperand(dst));
  bind(&not_nan);
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

}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_MIPS64_LIFTOFF_ASSEMBLER_MIPS64_INL_H_
