// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>  // For LONG_MIN, LONG_MAX.

#if V8_TARGET_ARCH_LOONG64

#include <optional>

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
#include "src/codegen/loong64/macro-assembler-loong64.h"
#endif

#define __ ACCESS_MASM(masm)

namespace v8 {
namespace internal {

static inline bool IsZero(const Operand& rk) {
  if (rk.is_reg()) {
    return rk.rm() == zero_reg;
  } else {
    return rk.immediate() == 0;
  }
}

int MacroAssembler::RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                                    Register exclusion1,
                                                    Register exclusion2,
                                                    Register exclusion3) const {
  int bytes = 0;

  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  bytes += list.Count() * kSystemPointerSize;

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
  bytes += list.Count() * kSystemPointerSize;

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
  bytes += list.Count() * kSystemPointerSize;

  return bytes;
}

void MacroAssembler::LoadRoot(Register destination, RootIndex index) {
  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index) &&
      is_int12(ReadOnlyRootPtr(index))) {
    DecompressTagged(destination, ReadOnlyRootPtr(index));
    return;
  }
  // Many roots have addresses that are too large to fit into addition immediate
  // operands. Evidence suggests that the extra instruction for decompression
  // costs us more than the load.
  Ld_d(destination, MemOperand(s6, RootRegisterOffsetForRootIndex(index)));
}
void MacroAssembler::LoadTaggedRoot(Register destination, RootIndex index) {
  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index) &&
      is_int12(ReadOnlyRootPtr(index))) {
    li(destination, (int32_t)ReadOnlyRootPtr(index));
    return;
  }
  Ld_w(destination, MemOperand(s6, RootRegisterOffsetForRootIndex(index)));
}

void MacroAssembler::PushCommonFrame(Register marker_reg) {
  if (marker_reg.is_valid()) {
    Push(ra, fp, marker_reg);
    Add_d(fp, sp, Operand(kSystemPointerSize));
  } else {
    Push(ra, fp);
    mov(fp, sp);
  }
}

void MacroAssembler::PushStandardFrame(Register function_reg) {
  int offset = -StandardFrameConstants::kContextOffset;
  if (function_reg.is_valid()) {
    Push(ra, fp, cp, function_reg, kJavaScriptCallArgCountRegister);
    offset += 2 * kSystemPointerSize;
  } else {
    Push(ra, fp, cp, kJavaScriptCallArgCountRegister);
    offset += kSystemPointerSize;
  }
  Add_d(fp, sp, Operand(offset));
}

// Clobbers object, dst, value, and ra, if (ra_status == kRAHasBeenSaved)
// The register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWriteField(Register object, int offset,
                                      Register value, RAStatus ra_status,
                                      SaveFPRegsMode save_fp,
                                      SmiCheck smi_check, SlotDescriptor slot) {
  ASM_CODE_COMMENT(this);
  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis.
  Label done;

  // Skip barrier if writing a smi.
  if (smi_check == SmiCheck::kInline) {
    JumpIfSmi(value, &done);
  }

  // Although the object register is tagged, the offset is relative to the start
  // of the object, so offset must be a multiple of kPointerSize.
  DCHECK(IsAligned(offset, kTaggedSize));

  if (v8_flags.debug_code) {
    Label ok;
    BlockTrampolinePoolScope block_trampoline_pool(this);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Add_d(scratch, object, offset - kHeapObjectTag);
    And(scratch, scratch, Operand(kTaggedSize - 1));
    Branch(&ok, eq, scratch, Operand(zero_reg));
    Abort(AbortReason::kUnalignedCellInWriteBarrier);
    bind(&ok);
  }

  RecordWrite(object, Operand(offset - kHeapObjectTag), value, ra_status,
              save_fp, SmiCheck::kOmit, slot);

  bind(&done);
}

void MacroAssembler::DecodeSandboxedPointer(Register value) {
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  srli_d(value, value, kSandboxedPointerShift);
  Add_d(value, value, kPtrComprCageBaseRegister);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::LoadSandboxedPointerField(Register destination,
                                               MemOperand field_operand) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  Ld_d(destination, field_operand);
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
  Register scratch = temps.Acquire();
  Sub_d(scratch, value, kPtrComprCageBaseRegister);
  slli_d(scratch, scratch, kSandboxedPointerShift);
  St_d(scratch, dst_field_operand);
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
  Register external_table = temps.Acquire();
  if (isolate_root == no_reg) {
    DCHECK(root_array_available_);
    isolate_root = kRootRegister;
  }
  Ld_d(external_table,
       MemOperand(isolate_root,
                  IsolateData::external_pointer_table_offset() +
                      Internals::kExternalPointerTableBasePointerOffset));
  Ld_wu(destination, field_operand);
  srli_d(destination, destination, kExternalPointerIndexShift);
  slli_d(destination, destination, kExternalPointerTableEntrySizeLog2);
  Ld_d(destination, MemOperand(external_table, destination));
  // We need another scratch register for the 64-bit tag constant. Instead of
  // forcing the `And` to allocate a new temp register (which we may not have),
  // reuse the temp register that we used for the external pointer table base.
  Register tag_reg = external_table;
  li(tag_reg, Operand(~tag));
  And(destination, destination, tag_reg);
#else
  Ld_d(destination, field_operand);
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
  Register handle = temps.hasAvailable() ? temps.Acquire() : t8;
  Ld_wu(handle, field_operand);

  ResolveIndirectPointerHandle(destination, handle, tag);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

void MacroAssembler::StoreIndirectPointerField(Register value,
                                               MemOperand dst_field_operand) {
#ifdef V8_ENABLE_SANDBOX
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Ld_w(scratch, FieldMemOperand(
                    value, ExposedTrustedObject::kSelfIndirectPointerOffset));
  St_w(scratch, dst_field_operand);
#else
  UNREACHABLE();
#endif
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
    DCHECK(!AreAliased(destination, handle));
    And(destination, handle, kCodePointerHandleMarker);
    Branch(&is_trusted_pointer_handle, eq, destination, Operand(zero_reg));
    ResolveCodePointerHandle(destination, handle);
    Branch(&done);
    bind(&is_trusted_pointer_handle);
    ResolveTrustedPointerHandle(destination, handle,
                                kUnknownIndirectPointerTag);
    bind(&done);
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

  DCHECK(root_array_available_);
  Register table = destination;
  Ld_d(table,
       MemOperand(kRootRegister, IsolateData::trusted_pointer_table_offset()));
  srli_d(handle, handle, kTrustedPointerHandleShift);
  Alsl_d(destination, handle, table, kTrustedPointerTableEntrySizeLog2);
  Ld_d(destination, MemOperand(destination, 0));
  // Untag the pointer and remove the marking bit in one operation.
  Register tag_reg = handle;
  li(tag_reg, Operand(~(tag | kTrustedPointerTableMarkBit)));
  and_(destination, destination, tag_reg);
}

void MacroAssembler::ResolveCodePointerHandle(Register destination,
                                              Register handle) {
  DCHECK(!AreAliased(handle, destination));

  Register table = destination;
  li(table, ExternalReference::code_pointer_table_address());
  srli_d(handle, handle, kCodePointerHandleShift);
  Alsl_d(destination, handle, table, kCodePointerTableEntrySizeLog2);
  Ld_d(destination,
       MemOperand(destination, kCodePointerTableEntryCodeObjectOffset));
  // The LSB is used as marking bit by the code pointer table, so here we have
  // to set it using a bitwise OR as it may or may not be set.
  Or(destination, destination, Operand(kHeapObjectTag));
}

void MacroAssembler::LoadCodeEntrypointViaCodePointer(Register destination,
                                                      MemOperand field_operand,
                                                      CodeEntrypointTag tag) {
  DCHECK_NE(tag, kInvalidEntrypointTag);
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, ExternalReference::code_pointer_table_address());
  Ld_wu(destination, field_operand);
  srli_d(destination, destination, kCodePointerHandleShift);
  slli_d(destination, destination, kCodePointerTableEntrySizeLog2);
  Ld_d(destination, MemOperand(scratch, destination));
  if (tag != 0) {
    li(scratch, Operand(tag));
    xor_(destination, destination, scratch);
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
  MultiPush(registers);
}

void MacroAssembler::MaybeRestoreRegisters(RegList registers) {
  if (registers.is_empty()) return;
  MultiPop(registers);
}

void MacroAssembler::CallEphemeronKeyBarrier(Register object, Operand offset,
                                             SaveFPRegsMode fp_mode) {
  ASM_CODE_COMMENT(this);
  RegList registers = WriteBarrierDescriptor::ComputeSavedRegisters(object);
  MaybeSaveRegisters(registers);

  Register object_parameter = WriteBarrierDescriptor::ObjectRegister();
  Register slot_address_parameter =
      WriteBarrierDescriptor::SlotAddressRegister();

  MoveObjectAndSlot(object_parameter, slot_address_parameter, object, offset);

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
  li(IndirectPointerWriteBarrierDescriptor::IndirectPointerTagRegister(),
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

void MacroAssembler::MoveObjectAndSlot(Register dst_object, Register dst_slot,
                                       Register object, Operand offset) {
  ASM_CODE_COMMENT(this);
  DCHECK_NE(dst_object, dst_slot);
  // If `offset` is a register, it cannot overlap with `object`.
  DCHECK_IMPLIES(!offset.IsImmediate(), offset.rm() != object);

  // If the slot register does not overlap with the object register, we can
  // overwrite it.
  if (dst_slot != object) {
    Add_d(dst_slot, object, offset);
    mov(dst_object, object);
    return;
  }

  DCHECK_EQ(dst_slot, object);

  // If the destination object register does not overlap with the offset
  // register, we can overwrite it.
  if (offset.IsImmediate() || (offset.rm() != dst_object)) {
    mov(dst_object, dst_slot);
    Add_d(dst_slot, dst_slot, offset);
    return;
  }

  DCHECK_EQ(dst_object, offset.rm());

  // We only have `dst_slot` and `dst_object` left as distinct registers so we
  // have to swap them. We write this as a add+sub sequence to avoid using a
  // scratch register.
  Add_d(dst_slot, dst_slot, dst_object);
  Sub_d(dst_object, dst_slot, dst_object);
}

// If lr_status is kLRHasBeenSaved, lr will be clobbered.
// TODO(LOONG_dev): LOONG64 Check this comment
// Clobbers object, address, value, and ra, if (ra_status == kRAHasBeenSaved)
// The register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWrite(Register object, Operand offset,
                                 Register value, RAStatus ra_status,
                                 SaveFPRegsMode fp_mode, SmiCheck smi_check,
                                 SlotDescriptor slot) {
  DCHECK(!AreAliased(object, value));

  if (v8_flags.debug_code) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
    Add_d(scratch, object, offset);
    if (slot.contains_indirect_pointer()) {
      LoadIndirectPointerField(scratch, MemOperand(scratch, 0),
                               slot.indirect_pointer_tag());
    } else {
      DCHECK(slot.contains_direct_pointer());
      LoadTaggedField(scratch, MemOperand(scratch, 0));
    }
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

  CheckPageFlag(value, MemoryChunk::kPointersToHereAreInterestingMask, eq,
                &done);

  CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask, eq,
                &done);

  // Record the actual write.
  if (ra_status == kRAHasNotBeenSaved) {
    Push(ra);
  }

  Register slot_address = WriteBarrierDescriptor::SlotAddressRegister();
  DCHECK(!AreAliased(object, slot_address, value));
  if (slot.contains_direct_pointer()) {
    DCHECK(offset.IsImmediate());
    Add_d(slot_address, object, offset);
    CallRecordWriteStub(object, slot_address, fp_mode,
                        StubCallMode::kCallBuiltinPointer);
  } else {
    DCHECK(slot.contains_indirect_pointer());
    CallIndirectPointerBarrier(object, offset, fp_mode,
                               slot.indirect_pointer_tag());
  }
  if (ra_status == kRAHasNotBeenSaved) {
    Pop(ra);
  }

  bind(&done);
}

// ---------------------------------------------------------------------------
// Instruction macros.

void MacroAssembler::Add_w(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    add_w(rd, rj, rk.rm());
  } else {
    if (is_int12(rk.immediate()) && !MustUseReg(rk.rmode())) {
      addi_w(rd, rj, static_cast<int32_t>(rk.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rj != scratch);
      li(scratch, rk);
      add_w(rd, rj, scratch);
    }
  }
}

void MacroAssembler::Add_d(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    add_d(rd, rj, rk.rm());
  } else {
    if (is_int12(rk.immediate()) && !MustUseReg(rk.rmode())) {
      addi_d(rd, rj, static_cast<int32_t>(rk.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      DCHECK(rj != scratch);
      li(scratch, rk);
      add_d(rd, rj, scratch);
    }
  }
}

void MacroAssembler::Sub_w(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    sub_w(rd, rj, rk.rm());
  } else {
    DCHECK(is_int32(rk.immediate()));
    if (is_int12(-rk.immediate()) && !MustUseReg(rk.rmode())) {
      // No subi_w instr, use addi_w(x, y, -imm).
      addi_w(rd, rj, static_cast<int32_t>(-rk.immediate()));
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rj != scratch);
      if (-rk.immediate() >> 12 == 0 && !MustUseReg(rk.rmode())) {
        // Use load -imm and addu when loading -imm generates one instruction.
        li(scratch, -rk.immediate());
        add_w(rd, rj, scratch);
      } else {
        // li handles the relocation.
        li(scratch, rk);
        sub_w(rd, rj, scratch);
      }
    }
  }
}

void MacroAssembler::Sub_d(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    sub_d(rd, rj, rk.rm());
  } else if (is_int12(-rk.immediate()) && !MustUseReg(rk.rmode())) {
    // No subi_d instr, use addi_d(x, y, -imm).
    addi_d(rd, rj, static_cast<int32_t>(-rk.immediate()));
  } else {
    DCHECK(rj != t7);
    int li_count = InstrCountForLi64Bit(rk.immediate());
    int li_neg_count = InstrCountForLi64Bit(-rk.immediate());
    if (li_neg_count < li_count && !MustUseReg(rk.rmode())) {
      // Use load -imm and add_d when loading -imm generates one instruction.
      DCHECK(rk.immediate() != std::numeric_limits<int32_t>::min());
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, Operand(-rk.immediate()));
      add_d(rd, rj, scratch);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, rk);
      sub_d(rd, rj, scratch);
    }
  }
}

