// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_IA32

#include <stdint.h>

#include "include/v8-internal.h"
#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/assembler.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/ia32/assembler-ia32.h"
#include "src/codegen/ia32/register-ia32.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/label.h"
#include "src/codegen/macro-assembler-base.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/register.h"
#include "src/codegen/reglist.h"
#include "src/codegen/reloc-info.h"
#include "src/common/globals.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames.h"
#include "src/execution/isolate-data.h"
#include "src/execution/isolate.h"
#include "src/flags/flags.h"
#include "src/handles/handles-inl.h"
#include "src/handles/handles.h"
#include "src/heap/factory-inl.h"
#include "src/heap/factory.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/logging/counters.h"
#include "src/objects/code.h"
#include "src/objects/contexts.h"
#include "src/objects/fixed-array.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-function.h"
#include "src/objects/map.h"
#include "src/objects/objects.h"
#include "src/objects/oddball.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/slots-inl.h"
#include "src/objects/smi.h"
#include "src/roots/roots-inl.h"
#include "src/roots/roots.h"
#include "src/runtime/runtime.h"
#include "src/utils/utils.h"

// Satisfy cpplint check, but don't include platform-specific header. It is
// included recursively via macro-assembler.h.
#if 0
#include "src/codegen/ia32/macro-assembler-ia32.h"
#endif

#define __ ACCESS_MASM(masm)

namespace v8 {
namespace internal {

Operand StackArgumentsAccessor::GetArgumentOperand(int index) const {
  DCHECK_GE(index, 0);
  // arg[0] = esp + kPCOnStackSize;
  // arg[i] = arg[0] + i * kSystemPointerSize;
  return Operand(esp, kPCOnStackSize + index * kSystemPointerSize);
}

// -------------------------------------------------------------------------
// MacroAssembler implementation.

void MacroAssembler::InitializeRootRegister() {
  ASM_CODE_COMMENT(this);
  ExternalReference isolate_root = ExternalReference::isolate_root(isolate());
  Move(kRootRegister, Immediate(isolate_root));
}

Operand MacroAssembler::RootAsOperand(RootIndex index) {
  DCHECK(root_array_available());
  return Operand(kRootRegister, RootRegisterOffsetForRootIndex(index));
}

void MacroAssembler::LoadRoot(Register destination, RootIndex index) {
  ASM_CODE_COMMENT(this);
  if (root_array_available()) {
    mov(destination, RootAsOperand(index));
    return;
  }

  if (RootsTable::IsImmortalImmovable(index)) {
    Handle<Object> object = isolate()->root_handle(index);
    if (IsSmi(*object)) {
      mov(destination, Immediate(Cast<Smi>(*object)));
      return;
    } else {
      DCHECK(IsHeapObject(*object));
      mov(destination, Cast<HeapObject>(object));
      return;
    }
  }

  ExternalReference isolate_root = ExternalReference::isolate_root(isolate());
  lea(destination,
      Operand(isolate_root.address(), RelocInfo::EXTERNAL_REFERENCE));
  mov(destination, Operand(destination, RootRegisterOffsetForRootIndex(index)));
}

void MacroAssembler::CompareRoot(Register with, Register scratch,
                                 RootIndex index) {
  ASM_CODE_COMMENT(this);
  if (root_array_available()) {
    CompareRoot(with, index);
  } else {
    ExternalReference isolate_root = ExternalReference::isolate_root(isolate());
    lea(scratch,
        Operand(isolate_root.address(), RelocInfo::EXTERNAL_REFERENCE));
    cmp(with, Operand(scratch, RootRegisterOffsetForRootIndex(index)));
  }
}

void MacroAssembler::CompareRoot(Register with, RootIndex index) {
  ASM_CODE_COMMENT(this);
  if (root_array_available()) {
    cmp(with, RootAsOperand(index));
    return;
  }

  DCHECK(RootsTable::IsImmortalImmovable(index));
  Handle<Object> object = isolate()->root_handle(index);
  if (IsHeapObject(*object)) {
    cmp(with, Cast<HeapObject>(object));
  } else {
    cmp(with, Immediate(Cast<Smi>(*object)));
  }
}

void MacroAssembler::PushRoot(RootIndex index) {
  ASM_CODE_COMMENT(this);
  if (root_array_available()) {
    DCHECK(RootsTable::IsImmortalImmovable(index));
    push(RootAsOperand(index));
    return;
  }

  // TODO(v8:6666): Add a scratch register or remove all uses.
  DCHECK(RootsTable::IsImmortalImmovable(index));
  Handle<Object> object = isolate()->root_handle(index);
  if (IsHeapObject(*object)) {
    Push(Cast<HeapObject>(object));
  } else {
    Push(Cast<Smi>(*object));
  }
}

void MacroAssembler::CompareRange(Register value, unsigned lower_limit,
                                  unsigned higher_limit, Register scratch) {
  ASM_CODE_COMMENT(this);
  DCHECK_LT(lower_limit, higher_limit);
  if (lower_limit != 0) {
    lea(scratch, Operand(value, 0u - lower_limit));
    cmp(scratch, Immediate(higher_limit - lower_limit));
  } else {
    cmp(value, Immediate(higher_limit));
  }
}

void MacroAssembler::JumpIfIsInRange(Register value, unsigned lower_limit,
                                     unsigned higher_limit, Register scratch,
                                     Label* on_in_range,
                                     Label::Distance near_jump) {
  CompareRange(value, lower_limit, higher_limit, scratch);
  j(below_equal, on_in_range, near_jump);
}

void MacroAssembler::PushArray(Register array, Register size, Register scratch,
                               PushArrayOrder order) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(array, size, scratch));
  Register counter = scratch;
  Label loop, entry;
  if (order == PushArrayOrder::kReverse) {
    mov(counter, 0);
    jmp(&entry);
    bind(&loop);
    Push(Operand(array, counter, times_system_pointer_size, 0));
    inc(counter);
    bind(&entry);
    cmp(counter, size);
    j(less, &loop, Label::kNear);
  } else {
    mov(counter, size);
    jmp(&entry);
    bind(&loop);
    Push(Operand(array, counter, times_system_pointer_size, 0));
    bind(&entry);
    dec(counter);
    j(greater_equal, &loop, Label::kNear);
  }
}

Operand MacroAssembler::ExternalReferenceAsOperand(ExternalReference reference,
                                                   Register scratch) {
  if (root_array_available()) {
    if (reference.IsIsolateFieldId()) {
      return Operand(kRootRegister, reference.offset_from_root_register());
    }
    if (options().enable_root_relative_access) {
      intptr_t delta =
          RootRegisterOffsetForExternalReference(isolate(), reference);
      return Operand(kRootRegister, delta);
    }
    if (options().isolate_independent_code) {
      if (IsAddressableThroughRootRegister(isolate(), reference)) {
        // Some external references can be efficiently loaded as an offset from
        // kRootRegister.
        intptr_t offset =
            RootRegisterOffsetForExternalReference(isolate(), reference);
        return Operand(kRootRegister, offset);
      } else {
        // Otherwise, do a memory load from the external reference table.
        mov(scratch, Operand(kRootRegister,
                             RootRegisterOffsetForExternalReferenceTableEntry(
                                 isolate(), reference)));
        return Operand(scratch, 0);
      }
    }
  }
  Move(scratch, Immediate(reference));
  return Operand(scratch, 0);
}

// TODO(v8:6666): If possible, refactor into a platform-independent function in
// MacroAssembler.
Operand MacroAssembler::HeapObjectAsOperand(Handle<HeapObject> object) {
  DCHECK(root_array_available());

  Builtin builtin;
  RootIndex root_index;
  if (isolate()->roots_table().IsRootHandle(object, &root_index)) {
    return RootAsOperand(root_index);
  } else if (isolate()->builtins()->IsBuiltinHandle(object, &builtin)) {
    return Operand(kRootRegister, RootRegisterOffsetForBuiltin(builtin));
  } else if (object.is_identical_to(code_object_) &&
             Builtins::IsBuiltinId(maybe_builtin_)) {
    return Operand(kRootRegister, RootRegisterOffsetForBuiltin(maybe_builtin_));
  } else {
    // Objects in the constants table need an additional indirection, which
    // cannot be represented as a single Operand.
    UNREACHABLE();
  }
}

void MacroAssembler::LoadFromConstantsTable(Register destination,
                                            int constant_index) {
  ASM_CODE_COMMENT(this);
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kBuiltinsConstantsTable));
  LoadRoot(destination, RootIndex::kBuiltinsConstantsTable);
  mov(destination,
      FieldOperand(destination, FixedArray::OffsetOfElementAt(constant_index)));
}

void MacroAssembler::LoadRootRegisterOffset(Register destination,
                                            intptr_t offset) {
  ASM_CODE_COMMENT(this);
  DCHECK(is_int32(offset));
  DCHECK(root_array_available());
  if (offset == 0) {
    mov(destination, kRootRegister);
  } else {
    lea(destination, Operand(kRootRegister, static_cast<int32_t>(offset)));
  }
}

void MacroAssembler::LoadRootRelative(Register destination, int32_t offset) {
  ASM_CODE_COMMENT(this);
  DCHECK(root_array_available());
  mov(destination, Operand(kRootRegister, offset));
}

void MacroAssembler::StoreRootRelative(int32_t offset, Register value) {
  ASM_CODE_COMMENT(this);
  DCHECK(root_array_available());
  mov(Operand(kRootRegister, offset), value);
}

void MacroAssembler::LoadAddress(Register destination,
                                 ExternalReference source) {
  if (root_array_available()) {
    if (source.IsIsolateFieldId()) {
      lea(destination,
          Operand(kRootRegister, source.offset_from_root_register()));
      return;
    }
    if (options().isolate_independent_code) {
      IndirectLoadExternalReference(destination, source);
      return;
    }
  }
  // External references should not get created with IDs if
  // `!root_array_available()`.
  CHECK(!source.IsIsolateFieldId());
  mov(destination, Immediate(source));
}

int MacroAssembler::RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                                    Register exclusion) const {
  int bytes = 0;
  RegList saved_regs = kCallerSaved - exclusion;
  bytes += kSystemPointerSize * saved_regs.Count();

  if (fp_mode == SaveFPRegsMode::kSave) {
    // Count all XMM registers except XMM0.
    bytes += kStackSavedSavedFPSize * (XMMRegister::kNumRegisters - 1);
  }

  return bytes;
}

