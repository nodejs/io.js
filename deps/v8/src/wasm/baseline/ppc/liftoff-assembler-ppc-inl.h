// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_PPC_LIFTOFF_ASSEMBLER_PPC_INL_H_
#define V8_WASM_BASELINE_PPC_LIFTOFF_ASSEMBLER_PPC_INL_H_

#include "src/codegen/assembler.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/wasm/baseline/liftoff-assembler.h"
#include "src/wasm/baseline/parallel-move-inl.h"
#include "src/wasm/object-access.h"
#include "src/wasm/simd-shuffle.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"

namespace v8::internal::wasm {

namespace liftoff {

//  half
//  slot        Frame
//  -----+--------------------+---------------------------
//  n+3  |   parameter n      |
//  ...  |       ...          |
//   4   |   parameter 1      | or parameter 2
//   3   |   parameter 0      | or parameter 1
//   2   |  (result address)  | or parameter 0
//  -----+--------------------+---------------------------
//   2   | return addr (lr)   |
//   1   | previous frame (fp)|
//   0   | const pool (r28)   | if const pool is enabled
//  -----+--------------------+  <-- frame ptr (fp) or cp
//  -1   | StackFrame::WASM   |
//  -2   |    instance        |
//  -3   |    feedback vector |
//  -4   |    tiering budget  |
//  -----+--------------------+---------------------------
//  -5   |    slot 0 (high)   |   ^
//  -6   |    slot 0 (low)    |   |
//  -7   |    slot 1 (high)   | Frame slots
//  -8   |    slot 1 (low)    |   |
//       |                    |   v
//  -----+--------------------+  <-- stack ptr (sp)
//
//


// TODO(tpearson): Much of this logic is already implemented in
// the MacroAssembler GenerateMemoryOperationWithAlignPrefixed()
// macro. Deduplicate this code using that macro where possible.
inline MemOperand GetMemOp(LiftoffAssembler* assm, Register addr,
                           Register offset, uintptr_t offset_imm,
                           Register scratch, bool i64_offset = false,
                           unsigned shift_amount = 0) {
  Register kScratchReg2 = scratch;
  DCHECK_NE(addr, kScratchReg2);
  DCHECK_NE(offset, kScratchReg2);
  if (offset != no_reg) {
    if (!i64_offset) {
      // extract least significant 32 bits without sign extend
      assm->ExtractBitRange(kScratchReg2, offset, 31, 0, LeaveRC, false);
      offset = kScratchReg2;
    }
    if (shift_amount != 0) {
      assm->ShiftLeftU64(kScratchReg2, offset, Operand(shift_amount));
    }
    assm->AddS64(kScratchReg2, offset, addr);
    addr = kScratchReg2;
  }
  if (is_int31(offset_imm)) {
    int32_t offset_imm32 = static_cast<int32_t>(offset_imm);
    return MemOperand(addr, offset_imm32);
  } else {
    // Offset immediate does not fit in 31 bits.
    assm->mov(kScratchReg2, Operand(offset_imm));
    assm->AddS64(kScratchReg2, addr, kScratchReg2);
    return MemOperand(kScratchReg2, 0);
  }
}

inline MemOperand GetStackSlot(uint32_t offset) {
  return MemOperand(fp, -static_cast<int32_t>(offset));
}

inline MemOperand GetInstanceDataOperand() {
  return GetStackSlot(WasmLiftoffFrameConstants::kInstanceDataOffset);
}

inline void StoreToMemory(LiftoffAssembler* assm, MemOperand dst,
                          const LiftoffAssembler::VarState& src,
                          Register scratch1, Register scratch2) {
  if (src.is_reg()) {
    switch (src.kind()) {
      case kI16:
        assm->StoreU16(src.reg().gp(), dst, scratch1);
        break;
      case kI32:
        assm->StoreU32(src.reg().gp(), dst, scratch1);
        break;
      case kI64:
        assm->StoreU64(src.reg().gp(), dst, scratch1);
        break;
      case kF32:
        assm->StoreF32(src.reg().fp(), dst, scratch1);
        break;
      case kF64:
        assm->StoreF64(src.reg().fp(), dst, scratch1);
        break;
      case kS128:
        assm->StoreSimd128(src.reg().fp().toSimd(), dst, scratch1);
        break;
      default:
        UNREACHABLE();
    }
  } else if (src.is_const()) {
    if (src.kind() == kI32) {
      assm->mov(scratch2, Operand(src.i32_const()));
      assm->StoreU32(scratch2, dst, scratch1);
    } else {
      assm->mov(scratch2, Operand(static_cast<int64_t>(src.i32_const())));
      assm->StoreU64(scratch2, dst, scratch1);
    }
  } else if (value_kind_size(src.kind()) == 4) {
    assm->LoadU32(scratch2, liftoff::GetStackSlot(src.offset()), scratch1);
    assm->StoreU32(scratch2, dst, scratch1);
  } else {
    DCHECK_EQ(8, value_kind_size(src.kind()));
    assm->LoadU64(scratch2, liftoff::GetStackSlot(src.offset()), scratch1);
    assm->StoreU64(scratch2, dst, scratch1);
  }
}

}  // namespace liftoff

int LiftoffAssembler::PrepareStackFrame() {
  int offset = pc_offset();
  addi(sp, sp, Operand::Zero());
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

  Register scratch = ip;
  mov(scratch, Operand(StackFrame::TypeToMarker(StackFrame::WASM)));
  PushCommonFrame(scratch);
  LoadConstant(LiftoffRegister(kLiftoffFrameSetupFunctionReg),
               WasmValue(declared_function_index));
  CallBuiltin(Builtin::kWasmLiftoffFrameSetup);
}

void LiftoffAssembler::PrepareTailCall(int num_callee_stack_params,
                                       int stack_param_delta) {
  Register scratch = ip;
  // Push the return address and frame pointer to complete the stack frame.
  AddS64(sp, sp, Operand(-2 * kSystemPointerSize), r0);
  LoadU64(scratch, MemOperand(fp, kSystemPointerSize), r0);
  StoreU64(scratch, MemOperand(sp, kSystemPointerSize), r0);
  LoadU64(scratch, MemOperand(fp), r0);
  StoreU64(scratch, MemOperand(sp), r0);

  // Shift the whole frame upwards.
  int slot_count = num_callee_stack_params + 2;
  for (int i = slot_count - 1; i >= 0; --i) {
    LoadU64(scratch, MemOperand(sp, i * kSystemPointerSize), r0);
    StoreU64(scratch,
             MemOperand(fp, (i - stack_param_delta) * kSystemPointerSize), r0);
  }

  // Set the new stack and frame pointer.
  AddS64(sp, fp, Operand(-stack_param_delta * kSystemPointerSize), r0);
  Pop(r0, fp);
  mtlr(r0);
}

void LiftoffAssembler::AlignFrameSize() {}

void LiftoffAssembler::PatchPrepareStackFrame(
    int offset, SafepointTableBuilder* safepoint_table_builder,
    bool feedback_vector_slot) {
  int frame_size =
      GetTotalFrameSize() -
      (V8_EMBEDDED_CONSTANT_POOL_BOOL ? 3 : 2) * kSystemPointerSize;
  // The frame setup builtin also pushes the feedback vector.
  if (feedback_vector_slot) {
    frame_size -= kSystemPointerSize;
  }

  Assembler patching_assembler(
      AssemblerOptions{},
      ExternalAssemblerBuffer(buffer_start_ + offset, kInstrSize + kGap));

  if (V8_LIKELY(frame_size < 4 * KB)) {
    patching_assembler.addi(sp, sp, Operand(-frame_size));
    return;
  }

  // The frame size is bigger than 4KB, so we might overflow the available stack
  // space if we first allocate the frame and then do the stack check (we will
  // need some remaining stack space for throwing the exception). That's why we
  // check the available stack space before we allocate the frame. To do this we
  // replace the {__ sub(sp, sp, framesize)} with a jump to OOL code that does
  // this "extended stack check".
  //
  // The OOL code can simply be generated here with the normal assembler,
  // because all other code generation, including OOL code, has already finished
  // when {PatchPrepareStackFrame} is called. The function prologue then jumps
  // to the current {pc_offset()} to execute the OOL code for allocating the
  // large frame.

  // Emit the unconditional branch in the function prologue (from {offset} to
  // {pc_offset()}).

  int jump_offset = pc_offset() - offset;
  if (!is_int26(jump_offset)) {
    bailout(kUnsupportedArchitecture, "branch offset overflow");
    return;
  }
  patching_assembler.b(jump_offset, LeaveLK);

  // If the frame is bigger than the stack, we throw the stack overflow
  // exception unconditionally. Thereby we can avoid the integer overflow
  // check in the condition code.
  RecordComment("OOL: stack check for large frame");
  Label continuation;
  if (frame_size < v8_flags.stack_size * 1024) {
    Register stack_limit = ip;
    LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit, r0);
    AddS64(stack_limit, stack_limit, Operand(frame_size), r0);
    CmpU64(sp, stack_limit);
    bge(&continuation);
  }

  Call(static_cast<Address>(Builtin::kWasmStackOverflow),
       RelocInfo::WASM_STUB_CALL);
  // The call will not return; just define an empty safepoint.
  safepoint_table_builder->DefineSafepoint(this);
  if (v8_flags.debug_code) stop();

  bind(&continuation);

  // Now allocate the stack space. Note that this might do more than just
  // decrementing the SP; consult {MacroAssembler::AllocateStackSpace}.
  SubS64(sp, sp, Operand(frame_size), r0);

  // Jump back to the start of the function, from {pc_offset()} to
  // right after the reserved space for the {__ sub(sp, sp, framesize)} (which
  // is a branch now).
  jump_offset = offset - pc_offset() + kInstrSize;
  if (!is_int26(jump_offset)) {
    bailout(kUnsupportedArchitecture, "branch offset overflow");
    return;
  }
  b(jump_offset, LeaveLK);
}

void LiftoffAssembler::FinishCode() { EmitConstantPool(); }

void LiftoffAssembler::AbortCompilation() { FinishCode(); }

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
  return (kind == kS128 || is_reference(kind));
}

void LiftoffAssembler::CheckTierUp(int declared_func_index, int budget_used,
                                   Label* ool_label,
                                   const FreezeCacheState& frozen) {
  Register budget_array = ip;
  Register instance_data = cache_state_.cached_instance_data;

  if (instance_data == no_reg) {
    instance_data = budget_array;  // Reuse the temp register.
    LoadInstanceDataFromFrame(instance_data);
  }

  constexpr int kArrayOffset = wasm::ObjectAccess::ToTagged(
      WasmTrustedInstanceData::kTieringBudgetArrayOffset);
  LoadU64(budget_array, MemOperand(instance_data, kArrayOffset), r0);

  int budget_arr_offset = kInt32Size * declared_func_index;
  // Pick a random register from kLiftoffAssemblerGpCacheRegs.
  // TODO(miladfarca): Use ScratchRegisterScope when available.
  Register budget = r15;
  push(budget);
  MemOperand budget_addr(budget_array, budget_arr_offset);
  LoadS32(budget, budget_addr, r0);
  mov(r0, Operand(budget_used));
  sub(budget, budget, r0, LeaveOE, SetRC);
  StoreU32(budget, budget_addr, r0);
  pop(budget);
  blt(ool_label, cr0);
}

void LiftoffAssembler::LoadConstant(LiftoffRegister reg, WasmValue value) {
  switch (value.type().kind()) {
    case kI32:
      mov(reg.gp(), Operand(value.to_i32()));
      break;
    case kI64:
      mov(reg.gp(), Operand(value.to_i64()));
      break;
    case kF32: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      mov(scratch, Operand(value.to_f32_boxed().get_bits()));
      MovIntToFloat(reg.fp(), scratch, ip);
      break;
    }
    case kF64: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      mov(scratch, Operand(value.to_f64_boxed().get_bits()));
      MovInt64ToDouble(reg.fp(), scratch);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::LoadInstanceDataFromFrame(Register dst) {
  LoadU64(dst, liftoff::GetInstanceDataOperand(), r0);
}

void LiftoffAssembler::LoadTrustedPointer(Register dst, Register src_addr,
                                          int offset, IndirectPointerTag tag) {
  MemOperand src{src_addr, offset};
  LoadTrustedPointerField(dst, src, tag, r0);
}

void LiftoffAssembler::LoadFromInstance(Register dst, Register instance,
                                        int offset, int size) {
  DCHECK_LE(0, offset);
  switch (size) {
    case 1:
      LoadU8(dst, MemOperand(instance, offset), r0);
      break;
    case 4:
      LoadU32(dst, MemOperand(instance, offset), r0);
      break;
    case 8:
      LoadU64(dst, MemOperand(instance, offset), r0);
      break;
    default:
      UNIMPLEMENTED();
  }
}

void LiftoffAssembler::LoadTaggedPointerFromInstance(Register dst,
                                                     Register instance,
                                                     int offset) {
  LoadTaggedField(dst, MemOperand(instance, offset), r0);
}

void LiftoffAssembler::SpillInstanceData(Register instance) {
  StoreU64(instance, liftoff::GetInstanceDataOperand(), r0);
}

void LiftoffAssembler::ResetOSRTarget() {}

void LiftoffAssembler::LoadTaggedPointer(Register dst, Register src_addr,
                                         Register offset_reg,
                                         int32_t offset_imm,
                                         uint32_t* protected_load_pc,
                                         bool needs_shift) {
  unsigned shift_amount = !needs_shift ? 0 : COMPRESS_POINTERS_BOOL ? 2 : 3;
  if (offset_reg != no_reg && shift_amount != 0) {
    ShiftLeftU64(ip, offset_reg, Operand(shift_amount));
    offset_reg = ip;
  }
  if (protected_load_pc) *protected_load_pc = pc_offset();
  LoadTaggedField(dst, MemOperand(src_addr, offset_reg, offset_imm), r0);
}

void LiftoffAssembler::LoadProtectedPointer(Register dst, Register src_addr,
                                            int32_t offset) {
  static_assert(!V8_ENABLE_SANDBOX_BOOL);
  LoadTaggedPointer(dst, src_addr, no_reg, offset);
}

void LiftoffAssembler::LoadFullPointer(Register dst, Register src_addr,
                                       int32_t offset_imm) {
  LoadU64(dst, MemOperand(src_addr, offset_imm), r0);
}

#ifdef V8_ENABLE_SANDBOX
void LiftoffAssembler::LoadCodeEntrypointViaCodePointer(Register dst,
                                                        Register src_addr,
                                                        int32_t offset_imm) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MemOperand src_op =
      liftoff::GetMemOp(this, src_addr, no_reg, offset_imm, scratch);
  MacroAssembler::LoadCodeEntrypointViaCodePointer(dst, src_op, scratch);
}
#endif

