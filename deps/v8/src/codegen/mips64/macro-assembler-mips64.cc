// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>  // For LONG_MIN, LONG_MAX.

#if V8_TARGET_ARCH_MIPS64

#include "src/base/bits.h"
#include "src/base/division-by-constant.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/external-reference-table.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/register-configuration.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frames-inl.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/init/bootstrapper.h"
#include "src/logging/counters.h"
#include "src/objects/heap-number.h"
#include "src/runtime/runtime.h"
#include "src/snapshot/snapshot.h"

// Satisfy cpplint check, but don't include platform-specific header. It is
// included recursively via macro-assembler.h.
#if 0
#include "src/codegen/mips64/macro-assembler-mips64.h"
#endif

#define __ ACCESS_MASM(masm)

namespace v8 {
namespace internal {

static inline bool IsZero(const Operand& rt) {
  if (rt.is_reg()) {
    return rt.rm() == zero_reg;
  } else {
    return rt.immediate() == 0;
  }
}

int MacroAssembler::RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                                    Register exclusion1,
                                                    Register exclusion2,
                                                    Register exclusion3) const {
  int bytes = 0;
  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  bytes += list.Count() * kPointerSize;

  if (fp_mode == SaveFPRegsMode::kSave) {
    bytes += kCallerSavedFPU.Count() * kDoubleSize;
  }

  return bytes;
}

int MacroAssembler::PushCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1,
                                    Register exclusion2, Register exclusion3) {
  ASM_CODE_COMMENT(this);
  int bytes = 0;
  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  MultiPush(list);
  bytes += list.Count() * kPointerSize;

  if (fp_mode == SaveFPRegsMode::kSave) {
    MultiPushFPU(kCallerSavedFPU);
    bytes += kCallerSavedFPU.Count() * kDoubleSize;
  }

  return bytes;
}

int MacroAssembler::PopCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1,
                                   Register exclusion2, Register exclusion3) {
  ASM_CODE_COMMENT(this);
  int bytes = 0;
  if (fp_mode == SaveFPRegsMode::kSave) {
    MultiPopFPU(kCallerSavedFPU);
    bytes += kCallerSavedFPU.Count() * kDoubleSize;
  }

  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  MultiPop(list);
  bytes += list.Count() * kPointerSize;

  return bytes;
}

void MacroAssembler::LoadRoot(Register destination, RootIndex index) {
  Ld(destination, MemOperand(s6, RootRegisterOffsetForRootIndex(index)));
}

void MacroAssembler::LoadRoot(Register destination, RootIndex index,
                              Condition cond, Register src1,
                              const Operand& src2) {
  Branch(2, NegateCondition(cond), src1, src2);
  Ld(destination, MemOperand(s6, RootRegisterOffsetForRootIndex(index)));
}

void MacroAssembler::PushCommonFrame(Register marker_reg) {
  if (marker_reg.is_valid()) {
    Push(ra, fp, marker_reg);
    Daddu(fp, sp, Operand(kPointerSize));
  } else {
    Push(ra, fp);
    mov(fp, sp);
  }
}

void MacroAssembler::PushStandardFrame(Register function_reg) {
  int offset = -StandardFrameConstants::kContextOffset;
  if (function_reg.is_valid()) {
    Push(ra, fp, cp, function_reg, kJavaScriptCallArgCountRegister);
    offset += 2 * kPointerSize;
  } else {
    Push(ra, fp, cp, kJavaScriptCallArgCountRegister);
    offset += kPointerSize;
  }
  Daddu(fp, sp, Operand(offset));
}

// Clobbers object, dst, value, and ra, if (ra_status == kRAHasBeenSaved)
// The register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWriteField(Register object, int offset,
                                      Register value, Register dst,
                                      RAStatus ra_status,
                                      SaveFPRegsMode save_fp,
                                      SmiCheck smi_check) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(value, dst, t8, object));
  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis.
  Label done;

  // Skip barrier if writing a smi.
  if (smi_check == SmiCheck::kInline) {
    JumpIfSmi(value, &done);
  }

  // Although the object register is tagged, the offset is relative to the start
  // of the object, so offset must be a multiple of kPointerSize.
  DCHECK(IsAligned(offset, kPointerSize));

  Daddu(dst, object, Operand(offset - kHeapObjectTag));
  if (v8_flags.debug_code) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Label ok;
    And(t8, dst, Operand(kPointerSize - 1));
    Branch(&ok, eq, t8, Operand(zero_reg));
    stop();
    bind(&ok);
  }

  RecordWrite(object, dst, value, ra_status, save_fp, SmiCheck::kOmit);

  bind(&done);

  // Clobber clobbered input registers when running with the debug-code flag
  // turned on to provoke errors.
  if (v8_flags.debug_code) {
    li(value, Operand(base::bit_cast<int64_t>(kZapValue + 4)));
    li(dst, Operand(base::bit_cast<int64_t>(kZapValue + 8)));
  }
}

void MacroAssembler::MaybeSaveRegisters(RegList registers) {
  if (registers.is_empty()) return;
  MultiPush(registers);
}

void MacroAssembler::MaybeRestoreRegisters(RegList registers) {
  if (registers.is_empty()) return;
  MultiPop(registers);
}

void MacroAssembler::CallEphemeronKeyBarrier(Register object,
                                             Register slot_address,
                                             SaveFPRegsMode fp_mode) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, slot_address));
  RegList registers =
      WriteBarrierDescriptor::ComputeSavedRegisters(object, slot_address);
  MaybeSaveRegisters(registers);

  Register object_parameter = WriteBarrierDescriptor::ObjectRegister();
  Register slot_address_parameter =
      WriteBarrierDescriptor::SlotAddressRegister();

  Push(object);
  Push(slot_address);
  Pop(slot_address_parameter);
  Pop(object_parameter);

  CallBuiltin(Builtins::EphemeronKeyBarrier(fp_mode));
  MaybeRestoreRegisters(registers);
}

void MacroAssembler::CallRecordWriteStubSaveRegisters(Register object,
                                                      Register slot_address,
                                                      SaveFPRegsMode fp_mode,
                                                      StubCallMode mode) {
  DCHECK(!AreAliased(object, slot_address));
  RegList registers =
      WriteBarrierDescriptor::ComputeSavedRegisters(object, slot_address);
  MaybeSaveRegisters(registers);

  Register object_parameter = WriteBarrierDescriptor::ObjectRegister();
  Register slot_address_parameter =
      WriteBarrierDescriptor::SlotAddressRegister();

  Push(object);
  Push(slot_address);
  Pop(slot_address_parameter);
  Pop(object_parameter);

  CallRecordWriteStub(object_parameter, slot_address_parameter, fp_mode, mode);

  MaybeRestoreRegisters(registers);
}

void MacroAssembler::CallRecordWriteStub(Register object, Register slot_address,
                                         SaveFPRegsMode fp_mode,
                                         StubCallMode mode) {
  // Use CallRecordWriteStubSaveRegisters if the object and slot registers
  // need to be caller saved.
  DCHECK_EQ(WriteBarrierDescriptor::ObjectRegister(), object);
  DCHECK_EQ(WriteBarrierDescriptor::SlotAddressRegister(), slot_address);
#if V8_ENABLE_WEBASSEMBLY
  if (mode == StubCallMode::kCallWasmRuntimeStub) {
    auto wasm_target =
        static_cast<Address>(wasm::WasmCode::GetRecordWriteBuiltin(fp_mode));
    Call(wasm_target, RelocInfo::WASM_STUB_CALL);
#else
  if (false) {
#endif
  } else {
    CallBuiltin(Builtins::RecordWrite(fp_mode));
  }
}

// Clobbers object, address, value, and ra, if (ra_status == kRAHasBeenSaved)
// The register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWrite(Register object, Register address,
                                 Register value, RAStatus ra_status,
                                 SaveFPRegsMode fp_mode, SmiCheck smi_check) {
  DCHECK(!AreAliased(object, address, value, t8));
  DCHECK(!AreAliased(object, address, value, t9));

  if (v8_flags.debug_code) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(!AreAliased(object, value, scratch));
    Ld(scratch, MemOperand(address));
    Assert(eq, AbortReason::kWrongAddressOrValuePassedToRecordWrite, scratch,
           Operand(value));
  }

  if (v8_flags.disable_write_barriers) {
    return;
  }

  // First, check if a write barrier is even needed. The tests below
  // catch stores of smis and stores into the young generation.
  Label done;

  if (smi_check == SmiCheck::kInline) {
    DCHECK_EQ(0, kSmiTag);
    JumpIfSmi(value, &done);
  }

  CheckPageFlag(value,
                value,  // Used as scratch.
                MemoryChunk::kPointersToHereAreInterestingMask, eq, &done);
  CheckPageFlag(object,
                value,  // Used as scratch.
                MemoryChunk::kPointersFromHereAreInterestingMask, eq, &done);

  // Record the actual write.
  if (ra_status == kRAHasNotBeenSaved) {
    push(ra);
  }

  Register slot_address = WriteBarrierDescriptor::SlotAddressRegister();
  DCHECK(!AreAliased(object, slot_address, value));
  mov(slot_address, address);
  CallRecordWriteStub(object, slot_address, fp_mode);

  if (ra_status == kRAHasNotBeenSaved) {
    pop(ra);
  }

  bind(&done);

  // Clobber clobbered registers when running with the debug-code flag
  // turned on to provoke errors.
  if (v8_flags.debug_code) {
    li(address, Operand(base::bit_cast<int64_t>(kZapValue + 12)));
    li(value, Operand(base::bit_cast<int64_t>(kZapValue + 16)));
    li(slot_address, Operand(base::bit_cast<int64_t>(kZapValue + 20)));
  }
}

// ---------------------------------------------------------------------------
// Instruction macros.

void MacroAssembler::Addu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    addu(rd, rs, rt.rm());
  } else {
    if (is_int16(rt.immediate()) && !MustUseReg(rt.rmode())) {
      addiu(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      addu(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Daddu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    daddu(rd, rs, rt.rm());
  } else {
    if (is_int16(rt.immediate()) && !MustUseReg(rt.rmode())) {
      daddiu(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      daddu(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Subu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    subu(rd, rs, rt.rm());
  } else {
    DCHECK(is_int32(rt.immediate()));
    if (is_int16(-rt.immediate()) && !MustUseReg(rt.rmode())) {
      addiu(rd, rs,
            static_cast<int32_t>(
                -rt.immediate()));  // No subiu instr, use addiu(x, y, -imm).
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      if (-rt.immediate() >> 16 == 0 && !MustUseReg(rt.rmode())) {
        // Use load -imm and addu when loading -imm generates one instruction.
        li(scratch, -rt.immediate());
        addu(rd, rs, scratch);
      } else {
        // li handles the relocation.
        li(scratch, rt);
        subu(rd, rs, scratch);
      }
    }
  }
}

void MacroAssembler::Dsubu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    dsubu(rd, rs, rt.rm());
  } else if (is_int16(-rt.immediate()) && !MustUseReg(rt.rmode())) {
    daddiu(rd, rs,
           static_cast<int32_t>(
               -rt.immediate()));  // No dsubiu instr, use daddiu(x, y, -imm).
  } else {
    DCHECK(rs != at);
    int li_count = InstrCountForLi64Bit(rt.immediate());
    int li_neg_count = InstrCountForLi64Bit(-rt.immediate());
    if (li_neg_count < li_count && !MustUseReg(rt.rmode())) {
      // Use load -imm and daddu when loading -imm generates one instruction.
      DCHECK(rt.immediate() != std::numeric_limits<int32_t>::min());
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, Operand(-rt.immediate()));
      Daddu(rd, rs, scratch);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, rt);
      dsubu(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Mul(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mul(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    mul(rd, rs, scratch);
  }
}

void MacroAssembler::Mulh(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant != kMips64r6) {
      mult(rs, rt.rm());
      mfhi(rd);
    } else {
      muh(rd, rs, rt.rm());
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant != kMips64r6) {
      mult(rs, scratch);
      mfhi(rd);
    } else {
      muh(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Mulhu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant != kMips64r6) {
      multu(rs, rt.rm());
      mfhi(rd);
    } else {
      muhu(rd, rs, rt.rm());
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant != kMips64r6) {
      multu(rs, scratch);
      mfhi(rd);
    } else {
      muhu(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Dmul(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant == kMips64r6) {
      dmul(rd, rs, rt.rm());
    } else {
      dmult(rs, rt.rm());
      mflo(rd);
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant == kMips64r6) {
      dmul(rd, rs, scratch);
    } else {
      dmult(rs, scratch);
      mflo(rd);
    }
  }
}

void MacroAssembler::Dmulh(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant == kMips64r6) {
      dmuh(rd, rs, rt.rm());
    } else {
      dmult(rs, rt.rm());
      mfhi(rd);
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant == kMips64r6) {
      dmuh(rd, rs, scratch);
    } else {
      dmult(rs, scratch);
      mfhi(rd);
    }
  }
}

void MacroAssembler::Dmulhu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant == kMips64r6) {
      dmuhu(rd, rs, rt.rm());
    } else {
      dmultu(rs, rt.rm());
      mfhi(rd);
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant == kMips64r6) {
      dmuhu(rd, rs, scratch);
    } else {
      dmultu(rs, scratch);
      mfhi(rd);
    }
  }
}

void MacroAssembler::Mult(Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mult(rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    mult(rs, scratch);
  }
}

void MacroAssembler::Dmult(Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    dmult(rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    dmult(rs, scratch);
  }
}

void MacroAssembler::Multu(Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    multu(rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    multu(rs, scratch);
  }
}

void MacroAssembler::Dmultu(Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    dmultu(rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    dmultu(rs, scratch);
  }
}

void MacroAssembler::Div(Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    div(rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    div(rs, scratch);
  }
}

void MacroAssembler::Div(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant != kMips64r6) {
      div(rs, rt.rm());
      mflo(res);
    } else {
      div(res, rs, rt.rm());
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant != kMips64r6) {
      div(rs, scratch);
      mflo(res);
    } else {
      div(res, rs, scratch);
    }
  }
}

