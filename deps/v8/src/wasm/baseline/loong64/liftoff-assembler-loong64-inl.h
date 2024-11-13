// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LOONG64_LIFTOFF_ASSEMBLER_LOONG64_INL_H_
#define V8_WASM_BASELINE_LOONG64_LIFTOFF_ASSEMBLER_LOONG64_INL_H_

#include "src/codegen/loong64/assembler-loong64-inl.h"
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
      assm->bstrpick_d(kScratchReg, offset, 31, 0);
      offset = kScratchReg;
    }
    if (shift_amount != 0) {
      assm->alsl_d(kScratchReg, offset, addr, shift_amount);
    } else {
      assm->add_d(kScratchReg, offset, addr);
    }
    addr = kScratchReg;
  }
  if (is_int31(offset_imm)) {
    int32_t offset_imm32 = static_cast<int32_t>(offset_imm);
    return MemOperand(addr, offset_imm32);
  } else {
    assm->li(kScratchReg2, Operand(offset_imm));
    assm->add_d(kScratchReg2, addr, kScratchReg2);
    return MemOperand(kScratchReg2, 0);
  }
}

inline void Load(LiftoffAssembler* assm, LiftoffRegister dst, MemOperand src,
                 ValueKind kind) {
  switch (kind) {
    case kI16:
      assm->Ld_h(dst.gp(), src);
      break;
    case kI32:
      assm->Ld_w(dst.gp(), src);
      break;
    case kI64:
    case kRef:
    case kRefNull:
    case kRtt:
      assm->Ld_d(dst.gp(), src);
      break;
    case kF32:
      assm->Fld_s(dst.fp(), src);
      break;
    case kF64:
      assm->Fld_d(dst.fp(), src);
      break;
    case kS128:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
}

inline void Store(LiftoffAssembler* assm, MemOperand dst, LiftoffRegister src,
                  ValueKind kind) {
  switch (kind) {
    case kI16:
      assm->St_h(src.gp(), dst);
      break;
    case kI32:
      assm->St_w(src.gp(), dst);
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      assm->St_d(src.gp(), dst);
      break;
    case kF32:
      assm->Fst_s(src.fp(), dst);
      break;
    case kF64:
      assm->Fst_d(src.fp(), dst);
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
      assm->addi_d(sp, sp, -kSystemPointerSize);
      assm->St_w(reg.gp(), MemOperand(sp, 0));
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      assm->Push(reg.gp());
      break;
    case kF32:
      assm->addi_d(sp, sp, -kSystemPointerSize);
      assm->Fst_s(reg.fp(), MemOperand(sp, 0));
      break;
    case kF64:
      assm->addi_d(sp, sp, -kSystemPointerSize);
      assm->Fst_d(reg.fp(), MemOperand(sp, 0));
      break;
    case kS128:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
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
      assm->li(temp, static_cast<int64_t>(src.i32_const()));
    }
  } else {
    DCHECK(src.is_stack());
    if (value_kind_size(src.kind()) == 4) {
      assm->Ld_w(temp, liftoff::GetStackSlot(src.offset()));
    } else {
      assm->Ld_d(temp, liftoff::GetStackSlot(src.offset()));
    }
  }

  if (value_kind_size(src.kind()) == 4) {
    assm->St_w(temp, dst);
  } else {
    DCHECK_EQ(8, value_kind_size(src.kind()));
    assm->St_d(temp, dst);
  }
}

}  // namespace liftoff

int LiftoffAssembler::PrepareStackFrame() {
  int offset = pc_offset();
  // When constant that represents size of stack frame can't be represented
  // as 16bit we need three instructions to add it to sp, so we reserve space
  // for this case.
  addi_d(sp, sp, 0);
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

  // On LOONG64, we must push at least {ra} before calling the stub, otherwise
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
  Ld_d(scratch, MemOperand(fp, 8));
  Push(scratch);
  Ld_d(scratch, MemOperand(fp, 0));
  Push(scratch);

  // Shift the whole frame upwards.
  int slot_count = num_callee_stack_params + 2;
  for (int i = slot_count - 1; i >= 0; --i) {
    Ld_d(scratch, MemOperand(sp, i * 8));
    St_d(scratch, MemOperand(fp, (i - stack_param_delta) * 8));
  }

  // Set the new stack and frame pointer.
  addi_d(sp, fp, -stack_param_delta * 8);
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
    patching_assembler.Add_d(sp, sp, Operand(-frame_size));
    return;
  }

  // The frame size is bigger than 4KB, so we might overflow the available stack
  // space if we first allocate the frame and then do the stack check (we will
  // need some remaining stack space for throwing the exception). That's why we
  // check the available stack space before we allocate the frame. To do this we
  // replace the {__ Add_d(sp, sp, -frame_size)} with a jump to OOL code that
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
  CHECK(is_int26(imm32));
  patching_assembler.b(imm32 >> 2);

  // If the frame is bigger than the stack, we throw the stack overflow
  // exception unconditionally. Thereby we can avoid the integer overflow
  // check in the condition code.
  RecordComment("OOL: stack check for large frame");
  Label continuation;
  if (frame_size < v8_flags.stack_size * 1024) {
    Register stack_limit = kScratchReg;
    LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit);
    Add_d(stack_limit, stack_limit, Operand(frame_size));
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
  Add_d(sp, sp, Operand(-frame_size));

  // Jump back to the start of the function, from {pc_offset()} to
  // right after the reserved space for the {__ Add_d(sp, sp, -framesize)}
  // (which is a Branch now).
  int func_start_offset = offset + 3 * kInstrSize;
  imm32 = func_start_offset - pc_offset();
  CHECK(is_int26(imm32));
  b(imm32 >> 2);
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
  Ld_d(budget_array, MemOperand(instance_data, kArrayOffset));

  int budget_arr_offset = kInt32Size * declared_func_index;

  Register budget = kScratchReg2;
  MemOperand budget_addr(budget_array, budget_arr_offset);
  Ld_w(budget, budget_addr);
  Sub_w(budget, budget, budget_used);
  St_w(budget, budget_addr);

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
  Ld_d(dst, liftoff::GetInstanceDataOperand());
}

void LiftoffAssembler::LoadTrustedPointer(Register dst, Register src_addr,
                                          int offset, IndirectPointerTag tag) {
  MemOperand src{src_addr, offset};
  LoadTrustedPointerField(dst, src, tag);
}

void LiftoffAssembler::LoadFromInstance(Register dst, Register instance,
                                        int offset, int size) {
  DCHECK_LE(0, offset);
  switch (size) {
    case 1:
      Ld_b(dst, MemOperand(instance, offset));
      break;
    case 4:
      Ld_w(dst, MemOperand(instance, offset));
      break;
    case 8:
      Ld_d(dst, MemOperand(instance, offset));
      break;
    default:
      UNIMPLEMENTED();
  }
}

void LiftoffAssembler::LoadTaggedPointerFromInstance(Register dst,
                                                     Register instance,
                                                     int32_t offset) {
  LoadTaggedField(dst, MemOperand(instance, offset));
}

void LiftoffAssembler::SpillInstanceData(Register instance) {
  St_d(instance, liftoff::GetInstanceDataOperand());
}

void LiftoffAssembler::ResetOSRTarget() {}

void LiftoffAssembler::LoadTaggedPointer(Register dst, Register src_addr,
                                         Register offset_reg,
                                         int32_t offset_imm,
                                         uint32_t* protected_load_pc,
                                         bool needs_shift) {
  unsigned shift_amount = !needs_shift ? 0 : COMPRESS_POINTERS_BOOL ? 2 : 3;
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm,
                                        false, shift_amount);
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
  }
}

void LiftoffAssembler::LoadProtectedPointer(Register dst, Register src_addr,
                                            int32_t offset_imm) {
  LoadProtectedPointerField(dst, MemOperand{src_addr, offset_imm});
}