int MacroAssembler::PushCallerSaved(SaveFPRegsMode fp_mode,
                                    Register exclusion) {
  ASM_CODE_COMMENT(this);
  // We don't allow a GC in a write barrier slow path so there is no need to
  // store the registers in any particular way, but we do have to store and
  // restore them.
  int bytes = 0;
  RegList saved_regs = kCallerSaved - exclusion;
  for (Register reg : saved_regs) {
    push(reg);
    bytes += kSystemPointerSize;
  }

  if (fp_mode == SaveFPRegsMode::kSave) {
    // Save all XMM registers except XMM0.
    const int delta = kStackSavedSavedFPSize * (XMMRegister::kNumRegisters - 1);
    AllocateStackSpace(delta);
    for (int i = XMMRegister::kNumRegisters - 1; i > 0; i--) {
      XMMRegister reg = XMMRegister::from_code(i);
#if V8_ENABLE_WEBASSEMBLY
      Movdqu(Operand(esp, (i - 1) * kStackSavedSavedFPSize), reg);
#else
      Movsd(Operand(esp, (i - 1) * kStackSavedSavedFPSize), reg);
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    bytes += delta;
  }

  return bytes;
}

int MacroAssembler::PopCallerSaved(SaveFPRegsMode fp_mode, Register exclusion) {
  ASM_CODE_COMMENT(this);
  int bytes = 0;
  if (fp_mode == SaveFPRegsMode::kSave) {
    // Restore all XMM registers except XMM0.
    const int delta = kStackSavedSavedFPSize * (XMMRegister::kNumRegisters - 1);
    for (int i = XMMRegister::kNumRegisters - 1; i > 0; i--) {
      XMMRegister reg = XMMRegister::from_code(i);
#if V8_ENABLE_WEBASSEMBLY
      Movdqu(reg, Operand(esp, (i - 1) * kStackSavedSavedFPSize));
#else
      Movsd(reg, Operand(esp, (i - 1) * kStackSavedSavedFPSize));
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    add(esp, Immediate(delta));
    bytes += delta;
  }

  RegList saved_regs = kCallerSaved - exclusion;
  for (Register reg : base::Reversed(saved_regs)) {
    pop(reg);
    bytes += kSystemPointerSize;
  }

  return bytes;
}

void MacroAssembler::RecordWriteField(Register object, int offset,
                                      Register value, Register slot_address,
                                      SaveFPRegsMode save_fp,
                                      SmiCheck smi_check) {
  ASM_CODE_COMMENT(this);
  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis.
  Label done;

  // Skip barrier if writing a smi.
  if (smi_check == SmiCheck::kInline) {
    JumpIfSmi(value, &done);
  }

  // Although the object register is tagged, the offset is relative to the start
  // of the object, so so offset must be a multiple of kTaggedSize.
  DCHECK(IsAligned(offset, kTaggedSize));

  lea(slot_address, FieldOperand(object, offset));
  if (v8_flags.debug_code) {
    Label ok;
    test_b(slot_address, Immediate(kTaggedSize - 1));
    j(zero, &ok, Label::kNear);
    int3();
    bind(&ok);
  }

  RecordWrite(object, slot_address, value, save_fp, SmiCheck::kOmit);

  bind(&done);

  // Clobber clobbered input registers when running with the debug-code flag
  // turned on to provoke errors.
  if (v8_flags.debug_code) {
    mov(value, Immediate(base::bit_cast<int32_t>(kZapValue)));
    mov(slot_address, Immediate(base::bit_cast<int32_t>(kZapValue)));
  }
}

void MacroAssembler::MaybeSaveRegisters(RegList registers) {
  for (Register reg : registers) {
    push(reg);
  }
}

void MacroAssembler::MaybeRestoreRegisters(RegList registers) {
  for (Register reg : base::Reversed(registers)) {
    pop(reg);
  }
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

  push(object);
  push(slot_address);
  pop(slot_address_parameter);
  pop(object_parameter);

  CallBuiltin(Builtins::EphemeronKeyBarrier(fp_mode));
  MaybeRestoreRegisters(registers);
}

void MacroAssembler::CallRecordWriteStubSaveRegisters(Register object,
                                                      Register slot_address,
                                                      SaveFPRegsMode fp_mode,
                                                      StubCallMode mode) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, slot_address));
  RegList registers =
      WriteBarrierDescriptor::ComputeSavedRegisters(object, slot_address);
  MaybeSaveRegisters(registers);

  Register object_parameter = WriteBarrierDescriptor::ObjectRegister();
  Register slot_address_parameter =
      WriteBarrierDescriptor::SlotAddressRegister();

  push(object);
  push(slot_address);
  pop(slot_address_parameter);
  pop(object_parameter);

  CallRecordWriteStub(object_parameter, slot_address_parameter, fp_mode, mode);

  MaybeRestoreRegisters(registers);
}

void MacroAssembler::CallRecordWriteStub(Register object, Register slot_address,
                                         SaveFPRegsMode fp_mode,
                                         StubCallMode mode) {
  ASM_CODE_COMMENT(this);
  // Use CallRecordWriteStubSaveRegisters if the object and slot registers
  // need to be caller saved.
  DCHECK_EQ(WriteBarrierDescriptor::ObjectRegister(), object);
  DCHECK_EQ(WriteBarrierDescriptor::SlotAddressRegister(), slot_address);
#if V8_ENABLE_WEBASSEMBLY
  if (mode == StubCallMode::kCallWasmRuntimeStub) {
    // Use {wasm_call} for direct Wasm call within a module.
    auto wasm_target =
        static_cast<Address>(wasm::WasmCode::GetRecordWriteBuiltin(fp_mode));
    wasm_call(wasm_target, RelocInfo::WASM_STUB_CALL);
#else
  if (false) {
#endif
  } else {
    CallBuiltin(Builtins::RecordWrite(fp_mode));
  }
}

void MacroAssembler::RecordWrite(Register object, Register slot_address,
                                 Register value, SaveFPRegsMode fp_mode,
                                 SmiCheck smi_check) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, value, slot_address));
  AssertNotSmi(object);

  if (v8_flags.disable_write_barriers) {
    return;
  }

  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT_STRING(this, "Verify slot_address");
    Label ok;
    cmp(value, Operand(slot_address, 0));
    j(equal, &ok, Label::kNear);
    int3();
    bind(&ok);
  }

  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis and stores into young gen.
  Label done;

  if (smi_check == SmiCheck::kInline) {
    // Skip barrier if writing a smi.
    JumpIfSmi(value, &done, Label::kNear);
  }

  CheckPageFlag(value,
                value,  // Used as scratch.
                MemoryChunk::kPointersToHereAreInterestingMask, zero, &done,
                Label::kNear);
  CheckPageFlag(object,
                value,  // Used as scratch.
                MemoryChunk::kPointersFromHereAreInterestingMask, zero, &done,
                Label::kNear);
  RecordComment("CheckPageFlag]");

  CallRecordWriteStub(object, slot_address, fp_mode);

  bind(&done);

  // Clobber clobbered registers when running with the debug-code flag
  // turned on to provoke errors.
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT_STRING(this, "Clobber slot_address and value");
    mov(slot_address, Immediate(base::bit_cast<int32_t>(kZapValue)));
    mov(value, Immediate(base::bit_cast<int32_t>(kZapValue)));
  }
}

void MacroAssembler::Cvtsi2ss(XMMRegister dst, Operand src) {
  xorps(dst, dst);
  cvtsi2ss(dst, src);
}

void MacroAssembler::Cvtsi2sd(XMMRegister dst, Operand src) {
  xorpd(dst, dst);
  cvtsi2sd(dst, src);
}

void MacroAssembler::Cvtui2ss(XMMRegister dst, Operand src, Register tmp) {
  Label done;
  Register src_reg = src.is_reg_only() ? src.reg() : tmp;
  if (src_reg == tmp) mov(tmp, src);
  cvtsi2ss(dst, src_reg);
  test(src_reg, src_reg);
  j(positive, &done, Label::kNear);

  // Compute {src/2 | (src&1)} (retain the LSB to avoid rounding errors).
  if (src_reg != tmp) mov(tmp, src_reg);
  shr(tmp, 1);
  // The LSB is shifted into CF. If it is set, set the LSB in {tmp}.
  Label msb_not_set;
  j(not_carry, &msb_not_set, Label::kNear);
  or_(tmp, Immediate(1));
  bind(&msb_not_set);
  cvtsi2ss(dst, tmp);
  addss(dst, dst);
  bind(&done);
}

void MacroAssembler::Cvttss2ui(Register dst, Operand src, XMMRegister tmp) {
  Label done;
  cvttss2si(dst, src);
  test(dst, dst);
  j(positive, &done);
  Move(tmp, static_cast<float>(INT32_MIN));
  addss(tmp, src);
  cvttss2si(dst, tmp);
  or_(dst, Immediate(0x80000000));
  bind(&done);
}

void MacroAssembler::Cvtui2sd(XMMRegister dst, Operand src, Register scratch) {
  Label done;
  cmp(src, Immediate(0));
  ExternalReference uint32_bias = ExternalReference::address_of_uint32_bias();
  Cvtsi2sd(dst, src);
  j(not_sign, &done, Label::kNear);
  addsd(dst, ExternalReferenceAsOperand(uint32_bias, scratch));
  bind(&done);
}

void MacroAssembler::Cvttsd2ui(Register dst, Operand src, XMMRegister tmp) {
  Move(tmp, -2147483648.0);
  addsd(tmp, src);
  cvttsd2si(dst, tmp);
  add(dst, Immediate(0x80000000));
}

void MacroAssembler::ShlPair(Register high, Register low, uint8_t shift) {
  DCHECK_GE(63, shift);
  if (shift >= 32) {
    mov(high, low);
    if (shift != 32) shl(high, shift - 32);
    xor_(low, low);
  } else {
    shld(high, low, shift);
    shl(low, shift);
  }
}