void MacroAssembler::Mod(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant != kMips64r6) {
      div(rs, rt.rm());
      mfhi(rd);
    } else {
      mod(rd, rs, rt.rm());
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant != kMips64r6) {
      div(rs, scratch);
      mfhi(rd);
    } else {
      mod(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Modu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant != kMips64r6) {
      divu(rs, rt.rm());
      mfhi(rd);
    } else {
      modu(rd, rs, rt.rm());
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant != kMips64r6) {
      divu(rs, scratch);
      mfhi(rd);
    } else {
      modu(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Ddiv(Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    ddiv(rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    ddiv(rs, scratch);
  }
}

void MacroAssembler::Ddiv(Register rd, Register rs, const Operand& rt) {
  if (kArchVariant != kMips64r6) {
    if (rt.is_reg()) {
      ddiv(rs, rt.rm());
      mflo(rd);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      ddiv(rs, scratch);
      mflo(rd);
    }
  } else {
    if (rt.is_reg()) {
      ddiv(rd, rs, rt.rm());
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      ddiv(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Divu(Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    divu(rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    divu(rs, scratch);
  }
}

void MacroAssembler::Divu(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant != kMips64r6) {
      divu(rs, rt.rm());
      mflo(res);
    } else {
      divu(res, rs, rt.rm());
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant != kMips64r6) {
      divu(rs, scratch);
      mflo(res);
    } else {
      divu(res, rs, scratch);
    }
  }
}

void MacroAssembler::Ddivu(Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    ddivu(rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    ddivu(rs, scratch);
  }
}

void MacroAssembler::Ddivu(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (kArchVariant != kMips64r6) {
      ddivu(rs, rt.rm());
      mflo(res);
    } else {
      ddivu(res, rs, rt.rm());
    }
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    if (kArchVariant != kMips64r6) {
      ddivu(rs, scratch);
      mflo(res);
    } else {
      ddivu(res, rs, scratch);
    }
  }
}

void MacroAssembler::Dmod(Register rd, Register rs, const Operand& rt) {
  if (kArchVariant != kMips64r6) {
    if (rt.is_reg()) {
      ddiv(rs, rt.rm());
      mfhi(rd);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      ddiv(rs, scratch);
      mfhi(rd);
    }
  } else {
    if (rt.is_reg()) {
      dmod(rd, rs, rt.rm());
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      dmod(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Dmodu(Register rd, Register rs, const Operand& rt) {
  if (kArchVariant != kMips64r6) {
    if (rt.is_reg()) {
      ddivu(rs, rt.rm());
      mfhi(rd);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      ddivu(rs, scratch);
      mfhi(rd);
    }
  } else {
    if (rt.is_reg()) {
      dmodu(rd, rs, rt.rm());
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      dmodu(rd, rs, scratch);
    }
  }
}

void MacroAssembler::And(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    and_(rd, rs, rt.rm());
  } else {
    if (is_uint16(rt.immediate()) && !MustUseReg(rt.rmode())) {
      andi(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      and_(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Or(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    or_(rd, rs, rt.rm());
  } else {
    if (is_uint16(rt.immediate()) && !MustUseReg(rt.rmode())) {
      ori(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      or_(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Xor(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    xor_(rd, rs, rt.rm());
  } else {
    if (is_uint16(rt.immediate()) && !MustUseReg(rt.rmode())) {
      xori(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rs != scratch);
      li(scratch, rt);
      xor_(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Nor(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    nor(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rs != scratch);
    li(scratch, rt);
    nor(rd, rs, scratch);
  }
}

void MacroAssembler::Neg(Register rs, const Operand& rt) {
  dsubu(rs, zero_reg, rt.rm());
}

void MacroAssembler::Slt(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    slt(rd, rs, rt.rm());
  } else {
    if (is_int16(rt.immediate()) && !MustUseReg(rt.rmode())) {
      slti(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      DCHECK(rs != scratch);
      li(scratch, rt);
      slt(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Sltu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sltu(rd, rs, rt.rm());
  } else {
    const uint64_t int16_min = std::numeric_limits<int16_t>::min();
    if (is_uint15(rt.immediate()) && !MustUseReg(rt.rmode())) {
      // Imm range is: [0, 32767].
      sltiu(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else if (is_uint15(rt.immediate() - int16_min) &&
               !MustUseReg(rt.rmode())) {
      // Imm range is: [max_unsigned-32767,max_unsigned].
      sltiu(rd, rs, static_cast<uint16_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      DCHECK(rs != scratch);
      li(scratch, rt);
      sltu(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Sle(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    slt(rd, rt.rm(), rs);
  } else {
    if (rt.immediate() == 0 && !MustUseReg(rt.rmode())) {
      slt(rd, zero_reg, rs);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      BlockTrampolinePoolScope block_trampoline_pool(this);
      DCHECK(rs != scratch);
      li(scratch, rt);
      slt(rd, scratch, rs);
    }
  }
  xori(rd, rd, 1);
}

void MacroAssembler::Sleu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sltu(rd, rt.rm(), rs);
  } else {
    if (rt.immediate() == 0 && !MustUseReg(rt.rmode())) {
      sltu(rd, zero_reg, rs);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      BlockTrampolinePoolScope block_trampoline_pool(this);
      DCHECK(rs != scratch);
      li(scratch, rt);
      sltu(rd, scratch, rs);
    }
  }
  xori(rd, rd, 1);
}

void MacroAssembler::Sge(Register rd, Register rs, const Operand& rt) {
  Slt(rd, rs, rt);
  xori(rd, rd, 1);
}

void MacroAssembler::Sgeu(Register rd, Register rs, const Operand& rt) {
  Sltu(rd, rs, rt);
  xori(rd, rd, 1);
}

void MacroAssembler::Sgt(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    slt(rd, rt.rm(), rs);
  } else {
    if (rt.immediate() == 0 && !MustUseReg(rt.rmode())) {
      slt(rd, zero_reg, rs);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      BlockTrampolinePoolScope block_trampoline_pool(this);
      DCHECK(rs != scratch);
      li(scratch, rt);
      slt(rd, scratch, rs);
    }
  }
}

void MacroAssembler::Sgtu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sltu(rd, rt.rm(), rs);
  } else {
    if (rt.immediate() == 0 && !MustUseReg(rt.rmode())) {
      sltu(rd, zero_reg, rs);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      BlockTrampolinePoolScope block_trampoline_pool(this);
      DCHECK(rs != scratch);
      li(scratch, rt);
      sltu(rd, scratch, rs);
    }
  }
}

void MacroAssembler::Ror(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    rotrv(rd, rs, rt.rm());
  } else {
    int64_t ror_value = rt.immediate() % 32;
    if (ror_value < 0) {
      ror_value += 32;
    }
    rotr(rd, rs, ror_value);
  }
}

void MacroAssembler::Dror(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    drotrv(rd, rs, rt.rm());
  } else {
    int64_t dror_value = rt.immediate() % 64;
    if (dror_value < 0) dror_value += 64;
    if (dror_value <= 31) {
      drotr(rd, rs, dror_value);
    } else {
      drotr32(rd, rs, dror_value - 32);
    }
  }
}

void MacroAssembler::Pref(int32_t hint, const MemOperand& rs) {
  pref(hint, rs);
}

void MacroAssembler::Lsa(Register rd, Register rt, Register rs, uint8_t sa,
                         Register scratch) {
  DCHECK(sa >= 1 && sa <= 31);
  if (kArchVariant == kMips64r6 && sa <= 4) {
    lsa(rd, rt, rs, sa - 1);
  } else {
    Register tmp = rd == rt ? scratch : rd;
    DCHECK(tmp != rt);
    sll(tmp, rs, sa);
    Addu(rd, rt, tmp);
  }
}

void MacroAssembler::Dlsa(Register rd, Register rt, Register rs, uint8_t sa,
                          Register scratch) {
  DCHECK(sa >= 1 && sa <= 63);
  if (kArchVariant == kMips64r6 && sa <= 4) {
    dlsa(rd, rt, rs, sa - 1);
  } else {
    Register tmp = rd == rt ? scratch : rd;
    DCHECK(tmp != rt);
    if (sa <= 31)
      dsll(tmp, rs, sa);
    else
      dsll32(tmp, rs, sa - 32);
    Daddu(rd, rt, tmp);
  }
}

void MacroAssembler::Bovc(Register rs, Register rt, Label* L) {
  if (is_trampoline_emitted()) {
    Label skip;
    bnvc(rs, rt, &skip);
    BranchLong(L, PROTECT);
    bind(&skip);
  } else {
    bovc(rs, rt, L);
  }
}

void MacroAssembler::Bnvc(Register rs, Register rt, Label* L) {
  if (is_trampoline_emitted()) {
    Label skip;
    bovc(rs, rt, &skip);
    BranchLong(L, PROTECT);
    bind(&skip);
  } else {
    bnvc(rs, rt, L);
  }
}

// ------------Pseudo-instructions-------------

// Change endianness
void MacroAssembler::ByteSwapSigned(Register dest, Register src,
                                    int operand_size) {
  DCHECK(operand_size == 2 || operand_size == 4 || operand_size == 8);
  DCHECK(kArchVariant == kMips64r6 || kArchVariant == kMips64r2);
  if (operand_size == 2) {
    wsbh(dest, src);
    seh(dest, dest);
  } else if (operand_size == 4) {
    wsbh(dest, src);
    rotr(dest, dest, 16);
  } else {
    dsbh(dest, src);
    dshd(dest, dest);
  }
}

void MacroAssembler::ByteSwapUnsigned(Register dest, Register src,
                                      int operand_size) {
  DCHECK(operand_size == 2 || operand_size == 4);
  if (operand_size == 2) {
    wsbh(dest, src);
    andi(dest, dest, 0xFFFF);
  } else {
    wsbh(dest, src);
    rotr(dest, dest, 16);
    dinsu_(dest, zero_reg, 32, 32);
  }
}

void MacroAssembler::Ulw(Register rd, const MemOperand& rs) {
  DCHECK(rd != at);
  DCHECK(rs.rm() != at);
  if (kArchVariant == kMips64r6) {
    Lw(rd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    DCHECK(kMipsLwrOffset <= 3 && kMipsLwlOffset <= 3);
    MemOperand source = rs;
    // Adjust offset for two accesses and check if offset + 3 fits into int16_t.
    AdjustBaseAndOffset(&source, OffsetAccessType::TWO_ACCESSES, 3);
    if (rd != source.rm()) {
      lwr(rd, MemOperand(source.rm(), source.offset() + kMipsLwrOffset));
      lwl(rd, MemOperand(source.rm(), source.offset() + kMipsLwlOffset));
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      lwr(scratch, MemOperand(rs.rm(), rs.offset() + kMipsLwrOffset));
      lwl(scratch, MemOperand(rs.rm(), rs.offset() + kMipsLwlOffset));
      mov(rd, scratch);
    }
  }
}

void MacroAssembler::Ulwu(Register rd, const MemOperand& rs) {
  if (kArchVariant == kMips64r6) {
    Lwu(rd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    Ulw(rd, rs);
    Dext(rd, rd, 0, 32);
  }
}

void MacroAssembler::Usw(Register rd, const MemOperand& rs) {
  DCHECK(rd != at);
  DCHECK(rs.rm() != at);
  DCHECK(rd != rs.rm());
  if (kArchVariant == kMips64r6) {
    Sw(rd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    DCHECK(kMipsSwrOffset <= 3 && kMipsSwlOffset <= 3);
    MemOperand source = rs;
    // Adjust offset for two accesses and check if offset + 3 fits into int16_t.
    AdjustBaseAndOffset(&source, OffsetAccessType::TWO_ACCESSES, 3);
    swr(rd, MemOperand(source.rm(), source.offset() + kMipsSwrOffset));
    swl(rd, MemOperand(source.rm(), source.offset() + kMipsSwlOffset));
  }
}

void MacroAssembler::Ulh(Register rd, const MemOperand& rs) {
  DCHECK(rd != at);
  DCHECK(rs.rm() != at);
  if (kArchVariant == kMips64r6) {
    Lh(rd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    MemOperand source = rs;
    // Adjust offset for two accesses and check if offset + 1 fits into int16_t.
    AdjustBaseAndOffset(&source, OffsetAccessType::TWO_ACCESSES, 1);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    if (source.rm() == scratch) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
      Lb(rd, MemOperand(source.rm(), source.offset() + 1));
      Lbu(scratch, source);
#elif defined(V8_TARGET_BIG_ENDIAN)
      Lb(rd, source);
      Lbu(scratch, MemOperand(source.rm(), source.offset() + 1));
#endif
    } else {
#if defined(V8_TARGET_LITTLE_ENDIAN)
      Lbu(scratch, source);
      Lb(rd, MemOperand(source.rm(), source.offset() + 1));
#elif defined(V8_TARGET_BIG_ENDIAN)
      Lbu(scratch, MemOperand(source.rm(), source.offset() + 1));
      Lb(rd, source);
#endif
    }
    dsll(rd, rd, 8);
    or_(rd, rd, scratch);
  }
}

void MacroAssembler::Ulhu(Register rd, const MemOperand& rs) {
  DCHECK(rd != at);
  DCHECK(rs.rm() != at);
  if (kArchVariant == kMips64r6) {
    Lhu(rd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    MemOperand source = rs;
    // Adjust offset for two accesses and check if offset + 1 fits into int16_t.
    AdjustBaseAndOffset(&source, OffsetAccessType::TWO_ACCESSES, 1);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    if (source.rm() == scratch) {
#if defined(V8_TARGET_LITTLE_ENDIAN)
      Lbu(rd, MemOperand(source.rm(), source.offset() + 1));
      Lbu(scratch, source);
#elif defined(V8_TARGET_BIG_ENDIAN)
      Lbu(rd, source);
      Lbu(scratch, MemOperand(source.rm(), source.offset() + 1));
#endif
    } else {
#if defined(V8_TARGET_LITTLE_ENDIAN)
      Lbu(scratch, source);
      Lbu(rd, MemOperand(source.rm(), source.offset() + 1));
#elif defined(V8_TARGET_BIG_ENDIAN)
      Lbu(scratch, MemOperand(source.rm(), source.offset() + 1));
      Lbu(rd, source);
#endif
    }
    dsll(rd, rd, 8);
    or_(rd, rd, scratch);
  }
}

void MacroAssembler::Ush(Register rd, const MemOperand& rs, Register scratch) {
  DCHECK(rd != at);
  DCHECK(rs.rm() != at);
  DCHECK(rs.rm() != scratch);
  DCHECK(scratch != at);
  if (kArchVariant == kMips64r6) {
    Sh(rd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    MemOperand source = rs;
    // Adjust offset for two accesses and check if offset + 1 fits into int16_t.
    AdjustBaseAndOffset(&source, OffsetAccessType::TWO_ACCESSES, 1);

    if (scratch != rd) {
      mov(scratch, rd);
    }

#if defined(V8_TARGET_LITTLE_ENDIAN)
    Sb(scratch, source);
    srl(scratch, scratch, 8);
    Sb(scratch, MemOperand(source.rm(), source.offset() + 1));
#elif defined(V8_TARGET_BIG_ENDIAN)
    Sb(scratch, MemOperand(source.rm(), source.offset() + 1));
    srl(scratch, scratch, 8);
    Sb(scratch, source);
#endif
  }
}

void MacroAssembler::Uld(Register rd, const MemOperand& rs) {
  DCHECK(rd != at);
  DCHECK(rs.rm() != at);
  if (kArchVariant == kMips64r6) {
    Ld(rd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    DCHECK(kMipsLdrOffset <= 7 && kMipsLdlOffset <= 7);
    MemOperand source = rs;
    // Adjust offset for two accesses and check if offset + 7 fits into int16_t.
    AdjustBaseAndOffset(&source, OffsetAccessType::TWO_ACCESSES, 7);
    if (rd != source.rm()) {
      ldr(rd, MemOperand(source.rm(), source.offset() + kMipsLdrOffset));
      ldl(rd, MemOperand(source.rm(), source.offset() + kMipsLdlOffset));
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      ldr(scratch, MemOperand(rs.rm(), rs.offset() + kMipsLdrOffset));
      ldl(scratch, MemOperand(rs.rm(), rs.offset() + kMipsLdlOffset));
      mov(rd, scratch);
    }
  }
}

// Load consequent 32-bit word pair in 64-bit reg. and put first word in low
// bits,
// second word in high bits.
void MacroAssembler::LoadWordPair(Register rd, const MemOperand& rs,
                                  Register scratch) {
  Lwu(rd, rs);
  Lw(scratch, MemOperand(rs.rm(), rs.offset() + kPointerSize / 2));
  dsll32(scratch, scratch, 0);
  Daddu(rd, rd, scratch);
}

void MacroAssembler::Usd(Register rd, const MemOperand& rs) {
  DCHECK(rd != at);
  DCHECK(rs.rm() != at);
  if (kArchVariant == kMips64r6) {
    Sd(rd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    DCHECK(kMipsSdrOffset <= 7 && kMipsSdlOffset <= 7);
    MemOperand source = rs;
    // Adjust offset for two accesses and check if offset + 7 fits into int16_t.
    AdjustBaseAndOffset(&source, OffsetAccessType::TWO_ACCESSES, 7);
    sdr(rd, MemOperand(source.rm(), source.offset() + kMipsSdrOffset));
    sdl(rd, MemOperand(source.rm(), source.offset() + kMipsSdlOffset));
  }
}

// Do 64-bit store as two consequent 32-bit stores to unaligned address.
void MacroAssembler::StoreWordPair(Register rd, const MemOperand& rs,
                                   Register scratch) {
  Sw(rd, rs);
  dsrl32(scratch, rd, 0);
  Sw(scratch, MemOperand(rs.rm(), rs.offset() + kPointerSize / 2));
}

void MacroAssembler::Ulwc1(FPURegister fd, const MemOperand& rs,
                           Register scratch) {
  if (kArchVariant == kMips64r6) {
    Lwc1(fd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    Ulw(scratch, rs);
    mtc1(scratch, fd);
  }
}

void MacroAssembler::Uswc1(FPURegister fd, const MemOperand& rs,
                           Register scratch) {
  if (kArchVariant == kMips64r6) {
    Swc1(fd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    mfc1(scratch, fd);
    Usw(scratch, rs);
  }
}

void MacroAssembler::Uldc1(FPURegister fd, const MemOperand& rs,
                           Register scratch) {
  DCHECK(scratch != at);
  if (kArchVariant == kMips64r6) {
    Ldc1(fd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    Uld(scratch, rs);
    dmtc1(scratch, fd);
  }
}

void MacroAssembler::Usdc1(FPURegister fd, const MemOperand& rs,
                           Register scratch) {
  DCHECK(scratch != at);
  if (kArchVariant == kMips64r6) {
    Sdc1(fd, rs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    dmfc1(scratch, fd);
    Usd(scratch, rs);
  }
}

void MacroAssembler::Lb(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  lb(rd, source);
}

void MacroAssembler::Lbu(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  lbu(rd, source);
}

void MacroAssembler::Sb(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  sb(rd, source);
}

void MacroAssembler::Lh(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  lh(rd, source);
}

void MacroAssembler::Lhu(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  lhu(rd, source);
}

void MacroAssembler::Sh(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  sh(rd, source);
}

void MacroAssembler::Lw(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  lw(rd, source);
}

void MacroAssembler::Lwu(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  lwu(rd, source);
}

void MacroAssembler::Sw(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  sw(rd, source);
}

void MacroAssembler::Ld(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  ld(rd, source);
}

void MacroAssembler::Sd(Register rd, const MemOperand& rs) {
  MemOperand source = rs;
  AdjustBaseAndOffset(&source);
  sd(rd, source);
}

void MacroAssembler::Lwc1(FPURegister fd, const MemOperand& src) {
  MemOperand tmp = src;
  AdjustBaseAndOffset(&tmp);
  lwc1(fd, tmp);
}

void MacroAssembler::Swc1(FPURegister fs, const MemOperand& src) {
  MemOperand tmp = src;
  AdjustBaseAndOffset(&tmp);
  swc1(fs, tmp);
}

void MacroAssembler::Ldc1(FPURegister fd, const MemOperand& src) {
  MemOperand tmp = src;
  AdjustBaseAndOffset(&tmp);
  ldc1(fd, tmp);
}

void MacroAssembler::Sdc1(FPURegister fs, const MemOperand& src) {
  MemOperand tmp = src;
  AdjustBaseAndOffset(&tmp);
  sdc1(fs, tmp);
}

void MacroAssembler::Ll(Register rd, const MemOperand& rs) {
  bool is_one_instruction = (kArchVariant == kMips64r6) ? is_int9(rs.offset())
                                                        : is_int16(rs.offset());
  if (is_one_instruction) {
    ll(rd, rs);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, rs.offset());
    daddu(scratch, scratch, rs.rm());
    ll(rd, MemOperand(scratch, 0));
  }
}

void MacroAssembler::Lld(Register rd, const MemOperand& rs) {
  bool is_one_instruction = (kArchVariant == kMips64r6) ? is_int9(rs.offset())
                                                        : is_int16(rs.offset());
  if (is_one_instruction) {
    lld(rd, rs);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, rs.offset());
    daddu(scratch, scratch, rs.rm());
    lld(rd, MemOperand(scratch, 0));
  }
}

void MacroAssembler::Sc(Register rd, const MemOperand& rs) {
  bool is_one_instruction = (kArchVariant == kMips64r6) ? is_int9(rs.offset())
                                                        : is_int16(rs.offset());
  if (is_one_instruction) {
    sc(rd, rs);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, rs.offset());
    daddu(scratch, scratch, rs.rm());
    sc(rd, MemOperand(scratch, 0));
  }
}

void MacroAssembler::Scd(Register rd, const MemOperand& rs) {
  bool is_one_instruction = (kArchVariant == kMips64r6) ? is_int9(rs.offset())
                                                        : is_int16(rs.offset());
  if (is_one_instruction) {
    scd(rd, rs);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, rs.offset());
    daddu(scratch, scratch, rs.rm());
    scd(rd, MemOperand(scratch, 0));
  }
}

void MacroAssembler::li(Register dst, Handle<HeapObject> value, LiFlags mode) {
  // TODO(jgruber,v8:8887): Also consider a root-relative load when generating
  // non-isolate-independent code. In many cases it might be cheaper than
  // embedding the relocatable value.
  if (root_array_available_ && options().isolate_independent_code) {
    IndirectLoadConstant(dst, value);
    return;
  }
  li(dst, Operand(value), mode);
}

void MacroAssembler::li(Register dst, ExternalReference reference,
                        LiFlags mode) {
  if (root_array_available()) {
    if (reference.IsIsolateFieldId()) {
      Daddu(dst, kRootRegister, Operand(reference.offset_from_root_register()));
      return;
    }
    if (options().isolate_independent_code) {
      IndirectLoadExternalReference(dst, reference);
      return;
    }
  }

  // External references should not get created with IDs if
  // `!root_array_available()`.
  CHECK(!reference.IsIsolateFieldId());
  li(dst, Operand(reference), mode);
}

static inline int InstrCountForLiLower32Bit(int64_t value) {
  if (!is_int16(static_cast<int32_t>(value)) && (value & kUpper16MaskOf64) &&
      (value & kImm16Mask)) {
    return 2;
  } else {
    return 1;
  }
}

void MacroAssembler::LiLower32BitHelper(Register rd, Operand j) {
  if (is_int16(static_cast<int32_t>(j.immediate()))) {
    daddiu(rd, zero_reg, (j.immediate() & kImm16Mask));
  } else if (!(j.immediate() & kUpper16MaskOf64)) {
    ori(rd, zero_reg, j.immediate() & kImm16Mask);
  } else {
    lui(rd, j.immediate() >> kLuiShift & kImm16Mask);
    if (j.immediate() & kImm16Mask) {
      ori(rd, rd, j.immediate() & kImm16Mask);
    }
  }
}

static inline int InstrCountForLoadReplicatedConst32(int64_t value) {
  uint32_t x = static_cast<uint32_t>(value);
  uint32_t y = static_cast<uint32_t>(value >> 32);

  if (x == y) {
    return (is_uint16(x) || is_int16(x) || (x & kImm16Mask) == 0) ? 2 : 3;
  }

  return INT_MAX;
}

int MacroAssembler::InstrCountForLi64Bit(int64_t value) {
  if (is_int32(value)) {
    return InstrCountForLiLower32Bit(value);
  } else {
    int bit31 = value >> 31 & 0x1;
    if ((value & kUpper16MaskOf64) == 0 && is_int16(value >> 32) &&
        kArchVariant == kMips64r6) {
      return 2;
    } else if ((value & (kHigher16MaskOf64 | kUpper16MaskOf64)) == 0 &&
               kArchVariant == kMips64r6) {
      return 2;
    } else if ((value & kImm16Mask) == 0 && is_int16((value >> 32) + bit31) &&
               kArchVariant == kMips64r6) {
      return 2;
    } else if ((value & kImm16Mask) == 0 &&
               ((value >> 31) & 0x1FFFF) == ((0x20000 - bit31) & 0x1FFFF) &&
               kArchVariant == kMips64r6) {
      return 2;
    } else if (is_int16(static_cast<int32_t>(value)) &&
               is_int16((value >> 32) + bit31) && kArchVariant == kMips64r6) {
      return 2;
    } else if (is_int16(static_cast<int32_t>(value)) &&
               ((value >> 31) & 0x1FFFF) == ((0x20000 - bit31) & 0x1FFFF) &&
               kArchVariant == kMips64r6) {
      return 2;
    } else if (base::bits::IsPowerOfTwo(value + 1) ||
               value == std::numeric_limits<int64_t>::max()) {
      return 2;
    } else {
      int shift_cnt = base::bits::CountTrailingZeros64(value);
      int rep32_count = InstrCountForLoadReplicatedConst32(value);
      int64_t tmp = value >> shift_cnt;
      if (is_uint16(tmp)) {
        return 2;
      } else if (is_int16(tmp)) {
        return 2;
      } else if (rep32_count < 3) {
        return 2;
      } else if (is_int32(tmp)) {
        return 3;
      } else {
        shift_cnt = 16 + base::bits::CountTrailingZeros64(value >> 16);
        tmp = value >> shift_cnt;
        if (is_uint16(tmp)) {
          return 3;
        } else if (is_int16(tmp)) {
          return 3;
        } else if (rep32_count < 4) {
          return 3;
        } else if (kArchVariant == kMips64r6) {
          int64_t imm = value;
          int count = InstrCountForLiLower32Bit(imm);
          imm = (imm >> 32) + bit31;
          if (imm & kImm16Mask) {
            count++;
          }
          imm = (imm >> 16) + (imm >> 15 & 0x1);
          if (imm & kImm16Mask) {
            count++;
          }
          return count;
        } else {
          if (is_int48(value)) {
            int64_t k = value >> 16;
            int count = InstrCountForLiLower32Bit(k) + 1;
            if (value & kImm16Mask) {
              count++;
            }
            return count;
          } else {
            int64_t k = value >> 32;
            int count = InstrCountForLiLower32Bit(k);
            if ((value >> 16) & kImm16Mask) {
              count += 3;
              if (value & kImm16Mask) {
                count++;
              }
            } else {
              count++;
              if (value & kImm16Mask) {
                count++;
              }
            }
            return count;
          }
        }
      }
    }
  }
  UNREACHABLE();
  return INT_MAX;
}

// All changes to if...else conditions here must be added to
// InstrCountForLi64Bit as well.
void MacroAssembler::li_optimized(Register rd, Operand j, LiFlags mode) {
  DCHECK(!j.is_reg());
  DCHECK(!MustUseReg(j.rmode()));
  DCHECK(mode == OPTIMIZE_SIZE);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Normal load of an immediate value which does not need Relocation Info.
  if (is_int32(j.immediate())) {
    LiLower32BitHelper(rd, j);
  } else {
    int bit31 = j.immediate() >> 31 & 0x1;
    if ((j.immediate() & kUpper16MaskOf64) == 0 &&
        is_int16(j.immediate() >> 32) && kArchVariant == kMips64r6) {
      // 64-bit value which consists of an unsigned 16-bit value in its
      // least significant 32-bits, and a signed 16-bit value in its
      // most significant 32-bits.
      ori(rd, zero_reg, j.immediate() & kImm16Mask);
      dahi(rd, j.immediate() >> 32 & kImm16Mask);
    } else if ((j.immediate() & (kHigher16MaskOf64 | kUpper16MaskOf64)) == 0 &&
               kArchVariant == kMips64r6) {
      // 64-bit value which consists of an unsigned 16-bit value in its
      // least significant 48-bits, and a signed 16-bit value in its
      // most significant 16-bits.
      ori(rd, zero_reg, j.immediate() & kImm16Mask);
      dati(rd, j.immediate() >> 48 & kImm16Mask);
    } else if ((j.immediate() & kImm16Mask) == 0 &&
               is_int16((j.immediate() >> 32) + bit31) &&
               kArchVariant == kMips64r6) {
      // 16 LSBs (Least Significant Bits) all set to zero.
      // 48 MSBs (Most Significant Bits) hold a signed 32-bit value.
      lui(rd, j.immediate() >> kLuiShift & kImm16Mask);
      dahi(rd, ((j.immediate() >> 32) + bit31) & kImm16Mask);
    } else if ((j.immediate() & kImm16Mask) == 0 &&
               ((j.immediate() >> 31) & 0x1FFFF) ==
                   ((0x20000 - bit31) & 0x1FFFF) &&
               kArchVariant == kMips64r6) {
      // 16 LSBs all set to zero.
      // 48 MSBs hold a signed value which can't be represented by signed
      // 32-bit number, and the middle 16 bits are all zero, or all one.
      lui(rd, j.immediate() >> kLuiShift & kImm16Mask);
      dati(rd, ((j.immediate() >> 48) + bit31) & kImm16Mask);
    } else if (is_int16(static_cast<int32_t>(j.immediate())) &&
               is_int16((j.immediate() >> 32) + bit31) &&
               kArchVariant == kMips64r6) {
      // 32 LSBs contain a signed 16-bit number.
      // 32 MSBs contain a signed 16-bit number.
      daddiu(rd, zero_reg, j.immediate() & kImm16Mask);
      dahi(rd, ((j.immediate() >> 32) + bit31) & kImm16Mask);
    } else if (is_int16(static_cast<int32_t>(j.immediate())) &&
               ((j.immediate() >> 31) & 0x1FFFF) ==
                   ((0x20000 - bit31) & 0x1FFFF) &&
               kArchVariant == kMips64r6) {
      // 48 LSBs contain an unsigned 16-bit number.
      // 16 MSBs contain a signed 16-bit number.
      daddiu(rd, zero_reg, j.immediate() & kImm16Mask);
      dati(rd, ((j.immediate() >> 48) + bit31) & kImm16Mask);
    } else if (base::bits::IsPowerOfTwo(j.immediate() + 1) ||
               j.immediate() == std::numeric_limits<int64_t>::max()) {
      // 64-bit values which have their "n" LSBs set to one, and their
      // "64-n" MSBs set to zero. "n" must meet the restrictions 0 < n < 64.
      int shift_cnt = 64 - base::bits::CountTrailingZeros64(j.immediate() + 1);
      daddiu(rd, zero_reg, -1);
      if (shift_cnt < 32) {
        dsrl(rd, rd, shift_cnt);
      } else {
        dsrl32(rd, rd, shift_cnt & 31);
      }
    } else {
      int shift_cnt = base::bits::CountTrailingZeros64(j.immediate());
      int rep32_count = InstrCountForLoadReplicatedConst32(j.immediate());
      int64_t tmp = j.immediate() >> shift_cnt;
      if (is_uint16(tmp)) {
        // Value can be computed by loading a 16-bit unsigned value, and
        // then shifting left.
        ori(rd, zero_reg, tmp & kImm16Mask);
        if (shift_cnt < 32) {
          dsll(rd, rd, shift_cnt);
        } else {
          dsll32(rd, rd, shift_cnt & 31);
        }
      } else if (is_int16(tmp)) {
        // Value can be computed by loading a 16-bit signed value, and
        // then shifting left.
        daddiu(rd, zero_reg, static_cast<int32_t>(tmp));
        if (shift_cnt < 32) {
          dsll(rd, rd, shift_cnt);
        } else {
          dsll32(rd, rd, shift_cnt & 31);
        }
      } else if (rep32_count < 3) {
        // Value being loaded has 32 LSBs equal to the 32 MSBs, and the
        // value loaded into the 32 LSBs can be loaded with a single
        // MIPS instruction.
        LiLower32BitHelper(rd, j);
        Dins(rd, rd, 32, 32);
      } else if (is_int32(tmp)) {
        // Loads with 3 instructions.
        // Value can be computed by loading a 32-bit signed value, and
        // then shifting left.
        lui(rd, tmp >> kLuiShift & kImm16Mask);
        ori(rd, rd, tmp & kImm16Mask);
        if (shift_cnt < 32) {
          dsll(rd, rd, shift_cnt);
        } else {
          dsll32(rd, rd, shift_cnt & 31);
        }
      } else {
        shift_cnt = 16 + base::bits::CountTrailingZeros64(j.immediate() >> 16);
        tmp = j.immediate() >> shift_cnt;
        if (is_uint16(tmp)) {
          // Value can be computed by loading a 16-bit unsigned value,
          // shifting left, and "or"ing in another 16-bit unsigned value.
          ori(rd, zero_reg, tmp & kImm16Mask);
          if (shift_cnt < 32) {
            dsll(rd, rd, shift_cnt);
          } else {
            dsll32(rd, rd, shift_cnt & 31);
          }
          ori(rd, rd, j.immediate() & kImm16Mask);
        } else if (is_int16(tmp)) {
          // Value can be computed by loading a 16-bit signed value,
          // shifting left, and "or"ing in a 16-bit unsigned value.
          daddiu(rd, zero_reg, static_cast<int32_t>(tmp));
          if (shift_cnt < 32) {
            dsll(rd, rd, shift_cnt);
          } else {
            dsll32(rd, rd, shift_cnt & 31);
          }
          ori(rd, rd, j.immediate() & kImm16Mask);
        } else if (rep32_count < 4) {
          // Value being loaded has 32 LSBs equal to the 32 MSBs, and the
          // value in the 32 LSBs requires 2 MIPS instructions to load.
          LiLower32BitHelper(rd, j);
          Dins(rd, rd, 32, 32);
        } else if (kArchVariant == kMips64r6) {
          // Loads with 3-4 instructions.
          // Catch-all case to get any other 64-bit values which aren't
          // handled by special cases above.
          int64_t imm = j.immediate();
          LiLower32BitHelper(rd, j);
          imm = (imm >> 32) + bit31;
          if (imm & kImm16Mask) {
            dahi(rd, imm & kImm16Mask);
          }
          imm = (imm >> 16) + (imm >> 15 & 0x1);
          if (imm & kImm16Mask) {
            dati(rd, imm & kImm16Mask);
          }
        } else {
          if (is_int48(j.immediate())) {
            Operand k = Operand(j.immediate() >> 16);
            LiLower32BitHelper(rd, k);
            dsll(rd, rd, 16);
            if (j.immediate() & kImm16Mask) {
              ori(rd, rd, j.immediate() & kImm16Mask);
            }
          } else {
            Operand k = Operand(j.immediate() >> 32);
            LiLower32BitHelper(rd, k);
            if ((j.immediate() >> 16) & kImm16Mask) {
              dsll(rd, rd, 16);
              ori(rd, rd, (j.immediate() >> 16) & kImm16Mask);
              dsll(rd, rd, 16);
              if (j.immediate() & kImm16Mask) {
                ori(rd, rd, j.immediate() & kImm16Mask);
              }
            } else {
              dsll32(rd, rd, 0);
              if (j.immediate() & kImm16Mask) {
                ori(rd, rd, j.immediate() & kImm16Mask);
              }
            }
          }
        }
      }
    }
  }
}

void MacroAssembler::li(Register rd, Operand j, LiFlags mode) {
  DCHECK(!j.is_reg());
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (!MustUseReg(j.rmode()) && mode == OPTIMIZE_SIZE) {
    int li_count = InstrCountForLi64Bit(j.immediate());
    int li_neg_count = InstrCountForLi64Bit(-j.immediate());
    int li_not_count = InstrCountForLi64Bit(~j.immediate());
    // Loading -MIN_INT64 could cause problems, but loading MIN_INT64 takes only
    // two instructions so no need to check for this.
    if (li_neg_count <= li_not_count && li_neg_count < li_count - 1) {
      DCHECK(j.immediate() != std::numeric_limits<int64_t>::min());
      li_optimized(rd, Operand(-j.immediate()), mode);
      Dsubu(rd, zero_reg, rd);
    } else if (li_neg_count > li_not_count && li_not_count < li_count - 1) {
      DCHECK(j.immediate() != std::numeric_limits<int64_t>::min());
      li_optimized(rd, Operand(~j.immediate()), mode);
      nor(rd, rd, rd);
    } else {
      li_optimized(rd, j, mode);
    }
  } else if (MustUseReg(j.rmode())) {
    int64_t immediate;
    if (j.IsHeapNumberRequest()) {
      RequestHeapNumber(j.heap_number_request());
      immediate = 0;
    } else {
      immediate = j.immediate();
    }

    RecordRelocInfo(j.rmode(), immediate);
    if (RelocInfo::IsWasmCanonicalSigId(j.rmode())) {
      // wasm_canonical_sig_id is 32-bit value.
      DCHECK(is_int32(immediate));
      lui(rd, (immediate >> 16) & kImm16Mask);
      ori(rd, rd, immediate & kImm16Mask);
      return;
    }
    lui(rd, (immediate >> 32) & kImm16Mask);
    ori(rd, rd, (immediate >> 16) & kImm16Mask);
    dsll(rd, rd, 16);
    ori(rd, rd, immediate & kImm16Mask);
  } else if (mode == ADDRESS_LOAD) {
    // We always need the same number of instructions as we may need to patch
    // this code to load another value which may need all 4 instructions.
    lui(rd, (j.immediate() >> 32) & kImm16Mask);
    ori(rd, rd, (j.immediate() >> 16) & kImm16Mask);
    dsll(rd, rd, 16);
    ori(rd, rd, j.immediate() & kImm16Mask);
  } else {  // mode == CONSTANT_SIZE - always emit the same instruction
            // sequence.
    if (kArchVariant == kMips64r6) {
      int64_t imm = j.immediate();
      lui(rd, imm >> kLuiShift & kImm16Mask);
      ori(rd, rd, (imm & kImm16Mask));
      imm = (imm >> 32) + ((imm >> 31) & 0x1);
      dahi(rd, imm & kImm16Mask & kImm16Mask);
      imm = (imm >> 16) + ((imm >> 15) & 0x1);
      dati(rd, imm & kImm16Mask & kImm16Mask);
    } else {
      lui(rd, (j.immediate() >> 48) & kImm16Mask);
      ori(rd, rd, (j.immediate() >> 32) & kImm16Mask);
      dsll(rd, rd, 16);
      ori(rd, rd, (j.immediate() >> 16) & kImm16Mask);
      dsll(rd, rd, 16);
      ori(rd, rd, j.immediate() & kImm16Mask);
    }
  }
}

void MacroAssembler::LoadIsolateField(Register dst, IsolateFieldId id) {
  li(dst, ExternalReference::Create(id));
}

void MacroAssembler::MultiPush(RegList regs) {
  int16_t num_to_push = regs.Count();
  int16_t stack_offset = num_to_push * kPointerSize;

  Dsubu(sp, sp, Operand(stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs.bits() & (1 << i)) != 0) {
      stack_offset -= kPointerSize;
      Sd(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
}

void MacroAssembler::MultiPop(RegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs.bits() & (1 << i)) != 0) {
      Ld(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kPointerSize;
    }
  }
  daddiu(sp, sp, stack_offset);
}

void MacroAssembler::MultiPushFPU(DoubleRegList regs) {
  int16_t num_to_push = regs.Count();
  int16_t stack_offset = num_to_push * kDoubleSize;

  Dsubu(sp, sp, Operand(stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs.bits() & (1 << i)) != 0) {
      stack_offset -= kDoubleSize;
      Sdc1(FPURegister::from_code(i), MemOperand(sp, stack_offset));
    }
  }
}

void MacroAssembler::MultiPopFPU(DoubleRegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs.bits() & (1 << i)) != 0) {
      Ldc1(FPURegister::from_code(i), MemOperand(sp, stack_offset));
      stack_offset += kDoubleSize;
    }
  }
  daddiu(sp, sp, stack_offset);
}

void MacroAssembler::MultiPushMSA(DoubleRegList regs) {
  int16_t num_to_push = regs.Count();
  int16_t stack_offset = num_to_push * kSimd128Size;

  Dsubu(sp, sp, Operand(stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs.bits() & (1 << i)) != 0) {
      stack_offset -= kSimd128Size;
      st_d(MSARegister::from_code(i), MemOperand(sp, stack_offset));
    }
  }
}

void MacroAssembler::MultiPopMSA(DoubleRegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs.bits() & (1 << i)) != 0) {
      ld_d(MSARegister::from_code(i), MemOperand(sp, stack_offset));
      stack_offset += kSimd128Size;
    }
  }
  daddiu(sp, sp, stack_offset);
}

void MacroAssembler::Ext(Register rt, Register rs, uint16_t pos,
                         uint16_t size) {
  DCHECK_LT(pos, 32);
  DCHECK_LT(pos + size, 33);
  ext_(rt, rs, pos, size);
}

void MacroAssembler::Dext(Register rt, Register rs, uint16_t pos,
                          uint16_t size) {
  DCHECK(pos < 64 && 0 < size && size <= 64 && 0 < pos + size &&
         pos + size <= 64);
  if (size > 32) {
    dextm_(rt, rs, pos, size);
  } else if (pos >= 32) {
    dextu_(rt, rs, pos, size);
  } else {
    dext_(rt, rs, pos, size);
  }
}

void MacroAssembler::Ins(Register rt, Register rs, uint16_t pos,
                         uint16_t size) {
  DCHECK_LT(pos, 32);
  DCHECK_LE(pos + size, 32);
  DCHECK_NE(size, 0);
  ins_(rt, rs, pos, size);
}

void MacroAssembler::Dins(Register rt, Register rs, uint16_t pos,
                          uint16_t size) {
  DCHECK(pos < 64 && 0 < size && size <= 64 && 0 < pos + size &&
         pos + size <= 64);
  if (pos + size <= 32) {
    dins_(rt, rs, pos, size);
  } else if (pos < 32) {
    dinsm_(rt, rs, pos, size);
  } else {
    dinsu_(rt, rs, pos, size);
  }
}

void MacroAssembler::ExtractBits(Register dest, Register source, Register pos,
                                 int size, bool sign_extend) {
  dsrav(dest, source, pos);
  Dext(dest, dest, 0, size);
  if (sign_extend) {
    switch (size) {
      case 8:
        seb(dest, dest);
        break;
      case 16:
        seh(dest, dest);
        break;
      case 32:
        // sign-extend word
        sll(dest, dest, 0);
        break;
      default:
        UNREACHABLE();
    }
  }
}

void MacroAssembler::InsertBits(Register dest, Register source, Register pos,
                                int size) {
  Dror(dest, dest, pos);
  Dins(dest, source, 0, size);
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Dsubu(scratch, zero_reg, pos);
    Dror(dest, dest, scratch);
  }
}

void MacroAssembler::Neg_s(FPURegister fd, FPURegister fs) {
  if (kArchVariant == kMips64r6) {
    // r6 neg_s changes the sign for NaN-like operands as well.
    neg_s(fd, fs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Label is_nan, done;
    Register scratch1 = t8;
    Register scratch2 = t9;
    CompareIsNanF32(fs, fs);
    BranchTrueShortF(&is_nan);
    Branch(USE_DELAY_SLOT, &done);
    // For NaN input, neg_s will return the same NaN value,
    // while the sign has to be changed separately.
    neg_s(fd, fs);  // In delay slot.
    bind(&is_nan);
    mfc1(scratch1, fs);
    li(scratch2, kBinary32SignMask);
    Xor(scratch1, scratch1, scratch2);
    mtc1(scratch1, fd);
    bind(&done);
  }
}

void MacroAssembler::Neg_d(FPURegister fd, FPURegister fs) {
  if (kArchVariant == kMips64r6) {
    // r6 neg_d changes the sign for NaN-like operands as well.
    neg_d(fd, fs);
  } else {
    DCHECK_EQ(kArchVariant, kMips64r2);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Label is_nan, done;
    Register scratch1 = t8;
    Register scratch2 = t9;
    CompareIsNanF64(fs, fs);
    BranchTrueShortF(&is_nan);
    Branch(USE_DELAY_SLOT, &done);
    // For NaN input, neg_d will return the same NaN value,
    // while the sign has to be changed separately.
    neg_d(fd, fs);  // In delay slot.
    bind(&is_nan);
    dmfc1(scratch1, fs);
    li(scratch2, base::Double::kSignMask);
    Xor(scratch1, scratch1, scratch2);
    dmtc1(scratch1, fd);
    bind(&done);
  }
}

void MacroAssembler::Cvt_d_uw(FPURegister fd, FPURegister fs) {
  // Move the data from fs to t8.
  BlockTrampolinePoolScope block_trampoline_pool(this);
  mfc1(t8, fs);
  Cvt_d_uw(fd, t8);
}

void MacroAssembler::Cvt_d_uw(FPURegister fd, Register rs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);

  // Convert rs to a FP value in fd.
  DCHECK(rs != t9);
  DCHECK(rs != at);

  // Zero extend int32 in rs.
  Dext(t9, rs, 0, 32);
  dmtc1(t9, fd);
  cvt_d_l(fd, fd);
}

void MacroAssembler::Cvt_d_ul(FPURegister fd, FPURegister fs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Move the data from fs to t8.
  dmfc1(t8, fs);
  Cvt_d_ul(fd, t8);
}

void MacroAssembler::Cvt_d_ul(FPURegister fd, Register rs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Convert rs to a FP value in fd.

  DCHECK(rs != t9);
  DCHECK(rs != at);

  Label msb_clear, conversion_done;

  Branch(&msb_clear, ge, rs, Operand(zero_reg));

  // Rs >= 2^63
  andi(t9, rs, 1);
  dsrl(rs, rs, 1);
  or_(t9, t9, rs);
  dmtc1(t9, fd);
  cvt_d_l(fd, fd);
  Branch(USE_DELAY_SLOT, &conversion_done);
  add_d(fd, fd, fd);  // In delay slot.

  bind(&msb_clear);
  // Rs < 2^63, we can do simple conversion.
  dmtc1(rs, fd);
  cvt_d_l(fd, fd);

  bind(&conversion_done);
}

void MacroAssembler::Cvt_s_uw(FPURegister fd, FPURegister fs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Move the data from fs to t8.
  mfc1(t8, fs);
  Cvt_s_uw(fd, t8);
}

void MacroAssembler::Cvt_s_uw(FPURegister fd, Register rs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Convert rs to a FP value in fd.
  DCHECK(rs != t9);
  DCHECK(rs != at);

  // Zero extend int32 in rs.
  Dext(t9, rs, 0, 32);
  dmtc1(t9, fd);
  cvt_s_l(fd, fd);
}

void MacroAssembler::Cvt_s_ul(FPURegister fd, FPURegister fs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Move the data from fs to t8.
  dmfc1(t8, fs);
  Cvt_s_ul(fd, t8);
}

void MacroAssembler::Cvt_s_ul(FPURegister fd, Register rs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Convert rs to a FP value in fd.

  DCHECK(rs != t9);
  DCHECK(rs != at);

  Label positive, conversion_done;

  Branch(&positive, ge, rs, Operand(zero_reg));

  // Rs >= 2^31.
  andi(t9, rs, 1);
  dsrl(rs, rs, 1);
  or_(t9, t9, rs);
  dmtc1(t9, fd);
  cvt_s_l(fd, fd);
  Branch(USE_DELAY_SLOT, &conversion_done);
  add_s(fd, fd, fd);  // In delay slot.

  bind(&positive);
  // Rs < 2^31, we can do simple conversion.
  dmtc1(rs, fd);
  cvt_s_l(fd, fd);

  bind(&conversion_done);
}

void MacroAssembler::Round_l_d(FPURegister fd, FPURegister fs) {
  round_l_d(fd, fs);
}

void MacroAssembler::Floor_l_d(FPURegister fd, FPURegister fs) {
  floor_l_d(fd, fs);
}

void MacroAssembler::Ceil_l_d(FPURegister fd, FPURegister fs) {
  ceil_l_d(fd, fs);
}

void MacroAssembler::Trunc_l_d(FPURegister fd, FPURegister fs) {
  trunc_l_d(fd, fs);
}

void MacroAssembler::Trunc_l_ud(FPURegister fd, FPURegister fs,
                                FPURegister scratch) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Load to GPR.
  dmfc1(t8, fs);
  // Reset sign bit.
  {
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    li(scratch1, 0x7FFFFFFFFFFFFFFF);
    and_(t8, t8, scratch1);
  }
  dmtc1(t8, fs);
  trunc_l_d(fd, fs);
}

void MacroAssembler::Trunc_uw_d(FPURegister fd, FPURegister fs,
                                FPURegister scratch) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Trunc_uw_d(t8, fs, scratch);
  mtc1(t8, fd);
}

void MacroAssembler::Trunc_uw_s(FPURegister fd, FPURegister fs,
                                FPURegister scratch) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Trunc_uw_s(t8, fs, scratch);
  mtc1(t8, fd);
}

void MacroAssembler::Trunc_ul_d(FPURegister fd, FPURegister fs,
                                FPURegister scratch, Register result) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Trunc_ul_d(t8, fs, scratch, result);
  dmtc1(t8, fd);
}

void MacroAssembler::Trunc_ul_s(FPURegister fd, FPURegister fs,
                                FPURegister scratch, Register result) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Trunc_ul_s(t8, fs, scratch, result);
  dmtc1(t8, fd);
}

void MacroAssembler::Trunc_w_d(FPURegister fd, FPURegister fs) {
  trunc_w_d(fd, fs);
}

void MacroAssembler::Round_w_d(FPURegister fd, FPURegister fs) {
  round_w_d(fd, fs);
}

void MacroAssembler::Floor_w_d(FPURegister fd, FPURegister fs) {
  floor_w_d(fd, fs);
}

void MacroAssembler::Ceil_w_d(FPURegister fd, FPURegister fs) {
  ceil_w_d(fd, fs);
}

void MacroAssembler::Trunc_uw_d(Register rd, FPURegister fs,
                                FPURegister scratch) {
  DCHECK(fs != scratch);
  DCHECK(rd != at);

  {
    // Load 2^32 into scratch as its float representation.
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    li(scratch1, 0x41F00000);
    mtc1(zero_reg, scratch);
    mthc1(scratch1, scratch);
  }
  // Test if scratch > fd.
  // If fd < 2^32 we can convert it normally.
  Label simple_convert;
  CompareF64(ULT, fs, scratch);
  BranchTrueShortF(&simple_convert);

  // If fd > 2^32, the result should be UINT_32_MAX;
  Addu(rd, zero_reg, -1);

  Label done;
  Branch(&done);
  // Simple conversion.
  bind(&simple_convert);
  // Double -> Int64 -> Uint32;
  trunc_l_d(scratch, fs);
  mfc1(rd, scratch);

  bind(&done);
}

void MacroAssembler::Trunc_uw_s(Register rd, FPURegister fs,
                                FPURegister scratch) {
  DCHECK(fs != scratch);
  DCHECK(rd != at);

  {
    // Load 2^32 into scratch as its float representation.
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    li(scratch1, 0x4F800000);
    mtc1(scratch1, scratch);
  }
  // Test if scratch > fs.
  // If fs < 2^32 we can convert it normally.
  Label simple_convert;
  CompareF32(ULT, fs, scratch);
  BranchTrueShortF(&simple_convert);

  // If fd > 2^32, the result should be UINT_32_MAX;
  Addu(rd, zero_reg, -1);

  Label done;
  Branch(&done);
  // Simple conversion.
  bind(&simple_convert);
  // Float -> Int64 -> Uint32;
  trunc_l_s(scratch, fs);
  mfc1(rd, scratch);

  bind(&done);
}

void MacroAssembler::Trunc_ul_d(Register rd, FPURegister fs,
                                FPURegister scratch, Register result) {
  DCHECK(fs != scratch);
  DCHECK(result.is_valid() ? !AreAliased(rd, result, at) : !AreAliased(rd, at));

  Label simple_convert, done, fail;
  if (result.is_valid()) {
    mov(result, zero_reg);
    Move(scratch, -1.0);
    // If fd =< -1 or unordered, then the conversion fails.
    CompareF64(ULE, fs, scratch);
    BranchTrueShortF(&fail);
  }

  // Load 2^63 into scratch as its double representation.
  li(at, 0x43E0000000000000);
  dmtc1(at, scratch);

  // Test if scratch > fs.
  // If fs < 2^63 or unordered, we can convert it normally.
  CompareF64(ULT, fs, scratch);
  BranchTrueShortF(&simple_convert);

  // First we subtract 2^63 from fs, then trunc it to rd
  // and add 2^63 to rd.
  sub_d(scratch, fs, scratch);
  trunc_l_d(scratch, scratch);
  dmfc1(rd, scratch);
  Or(rd, rd, Operand(1UL << 63));
  Branch(&done);

  // Simple conversion.
  bind(&simple_convert);
  trunc_l_d(scratch, fs);
  dmfc1(rd, scratch);

  bind(&done);
  if (result.is_valid()) {
    // Conversion is failed if the result is negative.
    {
      UseScratchRegisterScope temps(this);
      Register scratch1 = temps.Acquire();
      addiu(scratch1, zero_reg, -1);
      dsrl(scratch1, scratch1, 1);  // Load 2^62.
      dmfc1(result, scratch);
      xor_(result, result, scratch1);
    }
    Slt(result, zero_reg, result);
  }

  bind(&fail);
}

void MacroAssembler::Trunc_ul_s(Register rd, FPURegister fs,
                                FPURegister scratch, Register result) {
  DCHECK(fs != scratch);
  DCHECK(result.is_valid() ? !AreAliased(rd, result, at) : !AreAliased(rd, at));

  Label simple_convert, done, fail;
  if (result.is_valid()) {
    mov(result, zero_reg);
    Move(scratch, -1.0f);
    // If fd =< -1 or unordered, then the conversion fails.
    CompareF32(ULE, fs, scratch);
    BranchTrueShortF(&fail);
  }

  {
    // Load 2^63 into scratch as its float representation.
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    li(scratch1, 0x5F000000);
    mtc1(scratch1, scratch);
  }

  // Test if scratch > fs.
  // If fs < 2^63 or unordered, we can convert it normally.
  CompareF32(ULT, fs, scratch);
  BranchTrueShortF(&simple_convert);

  // First we subtract 2^63 from fs, then trunc it to rd
  // and add 2^63 to rd.
  sub_s(scratch, fs, scratch);
  trunc_l_s(scratch, scratch);
  dmfc1(rd, scratch);
  Or(rd, rd, Operand(1UL << 63));
  Branch(&done);

  // Simple conversion.
  bind(&simple_convert);
  trunc_l_s(scratch, fs);
  dmfc1(rd, scratch);

  bind(&done);
  if (result.is_valid()) {
    // Conversion is failed if the result is negative or unordered.
    {
      UseScratchRegisterScope temps(this);
      Register scratch1 = temps.Acquire();
      addiu(scratch1, zero_reg, -1);
      dsrl(scratch1, scratch1, 1);  // Load 2^62.
      dmfc1(result, scratch);
      xor_(result, result, scratch1);
    }
    Slt(result, zero_reg, result);
  }

  bind(&fail);
}

template <typename RoundFunc>
void MacroAssembler::RoundDouble(FPURegister dst, FPURegister src,
                                 FPURoundingMode mode, RoundFunc round) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = t8;
  if (kArchVariant == kMips64r6) {
    cfc1(scratch, FCSR);
    li(at, Operand(mode));
    ctc1(at, FCSR);
    rint_d(dst, src);
    ctc1(scratch, FCSR);
  } else {
    Label done;
    if (!IsDoubleZeroRegSet()) {
      Move(kDoubleRegZero, 0.0);
    }
    mfhc1(scratch, src);
    Ext(at, scratch, HeapNumber::kExponentShift, HeapNumber::kExponentBits);
    Branch(USE_DELAY_SLOT, &done, hs, at,
           Operand(HeapNumber::kExponentBias + HeapNumber::kMantissaBits));
    mov_d(dst, src);

    round(this, dst, src);
    dmfc1(at, dst);
    Branch(USE_DELAY_SLOT, &done, ne, at, Operand(zero_reg));
    cvt_d_l(dst, dst);
    srl(at, scratch, 31);
    sll(at, at, 31);
    mthc1(at, dst);
    bind(&done);
  }
}

void MacroAssembler::Floor_d_d(FPURegister dst, FPURegister src) {
  RoundDouble(dst, src, mode_floor,
              [](MacroAssembler* masm, FPURegister dst, FPURegister src) {
                masm->floor_l_d(dst, src);
              });
}

void MacroAssembler::Ceil_d_d(FPURegister dst, FPURegister src) {
  RoundDouble(dst, src, mode_ceil,
              [](MacroAssembler* masm, FPURegister dst, FPURegister src) {
                masm->ceil_l_d(dst, src);
              });
}

void MacroAssembler::Trunc_d_d(FPURegister dst, FPURegister src) {
  RoundDouble(dst, src, mode_trunc,
              [](MacroAssembler* masm, FPURegister dst, FPURegister src) {
                masm->trunc_l_d(dst, src);
              });
}

void MacroAssembler::Round_d_d(FPURegister dst, FPURegister src) {
  RoundDouble(dst, src, mode_round,
              [](MacroAssembler* masm, FPURegister dst, FPURegister src) {
                masm->round_l_d(dst, src);
              });
}

template <typename RoundFunc>
void MacroAssembler::RoundFloat(FPURegister dst, FPURegister src,
                                FPURoundingMode mode, RoundFunc round) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = t8;
  if (kArchVariant == kMips64r6) {
    cfc1(scratch, FCSR);
    li(at, Operand(mode));
    ctc1(at, FCSR);
    rint_s(dst, src);
    ctc1(scratch, FCSR);
  } else {
    int32_t kFloat32ExponentBias = 127;
    int32_t kFloat32MantissaBits = 23;
    int32_t kFloat32ExponentBits = 8;
    Label done;
    if (!IsDoubleZeroRegSet()) {
      Move(kDoubleRegZero, 0.0);
    }
    mfc1(scratch, src);
    Ext(at, scratch, kFloat32MantissaBits, kFloat32ExponentBits);
    Branch(USE_DELAY_SLOT, &done, hs, at,
           Operand(kFloat32ExponentBias + kFloat32MantissaBits));
    mov_s(dst, src);

    round(this, dst, src);
    mfc1(at, dst);
    Branch(USE_DELAY_SLOT, &done, ne, at, Operand(zero_reg));
    cvt_s_w(dst, dst);
    srl(at, scratch, 31);
    sll(at, at, 31);
    mtc1(at, dst);
    bind(&done);
  }
}

void MacroAssembler::Floor_s_s(FPURegister dst, FPURegister src) {
  RoundFloat(dst, src, mode_floor,
             [](MacroAssembler* masm, FPURegister dst, FPURegister src) {
               masm->floor_w_s(dst, src);
             });
}

void MacroAssembler::Ceil_s_s(FPURegister dst, FPURegister src) {
  RoundFloat(dst, src, mode_ceil,
             [](MacroAssembler* masm, FPURegister dst, FPURegister src) {
               masm->ceil_w_s(dst, src);
             });
}

void MacroAssembler::Trunc_s_s(FPURegister dst, FPURegister src) {
  RoundFloat(dst, src, mode_trunc,
             [](MacroAssembler* masm, FPURegister dst, FPURegister src) {
               masm->trunc_w_s(dst, src);
             });
}

void MacroAssembler::Round_s_s(FPURegister dst, FPURegister src) {
  RoundFloat(dst, src, mode_round,
             [](MacroAssembler* masm, FPURegister dst, FPURegister src) {
               masm->round_w_s(dst, src);
             });
}

void MacroAssembler::LoadLane(MSASize sz, MSARegister dst, uint8_t laneidx,
                              MemOperand src) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  switch (sz) {
    case MSA_B:
      Lbu(scratch, src);
      insert_b(dst, laneidx, scratch);
      break;
    case MSA_H:
      Lhu(scratch, src);
      insert_h(dst, laneidx, scratch);
      break;
    case MSA_W:
      Lwu(scratch, src);
      insert_w(dst, laneidx, scratch);
      break;
    case MSA_D:
      Ld(scratch, src);
      insert_d(dst, laneidx, scratch);
      break;
    default:
      UNREACHABLE();
  }
}

void MacroAssembler::StoreLane(MSASize sz, MSARegister src, uint8_t laneidx,
                               MemOperand dst) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  switch (sz) {
    case MSA_B:
      copy_u_b(scratch, src, laneidx);
      Sb(scratch, dst);
      break;
    case MSA_H:
      copy_u_h(scratch, src, laneidx);
      Sh(scratch, dst);
      break;
    case MSA_W:
      if (laneidx == 0) {
        FPURegister src_reg = FPURegister::from_code(src.code());
        Swc1(src_reg, dst);
      } else {
        copy_u_w(scratch, src, laneidx);
        Sw(scratch, dst);
      }
      break;
    case MSA_D:
      if (laneidx == 0) {
        FPURegister src_reg = FPURegister::from_code(src.code());
        Sdc1(src_reg, dst);
      } else {
        copy_s_d(scratch, src, laneidx);
        Sd(scratch, dst);
      }
      break;
    default:
      UNREACHABLE();
  }
}

#define EXT_MUL_BINOP(type, ilv_instr, dotp_instr)            \
  case type:                                                  \
    xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero); \
    ilv_instr(kSimd128ScratchReg, kSimd128RegZero, src1);     \
    ilv_instr(kSimd128RegZero, kSimd128RegZero, src2);        \
    dotp_instr(dst, kSimd128ScratchReg, kSimd128RegZero);     \
    break;

void MacroAssembler::ExtMulLow(MSADataType type, MSARegister dst,
                               MSARegister src1, MSARegister src2) {
  switch (type) {
    EXT_MUL_BINOP(MSAS8, ilvr_b, dotp_s_h)
    EXT_MUL_BINOP(MSAS16, ilvr_h, dotp_s_w)
    EXT_MUL_BINOP(MSAS32, ilvr_w, dotp_s_d)
    EXT_MUL_BINOP(MSAU8, ilvr_b, dotp_u_h)
    EXT_MUL_BINOP(MSAU16, ilvr_h, dotp_u_w)
    EXT_MUL_BINOP(MSAU32, ilvr_w, dotp_u_d)
    default:
      UNREACHABLE();
  }
}

void MacroAssembler::ExtMulHigh(MSADataType type, MSARegister dst,
                                MSARegister src1, MSARegister src2) {
  switch (type) {
    EXT_MUL_BINOP(MSAS8, ilvl_b, dotp_s_h)
    EXT_MUL_BINOP(MSAS16, ilvl_h, dotp_s_w)
    EXT_MUL_BINOP(MSAS32, ilvl_w, dotp_s_d)
    EXT_MUL_BINOP(MSAU8, ilvl_b, dotp_u_h)
    EXT_MUL_BINOP(MSAU16, ilvl_h, dotp_u_w)
    EXT_MUL_BINOP(MSAU32, ilvl_w, dotp_u_d)
    default:
      UNREACHABLE();
  }
}
#undef EXT_MUL_BINOP

void MacroAssembler::LoadSplat(MSASize sz, MSARegister dst, MemOperand src) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  switch (sz) {
    case MSA_B:
      Lb(scratch, src);
      fill_b(dst, scratch);
      break;
    case MSA_H:
      Lh(scratch, src);
      fill_h(dst, scratch);
      break;
    case MSA_W:
      Lw(scratch, src);
      fill_w(dst, scratch);
      break;
    case MSA_D:
      Ld(scratch, src);
      fill_d(dst, scratch);
      break;
    default:
      UNREACHABLE();
  }
}

void MacroAssembler::ExtAddPairwise(MSADataType type, MSARegister dst,
                                    MSARegister src) {
  switch (type) {
    case MSAS8:
      hadd_s_h(dst, src, src);
      break;
    case MSAU8:
      hadd_u_h(dst, src, src);
      break;
    case MSAS16:
      hadd_s_w(dst, src, src);
      break;
    case MSAU16:
      hadd_u_w(dst, src, src);
      break;
    default:
      UNREACHABLE();
  }
}

void MacroAssembler::MSARoundW(MSARegister dst, MSARegister src,
                               FPURoundingMode mode) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = t8;
  Register scratch2 = at;
  cfcmsa(scratch, MSACSR);
  if (mode == kRoundToNearest) {
    scratch2 = zero_reg;
  } else {
    li(scratch2, Operand(mode));
  }
  ctcmsa(MSACSR, scratch2);
  frint_w(dst, src);
  ctcmsa(MSACSR, scratch);
}

void MacroAssembler::MSARoundD(MSARegister dst, MSARegister src,
                               FPURoundingMode mode) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = t8;
  Register scratch2 = at;
  cfcmsa(scratch, MSACSR);
  if (mode == kRoundToNearest) {
    scratch2 = zero_reg;
  } else {
    li(scratch2, Operand(mode));
  }
  ctcmsa(MSACSR, scratch2);
  frint_d(dst, src);
  ctcmsa(MSACSR, scratch);
}

void MacroAssembler::Madd_s(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft, FPURegister scratch) {
  DCHECK(fr != scratch && fs != scratch && ft != scratch);
  mul_s(scratch, fs, ft);
  add_s(fd, fr, scratch);
}

void MacroAssembler::Madd_d(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft, FPURegister scratch) {
  DCHECK(fr != scratch && fs != scratch && ft != scratch);
  mul_d(scratch, fs, ft);
  add_d(fd, fr, scratch);
}

void MacroAssembler::Msub_s(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft, FPURegister scratch) {
  DCHECK(fr != scratch && fs != scratch && ft != scratch);
  mul_s(scratch, fs, ft);
  sub_s(fd, scratch, fr);
}

void MacroAssembler::Msub_d(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft, FPURegister scratch) {
  DCHECK(fr != scratch && fs != scratch && ft != scratch);
  mul_d(scratch, fs, ft);
  sub_d(fd, scratch, fr);
}

void MacroAssembler::CompareF(SecondaryField sizeField, FPUCondition cc,
                              FPURegister cmp1, FPURegister cmp2) {
  if (kArchVariant == kMips64r6) {
    sizeField = sizeField == D ? L : W;
    DCHECK(cmp1 != kDoubleCompareReg && cmp2 != kDoubleCompareReg);
    cmp(cc, sizeField, kDoubleCompareReg, cmp1, cmp2);
  } else {
    c(cc, sizeField, cmp1, cmp2);
  }
}

void MacroAssembler::CompareIsNanF(SecondaryField sizeField, FPURegister cmp1,
                                   FPURegister cmp2) {
  CompareF(sizeField, UN, cmp1, cmp2);
}

void MacroAssembler::BranchTrueShortF(Label* target, BranchDelaySlot bd) {
  if (kArchVariant == kMips64r6) {
    bc1nez(target, kDoubleCompareReg);
  } else {
    bc1t(target);
  }
  if (bd == PROTECT) {
    nop();
  }
}

void MacroAssembler::BranchFalseShortF(Label* target, BranchDelaySlot bd) {
  if (kArchVariant == kMips64r6) {
    bc1eqz(target, kDoubleCompareReg);
  } else {
    bc1f(target);
  }
  if (bd == PROTECT) {
    nop();
  }
}

void MacroAssembler::BranchTrueF(Label* target, BranchDelaySlot bd) {
  bool long_branch =
      target->is_bound() ? !is_near(target) : is_trampoline_emitted();
  if (long_branch) {
    Label skip;
    BranchFalseShortF(&skip);
    BranchLong(target, bd);
    bind(&skip);
  } else {
    BranchTrueShortF(target, bd);
  }
}

void MacroAssembler::BranchFalseF(Label* target, BranchDelaySlot bd) {
  bool long_branch =
      target->is_bound() ? !is_near(target) : is_trampoline_emitted();
  if (long_branch) {
    Label skip;
    BranchTrueShortF(&skip);
    BranchLong(target, bd);
    bind(&skip);
  } else {
    BranchFalseShortF(target, bd);
  }
}

void MacroAssembler::BranchMSA(Label* target, MSABranchDF df,
                               MSABranchCondition cond, MSARegister wt,
                               BranchDelaySlot bd) {
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);

    if (target) {
      bool long_branch =
          target->is_bound() ? !is_near(target) : is_trampoline_emitted();
      if (long_branch) {
        Label skip;
        MSABranchCondition neg_cond = NegateMSABranchCondition(cond);
        BranchShortMSA(df, &skip, neg_cond, wt, bd);
        BranchLong(target, bd);
        bind(&skip);
      } else {
        BranchShortMSA(df, target, cond, wt, bd);
      }
    }
  }
}

void MacroAssembler::BranchShortMSA(MSABranchDF df, Label* target,
                                    MSABranchCondition cond, MSARegister wt,
                                    BranchDelaySlot bd) {
  if (IsEnabled(MIPS_SIMD)) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    if (target) {
      switch (cond) {
        case all_not_zero:
          switch (df) {
            case MSA_BRANCH_D:
              bnz_d(wt, target);
              break;
            case MSA_BRANCH_W:
              bnz_w(wt, target);
              break;
            case MSA_BRANCH_H:
              bnz_h(wt, target);
              break;
            case MSA_BRANCH_B:
            default:
              bnz_b(wt, target);
          }
          break;
        case one_elem_not_zero:
          bnz_v(wt, target);
          break;
        case one_elem_zero:
          switch (df) {
            case MSA_BRANCH_D:
              bz_d(wt, target);
              break;
            case MSA_BRANCH_W:
              bz_w(wt, target);
              break;
            case MSA_BRANCH_H:
              bz_h(wt, target);
              break;
            case MSA_BRANCH_B:
            default:
              bz_b(wt, target);
          }
          break;
        case all_zero:
          bz_v(wt, target);
          break;
        default:
          UNREACHABLE();
      }
    }
  } else {
    UNREACHABLE();
  }
  if (bd == PROTECT) {
    nop();
  }
}

void MacroAssembler::FmoveLow(FPURegister dst, Register src_low) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  DCHECK(src_low != scratch);
  mfhc1(scratch, dst);
  mtc1(src_low, dst);
  mthc1(scratch, dst);
}

