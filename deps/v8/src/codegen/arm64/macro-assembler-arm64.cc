// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM64

#include <optional>

#include "src/base/bits.h"
#include "src/base/division-by-constant.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/assembler.h"
#include "src/codegen/callable.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/external-reference-table.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/reloc-info.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames-inl.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/init/bootstrapper.h"
#include "src/logging/counters.h"
#include "src/runtime/runtime.h"
#include "src/snapshot/snapshot.h"

// Satisfy cpplint check, but don't include platform-specific header. It is
// included recursively via macro-assembler.h.
#if 0
#include "src/codegen/arm64/macro-assembler-arm64.h"
#endif

#define __ ACCESS_MASM(masm)

namespace v8 {
namespace internal {

CPURegList MacroAssembler::DefaultTmpList() { return CPURegList(ip0, ip1); }

CPURegList MacroAssembler::DefaultFPTmpList() {
  return CPURegList(fp_scratch1, fp_scratch2);
}

namespace {

// For WebAssembly we care about the full floating point register. If we are not
// running Wasm, we can get away with saving half of those registers.
#if V8_ENABLE_WEBASSEMBLY
constexpr int kStackSavedSavedFPSizeInBits = kQRegSizeInBits;
#else
constexpr int kStackSavedSavedFPSizeInBits = kDRegSizeInBits;
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace

void MacroAssembler::PushCPURegList(CPURegList registers) {
  // If LR was stored here, we would need to sign it if
  // V8_ENABLE_CONTROL_FLOW_INTEGRITY is on.
  DCHECK(!registers.IncludesAliasOf(lr));

  int size = registers.RegisterSizeInBytes();
  DCHECK_EQ(0, (size * registers.Count()) % 16);

  // Push up to four registers at a time.
  while (!registers.IsEmpty()) {
    int count_before = registers.Count();
    const CPURegister& src0 = registers.PopHighestIndex();
    const CPURegister& src1 = registers.PopHighestIndex();
    const CPURegister& src2 = registers.PopHighestIndex();
    const CPURegister& src3 = registers.PopHighestIndex();
    int count = count_before - registers.Count();
    PushHelper(count, size, src0, src1, src2, src3);
  }
}

void MacroAssembler::PopCPURegList(CPURegList registers) {
  int size = registers.RegisterSizeInBytes();
  DCHECK_EQ(0, (size * registers.Count()) % 16);

  // If LR was loaded here, we would need to authenticate it if
  // V8_ENABLE_CONTROL_FLOW_INTEGRITY is on.
  DCHECK(!registers.IncludesAliasOf(lr));

  // Pop up to four registers at a time.
  while (!registers.IsEmpty()) {
    int count_before = registers.Count();
    const CPURegister& dst0 = registers.PopLowestIndex();
    const CPURegister& dst1 = registers.PopLowestIndex();
    const CPURegister& dst2 = registers.PopLowestIndex();
    const CPURegister& dst3 = registers.PopLowestIndex();
    int count = count_before - registers.Count();
    PopHelper(count, size, dst0, dst1, dst2, dst3);
  }
}

void MacroAssembler::PushAll(RegList reglist) {
  if (reglist.Count() % 2 != 0) {
    DCHECK(!reglist.has(xzr));
    reglist.set(xzr);
  }

  CPURegList registers(kXRegSizeInBits, reglist);
  int size = registers.RegisterSizeInBytes();
  DCHECK_EQ(0, (size * registers.Count()) % 16);

  // If LR was stored here, we would need to sign it if
  // V8_ENABLE_CONTROL_FLOW_INTEGRITY is on.
  DCHECK(!registers.IncludesAliasOf(lr));

  while (!registers.IsEmpty()) {
    const CPURegister& src0 = registers.PopLowestIndex();
    const CPURegister& src1 = registers.PopLowestIndex();
    stp(src1, src0, MemOperand(sp, -2 * size, PreIndex));
  }
}

void MacroAssembler::PopAll(RegList reglist) {
  if (reglist.Count() % 2 != 0) {
    DCHECK(!reglist.has(xzr));
    reglist.set(xzr);
  }

  CPURegList registers(kXRegSizeInBits, reglist);
  int size = registers.RegisterSizeInBytes();
  DCHECK_EQ(0, (size * registers.Count()) % 16);

  // If LR was loaded here, we would need to authenticate it if
  // V8_ENABLE_CONTROL_FLOW_INTEGRITY is on.
  DCHECK(!registers.IncludesAliasOf(lr));

  while (!registers.IsEmpty()) {
    const CPURegister& dst0 = registers.PopHighestIndex();
    const CPURegister& dst1 = registers.PopHighestIndex();
    ldp(dst0, dst1, MemOperand(sp, 2 * size, PostIndex));
  }
}

int MacroAssembler::RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                                    Register exclusion) const {
  auto list = kCallerSaved;
  list.Remove(exclusion);
  list.Align();

  int bytes = list.TotalSizeInBytes();

  if (fp_mode == SaveFPRegsMode::kSave) {
    auto fp_list = CPURegList::GetCallerSavedV(kStackSavedSavedFPSizeInBits);
    DCHECK_EQ(fp_list.Count() % 2, 0);
    bytes += fp_list.TotalSizeInBytes();
  }
  return bytes;
}

int MacroAssembler::PushCallerSaved(SaveFPRegsMode fp_mode,
                                    Register exclusion) {
  ASM_CODE_COMMENT(this);
  auto list = kCallerSaved;
  list.Remove(exclusion);
  list.Align();

  PushCPURegList(list);

  int bytes = list.TotalSizeInBytes();

  if (fp_mode == SaveFPRegsMode::kSave) {
    auto fp_list = CPURegList::GetCallerSavedV(kStackSavedSavedFPSizeInBits);
    DCHECK_EQ(fp_list.Count() % 2, 0);
    PushCPURegList(fp_list);
    bytes += fp_list.TotalSizeInBytes();
  }
  return bytes;
}

int MacroAssembler::PopCallerSaved(SaveFPRegsMode fp_mode, Register exclusion) {
  ASM_CODE_COMMENT(this);
  int bytes = 0;
  if (fp_mode == SaveFPRegsMode::kSave) {
    auto fp_list = CPURegList::GetCallerSavedV(kStackSavedSavedFPSizeInBits);
    DCHECK_EQ(fp_list.Count() % 2, 0);
    PopCPURegList(fp_list);
    bytes += fp_list.TotalSizeInBytes();
  }

  auto list = kCallerSaved;
  list.Remove(exclusion);
  list.Align();

  PopCPURegList(list);
  bytes += list.TotalSizeInBytes();

  return bytes;
}

void MacroAssembler::LogicalMacro(const Register& rd, const Register& rn,
                                  const Operand& operand, LogicalOp op) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);

  if (operand.NeedsRelocation(this)) {
    Register temp = temps.AcquireX();
    Ldr(temp, operand.immediate());
    Logical(rd, rn, temp, op);

  } else if (operand.IsImmediate()) {
    int64_t immediate = operand.ImmediateValue();
    unsigned reg_size = rd.SizeInBits();

    // If the operation is NOT, invert the operation and immediate.
    if ((op & NOT) == NOT) {
      op = static_cast<LogicalOp>(op & ~NOT);
      immediate = ~immediate;
    }

    // Ignore the top 32 bits of an immediate if we're moving to a W register.
    if (rd.Is32Bits()) {
      immediate &= kWRegMask;
    }

    DCHECK(rd.Is64Bits() || is_uint32(immediate));

    // Special cases for all set or all clear immediates.
    if (immediate == 0) {
      switch (op) {
        case AND:
          Mov(rd, 0);
          return;
        case ORR:  // Fall through.
        case EOR:
          Mov(rd, rn);
          return;
        case ANDS:  // Fall through.
        case BICS:
          break;
        default:
          UNREACHABLE();
      }
    } else if ((rd.Is64Bits() && (immediate == -1L)) ||
               (rd.Is32Bits() && (immediate == 0xFFFFFFFFL))) {
      switch (op) {
        case AND:
          Mov(rd, rn);
          return;
        case ORR:
          Mov(rd, immediate);
          return;
        case EOR:
          Mvn(rd, rn);
          return;
        case ANDS:  // Fall through.
        case BICS:
          break;
        default:
          UNREACHABLE();
      }
    }

    unsigned n, imm_s, imm_r;
    if (IsImmLogical(immediate, reg_size, &n, &imm_s, &imm_r)) {
      // Immediate can be encoded in the instruction.
      LogicalImmediate(rd, rn, n, imm_s, imm_r, op);
    } else {
      // Immediate can't be encoded: synthesize using move immediate.
      Register temp = temps.AcquireSameSizeAs(rn);

      // If the left-hand input is the stack pointer, we can't pre-shift the
      // immediate, as the encoding won't allow the subsequent post shift.
      PreShiftImmMode mode = rn == sp ? kNoShift : kAnyShift;
      Operand imm_operand = MoveImmediateForShiftedOp(temp, immediate, mode);

      if (rd.IsSP()) {
        // If rd is the stack pointer we cannot use it as the destination
        // register so we use the temp register as an intermediate again.
        Logical(temp, rn, imm_operand, op);
        Mov(sp, temp);
      } else {
        Logical(rd, rn, imm_operand, op);
      }
    }

  } else if (operand.IsExtendedRegister()) {
    DCHECK(operand.reg().SizeInBits() <= rd.SizeInBits());
    // Add/sub extended supports shift <= 4. We want to support exactly the
    // same modes here.
    DCHECK_LE(operand.shift_amount(), 4);
    DCHECK(operand.reg().Is64Bits() ||
           ((operand.extend() != UXTX) && (operand.extend() != SXTX)));
    Register temp = temps.AcquireSameSizeAs(rn);
    EmitExtendShift(temp, operand.reg(), operand.extend(),
                    operand.shift_amount());
    Logical(rd, rn, temp, op);

  } else {
    // The operand can be encoded in the instruction.
    DCHECK(operand.IsShiftedRegister());
    Logical(rd, rn, operand, op);
  }
}

void MacroAssembler::Mov(const Register& rd, uint64_t imm) {
  DCHECK(allow_macro_instructions());
  DCHECK(is_uint32(imm) || is_int32(imm) || rd.Is64Bits());
  DCHECK(!rd.IsZero());

  // TODO(all) extend to support more immediates.
  //
  // Immediates on Aarch64 can be produced using an initial value, and zero to
  // three move keep operations.
  //
  // Initial values can be generated with:
  //  1. 64-bit move zero (movz).
  //  2. 32-bit move inverted (movn).
  //  3. 64-bit move inverted.
  //  4. 32-bit orr immediate.
  //  5. 64-bit orr immediate.
  // Move-keep may then be used to modify each of the 16-bit half-words.
  //
  // The code below supports all five initial value generators, and
  // applying move-keep operations to move-zero and move-inverted initial
  // values.

  // Try to move the immediate in one instruction, and if that fails, switch to
  // using multiple instructions.
  if (!TryOneInstrMoveImmediate(rd, imm)) {
    unsigned reg_size = rd.SizeInBits();

    // Generic immediate case. Imm will be represented by
    //   [imm3, imm2, imm1, imm0], where each imm is 16 bits.
    // A move-zero or move-inverted is generated for the first non-zero or
    // non-0xFFFF immX, and a move-keep for subsequent non-zero immX.

    uint64_t ignored_halfword = 0;
    bool invert_move = false;
    // If the number of 0xFFFF halfwords is greater than the number of 0x0000
    // halfwords, it's more efficient to use move-inverted.
    if (CountSetHalfWords(imm, reg_size) > CountSetHalfWords(~imm, reg_size)) {
      ignored_halfword = 0xFFFFL;
      invert_move = true;
    }

    // Mov instructions can't move immediate values into the stack pointer, so
    // set up a temporary register, if needed.
    UseScratchRegisterScope temps(this);
    Register temp = rd.IsSP() ? temps.AcquireSameSizeAs(rd) : rd;

    // Iterate through the halfwords. Use movn/movz for the first non-ignored
    // halfword, and movk for subsequent halfwords.
    DCHECK_EQ(reg_size % 16, 0);
    bool first_mov_done = false;
    for (int i = 0; i < (rd.SizeInBits() / 16); i++) {
      uint64_t imm16 = (imm >> (16 * i)) & 0xFFFFL;
      if (imm16 != ignored_halfword) {
        if (!first_mov_done) {
          if (invert_move) {
            movn(temp, (~imm16) & 0xFFFFL, 16 * i);
          } else {
            movz(temp, imm16, 16 * i);
          }
          first_mov_done = true;
        } else {
          // Construct a wider constant.
          movk(temp, imm16, 16 * i);
        }
      }
    }
    DCHECK(first_mov_done);

    // Move the temporary if the original destination register was the stack
    // pointer.
    if (rd.IsSP()) {
      mov(rd, temp);
    }
  }
}

void MacroAssembler::Mov(const Register& rd, ExternalReference reference) {
  if (root_array_available_) {
    if (reference.IsIsolateFieldId()) {
      Add(rd, kRootRegister, Operand(reference.offset_from_root_register()));
      return;
    }
  }
  // External references should not get created with IDs if
  // `!root_array_available()`.
  CHECK(!reference.IsIsolateFieldId());
  Mov(rd, Operand(reference));
}

void MacroAssembler::LoadIsolateField(const Register& rd, IsolateFieldId id) {
  Mov(rd, ExternalReference::Create(id));
}

void MacroAssembler::Mov(const Register& rd, const Operand& operand,
                         DiscardMoveMode discard_mode) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());

  // Provide a swap register for instructions that need to write into the
  // system stack pointer (and can't do this inherently).
  UseScratchRegisterScope temps(this);
  Register dst = (rd.IsSP()) ? temps.AcquireSameSizeAs(rd) : rd;

  if (operand.NeedsRelocation(this)) {
    // TODO(jgruber,v8:8887): Also consider a root-relative load when generating
    // non-isolate-independent code. In many cases it might be cheaper than
    // embedding the relocatable value.
    if (root_array_available_ && options().isolate_independent_code) {
      if (operand.ImmediateRMode() == RelocInfo::EXTERNAL_REFERENCE) {
        Address addr = static_cast<Address>(operand.ImmediateValue());
        ExternalReference reference = base::bit_cast<ExternalReference>(addr);
        IndirectLoadExternalReference(rd, reference);
        return;
      } else if (RelocInfo::IsEmbeddedObjectMode(operand.ImmediateRMode())) {
        Handle<HeapObject> x(
            reinterpret_cast<Address*>(operand.ImmediateValue()));
        // TODO(v8:9706): Fix-it! This load will always uncompress the value
        // even when we are loading a compressed embedded object.
        IndirectLoadConstant(rd.X(), x);
        return;
      }
    }
    Ldr(dst, operand);
  } else if (operand.IsImmediate()) {
    // Call the macro assembler for generic immediates.
    Mov(dst, operand.ImmediateValue());
  } else if (operand.IsShiftedRegister() && (operand.shift_amount() != 0)) {
    // Emit a shift instruction if moving a shifted register. This operation
    // could also be achieved using an orr instruction (like orn used by Mvn),
    // but using a shift instruction makes the disassembly clearer.
    EmitShift(dst, operand.reg(), operand.shift(), operand.shift_amount());
  } else if (operand.IsExtendedRegister()) {
    // Emit an extend instruction if moving an extended register. This handles
    // extend with post-shift operations, too.
    EmitExtendShift(dst, operand.reg(), operand.extend(),
                    operand.shift_amount());
  } else {
    // Otherwise, emit a register move only if the registers are distinct, or
    // if they are not X registers.
    //
    // Note that mov(w0, w0) is not a no-op because it clears the top word of
    // x0. A flag is provided (kDiscardForSameWReg) if a move between the same W
    // registers is not required to clear the top word of the X register. In
    // this case, the instruction is discarded.
    //
    // If sp is an operand, add #0 is emitted, otherwise, orr #0.
    if (rd != operand.reg() ||
        (rd.Is32Bits() && (discard_mode == kDontDiscardForSameWReg))) {
      Assembler::mov(rd, operand.reg());
    }
    // This case can handle writes into the system stack pointer directly.
    dst = rd;
  }

  // Copy the result to the system stack pointer.
  if (dst != rd) {
    DCHECK(rd.IsSP());
    Assembler::mov(rd, dst);
  }
}

void MacroAssembler::Mov(const Register& rd, Tagged<Smi> smi) {
  return Mov(rd, Operand(smi));
}

void MacroAssembler::Movi16bitHelper(const VRegister& vd, uint64_t imm) {
  DCHECK(is_uint16(imm));
  int byte1 = (imm & 0xFF);
  int byte2 = ((imm >> 8) & 0xFF);
  if (byte1 == byte2) {
    movi(vd.Is64Bits() ? vd.V8B() : vd.V16B(), byte1);
  } else if (byte1 == 0) {
    movi(vd, byte2, LSL, 8);
  } else if (byte2 == 0) {
    movi(vd, byte1);
  } else if (byte1 == 0xFF) {
    mvni(vd, ~byte2 & 0xFF, LSL, 8);
  } else if (byte2 == 0xFF) {
    mvni(vd, ~byte1 & 0xFF);
  } else {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireW();
    movz(temp, imm);
    dup(vd, temp);
  }
}

void MacroAssembler::Movi32bitHelper(const VRegister& vd, uint64_t imm) {
  DCHECK(is_uint32(imm));

  uint8_t bytes[sizeof(imm)];
  memcpy(bytes, &imm, sizeof(imm));

  // All bytes are either 0x00 or 0xFF.
  {
    bool all0orff = true;
    for (int i = 0; i < 4; ++i) {
      if ((bytes[i] != 0) && (bytes[i] != 0xFF)) {
        all0orff = false;
        break;
      }
    }

    if (all0orff == true) {
      movi(vd.Is64Bits() ? vd.V1D() : vd.V2D(), ((imm << 32) | imm));
      return;
    }
  }

  // Of the 4 bytes, only one byte is non-zero.
  for (int i = 0; i < 4; i++) {
    if ((imm & (0xFF << (i * 8))) == imm) {
      movi(vd, bytes[i], LSL, i * 8);
      return;
    }
  }

  // Of the 4 bytes, only one byte is not 0xFF.
  for (int i = 0; i < 4; i++) {
    uint32_t mask = ~(0xFF << (i * 8));
    if ((imm & mask) == mask) {
      mvni(vd, ~bytes[i] & 0xFF, LSL, i * 8);
      return;
    }
  }

  // Immediate is of the form 0x00MMFFFF.
  if ((imm & 0xFF00FFFF) == 0x0000FFFF) {
    movi(vd, bytes[2], MSL, 16);
    return;
  }

  // Immediate is of the form 0x0000MMFF.
  if ((imm & 0xFFFF00FF) == 0x000000FF) {
    movi(vd, bytes[1], MSL, 8);
    return;
  }

  // Immediate is of the form 0xFFMM0000.
  if ((imm & 0xFF00FFFF) == 0xFF000000) {
    mvni(vd, ~bytes[2] & 0xFF, MSL, 16);
    return;
  }
  // Immediate is of the form 0xFFFFMM00.
  if ((imm & 0xFFFF00FF) == 0xFFFF0000) {
    mvni(vd, ~bytes[1] & 0xFF, MSL, 8);
    return;
  }

  // Top and bottom 16-bits are equal.
  if (((imm >> 16) & 0xFFFF) == (imm & 0xFFFF)) {
    Movi16bitHelper(vd.Is64Bits() ? vd.V4H() : vd.V8H(), imm & 0xFFFF);
    return;
  }

  // Default case.
  {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireW();
    Mov(temp, imm);
    dup(vd, temp);
  }
}

void MacroAssembler::Movi64bitHelper(const VRegister& vd, uint64_t imm) {
  // All bytes are either 0x00 or 0xFF.
  {
    bool all0orff = true;
    for (int i = 0; i < 8; ++i) {
      int byteval = (imm >> (i * 8)) & 0xFF;
      if (byteval != 0 && byteval != 0xFF) {
        all0orff = false;
        break;
      }
    }
    if (all0orff == true) {
      movi(vd, imm);
      return;
    }
  }

  // Top and bottom 32-bits are equal.
  if (((imm >> 32) & 0xFFFFFFFF) == (imm & 0xFFFFFFFF)) {
    Movi32bitHelper(vd.Is64Bits() ? vd.V2S() : vd.V4S(), imm & 0xFFFFFFFF);
    return;
  }

  // Default case.
  {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireX();
    Mov(temp, imm);
    if (vd.Is1D()) {
      fmov(vd.D(), temp);
    } else {
      dup(vd.V2D(), temp);
    }
  }
}