void MacroAssembler::ShlPair_cl(Register high, Register low) {
  ASM_CODE_COMMENT(this);
  shld_cl(high, low);
  shl_cl(low);
  Label done;
  test(ecx, Immediate(0x20));
  j(equal, &done, Label::kNear);
  mov(high, low);
  xor_(low, low);
  bind(&done);
}

void MacroAssembler::ShrPair(Register high, Register low, uint8_t shift) {
  DCHECK_GE(63, shift);
  if (shift >= 32) {
    mov(low, high);
    if (shift != 32) shr(low, shift - 32);
    xor_(high, high);
  } else {
    shrd(low, high, shift);
    shr(high, shift);
  }
}

void MacroAssembler::ShrPair_cl(Register high, Register low) {
  ASM_CODE_COMMENT(this);
  shrd_cl(low, high);
  shr_cl(high);
  Label done;
  test(ecx, Immediate(0x20));
  j(equal, &done, Label::kNear);
  mov(low, high);
  xor_(high, high);
  bind(&done);
}

void MacroAssembler::SarPair(Register high, Register low, uint8_t shift) {
  ASM_CODE_COMMENT(this);
  DCHECK_GE(63, shift);
  if (shift >= 32) {
    mov(low, high);
    if (shift != 32) sar(low, shift - 32);
    sar(high, 31);
  } else {
    shrd(low, high, shift);
    sar(high, shift);
  }
}

void MacroAssembler::SarPair_cl(Register high, Register low) {
  ASM_CODE_COMMENT(this);
  shrd_cl(low, high);
  sar_cl(high);
  Label done;
  test(ecx, Immediate(0x20));
  j(equal, &done, Label::kNear);
  mov(low, high);
  sar(high, 31);
  bind(&done);
}

void MacroAssembler::LoadMap(Register destination, Register object) {
  mov(destination, FieldOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadFeedbackVector(Register dst, Register closure,
                                        Register scratch, Label* fbv_undef,
                                        Label::Distance distance) {
  Label done;

  // Load the feedback vector from the closure.
  mov(dst, FieldOperand(closure, JSFunction::kFeedbackCellOffset));
  mov(dst, FieldOperand(dst, FeedbackCell::kValueOffset));

  // Check if feedback vector is valid.
  mov(scratch, FieldOperand(dst, HeapObject::kMapOffset));
  CmpInstanceType(scratch, FEEDBACK_VECTOR_TYPE);
  j(equal, &done, Label::kNear);

  // Not valid, load undefined.
  LoadRoot(dst, RootIndex::kUndefinedValue);
  jmp(fbv_undef, distance);

  bind(&done);
}

void MacroAssembler::CmpObjectType(Register heap_object, InstanceType type,
                                   Register map) {
  ASM_CODE_COMMENT(this);
  LoadMap(map, heap_object);
  CmpInstanceType(map, type);
}

void MacroAssembler::CmpInstanceType(Register map, InstanceType type) {
  cmpw(FieldOperand(map, Map::kInstanceTypeOffset), Immediate(type));
}

void MacroAssembler::CmpInstanceTypeRange(Register map,
                                          Register instance_type_out,
                                          Register scratch,
                                          InstanceType lower_limit,
                                          InstanceType higher_limit) {
  ASM_CODE_COMMENT(this);
  DCHECK_LT(lower_limit, higher_limit);
  movzx_w(instance_type_out, FieldOperand(map, Map::kInstanceTypeOffset));
  CompareRange(instance_type_out, lower_limit, higher_limit, scratch);
}

void MacroAssembler::TestCodeIsMarkedForDeoptimization(Register code) {
  test(FieldOperand(code, Code::kFlagsOffset),
       Immediate(1 << Code::kMarkedForDeoptimizationBit));
}

Immediate MacroAssembler::ClearedValue() const {
  return Immediate(static_cast<int32_t>(i::ClearedValue(isolate()).ptr()));
}

namespace {

void TailCallOptimizedCodeSlot(MacroAssembler* masm,
                               Register optimized_code_entry) {
  // ----------- S t a t e -------------
  //  -- eax : actual argument count
  //  -- edx : new target (preserved for callee if needed, and caller)
  //  -- edi : target function (preserved for callee if needed, and caller)
  // -----------------------------------
  ASM_CODE_COMMENT(masm);
  DCHECK(!AreAliased(edx, edi, optimized_code_entry));

  Register closure = edi;
  __ Push(eax);
  __ Push(edx);

  Label heal_optimized_code_slot;

  // If the optimized code is cleared, go to runtime to update the optimization
  // marker field.
  __ LoadWeakValue(optimized_code_entry, &heal_optimized_code_slot);

  // The entry references a CodeWrapper object. Unwrap it now.
  __ mov(optimized_code_entry,
         FieldOperand(optimized_code_entry, CodeWrapper::kCodeOffset));

  // Check if the optimized code is marked for deopt. If it is, bailout to a
  // given label.
  __ TestCodeIsMarkedForDeoptimization(optimized_code_entry);
  __ j(not_zero, &heal_optimized_code_slot);

  // Optimized code is good, get it into the closure and link the closure
  // into the optimized functions list, then tail call the optimized code.
  __ Push(optimized_code_entry);
  __ ReplaceClosureCodeWithOptimizedCode(optimized_code_entry, closure, edx,
                                         ecx);
  static_assert(kJavaScriptCallCodeStartRegister == ecx, "ABI mismatch");
  __ Pop(optimized_code_entry);
  __ LoadCodeInstructionStart(ecx, optimized_code_entry);
  __ Pop(edx);
  __ Pop(eax);
  __ jmp(ecx);

  // Optimized code slot contains deoptimized code or code is cleared and
  // optimized code marker isn't updated. Evict the code, update the marker
  // and re-enter the closure's code.
  __ bind(&heal_optimized_code_slot);
  __ Pop(edx);
  __ Pop(eax);
  __ GenerateTailCallToReturnedCode(Runtime::kHealOptimizedCodeSlot);
}

}  // namespace

#ifdef V8_ENABLE_DEBUG_CODE
void MacroAssembler::AssertFeedbackCell(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    CmpObjectType(object, FEEDBACK_CELL_TYPE, scratch);
    Assert(equal, AbortReason::kExpectedFeedbackCell);
  }
}
void MacroAssembler::AssertFeedbackVector(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    CmpObjectType(object, FEEDBACK_VECTOR_TYPE, scratch);
    Assert(equal, AbortReason::kExpectedFeedbackVector);
  }
}
#endif  // V8_ENABLE_DEBUG_CODE

void MacroAssembler::ReplaceClosureCodeWithOptimizedCode(
    Register optimized_code, Register closure, Register value,
    Register slot_address) {
  ASM_CODE_COMMENT(this);
  // Store the optimized code in the closure.
  mov(FieldOperand(closure, JSFunction::kCodeOffset), optimized_code);
  mov(value, optimized_code);  // Write barrier clobbers slot_address below.
  RecordWriteField(closure, JSFunction::kCodeOffset, value, slot_address,
                   SaveFPRegsMode::kIgnore, SmiCheck::kOmit);
}

void MacroAssembler::GenerateTailCallToReturnedCode(
    Runtime::FunctionId function_id) {
  // ----------- S t a t e -------------
  //  -- eax : actual argument count
  //  -- edx : new target (preserved for callee)
  //  -- edi : target function (preserved for callee)
  // -----------------------------------
  ASM_CODE_COMMENT(this);
  {
    FrameScope scope(this, StackFrame::INTERNAL);
    // Push a copy of the target function, the new target and the actual
    // argument count.
    push(kJavaScriptCallTargetRegister);
    push(kJavaScriptCallNewTargetRegister);
    SmiTag(kJavaScriptCallArgCountRegister);
    push(kJavaScriptCallArgCountRegister);
    // Function is also the parameter to the runtime call.
    push(kJavaScriptCallTargetRegister);

    CallRuntime(function_id, 1);
    mov(ecx, eax);

    // Restore target function, new target and actual argument count.
    pop(kJavaScriptCallArgCountRegister);
    SmiUntag(kJavaScriptCallArgCountRegister);
    pop(kJavaScriptCallNewTargetRegister);
    pop(kJavaScriptCallTargetRegister);
  }

  static_assert(kJavaScriptCallCodeStartRegister == ecx, "ABI mismatch");
  JumpCodeObject(ecx);
}

// Read off the flags in the feedback vector and check if there
// is optimized code or a tiering state that needs to be processed.
// Registers flags and feedback_vector must be aliased.
void MacroAssembler::LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
    Register flags, XMMRegister saved_feedback_vector,
    CodeKind current_code_kind, Label* flags_need_processing) {
  ASM_CODE_COMMENT(this);
  DCHECK(CodeKindCanTierUp(current_code_kind));
  Register feedback_vector = flags;

  // Store feedback_vector. We may need it if we need to load the optimize code
  // slot entry.
  movd(saved_feedback_vector, feedback_vector);
  mov_w(flags, FieldOperand(feedback_vector, FeedbackVector::kFlagsOffset));

  // Check if there is optimized code or a tiering state that needes to be
  // processed.
  uint32_t kFlagsMask = FeedbackVector::kFlagsTieringStateIsAnyRequested |
                        FeedbackVector::kFlagsMaybeHasTurbofanCode |
                        FeedbackVector::kFlagsLogNextExecution;
  if (current_code_kind != CodeKind::MAGLEV) {
    kFlagsMask |= FeedbackVector::kFlagsMaybeHasMaglevCode;
  }
  test_w(flags, Immediate(kFlagsMask));
  j(not_zero, flags_need_processing);
}

void MacroAssembler::OptimizeCodeOrTailCallOptimizedCodeSlot(
    Register flags, XMMRegister saved_feedback_vector) {
  ASM_CODE_COMMENT(this);
  Label maybe_has_optimized_code, maybe_needs_logging;
  // Check if optimized code is available.
  test(flags, Immediate(FeedbackVector::kFlagsTieringStateIsAnyRequested));
  j(zero, &maybe_needs_logging);

  GenerateTailCallToReturnedCode(Runtime::kCompileOptimized);

  bind(&maybe_needs_logging);
  test(flags, Immediate(FeedbackVector::LogNextExecutionBit::kMask));
  j(zero, &maybe_has_optimized_code);
  GenerateTailCallToReturnedCode(Runtime::kFunctionLogNextExecution);

  bind(&maybe_has_optimized_code);
  Register optimized_code_entry = flags;
  Register feedback_vector = flags;
  movd(feedback_vector, saved_feedback_vector);  // Restore feedback vector.
  mov(optimized_code_entry,
      FieldOperand(feedback_vector, FeedbackVector::kMaybeOptimizedCodeOffset));
  TailCallOptimizedCodeSlot(this, optimized_code_entry);
}