void MacroAssembler::Move(FPURegister dst, uint32_t src) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(static_cast<int32_t>(src)));
  mtc1(scratch, dst);
}

void MacroAssembler::Move(FPURegister dst, uint64_t src) {
  // Handle special values first.
  if (src == base::bit_cast<uint64_t>(0.0) && has_double_zero_reg_set_) {
    mov_d(dst, kDoubleRegZero);
  } else if (src == base::bit_cast<uint64_t>(-0.0) &&
             has_double_zero_reg_set_) {
    Neg_d(dst, kDoubleRegZero);
  } else {
    uint32_t lo = src & 0xFFFFFFFF;
    uint32_t hi = src >> 32;
    // Move the low part of the double into the lower of the corresponding FPU
    // register of FPU register pair.
    if (lo != 0) {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, Operand(lo));
      mtc1(scratch, dst);
    } else {
      mtc1(zero_reg, dst);
    }
    // Move the high part of the double into the higher of the corresponding FPU
    // register of FPU register pair.
    if (hi != 0) {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, Operand(hi));
      mthc1(scratch, dst);
    } else {
      mthc1(zero_reg, dst);
    }
    if (dst == kDoubleRegZero) has_double_zero_reg_set_ = true;
  }
}

void MacroAssembler::Movz(Register rd, Register rs, Register rt) {
  if (kArchVariant == kMips64r6) {
    Label done;
    Branch(&done, ne, rt, Operand(zero_reg));
    mov(rd, rs);
    bind(&done);
  } else {
    movz(rd, rs, rt);
  }
}