void MacroAssembler::Mul_w(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mul_w(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mul_w(rd, rj, scratch);
  }
}

void MacroAssembler::Mulh_w(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mulh_w(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mulh_w(rd, rj, scratch);
  }
}

void MacroAssembler::Mulh_wu(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mulh_wu(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mulh_wu(rd, rj, scratch);
  }
}

void MacroAssembler::Mul_d(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mul_d(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mul_d(rd, rj, scratch);
  }
}

void MacroAssembler::Mulh_d(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mulh_d(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mulh_d(rd, rj, scratch);
  }
}

void MacroAssembler::Mulh_du(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mulh_du(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mulh_du(rd, rj, scratch);
  }
}

void MacroAssembler::Div_w(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    div_w(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    div_w(rd, rj, scratch);
  }
}

void MacroAssembler::Mod_w(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mod_w(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mod_w(rd, rj, scratch);
  }
}

void MacroAssembler::Mod_wu(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mod_wu(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mod_wu(rd, rj, scratch);
  }
}

void MacroAssembler::Div_d(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    div_d(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    div_d(rd, rj, scratch);
  }
}

void MacroAssembler::Div_wu(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    div_wu(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    div_wu(rd, rj, scratch);
  }
}

void MacroAssembler::Div_du(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    div_du(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    div_du(rd, rj, scratch);
  }
}

void MacroAssembler::Mod_d(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mod_d(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mod_d(rd, rj, scratch);
  }
}

void MacroAssembler::Mod_du(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    mod_du(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    mod_du(rd, rj, scratch);
  }
}

void MacroAssembler::And(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    and_(rd, rj, rk.rm());
  } else {
    if (is_uint12(rk.immediate()) && !MustUseReg(rk.rmode())) {
      andi(rd, rj, static_cast<int32_t>(rk.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rj != scratch);
      li(scratch, rk);
      and_(rd, rj, scratch);
    }
  }
}

void MacroAssembler::Or(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    or_(rd, rj, rk.rm());
  } else {
    if (is_uint12(rk.immediate()) && !MustUseReg(rk.rmode())) {
      ori(rd, rj, static_cast<int32_t>(rk.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rj != scratch);
      li(scratch, rk);
      or_(rd, rj, scratch);
    }
  }
}

void MacroAssembler::Xor(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    xor_(rd, rj, rk.rm());
  } else {
    if (is_uint12(rk.immediate()) && !MustUseReg(rk.rmode())) {
      xori(rd, rj, static_cast<int32_t>(rk.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      DCHECK(rj != scratch);
      li(scratch, rk);
      xor_(rd, rj, scratch);
    }
  }
}

void MacroAssembler::Nor(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    nor(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    nor(rd, rj, scratch);
  }
}

void MacroAssembler::Andn(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    andn(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    andn(rd, rj, scratch);
  }
}

void MacroAssembler::Orn(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    orn(rd, rj, rk.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(rj != scratch);
    li(scratch, rk);
    orn(rd, rj, scratch);
  }
}

void MacroAssembler::Neg(Register rj, const Operand& rk) {
  DCHECK(rk.is_reg());
  sub_d(rj, zero_reg, rk.rm());
}

void MacroAssembler::Slt(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    slt(rd, rj, rk.rm());
  } else {
    if (is_int12(rk.immediate()) && !MustUseReg(rk.rmode())) {
      slti(rd, rj, static_cast<int32_t>(rk.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      DCHECK(rj != scratch);
      li(scratch, rk);
      slt(rd, rj, scratch);
    }
  }
}

void MacroAssembler::Sltu(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    sltu(rd, rj, rk.rm());
  } else {
    if (is_int12(rk.immediate()) && !MustUseReg(rk.rmode())) {
      sltui(rd, rj, static_cast<int32_t>(rk.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      DCHECK(rj != scratch);
      li(scratch, rk);
      sltu(rd, rj, scratch);
    }
  }
}

void MacroAssembler::Sle(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    slt(rd, rk.rm(), rj);
  } else {
    if (rk.immediate() == 0 && !MustUseReg(rk.rmode())) {
      slt(rd, zero_reg, rj);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      BlockTrampolinePoolScope block_trampoline_pool(this);
      DCHECK(rj != scratch);
      li(scratch, rk);
      slt(rd, scratch, rj);
    }
  }
  xori(rd, rd, 1);
}

void MacroAssembler::Sleu(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    sltu(rd, rk.rm(), rj);
  } else {
    if (rk.immediate() == 0 && !MustUseReg(rk.rmode())) {
      sltu(rd, zero_reg, rj);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      BlockTrampolinePoolScope block_trampoline_pool(this);
      DCHECK(rj != scratch);
      li(scratch, rk);
      sltu(rd, scratch, rj);
    }
  }
  xori(rd, rd, 1);
}

void MacroAssembler::Sge(Register rd, Register rj, const Operand& rk) {
  Slt(rd, rj, rk);
  xori(rd, rd, 1);
}

void MacroAssembler::Sgeu(Register rd, Register rj, const Operand& rk) {
  Sltu(rd, rj, rk);
  xori(rd, rd, 1);
}

void MacroAssembler::Sgt(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    slt(rd, rk.rm(), rj);
  } else {
    if (rk.immediate() == 0 && !MustUseReg(rk.rmode())) {
      slt(rd, zero_reg, rj);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      BlockTrampolinePoolScope block_trampoline_pool(this);
      DCHECK(rj != scratch);
      li(scratch, rk);
      slt(rd, scratch, rj);
    }
  }
}

void MacroAssembler::Sgtu(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    sltu(rd, rk.rm(), rj);
  } else {
    if (rk.immediate() == 0 && !MustUseReg(rk.rmode())) {
      sltu(rd, zero_reg, rj);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
      BlockTrampolinePoolScope block_trampoline_pool(this);
      DCHECK(rj != scratch);
      li(scratch, rk);
      sltu(rd, scratch, rj);
    }
  }
}

void MacroAssembler::Rotr_w(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    rotr_w(rd, rj, rk.rm());
  } else {
    int64_t ror_value = rk.immediate() % 32;
    if (ror_value < 0) {
      ror_value += 32;
    }
    rotri_w(rd, rj, ror_value);
  }
}

void MacroAssembler::Rotr_d(Register rd, Register rj, const Operand& rk) {
  if (rk.is_reg()) {
    rotr_d(rd, rj, rk.rm());
  } else {
    int64_t dror_value = rk.immediate() % 64;
    if (dror_value < 0) dror_value += 64;
    rotri_d(rd, rj, dror_value);
  }
}

void MacroAssembler::Alsl_w(Register rd, Register rj, Register rk, uint8_t sa,
                            Register scratch) {
  DCHECK(sa >= 1 && sa <= 31);
  if (sa <= 4) {
    alsl_w(rd, rj, rk, sa);
  } else {
    Register tmp = rd == rk ? scratch : rd;
    DCHECK(tmp != rk);
    slli_w(tmp, rj, sa);
    add_w(rd, rk, tmp);
  }
}

void MacroAssembler::Alsl_d(Register rd, Register rj, Register rk, uint8_t sa,
                            Register scratch) {
  DCHECK(sa >= 1 && sa <= 63);
  if (sa <= 4) {
    alsl_d(rd, rj, rk, sa);
  } else {
    Register tmp = rd == rk ? scratch : rd;
    DCHECK(tmp != rk);
    slli_d(tmp, rj, sa);
    add_d(rd, rk, tmp);
  }
}

// ------------Pseudo-instructions-------------

// Change endianness
void MacroAssembler::ByteSwap(Register dest, Register src, int operand_size) {
  DCHECK(operand_size == 4 || operand_size == 8);
  if (operand_size == 4) {
    revb_2w(dest, src);
    slli_w(dest, dest, 0);
  } else {
    revb_d(dest, src);
  }
}

void MacroAssembler::Ld_b(Register rd, const MemOperand& rj) {
  MemOperand source = rj;
  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    ldx_b(rd, source.base(), source.index());
  } else {
    ld_b(rd, source.base(), source.offset());
  }
}

void MacroAssembler::Ld_bu(Register rd, const MemOperand& rj) {
  MemOperand source = rj;
  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    ldx_bu(rd, source.base(), source.index());
  } else {
    ld_bu(rd, source.base(), source.offset());
  }
}

void MacroAssembler::St_b(Register rd, const MemOperand& rj) {
  MemOperand source = rj;
  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    stx_b(rd, source.base(), source.index());
  } else {
    st_b(rd, source.base(), source.offset());
  }
}

void MacroAssembler::Ld_h(Register rd, const MemOperand& rj) {
  MemOperand source = rj;
  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    ldx_h(rd, source.base(), source.index());
  } else {
    ld_h(rd, source.base(), source.offset());
  }
}

void MacroAssembler::Ld_hu(Register rd, const MemOperand& rj) {
  MemOperand source = rj;
  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    ldx_hu(rd, source.base(), source.index());
  } else {
    ld_hu(rd, source.base(), source.offset());
  }
}

void MacroAssembler::St_h(Register rd, const MemOperand& rj) {
  MemOperand source = rj;
  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    stx_h(rd, source.base(), source.index());
  } else {
    st_h(rd, source.base(), source.offset());
  }
}

void MacroAssembler::Ld_w(Register rd, const MemOperand& rj) {
  MemOperand source = rj;

  if (!(source.hasIndexReg()) && is_int16(source.offset()) &&
      (source.offset() & 0b11) == 0) {
    ldptr_w(rd, source.base(), source.offset());
    return;
  }

  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    ldx_w(rd, source.base(), source.index());
  } else {
    ld_w(rd, source.base(), source.offset());
  }
}

void MacroAssembler::Ld_wu(Register rd, const MemOperand& rj) {
  MemOperand source = rj;
  AdjustBaseAndOffset(&source);

  if (source.hasIndexReg()) {
    ldx_wu(rd, source.base(), source.index());
  } else {
    ld_wu(rd, source.base(), source.offset());
  }
}

void MacroAssembler::St_w(Register rd, const MemOperand& rj) {
  MemOperand source = rj;

  if (!(source.hasIndexReg()) && is_int16(source.offset()) &&
      (source.offset() & 0b11) == 0) {
    stptr_w(rd, source.base(), source.offset());
    return;
  }

  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    stx_w(rd, source.base(), source.index());
  } else {
    st_w(rd, source.base(), source.offset());
  }
}

void MacroAssembler::Ld_d(Register rd, const MemOperand& rj) {
  MemOperand source = rj;

  if (!(source.hasIndexReg()) && is_int16(source.offset()) &&
      (source.offset() & 0b11) == 0) {
    ldptr_d(rd, source.base(), source.offset());
    return;
  }

  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    ldx_d(rd, source.base(), source.index());
  } else {
    ld_d(rd, source.base(), source.offset());
  }
}

void MacroAssembler::St_d(Register rd, const MemOperand& rj) {
  MemOperand source = rj;

  if (!(source.hasIndexReg()) && is_int16(source.offset()) &&
      (source.offset() & 0b11) == 0) {
    stptr_d(rd, source.base(), source.offset());
    return;
  }

  AdjustBaseAndOffset(&source);
  if (source.hasIndexReg()) {
    stx_d(rd, source.base(), source.index());
  } else {
    st_d(rd, source.base(), source.offset());
  }
}

void MacroAssembler::Fld_s(FPURegister fd, const MemOperand& src) {
  MemOperand tmp = src;
  AdjustBaseAndOffset(&tmp);
  if (tmp.hasIndexReg()) {
    fldx_s(fd, tmp.base(), tmp.index());
  } else {
    fld_s(fd, tmp.base(), tmp.offset());
  }
}

void MacroAssembler::Fst_s(FPURegister fs, const MemOperand& src) {
  MemOperand tmp = src;
  AdjustBaseAndOffset(&tmp);
  if (tmp.hasIndexReg()) {
    fstx_s(fs, tmp.base(), tmp.index());
  } else {
    fst_s(fs, tmp.base(), tmp.offset());
  }
}

void MacroAssembler::Fld_d(FPURegister fd, const MemOperand& src) {
  MemOperand tmp = src;
  AdjustBaseAndOffset(&tmp);
  if (tmp.hasIndexReg()) {
    fldx_d(fd, tmp.base(), tmp.index());
  } else {
    fld_d(fd, tmp.base(), tmp.offset());
  }
}

void MacroAssembler::Fst_d(FPURegister fs, const MemOperand& src) {
  MemOperand tmp = src;
  AdjustBaseAndOffset(&tmp);
  if (tmp.hasIndexReg()) {
    fstx_d(fs, tmp.base(), tmp.index());
  } else {
    fst_d(fs, tmp.base(), tmp.offset());
  }
}

void MacroAssembler::Ll_w(Register rd, const MemOperand& rj) {
  DCHECK(!rj.hasIndexReg());
  bool is_one_instruction = is_int14(rj.offset());
  if (is_one_instruction) {
    ll_w(rd, rj.base(), rj.offset());
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, rj.offset());
    add_d(scratch, scratch, rj.base());
    ll_w(rd, scratch, 0);
  }
}

void MacroAssembler::Ll_d(Register rd, const MemOperand& rj) {
  DCHECK(!rj.hasIndexReg());
  bool is_one_instruction = is_int14(rj.offset());
  if (is_one_instruction) {
    ll_d(rd, rj.base(), rj.offset());
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, rj.offset());
    add_d(scratch, scratch, rj.base());
    ll_d(rd, scratch, 0);
  }
}

void MacroAssembler::Sc_w(Register rd, const MemOperand& rj) {
  DCHECK(!rj.hasIndexReg());
  bool is_one_instruction = is_int14(rj.offset());
  if (is_one_instruction) {
    sc_w(rd, rj.base(), rj.offset());
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, rj.offset());
    add_d(scratch, scratch, rj.base());
    sc_w(rd, scratch, 0);
  }
}

void MacroAssembler::Sc_d(Register rd, const MemOperand& rj) {
  DCHECK(!rj.hasIndexReg());
  bool is_one_instruction = is_int14(rj.offset());
  if (is_one_instruction) {
    sc_d(rd, rj.base(), rj.offset());
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, rj.offset());
    add_d(scratch, scratch, rj.base());
    sc_d(rd, scratch, 0);
  }
}