#ifdef V8_ENABLE_DEBUG_CODE
void MacroAssembler::AssertSmi(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    test(object, Immediate(kSmiTagMask));
    Check(equal, AbortReason::kOperandIsNotASmi);
  }
}

void MacroAssembler::AssertSmi(Operand object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  test(object, Immediate(kSmiTagMask));
  Check(equal, AbortReason::kOperandIsNotASmi);
}

void MacroAssembler::AssertConstructor(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    test(object, Immediate(kSmiTagMask));
    Check(not_equal, AbortReason::kOperandIsASmiAndNotAConstructor);
    Push(object);
    LoadMap(object, object);
    test_b(FieldOperand(object, Map::kBitFieldOffset),
           Immediate(Map::Bits1::IsConstructorBit::kMask));
    Pop(object);
    Check(not_zero, AbortReason::kOperandIsNotAConstructor);
  }
}

void MacroAssembler::AssertFunction(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    test(object, Immediate(kSmiTagMask));
    Check(not_equal, AbortReason::kOperandIsASmiAndNotAFunction);
    Push(object);
    LoadMap(object, object);
    CmpInstanceTypeRange(object, scratch, scratch, FIRST_JS_FUNCTION_TYPE,
                         LAST_JS_FUNCTION_TYPE);
    Pop(object);
    Check(below_equal, AbortReason::kOperandIsNotAFunction);
  }
}

void MacroAssembler::AssertCallableFunction(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    test(object, Immediate(kSmiTagMask));
    Check(not_equal, AbortReason::kOperandIsASmiAndNotAFunction);
    Push(object);
    LoadMap(object, object);
    CmpInstanceTypeRange(object, scratch, scratch,
                         FIRST_CALLABLE_JS_FUNCTION_TYPE,
                         LAST_CALLABLE_JS_FUNCTION_TYPE);
    Pop(object);
    Check(below_equal, AbortReason::kOperandIsNotACallableFunction);
  }
}

void MacroAssembler::AssertBoundFunction(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    test(object, Immediate(kSmiTagMask));
    Check(not_equal, AbortReason::kOperandIsASmiAndNotABoundFunction);
    Push(object);
    CmpObjectType(object, JS_BOUND_FUNCTION_TYPE, object);
    Pop(object);
    Check(equal, AbortReason::kOperandIsNotABoundFunction);
  }
}

void MacroAssembler::AssertGeneratorObject(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);

  test(object, Immediate(kSmiTagMask));
  Check(not_equal, AbortReason::kOperandIsASmiAndNotAGeneratorObject);

  {
    Push(object);
    Register map = object;

    LoadMap(map, object);

    // Check if JSGeneratorObject
    CmpInstanceTypeRange(map, map, map, FIRST_JS_GENERATOR_OBJECT_TYPE,
                         LAST_JS_GENERATOR_OBJECT_TYPE);
    Pop(object);
  }

  Check(below_equal, AbortReason::kOperandIsNotAGeneratorObject);
}

void MacroAssembler::AssertUndefinedOrAllocationSite(Register object,
                                                     Register scratch) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    Label done_checking;
    AssertNotSmi(object);
    CompareRoot(object, scratch, RootIndex::kUndefinedValue);
    j(equal, &done_checking);
    LoadRoot(scratch, RootIndex::kAllocationSiteWithWeakNextMap);
    cmp(FieldOperand(object, 0), scratch);
    Assert(equal, AbortReason::kExpectedUndefinedOrCell);
    bind(&done_checking);
  }
}

void MacroAssembler::AssertNotSmi(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    test(object, Immediate(kSmiTagMask));
    Check(not_equal, AbortReason::kOperandIsASmi);
  }
}

void MacroAssembler::AssertJSAny(Register object, Register map_tmp,
                                 AbortReason abort_reason) {
  if (!v8_flags.debug_code) return;

  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, map_tmp));
  Label ok;

  JumpIfSmi(object, &ok, Label::kNear);

  mov(map_tmp, FieldOperand(object, HeapObject::kMapOffset));

  CmpInstanceType(map_tmp, LAST_NAME_TYPE);
  j(below_equal, &ok, Label::kNear);

  CmpInstanceType(map_tmp, FIRST_JS_RECEIVER_TYPE);
  j(above_equal, &ok, Label::kNear);

  CompareRoot(map_tmp, RootIndex::kHeapNumberMap);
  j(equal, &ok, Label::kNear);

  CompareRoot(map_tmp, RootIndex::kBigIntMap);
  j(equal, &ok, Label::kNear);

  CompareRoot(object, RootIndex::kUndefinedValue);
  j(equal, &ok, Label::kNear);

  CompareRoot(object, RootIndex::kTrueValue);
  j(equal, &ok, Label::kNear);

  CompareRoot(object, RootIndex::kFalseValue);
  j(equal, &ok, Label::kNear);

  CompareRoot(object, RootIndex::kNullValue);
  j(equal, &ok, Label::kNear);

  Abort(abort_reason);

  bind(&ok);
}

void MacroAssembler::Assert(Condition cc, AbortReason reason) {
  if (v8_flags.debug_code) Check(cc, reason);
}

void MacroAssembler::AssertUnreachable(AbortReason reason) {
  if (v8_flags.debug_code) Abort(reason);
}
#endif  // V8_ENABLE_DEBUG_CODE

void MacroAssembler::StubPrologue(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  push(ebp);  // Caller's frame pointer.
  mov(ebp, esp);
  push(Immediate(StackFrame::TypeToMarker(type)));
}

void MacroAssembler::Prologue() {
  ASM_CODE_COMMENT(this);
  push(ebp);  // Caller's frame pointer.
  mov(ebp, esp);
  push(kContextRegister);                 // Callee's context.
  push(kJSFunctionRegister);              // Callee's JS function.
  push(kJavaScriptCallArgCountRegister);  // Actual argument count.
}

void MacroAssembler::DropArguments(Register count) {
  lea(esp, Operand(esp, count, times_system_pointer_size, 0));
}

void MacroAssembler::DropArguments(Register count, Register scratch) {
  DCHECK(!AreAliased(count, scratch));
  PopReturnAddressTo(scratch);
  DropArguments(count);
  PushReturnAddressFrom(scratch);
}

void MacroAssembler::DropArgumentsAndPushNewReceiver(Register argc,
                                                     Register receiver,
                                                     Register scratch) {
  DCHECK(!AreAliased(argc, receiver, scratch));
  PopReturnAddressTo(scratch);
  DropArguments(argc);
  Push(receiver);
  PushReturnAddressFrom(scratch);
}

void MacroAssembler::DropArgumentsAndPushNewReceiver(Register argc,
                                                     Operand receiver,
                                                     Register scratch) {
  DCHECK(!AreAliased(argc, scratch));
  DCHECK(!receiver.is_reg(scratch));
  PopReturnAddressTo(scratch);
  DropArguments(argc);
  Push(receiver);
  PushReturnAddressFrom(scratch);
}

void MacroAssembler::EnterFrame(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  push(ebp);
  mov(ebp, esp);
  if (!StackFrame::IsJavaScript(type)) {
    Push(Immediate(StackFrame::TypeToMarker(type)));
  }
#if V8_ENABLE_WEBASSEMBLY
  if (type == StackFrame::WASM) Push(kWasmInstanceRegister);
#endif  // V8_ENABLE_WEBASSEMBLY
}

void MacroAssembler::LeaveFrame(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  if (v8_flags.debug_code && !StackFrame::IsJavaScript(type)) {
    cmp(Operand(ebp, CommonFrameConstants::kContextOrFrameTypeOffset),
        Immediate(StackFrame::TypeToMarker(type)));
    Check(equal, AbortReason::kStackFrameTypesMustMatch);
  }
  leave();
}

#ifdef V8_OS_WIN
void MacroAssembler::AllocateStackSpace(Register bytes_scratch) {
  ASM_CODE_COMMENT(this);
  // In windows, we cannot increment the stack size by more than one page
  // (minimum page size is 4KB) without accessing at least one byte on the
  // page. Check this:
  // https://msdn.microsoft.com/en-us/library/aa227153(v=vs.60).aspx.
  Label check_offset;
  Label touch_next_page;
  jmp(&check_offset);
  bind(&touch_next_page);
  sub(esp, Immediate(kStackPageSize));
  // Just to touch the page, before we increment further.
  mov(Operand(esp, 0), Immediate(0));
  sub(bytes_scratch, Immediate(kStackPageSize));

  bind(&check_offset);
  cmp(bytes_scratch, kStackPageSize);
  j(greater_equal, &touch_next_page);

  sub(esp, bytes_scratch);
}

void MacroAssembler::AllocateStackSpace(int bytes) {
  ASM_CODE_COMMENT(this);
  DCHECK_GE(bytes, 0);
  while (bytes >= kStackPageSize) {
    sub(esp, Immediate(kStackPageSize));
    mov(Operand(esp, 0), Immediate(0));
    bytes -= kStackPageSize;
  }
  if (bytes == 0) return;
  sub(esp, Immediate(bytes));
}
#endif