void MacroAssembler::Movi(const VRegister& vd, uint64_t imm, Shift shift,
                          int shift_amount) {
  DCHECK(allow_macro_instructions());
  if (shift_amount != 0 || shift != LSL) {
    movi(vd, imm, shift, shift_amount);
  } else if (vd.Is8B() || vd.Is16B()) {
    // 8-bit immediate.
    DCHECK(is_uint8(imm));
    movi(vd, imm);
  } else if (vd.Is4H() || vd.Is8H()) {
    // 16-bit immediate.
    Movi16bitHelper(vd, imm);
  } else if (vd.Is2S() || vd.Is4S()) {
    // 32-bit immediate.
    Movi32bitHelper(vd, imm);
  } else {
    // 64-bit immediate.
    Movi64bitHelper(vd, imm);
  }
}

void MacroAssembler::Movi(const VRegister& vd, uint64_t hi, uint64_t lo) {
  // TODO(v8:11033): Move 128-bit values in a more efficient way.
  DCHECK(vd.Is128Bits());
  if (hi == lo) {
    Movi(vd.V2D(), lo);
    return;
  }

  Movi(vd.V1D(), lo);

  if (hi != 0) {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireX();
    Mov(temp, hi);
    Ins(vd.V2D(), 1, temp);
  }
}

void MacroAssembler::Mvn(const Register& rd, const Operand& operand) {
  DCHECK(allow_macro_instructions());

  if (operand.NeedsRelocation(this)) {
    Ldr(rd, operand.immediate());
    mvn(rd, rd);

  } else if (operand.IsImmediate()) {
    // Call the macro assembler for generic immediates.
    Mov(rd, ~operand.ImmediateValue());

  } else if (operand.IsExtendedRegister()) {
    // Emit two instructions for the extend case. This differs from Mov, as
    // the extend and invert can't be achieved in one instruction.
    EmitExtendShift(rd, operand.reg(), operand.extend(),
                    operand.shift_amount());
    mvn(rd, rd);

  } else {
    mvn(rd, operand);
  }
}

unsigned MacroAssembler::CountSetHalfWords(uint64_t imm, unsigned reg_size) {
  DCHECK_EQ(reg_size % 16, 0);

#define HALFWORD(idx) (((imm >> ((idx)*16)) & 0xFFFF) ? 1u : 0u)
  switch (reg_size / 16) {
    case 1:
      return HALFWORD(0);
    case 2:
      return HALFWORD(0) + HALFWORD(1);
    case 4:
      return HALFWORD(0) + HALFWORD(1) + HALFWORD(2) + HALFWORD(3);
  }
#undef HALFWORD
  UNREACHABLE();
}

// The movz instruction can generate immediates containing an arbitrary 16-bit
// half-word, with remaining bits clear, eg. 0x00001234, 0x0000123400000000.
bool MacroAssembler::IsImmMovz(uint64_t imm, unsigned reg_size) {
  DCHECK((reg_size == kXRegSizeInBits) || (reg_size == kWRegSizeInBits));
  return CountSetHalfWords(imm, reg_size) <= 1;
}

// The movn instruction can generate immediates containing an arbitrary 16-bit
// half-word, with remaining bits set, eg. 0xFFFF1234, 0xFFFF1234FFFFFFFF.
bool MacroAssembler::IsImmMovn(uint64_t imm, unsigned reg_size) {
  return IsImmMovz(~imm, reg_size);
}

void MacroAssembler::ConditionalCompareMacro(const Register& rn,
                                             const Operand& operand,
                                             StatusFlags nzcv, Condition cond,
                                             ConditionalCompareOp op) {
  DCHECK((cond != al) && (cond != nv));
  if (operand.NeedsRelocation(this)) {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireX();
    Ldr(temp, operand.immediate());
    ConditionalCompareMacro(rn, temp, nzcv, cond, op);

  } else if ((operand.IsShiftedRegister() && (operand.shift_amount() == 0)) ||
             (operand.IsImmediate() &&
              IsImmConditionalCompare(operand.ImmediateValue()))) {
    // The immediate can be encoded in the instruction, or the operand is an
    // unshifted register: call the assembler.
    ConditionalCompare(rn, operand, nzcv, cond, op);

  } else {
    // The operand isn't directly supported by the instruction: perform the
    // operation on a temporary register.
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireSameSizeAs(rn);
    Mov(temp, operand);
    ConditionalCompare(rn, temp, nzcv, cond, op);
  }
}

void MacroAssembler::Csel(const Register& rd, const Register& rn,
                          const Operand& operand, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());
  DCHECK((cond != al) && (cond != nv));
  if (operand.IsImmediate()) {
    // Immediate argument. Handle special cases of 0, 1 and -1 using zero
    // register.
    int64_t imm = operand.ImmediateValue();
    Register zr = AppropriateZeroRegFor(rn);
    if (imm == 0) {
      csel(rd, rn, zr, cond);
    } else if (imm == 1) {
      csinc(rd, rn, zr, cond);
    } else if (imm == -1) {
      csinv(rd, rn, zr, cond);
    } else {
      UseScratchRegisterScope temps(this);
      Register temp = temps.AcquireSameSizeAs(rn);
      Mov(temp, imm);
      csel(rd, rn, temp, cond);
    }
  } else if (operand.IsShiftedRegister() && (operand.shift_amount() == 0)) {
    // Unshifted register argument.
    csel(rd, rn, operand.reg(), cond);
  } else {
    // All other arguments.
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireSameSizeAs(rn);
    Mov(temp, operand);
    csel(rd, rn, temp, cond);
  }
}

bool MacroAssembler::TryOneInstrMoveImmediate(const Register& dst,
                                              int64_t imm) {
  unsigned n, imm_s, imm_r;
  int reg_size = dst.SizeInBits();
  if (IsImmMovz(imm, reg_size) && !dst.IsSP()) {
    // Immediate can be represented in a move zero instruction. Movz can't write
    // to the stack pointer.
    movz(dst, imm);
    return true;
  } else if (IsImmMovn(imm, reg_size) && !dst.IsSP()) {
    // Immediate can be represented in a move not instruction. Movn can't write
    // to the stack pointer.
    movn(dst, dst.Is64Bits() ? ~imm : (~imm & kWRegMask));
    return true;
  } else if (IsImmLogical(imm, reg_size, &n, &imm_s, &imm_r)) {
    // Immediate can be represented in a logical orr instruction.
    LogicalImmediate(dst, AppropriateZeroRegFor(dst), n, imm_s, imm_r, ORR);
    return true;
  }
  return false;
}

Operand MacroAssembler::MoveImmediateForShiftedOp(const Register& dst,
                                                  int64_t imm,
                                                  PreShiftImmMode mode) {
  int reg_size = dst.SizeInBits();
  // Encode the immediate in a single move instruction, if possible.
  if (TryOneInstrMoveImmediate(dst, imm)) {
    // The move was successful; nothing to do here.
  } else {
    // Pre-shift the immediate to the least-significant bits of the register.
    int shift_low;
    if (reg_size == 64) {
      shift_low = base::bits::CountTrailingZeros(imm);
    } else {
      DCHECK_EQ(reg_size, 32);
      shift_low = base::bits::CountTrailingZeros(static_cast<uint32_t>(imm));
    }

    if (mode == kLimitShiftForSP) {
      // When applied to the stack pointer, the subsequent arithmetic operation
      // can use the extend form to shift left by a maximum of four bits. Right
      // shifts are not allowed, so we filter them out later before the new
      // immediate is tested.
      shift_low = std::min(shift_low, 4);
    }
    int64_t imm_low = imm >> shift_low;

    // Pre-shift the immediate to the most-significant bits of the register. We
    // insert set bits in the least-significant bits, as this creates a
    // different immediate that may be encodable using movn or orr-immediate.
    // If this new immediate is encodable, the set bits will be eliminated by
    // the post shift on the following instruction.
    int shift_high = CountLeadingZeros(imm, reg_size);
    int64_t imm_high = (imm << shift_high) | ((INT64_C(1) << shift_high) - 1);

    if ((mode != kNoShift) && TryOneInstrMoveImmediate(dst, imm_low)) {
      // The new immediate has been moved into the destination's low bits:
      // return a new leftward-shifting operand.
      return Operand(dst, LSL, shift_low);
    } else if ((mode == kAnyShift) && TryOneInstrMoveImmediate(dst, imm_high)) {
      // The new immediate has been moved into the destination's high bits:
      // return a new rightward-shifting operand.
      return Operand(dst, LSR, shift_high);
    } else {
      // Use the generic move operation to set up the immediate.
      Mov(dst, imm);
    }
  }
  return Operand(dst);
}

void MacroAssembler::AddSubMacro(const Register& rd, const Register& rn,
                                 const Operand& operand, FlagsUpdate S,
                                 AddSubOp op) {
  if (operand.IsZero() && rd == rn && rd.Is64Bits() && rn.Is64Bits() &&
      !operand.NeedsRelocation(this) && (S == LeaveFlags)) {
    // The instruction would be a nop. Avoid generating useless code.
    return;
  }

  if (operand.NeedsRelocation(this)) {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireSameSizeAs(rn);
    DCHECK_IMPLIES(temp.IsW(), RelocInfo::IsCompressedEmbeddedObject(
                                   operand.ImmediateRMode()));
    Ldr(temp, operand.immediate());
    AddSubMacro(rd, rn, temp, S, op);
  } else if ((operand.IsImmediate() &&
              !IsImmAddSub(operand.ImmediateValue())) ||
             (rn.IsZero() && !operand.IsShiftedRegister()) ||
             (operand.IsShiftedRegister() && (operand.shift() == ROR))) {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireSameSizeAs(rn);
    if (operand.IsImmediate()) {
      PreShiftImmMode mode = kAnyShift;

      // If the destination or source register is the stack pointer, we can
      // only pre-shift the immediate right by values supported in the add/sub
      // extend encoding.
      if (rd == sp) {
        // If the destination is SP and flags will be set, we can't pre-shift
        // the immediate at all.
        mode = (S == SetFlags) ? kNoShift : kLimitShiftForSP;
      } else if (rn == sp) {
        mode = kLimitShiftForSP;
      }

      Operand imm_operand =
          MoveImmediateForShiftedOp(temp, operand.ImmediateValue(), mode);
      AddSub(rd, rn, imm_operand, S, op);
    } else {
      Mov(temp, operand);
      AddSub(rd, rn, temp, S, op);
    }
  } else {
    AddSub(rd, rn, operand, S, op);
  }
}

void MacroAssembler::AddSubWithCarryMacro(const Register& rd,
                                          const Register& rn,
                                          const Operand& operand, FlagsUpdate S,
                                          AddSubWithCarryOp op) {
  DCHECK(rd.SizeInBits() == rn.SizeInBits());
  UseScratchRegisterScope temps(this);

  if (operand.NeedsRelocation(this)) {
    Register temp = temps.AcquireX();
    Ldr(temp, operand.immediate());
    AddSubWithCarryMacro(rd, rn, temp, S, op);

  } else if (operand.IsImmediate() ||
             (operand.IsShiftedRegister() && (operand.shift() == ROR))) {
    // Add/sub with carry (immediate or ROR shifted register.)
    Register temp = temps.AcquireSameSizeAs(rn);
    Mov(temp, operand);
    AddSubWithCarry(rd, rn, temp, S, op);

  } else if (operand.IsShiftedRegister() && (operand.shift_amount() != 0)) {
    // Add/sub with carry (shifted register).
    DCHECK(operand.reg().SizeInBits() == rd.SizeInBits());
    DCHECK(operand.shift() != ROR);
    DCHECK(is_uintn(operand.shift_amount(), rd.SizeInBits() == kXRegSizeInBits
                                                ? kXRegSizeInBitsLog2
                                                : kWRegSizeInBitsLog2));
    Register temp = temps.AcquireSameSizeAs(rn);
    EmitShift(temp, operand.reg(), operand.shift(), operand.shift_amount());
    AddSubWithCarry(rd, rn, temp, S, op);

  } else if (operand.IsExtendedRegister()) {
    // Add/sub with carry (extended register).
    DCHECK(operand.reg().SizeInBits() <= rd.SizeInBits());
    // Add/sub extended supports a shift <= 4. We want to support exactly the
    // same modes.
    DCHECK_LE(operand.shift_amount(), 4);
    DCHECK(operand.reg().Is64Bits() ||
           ((operand.extend() != UXTX) && (operand.extend() != SXTX)));
    Register temp = temps.AcquireSameSizeAs(rn);
    EmitExtendShift(temp, operand.reg(), operand.extend(),
                    operand.shift_amount());
    AddSubWithCarry(rd, rn, temp, S, op);

  } else {
    // The addressing mode is directly supported by the instruction.
    AddSubWithCarry(rd, rn, operand, S, op);
  }
}

void MacroAssembler::LoadStoreMacro(const CPURegister& rt,
                                    const MemOperand& addr, LoadStoreOp op) {
  // Call the most common addressing modes used by Liftoff directly for improved
  // compilation performance: X register + immediate, X register + W register.
  Instr memop = op | Rt(rt) | RnSP(addr.base());
  if (addr.IsImmediateOffset()) {
    int64_t offset = addr.offset();
    unsigned size_log2 = CalcLSDataSizeLog2(op);
    if (IsImmLSScaled(offset, size_log2)) {
      LoadStoreScaledImmOffset(memop, static_cast<int>(offset), size_log2);
      return;
    } else if (IsImmLSUnscaled(offset)) {
      LoadStoreUnscaledImmOffset(memop, static_cast<int>(offset));
      return;
    }
  } else if (addr.IsRegisterOffset() && (addr.extend() == UXTW) &&
             (addr.shift_amount() == 0)) {
    LoadStoreWRegOffset(memop, addr.regoffset());
    return;
  }

  // Remaining complex cases handled in sub-function.
  LoadStoreMacroComplex(rt, addr, op);
}

void MacroAssembler::LoadStoreMacroComplex(const CPURegister& rt,
                                           const MemOperand& addr,
                                           LoadStoreOp op) {
  int64_t offset = addr.offset();
  bool is_imm_unscaled = IsImmLSUnscaled(offset);
  if (addr.IsRegisterOffset() ||
      (is_imm_unscaled && (addr.IsPostIndex() || addr.IsPreIndex()))) {
    // Load/store encodable in one instruction.
    LoadStore(rt, addr, op);
  } else if (addr.IsImmediateOffset()) {
    // Load/stores with immediate offset addressing should have been handled by
    // the caller.
    DCHECK(!IsImmLSScaled(offset, CalcLSDataSizeLog2(op)) && !is_imm_unscaled);
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireSameSizeAs(addr.base());
    Mov(temp, offset);
    LoadStore(rt, MemOperand(addr.base(), temp), op);
  } else if (addr.IsPostIndex()) {
    // Post-index beyond unscaled addressing range.
    DCHECK(!is_imm_unscaled);
    LoadStore(rt, MemOperand(addr.base()), op);
    add(addr.base(), addr.base(), offset);
  } else {
    // Pre-index beyond unscaled addressing range.
    DCHECK(!is_imm_unscaled && addr.IsPreIndex());
    add(addr.base(), addr.base(), offset);
    LoadStore(rt, MemOperand(addr.base()), op);
  }
}

void MacroAssembler::LoadStorePairMacro(const CPURegister& rt,
                                        const CPURegister& rt2,
                                        const MemOperand& addr,
                                        LoadStorePairOp op) {
  if (addr.IsRegisterOffset()) {
    UseScratchRegisterScope temps(this);
    Register base = addr.base();
    Register temp = temps.AcquireSameSizeAs(base);
    Add(temp, base, addr.regoffset());
    LoadStorePair(rt, rt2, MemOperand(temp), op);
    return;
  }

  int64_t offset = addr.offset();
  unsigned size = CalcLSPairDataSize(op);

  // Check if the offset fits in the immediate field of the appropriate
  // instruction. If not, emit two instructions to perform the operation.
  if (IsImmLSPair(offset, size)) {
    // Encodable in one load/store pair instruction.
    LoadStorePair(rt, rt2, addr, op);
  } else {
    Register base = addr.base();
    if (addr.IsImmediateOffset()) {
      UseScratchRegisterScope temps(this);
      Register temp = temps.AcquireSameSizeAs(base);
      Add(temp, base, offset);
      LoadStorePair(rt, rt2, MemOperand(temp), op);
    } else if (addr.IsPostIndex()) {
      LoadStorePair(rt, rt2, MemOperand(base), op);
      Add(base, base, offset);
    } else {
      DCHECK(addr.IsPreIndex());
      Add(base, base, offset);
      LoadStorePair(rt, rt2, MemOperand(base), op);
    }
  }
}

void MacroAssembler::Adr(const Register& rd, Label* label, AdrHint hint) {
  DCHECK(allow_macro_instructions());
  DCHECK(!rd.IsZero());

  if (hint == kAdrNear) {
    adr(rd, label);
    return;
  }

  DCHECK_EQ(hint, kAdrFar);
  if (label->is_bound()) {
    int label_offset = label->pos() - pc_offset();
    if (Instruction::IsValidPCRelOffset(label_offset)) {
      adr(rd, label);
    } else {
      DCHECK_LE(label_offset, 0);
      int min_adr_offset = -(1 << (Instruction::ImmPCRelRangeBitwidth - 1));
      adr(rd, min_adr_offset);
      Add(rd, rd, label_offset - min_adr_offset);
    }
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.AcquireX();

    InstructionAccurateScope scope(this,
                                   PatchingAssembler::kAdrFarPatchableNInstrs);
    adr(rd, label);
    for (int i = 0; i < PatchingAssembler::kAdrFarPatchableNNops; ++i) {
      nop(ADR_FAR_NOP);
    }
    movz(scratch, 0);
  }
}

void MacroAssembler::B(Label* label, BranchType type, Register reg, int bit) {
  DCHECK((reg == NoReg || type >= kBranchTypeFirstUsingReg) &&
         (bit == -1 || type >= kBranchTypeFirstUsingBit));
  if (kBranchTypeFirstCondition <= type && type <= kBranchTypeLastCondition) {
    B(static_cast<Condition>(type), label);
  } else {
    switch (type) {
      case always:
        B(label);
        break;
      case never:
        break;
      case reg_zero:
        Cbz(reg, label);
        break;
      case reg_not_zero:
        Cbnz(reg, label);
        break;
      case reg_bit_clear:
        Tbz(reg, bit, label);
        break;
      case reg_bit_set:
        Tbnz(reg, bit, label);
        break;
      default:
        UNREACHABLE();
    }
  }
}

void MacroAssembler::B(Label* label, Condition cond) {
  DCHECK(allow_macro_instructions());
  DCHECK((cond != al) && (cond != nv));

  bool need_extra_instructions =
      NeedExtraInstructionsOrRegisterBranch<CondBranchType>(label);

  if (V8_UNLIKELY(need_extra_instructions)) {
    Label done;
    b(&done, NegateCondition(cond));
    B(label);
    bind(&done);
  } else {
    b(label, cond);
  }
}

void MacroAssembler::Tbnz(const Register& rt, unsigned bit_pos, Label* label) {
  DCHECK(allow_macro_instructions());

  bool need_extra_instructions =
      NeedExtraInstructionsOrRegisterBranch<TestBranchType>(label);

  if (V8_UNLIKELY(need_extra_instructions)) {
    Label done;
    tbz(rt, bit_pos, &done);
    B(label);
    bind(&done);
  } else {
    tbnz(rt, bit_pos, label);
  }
}

void MacroAssembler::Tbz(const Register& rt, unsigned bit_pos, Label* label) {
  DCHECK(allow_macro_instructions());

  bool need_extra_instructions =
      NeedExtraInstructionsOrRegisterBranch<TestBranchType>(label);

  if (V8_UNLIKELY(need_extra_instructions)) {
    Label done;
    tbnz(rt, bit_pos, &done);
    B(label);
    bind(&done);
  } else {
    tbz(rt, bit_pos, label);
  }
}

void MacroAssembler::Cbnz(const Register& rt, Label* label) {
  DCHECK(allow_macro_instructions());

  bool need_extra_instructions =
      NeedExtraInstructionsOrRegisterBranch<CompareBranchType>(label);

  if (V8_UNLIKELY(need_extra_instructions)) {
    Label done;
    cbz(rt, &done);
    B(label);
    bind(&done);
  } else {
    cbnz(rt, label);
  }
}

void MacroAssembler::Cbz(const Register& rt, Label* label) {
  DCHECK(allow_macro_instructions());

  bool need_extra_instructions =
      NeedExtraInstructionsOrRegisterBranch<CompareBranchType>(label);

  if (V8_UNLIKELY(need_extra_instructions)) {
    Label done;
    cbnz(rt, &done);
    B(label);
    bind(&done);
  } else {
    cbz(rt, label);
  }
}

// Pseudo-instructions.

void MacroAssembler::Abs(const Register& rd, const Register& rm,
                         Label* is_not_representable, Label* is_representable) {
  DCHECK(allow_macro_instructions());
  DCHECK(AreSameSizeAndType(rd, rm));

  Cmp(rm, 1);
  Cneg(rd, rm, lt);

  // If the comparison sets the v flag, the input was the smallest value
  // representable by rm, and the mathematical result of abs(rm) is not
  // representable using two's complement.
  if ((is_not_representable != nullptr) && (is_representable != nullptr)) {
    B(is_not_representable, vs);
    B(is_representable);
  } else if (is_not_representable != nullptr) {
    B(is_not_representable, vs);
  } else if (is_representable != nullptr) {
    B(is_representable, vc);
  }
}