void MacroAssembler::Movn(Register rd, Register rs, Register rt) {
  if (kArchVariant == kMips64r6) {
    Label done;
    Branch(&done, eq, rt, Operand(zero_reg));
    mov(rd, rs);
    bind(&done);
  } else {
    movn(rd, rs, rt);
  }
}

void MacroAssembler::LoadZeroIfConditionNotZero(Register dest,
                                                Register condition) {
  if (kArchVariant == kMips64r6) {
    seleqz(dest, dest, condition);
  } else {
    Movn(dest, zero_reg, condition);
  }
}

void MacroAssembler::LoadZeroIfConditionZero(Register dest,
                                             Register condition) {
  if (kArchVariant == kMips64r6) {
    selnez(dest, dest, condition);
  } else {
    Movz(dest, zero_reg, condition);
  }
}

void MacroAssembler::LoadZeroIfFPUCondition(Register dest) {
  if (kArchVariant == kMips64r6) {
    dmfc1(kScratchReg, kDoubleCompareReg);
    LoadZeroIfConditionNotZero(dest, kScratchReg);
  } else {
    Movt(dest, zero_reg);
  }
}

void MacroAssembler::LoadZeroIfNotFPUCondition(Register dest) {
  if (kArchVariant == kMips64r6) {
    dmfc1(kScratchReg, kDoubleCompareReg);
    LoadZeroIfConditionZero(dest, kScratchReg);
  } else {
    Movf(dest, zero_reg);
  }
}

void MacroAssembler::Movt(Register rd, Register rs, uint16_t cc) {
  movt(rd, rs, cc);
}

void MacroAssembler::Movf(Register rd, Register rs, uint16_t cc) {
  movf(rd, rs, cc);
}

void MacroAssembler::Clz(Register rd, Register rs) { clz(rd, rs); }

void MacroAssembler::Dclz(Register rd, Register rs) { dclz(rd, rs); }

void MacroAssembler::Ctz(Register rd, Register rs) {
  if (kArchVariant == kMips64r6) {
    // We don't have an instruction to count the number of trailing zeroes.
    // Start by flipping the bits end-for-end so we can count the number of
    // leading zeroes instead.
    rotr(rd, rs, 16);
    wsbh(rd, rd);
    bitswap(rd, rd);
    Clz(rd, rd);
  } else {
    // Convert trailing zeroes to trailing ones, and bits to their left
    // to zeroes.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Daddu(scratch, rs, -1);
    Xor(rd, scratch, rs);
    And(rd, rd, scratch);
    // Count number of leading zeroes.
    Clz(rd, rd);
    // Subtract number of leading zeroes from 32 to get number of trailing
    // ones. Remember that the trailing ones were formerly trailing zeroes.
    li(scratch, 32);
    Subu(rd, scratch, rd);
  }
}

void MacroAssembler::Dctz(Register rd, Register rs) {
  if (kArchVariant == kMips64r6) {
    // We don't have an instruction to count the number of trailing zeroes.
    // Start by flipping the bits end-for-end so we can count the number of
    // leading zeroes instead.
    dsbh(rd, rs);
    dshd(rd, rd);
    dbitswap(rd, rd);
    dclz(rd, rd);
  } else {
    // Convert trailing zeroes to trailing ones, and bits to their left
    // to zeroes.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Daddu(scratch, rs, -1);
    Xor(rd, scratch, rs);
    And(rd, rd, scratch);
    // Count number of leading zeroes.
    dclz(rd, rd);
    // Subtract number of leading zeroes from 64 to get number of trailing
    // ones. Remember that the trailing ones were formerly trailing zeroes.
    li(scratch, 64);
    Dsubu(rd, scratch, rd);
  }
}

void MacroAssembler::Popcnt(Register rd, Register rs) {
  ASM_CODE_COMMENT(this);
  // https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
  //
  // A generalization of the best bit counting method to integers of
  // bit-widths up to 128 (parameterized by type T) is this:
  //
  // v = v - ((v >> 1) & (T)~(T)0/3);                           // temp
  // v = (v & (T)~(T)0/15*3) + ((v >> 2) & (T)~(T)0/15*3);      // temp
  // v = (v + (v >> 4)) & (T)~(T)0/255*15;                      // temp
  // c = (T)(v * ((T)~(T)0/255)) >> (sizeof(T) - 1) * BITS_PER_BYTE; //count
  //
  // For comparison, for 32-bit quantities, this algorithm can be executed
  // using 20 MIPS instructions (the calls to LoadConst32() generate two
  // machine instructions each for the values being used in this algorithm).
  // A(n unrolled) loop-based algorithm requires 25 instructions.
  //
  // For a 64-bit operand this can be performed in 24 instructions compared
  // to a(n unrolled) loop based algorithm which requires 38 instructions.
  //
  // There are algorithms which are faster in the cases where very few
  // bits are set but the algorithm here attempts to minimize the total
  // number of instructions executed even when a large number of bits
  // are set.
  uint32_t B0 = 0x55555555;     // (T)~(T)0/3
  uint32_t B1 = 0x33333333;     // (T)~(T)0/15*3
  uint32_t B2 = 0x0F0F0F0F;     // (T)~(T)0/255*15
  uint32_t value = 0x01010101;  // (T)~(T)0/255
  uint32_t shift = 24;          // (sizeof(T) - 1) * BITS_PER_BYTE

  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.Acquire();
  Register scratch2 = t8;
  srl(scratch, rs, 1);
  li(scratch2, B0);
  And(scratch, scratch, scratch2);
  Subu(scratch, rs, scratch);
  li(scratch2, B1);
  And(rd, scratch, scratch2);
  srl(scratch, scratch, 2);
  And(scratch, scratch, scratch2);
  Addu(scratch, rd, scratch);
  srl(rd, scratch, 4);
  Addu(rd, rd, scratch);
  li(scratch2, B2);
  And(rd, rd, scratch2);
  li(scratch, value);
  Mul(rd, rd, scratch);
  srl(rd, rd, shift);
}

void MacroAssembler::Dpopcnt(Register rd, Register rs) {
  ASM_CODE_COMMENT(this);
  uint64_t B0 = 0x5555555555555555l;     // (T)~(T)0/3
  uint64_t B1 = 0x3333333333333333l;     // (T)~(T)0/15*3
  uint64_t B2 = 0x0F0F0F0F0F0F0F0Fl;     // (T)~(T)0/255*15
  uint64_t value = 0x0101010101010101l;  // (T)~(T)0/255
  uint64_t shift = 24;                   // (sizeof(T) - 1) * BITS_PER_BYTE

  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.Acquire();
  Register scratch2 = t8;
  dsrl(scratch, rs, 1);
  li(scratch2, B0);
  And(scratch, scratch, scratch2);
  Dsubu(scratch, rs, scratch);
  li(scratch2, B1);
  And(rd, scratch, scratch2);
  dsrl(scratch, scratch, 2);
  And(scratch, scratch, scratch2);
  Daddu(scratch, rd, scratch);
  dsrl(rd, scratch, 4);
  Daddu(rd, rd, scratch);
  li(scratch2, B2);
  And(rd, rd, scratch2);
  li(scratch, value);
  Dmul(rd, rd, scratch);
  dsrl32(rd, rd, shift);
}

void MacroAssembler::TryInlineTruncateDoubleToI(Register result,
                                                DoubleRegister double_input,
                                                Label* done) {
  DoubleRegister single_scratch = kScratchDoubleReg.low();
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = t9;

  // Try a conversion to a signed integer.
  trunc_w_d(single_scratch, double_input);
  mfc1(result, single_scratch);
  // Retrieve the FCSR.
  cfc1(scratch, FCSR);
  // Check for overflow and NaNs.
  And(scratch, scratch,
      kFCSROverflowCauseMask | kFCSRUnderflowCauseMask |
          kFCSRInvalidOpCauseMask);
  // If we had no exceptions we are done.
  Branch(done, eq, scratch, Operand(zero_reg));
}

void MacroAssembler::TruncateDoubleToI(Isolate* isolate, Zone* zone,
                                       Register result,
                                       DoubleRegister double_input,
                                       StubCallMode stub_mode) {
  Label done;

  TryInlineTruncateDoubleToI(result, double_input, &done);

  // If we fell through then inline version didn't succeed - call stub instead.
  push(ra);
  Dsubu(sp, sp, Operand(kDoubleSize));  // Put input on stack.
  Sdc1(double_input, MemOperand(sp, 0));

#if V8_ENABLE_WEBASSEMBLY
  if (stub_mode == StubCallMode::kCallWasmRuntimeStub) {
    Call(static_cast<Address>(Builtin::kDoubleToI), RelocInfo::WASM_STUB_CALL);
#else
  // For balance.
  if (false) {
#endif  // V8_ENABLE_WEBASSEMBLY
  } else {
    CallBuiltin(Builtin::kDoubleToI);
  }
  Ld(result, MemOperand(sp, 0));

  Daddu(sp, sp, Operand(kDoubleSize));
  pop(ra);

  bind(&done);
}

void MacroAssembler::CompareWord(Condition cond, Register dst, Register lhs,
                                 const Operand& rhs) {
  switch (cond) {
    case eq:
    case ne: {
      if (rhs.IsImmediate()) {
        if (rhs.immediate() == 0) {
          if (cond == eq) {
            Sltu(dst, lhs, 1);
          } else {
            Sltu(dst, zero_reg, lhs);
          }
        } else if (is_int16(-rhs.immediate())) {
          Daddu(dst, lhs, Operand(-rhs.immediate()));
          if (cond == eq) {
            Sltu(dst, dst, 1);
          } else {
            Sltu(dst, zero_reg, dst);
          }
        } else {
          Xor(dst, lhs, rhs);
          if (cond == eq) {
            Sltu(dst, dst, 1);
          } else {
            Sltu(dst, zero_reg, dst);
          }
        }
      } else {
        Xor(dst, lhs, rhs);
        if (cond == eq) {
          Sltu(dst, dst, 1);
        } else {
          Sltu(dst, zero_reg, dst);
        }
      }
      break;
    }
    case lt:
      Slt(dst, lhs, rhs);
      break;
    case gt:
      Sgt(dst, lhs, rhs);
      break;
    case le:
      Sle(dst, lhs, rhs);
      break;
    case ge:
      Sge(dst, lhs, rhs);
      break;
    case lo:
      Sltu(dst, lhs, rhs);
      break;
    case hs:
      Sgeu(dst, lhs, rhs);
      break;
    case hi:
      Sgtu(dst, lhs, rhs);
      break;
    case ls:
      Sleu(dst, lhs, rhs);
      break;
    default:
      UNREACHABLE();
  }
}

// Emulated condtional branches do not emit a nop in the branch delay slot.
//
// BRANCH_ARGS_CHECK checks that conditional jump arguments are correct.
#define BRANCH_ARGS_CHECK(cond, rs, rt)                                  \
  DCHECK((cond == cc_always && rs == zero_reg && rt.rm() == zero_reg) || \
         (cond != cc_always && (rs != zero_reg || rt.rm() != zero_reg)))

void MacroAssembler::Branch(int32_t offset, BranchDelaySlot bdslot) {
  DCHECK_EQ(kArchVariant, kMips64r6 ? is_int26(offset) : is_int16(offset));
  BranchShort(offset, bdslot);
}

void MacroAssembler::Branch(int32_t offset, Condition cond, Register rs,
                            const Operand& rt, BranchDelaySlot bdslot) {
  bool is_near = BranchShortCheck(offset, nullptr, cond, rs, rt, bdslot);
  DCHECK(is_near);
  USE(is_near);
}

void MacroAssembler::Branch(Label* L, BranchDelaySlot bdslot) {
  if (L->is_bound()) {
    if (is_near_branch(L)) {
      BranchShort(L, bdslot);
    } else {
      BranchLong(L, bdslot);
    }
  } else {
    if (is_trampoline_emitted()) {
      BranchLong(L, bdslot);
    } else {
      BranchShort(L, bdslot);
    }
  }
}

void MacroAssembler::Branch(Label* L, Condition cond, Register rs,
                            const Operand& rt, BranchDelaySlot bdslot) {
  if (L->is_bound()) {
    if (!BranchShortCheck(0, L, cond, rs, rt, bdslot)) {
      if (cond != cc_always) {
        Label skip;
        Condition neg_cond = NegateCondition(cond);
        BranchShort(&skip, neg_cond, rs, rt);
        BranchLong(L, bdslot);
        bind(&skip);
      } else {
        BranchLong(L, bdslot);
      }
    }
  } else {
    if (is_trampoline_emitted()) {
      if (cond != cc_always) {
        Label skip;
        Condition neg_cond = NegateCondition(cond);
        BranchShort(&skip, neg_cond, rs, rt);
        BranchLong(L, bdslot);
        bind(&skip);
      } else {
        BranchLong(L, bdslot);
      }
    } else {
      BranchShort(L, cond, rs, rt, bdslot);
    }
  }
}

void MacroAssembler::Branch(Label* L, Condition cond, Register rs,
                            RootIndex index, BranchDelaySlot bdslot) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  LoadRoot(scratch, index);
  Branch(L, cond, rs, Operand(scratch), bdslot);
}