void LiftoffAssembler::LoadFullPointer(Register dst, Register src_addr,
                                       int32_t offset_imm) {
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, no_reg, offset_imm);
  Ld_d(dst, src_op);
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
    Add_d(effective_offset, offset_reg, Operand(offset_imm));
    offset_op = Operand(effective_offset);
  }

  if (offset_op.is_reg()) {
    StoreTaggedField(src, MemOperand(dst_addr, offset_op.rm()));
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
  BlockTrampolinePoolScope block_trampoline_pool(this);
  unsigned shift_amount = needs_shift ? type.size_log_2() : 0;
  MemOperand src_op = liftoff::GetMemOp(this, src_addr, offset_reg, offset_imm,
                                        i64_offset, shift_amount);

  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI64Load8U:
      Ld_bu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load8S:
    case LoadType::kI64Load8S:
      Ld_b(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U:
      MacroAssembler::Ld_hu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load16S:
    case LoadType::kI64Load16S:
      MacroAssembler::Ld_h(dst.gp(), src_op);
      break;
    case LoadType::kI64Load32U:
      MacroAssembler::Ld_wu(dst.gp(), src_op);
      break;
    case LoadType::kI32Load:
    case LoadType::kI64Load32S:
      MacroAssembler::Ld_w(dst.gp(), src_op);
      break;
    case LoadType::kI64Load:
      MacroAssembler::Ld_d(dst.gp(), src_op);
      break;
    case LoadType::kF32Load:
      MacroAssembler::Fld_s(dst.fp(), src_op);
      break;
    case LoadType::kF32LoadF16:
      UNIMPLEMENTED();
      break;
    case LoadType::kF64Load:
      MacroAssembler::Fld_d(dst.fp(), src_op);
      break;
    case LoadType::kS128Load:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
  // protected_load_pc should be the address of the load/store instruction.
  // The MacroAssembler load/store may contain some instructions for adjusting
  // MemOperand, so use pc_offset - kInstrSize to locate.
  if (protected_load_pc) *protected_load_pc = pc_offset() - kInstrSize;
}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uintptr_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList pinned,
                             uint32_t* protected_store_pc, bool is_store_mem,
                             bool i64_offset) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  MemOperand dst_op =
      liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm, i64_offset);

  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      St_b(src.gp(), dst_op);
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      MacroAssembler::St_h(src.gp(), dst_op);
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      MacroAssembler::St_w(src.gp(), dst_op);
      break;
    case StoreType::kI64Store:
      MacroAssembler::St_d(src.gp(), dst_op);
      break;
    case StoreType::kF32Store:
      MacroAssembler::Fst_s(src.fp(), dst_op);
      break;
    case StoreType::kF32StoreF16:
      UNIMPLEMENTED();
      break;
    case StoreType::kF64Store:
      MacroAssembler::Fst_d(src.fp(), dst_op);
      break;
    case StoreType::kS128Store:
      UNREACHABLE();
      break;
    default:
      UNREACHABLE();
  }
  // protected_store_pc should be the address of the load/store instruction.
  // The MacroAssembler load/store may contain some instructions for adjusting
  // MemOperand, so use pc_offset - kInstrSize to locate.
  if (protected_store_pc) *protected_store_pc = pc_offset() - kInstrSize;
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
      Ld_bu(dst.gp(), src_op);
      dbar(0);
      return;
    }
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U: {
      Ld_hu(dst.gp(), src_op);
      dbar(0);
      return;
    }
    case LoadType::kI32Load: {
      Ld_w(dst.gp(), src_op);
      dbar(0);
      return;
    }
    case LoadType::kI64Load32U: {
      Ld_wu(dst.gp(), src_op);
      dbar(0);
      return;
    }
    case LoadType::kI64Load: {
      Ld_d(dst.gp(), src_op);
      dbar(0);
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
      dbar(0);
      St_b(src.gp(), dst_op);
      return;
    }
    case StoreType::kI64Store16:
    case StoreType::kI32Store16: {
      dbar(0);
      St_h(src.gp(), dst_op);
      return;
    }
    case StoreType::kI64Store32:
    case StoreType::kI32Store: {
      dbar(0);
      St_w(src.gp(), dst_op);
      return;
    }
    case StoreType::kI64Store: {
      dbar(0);
      St_d(src.gp(), dst_op);
      return;
    }
    default:
      UNREACHABLE();
  }
}

#define ASSEMBLE_ATOMIC_BINOP_EXT(load_linked, store_conditional, size, \
                                  bin_instr, aligned)                   \
  do {                                                                  \
    Label binop;                                                        \
    andi(temp3, temp0, aligned);                                        \
    Sub_d(temp0, temp0, Operand(temp3));                                \
    slli_w(temp3, temp3, 3);                                            \
    dbar(0);                                                            \
    bind(&binop);                                                       \
    load_linked(temp1, MemOperand(temp0, 0));                           \
    ExtractBits(result.gp(), temp1, temp3, size, false);                \
    bin_instr(temp2, result.gp(), Operand(value.gp()));                 \
    InsertBits(temp1, temp2, temp3, size);                              \
    store_conditional(temp1, MemOperand(temp0, 0));                     \
    BranchShort(&binop, eq, temp1, Operand(zero_reg));                  \
    dbar(0);                                                            \
  } while (0)

#define ATOMIC_BINOP_CASE(name, inst32, inst64, opcode)                        \
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
    Add_d(temp0, dst_op.base(), dst_op.offset());                              \
    switch (type.value()) {                                                    \
      case StoreType::kI64Store8:                                              \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, 8, inst64, 7);                   \
        break;                                                                 \
      case StoreType::kI32Store8:                                              \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, 8, inst32, 3);                   \
        break;                                                                 \
      case StoreType::kI64Store16:                                             \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, 16, inst64, 7);                  \
        break;                                                                 \
      case StoreType::kI32Store16:                                             \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, 16, inst32, 3);                  \
        break;                                                                 \
      case StoreType::kI64Store32:                                             \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, 32, inst64, 7);                  \
        break;                                                                 \
      case StoreType::kI32Store:                                               \
        am##opcode##_db_w(result.gp(), value.gp(), temp0);                     \
        break;                                                                 \
      case StoreType::kI64Store:                                               \
        am##opcode##_db_d(result.gp(), value.gp(), temp0);                     \
        break;                                                                 \
      default:                                                                 \
        UNREACHABLE();                                                         \
    }                                                                          \
  }

ATOMIC_BINOP_CASE(Add, Add_w, Add_d, add)
ATOMIC_BINOP_CASE(And, And, And, and)
ATOMIC_BINOP_CASE(Or, Or, Or, or)
ATOMIC_BINOP_CASE(Xor, Xor, Xor, xor)

#define ASSEMBLE_ATOMIC_BINOP(load_linked, store_conditional, bin_instr) \
  do {                                                                   \
    Label binop;                                                         \
    dbar(0);                                                             \
    bind(&binop);                                                        \
    load_linked(result.gp(), MemOperand(temp0, 0));                      \
    bin_instr(temp1, result.gp(), Operand(value.gp()));                  \
    store_conditional(temp1, MemOperand(temp0, 0));                      \
    BranchShort(&binop, eq, temp1, Operand(zero_reg));                   \
    dbar(0);                                                             \
  } while (0)