void MacroAssembler::Switch(Register scratch, Register value,
                            int case_value_base, Label** labels,
                            int num_labels) {
  Register table = scratch;
  Label fallthrough, jump_table;
  if (case_value_base != 0) {
    Sub(value, value, case_value_base);
  }
  Cmp(value, Immediate(num_labels));
  B(&fallthrough, hs);
  Adr(table, &jump_table);
  Ldr(table, MemOperand(table, value, LSL, kSystemPointerSizeLog2));
  Br(table);
  // Emit the jump table inline, under the assumption that it's not too big.
  // Make sure there are no veneer pool entries in the middle of the table.
  const int jump_table_size = num_labels * kSystemPointerSize;
  CheckVeneerPool(false, false, jump_table_size);
  BlockPoolsScope no_pool_inbetween(this, jump_table_size);
  Align(kSystemPointerSize);
  bind(&jump_table);
  for (int i = 0; i < num_labels; ++i) {
    dcptr(labels[i]);
  }
  bind(&fallthrough);
}

// Abstracted stack operations.

void MacroAssembler::Push(const CPURegister& src0, const CPURegister& src1,
                          const CPURegister& src2, const CPURegister& src3,
                          const CPURegister& src4, const CPURegister& src5,
                          const CPURegister& src6, const CPURegister& src7) {
  DCHECK(AreSameSizeAndType(src0, src1, src2, src3, src4, src5, src6, src7));

  int count = 5 + src5.is_valid() + src6.is_valid() + src6.is_valid();
  int size = src0.SizeInBytes();
  DCHECK_EQ(0, (size * count) % 16);

  PushHelper(4, size, src0, src1, src2, src3);
  PushHelper(count - 4, size, src4, src5, src6, src7);
}

void MacroAssembler::Pop(const CPURegister& dst0, const CPURegister& dst1,
                         const CPURegister& dst2, const CPURegister& dst3,
                         const CPURegister& dst4, const CPURegister& dst5,
                         const CPURegister& dst6, const CPURegister& dst7) {
  // It is not valid to pop into the same register more than once in one
  // instruction, not even into the zero register.
  DCHECK(!AreAliased(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7));
  DCHECK(AreSameSizeAndType(dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7));
  DCHECK(dst0.is_valid());

  int count = 5 + dst5.is_valid() + dst6.is_valid() + dst7.is_valid();
  int size = dst0.SizeInBytes();
  DCHECK_EQ(0, (size * count) % 16);

  PopHelper(4, size, dst0, dst1, dst2, dst3);
  PopHelper(count - 4, size, dst4, dst5, dst6, dst7);
}

void MacroAssembler::PushMultipleTimes(CPURegister src, Register count) {
  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireSameSizeAs(count);

  Label loop, leftover2, leftover1, done;

  Subs(temp, count, 4);
  B(mi, &leftover2);

  // Push groups of four first.
  Bind(&loop);
  Subs(temp, temp, 4);
  PushHelper(4, src.SizeInBytes(), src, src, src, src);
  B(pl, &loop);

  // Push groups of two.
  Bind(&leftover2);
  Tbz(count, 1, &leftover1);
  PushHelper(2, src.SizeInBytes(), src, src, NoReg, NoReg);

  // Push the last one (if required).
  Bind(&leftover1);
  Tbz(count, 0, &done);
  PushHelper(1, src.SizeInBytes(), src, NoReg, NoReg, NoReg);

  Bind(&done);
}

void MacroAssembler::PushHelper(int count, int size, const CPURegister& src0,
                                const CPURegister& src1,
                                const CPURegister& src2,
                                const CPURegister& src3) {
  // Ensure that we don't unintentially modify scratch or debug registers.
  InstructionAccurateScope scope(this);

  DCHECK(AreSameSizeAndType(src0, src1, src2, src3));
  DCHECK(size == src0.SizeInBytes());

  // When pushing multiple registers, the store order is chosen such that
  // Push(a, b) is equivalent to Push(a) followed by Push(b).
  switch (count) {
    case 1:
      DCHECK(src1.IsNone() && src2.IsNone() && src3.IsNone());
      str(src0, MemOperand(sp, -1 * size, PreIndex));
      break;
    case 2:
      DCHECK(src2.IsNone() && src3.IsNone());
      stp(src1, src0, MemOperand(sp, -2 * size, PreIndex));
      break;
    case 3:
      DCHECK(src3.IsNone());
      stp(src2, src1, MemOperand(sp, -3 * size, PreIndex));
      str(src0, MemOperand(sp, 2 * size));
      break;
    case 4:
      // Skip over 4 * size, then fill in the gap. This allows four W registers
      // to be pushed using sp, whilst maintaining 16-byte alignment for sp
      // at all times.
      stp(src3, src2, MemOperand(sp, -4 * size, PreIndex));
      stp(src1, src0, MemOperand(sp, 2 * size));
      break;
    default:
      UNREACHABLE();
  }
}

void MacroAssembler::PopHelper(int count, int size, const CPURegister& dst0,
                               const CPURegister& dst1, const CPURegister& dst2,
                               const CPURegister& dst3) {
  // Ensure that we don't unintentially modify scratch or debug registers.
  InstructionAccurateScope scope(this);

  DCHECK(AreSameSizeAndType(dst0, dst1, dst2, dst3));
  DCHECK(size == dst0.SizeInBytes());

  // When popping multiple registers, the load order is chosen such that
  // Pop(a, b) is equivalent to Pop(a) followed by Pop(b).
  switch (count) {
    case 1:
      DCHECK(dst1.IsNone() && dst2.IsNone() && dst3.IsNone());
      ldr(dst0, MemOperand(sp, 1 * size, PostIndex));
      break;
    case 2:
      DCHECK(dst2.IsNone() && dst3.IsNone());
      ldp(dst0, dst1, MemOperand(sp, 2 * size, PostIndex));
      break;
    case 3:
      DCHECK(dst3.IsNone());
      ldr(dst2, MemOperand(sp, 2 * size));
      ldp(dst0, dst1, MemOperand(sp, 3 * size, PostIndex));
      break;
    case 4:
      // Load the higher addresses first, then load the lower addresses and
      // skip the whole block in the second instruction. This allows four W
      // registers to be popped using sp, whilst maintaining 16-byte alignment
      // for sp at all times.
      ldp(dst2, dst3, MemOperand(sp, 2 * size));
      ldp(dst0, dst1, MemOperand(sp, 4 * size, PostIndex));
      break;
    default:
      UNREACHABLE();
  }
}

void MacroAssembler::PokePair(const CPURegister& src1, const CPURegister& src2,
                              int offset) {
  DCHECK(AreSameSizeAndType(src1, src2));
  DCHECK((offset >= 0) && ((offset % src1.SizeInBytes()) == 0));
  Stp(src1, src2, MemOperand(sp, offset));
}

void MacroAssembler::PeekPair(const CPURegister& dst1, const CPURegister& dst2,
                              int offset) {
  DCHECK(AreSameSizeAndType(dst1, dst2));
  DCHECK((offset >= 0) && ((offset % dst1.SizeInBytes()) == 0));
  Ldp(dst1, dst2, MemOperand(sp, offset));
}

void MacroAssembler::PushCalleeSavedRegisters() {
  ASM_CODE_COMMENT(this);
  // Ensure that the macro-assembler doesn't use any scratch registers.
  InstructionAccurateScope scope(this);

  MemOperand tos(sp, -2 * static_cast<int>(kXRegSize), PreIndex);

  stp(d14, d15, tos);
  stp(d12, d13, tos);
  stp(d10, d11, tos);
  stp(d8, d9, tos);

  stp(x27, x28, tos);
  stp(x25, x26, tos);
  stp(x23, x24, tos);
  stp(x21, x22, tos);
  stp(x19, x20, tos);

  static_assert(
      EntryFrameConstants::kCalleeSavedRegisterBytesPushedBeforeFpLrPair ==
      18 * kSystemPointerSize);

#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
    // Use the stack pointer's value immediately before pushing the LR as the
    // context for signing it. This is what the StackFrameIterator expects.
    pacibsp();
#endif

    stp(x29, x30, tos);  // fp, lr

    static_assert(
        EntryFrameConstants::kCalleeSavedRegisterBytesPushedAfterFpLrPair == 0);
}

void MacroAssembler::PopCalleeSavedRegisters() {
  ASM_CODE_COMMENT(this);
  // Ensure that the macro-assembler doesn't use any scratch registers.
  InstructionAccurateScope scope(this);

  MemOperand tos(sp, 2 * kXRegSize, PostIndex);

  ldp(x29, x30, tos);  // fp, lr

#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  // The context (stack pointer value) for authenticating the LR here must
  // match the one used for signing it (see `PushCalleeSavedRegisters`).
  autibsp();
#endif

    ldp(x19, x20, tos);
    ldp(x21, x22, tos);
    ldp(x23, x24, tos);
    ldp(x25, x26, tos);
    ldp(x27, x28, tos);

    ldp(d8, d9, tos);
    ldp(d10, d11, tos);
    ldp(d12, d13, tos);
    ldp(d14, d15, tos);
}

namespace {

void TailCallOptimizedCodeSlot(MacroAssembler* masm,
                               Register optimized_code_entry,
                               Register scratch) {
  // ----------- S t a t e -------------
  //  -- x0 : actual argument count
  //  -- x3 : new target (preserved for callee if needed, and caller)
  //  -- x1 : target function (preserved for callee if needed, and caller)
  // -----------------------------------
  ASM_CODE_COMMENT(masm);
  DCHECK(!AreAliased(x1, x3, optimized_code_entry, scratch));

  Register closure = x1;
  Label heal_optimized_code_slot;

  // If the optimized code is cleared, go to runtime to update the optimization
  // marker field.
  __ LoadWeakValue(optimized_code_entry, optimized_code_entry,
                   &heal_optimized_code_slot);

  // The entry references a CodeWrapper object. Unwrap it now.
  __ LoadCodePointerField(
      optimized_code_entry,
      FieldMemOperand(optimized_code_entry, CodeWrapper::kCodeOffset));

  // Check if the optimized code is marked for deopt. If it is, call the
  // runtime to clear it.
  __ AssertCode(optimized_code_entry);
  __ JumpIfCodeIsMarkedForDeoptimization(optimized_code_entry, scratch,
                                         &heal_optimized_code_slot);

  // Optimized code is good, get it into the closure and link the closure into
  // the optimized functions list, then tail call the optimized code.
  __ ReplaceClosureCodeWithOptimizedCode(optimized_code_entry, closure);
  static_assert(kJavaScriptCallCodeStartRegister == x2, "ABI mismatch");
  __ Move(x2, optimized_code_entry);
  __ JumpCodeObject(x2, kJSEntrypointTag);

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
    IsObjectType(object, scratch, scratch, FEEDBACK_CELL_TYPE);
    Assert(eq, AbortReason::kExpectedFeedbackCell);
  }
}
void MacroAssembler::AssertFeedbackVector(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    IsObjectType(object, scratch, scratch, FEEDBACK_VECTOR_TYPE);
    Assert(eq, AbortReason::kExpectedFeedbackVector);
  }
}
#endif  // V8_ENABLE_DEBUG_CODE

void MacroAssembler::ReplaceClosureCodeWithOptimizedCode(
    Register optimized_code, Register closure) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(optimized_code, closure));
  // Store code entry in the closure.
  AssertCode(optimized_code);
  StoreCodePointerField(optimized_code,
                        FieldMemOperand(closure, JSFunction::kCodeOffset));
  RecordWriteField(closure, JSFunction::kCodeOffset, optimized_code,
                   kLRHasNotBeenSaved, SaveFPRegsMode::kIgnore, SmiCheck::kOmit,
                   SlotDescriptor::ForCodePointerSlot());
}

void MacroAssembler::GenerateTailCallToReturnedCode(
    Runtime::FunctionId function_id) {
  ASM_CODE_COMMENT(this);
  // ----------- S t a t e -------------
  //  -- x0 : actual argument count
  //  -- x1 : target function (preserved for callee)
  //  -- x3 : new target (preserved for callee)
  // -----------------------------------
  {
    FrameScope scope(this, StackFrame::INTERNAL);
    // Push a copy of the target function, the new target and the actual
    // argument count.
    SmiTag(kJavaScriptCallArgCountRegister);
    Push(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
         kJavaScriptCallArgCountRegister, padreg);
    // Push another copy as a parameter to the runtime call.
    PushArgument(kJavaScriptCallTargetRegister);

    CallRuntime(function_id, 1);
    Mov(x2, x0);

    // Restore target function, new target and actual argument count.
    Pop(padreg, kJavaScriptCallArgCountRegister,
        kJavaScriptCallNewTargetRegister, kJavaScriptCallTargetRegister);
    SmiUntag(kJavaScriptCallArgCountRegister);
  }

  static_assert(kJavaScriptCallCodeStartRegister == x2, "ABI mismatch");
  JumpCodeObject(x2, kJSEntrypointTag);
}

// Read off the flags in the feedback vector and check if there
// is optimized code or a tiering state that needs to be processed.
Condition MacroAssembler::LoadFeedbackVectorFlagsAndCheckIfNeedsProcessing(
    Register flags, Register feedback_vector, CodeKind current_code_kind) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(flags, feedback_vector));
  DCHECK(CodeKindCanTierUp(current_code_kind));
  Ldrh(flags, FieldMemOperand(feedback_vector, FeedbackVector::kFlagsOffset));
  uint32_t kFlagsMask = FeedbackVector::kFlagsTieringStateIsAnyRequested |
                        FeedbackVector::kFlagsMaybeHasTurbofanCode |
                        FeedbackVector::kFlagsLogNextExecution;
  if (current_code_kind != CodeKind::MAGLEV) {
    kFlagsMask |= FeedbackVector::kFlagsMaybeHasMaglevCode;
  }
  Tst(flags, kFlagsMask);
  return ne;
}

void MacroAssembler::LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
    Register flags, Register feedback_vector, CodeKind current_code_kind,
    Label* flags_need_processing) {
  ASM_CODE_COMMENT(this);
  B(LoadFeedbackVectorFlagsAndCheckIfNeedsProcessing(flags, feedback_vector,
                                                     current_code_kind),
    flags_need_processing);
}

void MacroAssembler::OptimizeCodeOrTailCallOptimizedCodeSlot(
    Register flags, Register feedback_vector) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(flags, feedback_vector));
  Label maybe_has_optimized_code, maybe_needs_logging;
  // Check if optimized code is available.
  TestAndBranchIfAllClear(flags,
                          FeedbackVector::kFlagsTieringStateIsAnyRequested,
                          &maybe_needs_logging);
  GenerateTailCallToReturnedCode(Runtime::kCompileOptimized);

  bind(&maybe_needs_logging);
  TestAndBranchIfAllClear(flags, FeedbackVector::LogNextExecutionBit::kMask,
                          &maybe_has_optimized_code);
  GenerateTailCallToReturnedCode(Runtime::kFunctionLogNextExecution);

  bind(&maybe_has_optimized_code);
  Register optimized_code_entry = x7;
  LoadTaggedField(optimized_code_entry,
                  FieldMemOperand(feedback_vector,
                                  FeedbackVector::kMaybeOptimizedCodeOffset));
  TailCallOptimizedCodeSlot(this, optimized_code_entry, x4);
}

Condition MacroAssembler::CheckSmi(Register object) {
  static_assert(kSmiTag == 0);
  Tst(object, kSmiTagMask);
  return eq;
}

#ifdef V8_ENABLE_DEBUG_CODE
void MacroAssembler::AssertSpAligned() {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  HardAbortScope hard_abort(this);  // Avoid calls to Abort.
  // Arm64 requires the stack pointer to be 16-byte aligned prior to address
  // calculation.
  UseScratchRegisterScope scope(this);
  Register temp = scope.AcquireX();
  Mov(temp, sp);
  Tst(temp, 15);
  Check(eq, AbortReason::kUnexpectedStackPointer);
}

void MacroAssembler::AssertFPCRState(Register fpcr) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  Label unexpected_mode, done;
  UseScratchRegisterScope temps(this);
  if (fpcr.IsNone()) {
    fpcr = temps.AcquireX();
    Mrs(fpcr, FPCR);
  }

  // Settings left to their default values:
  //   - Assert that flush-to-zero is not set.
  Tbnz(fpcr, FZ_offset, &unexpected_mode);
  //   - Assert that the rounding mode is nearest-with-ties-to-even.
  static_assert(FPTieEven == 0);
  Tst(fpcr, RMode_mask);
  B(eq, &done);

  Bind(&unexpected_mode);
  Abort(AbortReason::kUnexpectedFPCRMode);

  Bind(&done);
}

void MacroAssembler::AssertSmi(Register object, AbortReason reason) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  static_assert(kSmiTag == 0);
  Tst(object, kSmiTagMask);
  Check(eq, reason);
}

void MacroAssembler::AssertNotSmi(Register object, AbortReason reason) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  static_assert(kSmiTag == 0);
  Tst(object, kSmiTagMask);
  Check(ne, reason);
}

void MacroAssembler::AssertZeroExtended(Register int32_register) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  Tst(int32_register.X(), kMaxUInt32);
  Check(ls, AbortReason::k32BitValueInRegisterIsNotZeroExtended);
}

void MacroAssembler::AssertMap(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  AssertNotSmi(object, AbortReason::kOperandIsNotAMap);

  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();

  IsObjectType(object, temp, temp, MAP_TYPE);
  Check(eq, AbortReason::kOperandIsNotAMap);
}

void MacroAssembler::AssertCode(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  AssertNotSmi(object, AbortReason::kOperandIsNotACode);

  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();

  IsObjectType(object, temp, temp, CODE_TYPE);
  Check(eq, AbortReason::kOperandIsNotACode);
}

void MacroAssembler::AssertConstructor(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  AssertNotSmi(object, AbortReason::kOperandIsASmiAndNotAConstructor);

  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();

  LoadMap(temp, object);
  Ldrb(temp, FieldMemOperand(temp, Map::kBitFieldOffset));
  Tst(temp, Operand(Map::Bits1::IsConstructorBit::kMask));

  Check(ne, AbortReason::kOperandIsNotAConstructor);
}

void MacroAssembler::AssertFunction(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  AssertNotSmi(object, AbortReason::kOperandIsASmiAndNotAFunction);

  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  LoadMap(temp, object);
  CompareInstanceTypeRange(temp, temp, FIRST_JS_FUNCTION_TYPE,
                           LAST_JS_FUNCTION_TYPE);
  Check(ls, AbortReason::kOperandIsNotAFunction);
}

void MacroAssembler::AssertCallableFunction(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  AssertNotSmi(object, AbortReason::kOperandIsASmiAndNotAFunction);

  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  LoadMap(temp, object);
  CompareInstanceTypeRange(temp, temp, FIRST_CALLABLE_JS_FUNCTION_TYPE,
                           LAST_CALLABLE_JS_FUNCTION_TYPE);
  Check(ls, AbortReason::kOperandIsNotACallableFunction);
}

void MacroAssembler::AssertBoundFunction(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  AssertNotSmi(object, AbortReason::kOperandIsASmiAndNotABoundFunction);

  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();

  IsObjectType(object, temp, temp, JS_BOUND_FUNCTION_TYPE);
  Check(eq, AbortReason::kOperandIsNotABoundFunction);
}

void MacroAssembler::AssertSmiOrHeapObjectInMainCompressionCage(
    Register object) {
  if (!PointerCompressionIsEnabled()) return;
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  // We may not have any scratch registers so we preserve our input register.
  Push(object, xzr);
  Label ok;
  B(&ok, CheckSmi(object));
  Mov(object, Operand(object, LSR, 32));
  // Either the value is now equal to the right-shifted pointer compression
  // cage base or it's zero if we got a compressed pointer register as input.
  Cmp(object, 0);
  B(kEqual, &ok);
  Cmp(object, Operand(kPtrComprCageBaseRegister, LSR, 32));
  Check(kEqual, AbortReason::kObjectNotTagged);
  bind(&ok);
  Pop(xzr, object);
}

void MacroAssembler::AssertGeneratorObject(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  AssertNotSmi(object, AbortReason::kOperandIsASmiAndNotAGeneratorObject);

  // Load map
  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  LoadMap(temp, object);

  // Load instance type and check if JSGeneratorObject
  CompareInstanceTypeRange(temp, temp, FIRST_JS_GENERATOR_OBJECT_TYPE,
                           LAST_JS_GENERATOR_OBJECT_TYPE);
  // Restore generator object to register and perform assertion
  Check(ls, AbortReason::kOperandIsNotAGeneratorObject);
}