void MacroAssembler::BranchShortHelper(int16_t offset, Label* L,
                                       BranchDelaySlot bdslot) {
  DCHECK(L == nullptr || offset == 0);
  offset = GetOffset(offset, L, OffsetSize::kOffset16);
  b(offset);

  // Emit a nop in the branch delay slot if required.
  if (bdslot == PROTECT) nop();
}

void MacroAssembler::BranchShortHelperR6(int32_t offset, Label* L) {
  DCHECK(L == nullptr || offset == 0);
  offset = GetOffset(offset, L, OffsetSize::kOffset26);
  bc(offset);
}

void MacroAssembler::BranchShort(int32_t offset, BranchDelaySlot bdslot) {
  if (kArchVariant == kMips64r6 && bdslot == PROTECT) {
    DCHECK(is_int26(offset));
    BranchShortHelperR6(offset, nullptr);
  } else {
    DCHECK(is_int16(offset));
    BranchShortHelper(offset, nullptr, bdslot);
  }
}

void MacroAssembler::BranchShort(Label* L, BranchDelaySlot bdslot) {
  if (kArchVariant == kMips64r6 && bdslot == PROTECT) {
    BranchShortHelperR6(0, L);
  } else {
    BranchShortHelper(0, L, bdslot);
  }
}

int32_t MacroAssembler::GetOffset(int32_t offset, Label* L, OffsetSize bits) {
  if (L) {
    offset = branch_offset_helper(L, bits) >> 2;
  } else {
    DCHECK(is_intn(offset, bits));
  }
  return offset;
}

Register MacroAssembler::GetRtAsRegisterHelper(const Operand& rt,
                                               Register scratch) {
  Register r2 = no_reg;
  if (rt.is_reg()) {
    r2 = rt.rm();
  } else {
    r2 = scratch;
    li(r2, rt);
  }

  return r2;
}

bool MacroAssembler::CalculateOffset(Label* L, int32_t* offset,
                                     OffsetSize bits) {
  if (!is_near(L, bits)) return false;
  *offset = GetOffset(*offset, L, bits);
  return true;
}

bool MacroAssembler::CalculateOffset(Label* L, int32_t* offset, OffsetSize bits,
                                     Register* scratch, const Operand& rt) {
  if (!is_near(L, bits)) return false;
  *scratch = GetRtAsRegisterHelper(rt, *scratch);
  *offset = GetOffset(*offset, L, bits);
  return true;
}

bool MacroAssembler::BranchShortHelperR6(int32_t offset, Label* L,
                                         Condition cond, Register rs,
                                         const Operand& rt) {
  DCHECK(L == nullptr || offset == 0);
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;

  // Be careful to always use shifted_branch_offset only just before the
  // branch instruction, as the location will be remember for patching the
  // target.
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    switch (cond) {
      case cc_always:
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
        bc(offset);
        break;
      case eq:
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          // Pre R6 beq is used here to make the code patchable. Otherwise bc
          // should be used which has no condition field so is not patchable.
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          beq(rs, scratch, offset);
          nop();
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          beqzc(rs, offset);
        } else {
          // We don't want any other register but scratch clobbered.
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          beqc(rs, scratch, offset);
        }
        break;
      case ne:
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          // Pre R6 bne is used here to make the code patchable. Otherwise we
          // should not generate any instruction.
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          bne(rs, scratch, offset);
          nop();
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          bnezc(rs, offset);
        } else {
          // We don't want any other register but scratch clobbered.
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          bnec(rs, scratch, offset);
        }
        break;

      // Signed comparison.
      case greater:
        // rs > rt
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          break;  // No code needs to be emitted.
        } else if (rs == zero_reg) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          bltzc(scratch, offset);
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16)) return false;
          bgtzc(rs, offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          bltc(scratch, rs, offset);
        }
        break;
      case greater_equal:
        // rs >= rt
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
          bc(offset);
        } else if (rs == zero_reg) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          blezc(scratch, offset);
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16)) return false;
          bgezc(rs, offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          bgec(rs, scratch, offset);
        }
        break;
      case less:
        // rs < rt
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          break;  // No code needs to be emitted.
        } else if (rs == zero_reg) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          bgtzc(scratch, offset);
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16)) return false;
          bltzc(rs, offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          bltc(rs, scratch, offset);
        }
        break;
      case less_equal:
        // rs <= rt
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
          bc(offset);
        } else if (rs == zero_reg) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          bgezc(scratch, offset);
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16)) return false;
          blezc(rs, offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          bgec(scratch, rs, offset);
        }
        break;

      // Unsigned comparison.
      case Ugreater:
        // rs > rt
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          break;  // No code needs to be emitted.
        } else if (rs == zero_reg) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21, &scratch, rt))
            return false;
          bnezc(scratch, offset);
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          bnezc(rs, offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          bltuc(scratch, rs, offset);
        }
        break;
      case Ugreater_equal:
        // rs >= rt
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
          bc(offset);
        } else if (rs == zero_reg) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21, &scratch, rt))
            return false;
          beqzc(scratch, offset);
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
          bc(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          bgeuc(rs, scratch, offset);
        }
        break;
      case Uless:
        // rs < rt
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          break;  // No code needs to be emitted.
        } else if (rs == zero_reg) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21, &scratch, rt))
            return false;
          bnezc(scratch, offset);
        } else if (IsZero(rt)) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          bltuc(rs, scratch, offset);
        }
        break;
      case Uless_equal:
        // rs <= rt
        if (rt.is_reg() && rs.code() == rt.rm().code()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
          bc(offset);
        } else if (rs == zero_reg) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset26, &scratch, rt))
            return false;
          bc(offset);
        } else if (IsZero(rt)) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          beqzc(rs, offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
            return false;
          DCHECK(rs != scratch);
          bgeuc(scratch, rs, offset);
        }
        break;
      default:
        UNREACHABLE();
    }
  }
  CheckTrampolinePoolQuick(1);
  return true;
}

bool MacroAssembler::BranchShortHelper(int16_t offset, Label* L, Condition cond,
                                       Register rs, const Operand& rt,
                                       BranchDelaySlot bdslot) {
  DCHECK(L == nullptr || offset == 0);
  if (!is_near(L, OffsetSize::kOffset16)) return false;

  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
  int32_t offset32;

  // Be careful to always use shifted_branch_offset only just before the
  // branch instruction, as the location will be remember for patching the
  // target.
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    switch (cond) {
      case cc_always:
        offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
        b(offset32);
        break;
      case eq:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          beq(rs, zero_reg, offset32);
        } else {
          // We don't want any other register but scratch clobbered.
          scratch = GetRtAsRegisterHelper(rt, scratch);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          beq(rs, scratch, offset32);
        }
        break;
      case ne:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bne(rs, zero_reg, offset32);
        } else {
          // We don't want any other register but scratch clobbered.
          scratch = GetRtAsRegisterHelper(rt, scratch);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bne(rs, scratch, offset32);
        }
        break;

      // Signed comparison.
      case greater:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bgtz(rs, offset32);
        } else {
          Slt(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bne(scratch, zero_reg, offset32);
        }
        break;
      case greater_equal:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bgez(rs, offset32);
        } else {
          Slt(scratch, rs, rt);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          beq(scratch, zero_reg, offset32);
        }
        break;
      case less:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bltz(rs, offset32);
        } else {
          Slt(scratch, rs, rt);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bne(scratch, zero_reg, offset32);
        }
        break;
      case less_equal:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          blez(rs, offset32);
        } else {
          Slt(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          beq(scratch, zero_reg, offset32);
        }
        break;

      // Unsigned comparison.
      case Ugreater:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bne(rs, zero_reg, offset32);
        } else {
          Sltu(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bne(scratch, zero_reg, offset32);
        }
        break;
      case Ugreater_equal:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          b(offset32);
        } else {
          Sltu(scratch, rs, rt);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          beq(scratch, zero_reg, offset32);
        }
        break;
      case Uless:
        if (IsZero(rt)) {
          return true;  // No code needs to be emitted.
        } else {
          Sltu(scratch, rs, rt);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          bne(scratch, zero_reg, offset32);
        }
        break;
      case Uless_equal:
        if (IsZero(rt)) {
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          beq(rs, zero_reg, offset32);
        } else {
          Sltu(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
          offset32 = GetOffset(offset, L, OffsetSize::kOffset16);
          beq(scratch, zero_reg, offset32);
        }
        break;
      default:
        UNREACHABLE();
    }
  }

  // Emit a nop in the branch delay slot if required.
  if (bdslot == PROTECT) nop();

  return true;
}

bool MacroAssembler::BranchShortCheck(int32_t offset, Label* L, Condition cond,
                                      Register rs, const Operand& rt,
                                      BranchDelaySlot bdslot) {
  BRANCH_ARGS_CHECK(cond, rs, rt);

  if (!L) {
    if (kArchVariant == kMips64r6 && bdslot == PROTECT) {
      DCHECK(is_int26(offset));
      return BranchShortHelperR6(offset, nullptr, cond, rs, rt);
    } else {
      DCHECK(is_int16(offset));
      return BranchShortHelper(offset, nullptr, cond, rs, rt, bdslot);
    }
  } else {
    DCHECK_EQ(offset, 0);
    if (kArchVariant == kMips64r6 && bdslot == PROTECT) {
      return BranchShortHelperR6(0, L, cond, rs, rt);
    } else {
      return BranchShortHelper(0, L, cond, rs, rt, bdslot);
    }
  }
}

void MacroAssembler::BranchShort(int32_t offset, Condition cond, Register rs,
                                 const Operand& rt, BranchDelaySlot bdslot) {
  BranchShortCheck(offset, nullptr, cond, rs, rt, bdslot);
}

void MacroAssembler::BranchShort(Label* L, Condition cond, Register rs,
                                 const Operand& rt, BranchDelaySlot bdslot) {
  BranchShortCheck(0, L, cond, rs, rt, bdslot);
}

void MacroAssembler::BranchAndLink(int32_t offset, BranchDelaySlot bdslot) {
  BranchAndLinkShort(offset, bdslot);
}

void MacroAssembler::BranchAndLink(int32_t offset, Condition cond, Register rs,
                                   const Operand& rt, BranchDelaySlot bdslot) {
  bool is_near = BranchAndLinkShortCheck(offset, nullptr, cond, rs, rt, bdslot);
  DCHECK(is_near);
  USE(is_near);
}

void MacroAssembler::BranchAndLink(Label* L, BranchDelaySlot bdslot) {
  if (L->is_bound()) {
    if (is_near_branch(L)) {
      BranchAndLinkShort(L, bdslot);
    } else {
      BranchAndLinkLong(L, bdslot);
    }
  } else {
    if (is_trampoline_emitted()) {
      BranchAndLinkLong(L, bdslot);
    } else {
      BranchAndLinkShort(L, bdslot);
    }
  }
}

void MacroAssembler::BranchAndLink(Label* L, Condition cond, Register rs,
                                   const Operand& rt, BranchDelaySlot bdslot) {
  if (L->is_bound()) {
    if (!BranchAndLinkShortCheck(0, L, cond, rs, rt, bdslot)) {
      Label skip;
      Condition neg_cond = NegateCondition(cond);
      BranchShort(&skip, neg_cond, rs, rt);
      BranchAndLinkLong(L, bdslot);
      bind(&skip);
    }
  } else {
    if (is_trampoline_emitted()) {
      Label skip;
      Condition neg_cond = NegateCondition(cond);
      BranchShort(&skip, neg_cond, rs, rt);
      BranchAndLinkLong(L, bdslot);
      bind(&skip);
    } else {
      BranchAndLinkShortCheck(0, L, cond, rs, rt, bdslot);
    }
  }
}

void MacroAssembler::BranchAndLinkShortHelper(int16_t offset, Label* L,
                                              BranchDelaySlot bdslot) {
  DCHECK(L == nullptr || offset == 0);
  offset = GetOffset(offset, L, OffsetSize::kOffset16);
  bal(offset);

  // Emit a nop in the branch delay slot if required.
  if (bdslot == PROTECT) nop();
}

void MacroAssembler::BranchAndLinkShortHelperR6(int32_t offset, Label* L) {
  DCHECK(L == nullptr || offset == 0);
  offset = GetOffset(offset, L, OffsetSize::kOffset26);
  balc(offset);
}

void MacroAssembler::BranchAndLinkShort(int32_t offset,
                                        BranchDelaySlot bdslot) {
  if (kArchVariant == kMips64r6 && bdslot == PROTECT) {
    DCHECK(is_int26(offset));
    BranchAndLinkShortHelperR6(offset, nullptr);
  } else {
    DCHECK(is_int16(offset));
    BranchAndLinkShortHelper(offset, nullptr, bdslot);
  }
}

void MacroAssembler::BranchAndLinkShort(Label* L, BranchDelaySlot bdslot) {
  if (kArchVariant == kMips64r6 && bdslot == PROTECT) {
    BranchAndLinkShortHelperR6(0, L);
  } else {
    BranchAndLinkShortHelper(0, L, bdslot);
  }
}

bool MacroAssembler::BranchAndLinkShortHelperR6(int32_t offset, Label* L,
                                                Condition cond, Register rs,
                                                const Operand& rt) {
  DCHECK(L == nullptr || offset == 0);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
  OffsetSize bits = OffsetSize::kOffset16;

  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK((cond == cc_always && is_int26(offset)) || is_int16(offset));
  switch (cond) {
    case cc_always:
      if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
      balc(offset);
      break;
    case eq:
      if (!is_near(L, bits)) return false;
      Subu(scratch, rs, rt);
      offset = GetOffset(offset, L, bits);
      beqzalc(scratch, offset);
      break;
    case ne:
      if (!is_near(L, bits)) return false;
      Subu(scratch, rs, rt);
      offset = GetOffset(offset, L, bits);
      bnezalc(scratch, offset);
      break;

    // Signed comparison.
    case greater:
      // rs > rt
      if (rs.code() == rt.rm().code()) {
        break;  // No code needs to be emitted.
      } else if (rs == zero_reg) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
          return false;
        bltzalc(scratch, offset);
      } else if (IsZero(rt)) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset16)) return false;
        bgtzalc(rs, offset);
      } else {
        if (!is_near(L, bits)) return false;
        Slt(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
        offset = GetOffset(offset, L, bits);
        bnezalc(scratch, offset);
      }
      break;
    case greater_equal:
      // rs >= rt
      if (rs.code() == rt.rm().code()) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
        balc(offset);
      } else if (rs == zero_reg) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
          return false;
        blezalc(scratch, offset);
      } else if (IsZero(rt)) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset16)) return false;
        bgezalc(rs, offset);
      } else {
        if (!is_near(L, bits)) return false;
        Slt(scratch, rs, rt);
        offset = GetOffset(offset, L, bits);
        beqzalc(scratch, offset);
      }
      break;
    case less:
      // rs < rt
      if (rs.code() == rt.rm().code()) {
        break;  // No code needs to be emitted.
      } else if (rs == zero_reg) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
          return false;
        bgtzalc(scratch, offset);
      } else if (IsZero(rt)) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset16)) return false;
        bltzalc(rs, offset);
      } else {
        if (!is_near(L, bits)) return false;
        Slt(scratch, rs, rt);
        offset = GetOffset(offset, L, bits);
        bnezalc(scratch, offset);
      }
      break;
    case less_equal:
      // rs <= r2
      if (rs.code() == rt.rm().code()) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset26)) return false;
        balc(offset);
      } else if (rs == zero_reg) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset16, &scratch, rt))
          return false;
        bgezalc(scratch, offset);
      } else if (IsZero(rt)) {
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset16)) return false;
        blezalc(rs, offset);
      } else {
        if (!is_near(L, bits)) return false;
        Slt(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
        offset = GetOffset(offset, L, bits);
        beqzalc(scratch, offset);
      }
      break;

    // Unsigned comparison.
    case Ugreater:
      // rs > r2
      if (!is_near(L, bits)) return false;
      Sltu(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
      offset = GetOffset(offset, L, bits);
      bnezalc(scratch, offset);
      break;
    case Ugreater_equal:
      // rs >= r2
      if (!is_near(L, bits)) return false;
      Sltu(scratch, rs, rt);
      offset = GetOffset(offset, L, bits);
      beqzalc(scratch, offset);
      break;
    case Uless:
      // rs < r2
      if (!is_near(L, bits)) return false;
      Sltu(scratch, rs, rt);
      offset = GetOffset(offset, L, bits);
      bnezalc(scratch, offset);
      break;
    case Uless_equal:
      // rs <= r2
      if (!is_near(L, bits)) return false;
      Sltu(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
      offset = GetOffset(offset, L, bits);
      beqzalc(scratch, offset);
      break;
    default:
      UNREACHABLE();
  }
  return true;
}

// Pre r6 we need to use a bgezal or bltzal, but they can't be used directly
// with the slt instructions. We could use sub or add instead but we would miss
// overflow cases, so we keep slt and add an intermediate third instruction.
bool MacroAssembler::BranchAndLinkShortHelper(int16_t offset, Label* L,
                                              Condition cond, Register rs,
                                              const Operand& rt,
                                              BranchDelaySlot bdslot) {
  DCHECK(L == nullptr || offset == 0);
  if (!is_near(L, OffsetSize::kOffset16)) return false;

  Register scratch = t8;
  BlockTrampolinePoolScope block_trampoline_pool(this);

  switch (cond) {
    case cc_always:
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bal(offset);
      break;
    case eq:
      bne(rs, GetRtAsRegisterHelper(rt, scratch), 2);
      nop();
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bal(offset);
      break;
    case ne:
      beq(rs, GetRtAsRegisterHelper(rt, scratch), 2);
      nop();
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bal(offset);
      break;

    // Signed comparison.
    case greater:
      Slt(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
      addiu(scratch, scratch, -1);
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bgezal(scratch, offset);
      break;
    case greater_equal:
      Slt(scratch, rs, rt);
      addiu(scratch, scratch, -1);
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bltzal(scratch, offset);
      break;
    case less:
      Slt(scratch, rs, rt);
      addiu(scratch, scratch, -1);
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bgezal(scratch, offset);
      break;
    case less_equal:
      Slt(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
      addiu(scratch, scratch, -1);
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bltzal(scratch, offset);
      break;

    // Unsigned comparison.
    case Ugreater:
      Sltu(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
      addiu(scratch, scratch, -1);
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bgezal(scratch, offset);
      break;
    case Ugreater_equal:
      Sltu(scratch, rs, rt);
      addiu(scratch, scratch, -1);
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bltzal(scratch, offset);
      break;
    case Uless:
      Sltu(scratch, rs, rt);
      addiu(scratch, scratch, -1);
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bgezal(scratch, offset);
      break;
    case Uless_equal:
      Sltu(scratch, GetRtAsRegisterHelper(rt, scratch), rs);
      addiu(scratch, scratch, -1);
      offset = GetOffset(offset, L, OffsetSize::kOffset16);
      bltzal(scratch, offset);
      break;

    default:
      UNREACHABLE();
  }

  // Emit a nop in the branch delay slot if required.
  if (bdslot == PROTECT) nop();

  return true;
}

bool MacroAssembler::BranchAndLinkShortCheck(int32_t offset, Label* L,
                                             Condition cond, Register rs,
                                             const Operand& rt,
                                             BranchDelaySlot bdslot) {
  BRANCH_ARGS_CHECK(cond, rs, rt);

  if (!L) {
    if (kArchVariant == kMips64r6 && bdslot == PROTECT) {
      DCHECK(is_int26(offset));
      return BranchAndLinkShortHelperR6(offset, nullptr, cond, rs, rt);
    } else {
      DCHECK(is_int16(offset));
      return BranchAndLinkShortHelper(offset, nullptr, cond, rs, rt, bdslot);
    }
  } else {
    DCHECK_EQ(offset, 0);
    if (kArchVariant == kMips64r6 && bdslot == PROTECT) {
      return BranchAndLinkShortHelperR6(0, L, cond, rs, rt);
    } else {
      return BranchAndLinkShortHelper(0, L, cond, rs, rt, bdslot);
    }
  }
}

void MacroAssembler::LoadFromConstantsTable(Register destination,
                                            int constant_index) {
  ASM_CODE_COMMENT(this);
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kBuiltinsConstantsTable));
  LoadRoot(destination, RootIndex::kBuiltinsConstantsTable);
  Ld(destination,
     FieldMemOperand(destination,
                     FixedArray::kHeaderSize + constant_index * kPointerSize));
}

void MacroAssembler::LoadRootRelative(Register destination, int32_t offset) {
  Ld(destination, MemOperand(kRootRegister, offset));
}

void MacroAssembler::StoreRootRelative(int32_t offset, Register value) {
  Sd(value, MemOperand(kRootRegister, offset));
}

void MacroAssembler::LoadRootRegisterOffset(Register destination,
                                            intptr_t offset) {
  if (offset == 0) {
    Move(destination, kRootRegister);
  } else {
    Daddu(destination, kRootRegister, Operand(offset));
  }
}

MemOperand MacroAssembler::ExternalReferenceAsOperand(
    ExternalReference reference, Register scratch) {
  if (root_array_available()) {
    if (reference.IsIsolateFieldId()) {
      return MemOperand(kRootRegister, reference.offset_from_root_register());
    }
    if (options().enable_root_relative_access) {
      int64_t offset =
          RootRegisterOffsetForExternalReference(isolate(), reference);
      if (is_int32(offset)) {
        return MemOperand(kRootRegister, static_cast<int32_t>(offset));
      }
    }
    if (root_array_available_ && options().isolate_independent_code) {
      if (IsAddressableThroughRootRegister(isolate(), reference)) {
        // Some external references can be efficiently loaded as an offset from
        // kRootRegister.
        intptr_t offset =
            RootRegisterOffsetForExternalReference(isolate(), reference);
        CHECK(is_int32(offset));
        return MemOperand(kRootRegister, static_cast<int32_t>(offset));
      } else {
        // Otherwise, do a memory load from the external reference table.
        DCHECK(scratch.is_valid());
        Ld(scratch, MemOperand(kRootRegister,
                               RootRegisterOffsetForExternalReferenceTableEntry(
                                   isolate(), reference)));
        return MemOperand(scratch, 0);
      }
    }
  }
  DCHECK(scratch.is_valid());
  li(scratch, reference);
  return MemOperand(scratch, 0);
}

void MacroAssembler::Jump(Register target, Condition cond, Register rs,
                          const Operand& rt, BranchDelaySlot bd) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (kArchVariant == kMips64r6 && bd == PROTECT) {
    if (cond == cc_always) {
      jic(target, 0);
    } else {
      BRANCH_ARGS_CHECK(cond, rs, rt);
      Branch(2, NegateCondition(cond), rs, rt);
      jic(target, 0);
    }
  } else {
    if (cond == cc_always) {
      jr(target);
    } else {
      BRANCH_ARGS_CHECK(cond, rs, rt);
      Branch(2, NegateCondition(cond), rs, rt);
      jr(target);
    }
    // Emit a nop in the branch delay slot if required.
    if (bd == PROTECT) nop();
  }
}

void MacroAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt,
                          BranchDelaySlot bd) {
  Label skip;
  if (cond != cc_always) {
    Branch(USE_DELAY_SLOT, &skip, NegateCondition(cond), rs, rt);
  }
  // The first instruction of 'li' may be placed in the delay slot.
  // This is not an issue, t9 is expected to be clobbered anyway.
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    li(t9, Operand(target, rmode));
    Jump(t9, al, zero_reg, Operand(zero_reg), bd);
    bind(&skip);
  }
}