void LiftoffAssembler::AtomicSub(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  LiftoffRegList pinned{dst_addr, value, result};
  if (offset_reg != no_reg) pinned.set(offset_reg);
  Register temp0 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp1 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp2 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  Register temp3 = pinned.set(GetUnusedRegister(kGpReg, pinned)).gp();
  MemOperand dst_op =
      liftoff::GetMemOp(this, dst_addr, offset_reg, offset_imm, i64_offset);
  Add_d(temp0, dst_op.base(), dst_op.offset());
  switch (type.value()) {
    case StoreType::kI64Store8:
      ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, 8, Sub_d, 7);
      break;
    case StoreType::kI32Store8:
      ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, 8, Sub_w, 3);
      break;
    case StoreType::kI64Store16:
      ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, 16, Sub_d, 7);
      break;
    case StoreType::kI32Store16:
      ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, 16, Sub_w, 3);
      break;
    case StoreType::kI64Store32:
      ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, 32, Sub_d, 7);
      break;
    case StoreType::kI32Store:
      ASSEMBLE_ATOMIC_BINOP(Ll_w, Sc_w, Sub_w);
      break;
    case StoreType::kI64Store:
      ASSEMBLE_ATOMIC_BINOP(Ll_d, Sc_d, Sub_d);
      break;
    default:
      UNREACHABLE();
  }
}
#undef ASSEMBLE_ATOMIC_BINOP
#undef ASSEMBLE_ATOMIC_BINOP_EXT
#undef ATOMIC_BINOP_CASE

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(load_linked, store_conditional, \
                                             size, aligned)                  \
  do {                                                                       \
    Label exchange;                                                          \
    andi(temp1, temp0, aligned);                                             \
    Sub_d(temp0, temp0, Operand(temp1));                                     \
    slli_w(temp1, temp1, 3);                                                 \
    dbar(0);                                                                 \
    bind(&exchange);                                                         \
    load_linked(temp2, MemOperand(temp0, 0));                                \
    ExtractBits(result.gp(), temp2, temp1, size, false);                     \
    InsertBits(temp2, value.gp(), temp1, size);                              \
    store_conditional(temp2, MemOperand(temp0, 0));                          \
    BranchShort(&exchange, eq, temp2, Operand(zero_reg));                    \
    dbar(0);                                                                 \
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
  Add_d(temp0, dst_op.base(), dst_op.offset());
  switch (type.value()) {
    case StoreType::kI64Store8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, 8, 7);
      break;
    case StoreType::kI32Store8:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, 8, 3);
      break;
    case StoreType::kI64Store16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, 16, 7);
      break;
    case StoreType::kI32Store16:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, 16, 3);
      break;
    case StoreType::kI64Store32:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, 32, 7);
      break;
    case StoreType::kI32Store:
      amswap_db_w(result.gp(), value.gp(), temp0);
      break;
    case StoreType::kI64Store:
      amswap_db_d(result.gp(), value.gp(), temp0);
      break;
    default:
      UNREACHABLE();
  }
}
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(load_linked,       \
                                                 store_conditional) \
  do {                                                              \
    Label compareExchange;                                          \
    Label exit;                                                     \
    dbar(0);                                                        \
    bind(&compareExchange);                                         \
    load_linked(result.gp(), MemOperand(temp0, 0));                 \
    BranchShort(&exit, ne, expected.gp(), Operand(result.gp()));    \
    mov(temp2, new_value.gp());                                     \
    store_conditional(temp2, MemOperand(temp0, 0));                 \
    BranchShort(&compareExchange, eq, temp2, Operand(zero_reg));    \
    bind(&exit);                                                    \
    dbar(0);                                                        \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(            \
    load_linked, store_conditional, size, aligned)               \
  do {                                                           \
    Label compareExchange;                                       \
    Label exit;                                                  \
    andi(temp1, temp0, aligned);                                 \
    Sub_d(temp0, temp0, Operand(temp1));                         \
    slli_w(temp1, temp1, 3);                                     \
    dbar(0);                                                     \
    bind(&compareExchange);                                      \
    load_linked(temp2, MemOperand(temp0, 0));                    \
    ExtractBits(result.gp(), temp2, temp1, size, false);         \
    ExtractBits(temp2, expected.gp(), zero_reg, size, false);    \
    BranchShort(&exit, ne, temp2, Operand(result.gp()));         \
    InsertBits(temp2, new_value.gp(), temp1, size);              \
    store_conditional(temp2, MemOperand(temp0, 0));              \
    BranchShort(&compareExchange, eq, temp2, Operand(zero_reg)); \
    bind(&exit);                                                 \
    dbar(0);                                                     \
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
  Add_d(temp0, dst_op.base(), dst_op.offset());
  switch (type.value()) {
    case StoreType::kI64Store8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, 8, 7);
      break;
    case StoreType::kI32Store8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, 8, 3);
      break;
    case StoreType::kI64Store16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, 16, 7);
      break;
    case StoreType::kI32Store16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, 16, 3);
      break;
    case StoreType::kI64Store32:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, 32, 7);
      break;
    case StoreType::kI32Store:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll_w, Sc_w);
      break;
    case StoreType::kI64Store:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll_d, Sc_d);
      break;
    default:
      UNREACHABLE();
  }
}
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT

void LiftoffAssembler::AtomicFence() { dbar(0); }

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
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  switch (kind) {
    case kI32:
    case kF32:
      Ld_w(scratch, liftoff::GetStackSlot(src_offset));
      St_w(scratch, liftoff::GetStackSlot(dst_offset));
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
    case kF64:
      Ld_d(scratch, liftoff::GetStackSlot(src_offset));
      St_d(scratch, liftoff::GetStackSlot(dst_offset));
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
    UNREACHABLE();
  }
}

void LiftoffAssembler::Spill(int offset, LiftoffRegister reg, ValueKind kind) {
  RecordUsedSpillOffset(offset);
  MemOperand dst = liftoff::GetStackSlot(offset);
  switch (kind) {
    case kI32:
      St_w(reg.gp(), dst);
      break;
    case kI64:
    case kRef:
    case kRefNull:
    case kRtt:
      St_d(reg.gp(), dst);
      break;
    case kF32:
      Fst_s(reg.fp(), dst);
      break;
    case kF64:
      MacroAssembler::Fst_d(reg.fp(), dst);
      break;
    case kS128:
      UNREACHABLE();
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
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      MacroAssembler::li(scratch, Operand(value.to_i32()));
      St_w(scratch, dst);
      break;
    }
    case kI64:
    case kRef:
    case kRefNull: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      MacroAssembler::li(scratch, value.to_i64());
      St_d(scratch, dst);
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
      Ld_w(reg.gp(), src);
      break;
    case kI64:
    case kRef:
    case kRefNull:
    // TODO(LOONG_dev): LOONG64 Check, MIPS64 dosn't need, ARM64/LOONG64 need?
    case kRtt:
      Ld_d(reg.gp(), src);
      break;
    case kF32:
      Fld_s(reg.fp(), src);
      break;
    case kF64:
      MacroAssembler::Fld_d(reg.fp(), src);
      break;
    case kS128:
      UNREACHABLE();
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
      St_d(zero_reg, liftoff::GetStackSlot(start + remainder));
    }
    DCHECK(remainder == 4 || remainder == 0);
    if (remainder) {
      St_w(zero_reg, liftoff::GetStackSlot(start + remainder));
    }
  } else {
    // General case for bigger counts (12 instructions).
    // Use a0 for start address (inclusive), a1 for end address (exclusive).
    Push(a1, a0);
    Add_d(a0, fp, Operand(-start - size));
    Add_d(a1, fp, Operand(-start));

    Label loop;
    bind(&loop);
    St_d(zero_reg, MemOperand(a0, 0));
    addi_d(a0, a0, kSystemPointerSize);
    BranchShort(&loop, ne, a0, Operand(a1));

    Pop(a1, a0);
  }
}

void LiftoffAssembler::LoadSpillAddress(Register dst, int offset,
                                        ValueKind /* kind */) {
  Sub_d(dst, fp, Operand(offset));
}

void LiftoffAssembler::emit_i64_clz(LiftoffRegister dst, LiftoffRegister src) {
  MacroAssembler::Clz_d(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_ctz(LiftoffRegister dst, LiftoffRegister src) {
  MacroAssembler::Ctz_d(dst.gp(), src.gp());
}

bool LiftoffAssembler::emit_i64_popcnt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  MacroAssembler::Popcnt_d(dst.gp(), src.gp());
  return true;
}

void LiftoffAssembler::IncrementSmi(LiftoffRegister dst, int offset) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  if (COMPRESS_POINTERS_BOOL) {
    DCHECK(SmiValuesAre31Bits());
    Ld_w(scratch, MemOperand(dst.gp(), offset));
    Add_w(scratch, scratch, Operand(Smi::FromInt(1)));
    St_w(scratch, MemOperand(dst.gp(), offset));
  } else {
    SmiUntag(scratch, MemOperand(dst.gp(), offset));
    Add_d(scratch, scratch, Operand(1));
    SmiTag(scratch);
    St_d(scratch, MemOperand(dst.gp(), offset));
  }
}