void MacroAssembler::AssertUndefinedOrAllocationSite(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Label done_checking;
  AssertNotSmi(object);
  JumpIfRoot(object, RootIndex::kUndefinedValue, &done_checking);
  LoadMap(scratch, object);
  CompareInstanceType(scratch, scratch, ALLOCATION_SITE_TYPE);
  Assert(eq, AbortReason::kExpectedUndefinedOrCell);
  Bind(&done_checking);
}

void MacroAssembler::AssertPositiveOrZero(Register value) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  Label done;
  int sign_bit = value.Is64Bits() ? kXSignBit : kWSignBit;
  Tbz(value, sign_bit, &done);
  Abort(AbortReason::kUnexpectedNegativeValue);
  Bind(&done);
}

void MacroAssembler::AssertJSAny(Register object, Register map_tmp,
                                 Register tmp, AbortReason abort_reason) {
  if (!v8_flags.debug_code) return;

  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, map_tmp, tmp));
  Label ok;

  JumpIfSmi(object, &ok);

  LoadMap(map_tmp, object);
  CompareInstanceType(map_tmp, tmp, LAST_NAME_TYPE);
  B(kUnsignedLessThanEqual, &ok);

  CompareInstanceType(map_tmp, tmp, FIRST_JS_RECEIVER_TYPE);
  B(kUnsignedGreaterThanEqual, &ok);

  CompareRoot(map_tmp, RootIndex::kHeapNumberMap);
  B(kEqual, &ok);

  CompareRoot(map_tmp, RootIndex::kBigIntMap);
  B(kEqual, &ok);

  CompareRoot(object, RootIndex::kUndefinedValue);
  B(kEqual, &ok);

  CompareRoot(object, RootIndex::kTrueValue);
  B(kEqual, &ok);

  CompareRoot(object, RootIndex::kFalseValue);
  B(kEqual, &ok);

  CompareRoot(object, RootIndex::kNullValue);
  B(kEqual, &ok);

  Abort(abort_reason);

  bind(&ok);
}

void MacroAssembler::Assert(Condition cond, AbortReason reason) {
  if (v8_flags.debug_code) {
    Check(cond, reason);
  }
}

void MacroAssembler::AssertUnreachable(AbortReason reason) {
  if (v8_flags.debug_code) Abort(reason);
}
#endif  // V8_ENABLE_DEBUG_CODE

void MacroAssembler::CopySlots(int dst, Register src, Register slot_count) {
  DCHECK(!src.IsZero());
  UseScratchRegisterScope scope(this);
  Register dst_reg = scope.AcquireX();
  SlotAddress(dst_reg, dst);
  SlotAddress(src, src);
  CopyDoubleWords(dst_reg, src, slot_count);
}

void MacroAssembler::CopySlots(Register dst, Register src,
                               Register slot_count) {
  DCHECK(!dst.IsZero() && !src.IsZero());
  SlotAddress(dst, dst);
  SlotAddress(src, src);
  CopyDoubleWords(dst, src, slot_count);
}

void MacroAssembler::CopyDoubleWords(Register dst, Register src, Register count,
                                     CopyDoubleWordsMode mode) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(dst, src, count));

  if (v8_flags.debug_code) {
    Register pointer1 = dst;
    Register pointer2 = src;
    if (mode == kSrcLessThanDst) {
      pointer1 = src;
      pointer2 = dst;
    }
    // Copy requires pointer1 < pointer2 || (pointer1 - pointer2) >= count.
    Label pointer1_below_pointer2;
    Subs(pointer1, pointer1, pointer2);
    B(lt, &pointer1_below_pointer2);
    Cmp(pointer1, count);
    Check(ge, AbortReason::kOffsetOutOfRange);
    Bind(&pointer1_below_pointer2);
    Add(pointer1, pointer1, pointer2);
  }
  static_assert(kSystemPointerSize == kDRegSize,
                "pointers must be the same size as doubles");

  if (mode == kDstLessThanSrcAndReverse) {
    Add(src, src, Operand(count, LSL, kSystemPointerSizeLog2));
    Sub(src, src, kSystemPointerSize);
  }

  int src_direction = (mode == kDstLessThanSrc) ? 1 : -1;
  int dst_direction = (mode == kSrcLessThanDst) ? -1 : 1;

  UseScratchRegisterScope scope(this);
  VRegister temp0 = scope.AcquireD();
  VRegister temp1 = scope.AcquireD();

  Label pairs, loop, done;

  Tbz(count, 0, &pairs);
  Ldr(temp0, MemOperand(src, src_direction * kSystemPointerSize, PostIndex));
  Sub(count, count, 1);
  Str(temp0, MemOperand(dst, dst_direction * kSystemPointerSize, PostIndex));

  Bind(&pairs);
  if (mode == kSrcLessThanDst) {
    // Adjust pointers for post-index ldp/stp with negative offset:
    Sub(dst, dst, kSystemPointerSize);
    Sub(src, src, kSystemPointerSize);
  } else if (mode == kDstLessThanSrcAndReverse) {
    Sub(src, src, kSystemPointerSize);
  }
  Bind(&loop);
  Cbz(count, &done);
  Ldp(temp0, temp1,
      MemOperand(src, 2 * src_direction * kSystemPointerSize, PostIndex));
  Sub(count, count, 2);
  if (mode == kDstLessThanSrcAndReverse) {
    Stp(temp1, temp0,
        MemOperand(dst, 2 * dst_direction * kSystemPointerSize, PostIndex));
  } else {
    Stp(temp0, temp1,
        MemOperand(dst, 2 * dst_direction * kSystemPointerSize, PostIndex));
  }
  B(&loop);

  // TODO(all): large copies may benefit from using temporary Q registers
  // to copy four double words per iteration.

  Bind(&done);
}

void MacroAssembler::SlotAddress(Register dst, int slot_offset) {
  Add(dst, sp, slot_offset << kSystemPointerSizeLog2);
}

void MacroAssembler::SlotAddress(Register dst, Register slot_offset) {
  Add(dst, sp, Operand(slot_offset, LSL, kSystemPointerSizeLog2));
}

void MacroAssembler::CanonicalizeNaN(const VRegister& dst,
                                     const VRegister& src) {
  AssertFPCRState();

  // Subtracting 0.0 preserves all inputs except for signalling NaNs, which
  // become quiet NaNs. We use fsub rather than fadd because fsub preserves -0.0
  // inputs: -0.0 + 0.0 = 0.0, but -0.0 - 0.0 = -0.0.
  Fsub(dst, src, fp_zero);
}

void MacroAssembler::LoadTaggedRoot(Register destination, RootIndex index) {
  ASM_CODE_COMMENT(this);
  if (CanBeImmediate(index)) {
    Mov(destination,
        Immediate(ReadOnlyRootPtr(index), RelocInfo::Mode::NO_INFO));
    return;
  }
  LoadRoot(destination, index);
}

void MacroAssembler::LoadRoot(Register destination, RootIndex index) {
  ASM_CODE_COMMENT(this);
  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index) &&
      IsImmAddSub(ReadOnlyRootPtr(index))) {
    DecompressTagged(destination, ReadOnlyRootPtr(index));
    return;
  }
  // Many roots have addresses that are too large to fit into addition immediate
  // operands. Evidence suggests that the extra instruction for decompression
  // costs us more than the load.
  Ldr(destination,
      MemOperand(kRootRegister, RootRegisterOffsetForRootIndex(index)));
}

void MacroAssembler::PushRoot(RootIndex index) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register tmp = temps.AcquireX();
  LoadRoot(tmp, index);
  Push(tmp);
}

void MacroAssembler::Move(Register dst, Tagged<Smi> src) { Mov(dst, src); }
void MacroAssembler::Move(Register dst, MemOperand src) { Ldr(dst, src); }
void MacroAssembler::Move(Register dst, Register src) {
  if (dst == src) return;
  Mov(dst, src);
}

void MacroAssembler::MovePair(Register dst0, Register src0, Register dst1,
                              Register src1) {
  DCHECK_NE(dst0, dst1);
  if (dst0 != src1) {
    Mov(dst0, src0);
    Mov(dst1, src1);
  } else if (dst1 != src0) {
    // Swap the order of the moves to resolve the overlap.
    Mov(dst1, src1);
    Mov(dst0, src0);
  } else {
    // Worse case scenario, this is a swap.
    Swap(dst0, src0);
  }
}

void MacroAssembler::Swap(Register lhs, Register rhs) {
  DCHECK(lhs.IsSameSizeAndType(rhs));
  DCHECK_NE(lhs, rhs);
  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  Mov(temp, rhs);
  Mov(rhs, lhs);
  Mov(lhs, temp);
}

void MacroAssembler::Swap(VRegister lhs, VRegister rhs) {
  DCHECK(lhs.IsSameSizeAndType(rhs));
  DCHECK_NE(lhs, rhs);
  UseScratchRegisterScope temps(this);
  VRegister temp = VRegister::no_reg();
  if (lhs.IsS()) {
    temp = temps.AcquireS();
  } else if (lhs.IsD()) {
    temp = temps.AcquireD();
  } else {
    DCHECK(lhs.IsQ());
    temp = temps.AcquireQ();
  }
  Mov(temp, rhs);
  Mov(rhs, lhs);
  Mov(lhs, temp);
}

void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments) {
  ASM_CODE_COMMENT(this);
  // All arguments must be on the stack before this function is called.
  // x0 holds the return value after the call.

  // Check that the number of arguments matches what the function expects.
  // If f->nargs is -1, the function can accept a variable number of arguments.
  CHECK(f->nargs < 0 || f->nargs == num_arguments);

  // Place the necessary arguments.
  Mov(x0, num_arguments);
  Mov(x1, ExternalReference::Create(f));

  bool switch_to_central = options().is_wasm;
  CallBuiltin(Builtins::RuntimeCEntry(f->result_size, switch_to_central));
}

void MacroAssembler::JumpToExternalReference(const ExternalReference& builtin,
                                             bool builtin_exit_frame) {
  ASM_CODE_COMMENT(this);
  Mov(x1, builtin);
  TailCallBuiltin(Builtins::CEntry(1, ArgvMode::kStack, builtin_exit_frame));
}

void MacroAssembler::TailCallRuntime(Runtime::FunctionId fid) {
  ASM_CODE_COMMENT(this);
  const Runtime::Function* function = Runtime::FunctionForId(fid);
  DCHECK_EQ(1, function->result_size);
  if (function->nargs >= 0) {
    // TODO(1236192): Most runtime routines don't need the number of
    // arguments passed in because it is constant. At some point we
    // should remove this need and make the runtime routine entry code
    // smarter.
    Mov(x0, function->nargs);
  }
  JumpToExternalReference(ExternalReference::Create(fid));
}

int MacroAssembler::ActivationFrameAlignment() {
#if V8_HOST_ARCH_ARM64
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one ARM
  // platform for another ARM platform with a different alignment.
  return base::OS::ActivationFrameAlignment();
#else   // V8_HOST_ARCH_ARM64
  // If we are using the simulator then we should always align to the expected
  // alignment. As the simulator is used to generate snapshots we do not know
  // if the target platform will need alignment, so this is controlled from a
  // flag.
  return v8_flags.sim_stack_alignment;
#endif  // V8_HOST_ARCH_ARM64
}

int MacroAssembler::CallCFunction(ExternalReference function,
                                  int num_of_reg_args,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  return CallCFunction(function, num_of_reg_args, 0, set_isolate_data_slots,
                       return_location);
}

int MacroAssembler::CallCFunction(ExternalReference function,
                                  int num_of_reg_args, int num_of_double_args,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  // Note: The "CallCFunction" code comment will be generated by the other
  // CallCFunction method called below.
  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  Mov(temp, function);
  return CallCFunction(temp, num_of_reg_args, num_of_double_args,
                       set_isolate_data_slots, return_location);
}

int MacroAssembler::CallCFunction(Register function, int num_of_reg_args,
                                  int num_of_double_args,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  ASM_CODE_COMMENT(this);
  DCHECK_LE(num_of_reg_args + num_of_double_args, kMaxCParameters);
  DCHECK(has_frame());

  Label get_pc;
  UseScratchRegisterScope temps(this);
  // We're doing a C call, which means non-parameter caller-saved registers
  // (x8-x17) will be clobbered and so are available to use as scratches.
  // In the worst-case scenario, we'll need 2 scratch registers. We pick 3
  // registers minus the `function` register, in case `function` aliases with
  // any of the registers.
  temps.Include(CPURegList(64, {x8, x9, x10, function}));
  temps.Exclude(function);

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // Save the frame pointer and PC so that the stack layout remains iterable,
    // even without an ExitFrame which normally exists between JS and C frames.
    UseScratchRegisterScope temps(this);
    Register pc_scratch = temps.AcquireX();

    Adr(pc_scratch, &get_pc);

    CHECK(root_array_available());
    static_assert(IsolateData::GetOffset(IsolateFieldId::kFastCCallCallerPC) ==
                  IsolateData::GetOffset(IsolateFieldId::kFastCCallCallerFP) +
                      8);
    Stp(fp, pc_scratch,
        ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
  }

  // Call directly. The function called cannot cause a GC, or allow preemption,
  // so the return address in the link register stays correct.
  Call(function);
  int call_pc_offset = pc_offset();
  bind(&get_pc);
  if (return_location) bind(return_location);

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // We don't unset the PC; the FP is the source of truth.
    Str(xzr, ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
  }

  if (num_of_reg_args > kRegisterPassedArguments) {
    // Drop the register passed arguments.
    int claim_slots = RoundUp(num_of_reg_args - kRegisterPassedArguments, 2);
    Drop(claim_slots);
  }

  if (num_of_double_args > kFPRegisterPassedArguments) {
    // Drop the register passed arguments.
    int claim_slots =
        RoundUp(num_of_double_args - kFPRegisterPassedArguments, 2);
    Drop(claim_slots);
  }

  return call_pc_offset;
}

void MacroAssembler::LoadFromConstantsTable(Register destination,
                                            int constant_index) {
  ASM_CODE_COMMENT(this);
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kBuiltinsConstantsTable));
  LoadRoot(destination, RootIndex::kBuiltinsConstantsTable);
  LoadTaggedField(destination,
                  FieldMemOperand(destination, FixedArray::OffsetOfElementAt(
                                                   constant_index)));
}

void MacroAssembler::LoadRootRelative(Register destination, int32_t offset) {
  Ldr(destination, MemOperand(kRootRegister, offset));
}

void MacroAssembler::StoreRootRelative(int32_t offset, Register value) {
  Str(value, MemOperand(kRootRegister, offset));
}

void MacroAssembler::LoadRootRegisterOffset(Register destination,
                                            intptr_t offset) {
  if (offset == 0) {
    Mov(destination, kRootRegister);
  } else {
    Add(destination, kRootRegister, offset);
  }
}

MemOperand MacroAssembler::ExternalReferenceAsOperand(
    ExternalReference reference, Register scratch) {
  if (root_array_available()) {
    if (reference.IsIsolateFieldId()) {
      return MemOperand(kRootRegister, reference.offset_from_root_register());
    }
    if (options().enable_root_relative_access) {
      intptr_t offset =
          RootRegisterOffsetForExternalReference(isolate(), reference);
      if (is_int32(offset)) {
        return MemOperand(kRootRegister, static_cast<int32_t>(offset));
      }
    }
    if (options().isolate_independent_code) {
      if (IsAddressableThroughRootRegister(isolate(), reference)) {
        // Some external references can be efficiently loaded as an offset from
        // kRootRegister.
        intptr_t offset =
            RootRegisterOffsetForExternalReference(isolate(), reference);
        CHECK(is_int32(offset));
        return MemOperand(kRootRegister, static_cast<int32_t>(offset));
      } else {
        // Otherwise, do a memory load from the external reference table.
        Ldr(scratch,
            MemOperand(kRootRegister,
                       RootRegisterOffsetForExternalReferenceTableEntry(
                           isolate(), reference)));
        return MemOperand(scratch, 0);
      }
    }
  }
  Mov(scratch, reference);
  return MemOperand(scratch, 0);
}

void MacroAssembler::Jump(Register target, Condition cond) {
  if (cond == nv) return;
  Label done;
  if (cond != al) B(NegateCondition(cond), &done);
  Br(target);
  Bind(&done);
}

void MacroAssembler::JumpHelper(int64_t offset, RelocInfo::Mode rmode,
                                Condition cond) {
  if (cond == nv) return;
  Label done;
  if (cond != al) B(NegateCondition(cond), &done);
  if (CanUseNearCallOrJump(rmode)) {
    DCHECK(IsNearCallOffset(offset));
    near_jump(static_cast<int>(offset), rmode);
  } else {
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireX();
    uint64_t imm = reinterpret_cast<uint64_t>(pc_) + offset * kInstrSize;
    Mov(temp, Immediate(imm, rmode));
    Br(temp);
  }
  Bind(&done);
}

// The calculated offset is either:
// * the 'target' input unmodified if this is a Wasm call, or
// * the offset of the target from the current PC, in instructions, for any
//   other type of call.
int64_t MacroAssembler::CalculateTargetOffset(Address target,
                                              RelocInfo::Mode rmode,
                                              uint8_t* pc) {
  int64_t offset = static_cast<int64_t>(target);
  if (rmode == RelocInfo::WASM_CALL || rmode == RelocInfo::WASM_STUB_CALL) {
    // The target of WebAssembly calls is still an index instead of an actual
    // address at this point, and needs to be encoded as-is.
    return offset;
  }
  offset -= reinterpret_cast<int64_t>(pc);
  DCHECK_EQ(offset % kInstrSize, 0);
  offset = offset / static_cast<int>(kInstrSize);
  return offset;
}

void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode,
                          Condition cond) {
  int64_t offset = CalculateTargetOffset(target, rmode, pc_);
  JumpHelper(offset, rmode, cond);
}

void MacroAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code));

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    TailCallBuiltin(builtin, cond);
    return;
  }
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  if (CanUseNearCallOrJump(rmode)) {
    EmbeddedObjectIndex index = AddEmbeddedObject(code);
    DCHECK(is_int32(index));
    JumpHelper(static_cast<int64_t>(index), rmode, cond);
  } else {
    Jump(code.address(), rmode, cond);
  }
}

void MacroAssembler::Jump(const ExternalReference& reference) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Mov(scratch, reference);
  Jump(scratch);
}

void MacroAssembler::Call(Register target) {
  BlockPoolsScope scope(this);
  Blr(target);
}

void MacroAssembler::Call(Address target, RelocInfo::Mode rmode) {
  BlockPoolsScope scope(this);
  if (CanUseNearCallOrJump(rmode)) {
    int64_t offset = CalculateTargetOffset(target, rmode, pc_);
    DCHECK(IsNearCallOffset(offset));
    near_call(static_cast<int>(offset), rmode);
  } else {
    IndirectCall(target, rmode);
  }
}

void MacroAssembler::Call(Handle<Code> code, RelocInfo::Mode rmode) {
  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code));
  BlockPoolsScope scope(this);

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    CallBuiltin(builtin);
    return;
  }

  DCHECK(RelocInfo::IsCodeTarget(rmode));

  if (CanUseNearCallOrJump(rmode)) {
    EmbeddedObjectIndex index = AddEmbeddedObject(code);
    DCHECK(is_int32(index));
    near_call(static_cast<int32_t>(index), rmode);
  } else {
    IndirectCall(code.address(), rmode);
  }
}

void MacroAssembler::Call(ExternalReference target) {
  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  Mov(temp, target);
  Call(temp);
}

void MacroAssembler::LoadEntryFromBuiltinIndex(Register builtin_index,
                                               Register target) {
  ASM_CODE_COMMENT(this);
  // The builtin_index register contains the builtin index as a Smi.
  if (SmiValuesAre32Bits()) {
    Asr(target, builtin_index, kSmiShift - kSystemPointerSizeLog2);
    Add(target, target, IsolateData::builtin_entry_table_offset());
    Ldr(target, MemOperand(kRootRegister, target));
  } else {
    DCHECK(SmiValuesAre31Bits());
    if (COMPRESS_POINTERS_BOOL) {
      Add(target, kRootRegister,
          Operand(builtin_index.W(), SXTW, kSystemPointerSizeLog2 - kSmiShift));
    } else {
      Add(target, kRootRegister,
          Operand(builtin_index, LSL, kSystemPointerSizeLog2 - kSmiShift));
    }
    Ldr(target, MemOperand(target, IsolateData::builtin_entry_table_offset()));
  }
}

void MacroAssembler::LoadEntryFromBuiltin(Builtin builtin,
                                          Register destination) {
  Ldr(destination, EntryFromBuiltinAsOperand(builtin));
}

MemOperand MacroAssembler::EntryFromBuiltinAsOperand(Builtin builtin) {
  ASM_CODE_COMMENT(this);
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
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.AcquireX();
      Ldr(scratch, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Call(scratch);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      near_call(static_cast<int>(builtin), RelocInfo::NEAR_BUILTIN_ENTRY);
      break;
    case BuiltinCallJumpMode::kIndirect: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.AcquireX();
      LoadEntryFromBuiltin(builtin, scratch);
      Call(scratch);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        EmbeddedObjectIndex index = AddEmbeddedObject(code);
        DCHECK(is_int32(index));
        near_call(static_cast<int32_t>(index), RelocInfo::CODE_TARGET);
      } else {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.AcquireX();
        LoadEntryFromBuiltin(builtin, scratch);
        Call(scratch);
      }
      break;
    }
  }
}