void MacroAssembler::EnterExitFrame(int extra_slots,
                                    StackFrame::Type frame_type,
                                    Register c_function) {
  ASM_CODE_COMMENT(this);
  DCHECK(frame_type == StackFrame::EXIT ||
         frame_type == StackFrame::BUILTIN_EXIT ||
         frame_type == StackFrame::API_ACCESSOR_EXIT ||
         frame_type == StackFrame::API_CALLBACK_EXIT);

  // Set up the frame structure on the stack.
  DCHECK_EQ(+2 * kSystemPointerSize, ExitFrameConstants::kCallerSPDisplacement);
  DCHECK_EQ(+1 * kSystemPointerSize, ExitFrameConstants::kCallerPCOffset);
  DCHECK_EQ(0 * kSystemPointerSize, ExitFrameConstants::kCallerFPOffset);
  push(ebp);
  mov(ebp, esp);

  push(Immediate(StackFrame::TypeToMarker(frame_type)));
  DCHECK_EQ(-2 * kSystemPointerSize, ExitFrameConstants::kSPOffset);
  push(Immediate(0));  // Saved entry sp, patched below.

  // Save the frame pointer and the context in top.
  DCHECK(!AreAliased(ebp, kContextRegister, c_function));
  using ER = ExternalReference;
  ER r0 = ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  mov(ExternalReferenceAsOperand(r0, no_reg), ebp);
  ER r1 = ER::Create(IsolateAddressId::kContextAddress, isolate());
  mov(ExternalReferenceAsOperand(r1, no_reg), kContextRegister);
  static_assert(edx == kRuntimeCallFunctionRegister);
  ER r2 = ER::Create(IsolateAddressId::kCFunctionAddress, isolate());
  mov(ExternalReferenceAsOperand(r2, no_reg), c_function);

  AllocateStackSpace(extra_slots * kSystemPointerSize);

  // Get the required frame alignment for the OS.
  const int kFrameAlignment = base::OS::ActivationFrameAlignment();
  if (kFrameAlignment > 0) {
    DCHECK(base::bits::IsPowerOfTwo(kFrameAlignment));
    and_(esp, -kFrameAlignment);
  }

  // Patch the saved entry sp.
  mov(Operand(ebp, ExitFrameConstants::kSPOffset), esp);
}

void MacroAssembler::LeaveExitFrame(Register scratch) {
  ASM_CODE_COMMENT(this);

  leave();

  // Clear the top frame.
  ExternalReference c_entry_fp_address =
      ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  mov(ExternalReferenceAsOperand(c_entry_fp_address, scratch), Immediate(0));

  // Restore the current context from top and clear it in debug mode.
  ExternalReference context_address =
      ExternalReference::Create(IsolateAddressId::kContextAddress, isolate());
  mov(esi, ExternalReferenceAsOperand(context_address, scratch));

#ifdef DEBUG
  push(eax);
  mov(ExternalReferenceAsOperand(context_address, eax),
      Immediate(Context::kInvalidContext));
  pop(eax);
#endif
}

void MacroAssembler::PushStackHandler(Register scratch) {
  ASM_CODE_COMMENT(this);
  // Adjust this code if not the case.
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize);
  static_assert(StackHandlerConstants::kNextOffset == 0);

  push(Immediate(0));  // Padding.

  // Link the current handler as the next handler.
  ExternalReference handler_address =
      ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate());
  push(ExternalReferenceAsOperand(handler_address, scratch));

  // Set this new handler as the current one.
  mov(ExternalReferenceAsOperand(handler_address, scratch), esp);
}

void MacroAssembler::PopStackHandler(Register scratch) {
  ASM_CODE_COMMENT(this);
  static_assert(StackHandlerConstants::kNextOffset == 0);
  ExternalReference handler_address =
      ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate());
  pop(ExternalReferenceAsOperand(handler_address, scratch));
  add(esp, Immediate(StackHandlerConstants::kSize - kSystemPointerSize));
}

void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments) {
  ASM_CODE_COMMENT(this);
  // If the expected number of arguments of the runtime function is
  // constant, we check that the actual number of arguments match the
  // expectation.
  CHECK(f->nargs < 0 || f->nargs == num_arguments);

  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  Move(kRuntimeCallArgCountRegister, Immediate(num_arguments));
  Move(kRuntimeCallFunctionRegister, Immediate(ExternalReference::Create(f)));
  CallBuiltin(Builtins::RuntimeCEntry(f->result_size));
}

void MacroAssembler::TailCallRuntime(Runtime::FunctionId fid) {
  // ----------- S t a t e -------------
  //  -- esp[0]                 : return address
  //  -- esp[8]                 : argument num_arguments - 1
  //  ...
  //  -- esp[8 * num_arguments] : argument 0 (receiver)
  //
  //  For runtime functions with variable arguments:
  //  -- eax                    : number of  arguments
  // -----------------------------------
  ASM_CODE_COMMENT(this);
  const Runtime::Function* function = Runtime::FunctionForId(fid);
  DCHECK_EQ(1, function->result_size);
  if (function->nargs >= 0) {
    // TODO(1236192): Most runtime routines don't need the number of
    // arguments passed in because it is constant. At some point we
    // should remove this need and make the runtime routine entry code
    // smarter.
    Move(kRuntimeCallArgCountRegister, Immediate(function->nargs));
  }
  JumpToExternalReference(ExternalReference::Create(fid));
}

void MacroAssembler::JumpToExternalReference(const ExternalReference& ext,
                                             bool builtin_exit_frame) {
  ASM_CODE_COMMENT(this);
  // Set the entry point and jump to the C entry runtime stub.
  Move(kRuntimeCallFunctionRegister, Immediate(ext));
  TailCallBuiltin(Builtins::CEntry(1, ArgvMode::kStack, builtin_exit_frame));
}

Operand MacroAssembler::StackLimitAsOperand(StackLimitKind kind) {
  DCHECK(root_array_available());
  intptr_t offset = kind == StackLimitKind::kRealStackLimit
                        ? IsolateData::real_jslimit_offset()
                        : IsolateData::jslimit_offset();

  CHECK(is_int32(offset));
  return Operand(kRootRegister, static_cast<int32_t>(offset));
}

void MacroAssembler::CompareStackLimit(Register with, StackLimitKind kind) {
  ASM_CODE_COMMENT(this);
  cmp(with, StackLimitAsOperand(kind));
}

void MacroAssembler::StackOverflowCheck(Register num_args, Register scratch,
                                        Label* stack_overflow,
                                        bool include_receiver) {
  ASM_CODE_COMMENT(this);
  DCHECK_NE(num_args, scratch);
  // Check the stack for overflow. We are not trying to catch
  // interruptions (e.g. debug break and preemption) here, so the "real stack
  // limit" is checked.
  ExternalReference real_stack_limit =
      ExternalReference::address_of_real_jslimit(isolate());
  // Compute the space that is left as a negative number in scratch. If
  // we already overflowed, this will be a positive number.
  mov(scratch, ExternalReferenceAsOperand(real_stack_limit, scratch));
  sub(scratch, esp);
  // TODO(victorgomes): Remove {include_receiver} and always require one extra
  // word of the stack space.
  lea(scratch, Operand(scratch, num_args, times_system_pointer_size, 0));
  if (include_receiver) {
    add(scratch, Immediate(kSystemPointerSize));
  }
  // See if we overflowed, i.e. scratch is positive.
  cmp(scratch, Immediate(0));
  // TODO(victorgomes):  Save some bytes in the builtins that use stack checks
  // by jumping to a builtin that throws the exception.
  j(greater, stack_overflow);  // Signed comparison.
}

void MacroAssembler::InvokePrologue(Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    Label* done, InvokeType type) {
  if (expected_parameter_count == actual_parameter_count) return;
  ASM_CODE_COMMENT(this);
  DCHECK_EQ(actual_parameter_count, eax);
  DCHECK_EQ(expected_parameter_count, ecx);
  Label regular_invoke;

  // If overapplication or if the actual argument count is equal to the
  // formal parameter count, no need to push extra undefined values.
  sub(expected_parameter_count, actual_parameter_count);
  j(less_equal, &regular_invoke, Label::kFar);

  // We need to preserve edx, edi, esi and ebx.
  movd(xmm0, edx);
  movd(xmm1, edi);
  movd(xmm2, esi);
  movd(xmm3, ebx);

  Label stack_overflow;
  StackOverflowCheck(expected_parameter_count, edx, &stack_overflow);

  Register scratch = esi;

  // Underapplication. Move the arguments already in the stack, including the
  // receiver and the return address.
  {
    Label copy, check;
    Register src = edx, dest = esp, num = edi, current = ebx;
    mov(src, esp);
    lea(scratch,
        Operand(expected_parameter_count, times_system_pointer_size, 0));
    AllocateStackSpace(scratch);
    // Extra words are the receiver (if not already included in argc) and the
    // return address (if a jump).
    int extra_words = type == InvokeType::kCall ? 0 : 1;
    lea(num, Operand(eax, extra_words));  // Number of words to copy.
    Move(current, 0);
    // Fall-through to the loop body because there are non-zero words to copy.
    bind(&copy);
    mov(scratch, Operand(src, current, times_system_pointer_size, 0));
    mov(Operand(dest, current, times_system_pointer_size, 0), scratch);
    inc(current);
    bind(&check);
    cmp(current, num);
    j(less, &copy);
    lea(edx, Operand(esp, num, times_system_pointer_size, 0));
  }

    // Fill remaining expected arguments with undefined values.
    movd(ebx, xmm3);  // Restore root.
    LoadRoot(scratch, RootIndex::kUndefinedValue);
    {
      Label loop;
      bind(&loop);
      dec(expected_parameter_count);
      mov(Operand(edx, expected_parameter_count, times_system_pointer_size, 0),
          scratch);
      j(greater, &loop, Label::kNear);
    }

    // Restore remaining registers.
    movd(esi, xmm2);
    movd(edi, xmm1);
    movd(edx, xmm0);

    jmp(&regular_invoke);

    bind(&stack_overflow);
    {
      FrameScope frame(
          this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);
      CallRuntime(Runtime::kThrowStackOverflow);
      int3();  // This should be unreachable.
    }

    bind(&regular_invoke);
}