void LiftoffAssembler::emit_i32_mul(Register dst, Register lhs, Register rhs) {
  MacroAssembler::Mul_w(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_divs(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));

  // Check if lhs == kMinInt and rhs == -1, since this case is unrepresentable.
  rotri_w(kScratchReg, lhs, 31);
  xori(kScratchReg, kScratchReg, 1);
  // If lhs == kMinInt, move rhs to kScratchReg.
  masknez(kScratchReg, rhs, kScratchReg);
  addi_w(kScratchReg, kScratchReg, 1);
  MacroAssembler::Branch(trap_div_unrepresentable, eq, kScratchReg,
                         Operand(zero_reg));

  MacroAssembler::Div_w(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_divu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Div_wu(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_rems(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Mod_w(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_remu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs, Operand(zero_reg));
  MacroAssembler::Mod_wu(dst, lhs, rhs);
}

#define I32_BINOP(name, instruction)                                 \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register lhs, \
                                         Register rhs) {             \
    instruction(dst, lhs, rhs);                                      \
  }

// clang-format off
I32_BINOP(add, add_w)
I32_BINOP(sub, sub_w)
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
I32_BINOP_I(add, Add_w)
I32_BINOP_I(sub, Sub_w)
I32_BINOP_I(and, And)
I32_BINOP_I(or, Or)
I32_BINOP_I(xor, Xor)
// clang-format on

#undef I32_BINOP_I

void LiftoffAssembler::emit_i32_clz(Register dst, Register src) {
  MacroAssembler::Clz_w(dst, src);
}

void LiftoffAssembler::emit_i32_ctz(Register dst, Register src) {
  MacroAssembler::Ctz_w(dst, src);
}

bool LiftoffAssembler::emit_i32_popcnt(Register dst, Register src) {
  MacroAssembler::Popcnt_w(dst, src);
  return true;
}

#define I32_SHIFTOP(name, instruction)                               \
  void LiftoffAssembler::emit_i32_##name(Register dst, Register src, \
                                         Register amount) {          \
    instruction(dst, src, amount);                                   \
  }
#define I32_SHIFTOP_I(name, instruction, instruction1)                  \
  I32_SHIFTOP(name, instruction)                                        \
  void LiftoffAssembler::emit_i32_##name##i(Register dst, Register src, \
                                            int amount) {               \
    instruction1(dst, src, amount & 0x1f);                              \
  }

I32_SHIFTOP_I(shl, sll_w, slli_w)
I32_SHIFTOP_I(sar, sra_w, srai_w)
I32_SHIFTOP_I(shr, srl_w, srli_w)

#undef I32_SHIFTOP
#undef I32_SHIFTOP_I

void LiftoffAssembler::emit_i64_addi(LiftoffRegister dst, LiftoffRegister lhs,
                                     int64_t imm) {
  MacroAssembler::Add_d(dst.gp(), lhs.gp(), Operand(imm));
}

void LiftoffAssembler::emit_i64_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  MacroAssembler::Mul_d(dst.gp(), lhs.gp(), rhs.gp());
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
  MacroAssembler::Mul_d(dst.gp(), lhs.gp(), scratch);
}