void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode, Condition cond,
                          Register rs, const Operand& rt, BranchDelaySlot bd) {
  DCHECK(!RelocInfo::IsCodeTarget(rmode));
  Jump(static_cast<intptr_t>(target), rmode, cond, rs, rt, bd);
}

void MacroAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt,
                          BranchDelaySlot bd) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label skip;
  if (cond != cc_always) {
    BranchShort(&skip, NegateCondition(cond), rs, rt);
  }

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    TailCallBuiltin(builtin);
    bind(&skip);
    return;
  }

  Jump(static_cast<intptr_t>(code.address()), rmode, cc_always, rs, rt, bd);
  bind(&skip);
}

void MacroAssembler::Jump(const ExternalReference& reference) {
  li(t9, reference);
  Jump(t9);
}

// Note: To call gcc-compiled C code on mips, you must call through t9.
void MacroAssembler::Call(Register target, Condition cond, Register rs,
                          const Operand& rt, BranchDelaySlot bd) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (kArchVariant == kMips64r6 && bd == PROTECT) {
    if (cond == cc_always) {
      jialc(target, 0);
    } else {
      BRANCH_ARGS_CHECK(cond, rs, rt);
      Branch(2, NegateCondition(cond), rs, rt);
      jialc(target, 0);
    }
  } else {
    if (cond == cc_always) {
      jalr(target);
    } else {
      BRANCH_ARGS_CHECK(cond, rs, rt);
      Branch(2, NegateCondition(cond), rs, rt);
      jalr(target);
    }
    // Emit a nop in the branch delay slot if required.
    if (bd == PROTECT) nop();
  }
  set_pc_for_safepoint();
}

void MacroAssembler::JumpIfIsInRange(Register value, unsigned lower_limit,
                                     unsigned higher_limit,
                                     Label* on_in_range) {
  ASM_CODE_COMMENT(this);
  if (lower_limit != 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Dsubu(scratch, value, Operand(lower_limit));
    Branch(on_in_range, ls, scratch, Operand(higher_limit - lower_limit));
  } else {
    Branch(on_in_range, ls, value, Operand(higher_limit - lower_limit));
  }
}

void MacroAssembler::Call(Address target, RelocInfo::Mode rmode, Condition cond,
                          Register rs, const Operand& rt, BranchDelaySlot bd) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  li(t9, Operand(static_cast<int64_t>(target), rmode), ADDRESS_LOAD);
  Call(t9, cond, rs, rt, bd);
}

void MacroAssembler::Call(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt,
                          BranchDelaySlot bd) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    CallBuiltin(builtin);
    return;
  }
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  Call(code.address(), rmode, cond, rs, rt, bd);
}

void MacroAssembler::LoadEntryFromBuiltinIndex(Register builtin_index,
                                               Register target) {
  ASM_CODE_COMMENT(this);
  static_assert(kSystemPointerSize == 8);
  static_assert(kSmiTagSize == 1);
  static_assert(kSmiTag == 0);

  // The builtin_index register contains the builtin index as a Smi.
  SmiUntag(target, builtin_index);
  Dlsa(target, kRootRegister, target, kSystemPointerSizeLog2);
  Ld(target, MemOperand(target, IsolateData::builtin_entry_table_offset()));
}
void MacroAssembler::LoadEntryFromBuiltin(Builtin builtin,
                                          Register destination) {
  Ld(destination, EntryFromBuiltinAsOperand(builtin));
}
MemOperand MacroAssembler::EntryFromBuiltinAsOperand(Builtin builtin) {
  DCHECK(root_array_available());
  return MemOperand(kRootRegister,
                    IsolateData::BuiltinEntrySlotOffset(builtin));
}

void MacroAssembler::CallBuiltinByIndex(Register builtin_index,
                                        Register target) {
  ASM_CODE_COMMENT(this);
  LoadEntryFromBuiltinIndex(builtin_index, target);
  Call(target);
}
void MacroAssembler::CallBuiltin(Builtin builtin) {
  ASM_CODE_COMMENT_STRING(this, CommentForOffHeapTrampoline("call", builtin));
  Register temp = t9;
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      li(temp, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Call(temp);
      break;
    }
    case BuiltinCallJumpMode::kIndirect: {
      LoadEntryFromBuiltin(builtin, temp);
      Call(temp);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      Handle<Code> code = isolate()->builtins()->code_handle(builtin);
      IndirectLoadConstant(temp, code);
      CallCodeObject(temp, kJSEntrypointTag);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      // Short builtin calls is unsupported in mips64.
      UNREACHABLE();
  }
}

void MacroAssembler::TailCallBuiltin(Builtin builtin, Condition cond,
                                     Register type, Operand range) {
  if (cond != cc_always) {
    Label done;
    Branch(&done, NegateCondition(cond), type, range);
    TailCallBuiltin(builtin);
    bind(&done);
  } else {
    TailCallBuiltin(builtin);
  }
}

void MacroAssembler::TailCallBuiltin(Builtin builtin) {
  ASM_CODE_COMMENT_STRING(this,
                          CommentForOffHeapTrampoline("tail call", builtin));
  Register temp = t9;

  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      li(temp, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Jump(temp);
      break;
    }
    case BuiltinCallJumpMode::kIndirect: {
      LoadEntryFromBuiltin(builtin, temp);
      Jump(temp);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      Handle<Code> code = isolate()->builtins()->code_handle(builtin);
      IndirectLoadConstant(temp, code);
      JumpCodeObject(temp, kJSEntrypointTag);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      UNREACHABLE();
  }
}

void MacroAssembler::PatchAndJump(Address target) {
  if (kArchVariant != kMips64r6) {
    ASM_CODE_COMMENT(this);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    mov(scratch, ra);
    bal(1);                                  // jump to ld
    nop();                                   // in the delay slot
    ld(t9, MemOperand(ra, kInstrSize * 3));  // ra == pc_
    jr(t9);
    mov(ra, scratch);  // in delay slot
    DCHECK_EQ(reinterpret_cast<uint64_t>(pc_) % 8, 0);
    *reinterpret_cast<uint64_t*>(pc_) = target;  // pc_ should be align.
    pc_ += sizeof(uint64_t);
  } else {
    // TODO(mips r6): Implement.
    UNIMPLEMENTED();
  }
}

void MacroAssembler::StoreReturnAddressAndCall(Register target) {
  ASM_CODE_COMMENT(this);
  // This generates the final instruction sequence for calls to C functions
  // once an exit frame has been constructed.
  //
  // Note that this assumes the caller code (i.e. the InstructionStream object
  // currently being generated) is immovable or that the callee function cannot
  // trigger GC, since the callee function will return to it.

  // Compute the return address in lr to return to after the jump below. The pc
  // is already at '+ 8' from the current instruction; but return is after three
  // instructions, so add another 4 to pc to get the return address.

  Assembler::BlockTrampolinePoolScope block_trampoline_pool(this);
  static constexpr int kNumInstructionsToJump = 4;
  Label find_ra;
  // Adjust the value in ra to point to the correct return location, 2nd
  // instruction past the real call into C code (the jalr(t9)), and push it.
  // This is the return address of the exit frame.
  if (kArchVariant >= kMips64r6) {
    addiupc(ra, kNumInstructionsToJump + 1);
  } else {
    // This no-op-and-link sequence saves PC + 8 in ra register on pre-r6 MIPS
    nal();  // nal has branch delay slot.
    Daddu(ra, ra, kNumInstructionsToJump * kInstrSize);
  }
  bind(&find_ra);

  // This spot was reserved in EnterExitFrame.
  Sd(ra, MemOperand(sp));
  // Stack space reservation moved to the branch delay slot below.
  // Stack is still aligned.

  // Call the C routine.
  mov(t9, target);  // Function pointer to t9 to conform to ABI for PIC.
  jalr(t9);
  // Set up sp in the delay slot.
  daddiu(sp, sp, -kCArgsSlotsSize);
  // Make sure the stored 'ra' points to this position.
  DCHECK_EQ(kNumInstructionsToJump, InstructionsGeneratedSince(&find_ra));
}

void MacroAssembler::Ret(Condition cond, Register rs, const Operand& rt,
                         BranchDelaySlot bd) {
  Jump(ra, cond, rs, rt, bd);
}

void MacroAssembler::BranchLong(Label* L, BranchDelaySlot bdslot) {
  if (kArchVariant == kMips64r6 && bdslot == PROTECT &&
      (!L->is_bound() || is_near_r6(L))) {
    BranchShortHelperR6(0, L);
  } else {
    // Generate position independent long branch.
    BlockTrampolinePoolScope block_trampoline_pool(this);
    int64_t imm64 = branch_long_offset(L);
    DCHECK(is_int32(imm64));
    int32_t imm32 = static_cast<int32_t>(imm64);
    or_(t8, ra, zero_reg);
    nal();                                        // Read PC into ra register.
    lui(t9, (imm32 & kHiMaskOf32) >> kLuiShift);  // Branch delay slot.
    ori(t9, t9, (imm32 & kImm16Mask));
    daddu(t9, ra, t9);
    if (bdslot == USE_DELAY_SLOT) {
      or_(ra, t8, zero_reg);
    }
    jr(t9);
    // Emit a or_ in the branch delay slot if it's protected.
    if (bdslot == PROTECT) or_(ra, t8, zero_reg);
  }
}

void MacroAssembler::BranchLong(int32_t offset, BranchDelaySlot bdslot) {
  if (kArchVariant == kMips64r6 && bdslot == PROTECT && (is_int26(offset))) {
    BranchShortHelperR6(offset, nullptr);
  } else {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    or_(t8, ra, zero_reg);
    nal();                                         // Read PC into ra register.
    lui(t9, (offset & kHiMaskOf32) >> kLuiShift);  // Branch delay slot.
    ori(t9, t9, (offset & kImm16Mask));
    daddu(t9, ra, t9);
    if (bdslot == USE_DELAY_SLOT) {
      or_(ra, t8, zero_reg);
    }
    jr(t9);
    // Emit a or_ in the branch delay slot if it's protected.
    if (bdslot == PROTECT) or_(ra, t8, zero_reg);
  }
}

void MacroAssembler::BranchAndLinkLong(Label* L, BranchDelaySlot bdslot) {
  if (kArchVariant == kMips64r6 && bdslot == PROTECT &&
      (!L->is_bound() || is_near_r6(L))) {
    BranchAndLinkShortHelperR6(0, L);
  } else {
    // Generate position independent long branch and link.
    BlockTrampolinePoolScope block_trampoline_pool(this);
    int64_t imm64 = branch_long_offset(L);
    DCHECK(is_int32(imm64));
    int32_t imm32 = static_cast<int32_t>(imm64);
    lui(t8, (imm32 & kHiMaskOf32) >> kLuiShift);
    nal();                              // Read PC into ra register.
    ori(t8, t8, (imm32 & kImm16Mask));  // Branch delay slot.
    daddu(t8, ra, t8);
    jalr(t8);
    // Emit a nop in the branch delay slot if required.
    if (bdslot == PROTECT) nop();
  }
}

void MacroAssembler::DropArguments(Register count) {
  Dlsa(sp, sp, count, kPointerSizeLog2);
}

void MacroAssembler::DropArgumentsAndPushNewReceiver(Register argc,
                                                     Register receiver) {
  DCHECK(!AreAliased(argc, receiver));
  DropArguments(argc);
  push(receiver);
}

void MacroAssembler::DropAndRet(int drop) {
  int32_t drop_size = drop * kSystemPointerSize;
  DCHECK(is_int31(drop_size));

  if (is_int16(drop_size)) {
    Ret(USE_DELAY_SLOT);
    daddiu(sp, sp, drop_size);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, drop_size);
    Ret(USE_DELAY_SLOT);
    daddu(sp, sp, scratch);
  }
}

void MacroAssembler::DropAndRet(int drop, Condition cond, Register r1,
                                const Operand& r2) {
  // Both Drop and Ret need to be conditional.
  Label skip;
  if (cond != cc_always) {
    Branch(&skip, NegateCondition(cond), r1, r2);
  }

  Drop(drop);
  Ret();

  if (cond != cc_always) {
    bind(&skip);
  }
}

void MacroAssembler::Drop(int count, Condition cond, Register reg,
                          const Operand& op) {
  if (count <= 0) {
    return;
  }

  Label skip;

  if (cond != al) {
    Branch(&skip, NegateCondition(cond), reg, op);
  }

  Daddu(sp, sp, Operand(count * kPointerSize));

  if (cond != al) {
    bind(&skip);
  }
}

void MacroAssembler::Swap(Register reg1, Register reg2, Register scratch) {
  if (scratch == no_reg) {
    Xor(reg1, reg1, Operand(reg2));
    Xor(reg2, reg2, Operand(reg1));
    Xor(reg1, reg1, Operand(reg2));
  } else {
    mov(scratch, reg1);
    mov(reg1, reg2);
    mov(reg2, scratch);
  }
}

void MacroAssembler::Call(Label* target) { BranchAndLink(target); }

void MacroAssembler::LoadAddress(Register dst, Label* target) {
  uint64_t address = jump_address(target);
  li(dst, address);
}

void MacroAssembler::LoadAddressPCRelative(Register dst, Label* target) {
  ASM_CODE_COMMENT(this);
  nal();
  // daddiu could handle 16-bit pc offset.
  int32_t offset = branch_offset_helper(target, OffsetSize::kOffset16);
  DCHECK(is_int16(offset));
  mov(t8, ra);
  daddiu(dst, ra, offset);
  mov(ra, t8);
}

void MacroAssembler::Push(Tagged<Smi> smi) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(smi));
  push(scratch);
}

void MacroAssembler::Push(Handle<HeapObject> handle) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(handle));
  push(scratch);
}

void MacroAssembler::PushArray(Register array, Register size, Register scratch,
                               Register scratch2, PushArrayOrder order) {
  DCHECK(!AreAliased(array, size, scratch, scratch2));
  Label loop, entry;
  if (order == PushArrayOrder::kReverse) {
    mov(scratch, zero_reg);
    jmp(&entry);
    bind(&loop);
    Dlsa(scratch2, array, scratch, kPointerSizeLog2);
    Ld(scratch2, MemOperand(scratch2));
    push(scratch2);
    Daddu(scratch, scratch, Operand(1));
    bind(&entry);
    Branch(&loop, less, scratch, Operand(size));
  } else {
    mov(scratch, size);
    jmp(&entry);
    bind(&loop);
    Dlsa(scratch2, array, scratch, kPointerSizeLog2);
    Ld(scratch2, MemOperand(scratch2));
    push(scratch2);
    bind(&entry);
    Daddu(scratch, scratch, Operand(-1));
    Branch(&loop, greater_equal, scratch, Operand(zero_reg));
  }
}

// ---------------------------------------------------------------------------
// Exception handling.

void MacroAssembler::PushStackHandler() {
  // Adjust this code if not the case.
  static_assert(StackHandlerConstants::kSize == 2 * kPointerSize);
  static_assert(StackHandlerConstants::kNextOffset == 0 * kPointerSize);

  Push(Smi::zero());  // Padding.

  // Link the current handler as the next handler.
  li(t2,
     ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  Ld(t1, MemOperand(t2));
  push(t1);

  // Set this new handler as the current one.
  Sd(sp, MemOperand(t2));
}

void MacroAssembler::PopStackHandler() {
  static_assert(StackHandlerConstants::kNextOffset == 0);
  pop(a1);
  Daddu(sp, sp,
        Operand(
            static_cast<int64_t>(StackHandlerConstants::kSize - kPointerSize)));
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch,
     ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  Sd(a1, MemOperand(scratch));
}

void MacroAssembler::FPUCanonicalizeNaN(const DoubleRegister dst,
                                        const DoubleRegister src) {
  sub_d(dst, src, kDoubleRegZero);
}

void MacroAssembler::MovFromFloatResult(const DoubleRegister dst) {
  if (IsMipsSoftFloatABI) {
    if (kArchEndian == kLittle) {
      Move(dst, v0, v1);
    } else {
      Move(dst, v1, v0);
    }
  } else {
    Move(dst, f0);  // Reg f0 is o32 ABI FP return value.
  }
}

void MacroAssembler::MovFromFloatParameter(const DoubleRegister dst) {
  if (IsMipsSoftFloatABI) {
    if (kArchEndian == kLittle) {
      Move(dst, a0, a1);
    } else {
      Move(dst, a1, a0);
    }
  } else {
    Move(dst, f12);  // Reg f12 is n64 ABI FP first argument value.
  }
}

void MacroAssembler::MovToFloatParameter(DoubleRegister src) {
  if (!IsMipsSoftFloatABI) {
    Move(f12, src);
  } else {
    if (kArchEndian == kLittle) {
      Move(a0, a1, src);
    } else {
      Move(a1, a0, src);
    }
  }
}

void MacroAssembler::MovToFloatResult(DoubleRegister src) {
  if (!IsMipsSoftFloatABI) {
    Move(f0, src);
  } else {
    if (kArchEndian == kLittle) {
      Move(v0, v1, src);
    } else {
      Move(v1, v0, src);
    }
  }
}

void MacroAssembler::MovToFloatParameters(DoubleRegister src1,
                                          DoubleRegister src2) {
  if (!IsMipsSoftFloatABI) {
    const DoubleRegister fparg2 = f13;
    if (src2 == f12) {
      DCHECK(src1 != fparg2);
      Move(fparg2, src2);
      Move(f12, src1);
    } else {
      Move(f12, src1);
      Move(fparg2, src2);
    }
  } else {
    if (kArchEndian == kLittle) {
      Move(a0, a1, src1);
      Move(a2, a3, src2);
    } else {
      Move(a1, a0, src1);
      Move(a3, a2, src2);
    }
  }
}

// -----------------------------------------------------------------------------
// JavaScript invokes.

void MacroAssembler::LoadStackLimit(Register destination, StackLimitKind kind) {
  ASM_CODE_COMMENT(this);
  DCHECK(root_array_available());
  intptr_t offset = kind == StackLimitKind::kRealStackLimit
                        ? IsolateData::real_jslimit_offset()
                        : IsolateData::jslimit_offset();

  Ld(destination, MemOperand(kRootRegister, static_cast<int32_t>(offset)));
}

void MacroAssembler::StackOverflowCheck(Register num_args, Register scratch1,
                                        Register scratch2,
                                        Label* stack_overflow) {
  ASM_CODE_COMMENT(this);
  // Check the stack for overflow. We are not trying to catch
  // interruptions (e.g. debug break and preemption) here, so the "real stack
  // limit" is checked.

  LoadStackLimit(scratch1, StackLimitKind::kRealStackLimit);
  // Make scratch1 the space we have left. The stack might already be overflowed
  // here which will cause scratch1 to become negative.
  dsubu(scratch1, sp, scratch1);
  // Check if the arguments will overflow the stack.
  dsll(scratch2, num_args, kPointerSizeLog2);
  // Signed comparison.
  Branch(stack_overflow, le, scratch1, Operand(scratch2));
}

void MacroAssembler::TestCodeIsMarkedForDeoptimizationAndJump(
    Register code_data_container, Register scratch, Condition cond,
    Label* target) {
  Lwu(scratch, FieldMemOperand(code_data_container, Code::kFlagsOffset));
  And(scratch, scratch, Operand(1 << Code::kMarkedForDeoptimizationBit));
  Branch(target, cond, scratch, Operand(zero_reg));
}

Operand MacroAssembler::ClearedValue() const {
  return Operand(static_cast<int32_t>(i::ClearedValue(isolate()).ptr()));
}

void MacroAssembler::InvokePrologue(Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    Label* done, InvokeType type) {
  ASM_CODE_COMMENT(this);
  Label regular_invoke;

  //  a0: actual arguments count
  //  a1: function (passed through to callee)
  //  a2: expected arguments count

  DCHECK_EQ(actual_parameter_count, a0);
  DCHECK_EQ(expected_parameter_count, a2);

  // If overapplication or if the actual argument count is equal to the
  // formal parameter count, no need to push extra undefined values.
  Dsubu(expected_parameter_count, expected_parameter_count,
        actual_parameter_count);
  Branch(&regular_invoke, le, expected_parameter_count, Operand(zero_reg));

  Label stack_overflow;
  StackOverflowCheck(expected_parameter_count, t0, t1, &stack_overflow);
  // Underapplication. Move the arguments already in the stack, including the
  // receiver and the return address.
  {
    Label copy;
    Register src = a6, dest = a7;
    mov(src, sp);
    dsll(t0, expected_parameter_count, kSystemPointerSizeLog2);
    Dsubu(sp, sp, Operand(t0));
    // Update stack pointer.
    mov(dest, sp);
    mov(t0, actual_parameter_count);
    bind(&copy);
    Ld(t1, MemOperand(src, 0));
    Sd(t1, MemOperand(dest, 0));
    Dsubu(t0, t0, Operand(1));
    Daddu(src, src, Operand(kSystemPointerSize));
    Daddu(dest, dest, Operand(kSystemPointerSize));
    Branch(&copy, gt, t0, Operand(zero_reg));
  }

  // Fill remaining expected arguments with undefined values.
  LoadRoot(t0, RootIndex::kUndefinedValue);
  {
    Label loop;
    bind(&loop);
    Sd(t0, MemOperand(a7, 0));
    Dsubu(expected_parameter_count, expected_parameter_count, Operand(1));
    Daddu(a7, a7, Operand(kSystemPointerSize));
    Branch(&loop, gt, expected_parameter_count, Operand(zero_reg));
  }
  b(&regular_invoke);
  nop();

  bind(&stack_overflow);
  {
    FrameScope frame(
        this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);
    CallRuntime(Runtime::kThrowStackOverflow);
    break_(0xCC);
  }

  bind(&regular_invoke);
}

void MacroAssembler::CheckDebugHook(Register fun, Register new_target,
                                    Register expected_parameter_count,
                                    Register actual_parameter_count) {
  Label skip_hook;

  li(t0, ExternalReference::debug_hook_on_function_call_address(isolate()));
  Lb(t0, MemOperand(t0));
  Branch(&skip_hook, eq, t0, Operand(zero_reg));

  {
    // Load receiver to pass it later to DebugOnFunctionCall hook.
    LoadReceiver(t0);

    FrameScope frame(
        this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);
    SmiTag(expected_parameter_count);
    Push(expected_parameter_count);

    SmiTag(actual_parameter_count);
    Push(actual_parameter_count);

    if (new_target.is_valid()) {
      Push(new_target);
    }
    Push(fun);
    Push(fun);
    Push(t0);
    CallRuntime(Runtime::kDebugOnFunctionCall);
    Pop(fun);
    if (new_target.is_valid()) {
      Pop(new_target);
    }

    Pop(actual_parameter_count);
    SmiUntag(actual_parameter_count);

    Pop(expected_parameter_count);
    SmiUntag(expected_parameter_count);
  }
  bind(&skip_hook);
}

void MacroAssembler::InvokeFunctionCode(Register function, Register new_target,
                                        Register expected_parameter_count,
                                        Register actual_parameter_count,
                                        InvokeType type) {
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());
  DCHECK_EQ(function, a1);
  DCHECK_IMPLIES(new_target.is_valid(), new_target == a3);

  // On function call, call into the debugger if necessary.
  CheckDebugHook(function, new_target, expected_parameter_count,
                 actual_parameter_count);

  // Clear the new.target register if not given.
  if (!new_target.is_valid()) {
    LoadRoot(a3, RootIndex::kUndefinedValue);
  }

  Label done;
  InvokePrologue(expected_parameter_count, actual_parameter_count, &done, type);
  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  switch (type) {
    case InvokeType::kCall:
      CallJSFunction(function);
      break;
    case InvokeType::kJump:
      JumpJSFunction(function);
      break;
  }

  // Continue here if InvokePrologue does handle the invocation due to
  // mismatched parameter counts.
  bind(&done);
}

void MacroAssembler::InvokeFunctionWithNewTarget(
    Register function, Register new_target, Register actual_parameter_count,
    InvokeType type) {
  ASM_CODE_COMMENT(this);
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());

  // Contract with called JS functions requires that function is passed in a1.
  DCHECK_EQ(function, a1);
  Register expected_parameter_count = a2;
  Register temp_reg = t0;
  Ld(temp_reg, FieldMemOperand(a1, JSFunction::kSharedFunctionInfoOffset));
  Ld(cp, FieldMemOperand(a1, JSFunction::kContextOffset));
  // The argument count is stored as uint16_t
  Lhu(expected_parameter_count,
      FieldMemOperand(temp_reg,
                      SharedFunctionInfo::kFormalParameterCountOffset));

  InvokeFunctionCode(a1, new_target, expected_parameter_count,
                     actual_parameter_count, type);
}