void MacroAssembler::li(Register dst, Handle<HeapObject> value,
                        RelocInfo::Mode rmode, LiFlags mode) {
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
      Add_d(dst, kRootRegister, Operand(reference.offset_from_root_register()));
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
  if (is_int12(static_cast<int32_t>(value)) ||
      is_uint12(static_cast<int32_t>(value)) || !(value & kImm12Mask)) {
    return 1;
  } else {
    return 2;
  }
}

void MacroAssembler::LiLower32BitHelper(Register rd, Operand j) {
  if (is_int12(static_cast<int32_t>(j.immediate()))) {
    addi_d(rd, zero_reg, j.immediate());
  } else if (is_uint12(static_cast<int32_t>(j.immediate()))) {
    ori(rd, zero_reg, j.immediate() & kImm12Mask);
  } else {
    lu12i_w(rd, j.immediate() >> 12 & 0xfffff);
    if (j.immediate() & kImm12Mask) {
      ori(rd, rd, j.immediate() & kImm12Mask);
    }
  }
}

int MacroAssembler::InstrCountForLi64Bit(int64_t value) {
  if (is_int32(value)) {
    return InstrCountForLiLower32Bit(value);
  } else if (is_int52(value)) {
    return InstrCountForLiLower32Bit(value) + 1;
  } else if ((value & 0xffffffffL) == 0) {
    // 32 LSBs (Least Significant Bits) all set to zero.
    uint8_t tzc = base::bits::CountTrailingZeros32(value >> 32);
    uint8_t lzc = base::bits::CountLeadingZeros32(value >> 32);
    if (tzc >= 20) {
      return 1;
    } else if (tzc + lzc > 12) {
      return 2;
    } else {
      return 3;
    }
  } else {
    int64_t imm21 = (value >> 31) & 0x1fffffL;
    if (imm21 != 0x1fffffL && imm21 != 0) {
      return InstrCountForLiLower32Bit(value) + 2;
    } else {
      return InstrCountForLiLower32Bit(value) + 1;
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
  int64_t imm = j.immediate();
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Normal load of an immediate value which does not need Relocation Info.
  if (is_int32(imm)) {
    LiLower32BitHelper(rd, j);
  } else if (is_int52(imm)) {
    LiLower32BitHelper(rd, j);
    lu32i_d(rd, imm >> 32 & 0xfffff);
  } else if ((imm & 0xffffffffL) == 0) {
    // 32 LSBs (Least Significant Bits) all set to zero.
    uint8_t tzc = base::bits::CountTrailingZeros32(imm >> 32);
    uint8_t lzc = base::bits::CountLeadingZeros32(imm >> 32);
    if (tzc >= 20) {
      lu52i_d(rd, zero_reg, imm >> 52 & kImm12Mask);
    } else if (tzc + lzc > 12) {
      int32_t mask = (1 << (32 - tzc)) - 1;
      lu12i_w(rd, imm >> (tzc + 32) & mask);
      slli_d(rd, rd, tzc + 20);
    } else {
      xor_(rd, rd, rd);
      lu32i_d(rd, imm >> 32 & 0xfffff);
      lu52i_d(rd, rd, imm >> 52 & kImm12Mask);
    }
  } else {
    int64_t imm21 = (imm >> 31) & 0x1fffffL;
    LiLower32BitHelper(rd, j);
    if (imm21 != 0x1fffffL && imm21 != 0) lu32i_d(rd, imm >> 32 & 0xfffff);
    lu52i_d(rd, rd, imm >> 52 & kImm12Mask);
  }
}

void MacroAssembler::li(Register rd, Operand j, LiFlags mode) {
  DCHECK(!j.is_reg());
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (!MustUseReg(j.rmode()) && mode == OPTIMIZE_SIZE) {
    li_optimized(rd, j, mode);
  } else if (RelocInfo::IsCompressedEmbeddedObject(j.rmode())) {
    Handle<HeapObject> handle(reinterpret_cast<Address*>(j.immediate()));
    uint32_t immediate = AddEmbeddedObject(handle);
    RecordRelocInfo(j.rmode(), immediate);
    lu12i_w(rd, immediate >> 12 & 0xfffff);
    ori(rd, rd, immediate & kImm12Mask);
  } else if (MustUseReg(j.rmode())) {
    int64_t immediate;
    if (j.IsHeapNumberRequest()) {
      RequestHeapNumber(j.heap_number_request());
      immediate = 0;
    } else if (RelocInfo::IsFullEmbeddedObject(j.rmode())) {
      Handle<HeapObject> handle(reinterpret_cast<Address*>(j.immediate()));
      immediate = AddEmbeddedObject(handle);
    } else {
      immediate = j.immediate();
    }

    RecordRelocInfo(j.rmode(), immediate);
    lu12i_w(rd, immediate >> 12 & 0xfffff);
    ori(rd, rd, immediate & kImm12Mask);
    if (RelocInfo::IsWasmCanonicalSigId(j.rmode())) {
      // wasm_canonical_sig_id is 32-bit value.
      DCHECK(is_int32(immediate));
      return;
    }
    lu32i_d(rd, immediate >> 32 & 0xfffff);
  } else if (mode == ADDRESS_LOAD) {
    // We always need the same number of instructions as we may need to patch
    // this code to load another value which may need all 3 instructions.
    lu12i_w(rd, j.immediate() >> 12 & 0xfffff);
    ori(rd, rd, j.immediate() & kImm12Mask);
    lu32i_d(rd, j.immediate() >> 32 & 0xfffff);
  } else {  // mode == CONSTANT_SIZE - always emit the same instruction
            // sequence.
    lu12i_w(rd, j.immediate() >> 12 & 0xfffff);
    ori(rd, rd, j.immediate() & kImm12Mask);
    lu32i_d(rd, j.immediate() >> 32 & 0xfffff);
    lu52i_d(rd, rd, j.immediate() >> 52 & kImm12Mask);
  }
}

void MacroAssembler::LoadIsolateField(Register dst, IsolateFieldId id) {
  li(dst, ExternalReference::Create(id));
}

void MacroAssembler::MultiPush(RegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs.bits() & (1 << i)) != 0) {
      stack_offset -= kSystemPointerSize;
      St_d(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
  addi_d(sp, sp, stack_offset);
}

void MacroAssembler::MultiPush(RegList regs1, RegList regs2) {
  DCHECK((regs1 & regs2).is_empty());
  int16_t stack_offset = 0;

  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs1.bits() & (1 << i)) != 0) {
      stack_offset -= kSystemPointerSize;
      St_d(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs2.bits() & (1 << i)) != 0) {
      stack_offset -= kSystemPointerSize;
      St_d(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
  addi_d(sp, sp, stack_offset);
}

void MacroAssembler::MultiPush(RegList regs1, RegList regs2, RegList regs3) {
  DCHECK((regs1 & regs2).is_empty());
  DCHECK((regs1 & regs3).is_empty());
  DCHECK((regs2 & regs3).is_empty());
  int16_t stack_offset = 0;

  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs1.bits() & (1 << i)) != 0) {
      stack_offset -= kSystemPointerSize;
      St_d(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs2.bits() & (1 << i)) != 0) {
      stack_offset -= kSystemPointerSize;
      St_d(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs3.bits() & (1 << i)) != 0) {
      stack_offset -= kSystemPointerSize;
      St_d(ToRegister(i), MemOperand(sp, stack_offset));
    }
  }
  addi_d(sp, sp, stack_offset);
}

void MacroAssembler::MultiPop(RegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs.bits() & (1 << i)) != 0) {
      Ld_d(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kSystemPointerSize;
    }
  }
  addi_d(sp, sp, stack_offset);
}

void MacroAssembler::MultiPop(RegList regs1, RegList regs2) {
  DCHECK((regs1 & regs2).is_empty());
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs2.bits() & (1 << i)) != 0) {
      Ld_d(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kSystemPointerSize;
    }
  }
  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs1.bits() & (1 << i)) != 0) {
      Ld_d(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kSystemPointerSize;
    }
  }
  addi_d(sp, sp, stack_offset);
}

void MacroAssembler::MultiPop(RegList regs1, RegList regs2, RegList regs3) {
  DCHECK((regs1 & regs2).is_empty());
  DCHECK((regs1 & regs3).is_empty());
  DCHECK((regs2 & regs3).is_empty());
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs3.bits() & (1 << i)) != 0) {
      Ld_d(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kSystemPointerSize;
    }
  }
  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs2.bits() & (1 << i)) != 0) {
      Ld_d(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kSystemPointerSize;
    }
  }
  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs1.bits() & (1 << i)) != 0) {
      Ld_d(ToRegister(i), MemOperand(sp, stack_offset));
      stack_offset += kSystemPointerSize;
    }
  }
  addi_d(sp, sp, stack_offset);
}

void MacroAssembler::MultiPushFPU(DoubleRegList regs) {
  int16_t num_to_push = regs.Count();
  int16_t stack_offset = num_to_push * kDoubleSize;

  Sub_d(sp, sp, Operand(stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs.bits() & (1 << i)) != 0) {
      stack_offset -= kDoubleSize;
      Fst_d(FPURegister::from_code(i), MemOperand(sp, stack_offset));
    }
  }
}

void MacroAssembler::MultiPopFPU(DoubleRegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs.bits() & (1 << i)) != 0) {
      Fld_d(FPURegister::from_code(i), MemOperand(sp, stack_offset));
      stack_offset += kDoubleSize;
    }
  }
  addi_d(sp, sp, stack_offset);
}

void MacroAssembler::Bstrpick_w(Register rk, Register rj, uint16_t msbw,
                                uint16_t lsbw) {
  DCHECK_LT(lsbw, msbw);
  DCHECK_LT(lsbw, 32);
  DCHECK_LT(msbw, 32);
  bstrpick_w(rk, rj, msbw, lsbw);
}

void MacroAssembler::Bstrpick_d(Register rk, Register rj, uint16_t msbw,
                                uint16_t lsbw) {
  DCHECK_LT(lsbw, msbw);
  DCHECK_LT(lsbw, 64);
  DCHECK_LT(msbw, 64);
  bstrpick_d(rk, rj, msbw, lsbw);
}

void MacroAssembler::Neg_s(FPURegister fd, FPURegister fj) { fneg_s(fd, fj); }

void MacroAssembler::Neg_d(FPURegister fd, FPURegister fj) { fneg_d(fd, fj); }

void MacroAssembler::Ffint_d_uw(FPURegister fd, FPURegister fj) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  movfr2gr_s(t8, fj);
  Ffint_d_uw(fd, t8);
}

void MacroAssembler::Ffint_d_uw(FPURegister fd, Register rj) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(rj != t7);

  Bstrpick_d(t7, rj, 31, 0);
  movgr2fr_d(fd, t7);
  ffint_d_l(fd, fd);
}

void MacroAssembler::Ffint_d_ul(FPURegister fd, FPURegister fj) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  movfr2gr_d(t8, fj);
  Ffint_d_ul(fd, t8);
}

void MacroAssembler::Ffint_d_ul(FPURegister fd, Register rj) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(rj != t7);

  Label msb_clear, conversion_done;

  Branch(&msb_clear, ge, rj, Operand(zero_reg));

  // Rj >= 2^63
  andi(t7, rj, 1);
  srli_d(rj, rj, 1);
  or_(t7, t7, rj);
  movgr2fr_d(fd, t7);
  ffint_d_l(fd, fd);
  fadd_d(fd, fd, fd);
  Branch(&conversion_done);

  bind(&msb_clear);
  // Rs < 2^63, we can do simple conversion.
  movgr2fr_d(fd, rj);
  ffint_d_l(fd, fd);

  bind(&conversion_done);
}

void MacroAssembler::Ffint_s_uw(FPURegister fd, FPURegister fj) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  movfr2gr_d(t8, fj);
  Ffint_s_uw(fd, t8);
}

void MacroAssembler::Ffint_s_uw(FPURegister fd, Register rj) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(rj != t7);

  bstrpick_d(t7, rj, 31, 0);
  movgr2fr_d(fd, t7);
  ffint_s_l(fd, fd);
}

void MacroAssembler::Ffint_s_ul(FPURegister fd, FPURegister fj) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  movfr2gr_d(t8, fj);
  Ffint_s_ul(fd, t8);
}

void MacroAssembler::Ffint_s_ul(FPURegister fd, Register rj) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(rj != t7);

  Label positive, conversion_done;

  Branch(&positive, ge, rj, Operand(zero_reg));

  // Rs >= 2^31.
  andi(t7, rj, 1);
  srli_d(rj, rj, 1);
  or_(t7, t7, rj);
  movgr2fr_d(fd, t7);
  ffint_s_l(fd, fd);
  fadd_s(fd, fd, fd);
  Branch(&conversion_done);

  bind(&positive);
  // Rs < 2^31, we can do simple conversion.
  movgr2fr_d(fd, rj);
  ffint_s_l(fd, fd);

  bind(&conversion_done);
}

void MacroAssembler::Ftintrne_l_d(FPURegister fd, FPURegister fj) {
  ftintrne_l_d(fd, fj);
}

void MacroAssembler::Ftintrm_l_d(FPURegister fd, FPURegister fj) {
  ftintrm_l_d(fd, fj);
}

void MacroAssembler::Ftintrp_l_d(FPURegister fd, FPURegister fj) {
  ftintrp_l_d(fd, fj);
}

void MacroAssembler::Ftintrz_l_d(FPURegister fd, FPURegister fj) {
  ftintrz_l_d(fd, fj);
}

void MacroAssembler::Ftintrz_l_ud(FPURegister fd, FPURegister fj,
                                  FPURegister scratch) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // Load to GPR.
  movfr2gr_d(t8, fj);
  // Reset sign bit.
  {
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    li(scratch1, 0x7FFFFFFFFFFFFFFFl);
    and_(t8, t8, scratch1);
  }
  movgr2fr_d(scratch, t8);
  Ftintrz_l_d(fd, scratch);
}

void MacroAssembler::Ftintrz_uw_d(FPURegister fd, FPURegister fj,
                                  FPURegister scratch) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Ftintrz_uw_d(t8, fj, scratch);
  movgr2fr_w(fd, t8);
}

void MacroAssembler::Ftintrz_uw_s(FPURegister fd, FPURegister fj,
                                  FPURegister scratch) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Ftintrz_uw_s(t8, fj, scratch);
  movgr2fr_w(fd, t8);
}

void MacroAssembler::Ftintrz_ul_d(FPURegister fd, FPURegister fj,
                                  FPURegister scratch, Register result) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Ftintrz_ul_d(t8, fj, scratch, result);
  movgr2fr_d(fd, t8);
}