// TODO(ishell): remove cond parameter from here to simplify things.
void MacroAssembler::TailCallBuiltin(Builtin builtin, Condition cond) {
  ASM_CODE_COMMENT_STRING(this,
                          CommentForOffHeapTrampoline("tail call", builtin));

  // The control flow integrity (CFI) feature allows us to "sign" code entry
  // points as a target for calls, jumps or both. Arm64 has special
  // instructions for this purpose, so-called "landing pads" (see
  // MacroAssembler::CallTarget(), MacroAssembler::JumpTarget() and
  // MacroAssembler::JumpOrCallTarget()). Currently, we generate "Call"
  // landing pads for CPP builtins. In order to allow tail calling to those
  // builtins we have to use a workaround.
  // x17 is used to allow using "Call" (i.e. `bti c`) rather than "Jump"
  // (i.e. `bti j`) landing pads for the tail-called code.
  Register temp = x17;

  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      Ldr(temp, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Jump(temp, cond);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative: {
      if (cond != nv) {
        Label done;
        if (cond != al) B(NegateCondition(cond), &done);
        near_jump(static_cast<int>(builtin), RelocInfo::NEAR_BUILTIN_ENTRY);
        Bind(&done);
      }
      break;
    }
    case BuiltinCallJumpMode::kIndirect: {
      LoadEntryFromBuiltin(builtin, temp);
      Jump(temp, cond);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        EmbeddedObjectIndex index = AddEmbeddedObject(code);
        DCHECK(is_int32(index));
        JumpHelper(static_cast<int64_t>(index), RelocInfo::CODE_TARGET, cond);
      } else {
        LoadEntryFromBuiltin(builtin, temp);
        Jump(temp, cond);
      }
      break;
    }
  }
}

void MacroAssembler::LoadCodeInstructionStart(Register destination,
                                              Register code_object,
                                              CodeEntrypointTag tag) {
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  LoadCodeEntrypointViaCodePointer(
      destination,
      FieldMemOperand(code_object, Code::kSelfIndirectPointerOffset), tag);
#else
  Ldr(destination, FieldMemOperand(code_object, Code::kInstructionStartOffset));
#endif
}

void MacroAssembler::CallCodeObject(Register code_object,
                                    CodeEntrypointTag tag) {
  ASM_CODE_COMMENT(this);
  LoadCodeInstructionStart(code_object, code_object, tag);
  Call(code_object);
}

void MacroAssembler::JumpCodeObject(Register code_object, CodeEntrypointTag tag,
                                    JumpMode jump_mode) {
  ASM_CODE_COMMENT(this);
  DCHECK_EQ(JumpMode::kJump, jump_mode);
  LoadCodeInstructionStart(code_object, code_object, tag);
  // We jump through x17 here because for Branch Identification (BTI) we use
  // "Call" (`bti c`) rather than "Jump" (`bti j`) landing pads for tail-called
  // code. See TailCallBuiltin for more information.
  if (code_object != x17) {
    Mov(x17, code_object);
  }
  Jump(x17);
}

void MacroAssembler::CallJSFunction(Register function_object) {
  Register code = kJavaScriptCallCodeStartRegister;
#ifdef V8_ENABLE_SANDBOX
  // When the sandbox is enabled, we can directly fetch the entrypoint pointer
  // from the code pointer table instead of going through the Code object. In
  // this way, we avoid one memory load on this code path.
  LoadCodeEntrypointViaCodePointer(
      code, FieldMemOperand(function_object, JSFunction::kCodeOffset),
      kJSEntrypointTag);
  Call(code);
#else
  LoadTaggedField(code,
                  FieldMemOperand(function_object, JSFunction::kCodeOffset));
  CallCodeObject(code, kJSEntrypointTag);
#endif
}

void MacroAssembler::JumpJSFunction(Register function_object,
                                    JumpMode jump_mode) {
  Register code = kJavaScriptCallCodeStartRegister;
#ifdef V8_ENABLE_SANDBOX
  // When the sandbox is enabled, we can directly fetch the entrypoint pointer
  // from the code pointer table instead of going through the Code object. In
  // this way, we avoid one memory load on this code path.
  LoadCodeEntrypointViaCodePointer(
      code, FieldMemOperand(function_object, JSFunction::kCodeOffset),
      kJSEntrypointTag);
  DCHECK_EQ(jump_mode, JumpMode::kJump);
  // We jump through x17 here because for Branch Identification (BTI) we use
  // "Call" (`bti c`) rather than "Jump" (`bti j`) landing pads for tail-called
  // code. See TailCallBuiltin for more information.
  DCHECK_NE(code, x17);
  Mov(x17, code);
  Jump(x17);
#else
  LoadTaggedField(code,
                  FieldMemOperand(function_object, JSFunction::kCodeOffset));
  JumpCodeObject(code, kJSEntrypointTag, jump_mode);
#endif
}

void MacroAssembler::StoreReturnAddressAndCall(Register target) {
  ASM_CODE_COMMENT(this);
  // This generates the final instruction sequence for calls to C functions
  // once an exit frame has been constructed.
  //
  // Note that this assumes the caller code (i.e. the InstructionStream object
  // currently being generated) is immovable or that the callee function cannot
  // trigger GC, since the callee function will return to it.

  UseScratchRegisterScope temps(this);
  temps.Exclude(x16, x17);
  DCHECK(!AreAliased(x16, x17, target));

  Label return_location;
  Adr(x17, &return_location);
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  Add(x16, sp, kSystemPointerSize);
  Pacib1716();
#endif
  Str(x17, MemOperand(sp));

  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT_STRING(this, "Verify fp[kSPOffset]-8");
    // Verify that the slot below fp[kSPOffset]-8 points to the signed return
    // location.
    Ldr(x16, MemOperand(fp, ExitFrameConstants::kSPOffset));
    Ldr(x16, MemOperand(x16, -static_cast<int64_t>(kXRegSize)));
    Cmp(x16, x17);
    Check(eq, AbortReason::kReturnAddressNotFoundInFrame);
  }

  Blr(target);
  Bind(&return_location);
}

void MacroAssembler::IndirectCall(Address target, RelocInfo::Mode rmode) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register temp = temps.AcquireX();
  Mov(temp, Immediate(target, rmode));
  Blr(temp);
}

bool MacroAssembler::IsNearCallOffset(int64_t offset) {
  return is_int26(offset);
}

// Check if the code object is marked for deoptimization. If it is, then it
// jumps to the CompileLazyDeoptimizedCode builtin. In order to do this we need
// to:
//    1. read from memory the word that contains that bit, which can be found in
//       the flags in the referenced {Code} object;
//    2. test kMarkedForDeoptimizationBit in those flags; and
//    3. if it is not zero then it jumps to the builtin.
void MacroAssembler::BailoutIfDeoptimized() {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  int offset = InstructionStream::kCodeOffset - InstructionStream::kHeaderSize;
  LoadProtectedPointerField(
      scratch, MemOperand(kJavaScriptCallCodeStartRegister, offset));
  Ldr(scratch.W(), FieldMemOperand(scratch, Code::kFlagsOffset));
  Label not_deoptimized;
  Tbz(scratch.W(), Code::kMarkedForDeoptimizationBit, &not_deoptimized);
  TailCallBuiltin(Builtin::kCompileLazyDeoptimizedCode);
  Bind(&not_deoptimized);
}

void MacroAssembler::CallForDeoptimization(
    Builtin target, int deopt_id, Label* exit, DeoptimizeKind kind, Label* ret,
    Label* jump_deoptimization_entry_label) {
  ASM_CODE_COMMENT(this);
  BlockPoolsScope scope(this);
  bl(jump_deoptimization_entry_label);
  DCHECK_EQ(SizeOfCodeGeneratedSince(exit),
            (kind == DeoptimizeKind::kLazy) ? Deoptimizer::kLazyDeoptExitSize
                                            : Deoptimizer::kEagerDeoptExitSize);
}

void MacroAssembler::LoadStackLimit(Register destination, StackLimitKind kind) {
  ASM_CODE_COMMENT(this);
  DCHECK(root_array_available());
  intptr_t offset = kind == StackLimitKind::kRealStackLimit
                        ? IsolateData::real_jslimit_offset()
                        : IsolateData::jslimit_offset();

  Ldr(destination, MemOperand(kRootRegister, offset));
}

void MacroAssembler::StackOverflowCheck(Register num_args,
                                        Label* stack_overflow) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();

  // Check the stack for overflow.
  // We are not trying to catch interruptions (e.g. debug break and
  // preemption) here, so the "real stack limit" is checked.

  LoadStackLimit(scratch, StackLimitKind::kRealStackLimit);
  // Make scratch the space we have left. The stack might already be overflowed
  // here which will cause scratch to become negative.
  Sub(scratch, sp, scratch);
  // Check if the arguments will overflow the stack.
  Cmp(scratch, Operand(num_args, LSL, kSystemPointerSizeLog2));
  B(le, stack_overflow);
}

void MacroAssembler::InvokePrologue(Register formal_parameter_count,
                                    Register actual_argument_count, Label* done,
                                    InvokeType type) {
  ASM_CODE_COMMENT(this);
  //  x0: actual arguments count.
  //  x1: function (passed through to callee).
  //  x2: expected arguments count.
  //  x3: new target
  Label regular_invoke;
  DCHECK_EQ(actual_argument_count, x0);
  DCHECK_EQ(formal_parameter_count, x2);

  // If overapplication or if the actual argument count is equal to the
  // formal parameter count, no need to push extra undefined values.
  Register extra_argument_count = x2;
  Subs(extra_argument_count, formal_parameter_count, actual_argument_count);
  B(le, &regular_invoke);

  // The stack pointer in arm64 needs to be 16-byte aligned. We might need to
  // (1) add an extra padding or (2) remove (re-use) the extra padding already
  // in the stack. Let {slots_to_copy} be the number of slots (arguments) to
  // move up in the stack and let {slots_to_claim} be the number of extra stack
  // slots to claim.
  Label even_extra_count, skip_move;
  Register slots_to_copy = x4;
  Register slots_to_claim = x5;

  Mov(slots_to_copy, actual_argument_count);
  Mov(slots_to_claim, extra_argument_count);
  Tbz(extra_argument_count, 0, &even_extra_count);

  // Calculate {slots_to_claim} when {extra_argument_count} is odd.
  // If {actual_argument_count} is even, we need one extra padding slot
  // {slots_to_claim = extra_argument_count + 1}.
  // If {actual_argument_count} is odd, we know that the
  // original arguments will have a padding slot that we can reuse
  // {slots_to_claim = extra_argument_count - 1}.
  {
    Register scratch = x11;
    Add(slots_to_claim, extra_argument_count, 1);
    And(scratch, actual_argument_count, 1);
    Sub(slots_to_claim, slots_to_claim, Operand(scratch, LSL, 1));
  }

  Bind(&even_extra_count);
  Cbz(slots_to_claim, &skip_move);

  Label stack_overflow;
  StackOverflowCheck(slots_to_claim, &stack_overflow);
  Claim(slots_to_claim);

  // Move the arguments already in the stack including the receiver.
  {
    Register src = x6;
    Register dst = x7;
    SlotAddress(src, slots_to_claim);
    SlotAddress(dst, 0);
    CopyDoubleWords(dst, src, slots_to_copy);
  }

  Bind(&skip_move);
  Register pointer_next_value = x5;

  // Copy extra arguments as undefined values.
  {
    Label loop;
    Register undefined_value = x6;
    Register count = x7;
    LoadRoot(undefined_value, RootIndex::kUndefinedValue);
    SlotAddress(pointer_next_value, actual_argument_count);
    Mov(count, extra_argument_count);
    Bind(&loop);
    Str(undefined_value,
        MemOperand(pointer_next_value, kSystemPointerSize, PostIndex));
    Subs(count, count, 1);
    Cbnz(count, &loop);
  }

  // Set padding if needed.
  {
    Label skip;
    Register total_args_slots = x4;
    Add(total_args_slots, actual_argument_count, extra_argument_count);
    Tbz(total_args_slots, 0, &skip);
    Str(padreg, MemOperand(pointer_next_value));
    Bind(&skip);
  }
  B(&regular_invoke);

  bind(&stack_overflow);
  {
    FrameScope frame(
        this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);
    CallRuntime(Runtime::kThrowStackOverflow);
    Unreachable();
  }

  Bind(&regular_invoke);
}

void MacroAssembler::CallDebugOnFunctionCall(Register fun, Register new_target,
                                             Register expected_parameter_count,
                                             Register actual_parameter_count) {
  ASM_CODE_COMMENT(this);
  // Load receiver to pass it later to DebugOnFunctionCall hook.
  Peek(x4, ReceiverOperand());
  FrameScope frame(
      this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);

  if (!new_target.is_valid()) new_target = padreg;

  // Save values on stack.
  SmiTag(expected_parameter_count);
  SmiTag(actual_parameter_count);
  Push(expected_parameter_count, actual_parameter_count, new_target, fun);
  Push(fun, x4);
  CallRuntime(Runtime::kDebugOnFunctionCall);

  // Restore values from stack.
  Pop(fun, new_target, actual_parameter_count, expected_parameter_count);
  SmiUntag(actual_parameter_count);
  SmiUntag(expected_parameter_count);
}

void MacroAssembler::InvokeFunctionCode(Register function, Register new_target,
                                        Register expected_parameter_count,
                                        Register actual_parameter_count,
                                        InvokeType type) {
  ASM_CODE_COMMENT(this);
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());
  DCHECK_EQ(function, x1);
  DCHECK_IMPLIES(new_target.is_valid(), new_target == x3);

  // On function call, call into the debugger if necessary.
  Label debug_hook, continue_after_hook;
  {
    Mov(x4, ExternalReference::debug_hook_on_function_call_address(isolate()));
    Ldrsb(x4, MemOperand(x4));
    Cbnz(x4, &debug_hook);
  }
  bind(&continue_after_hook);

  // Clear the new.target register if not given.
  if (!new_target.is_valid()) {
    LoadRoot(x3, RootIndex::kUndefinedValue);
  }

  Label done;
  InvokePrologue(expected_parameter_count, actual_parameter_count, &done, type);

  // If actual != expected, InvokePrologue will have handled the call through
  // the argument adaptor mechanism.
  // The called function expects the call kind in x5.
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
  B(&done);

  // Deferred debug hook.
  bind(&debug_hook);
  CallDebugOnFunctionCall(function, new_target, expected_parameter_count,
                          actual_parameter_count);
  B(&continue_after_hook);

  // Continue here if InvokePrologue does handle the invocation due to
  // mismatched parameter counts.
  Bind(&done);
}

void MacroAssembler::JumpIfCodeIsMarkedForDeoptimization(
    Register code, Register scratch, Label* if_marked_for_deoptimization) {
  Ldr(scratch.W(), FieldMemOperand(code, Code::kFlagsOffset));
  Tbnz(scratch.W(), Code::kMarkedForDeoptimizationBit,
       if_marked_for_deoptimization);
}

void MacroAssembler::JumpIfCodeIsTurbofanned(Register code, Register scratch,
                                             Label* if_turbofanned) {
  Ldr(scratch.W(), FieldMemOperand(code, Code::kFlagsOffset));
  Tbnz(scratch.W(), Code::kIsTurbofannedBit, if_turbofanned);
}

Operand MacroAssembler::ClearedValue() const {
  return Operand(static_cast<int32_t>(i::ClearedValue(isolate()).ptr()));
}

Operand MacroAssembler::ReceiverOperand() { return Operand(0); }

void MacroAssembler::InvokeFunctionWithNewTarget(
    Register function, Register new_target, Register actual_parameter_count,
    InvokeType type) {
  ASM_CODE_COMMENT(this);
  // You can't call a function without a valid frame.
  DCHECK(type == InvokeType::kJump || has_frame());

  // Contract with called JS functions requires that function is passed in x1.
  // (See FullCodeGenerator::Generate().)
  DCHECK_EQ(function, x1);

  Register expected_parameter_count = x2;

  LoadTaggedField(cp, FieldMemOperand(function, JSFunction::kContextOffset));
  // The number of arguments is stored as an int32_t, and -1 is a marker
  // (kDontAdaptArgumentsSentinel), so we need sign
  // extension to correctly handle it.
  LoadTaggedField(
      expected_parameter_count,
      FieldMemOperand(function, JSFunction::kSharedFunctionInfoOffset));
  Ldrh(expected_parameter_count,
       FieldMemOperand(expected_parameter_count,
                       SharedFunctionInfo::kFormalParameterCountOffset));

  InvokeFunctionCode(function, new_target, expected_parameter_count,
                     actual_parameter_count, type);
}

void MacroAssembler::InvokeFunction(Register function,
                                    Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    InvokeType type) {
  ASM_CODE_COMMENT(this);
  // You can't call a function without a valid frame.
  DCHECK(type == InvokeType::kJump || has_frame());

  // Contract with called JS functions requires that function is passed in x1.
  // (See FullCodeGenerator::Generate().)
  DCHECK_EQ(function, x1);

  // Set up the context.
  LoadTaggedField(cp, FieldMemOperand(function, JSFunction::kContextOffset));

  InvokeFunctionCode(function, no_reg, expected_parameter_count,
                     actual_parameter_count, type);
}

void MacroAssembler::TryConvertDoubleToInt64(Register result,
                                             DoubleRegister double_input,
                                             Label* done) {
  ASM_CODE_COMMENT(this);
  // Try to convert with an FPU convert instruction. It's trivial to compute
  // the modulo operation on an integer register so we convert to a 64-bit
  // integer.
  //
  // Fcvtzs will saturate to INT64_MIN (0x800...00) or INT64_MAX (0x7FF...FF)
  // when the double is out of range. NaNs and infinities will be converted to 0
  // (as ECMA-262 requires).
  Fcvtzs(result.X(), double_input);

  // The values INT64_MIN (0x800...00) or INT64_MAX (0x7FF...FF) are not
  // representable using a double, so if the result is one of those then we know
  // that saturation occurred, and we need to manually handle the conversion.
  //
  // It is easy to detect INT64_MIN and INT64_MAX because adding or subtracting
  // 1 will cause signed overflow.
  Cmp(result.X(), 1);
  Ccmp(result.X(), -1, VFlag, vc);

  B(vc, done);
}

void MacroAssembler::TruncateDoubleToI(Isolate* isolate, Zone* zone,
                                       Register result,
                                       DoubleRegister double_input,
                                       StubCallMode stub_mode,
                                       LinkRegisterStatus lr_status) {
  ASM_CODE_COMMENT(this);
  if (CpuFeatures::IsSupported(JSCVT)) {
    Fjcvtzs(result.W(), double_input);
    return;
  }

  Label done;

  // Try to convert the double to an int64. If successful, the bottom 32 bits
  // contain our truncated int32 result.
  TryConvertDoubleToInt64(result, double_input, &done);

  // If we fell through then inline version didn't succeed - call stub instead.
  if (lr_status == kLRHasNotBeenSaved) {
    Push<MacroAssembler::kSignLR>(lr, double_input);
  } else {
    Push<MacroAssembler::kDontStoreLR>(xzr, double_input);
  }

  // DoubleToI preserves any registers it needs to clobber.
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
  Ldr(result, MemOperand(sp, 0));

  DCHECK_EQ(xzr.SizeInBytes(), double_input.SizeInBytes());

  if (lr_status == kLRHasNotBeenSaved) {
    // Pop into xzr here to drop the double input on the stack:
    Pop<MacroAssembler::kAuthLR>(xzr, lr);
  } else {
    Drop(2);
  }

  Bind(&done);
  // Keep our invariant that the upper 32 bits are zero.
  Uxtw(result.W(), result.W());
}

void MacroAssembler::Prologue() {
  ASM_CODE_COMMENT(this);
  Push<MacroAssembler::kSignLR>(lr, fp);
  mov(fp, sp);
  static_assert(kExtraSlotClaimedByPrologue == 1);
  Push(cp, kJSFunctionRegister, kJavaScriptCallArgCountRegister, padreg);
}