void MacroAssembler::InvokeFunction(Register function,
                                    Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    InvokeType type) {
  ASM_CODE_COMMENT(this);
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());

  // Contract with called JS functions requires that function is passed in a1.
  DCHECK_EQ(function, a1);

  // Get the function and setup the context.
  Ld(cp, FieldMemOperand(a1, JSFunction::kContextOffset));

  InvokeFunctionCode(a1, no_reg, expected_parameter_count,
                     actual_parameter_count, type);
}

// ---------------------------------------------------------------------------
// Support functions.

void MacroAssembler::GetObjectType(Register object, Register map,
                                   Register type_reg) {
  LoadMap(map, object);
  Lhu(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
}

void MacroAssembler::GetInstanceTypeRange(Register map, Register type_reg,
                                          InstanceType lower_limit,
                                          Register range) {
  Lhu(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  Dsubu(range, type_reg, Operand(lower_limit));
}

// -----------------------------------------------------------------------------
// Runtime calls.

void MacroAssembler::DaddOverflow(Register dst, Register left,
                                  const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = t8;
  if (!right.is_reg()) {
    li(at, Operand(right));
    right_reg = at;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch && right_reg != scratch && dst != scratch &&
         overflow != scratch);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    daddu(scratch, left, right_reg);
    xor_(overflow, scratch, left);
    xor_(at, scratch, right_reg);
    and_(overflow, overflow, at);
    mov(dst, scratch);
  } else {
    daddu(dst, left, right_reg);
    xor_(overflow, dst, left);
    xor_(at, dst, right_reg);
    and_(overflow, overflow, at);
  }
}

void MacroAssembler::DsubOverflow(Register dst, Register left,
                                  const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = t8;
  if (!right.is_reg()) {
    li(at, Operand(right));
    right_reg = at;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch && right_reg != scratch && dst != scratch &&
         overflow != scratch);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    dsubu(scratch, left, right_reg);
    xor_(overflow, left, scratch);
    xor_(at, left, right_reg);
    and_(overflow, overflow, at);
    mov(dst, scratch);
  } else {
    dsubu(dst, left, right_reg);
    xor_(overflow, left, dst);
    xor_(at, left, right_reg);
    and_(overflow, overflow, at);
  }
}

void MacroAssembler::MulOverflow(Register dst, Register left,
                                 const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = t8;
  if (!right.is_reg()) {
    li(at, Operand(right));
    right_reg = at;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch && right_reg != scratch && dst != scratch &&
         overflow != scratch);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    Mul(scratch, left, right_reg);
    Mulh(overflow, left, right_reg);
    mov(dst, scratch);
  } else {
    Mul(dst, left, right_reg);
    Mulh(overflow, left, right_reg);
  }

  dsra32(scratch, dst, 0);
  xor_(overflow, overflow, scratch);
}

void MacroAssembler::DMulOverflow(Register dst, Register left,
                                  const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = t8;
  if (!right.is_reg()) {
    li(at, Operand(right));
    right_reg = at;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch && right_reg != scratch && dst != scratch &&
         overflow != scratch);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    Dmul(scratch, left, right_reg);
    Dmulh(overflow, left, right_reg);
    mov(dst, scratch);
  } else {
    Dmul(dst, left, right_reg);
    Dmulh(overflow, left, right_reg);
  }

  dsra32(scratch, dst, 31);
  xor_(overflow, overflow, scratch);
}

void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments) {
  ASM_CODE_COMMENT(this);
  // All parameters are on the stack. v0 has the return value after call.

  // If the expected number of arguments of the runtime function is
  // constant, we check that the actual number of arguments match the
  // expectation.
  CHECK(f->nargs < 0 || f->nargs == num_arguments);

  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  PrepareCEntryArgs(num_arguments);
  PrepareCEntryFunction(ExternalReference::Create(f));
  CallBuiltin(Builtins::RuntimeCEntry(f->result_size));
}

void MacroAssembler::TailCallRuntime(Runtime::FunctionId fid) {
  ASM_CODE_COMMENT(this);
  const Runtime::Function* function = Runtime::FunctionForId(fid);
  DCHECK_EQ(1, function->result_size);
  if (function->nargs >= 0) {
    PrepareCEntryArgs(function->nargs);
  }
  JumpToExternalReference(ExternalReference::Create(fid));
}

void MacroAssembler::JumpToExternalReference(const ExternalReference& builtin,
                                             bool builtin_exit_frame) {
  PrepareCEntryFunction(builtin);
  TailCallBuiltin(Builtins::CEntry(1, ArgvMode::kStack, builtin_exit_frame));
}

void MacroAssembler::LoadWeakValue(Register out, Register in,
                                   Label* target_if_cleared) {
  Branch(target_if_cleared, eq, in, Operand(kClearedWeakHeapObjectLower32));

  And(out, in, Operand(~kWeakHeapObjectMask));
}

void MacroAssembler::EmitIncrementCounter(StatsCounter* counter, int value,
                                          Register scratch1,
                                          Register scratch2) {
  DCHECK_GT(value, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    ASM_CODE_COMMENT(this);
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t dummy_stats_counter_
    // field.
    li(scratch2, ExternalReference::Create(counter));
    Lw(scratch1, MemOperand(scratch2));
    Addu(scratch1, scratch1, Operand(value));
    Sw(scratch1, MemOperand(scratch2));
  }
}

void MacroAssembler::EmitDecrementCounter(StatsCounter* counter, int value,
                                          Register scratch1,
                                          Register scratch2) {
  DCHECK_GT(value, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    ASM_CODE_COMMENT(this);
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t dummy_stats_counter_
    // field.
    li(scratch2, ExternalReference::Create(counter));
    Lw(scratch1, MemOperand(scratch2));
    Subu(scratch1, scratch1, Operand(value));
    Sw(scratch1, MemOperand(scratch2));
  }
}

// -----------------------------------------------------------------------------
// Debugging.

void MacroAssembler::Trap() { stop(); }
void MacroAssembler::DebugBreak() { stop(); }

void MacroAssembler::Check(Condition cc, AbortReason reason, Register rs,
                           Operand rt) {
  Label L;
  Branch(&L, cc, rs, rt);
  Abort(reason);
  // Will not return here.
  bind(&L);
}

void MacroAssembler::Abort(AbortReason reason) {
  Label abort_start;
  bind(&abort_start);
  if (v8_flags.code_comments) {
    const char* msg = GetAbortReason(reason);
    RecordComment("Abort message: ");
    RecordComment(msg);
  }

  // Avoid emitting call to builtin if requested.
  if (trap_on_abort()) {
    stop();
    return;
  }

  if (should_abort_hard()) {
    // We don't care if we constructed a frame. Just pretend we did.
    FrameScope assume_frame(this, StackFrame::NO_FRAME_TYPE);
    PrepareCallCFunction(1, a0);
    li(a0, Operand(static_cast<int>(reason)));
    li(a1, ExternalReference::abort_with_reason());
    // Use Call directly to avoid any unneeded overhead. The function won't
    // return anyway.
    Call(a1);
    return;
  }

  Move(a0, Smi::FromInt(static_cast<int>(reason)));

  {
    // We don't actually want to generate a pile of code for this, so just
    // claim there is a stack frame, without generating one.
    FrameScope scope(this, StackFrame::NO_FRAME_TYPE);
    if (root_array_available()) {
      // Generate an indirect call via builtins entry table here in order to
      // ensure that the interpreter_entry_return_pc_offset is the same for
      // InterpreterEntryTrampoline and InterpreterEntryTrampolineForProfiling
      // when v8_flags.debug_code is enabled.
      LoadEntryFromBuiltin(Builtin::kAbort, t9);
      Call(t9);
    } else {
      CallBuiltin(Builtin::kAbort);
    }
  }
  // Will not return here.
  if (is_trampoline_pool_blocked()) {
    // If the calling code cares about the exact number of
    // instructions generated, we insert padding here to keep the size
    // of the Abort macro constant.
    // Currently in debug mode with debug_code enabled the number of
    // generated instructions is 10, so we use this as a maximum value.
    static const int kExpectedAbortInstructions = 10;
    int abort_instructions = InstructionsGeneratedSince(&abort_start);
    DCHECK_LE(abort_instructions, kExpectedAbortInstructions);
    while (abort_instructions++ < kExpectedAbortInstructions) {
      nop();
    }
  }
}

void MacroAssembler::LoadMap(Register destination, Register object) {
  Ld(destination, FieldMemOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadFeedbackVector(Register dst, Register closure,
                                        Register scratch, Label* fbv_undef) {
  Label done;
  // Load the feedback vector from the closure.
  Ld(dst, FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  Ld(dst, FieldMemOperand(dst, FeedbackCell::kValueOffset));

  // Check if feedback vector is valid.
  Ld(scratch, FieldMemOperand(dst, HeapObject::kMapOffset));
  Lhu(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  Branch(&done, eq, scratch, Operand(FEEDBACK_VECTOR_TYPE));

  // Not valid, load undefined.
  LoadRoot(dst, RootIndex::kUndefinedValue);
  Branch(fbv_undef);

  bind(&done);
}

void MacroAssembler::LoadNativeContextSlot(Register dst, int index) {
  LoadMap(dst, cp);
  Ld(dst,
     FieldMemOperand(dst, Map::kConstructorOrBackPointerOrNativeContextOffset));
  Ld(dst, MemOperand(dst, Context::SlotOffset(index)));
}

void MacroAssembler::StubPrologue(StackFrame::Type type) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(StackFrame::TypeToMarker(type)));
  PushCommonFrame(scratch);
}

void MacroAssembler::Prologue() { PushStandardFrame(a1); }

void MacroAssembler::EnterFrame(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Push(ra, fp);
  Move(fp, sp);
  if (!StackFrame::IsJavaScript(type)) {
    li(kScratchReg, Operand(StackFrame::TypeToMarker(type)));
    Push(kScratchReg);
  }
#if V8_ENABLE_WEBASSEMBLY
  if (type == StackFrame::WASM || type == StackFrame::WASM_LIFTOFF_SETUP)
    Push(kWasmInstanceRegister);
#endif  // V8_ENABLE_WEBASSEMBLY
}

void MacroAssembler::LeaveFrame(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  daddiu(sp, fp, 2 * kPointerSize);
  Ld(ra, MemOperand(fp, 1 * kPointerSize));
  Ld(fp, MemOperand(fp, 0 * kPointerSize));
}

void MacroAssembler::EnterExitFrame(Register scratch, int stack_space,
                                    StackFrame::Type frame_type) {
  ASM_CODE_COMMENT(this);
  DCHECK(frame_type == StackFrame::EXIT ||
         frame_type == StackFrame::BUILTIN_EXIT ||
         frame_type == StackFrame::API_ACCESSOR_EXIT ||
         frame_type == StackFrame::API_CALLBACK_EXIT);

  using ER = ExternalReference;

  // Set up the frame structure on the stack.
  static_assert(2 * kPointerSize == ExitFrameConstants::kCallerSPDisplacement);
  static_assert(1 * kPointerSize == ExitFrameConstants::kCallerPCOffset);
  static_assert(0 * kPointerSize == ExitFrameConstants::kCallerFPOffset);

  // This is how the stack will look:
  // fp + 2 (==kCallerSPDisplacement) - old stack's end
  // [fp + 1 (==kCallerPCOffset)] - saved old ra
  // [fp + 0 (==kCallerFPOffset)] - saved old fp
  // [fp - 1 frame_type Smi
  // [fp - 2 (==kSPOffset)] - sp of the called function
  // fp - (2 + stack_space + alignment) == sp == [fp - kSPOffset] - top of the
  //   new stack (will contain saved ra)

  // Save registers and reserve room for saved entry sp.
  daddiu(sp, sp, -2 * kPointerSize - ExitFrameConstants::kFixedFrameSizeFromFp);
  Sd(ra, MemOperand(sp, 3 * kPointerSize));
  Sd(fp, MemOperand(sp, 2 * kPointerSize));
  li(scratch, Operand(StackFrame::TypeToMarker(frame_type)));
  Sd(scratch, MemOperand(sp, 1 * kPointerSize));

  // Set up new frame pointer.
  daddiu(fp, sp, ExitFrameConstants::kFixedFrameSizeFromFp);

  if (v8_flags.debug_code) {
    Sd(zero_reg, MemOperand(fp, ExitFrameConstants::kSPOffset));
  }

  // Save the frame pointer and the context in top.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  Sd(fp, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));

  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  Sd(cp, ExternalReferenceAsOperand(context_address, no_reg));

  const int frame_alignment = MacroAssembler::ActivationFrameAlignment();

  // Reserve place for the return address, stack space and align the frame
  // preparing for calling the runtime function.
  DCHECK_GE(stack_space, 0);
  Dsubu(sp, sp, Operand((stack_space + 1) * kPointerSize));
  if (frame_alignment > 0) {
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    And(sp, sp, Operand(-frame_alignment));  // Align stack.
  }

  // Set the exit frame sp value to point just before the return address
  // location.
  daddiu(scratch, sp, kPointerSize);
  Sd(scratch, MemOperand(fp, ExitFrameConstants::kSPOffset));
}

void MacroAssembler::LeaveExitFrame(Register scratch) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);

  using ER = ExternalReference;

  // Restore current context from top and clear it in debug mode.
  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  Ld(cp, ExternalReferenceAsOperand(context_address, no_reg));

  if (v8_flags.debug_code) {
    li(scratch, Operand(Context::kInvalidContext));
    Sd(scratch, ExternalReferenceAsOperand(context_address, no_reg));
  }

  // Clear the top frame.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  Sd(zero_reg, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));

  // Pop the arguments, restore registers, and return.
  mov(sp, fp);  // Respect ABI stack constraint.
  Ld(fp, MemOperand(sp, ExitFrameConstants::kCallerFPOffset));
  Ld(ra, MemOperand(sp, ExitFrameConstants::kCallerPCOffset));

  daddiu(sp, sp, 2 * kPointerSize);
}

int MacroAssembler::ActivationFrameAlignment() {
#if V8_HOST_ARCH_MIPS || V8_HOST_ARCH_MIPS64
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one Mips
  // platform for another Mips platform with a different alignment.
  return base::OS::ActivationFrameAlignment();
#else   // V8_HOST_ARCH_MIPS
  // If we are using the simulator then we should always align to the expected
  // alignment. As the simulator is used to generate snapshots we do not know
  // if the target platform will need alignment, so this is controlled from a
  // flag.
  return v8_flags.sim_stack_alignment;
#endif  // V8_HOST_ARCH_MIPS
}

void MacroAssembler::SmiUntag(Register dst, const MemOperand& src) {
  if (SmiValuesAre32Bits()) {
    Lw(dst, MemOperand(src.rm(), SmiWordOffset(src.offset())));
  } else {
    DCHECK(SmiValuesAre31Bits());
    Lw(dst, src);
    SmiUntag(dst);
  }
}

void MacroAssembler::JumpIfSmi(Register value, Label* smi_label,
                               BranchDelaySlot bd) {
  DCHECK_EQ(0, kSmiTag);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  andi(scratch, value, kSmiTagMask);
  Branch(bd, smi_label, eq, scratch, Operand(zero_reg));
}

void MacroAssembler::JumpIfNotSmi(Register value, Label* not_smi_label,
                                  BranchDelaySlot bd) {
  DCHECK_EQ(0, kSmiTag);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  andi(scratch, value, kSmiTagMask);
  Branch(bd, not_smi_label, ne, scratch, Operand(zero_reg));
}

#ifdef V8_ENABLE_DEBUG_CODE

void MacroAssembler::Assert(Condition cc, AbortReason reason, Register rs,
                            Operand rt) {
  if (v8_flags.debug_code) Check(cc, reason, rs, rt);
}

void MacroAssembler::AssertJSAny(Register object, Register map_tmp,
                                 Register tmp, AbortReason abort_reason) {
  if (!v8_flags.debug_code) return;

  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, map_tmp, tmp));
  Label ok;

  JumpIfSmi(object, &ok);

  GetObjectType(object, map_tmp, tmp);

  Branch(&ok, kUnsignedLessThanEqual, tmp, Operand(LAST_NAME_TYPE));

  Branch(&ok, kUnsignedGreaterThanEqual, tmp, Operand(FIRST_JS_RECEIVER_TYPE));

  Branch(&ok, kEqual, map_tmp, RootIndex::kHeapNumberMap);

  Branch(&ok, kEqual, map_tmp, RootIndex::kBigIntMap);

  Branch(&ok, kEqual, object, RootIndex::kUndefinedValue);

  Branch(&ok, kEqual, object, RootIndex::kTrueValue);

  Branch(&ok, kEqual, object, RootIndex::kFalseValue);

  Branch(&ok, kEqual, object, RootIndex::kNullValue);

  Abort(abort_reason);
  bind(&ok);
}

void MacroAssembler::AssertNotSmi(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    static_assert(kSmiTag == 0);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    andi(scratch, object, kSmiTagMask);
    Check(ne, AbortReason::kOperandIsASmi, scratch, Operand(zero_reg));
  }
}

void MacroAssembler::AssertSmi(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    static_assert(kSmiTag == 0);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    andi(scratch, object, kSmiTagMask);
    Check(eq, AbortReason::kOperandIsASmi, scratch, Operand(zero_reg));
  }
}

void MacroAssembler::AssertStackIsAligned() {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    const int frame_alignment = ActivationFrameAlignment();
    const int frame_alignment_mask = frame_alignment - 1;

    if (frame_alignment > kPointerSize) {
      Label alignment_as_expected;
      DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
      {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();
        andi(scratch, sp, frame_alignment_mask);
        Branch(&alignment_as_expected, eq, scratch, Operand(zero_reg));
      }
      // Don't use Check here, as it will call Runtime_Abort re-entering here.
      stop();
      bind(&alignment_as_expected);
    }
  }
}

void MacroAssembler::AssertConstructor(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    static_assert(kSmiTag == 0);
    SmiTst(object, t8);
    Check(ne, AbortReason::kOperandIsASmiAndNotAConstructor, t8,
          Operand(zero_reg));

    LoadMap(t8, object);
    Lbu(t8, FieldMemOperand(t8, Map::kBitFieldOffset));
    And(t8, t8, Operand(Map::Bits1::IsConstructorBit::kMask));
    Check(ne, AbortReason::kOperandIsNotAConstructor, t8, Operand(zero_reg));
  }
}

void MacroAssembler::AssertFunction(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    static_assert(kSmiTag == 0);
    SmiTst(object, t8);
    Check(ne, AbortReason::kOperandIsASmiAndNotAFunction, t8,
          Operand(zero_reg));
    push(object);
    LoadMap(object, object);
    GetInstanceTypeRange(object, object, FIRST_JS_FUNCTION_TYPE, t8);
    Check(ls, AbortReason::kOperandIsNotAFunction, t8,
          Operand(LAST_JS_FUNCTION_TYPE - FIRST_JS_FUNCTION_TYPE));
    pop(object);
  }
}

void MacroAssembler::AssertCallableFunction(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    static_assert(kSmiTag == 0);
    SmiTst(object, t8);
    Check(ne, AbortReason::kOperandIsASmiAndNotAFunction, t8,
          Operand(zero_reg));
    push(object);
    LoadMap(object, object);
    GetInstanceTypeRange(object, object, FIRST_CALLABLE_JS_FUNCTION_TYPE, t8);
    Check(ls, AbortReason::kOperandIsNotACallableFunction, t8,
          Operand(LAST_CALLABLE_JS_FUNCTION_TYPE -
                  FIRST_CALLABLE_JS_FUNCTION_TYPE));
    pop(object);
  }
}

void MacroAssembler::AssertBoundFunction(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    static_assert(kSmiTag == 0);
    SmiTst(object, t8);
    Check(ne, AbortReason::kOperandIsASmiAndNotABoundFunction, t8,
          Operand(zero_reg));
    GetObjectType(object, t8, t8);
    Check(eq, AbortReason::kOperandIsNotABoundFunction, t8,
          Operand(JS_BOUND_FUNCTION_TYPE));
  }
}

void MacroAssembler::AssertGeneratorObject(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  static_assert(kSmiTag == 0);
  SmiTst(object, t8);
  Check(ne, AbortReason::kOperandIsASmiAndNotAGeneratorObject, t8,
        Operand(zero_reg));
  GetObjectType(object, t8, t8);
  Dsubu(t8, t8, Operand(FIRST_JS_GENERATOR_OBJECT_TYPE));
  Check(
      ls, AbortReason::kOperandIsNotAGeneratorObject, t8,
      Operand(LAST_JS_GENERATOR_OBJECT_TYPE - FIRST_JS_GENERATOR_OBJECT_TYPE));
}

void MacroAssembler::AssertUndefinedOrAllocationSite(Register object,
                                                     Register scratch) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    Label done_checking;
    AssertNotSmi(object);
    LoadRoot(scratch, RootIndex::kUndefinedValue);
    Branch(&done_checking, eq, object, Operand(scratch));
    GetObjectType(object, scratch, scratch);
    Assert(eq, AbortReason::kExpectedUndefinedOrCell, scratch,
           Operand(ALLOCATION_SITE_TYPE));
    bind(&done_checking);
  }
}

#endif  // V8_ENABLE_DEBUG_CODE

void MacroAssembler::Float32Max(FPURegister dst, FPURegister src1,
                                FPURegister src2, Label* out_of_line) {
  ASM_CODE_COMMENT(this);
  if (src1 == src2) {
    Move_s(dst, src1);
    return;
  }

  // Check if one of operands is NaN.
  CompareIsNanF32(src1, src2);
  BranchTrueF(out_of_line);

  if (kArchVariant >= kMips64r6) {
    max_s(dst, src1, src2);
  } else {
    Label return_left, return_right, done;

    CompareF32(OLT, src1, src2);
    BranchTrueShortF(&return_right);
    CompareF32(OLT, src2, src1);
    BranchTrueShortF(&return_left);

    // Operands are equal, but check for +/-0.
    {
      BlockTrampolinePoolScope block_trampoline_pool(this);
      mfc1(t8, src1);
      dsll32(t8, t8, 0);
      Branch(&return_left, eq, t8, Operand(zero_reg));
      Branch(&return_right);
    }

    bind(&return_right);
    if (src2 != dst) {
      Move_s(dst, src2);
    }
    Branch(&done);

    bind(&return_left);
    if (src1 != dst) {
      Move_s(dst, src1);
    }

    bind(&done);
  }
}

void MacroAssembler::Float32MaxOutOfLine(FPURegister dst, FPURegister src1,
                                         FPURegister src2) {
  add_s(dst, src1, src2);
}

void MacroAssembler::Float32Min(FPURegister dst, FPURegister src1,
                                FPURegister src2, Label* out_of_line) {
  ASM_CODE_COMMENT(this);
  if (src1 == src2) {
    Move_s(dst, src1);
    return;
  }

  // Check if one of operands is NaN.
  CompareIsNanF32(src1, src2);
  BranchTrueF(out_of_line);

  if (kArchVariant >= kMips64r6) {
    min_s(dst, src1, src2);
  } else {
    Label return_left, return_right, done;

    CompareF32(OLT, src1, src2);
    BranchTrueShortF(&return_left);
    CompareF32(OLT, src2, src1);
    BranchTrueShortF(&return_right);

    // Left equals right => check for -0.
    {
      BlockTrampolinePoolScope block_trampoline_pool(this);
      mfc1(t8, src1);
      dsll32(t8, t8, 0);
      Branch(&return_right, eq, t8, Operand(zero_reg));
      Branch(&return_left);
    }

    bind(&return_right);
    if (src2 != dst) {
      Move_s(dst, src2);
    }
    Branch(&done);

    bind(&return_left);
    if (src1 != dst) {
      Move_s(dst, src1);
    }

    bind(&done);
  }
}

void MacroAssembler::Float32MinOutOfLine(FPURegister dst, FPURegister src1,
                                         FPURegister src2) {
  add_s(dst, src1, src2);
}