void MacroAssembler::Ftintrz_ul_s(FPURegister fd, FPURegister fj,
                                  FPURegister scratch, Register result) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Ftintrz_ul_s(t8, fj, scratch, result);
  movgr2fr_d(fd, t8);
}

void MacroAssembler::Ftintrz_w_d(FPURegister fd, FPURegister fj) {
  ftintrz_w_d(fd, fj);
}

void MacroAssembler::Ftintrne_w_d(FPURegister fd, FPURegister fj) {
  ftintrne_w_d(fd, fj);
}

void MacroAssembler::Ftintrm_w_d(FPURegister fd, FPURegister fj) {
  ftintrm_w_d(fd, fj);
}

void MacroAssembler::Ftintrp_w_d(FPURegister fd, FPURegister fj) {
  ftintrp_w_d(fd, fj);
}

void MacroAssembler::Ftintrz_uw_d(Register rd, FPURegister fj,
                                  FPURegister scratch) {
  DCHECK(fj != scratch);
  DCHECK(rd != t7);

  {
    // Load 2^32 into scratch as its float representation.
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    li(scratch1, 0x41F0000000000000);
    movgr2fr_d(scratch, scratch1);
  }
  // Test if scratch > fd.
  // If fd < 2^32 we can convert it normally.
  Label simple_convert;
  CompareF64(fj, scratch, CULT);
  BranchTrueShortF(&simple_convert);

  // If fd > 2^32, the result should be UINT_32_MAX;
  Add_w(rd, zero_reg, -1);

  Label done;
  Branch(&done);
  // Simple conversion.
  bind(&simple_convert);
  // Double -> Int64 -> Uint32;
  ftintrz_l_d(scratch, fj);
  movfr2gr_s(rd, scratch);

  bind(&done);
}

void MacroAssembler::Ftintrz_uw_s(Register rd, FPURegister fj,
                                  FPURegister scratch) {
  DCHECK(fj != scratch);
  DCHECK(rd != t7);
  {
    // Load 2^32 into scratch as its float representation.
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    li(scratch1, 0x4F800000);
    movgr2fr_w(scratch, scratch1);
  }
  // Test if scratch > fs.
  // If fs < 2^32 we can convert it normally.
  Label simple_convert;
  CompareF32(fj, scratch, CULT);
  BranchTrueShortF(&simple_convert);

  // If fd > 2^32, the result should be UINT_32_MAX;
  Add_w(rd, zero_reg, -1);

  Label done;
  Branch(&done);
  // Simple conversion.
  bind(&simple_convert);
  // Float -> Int64 -> Uint32;
  ftintrz_l_s(scratch, fj);
  movfr2gr_s(rd, scratch);

  bind(&done);
}

void MacroAssembler::Ftintrz_ul_d(Register rd, FPURegister fj,
                                  FPURegister scratch, Register result) {
  DCHECK(fj != scratch);
  DCHECK(result.is_valid() ? !AreAliased(rd, result, t7) : !AreAliased(rd, t7));

  Label simple_convert, done, fail;
  if (result.is_valid()) {
    mov(result, zero_reg);
    Move(scratch, -1.0);
    // If fd =< -1 or unordered, then the conversion fails.
    CompareF64(fj, scratch, CULE);
    BranchTrueShortF(&fail);
  }

  // Load 2^63 into scratch as its double representation.
  li(t7, 0x43E0000000000000);
  movgr2fr_d(scratch, t7);

  // Test if scratch > fs.
  // If fs < 2^63 or unordered we can convert it normally.
  CompareF64(fj, scratch, CULT);
  BranchTrueShortF(&simple_convert);

  // First we subtract 2^63 from fs, then trunc it to rd
  // and add 2^63 to rd.
  fsub_d(scratch, fj, scratch);
  ftintrz_l_d(scratch, scratch);
  movfr2gr_d(rd, scratch);
  Or(rd, rd, Operand(1UL << 63));
  Branch(&done);

  // Simple conversion.
  bind(&simple_convert);
  ftintrz_l_d(scratch, fj);
  movfr2gr_d(rd, scratch);

  bind(&done);
  if (result.is_valid()) {
    // Conversion is failed if the result is negative.
    {
      UseScratchRegisterScope temps(this);
      Register scratch1 = temps.Acquire();
      addi_d(scratch1, zero_reg, -1);
      srli_d(scratch1, scratch1, 1);  // Load 2^62.
      movfr2gr_d(result, scratch);
      xor_(result, result, scratch1);
    }
    Slt(result, zero_reg, result);
  }

  bind(&fail);
}

void MacroAssembler::Ftintrz_ul_s(Register rd, FPURegister fj,
                                  FPURegister scratch, Register result) {
  DCHECK(fj != scratch);
  DCHECK(result.is_valid() ? !AreAliased(rd, result, t7) : !AreAliased(rd, t7));

  Label simple_convert, done, fail;
  if (result.is_valid()) {
    mov(result, zero_reg);
    Move(scratch, -1.0f);
    // If fd =< -1 or unordered, then the conversion fails.
    CompareF32(fj, scratch, CULE);
    BranchTrueShortF(&fail);
  }

  {
    // Load 2^63 into scratch as its float representation.
    UseScratchRegisterScope temps(this);
    Register scratch1 = temps.Acquire();
    li(scratch1, 0x5F000000);
    movgr2fr_w(scratch, scratch1);
  }

  // Test if scratch > fs.
  // If fs < 2^63 or unordered, we can convert it normally.
  CompareF32(fj, scratch, CULT);
  BranchTrueShortF(&simple_convert);

  // First we subtract 2^63 from fs, then trunc it to rd
  // and add 2^63 to rd.
  fsub_s(scratch, fj, scratch);
  ftintrz_l_s(scratch, scratch);
  movfr2gr_d(rd, scratch);
  Or(rd, rd, Operand(1UL << 63));
  Branch(&done);

  // Simple conversion.
  bind(&simple_convert);
  ftintrz_l_s(scratch, fj);
  movfr2gr_d(rd, scratch);

  bind(&done);
  if (result.is_valid()) {
    // Conversion is failed if the result is negative or unordered.
    {
      UseScratchRegisterScope temps(this);
      Register scratch1 = temps.Acquire();
      addi_d(scratch1, zero_reg, -1);
      srli_d(scratch1, scratch1, 1);  // Load 2^62.
      movfr2gr_d(result, scratch);
      xor_(result, result, scratch1);
    }
    Slt(result, zero_reg, result);
  }

  bind(&fail);
}

void MacroAssembler::RoundDouble(FPURegister dst, FPURegister src,
                                 FPURoundingMode mode) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = t8;
  movfcsr2gr(scratch);
  li(t7, Operand(mode));
  movgr2fcsr(t7);
  frint_d(dst, src);
  movgr2fcsr(scratch);
}

void MacroAssembler::Floor_d(FPURegister dst, FPURegister src) {
  RoundDouble(dst, src, mode_floor);
}

void MacroAssembler::Ceil_d(FPURegister dst, FPURegister src) {
  RoundDouble(dst, src, mode_ceil);
}

void MacroAssembler::Trunc_d(FPURegister dst, FPURegister src) {
  RoundDouble(dst, src, mode_trunc);
}

void MacroAssembler::Round_d(FPURegister dst, FPURegister src) {
  RoundDouble(dst, src, mode_round);
}

void MacroAssembler::RoundFloat(FPURegister dst, FPURegister src,
                                FPURoundingMode mode) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = t8;
  movfcsr2gr(scratch);
  li(t7, Operand(mode));
  movgr2fcsr(t7);
  frint_s(dst, src);
  movgr2fcsr(scratch);
}

void MacroAssembler::Floor_s(FPURegister dst, FPURegister src) {
  RoundFloat(dst, src, mode_floor);
}

void MacroAssembler::Ceil_s(FPURegister dst, FPURegister src) {
  RoundFloat(dst, src, mode_ceil);
}

void MacroAssembler::Trunc_s(FPURegister dst, FPURegister src) {
  RoundFloat(dst, src, mode_trunc);
}

void MacroAssembler::Round_s(FPURegister dst, FPURegister src) {
  RoundFloat(dst, src, mode_round);
}

void MacroAssembler::CompareF(FPURegister cmp1, FPURegister cmp2,
                              FPUCondition cc, CFRegister cd, bool f32) {
  if (f32) {
    fcmp_cond_s(cc, cmp1, cmp2, cd);
  } else {
    fcmp_cond_d(cc, cmp1, cmp2, cd);
  }
}

void MacroAssembler::CompareIsNanF(FPURegister cmp1, FPURegister cmp2,
                                   CFRegister cd, bool f32) {
  CompareF(cmp1, cmp2, CUN, cd, f32);
}

void MacroAssembler::BranchTrueShortF(Label* target, CFRegister cj) {
  bcnez(cj, target);
}

void MacroAssembler::BranchFalseShortF(Label* target, CFRegister cj) {
  bceqz(cj, target);
}

void MacroAssembler::BranchTrueF(Label* target, CFRegister cj) {
  // TODO(yuyin): can be optimzed
  bool long_branch = target->is_bound()
                         ? !is_near(target, OffsetSize::kOffset21)
                         : is_trampoline_emitted();
  if (long_branch) {
    Label skip;
    BranchFalseShortF(&skip, cj);
    Branch(target);
    bind(&skip);
  } else {
    BranchTrueShortF(target, cj);
  }
}

void MacroAssembler::BranchFalseF(Label* target, CFRegister cj) {
  bool long_branch = target->is_bound()
                         ? !is_near(target, OffsetSize::kOffset21)
                         : is_trampoline_emitted();
  if (long_branch) {
    Label skip;
    BranchTrueShortF(&skip, cj);
    Branch(target);
    bind(&skip);
  } else {
    BranchFalseShortF(target, cj);
  }
}

void MacroAssembler::FmoveLow(FPURegister dst, Register src_low) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  DCHECK(src_low != scratch);
  movfrh2gr_s(scratch, dst);
  movgr2fr_w(dst, src_low);
  movgr2frh_w(dst, scratch);
}

void MacroAssembler::Move(FPURegister dst, uint32_t src) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(static_cast<int32_t>(src)));
  movgr2fr_w(dst, scratch);
}

void MacroAssembler::Move(FPURegister dst, uint64_t src) {
  // Handle special values first.
  if (src == base::bit_cast<uint64_t>(0.0) && has_double_zero_reg_set_) {
    fmov_d(dst, kDoubleRegZero);
  } else if (src == base::bit_cast<uint64_t>(-0.0) &&
             has_double_zero_reg_set_) {
    Neg_d(dst, kDoubleRegZero);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch, Operand(static_cast<int64_t>(src)));
    movgr2fr_d(dst, scratch);
    if (dst == kDoubleRegZero) has_double_zero_reg_set_ = true;
  }
}

void MacroAssembler::Movz(Register rd, Register rj, Register rk) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  masknez(scratch, rj, rk);
  maskeqz(rd, rd, rk);
  or_(rd, rd, scratch);
}

void MacroAssembler::Movn(Register rd, Register rj, Register rk) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  maskeqz(scratch, rj, rk);
  masknez(rd, rd, rk);
  or_(rd, rd, scratch);
}

void MacroAssembler::LoadZeroIfConditionNotZero(Register dest,
                                                Register condition) {
  masknez(dest, dest, condition);
}

void MacroAssembler::LoadZeroIfConditionZero(Register dest,
                                             Register condition) {
  maskeqz(dest, dest, condition);
}

void MacroAssembler::LoadZeroIfFPUCondition(Register dest, CFRegister cc) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  movcf2gr(scratch, cc);
  LoadZeroIfConditionNotZero(dest, scratch);
}

void MacroAssembler::LoadZeroIfNotFPUCondition(Register dest, CFRegister cc) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  movcf2gr(scratch, cc);
  LoadZeroIfConditionZero(dest, scratch);
}

void MacroAssembler::Clz_w(Register rd, Register rj) { clz_w(rd, rj); }

void MacroAssembler::Clz_d(Register rd, Register rj) { clz_d(rd, rj); }

void MacroAssembler::Ctz_w(Register rd, Register rj) { ctz_w(rd, rj); }

void MacroAssembler::Ctz_d(Register rd, Register rj) { ctz_d(rd, rj); }

// TODO(LOONG_dev): Optimize like arm64, use simd instruction
void MacroAssembler::Popcnt_w(Register rd, Register rj) {
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
  // There are algorithms which are faster in the cases where very few
  // bits are set but the algorithm here attempts to minimize the total
  // number of instructions executed even when a large number of bits
  // are set.
  int32_t B0 = 0x55555555;     // (T)~(T)0/3
  int32_t B1 = 0x33333333;     // (T)~(T)0/15*3
  int32_t B2 = 0x0F0F0F0F;     // (T)~(T)0/255*15
  int32_t value = 0x01010101;  // (T)~(T)0/255
  uint32_t shift = 24;         // (sizeof(T) - 1) * BITS_PER_BYTE

  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.Acquire();
  Register scratch2 = t8;
  srli_w(scratch, rj, 1);
  li(scratch2, B0);
  And(scratch, scratch, scratch2);
  Sub_w(scratch, rj, scratch);
  li(scratch2, B1);
  And(rd, scratch, scratch2);
  srli_w(scratch, scratch, 2);
  And(scratch, scratch, scratch2);
  Add_w(scratch, rd, scratch);
  srli_w(rd, scratch, 4);
  Add_w(rd, rd, scratch);
  li(scratch2, B2);
  And(rd, rd, scratch2);
  li(scratch, value);
  Mul_w(rd, rd, scratch);
  srli_w(rd, rd, shift);
}

void MacroAssembler::Popcnt_d(Register rd, Register rj) {
  ASM_CODE_COMMENT(this);
  int64_t B0 = 0x5555555555555555l;     // (T)~(T)0/3
  int64_t B1 = 0x3333333333333333l;     // (T)~(T)0/15*3
  int64_t B2 = 0x0F0F0F0F0F0F0F0Fl;     // (T)~(T)0/255*15
  int64_t value = 0x0101010101010101l;  // (T)~(T)0/255
  uint32_t shift = 56;                  // (sizeof(T) - 1) * BITS_PER_BYTE

  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.Acquire();
  Register scratch2 = t8;
  srli_d(scratch, rj, 1);
  li(scratch2, B0);
  And(scratch, scratch, scratch2);
  Sub_d(scratch, rj, scratch);
  li(scratch2, B1);
  And(rd, scratch, scratch2);
  srli_d(scratch, scratch, 2);
  And(scratch, scratch, scratch2);
  Add_d(scratch, rd, scratch);
  srli_d(rd, scratch, 4);
  Add_d(rd, rd, scratch);
  li(scratch2, B2);
  And(rd, rd, scratch2);
  li(scratch, value);
  Mul_d(rd, rd, scratch);
  srli_d(rd, rd, shift);
}