void MacroAssembler::CallDebugOnFunctionCall(Register fun, Register new_target,
                                             Register expected_parameter_count,
                                             Register actual_parameter_count) {
  ASM_CODE_COMMENT(this);

  // We have no available register. So we spill the root register (ebx) and
  // recover it later.
  movd(xmm0, kRootRegister);

  // Load receiver to pass it later to DebugOnFunctionCall hook.
  // Receiver is located on top of the stack if we have a frame (usually a
  // construct frame), or after the return address if we do not yet have a
  // frame.
  Register receiver = kRootRegister;
  mov(receiver, Operand(esp, has_frame() ? 0 : kSystemPointerSize));

  FrameScope frame(
      this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);

  SmiTag(expected_parameter_count);
  Push(expected_parameter_count);

  SmiTag(actual_parameter_count);
  Push(actual_parameter_count);
  SmiUntag(actual_parameter_count);

  if (new_target.is_valid()) {
    Push(new_target);
  }
  Push(fun);
  Push(fun);
  Push(receiver);

  // Recover root register.
  movd(kRootRegister, xmm0);

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
  ASM_CODE_COMMENT(this);
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());
  DCHECK_EQ(function, edi);
  DCHECK_IMPLIES(new_target.is_valid(), new_target == edx);
  DCHECK(expected_parameter_count == ecx || expected_parameter_count == eax);
  DCHECK_EQ(actual_parameter_count, eax);

  // On function call, call into the debugger if necessary.
  Label debug_hook, continue_after_hook;
  {
    ExternalReference debug_hook_active =
        ExternalReference::debug_hook_on_function_call_address(isolate());
    push(eax);
    cmpb(ExternalReferenceAsOperand(debug_hook_active, eax), Immediate(0));
    pop(eax);
    j(not_equal, &debug_hook);
  }
  bind(&continue_after_hook);

  // Clear the new.target register if not given.
  if (!new_target.is_valid()) {
    Move(edx, isolate()->factory()->undefined_value());
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
  jmp(&done, Label::kNear);

  // Deferred debug hook.
  bind(&debug_hook);
  CallDebugOnFunctionCall(function, new_target, expected_parameter_count,
                          actual_parameter_count);
  jmp(&continue_after_hook);

  bind(&done);
}

void MacroAssembler::InvokeFunction(Register fun, Register new_target,
                                    Register actual_parameter_count,
                                    InvokeType type) {
  ASM_CODE_COMMENT(this);
  // You can't call a function without a valid frame.
  DCHECK(type == InvokeType::kJump || has_frame());

  DCHECK(fun == edi);
  mov(ecx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
  mov(esi, FieldOperand(edi, JSFunction::kContextOffset));
  movzx_w(ecx,
          FieldOperand(ecx, SharedFunctionInfo::kFormalParameterCountOffset));

  InvokeFunctionCode(edi, new_target, ecx, actual_parameter_count, type);
}

void MacroAssembler::LoadGlobalProxy(Register dst) {
  LoadNativeContextSlot(dst, Context::GLOBAL_PROXY_INDEX);
}

void MacroAssembler::LoadNativeContextSlot(Register destination, int index) {
  ASM_CODE_COMMENT(this);
  // Load the native context from the current context.
  LoadMap(destination, esi);
  mov(destination,
      FieldOperand(destination,
                   Map::kConstructorOrBackPointerOrNativeContextOffset));
  // Load the function from the native context.
  mov(destination, Operand(destination, Context::SlotOffset(index)));
}

void MacroAssembler::Ret() { ret(0); }

void MacroAssembler::Ret(int bytes_dropped, Register scratch) {
  if (is_uint16(bytes_dropped)) {
    ret(bytes_dropped);
  } else {
    pop(scratch);
    add(esp, Immediate(bytes_dropped));
    push(scratch);
    ret(0);
  }
}

void MacroAssembler::Push(Immediate value) {
  if (root_array_available()) {
    if (value.is_external_reference()) {
      ExternalReference reference = value.external_reference();
      if (reference.IsIsolateFieldId()) {
        push(kRootRegister);
        add(Operand(esp, 0), Immediate(reference.offset_from_root_register()));
        return;
      }
      if (options().isolate_independent_code) {
        push(kRootRegister);
        add(Operand(esp, 0), Immediate(RootRegisterOffsetForExternalReference(
                                 isolate(), reference)));
        return;
      }
    }
    if (value.is_embedded_object()) {
      Push(HeapObjectAsOperand(value.embedded_object()));
      return;
    }
  }
  push(value);
}

void MacroAssembler::Drop(int stack_elements) {
  if (stack_elements > 0) {
    add(esp, Immediate(stack_elements * kSystemPointerSize));
  }
}

void MacroAssembler::Move(Register dst, Register src) {
  if (dst != src) {
    mov(dst, src);
  }
}

void MacroAssembler::Move(Register dst, const Immediate& src) {
  if (!src.is_heap_number_request() && src.is_zero()) {
    xor_(dst, dst);  // Shorter than mov of 32-bit immediate 0.
  } else if (src.is_external_reference()) {
    LoadAddress(dst, src.external_reference());
  } else {
    mov(dst, src);
  }
}

namespace {
bool ShouldUsePushPopForMove(bool root_array_available,
                             bool isolate_independent_code,
                             const Immediate& src) {
  if (root_array_available) {
    if (src.is_external_reference() &&
        src.external_reference().IsIsolateFieldId()) {
      return true;
    }
    if (isolate_independent_code) {
      if (src.is_external_reference()) return true;
      if (src.is_embedded_object()) return true;
      if (src.is_heap_number_request()) return true;
    }
  }
  return false;
}
}  // namespace

void MacroAssembler::Move(Operand dst, const Immediate& src) {
  // Since there's no scratch register available, take a detour through the
  // stack.
  if (ShouldUsePushPopForMove(root_array_available(),
                              options().isolate_independent_code, src)) {
    Push(src);
    pop(dst);
  } else if (src.is_embedded_object()) {
    mov(dst, src.embedded_object());
  } else {
    mov(dst, src);
  }
}

void MacroAssembler::Move(Register dst, Operand src) { mov(dst, src); }

void MacroAssembler::Move(Register dst, Handle<HeapObject> src) {
  if (root_array_available() && options().isolate_independent_code) {
    IndirectLoadConstant(dst, src);
    return;
  }
  mov(dst, src);
}

void MacroAssembler::Move(XMMRegister dst, uint32_t src) {
  if (src == 0) {
    pxor(dst, dst);
  } else {
    unsigned cnt = base::bits::CountPopulation(src);
    unsigned nlz = base::bits::CountLeadingZeros32(src);
    unsigned ntz = base::bits::CountTrailingZeros32(src);
    if (nlz + cnt + ntz == 32) {
      pcmpeqd(dst, dst);
      if (ntz == 0) {
        psrld(dst, 32 - cnt);
      } else {
        pslld(dst, 32 - cnt);
        if (nlz != 0) psrld(dst, nlz);
      }
    } else {
      push(eax);
      mov(eax, Immediate(src));
      movd(dst, Operand(eax));
      pop(eax);
    }
  }
}

void MacroAssembler::Move(XMMRegister dst, uint64_t src) {
  if (src == 0) {
    pxor(dst, dst);
  } else {
    uint32_t lower = static_cast<uint32_t>(src);
    uint32_t upper = static_cast<uint32_t>(src >> 32);
    unsigned cnt = base::bits::CountPopulation(src);
    unsigned nlz = base::bits::CountLeadingZeros64(src);
    unsigned ntz = base::bits::CountTrailingZeros64(src);
    if (nlz + cnt + ntz == 64) {
      pcmpeqd(dst, dst);
      if (ntz == 0) {
        psrlq(dst, 64 - cnt);
      } else {
        psllq(dst, 64 - cnt);
        if (nlz != 0) psrlq(dst, nlz);
      }
    } else if (lower == 0) {
      Move(dst, upper);
      psllq(dst, 32);
    } else if (CpuFeatures::IsSupported(SSE4_1)) {
      CpuFeatureScope scope(this, SSE4_1);
      push(eax);
      Move(eax, Immediate(lower));
      movd(dst, Operand(eax));
      if (upper != lower) {
        Move(eax, Immediate(upper));
      }
      pinsrd(dst, Operand(eax), 1);
      pop(eax);
    } else {
      push(Immediate(upper));
      push(Immediate(lower));
      movsd(dst, Operand(esp, 0));
      add(esp, Immediate(kDoubleSize));
    }
  }
}

void MacroAssembler::PextrdPreSse41(Register dst, XMMRegister src,
                                    uint8_t imm8) {
  if (imm8 == 0) {
    Movd(dst, src);
    return;
  }
  // Without AVX or SSE, we can only have 64-bit values in xmm registers.
  // We don't have an xmm scratch register, so move the data via the stack. This
  // path is rarely required, so it's acceptable to be slow.
  DCHECK_LT(imm8, 2);
  AllocateStackSpace(kDoubleSize);
  movsd(Operand(esp, 0), src);
  mov(dst, Operand(esp, imm8 * kUInt32Size));
  add(esp, Immediate(kDoubleSize));
}

void MacroAssembler::PinsrdPreSse41(XMMRegister dst, Operand src, uint8_t imm8,
                                    uint32_t* load_pc_offset) {
  // Without AVX or SSE, we can only have 64-bit values in xmm registers.
  // We don't have an xmm scratch register, so move the data via the stack. This
  // path is rarely required, so it's acceptable to be slow.
  DCHECK_LT(imm8, 2);
  AllocateStackSpace(kDoubleSize);
  // Write original content of {dst} to the stack.
  movsd(Operand(esp, 0), dst);
  // Overwrite the portion specified in {imm8}.
  if (src.is_reg_only()) {
    mov(Operand(esp, imm8 * kUInt32Size), src.reg());
  } else {
    movss(dst, src);
    movss(Operand(esp, imm8 * kUInt32Size), dst);
  }
  // Load back the full value into {dst}.
  movsd(dst, Operand(esp, 0));
  add(esp, Immediate(kDoubleSize));
}

void MacroAssembler::Lzcnt(Register dst, Operand src) {
  if (CpuFeatures::IsSupported(LZCNT)) {
    CpuFeatureScope scope(this, LZCNT);
    lzcnt(dst, src);
    return;
  }
  Label not_zero_src;
  bsr(dst, src);
  j(not_zero, &not_zero_src, Label::kNear);
  mov(dst, 63);  // 63^31 == 32
  bind(&not_zero_src);
  xor_(dst, Immediate(31));  // for x in [0..31], 31^x == 31-x.
}

void MacroAssembler::Tzcnt(Register dst, Operand src) {
  if (CpuFeatures::IsSupported(BMI1)) {
    CpuFeatureScope scope(this, BMI1);
    tzcnt(dst, src);
    return;
  }
  Label not_zero_src;
  bsf(dst, src);
  j(not_zero, &not_zero_src, Label::kNear);
  mov(dst, 32);  // The result of tzcnt is 32 if src = 0.
  bind(&not_zero_src);
}

void MacroAssembler::Popcnt(Register dst, Operand src) {
  if (CpuFeatures::IsSupported(POPCNT)) {
    CpuFeatureScope scope(this, POPCNT);
    popcnt(dst, src);
    return;
  }
  FATAL("no POPCNT support");
}

void MacroAssembler::LoadWeakValue(Register in_out, Label* target_if_cleared) {
  ASM_CODE_COMMENT(this);
  cmp(in_out, Immediate(kClearedWeakHeapObjectLower32));
  j(equal, target_if_cleared);

  and_(in_out, Immediate(~kWeakHeapObjectMask));
}

void MacroAssembler::EmitIncrementCounter(StatsCounter* counter, int value,
                                          Register scratch) {
  DCHECK_GT(value, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    ASM_CODE_COMMENT(this);
    Operand operand =
        ExternalReferenceAsOperand(ExternalReference::Create(counter), scratch);
    if (value == 1) {
      inc(operand);
    } else {
      add(operand, Immediate(value));
    }
  }
}

void MacroAssembler::EmitDecrementCounter(StatsCounter* counter, int value,
                                          Register scratch) {
  DCHECK_GT(value, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    ASM_CODE_COMMENT(this);
    Operand operand =
        ExternalReferenceAsOperand(ExternalReference::Create(counter), scratch);
    if (value == 1) {
      dec(operand);
    } else {
      sub(operand, Immediate(value));
    }
  }
}

void MacroAssembler::Check(Condition cc, AbortReason reason) {
  Label L;
  j(cc, &L);
  Abort(reason);
  // will not return here
  bind(&L);
}

void MacroAssembler::CheckStackAlignment() {
  ASM_CODE_COMMENT(this);
  int frame_alignment = base::OS::ActivationFrameAlignment();
  int frame_alignment_mask = frame_alignment - 1;
  if (frame_alignment > kSystemPointerSize) {
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    Label alignment_as_expected;
    test(esp, Immediate(frame_alignment_mask));
    j(zero, &alignment_as_expected);
    // Abort if stack is not aligned.
    int3();
    bind(&alignment_as_expected);
  }
}

void MacroAssembler::AlignStackPointer() {
  const int kFrameAlignment = base::OS::ActivationFrameAlignment();
  if (kFrameAlignment > 0) {
    DCHECK(base::bits::IsPowerOfTwo(kFrameAlignment));
    DCHECK(is_int8(kFrameAlignment));
    and_(esp, Immediate(-kFrameAlignment));
  }
}

void MacroAssembler::Abort(AbortReason reason) {
  if (v8_flags.code_comments) {
    const char* msg = GetAbortReason(reason);
    RecordComment("Abort message: ");
    RecordComment(msg);
  }

  // Avoid emitting call to builtin if requested.
  if (trap_on_abort()) {
    int3();
    return;
  }

  if (should_abort_hard()) {
    // We don't care if we constructed a frame. Just pretend we did.
    FrameScope assume_frame(this, StackFrame::NO_FRAME_TYPE);
    PrepareCallCFunction(1, eax);
    mov(Operand(esp, 0), Immediate(static_cast<int>(reason)));
    CallCFunction(ExternalReference::abort_with_reason(), 1);
    return;
  }

  Move(edx, Smi::FromInt(static_cast<int>(reason)));

  {
    // We don't actually want to generate a pile of code for this, so just
    // claim there is a stack frame, without generating one.
    FrameScope scope(this, StackFrame::NO_FRAME_TYPE);
    if (root_array_available()) {
      // Generate an indirect call via builtins entry table here in order to
      // ensure that the interpreter_entry_return_pc_offset is the same for
      // InterpreterEntryTrampoline and InterpreterEntryTrampolineForProfiling
      // when v8_flags.debug_code is enabled.
      Call(EntryFromBuiltinAsOperand(Builtin::kAbort));
    } else {
      CallBuiltin(Builtin::kAbort);
    }
  }

  // will not return here
  int3();
}

void MacroAssembler::PrepareCallCFunction(int num_arguments, Register scratch) {
  ASM_CODE_COMMENT(this);
  int frame_alignment = base::OS::ActivationFrameAlignment();
  if (frame_alignment != 0) {
    // Make stack end at alignment and make room for num_arguments words
    // and the original value of esp.
    mov(scratch, esp);
    AllocateStackSpace((num_arguments + 1) * kSystemPointerSize);
    AlignStackPointer();
    mov(Operand(esp, num_arguments * kSystemPointerSize), scratch);
  } else {
    AllocateStackSpace(num_arguments * kSystemPointerSize);
  }
}

int MacroAssembler::CallCFunction(ExternalReference function, int num_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  // Note: The "CallCFunction" code comment will be generated by the other
  // CallCFunction method called below.
  // Trashing eax is ok as it will be the return value.
  Move(eax, Immediate(function));
  return CallCFunction(eax, num_arguments, set_isolate_data_slots,
                       return_location);
}

int MacroAssembler::CallCFunction(Register function, int num_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
  ASM_CODE_COMMENT(this);
  DCHECK_LE(num_arguments, kMaxCParameters);
  DCHECK(has_frame());
  // Check stack alignment.
  if (v8_flags.debug_code) {
    CheckStackAlignment();
  }

  Label get_pc;

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // Save the frame pointer and PC so that the stack layout remains iterable,
    // even without an ExitFrame which normally exists between JS and C frames.
    // Find two caller-saved scratch registers.
    Register pc_scratch = eax;
    Register scratch = ecx;
    if (function == eax) pc_scratch = edx;
    if (function == ecx) scratch = edx;
    LoadLabelAddress(pc_scratch, &get_pc);

    // The root array is always available in production code. Only in one unit
    // test it is not available. The following code is not needed in the unit
    // test though, so we don't provide code here for the case where the root
    // array is not available.
    CHECK(root_array_available());
    mov(ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC),
        pc_scratch);
    mov(ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP), ebp);
  }

  call(function);
  int call_pc_offset = pc_offset();
  bind(&get_pc);
  if (return_location) bind(return_location);

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // We don't unset the PC; the FP is the source of truth.
    mov(ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP),
        Immediate(0));
  }

  if (base::OS::ActivationFrameAlignment() != 0) {
    mov(esp, Operand(esp, num_arguments * kSystemPointerSize));
  } else {
    add(esp, Immediate(num_arguments * kSystemPointerSize));
  }

  return call_pc_offset;
}