void LiftoffAssembler::StoreTaggedPointer(Register dst_addr,
                                          Register offset_reg,
                                          int32_t offset_imm, Register src,
                                          LiftoffRegList /* pinned */,
                                          uint32_t* protected_store_pc,
                                          SkipWriteBarrier skip_write_barrier) {
  MemOperand dst_op = MemOperand(dst_addr, offset_reg, offset_imm);
  if (protected_store_pc) *protected_store_pc = pc_offset();
  StoreTaggedField(src, dst_op, r0);

  if (skip_write_barrier || v8_flags.disable_write_barriers) return;

  Label exit;
  // NOTE: to_condition(kZero) is the equality condition (eq)
  // This line verifies the masked address is equal to dst_addr,
  // not that it is zero!
  CheckPageFlag(dst_addr, ip, MemoryChunk::kPointersFromHereAreInterestingMask,
                to_condition(kZero), &exit);
  JumpIfSmi(src, &exit);
  CheckPageFlag(src, ip, MemoryChunk::kPointersToHereAreInterestingMask, eq,
                &exit);
  mov(ip, Operand(offset_imm));
  add(ip, ip, dst_addr);
  if (offset_reg != no_reg) {
    add(ip, ip, offset_reg);
  }
  CallRecordWriteStubSaveRegisters(dst_addr, ip, SaveFPRegsMode::kSave,
                                   StubCallMode::kCallWasmRuntimeStub);
  bind(&exit);
}