void MacroAssembler::ExtractBits(Register dest, Register source, Register pos,
                                 int size, bool sign_extend) {
  sra_d(dest, source, pos);
  bstrpick_d(dest, dest, size - 1, 0);
  if (sign_extend) {
    switch (size) {
      case 8:
        ext_w_b(dest, dest);
        break;
      case 16:
        ext_w_h(dest, dest);
        break;
      case 32:
        // sign-extend word
        slli_w(dest, dest, 0);
        break;
      default:
        UNREACHABLE();
    }
  }
}

void MacroAssembler::InsertBits(Register dest, Register source, Register pos,
                                int size) {
  Rotr_d(dest, dest, pos);
  bstrins_d(dest, source, size - 1, 0);
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Sub_d(scratch, zero_reg, pos);
    Rotr_d(dest, dest, scratch);
  }
}

void MacroAssembler::TryInlineTruncateDoubleToI(Register result,
                                                DoubleRegister double_input,
                                                Label* done) {
  DoubleRegister single_scratch = kScratchDoubleReg;
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();

  ftintrz_l_d(single_scratch, double_input);
  movfr2gr_d(scratch2, single_scratch);
  li(scratch, 1L << 63);
  Xor(scratch, scratch, scratch2);
  rotri_d(scratch2, scratch, 1);
  movfr2gr_s(result, single_scratch);
  Branch(done, ne, scratch, Operand(scratch2));
}

void MacroAssembler::TruncateDoubleToI(Isolate* isolate, Zone* zone,
                                       Register result,
                                       DoubleRegister double_input,
                                       StubCallMode stub_mode) {
  Label done;

  TryInlineTruncateDoubleToI(result, double_input, &done);

  // If we fell through then inline version didn't succeed - call stub instead.
  Sub_d(sp, sp,
        Operand(kDoubleSize + kSystemPointerSize));  // Put input on stack.
  St_d(ra, MemOperand(sp, kSystemPointerSize));
  Fst_d(double_input, MemOperand(sp, 0));

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

  Pop(ra, result);
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
        } else if (is_int12(-rhs.immediate())) {
          Add_d(dst, lhs, Operand(-rhs.immediate()));
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

// BRANCH_ARGS_CHECK checks that conditional jump arguments are correct.
#define BRANCH_ARGS_CHECK(cond, rj, rk)                                  \
  DCHECK((cond == cc_always && rj == zero_reg && rk.rm() == zero_reg) || \
         (cond != cc_always && (rj != zero_reg || rk.rm() != zero_reg)))

void MacroAssembler::Branch(Label* L, bool need_link) {
  int offset = GetOffset(L, OffsetSize::kOffset26);
  if (need_link) {
    bl(offset);
  } else {
    b(offset);
  }
}

void MacroAssembler::Branch(Label* L, Condition cond, Register rj,
                            const Operand& rk, bool need_link) {
  if (L->is_bound()) {
    BRANCH_ARGS_CHECK(cond, rj, rk);
    if (!BranchShortOrFallback(L, cond, rj, rk, need_link)) {
      if (cond != cc_always) {
        Label skip;
        Condition neg_cond = NegateCondition(cond);
        BranchShort(&skip, neg_cond, rj, rk, need_link);
        Branch(L, need_link);
        bind(&skip);
      } else {
        Branch(L);
      }
    }
  } else {
    if (is_trampoline_emitted()) {
      if (cond != cc_always) {
        Label skip;
        Condition neg_cond = NegateCondition(cond);
        BranchShort(&skip, neg_cond, rj, rk, need_link);
        Branch(L, need_link);
        bind(&skip);
      } else {
        Branch(L);
      }
    } else {
      BranchShort(L, cond, rj, rk, need_link);
    }
  }
}

void MacroAssembler::Branch(Label* L, Condition cond, Register rj,
                            RootIndex index, bool need_sign_extend) {
  UseScratchRegisterScope temps(this);
  Register right = temps.Acquire();
  if (COMPRESS_POINTERS_BOOL) {
    Register left = rj;
    if (need_sign_extend) {
      left = temps.hasAvailable() ? temps.Acquire() : t8;
      slli_w(left, rj, 0);
    }
    LoadTaggedRoot(right, index);
    Branch(L, cond, left, Operand(right));
  } else {
    LoadRoot(right, index);
    Branch(L, cond, rj, Operand(right));
  }
}

int32_t MacroAssembler::GetOffset(Label* L, OffsetSize bits) {
  return branch_offset_helper(L, bits) >> 2;
}

Register MacroAssembler::GetRkAsRegisterHelper(const Operand& rk,
                                               Register scratch) {
  Register r2 = no_reg;
  if (rk.is_reg()) {
    r2 = rk.rm();
  } else {
    r2 = scratch;
    li(r2, rk);
  }

  return r2;
}

bool MacroAssembler::BranchShortOrFallback(Label* L, Condition cond,
                                           Register rj, const Operand& rk,
                                           bool need_link) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.hasAvailable() ? temps.Acquire() : t8;
  DCHECK_NE(rj, zero_reg);

  // Be careful to always use shifted_branch_offset only just before the
  // branch instruction, as the location will be remember for patching the
  // target.
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    int offset = 0;
    switch (cond) {
      case cc_always:
        if (L->is_bound() && !is_near(L, OffsetSize::kOffset26)) return false;
        offset = GetOffset(L, OffsetSize::kOffset26);
        if (need_link) {
          bl(offset);
        } else {
          b(offset);
        }
        break;
      case eq:
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          // beq is used here to make the code patchable. Otherwise b should
          // be used which has no condition field so is not patchable.
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset16);
          beq(rj, rj, offset);
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset21)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset21);
          beqz(rj, offset);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          // We don't want any other register but scratch clobbered.
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          offset = GetOffset(L, OffsetSize::kOffset16);
          beq(rj, sc, offset);
        }
        break;
      case ne:
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          // bne is used here to make the code patchable. Otherwise we
          // should not generate any instruction.
          offset = GetOffset(L, OffsetSize::kOffset16);
          bne(rj, rj, offset);
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset21)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset21);
          bnez(rj, offset);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          // We don't want any other register but scratch clobbered.
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bne(rj, sc, offset);
        }
        break;

      // Signed comparison.
      case greater:
        // rj > rk
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          // No code needs to be emitted.
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset16);
          blt(zero_reg, rj, offset);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          DCHECK(rj != sc);
          offset = GetOffset(L, OffsetSize::kOffset16);
          blt(sc, rj, offset);
        }
        break;
      case greater_equal:
        // rj >= rk
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset26)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset26);
          b(offset);
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bge(rj, zero_reg, offset);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          DCHECK(rj != sc);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bge(rj, sc, offset);
        }
        break;
      case less:
        // rj < rk
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          // No code needs to be emitted.
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset16);
          blt(rj, zero_reg, offset);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          DCHECK(rj != sc);
          offset = GetOffset(L, OffsetSize::kOffset16);
          blt(rj, sc, offset);
        }
        break;
      case less_equal:
        // rj <= rk
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset26)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset26);
          b(offset);
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bge(zero_reg, rj, offset);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          DCHECK(rj != sc);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bge(sc, rj, offset);
        }
        break;

      // Unsigned comparison.
      case Ugreater:
        // rj > rk
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          // No code needs to be emitted.
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset26)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset26);
          bnez(rj, offset);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          DCHECK(rj != sc);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bltu(sc, rj, offset);
        }
        break;
      case Ugreater_equal:
        // rj >= rk
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset26)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset26);
          b(offset);
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset26)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset26);
          b(offset);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          DCHECK(rj != sc);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bgeu(rj, sc, offset);
        }
        break;
      case Uless:
        // rj < rk
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          // No code needs to be emitted.
        } else if (IsZero(rk)) {
          // No code needs to be emitted.
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          DCHECK(rj != sc);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bltu(rj, sc, offset);
        }
        break;
      case Uless_equal:
        // rj <= rk
        if (rk.is_reg() && rj.code() == rk.rm().code()) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset26)) return false;
          if (need_link) pcaddi(ra, 2);
          offset = GetOffset(L, OffsetSize::kOffset26);
          b(offset);
        } else if (IsZero(rk)) {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset21)) return false;
          if (need_link) pcaddi(ra, 2);
          beqz(rj, L);
        } else {
          if (L->is_bound() && !is_near(L, OffsetSize::kOffset16)) return false;
          if (need_link) pcaddi(ra, 2);
          Register sc = GetRkAsRegisterHelper(rk, scratch);
          DCHECK(rj != sc);
          offset = GetOffset(L, OffsetSize::kOffset16);
          bgeu(sc, rj, offset);
        }
        break;
      default:
        UNREACHABLE();
    }
  }
  return true;
}

void MacroAssembler::BranchShort(Label* L, Condition cond, Register rj,
                                 const Operand& rk, bool need_link) {
  BRANCH_ARGS_CHECK(cond, rj, rk);
  bool result = BranchShortOrFallback(L, cond, rj, rk, need_link);
  DCHECK(result);
  USE(result);
}

void MacroAssembler::CompareTaggedAndBranch(Label* label, Condition cond,
                                            Register r1, const Operand& r2,
                                            bool need_link) {
  if (COMPRESS_POINTERS_BOOL) {
    UseScratchRegisterScope temps(this);
    Register scratch0 = temps.Acquire();
    slli_w(scratch0, r1, 0);
    if (IsZero(r2)) {
      Branch(label, cond, scratch0, Operand(zero_reg), need_link);
    } else {
      Register scratch1 = temps.hasAvailable() ? temps.Acquire() : t8;
      if (r2.is_reg()) {
        slli_w(scratch1, r2.rm(), 0);
      } else {
        li(scratch1, r2);
      }
      Branch(label, cond, scratch0, Operand(scratch1), need_link);
    }
  } else {
    Branch(label, cond, r1, r2, need_link);
  }
}

void MacroAssembler::LoadLabelRelative(Register dest, Label* target) {
  ASM_CODE_COMMENT(this);
  // pcaddi could handle 22-bit pc offset.
  int32_t offset = branch_offset_helper(target, OffsetSize::kOffset20);
  DCHECK(is_int22(offset));
  pcaddi(dest, offset >> 2);
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
  Ld_d(destination, MemOperand(kRootRegister, offset));
}

void MacroAssembler::StoreRootRelative(int32_t offset, Register value) {
  St_d(value, MemOperand(kRootRegister, offset));
}

void MacroAssembler::LoadRootRegisterOffset(Register destination,
                                            intptr_t offset) {
  if (offset == 0) {
    Move(destination, kRootRegister);
  } else {
    Add_d(destination, kRootRegister, Operand(offset));
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
        DCHECK(scratch.is_valid());
        Ld_d(scratch,
             MemOperand(kRootRegister,
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

void MacroAssembler::Jump(Register target, Condition cond, Register rj,
                          const Operand& rk) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (cond == cc_always) {
    jirl(zero_reg, target, 0);
  } else {
    BRANCH_ARGS_CHECK(cond, rj, rk);
    Label skip;
    Branch(&skip, NegateCondition(cond), rj, rk);
    jirl(zero_reg, target, 0);
    bind(&skip);
  }
}

void MacroAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond, Register rj, const Operand& rk) {
  Label skip;
  if (cond != cc_always) {
    Branch(&skip, NegateCondition(cond), rj, rk);
  }
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    li(t7, Operand(target, rmode));
    jirl(zero_reg, t7, 0);
    bind(&skip);
  }
}

void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode, Condition cond,
                          Register rj, const Operand& rk) {
  Jump(static_cast<intptr_t>(target), rmode, cond, rj, rk);
}

void MacroAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, Register rj, const Operand& rk) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label skip;
  if (cond != cc_always) {
    BranchShort(&skip, NegateCondition(cond), rj, rk);
  }

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    TailCallBuiltin(builtin);
    bind(&skip);
    return;
  }

  int32_t target_index = AddCodeTarget(code);
  Jump(static_cast<Address>(target_index), rmode, cc_always, rj, rk);
  bind(&skip);
}

void MacroAssembler::Jump(const ExternalReference& reference) {
  li(t7, reference);
  Jump(t7);
}

// Note: To call gcc-compiled C code on loonarch, you must call through t[0-8].
void MacroAssembler::Call(Register target, Condition cond, Register rj,
                          const Operand& rk) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (cond == cc_always) {
    jirl(ra, target, 0);
  } else {
    BRANCH_ARGS_CHECK(cond, rj, rk);
    Label skip;
    Branch(&skip, NegateCondition(cond), rj, rk);
    jirl(ra, target, 0);
    bind(&skip);
  }
  set_pc_for_safepoint();
}

void MacroAssembler::CompareTaggedRootAndBranch(const Register& obj,
                                                RootIndex index, Condition cc,
                                                Label* target) {
  ASM_CODE_COMMENT(this);
  // AssertSmiOrHeapObjectInMainCompressionCage(obj);
  UseScratchRegisterScope temps(this);
  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index)) {
    CompareTaggedAndBranch(target, cc, obj, Operand(ReadOnlyRootPtr(index)));
    return;
  }
  // Some smi roots contain system pointer size values like stack limits.
  DCHECK(base::IsInRange(index, RootIndex::kFirstStrongOrReadOnlyRoot,
                         RootIndex::kLastStrongOrReadOnlyRoot));
  Register temp = temps.Acquire();
  DCHECK(!AreAliased(obj, temp));
  LoadRoot(temp, index);
  CompareTaggedAndBranch(target, cc, obj, Operand(temp));
}