void MacroAssembler::PushPC() {
  // Push the current PC onto the stack as "return address" via calling
  // the next instruction.
  // This does not pollute the RAS:
  // see https://blog.stuffedcow.net/2018/04/ras-microbenchmarks/#call0.
  Label get_pc;
  call(&get_pc);
  bind(&get_pc);
}

void MacroAssembler::Call(Handle<Code> code_object, RelocInfo::Mode rmode) {
  ASM_CODE_COMMENT(this);
  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code_object));
  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code_object, &builtin)) {
    CallBuiltin(builtin);
    return;
  }
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  call(code_object, rmode);
}

void MacroAssembler::LoadEntryFromBuiltinIndex(Register builtin_index,
                                               Register target) {
  ASM_CODE_COMMENT(this);
  static_assert(kSystemPointerSize == 4);
  static_assert(kSmiShiftSize == 0);
  static_assert(kSmiTagSize == 1);
  static_assert(kSmiTag == 0);

  // The builtin_index register contains the builtin index as a Smi.
  // Untagging is folded into the indexing operand below (we use
  // times_half_system_pointer_size instead of times_system_pointer_size since
  // smis are already shifted by one).
  mov(target,
      Operand(kRootRegister, builtin_index, times_half_system_pointer_size,
              IsolateData::builtin_entry_table_offset()));
}

void MacroAssembler::CallBuiltinByIndex(Register builtin_index,
                                        Register target) {
  ASM_CODE_COMMENT(this);
  LoadEntryFromBuiltinIndex(builtin_index, target);
  call(target);
}

void MacroAssembler::CallBuiltin(Builtin builtin) {
  ASM_CODE_COMMENT_STRING(this, CommentForOffHeapTrampoline("call", builtin));
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      call(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      UNREACHABLE();
    case BuiltinCallJumpMode::kIndirect:
      call(EntryFromBuiltinAsOperand(builtin));
      break;
    case BuiltinCallJumpMode::kForMksnapshot: {
      Handle<Code> code = isolate()->builtins()->code_handle(builtin);
      call(code, RelocInfo::CODE_TARGET);
      break;
    }
  }
}

void MacroAssembler::TailCallBuiltin(Builtin builtin) {
  ASM_CODE_COMMENT_STRING(this,
                          CommentForOffHeapTrampoline("tail call", builtin));
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      jmp(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      UNREACHABLE();
    case BuiltinCallJumpMode::kIndirect:
      jmp(EntryFromBuiltinAsOperand(builtin));
      break;
    case BuiltinCallJumpMode::kForMksnapshot: {
      Handle<Code> code = isolate()->builtins()->code_handle(builtin);
      jmp(code, RelocInfo::CODE_TARGET);
      break;
    }
  }
}

Operand MacroAssembler::EntryFromBuiltinAsOperand(Builtin builtin) {
  ASM_CODE_COMMENT(this);
  return Operand(kRootRegister, IsolateData::BuiltinEntrySlotOffset(builtin));
}

void MacroAssembler::LoadCodeInstructionStart(Register destination,
                                              Register code_object,
                                              CodeEntrypointTag tag) {
  ASM_CODE_COMMENT(this);
  mov(destination, FieldOperand(code_object, Code::kInstructionStartOffset));
}

void MacroAssembler::CallCodeObject(Register code_object) {
  LoadCodeInstructionStart(code_object, code_object);
  call(code_object);
}

void MacroAssembler::JumpCodeObject(Register code_object, JumpMode jump_mode) {
  LoadCodeInstructionStart(code_object, code_object);
  switch (jump_mode) {
    case JumpMode::kJump:
      jmp(code_object);
      return;
    case JumpMode::kPushAndReturn:
      push(code_object);
      ret(0);
      return;
  }
}

void MacroAssembler::CallJSFunction(Register function_object) {
  static_assert(kJavaScriptCallCodeStartRegister == ecx, "ABI mismatch");
  mov(ecx, FieldOperand(function_object, JSFunction::kCodeOffset));
  CallCodeObject(ecx);
}