void LiftoffAssembler::Load(LiftoffRegister dst, Register src_addr,
                            Register offset_reg, uintptr_t offset_imm,
                            LoadType type, uint32_t* protected_load_pc,
                            bool is_load_mem, bool i64_offset,
                            bool needs_shift) {
  if (!i64_offset && offset_reg != no_reg) {
    ZeroExtWord32(ip, offset_reg);
    offset_reg = ip;
  }
  unsigned shift_amount = needs_shift ? type.size_log_2() : 0;
  if (offset_reg != no_reg && shift_amount != 0) {
    ShiftLeftU64(ip, offset_reg, Operand(shift_amount));
    offset_reg = ip;
  }
  MemOperand src_op = MemOperand(src_addr, offset_reg, offset_imm);
  if (protected_load_pc) *protected_load_pc = pc_offset();
  switch (type.value()) {
    case LoadType::kI32Load8U:
    case LoadType::kI64Load8U:
      LoadU8(dst.gp(), src_op, r0);
      break;
    case LoadType::kI32Load8S:
    case LoadType::kI64Load8S:
      LoadS8(dst.gp(), src_op, r0);
      break;
    case LoadType::kI32Load16U:
    case LoadType::kI64Load16U:
      if (is_load_mem) {
        LoadU16LE(dst.gp(), src_op, r0);
      } else {
        LoadU16(dst.gp(), src_op, r0);
      }
      break;
    case LoadType::kI32Load16S:
    case LoadType::kI64Load16S:
      if (is_load_mem) {
        LoadS16LE(dst.gp(), src_op, r0);
      } else {
        LoadS16(dst.gp(), src_op, r0);
      }
      break;
    case LoadType::kI64Load32U:
      if (is_load_mem) {
        LoadU32LE(dst.gp(), src_op, r0);
      } else {
        LoadU32(dst.gp(), src_op, r0);
      }
      break;
    case LoadType::kI32Load:
    case LoadType::kI64Load32S:
      if (is_load_mem) {
        LoadS32LE(dst.gp(), src_op, r0);
      } else {
        LoadS32(dst.gp(), src_op, r0);
      }
      break;
    case LoadType::kI64Load:
      if (is_load_mem) {
        LoadU64LE(dst.gp(), src_op, r0);
      } else {
        LoadU64(dst.gp(), src_op, r0);
      }
      break;
    case LoadType::kF32Load:
      if (is_load_mem) {
        // `ip` could be used as offset_reg.
        Register scratch = ip;
        if (offset_reg == ip) {
          scratch = GetRegisterThatIsNotOneOf(src_addr);
          push(scratch);
        }
        LoadF32LE(dst.fp(), src_op, r0, scratch);
        if (offset_reg == ip) {
          pop(scratch);
        }
      } else {
        LoadF32(dst.fp(), src_op, r0);
      }
      break;
    case LoadType::kF64Load:
      if (is_load_mem) {
        // `ip` could be used as offset_reg.
        Register scratch = ip;
        if (offset_reg == ip) {
          scratch = GetRegisterThatIsNotOneOf(src_addr);
          push(scratch);
        }
        LoadF64LE(dst.fp(), src_op, r0, scratch);
        if (offset_reg == ip) {
          pop(scratch);
        }
      } else {
        LoadF64(dst.fp(), src_op, r0);
      }
      break;
    case LoadType::kS128Load:
      if (is_load_mem) {
        LoadSimd128LE(dst.fp().toSimd(), src_op, r0);
      } else {
        LoadSimd128(dst.fp().toSimd(), src_op, r0);
      }
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Store(Register dst_addr, Register offset_reg,
                             uintptr_t offset_imm, LiftoffRegister src,
                             StoreType type, LiftoffRegList pinned,
                             uint32_t* protected_store_pc, bool is_store_mem,
                             bool i64_offset) {
  if (!i64_offset && offset_reg != no_reg) {
    ZeroExtWord32(ip, offset_reg);
    offset_reg = ip;
  }
  MemOperand dst_op = MemOperand(dst_addr, offset_reg, offset_imm);
  if (protected_store_pc) *protected_store_pc = pc_offset();
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8:
      StoreU8(src.gp(), dst_op, r0);
      break;
    case StoreType::kI32Store16:
    case StoreType::kI64Store16:
      if (is_store_mem) {
        StoreU16LE(src.gp(), dst_op, r0);
      } else {
        StoreU16(src.gp(), dst_op, r0);
      }
      break;
    case StoreType::kI32Store:
    case StoreType::kI64Store32:
      if (is_store_mem) {
        StoreU32LE(src.gp(), dst_op, r0);
      } else {
        StoreU32(src.gp(), dst_op, r0);
      }
      break;
    case StoreType::kI64Store:
      if (is_store_mem) {
        StoreU64LE(src.gp(), dst_op, r0);
      } else {
        StoreU64(src.gp(), dst_op, r0);
      }
      break;
    case StoreType::kF32Store:
      if (is_store_mem) {
        Register scratch2 = GetUnusedRegister(kGpReg, pinned).gp();
        StoreF32LE(src.fp(), dst_op, r0, scratch2);
      } else {
        StoreF32(src.fp(), dst_op, r0);
      }
      break;
    case StoreType::kF64Store:
      if (is_store_mem) {
        Register scratch2 = GetUnusedRegister(kGpReg, pinned).gp();
        StoreF64LE(src.fp(), dst_op, r0, scratch2);
      } else {
        StoreF64(src.fp(), dst_op, r0);
      }
      break;
    case StoreType::kS128Store: {
      if (is_store_mem) {
        StoreSimd128LE(src.fp().toSimd(), dst_op, r0, kScratchSimd128Reg);
      } else {
        StoreSimd128(src.fp().toSimd(), dst_op, r0);
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicLoad(LiftoffRegister dst, Register src_addr,
                                  Register offset_reg, uintptr_t offset_imm,
                                  LoadType type, LiftoffRegList /* pinned */,
                                  bool i64_offset) {
  Load(dst, src_addr, offset_reg, offset_imm, type, nullptr, true, i64_offset);
  lwsync();
}

void LiftoffAssembler::AtomicStore(Register dst_addr, Register offset_reg,
                                   uintptr_t offset_imm, LiftoffRegister src,
                                   StoreType type, LiftoffRegList pinned,
                                   bool i64_offset) {
  lwsync();
  Store(dst_addr, offset_reg, offset_imm, src, type, pinned, nullptr, true,
        i64_offset);
  sync();
}

#ifdef V8_TARGET_BIG_ENDIAN
constexpr bool is_be = true;
#else
constexpr bool is_be = false;
#endif

#define ATOMIC_OP(instr)                                                 \
  {                                                                      \
    if (!i64_offset && offset_reg != no_reg) {                           \
      ZeroExtWord32(ip, offset_reg);                                     \
      offset_reg = ip;                                                   \
    }                                                                    \
                                                                         \
    Register offset = r0;                                                \
    if (offset_imm != 0) {                                               \
      mov(offset, Operand(offset_imm));                                  \
      if (offset_reg != no_reg) add(offset, offset, offset_reg);         \
      mr(ip, offset);                                                    \
      offset = ip;                                                       \
    } else if (offset_reg != no_reg) {                                   \
      offset = offset_reg;                                               \
    }                                                                    \
                                                                         \
    MemOperand dst = MemOperand(offset, dst_addr);                       \
                                                                         \
    switch (type.value()) {                                              \
      case StoreType::kI32Store8:                                        \
      case StoreType::kI64Store8: {                                      \
        auto op_func = [&](Register dst, Register lhs, Register rhs) {   \
          instr(dst, lhs, rhs);                                          \
        };                                                               \
        AtomicOps<uint8_t>(dst, value.gp(), result.gp(), r0, op_func);   \
        break;                                                           \
      }                                                                  \
      case StoreType::kI32Store16:                                       \
      case StoreType::kI64Store16: {                                     \
        auto op_func = [&](Register dst, Register lhs, Register rhs) {   \
          if (is_be) {                                                   \
            Register scratch = GetRegisterThatIsNotOneOf(lhs, rhs, dst); \
            push(scratch);                                               \
            ByteReverseU16(dst, lhs, scratch);                           \
            instr(dst, dst, rhs);                                        \
            ByteReverseU16(dst, dst, scratch);                           \
            pop(scratch);                                                \
          } else {                                                       \
            instr(dst, lhs, rhs);                                        \
          }                                                              \
        };                                                               \
        AtomicOps<uint16_t>(dst, value.gp(), result.gp(), r0, op_func);  \
        if (is_be) {                                                     \
          ByteReverseU16(result.gp(), result.gp(), ip);                  \
        }                                                                \
        break;                                                           \
      }                                                                  \
      case StoreType::kI32Store:                                         \
      case StoreType::kI64Store32: {                                     \
        auto op_func = [&](Register dst, Register lhs, Register rhs) {   \
          if (is_be) {                                                   \
            Register scratch = GetRegisterThatIsNotOneOf(lhs, rhs, dst); \
            push(scratch);                                               \
            ByteReverseU32(dst, lhs, scratch);                           \
            instr(dst, dst, rhs);                                        \
            ByteReverseU32(dst, dst, scratch);                           \
            pop(scratch);                                                \
          } else {                                                       \
            instr(dst, lhs, rhs);                                        \
          }                                                              \
        };                                                               \
        AtomicOps<uint32_t>(dst, value.gp(), result.gp(), r0, op_func);  \
        if (is_be) {                                                     \
          ByteReverseU32(result.gp(), result.gp(), ip);                  \
        }                                                                \
        break;                                                           \
      }                                                                  \
      case StoreType::kI64Store: {                                       \
        auto op_func = [&](Register dst, Register lhs, Register rhs) {   \
          if (is_be) {                                                   \
            ByteReverseU64(dst, lhs);                                    \
            instr(dst, dst, rhs);                                        \
            ByteReverseU64(dst, dst);                                    \
          } else {                                                       \
            instr(dst, lhs, rhs);                                        \
          }                                                              \
        };                                                               \
        AtomicOps<uint64_t>(dst, value.gp(), result.gp(), r0, op_func);  \
        if (is_be) {                                                     \
          ByteReverseU64(result.gp(), result.gp());                      \
        }                                                                \
        break;                                                           \
      }                                                                  \
      default:                                                           \
        UNREACHABLE();                                                   \
    }                                                                    \
  }

void LiftoffAssembler::AtomicAdd(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  ATOMIC_OP(add);
}

void LiftoffAssembler::AtomicSub(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  ATOMIC_OP(sub);
}

void LiftoffAssembler::AtomicAnd(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  ATOMIC_OP(and_);
}

void LiftoffAssembler::AtomicOr(Register dst_addr, Register offset_reg,
                                uintptr_t offset_imm, LiftoffRegister value,
                                LiftoffRegister result, StoreType type,
                                bool i64_offset) {
  ATOMIC_OP(orx);
}

void LiftoffAssembler::AtomicXor(Register dst_addr, Register offset_reg,
                                 uintptr_t offset_imm, LiftoffRegister value,
                                 LiftoffRegister result, StoreType type,
                                 bool i64_offset) {
  ATOMIC_OP(xor_);
}

void LiftoffAssembler::AtomicExchange(Register dst_addr, Register offset_reg,
                                      uintptr_t offset_imm,
                                      LiftoffRegister value,
                                      LiftoffRegister result, StoreType type,
                                      bool i64_offset) {
  if (!i64_offset && offset_reg != no_reg) {
    ZeroExtWord32(ip, offset_reg);
    offset_reg = ip;
  }

  Register offset = r0;
  if (offset_imm != 0) {
    mov(offset, Operand(offset_imm));
    if (offset_reg != no_reg) add(offset, offset, offset_reg);
    mr(ip, offset);
    offset = ip;
  } else if (offset_reg != no_reg) {
    offset = offset_reg;
  }
  MemOperand dst = MemOperand(offset, dst_addr);
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8: {
      MacroAssembler::AtomicExchange<uint8_t>(dst, value.gp(), result.gp());
      break;
    }
    case StoreType::kI32Store16:
    case StoreType::kI64Store16: {
      if (is_be) {
        Register scratch = GetRegisterThatIsNotOneOf(value.gp(), result.gp());
        push(scratch);
        ByteReverseU16(r0, value.gp(), scratch);
        pop(scratch);
        MacroAssembler::AtomicExchange<uint16_t>(dst, r0, result.gp());
        ByteReverseU16(result.gp(), result.gp(), ip);
      } else {
        MacroAssembler::AtomicExchange<uint16_t>(dst, value.gp(), result.gp());
      }
      break;
    }
    case StoreType::kI32Store:
    case StoreType::kI64Store32: {
      if (is_be) {
        Register scratch = GetRegisterThatIsNotOneOf(value.gp(), result.gp());
        push(scratch);
        ByteReverseU32(r0, value.gp(), scratch);
        pop(scratch);
        MacroAssembler::AtomicExchange<uint32_t>(dst, r0, result.gp());
        ByteReverseU32(result.gp(), result.gp(), ip);
      } else {
        MacroAssembler::AtomicExchange<uint32_t>(dst, value.gp(), result.gp());
      }
      break;
    }
    case StoreType::kI64Store: {
      if (is_be) {
        ByteReverseU64(r0, value.gp());
        MacroAssembler::AtomicExchange<uint64_t>(dst, r0, result.gp());
        ByteReverseU64(result.gp(), result.gp());
      } else {
        MacroAssembler::AtomicExchange<uint64_t>(dst, value.gp(), result.gp());
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicCompareExchange(
    Register dst_addr, Register offset_reg, uintptr_t offset_imm,
    LiftoffRegister expected, LiftoffRegister new_value, LiftoffRegister result,
    StoreType type, bool i64_offset) {
  if (!i64_offset && offset_reg != no_reg) {
    ZeroExtWord32(ip, offset_reg);
    offset_reg = ip;
  }

  Register offset = r0;
  if (offset_imm != 0) {
    mov(offset, Operand(offset_imm));
    if (offset_reg != no_reg) add(offset, offset, offset_reg);
    mr(ip, offset);
    offset = ip;
  } else if (offset_reg != no_reg) {
    offset = offset_reg;
  }
  MemOperand dst = MemOperand(offset, dst_addr);
  switch (type.value()) {
    case StoreType::kI32Store8:
    case StoreType::kI64Store8: {
      MacroAssembler::AtomicCompareExchange<uint8_t>(
          dst, expected.gp(), new_value.gp(), result.gp(), r0);
      break;
    }
    case StoreType::kI32Store16:
    case StoreType::kI64Store16: {
      if (is_be) {
        Push(new_value.gp(), expected.gp());
        Register scratch = GetRegisterThatIsNotOneOf(
            new_value.gp(), expected.gp(), result.gp());
        push(scratch);
        ByteReverseU16(new_value.gp(), new_value.gp(), scratch);
        ByteReverseU16(expected.gp(), expected.gp(), scratch);
        pop(scratch);
        MacroAssembler::AtomicCompareExchange<uint16_t>(
            dst, expected.gp(), new_value.gp(), result.gp(), r0);
        ByteReverseU16(result.gp(), result.gp(), r0);
        Pop(new_value.gp(), expected.gp());
      } else {
        MacroAssembler::AtomicCompareExchange<uint16_t>(
            dst, expected.gp(), new_value.gp(), result.gp(), r0);
      }
      break;
    }
    case StoreType::kI32Store:
    case StoreType::kI64Store32: {
      if (is_be) {
        Push(new_value.gp(), expected.gp());
        Register scratch = GetRegisterThatIsNotOneOf(
            new_value.gp(), expected.gp(), result.gp());
        push(scratch);
        ByteReverseU32(new_value.gp(), new_value.gp(), scratch);
        ByteReverseU32(expected.gp(), expected.gp(), scratch);
        pop(scratch);
        MacroAssembler::AtomicCompareExchange<uint32_t>(
            dst, expected.gp(), new_value.gp(), result.gp(), r0);
        ByteReverseU32(result.gp(), result.gp(), r0);
        Pop(new_value.gp(), expected.gp());
      } else {
        MacroAssembler::AtomicCompareExchange<uint32_t>(
            dst, expected.gp(), new_value.gp(), result.gp(), r0);
      }
      break;
    }
    case StoreType::kI64Store: {
      if (is_be) {
        Push(new_value.gp(), expected.gp());
        ByteReverseU64(new_value.gp(), new_value.gp());
        ByteReverseU64(expected.gp(), expected.gp());
        MacroAssembler::AtomicCompareExchange<uint64_t>(
            dst, expected.gp(), new_value.gp(), result.gp(), r0);
        ByteReverseU64(result.gp(), result.gp());
        Pop(new_value.gp(), expected.gp());
      } else {
        MacroAssembler::AtomicCompareExchange<uint64_t>(
            dst, expected.gp(), new_value.gp(), result.gp(), r0);
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::AtomicFence() { sync(); }

void LiftoffAssembler::LoadCallerFrameSlot(LiftoffRegister dst,
                                           uint32_t caller_slot_idx,
                                           ValueKind kind) {
  int32_t offset = (caller_slot_idx + 1) * kSystemPointerSize;
  switch (kind) {
    case kI32: {
#if defined(V8_TARGET_BIG_ENDIAN)
      LoadS32(dst.gp(), MemOperand(fp, offset + 4), r0);
      break;
#else
      LoadS32(dst.gp(), MemOperand(fp, offset), r0);
      break;
#endif
    }
    case kRef:
    case kRtt:
    case kRefNull:
    case kI64: {
      LoadU64(dst.gp(), MemOperand(fp, offset), r0);
      break;
    }
    case kF32: {
      LoadF32(dst.fp(), MemOperand(fp, offset), r0);
      break;
    }
    case kF64: {
      LoadF64(dst.fp(), MemOperand(fp, offset), r0);
      break;
    }
    case kS128: {
      LoadSimd128(dst.fp().toSimd(), MemOperand(fp, offset), r0);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::StoreCallerFrameSlot(LiftoffRegister src,
                                            uint32_t caller_slot_idx,
                                            ValueKind kind) {
  int32_t offset = (caller_slot_idx + 1) * kSystemPointerSize;
  switch (kind) {
    case kI32: {
#if defined(V8_TARGET_BIG_ENDIAN)
      StoreU32(src.gp(), MemOperand(fp, offset + 4), r0);
      break;
#else
      StoreU32(src.gp(), MemOperand(fp, offset), r0);
      break;
#endif
    }
    case kRef:
    case kRtt:
    case kRefNull:
    case kI64: {
      StoreU64(src.gp(), MemOperand(fp, offset), r0);
      break;
    }
    case kF32: {
      StoreF32(src.fp(), MemOperand(fp, offset), r0);
      break;
    }
    case kF64: {
      StoreF64(src.fp(), MemOperand(fp, offset), r0);
      break;
    }
    case kS128: {
      StoreSimd128(src.fp().toSimd(), MemOperand(fp, offset), r0);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::LoadReturnStackSlot(LiftoffRegister dst, int offset,
                                           ValueKind kind) {
  switch (kind) {
    case kI32: {
#if defined(V8_TARGET_BIG_ENDIAN)
      LoadS32(dst.gp(), MemOperand(sp, offset + 4), r0);
      break;
#else
      LoadS32(dst.gp(), MemOperand(sp, offset), r0);
      break;
#endif
    }
    case kRef:
    case kRtt:
    case kRefNull:
    case kI64: {
      LoadU64(dst.gp(), MemOperand(sp, offset), r0);
      break;
    }
    case kF32: {
      LoadF32(dst.fp(), MemOperand(sp, offset), r0);
      break;
    }
    case kF64: {
      LoadF64(dst.fp(), MemOperand(sp, offset), r0);
      break;
    }
    case kS128: {
      LoadSimd128(dst.fp().toSimd(), MemOperand(sp, offset), r0);
      break;
    }
    default:
      UNREACHABLE();
  }
}

#ifdef V8_TARGET_BIG_ENDIAN
constexpr int stack_bias = -4;
#else
constexpr int stack_bias = 0;
#endif

void LiftoffAssembler::MoveStackValue(uint32_t dst_offset, uint32_t src_offset,
                                      ValueKind kind) {
  DCHECK_NE(dst_offset, src_offset);

  switch (kind) {
    case kI32:
    case kF32:
      LoadU32(ip, liftoff::GetStackSlot(src_offset + stack_bias), r0);
      StoreU32(ip, liftoff::GetStackSlot(dst_offset + stack_bias), r0);
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
    case kF64:
      LoadU64(ip, liftoff::GetStackSlot(src_offset), r0);
      StoreU64(ip, liftoff::GetStackSlot(dst_offset), r0);
      break;
    case kS128:
      LoadSimd128(kScratchSimd128Reg, liftoff::GetStackSlot(src_offset), r0);
      StoreSimd128(kScratchSimd128Reg, liftoff::GetStackSlot(dst_offset), r0);
      break;
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Move(Register dst, Register src, ValueKind kind) {
  mr(dst, src);
}

void LiftoffAssembler::Move(DoubleRegister dst, DoubleRegister src,
                            ValueKind kind) {
  if (kind == kF32 || kind == kF64) {
    fmr(dst, src);
  } else {
    DCHECK_EQ(kS128, kind);
    vor(dst.toSimd(), src.toSimd(), src.toSimd());
  }
}

void LiftoffAssembler::Spill(int offset, LiftoffRegister reg, ValueKind kind) {
  DCHECK_LT(0, offset);
  RecordUsedSpillOffset(offset);

  switch (kind) {
    case kI32:
      StoreU32(reg.gp(), liftoff::GetStackSlot(offset + stack_bias), r0);
      break;
    case kI64:
    case kRefNull:
    case kRef:
    case kRtt:
      StoreU64(reg.gp(), liftoff::GetStackSlot(offset), r0);
      break;
    case kF32:
      StoreF32(reg.fp(), liftoff::GetStackSlot(offset + stack_bias), r0);
      break;
    case kF64:
      StoreF64(reg.fp(), liftoff::GetStackSlot(offset), r0);
      break;
    case kS128: {
      StoreSimd128(reg.fp().toSimd(), liftoff::GetStackSlot(offset), r0);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::Spill(int offset, WasmValue value) {
  RecordUsedSpillOffset(offset);
  UseScratchRegisterScope temps(this);
  Register src = no_reg;
  src = ip;
  switch (value.type().kind()) {
    case kI32: {
      mov(src, Operand(value.to_i32()));
      StoreU32(src, liftoff::GetStackSlot(offset + stack_bias), r0);
      break;
    }
    case kI64: {
      mov(src, Operand(value.to_i64()));
      StoreU64(src, liftoff::GetStackSlot(offset), r0);
      break;
    }
    default:
      // We do not track f32 and f64 constants, hence they are unreachable.
      UNREACHABLE();
  }
}

void LiftoffAssembler::Fill(LiftoffRegister reg, int offset, ValueKind kind) {
  switch (kind) {
    case kI32:
      LoadS32(reg.gp(), liftoff::GetStackSlot(offset + stack_bias), r0);
      break;
    case kI64:
    case kRef:
    case kRefNull:
    case kRtt:
      LoadU64(reg.gp(), liftoff::GetStackSlot(offset), r0);
      break;
    case kF32:
      LoadF32(reg.fp(), liftoff::GetStackSlot(offset + stack_bias), r0);
      break;
    case kF64:
      LoadF64(reg.fp(), liftoff::GetStackSlot(offset), r0);
      break;
    case kS128: {
      LoadSimd128(reg.fp().toSimd(), liftoff::GetStackSlot(offset), r0);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::FillI64Half(Register, int offset, RegPairHalf) {
  bailout(kUnsupportedArchitecture, "FillI64Half");
}

void LiftoffAssembler::FillStackSlotsWithZero(int start, int size) {
  DCHECK_LT(0, size);
  DCHECK_EQ(0, size % 8);
  RecordUsedSpillOffset(start + size);

  // We need a zero reg. Always use r0 for that, and push it before to restore
  // its value afterwards.

  if (size <= 36) {
    // Special straight-line code for up to nine words. Generates one
    // instruction per word.
    mov(ip, Operand::Zero());
    uint32_t remainder = size;
    for (; remainder >= kStackSlotSize; remainder -= kStackSlotSize) {
      StoreU64(ip, liftoff::GetStackSlot(start + remainder), r0);
    }
    DCHECK(remainder == 4 || remainder == 0);
    if (remainder) {
      StoreU32(ip, liftoff::GetStackSlot(start + remainder), r0);
    }
  } else {
    Label loop;
    push(r4);

    mov(r4, Operand(size / kSystemPointerSize));
    mtctr(r4);

    SubS64(r4, fp, Operand(start + size + kSystemPointerSize), r0);
    mov(r0, Operand::Zero());

    bind(&loop);
    StoreU64WithUpdate(r0, MemOperand(r4, kSystemPointerSize));
    bdnz(&loop);

    pop(r4);
  }
}

void LiftoffAssembler::LoadSpillAddress(Register dst, int offset,
                                        ValueKind kind) {
  if (kind == kI32) offset = offset + stack_bias;
  SubS64(dst, fp, Operand(offset));
}

#define SIGN_EXT(r) extsw(r, r)
#define ROUND_F64_TO_F32(fpr) frsp(fpr, fpr)
#define INT32_AND_WITH_1F(x) Operand(x & 0x1f)
#define INT32_AND_WITH_3F(x) Operand(x & 0x3f)
#define REGISTER_AND_WITH_1F    \
  ([&](Register rhs) {          \
    andi(r0, rhs, Operand(31)); \
    return r0;                  \
  })

#define REGISTER_AND_WITH_3F    \
  ([&](Register rhs) {          \
    andi(r0, rhs, Operand(63)); \
    return r0;                  \
  })

#define LFR_TO_REG(reg) reg.gp()

// V(name, instr, dtype, stype, dcast, scast, rcast, return_val, return_type)
#define UNOP_LIST(V)                                                         \
  V(f32_abs, fabs, DoubleRegister, DoubleRegister, , , USE, , void)          \
  V(f32_neg, fneg, DoubleRegister, DoubleRegister, , , USE, , void)          \
  V(f32_sqrt, fsqrt, DoubleRegister, DoubleRegister, , , ROUND_F64_TO_F32, , \
    void)                                                                    \
  V(f32_floor, frim, DoubleRegister, DoubleRegister, , , ROUND_F64_TO_F32,   \
    true, bool)                                                              \
  V(f32_ceil, frip, DoubleRegister, DoubleRegister, , , ROUND_F64_TO_F32,    \
    true, bool)                                                              \
  V(f32_trunc, friz, DoubleRegister, DoubleRegister, , , ROUND_F64_TO_F32,   \
    true, bool)                                                              \
  V(f64_abs, fabs, DoubleRegister, DoubleRegister, , , USE, , void)          \
  V(f64_neg, fneg, DoubleRegister, DoubleRegister, , , USE, , void)          \
  V(f64_sqrt, fsqrt, DoubleRegister, DoubleRegister, , , USE, , void)        \
  V(f64_floor, frim, DoubleRegister, DoubleRegister, , , USE, true, bool)    \
  V(f64_ceil, frip, DoubleRegister, DoubleRegister, , , USE, true, bool)     \
  V(f64_trunc, friz, DoubleRegister, DoubleRegister, , , USE, true, bool)    \
  V(i32_clz, CountLeadingZerosU32, Register, Register, , , USE, , void)      \
  V(i32_ctz, CountTrailingZerosU32, Register, Register, , , USE, , void)     \
  V(i64_clz, CountLeadingZerosU64, LiftoffRegister, LiftoffRegister,         \
    LFR_TO_REG, LFR_TO_REG, USE, , void)                                     \
  V(i64_ctz, CountTrailingZerosU64, LiftoffRegister, LiftoffRegister,        \
    LFR_TO_REG, LFR_TO_REG, USE, , void)                                     \
  V(u32_to_uintptr, ZeroExtWord32, Register, Register, , , USE, , void)      \
  V(i32_signextend_i8, extsb, Register, Register, , , USE, , void)           \
  V(i32_signextend_i16, extsh, Register, Register, , , USE, , void)          \
  V(i64_signextend_i8, extsb, LiftoffRegister, LiftoffRegister, LFR_TO_REG,  \
    LFR_TO_REG, USE, , void)                                                 \
  V(i64_signextend_i16, extsh, LiftoffRegister, LiftoffRegister, LFR_TO_REG, \
    LFR_TO_REG, USE, , void)                                                 \
  V(i64_signextend_i32, extsw, LiftoffRegister, LiftoffRegister, LFR_TO_REG, \
    LFR_TO_REG, USE, , void)                                                 \
  V(i32_popcnt, Popcnt32, Register, Register, , , USE, true, bool)           \
  V(i64_popcnt, Popcnt64, LiftoffRegister, LiftoffRegister, LFR_TO_REG,      \
    LFR_TO_REG, USE, true, bool)

#define EMIT_UNOP_FUNCTION(name, instr, dtype, stype, dcast, scast, rcast, \
                           ret, return_type)                               \
  return_type LiftoffAssembler::emit_##name(dtype dst, stype src) {        \
    auto _dst = dcast(dst);                                                \
    auto _src = scast(src);                                                \
    instr(_dst, _src);                                                     \
    rcast(_dst);                                                           \
    return ret;                                                            \
  }
UNOP_LIST(EMIT_UNOP_FUNCTION)
#undef EMIT_UNOP_FUNCTION
#undef UNOP_LIST

// V(name, instr, dtype, stype1, stype2, dcast, scast1, scast2, rcast,
// return_val, return_type)
#define BINOP_LIST(V)                                                          \
  V(f32_copysign, CopySignF64, DoubleRegister, DoubleRegister, DoubleRegister, \
    , , , USE, , void)                                                         \
  V(f64_copysign, CopySignF64, DoubleRegister, DoubleRegister, DoubleRegister, \
    , , , USE, , void)                                                         \
  V(f32_min, MinF64, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f32_max, MaxF64, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f64_min, MinF64, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f64_max, MaxF64, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(i64_sub, SubS64, LiftoffRegister, LiftoffRegister, LiftoffRegister,        \
    LFR_TO_REG, LFR_TO_REG, LFR_TO_REG, USE, , void)                           \
  V(i64_add, AddS64, LiftoffRegister, LiftoffRegister, LiftoffRegister,        \
    LFR_TO_REG, LFR_TO_REG, LFR_TO_REG, USE, , void)                           \
  V(i64_addi, AddS64, LiftoffRegister, LiftoffRegister, int64_t, LFR_TO_REG,   \
    LFR_TO_REG, Operand, USE, , void)                                          \
  V(i32_sub, SubS32, Register, Register, Register, , , , USE, , void)          \
  V(i32_add, AddS32, Register, Register, Register, , , , USE, , void)          \
  V(i32_addi, AddS32, Register, Register, int32_t, , , Operand, USE, , void)   \
  V(i32_subi, SubS32, Register, Register, int32_t, , , Operand, USE, , void)   \
  V(i32_mul, MulS32, Register, Register, Register, , , , USE, , void)          \
  V(i64_mul, MulS64, LiftoffRegister, LiftoffRegister, LiftoffRegister,        \
    LFR_TO_REG, LFR_TO_REG, LFR_TO_REG, USE, , void)                           \
  V(i32_andi, AndU32, Register, Register, int32_t, , , Operand, USE, , void)   \
  V(i32_ori, OrU32, Register, Register, int32_t, , , Operand, USE, , void)     \
  V(i32_xori, XorU32, Register, Register, int32_t, , , Operand, USE, , void)   \
  V(i32_and, AndU32, Register, Register, Register, , , , USE, , void)          \
  V(i32_or, OrU32, Register, Register, Register, , , , USE, , void)            \
  V(i32_xor, XorU32, Register, Register, Register, , , , USE, , void)          \
  V(i64_and, AndU64, LiftoffRegister, LiftoffRegister, LiftoffRegister,        \
    LFR_TO_REG, LFR_TO_REG, LFR_TO_REG, USE, , void)                           \
  V(i64_or, OrU64, LiftoffRegister, LiftoffRegister, LiftoffRegister,          \
    LFR_TO_REG, LFR_TO_REG, LFR_TO_REG, USE, , void)                           \
  V(i64_xor, XorU64, LiftoffRegister, LiftoffRegister, LiftoffRegister,        \
    LFR_TO_REG, LFR_TO_REG, LFR_TO_REG, USE, , void)                           \
  V(i64_andi, AndU64, LiftoffRegister, LiftoffRegister, int32_t, LFR_TO_REG,   \
    LFR_TO_REG, Operand, USE, , void)                                          \
  V(i64_ori, OrU64, LiftoffRegister, LiftoffRegister, int32_t, LFR_TO_REG,     \
    LFR_TO_REG, Operand, USE, , void)                                          \
  V(i64_xori, XorU64, LiftoffRegister, LiftoffRegister, int32_t, LFR_TO_REG,   \
    LFR_TO_REG, Operand, USE, , void)                                          \
  V(i32_shli, ShiftLeftU32, Register, Register, int32_t, , ,                   \
    INT32_AND_WITH_1F, USE, , void)                                            \
  V(i32_sari, ShiftRightS32, Register, Register, int32_t, , ,                  \
    INT32_AND_WITH_1F, USE, , void)                                            \
  V(i32_shri, ShiftRightU32, Register, Register, int32_t, , ,                  \
    INT32_AND_WITH_1F, USE, , void)                                            \
  V(i32_shl, ShiftLeftU32, Register, Register, Register, , ,                   \
    REGISTER_AND_WITH_1F, USE, , void)                                         \
  V(i32_sar, ShiftRightS32, Register, Register, Register, , ,                  \
    REGISTER_AND_WITH_1F, USE, , void)                                         \
  V(i32_shr, ShiftRightU32, Register, Register, Register, , ,                  \
    REGISTER_AND_WITH_1F, USE, , void)                                         \
  V(i64_shl, ShiftLeftU64, LiftoffRegister, LiftoffRegister, Register,         \
    LFR_TO_REG, LFR_TO_REG, REGISTER_AND_WITH_3F, USE, , void)                 \
  V(i64_sar, ShiftRightS64, LiftoffRegister, LiftoffRegister, Register,        \
    LFR_TO_REG, LFR_TO_REG, REGISTER_AND_WITH_3F, USE, , void)                 \
  V(i64_shr, ShiftRightU64, LiftoffRegister, LiftoffRegister, Register,        \
    LFR_TO_REG, LFR_TO_REG, REGISTER_AND_WITH_3F, USE, , void)                 \
  V(i64_shli, ShiftLeftU64, LiftoffRegister, LiftoffRegister, int32_t,         \
    LFR_TO_REG, LFR_TO_REG, INT32_AND_WITH_3F, USE, , void)                    \
  V(i64_sari, ShiftRightS64, LiftoffRegister, LiftoffRegister, int32_t,        \
    LFR_TO_REG, LFR_TO_REG, INT32_AND_WITH_3F, USE, , void)                    \
  V(i64_shri, ShiftRightU64, LiftoffRegister, LiftoffRegister, int32_t,        \
    LFR_TO_REG, LFR_TO_REG, INT32_AND_WITH_3F, USE, , void)                    \
  V(f64_add, AddF64, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f64_sub, SubF64, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f64_mul, MulF64, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f64_div, DivF64, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f32_add, AddF32, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f32_sub, SubF32, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f32_mul, MulF32, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)                                                               \
  V(f32_div, DivF32, DoubleRegister, DoubleRegister, DoubleRegister, , , ,     \
    USE, , void)

#define EMIT_BINOP_FUNCTION(name, instr, dtype, stype1, stype2, dcast, scast1, \
                            scast2, rcast, ret, return_type)                   \
  return_type LiftoffAssembler::emit_##name(dtype dst, stype1 lhs,             \
                                            stype2 rhs) {                      \
    auto _dst = dcast(dst);                                                    \
    auto _lhs = scast1(lhs);                                                   \
    auto _rhs = scast2(rhs);                                                   \
    instr(_dst, _lhs, _rhs);                                                   \
    rcast(_dst);                                                               \
    return ret;                                                                \
  }

BINOP_LIST(EMIT_BINOP_FUNCTION)
#undef BINOP_LIST
#undef EMIT_BINOP_FUNCTION
#undef SIGN_EXT
#undef INT32_AND_WITH_1F
#undef REGISTER_AND_WITH_1F
#undef LFR_TO_REG

bool LiftoffAssembler::emit_f32_nearest_int(DoubleRegister dst,
                                            DoubleRegister src) {
  return false;
}

bool LiftoffAssembler::emit_f64_nearest_int(DoubleRegister dst,
                                            DoubleRegister src) {
  return false;
}

void LiftoffAssembler::IncrementSmi(LiftoffRegister dst, int offset) {
  UseScratchRegisterScope temps(this);
  if (COMPRESS_POINTERS_BOOL) {
    DCHECK(SmiValuesAre31Bits());
    Register scratch = temps.Acquire();
    LoadS32(scratch, MemOperand(dst.gp(), offset), r0);
    AddS64(scratch, scratch, Operand(Smi::FromInt(1)));
    StoreU32(scratch, MemOperand(dst.gp(), offset), r0);
  } else {
    Register scratch = temps.Acquire();
    SmiUntag(scratch, MemOperand(dst.gp(), offset), LeaveRC, r0);
    AddS64(scratch, scratch, Operand(1));
    SmiTag(scratch);
    StoreU64(scratch, MemOperand(dst.gp(), offset), r0);
  }
}

void LiftoffAssembler::emit_i32_divs(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  Label cont;

  // Check for division by zero.
  CmpS32(rhs, Operand::Zero(), r0);
  b(eq, trap_div_by_zero);

  // Check for kMinInt / -1. This is unrepresentable.
  CmpS32(rhs, Operand(-1), r0);
  bne(&cont);
  CmpS32(lhs, Operand(kMinInt), r0);
  b(eq, trap_div_unrepresentable);

  bind(&cont);
  DivS32(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_divu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  CmpS32(rhs, Operand::Zero(), r0);
  beq(trap_div_by_zero);
  DivU32(dst, lhs, rhs);
}

void LiftoffAssembler::emit_i32_rems(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  Label cont, done, trap_div_unrepresentable;
  // Check for division by zero.
  CmpS32(rhs, Operand::Zero(), r0);
  beq(trap_div_by_zero);

  // Check kMinInt/-1 case.
  CmpS32(rhs, Operand(-1), r0);
  bne(&cont);
  CmpS32(lhs, Operand(kMinInt), r0);
  beq(&trap_div_unrepresentable);

  // Continue noraml calculation.
  bind(&cont);
  ModS32(dst, lhs, rhs);
  bne(&done);

  // trap by kMinInt/-1 case.
  bind(&trap_div_unrepresentable);
  mov(dst, Operand(0));
  bind(&done);
}

void LiftoffAssembler::emit_i32_remu(Register dst, Register lhs, Register rhs,
                                     Label* trap_div_by_zero) {
  CmpS32(rhs, Operand::Zero(), r0);
  beq(trap_div_by_zero);
  ModU32(dst, lhs, rhs);
}

bool LiftoffAssembler::emit_i64_divs(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero,
                                     Label* trap_div_unrepresentable) {
  constexpr int64_t kMinInt64 = static_cast<int64_t>(1) << 63;
  Label cont;
  // Check for division by zero.
  CmpS64(rhs.gp(), Operand::Zero(), r0);
  beq(trap_div_by_zero);

  // Check for kMinInt / -1. This is unrepresentable.
  CmpS64(rhs.gp(), Operand(-1), r0);
  bne(&cont);
  CmpS64(lhs.gp(), Operand(kMinInt64), r0);
  beq(trap_div_unrepresentable);

  bind(&cont);
  DivS64(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_divu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  CmpS64(rhs.gp(), Operand::Zero(), r0);
  beq(trap_div_by_zero);
  // Do div.
  DivU64(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_i64_rems(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  constexpr int64_t kMinInt64 = static_cast<int64_t>(1) << 63;

  Label trap_div_unrepresentable;
  Label done;
  Label cont;

  // Check for division by zero.
  CmpS64(rhs.gp(), Operand::Zero(), r0);
  beq(trap_div_by_zero);

  // Check for kMinInt / -1. This is unrepresentable.
  CmpS64(rhs.gp(), Operand(-1), r0);
  bne(&cont);
  CmpS64(lhs.gp(), Operand(kMinInt64), r0);
  beq(&trap_div_unrepresentable);

  bind(&cont);
  ModS64(dst.gp(), lhs.gp(), rhs.gp());
  bne(&done);

  bind(&trap_div_unrepresentable);
  mov(dst.gp(), Operand(0));
  bind(&done);
  return true;
}

bool LiftoffAssembler::emit_i64_remu(LiftoffRegister dst, LiftoffRegister lhs,
                                     LiftoffRegister rhs,
                                     Label* trap_div_by_zero) {
  CmpS64(rhs.gp(), Operand::Zero(), r0);
  beq(trap_div_by_zero);
  ModU64(dst.gp(), lhs.gp(), rhs.gp());
  return true;
}

bool LiftoffAssembler::emit_type_conversion(WasmOpcode opcode,
                                            LiftoffRegister dst,
                                            LiftoffRegister src, Label* trap) {
  switch (opcode) {
    case kExprI32ConvertI64:
      extsw(dst.gp(), src.gp());
      return true;
    case kExprI64SConvertI32:
      extsw(dst.gp(), src.gp());
      return true;
    case kExprI64UConvertI32:
      ZeroExtWord32(dst.gp(), src.gp());
      return true;
    case kExprF32ConvertF64:
      frsp(dst.fp(), src.fp());
      return true;
    case kExprF64ConvertF32:
      fmr(dst.fp(), src.fp());
      return true;
    case kExprF32SConvertI32: {
      ConvertIntToFloat(src.gp(), dst.fp());
      return true;
    }
    case kExprF32UConvertI32: {
      ConvertUnsignedIntToFloat(src.gp(), dst.fp());
      return true;
    }
    case kExprF64SConvertI32: {
      ConvertIntToDouble(src.gp(), dst.fp());
      return true;
    }
    case kExprF64UConvertI32: {
      ConvertUnsignedIntToDouble(src.gp(), dst.fp());
      return true;
    }
    case kExprF64SConvertI64: {
      ConvertInt64ToDouble(src.gp(), dst.fp());
      return true;
    }
    case kExprF64UConvertI64: {
      ConvertUnsignedInt64ToDouble(src.gp(), dst.fp());
      return true;
    }
    case kExprF32SConvertI64: {
      ConvertInt64ToFloat(src.gp(), dst.fp());
      return true;
    }
    case kExprF32UConvertI64: {
      ConvertUnsignedInt64ToFloat(src.gp(), dst.fp());
      return true;
    }
    case kExprI32SConvertF64:
    case kExprI32SConvertF32: {
      LoadDoubleLiteral(kScratchDoubleReg, base::Double(0.0), r0);
      fcmpu(src.fp(), kScratchDoubleReg);
      bunordered(trap);

      mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      fctiwz(kScratchDoubleReg, src.fp());
      MovDoubleLowToInt(dst.gp(), kScratchDoubleReg);
      mcrfs(cr7, VXCVI);
      boverflow(trap, cr7);
      return true;
    }
    case kExprI32UConvertF64:
    case kExprI32UConvertF32: {
      mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      ConvertDoubleToUnsignedInt64(src.fp(), r0, kScratchDoubleReg,
                                   kRoundToZero);
      mcrfs(cr7, VXCVI);  // extract FPSCR field containing VXCVI into cr7
      boverflow(trap, cr7);
      ZeroExtWord32(dst.gp(), r0);
      CmpU64(dst.gp(), r0);
      bne(trap);
      return true;
    }
    case kExprI64SConvertF64:
    case kExprI64SConvertF32: {
      LoadDoubleLiteral(kScratchDoubleReg, base::Double(0.0), r0);
      fcmpu(src.fp(), kScratchDoubleReg);
      bunordered(trap);

      mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      fctidz(kScratchDoubleReg, src.fp());
      MovDoubleToInt64(dst.gp(), kScratchDoubleReg);
      mcrfs(cr7, VXCVI);
      boverflow(trap, cr7);
      return true;
    }
    case kExprI64UConvertF64:
    case kExprI64UConvertF32: {
      LoadDoubleLiteral(kScratchDoubleReg, base::Double(0.0), r0);
      fcmpu(src.fp(), kScratchDoubleReg);
      bunordered(trap);

      mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      fctiduz(kScratchDoubleReg, src.fp());
      MovDoubleToInt64(dst.gp(), kScratchDoubleReg);
      mcrfs(cr7, VXCVI);
      boverflow(trap, cr7);
      return true;
    }
    case kExprI32SConvertSatF64:
    case kExprI32SConvertSatF32: {
      Label done, src_is_nan;
      LoadDoubleLiteral(kScratchDoubleReg, base::Double(0.0), r0);
      fcmpu(src.fp(), kScratchDoubleReg);
      bunordered(&src_is_nan);

      mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      fctiwz(kScratchDoubleReg, src.fp());
      MovDoubleLowToInt(dst.gp(), kScratchDoubleReg);
      b(&done);

      bind(&src_is_nan);
      mov(dst.gp(), Operand::Zero());

      bind(&done);
      return true;
    }
    case kExprI32UConvertSatF64:
    case kExprI32UConvertSatF32: {
      Label done, src_is_nan;
      LoadDoubleLiteral(kScratchDoubleReg, base::Double(0.0), r0);
      fcmpu(src.fp(), kScratchDoubleReg);
      bunordered(&src_is_nan);

      mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      fctiwuz(kScratchDoubleReg, src.fp());
      MovDoubleLowToInt(dst.gp(), kScratchDoubleReg);
      b(&done);

      bind(&src_is_nan);
      mov(dst.gp(), Operand::Zero());

      bind(&done);
      return true;
    }
    case kExprI64SConvertSatF64:
    case kExprI64SConvertSatF32: {
      Label done, src_is_nan;
      LoadDoubleLiteral(kScratchDoubleReg, base::Double(0.0), r0);
      fcmpu(src.fp(), kScratchDoubleReg);
      bunordered(&src_is_nan);

      mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      fctidz(kScratchDoubleReg, src.fp());
      MovDoubleToInt64(dst.gp(), kScratchDoubleReg);
      b(&done);

      bind(&src_is_nan);
      mov(dst.gp(), Operand::Zero());

      bind(&done);
      return true;
    }
    case kExprI64UConvertSatF64:
    case kExprI64UConvertSatF32: {
      Label done, src_is_nan;
      LoadDoubleLiteral(kScratchDoubleReg, base::Double(0.0), r0);
      fcmpu(src.fp(), kScratchDoubleReg);
      bunordered(&src_is_nan);

      mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      fctiduz(kScratchDoubleReg, src.fp());
      MovDoubleToInt64(dst.gp(), kScratchDoubleReg);
      b(&done);

      bind(&src_is_nan);
      mov(dst.gp(), Operand::Zero());

      bind(&done);
      return true;
    }
    case kExprI32ReinterpretF32: {
      MovFloatToInt(dst.gp(), src.fp(), kScratchDoubleReg);
      return true;
    }
    case kExprI64ReinterpretF64: {
      MovDoubleToInt64(dst.gp(), src.fp());
      return true;
    }
    case kExprF32ReinterpretI32: {
      MovIntToFloat(dst.fp(), src.gp(), r0);
      return true;
    }
    case kExprF64ReinterpretI64: {
      MovInt64ToDouble(dst.fp(), src.gp());
      return true;
    }
    default:
      UNREACHABLE();
  }
}

void LiftoffAssembler::emit_jump(Label* label) { b(al, label); }

void LiftoffAssembler::emit_jump(Register target) { Jump(target); }

void LiftoffAssembler::emit_cond_jump(Condition cond, Label* label,
                                      ValueKind kind, Register lhs,
                                      Register rhs,
                                      const FreezeCacheState& frozen) {
  bool use_signed = is_signed(cond);

  if (rhs != no_reg) {
    switch (kind) {
      case kI32:
        if (use_signed) {
          CmpS32(lhs, rhs);
        } else {
          CmpU32(lhs, rhs);
        }
        break;
      case kRef:
      case kRefNull:
      case kRtt:
        DCHECK(cond == kEqual || cond == kNotEqual);
#if defined(V8_COMPRESS_POINTERS)
        if (use_signed) {
          CmpS32(lhs, rhs);
        } else {
          CmpU32(lhs, rhs);
        }
#else
        if (use_signed) {
          CmpS64(lhs, rhs);
        } else {
          CmpU64(lhs, rhs);
        }
#endif
        break;
      case kI64:
        if (use_signed) {
          CmpS64(lhs, rhs);
        } else {
          CmpU64(lhs, rhs);
        }
        break;
      default:
        UNREACHABLE();
    }
  } else {
    DCHECK_EQ(kind, kI32);
    CHECK(use_signed);
    CmpS32(lhs, Operand::Zero(), r0);
  }

  b(to_condition(cond), label);
}

void LiftoffAssembler::emit_i32_cond_jumpi(Condition cond, Label* label,
                                           Register lhs, int32_t imm,
                                           const FreezeCacheState& frozen) {
  bool use_signed = is_signed(cond);
  if (use_signed) {
    CmpS32(lhs, Operand(imm), r0);
  } else {
    CmpU32(lhs, Operand(imm), r0);
  }
  b(to_condition(cond), label);
}

void LiftoffAssembler::emit_ptrsize_cond_jumpi(Condition cond, Label* label,
                                               Register lhs, int32_t imm,
                                               const FreezeCacheState& frozen) {
  bool use_signed = is_signed(cond);
  if (use_signed) {
    CmpS64(lhs, Operand(imm), r0);
  } else {
    CmpU64(lhs, Operand(imm), r0);
  }
  b(to_condition(cond), label);
}

void LiftoffAssembler::emit_i32_eqz(Register dst, Register src) {
  Label done;
  CmpS32(src, Operand(0), r0);
  mov(dst, Operand(1));
  beq(&done);
  mov(dst, Operand::Zero());
  bind(&done);
}

void LiftoffAssembler::emit_i32_set_cond(Condition cond, Register dst,
                                         Register lhs, Register rhs) {
  bool use_signed = is_signed(cond);
  if (use_signed) {
    CmpS32(lhs, rhs);
  } else {
    CmpU32(lhs, rhs);
  }
  Label done;
  mov(dst, Operand(1));
  b(to_condition(to_condition(cond)), &done);
  mov(dst, Operand::Zero());
  bind(&done);
}

void LiftoffAssembler::emit_i64_eqz(Register dst, LiftoffRegister src) {
  Label done;
  cmpi(src.gp(), Operand(0));
  mov(dst, Operand(1));
  beq(&done);
  mov(dst, Operand::Zero());
  bind(&done);
}

void LiftoffAssembler::emit_i64_set_cond(Condition cond, Register dst,
                                         LiftoffRegister lhs,
                                         LiftoffRegister rhs) {
  bool use_signed = is_signed(cond);
  if (use_signed) {
    CmpS64(lhs.gp(), rhs.gp());
  } else {
    CmpU64(lhs.gp(), rhs.gp());
  }
  Label done;
  mov(dst, Operand(1));
  b(to_condition(to_condition(cond)), &done);
  mov(dst, Operand::Zero());
  bind(&done);
}

void LiftoffAssembler::emit_f32_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  fcmpu(lhs, rhs, cr0);
  Label nan, done;
  bunordered(&nan, cr0);
  mov(dst, Operand::Zero());
  b(NegateCondition(to_condition(to_condition(cond))), &done, cr0);
  mov(dst, Operand(1));
  b(&done);
  bind(&nan);
  if (cond == kNotEqual) {
    mov(dst, Operand(1));
  } else {
    mov(dst, Operand::Zero());
  }
  bind(&done);
}

void LiftoffAssembler::emit_f64_set_cond(Condition cond, Register dst,
                                         DoubleRegister lhs,
                                         DoubleRegister rhs) {
  emit_f32_set_cond(to_condition(cond), dst, lhs, rhs);
}

void LiftoffAssembler::emit_i64_muli(LiftoffRegister dst, LiftoffRegister lhs,
                                     int32_t imm) {
  if (base::bits::IsPowerOfTwo(imm)) {
    emit_i64_shli(dst, lhs, base::bits::WhichPowerOfTwo(imm));
    return;
  }
  // TODO(miladfarca): Try to use mulli once simulator supports it.
  mov(r0, Operand(imm));
  MulS64(dst.gp(), lhs.gp(), r0);
}

bool LiftoffAssembler::emit_select(LiftoffRegister dst, Register condition,
                                   LiftoffRegister true_value,
                                   LiftoffRegister false_value,
                                   ValueKind kind) {
  return false;
}

void LiftoffAssembler::clear_i32_upper_half(Register dst) {
  ZeroExtWord32(dst, dst);
}

#define SIMD_BINOP_LIST(V)                           \
  V(f64x2_add, F64x2Add)                             \
  V(f64x2_sub, F64x2Sub)                             \
  V(f64x2_mul, F64x2Mul)                             \
  V(f64x2_div, F64x2Div)                             \
  V(f64x2_eq, F64x2Eq)                               \
  V(f64x2_lt, F64x2Lt)                               \
  V(f64x2_le, F64x2Le)                               \
  V(f32x4_add, F32x4Add)                             \
  V(f32x4_sub, F32x4Sub)                             \
  V(f32x4_mul, F32x4Mul)                             \
  V(f32x4_div, F32x4Div)                             \
  V(f32x4_min, F32x4Min)                             \
  V(f32x4_max, F32x4Max)                             \
  V(f32x4_eq, F32x4Eq)                               \
  V(f32x4_lt, F32x4Lt)                               \
  V(f32x4_le, F32x4Le)                               \
  V(i64x2_add, I64x2Add)                             \
  V(i64x2_sub, I64x2Sub)                             \
  V(i64x2_eq, I64x2Eq)                               \
  V(i64x2_gt_s, I64x2GtS)                            \
  V(i32x4_add, I32x4Add)                             \
  V(i32x4_sub, I32x4Sub)                             \
  V(i32x4_mul, I32x4Mul)                             \
  V(i32x4_min_s, I32x4MinS)                          \
  V(i32x4_min_u, I32x4MinU)                          \
  V(i32x4_max_s, I32x4MaxS)                          \
  V(i32x4_max_u, I32x4MaxU)                          \
  V(i32x4_eq, I32x4Eq)                               \
  V(i32x4_gt_s, I32x4GtS)                            \
  V(i32x4_gt_u, I32x4GtU)                            \
  V(i32x4_dot_i16x8_s, I32x4DotI16x8S)               \
  V(i16x8_add, I16x8Add)                             \
  V(i16x8_sub, I16x8Sub)                             \
  V(i16x8_mul, I16x8Mul)                             \
  V(i16x8_min_s, I16x8MinS)                          \
  V(i16x8_min_u, I16x8MinU)                          \
  V(i16x8_max_s, I16x8MaxS)                          \
  V(i16x8_max_u, I16x8MaxU)                          \
  V(i16x8_eq, I16x8Eq)                               \
  V(i16x8_gt_s, I16x8GtS)                            \
  V(i16x8_gt_u, I16x8GtU)                            \
  V(i16x8_add_sat_s, I16x8AddSatS)                   \
  V(i16x8_sub_sat_s, I16x8SubSatS)                   \
  V(i16x8_add_sat_u, I16x8AddSatU)                   \
  V(i16x8_sub_sat_u, I16x8SubSatU)                   \
  V(i16x8_sconvert_i32x4, I16x8SConvertI32x4)        \
  V(i16x8_uconvert_i32x4, I16x8UConvertI32x4)        \
  V(i16x8_rounding_average_u, I16x8RoundingAverageU) \
  V(i16x8_q15mulr_sat_s, I16x8Q15MulRSatS)           \
  V(i8x16_add, I8x16Add)                             \
  V(i8x16_sub, I8x16Sub)                             \
  V(i8x16_min_s, I8x16MinS)                          \
  V(i8x16_min_u, I8x16MinU)                          \
  V(i8x16_max_s, I8x16MaxS)                          \
  V(i8x16_max_u, I8x16MaxU)                          \
  V(i8x16_eq, I8x16Eq)                               \
  V(i8x16_gt_s, I8x16GtS)                            \
  V(i8x16_gt_u, I8x16GtU)                            \
  V(i8x16_add_sat_s, I8x16AddSatS)                   \
  V(i8x16_sub_sat_s, I8x16SubSatS)                   \
  V(i8x16_add_sat_u, I8x16AddSatU)                   \
  V(i8x16_sub_sat_u, I8x16SubSatU)                   \
  V(i8x16_sconvert_i16x8, I8x16SConvertI16x8)        \
  V(i8x16_uconvert_i16x8, I8x16UConvertI16x8)        \
  V(i8x16_rounding_average_u, I8x16RoundingAverageU) \
  V(s128_and, S128And)                               \
  V(s128_or, S128Or)                                 \
  V(s128_xor, S128Xor)                               \
  V(s128_and_not, S128AndNot)

#define EMIT_SIMD_BINOP(name, op)                                              \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister lhs, \
                                     LiftoffRegister rhs) {                    \
    op(dst.fp().toSimd(), lhs.fp().toSimd(), rhs.fp().toSimd());               \
  }
SIMD_BINOP_LIST(EMIT_SIMD_BINOP)
#undef EMIT_SIMD_BINOP
#undef SIMD_BINOP_LIST

#define SIMD_BINOP_WITH_SCRATCH_LIST(V)               \
  V(f64x2_ne, F64x2Ne)                                \
  V(f64x2_pmin, F64x2Pmin)                            \
  V(f64x2_pmax, F64x2Pmax)                            \
  V(f32x4_ne, F32x4Ne)                                \
  V(f32x4_pmin, F32x4Pmin)                            \
  V(f32x4_pmax, F32x4Pmax)                            \
  V(i64x2_ne, I64x2Ne)                                \
  V(i64x2_ge_s, I64x2GeS)                             \
  V(i64x2_extmul_low_i32x4_s, I64x2ExtMulLowI32x4S)   \
  V(i64x2_extmul_low_i32x4_u, I64x2ExtMulLowI32x4U)   \
  V(i64x2_extmul_high_i32x4_s, I64x2ExtMulHighI32x4S) \
  V(i64x2_extmul_high_i32x4_u, I64x2ExtMulHighI32x4U) \
  V(i32x4_ne, I32x4Ne)                                \
  V(i32x4_ge_s, I32x4GeS)                             \
  V(i32x4_ge_u, I32x4GeU)                             \
  V(i32x4_extmul_low_i16x8_s, I32x4ExtMulLowI16x8S)   \
  V(i32x4_extmul_low_i16x8_u, I32x4ExtMulLowI16x8U)   \
  V(i32x4_extmul_high_i16x8_s, I32x4ExtMulHighI16x8S) \
  V(i32x4_extmul_high_i16x8_u, I32x4ExtMulHighI16x8U) \
  V(i16x8_ne, I16x8Ne)                                \
  V(i16x8_ge_s, I16x8GeS)                             \
  V(i16x8_ge_u, I16x8GeU)                             \
  V(i16x8_extmul_low_i8x16_s, I16x8ExtMulLowI8x16S)   \
  V(i16x8_extmul_low_i8x16_u, I16x8ExtMulLowI8x16U)   \
  V(i16x8_extmul_high_i8x16_s, I16x8ExtMulHighI8x16S) \
  V(i16x8_extmul_high_i8x16_u, I16x8ExtMulHighI8x16U) \
  V(i16x8_dot_i8x16_i7x16_s, I16x8DotI8x16S)          \
  V(i8x16_ne, I8x16Ne)                                \
  V(i8x16_ge_s, I8x16GeS)                             \
  V(i8x16_ge_u, I8x16GeU)                             \
  V(i8x16_swizzle, I8x16Swizzle)

#define EMIT_SIMD_BINOP_WITH_SCRATCH(name, op)                                 \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister lhs, \
                                     LiftoffRegister rhs) {                    \
    op(dst.fp().toSimd(), lhs.fp().toSimd(), rhs.fp().toSimd(),                \
       kScratchSimd128Reg);                                                    \
  }
SIMD_BINOP_WITH_SCRATCH_LIST(EMIT_SIMD_BINOP_WITH_SCRATCH)
#undef EMIT_SIMD_BINOP_WITH_SCRATCH
#undef SIMD_BINOP_WITH_SCRATCH_LIST

#define SIMD_SHIFT_RR_LIST(V) \
  V(i64x2_shl, I64x2Shl)      \
  V(i64x2_shr_s, I64x2ShrS)   \
  V(i64x2_shr_u, I64x2ShrU)   \
  V(i32x4_shl, I32x4Shl)      \
  V(i32x4_shr_s, I32x4ShrS)   \
  V(i32x4_shr_u, I32x4ShrU)   \
  V(i16x8_shl, I16x8Shl)      \
  V(i16x8_shr_s, I16x8ShrS)   \
  V(i16x8_shr_u, I16x8ShrU)   \
  V(i8x16_shl, I8x16Shl)      \
  V(i8x16_shr_s, I8x16ShrS)   \
  V(i8x16_shr_u, I8x16ShrU)

#define EMIT_SIMD_SHIFT_RR(name, op)                                           \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister lhs, \
                                     LiftoffRegister rhs) {                    \
    op(dst.fp().toSimd(), lhs.fp().toSimd(), rhs.gp(), kScratchSimd128Reg);    \
  }
SIMD_SHIFT_RR_LIST(EMIT_SIMD_SHIFT_RR)
#undef EMIT_SIMD_SHIFT_RR
#undef SIMD_SHIFT_RR_LIST

#define SIMD_SHIFT_RI_LIST(V) \
  V(i64x2_shli, I64x2Shl)     \
  V(i64x2_shri_s, I64x2ShrS)  \
  V(i64x2_shri_u, I64x2ShrU)  \
  V(i32x4_shli, I32x4Shl)     \
  V(i32x4_shri_s, I32x4ShrS)  \
  V(i32x4_shri_u, I32x4ShrU)  \
  V(i16x8_shli, I16x8Shl)     \
  V(i16x8_shri_s, I16x8ShrS)  \
  V(i16x8_shri_u, I16x8ShrU)  \
  V(i8x16_shli, I8x16Shl)     \
  V(i8x16_shri_s, I8x16ShrS)  \
  V(i8x16_shri_u, I8x16ShrU)

#define EMIT_SIMD_SHIFT_RI(name, op)                                           \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister lhs, \
                                     int32_t rhs) {                            \
    op(dst.fp().toSimd(), lhs.fp().toSimd(), Operand(rhs), r0,                 \
       kScratchSimd128Reg);                                                    \
  }
SIMD_SHIFT_RI_LIST(EMIT_SIMD_SHIFT_RI)
#undef EMIT_SIMD_SHIFT_RI
#undef SIMD_SHIFT_RI_LIST

#define SIMD_UNOP_LIST(V)                                      \
  V(f64x2_abs, F64x2Abs, , void)                               \
  V(f64x2_neg, F64x2Neg, , void)                               \
  V(f64x2_sqrt, F64x2Sqrt, , void)                             \
  V(f64x2_ceil, F64x2Ceil, true, bool)                         \
  V(f64x2_floor, F64x2Floor, true, bool)                       \
  V(f64x2_trunc, F64x2Trunc, true, bool)                       \
  V(f64x2_promote_low_f32x4, F64x2PromoteLowF32x4, , void)     \
  V(f32x4_abs, F32x4Abs, , void)                               \
  V(f32x4_neg, F32x4Neg, , void)                               \
  V(f32x4_sqrt, F32x4Sqrt, , void)                             \
  V(f32x4_ceil, F32x4Ceil, true, bool)                         \
  V(f32x4_floor, F32x4Floor, true, bool)                       \
  V(f32x4_trunc, F32x4Trunc, true, bool)                       \
  V(f32x4_sconvert_i32x4, F32x4SConvertI32x4, , void)          \
  V(f32x4_uconvert_i32x4, F32x4UConvertI32x4, , void)          \
  V(i64x2_neg, I64x2Neg, , void)                               \
  V(f64x2_convert_low_i32x4_s, F64x2ConvertLowI32x4S, , void)  \
  V(i64x2_sconvert_i32x4_low, I64x2SConvertI32x4Low, , void)   \
  V(i64x2_sconvert_i32x4_high, I64x2SConvertI32x4High, , void) \
  V(i32x4_neg, I32x4Neg, , void)                               \
  V(i32x4_sconvert_i16x8_low, I32x4SConvertI16x8Low, , void)   \
  V(i32x4_sconvert_i16x8_high, I32x4SConvertI16x8High, , void) \
  V(i32x4_uconvert_f32x4, I32x4UConvertF32x4, , void)          \
  V(i16x8_sconvert_i8x16_low, I16x8SConvertI8x16Low, , void)   \
  V(i16x8_sconvert_i8x16_high, I16x8SConvertI8x16High, , void) \
  V(i8x16_popcnt, I8x16Popcnt, , void)                         \
  V(s128_not, S128Not, , void)

#define EMIT_SIMD_UNOP(name, op, return_val, return_type)          \
  return_type LiftoffAssembler::emit_##name(LiftoffRegister dst,   \
                                            LiftoffRegister src) { \
    op(dst.fp().toSimd(), src.fp().toSimd());                      \
    return return_val;                                             \
  }
SIMD_UNOP_LIST(EMIT_SIMD_UNOP)
#undef EMIT_SIMD_UNOP
#undef SIMD_UNOP_LIST

#define SIMD_UNOP_WITH_SCRATCH_LIST(V)                             \
  V(f32x4_demote_f64x2_zero, F32x4DemoteF64x2Zero, , void)         \
  V(i64x2_abs, I64x2Abs, , void)                                   \
  V(i32x4_abs, I32x4Abs, , void)                                   \
  V(i32x4_sconvert_f32x4, I32x4SConvertF32x4, , void)              \
  V(i32x4_trunc_sat_f64x2_s_zero, I32x4TruncSatF64x2SZero, , void) \
  V(i32x4_trunc_sat_f64x2_u_zero, I32x4TruncSatF64x2UZero, , void) \
  V(i16x8_abs, I16x8Abs, , void)                                   \
  V(i16x8_neg, I16x8Neg, , void)                                   \
  V(i8x16_abs, I8x16Abs, , void)                                   \
  V(i8x16_neg, I8x16Neg, , void)

#define EMIT_SIMD_UNOP_WITH_SCRATCH(name, op, return_val, return_type) \
  return_type LiftoffAssembler::emit_##name(LiftoffRegister dst,       \
                                            LiftoffRegister src) {     \
    op(dst.fp().toSimd(), src.fp().toSimd(), kScratchSimd128Reg);      \
    return return_val;                                                 \
  }
SIMD_UNOP_WITH_SCRATCH_LIST(EMIT_SIMD_UNOP_WITH_SCRATCH)
#undef EMIT_SIMD_UNOP_WITH_SCRATCH
#undef SIMD_UNOP_WITH_SCRATCH_LIST

#define SIMD_ALL_TRUE_LIST(V)    \
  V(i64x2_alltrue, I64x2AllTrue) \
  V(i32x4_alltrue, I32x4AllTrue) \
  V(i16x8_alltrue, I16x8AllTrue) \
  V(i8x16_alltrue, I8x16AllTrue)
#define EMIT_SIMD_ALL_TRUE(name, op)                             \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst,        \
                                     LiftoffRegister src) {      \
    op(dst.gp(), src.fp().toSimd(), r0, ip, kScratchSimd128Reg); \
  }
SIMD_ALL_TRUE_LIST(EMIT_SIMD_ALL_TRUE)
#undef EMIT_SIMD_ALL_TRUE
#undef SIMD_ALL_TRUE_LIST

#define SIMD_QFM_LIST(V)   \
  V(f64x2_qfma, F64x2Qfma) \
  V(f64x2_qfms, F64x2Qfms) \
  V(f32x4_qfma, F32x4Qfma) \
  V(f32x4_qfms, F32x4Qfms)

#define EMIT_SIMD_QFM(name, op)                                        \
  void LiftoffAssembler::emit_##name(                                  \
      LiftoffRegister dst, LiftoffRegister src1, LiftoffRegister src2, \
      LiftoffRegister src3) {                                          \
    op(dst.fp().toSimd(), src1.fp().toSimd(), src2.fp().toSimd(),      \
       src3.fp().toSimd(), kScratchSimd128Reg);                        \
  }
SIMD_QFM_LIST(EMIT_SIMD_QFM)
#undef EMIT_SIMD_QFM
#undef SIMD_QFM_LIST

#define SIMD_EXT_ADD_PAIRWISE_LIST(V)                         \
  V(i32x4_extadd_pairwise_i16x8_s, I32x4ExtAddPairwiseI16x8S) \
  V(i32x4_extadd_pairwise_i16x8_u, I32x4ExtAddPairwiseI16x8U) \
  V(i16x8_extadd_pairwise_i8x16_s, I16x8ExtAddPairwiseI8x16S) \
  V(i16x8_extadd_pairwise_i8x16_u, I16x8ExtAddPairwiseI8x16U)
#define EMIT_SIMD_EXT_ADD_PAIRWISE(name, op)                     \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst,        \
                                     LiftoffRegister src) {      \
    op(dst.fp().toSimd(), src.fp().toSimd(), kScratchSimd128Reg, \
       kScratchSimd128Reg2);                                     \
  }
SIMD_EXT_ADD_PAIRWISE_LIST(EMIT_SIMD_EXT_ADD_PAIRWISE)
#undef EMIT_SIMD_EXT_ADD_PAIRWISE
#undef SIMD_EXT_ADD_PAIRWISE_LIST

#define SIMD_RELAXED_BINOP_LIST(V)        \
  V(i8x16_relaxed_swizzle, i8x16_swizzle) \
  V(f64x2_relaxed_min, f64x2_pmin)        \
  V(f64x2_relaxed_max, f64x2_pmax)        \
  V(f32x4_relaxed_min, f32x4_pmin)        \
  V(f32x4_relaxed_max, f32x4_pmax)        \
  V(i16x8_relaxed_q15mulr_s, i16x8_q15mulr_sat_s)

#define SIMD_VISIT_RELAXED_BINOP(name, op)                                     \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister lhs, \
                                     LiftoffRegister rhs) {                    \
    emit_##op(dst, lhs, rhs);                                                  \
  }
SIMD_RELAXED_BINOP_LIST(SIMD_VISIT_RELAXED_BINOP)
#undef SIMD_VISIT_RELAXED_BINOP
#undef SIMD_RELAXED_BINOP_LIST

#define SIMD_RELAXED_UNOP_LIST(V)                                   \
  V(i32x4_relaxed_trunc_f32x4_s, i32x4_sconvert_f32x4)              \
  V(i32x4_relaxed_trunc_f32x4_u, i32x4_uconvert_f32x4)              \
  V(i32x4_relaxed_trunc_f64x2_s_zero, i32x4_trunc_sat_f64x2_s_zero) \
  V(i32x4_relaxed_trunc_f64x2_u_zero, i32x4_trunc_sat_f64x2_u_zero)

#define SIMD_VISIT_RELAXED_UNOP(name, op)                   \
  void LiftoffAssembler::emit_##name(LiftoffRegister dst,   \
                                     LiftoffRegister src) { \
    emit_##op(dst, src);                                    \
  }
SIMD_RELAXED_UNOP_LIST(SIMD_VISIT_RELAXED_UNOP)
#undef SIMD_VISIT_RELAXED_UNOP
#undef SIMD_RELAXED_UNOP_LIST

#define F16_UNOP_LIST(V)     \
  V(f16x8_splat)             \
  V(f16x8_abs)               \
  V(f16x8_neg)               \
  V(f16x8_sqrt)              \
  V(f16x8_ceil)              \
  V(f16x8_floor)             \
  V(f16x8_trunc)             \
  V(f16x8_nearest_int)       \
  V(i16x8_sconvert_f16x8)    \
  V(i16x8_uconvert_f16x8)    \
  V(f16x8_sconvert_i16x8)    \
  V(f16x8_uconvert_i16x8)    \
  V(f16x8_demote_f32x4_zero) \
  V(f32x4_promote_low_f16x8) \
  V(f16x8_demote_f64x2_zero)

#define VISIT_F16_UNOP(name)                                \
  bool LiftoffAssembler::emit_##name(LiftoffRegister dst,   \
                                     LiftoffRegister src) { \
    return false;                                           \
  }
F16_UNOP_LIST(VISIT_F16_UNOP)
#undef VISIT_F16_UNOP
#undef F16_UNOP_LIST

#define F16_BINOP_LIST(V) \
  V(f16x8_eq)             \
  V(f16x8_ne)             \
  V(f16x8_lt)             \
  V(f16x8_le)             \
  V(f16x8_add)            \
  V(f16x8_sub)            \
  V(f16x8_mul)            \
  V(f16x8_div)            \
  V(f16x8_min)            \
  V(f16x8_max)            \
  V(f16x8_pmin)           \
  V(f16x8_pmax)

#define VISIT_F16_BINOP(name)                                                  \
  bool LiftoffAssembler::emit_##name(LiftoffRegister dst, LiftoffRegister lhs, \
                                     LiftoffRegister rhs) {                    \
    return false;                                                              \
  }
F16_BINOP_LIST(VISIT_F16_BINOP)
#undef VISIT_F16_BINOP
#undef F16_BINOP_LIST

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

void LiftoffAssembler::emit_f64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  F64x2Splat(dst.fp().toSimd(), src.fp(), r0);
}

void LiftoffAssembler::emit_f32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  F32x4Splat(dst.fp().toSimd(), src.fp(), kScratchDoubleReg, r0);
}

void LiftoffAssembler::emit_i64x2_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  I64x2Splat(dst.fp().toSimd(), src.gp());
}

void LiftoffAssembler::emit_i32x4_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  I32x4Splat(dst.fp().toSimd(), src.gp());
}

void LiftoffAssembler::emit_i16x8_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  I16x8Splat(dst.fp().toSimd(), src.gp());
}

void LiftoffAssembler::emit_i8x16_splat(LiftoffRegister dst,
                                        LiftoffRegister src) {
  I8x16Splat(dst.fp().toSimd(), src.gp());
}

void LiftoffAssembler::emit_f64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  F64x2ExtractLane(dst.fp(), lhs.fp().toSimd(), imm_lane_idx,
                   kScratchSimd128Reg, r0);
}

void LiftoffAssembler::emit_f32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  F32x4ExtractLane(dst.fp(), lhs.fp().toSimd(), imm_lane_idx,
                   kScratchSimd128Reg, r0, ip);
}

void LiftoffAssembler::emit_i64x2_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  I64x2ExtractLane(dst.gp(), lhs.fp().toSimd(), imm_lane_idx,
                   kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i32x4_extract_lane(LiftoffRegister dst,
                                               LiftoffRegister lhs,
                                               uint8_t imm_lane_idx) {
  I32x4ExtractLane(dst.gp(), lhs.fp().toSimd(), imm_lane_idx,
                   kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i16x8_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  I16x8ExtractLaneU(dst.gp(), lhs.fp().toSimd(), imm_lane_idx,
                    kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i16x8_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  I16x8ExtractLaneS(dst.gp(), lhs.fp().toSimd(), imm_lane_idx,
                    kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i8x16_extract_lane_u(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  I8x16ExtractLaneU(dst.gp(), lhs.fp().toSimd(), imm_lane_idx,
                    kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i8x16_extract_lane_s(LiftoffRegister dst,
                                                 LiftoffRegister lhs,
                                                 uint8_t imm_lane_idx) {
  I8x16ExtractLaneS(dst.gp(), lhs.fp().toSimd(), imm_lane_idx,
                    kScratchSimd128Reg);
}

void LiftoffAssembler::emit_f64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  F64x2ReplaceLane(dst.fp().toSimd(), src1.fp().toSimd(), src2.fp(),
                   imm_lane_idx, r0, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_f32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  F32x4ReplaceLane(dst.fp().toSimd(), src1.fp().toSimd(), src2.fp(),
                   imm_lane_idx, r0, kScratchDoubleReg, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i64x2_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  I64x2ReplaceLane(dst.fp().toSimd(), src1.fp().toSimd(), src2.gp(),
                   imm_lane_idx, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i32x4_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  I32x4ReplaceLane(dst.fp().toSimd(), src1.fp().toSimd(), src2.gp(),
                   imm_lane_idx, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i16x8_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  I16x8ReplaceLane(dst.fp().toSimd(), src1.fp().toSimd(), src2.gp(),
                   imm_lane_idx, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i8x16_replace_lane(LiftoffRegister dst,
                                               LiftoffRegister src1,
                                               LiftoffRegister src2,
                                               uint8_t imm_lane_idx) {
  I8x16ReplaceLane(dst.fp().toSimd(), src1.fp().toSimd(), src2.gp(),
                   imm_lane_idx, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i64x2_mul(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  // TODO(miladfarca): Make use of UseScratchRegisterScope.
  Register scratch = GetRegisterThatIsNotOneOf(ip, r0);
  push(scratch);
  I64x2Mul(dst.fp().toSimd(), lhs.fp().toSimd(), rhs.fp().toSimd(), ip, r0,
           scratch, kScratchSimd128Reg);
  pop(scratch);
}

void LiftoffAssembler::emit_f64x2_min(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  F64x2Min(dst.fp().toSimd(), lhs.fp().toSimd(), rhs.fp().toSimd(),
           kScratchSimd128Reg, kScratchSimd128Reg2);
}

void LiftoffAssembler::emit_f64x2_max(LiftoffRegister dst, LiftoffRegister lhs,
                                      LiftoffRegister rhs) {
  F64x2Max(dst.fp().toSimd(), lhs.fp().toSimd(), rhs.fp().toSimd(),
           kScratchSimd128Reg, kScratchSimd128Reg2);
}

bool LiftoffAssembler::emit_f64x2_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  return false;
}

bool LiftoffAssembler::emit_f32x4_nearest_int(LiftoffRegister dst,
                                              LiftoffRegister src) {
  return false;
}

void LiftoffAssembler::LoadTransform(LiftoffRegister dst, Register src_addr,
                                     Register offset_reg, uintptr_t offset_imm,
                                     LoadType type,
                                     LoadTransformationKind transform,
                                     uint32_t* protected_load_pc) {
  MemOperand src_op = MemOperand(src_addr, offset_reg, offset_imm);
  *protected_load_pc = pc_offset();
  MachineType memtype = type.mem_type();
  if (transform == LoadTransformationKind::kExtend) {
    if (memtype == MachineType::Int8()) {
      LoadAndExtend8x8SLE(dst.fp().toSimd(), src_op, r0);
    } else if (memtype == MachineType::Uint8()) {
      LoadAndExtend8x8ULE(dst.fp().toSimd(), src_op, r0, kScratchSimd128Reg);
    } else if (memtype == MachineType::Int16()) {
      LoadAndExtend16x4SLE(dst.fp().toSimd(), src_op, r0);
    } else if (memtype == MachineType::Uint16()) {
      LoadAndExtend16x4ULE(dst.fp().toSimd(), src_op, r0, kScratchSimd128Reg);
    } else if (memtype == MachineType::Int32()) {
      LoadAndExtend32x2SLE(dst.fp().toSimd(), src_op, r0);
    } else if (memtype == MachineType::Uint32()) {
      LoadAndExtend32x2ULE(dst.fp().toSimd(), src_op, r0, kScratchSimd128Reg);
    }
  } else if (transform == LoadTransformationKind::kZeroExtend) {
    if (memtype == MachineType::Int32()) {
      LoadV32ZeroLE(dst.fp().toSimd(), src_op, r0, kScratchSimd128Reg);
    } else {
      DCHECK_EQ(MachineType::Int64(), memtype);
      LoadV64ZeroLE(dst.fp().toSimd(), src_op, r0, kScratchSimd128Reg);
    }
  } else {
    DCHECK_EQ(LoadTransformationKind::kSplat, transform);
    if (memtype == MachineType::Int8()) {
      LoadAndSplat8x16LE(dst.fp().toSimd(), src_op, r0);
    } else if (memtype == MachineType::Int16()) {
      LoadAndSplat16x8LE(dst.fp().toSimd(), src_op, r0);
    } else if (memtype == MachineType::Int32()) {
      LoadAndSplat32x4LE(dst.fp().toSimd(), src_op, r0);
    } else if (memtype == MachineType::Int64()) {
      LoadAndSplat64x2LE(dst.fp().toSimd(), src_op, r0);
    }
  }
}

void LiftoffAssembler::emit_smi_check(Register obj, Label* target,
                                      SmiCheckMode mode,
                                      const FreezeCacheState& frozen) {
  TestIfSmi(obj, r0);
  Condition condition = mode == kJumpOnSmi ? eq : ne;
  b(condition, target, cr0);  // branch if SMI
}

void LiftoffAssembler::LoadLane(LiftoffRegister dst, LiftoffRegister src,
                                Register addr, Register offset_reg,
                                uintptr_t offset_imm, LoadType type,
                                uint8_t laneidx, uint32_t* protected_load_pc,
                                bool i64_offset) {
  if (!i64_offset && offset_reg != no_reg) {
    ZeroExtWord32(ip, offset_reg);
    offset_reg = ip;
  }
  MemOperand src_op = MemOperand(addr, offset_reg, offset_imm);

  MachineType mem_type = type.mem_type();
  if (dst != src) {
    vor(dst.fp().toSimd(), src.fp().toSimd(), src.fp().toSimd());
  }

  if (protected_load_pc) *protected_load_pc = pc_offset();
  if (mem_type == MachineType::Int8()) {
    LoadLane8LE(dst.fp().toSimd(), src_op, laneidx, r0, kScratchSimd128Reg);
  } else if (mem_type == MachineType::Int16()) {
    LoadLane16LE(dst.fp().toSimd(), src_op, laneidx, r0, kScratchSimd128Reg);
  } else if (mem_type == MachineType::Int32()) {
    LoadLane32LE(dst.fp().toSimd(), src_op, laneidx, r0, kScratchSimd128Reg);
  } else {
    DCHECK_EQ(MachineType::Int64(), mem_type);
    LoadLane64LE(dst.fp().toSimd(), src_op, laneidx, r0, kScratchSimd128Reg);
  }
}

void LiftoffAssembler::StoreLane(Register dst, Register offset,
                                 uintptr_t offset_imm, LiftoffRegister src,
                                 StoreType type, uint8_t lane,
                                 uint32_t* protected_store_pc,
                                 bool i64_offset) {
  if (!i64_offset && offset != no_reg) {
    ZeroExtWord32(ip, offset);
    offset = ip;
  }
  MemOperand dst_op = MemOperand(dst, offset, offset_imm);

  if (protected_store_pc) *protected_store_pc = pc_offset();

  MachineRepresentation rep = type.mem_rep();
  if (rep == MachineRepresentation::kWord8) {
    StoreLane8LE(src.fp().toSimd(), dst_op, lane, r0, kScratchSimd128Reg);
  } else if (rep == MachineRepresentation::kWord16) {
    StoreLane16LE(src.fp().toSimd(), dst_op, lane, r0, kScratchSimd128Reg);
  } else if (rep == MachineRepresentation::kWord32) {
    StoreLane32LE(src.fp().toSimd(), dst_op, lane, r0, kScratchSimd128Reg);
  } else {
    DCHECK_EQ(MachineRepresentation::kWord64, rep);
    StoreLane64LE(src.fp().toSimd(), dst_op, lane, r0, kScratchSimd128Reg);
  }
}

void LiftoffAssembler::emit_s128_relaxed_laneselect(LiftoffRegister dst,
                                                    LiftoffRegister src1,
                                                    LiftoffRegister src2,
                                                    LiftoffRegister mask,
                                                    int lane_width) {
  // PPC uses bytewise selection for all lane widths.
  emit_s128_select(dst, src1, src2, mask);
}

void LiftoffAssembler::emit_f64x2_convert_low_i32x4_u(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  F64x2ConvertLowI32x4U(dst.fp().toSimd(), src.fp().toSimd(), r0,
                        kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i64x2_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  I64x2BitMask(dst.gp(), src.fp().toSimd(), r0, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  I64x2UConvertI32x4Low(dst.fp().toSimd(), src.fp().toSimd(), r0,
                        kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i64x2_uconvert_i32x4_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I64x2UConvertI32x4High(dst.fp().toSimd(), src.fp().toSimd(), r0,
                         kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i32x4_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  I32x4BitMask(dst.gp(), src.fp().toSimd(), r0, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i16x8_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  I16x8BitMask(dst.gp(), src.fp().toSimd(), r0, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i32x4_dot_i8x16_i7x16_add_s(LiftoffRegister dst,
                                                        LiftoffRegister lhs,
                                                        LiftoffRegister rhs,
                                                        LiftoffRegister acc) {
  I32x4DotI8x16AddS(dst.fp().toSimd(), lhs.fp().toSimd(), rhs.fp().toSimd(),
                    acc.fp().toSimd());
}

void LiftoffAssembler::emit_i8x16_shuffle(LiftoffRegister dst,
                                          LiftoffRegister lhs,
                                          LiftoffRegister rhs,
                                          const uint8_t shuffle[16],
                                          bool is_swizzle) {
  // Remap the shuffle indices to match IBM lane numbering.
  // TODO(miladfarca): Put this in a function and share it with the instruction
  // selector.
  int max_index = 15;
  int total_lane_count = 2 * kSimd128Size;
  uint8_t shuffle_remapped[kSimd128Size];
  for (int i = 0; i < kSimd128Size; i++) {
    uint8_t current_index = shuffle[i];
    shuffle_remapped[i] = (current_index <= max_index
                               ? max_index - current_index
                               : total_lane_count - current_index + max_index);
  }
  uint64_t vals[2];
  memcpy(vals, shuffle_remapped, sizeof(shuffle_remapped));
#ifdef V8_TARGET_BIG_ENDIAN
  vals[0] = ByteReverse(vals[0]);
  vals[1] = ByteReverse(vals[1]);
#endif
  I8x16Shuffle(dst.fp().toSimd(), lhs.fp().toSimd(), rhs.fp().toSimd(), vals[1],
               vals[0], r0, ip, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_v128_anytrue(LiftoffRegister dst,
                                         LiftoffRegister src) {
  V128AnyTrue(dst.gp(), src.fp().toSimd(), r0, ip, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i8x16_bitmask(LiftoffRegister dst,
                                          LiftoffRegister src) {
  I8x16BitMask(dst.gp(), src.fp().toSimd(), r0, ip, kScratchSimd128Reg);
}

void LiftoffAssembler::emit_s128_const(LiftoffRegister dst,
                                       const uint8_t imms[16]) {
  uint64_t vals[2];
  memcpy(vals, imms, sizeof(vals));
#ifdef V8_TARGET_BIG_ENDIAN
  vals[0] = ByteReverse(vals[0]);
  vals[1] = ByteReverse(vals[1]);
#endif
  S128Const(dst.fp().toSimd(), vals[1], vals[0], r0, ip);
}

void LiftoffAssembler::emit_s128_select(LiftoffRegister dst,
                                        LiftoffRegister src1,
                                        LiftoffRegister src2,
                                        LiftoffRegister mask) {
  S128Select(dst.fp().toSimd(), src1.fp().toSimd(), src2.fp().toSimd(),
             mask.fp().toSimd());
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  I16x8UConvertI8x16Low(dst.fp().toSimd(), src.fp().toSimd(), r0,
                        kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i16x8_uconvert_i8x16_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I16x8UConvertI8x16High(dst.fp().toSimd(), src.fp().toSimd(), r0,
                         kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_low(LiftoffRegister dst,
                                                     LiftoffRegister src) {
  I32x4UConvertI16x8Low(dst.fp().toSimd(), src.fp().toSimd(), r0,
                        kScratchSimd128Reg);
}

void LiftoffAssembler::emit_i32x4_uconvert_i16x8_high(LiftoffRegister dst,
                                                      LiftoffRegister src) {
  I32x4UConvertI16x8High(dst.fp().toSimd(), src.fp().toSimd(), r0,
                         kScratchSimd128Reg);
}

void LiftoffAssembler::set_trap_on_oob_mem64(Register index, uint64_t oob_size,
                                             uint64_t oob_index) {
  UNREACHABLE();
}

void LiftoffAssembler::StackCheck(Label* ool_code) {
  Register limit_address = ip;
  LoadStackLimit(limit_address, StackLimitKind::kInterruptStackLimit, r0);
  CmpU64(sp, limit_address);
  ble(ool_code);
}

void LiftoffAssembler::AssertUnreachable(AbortReason reason) {
  if (v8_flags.debug_code) Abort(reason);
}

void LiftoffAssembler::PushRegisters(LiftoffRegList regs) {
  MultiPush(regs.GetGpList());
  DoubleRegList fp_regs = regs.GetFpList();
  MultiPushF64AndV128(fp_regs, Simd128RegList::FromBits(fp_regs.bits()), ip,
                      r0);
}

void LiftoffAssembler::PopRegisters(LiftoffRegList regs) {
  DoubleRegList fp_regs = regs.GetFpList();
  MultiPopF64AndV128(fp_regs, Simd128RegList::FromBits(fp_regs.bits()), ip, r0);
  MultiPop(regs.GetGpList());
}

void LiftoffAssembler::RecordSpillsInSafepoint(
    SafepointTableBuilder::Safepoint& safepoint, LiftoffRegList all_spills,
    LiftoffRegList ref_spills, int spill_offset) {
  LiftoffRegList fp_spills = all_spills & kFpCacheRegList;
  int spill_space_size = fp_spills.GetNumRegsSet() * kSimd128Size;
  LiftoffRegList gp_spills = all_spills & kGpCacheRegList;
  while (!gp_spills.is_empty()) {
    LiftoffRegister reg = gp_spills.GetLastRegSet();
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
  Drop(num_stack_slots);
  Ret();
}

void LiftoffAssembler::CallCWithStackBuffer(
    const std::initializer_list<VarState> args, const LiftoffRegister* rets,
    ValueKind return_kind, ValueKind out_argument_kind, int stack_bytes,
    ExternalReference ext_ref) {
  int total_size = RoundUp(stack_bytes, kSystemPointerSize);

  int size = total_size;
  constexpr int kStackPageSize = 4 * KB;

  // Reserve space in the stack.
  while (size > kStackPageSize) {
    SubS64(sp, sp, Operand(kStackPageSize), r0);
    StoreU64(r0, MemOperand(sp));
    size -= kStackPageSize;
  }

  SubS64(sp, sp, Operand(size), r0);

  int arg_offset = 0;
  for (const VarState& arg : args) {
    MemOperand dst{sp, arg_offset};
    liftoff::StoreToMemory(this, dst, arg, r0, ip);
    arg_offset += value_kind_size(arg.kind());
  }
  DCHECK_LE(arg_offset, stack_bytes);

  // Pass a pointer to the buffer with the arguments to the C function.
  mr(r3, sp);

  // Now call the C function.
  constexpr int kNumCCallArgs = 1;
  PrepareCallCFunction(kNumCCallArgs, r0);
  CallCFunction(ext_ref, kNumCCallArgs);

  // Move return value to the right register.
  const LiftoffRegister* result_reg = rets;
  if (return_kind != kVoid) {
    constexpr Register kReturnReg = r3;
    if (kReturnReg != rets->gp()) {
      Move(*rets, LiftoffRegister(kReturnReg), return_kind);
    }
    result_reg++;
  }

  // Load potential output value from the buffer on the stack.
  if (out_argument_kind != kVoid) {
    switch (out_argument_kind) {
      case kI16:
        LoadS16(result_reg->gp(), MemOperand(sp));
        break;
      case kI32:
        LoadS32(result_reg->gp(), MemOperand(sp));
        break;
      case kI64:
      case kRefNull:
      case kRef:
      case kRtt:
        LoadU64(result_reg->gp(), MemOperand(sp));
        break;
      case kF32:
        LoadF32(result_reg->fp(), MemOperand(sp));
        break;
      case kF64:
        LoadF64(result_reg->fp(), MemOperand(sp));
        break;
      case kS128:
        LoadSimd128(result_reg->fp().toSimd(), MemOperand(sp), r0);
        break;
      default:
        UNREACHABLE();
    }
  }
  AddS64(sp, sp, Operand(total_size), r0);
}

void LiftoffAssembler::CallC(const std::initializer_list<VarState> args,
                             ExternalReference ext_ref) {
  // First, prepare the stack for the C call.
  int num_args = static_cast<int>(args.size());
  PrepareCallCFunction(num_args, r0);

  // Then execute the parallel register move and also move values to parameter
  // stack slots.
  int reg_args = 0;
  int stack_args = 0;
  ParallelMove parallel_move{this};
  for (const VarState& arg : args) {
    if (reg_args < int{arraysize(kCArgRegs)}) {
      parallel_move.LoadIntoRegister(LiftoffRegister{kCArgRegs[reg_args]}, arg);
      ++reg_args;
    } else {
      int bias = 0;
      // On BE machines values with less than 8 bytes are right justified.
      // bias here is relative to the stack pointer.
      if (arg.kind() == kI32 || arg.kind() == kF32) bias = -stack_bias;
      int offset =
          (kStackFrameExtraParamSlot + stack_args) * kSystemPointerSize;
      MemOperand dst{sp, offset + bias};
      liftoff::StoreToMemory(this, dst, arg, r0, ip);
      ++stack_args;
    }
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
  DCHECK(target != no_reg);
  Call(target);
}

void LiftoffAssembler::TailCallIndirect(Register target) {
  DCHECK(target != no_reg);
  Jump(target);
}

void LiftoffAssembler::CallBuiltin(Builtin builtin) {
  // A direct call to a builtin. Just encode the builtin index. This will be
  // patched at relocation.
  Call(static_cast<Address>(builtin), RelocInfo::WASM_STUB_CALL);
}

void LiftoffAssembler::AllocateStackSlot(Register addr, uint32_t size) {
  SubS64(sp, sp, Operand(size), r0);
  mr(addr, sp);
}

void LiftoffAssembler::DeallocateStackSlot(uint32_t size) {
  AddS64(sp, sp, Operand(size));
}

void LiftoffAssembler::MaybeOSR() {}

void LiftoffAssembler::emit_set_if_nan(Register dst, DoubleRegister src,
                                       ValueKind kind) {
  Label return_nan, done;
  fcmpu(src, src);
  bunordered(&return_nan);
  b(&done);
  bind(&return_nan);
  StoreF32(src, MemOperand(dst), r0);
  bind(&done);
}

void LiftoffAssembler::emit_s128_set_if_nan(Register dst, LiftoffRegister src,
                                            Register tmp_gp,
                                            LiftoffRegister tmp_s128,
                                            ValueKind lane_kind) {
  Label done;
  if (lane_kind == kF32) {
    xvcmpeqsp(tmp_s128.fp().toSimd(), src.fp().toSimd(), src.fp().toSimd(),
              SetRC);
  } else {
    DCHECK_EQ(lane_kind, kF64);
    xvcmpeqdp(tmp_s128.fp().toSimd(), src.fp().toSimd(), src.fp().toSimd(),
              SetRC);
  }
  // CR_LT which is targeting cr6 bit 0, indicating if all lanes true (no lanes
  // are NaN).
  Condition all_lanes_true = lt;
  b(all_lanes_true, &done, cr6);
  // Do not use the src register as a Fp register to store a value.
  // We use two different sets for Fp and Simd registers on PPC.
  li(tmp_gp, Operand(1));
  StoreU32(tmp_gp, MemOperand(dst), r0);
  bind(&done);
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
      case LiftoffAssembler::VarState::kStack: {
        switch (src.kind()) {
          case kI32:
          case kRef:
          case kRefNull:
          case kRtt:
          case kI64: {
            asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
            UseScratchRegisterScope temps(asm_);
            Register scratch = temps.Acquire();
            asm_->LoadU64(scratch, liftoff::GetStackSlot(slot.src_offset_), r0);
            asm_->Push(scratch);
            break;
          }
          case kF32: {
            asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
            asm_->LoadF32(kScratchDoubleReg,
                          liftoff::GetStackSlot(slot.src_offset_ + stack_bias),
                          r0);
            asm_->AddS64(sp, sp, Operand(-kSystemPointerSize));
            asm_->StoreF32(kScratchDoubleReg, MemOperand(sp), r0);
            break;
          }
          case kF64: {
            asm_->AllocateStackSpace(stack_decrement - kDoubleSize);
            asm_->LoadF64(kScratchDoubleReg,
                          liftoff::GetStackSlot(slot.src_offset_), r0);
            asm_->AddS64(sp, sp, Operand(-kSystemPointerSize), r0);
            asm_->StoreF64(kScratchDoubleReg, MemOperand(sp), r0);
            break;
          }
          case kS128: {
            asm_->AllocateStackSpace(stack_decrement - kSimd128Size);
            asm_->LoadSimd128(kScratchSimd128Reg,
                              liftoff::GetStackSlot(slot.src_offset_), r0);
            asm_->AddS64(sp, sp, Operand(-kSimd128Size));
            asm_->StoreSimd128(kScratchSimd128Reg, MemOperand(sp), r0);
            break;
          }
          default:
            UNREACHABLE();
        }
        break;
      }
      case LiftoffAssembler::VarState::kRegister: {
        int pushed_bytes = SlotSizeInBytes(slot);
        asm_->AllocateStackSpace(stack_decrement - pushed_bytes);
        switch (src.kind()) {
          case kI64:
          case kI32:
          case kRef:
          case kRefNull:
          case kRtt:
            asm_->push(src.reg().gp());
            break;
          case kF32:
            asm_->AddS64(sp, sp, Operand(-kSystemPointerSize), r0);
            asm_->StoreF32(src.reg().fp(), MemOperand(sp), r0);
            break;
          case kF64:
            asm_->AddS64(sp, sp, Operand(-kSystemPointerSize), r0);
            asm_->StoreF64(src.reg().fp(), MemOperand(sp), r0);
            break;
          case kS128: {
            asm_->AddS64(sp, sp, Operand(-kSimd128Size), r0);
            asm_->StoreSimd128(src.reg().fp().toSimd(), MemOperand(sp), r0);
            break;
          }
          default:
            UNREACHABLE();
        }
        break;
      }
      case LiftoffAssembler::VarState::kIntConst: {
        asm_->AllocateStackSpace(stack_decrement - kSystemPointerSize);
        DCHECK(src.kind() == kI32 || src.kind() == kI64);
        UseScratchRegisterScope temps(asm_);
        Register scratch = temps.Acquire();

        switch (src.kind()) {
          case kI32:
            asm_->mov(scratch, Operand(src.i32_const()));
            break;
          case kI64:
            asm_->mov(scratch, Operand(int64_t{slot.src_.i32_const()}));
            break;
          default:
            UNREACHABLE();
        }
        asm_->push(scratch);
        break;
      }
    }
  }
}

}  // namespace v8::internal::wasm

#undef BAILOUT

#endif  // V8_WASM_BASELINE_PPC_LIFTOFF_ASSEMBLER_PPC_INL_H_