// Compare the object in a register to a value from the root list.
void MacroAssembler::CompareRootAndBranch(const Register& obj, RootIndex index,
                                          Condition cc, Label* target,
                                          ComparisonMode mode) {
  ASM_CODE_COMMENT(this);
  if (mode == ComparisonMode::kFullPointer ||
      !base::IsInRange(index, RootIndex::kFirstStrongOrReadOnlyRoot,
                       RootIndex::kLastStrongOrReadOnlyRoot)) {
    // Some smi roots contain system pointer size values like stack limits.
    UseScratchRegisterScope temps(this);
    Register temp = temps.Acquire();
    DCHECK(!AreAliased(obj, temp));
    LoadRoot(temp, index);
    Branch(target, cc, obj, Operand(temp));
    return;
  }
  CompareTaggedRootAndBranch(obj, index, cc, target);
}

void MacroAssembler::JumpIfIsInRange(Register value, unsigned lower_limit,
                                     unsigned higher_limit,
                                     Label* on_in_range) {
  ASM_CODE_COMMENT(this);
  if (lower_limit != 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Sub_d(scratch, value, Operand(lower_limit));
    Branch(on_in_range, ls, scratch, Operand(higher_limit - lower_limit));
  } else {
    Branch(on_in_range, ls, value, Operand(higher_limit - lower_limit));
  }
}

void MacroAssembler::Call(Address target, RelocInfo::Mode rmode, Condition cond,
                          Register rj, const Operand& rk) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label skip;
  if (cond != cc_always) {
    BranchShort(&skip, NegateCondition(cond), rj, rk);
  }
  intptr_t offset_diff = target - pc_offset();
  if (RelocInfo::IsNoInfo(rmode) && is_int28(offset_diff)) {
    bl(offset_diff >> 2);
  } else if (RelocInfo::IsNoInfo(rmode) && is_int38(offset_diff)) {
    pcaddu18i(t7, static_cast<int32_t>(offset_diff) >> 18);
    jirl(ra, t7, (offset_diff & 0x3ffff) >> 2);
  } else {
    li(t7, Operand(static_cast<int64_t>(target), rmode), ADDRESS_LOAD);
    Call(t7, cc_always, rj, rk);
  }
  bind(&skip);
}

void MacroAssembler::Call(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, Register rj, const Operand& rk) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    CallBuiltin(builtin);
    return;
  }

  DCHECK(RelocInfo::IsCodeTarget(rmode));
  int32_t target_index = AddCodeTarget(code);
  Call(static_cast<Address>(target_index), rmode, cond, rj, rk);
}

void MacroAssembler::LoadEntryFromBuiltinIndex(Register builtin_index,
                                               Register target) {
  ASM_CODE_COMMENT(this);
  static_assert(kSystemPointerSize == 8);
  static_assert(kSmiTagSize == 1);
  static_assert(kSmiTag == 0);

  // The builtin_index register contains the builtin index as a Smi.
  SmiUntag(target, builtin_index);
  Alsl_d(target, target, kRootRegister, kSystemPointerSizeLog2, t7);
  Ld_d(target, MemOperand(target, IsolateData::builtin_entry_table_offset()));
}

void MacroAssembler::LoadEntryFromBuiltin(Builtin builtin,
                                          Register destination) {
  Ld_d(destination, EntryFromBuiltinAsOperand(builtin));
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
  UseScratchRegisterScope temps(this);
  Register temp = temps.Acquire();
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      li(temp, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Call(temp);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative: {
      RecordRelocInfo(RelocInfo::NEAR_BUILTIN_ENTRY);
      bl(static_cast<int>(builtin));
      set_pc_for_safepoint();
      break;
    }
    case BuiltinCallJumpMode::kIndirect: {
      LoadEntryFromBuiltin(builtin, temp);
      Call(temp);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        int32_t code_target_index = AddCodeTarget(code);
        RecordRelocInfo(RelocInfo::RELATIVE_CODE_TARGET);
        bl(code_target_index);
        set_pc_for_safepoint();
      } else {
        LoadEntryFromBuiltin(builtin, temp);
        Call(temp);
      }
      break;
    }
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
  UseScratchRegisterScope temps(this);
  Register temp = temps.Acquire();

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
    case BuiltinCallJumpMode::kPCRelative: {
      RecordRelocInfo(RelocInfo::NEAR_BUILTIN_ENTRY);
      b(static_cast<int>(builtin));
      set_pc_for_safepoint();
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        int32_t code_target_index = AddCodeTarget(code);
        RecordRelocInfo(RelocInfo::RELATIVE_CODE_TARGET);
        b(code_target_index);
      } else {
        LoadEntryFromBuiltin(builtin, temp);
        Jump(temp);
      }
      break;
    }
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

  Assembler::BlockTrampolinePoolScope block_trampoline_pool(this);
  static constexpr int kNumInstructionsToJump = 2;
  Label find_ra;
  // Adjust the value in ra to point to the correct return location, 2nd
  // instruction past the real call into C code (the jirl)), and push it.
  // This is the return address of the exit frame.
  pcaddi(ra, kNumInstructionsToJump + 1);
  bind(&find_ra);

  // This spot was reserved in EnterExitFrame.
  St_d(ra, MemOperand(sp, 0));
  // Stack is still aligned.

  // TODO(LOONG_dev): can be jirl target? a0 -- a7?
  jirl(zero_reg, target, 0);
  // Make sure the stored 'ra' points to this position.
  DCHECK_EQ(kNumInstructionsToJump, InstructionsGeneratedSince(&find_ra));
}

void MacroAssembler::DropArguments(Register count) {
  Alsl_d(sp, count, sp, kSystemPointerSizeLog2);
}

void MacroAssembler::DropArgumentsAndPushNewReceiver(Register argc,
                                                     Register receiver) {
  DCHECK(!AreAliased(argc, receiver));
  DropArguments(argc);
  Push(receiver);
}

void MacroAssembler::Ret(Condition cond, Register rj, const Operand& rk) {
  Jump(ra, cond, rj, rk);
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

  Add_d(sp, sp, Operand(count * kSystemPointerSize));

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

void MacroAssembler::Call(Label* target) { Branch(target, true); }

void MacroAssembler::Push(Tagged<Smi> smi) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(smi));
  Push(scratch);
}

void MacroAssembler::Push(Handle<HeapObject> handle) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(handle));
  Push(scratch);
}

void MacroAssembler::PushArray(Register array, Register size, Register scratch,
                               Register scratch2, PushArrayOrder order) {
  DCHECK(!AreAliased(array, size, scratch, scratch2));
  Label loop, entry;
  if (order == PushArrayOrder::kReverse) {
    mov(scratch, zero_reg);
    jmp(&entry);
    bind(&loop);
    Alsl_d(scratch2, scratch, array, kSystemPointerSizeLog2, t7);
    Ld_d(scratch2, MemOperand(scratch2, 0));
    Push(scratch2);
    Add_d(scratch, scratch, Operand(1));
    bind(&entry);
    Branch(&loop, less, scratch, Operand(size));
  } else {
    mov(scratch, size);
    jmp(&entry);
    bind(&loop);
    Alsl_d(scratch2, scratch, array, kSystemPointerSizeLog2, t7);
    Ld_d(scratch2, MemOperand(scratch2, 0));
    Push(scratch2);
    bind(&entry);
    Add_d(scratch, scratch, Operand(-1));
    Branch(&loop, greater_equal, scratch, Operand(zero_reg));
  }
}

// ---------------------------------------------------------------------------
// Exception handling.

void MacroAssembler::PushStackHandler() {
  // Adjust this code if not the case.
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize);
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize);

  Push(Smi::zero());  // Padding.

  // Link the current handler as the next handler.
  li(t2,
     ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  Ld_d(t1, MemOperand(t2, 0));
  Push(t1);

  // Set this new handler as the current one.
  St_d(sp, MemOperand(t2, 0));
}

void MacroAssembler::PopStackHandler() {
  static_assert(StackHandlerConstants::kNextOffset == 0);
  Pop(a1);
  Add_d(sp, sp,
        Operand(static_cast<int64_t>(StackHandlerConstants::kSize -
                                     kSystemPointerSize)));
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch,
     ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  St_d(a1, MemOperand(scratch, 0));
}

void MacroAssembler::FPUCanonicalizeNaN(const DoubleRegister dst,
                                        const DoubleRegister src) {
  fsub_d(dst, src, kDoubleRegZero);
}

// -----------------------------------------------------------------------------
// JavaScript invokes.

void MacroAssembler::LoadStackLimit(Register destination, StackLimitKind kind) {
  ASM_CODE_COMMENT(this);
  DCHECK(root_array_available());
  intptr_t offset = kind == StackLimitKind::kRealStackLimit
                        ? IsolateData::real_jslimit_offset()
                        : IsolateData::jslimit_offset();

  Ld_d(destination, MemOperand(kRootRegister, static_cast<int32_t>(offset)));
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
  sub_d(scratch1, sp, scratch1);
  // Check if the arguments will overflow the stack.
  slli_d(scratch2, num_args, kSystemPointerSizeLog2);
  // Signed comparison.
  Branch(stack_overflow, le, scratch1, Operand(scratch2));
}

void MacroAssembler::TestCodeIsMarkedForDeoptimizationAndJump(
    Register code_data_container, Register scratch, Condition cond,
    Label* target) {
  Ld_wu(scratch, FieldMemOperand(code_data_container, Code::kFlagsOffset));
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
  sub_d(expected_parameter_count, expected_parameter_count,
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
    slli_d(t0, expected_parameter_count, kSystemPointerSizeLog2);
    Sub_d(sp, sp, Operand(t0));
    // Update stack pointer.
    mov(dest, sp);
    mov(t0, actual_parameter_count);
    bind(&copy);
    Ld_d(t1, MemOperand(src, 0));
    St_d(t1, MemOperand(dest, 0));
    Sub_d(t0, t0, Operand(1));
    Add_d(src, src, Operand(kSystemPointerSize));
    Add_d(dest, dest, Operand(kSystemPointerSize));
    Branch(&copy, gt, t0, Operand(zero_reg));
  }

  // Fill remaining expected arguments with undefined values.
  LoadRoot(t0, RootIndex::kUndefinedValue);
  {
    Label loop;
    bind(&loop);
    St_d(t0, MemOperand(a7, 0));
    Sub_d(expected_parameter_count, expected_parameter_count, Operand(1));
    Add_d(a7, a7, Operand(kSystemPointerSize));
    Branch(&loop, gt, expected_parameter_count, Operand(zero_reg));
  }
  b(&regular_invoke);

  bind(&stack_overflow);
  {
    FrameScope frame(
        this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);
    CallRuntime(Runtime::kThrowStackOverflow);
    break_(0xCC);
  }

  bind(&regular_invoke);
}

void MacroAssembler::CallDebugOnFunctionCall(Register fun, Register new_target,
                                             Register expected_parameter_count,
                                             Register actual_parameter_count) {
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
  // TODO(LOONG_dev): MultiPush/Pop
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

void MacroAssembler::InvokeFunctionCode(Register function, Register new_target,
                                        Register expected_parameter_count,
                                        Register actual_parameter_count,
                                        InvokeType type) {
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());
  DCHECK_EQ(function, a1);
  DCHECK_IMPLIES(new_target.is_valid(), new_target == a3);

  // On function call, call into the debugger if necessary.
  Label debug_hook, continue_after_hook;
  {
    li(t0, ExternalReference::debug_hook_on_function_call_address(isolate()));
    Ld_b(t0, MemOperand(t0, 0));
    BranchShort(&debug_hook, ne, t0, Operand(zero_reg));
  }
  bind(&continue_after_hook);

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

  Branch(&done);

  // Deferred debug hook.
  bind(&debug_hook);
  CallDebugOnFunctionCall(function, new_target, expected_parameter_count,
                          actual_parameter_count);
  Branch(&continue_after_hook);

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
  LoadTaggedField(temp_reg,
                  FieldMemOperand(a1, JSFunction::kSharedFunctionInfoOffset));
  LoadTaggedField(cp, FieldMemOperand(a1, JSFunction::kContextOffset));
  // The argument count is stored as uint16_t
  Ld_hu(expected_parameter_count,
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
  LoadTaggedField(cp, FieldMemOperand(a1, JSFunction::kContextOffset));

  InvokeFunctionCode(a1, no_reg, expected_parameter_count,
                     actual_parameter_count, type);
}

// ---------------------------------------------------------------------------
// Support functions.

void MacroAssembler::GetObjectType(Register object, Register map,
                                   Register type_reg) {
  LoadMap(map, object);
  Ld_hu(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
}

void MacroAssembler::GetInstanceTypeRange(Register map, Register type_reg,
                                          InstanceType lower_limit,
                                          Register range) {
  Ld_hu(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  if (lower_limit != 0 || type_reg != range) {
    Sub_d(range, type_reg, Operand(lower_limit));
  }
}

// -----------------------------------------------------------------------------
// Runtime calls.

void MacroAssembler::AddOverflow_d(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  Register right_reg = no_reg;
  if (!right.is_reg()) {
    li(scratch, Operand(right));
    right_reg = scratch;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch2 && right_reg != scratch2 && dst != scratch2 &&
         overflow != scratch2);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    add_d(scratch2, left, right_reg);
    xor_(overflow, scratch2, left);
    xor_(scratch, scratch2, right_reg);
    and_(overflow, overflow, scratch);
    mov(dst, scratch2);
  } else {
    add_d(dst, left, right_reg);
    xor_(overflow, dst, left);
    xor_(scratch, dst, right_reg);
    and_(overflow, overflow, scratch);
  }
}

void MacroAssembler::SubOverflow_d(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  Register right_reg = no_reg;
  if (!right.is_reg()) {
    li(scratch, Operand(right));
    right_reg = scratch;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch2 && right_reg != scratch2 && dst != scratch2 &&
         overflow != scratch2);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    Sub_d(scratch2, left, right_reg);
    xor_(overflow, left, scratch2);
    xor_(scratch, left, right_reg);
    and_(overflow, overflow, scratch);
    mov(dst, scratch2);
  } else {
    sub_d(dst, left, right_reg);
    xor_(overflow, left, dst);
    xor_(scratch, left, right_reg);
    and_(overflow, overflow, scratch);
  }
}

void MacroAssembler::MulOverflow_w(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  Register right_reg = no_reg;
  if (!right.is_reg()) {
    li(scratch, Operand(right));
    right_reg = scratch;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch2 && right_reg != scratch2 && dst != scratch2 &&
         overflow != scratch2);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    Mul_w(scratch2, left, right_reg);
    Mulh_w(overflow, left, right_reg);
    mov(dst, scratch2);
  } else {
    Mul_w(dst, left, right_reg);
    Mulh_w(overflow, left, right_reg);
  }

  srai_d(scratch2, dst, 32);
  xor_(overflow, overflow, scratch2);
}