bool LiftoffAssembler::emit_i64_divs(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));

  // Check if lhs == MinInt64 and rhs == -1, since this case is unrepresentable.
  rotri_d(kScratchReg, lhs.gp(), 63);
  xori(kScratchReg, kScratchReg, 1);
  // If lhs == MinInt64, move rhs to kScratchReg.
  masknez(kScratchReg, rhs.gp(), kScratchReg);
  addi_d(kScratchReg, kScratchReg, 1);
  MacroAssembler::Branch(trap_div_unrepresentable, eq, kScratchReg,
                         Operand(zero_reg));

  MacroAssembler::Div_d(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_divu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Div_du(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_rems(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Mod_d(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_remu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  MacroAssembler::Branch(trap_div_by_zero, eq, rhs.gp(), Operand(zero_reg));
  MacroAssembler::Mod_du(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

#define I64_BINOP(name, instruction)                                   \
  void LiftoffAssembler::emit_i64_##name(                              \
      LiftoffRegister dst, LiftoffRegister lhs, LiftoffRegister rhs) { \
    instruction(dst.gp(), lhs.gp(), rhs.gp());                         \
  }

// clang-format off
I64_BINOP(add, Add_d)
I64_BINOP(sub, Sub_d)
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
#define I64_SHIFTOP_I(name, instruction, instructioni)                         \
  I64_SHIFTOP(name, instruction)                                               \
  void LiftoffAssembler::emit_i64_##name##i(LiftoffRegister dst,               \
                                            LiftoffRegister src, int amount) { \
    instructioni(dst.gp(), src.gp(), amount & 63);                             \
  }

I64_SHIFTOP_I(shl, sll_d, slli_d)
I64_SHIFTOP_I(sar, sra_d, srai_d)
I64_SHIFTOP_I(shr, srl_d, srli_d)

#undef I64_SHIFTOP
#undef I64_SHIFTOP_I

void LiftoffAssembler::emit_u32_to_uintptr(Register dst, Register src) {
  bstrpick_d(dst, src, 31, 0);
}

void LiftoffAssembler::clear_i32_upper_half(Register dst) {
  // Don't need to clear the upper halves of i32 values for sandbox on
  // LoongArch64, because we'll explicitly zero-extend their lower halves before
  // using them for memory accesses anyway.
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
  fcopysign_s(dst, lhs, rhs);
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
  fcopysign_d(dst, lhs, rhs);
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

FP_BINOP(f32_add, fadd_s)
FP_BINOP(f32_sub, fsub_s)
FP_BINOP(f32_mul, fmul_s)
FP_BINOP(f32_div, fdiv_s)
FP_UNOP(f32_abs, fabs_s)
FP_UNOP_RETURN_TRUE(f32_ceil, Ceil_s)
FP_UNOP_RETURN_TRUE(f32_floor, Floor_s)
FP_UNOP_RETURN_TRUE(f32_trunc, Trunc_s)
FP_UNOP_RETURN_TRUE(f32_nearest_int, Round_s)
FP_UNOP(f32_sqrt, fsqrt_s)
FP_BINOP(f64_add, fadd_d)
FP_BINOP(f64_sub, fsub_d)
FP_BINOP(f64_mul, fmul_d)
FP_BINOP(f64_div, fdiv_d)
FP_UNOP(f64_abs, fabs_d)
FP_UNOP_RETURN_TRUE(f64_ceil, Ceil_d)
FP_UNOP_RETURN_TRUE(f64_floor, Floor_d)
FP_UNOP_RETURN_TRUE(f64_trunc, Trunc_d)
FP_UNOP_RETURN_TRUE(f64_nearest_int, Round_d)
FP_UNOP(f64_sqrt, fsqrt_d)

#undef FP_BINOP
#undef FP_UNOP
#undef FP_UNOP_RETURN_TRUE

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  switch (opcode) {
    case kExprI32ConvertI64:
      MacroAssembler::bstrpick_w(dst.gp(), src.gp(), 31, 0);
      return true;
    case kExprI32SConvertF32: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_s(rounded.fp(), src.fp());
      ftintrz_w_s(kScratchDoubleReg, rounded.fp());
      movfr2gr_s(dst.gp(), kScratchDoubleReg);
      // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
      // because INT32_MIN allows easier out-of-bounds detection.
      MacroAssembler::Add_w(kScratchReg, dst.gp(), 1);
      MacroAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      MacroAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      movgr2fr_w(kScratchDoubleReg, dst.gp());
      ffint_s_w(converted_back.fp(), kScratchDoubleReg);
      MacroAssembler::CompareF32(rounded.fp(), converted_back.fp(), CEQ);
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32UConvertF32: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_s(rounded.fp(), src.fp());
      MacroAssembler::Ftintrz_uw_s(dst.gp(), rounded.fp(), kScratchDoubleReg);
      // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
      // because 0 allows easier out-of-bounds detection.
      MacroAssembler::Add_w(kScratchReg, dst.gp(), 1);
      MacroAssembler::Movz(dst.gp(), zero_reg, kScratchReg);

      // Checking if trap.
      MacroAssembler::Ffint_d_uw(converted_back.fp(), dst.gp());
      fcvt_s_d(converted_back.fp(), converted_back.fp());
      MacroAssembler::CompareF32(rounded.fp(), converted_back.fp(), CEQ);
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32SConvertF64: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_d(rounded.fp(), src.fp());
      ftintrz_w_d(kScratchDoubleReg, rounded.fp());
      movfr2gr_s(dst.gp(), kScratchDoubleReg);

      // Checking if trap.
      ffint_d_w(converted_back.fp(), kScratchDoubleReg);
      MacroAssembler::CompareF64(rounded.fp(), converted_back.fp(), CEQ);
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32UConvertF64: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_d(rounded.fp(), src.fp());
      MacroAssembler::Ftintrz_uw_d(dst.gp(), rounded.fp(), kScratchDoubleReg);

      // Checking if trap.
      MacroAssembler::Ffint_d_uw(converted_back.fp(), dst.gp());
      MacroAssembler::CompareF64(rounded.fp(), converted_back.fp(), CEQ);
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI32ReinterpretF32:
      MacroAssembler::FmoveLow(dst.gp(), src.fp());
      return true;
    case kExprI64SConvertI32:
      slli_w(dst.gp(), src.gp(), 0);
      return true;
    case kExprI64UConvertI32:
      MacroAssembler::bstrpick_d(dst.gp(), src.gp(), 31, 0);
      return true;
    case kExprI64SConvertF32: {
      LiftoffRegister rounded = GetUnusedRegister(kFpReg, LiftoffRegList{src});
      LiftoffRegister converted_back =
          GetUnusedRegister(kFpReg, LiftoffRegList{src, rounded});

      // Real conversion.
      MacroAssembler::Trunc_s(rounded.fp(), src.fp());
      ftintrz_l_s(kScratchDoubleReg, rounded.fp());
      movfr2gr_d(dst.gp(), kScratchDoubleReg);
      // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
      // because INT64_MIN allows easier out-of-bounds detection.
      MacroAssembler::Add_d(kScratchReg, dst.gp(), 1);
      MacroAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      MacroAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      movgr2fr_d(kScratchDoubleReg, dst.gp());
      ffint_s_l(converted_back.fp(), kScratchDoubleReg);
      MacroAssembler::CompareF32(rounded.fp(), converted_back.fp(), CEQ);
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI64UConvertF32: {
      // Real conversion.
      MacroAssembler::Ftintrz_ul_s(dst.gp(), src.fp(), kScratchDoubleReg,
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
      MacroAssembler::Trunc_d(rounded.fp(), src.fp());
      ftintrz_l_d(kScratchDoubleReg, rounded.fp());
      movfr2gr_d(dst.gp(), kScratchDoubleReg);
      // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
      // because INT64_MIN allows easier out-of-bounds detection.
      MacroAssembler::Add_d(kScratchReg, dst.gp(), 1);
      MacroAssembler::Slt(kScratchReg2, kScratchReg, dst.gp());
      MacroAssembler::Movn(dst.gp(), kScratchReg, kScratchReg2);

      // Checking if trap.
      movgr2fr_d(kScratchDoubleReg, dst.gp());
      ffint_d_l(converted_back.fp(), kScratchDoubleReg);
      MacroAssembler::CompareF64(rounded.fp(), converted_back.fp(), CEQ);
      MacroAssembler::BranchFalseF(trap);
      return true;
    }
    case kExprI64UConvertF64: {
      // Real conversion.
      MacroAssembler::Ftintrz_ul_d(dst.gp(), src.fp(), kScratchDoubleReg,
                                   kScratchReg);

      // Checking if trap.
      MacroAssembler::Branch(trap, eq, kScratchReg, Operand(zero_reg));
      return true;
    }
    case kExprI64ReinterpretF64:
      movfr2gr_d(dst.gp(), src.fp());
      return true;
    case kExprF32SConvertI32: {
      LiftoffRegister scratch = GetUnusedRegister(kFpReg, LiftoffRegList{dst});
      movgr2fr_w(scratch.fp(), src.gp());
      ffint_s_w(dst.fp(), scratch.fp());
      return true;
    }
    case kExprF32UConvertI32:
      MacroAssembler::Ffint_s_uw(dst.fp(), src.gp());
      return true;
    case kExprF32ConvertF64:
      fcvt_s_d(dst.fp(), src.fp());
      return true;
    case kExprF32ReinterpretI32:
      MacroAssembler::FmoveLow(dst.fp(), src.gp());
      return true;
    case kExprF64SConvertI32: {
      LiftoffRegister scratch = GetUnusedRegister(kFpReg, LiftoffRegList{dst});
      movgr2fr_w(scratch.fp(), src.gp());
      ffint_d_w(dst.fp(), scratch.fp());
      return true;
    }
    case kExprF64UConvertI32:
      MacroAssembler::Ffint_d_uw(dst.fp(), src.gp());
      return true;
    case kExprF64ConvertF32:
      fcvt_d_s(dst.fp(), src.fp());
      return true;
    case kExprF64ReinterpretI64:
      movgr2fr_d(dst.fp(), src.gp());
      return true;
    case kExprI32SConvertSatF32:
      ftintrz_w_s(kScratchDoubleReg, src.fp());
      movfr2gr_s(dst.gp(), kScratchDoubleReg);
      return true;
    case kExprI32UConvertSatF32: {
      Label isnan_or_lessthan_or_equal_zero;
      mov(dst.gp(), zero_reg);
      MacroAssembler::Move(kScratchDoubleReg, static_cast<float>(0.0));
      CompareF32(src.fp(), kScratchDoubleReg, CULE);
      BranchTrueShortF(&isnan_or_lessthan_or_equal_zero);
      Ftintrz_uw_s(dst.gp(), src.fp(), kScratchDoubleReg);
      bind(&isnan_or_lessthan_or_equal_zero);
      return true;
    }
    case kExprI32SConvertSatF64:
      ftintrz_w_d(kScratchDoubleReg, src.fp());
      movfr2gr_s(dst.gp(), kScratchDoubleReg);
      return true;
    case kExprI32UConvertSatF64: {
      Label isnan_or_lessthan_or_equal_zero;
      mov(dst.gp(), zero_reg);
      MacroAssembler::Move(kScratchDoubleReg, static_cast<double>(0.0));
      CompareF64(src.fp(), kScratchDoubleReg, CULE);
      BranchTrueShortF(&isnan_or_lessthan_or_equal_zero);
      Ftintrz_uw_d(dst.gp(), src.fp(), kScratchDoubleReg);
      bind(&isnan_or_lessthan_or_equal_zero);
      return true;
    }
    case kExprI64SConvertSatF32:
      ftintrz_l_s(kScratchDoubleReg, src.fp());
      movfr2gr_d(dst.gp(), kScratchDoubleReg);
      return true;
    case kExprI64UConvertSatF32: {
      Label isnan_or_lessthan_or_equal_zero;
      mov(dst.gp(), zero_reg);
      MacroAssembler::Move(kScratchDoubleReg, static_cast<float>(0.0));
      CompareF32(src.fp(), kScratchDoubleReg, CULE);
      BranchTrueShortF(&isnan_or_lessthan_or_equal_zero);
      Ftintrz_ul_s(dst.gp(), src.fp(), kScratchDoubleReg);
      bind(&isnan_or_lessthan_or_equal_zero);
      return true;
    }
    case kExprI64SConvertSatF64:
      ftintrz_l_d(kScratchDoubleReg, src.fp());
      movfr2gr_d(dst.gp(), kScratchDoubleReg);
      return true;
    case kExprI64UConvertSatF64: {
      Label isnan_or_lessthan_or_equal_zero;
      mov(dst.gp(), zero_reg);
      MacroAssembler::Move(kScratchDoubleReg, static_cast<double>(0.0));
      CompareF64(src.fp(), kScratchDoubleReg, CULE);
      BranchTrueShortF(&isnan_or_lessthan_or_equal_zero);
      Ftintrz_ul_d(dst.gp(), src.fp(), kScratchDoubleReg);
      bind(&isnan_or_lessthan_or_equal_zero);
      return true;
    }
    default:
      return false;
  }
}

void LiftoffAssembler::emit_i32_signextend_i8(Register dst, Register src) {
  ext_w_b(dst, src);
}

void LiftoffAssembler::emit_i32_signextend_i16(Register dst, Register src) {
  ext_w_h(dst, src);
}

void LiftoffAssembler::emit_i64_signextend_i8(LiftoffRegister dst,
                                              LiftoffRegister src) {
  ext_w_b(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_signextend_i16(LiftoffRegister dst,
                                               LiftoffRegister src) {
  ext_w_h(dst.gp(), src.gp());
}

void LiftoffAssembler::emit_i64_signextend_i32(LiftoffRegister dst,
                                               LiftoffRegister src) {
  slli_w(dst.gp(), src.gp(), 0);
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
      slli_w(scratch0, lhs, 0);
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
  slli_w(dst, src, 0);
  sltui(dst, dst, 1);
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  UseScratchRegisterScope temps(this);
  Register scratch0 = temps.Acquire();
  Register scratch1 = kScratchReg;

  slli_w(scratch0, lhs, 0);
  slli_w(scratch1, rhs, 0);

  CompareWord(cond, dst, scratch0, Operand(scratch1));
}

void LiftoffAssembler::emit_i64_eqz(Register dst, LiftoffRegister src) {
  sltui(dst, src.gp(), 1);
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
      return CEQ;
    case kNotEqual:
      *predicate = false;
      return CEQ;
    case kUnsignedLessThan:
      *predicate = true;
      return CLT;
    case kUnsignedGreaterThanEqual:
      *predicate = false;
      return CLT;
    case kUnsignedLessThanEqual:
      *predicate = true;
      return CLE;
    case kUnsignedGreaterThan:
      *predicate = false;
      return CLE;
    default:
      *predicate = true;
      break;
  }
  UNREACHABLE();
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
  MacroAssembler::CompareF32(lhs, rhs, fcond);
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
  MacroAssembler::CompareF64(lhs, rhs, fcond);
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
  bailout(kSimd, "load extend and load splat unimplemented");
}

void LiftoffAssembler::LoadLane(LiftoffRegister dst, LiftoffRegister src,
                                Register addr, Register offset_reg,
                                uintptr_t offset_imm, LoadType type,
                                uint8_t laneidx, uint32_t* protected_load_pc,
                                bool i64_offset) {
  bailout(kSimd, "loadlane");
}

void LiftoffAssembler::StoreLane(Register dst, Register offset,
                                 uintptr_t offset_imm, LiftoffRegister src,
                                 StoreType type, uint8_t lane,
                                 uint32_t* protected_store_pc,
                                 bool i64_offset) {
  bailout(kSimd, "storelane");
}

void LiftoffAssembler::emit_i8x16_shuffle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs,
                                          const uint8_t shuffle[16],
                                          bool is_swizzle) {
  bailout(kSimd, "emit_i8x16_shuffle");
}

void LiftoffAssembler::emit_i8x16_swizzle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_swizzle");
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
  bailout(kSimd, "emit_i8x16_splat");
}

void LiftoffAssembler::emit_i16x8_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_splat");
}

void LiftoffAssembler::emit_i32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_splat");
}

void LiftoffAssembler::emit_i64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_splat");
}

void LiftoffAssembler::emit_f32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_splat");
}