void MacroAssembler::EnterFrame(StackFrame::Type type) {
  UseScratchRegisterScope temps(this);

  if (StackFrame::IsJavaScript(type)) {
    // Just push a minimal "machine frame", saving the frame pointer and return
    // address, without any markers.
    Push<MacroAssembler::kSignLR>(lr, fp);
    Mov(fp, sp);
    // sp[1] : lr
    // sp[0] : fp
  } else {
      Register type_reg = temps.AcquireX();
      Mov(type_reg, StackFrame::TypeToMarker(type));
      Register fourth_reg = no_reg;
      if (type == StackFrame::CONSTRUCT || type == StackFrame::FAST_CONSTRUCT) {
        fourth_reg = cp;
#if V8_ENABLE_WEBASSEMBLY
      } else if (type == StackFrame::WASM ||
                 type == StackFrame::WASM_LIFTOFF_SETUP ||
                 type == StackFrame::WASM_EXIT) {
        fourth_reg = kWasmInstanceRegister;
#endif  // V8_ENABLE_WEBASSEMBLY
      } else {
        fourth_reg = padreg;
      }
      Push<MacroAssembler::kSignLR>(lr, fp, type_reg, fourth_reg);
      static constexpr int kSPToFPDelta  = 2 * kSystemPointerSize;
      Add(fp, sp, kSPToFPDelta);
      // sp[3] : lr
      // sp[2] : fp
      // sp[1] : type
      // sp[0] : cp | wasm instance | for alignment
  }
}

void MacroAssembler::LeaveFrame(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  // Drop the execution stack down to the frame pointer and restore
  // the caller frame pointer and return address.
  Mov(sp, fp);
  Pop<MacroAssembler::kAuthLR>(fp, lr);
}

void MacroAssembler::EnterExitFrame(const Register& scratch, int extra_space,
                                    StackFrame::Type frame_type) {
  ASM_CODE_COMMENT(this);
  DCHECK(frame_type == StackFrame::EXIT ||
         frame_type == StackFrame::BUILTIN_EXIT ||
         frame_type == StackFrame::API_ACCESSOR_EXIT ||
         frame_type == StackFrame::API_CALLBACK_EXIT);

  // Set up the new stack frame.
  Push<MacroAssembler::kSignLR>(lr, fp);
  Mov(fp, sp);
  Mov(scratch, StackFrame::TypeToMarker(frame_type));
  Push(scratch, xzr);
  //          fp[8]: CallerPC (lr)
  //    fp -> fp[0]: CallerFP (old fp)
  //          fp[-8]: STUB marker
  //    sp -> fp[-16]: Space reserved for SPOffset.
  static_assert((2 * kSystemPointerSize) ==
                ExitFrameConstants::kCallerSPOffset);
  static_assert((1 * kSystemPointerSize) ==
                ExitFrameConstants::kCallerPCOffset);
  static_assert((0 * kSystemPointerSize) ==
                ExitFrameConstants::kCallerFPOffset);
  static_assert((-2 * kSystemPointerSize) == ExitFrameConstants::kSPOffset);

  // Save the frame pointer and context pointer in the top frame.
  Mov(scratch,
      ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate()));
  Str(fp, MemOperand(scratch));
  Mov(scratch,
      ExternalReference::Create(IsolateAddressId::kContextAddress, isolate()));
  Str(cp, MemOperand(scratch));

  static_assert((-2 * kSystemPointerSize) ==
                ExitFrameConstants::kLastExitFrameField);

  // Round the number of space we need to claim to a multiple of two.
  int slots_to_claim = RoundUp(extra_space + 1, 2);

  // Reserve space for the return address and for user requested memory.
  // We do this before aligning to make sure that we end up correctly
  // aligned with the minimum of wasted space.
  Claim(slots_to_claim, kXRegSize);
  //         fp[8]: CallerPC (lr)
  //   fp -> fp[0]: CallerFP (old fp)
  //         fp[-8]: STUB marker
  //         fp[-16]: Space reserved for SPOffset.
  //         sp[8]: Extra space reserved for caller (if extra_space != 0).
  //   sp -> sp[0]: Space reserved for the return address.

  // ExitFrame::GetStateForFramePointer expects to find the return address at
  // the memory address immediately below the pointer stored in SPOffset.
  // It is not safe to derive much else from SPOffset, because the size of the
  // padding can vary.
  Add(scratch, sp, kXRegSize);
  Str(scratch, MemOperand(fp, ExitFrameConstants::kSPOffset));
}

// Leave the current exit frame.
void MacroAssembler::LeaveExitFrame(const Register& scratch,
                                    const Register& scratch2) {
  ASM_CODE_COMMENT(this);

  // Restore the context pointer from the top frame.
  Mov(scratch,
      ExternalReference::Create(IsolateAddressId::kContextAddress, isolate()));
  Ldr(cp, MemOperand(scratch));

  if (v8_flags.debug_code) {
    // Also emit debug code to clear the cp in the top frame.
    Mov(scratch2, Operand(Context::kInvalidContext));
    Mov(scratch, ExternalReference::Create(IsolateAddressId::kContextAddress,
                                           isolate()));
    Str(scratch2, MemOperand(scratch));
  }
  // Clear the frame pointer from the top frame.
  Mov(scratch,
      ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate()));
  Str(xzr, MemOperand(scratch));

  // Pop the exit frame.
  //         fp[8]: CallerPC (lr)
  //   fp -> fp[0]: CallerFP (old fp)
  //         fp[...]: The rest of the frame.
  Mov(sp, fp);
  Pop<MacroAssembler::kAuthLR>(fp, lr);
}

void MacroAssembler::LoadGlobalProxy(Register dst) {
  ASM_CODE_COMMENT(this);
  LoadNativeContextSlot(dst, Context::GLOBAL_PROXY_INDEX);
}

void MacroAssembler::LoadWeakValue(Register out, Register in,
                                   Label* target_if_cleared) {
  ASM_CODE_COMMENT(this);
  CompareAndBranch(in.W(), Operand(kClearedWeakHeapObjectLower32), eq,
                   target_if_cleared);

  and_(out, in, Operand(~kWeakHeapObjectMask));
}

void MacroAssembler::EmitIncrementCounter(StatsCounter* counter, int value,
                                          Register scratch1,
                                          Register scratch2) {
  ASM_CODE_COMMENT(this);
  DCHECK_NE(value, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t dummy_stats_counter_
    // field.
    Mov(scratch2, ExternalReference::Create(counter));
    Ldr(scratch1.W(), MemOperand(scratch2));
    Add(scratch1.W(), scratch1.W(), value);
    Str(scratch1.W(), MemOperand(scratch2));
  }
}

void MacroAssembler::JumpIfObjectType(Register object, Register map,
                                      Register type_reg, InstanceType type,
                                      Label* if_cond_pass, Condition cond) {
  ASM_CODE_COMMENT(this);
  CompareObjectType(object, map, type_reg, type);
  B(cond, if_cond_pass);
}

void MacroAssembler::JumpIfJSAnyIsNotPrimitive(Register heap_object,
                                               Register scratch, Label* target,
                                               Label::Distance distance,
                                               Condition cc) {
  CHECK(cc == Condition::kUnsignedLessThan ||
        cc == Condition::kUnsignedGreaterThanEqual);
  if (V8_STATIC_ROOTS_BOOL) {
#ifdef DEBUG
    Label ok;
    LoadMap(scratch, heap_object);
    CompareInstanceTypeRange(scratch, scratch, FIRST_JS_RECEIVER_TYPE,
                             LAST_JS_RECEIVER_TYPE);
    B(Condition::kUnsignedLessThanEqual, &ok);
    LoadMap(scratch, heap_object);
    CompareInstanceTypeRange(scratch, scratch, FIRST_PRIMITIVE_HEAP_OBJECT_TYPE,
                             LAST_PRIMITIVE_HEAP_OBJECT_TYPE);
    B(Condition::kUnsignedLessThanEqual, &ok);
    Abort(AbortReason::kInvalidReceiver);
    bind(&ok);
#endif  // DEBUG

    // All primitive object's maps are allocated at the start of the read only
    // heap. Thus JS_RECEIVER's must have maps with larger (compressed)
    // addresses.
    LoadCompressedMap(scratch, heap_object);
    CmpTagged(scratch, Immediate(InstanceTypeChecker::kNonJsReceiverMapLimit));
  } else {
    static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
    CompareObjectType(heap_object, scratch, scratch, FIRST_JS_RECEIVER_TYPE);
  }
  B(cc, target);
}

#if V8_STATIC_ROOTS_BOOL
void MacroAssembler::CompareInstanceTypeWithUniqueCompressedMap(
    Register map, Register scratch, InstanceType type) {
  std::optional<RootIndex> expected =
      InstanceTypeChecker::UniqueMapOfInstanceType(type);
  CHECK(expected);
  Tagged_t expected_ptr = ReadOnlyRootPtr(*expected);
  DCHECK_NE(map, scratch);
  UseScratchRegisterScope temps(this);
  CHECK(IsImmAddSub(expected_ptr) || scratch != Register::no_reg() ||
        temps.CanAcquire());
  if (!IsImmAddSub(expected_ptr)) {
    if (scratch == Register::no_reg()) {
      scratch = temps.AcquireX();
      DCHECK_NE(map, scratch);
    }
    Operand imm_operand =
        MoveImmediateForShiftedOp(scratch, expected_ptr, kAnyShift);
    CmpTagged(map, imm_operand);
  } else {
    CmpTagged(map, Immediate(expected_ptr));
  }
}

void MacroAssembler::IsObjectTypeFast(Register object,
                                      Register compressed_map_scratch,
                                      InstanceType type) {
  ASM_CODE_COMMENT(this);
  CHECK(InstanceTypeChecker::UniqueMapOfInstanceType(type));
  LoadCompressedMap(compressed_map_scratch, object);
  CompareInstanceTypeWithUniqueCompressedMap(compressed_map_scratch,
                                             Register::no_reg(), type);
}
#endif  // V8_STATIC_ROOTS_BOOL

// Sets equality condition flags.
void MacroAssembler::IsObjectType(Register object, Register scratch1,
                                  Register scratch2, InstanceType type) {
  ASM_CODE_COMMENT(this);

#if V8_STATIC_ROOTS_BOOL
  if (InstanceTypeChecker::UniqueMapOfInstanceType(type)) {
    LoadCompressedMap(scratch1, object);
    CompareInstanceTypeWithUniqueCompressedMap(
        scratch1, scratch1 != scratch2 ? scratch2 : Register::no_reg(), type);
    return;
  }
#endif  // V8_STATIC_ROOTS_BOOL

  CompareObjectType(object, scratch1, scratch2, type);
}

// Sets equality condition flags.
void MacroAssembler::IsObjectTypeInRange(Register heap_object, Register scratch,
                                         InstanceType lower_limit,
                                         InstanceType higher_limit) {
  DCHECK_LT(lower_limit, higher_limit);
#if V8_STATIC_ROOTS_BOOL
  if (auto range = InstanceTypeChecker::UniqueMapRangeOfInstanceTypeRange(
          lower_limit, higher_limit)) {
    LoadCompressedMap(scratch.W(), heap_object);
    CompareRange(scratch.W(), scratch.W(), range->first, range->second);
    return;
  }
#endif  // V8_STATIC_ROOTS_BOOL
  LoadMap(scratch, heap_object);
  CompareInstanceTypeRange(scratch, scratch, lower_limit, higher_limit);
}

// Sets condition flags based on comparison, and returns type in type_reg.
void MacroAssembler::CompareObjectType(Register object, Register map,
                                       Register type_reg, InstanceType type) {
  ASM_CODE_COMMENT(this);
  LoadMap(map, object);
  CompareInstanceType(map, type_reg, type);
}

void MacroAssembler::CompareRange(Register value, Register scratch,
                                  unsigned lower_limit, unsigned higher_limit) {
  ASM_CODE_COMMENT(this);
  DCHECK_LT(lower_limit, higher_limit);
  if (lower_limit != 0) {
    Sub(scratch.W(), value.W(), Operand(lower_limit));
    Cmp(scratch.W(), Operand(higher_limit - lower_limit));
  } else {
    Cmp(value.W(), Immediate(higher_limit));
  }
}

void MacroAssembler::JumpIfIsInRange(Register value, Register scratch,
                                     unsigned lower_limit,
                                     unsigned higher_limit,
                                     Label* on_in_range) {
  CompareRange(value, scratch, lower_limit, higher_limit);
  B(ls, on_in_range);
}

void MacroAssembler::LoadCompressedMap(Register dst, Register object) {
  ASM_CODE_COMMENT(this);
  Ldr(dst.W(), FieldMemOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadMap(Register dst, Register object) {
  ASM_CODE_COMMENT(this);
  LoadTaggedField(dst, FieldMemOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadFeedbackVector(Register dst, Register closure,
                                        Register scratch, Label* fbv_undef) {
  Label done;

  // Load the feedback vector from the closure.
  LoadTaggedField(dst,
                  FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  LoadTaggedField(dst, FieldMemOperand(dst, FeedbackCell::kValueOffset));

  // Check if feedback vector is valid.
  LoadTaggedField(scratch, FieldMemOperand(dst, HeapObject::kMapOffset));
  Ldrh(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  Cmp(scratch, FEEDBACK_VECTOR_TYPE);
  B(eq, &done);

  // Not valid, load undefined.
  LoadRoot(dst, RootIndex::kUndefinedValue);
  B(fbv_undef);

  Bind(&done);
}

// Sets condition flags based on comparison, and returns type in type_reg.
void MacroAssembler::CompareInstanceType(Register map, Register type_reg,
                                         InstanceType type) {
  ASM_CODE_COMMENT(this);
  Ldrh(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  Cmp(type_reg, type);
}

// Sets condition flags based on comparison, and returns type in type_reg.
void MacroAssembler::CompareInstanceTypeRange(Register map, Register type_reg,
                                              InstanceType lower_limit,
                                              InstanceType higher_limit) {
  ASM_CODE_COMMENT(this);
  DCHECK_LT(lower_limit, higher_limit);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Ldrh(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  CompareRange(type_reg, scratch, lower_limit, higher_limit);
}

void MacroAssembler::LoadElementsKindFromMap(Register result, Register map) {
  ASM_CODE_COMMENT(this);
  // Load the map's "bit field 2".
  Ldrb(result, FieldMemOperand(map, Map::kBitField2Offset));
  // Retrieve elements_kind from bit field 2.
  DecodeField<Map::Bits2::ElementsKindBits>(result);
}

void MacroAssembler::CompareTaggedRoot(const Register& obj, RootIndex index) {
  ASM_CODE_COMMENT(this);
  AssertSmiOrHeapObjectInMainCompressionCage(obj);
  UseScratchRegisterScope temps(this);
  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index)) {
    CmpTagged(obj, Immediate(ReadOnlyRootPtr(index)));
    return;
  }
  // Some smi roots contain system pointer size values like stack limits.
  DCHECK(base::IsInRange(index, RootIndex::kFirstStrongOrReadOnlyRoot,
                         RootIndex::kLastStrongOrReadOnlyRoot));
  Register temp = temps.AcquireX();
  DCHECK(!AreAliased(obj, temp));
  LoadRoot(temp, index);
  CmpTagged(obj, temp);
}

void MacroAssembler::CompareRoot(const Register& obj, RootIndex index,
                                 ComparisonMode mode) {
  ASM_CODE_COMMENT(this);
  if (mode == ComparisonMode::kFullPointer ||
      !base::IsInRange(index, RootIndex::kFirstStrongOrReadOnlyRoot,
                       RootIndex::kLastStrongOrReadOnlyRoot)) {
    // Some smi roots contain system pointer size values like stack limits.
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireX();
    DCHECK(!AreAliased(obj, temp));
    LoadRoot(temp, index);
    Cmp(obj, temp);
    return;
  }
  CompareTaggedRoot(obj, index);
}

void MacroAssembler::JumpIfRoot(const Register& obj, RootIndex index,
                                Label* if_equal) {
  CompareRoot(obj, index);
  B(eq, if_equal);
}

void MacroAssembler::JumpIfNotRoot(const Register& obj, RootIndex index,
                                   Label* if_not_equal) {
  CompareRoot(obj, index);
  B(ne, if_not_equal);
}

void MacroAssembler::JumpIfIsInRange(const Register& value,
                                     unsigned lower_limit,
                                     unsigned higher_limit,
                                     Label* on_in_range) {
  ASM_CODE_COMMENT(this);
  if (lower_limit != 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.AcquireW();
    Sub(scratch, value, Operand(lower_limit));
    CompareAndBranch(scratch, Operand(higher_limit - lower_limit), ls,
                     on_in_range);
  } else {
    CompareAndBranch(value, Operand(higher_limit - lower_limit), ls,
                     on_in_range);
  }
}

void MacroAssembler::LoadTaggedField(const Register& destination,
                                     const MemOperand& field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTagged(destination, field_operand);
  } else {
    Ldr(destination, field_operand);
  }
}

void MacroAssembler::LoadTaggedFieldWithoutDecompressing(
    const Register& destination, const MemOperand& field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    Ldr(destination.W(), field_operand);
  } else {
    Ldr(destination, field_operand);
  }
}

void MacroAssembler::LoadTaggedSignedField(const Register& destination,
                                           const MemOperand& field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTaggedSigned(destination, field_operand);
  } else {
    Ldr(destination, field_operand);
  }
}

void MacroAssembler::SmiUntagField(Register dst, const MemOperand& src) {
  SmiUntag(dst, src);
}

void MacroAssembler::StoreTwoTaggedFields(const Register& value,
                                          const MemOperand& dst_field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    Stp(value.W(), value.W(), dst_field_operand);
  } else {
    Stp(value, value, dst_field_operand);
  }
}

void MacroAssembler::StoreTaggedField(const Register& value,
                                      const MemOperand& dst_field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    Str(value.W(), dst_field_operand);
  } else {
    Str(value, dst_field_operand);
  }
}

void MacroAssembler::AtomicStoreTaggedField(const Register& value,
                                            const Register& dst_base,
                                            const Register& dst_index,
                                            const Register& temp) {
  Add(temp, dst_base, dst_index);
  if (COMPRESS_POINTERS_BOOL) {
    Stlr(value.W(), temp);
  } else {
    Stlr(value, temp);
  }
}

void MacroAssembler::DecompressTaggedSigned(const Register& destination,
                                            const MemOperand& field_operand) {
  ASM_CODE_COMMENT(this);
  Ldr(destination.W(), field_operand);
  if (v8_flags.debug_code) {
    // Corrupt the top 32 bits. Made up of 16 fixed bits and 16 pc offset bits.
    Add(destination, destination,
        ((kDebugZapValue << 16) | (pc_offset() & 0xffff)) << 32);
  }
}

void MacroAssembler::DecompressTagged(const Register& destination,
                                      const MemOperand& field_operand) {
  ASM_CODE_COMMENT(this);
  Ldr(destination.W(), field_operand);
  Add(destination, kPtrComprCageBaseRegister, destination);
}

void MacroAssembler::DecompressTagged(const Register& destination,
                                      const Register& source) {
  ASM_CODE_COMMENT(this);
  Add(destination, kPtrComprCageBaseRegister, Operand(source, UXTW));
}

void MacroAssembler::DecompressTagged(const Register& destination,
                                      Tagged_t immediate) {
  ASM_CODE_COMMENT(this);
  if (IsImmAddSub(immediate)) {
    Add(destination, kPtrComprCageBaseRegister,
        Immediate(immediate, RelocInfo::Mode::NO_INFO));
  } else {
    // Immediate is larger than 12 bit and therefore can't be encoded directly.
    // Use destination as a temporary to not acquire a scratch register.
    DCHECK_NE(destination, sp);
    Operand imm_operand =
        MoveImmediateForShiftedOp(destination, immediate, kAnyShift);
    Add(destination, kPtrComprCageBaseRegister, imm_operand);
  }
}

void MacroAssembler::DecompressProtected(const Register& destination,
                                         const MemOperand& field_operand) {
#if V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Ldr(destination.W(), field_operand);
  Ldr(scratch,
      MemOperand(kRootRegister, IsolateData::trusted_cage_base_offset()));
  Orr(destination, destination, scratch);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

void MacroAssembler::AtomicDecompressTaggedSigned(const Register& destination,
                                                  const Register& base,
                                                  const Register& index,
                                                  const Register& temp) {
  ASM_CODE_COMMENT(this);
  Add(temp, base, index);
  Ldar(destination.W(), temp);
  if (v8_flags.debug_code) {
    // Corrupt the top 32 bits. Made up of 16 fixed bits and 16 pc offset bits.
    Add(destination, destination,
        ((kDebugZapValue << 16) | (pc_offset() & 0xffff)) << 32);
  }
}

void MacroAssembler::AtomicDecompressTagged(const Register& destination,
                                            const Register& base,
                                            const Register& index,
                                            const Register& temp) {
  ASM_CODE_COMMENT(this);
  Add(temp, base, index);
  Ldar(destination.W(), temp);
  Add(destination, kPtrComprCageBaseRegister, destination);
}

void MacroAssembler::CheckPageFlag(const Register& object, int mask,
                                   Condition cc, Label* condition_met) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  And(scratch, object, ~MemoryChunk::GetAlignmentMaskForAssembler());
  Ldr(scratch, MemOperand(scratch, MemoryChunkLayout::kFlagsOffset));
  if (cc == ne) {
    TestAndBranchIfAnySet(scratch, mask, condition_met);
  } else {
    DCHECK_EQ(cc, eq);
    TestAndBranchIfAllClear(scratch, mask, condition_met);
  }
}

void MacroAssembler::JumpIfMarking(Label* is_marking,
                                   Label::Distance condition_met_distance) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Ldrb(scratch,
       MemOperand(kRootRegister, IsolateData::is_marking_flag_offset()));
  Cbnz(scratch, is_marking);
}