void MacroAssembler::MulOverflow_d(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  Register right_reg = no_reg;
  if (!right.is_reg()) {
    li(scratch, Operand(right));
    right_reg = scratch;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch2 && right_reg != scratch2 && dst != scratch2 &&
         overflow != scratch2);
  DCHECK(overflow != left && overflow != right_reg);

  if (dst == left || dst == right_reg) {
    Mul_d(scratch2, left, right_reg);
    Mulh_d(overflow, left, right_reg);
    mov(dst, scratch2);
  } else {
    Mul_d(dst, left, right_reg);
    Mulh_d(overflow, left, right_reg);
  }

  srai_d(scratch2, dst, 63);
  xor_(overflow, overflow, scratch2);
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
  CompareTaggedAndBranch(target_if_cleared, eq, in,
                         Operand(kClearedWeakHeapObjectLower32));
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
    Ld_w(scratch1, MemOperand(scratch2, 0));
    Add_w(scratch1, scratch1, Operand(value));
    St_w(scratch1, MemOperand(scratch2, 0));
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
    Ld_w(scratch1, MemOperand(scratch2, 0));
    Sub_w(scratch1, scratch1, Operand(value));
    St_w(scratch1, MemOperand(scratch2, 0));
  }
}

// -----------------------------------------------------------------------------
// Debugging.

void MacroAssembler::Trap() { stop(); }
void MacroAssembler::DebugBreak() { stop(); }

void MacroAssembler::Check(Condition cc, AbortReason reason, Register rj,
                           Operand rk) {
  Label L;
  Branch(&L, cc, rj, rk);
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
      LoadEntryFromBuiltin(Builtin::kAbort, t7);
      Call(t7);
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
  LoadTaggedField(destination, FieldMemOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadCompressedMap(Register dst, Register object) {
  ASM_CODE_COMMENT(this);
  Ld_w(dst, FieldMemOperand(object, HeapObject::kMapOffset));
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
  Ld_hu(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  Branch(&done, eq, scratch, Operand(FEEDBACK_VECTOR_TYPE));

  // Not valid, load undefined.
  LoadRoot(dst, RootIndex::kUndefinedValue);
  Branch(fbv_undef);

  bind(&done);
}

void MacroAssembler::LoadNativeContextSlot(Register dst, int index) {
  LoadMap(dst, cp);
  LoadTaggedField(
      dst, FieldMemOperand(
               dst, Map::kConstructorOrBackPointerOrNativeContextOffset));
  LoadTaggedField(dst, MemOperand(dst, Context::SlotOffset(index)));
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
  addi_d(sp, fp, 2 * kSystemPointerSize);
  Ld_d(ra, MemOperand(fp, 1 * kSystemPointerSize));
  Ld_d(fp, MemOperand(fp, 0 * kSystemPointerSize));
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
  static_assert(2 * kSystemPointerSize ==
                ExitFrameConstants::kCallerSPDisplacement);
  static_assert(1 * kSystemPointerSize == ExitFrameConstants::kCallerPCOffset);
  static_assert(0 * kSystemPointerSize == ExitFrameConstants::kCallerFPOffset);

  // This is how the stack will look:
  // fp + 2 (==kCallerSPDisplacement) - old stack's end
  // [fp + 1 (==kCallerPCOffset)] - saved old ra
  // [fp + 0 (==kCallerFPOffset)] - saved old fp
  // [fp - 1 frame_type Smi
  // [fp - 2 (==kSPOffset)] - sp of the called function
  // fp - (2 + stack_space + alignment) == sp == [fp - kSPOffset] - top of the
  //   new stack (will contain saved ra)

  // Save registers and reserve room for saved entry sp.
  addi_d(sp, sp,
         -2 * kSystemPointerSize - ExitFrameConstants::kFixedFrameSizeFromFp);
  St_d(ra, MemOperand(sp, 3 * kSystemPointerSize));
  St_d(fp, MemOperand(sp, 2 * kSystemPointerSize));
  li(scratch, Operand(StackFrame::TypeToMarker(frame_type)));
  St_d(scratch, MemOperand(sp, 1 * kSystemPointerSize));

  // Set up new frame pointer.
  addi_d(fp, sp, ExitFrameConstants::kFixedFrameSizeFromFp);

  if (v8_flags.debug_code) {
    St_d(zero_reg, MemOperand(fp, ExitFrameConstants::kSPOffset));
  }

  // Save the frame pointer and the context in top.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  St_d(fp, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));

  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  St_d(cp, ExternalReferenceAsOperand(context_address, no_reg));

  const int frame_alignment = MacroAssembler::ActivationFrameAlignment();

  // Reserve place for the return address, stack space and align the frame
  // preparing for calling the runtime function.
  DCHECK_GE(stack_space, 0);
  Sub_d(sp, sp, Operand((stack_space + 1) * kSystemPointerSize));
  if (frame_alignment > 0) {
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    And(sp, sp, Operand(-frame_alignment));  // Align stack.
  }

  // Set the exit frame sp value to point just before the return address
  // location.
  addi_d(scratch, sp, kSystemPointerSize);
  St_d(scratch, MemOperand(fp, ExitFrameConstants::kSPOffset));
}

void MacroAssembler::LeaveExitFrame(Register scratch) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);

  using ER = ExternalReference;

  // Restore current context from top and clear it in debug mode.
  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  Ld_d(cp, ExternalReferenceAsOperand(context_address, no_reg));

  if (v8_flags.debug_code) {
    li(scratch, Operand(Context::kInvalidContext));
    St_d(scratch, ExternalReferenceAsOperand(context_address, no_reg));
  }

  // Clear the top frame.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  St_d(zero_reg, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));

  // Pop the arguments, restore registers, and return.
  mov(sp, fp);  // Respect ABI stack constraint.
  Ld_d(fp, MemOperand(sp, ExitFrameConstants::kCallerFPOffset));
  Ld_d(ra, MemOperand(sp, ExitFrameConstants::kCallerPCOffset));
  addi_d(sp, sp, 2 * kSystemPointerSize);
}

int MacroAssembler::ActivationFrameAlignment() {
#if V8_HOST_ARCH_LOONG64
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one LOONG64
  // platform for another LOONG64 platform with a different alignment.
  return base::OS::ActivationFrameAlignment();
#else   // V8_HOST_ARCH_LOONG64
  // If we are using the simulator then we should always align to the expected
  // alignment. As the simulator is used to generate snapshots we do not know
  // if the target platform will need alignment, so this is controlled from a
  // flag.
  return v8_flags.sim_stack_alignment;
#endif  // V8_HOST_ARCH_LOONG64
}

void MacroAssembler::SmiUntag(Register dst, const MemOperand& src) {
  if (SmiValuesAre32Bits()) {
    Ld_w(dst, MemOperand(src.base(), SmiWordOffset(src.offset())));
  } else {
    DCHECK(SmiValuesAre31Bits());
    if (COMPRESS_POINTERS_BOOL) {
      Ld_w(dst, src);
    } else {
      Ld_d(dst, src);
    }
    SmiUntag(dst);
  }
}

void MacroAssembler::JumpIfSmi(Register value, Label* smi_label) {
  DCHECK_EQ(0, kSmiTag);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  andi(scratch, value, kSmiTagMask);
  Branch(smi_label, eq, scratch, Operand(zero_reg));
}

void MacroAssembler::JumpIfNotSmi(Register value, Label* not_smi_label) {
  DCHECK_EQ(0, kSmiTag);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  andi(scratch, value, kSmiTagMask);
  Branch(not_smi_label, ne, scratch, Operand(zero_reg));
}

void MacroAssembler::JumpIfObjectType(Label* target, Condition cc,
                                      Register object,
                                      InstanceType instance_type,
                                      Register scratch) {
  DCHECK(cc == eq || cc == ne);
  UseScratchRegisterScope temps(this);
  if (scratch == no_reg) {
    scratch = temps.Acquire();
  }
  if (V8_STATIC_ROOTS_BOOL) {
    if (std::optional<RootIndex> expected =
            InstanceTypeChecker::UniqueMapOfInstanceType(instance_type)) {
      Tagged_t ptr = ReadOnlyRootPtr(*expected);
      LoadCompressedMap(scratch, object);
      Branch(target, cc, scratch, Operand(ptr));
      return;
    }
  }
  GetObjectType(object, scratch, scratch);
  Branch(target, cc, scratch, Operand(instance_type));
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
    GetInstanceTypeRange(scratch, scratch, FIRST_JS_RECEIVER_TYPE, scratch);
    Branch(&ok, Condition::kUnsignedLessThanEqual, scratch,
           Operand(LAST_JS_RECEIVER_TYPE - FIRST_JS_RECEIVER_TYPE));

    LoadMap(scratch, heap_object);
    GetInstanceTypeRange(scratch, scratch, FIRST_PRIMITIVE_HEAP_OBJECT_TYPE,
                         scratch);
    Branch(&ok, Condition::kUnsignedLessThanEqual, scratch,
           Operand(LAST_PRIMITIVE_HEAP_OBJECT_TYPE -
                   FIRST_PRIMITIVE_HEAP_OBJECT_TYPE));

    Abort(AbortReason::kInvalidReceiver);
    bind(&ok);
#endif  // DEBUG

    // All primitive object's maps are allocated at the start of the read only
    // heap. Thus JS_RECEIVER's must have maps with larger (compressed)
    // addresses.
    LoadCompressedMap(scratch, heap_object);
    Branch(target, cc, scratch,
           Operand(InstanceTypeChecker::kNonJsReceiverMapLimit));
  } else {
    static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
    GetObjectType(heap_object, scratch, scratch);
    Branch(target, cc, scratch, Operand(FIRST_JS_RECEIVER_TYPE));
  }
}

#ifdef V8_ENABLE_DEBUG_CODE

void MacroAssembler::Assert(Condition cc, AbortReason reason, Register rs,
                            Operand rk) {
  if (v8_flags.debug_code) Check(cc, reason, rs, rk);
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

    if (frame_alignment > kSystemPointerSize) {
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
    Ld_bu(t8, FieldMemOperand(t8, Map::kBitFieldOffset));
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
    Push(object);
    LoadMap(object, object);
    GetInstanceTypeRange(object, object, FIRST_JS_FUNCTION_TYPE, t8);
    Check(ls, AbortReason::kOperandIsNotAFunction, t8,
          Operand(LAST_JS_FUNCTION_TYPE - FIRST_JS_FUNCTION_TYPE));
    Pop(object);
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
    Push(object);
    LoadMap(object, object);
    GetInstanceTypeRange(object, object, FIRST_CALLABLE_JS_FUNCTION_TYPE, t8);
    Check(ls, AbortReason::kOperandIsNotACallableFunction, t8,
          Operand(LAST_CALLABLE_JS_FUNCTION_TYPE -
                  FIRST_CALLABLE_JS_FUNCTION_TYPE));
    Pop(object);
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
  Sub_d(t8, t8, Operand(FIRST_JS_GENERATOR_OBJECT_TYPE));
  Check(
      ls, AbortReason::kOperandIsNotAGeneratorObject, t8,
      Operand(LAST_JS_GENERATOR_OBJECT_TYPE - FIRST_JS_GENERATOR_OBJECT_TYPE));
}

void MacroAssembler::AssertUnreachable(AbortReason reason) {
  if (v8_flags.debug_code) Abort(reason);
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

  fmax_s(dst, src1, src2);
}

void MacroAssembler::Float32MaxOutOfLine(FPURegister dst, FPURegister src1,
                                         FPURegister src2) {
  fadd_s(dst, src1, src2);
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

  fmin_s(dst, src1, src2);
}

void MacroAssembler::Float32MinOutOfLine(FPURegister dst, FPURegister src1,
                                         FPURegister src2) {
  fadd_s(dst, src1, src2);
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

  fmax_d(dst, src1, src2);
}

void MacroAssembler::Float64MaxOutOfLine(FPURegister dst, FPURegister src1,
                                         FPURegister src2) {
  fadd_d(dst, src1, src2);
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

  fmin_d(dst, src1, src2);
}

void MacroAssembler::Float64MinOutOfLine(FPURegister dst, FPURegister src1,
                                         FPURegister src2) {
  fadd_d(dst, src1, src2);
}

int MacroAssembler::CalculateStackPassedWords(int num_reg_arguments,
                                              int num_double_arguments) {
  int stack_passed_words = 0;

  // Up to eight simple arguments are passed in registers a0..a7.
  if (num_reg_arguments > kRegisterPassedArguments) {
    stack_passed_words += num_reg_arguments - kRegisterPassedArguments;
  }
  if (num_double_arguments > kFPRegisterPassedArguments) {
    int num_count = num_double_arguments - kFPRegisterPassedArguments;
    if (num_reg_arguments >= kRegisterPassedArguments) {
      stack_passed_words += num_count;
    } else if (num_count > kRegisterPassedArguments - num_reg_arguments) {
      stack_passed_words +=
          num_count - (kRegisterPassedArguments - num_reg_arguments);
    }
  }
  return stack_passed_words;
}

void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          int num_double_arguments,
                                          Register scratch) {
  ASM_CODE_COMMENT(this);
  int frame_alignment = ActivationFrameAlignment();

  // Up to eight simple arguments in a0..a3, a4..a7, No argument slots.
  // Remaining arguments are pushed on the stack.
  int stack_passed_arguments =
      CalculateStackPassedWords(num_reg_arguments, num_double_arguments);
  if (frame_alignment > kSystemPointerSize) {
    // Make stack end at alignment and make room for num_arguments - 4 words
    // and the original value of sp.
    mov(scratch, sp);
    Sub_d(sp, sp, Operand((stack_passed_arguments + 1) * kSystemPointerSize));
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    bstrins_d(sp, zero_reg, std::log2(frame_alignment) - 1, 0);
    St_d(scratch, MemOperand(sp, stack_passed_arguments * kSystemPointerSize));
  } else {
    Sub_d(sp, sp, Operand(stack_passed_arguments * kSystemPointerSize));
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
  li(t7, function);
  return CallCFunctionHelper(t7, num_reg_arguments, num_double_arguments,
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

#if V8_HOST_ARCH_LOONG64
  if (v8_flags.debug_code) {
    int frame_alignment = base::OS::ActivationFrameAlignment();
    int frame_alignment_mask = frame_alignment - 1;
    if (frame_alignment > kSystemPointerSize) {
      DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
      Label alignment_as_expected;
      {
        Register scratch = t8;
        And(scratch, sp, Operand(frame_alignment_mask));
        Branch(&alignment_as_expected, eq, scratch, Operand(zero_reg));
      }
      // Don't use Check here, as it will call Runtime_Abort possibly
      // re-entering here.
      stop();
      bind(&alignment_as_expected);
    }
  }
#endif  // V8_HOST_ARCH_LOONG64

  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
      if (function != t7) {
        mov(t7, function);
        function = t7;
      }

      // Save the frame pointer and PC so that the stack layout remains
      // iterable, even without an ExitFrame which normally exists between JS
      // and C frames. 't' registers are caller-saved so this is safe as a
      // scratch register.
      Register pc_scratch = t1;
      DCHECK(!AreAliased(pc_scratch, function));
      CHECK(root_array_available());

      LoadLabelRelative(pc_scratch, &get_pc);

      St_d(pc_scratch,
           ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC));
      St_d(fp, ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
    }

    Call(function);
    int call_pc_offset = pc_offset();
    bind(&get_pc);
    if (return_location) bind(return_location);

    if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
      // We don't unset the PC; the FP is the source of truth.
      St_d(zero_reg,
           ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
    }

    int stack_passed_arguments =
        CalculateStackPassedWords(num_reg_arguments, num_double_arguments);

    if (base::OS::ActivationFrameAlignment() > kSystemPointerSize) {
      Ld_d(sp, MemOperand(sp, stack_passed_arguments * kSystemPointerSize));
    } else {
      Add_d(sp, sp, Operand(stack_passed_arguments * kSystemPointerSize));
    }

    set_pc_for_safepoint();

    return call_pc_offset;
  }
}