void LiftoffAssembler::emit_f64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_splat");
}

#define SIMD_BINOP(name1, name2)                                         \
  void LiftoffAssembler::emit_##name1##_extmul_low_##name2(              \
      LiftoffRegister dst, LiftoffRegister src1, LiftoffRegister src2) { \
    bailout(kSimd, "emit_" #name1 "_extmul_low_" #name2);                \
  }                                                                      \
  void LiftoffAssembler::emit_##name1##_extmul_high_##name2(             \
      LiftoffRegister dst, LiftoffRegister src1, LiftoffRegister src2) { \
    bailout(kSimd, "emit_" #name1 "_extmul_high_" #name2);               \
  }

SIMD_BINOP(i16x8, i8x16_s)
SIMD_BINOP(i16x8, i8x16_u)

SIMD_BINOP(i32x4, i16x8_s)
SIMD_BINOP(i32x4, i16x8_u)

SIMD_BINOP(i64x2, i32x4_s)
SIMD_BINOP(i64x2, i32x4_u)

#undef SIMD_BINOP

#define SIMD_BINOP(name1, name2)                                 \
  void LiftoffAssembler::emit_##name1##_extadd_pairwise_##name2( \
      LiftoffRegister dst, LiftoffRegister src) {                \
    bailout(kSimd, "emit_" #name1 "_extadd_pairwise_" #name2);   \
  }

SIMD_BINOP(i16x8, i8x16_s)
SIMD_BINOP(i16x8, i8x16_u)
SIMD_BINOP(i32x4, i16x8_s)
SIMD_BINOP(i32x4, i16x8_u)
#undef SIMD_BINOP

void LiftoffAssembler::emit_i16x8_q15mulr_sat_s(LiftoffRegister dst,
                                                LiftoffRegister src1,
                                                LiftoffRegister src2) {
  bailout(kSimd, "emit_i16x8_q15mulr_sat_s");
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
  bailout(kSimd, "emit_i8x16_eq");
}

void LiftoffAssembler::emit_i8x16_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_ne");
}

void LiftoffAssembler::emit_i8x16_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_gt_s");
}

void LiftoffAssembler::emit_i8x16_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_gt_u");
}

void LiftoffAssembler::emit_i8x16_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_ge_s");
}

void LiftoffAssembler::emit_i8x16_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_ge_u");
}

void LiftoffAssembler::emit_i16x8_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_eq");
}

void LiftoffAssembler::emit_i16x8_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_ne");
}

void LiftoffAssembler::emit_i16x8_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_gt_s");
}

void LiftoffAssembler::emit_i16x8_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_gt_u");
}

void LiftoffAssembler::emit_i16x8_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_ge_s");
}

void LiftoffAssembler::emit_i16x8_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_ge_u");
}

void LiftoffAssembler::emit_i32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_eq");
}

void LiftoffAssembler::emit_i32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_ne");
}

void LiftoffAssembler::emit_i32x4_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_gt_s");
}

void LiftoffAssembler::emit_i32x4_gt_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_gt_u");
}

void LiftoffAssembler::emit_i32x4_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_ge_s");
}

void LiftoffAssembler::emit_i32x4_ge_u(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_ge_u");
}

void LiftoffAssembler::emit_f32x4_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_eq");
}

void LiftoffAssembler::emit_f32x4_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_ne");
}

void LiftoffAssembler::emit_f32x4_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_lt");
}

void LiftoffAssembler::emit_f32x4_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_le");
}

void LiftoffAssembler::emit_i64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_eq");
}

void LiftoffAssembler::emit_i64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_ne");
}

void LiftoffAssembler::emit_i64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_abs");
}

void LiftoffAssembler::emit_f64x2_eq(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_eq");
}

void LiftoffAssembler::emit_f64x2_ne(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_ne");
}

void LiftoffAssembler::emit_f64x2_lt(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_lt");
}

void LiftoffAssembler::emit_f64x2_le(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_le");
}

void LiftoffAssembler::emit_s128_const(LiftoffRegister dst,
                                       const uint8_t imms[16]) {
  bailout(kSimd, "emit_s128_const");
}

void LiftoffAssembler::emit_s128_not(LiftoffRegister dst, LiftoffRegister src) {
  bailout(kSimd, "emit_s128_not");
}

void LiftoffAssembler::emit_s128_and(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_s128_and");
}

void LiftoffAssembler::emit_s128_or(LiftoffRegister dst, LiftoffRegister lhs,
                                    LiftoffRegister rhs) {
  bailout(kSimd, "emit_s128_or");
}

void LiftoffAssembler::emit_s128_xor(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_s128_xor");
}

void LiftoffAssembler::emit_s128_and_not(LiftoffRegister dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  bailout(kSimd, "emit_s128_and_not");
}

void LiftoffAssembler::emit_s128_select(LiftoffRegister dst,
                                        LiftoffRegister src1,
                                        LiftoffRegister src2,
                                        LiftoffRegister mask) {
  bailout(kSimd, "emit_s128_select");
}

void LiftoffAssembler::emit_i8x16_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i8x16_neg");
}

void LiftoffAssembler::emit_v128_anytrue(LiftoffRegister dst,
                                         LiftoffRegister src) {
  bailout(kSimd, "emit_v128_anytrue");
}

void LiftoffAssembler::emit_i8x16_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  bailout(kSimd, "emit_i8x16_alltrue");
}

void LiftoffAssembler::emit_i8x16_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  bailout(kSimd, "emit_i8x16_bitmask");
}

void LiftoffAssembler::emit_i8x16_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_shl");
}

void LiftoffAssembler::emit_i8x16_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  bailout(kSimd, "emit_i8x16_shli");
}

void LiftoffAssembler::emit_i8x16_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_shr_s");
}

void LiftoffAssembler::emit_i8x16_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  bailout(kSimd, "emit_i8x16_shri_s");
}

void LiftoffAssembler::emit_i8x16_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_shr_u");
}

void LiftoffAssembler::emit_i8x16_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  bailout(kSimd, "emit_i8x16_shri_u");
}

void LiftoffAssembler::emit_i8x16_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_add");
}

void LiftoffAssembler::emit_i8x16_add_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_add_sat_s");
}

void LiftoffAssembler::emit_i8x16_add_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_add_sat_u");
}

void LiftoffAssembler::emit_i8x16_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_sub");
}

void LiftoffAssembler::emit_i8x16_sub_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_sub_sat_s");
}

void LiftoffAssembler::emit_i8x16_sub_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_sub_sat_u");
}

void LiftoffAssembler::emit_i8x16_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_min_s");
}