void MacroAssembler::JumpJSFunction(Register function_object,
                                    JumpMode jump_mode) {
  static_assert(kJavaScriptCallCodeStartRegister == ecx, "ABI mismatch");
  mov(ecx, FieldOperand(function_object, JSFunction::kCodeOffset));
  JumpCodeObject(ecx, jump_mode);
}

void MacroAssembler::Jump(const ExternalReference& reference) {
  DCHECK(root_array_available());
  jmp(Operand(kRootRegister, RootRegisterOffsetForExternalReferenceTableEntry(
                                 isolate(), reference)));
}

void MacroAssembler::Jump(Handle<Code> code_object, RelocInfo::Mode rmode) {
  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code_object));
  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code_object, &builtin)) {
    TailCallBuiltin(builtin);
    return;
  }
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  jmp(code_object, rmode);
}

void MacroAssembler::LoadLabelAddress(Register dst, Label* lbl) {
  // An lea of a label using position independent code
  // The instruction delta 10 is the difference between the
  // value of PC we obtain, from that what we need
  // which is just after the lea instruction itself.
  //

  // The byte distance between acquired PC and end of sequence.
  const int kInsDelta = 10;
  PushPC();
#ifdef DEBUG
  const int kStart = pc_offset();
#endif
  pop(dst);
  add(dst, Immediate(kInsDelta));  // point to after next instruction
  lea(dst, dst, lbl);
  DCHECK(pc_offset() - kStart == kInsDelta);
}

void MacroAssembler::MemoryChunkHeaderFromObject(Register object,
                                                 Register header) {
  constexpr intptr_t alignment_mask =
      MemoryChunk::GetAlignmentMaskForAssembler();
  if (header == object) {
    and_(header, Immediate(~alignment_mask));
  } else {
    mov(header, Immediate(~alignment_mask));
    and_(header, object);
  }
}

void MacroAssembler::CheckPageFlag(Register object, Register scratch, int mask,
                                   Condition cc, Label* condition_met,
                                   Label::Distance condition_met_distance) {
  ASM_CODE_COMMENT(this);
  DCHECK(cc == zero || cc == not_zero);
  MemoryChunkHeaderFromObject(object, scratch);
  if (mask < (1 << kBitsPerByte)) {
    test_b(Operand(scratch, MemoryChunkLayout::kFlagsOffset), Immediate(mask));
  } else {
    test(Operand(scratch, MemoryChunkLayout::kFlagsOffset), Immediate(mask));
  }
  j(cc, condition_met, condition_met_distance);
}

void MacroAssembler::ComputeCodeStartAddress(Register dst) {
  ASM_CODE_COMMENT(this);
  // In order to get the address of the current instruction, we first need
  // to use a call and then use a pop, thus pushing the return address to
  // the stack and then popping it into the register.
  Label current;
  call(&current);
  int pc = pc_offset();
  bind(&current);
  pop(dst);
  if (pc != 0) {
    sub(dst, Immediate(pc));
  }
}

void MacroAssembler::CallForDeoptimization(Builtin target, int, Label* exit,
                                           DeoptimizeKind kind, Label* ret,
                                           Label*) {
  ASM_CODE_COMMENT(this);
#if V8_ENABLE_WEBASSEMBLY
  if (options().is_wasm) {
    CHECK(v8_flags.wasm_deopt);
    wasm_call(static_cast<Address>(target), RelocInfo::WASM_STUB_CALL);
#else
  // For balance.
  if (false) {
#endif  // V8_ENABLE_WEBASSEMBLY
  } else {
    CallBuiltin(target);
  }
  DCHECK_EQ(SizeOfCodeGeneratedSince(exit),
            (kind == DeoptimizeKind::kLazy) ? Deoptimizer::kLazyDeoptExitSize
                                            : Deoptimizer::kEagerDeoptExitSize);
}

void MacroAssembler::Trap() { int3(); }
void MacroAssembler::DebugBreak() { int3(); }

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

  using ER = ExternalReference;

  Isolate* isolate = masm->isolate();
  MemOperand next_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_next_address(isolate), no_reg);
  MemOperand limit_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_limit_address(isolate), no_reg);
  MemOperand level_mem_op = __ ExternalReferenceAsOperand(
      ER::handle_scope_level_address(isolate), no_reg);

  Register return_value = eax;
  DCHECK(function_address == edx || function_address == eax);
  // Use scratch as an "opposite" of function_address register.
  Register scratch = function_address == edx ? ecx : edx;

  // Allocate HandleScope in callee-saved registers.
  // We will need to restore the HandleScope after the call to the API function,
  // by allocating it in callee-saved registers it'll be preserved by C code.
  Register prev_next_address_reg = esi;
  Register prev_limit_reg = edi;

  DCHECK(!AreAliased(return_value, scratch, prev_next_address_reg,
                     prev_limit_reg));
  // function_address and thunk_arg might overlap but this function must not
  // corrupted them until the call is made (i.e. overlap with return_value is
  // fine).
  DCHECK(!AreAliased(function_address,  // incoming parameters
                     scratch, prev_next_address_reg, prev_limit_reg));
  DCHECK(!AreAliased(thunk_arg,  // incoming parameters
                     scratch, prev_next_address_reg, prev_limit_reg));
  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Allocate HandleScope in callee-save registers.");
    __ add(level_mem_op, Immediate(1));
    __ mov(prev_next_address_reg, next_mem_op);
    __ mov(prev_limit_reg, limit_mem_op);
  }

  Label profiler_or_side_effects_check_enabled, done_api_call;
  if (with_profiling) {
    __ RecordComment("Check if profiler or side effects check is enabled");
    __ cmpb(__ ExternalReferenceAsOperand(IsolateFieldId::kExecutionMode),
            Immediate(0));
    __ j(not_zero, &profiler_or_side_effects_check_enabled);
#ifdef V8_RUNTIME_CALL_STATS
    __ RecordComment("Check if RCS is enabled");
    __ Move(scratch, Immediate(ER::address_of_runtime_stats_flag()));
    __ cmp(Operand(scratch, 0), Immediate(0));
    __ j(not_zero, &profiler_or_side_effects_check_enabled);
#endif  // V8_RUNTIME_CALL_STATS
  }

  __ RecordComment("Call the api function directly.");
  __ call(function_address);
  __ bind(&done_api_call);

  __ RecordComment("Load the value from ReturnValue");
  __ mov(return_value, return_value_operand);

  Label propagate_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;

  {
    ASM_CODE_COMMENT_STRING(
        masm,
        "No more valid handles (the result handle was the last one)."
        "Restore previous handle scope.");
    __ mov(next_mem_op, prev_next_address_reg);
    __ sub(level_mem_op, Immediate(1));
    __ Assert(above_equal, AbortReason::kInvalidHandleScopeLevel);
    __ cmp(prev_limit_reg, limit_mem_op);
    __ j(not_equal, &delete_allocated_handles);
  }

  __ RecordComment("Leave the API exit frame.");
  __ bind(&leave_exit_frame);
  Register argc_reg = prev_limit_reg;
  if (argc_operand != nullptr) {
    __ mov(argc_reg, *argc_operand);
  }
  __ LeaveExitFrame(scratch);

  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Check if the function scheduled an exception.");
    __ mov(scratch, __ ExternalReferenceAsOperand(
                        ER::exception_address(isolate), no_reg));
    __ CompareRoot(scratch, RootIndex::kTheHoleValue);
    __ j(not_equal, &propagate_exception);
  }

  __ AssertJSAny(return_value, scratch,
                 AbortReason::kAPICallReturnedInvalidObject);

  if (argc_operand == nullptr) {
    DCHECK_NE(slots_to_drop_on_return, 0);
    __ ret(slots_to_drop_on_return * kSystemPointerSize);
  } else {
    __ pop(scratch);
    // {argc_operand} was loaded into {argc_reg} above.
    __ lea(esp, Operand(esp, argc_reg, times_system_pointer_size,
                        slots_to_drop_on_return * kSystemPointerSize));
    __ jmp(scratch);
  }

  if (with_profiling) {
    ASM_CODE_COMMENT_STRING(masm, "Call the api function via thunk wrapper.");
    __ bind(&profiler_or_side_effects_check_enabled);
    // Additional parameter is the address of the actual callback function.
    if (thunk_arg.is_valid()) {
      MemOperand thunk_arg_mem_op = __ ExternalReferenceAsOperand(
          IsolateFieldId::kApiCallbackThunkArgument);
      __ mov(thunk_arg_mem_op, thunk_arg);
    }
    __ Move(scratch, Immediate(thunk_ref));
    __ call(scratch);
    __ jmp(&done_api_call);
  }

  __ RecordComment("An exception was thrown. Propagate it.");
  __ bind(&propagate_exception);
  __ TailCallRuntime(Runtime::kPropagateException);

  {
    ASM_CODE_COMMENT_STRING(
        masm, "HandleScope limit has changed. Delete allocated extensions.");
    __ bind(&delete_allocated_handles);
    __ mov(limit_mem_op, prev_limit_reg);
    // Save the return value in a callee-save register.
    Register saved_result = prev_limit_reg;
    __ mov(saved_result, return_value);
    __ Move(scratch, Immediate(ER::isolate_address()));
    __ mov(Operand(esp, 0), scratch);
    __ Move(scratch, Immediate(ER::delete_handle_scope_extensions()));
    __ call(scratch);
    __ mov(return_value, saved_result);
    __ jmp(&leave_exit_frame);
  }
}

// SMI related operations

void MacroAssembler::SmiCompare(Register smi1, Register smi2) {
  AssertSmi(smi1);
  AssertSmi(smi2);
  cmp(smi1, smi2);
}

void MacroAssembler::SmiCompare(Register dst, Tagged<Smi> src) {
  AssertSmi(dst);
  cmp(dst, Immediate(src));
}

void MacroAssembler::SmiCompare(Register dst, Operand src) {
  AssertSmi(dst);
  AssertSmi(src);
  cmp(dst, src);
}

void MacroAssembler::SmiCompare(Operand dst, Register src) {
  AssertSmi(dst);
  AssertSmi(src);
  cmp(dst, src);
}

}  // namespace internal
}  // namespace v8

#undef __

#endif  // V8_TARGET_ARCH_IA32