#undef BRANCH_ARGS_CHECK

void MacroAssembler::CheckPageFlag(Register object, int mask, Condition cc,
                                   Label* condition_met) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  temps.Include(t8);
  Register scratch = temps.Acquire();
  And(scratch, object, Operand(~MemoryChunk::GetAlignmentMaskForAssembler()));
  Ld_d(scratch, MemOperand(scratch, MemoryChunkLayout::kFlagsOffset));
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
  // TODO(LOONG_dev): range check, add Pcadd macro function?
  pcaddi(dst, -pc_offset() >> 2);
}

void MacroAssembler::CallForDeoptimization(Builtin target, int, Label* exit,
                                           DeoptimizeKind kind, Label* ret,
                                           Label*) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Ld_d(t7,
       MemOperand(kRootRegister, IsolateData::BuiltinEntrySlotOffset(target)));
  Call(t7);
  DCHECK_EQ(SizeOfCodeGeneratedSince(exit),
            (kind == DeoptimizeKind::kLazy) ? Deoptimizer::kLazyDeoptExitSize
                                            : Deoptimizer::kEagerDeoptExitSize);
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
  Ld_d(destination,
       FieldMemOperand(code_object, Code::kInstructionStartOffset));
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
  Jump(code_object);
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
  Jump(code);
#else
  LoadTaggedField(code,
                  FieldMemOperand(function_object, JSFunction::kCodeOffset));
  JumpCodeObject(code, kJSEntrypointTag, jump_mode);
#endif
}

namespace {

void TailCallOptimizedCodeSlot(MacroAssembler* masm,
                               Register optimized_code_entry) {
  // ----------- S t a t e -------------
  //  -- a0 : actual argument count
  //  -- a3 : new target (preserved for callee if needed, and caller)
  //  -- a1 : target function (preserved for callee if needed, and caller)
  // -----------------------------------
  DCHECK(!AreAliased(optimized_code_entry, a1, a3));

  Register closure = a1;
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
  __ TestCodeIsMarkedForDeoptimizationAndJump(optimized_code_entry, a6, ne,
                                              &heal_optimized_code_slot);

  // Optimized code is good, get it into the closure and link the closure into
  // the optimized functions list, then tail call the optimized code.
  // The feedback vector is no longer used, so re-use it as a scratch
  // register.
  __ ReplaceClosureCodeWithOptimizedCode(optimized_code_entry, closure);

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
    Register optimized_code, Register closure) {
  ASM_CODE_COMMENT(this);
  StoreCodePointerField(optimized_code,
                        FieldMemOperand(closure, JSFunction::kCodeOffset));
  RecordWriteField(closure, JSFunction::kCodeOffset, optimized_code,
                   kRAHasNotBeenSaved, SaveFPRegsMode::kIgnore, SmiCheck::kOmit,
                   SlotDescriptor::ForCodePointerSlot());
}

void MacroAssembler::GenerateTailCallToReturnedCode(
    Runtime::FunctionId function_id) {
  ASM_CODE_COMMENT(this);
  // ----------- S t a t e -------------
  //  -- a0 : actual argument count
  //  -- a1 : target function (preserved for callee)
  //  -- a3 : new target (preserved for callee)
  // -----------------------------------
  {
    FrameScope scope(this, StackFrame::INTERNAL);
    // Push a copy of the target function, the new target and the actual
    // argument count.
    // Push function as parameter to the runtime call.
    SmiTag(kJavaScriptCallArgCountRegister);
    Push(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
         kJavaScriptCallArgCountRegister, kJavaScriptCallTargetRegister);

    CallRuntime(function_id, 1);
    LoadCodeInstructionStart(a2, a0, kJSEntrypointTag);
    // Restore target function, new target and actual argument count.
    Pop(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
        kJavaScriptCallArgCountRegister);
    SmiUntag(kJavaScriptCallArgCountRegister);
  }

  static_assert(kJavaScriptCallCodeStartRegister == a2, "ABI mismatch");
  Jump(a2);
}

// Read off the flags in the feedback vector and check if there
// is optimized code or a tiering state that needs to be processed.
void MacroAssembler::LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
    Register flags, Register feedback_vector, CodeKind current_code_kind,
    Label* flags_need_processing) {
  ASM_CODE_COMMENT(this);
  Register scratch = t2;
  DCHECK(!AreAliased(t2, flags, feedback_vector));
  DCHECK(CodeKindCanTierUp(current_code_kind));
  Ld_hu(flags, FieldMemOperand(feedback_vector, FeedbackVector::kFlagsOffset));
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
  DCHECK(!AreAliased(flags, feedback_vector));
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
  LoadTaggedField(optimized_code_entry,
                  FieldMemOperand(feedback_vector,
                                  FeedbackVector::kMaybeOptimizedCodeOffset));

  TailCallOptimizedCodeSlot(this, optimized_code_entry);
}

void MacroAssembler::LoadTaggedField(Register destination,
                                     const MemOperand& field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTagged(destination, field_operand);
  } else {
    Ld_d(destination, field_operand);
  }
}

void MacroAssembler::LoadTaggedSignedField(Register destination,
                                           const MemOperand& field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTaggedSigned(destination, field_operand);
  } else {
    Ld_d(destination, field_operand);
  }
}

void MacroAssembler::SmiUntagField(Register dst, const MemOperand& src) {
  SmiUntag(dst, src);
}

void MacroAssembler::StoreTaggedField(Register src, const MemOperand& dst) {
  if (COMPRESS_POINTERS_BOOL) {
    St_w(src, dst);
  } else {
    St_d(src, dst);
  }
}

void MacroAssembler::AtomicStoreTaggedField(Register src,
                                            const MemOperand& dst) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Add_d(scratch, dst.base(), dst.offset());
  if (COMPRESS_POINTERS_BOOL) {
    amswap_db_w(zero_reg, src, scratch);
  } else {
    amswap_db_d(zero_reg, src, scratch);
  }
}

void MacroAssembler::DecompressTaggedSigned(Register dst,
                                            const MemOperand& src) {
  ASM_CODE_COMMENT(this);
  Ld_wu(dst, src);
  if (v8_flags.debug_code) {
    //  Corrupt the top 32 bits. Made up of 16 fixed bits and 16 pc offset bits.
    Add_d(dst, dst, ((kDebugZapValue << 16) | (pc_offset() & 0xffff)) << 32);
  }
}

void MacroAssembler::DecompressTagged(Register dst, const MemOperand& src) {
  ASM_CODE_COMMENT(this);
  Ld_wu(dst, src);
  Add_d(dst, kPtrComprCageBaseRegister, dst);
}

void MacroAssembler::DecompressTagged(Register dst, Register src) {
  ASM_CODE_COMMENT(this);
  Bstrpick_d(dst, src, 31, 0);
  Add_d(dst, kPtrComprCageBaseRegister, Operand(dst));
}

void MacroAssembler::DecompressTagged(Register dst, Tagged_t immediate) {
  ASM_CODE_COMMENT(this);
  Add_d(dst, kPtrComprCageBaseRegister, static_cast<int32_t>(immediate));
}

void MacroAssembler::DecompressProtected(const Register& destination,
                                         const MemOperand& field_operand) {
#if V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Ld_wu(destination, field_operand);
  Ld_d(scratch,
       MemOperand(kRootRegister, IsolateData::trusted_cage_base_offset()));
  Or(destination, destination, scratch);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

void MacroAssembler::AtomicDecompressTaggedSigned(Register dst,
                                                  const MemOperand& src) {
  ASM_CODE_COMMENT(this);
  Ld_wu(dst, src);
  dbar(0);
  if (v8_flags.debug_code) {
    // Corrupt the top 32 bits. Made up of 16 fixed bits and 16 pc offset bits.
    Add_d(dst, dst, ((kDebugZapValue << 16) | (pc_offset() & 0xffff)) << 32);
  }
}

void MacroAssembler::AtomicDecompressTagged(Register dst,
                                            const MemOperand& src) {
  ASM_CODE_COMMENT(this);
  Ld_wu(dst, src);
  dbar(0);
  Add_d(dst, kPtrComprCageBaseRegister, dst);
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
  using ER = ExternalReference;

  Isolate* isolate = masm->isolate();
  MemOperand next_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_next_address(isolate), no_reg);
  MemOperand limit_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_limit_address(isolate), no_reg);
  MemOperand level_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_level_address(isolate), no_reg);

  Register return_value = a0;
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
    __ Ld_d(prev_next_address_reg, next_mem_op);
    __ Ld_d(prev_limit_reg, limit_mem_op);
    __ Ld_w(prev_level_reg, level_mem_op);
    __ Add_w(scratch, prev_level_reg, Operand(1));
    __ St_w(scratch, level_mem_op);
  }

  Label profiler_or_side_effects_check_enabled, done_api_call;
  if (with_profiling) {
    __ RecordComment("Check if profiler or side effects check is enabled");
    __ Ld_b(scratch,
            __ ExternalReferenceAsOperand(IsolateFieldId::kExecutionMode));
    __ Branch(&profiler_or_side_effects_check_enabled, ne, scratch,
              Operand(zero_reg));
#ifdef V8_RUNTIME_CALL_STATS
    __ RecordComment("Check if RCS is enabled");
    __ li(scratch, ER::address_of_runtime_stats_flag());
    __ Ld_w(scratch, MemOperand(scratch, 0));
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

  __ RecordComment("Load the value from ReturnValue");
  __ Ld_d(return_value, return_value_operand);

  {
    ASM_CODE_COMMENT_STRING(
        masm,
        "No more valid handles (the result handle was the last one)."
        "Restore previous handle scope.");
    __ St_d(prev_next_address_reg, next_mem_op);
    if (v8_flags.debug_code) {
      __ Ld_w(scratch, level_mem_op);
      __ Sub_w(scratch, scratch, Operand(1));
      __ Check(eq, AbortReason::kUnexpectedLevelAfterReturnFromApiCall, scratch,
               Operand(prev_level_reg));
    }
    __ St_w(prev_level_reg, level_mem_op);
    __ Ld_d(scratch, limit_mem_op);
    __ Branch(&delete_allocated_handles, ne, prev_limit_reg, Operand(scratch));
  }

  __ RecordComment("Leave the API exit frame.");
  __ bind(&leave_exit_frame);

  Register argc_reg = prev_limit_reg;
  if (argc_operand != nullptr) {
    // Load the number of stack slots to drop before LeaveExitFrame modifies sp.
    __ Ld_d(argc_reg, *argc_operand);
  }

  __ LeaveExitFrame(scratch);

  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Check if the function scheduled an exception.");
    __ LoadRoot(scratch, RootIndex::kTheHoleValue);
    __ Ld_d(scratch2, __ ExternalReferenceAsOperand(
                          ER::exception_address(isolate), no_reg));
    __ Branch(&propagate_exception, ne, scratch, Operand(scratch2));
  }

  __ AssertJSAny(return_value, scratch, scratch2,
                 AbortReason::kAPICallReturnedInvalidObject);

  if (argc_operand == nullptr) {
    DCHECK_NE(slots_to_drop_on_return, 0);
    __ Add_d(sp, sp, Operand(slots_to_drop_on_return * kSystemPointerSize));
  } else {
    // {argc_operand} was loaded into {argc_reg} above.
    if (slots_to_drop_on_return != 0) {
      __ Add_d(sp, sp, Operand(slots_to_drop_on_return * kSystemPointerSize));
    }
    __ Alsl_d(sp, argc_reg, sp, kSystemPointerSizeLog2);
  }

  __ Ret();

  if (with_profiling) {
    ASM_CODE_COMMENT_STRING(masm, "Call the api function via thunk wrapper.");
    __ bind(&profiler_or_side_effects_check_enabled);
    // Additional parameter is the address of the actual callback function.
    if (thunk_arg.is_valid()) {
      MemOperand thunk_arg_mem_op = __ ExternalReferenceAsOperand(
          IsolateFieldId::kApiCallbackThunkArgument);
      __ St_d(thunk_arg, thunk_arg_mem_op);
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
    __ St_d(prev_limit_reg, limit_mem_op);
    // Save the return value in a callee-save register.
    Register saved_result = prev_limit_reg;
    __ mov(saved_result, a0);
    __ PrepareCallCFunction(1, prev_level_reg);
    __ li(kCArgRegs[0], ER::isolate_address());
    __ CallCFunction(ER::delete_handle_scope_extensions(), 1);
    __ mov(kCArgRegs[0], saved_result);
    __ jmp(&leave_exit_frame);
  }
}

}  // namespace internal
}  // namespace v8

#undef __

#endif  // V8_TARGET_ARCH_LOONG64