void LiftoffAssembler::emit_i8x16_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_min_u");
}

void LiftoffAssembler::emit_i8x16_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_max_s");
}

void LiftoffAssembler::emit_i8x16_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_max_u");
}

void LiftoffAssembler::emit_i8x16_popcnt(LiftoffRegister dst,
                                         LiftoffRegister src) {
  bailout(kSimd, "emit_i8x16_popcnt");
}

void LiftoffAssembler::emit_i16x8_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_neg");
}

void LiftoffAssembler::emit_i16x8_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_alltrue");
}

void LiftoffAssembler::emit_i16x8_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_bitmask");
}

void LiftoffAssembler::emit_i16x8_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_shl");
}

void LiftoffAssembler::emit_i16x8_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  bailout(kSimd, "emit_i16x8_shli");
}

void LiftoffAssembler::emit_i16x8_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_shr_s");
}

void LiftoffAssembler::emit_i16x8_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  bailout(kSimd, "emit_i16x8_shri_s");
}

void LiftoffAssembler::emit_i16x8_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_shr_u");
}

void LiftoffAssembler::emit_i16x8_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  bailout(kSimd, "emit_i16x8_shri_u");
}

void LiftoffAssembler::emit_i16x8_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_add");
}

void LiftoffAssembler::emit_i16x8_add_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_add_sat_s");
}

void LiftoffAssembler::emit_i16x8_add_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_add_sat_u");
}

void LiftoffAssembler::emit_i16x8_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_sub");
}

void LiftoffAssembler::emit_i16x8_sub_sat_s(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_sub_sat_s");
}

void LiftoffAssembler::emit_i16x8_sub_sat_u(LiftoffRegister dst,
                                            LiftoffRegister lhs,
                                            LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_sub_sat_u");
}

void LiftoffAssembler::emit_i16x8_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_mul");
}

void LiftoffAssembler::emit_i16x8_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_min_s");
}

void LiftoffAssembler::emit_i16x8_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_min_u");
}

void LiftoffAssembler::emit_i16x8_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_max_s");
}

void LiftoffAssembler::emit_i16x8_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_max_u");
}

void LiftoffAssembler::emit_i32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_neg");
}

void LiftoffAssembler::emit_i32x4_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_alltrue");
}

void LiftoffAssembler::emit_i32x4_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_bitmask");
}

void LiftoffAssembler::emit_i32x4_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_shl");
}

void LiftoffAssembler::emit_i32x4_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  bailout(kSimd, "emit_i32x4_shli");
}

void LiftoffAssembler::emit_i32x4_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_shr_s");
}

void LiftoffAssembler::emit_i32x4_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  bailout(kSimd, "emit_i32x4_shri_s");
}

void LiftoffAssembler::emit_i32x4_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_shr_u");
}

void LiftoffAssembler::emit_i32x4_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  bailout(kSimd, "emit_i32x4_shri_u");
}

void LiftoffAssembler::emit_i32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_add");
}

void LiftoffAssembler::emit_i32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_sub");
}

void LiftoffAssembler::emit_i32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_mul");
}

void LiftoffAssembler::emit_i32x4_min_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_min_s");
}

void LiftoffAssembler::emit_i32x4_min_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_min_u");
}

void LiftoffAssembler::emit_i32x4_max_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_max_s");
}

void LiftoffAssembler::emit_i32x4_max_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_max_u");
}

void LiftoffAssembler::emit_i32x4_dot_i16x8_s(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kSimd, "emit_i32x4_dot_i16x8_s");
}

void LiftoffAssembler::emit_i64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_neg");
}

void LiftoffAssembler::emit_i64x2_alltrue(LiftoffRegister dst,
                                          LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_alltrue");
}

void LiftoffAssembler::emit_i64x2_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_bitmask");
}

void LiftoffAssembler::emit_i64x2_shl(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_shl");
}

void LiftoffAssembler::emit_i64x2_shli(LiftoffRegister dst, LiftoffRegister lhs,
                                       int32_t rhs) {
  bailout(kSimd, "emit_i64x2_shli");
}

void LiftoffAssembler::emit_i64x2_shr_s(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_shr_s");
}

void LiftoffAssembler::emit_i64x2_shri_s(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  bailout(kSimd, "emit_i64x2_shri_s");
}

void LiftoffAssembler::emit_i64x2_shr_u(LiftoffRegister dst,
                                        LiftoffRegister lhs,
                                        LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_shr_u");
}

void LiftoffAssembler::emit_i64x2_shri_u(LiftoffRegister dst,
                                         LiftoffRegister lhs, int32_t rhs) {
  bailout(kSimd, "emit_i64x2_shri_u");
}

void LiftoffAssembler::emit_i64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_add");
}

void LiftoffAssembler::emit_i64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_sub");
}

void LiftoffAssembler::emit_i64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_mul");
}

void LiftoffAssembler::emit_i64x2_gt_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_gt_s");
}

void LiftoffAssembler::emit_i64x2_ge_s(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_i64x2_ge_s");
}

void LiftoffAssembler::emit_f32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_abs");
}

void LiftoffAssembler::emit_f32x4_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_neg");
}

void LiftoffAssembler::emit_f32x4_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_sqrt");
}

bool LiftoffAssembler::emit_f32x4_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_ceil");
  return true;
}

bool LiftoffAssembler::emit_f32x4_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_floor");
  return true;
}

bool LiftoffAssembler::emit_f32x4_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_trunc");
  return true;
}

bool LiftoffAssembler::emit_f32x4_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_nearest_int");
  return true;
}

void LiftoffAssembler::emit_f32x4_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_add");
}

void LiftoffAssembler::emit_f32x4_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_sub");
}

void LiftoffAssembler::emit_f32x4_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_mul");
}

void LiftoffAssembler::emit_f32x4_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_div");
}

void LiftoffAssembler::emit_f32x4_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_min");
}

void LiftoffAssembler::emit_f32x4_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_max");
}

void LiftoffAssembler::emit_f32x4_relaxed_min(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_relaxed_min");
}

void LiftoffAssembler::emit_f32x4_relaxed_max(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_relaxed_max");
}

void LiftoffAssembler::emit_f32x4_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_pmin");
}

void LiftoffAssembler::emit_f32x4_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_f32x4_pmax");
}

void LiftoffAssembler::emit_f64x2_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_abs");
}

void LiftoffAssembler::emit_f64x2_neg(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_neg");
}

void LiftoffAssembler::emit_f64x2_sqrt(LiftoffRegister dst,
                                       LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_sqrt");
}

bool LiftoffAssembler::emit_f64x2_ceil(LiftoffRegister dst,
                                       LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_ceil");
  return true;
}

bool LiftoffAssembler::emit_f64x2_floor(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_floor");
  return true;
}

bool LiftoffAssembler::emit_f64x2_trunc(LiftoffRegister dst,
                                        LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_trunc");
  return true;
}

bool LiftoffAssembler::emit_f64x2_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_nearest_int");
  return true;
}

void LiftoffAssembler::emit_f64x2_add(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_add");
}

void LiftoffAssembler::emit_f64x2_sub(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_sub");
}

void LiftoffAssembler::emit_f64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_mul");
}

void LiftoffAssembler::emit_f64x2_div(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_div");
}

void LiftoffAssembler::emit_f64x2_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_min");
}

void LiftoffAssembler::emit_f64x2_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_max");
}

void LiftoffAssembler::emit_f64x2_pmin(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_pmin");
}

void LiftoffAssembler::emit_f64x2_pmax(LiftoffRegister dst, LiftoffRegister lhs,
                                       LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_pmax");
}

void LiftoffAssembler::emit_f64x2_relaxed_min(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_relaxed_min");
}

void LiftoffAssembler::emit_f64x2_relaxed_max(LiftoffRegister dst,
                                              LiftoffRegister lhs,
                                              LiftoffRegister rhs) {
  bailout(kSimd, "emit_f64x2_relaxed_max");
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_s(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_convert_low_i32x4_s");
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_u(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_convert_low_i32x4_u");
}

void LiftoffAssembler::emit_f64x2_promote_low_f32x4(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  bailout(kSimd, "emit_f64x2_promote_low_f32x4");
}

void LiftoffAssembler::emit_i32x4_sconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_sconvert_f32x4");
}

void LiftoffAssembler::emit_i32x4_uconvert_f32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_uconvert_f32x4");
}

void LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_s_zero(LiftoffRegister dst,
                                                         LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_trunc_sat_f64x2_s_zero");
}