void MacroAssembler::Float64Max(FPURegister dst, FPURegister src1,
                                FPURegister src2, Label* out_of_line) {
  ASM_CODE_COMMENT(this);
  if (src1 == src2) {
    Move_d(dst, src1);
    return;
  }

  // Check if one of operands is NaN.
  CompareIsNanF64(src1, src2);
  BranchTrueF(out_of_line);

  if (kArchVariant >= kMips64r6) {
    max_d(dst, src1, src2);
  } else {
    Label return_left, return_right, done;

    CompareF64(OLT, src1, src2);
    BranchTrueShortF(&return_right);
    CompareF64(OLT, src2, src1);
    BranchTrueShortF(&return_left);

    // Left equals right => check for -0.
    {
      BlockTrampolinePoolScope block_trampoline_pool(this);
      dmfc1(t8, src1);
      Branch(&return_left, eq, t8, Operand(zero_reg));
      Branch(&return_right);
    }

    bind(&return_right);
    if (src2 != dst) {
      Move_d(dst, src2);
    }
    Branch(&done);

    bind(&return_left);
    if (src1 != dst) {
      Move_d(dst, src1);
    }

    bind(&done);
  }
}

void MacroAssembler::Float64MaxOutOfLine(FPURegister dst, FPURegister src1,
                                         FPURegister src2) {
  add_d(dst, src1, src2);
}

void MacroAssembler::Float64Min(FPURegister dst, FPURegister src1,
                                FPURegister src2, Label* out_of_line) {
  ASM_CODE_COMMENT(this);
  if (src1 == src2) {
    Move_d(dst, src1);
    return;
  }

  // Check if one of operands is NaN.
  CompareIsNanF64(src1, src2);
  BranchTrueF(out_of_line);

  if (kArchVariant >= kMips64r6) {
    min_d(dst, src1, src2);
  } else {
    Label return_left, return_right, done;

    CompareF64(OLT, src1, src2);
    BranchTrueShortF(&return_left);
    CompareF64(OLT, src2, src1);
    BranchTrueShortF(&return_right);

    // Left equals right => check for -0.
    {
      BlockTrampolinePoolScope block_trampoline_pool(this);
      dmfc1(t8, src1);
      Branch(&return_right, eq, t8, Operand(zero_reg));
      Branch(&return_left);
    }

    bind(&return_right);
    if (src2 != dst) {
      Move_d(dst, src2);
    }
    Branch(&done);

    bind(&return_left);
    if (src1 != dst) {
      Move_d(dst, src1);
    }

    bind(&done);
  }
}

void MacroAssembler::Float64MinOutOfLine(FPURegister dst, FPURegister src1,
                                         FPURegister src2) {
  add_d(dst, src1, src2);
}

int MacroAssembler::CalculateStackPassedWords(int num_reg_arguments,
                                              int num_double_arguments) {
  int stack_passed_words = 0;
  int num_args = num_reg_arguments + num_double_arguments;

  // Up to eight arguments are passed in FPURegisters and GPRegisters.
  if (num_args > kRegisterPassedArguments) {
    stack_passed_words = num_args - kRegisterPassedArguments;
  }
  stack_passed_words += kCArgSlotCount;
  return stack_passed_words;
}

void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          int num_double_arguments,
                                          Register scratch) {
  ASM_CODE_COMMENT(this);
  int frame_alignment = ActivationFrameAlignment();

  // n64: Up to eight simple arguments in a0..a3, a4..a7, No argument slots.
  // O32: Up to four simple arguments are passed in registers a0..a3.
  // Those four arguments must have reserved argument slots on the stack for
  // mips, even though those argument slots are not normally used.
  // Both ABIs: Remaining arguments are pushed on the stack, above (higher
  // address than) the (O32) argument slots. (arg slot calculation handled by
  // CalculateStackPassedWords()).
  int stack_passed_arguments =
      CalculateStackPassedWords(num_reg_arguments, num_double_arguments);
  if (frame_alignment > kPointerSize) {
    // Make stack end at alignment and make room for num_arguments - 4 words
    // and the original value of sp.
    mov(scratch, sp);
    Dsubu(sp, sp, Operand((stack_passed_arguments + 1) * kPointerSize));
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    And(sp, sp, Operand(-frame_alignment));
    Sd(scratch, MemOperand(sp, stack_passed_arguments * kPointerSize));
  } else {
    Dsubu(sp, sp, Operand(stack_passed_arguments * kPointerSize));
  }
}

void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          Register scratch) {
  PrepareCallCFunction(num_reg_arguments, 0, scratch);
}

int MacroAssembler::CallCFunction(ExternalReference function,
                                  int num_reg_arguments,
                                  int num_double_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  li(t9, function);
  return CallCFunctionHelper(t9, num_reg_arguments, num_double_arguments,
                             set_isolate_data_slots, return_location);
}

int MacroAssembler::CallCFunction(Register function, int num_reg_arguments,
                                  int num_double_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  ASM_CODE_COMMENT(this);
  return CallCFunctionHelper(function, num_reg_arguments, num_double_arguments,
                             set_isolate_data_slots, return_location);
}

int MacroAssembler::CallCFunction(ExternalReference function, int num_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  return CallCFunction(function, num_arguments, 0, set_isolate_data_slots,
                       return_location);
}

int MacroAssembler::CallCFunction(Register function, int num_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  return CallCFunction(function, num_arguments, 0, set_isolate_data_slots,
                       return_location);
}

int MacroAssembler::CallCFunctionHelper(
    Register function, int num_reg_arguments, int num_double_arguments,
    SetIsolateDataSlots set_isolate_data_slots, Label* return_location) {
  DCHECK_LE(num_reg_arguments + num_double_arguments, kMaxCParameters);
  DCHECK(has_frame());

  Label get_pc;

  // Make sure that the stack is aligned before calling a C function unless
  // running in the simulator. The simulator has its own alignment check which
  // provides more information.
  // The argument stots are presumed to have been set up by
  // PrepareCallCFunction. The C function must be called via t9, for mips ABI.

#if V8_HOST_ARCH_MIPS || V8_HOST_ARCH_MIPS64
  if (v8_flags.debug_code) {
    int frame_alignment = base::OS::ActivationFrameAlignment();
    int frame_alignment_mask = frame_alignment - 1;
    if (frame_alignment > kPointerSize) {
      DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
      Label alignment_as_expected;
      {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();
        And(scratch, sp, Operand(frame_alignment_mask));
        Branch(&alignment_as_expected, eq, scratch, Operand(zero_reg));
      }
      // Don't use Check here, as it will call Runtime_Abort possibly
      // re-entering here.
      stop();
      bind(&alignment_as_expected);
    }
  }
#endif  // V8_HOST_ARCH_MIPS

  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
      if (function != t9) {
        mov(t9, function);
        function = t9;
      }

      // Save the frame pointer and PC so that the stack layout remains
      // iterable, even without an ExitFrame which normally exists between JS
      // and C frames. 't' registers are caller-saved so this is safe as a
      // scratch register.
      Register pc_scratch = t1;
      DCHECK(!AreAliased(pc_scratch, function));
      CHECK(root_array_available());

      LoadAddressPCRelative(pc_scratch, &get_pc);

      Sd(pc_scratch,
         ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC));
      Sd(fp, ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
    }

    Call(function);
    int call_pc_offset = pc_offset();
    bind(&get_pc);

    if (return_location) bind(return_location);

    if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
      // We don't unset the PC; the FP is the source of truth.
      Sd(zero_reg,
         ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
    }

    int stack_passed_arguments =
        CalculateStackPassedWords(num_reg_arguments, num_double_arguments);

    if (base::OS::ActivationFrameAlignment() > kPointerSize) {
      Ld(sp, MemOperand(sp, stack_passed_arguments * kPointerSize));
    } else {
      Daddu(sp, sp, Operand(stack_passed_arguments * kPointerSize));
    }

    set_pc_for_safepoint();

    return call_pc_offset;
  }
}

#undef BRANCH_ARGS_CHECK

void MacroAssembler::CheckPageFlag(Register object, Register scratch, int mask,
                                   Condition cc, Label* condition_met) {
  ASM_CODE_COMMENT(this);
  And(scratch, object, Operand(~MemoryChunk::GetAlignmentMaskForAssembler()));
  Ld(scratch, MemOperand(scratch, MemoryChunkLayout::kFlagsOffset));
  And(scratch, scratch, Operand(mask));
  Branch(condition_met, cc, scratch, Operand(zero_reg));
}

Register GetRegisterThatIsNotOneOf(Register reg1, Register reg2, Register reg3,
                                   Register reg4, Register reg5,
                                   Register reg6) {
  RegList regs = {reg1, reg2, reg3, reg4, reg5, reg6};

  const RegisterConfiguration* config = RegisterConfiguration::Default();
  for (int i = 0; i < config->num_allocatable_general_registers(); ++i) {
    int code = config->GetAllocatableGeneralCode(i);
    Register candidate = Register::from_code(code);
    if (regs.has(candidate)) continue;
    return candidate;
  }
  UNREACHABLE();
}

void MacroAssembler::ComputeCodeStartAddress(Register dst) {
  // This push on ra and the pop below together ensure that we restore the
  // register ra, which is needed while computing the code start address.
  push(ra);

  // The nal instruction puts the address of the current instruction into
  // the return address (ra) register, which we can use later on.
  if (kArchVariant == kMips64r6) {
    addiupc(ra, 1);
  } else {
    nal();
    nop();
  }
  int pc = pc_offset();
  li(dst, Operand(pc));
  Dsubu(dst, ra, dst);

  pop(ra);  // Restore ra
}

void MacroAssembler::CallForDeoptimization(Builtin target, int, Label* exit,
                                           DeoptimizeKind kind, Label* ret,
                                           Label*) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Ld(t9,
     MemOperand(kRootRegister, IsolateData::BuiltinEntrySlotOffset(target)));
  Call(t9);
  DCHECK_EQ(SizeOfCodeGeneratedSince(exit),
            (kind == DeoptimizeKind::kLazy) ? Deoptimizer::kLazyDeoptExitSize
                                            : Deoptimizer::kEagerDeoptExitSize);
}

void MacroAssembler::LoadCodeInstructionStart(
    Register destination, Register code_data_container_object,
    CodeEntrypointTag tag) {
  ASM_CODE_COMMENT(this);
  Ld(destination, FieldMemOperand(code_data_container_object,
                                  Code::kInstructionStartOffset));
}

void MacroAssembler::CallCodeObject(Register code_data_container_object,
                                    CodeEntrypointTag tag) {
  ASM_CODE_COMMENT(this);
  LoadCodeInstructionStart(code_data_container_object,
                           code_data_container_object, tag);
  Call(code_data_container_object);
}

void MacroAssembler::JumpCodeObject(Register code_data_container_object,
                                    CodeEntrypointTag tag, JumpMode jump_mode) {
  ASM_CODE_COMMENT(this);
  DCHECK_EQ(JumpMode::kJump, jump_mode);
  LoadCodeInstructionStart(code_data_container_object,
                           code_data_container_object, tag);
  Jump(code_data_container_object);
}

void MacroAssembler::CallJSFunction(Register function_object) {
  Register code = kJavaScriptCallCodeStartRegister;
  Ld(code, FieldMemOperand(function_object, JSFunction::kCodeOffset));
  CallCodeObject(code, kJSEntrypointTag);
}

void MacroAssembler::JumpJSFunction(Register function_object,
                                    JumpMode jump_mode) {
  Register code = kJavaScriptCallCodeStartRegister;
  Ld(code, FieldMemOperand(function_object, JSFunction::kCodeOffset));
  JumpCodeObject(code, kJSEntrypointTag, jump_mode);
}

namespace {

void TailCallOptimizedCodeSlot(MacroAssembler* masm,
                               Register optimized_code_entry, Register scratch1,
                               Register scratch2) {
  // ----------- S t a t e -------------
  //  -- a0 : actual argument count
  //  -- a3 : new target (preserved for callee if needed, and caller)
  //  -- a1 : target function (preserved for callee if needed, and caller)
  // -----------------------------------
  ASM_CODE_COMMENT(masm);
  DCHECK(!AreAliased(optimized_code_entry, a1, a3, scratch1, scratch2));

  Register closure = a1;
  Label heal_optimized_code_slot;

  // If the optimized code is cleared, go to runtime to update the optimization
  // marker field.
  __ LoadWeakValue(optimized_code_entry, optimized_code_entry,
                   &heal_optimized_code_slot);

  // The entry references a CodeWrapper object. Unwrap it now.
  __ Ld(optimized_code_entry,
        FieldMemOperand(optimized_code_entry, CodeWrapper::kCodeOffset));

  // Check if the optimized code is marked for deopt. If it is, call the
  // runtime to clear it.
  __ TestCodeIsMarkedForDeoptimizationAndJump(optimized_code_entry, scratch1,
                                              ne, &heal_optimized_code_slot);

  // Optimized code is good, get it into the closure and link the closure into
  // the optimized functions list, then tail call the optimized code.
  // The feedback vector is no longer used, so re-use it as a scratch
  // register.
  __ ReplaceClosureCodeWithOptimizedCode(optimized_code_entry, closure,
                                         scratch1, scratch2);

  static_assert(kJavaScriptCallCodeStartRegister == a2, "ABI mismatch");
  __ LoadCodeInstructionStart(a2, optimized_code_entry, kJSEntrypointTag);
  __ Jump(a2);

  // Optimized code slot contains deoptimized code or code is cleared and
  // optimized code marker isn't updated. Evict the code, update the marker
  // and re-enter the closure's code.
  __ bind(&heal_optimized_code_slot);
  __ GenerateTailCallToReturnedCode(Runtime::kHealOptimizedCodeSlot);
}

}  // namespace

#ifdef V8_ENABLE_DEBUG_CODE
void MacroAssembler::AssertFeedbackCell(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    GetObjectType(object, scratch, scratch);
    Assert(eq, AbortReason::kExpectedFeedbackCell, scratch,
           Operand(FEEDBACK_CELL_TYPE));
  }
}
void MacroAssembler::AssertFeedbackVector(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    GetObjectType(object, scratch, scratch);
    Assert(eq, AbortReason::kExpectedFeedbackVector, scratch,
           Operand(FEEDBACK_VECTOR_TYPE));
  }
}
#endif  // V8_ENABLE_DEBUG_CODE

void MacroAssembler::ReplaceClosureCodeWithOptimizedCode(
    Register optimized_code, Register closure, Register scratch1,
    Register scratch2) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(optimized_code, closure, scratch1, scratch2));
  // Store code entry in the closure.
  Sd(optimized_code, FieldMemOperand(closure, JSFunction::kCodeOffset));
  mov(scratch1, optimized_code);  // Write barrier clobbers scratch1 below.
  RecordWriteField(closure, JSFunction::kCodeOffset, scratch1, scratch2,
                   kRAHasNotBeenSaved, SaveFPRegsMode::kIgnore,
                   SmiCheck::kOmit);
}

void MacroAssembler::GenerateTailCallToReturnedCode(
    Runtime::FunctionId function_id) {
  // ----------- S t a t e -------------
  //  -- a0 : actual argument count
  //  -- a1 : target function (preserved for callee)
  //  -- a3 : new target (preserved for callee)
  // -----------------------------------
  ASM_CODE_COMMENT(this);
  {
    FrameScope scope(this, StackFrame::INTERNAL);
    // Push a copy of the target function, the new target and the actual
    // argument count.
    // Push function as parameter to the runtime call.
    SmiTag(kJavaScriptCallArgCountRegister);
    Push(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
         kJavaScriptCallArgCountRegister, kJavaScriptCallTargetRegister);

    CallRuntime(function_id, 1);
    // Restore target function, new target and actual argument count.
    Pop(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
        kJavaScriptCallArgCountRegister);
    SmiUntag(kJavaScriptCallArgCountRegister);
  }

  static_assert(kJavaScriptCallCodeStartRegister == a2, "ABI mismatch");
  LoadCodeInstructionStart(a2, v0, kJSEntrypointTag);
  Jump(a2);
}

void MacroAssembler::LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
    Register flags, Register feedback_vector, CodeKind current_code_kind,
    Label* flags_need_processing) {
  ASM_CODE_COMMENT(this);
  DCHECK(CodeKindCanTierUp(current_code_kind));
  Register scratch = t2;
  Lhu(flags, FieldMemOperand(feedback_vector, FeedbackVector::kFlagsOffset));
  uint32_t kFlagsMask = FeedbackVector::kFlagsTieringStateIsAnyRequested |
                        FeedbackVector::kFlagsMaybeHasTurbofanCode |
                        FeedbackVector::kFlagsLogNextExecution;
  if (current_code_kind != CodeKind::MAGLEV) {
    kFlagsMask |= FeedbackVector::kFlagsMaybeHasMaglevCode;
  }
  And(scratch, flags, Operand(kFlagsMask));
  Branch(flags_need_processing, ne, scratch, Operand(zero_reg));
}

void MacroAssembler::OptimizeCodeOrTailCallOptimizedCodeSlot(
    Register flags, Register feedback_vector) {
  ASM_CODE_COMMENT(this);
  Label maybe_has_optimized_code, maybe_needs_logging;
  // Check if optimized code marker is available.
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    And(scratch, flags,
        Operand(FeedbackVector::kFlagsTieringStateIsAnyRequested));
    Branch(&maybe_needs_logging, eq, scratch, Operand(zero_reg));
  }

  GenerateTailCallToReturnedCode(Runtime::kCompileOptimized);

  bind(&maybe_needs_logging);
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    And(scratch, flags, Operand(FeedbackVector::LogNextExecutionBit::kMask));
    Branch(&maybe_has_optimized_code, eq, scratch, Operand(zero_reg));
  }

  GenerateTailCallToReturnedCode(Runtime::kFunctionLogNextExecution);

  bind(&maybe_has_optimized_code);
  Register optimized_code_entry = flags;
  Ld(optimized_code_entry,
     FieldMemOperand(feedback_vector,
                     FeedbackVector::kMaybeOptimizedCodeOffset));
  TailCallOptimizedCodeSlot(this, optimized_code_entry, t3, a5);
}

// Calls an API function.  Allocates HandleScope, extracts returned value
// from handle and propagates exceptions. Clobbers C argument registers
// and C caller-saved registers. Restores context. On return removes
//   (*argc_operand + slots_to_drop_on_return) * kSystemPointerSize
// (GCed, includes the call JS arguments space and the additional space
// allocated for the fast call).
void CallApiFunctionAndReturn(MacroAssembler* masm, bool with_profiling,
                              Register function_address,
                              ExternalReference thunk_ref, Register thunk_arg,
                              int slots_to_drop_on_return,
                              MemOperand* argc_operand,
                              MemOperand return_value_operand) {
  using ER = ExternalReference;

  Isolate* isolate = masm->isolate();
  MemOperand next_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_next_address(isolate), no_reg);
  MemOperand limit_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_limit_address(isolate), no_reg);
  MemOperand level_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_level_address(isolate), no_reg);

  Register return_value = v0;
  Register scratch = a4;
  Register scratch2 = a5;

  // Allocate HandleScope in callee-saved registers.
  // We will need to restore the HandleScope after the call to the API function,
  // by allocating it in callee-saved registers it'll be preserved by C code.
  Register prev_next_address_reg = s0;
  Register prev_limit_reg = s1;
  Register prev_level_reg = s2;

  // C arguments (kCArgRegs[0/1]) are expected to be initialized outside, so
  // this function must not corrupt them (return_value overlaps with
  // kCArgRegs[0] but that's ok because we start using it only after the C
  // call).
  DCHECK(!AreAliased(kCArgRegs[0], kCArgRegs[1],  // C args
                     scratch, scratch2, prev_next_address_reg, prev_limit_reg));
  // function_address and thunk_arg might overlap but this function must not
  // corrupted them until the call is made (i.e. overlap with return_value is
  // fine).
  DCHECK(!AreAliased(function_address,  // incoming parameters
                     scratch, scratch2, prev_next_address_reg, prev_limit_reg));
  DCHECK(!AreAliased(thunk_arg,  // incoming parameters
                     scratch, scratch2, prev_next_address_reg, prev_limit_reg));

  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Allocate HandleScope in callee-save registers.");
    __ Ld(prev_next_address_reg, next_mem_op);
    __ Ld(prev_limit_reg, limit_mem_op);
    __ Lw(prev_level_reg, level_mem_op);
    __ Addu(scratch, prev_level_reg, Operand(1));
    __ Sw(scratch, level_mem_op);
  }

  Label profiler_or_side_effects_check_enabled, done_api_call;
  if (with_profiling) {
    __ RecordComment("Check if profiler or side effects check is enabled");
    __ Lb(scratch,
          __ ExternalReferenceAsOperand(IsolateFieldId::kExecutionMode));
    __ Branch(&profiler_or_side_effects_check_enabled, ne, scratch,
              Operand(zero_reg));
#ifdef V8_RUNTIME_CALL_STATS
    __ RecordComment("Check if RCS is enabled");
    __ li(scratch, ER::address_of_runtime_stats_flag());
    __ Lw(scratch, MemOperand(scratch, 0));
    __ Branch(&profiler_or_side_effects_check_enabled, ne, scratch,
              Operand(zero_reg));
#endif  // V8_RUNTIME_CALL_STATS
  }

  __ RecordComment("Call the api function directly.");
  __ StoreReturnAddressAndCall(function_address);
  __ bind(&done_api_call);

  Label propagate_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;

  __ RecordComment("Load value from ReturnValue.");
  __ Ld(return_value, return_value_operand);

  {
    ASM_CODE_COMMENT_STRING(
        masm,
        "No more valid handles (the result handle was the last one)."
        "Restore previous handle scope.");
    __ Sd(prev_next_address_reg, next_mem_op);
    if (v8_flags.debug_code) {
      __ Lw(scratch, level_mem_op);
      __ Subu(scratch, scratch, Operand(1));
      __ Check(eq, AbortReason::kUnexpectedLevelAfterReturnFromApiCall, scratch,
               Operand(prev_level_reg));
    }
    __ Sw(prev_level_reg, level_mem_op);
    __ Ld(scratch, limit_mem_op);
    __ Branch(&delete_allocated_handles, ne, prev_limit_reg, Operand(scratch));
  }

  __ RecordComment("Leave the API exit frame.");
  __ bind(&leave_exit_frame);

  Register argc_reg = prev_limit_reg;
  if (argc_operand != nullptr) {
    // Load the number of stack slots to drop before LeaveExitFrame modifies sp.
    __ Ld(argc_reg, *argc_operand);
  }

  __ LeaveExitFrame(scratch);

  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Check if the function scheduled an exception.");
    __ LoadRoot(scratch, RootIndex::kTheHoleValue);
    __ Ld(scratch2, __ ExternalReferenceAsOperand(
                        ER::exception_address(isolate), no_reg));
    __ Branch(&propagate_exception, ne, scratch, Operand(scratch2));
  }

  __ AssertJSAny(return_value, scratch, scratch2,
                 AbortReason::kAPICallReturnedInvalidObject);

  if (argc_operand == nullptr) {
    DCHECK_NE(slots_to_drop_on_return, 0);
    __ Daddu(sp, sp, Operand(slots_to_drop_on_return * kSystemPointerSize));
  } else {
    // {argc_operand} was loaded into {argc_reg} above.
    if (slots_to_drop_on_return != 0) {
      __ Daddu(sp, sp, Operand(slots_to_drop_on_return * kSystemPointerSize));
    }
    __ Dlsa(sp, sp, argc_reg, kSystemPointerSizeLog2);
  }

  __ Ret();

  if (with_profiling) {
    ASM_CODE_COMMENT_STRING(masm, "Call the api function via thunk wrapper.");
    __ bind(&profiler_or_side_effects_check_enabled);
    // Additional parameter is the address of the actual callback.
    if (thunk_arg.is_valid()) {
      MemOperand thunk_arg_mem_op = __ ExternalReferenceAsOperand(
          IsolateFieldId::kApiCallbackThunkArgument);
      __ Sd(thunk_arg, thunk_arg_mem_op);
    }
    __ li(scratch, thunk_ref);
    __ StoreReturnAddressAndCall(scratch);
    __ Branch(&done_api_call);
  }

  __ RecordComment("An exception was thrown. Propagate it.");
  __ bind(&propagate_exception);
  __ TailCallRuntime(Runtime::kPropagateException);

  {
    ASM_CODE_COMMENT_STRING(
        masm, "HandleScope limit has changed. Delete allocated extensions.");
    __ bind(&delete_allocated_handles);
    __ Sd(prev_limit_reg, limit_mem_op);
    // Save the return value in a callee-save register.
    Register saved_result = prev_limit_reg;
    __ mov(saved_result, v0);
    __ mov(kCArgRegs[0], v0);
    __ PrepareCallCFunction(1, prev_level_reg);
    __ li(kCArgRegs[0], ER::isolate_address());
    __ CallCFunction(ER::delete_handle_scope_extensions(), 1);
    __ mov(v0, saved_result);
    __ jmp(&leave_exit_frame);
  }
}

}  // namespace internal
}  // namespace v8

#undef __

#endif  // V8_TARGET_ARCH_MIPS64