void MacroAssembler::JumpIfNotMarking(Label* not_marking,
                                      Label::Distance condition_met_distance) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Ldrb(scratch,
       MemOperand(kRootRegister, IsolateData::is_marking_flag_offset()));
  Cbz(scratch, not_marking);
}

void MacroAssembler::RecordWriteField(Register object, int offset,
                                      Register value,
                                      LinkRegisterStatus lr_status,
                                      SaveFPRegsMode save_fp,
                                      SmiCheck smi_check, SlotDescriptor slot) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, value));
  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis.
  Label done;

  // Skip the barrier if writing a smi.
  if (smi_check == SmiCheck::kInline) {
    JumpIfSmi(value, &done);
  }

  // Although the object register is tagged, the offset is relative to the start
  // of the object, so offset must be a multiple of kTaggedSize.
  DCHECK(IsAligned(offset, kTaggedSize));

  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT_STRING(this, "Verify slot_address");
    Label ok;
    UseScratchRegisterScope temps(this);
    Register scratch = temps.AcquireX();
    DCHECK(!AreAliased(object, value, scratch));
    Add(scratch, object, offset - kHeapObjectTag);
    Tst(scratch, kTaggedSize - 1);
    B(eq, &ok);
    Abort(AbortReason::kUnalignedCellInWriteBarrier);
    Bind(&ok);
  }

  RecordWrite(object, Operand(offset - kHeapObjectTag), value, lr_status,
              save_fp, SmiCheck::kOmit, slot);

  Bind(&done);
}