void LiftoffAssembler::emit_i32x4_trunc_sat_f64x2_u_zero(LiftoffRegister dst,
                                                         LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_trunc_sat_f64x2_u_zero");
}

void LiftoffAssembler::emit_f32x4_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_sconvert_i32x4");
}

void LiftoffAssembler::emit_f32x4_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_uconvert_i32x4");
}

void LiftoffAssembler::emit_f32x4_demote_f64x2_zero(LiftoffRegister dst,
                                                    LiftoffRegister src) {
  bailout(kSimd, "emit_f32x4_demote_f64x2_zero");
}

void LiftoffAssembler::emit_i8x16_sconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_sconvert_i16x8");
}

void LiftoffAssembler::emit_i8x16_uconvert_i16x8(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_uconvert_i16x8");
}

void LiftoffAssembler::emit_i16x8_sconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_sconvert_i32x4");
}

void LiftoffAssembler::emit_i16x8_uconvert_i32x4(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_uconvert_i32x4");
}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_sconvert_i8x16_low");
}

void LiftoffAssembler::emit_i16x8_sconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_sconvert_i8x16_high");
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_uconvert_i8x16_low");
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_uconvert_i8x16_high");
}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_sconvert_i16x8_low");
}

void LiftoffAssembler::emit_i32x4_sconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_sconvert_i16x8_high");
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_uconvert_i16x8_low");
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_uconvert_i16x8_high");
}

void LiftoffAssembler::emit_i64x2_sconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_sconvert_i32x4_low");
}

void LiftoffAssembler::emit_i64x2_sconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_sconvert_i32x4_high");
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_uconvert_i32x4_low");
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i64x2_uconvert_i32x4_high");
}

void LiftoffAssembler::emit_i8x16_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i8x16_rounding_average_u");
}

void LiftoffAssembler::emit_i16x8_rounding_average_u(LiftoffRegister dst,
                                                     LiftoffRegister lhs,
                                                     LiftoffRegister rhs) {
  bailout(kSimd, "emit_i16x8_rounding_average_u");
}

void LiftoffAssembler::emit_i8x16_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i8x16_abs");
}

void LiftoffAssembler::emit_i16x8_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i16x8_abs");
}

void LiftoffAssembler::emit_i32x4_abs(LiftoffRegister dst,
                                      LiftoffRegister src) {
  bailout(kSimd, "emit_i32x4_abs");
}

void LiftoffAssembler::emit_i8x16_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i8x16_extract_lane_s");
}

void LiftoffAssembler::emit_i8x16_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i8x16_extract_lane_u");
}

void LiftoffAssembler::emit_i16x8_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i16x8_extract_lane_s");
}

void LiftoffAssembler::emit_i16x8_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i16x8_extract_lane_u");
}

void LiftoffAssembler::emit_i32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i32x4_extract_lane");
}

void LiftoffAssembler::emit_i64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i64x2_extract_lane");
}

void LiftoffAssembler::emit_f32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_f32x4_extract_lane");
}

void LiftoffAssembler::emit_f64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_f64x2_extract_lane");
}

void LiftoffAssembler::emit_i8x16_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i8x16_replace_lane");
}

void LiftoffAssembler::emit_i16x8_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i16x8_replace_lane");
}

void LiftoffAssembler::emit_i32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i32x4_replace_lane");
}

void LiftoffAssembler::emit_i64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_i64x2_replace_lane");
}

void LiftoffAssembler::emit_f32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_f32x4_replace_lane");
}

void LiftoffAssembler::emit_f64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  bailout(kSimd, "emit_f64x2_replace_lane");
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
  MacroAssembler::AssertUnreachable(reason);
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  unsigned num_gp_regs = gp_regs.GetNumRegsSet();
  if (num_gp_regs) {
    unsigned offset = num_gp_regs * kSystemPointerSize;
    addi_d(sp, sp, -offset);
    while (!gp_regs.is_empty()) {
      LiftoffRegister reg = gp_regs.GetFirstRegSet();
      offset -= kSystemPointerSize;
      St_d(reg.gp(), MemOperand(sp, offset));
      gp_regs.clear(reg);
    }
    DCHECK_EQ(offset, 0);
  }
  LiftoffRegList fp_regs = regs & kFpCacheRegList;
  unsigned num_fp_regs = fp_regs.GetNumRegsSet();
  if (num_fp_regs) {
    unsigned slot_size = 8;
    addi_d(sp, sp, -(num_fp_regs * slot_size));
    unsigned offset = 0;
    while (!fp_regs.is_empty()) {
      LiftoffRegister reg = fp_regs.GetFirstRegSet();
      MacroAssembler::Fst_d(reg.fp(), MemOperand(sp, offset));
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
    MacroAssembler::Fld_d(reg.fp(), MemOperand(sp, fp_offset));
    fp_regs.clear(reg);
    fp_offset += 8;
  }
  if (fp_offset) addi_d(sp, sp, fp_offset);
  LiftoffRegList gp_regs = regs & kGpCacheRegList;
  unsigned gp_offset = 0;
  while (!gp_regs.is_empty()) {
    LiftoffRegister reg = gp_regs.GetLastRegSet();
    Ld_d(reg.gp(), MemOperand(sp, gp_offset));
    gp_regs.clear(reg);
    gp_offset += kSystemPointerSize;
  }
  addi_d(sp, sp, gp_offset);
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
  Drop(static_cast<int>(num_stack_slots));
  Ret();
}

void LiftoffAssembler::CallCWithStackBuffer(
    const std::initializer_list<VarState> args, const LiftoffRegister* rets,
    ValueKind return_kind, ValueKind out_argument_kind, int stack_bytes,
    ExternalReference ext_ref) {
  addi_d(sp, sp, -stack_bytes);

  int arg_offset = 0;
  for (const VarState& arg : args) {
    liftoff::StoreToMemory(this, MemOperand{sp, arg_offset}, arg);
    arg_offset += value_kind_size(arg.kind());
  }
  DCHECK_LE(arg_offset, stack_bytes);

  // Pass a pointer to the buffer with the arguments to the C function.
  // On LoongArch, the first argument is passed in {a0}.
  constexpr Register kFirstArgReg = a0;
  mov(kFirstArgReg, sp);

  // Now call the C function.
  constexpr int kNumCCallArgs = 1;
  PrepareCallCFunction(kNumCCallArgs, kScratchReg);
  CallCFunction(ext_ref, kNumCCallArgs);

  // Move return value to the right register.
  const LiftoffRegister* next_result_reg = rets;
  if (return_kind != kVoid) {
    constexpr Register kReturnReg = a0;
#ifdef USE_SIMULATOR
    // When call to a host function in simulator, if the function return an
    // int32 value, the simulator does not sign-extend it to int64 because
    // in simulator we do not know whether the function returns an int32 or
    // int64. so we need to sign extend it here.
    if (return_kind == kI32) {
      slli_w(next_result_reg->gp(), kReturnReg, 0);
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

  addi_d(sp, sp, stack_bytes);
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
    Pop(kScratchReg);
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
  addi_d(sp, sp, -size);
  MacroAssembler::Move(addr, sp);
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) {
  addi_d(sp, sp, size);
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
  BranchFalseShortF(&not_nan);
  li(scratch, 1);
  St_w(scratch, MemOperand(dst, 0));
  bind(&not_nan);
}

void LiftoffAssembler::emit_s128_set_if_nan(Register dst, LiftoffRegister src,
                                            Register tmp_gp,
                                            LiftoffRegister tmp_s128,
                                            ValueKind lane_kind) {
  UNIMPLEMENTED();
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
          asm_->Ld_d(kScratchReg, liftoff::GetStackSlot(slot.src_offset_));
          asm_->Push(kScratchReg);
        } else {
          asm_->AllocateStackSpace(stack_decrement - kSimd128Size);
          asm_->Ld_d(kScratchReg, liftoff::GetStackSlot(slot.src_offset_ - 8));
          asm_->Push(kScratchReg);
          asm_->Ld_d(kScratchReg, liftoff::GetStackSlot(slot.src_offset_));
          asm_->Push(kScratchReg);
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
        asm_->Push(kScratchReg);
        break;
      }
    }
  }
}

}  // namespace v8::internal::wasm

#endif  // V8_WASM_BASELINE_LOONG64_LIFTOFF_ASSEMBLER_LOONG64_INL_H_