void MacroAssembler::DecodeSandboxedPointer(Register value) {
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  Add(value, kPtrComprCageBaseRegister,
      Operand(value, LSR, kSandboxedPointerShift));
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::LoadSandboxedPointerField(Register destination,
                                               MemOperand field_operand) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  Ldr(destination, field_operand);
  DecodeSandboxedPointer(destination);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::StoreSandboxedPointerField(Register value,
                                                MemOperand dst_field_operand) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Sub(scratch, value, kPtrComprCageBaseRegister);
  Mov(scratch, Operand(scratch, LSL, kSandboxedPointerShift));
  Str(scratch, dst_field_operand);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::LoadExternalPointerField(Register destination,
                                              MemOperand field_operand,
                                              ExternalPointerTag tag,
                                              Register isolate_root) {
  DCHECK(!AreAliased(destination, isolate_root));
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  DCHECK_NE(tag, kExternalPointerNullTag);
  DCHECK(!IsSharedExternalPointerType(tag));
  UseScratchRegisterScope temps(this);
  Register external_table = temps.AcquireX();
  if (isolate_root == no_reg) {
    DCHECK(root_array_available_);
    isolate_root = kRootRegister;
  }
  Ldr(external_table,
      MemOperand(isolate_root,
                 IsolateData::external_pointer_table_offset() +
                     Internals::kExternalPointerTableBasePointerOffset));
  Ldr(destination.W(), field_operand);
  Mov(destination, Operand(destination, LSR, kExternalPointerIndexShift));
  Ldr(destination, MemOperand(external_table, destination, LSL,
                              kExternalPointerTableEntrySizeLog2));
  // We need another scratch register for the 64-bit tag constant. Instead of
  // forcing the `And` to allocate a new temp register (which we may not have),
  // reuse the temp register that we used for the external pointer table base.
  Register tag_reg = external_table;
  Mov(tag_reg, Immediate(~tag));
  And(destination, destination, tag_reg);
#else
  Ldr(destination, field_operand);
#endif  // V8_ENABLE_SANDBOX
}

void MacroAssembler::LoadTrustedPointerField(Register destination,
                                             MemOperand field_operand,
                                             IndirectPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
  LoadIndirectPointerField(destination, field_operand, tag);
#else
  LoadTaggedField(destination, field_operand);
#endif
}

void MacroAssembler::StoreTrustedPointerField(Register value,
                                              MemOperand dst_field_operand) {
#ifdef V8_ENABLE_SANDBOX
  StoreIndirectPointerField(value, dst_field_operand);
#else
  StoreTaggedField(value, dst_field_operand);
#endif
}

void MacroAssembler::LoadIndirectPointerField(Register destination,
                                              MemOperand field_operand,
                                              IndirectPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);

  Register handle = temps.AcquireX();
  Ldr(handle.W(), field_operand);
  ResolveIndirectPointerHandle(destination, handle, tag);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

void MacroAssembler::StoreIndirectPointerField(Register value,
                                               MemOperand dst_field_operand) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Ldr(scratch.W(),
      FieldMemOperand(value, ExposedTrustedObject::kSelfIndirectPointerOffset));
  Str(scratch.W(), dst_field_operand);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

#ifdef V8_ENABLE_SANDBOX
void MacroAssembler::ResolveIndirectPointerHandle(Register destination,
                                                  Register handle,
                                                  IndirectPointerTag tag) {
  // The tag implies which pointer table to use.
  if (tag == kUnknownIndirectPointerTag) {
    // In this case we have to rely on the handle marking to determine which
    // pointer table to use.
    Label is_trusted_pointer_handle, done;
    constexpr int kCodePointerHandleMarkerBit = 0;
    static_assert((1 << kCodePointerHandleMarkerBit) ==
                  kCodePointerHandleMarker);
    Tbz(handle, kCodePointerHandleMarkerBit, &is_trusted_pointer_handle);
    ResolveCodePointerHandle(destination, handle);
    B(&done);
    Bind(&is_trusted_pointer_handle);
    ResolveTrustedPointerHandle(destination, handle,
                                kUnknownIndirectPointerTag);
    Bind(&done);
  } else if (tag == kCodeIndirectPointerTag) {
    ResolveCodePointerHandle(destination, handle);
  } else {
    ResolveTrustedPointerHandle(destination, handle, tag);
  }
}

void MacroAssembler::ResolveTrustedPointerHandle(Register destination,
                                                 Register handle,
                                                 IndirectPointerTag tag) {
  DCHECK_NE(tag, kCodeIndirectPointerTag);
  DCHECK(!AreAliased(handle, destination));

  Register table = destination;
  DCHECK(root_array_available_);
  Ldr(table,
      MemOperand{kRootRegister, IsolateData::trusted_pointer_table_offset()});
  Mov(handle, Operand(handle, LSR, kTrustedPointerHandleShift));
  Ldr(destination,
      MemOperand(table, handle, LSL, kTrustedPointerTableEntrySizeLog2));
  // Untag the pointer and remove the marking bit in one operation.
  Register tag_reg = handle;
  Mov(tag_reg, Immediate(~(tag | kTrustedPointerTableMarkBit)));
  And(destination, destination, tag_reg);
}

void MacroAssembler::ResolveCodePointerHandle(Register destination,
                                              Register handle) {
  DCHECK(!AreAliased(handle, destination));

  Register table = destination;
  Mov(table, ExternalReference::code_pointer_table_address());
  Mov(handle, Operand(handle, LSR, kCodePointerHandleShift));
  Add(destination, table, Operand(handle, LSL, kCodePointerTableEntrySizeLog2));
  Ldr(destination,
      MemOperand(destination,
                 Immediate(kCodePointerTableEntryCodeObjectOffset)));
  // The LSB is used as marking bit by the code pointer table, so here we have
  // to set it using a bitwise OR as it may or may not be set.
  Orr(destination, destination, Immediate(kHeapObjectTag));
}

void MacroAssembler::LoadCodeEntrypointViaCodePointer(Register destination,
                                                      MemOperand field_operand,
                                                      CodeEntrypointTag tag) {
  DCHECK_NE(tag, kInvalidEntrypointTag);
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Mov(scratch, ExternalReference::code_pointer_table_address());
  Ldr(destination.W(), field_operand);
  // TODO(saelo): can the offset computation be done more efficiently?
  Mov(destination, Operand(destination, LSR, kCodePointerHandleShift));
  Mov(destination, Operand(destination, LSL, kCodePointerTableEntrySizeLog2));
  Ldr(destination, MemOperand(scratch, destination));
  if (tag != 0) {
    Mov(scratch, Immediate(tag));
    Eor(destination, destination, scratch);
  }
}
#endif  // V8_ENABLE_SANDBOX

void MacroAssembler::LoadProtectedPointerField(Register destination,
                                               MemOperand field_operand) {
  DCHECK(root_array_available());
#ifdef V8_ENABLE_SANDBOX
  DecompressProtected(destination, field_operand);
#else
  LoadTaggedField(destination, field_operand);
#endif
}

void MacroAssembler::MaybeSaveRegisters(RegList registers) {
  if (registers.is_empty()) return;
  ASM_CODE_COMMENT(this);
  CPURegList regs(kXRegSizeInBits, registers);
  // If we were saving LR, we might need to sign it.
  DCHECK(!regs.IncludesAliasOf(lr));
  regs.Align();
  PushCPURegList(regs);
}

void MacroAssembler::MaybeRestoreRegisters(RegList registers) {
  if (registers.is_empty()) return;
  ASM_CODE_COMMENT(this);
  CPURegList regs(kXRegSizeInBits, registers);
  // If we were saving LR, we might need to sign it.
  DCHECK(!regs.IncludesAliasOf(lr));
  regs.Align();
  PopCPURegList(regs);
}

void MacroAssembler::CallEphemeronKeyBarrier(Register object, Operand offset,
                                             SaveFPRegsMode fp_mode) {
  ASM_CODE_COMMENT(this);
  RegList registers = WriteBarrierDescriptor::ComputeSavedRegisters(object);
  MaybeSaveRegisters(registers);

  MoveObjectAndSlot(WriteBarrierDescriptor::ObjectRegister(),
                    WriteBarrierDescriptor::SlotAddressRegister(), object,
                    offset);

  CallBuiltin(Builtins::EphemeronKeyBarrier(fp_mode));
  MaybeRestoreRegisters(registers);
}

void MacroAssembler::CallIndirectPointerBarrier(Register object, Operand offset,
                                                SaveFPRegsMode fp_mode,
                                                IndirectPointerTag tag) {
  ASM_CODE_COMMENT(this);
  RegList registers =
      IndirectPointerWriteBarrierDescriptor::ComputeSavedRegisters(object);
  MaybeSaveRegisters(registers);

  MoveObjectAndSlot(
      IndirectPointerWriteBarrierDescriptor::ObjectRegister(),
      IndirectPointerWriteBarrierDescriptor::SlotAddressRegister(), object,
      offset);
  Mov(IndirectPointerWriteBarrierDescriptor::IndirectPointerTagRegister(),
      Operand(tag));

  CallBuiltin(Builtins::IndirectPointerBarrier(fp_mode));
  MaybeRestoreRegisters(registers);
}

void MacroAssembler::CallRecordWriteStubSaveRegisters(Register object,
                                                      Operand offset,
                                                      SaveFPRegsMode fp_mode,
                                                      StubCallMode mode) {
  ASM_CODE_COMMENT(this);
  RegList registers = WriteBarrierDescriptor::ComputeSavedRegisters(object);
  MaybeSaveRegisters(registers);

  Register object_parameter = WriteBarrierDescriptor::ObjectRegister();
  Register slot_address_parameter =
      WriteBarrierDescriptor::SlotAddressRegister();
  MoveObjectAndSlot(object_parameter, slot_address_parameter, object, offset);

  CallRecordWriteStub(object_parameter, slot_address_parameter, fp_mode, mode);

  MaybeRestoreRegisters(registers);
}

void MacroAssembler::CallRecordWriteStub(Register object, Register slot_address,
                                         SaveFPRegsMode fp_mode,
                                         StubCallMode mode) {
  ASM_CODE_COMMENT(this);
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

void MacroAssembler::MoveObjectAndSlot(Register dst_object, Register dst_slot,
                                       Register object, Operand offset) {
  ASM_CODE_COMMENT(this);
  DCHECK_NE(dst_object, dst_slot);
  // If `offset` is a register, it cannot overlap with `object`.
  DCHECK_IMPLIES(!offset.IsImmediate(), offset.reg() != object);

  // If the slot register does not overlap with the object register, we can
  // overwrite it.
  if (dst_slot != object) {
    Add(dst_slot, object, offset);
    Mov(dst_object, object);
    return;
  }

  DCHECK_EQ(dst_slot, object);

  // If the destination object register does not overlap with the offset
  // register, we can overwrite it.
  if (offset.IsImmediate() || (offset.reg() != dst_object)) {
    Mov(dst_object, dst_slot);
    Add(dst_slot, dst_slot, offset);
    return;
  }

  DCHECK_EQ(dst_object, offset.reg());

  // We only have `dst_slot` and `dst_object` left as distinct registers so we
  // have to swap them. We write this as a add+sub sequence to avoid using a
  // scratch register.
  Add(dst_slot, dst_slot, dst_object);
  Sub(dst_object, dst_slot, dst_object);
}

// If lr_status is kLRHasBeenSaved, lr will be clobbered.
//
// The register 'object' contains a heap object pointer. The heap object tag is
// shifted away.
void MacroAssembler::RecordWrite(Register object, Operand offset,
                                 Register value, LinkRegisterStatus lr_status,
                                 SaveFPRegsMode fp_mode, SmiCheck smi_check,
                                 SlotDescriptor slot) {
  ASM_CODE_COMMENT(this);
  ASM_LOCATION_IN_ASSEMBLER("MacroAssembler::RecordWrite");
  DCHECK(!AreAliased(object, value));

  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT_STRING(this, "Verify slot_address");
    UseScratchRegisterScope temps(this);
    Register temp = temps.AcquireX();
    DCHECK(!AreAliased(object, value, temp));
    Add(temp, object, offset);
    if (slot.contains_indirect_pointer()) {
      LoadIndirectPointerField(temp, MemOperand(temp),
                               slot.indirect_pointer_tag());
    } else {
      DCHECK(slot.contains_direct_pointer());
      LoadTaggedField(temp, MemOperand(temp));
    }
    Cmp(temp, value);
    Check(eq, AbortReason::kWrongAddressOrValuePassedToRecordWrite);
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

  if (slot.contains_indirect_pointer()) {
    // The indirect pointer write barrier is only enabled during marking.
    JumpIfNotMarking(&done);
  } else {
    CheckPageFlag(value, MemoryChunk::kPointersToHereAreInterestingMask, eq,
                  &done);

    CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask, eq,
                  &done);
  }

  // Record the actual write.
  if (lr_status == kLRHasNotBeenSaved) {
    Push<MacroAssembler::kSignLR>(padreg, lr);
  }
  Register slot_address = WriteBarrierDescriptor::SlotAddressRegister();
  DCHECK(!AreAliased(object, slot_address, value));
  if (slot.contains_direct_pointer()) {
    // TODO(cbruni): Turn offset into int.
    DCHECK(offset.IsImmediate());
    Add(slot_address, object, offset);
    CallRecordWriteStub(object, slot_address, fp_mode,
                        StubCallMode::kCallBuiltinPointer);
  } else {
    DCHECK(slot.contains_indirect_pointer());
    CallIndirectPointerBarrier(object, offset, fp_mode,
                               slot.indirect_pointer_tag());
  }
  if (lr_status == kLRHasNotBeenSaved) {
    Pop<MacroAssembler::kAuthLR>(lr, padreg);
  }
  if (v8_flags.debug_code) Mov(slot_address, Operand(kZapValue));

  Bind(&done);
}

void MacroAssembler::Check(Condition cond, AbortReason reason) {
  Label ok;
  B(cond, &ok);
  Abort(reason);
  // Will not return here.
  Bind(&ok);
}

void MacroAssembler::Trap() { Brk(0); }
void MacroAssembler::DebugBreak() { Debug("DebugBreak", 0, BREAK); }

void MacroAssembler::Abort(AbortReason reason) {
  ASM_CODE_COMMENT(this);
  if (v8_flags.code_comments) {
    RecordComment("Abort message: ");
    RecordComment(GetAbortReason(reason));
  }

  // Avoid emitting call to builtin if requested.
  if (trap_on_abort()) {
    Brk(0);
    return;
  }

  // We need some scratch registers for the MacroAssembler, so make sure we have
  // some. This is safe here because Abort never returns.
  uint64_t old_tmp_list = TmpList()->bits();
  TmpList()->Combine(MacroAssembler::DefaultTmpList());

  if (should_abort_hard()) {
    // We don't care if we constructed a frame. Just pretend we did.
    FrameScope assume_frame(this, StackFrame::NO_FRAME_TYPE);
    Mov(w0, static_cast<int>(reason));
    Call(ExternalReference::abort_with_reason());
    return;
  }

  // Avoid infinite recursion; Push contains some assertions that use Abort.
  HardAbortScope hard_aborts(this);

  Mov(x1, Smi::FromInt(static_cast<int>(reason)));

  {
    // We don't actually want to generate a pile of code for this, so just
    // claim there is a stack frame, without generating one.
    FrameScope scope(this, StackFrame::NO_FRAME_TYPE);
    if (root_array_available()) {
      // Generate an indirect call via builtins entry table here in order to
      // ensure that the interpreter_entry_return_pc_offset is the same for
      // InterpreterEntryTrampoline and InterpreterEntryTrampolineForProfiling
      // when v8_flags.debug_code is enabled.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.AcquireX();
      LoadEntryFromBuiltin(Builtin::kAbort, scratch);
      Call(scratch);
    } else {
      CallBuiltin(Builtin::kAbort);
    }
  }

  TmpList()->set_bits(old_tmp_list);
}

void MacroAssembler::LoadNativeContextSlot(Register dst, int index) {
  LoadMap(dst, cp);
  LoadTaggedField(
      dst, FieldMemOperand(
               dst, Map::kConstructorOrBackPointerOrNativeContextOffset));
  LoadTaggedField(dst, MemOperand(dst, Context::SlotOffset(index)));
}

void MacroAssembler::TryLoadOptimizedOsrCode(Register scratch_and_result,
                                             CodeKind min_opt_level,
                                             Register feedback_vector,
                                             FeedbackSlot slot,
                                             Label* on_result,
                                             Label::Distance) {
  Label fallthrough, clear_slot;
  LoadTaggedField(
      scratch_and_result,
      FieldMemOperand(feedback_vector,
                      FeedbackVector::OffsetOfElementAt(slot.ToInt())));
  LoadWeakValue(scratch_and_result, scratch_and_result, &fallthrough);

  // Is it marked_for_deoptimization? If yes, clear the slot.
  {
    UseScratchRegisterScope temps(this);

    // The entry references a CodeWrapper object. Unwrap it now.
    LoadCodePointerField(
        scratch_and_result,
        FieldMemOperand(scratch_and_result, CodeWrapper::kCodeOffset));

    Register temp = temps.AcquireX();
    JumpIfCodeIsMarkedForDeoptimization(scratch_and_result, temp, &clear_slot);
    if (min_opt_level == CodeKind::TURBOFAN) {
      JumpIfCodeIsTurbofanned(scratch_and_result, temp, on_result);
      B(&fallthrough);
    } else {
      B(on_result);
    }
  }

  bind(&clear_slot);
  Mov(scratch_and_result, ClearedValue());
  StoreTaggedField(
      scratch_and_result,
      FieldMemOperand(feedback_vector,
                      FeedbackVector::OffsetOfElementAt(slot.ToInt())));

  bind(&fallthrough);
  Mov(scratch_and_result, 0);
}

// This is the main Printf implementation. All other Printf variants call
// PrintfNoPreserve after setting up one or more PreserveRegisterScopes.
void MacroAssembler::PrintfNoPreserve(const char* format,
                                      const CPURegister& arg0,
                                      const CPURegister& arg1,
                                      const CPURegister& arg2,
                                      const CPURegister& arg3) {
  ASM_CODE_COMMENT(this);
  // We cannot handle a caller-saved stack pointer. It doesn't make much sense
  // in most cases anyway, so this restriction shouldn't be too serious.
  DCHECK(!kCallerSaved.IncludesAliasOf(sp));

  // The provided arguments, and their proper procedure-call standard registers.
  CPURegister args[kPrintfMaxArgCount] = {arg0, arg1, arg2, arg3};
  CPURegister pcs[kPrintfMaxArgCount] = {NoReg, NoReg, NoReg, NoReg};

  int arg_count = kPrintfMaxArgCount;

  // The PCS varargs registers for printf. Note that x0 is used for the printf
  // format string.
  static const CPURegList kPCSVarargs =
      CPURegList(CPURegister::kRegister, kXRegSizeInBits, 1, arg_count);
  static const CPURegList kPCSVarargsFP =
      CPURegList(CPURegister::kVRegister, kDRegSizeInBits, 0, arg_count - 1);

  // We can use caller-saved registers as scratch values, except for the
  // arguments and the PCS registers where they might need to go.
  CPURegList tmp_list = kCallerSaved;
  tmp_list.Remove(x0);  // Used to pass the format string.
  tmp_list.Remove(kPCSVarargs);
  tmp_list.Remove(arg0, arg1, arg2, arg3);

  CPURegList fp_tmp_list = kCallerSavedV;
  fp_tmp_list.Remove(kPCSVarargsFP);
  fp_tmp_list.Remove(arg0, arg1, arg2, arg3);

  // Override the MacroAssembler's scratch register list. The lists will be
  // reset automatically at the end of the UseScratchRegisterScope.
  UseScratchRegisterScope temps(this);
  TmpList()->set_bits(tmp_list.bits());
  FPTmpList()->set_bits(fp_tmp_list.bits());

  // Copies of the printf vararg registers that we can pop from.
  CPURegList pcs_varargs = kPCSVarargs;
#ifndef V8_OS_WIN
  CPURegList pcs_varargs_fp = kPCSVarargsFP;
#endif

  // Place the arguments. There are lots of clever tricks and optimizations we
  // could use here, but Printf is a debug tool so instead we just try to keep
  // it simple: Move each input that isn't already in the right place to a
  // scratch register, then move everything back.
  for (unsigned i = 0; i < kPrintfMaxArgCount; i++) {
    // Work out the proper PCS register for this argument.
    if (args[i].IsRegister()) {
      pcs[i] = pcs_varargs.PopLowestIndex().X();
      // We might only need a W register here. We need to know the size of the
      // argument so we can properly encode it for the simulator call.
      if (args[i].Is32Bits()) pcs[i] = pcs[i].W();
    } else if (args[i].IsVRegister()) {
      // In C, floats are always cast to doubles for varargs calls.
#ifdef V8_OS_WIN
      // In case of variadic functions SIMD and Floating-point registers
      // aren't used. The general x0-x7 should be used instead.
      // https://docs.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions
      pcs[i] = pcs_varargs.PopLowestIndex().X();
#else
      pcs[i] = pcs_varargs_fp.PopLowestIndex().D();
#endif
    } else {
      DCHECK(args[i].IsNone());
      arg_count = i;
      break;
    }

    // If the argument is already in the right place, leave it where it is.
    if (args[i].Aliases(pcs[i])) continue;

    // Otherwise, if the argument is in a PCS argument register, allocate an
    // appropriate scratch register and then move it out of the way.
    if (kPCSVarargs.IncludesAliasOf(args[i]) ||
        kPCSVarargsFP.IncludesAliasOf(args[i])) {
      if (args[i].IsRegister()) {
        Register old_arg = args[i].Reg();
        Register new_arg = temps.AcquireSameSizeAs(old_arg);
        Mov(new_arg, old_arg);
        args[i] = new_arg;
      } else {
        VRegister old_arg = args[i].VReg();
        VRegister new_arg = temps.AcquireSameSizeAs(old_arg);
        Fmov(new_arg, old_arg);
        args[i] = new_arg;
      }
    }
  }

  // Do a second pass to move values into their final positions and perform any
  // conversions that may be required.
  for (int i = 0; i < arg_count; i++) {
#ifdef V8_OS_WIN
    if (args[i].IsVRegister()) {
      if (pcs[i].SizeInBytes() != args[i].SizeInBytes()) {
        // If the argument is half- or single-precision
        // converts to double-precision before that is
        // moved into the one of X scratch register.
        VRegister temp0 = temps.AcquireD();
        Fcvt(temp0.VReg(), args[i].VReg());
        Fmov(pcs[i].Reg(), temp0);
      } else {
        Fmov(pcs[i].Reg(), args[i].VReg());
      }
    } else {
      Mov(pcs[i].Reg(), args[i].Reg(), kDiscardForSameWReg);
    }
#else
    DCHECK(pcs[i].type() == args[i].type());
    if (pcs[i].IsRegister()) {
      Mov(pcs[i].Reg(), args[i].Reg(), kDiscardForSameWReg);
    } else {
      DCHECK(pcs[i].IsVRegister());
      if (pcs[i].SizeInBytes() == args[i].SizeInBytes()) {
        Fmov(pcs[i].VReg(), args[i].VReg());
      } else {
        Fcvt(pcs[i].VReg(), args[i].VReg());
      }
    }
#endif
  }

  // Load the format string into x0, as per the procedure-call standard.
  //
  // To make the code as portable as possible, the format string is encoded
  // directly in the instruction stream. It might be cleaner to encode it in a
  // literal pool, but since Printf is usually used for debugging, it is
  // beneficial for it to be minimally dependent on other features.
  Label format_address;
  Adr(x0, &format_address);

  // Emit the format string directly in the instruction stream.
  {
    BlockPoolsScope scope(this);
    Label after_data;
    B(&after_data);
    Bind(&format_address);
    EmitStringData(format);
    Unreachable();
    Bind(&after_data);
  }

  CallPrintf(arg_count, pcs);
}

void MacroAssembler::CallPrintf(int arg_count, const CPURegister* args) {
  ASM_CODE_COMMENT(this);
  // A call to printf needs special handling for the simulator, since the system
  // printf function will use a different instruction set and the procedure-call
  // standard will not be compatible.
  if (options().enable_simulator_code) {
    InstructionAccurateScope scope(this, kPrintfLength / kInstrSize);
    hlt(kImmExceptionIsPrintf);
    dc32(arg_count);  // kPrintfArgCountOffset

    // Determine the argument pattern.
    uint32_t arg_pattern_list = 0;
    for (int i = 0; i < arg_count; i++) {
      uint32_t arg_pattern;
      if (args[i].IsRegister()) {
        arg_pattern = args[i].Is32Bits() ? kPrintfArgW : kPrintfArgX;
      } else {
        DCHECK(args[i].Is64Bits());
        arg_pattern = kPrintfArgD;
      }
      DCHECK(arg_pattern < (1 << kPrintfArgPatternBits));
      arg_pattern_list |= (arg_pattern << (kPrintfArgPatternBits * i));
    }
    dc32(arg_pattern_list);  // kPrintfArgPatternListOffset
    return;
  }

  Call(ExternalReference::printf_function());
}

void MacroAssembler::Printf(const char* format, CPURegister arg0,
                            CPURegister arg1, CPURegister arg2,
                            CPURegister arg3) {
  ASM_CODE_COMMENT(this);
  // Printf is expected to preserve all registers, so make sure that none are
  // available as scratch registers until we've preserved them.
  uint64_t old_tmp_list = TmpList()->bits();
  uint64_t old_fp_tmp_list = FPTmpList()->bits();
  TmpList()->set_bits(0);
  FPTmpList()->set_bits(0);

  CPURegList saved_registers = kCallerSaved;
  saved_registers.Align();

  // Preserve all caller-saved registers as well as NZCV.
  // PushCPURegList asserts that the size of each list is a multiple of 16
  // bytes.
  PushCPURegList(saved_registers);
  PushCPURegList(kCallerSavedV);

  // We can use caller-saved registers as scratch values (except for argN).
  CPURegList tmp_list = saved_registers;
  CPURegList fp_tmp_list = kCallerSavedV;
  tmp_list.Remove(arg0, arg1, arg2, arg3);
  fp_tmp_list.Remove(arg0, arg1, arg2, arg3);
  TmpList()->set_bits(tmp_list.bits());
  FPTmpList()->set_bits(fp_tmp_list.bits());

  {
    UseScratchRegisterScope temps(this);
    // If any of the arguments are the current stack pointer, allocate a new
    // register for them, and adjust the value to compensate for pushing the
    // caller-saved registers.
    bool arg0_sp = arg0.is_valid() && sp.Aliases(arg0);
    bool arg1_sp = arg1.is_valid() && sp.Aliases(arg1);
    bool arg2_sp = arg2.is_valid() && sp.Aliases(arg2);
    bool arg3_sp = arg3.is_valid() && sp.Aliases(arg3);
    if (arg0_sp || arg1_sp || arg2_sp || arg3_sp) {
      // Allocate a register to hold the original stack pointer value, to pass
      // to PrintfNoPreserve as an argument.
      Register arg_sp = temps.AcquireX();
      Add(arg_sp, sp,
          saved_registers.TotalSizeInBytes() +
              kCallerSavedV.TotalSizeInBytes());
      if (arg0_sp) arg0 = Register::Create(arg_sp.code(), arg0.SizeInBits());
      if (arg1_sp) arg1 = Register::Create(arg_sp.code(), arg1.SizeInBits());
      if (arg2_sp) arg2 = Register::Create(arg_sp.code(), arg2.SizeInBits());
      if (arg3_sp) arg3 = Register::Create(arg_sp.code(), arg3.SizeInBits());
    }

    // Preserve NZCV.
    {
      UseScratchRegisterScope temps(this);
      Register tmp = temps.AcquireX();
      Mrs(tmp, NZCV);
      Push(tmp, xzr);
    }

    PrintfNoPreserve(format, arg0, arg1, arg2, arg3);

    // Restore NZCV.
    {
      UseScratchRegisterScope temps(this);
      Register tmp = temps.AcquireX();
      Pop(xzr, tmp);
      Msr(NZCV, tmp);
    }
  }

  PopCPURegList(kCallerSavedV);
  PopCPURegList(saved_registers);

  TmpList()->set_bits(old_tmp_list);
  FPTmpList()->set_bits(old_fp_tmp_list);
}

void MacroAssembler::ComputeCodeStartAddress(const Register& rd) {
  // We can use adr to load a pc relative location.
  adr(rd, -pc_offset());
}

void MacroAssembler::RestoreFPAndLR() {
  static_assert(StandardFrameConstants::kCallerFPOffset + kSystemPointerSize ==
                    StandardFrameConstants::kCallerPCOffset,
                "Offsets must be consecutive for ldp!");
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  // Make sure we can use x16 and x17.
  UseScratchRegisterScope temps(this);
  temps.Exclude(x16, x17);
  // We can load the return address directly into x17.
  Add(x16, fp, StandardFrameConstants::kCallerSPOffset);
  Ldp(fp, x17, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  Autib1716();
  Mov(lr, x17);
#else
  Ldp(fp, lr, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
#endif
}

#if V8_ENABLE_WEBASSEMBLY
void MacroAssembler::StoreReturnAddressInWasmExitFrame(Label* return_location) {
  UseScratchRegisterScope temps(this);
  temps.Exclude(x16, x17);
  Adr(x17, return_location);
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  Add(x16, fp, WasmExitFrameConstants::kCallingPCOffset + kSystemPointerSize);
  Pacib1716();
#endif
  Str(x17, MemOperand(fp, WasmExitFrameConstants::kCallingPCOffset));
}
#endif  // V8_ENABLE_WEBASSEMBLY

void MacroAssembler::PopcntHelper(Register dst, Register src) {
  UseScratchRegisterScope temps(this);
  VRegister scratch = temps.AcquireV(kFormat8B);
  VRegister tmp = src.Is32Bits() ? scratch.S() : scratch.D();
  Fmov(tmp, src);
  Cnt(scratch, scratch);
  Addv(scratch.B(), scratch);
  Fmov(dst, tmp);
}

void MacroAssembler::I8x16BitMask(Register dst, VRegister src, VRegister temp) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  VRegister tmp = temps.AcquireQ();
  VRegister mask = temps.AcquireQ();

  if (CpuFeatures::IsSupported(PMULL1Q) && temp.is_valid()) {
    CpuFeatureScope scope(this, PMULL1Q);

    Movi(mask.V2D(), 0x0102'0408'1020'4080);
    // Normalize the input - at most 1 bit per vector element should be set.
    Ushr(tmp.V16B(), src.V16B(), 7);
    // Collect the input bits into a byte of the output - once for each
    // half of the input.
    Pmull2(temp.V1Q(), mask.V2D(), tmp.V2D());
    Pmull(tmp.V1Q(), mask.V1D(), tmp.V1D());
    // Combine the bits from both input halves.
    Trn2(tmp.V8B(), tmp.V8B(), temp.V8B());
    Mov(dst.W(), tmp.V8H(), 3);
  } else {
    // Set i-th bit of each lane i. When AND with tmp, the lanes that
    // are signed will have i-th bit set, unsigned will be 0.
    Sshr(tmp.V16B(), src.V16B(), 7);
    Movi(mask.V2D(), 0x8040'2010'0804'0201);
    And(tmp.V16B(), mask.V16B(), tmp.V16B());
    Ext(mask.V16B(), tmp.V16B(), tmp.V16B(), 8);
    Zip1(tmp.V16B(), tmp.V16B(), mask.V16B());
    Addv(tmp.H(), tmp.V8H());
    Mov(dst.W(), tmp.V8H(), 0);
  }
}

void MacroAssembler::I16x8BitMask(Register dst, VRegister src) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  VRegister tmp = temps.AcquireQ();
  VRegister mask = temps.AcquireQ();

  if (CpuFeatures::IsSupported(PMULL1Q)) {
    CpuFeatureScope scope(this, PMULL1Q);

    // Normalize the input - at most 1 bit per vector element should be set.
    Ushr(tmp.V8H(), src.V8H(), 15);
    Movi(mask.V1D(), 0x0102'0408'1020'4080);
    // Trim some of the redundant 0 bits, so that we can operate on
    // only 64 bits.
    Xtn(tmp.V8B(), tmp.V8H());
    // Collect the input bits into a byte of the output.
    Pmull(tmp.V1Q(), tmp.V1D(), mask.V1D());
    Mov(dst.W(), tmp.V16B(), 7);
  } else {
    Sshr(tmp.V8H(), src.V8H(), 15);
    // Set i-th bit of each lane i. When AND with tmp, the lanes that
    // are signed will have i-th bit set, unsigned will be 0.
    Movi(mask.V2D(), 0x0080'0040'0020'0010, 0x0008'0004'0002'0001);
    And(tmp.V16B(), mask.V16B(), tmp.V16B());
    Addv(tmp.H(), tmp.V8H());
    Mov(dst.W(), tmp.V8H(), 0);
  }
}

void MacroAssembler::I32x4BitMask(Register dst, VRegister src) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register tmp = temps.AcquireX();
  Mov(dst.X(), src.D(), 1);
  Fmov(tmp.X(), src.D());
  And(dst.X(), dst.X(), 0x80000000'80000000);
  And(tmp.X(), tmp.X(), 0x80000000'80000000);
  Orr(dst.X(), dst.X(), Operand(dst.X(), LSL, 31));
  Orr(tmp.X(), tmp.X(), Operand(tmp.X(), LSL, 31));
  Lsr(dst.X(), dst.X(), 60);
  Bfxil(dst.X(), tmp.X(), 62, 2);
}

void MacroAssembler::I64x2BitMask(Register dst, VRegister src) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope scope(this);
  Register tmp = scope.AcquireX();
  Mov(dst.X(), src.D(), 1);
  Fmov(tmp.X(), src.D());
  Lsr(dst.X(), dst.X(), 62);
  Bfxil(dst.X(), tmp.X(), 63, 1);
}

void MacroAssembler::I64x2AllTrue(Register dst, VRegister src) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope scope(this);
  VRegister tmp = scope.AcquireV(kFormat2D);
  Cmeq(tmp.V2D(), src.V2D(), 0);
  Addp(tmp.D(), tmp);
  Fcmp(tmp.D(), tmp.D());
  Cset(dst, eq);
}

// Calls an API function. Allocates HandleScope, extracts returned value
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
  ASM_CODE_COMMENT(masm);
  ASM_LOCATION("CallApiFunctionAndReturn");

  using ER = ExternalReference;

  Isolate* isolate = masm->isolate();
  MemOperand next_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_next_address(isolate), no_reg);
  MemOperand limit_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_limit_address(isolate), no_reg);
  MemOperand level_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_level_address(isolate), no_reg);

  Register return_value = x0;
  Register scratch = x4;
  Register scratch2 = x5;

  // Allocate HandleScope in callee-saved registers.
  // We will need to restore the HandleScope after the call to the API function,
  // by allocating it in callee-saved registers it'll be preserved by C code.
  Register prev_next_address_reg = x19;
  Register prev_limit_reg = x20;
  Register prev_level_reg = w21;

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

  // Explicitly include x16/x17 to let StoreReturnAddressAndCall() use them.
  UseScratchRegisterScope fix_temps(masm);
  fix_temps.Include(x16, x17);

  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Allocate HandleScope in callee-save registers.");
    __ Ldr(prev_next_address_reg, next_mem_op);
    __ Ldr(prev_limit_reg, limit_mem_op);
    __ Ldr(prev_level_reg, level_mem_op);
    __ Add(scratch.W(), prev_level_reg, 1);
    __ Str(scratch.W(), level_mem_op);
  }

  Label profiler_or_side_effects_check_enabled, done_api_call;
  if (with_profiling) {
    __ RecordComment("Check if profiler or side effects check is enabled");
    __ Ldrb(scratch.W(),
            __ ExternalReferenceAsOperand(IsolateFieldId::kExecutionMode));
    __ Cbnz(scratch.W(), &profiler_or_side_effects_check_enabled);
#ifdef V8_RUNTIME_CALL_STATS
    __ RecordComment("Check if RCS is enabled");
    __ Mov(scratch, ER::address_of_runtime_stats_flag());
    __ Ldrsw(scratch.W(), MemOperand(scratch));
    __ Cbnz(scratch.W(), &profiler_or_side_effects_check_enabled);
#endif  // V8_RUNTIME_CALL_STATS
  }

  __ RecordComment("Call the api function directly.");
  __ StoreReturnAddressAndCall(function_address);
  __ Bind(&done_api_call);

  Label propagate_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;

  __ RecordComment("Load the value from ReturnValue");
  __ Ldr(return_value, return_value_operand);

  {
    ASM_CODE_COMMENT_STRING(
        masm,
        "No more valid handles (the result handle was the last one)."
        "Restore previous handle scope.");
    __ Str(prev_next_address_reg, next_mem_op);
    if (v8_flags.debug_code) {
      __ Ldr(scratch.W(), level_mem_op);
      __ Sub(scratch.W(), scratch.W(), 1);
      __ Cmp(scratch.W(), prev_level_reg);
      __ Check(eq, AbortReason::kUnexpectedLevelAfterReturnFromApiCall);
    }
    __ Str(prev_level_reg, level_mem_op);

    __ Ldr(scratch, limit_mem_op);
    __ Cmp(prev_limit_reg, scratch);
    __ B(ne, &delete_allocated_handles);
  }

  __ RecordComment("Leave the API exit frame.");
  __ Bind(&leave_exit_frame);

  Register argc_reg = prev_limit_reg;
  if (argc_operand != nullptr) {
    // Load the number of stack slots to drop before LeaveExitFrame modifies sp.
    __ Ldr(argc_reg, *argc_operand);
  }

  __ LeaveExitFrame(scratch, scratch2);

  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Check if the function scheduled an exception.");
    __ Mov(scratch, ER::exception_address(isolate));
    __ Ldr(scratch, MemOperand(scratch));
    __ JumpIfNotRoot(scratch, RootIndex::kTheHoleValue, &propagate_exception);
  }

  __ AssertJSAny(return_value, scratch, scratch2,
                 AbortReason::kAPICallReturnedInvalidObject);

  if (argc_operand == nullptr) {
    DCHECK_NE(slots_to_drop_on_return, 0);
    __ DropSlots(slots_to_drop_on_return);
  } else {
    // {argc_operand} was loaded into {argc_reg} above.
    __ DropArguments(argc_reg, slots_to_drop_on_return);
  }
  __ Ret();

  if (with_profiling) {
    ASM_CODE_COMMENT_STRING(masm, "Call the api function via thunk wrapper.");
    __ Bind(&profiler_or_side_effects_check_enabled);
    // Additional parameter is the address of the actual callback function.
    if (thunk_arg.is_valid()) {
      MemOperand thunk_arg_mem_op = __ ExternalReferenceAsOperand(
          IsolateFieldId::kApiCallbackThunkArgument);
      __ Str(thunk_arg, thunk_arg_mem_op);
    }
    __ Mov(scratch, thunk_ref);
    __ StoreReturnAddressAndCall(scratch);
    __ B(&done_api_call);
  }

  __ RecordComment("An exception was thrown. Propagate it.");
  __ Bind(&propagate_exception);
  __ TailCallRuntime(Runtime::kPropagateException);

  {
    ASM_CODE_COMMENT_STRING(
        masm, "HandleScope limit has changed. Delete allocated extensions.");
    __ Bind(&delete_allocated_handles);
    __ Str(prev_limit_reg, limit_mem_op);
    // Save the return value in a callee-save register.
    Register saved_result = prev_limit_reg;
    __ Mov(saved_result, x0);
    __ Mov(kCArgRegs[0], ER::isolate_address());
    __ CallCFunction(ER::delete_handle_scope_extensions(), 1);
    __ Mov(kCArgRegs[0], saved_result);
    __ B(&leave_exit_frame);
  }
}

}  // namespace internal
}  // namespace v8

#undef __

#endif  // V8_TARGET_ARCH_ARM64
