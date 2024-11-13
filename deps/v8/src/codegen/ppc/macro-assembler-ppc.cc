// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>  // For assert
#include <limits.h>  // For LONG_MIN, LONG_MAX.

#if V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64

#include <optional>

#include "src/base/bits.h"
#include "src/base/division-by-constant.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/external-reference-table.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/register.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frames-inl.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/init/bootstrapper.h"
#include "src/logging/counters.h"
#include "src/runtime/runtime.h"
#include "src/snapshot/snapshot.h"

// Satisfy cpplint check, but don't include platform-specific header. It is
// included recursively via macro-assembler.h.
#if 0
#include "src/codegen/ppc/macro-assembler-ppc.h"
#endif

#define __ ACCESS_MASM(masm)

namespace v8 {
namespace internal {

namespace {

// Simd and Floating Pointer registers are not shared. For WebAssembly we save
// both registers, If we are not running Wasm, we can get away with only saving
// FP registers.
#if V8_ENABLE_WEBASSEMBLY
constexpr int kStackSavedSavedFPSizeInBytes =
    (kNumCallerSavedDoubles * kSimd128Size) +
    (kNumCallerSavedDoubles * kDoubleSize);
#else
constexpr int kStackSavedSavedFPSizeInBytes =
    kNumCallerSavedDoubles * kDoubleSize;
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace

int MacroAssembler::RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                                    Register exclusion1,
                                                    Register exclusion2,
                                                    Register exclusion3) const {
  int bytes = 0;

  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  bytes += list.Count() * kSystemPointerSize;

  if (fp_mode == SaveFPRegsMode::kSave) {
    bytes += kStackSavedSavedFPSizeInBytes;
  }

  return bytes;
}

int MacroAssembler::PushCallerSaved(SaveFPRegsMode fp_mode, Register scratch1,
                                    Register scratch2, Register exclusion1,
                                    Register exclusion2, Register exclusion3) {
  int bytes = 0;

  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  MultiPush(list);
  bytes += list.Count() * kSystemPointerSize;

  if (fp_mode == SaveFPRegsMode::kSave) {
    MultiPushF64AndV128(kCallerSavedDoubles, kCallerSavedSimd128s, scratch1,
                        scratch2);
    bytes += kStackSavedSavedFPSizeInBytes;
  }

  return bytes;
}

int MacroAssembler::PopCallerSaved(SaveFPRegsMode fp_mode, Register scratch1,
                                   Register scratch2, Register exclusion1,
                                   Register exclusion2, Register exclusion3) {
  int bytes = 0;
  if (fp_mode == SaveFPRegsMode::kSave) {
    MultiPopF64AndV128(kCallerSavedDoubles, kCallerSavedSimd128s, scratch1,
                       scratch2);
    bytes += kStackSavedSavedFPSizeInBytes;
  }

  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  MultiPop(list);
  bytes += list.Count() * kSystemPointerSize;

  return bytes;
}

void MacroAssembler::Jump(Register target) {
  mtctr(target);
  bctr();
}

void MacroAssembler::LoadFromConstantsTable(Register destination,
                                            int constant_index) {
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kBuiltinsConstantsTable));

  DCHECK_NE(destination, r0);
  LoadRoot(destination, RootIndex::kBuiltinsConstantsTable);
  LoadTaggedField(destination,
                  FieldMemOperand(destination, FixedArray::OffsetOfElementAt(
                                                   constant_index)),
                  r0);
}

void MacroAssembler::LoadRootRelative(Register destination, int32_t offset) {
  LoadU64(destination, MemOperand(kRootRegister, offset), r0);
}

void MacroAssembler::StoreRootRelative(int32_t offset, Register value) {
  StoreU64(value, MemOperand(kRootRegister, offset));
}

void MacroAssembler::LoadRootRegisterOffset(Register destination,
                                            intptr_t offset) {
  if (offset == 0) {
    mr(destination, kRootRegister);
  } else {
    AddS64(destination, kRootRegister, Operand(offset), destination);
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
        LoadU64(scratch,
                MemOperand(kRootRegister,
                           RootRegisterOffsetForExternalReferenceTableEntry(
                               isolate(), reference)));
        return MemOperand(scratch, 0);
      }
    }
  }
  Move(scratch, reference);
  return MemOperand(scratch, 0);
}

void MacroAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond, CRegister cr) {
  Label skip;

  if (cond != al) b(NegateCondition(cond), &skip, cr);

  mov(ip, Operand(target, rmode));
  mtctr(ip);
  bctr();

  bind(&skip);
}

void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode, Condition cond,
                          CRegister cr) {
  DCHECK(!RelocInfo::IsCodeTarget(rmode));
  Jump(static_cast<intptr_t>(target), rmode, cond, cr);
}

void MacroAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, CRegister cr) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code));

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    TailCallBuiltin(builtin, cond, cr);
    return;
  }
  int32_t target_index = AddCodeTarget(code);
  Jump(static_cast<intptr_t>(target_index), rmode, cond, cr);
}

void MacroAssembler::Jump(const ExternalReference& reference) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Move(scratch, reference);
  if (ABI_USES_FUNCTION_DESCRIPTORS) {
    // AIX uses a function descriptor. When calling C code be
    // aware of this descriptor and pick up values from it.
    LoadU64(ToRegister(ABI_TOC_REGISTER),
            MemOperand(scratch, kSystemPointerSize));
    LoadU64(scratch, MemOperand(scratch, 0));
  }
  Jump(scratch);
}

void MacroAssembler::Call(Register target) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  // branch via link register and set LK bit for return point
  mtctr(target);
  bctrl();
}

void MacroAssembler::CallJSEntry(Register target) {
  CHECK(target == r5);
  Call(target);
}

int MacroAssembler::CallSizeNotPredictableCodeSize(Address target,
                                                   RelocInfo::Mode rmode,
                                                   Condition cond) {
  return (2 + kMovInstructionsNoConstantPool) * kInstrSize;
}

void MacroAssembler::Call(Address target, RelocInfo::Mode rmode,
                          Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(cond == al);

  // This can likely be optimized to make use of bc() with 24bit relative
  //
  // RecordRelocInfo(x.rmode_, x.immediate);
  // bc( BA, .... offset, LKset);
  //

  mov(ip, Operand(target, rmode));
  mtctr(ip);
  bctrl();
}

void MacroAssembler::Call(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code));

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    CallBuiltin(builtin, cond);
    return;
  }
  int32_t target_index = AddCodeTarget(code);
  Call(static_cast<Address>(target_index), rmode, cond);
}

void MacroAssembler::CallBuiltin(Builtin builtin, Condition cond) {
  ASM_CODE_COMMENT_STRING(this, CommentForOffHeapTrampoline("call", builtin));
  // Use ip directly instead of using UseScratchRegisterScope, as we do not
  // preserve scratch registers across calls.
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      Label skip;
      mov(ip, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      if (cond != al) b(NegateCondition(cond), &skip);
      Call(ip);
      bind(&skip);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      UNREACHABLE();
    case BuiltinCallJumpMode::kIndirect: {
      Label skip;
      LoadU64(ip, EntryFromBuiltinAsOperand(builtin));
      if (cond != al) b(NegateCondition(cond), &skip);
      Call(ip);
      bind(&skip);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        int32_t code_target_index = AddCodeTarget(code);
        Call(static_cast<Address>(code_target_index), RelocInfo::CODE_TARGET,
             cond);
      } else {
        Label skip;
        LoadU64(ip, EntryFromBuiltinAsOperand(builtin), r0);
        if (cond != al) b(NegateCondition(cond), &skip);
        Call(ip);
        bind(&skip);
      }
      break;
    }
  }
}

void MacroAssembler::TailCallBuiltin(Builtin builtin, Condition cond,
                                     CRegister cr) {
  ASM_CODE_COMMENT_STRING(this,
                          CommentForOffHeapTrampoline("tail call", builtin));
  // Use ip directly instead of using UseScratchRegisterScope, as we do not
  // preserve scratch registers across calls.
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      Label skip;
      mov(ip, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      if (cond != al) b(NegateCondition(cond), &skip, cr);
      Jump(ip);
      bind(&skip);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      UNREACHABLE();
    case BuiltinCallJumpMode::kIndirect: {
      Label skip;
      LoadU64(ip, EntryFromBuiltinAsOperand(builtin));
      if (cond != al) b(NegateCondition(cond), &skip, cr);
      Jump(ip);
      bind(&skip);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        int32_t code_target_index = AddCodeTarget(code);
        Jump(static_cast<intptr_t>(code_target_index), RelocInfo::CODE_TARGET,
             cond, cr);
      } else {
        Label skip;
        LoadU64(ip, EntryFromBuiltinAsOperand(builtin), r0);
        if (cond != al) b(NegateCondition(cond), &skip, cr);
        Jump(ip);
        bind(&skip);
      }
      break;
    }
  }
}

void MacroAssembler::Drop(int count) {
  if (count > 0) {
    AddS64(sp, sp, Operand(count * kSystemPointerSize), r0);
  }
}

void MacroAssembler::Drop(Register count, Register scratch) {
  ShiftLeftU64(scratch, count, Operand(kSystemPointerSizeLog2));
  add(sp, sp, scratch);
}

void MacroAssembler::TestCodeIsMarkedForDeoptimization(Register code,
                                                       Register scratch1,
                                                       Register scratch2) {
  LoadU32(scratch1, FieldMemOperand(code, Code::kFlagsOffset), scratch2);
  TestBit(scratch1, Code::kMarkedForDeoptimizationBit, scratch2);
}

Operand MacroAssembler::ClearedValue() const {
  return Operand(static_cast<int32_t>(i::ClearedValue(isolate()).ptr()));
}

void MacroAssembler::Call(Label* target) { b(target, SetLK); }

void MacroAssembler::Push(Handle<HeapObject> handle) {
  mov(r0, Operand(handle));
  push(r0);
}

void MacroAssembler::Push(Tagged<Smi> smi) {
  mov(r0, Operand(smi));
  push(r0);
}

void MacroAssembler::PushArray(Register array, Register size, Register scratch,
                               Register scratch2, PushArrayOrder order) {
  Label loop, done;

  if (order == kNormal) {
    cmpi(size, Operand::Zero());
    beq(&done);
    ShiftLeftU64(scratch, size, Operand(kSystemPointerSizeLog2));
    add(scratch, array, scratch);
    mtctr(size);

    bind(&loop);
    LoadU64WithUpdate(scratch2, MemOperand(scratch, -kSystemPointerSize));
    StoreU64WithUpdate(scratch2, MemOperand(sp, -kSystemPointerSize));
    bdnz(&loop);

    bind(&done);
  } else {
    cmpi(size, Operand::Zero());
    beq(&done);

    mtctr(size);
    subi(scratch, array, Operand(kSystemPointerSize));

    bind(&loop);
    LoadU64WithUpdate(scratch2, MemOperand(scratch, kSystemPointerSize));
    StoreU64WithUpdate(scratch2, MemOperand(sp, -kSystemPointerSize));
    bdnz(&loop);
    bind(&done);
  }
}

void MacroAssembler::Move(Register dst, Handle<HeapObject> value,
                          RelocInfo::Mode rmode) {
  // TODO(jgruber,v8:8887): Also consider a root-relative load when generating
  // non-isolate-independent code. In many cases it might be cheaper than
  // embedding the relocatable value.
  if (root_array_available_ && options().isolate_independent_code) {
    IndirectLoadConstant(dst, value);
    return;
  } else if (RelocInfo::IsCompressedEmbeddedObject(rmode)) {
    EmbeddedObjectIndex index = AddEmbeddedObject(value);
    DCHECK(is_uint32(index));
    mov(dst, Operand(static_cast<int>(index), rmode));
  } else {
    DCHECK(RelocInfo::IsFullEmbeddedObject(rmode));
    mov(dst, Operand(value.address(), rmode));
  }
}

void MacroAssembler::Move(Register dst, ExternalReference reference) {
  if (root_array_available()) {
    if (reference.IsIsolateFieldId()) {
      AddS64(dst, kRootRegister,
             Operand(reference.offset_from_root_register()));
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
  mov(dst, Operand(reference));
}

void MacroAssembler::LoadIsolateField(Register dst, IsolateFieldId id) {
  Move(dst, ExternalReference::Create(id));
}

void MacroAssembler::Move(Register dst, Register src, Condition cond) {
  DCHECK(cond == al);
  if (dst != src) {
    mr(dst, src);
  }
}

void MacroAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  if (dst != src) {
    fmr(dst, src);
  }
}

void MacroAssembler::MultiPush(RegList regs, Register location) {
  int16_t num_to_push = regs.Count();
  int16_t stack_offset = num_to_push * kSystemPointerSize;

  subi(location, location, Operand(stack_offset));
  for (int16_t i = Register::kNumRegisters - 1; i >= 0; i--) {
    if ((regs.bits() & (1 << i)) != 0) {
      stack_offset -= kSystemPointerSize;
      StoreU64(ToRegister(i), MemOperand(location, stack_offset));
    }
  }
}

void MacroAssembler::MultiPop(RegList regs, Register location) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < Register::kNumRegisters; i++) {
    if ((regs.bits() & (1 << i)) != 0) {
      LoadU64(ToRegister(i), MemOperand(location, stack_offset));
      stack_offset += kSystemPointerSize;
    }
  }
  addi(location, location, Operand(stack_offset));
}

void MacroAssembler::MultiPushDoubles(DoubleRegList dregs, Register location) {
  int16_t num_to_push = dregs.Count();
  int16_t stack_offset = num_to_push * kDoubleSize;

  subi(location, location, Operand(stack_offset));
  for (int16_t i = DoubleRegister::kNumRegisters - 1; i >= 0; i--) {
    if ((dregs.bits() & (1 << i)) != 0) {
      DoubleRegister dreg = DoubleRegister::from_code(i);
      stack_offset -= kDoubleSize;
      stfd(dreg, MemOperand(location, stack_offset));
    }
  }
}

void MacroAssembler::MultiPushV128(Simd128RegList simd_regs, Register scratch,
                                   Register location) {
  int16_t num_to_push = simd_regs.Count();
  int16_t stack_offset = num_to_push * kSimd128Size;

  subi(location, location, Operand(stack_offset));
  for (int16_t i = Simd128Register::kNumRegisters - 1; i >= 0; i--) {
    if ((simd_regs.bits() & (1 << i)) != 0) {
      Simd128Register simd_reg = Simd128Register::from_code(i);
      stack_offset -= kSimd128Size;
      StoreSimd128(simd_reg, MemOperand(location, stack_offset), scratch);
    }
  }
}

void MacroAssembler::MultiPopDoubles(DoubleRegList dregs, Register location) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < DoubleRegister::kNumRegisters; i++) {
    if ((dregs.bits() & (1 << i)) != 0) {
      DoubleRegister dreg = DoubleRegister::from_code(i);
      lfd(dreg, MemOperand(location, stack_offset));
      stack_offset += kDoubleSize;
    }
  }
  addi(location, location, Operand(stack_offset));
}

void MacroAssembler::MultiPopV128(Simd128RegList simd_regs, Register scratch,
                                  Register location) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < Simd128Register::kNumRegisters; i++) {
    if ((simd_regs.bits() & (1 << i)) != 0) {
      Simd128Register simd_reg = Simd128Register::from_code(i);
      LoadSimd128(simd_reg, MemOperand(location, stack_offset), scratch);
      stack_offset += kSimd128Size;
    }
  }
  addi(location, location, Operand(stack_offset));
}

void MacroAssembler::MultiPushF64AndV128(DoubleRegList dregs,
                                         Simd128RegList simd_regs,
                                         Register scratch1, Register scratch2,
                                         Register location) {
  MultiPushDoubles(dregs);
#if V8_ENABLE_WEBASSEMBLY
  bool generating_bultins =
      isolate() && isolate()->IsGeneratingEmbeddedBuiltins();
  if (generating_bultins) {
    // V8 uses the same set of fp param registers as Simd param registers.
    // As these registers are two different sets on ppc we must make
    // sure to also save them when Simd is enabled.
    // Check the comments under crrev.com/c/2645694 for more details.
    Label push_empty_simd, simd_pushed;
    Move(scratch1, ExternalReference::supports_wasm_simd_128_address());
    LoadU8(scratch1, MemOperand(scratch1), scratch2);
    cmpi(scratch1, Operand::Zero());  // If > 0 then simd is available.
    ble(&push_empty_simd);
    MultiPushV128(simd_regs, scratch1);
    b(&simd_pushed);
    bind(&push_empty_simd);
    // We still need to allocate empty space on the stack even if we
    // are not pushing Simd registers (see kFixedFrameSizeFromFp).
    addi(sp, sp,
         Operand(-static_cast<int8_t>(simd_regs.Count()) * kSimd128Size));
    bind(&simd_pushed);
  } else {
    if (CpuFeatures::SupportsWasmSimd128()) {
      MultiPushV128(simd_regs, scratch1);
    } else {
      addi(sp, sp,
           Operand(-static_cast<int8_t>(simd_regs.Count()) * kSimd128Size));
    }
  }
#endif
}

void MacroAssembler::MultiPopF64AndV128(DoubleRegList dregs,
                                        Simd128RegList simd_regs,
                                        Register scratch1, Register scratch2,
                                        Register location) {
#if V8_ENABLE_WEBASSEMBLY
  bool generating_bultins =
      isolate() && isolate()->IsGeneratingEmbeddedBuiltins();
  if (generating_bultins) {
    Label pop_empty_simd, simd_popped;
    Move(scratch1, ExternalReference::supports_wasm_simd_128_address());
    LoadU8(scratch1, MemOperand(scratch1), scratch2);
    cmpi(scratch1, Operand::Zero());  // If > 0 then simd is available.
    ble(&pop_empty_simd);
    MultiPopV128(simd_regs, scratch1);
    b(&simd_popped);
    bind(&pop_empty_simd);
    addi(sp, sp,
         Operand(static_cast<int8_t>(simd_regs.Count()) * kSimd128Size));
    bind(&simd_popped);
  } else {
    if (CpuFeatures::SupportsWasmSimd128()) {
      MultiPopV128(simd_regs, scratch1);
    } else {
      addi(sp, sp,
           Operand(static_cast<int8_t>(simd_regs.Count()) * kSimd128Size));
    }
  }
#endif
  MultiPopDoubles(dregs);
}

void MacroAssembler::LoadTaggedRoot(Register destination, RootIndex index) {
  ASM_CODE_COMMENT(this);
  if (CanBeImmediate(index)) {
    mov(destination, Operand(ReadOnlyRootPtr(index), RelocInfo::Mode::NO_INFO));
    return;
  }
  LoadRoot(destination, index);
}

void MacroAssembler::LoadRoot(Register destination, RootIndex index,
                              Condition cond) {
  DCHECK(cond == al);
  if (CanBeImmediate(index)) {
    DecompressTagged(destination, ReadOnlyRootPtr(index));
    return;
  }
  LoadU64(destination,
          MemOperand(kRootRegister, RootRegisterOffsetForRootIndex(index)), r0);
}

void MacroAssembler::LoadTaggedField(const Register& destination,
                                     const MemOperand& field_operand,
                                     const Register& scratch) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTagged(destination, field_operand);
  } else {
    LoadU64(destination, field_operand, scratch);
  }
}

void MacroAssembler::SmiUntag(Register dst, const MemOperand& src, RCBit rc,
                              Register scratch) {
  if (SmiValuesAre31Bits()) {
    LoadU32(dst, src, scratch);
  } else {
    LoadU64(dst, src, scratch);
  }

  SmiUntag(dst, rc);
}

void MacroAssembler::StoreTaggedField(const Register& value,
                                      const MemOperand& dst_field_operand,
                                      const Register& scratch) {
  if (COMPRESS_POINTERS_BOOL) {
    RecordComment("[ StoreTagged");
    StoreU32(value, dst_field_operand, scratch);
    RecordComment("]");
  } else {
    StoreU64(value, dst_field_operand, scratch);
  }
}

void MacroAssembler::DecompressTaggedSigned(Register destination,
                                            Register src) {
  RecordComment("[ DecompressTaggedSigned");
  ZeroExtWord32(destination, src);
  RecordComment("]");
}

void MacroAssembler::DecompressTaggedSigned(Register destination,
                                            MemOperand field_operand) {
  RecordComment("[ DecompressTaggedSigned");
  LoadU32(destination, field_operand, r0);
  RecordComment("]");
}

void MacroAssembler::DecompressTagged(Register destination, Register source) {
  RecordComment("[ DecompressTagged");
  ZeroExtWord32(destination, source);
  add(destination, destination, kPtrComprCageBaseRegister);
  RecordComment("]");
}

void MacroAssembler::DecompressTagged(Register destination,
                                      MemOperand field_operand) {
  RecordComment("[ DecompressTagged");
  LoadU32(destination, field_operand, r0);
  add(destination, destination, kPtrComprCageBaseRegister);
  RecordComment("]");
}

void MacroAssembler::DecompressTagged(const Register& destination,
                                      Tagged_t immediate) {
  ASM_CODE_COMMENT(this);
  AddS64(destination, kPtrComprCageBaseRegister,
         Operand(immediate, RelocInfo::Mode::NO_INFO));
}

void MacroAssembler::LoadTaggedSignedField(Register destination,
                                           MemOperand field_operand,
                                           Register scratch) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTaggedSigned(destination, field_operand);
  } else {
    LoadU64(destination, field_operand, scratch);
  }
}

void MacroAssembler::RecordWriteField(Register object, int offset,
                                      Register value, Register slot_address,
                                      LinkRegisterStatus lr_status,
                                      SaveFPRegsMode save_fp,
                                      SmiCheck smi_check, SlotDescriptor slot) {
  // First, check if a write barrier is even needed. The tests below
  // catch stores of Smis.
  Label done;

  // Skip barrier if writing a smi.
  if (smi_check == SmiCheck::kInline) {
    JumpIfSmi(value, &done);
  }

  // Although the object register is tagged, the offset is relative to the start
  // of the object, so so offset must be a multiple of kSystemPointerSize.
  DCHECK(IsAligned(offset, kTaggedSize));

  AddS64(slot_address, object, Operand(offset - kHeapObjectTag), r0);
  if (v8_flags.debug_code) {
    Label ok;
    andi(r0, slot_address, Operand(kTaggedSize - 1));
    beq(&ok, cr0);
    stop();
    bind(&ok);
  }

  RecordWrite(object, slot_address, value, lr_status, save_fp, SmiCheck::kOmit,
              slot);

  bind(&done);

  // Clobber clobbered input registers when running with the debug-code flag
  // turned on to provoke errors.
  if (v8_flags.debug_code) {
    mov(value, Operand(base::bit_cast<intptr_t>(kZapValue + 4)));
    mov(slot_address, Operand(base::bit_cast<intptr_t>(kZapValue + 8)));
  }
}

void MacroAssembler::DecodeSandboxedPointer(Register value) {
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  ShiftRightU64(value, value, Operand(kSandboxedPointerShift));
  AddS64(value, value, kPtrComprCageBaseRegister);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::LoadSandboxedPointerField(Register destination,
                                               const MemOperand& field_operand,
                                               Register scratch) {
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  LoadU64(destination, field_operand, scratch);
  DecodeSandboxedPointer(destination);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::StoreSandboxedPointerField(
    Register value, const MemOperand& dst_field_operand, Register scratch) {
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  DCHECK(!AreAliased(scratch, scratch2));
  SubS64(scratch2, value, kPtrComprCageBaseRegister);
  ShiftLeftU64(scratch2, scratch2, Operand(kSandboxedPointerShift));
  StoreU64(scratch2, dst_field_operand, scratch);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::LoadExternalPointerField(Register destination,
                                              MemOperand field_operand,
                                              ExternalPointerTag tag,
                                              Register isolate_root,
                                              Register scratch) {
  DCHECK(!AreAliased(destination, isolate_root));
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  DCHECK_NE(tag, kExternalPointerNullTag);
  DCHECK(!IsSharedExternalPointerType(tag));
  UseScratchRegisterScope temps(this);
  Register external_table = temps.Acquire();
  DCHECK(!AreAliased(scratch, external_table));
  if (isolate_root == no_reg) {
    DCHECK(root_array_available_);
    isolate_root = kRootRegister;
  }
  LoadU64(external_table,
          MemOperand(isolate_root,
                     IsolateData::external_pointer_table_offset() +
                         Internals::kExternalPointerTableBasePointerOffset),
          scratch);
  LoadU32(destination, field_operand, scratch);
  ShiftRightU64(destination, destination, Operand(kExternalPointerIndexShift));
  ShiftLeftU64(destination, destination,
               Operand(kExternalPointerTableEntrySizeLog2));
  LoadU64(destination, MemOperand(external_table, destination), scratch);
  mov(scratch, Operand(~tag));
  AndU64(destination, destination, scratch);
#else
  LoadU64(destination, field_operand, scratch);
#endif  // V8_ENABLE_SANDBOX
}

void MacroAssembler::LoadTrustedPointerField(Register destination,
                                             MemOperand field_operand,
                                             IndirectPointerTag tag,
                                             Register scratch) {
#ifdef V8_ENABLE_SANDBOX
  LoadIndirectPointerField(destination, field_operand, tag, scratch);
#else
  LoadTaggedField(destination, field_operand, scratch);
#endif
}

void MacroAssembler::StoreTrustedPointerField(Register value,
                                              MemOperand dst_field_operand,
                                              Register scratch) {
#ifdef V8_ENABLE_SANDBOX
  StoreIndirectPointerField(value, dst_field_operand, scratch);
#else
  StoreTaggedField(value, dst_field_operand, scratch);
#endif
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
    CompareInstanceTypeRange(scratch, scratch, r0, FIRST_JS_RECEIVER_TYPE,
                             LAST_JS_RECEIVER_TYPE);
    ble(&ok);
    LoadMap(scratch, heap_object);
    CompareInstanceTypeRange(scratch, scratch, r0,
                             FIRST_PRIMITIVE_HEAP_OBJECT_TYPE,
                             LAST_PRIMITIVE_HEAP_OBJECT_TYPE);
    ble(&ok);
    Abort(AbortReason::kInvalidReceiver);
    bind(&ok);
#endif  // DEBUG

    // All primitive object's maps are allocated at the start of the read only
    // heap. Thus JS_RECEIVER's must have maps with larger (compressed)
    // addresses.
    UseScratchRegisterScope temps(this);
    Register scratch2 = temps.Acquire();
    DCHECK(!AreAliased(scratch2, scratch));
    LoadCompressedMap(scratch, heap_object, scratch2);
    mov(scratch2, Operand(InstanceTypeChecker::kNonJsReceiverMapLimit));
    CompareTagged(scratch, scratch2);
  } else {
    static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
    CompareObjectType(heap_object, scratch, scratch, FIRST_JS_RECEIVER_TYPE);
  }
  b(to_condition(cc), target);
}

void MacroAssembler::LoadIndirectPointerField(Register destination,
                                              MemOperand field_operand,
                                              IndirectPointerTag tag,
                                              Register scratch) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  Register handle = scratch;
  DCHECK(!AreAliased(handle, destination));
  LoadU32(handle, field_operand, scratch);
  ResolveIndirectPointerHandle(destination, handle, tag, scratch);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

void MacroAssembler::StoreIndirectPointerField(Register value,
                                               MemOperand dst_field_operand,
                                               Register scratch) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  DCHECK(!AreAliased(scratch, scratch2));
  LoadU32(
      scratch2,
      FieldMemOperand(value, ExposedTrustedObject::kSelfIndirectPointerOffset),
      scratch);
  StoreU32(scratch2, dst_field_operand, scratch);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

#ifdef V8_ENABLE_SANDBOX
void MacroAssembler::ResolveIndirectPointerHandle(Register destination,
                                                  Register handle,
                                                  IndirectPointerTag tag,
                                                  Register scratch) {
  // Pointer resolution will fail in several paths if handle == ra
  DCHECK(!AreAliased(handle, r0));

  // The tag implies which pointer table to use.
  if (tag == kUnknownIndirectPointerTag) {
    // In this case we have to rely on the handle marking to determine which
    // pointer table to use.
    Label is_trusted_pointer_handle, done;
    mov(scratch, Operand(kCodePointerHandleMarker));
    AndU64(scratch, handle, scratch, SetRC);
    beq(&is_trusted_pointer_handle, cr0);
    ResolveCodePointerHandle(destination, handle, scratch);
    b(&done);
    bind(&is_trusted_pointer_handle);
    ResolveTrustedPointerHandle(destination, handle, kUnknownIndirectPointerTag,
                                scratch);
    bind(&done);
  } else if (tag == kCodeIndirectPointerTag) {
    ResolveCodePointerHandle(destination, handle, scratch);
  } else {
    ResolveTrustedPointerHandle(destination, handle, tag, scratch);
  }
}

void MacroAssembler::ResolveTrustedPointerHandle(Register destination,
                                                 Register handle,
                                                 IndirectPointerTag tag,
                                                 Register scratch) {
  DCHECK_NE(tag, kCodeIndirectPointerTag);
  DCHECK(!AreAliased(handle, destination));

  CHECK(root_array_available_);
  Register table = destination;
  Move(table, ExternalReference::trusted_pointer_table_base_address(isolate()));
  ShiftRightU64(handle, handle, Operand(kTrustedPointerHandleShift));
  ShiftLeftU64(handle, handle, Operand(kTrustedPointerTableEntrySizeLog2));
  LoadU64(destination, MemOperand(table, handle), scratch);
  // The LSB is used as marking bit by the trusted pointer table, so here we
  // have to set it using a bitwise OR as it may or may not be set.
  mov(handle, Operand(kHeapObjectTag));
  OrU64(destination, destination, handle);
}

void MacroAssembler::ResolveCodePointerHandle(Register destination,
                                              Register handle,
                                              Register scratch) {
  DCHECK(!AreAliased(handle, destination));

  Register table = destination;
  Move(table, ExternalReference::code_pointer_table_address());
  ShiftRightU64(handle, handle, Operand(kCodePointerHandleShift));
  ShiftLeftU64(handle, handle, Operand(kCodePointerTableEntrySizeLog2));
  AddS64(handle, table, handle);
  LoadU64(destination,
          MemOperand(handle, kCodePointerTableEntryCodeObjectOffset), scratch);
  // The LSB is used as marking bit by the code pointer table, so here we have
  // to set it using a bitwise OR as it may or may not be set.
  mov(handle, Operand(kHeapObjectTag));
  OrU64(destination, destination, handle);
}

void MacroAssembler::LoadCodeEntrypointViaCodePointer(Register destination,
                                                      MemOperand field_operand,
                                                      Register scratch) {
  ASM_CODE_COMMENT(this);

  // Due to register pressure, table is also used as a scratch register
  DCHECK(destination != r0);
  Register table = scratch;
  LoadU32(destination, field_operand, scratch);
  Move(table, ExternalReference::code_pointer_table_address());
  // TODO(tpearson): can the offset computation be done more efficiently?
  ShiftRightU64(destination, destination, Operand(kCodePointerHandleShift));
  ShiftLeftU64(destination, destination,
               Operand(kCodePointerTableEntrySizeLog2));
  LoadU64(destination, MemOperand(destination, table));
}
#endif  // V8_ENABLE_SANDBOX

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
  DCHECK(!AreAliased(object, slot_address));
  RegList registers =
      WriteBarrierDescriptor::ComputeSavedRegisters(object, slot_address);
  MaybeSaveRegisters(registers);

  Register object_parameter = WriteBarrierDescriptor::ObjectRegister();
  Register slot_address_parameter =
      WriteBarrierDescriptor::SlotAddressRegister();

  // TODO(tpearson): The following is equivalent to
  // MovePair(slot_address_parameter, slot_address, object_parameter, object);
  // Implement with MoveObjectAndSlot()
  push(object);
  push(slot_address);
  pop(slot_address_parameter);
  pop(object_parameter);

  CallBuiltin(Builtins::EphemeronKeyBarrier(fp_mode));
  MaybeRestoreRegisters(registers);
}

void MacroAssembler::CallIndirectPointerBarrier(Register object,
                                                Register slot_address,
                                                SaveFPRegsMode fp_mode,
                                                IndirectPointerTag tag) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, slot_address));
  RegList registers =
      IndirectPointerWriteBarrierDescriptor::ComputeSavedRegisters(
          object, slot_address);
  MaybeSaveRegisters(registers);

  Register object_parameter =
      IndirectPointerWriteBarrierDescriptor::ObjectRegister();
  Register slot_address_parameter =
      IndirectPointerWriteBarrierDescriptor::SlotAddressRegister();
  Register tag_parameter =
      IndirectPointerWriteBarrierDescriptor::IndirectPointerTagRegister();
  DCHECK(!AreAliased(object_parameter, slot_address_parameter, tag_parameter));

  // TODO(tpearson): The following is equivalent to
  // MovePair(slot_address_parameter, slot_address, object_parameter, object);
  // Implement with MoveObjectAndSlot()
  push(object);
  push(slot_address);
  pop(slot_address_parameter);
  pop(object_parameter);

  mov(tag_parameter, Operand(tag));

  CallBuiltin(Builtins::IndirectPointerBarrier(fp_mode));
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
  // Use CallRecordWriteStubSaveRegisters if the object and slot registers
  // need to be caller saved.
  DCHECK_EQ(WriteBarrierDescriptor::ObjectRegister(), object);
  DCHECK_EQ(WriteBarrierDescriptor::SlotAddressRegister(), slot_address);
#if V8_ENABLE_WEBASSEMBLY
  if (mode == StubCallMode::kCallWasmRuntimeStub) {
    // Use {near_call} for direct Wasm call within a module.
    auto wasm_target =
        static_cast<Address>(wasm::WasmCode::GetRecordWriteBuiltin(fp_mode));
    Call(wasm_target, RelocInfo::WASM_STUB_CALL);
#else
  if (false) {
#endif
  } else {
    CallBuiltin(Builtins::RecordWrite(fp_mode), al);
  }
}

// Will clobber 4 registers: object, address, scratch, ip.  The
// register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWrite(Register object, Register slot_address,
                                 Register value, LinkRegisterStatus lr_status,
                                 SaveFPRegsMode fp_mode, SmiCheck smi_check,
                                 SlotDescriptor slot) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(object, value, slot_address));
  if (v8_flags.debug_code) {
    Register value_check = r0;
    // TODO(tpearson): Figure out why ScratchRegisterScope returns a
    // register that is aliased with one of our other in-use registers
    // For now, use r11 (kScratchReg in the code generator)
    Register scratch = r11;
    ASM_CODE_COMMENT_STRING(this, "Verify slot_address");
    DCHECK(!AreAliased(object, value, value_check, scratch));
    if (slot.contains_indirect_pointer()) {
      LoadIndirectPointerField(value_check, MemOperand(slot_address),
                               slot.indirect_pointer_tag(), scratch);
    } else {
      DCHECK(slot.contains_direct_pointer());
      LoadTaggedField(value_check, MemOperand(slot_address));
    }
    CmpS64(value_check, value);
    Check(eq, AbortReason::kWrongAddressOrValuePassedToRecordWrite);
  }

  if (v8_flags.disable_write_barriers) {
    return;
  }

  // First, check if a write barrier is even needed. The tests below
  // catch stores of smis and stores into the young generation.
  Label done;

  if (smi_check == SmiCheck::kInline) {
    JumpIfSmi(value, &done);
  }

  CheckPageFlag(value,
                value,  // Used as scratch.
                MemoryChunk::kPointersToHereAreInterestingMask, eq, &done);
  CheckPageFlag(object,
                value,  // Used as scratch.
                MemoryChunk::kPointersFromHereAreInterestingMask, eq, &done);

  // Record the actual write.
  if (lr_status == kLRHasNotBeenSaved) {
    mflr(r0);
    push(r0);
  }
  if (slot.contains_direct_pointer()) {
    CallRecordWriteStubSaveRegisters(object, slot_address, fp_mode,
                                     StubCallMode::kCallBuiltinPointer);
  } else {
    DCHECK(slot.contains_indirect_pointer());
    CallIndirectPointerBarrier(object, slot_address, fp_mode,
                               slot.indirect_pointer_tag());
  }
  if (lr_status == kLRHasNotBeenSaved) {
    pop(r0);
    mtlr(r0);
  }

  if (v8_flags.debug_code) mov(slot_address, Operand(kZapValue));

  bind(&done);

  // Clobber clobbered registers when running with the debug-code flag
  // turned on to provoke errors.
  if (v8_flags.debug_code) {
    mov(slot_address, Operand(base::bit_cast<intptr_t>(kZapValue + 12)));
    mov(value, Operand(base::bit_cast<intptr_t>(kZapValue + 16)));
  }
}

void MacroAssembler::PushCommonFrame(Register marker_reg) {
  int fp_delta = 0;
  mflr(r0);
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    if (marker_reg.is_valid()) {
      Push(r0, fp, kConstantPoolRegister, marker_reg);
      fp_delta = 2;
    } else {
      Push(r0, fp, kConstantPoolRegister);
      fp_delta = 1;
    }
  } else {
    if (marker_reg.is_valid()) {
      Push(r0, fp, marker_reg);
      fp_delta = 1;
    } else {
      Push(r0, fp);
      fp_delta = 0;
    }
  }
  addi(fp, sp, Operand(fp_delta * kSystemPointerSize));
}

void MacroAssembler::PushStandardFrame(Register function_reg) {
  int fp_delta = 0;
  mflr(r0);
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    if (function_reg.is_valid()) {
      Push(r0, fp, kConstantPoolRegister, cp, function_reg);
      fp_delta = 3;
    } else {
      Push(r0, fp, kConstantPoolRegister, cp);
      fp_delta = 2;
    }
  } else {
    if (function_reg.is_valid()) {
      Push(r0, fp, cp, function_reg);
      fp_delta = 2;
    } else {
      Push(r0, fp, cp);
      fp_delta = 1;
    }
  }
  addi(fp, sp, Operand(fp_delta * kSystemPointerSize));
  Push(kJavaScriptCallArgCountRegister);
}

void MacroAssembler::RestoreFrameStateForTailCall() {
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    LoadU64(kConstantPoolRegister,
            MemOperand(fp, StandardFrameConstants::kConstantPoolOffset));
    set_constant_pool_available(false);
  }
  LoadU64(r0, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
  LoadU64(fp, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  mtlr(r0);
}

void MacroAssembler::CanonicalizeNaN(const DoubleRegister dst,
                                     const DoubleRegister src) {
  // Turn potential sNaN into qNaN.
  fsub(dst, src, kDoubleRegZero);
}

void MacroAssembler::ConvertIntToDouble(Register src, DoubleRegister dst) {
  MovIntToDouble(dst, src, r0);
  fcfid(dst, dst);
}

void MacroAssembler::ConvertUnsignedIntToDouble(Register src,
                                                DoubleRegister dst) {
  MovUnsignedIntToDouble(dst, src, r0);
  fcfid(dst, dst);
}

void MacroAssembler::ConvertIntToFloat(Register src, DoubleRegister dst) {
  MovIntToDouble(dst, src, r0);
  fcfids(dst, dst);
}

void MacroAssembler::ConvertUnsignedIntToFloat(Register src,
                                               DoubleRegister dst) {
  MovUnsignedIntToDouble(dst, src, r0);
  fcfids(dst, dst);
}

#if V8_TARGET_ARCH_PPC64
void MacroAssembler::ConvertInt64ToDouble(Register src,
                                          DoubleRegister double_dst) {
  MovInt64ToDouble(double_dst, src);
  fcfid(double_dst, double_dst);
}

void MacroAssembler::ConvertUnsignedInt64ToFloat(Register src,
                                                 DoubleRegister double_dst) {
  MovInt64ToDouble(double_dst, src);
  fcfidus(double_dst, double_dst);
}

void MacroAssembler::ConvertUnsignedInt64ToDouble(Register src,
                                                  DoubleRegister double_dst) {
  MovInt64ToDouble(double_dst, src);
  fcfidu(double_dst, double_dst);
}

void MacroAssembler::ConvertInt64ToFloat(Register src,
                                         DoubleRegister double_dst) {
  MovInt64ToDouble(double_dst, src);
  fcfids(double_dst, double_dst);
}
#endif

void MacroAssembler::ConvertDoubleToInt64(const DoubleRegister double_input,
#if !V8_TARGET_ARCH_PPC64
                                          const Register dst_hi,
#endif
                                          const Register dst,
                                          const DoubleRegister double_dst,
                                          FPRoundingMode rounding_mode) {
  if (rounding_mode == kRoundToZero) {
    fctidz(double_dst, double_input);
  } else {
    SetRoundingMode(rounding_mode);
    fctid(double_dst, double_input);
    ResetRoundingMode();
  }

  MovDoubleToInt64(
#if !V8_TARGET_ARCH_PPC64
      dst_hi,
#endif
      dst, double_dst);
}

#if V8_TARGET_ARCH_PPC64
void MacroAssembler::ConvertDoubleToUnsignedInt64(
    const DoubleRegister double_input, const Register dst,
    const DoubleRegister double_dst, FPRoundingMode rounding_mode) {
  if (rounding_mode == kRoundToZero) {
    fctiduz(double_dst, double_input);
  } else {
    SetRoundingMode(rounding_mode);
    fctidu(double_dst, double_input);
    ResetRoundingMode();
  }

  MovDoubleToInt64(dst, double_dst);
}
#endif

#if !V8_TARGET_ARCH_PPC64
void MacroAssembler::ShiftLeftPair(Register dst_low, Register dst_high,
                                   Register src_low, Register src_high,
                                   Register scratch, Register shift) {
  DCHECK(!AreAliased(dst_low, src_high));
  DCHECK(!AreAliased(dst_high, src_low));
  DCHECK(!AreAliased(dst_low, dst_high, shift));
  Label less_than_32;
  Label done;
  cmpi(shift, Operand(32));
  blt(&less_than_32);
  // If shift >= 32
  andi(scratch, shift, Operand(0x1F));
  ShiftLeftU32(dst_high, src_low, scratch);
  li(dst_low, Operand::Zero());
  b(&done);
  bind(&less_than_32);
  // If shift < 32
  subfic(scratch, shift, Operand(32));
  ShiftLeftU32(dst_high, src_high, shift);
  srw(scratch, src_low, scratch);
  orx(dst_high, dst_high, scratch);
  ShiftLeftU32(dst_low, src_low, shift);
  bind(&done);
}

void MacroAssembler::ShiftLeftPair(Register dst_low, Register dst_high,
                                   Register src_low, Register src_high,
                                   uint32_t shift) {
  DCHECK(!AreAliased(dst_low, src_high));
  DCHECK(!AreAliased(dst_high, src_low));
  if (shift == 32) {
    Move(dst_high, src_low);
    li(dst_low, Operand::Zero());
  } else if (shift > 32) {
    shift &= 0x1F;
    ShiftLeftU32(dst_high, src_low, Operand(shift));
    li(dst_low, Operand::Zero());
  } else if (shift == 0) {
    Move(dst_low, src_low);
    Move(dst_high, src_high);
  } else {
    ShiftLeftU32(dst_high, src_high, Operand(shift));
    rlwimi(dst_high, src_low, shift, 32 - shift, 31);
    ShiftLeftU32(dst_low, src_low, Operand(shift));
  }
}

void MacroAssembler::ShiftRightPair(Register dst_low, Register dst_high,
                                    Register src_low, Register src_high,
                                    Register scratch, Register shift) {
  DCHECK(!AreAliased(dst_low, src_high));
  DCHECK(!AreAliased(dst_high, src_low));
  DCHECK(!AreAliased(dst_low, dst_high, shift));
  Label less_than_32;
  Label done;
  cmpi(shift, Operand(32));
  blt(&less_than_32);
  // If shift >= 32
  andi(scratch, shift, Operand(0x1F));
  srw(dst_low, src_high, scratch);
  li(dst_high, Operand::Zero());
  b(&done);
  bind(&less_than_32);
  // If shift < 32
  subfic(scratch, shift, Operand(32));
  srw(dst_low, src_low, shift);
  ShiftLeftU32(scratch, src_high, scratch);
  orx(dst_low, dst_low, scratch);
  srw(dst_high, src_high, shift);
  bind(&done);
}

void MacroAssembler::ShiftRightPair(Register dst_low, Register dst_high,
                                    Register src_low, Register src_high,
                                    uint32_t shift) {
  DCHECK(!AreAliased(dst_low, src_high));
  DCHECK(!AreAliased(dst_high, src_low));
  if (shift == 32) {
    Move(dst_low, src_high);
    li(dst_high, Operand::Zero());
  } else if (shift > 32) {
    shift &= 0x1F;
    srwi(dst_low, src_high, Operand(shift));
    li(dst_high, Operand::Zero());
  } else if (shift == 0) {
    Move(dst_low, src_low);
    Move(dst_high, src_high);
  } else {
    srwi(dst_low, src_low, Operand(shift));
    rlwimi(dst_low, src_high, 32 - shift, 0, shift - 1);
    srwi(dst_high, src_high, Operand(shift));
  }
}

void MacroAssembler::ShiftRightAlgPair(Register dst_low, Register dst_high,
                                       Register src_low, Register src_high,
                                       Register scratch, Register shift) {
  DCHECK(!AreAliased(dst_low, src_high, shift));
  DCHECK(!AreAliased(dst_high, src_low, shift));
  Label less_than_32;
  Label done;
  cmpi(shift, Operand(32));
  blt(&less_than_32);
  // If shift >= 32
  andi(scratch, shift, Operand(0x1F));
  sraw(dst_low, src_high, scratch);
  srawi(dst_high, src_high, 31);
  b(&done);
  bind(&less_than_32);
  // If shift < 32
  subfic(scratch, shift, Operand(32));
  srw(dst_low, src_low, shift);
  ShiftLeftU32(scratch, src_high, scratch);
  orx(dst_low, dst_low, scratch);
  sraw(dst_high, src_high, shift);
  bind(&done);
}

void MacroAssembler::ShiftRightAlgPair(Register dst_low, Register dst_high,
                                       Register src_low, Register src_high,
                                       uint32_t shift) {
  DCHECK(!AreAliased(dst_low, src_high));
  DCHECK(!AreAliased(dst_high, src_low));
  if (shift == 32) {
    Move(dst_low, src_high);
    srawi(dst_high, src_high, 31);
  } else if (shift > 32) {
    shift &= 0x1F;
    srawi(dst_low, src_high, shift);
    srawi(dst_high, src_high, 31);
  } else if (shift == 0) {
    Move(dst_low, src_low);
    Move(dst_high, src_high);
  } else {
    srwi(dst_low, src_low, Operand(shift));
    rlwimi(dst_low, src_high, 32 - shift, 0, shift - 1);
    srawi(dst_high, src_high, shift);
  }
}
#endif

void MacroAssembler::LoadConstantPoolPointerRegisterFromCodeTargetAddress(
    Register code_target_address, Register scratch1, Register scratch2) {
  // Builtins do not use the constant pool (see is_constant_pool_available).
  static_assert(InstructionStream::kOnHeapBodyIsContiguous);

#ifdef V8_ENABLE_SANDBOX
  LoadCodeEntrypointViaCodePointer(
      scratch2,
      FieldMemOperand(code_target_address, Code::kSelfIndirectPointerOffset),
      scratch1);
#else
  LoadU64(scratch2,
          FieldMemOperand(code_target_address, Code::kInstructionStartOffset),
          scratch1);
#endif
  LoadU32(scratch1,
          FieldMemOperand(code_target_address, Code::kInstructionSizeOffset),
          scratch1);
  add(scratch2, scratch1, scratch2);
  LoadU32(kConstantPoolRegister,
          FieldMemOperand(code_target_address, Code::kConstantPoolOffsetOffset),
          scratch1);
  add(kConstantPoolRegister, scratch2, kConstantPoolRegister);
}

void MacroAssembler::LoadPC(Register dst) {
  b(4, SetLK);
  mflr(dst);
}

void MacroAssembler::ComputeCodeStartAddress(Register dst) {
  mflr(r0);
  LoadPC(dst);
  subi(dst, dst, Operand(pc_offset() - kInstrSize));
  mtlr(r0);
}

void MacroAssembler::LoadConstantPoolPointerRegister() {
  //
  // Builtins do not use the constant pool (see is_constant_pool_available).
  static_assert(InstructionStream::kOnHeapBodyIsContiguous);

  LoadPC(kConstantPoolRegister);
  int32_t delta = -pc_offset() + 4;
  add_label_offset(kConstantPoolRegister, kConstantPoolRegister,
                   ConstantPoolPosition(), delta);
}

void MacroAssembler::StubPrologue(StackFrame::Type type) {
  {
    ConstantPoolUnavailableScope constant_pool_unavailable(this);
    mov(r11, Operand(StackFrame::TypeToMarker(type)));
    PushCommonFrame(r11);
  }
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    LoadConstantPoolPointerRegister();
    set_constant_pool_available(true);
  }
}

void MacroAssembler::Prologue() {
  PushStandardFrame(r4);
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    // base contains prologue address
    LoadConstantPoolPointerRegister();
    set_constant_pool_available(true);
  }
}

void MacroAssembler::DropArguments(Register count) {
  ShiftLeftU64(ip, count, Operand(kSystemPointerSizeLog2));
  add(sp, sp, ip);
}

void MacroAssembler::DropArgumentsAndPushNewReceiver(Register argc,
                                                     Register receiver) {
  DCHECK(!AreAliased(argc, receiver));
  DropArguments(argc);
  push(receiver);
}

void MacroAssembler::EnterFrame(StackFrame::Type type,
                                bool load_constant_pool_pointer_reg) {
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL && load_constant_pool_pointer_reg) {
    // Push type explicitly so we can leverage the constant pool.
    // This path cannot rely on ip containing code entry.
    PushCommonFrame();
    LoadConstantPoolPointerRegister();
    if (!StackFrame::IsJavaScript(type)) {
      mov(ip, Operand(StackFrame::TypeToMarker(type)));
      push(ip);
    }
  } else {
    Register scratch = no_reg;
    if (!StackFrame::IsJavaScript(type)) {
      scratch = ip;
      mov(scratch, Operand(StackFrame::TypeToMarker(type)));
    }
    PushCommonFrame(scratch);
  }
#if V8_ENABLE_WEBASSEMBLY
  if (type == StackFrame::WASM) Push(kWasmInstanceRegister);
#endif  // V8_ENABLE_WEBASSEMBLY
}

int MacroAssembler::LeaveFrame(StackFrame::Type type, int stack_adjustment) {
  ConstantPoolUnavailableScope constant_pool_unavailable(this);
  // r3: preserved
  // r4: preserved
  // r5: preserved

  // Drop the execution stack down to the frame pointer and restore
  // the caller's state.
  int frame_ends;
  LoadU64(r0, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
  LoadU64(ip, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    LoadU64(kConstantPoolRegister,
            MemOperand(fp, StandardFrameConstants::kConstantPoolOffset));
  }
  mtlr(r0);
  frame_ends = pc_offset();
  AddS64(sp, fp,
         Operand(StandardFrameConstants::kCallerSPOffset + stack_adjustment),
         r0);
  mr(fp, ip);
  return frame_ends;
}

// ExitFrame layout (probably wrongish.. needs updating)
//
//  SP -> previousSP
//        LK reserved
//        sp_on_exit (for debug?)
// oldSP->prev SP
//        LK
//        <parameters on stack>

// Prior to calling EnterExitFrame, we've got a bunch of parameters
// on the stack that we need to wrap a real frame around.. so first
// we reserve a slot for LK and push the previous SP which is captured
// in the fp register (r31)
// Then - we buy a new frame

void MacroAssembler::EnterExitFrame(Register scratch, int stack_space,
                                    StackFrame::Type frame_type) {
  DCHECK(frame_type == StackFrame::EXIT ||
         frame_type == StackFrame::BUILTIN_EXIT ||
         frame_type == StackFrame::API_ACCESSOR_EXIT ||
         frame_type == StackFrame::API_CALLBACK_EXIT);

  using ER = ExternalReference;

  // Set up the frame structure on the stack.
  DCHECK_EQ(2 * kSystemPointerSize, ExitFrameConstants::kCallerSPDisplacement);
  DCHECK_EQ(1 * kSystemPointerSize, ExitFrameConstants::kCallerPCOffset);
  DCHECK_EQ(0 * kSystemPointerSize, ExitFrameConstants::kCallerFPOffset);

  // This is an opportunity to build a frame to wrap
  // all of the pushes that have happened inside of V8
  // since we were called from C code

  mov(ip, Operand(StackFrame::TypeToMarker(frame_type)));
  PushCommonFrame(ip);
  // Reserve room for saved entry sp.
  subi(sp, fp, Operand(ExitFrameConstants::kFixedFrameSizeFromFp));

  if (v8_flags.debug_code) {
    li(r8, Operand::Zero());
    StoreU64(r8, MemOperand(fp, ExitFrameConstants::kSPOffset));
  }
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
    StoreU64(kConstantPoolRegister,
             MemOperand(fp, ExitFrameConstants::kConstantPoolOffset));
  }

  // Save the frame pointer and the context in top.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  StoreU64(fp, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));

  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  StoreU64(cp, ExternalReferenceAsOperand(context_address, no_reg));

  AddS64(sp, sp, Operand(-(stack_space + 1) * kSystemPointerSize));

  // Allocate and align the frame preparing for calling the runtime
  // function.
  const int frame_alignment = ActivationFrameAlignment();
  if (frame_alignment > kSystemPointerSize) {
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    ClearRightImm(sp, sp,
                  Operand(base::bits::WhichPowerOfTwo(frame_alignment)));
  }
  li(r0, Operand::Zero());
  StoreU64WithUpdate(
      r0, MemOperand(sp, -kNumRequiredStackFrameSlots * kSystemPointerSize));

  // Set the exit frame sp value to point just before the return address
  // location.
  AddS64(r8, sp, Operand((kStackFrameExtraParamSlot + 1) * kSystemPointerSize),
         r0);
  StoreU64(r8, MemOperand(fp, ExitFrameConstants::kSPOffset));
}

int MacroAssembler::ActivationFrameAlignment() {
#if !defined(USE_SIMULATOR)
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one PPC
  // platform for another PPC platform with a different alignment.
  return base::OS::ActivationFrameAlignment();
#else  // Simulated
  // If we are using the simulator then we should always align to the expected
  // alignment. As the simulator is used to generate snapshots we do not know
  // if the target platform will need alignment, so this is controlled from a
  // flag.
  return v8_flags.sim_stack_alignment;
#endif
}

void MacroAssembler::LeaveExitFrame(Register scratch) {
  ConstantPoolUnavailableScope constant_pool_unavailable(this);

  using ER = ExternalReference;

  // Restore current context from top and clear it in debug mode.
  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  LoadU64(cp, ExternalReferenceAsOperand(context_address, no_reg));

#ifdef DEBUG
  mov(scratch, Operand(Context::kInvalidContext));
  StoreU64(scratch, ExternalReferenceAsOperand(context_address, no_reg));
#endif

  // Clear the top frame.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  mov(scratch, Operand::Zero());
  StoreU64(scratch, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));

  // Tear down the exit frame, pop the arguments, and return.
  LeaveFrame(StackFrame::EXIT);
}

void MacroAssembler::MovFromFloatResult(const DoubleRegister dst) {
  Move(dst, d1);
}

void MacroAssembler::MovFromFloatParameter(const DoubleRegister dst) {
  Move(dst, d1);
}

void MacroAssembler::LoadStackLimit(Register destination, StackLimitKind kind,
                                    Register scratch) {
  DCHECK(root_array_available());
  intptr_t offset = kind == StackLimitKind::kRealStackLimit
                        ? IsolateData::real_jslimit_offset()
                        : IsolateData::jslimit_offset();
  CHECK(is_int32(offset));
  LoadU64(destination, MemOperand(kRootRegister, offset), scratch);
}

void MacroAssembler::StackOverflowCheck(Register num_args, Register scratch,
                                        Label* stack_overflow) {
  // Check the stack for overflow. We are not trying to catch
  // interruptions (e.g. debug break and preemption) here, so the "real stack
  // limit" is checked.
  LoadStackLimit(scratch, StackLimitKind::kRealStackLimit, r0);
  // Make scratch the space we have left. The stack might already be overflowed
  // here which will cause scratch to become negative.
  sub(scratch, sp, scratch);
  // Check if the arguments will overflow the stack.
  ShiftLeftU64(r0, num_args, Operand(kSystemPointerSizeLog2));
  CmpS64(scratch, r0);
  ble(stack_overflow);  // Signed comparison.
}

void MacroAssembler::InvokePrologue(Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    Label* done, InvokeType type) {
  Label regular_invoke;

  //  r3: actual arguments count
  //  r4: function (passed through to callee)
  //  r5: expected arguments count

  DCHECK_EQ(actual_parameter_count, r3);
  DCHECK_EQ(expected_parameter_count, r5);

  // If overapplication or if the actual argument count is equal to the
  // formal parameter count, no need to push extra undefined values.
  sub(expected_parameter_count, expected_parameter_count,
      actual_parameter_count, LeaveOE, SetRC);
  ble(&regular_invoke, cr0);

  Label stack_overflow;
  Register scratch = r7;
  StackOverflowCheck(expected_parameter_count, scratch, &stack_overflow);

  // Underapplication. Move the arguments already in the stack, including the
  // receiver and the return address.
  {
    Label copy, skip;
    Register src = r9, dest = r8;
    addi(src, sp, Operand(-kSystemPointerSize));
    ShiftLeftU64(r0, expected_parameter_count, Operand(kSystemPointerSizeLog2));
    sub(sp, sp, r0);
    // Update stack pointer.
    addi(dest, sp, Operand(-kSystemPointerSize));
    mr(r0, actual_parameter_count);
    cmpi(r0, Operand::Zero());
    ble(&skip);
    mtctr(r0);

    bind(&copy);
    LoadU64WithUpdate(r0, MemOperand(src, kSystemPointerSize));
    StoreU64WithUpdate(r0, MemOperand(dest, kSystemPointerSize));
    bdnz(&copy);
    bind(&skip);
  }

  // Fill remaining expected arguments with undefined values.
  LoadRoot(scratch, RootIndex::kUndefinedValue);
  {
    mtctr(expected_parameter_count);

    Label loop;
    bind(&loop);
    StoreU64WithUpdate(scratch, MemOperand(r8, kSystemPointerSize));
    bdnz(&loop);
  }
  b(&regular_invoke);

  bind(&stack_overflow);
  {
    FrameScope frame(
        this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);
    CallRuntime(Runtime::kThrowStackOverflow);
    bkpt(0);
  }

  bind(&regular_invoke);
}

void MacroAssembler::CheckDebugHook(Register fun, Register new_target,
                                    Register expected_parameter_count,
                                    Register actual_parameter_count) {
  Label skip_hook;

  ExternalReference debug_hook_active =
      ExternalReference::debug_hook_on_function_call_address(isolate());
  Move(r7, debug_hook_active);
  LoadU8(r7, MemOperand(r7), r0);
  extsb(r7, r7);
  CmpSmiLiteral(r7, Smi::zero(), r0);
  beq(&skip_hook);

  {
    // Load receiver to pass it later to DebugOnFunctionCall hook.
    LoadReceiver(r7);
    FrameScope frame(
        this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);

    SmiTag(expected_parameter_count);
    Push(expected_parameter_count);

    SmiTag(actual_parameter_count);
    Push(actual_parameter_count);

    if (new_target.is_valid()) {
      Push(new_target);
    }
    Push(fun, fun, r7);
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
  DCHECK_EQ(function, r4);
  DCHECK_IMPLIES(new_target.is_valid(), new_target == r6);

  // On function call, call into the debugger if necessary.
  CheckDebugHook(function, new_target, expected_parameter_count,
                 actual_parameter_count);

  // Clear the new.target register if not given.
  if (!new_target.is_valid()) {
    LoadRoot(r6, RootIndex::kUndefinedValue);
  }

  Label done;
  InvokePrologue(expected_parameter_count, actual_parameter_count, &done, type);
  // We call indirectly through the code field in the function to
  // allow recompilation to take effect without changing any of the
  // call sites.
  switch (type) {
    case InvokeType::kCall:
      CallJSFunction(function, r0);
      break;
    case InvokeType::kJump:
      JumpJSFunction(function, r0);
      break;
  }

    // Continue here if InvokePrologue does handle the invocation due to
    // mismatched parameter counts.
    bind(&done);
}

void MacroAssembler::InvokeFunctionWithNewTarget(
    Register fun, Register new_target, Register actual_parameter_count,
    InvokeType type) {
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());

  // Contract with called JS functions requires that function is passed in r4.
  DCHECK_EQ(fun, r4);

  Register expected_reg = r5;
  Register temp_reg = r7;

  LoadTaggedField(
      temp_reg, FieldMemOperand(r4, JSFunction::kSharedFunctionInfoOffset), r0);
  LoadTaggedField(cp, FieldMemOperand(r4, JSFunction::kContextOffset), r0);
  LoadU16(expected_reg,
          FieldMemOperand(temp_reg,
                          SharedFunctionInfo::kFormalParameterCountOffset));

  InvokeFunctionCode(fun, new_target, expected_reg, actual_parameter_count,
                     type);
}

void MacroAssembler::InvokeFunction(Register function,
                                    Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    InvokeType type) {
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());

  // Contract with called JS functions requires that function is passed in r4.
  DCHECK_EQ(function, r4);

  // Get the function and setup the context.
  LoadTaggedField(cp, FieldMemOperand(r4, JSFunction::kContextOffset), r0);

  InvokeFunctionCode(r4, no_reg, expected_parameter_count,
                     actual_parameter_count, type);
}

void MacroAssembler::PushStackHandler() {
  // Adjust this code if not the case.
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize);
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize);

  Push(Smi::zero());  // Padding.

  // Link the current handler as the next handler.
  // Preserve r4-r8.
  Move(r3,
       ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  LoadU64(r0, MemOperand(r3));
  push(r0);

  // Set this new handler as the current one.
  StoreU64(sp, MemOperand(r3));
}

void MacroAssembler::PopStackHandler() {
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize);
  static_assert(StackHandlerConstants::kNextOffset == 0);

  pop(r4);
  Move(ip,
       ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  StoreU64(r4, MemOperand(ip));

  Drop(1);  // Drop padding.
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
  CHECK(scratch != Register::no_reg() || temps.CanAcquire());
  if (scratch == Register::no_reg()) {
    // TODO(tpearson): Figure out why ScratchRegisterScope returns a
    // register that is aliased with one of our other in-use registers
    // For now, use r11 (kScratchReg in the code generator)
    scratch = r11;
    DCHECK_NE(map, scratch);
  }
  mov(scratch, Operand(expected_ptr));
  CompareTagged(map, scratch);
}

void MacroAssembler::IsObjectTypeFast(Register object,
                                      Register compressed_map_scratch,
                                      InstanceType type, Register scratch) {
  ASM_CODE_COMMENT(this);
  CHECK(InstanceTypeChecker::UniqueMapOfInstanceType(type));
  LoadCompressedMap(compressed_map_scratch, object, scratch);
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
    DCHECK((scratch1 != scratch2) || (scratch1 != r0));
    LoadCompressedMap(scratch1, object, scratch1 != scratch2 ? scratch2 : r0);
    CompareInstanceTypeWithUniqueCompressedMap(
        scratch1, scratch1 != scratch2 ? scratch2 : r0, type);
    return;
  }
#endif  // V8_STATIC_ROOTS_BOOL

  CompareObjectType(object, scratch1, scratch2, type);
}

void MacroAssembler::CompareObjectType(Register object, Register map,
                                       Register type_reg, InstanceType type) {
  const Register temp = type_reg == no_reg ? r0 : type_reg;

  LoadMap(map, object);
  CompareInstanceType(map, temp, type);
}

void MacroAssembler::CompareObjectTypeRange(Register object, Register map,
                                            Register type_reg, Register scratch,
                                            InstanceType lower_limit,
                                            InstanceType upper_limit) {
  ASM_CODE_COMMENT(this);
  LoadMap(map, object);
  CompareInstanceTypeRange(map, type_reg, scratch, lower_limit, upper_limit);
}

void MacroAssembler::CompareInstanceType(Register map, Register type_reg,
                                         InstanceType type) {
  static_assert(Map::kInstanceTypeOffset < 4096);
  static_assert(LAST_TYPE <= 0xFFFF);
  lhz(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  cmpi(type_reg, Operand(type));
}

void MacroAssembler::CompareRange(Register value, Register scratch,
                                  unsigned lower_limit, unsigned higher_limit) {
  ASM_CODE_COMMENT(this);
  DCHECK_LT(lower_limit, higher_limit);
  if (lower_limit != 0) {
    mov(scratch, Operand(lower_limit));
    sub(scratch, value, scratch);
    cmpli(scratch, Operand(higher_limit - lower_limit));
  } else {
    mov(scratch, Operand(higher_limit));
    CmpU64(value, scratch);
  }
}

void MacroAssembler::CompareInstanceTypeRange(Register map, Register type_reg,
                                              Register scratch,
                                              InstanceType lower_limit,
                                              InstanceType higher_limit) {
  DCHECK_LT(lower_limit, higher_limit);
  LoadU16(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  CompareRange(type_reg, scratch, lower_limit, higher_limit);
}

void MacroAssembler::CompareTaggedRoot(const Register& obj, RootIndex index) {
  ASM_CODE_COMMENT(this);
  // Use r0 as a safe scratch register here, since temps.Acquire() tends
  // to spit back the register being passed as an argument in obj...
  Register temp = r0;
  DCHECK(!AreAliased(obj, temp));

  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index)) {
    mov(temp, Operand(ReadOnlyRootPtr(index)));
    CompareTagged(obj, temp);
    return;
  }
  // Some smi roots contain system pointer size values like stack limits.
  DCHECK(base::IsInRange(index, RootIndex::kFirstStrongOrReadOnlyRoot,
                         RootIndex::kLastStrongOrReadOnlyRoot));
  LoadRoot(temp, index);
  CompareTagged(obj, temp);
}

void MacroAssembler::CompareRoot(Register obj, RootIndex index) {
  ASM_CODE_COMMENT(this);
  // Use r0 as a safe scratch register here, since temps.Acquire() tends
  // to spit back the register being passed as an argument in obj...
  Register temp = r0;
  if (!base::IsInRange(index, RootIndex::kFirstStrongOrReadOnlyRoot,
                       RootIndex::kLastStrongOrReadOnlyRoot)) {
    // Some smi roots contain system pointer size values like stack limits.
    DCHECK(!AreAliased(obj, temp));
    LoadRoot(temp, index);
    CmpU64(obj, temp);
    return;
  }
  CompareTaggedRoot(obj, index);
}

void MacroAssembler::AddAndCheckForOverflow(Register dst, Register left,
                                            Register right,
                                            Register overflow_dst,
                                            Register scratch) {
  DCHECK(dst != overflow_dst);
  DCHECK(dst != scratch);
  DCHECK(overflow_dst != scratch);
  DCHECK(overflow_dst != left);
  DCHECK(overflow_dst != right);

  bool left_is_right = left == right;
  RCBit xorRC = left_is_right ? SetRC : LeaveRC;

  // C = A+B; C overflows if A/B have same sign and C has diff sign than A
  if (dst == left) {
    mr(scratch, left);                        // Preserve left.
    add(dst, left, right);                    // Left is overwritten.
    xor_(overflow_dst, dst, scratch, xorRC);  // Original left.
    if (!left_is_right) xor_(scratch, dst, right);
  } else if (dst == right) {
    mr(scratch, right);     // Preserve right.
    add(dst, left, right);  // Right is overwritten.
    xor_(overflow_dst, dst, left, xorRC);
    if (!left_is_right) xor_(scratch, dst, scratch);  // Original right.
  } else {
    add(dst, left, right);
    xor_(overflow_dst, dst, left, xorRC);
    if (!left_is_right) xor_(scratch, dst, right);
  }
  if (!left_is_right) and_(overflow_dst, scratch, overflow_dst, SetRC);
}

void MacroAssembler::AddAndCheckForOverflow(Register dst, Register left,
                                            intptr_t right,
                                            Register overflow_dst,
                                            Register scratch) {
  Register original_left = left;
  DCHECK(dst != overflow_dst);
  DCHECK(dst != scratch);
  DCHECK(overflow_dst != scratch);
  DCHECK(overflow_dst != left);

  // C = A+B; C overflows if A/B have same sign and C has diff sign than A
  if (dst == left) {
    // Preserve left.
    original_left = overflow_dst;
    mr(original_left, left);
  }
  AddS64(dst, left, Operand(right), scratch);
  xor_(overflow_dst, dst, original_left);
  if (right >= 0) {
    and_(overflow_dst, overflow_dst, dst, SetRC);
  } else {
    andc(overflow_dst, overflow_dst, dst, SetRC);
  }
}

void MacroAssembler::SubAndCheckForOverflow(Register dst, Register left,
                                            Register right,
                                            Register overflow_dst,
                                            Register scratch) {
  DCHECK(dst != overflow_dst);
  DCHECK(dst != scratch);
  DCHECK(overflow_dst != scratch);
  DCHECK(overflow_dst != left);
  DCHECK(overflow_dst != right);

  // C = A-B; C overflows if A/B have diff signs and C has diff sign than A
  if (dst == left) {
    mr(scratch, left);      // Preserve left.
    sub(dst, left, right);  // Left is overwritten.
    xor_(overflow_dst, dst, scratch);
    xor_(scratch, scratch, right);
    and_(overflow_dst, overflow_dst, scratch, SetRC);
  } else if (dst == right) {
    mr(scratch, right);     // Preserve right.
    sub(dst, left, right);  // Right is overwritten.
    xor_(overflow_dst, dst, left);
    xor_(scratch, left, scratch);
    and_(overflow_dst, overflow_dst, scratch, SetRC);
  } else {
    sub(dst, left, right);
    xor_(overflow_dst, dst, left);
    xor_(scratch, left, right);
    and_(overflow_dst, scratch, overflow_dst, SetRC);
  }
}

void MacroAssembler::MinF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, DoubleRegister scratch) {
  Label check_zero, return_left, return_right, return_nan, done;
  fcmpu(lhs, rhs);
  bunordered(&return_nan);
  if (CpuFeatures::IsSupported(PPC_7_PLUS)) {
    xsmindp(dst, lhs, rhs);
    b(&done);
  }
  beq(&check_zero);
  ble(&return_left);
  b(&return_right);

  bind(&check_zero);
  fcmpu(lhs, kDoubleRegZero);
  /* left == right != 0. */
  bne(&return_left);
  /* At this point, both left and right are either 0 or -0. */
  /* Min: The algorithm is: -((-L) + (-R)), which in case of L and R */
  /* being different registers is most efficiently expressed */
  /* as -((-L) - R). */
  fneg(scratch, lhs);
  if (scratch == rhs) {
    fadd(dst, scratch, rhs);
  } else {
    fsub(dst, scratch, rhs);
  }
  fneg(dst, dst);
  b(&done);

  bind(&return_nan);
  /* If left or right are NaN, fadd propagates the appropriate one.*/
  fadd(dst, lhs, rhs);
  b(&done);

  bind(&return_right);
  if (rhs != dst) {
    fmr(dst, rhs);
  }
  b(&done);

  bind(&return_left);
  if (lhs != dst) {
    fmr(dst, lhs);
  }
  bind(&done);
}

void MacroAssembler::MaxF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, DoubleRegister scratch) {
  Label check_zero, return_left, return_right, return_nan, done;
  fcmpu(lhs, rhs);
  bunordered(&return_nan);
  if (CpuFeatures::IsSupported(PPC_7_PLUS)) {
    xsmaxdp(dst, lhs, rhs);
    b(&done);
  }
  beq(&check_zero);
  bge(&return_left);
  b(&return_right);

  bind(&check_zero);
  fcmpu(lhs, kDoubleRegZero);
  /* left == right != 0. */
  bne(&return_left);
  /* At this point, both left and right are either 0 or -0. */
  fadd(dst, lhs, rhs);
  b(&done);

  bind(&return_nan);
  /* If left or right are NaN, fadd propagates the appropriate one.*/
  fadd(dst, lhs, rhs);
  b(&done);

  bind(&return_right);
  if (rhs != dst) {
    fmr(dst, rhs);
  }
  b(&done);

  bind(&return_left);
  if (lhs != dst) {
    fmr(dst, lhs);
  }
  bind(&done);
}

void MacroAssembler::JumpIfIsInRange(Register value, Register scratch,
                                     unsigned lower_limit,
                                     unsigned higher_limit,
                                     Label* on_in_range) {
  CompareRange(value, scratch, lower_limit, higher_limit);
  ble(on_in_range);
}

void MacroAssembler::TruncateDoubleToI(Isolate* isolate, Zone* zone,
                                       Register result,
                                       DoubleRegister double_input,
                                       StubCallMode stub_mode) {
  Label done;

  TryInlineTruncateDoubleToI(result, double_input, &done);

  // If we fell through then inline version didn't succeed - call stub instead.
  mflr(r0);
  push(r0);
  // Put input on stack.
  stfdu(double_input, MemOperand(sp, -kDoubleSize));

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

  LoadU64(result, MemOperand(sp));
  addi(sp, sp, Operand(kDoubleSize));
  pop(r0);
  mtlr(r0);

  bind(&done);
}

void MacroAssembler::TryInlineTruncateDoubleToI(Register result,
                                                DoubleRegister double_input,
                                                Label* done) {
  DoubleRegister double_scratch = kScratchDoubleReg;
#if !V8_TARGET_ARCH_PPC64
  Register scratch = ip;
#endif

  ConvertDoubleToInt64(double_input,
#if !V8_TARGET_ARCH_PPC64
                       scratch,
#endif
                       result, double_scratch);

// Test for overflow
#if V8_TARGET_ARCH_PPC64
  TestIfInt32(result, r0);
#else
  TestIfInt32(scratch, result, r0);
#endif
  beq(done);
}

namespace {

void TailCallOptimizedCodeSlot(MacroAssembler* masm,
                               Register optimized_code_entry,
                               Register scratch) {
  // ----------- S t a t e -------------
  //  -- r3 : actual argument count
  //  -- r6 : new target (preserved for callee if needed, and caller)
  //  -- r4 : target function (preserved for callee if needed, and caller)
  // -----------------------------------
  DCHECK(!AreAliased(r4, r6, optimized_code_entry, scratch));

  Register closure = r4;
  Label heal_optimized_code_slot;

  // If the optimized code is cleared, go to runtime to update the optimization
  // marker field.
  __ LoadWeakValue(optimized_code_entry, optimized_code_entry,
                   &heal_optimized_code_slot);

  // The entry references a CodeWrapper object. Unwrap it now.
  __ LoadCodePointerField(
      optimized_code_entry,
      FieldMemOperand(optimized_code_entry, CodeWrapper::kCodeOffset), scratch);

  // Check if the optimized code is marked for deopt. If it is, call the
  // runtime to clear it.
  {
    UseScratchRegisterScope temps(masm);
    __ TestCodeIsMarkedForDeoptimization(optimized_code_entry, temps.Acquire(),
                                         scratch);
    __ bne(&heal_optimized_code_slot, cr0);
  }

  // Optimized code is good, get it into the closure and link the closure
  // into the optimized functions list, then tail call the optimized code.
  __ ReplaceClosureCodeWithOptimizedCode(optimized_code_entry, closure, scratch,
                                         r8);
  static_assert(kJavaScriptCallCodeStartRegister == r5, "ABI mismatch");
  __ LoadCodeInstructionStart(r5, optimized_code_entry);
  __ Jump(r5);

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
    CompareObjectType(object, scratch, scratch, FEEDBACK_CELL_TYPE);
    Assert(eq, AbortReason::kExpectedFeedbackCell);
  }
}
void MacroAssembler::AssertFeedbackVector(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    CompareObjectType(object, scratch, scratch, FEEDBACK_VECTOR_TYPE);
    Assert(eq, AbortReason::kExpectedFeedbackVector);
  }
}
#endif  // V8_ENABLE_DEBUG_CODE

// Optimized code is good, get it into the closure and link the closure
// into the optimized functions list, then tail call the optimized code.
void MacroAssembler::ReplaceClosureCodeWithOptimizedCode(
    Register optimized_code, Register closure, Register scratch1,
    Register slot_address) {
  DCHECK(!AreAliased(optimized_code, closure, scratch1, slot_address));
  DCHECK_EQ(closure, kJSFunctionRegister);
  DCHECK(!AreAliased(optimized_code, closure));
  // Store code entry in the closure.
  StoreCodePointerField(optimized_code,
                        FieldMemOperand(closure, JSFunction::kCodeOffset), r0);
  // Write barrier clobbers scratch1 below.
  Register value = scratch1;
  mr(value, optimized_code);

  RecordWriteField(closure, JSFunction::kCodeOffset, value, slot_address,
                   kLRHasNotBeenSaved, SaveFPRegsMode::kIgnore, SmiCheck::kOmit,
                   SlotDescriptor::ForCodePointerSlot());
}

void MacroAssembler::GenerateTailCallToReturnedCode(
    Runtime::FunctionId function_id) {
  // ----------- S t a t e -------------
  //  -- r3 : actual argument count
  //  -- r4 : target function (preserved for callee)
  //  -- r6 : new target (preserved for callee)
  // -----------------------------------
  {
    FrameAndConstantPoolScope scope(this, StackFrame::INTERNAL);
    // Push a copy of the target function, the new target and the actual
    // argument count.
    // Push function as parameter to the runtime call.
    SmiTag(kJavaScriptCallArgCountRegister);
    Push(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
         kJavaScriptCallArgCountRegister, kJavaScriptCallTargetRegister);

    CallRuntime(function_id, 1);
    mr(r5, r3);

    // Restore target function, new target and actual argument count.
    Pop(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
        kJavaScriptCallArgCountRegister);
    SmiUntag(kJavaScriptCallArgCountRegister);
  }
  static_assert(kJavaScriptCallCodeStartRegister == r5, "ABI mismatch");
  JumpCodeObject(r5);
}

// Read off the flags in the feedback vector and check if there
// is optimized code or a tiering state that needs to be processed.
void MacroAssembler::LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
    Register flags, Register feedback_vector, CodeKind current_code_kind,
    Label* flags_need_processing) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(flags, feedback_vector));
  DCHECK(CodeKindCanTierUp(current_code_kind));
  LoadU16(flags,
          FieldMemOperand(feedback_vector, FeedbackVector::kFlagsOffset));
  uint32_t kFlagsMask = FeedbackVector::kFlagsTieringStateIsAnyRequested |
                        FeedbackVector::kFlagsMaybeHasTurbofanCode |
                        FeedbackVector::kFlagsLogNextExecution;
  if (current_code_kind != CodeKind::MAGLEV) {
    kFlagsMask |= FeedbackVector::kFlagsMaybeHasMaglevCode;
  }
  CHECK(is_uint16(kFlagsMask));
  mov(r0, Operand(kFlagsMask));
  AndU32(r0, flags, r0, SetRC);
  bne(flags_need_processing, cr0);
}

void MacroAssembler::OptimizeCodeOrTailCallOptimizedCodeSlot(
    Register flags, Register feedback_vector) {
  DCHECK(!AreAliased(flags, feedback_vector));
  Label maybe_has_optimized_code, maybe_needs_logging;
  // Check if optimized code is available
  TestBitMask(flags, FeedbackVector::kFlagsTieringStateIsAnyRequested, r0);
  beq(&maybe_needs_logging, cr0);

  GenerateTailCallToReturnedCode(Runtime::kCompileOptimized);

  bind(&maybe_needs_logging);
  TestBitMask(flags, FeedbackVector::LogNextExecutionBit::kMask, r0);
  beq(&maybe_has_optimized_code, cr0);
  GenerateTailCallToReturnedCode(Runtime::kFunctionLogNextExecution);

  bind(&maybe_has_optimized_code);
  Register optimized_code_entry = flags;
  LoadTaggedField(optimized_code_entry,
                  FieldMemOperand(feedback_vector,
                                  FeedbackVector::kMaybeOptimizedCodeOffset),
                  r0);
  TailCallOptimizedCodeSlot(this, optimized_code_entry, r9);
}

void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments) {
  // All parameters are on the stack.  r3 has the return value after call.

  // If the expected number of arguments of the runtime function is
  // constant, we check that the actual number of arguments match the
  // expectation.
  CHECK(f->nargs < 0 || f->nargs == num_arguments);

  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  mov(r3, Operand(num_arguments));
  Move(r4, ExternalReference::Create(f));
#if V8_TARGET_ARCH_PPC64
  CallBuiltin(Builtins::RuntimeCEntry(f->result_size));
#else
  CallBuiltin(Builtins::RuntimeCEntry(1));
#endif
}

void MacroAssembler::TailCallRuntime(Runtime::FunctionId fid) {
  const Runtime::Function* function = Runtime::FunctionForId(fid);
  DCHECK_EQ(1, function->result_size);
  if (function->nargs >= 0) {
    mov(r3, Operand(function->nargs));
  }
  JumpToExternalReference(ExternalReference::Create(fid));
}

void MacroAssembler::JumpToExternalReference(const ExternalReference& builtin,
                                             bool builtin_exit_frame) {
  Move(r4, builtin);
  TailCallBuiltin(Builtins::CEntry(1, ArgvMode::kStack, builtin_exit_frame));
}

void MacroAssembler::LoadWeakValue(Register out, Register in,
                                   Label* target_if_cleared) {
  CmpS32(in, Operand(kClearedWeakHeapObjectLower32), r0);
  beq(target_if_cleared);

  mov(r0, Operand(~kWeakHeapObjectMask));
  and_(out, in, r0);
}

void MacroAssembler::EmitIncrementCounter(StatsCounter* counter, int value,
                                          Register scratch1,
                                          Register scratch2) {
  DCHECK_GT(value, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t dummy_stats_counter_
    // field.
    Move(scratch2, ExternalReference::Create(counter));
    lwz(scratch1, MemOperand(scratch2));
    addi(scratch1, scratch1, Operand(value));
    stw(scratch1, MemOperand(scratch2));
  }
}

void MacroAssembler::EmitDecrementCounter(StatsCounter* counter, int value,
                                          Register scratch1,
                                          Register scratch2) {
  DCHECK_GT(value, 0);
  if (v8_flags.native_code_counters && counter->Enabled()) {
    // This operation has to be exactly 32-bit wide in case the external
    // reference table redirects the counter to a uint32_t dummy_stats_counter_
    // field.
    Move(scratch2, ExternalReference::Create(counter));
    lwz(scratch1, MemOperand(scratch2));
    subi(scratch1, scratch1, Operand(value));
    stw(scratch1, MemOperand(scratch2));
  }
}

void MacroAssembler::Check(Condition cond, AbortReason reason, CRegister cr) {
  Label L;
  b(cond, &L, cr);
  Abort(reason);
  // will not return here
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
    mov(r3, Operand(static_cast<int>(reason)));
    PrepareCallCFunction(1, 0, r4);
    Register dst = ip;
    if (!ABI_CALL_VIA_IP) {
      dst = r4;
    }
    Move(dst, ExternalReference::abort_with_reason());
    // Use Call directly to avoid any unneeded overhead. The function won't
    // return anyway.
    Call(dst);
    return;
  }

  LoadSmiLiteral(r4, Smi::FromInt(static_cast<int>(reason)));

  {
    // We don't actually want to generate a pile of code for this, so just
    // claim there is a stack frame, without generating one.
    FrameScope scope(this, StackFrame::NO_FRAME_TYPE);
    if (root_array_available()) {
      // Generate an indirect call via builtins entry table here in order to
      // ensure that the interpreter_entry_return_pc_offset is the same for
      // InterpreterEntryTrampoline and InterpreterEntryTrampolineForProfiling
      // when v8_flags.debug_code is enabled.
      LoadEntryFromBuiltin(Builtin::kAbort, ip);
      Call(ip);
    } else {
      CallBuiltin(Builtin::kAbort);
    }
  }
  // will not return here
}

void MacroAssembler::LoadMap(Register destination, Register object) {
  LoadTaggedField(destination, FieldMemOperand(object, HeapObject::kMapOffset),
                  r0);
}

void MacroAssembler::LoadFeedbackVector(Register dst, Register closure,
                                        Register scratch, Label* fbv_undef) {
  Label done;

  // Load the feedback vector from the closure.
  LoadTaggedField(
      dst, FieldMemOperand(closure, JSFunction::kFeedbackCellOffset), r0);
  LoadTaggedField(dst, FieldMemOperand(dst, FeedbackCell::kValueOffset), r0);

  // Check if feedback vector is valid.
  LoadTaggedField(scratch, FieldMemOperand(dst, HeapObject::kMapOffset), r0);
  LoadU16(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  CmpS32(scratch, Operand(FEEDBACK_VECTOR_TYPE), r0);
  b(eq, &done);

  // Not valid, load undefined.
  LoadRoot(dst, RootIndex::kUndefinedValue);
  b(fbv_undef);

  bind(&done);
}

void MacroAssembler::LoadCompressedMap(Register dst, Register object,
                                       Register scratch) {
  ASM_CODE_COMMENT(this);
  LoadU32(dst, FieldMemOperand(object, HeapObject::kMapOffset), scratch);
}

void MacroAssembler::LoadNativeContextSlot(Register dst, int index) {
  LoadMap(dst, cp);
  LoadTaggedField(
      dst,
      FieldMemOperand(dst, Map::kConstructorOrBackPointerOrNativeContextOffset),
      r0);
  LoadTaggedField(dst, MemOperand(dst, Context::SlotOffset(index)), r0);
}

#ifdef V8_ENABLE_DEBUG_CODE
void MacroAssembler::Assert(Condition cond, AbortReason reason, CRegister cr) {
  if (v8_flags.debug_code) Check(cond, reason, cr);
}

void MacroAssembler::AssertNotSmi(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object, r0);
    Check(ne, AbortReason::kOperandIsASmi, cr0);
  }
}

void MacroAssembler::AssertSmi(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object, r0);
    Check(eq, AbortReason::kOperandIsNotASmi, cr0);
  }
}

void MacroAssembler::AssertConstructor(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object, r0);
    Check(ne, AbortReason::kOperandIsASmiAndNotAConstructor, cr0);
    push(object);
    LoadMap(object, object);
    lbz(object, FieldMemOperand(object, Map::kBitFieldOffset));
    andi(object, object, Operand(Map::Bits1::IsConstructorBit::kMask));
    pop(object);
    Check(ne, AbortReason::kOperandIsNotAConstructor, cr0);
  }
}

void MacroAssembler::AssertFunction(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object, r0);
    Check(ne, AbortReason::kOperandIsASmiAndNotAFunction, cr0);
    push(object);
    LoadMap(object, object);
    CompareInstanceTypeRange(object, object, r0, FIRST_JS_FUNCTION_TYPE,
                             LAST_JS_FUNCTION_TYPE);
    pop(object);
    Check(le, AbortReason::kOperandIsNotAFunction);
  }
}

void MacroAssembler::AssertCallableFunction(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  static_assert(kSmiTag == 0);
  TestIfSmi(object, r0);
  Check(ne, AbortReason::kOperandIsASmiAndNotAFunction, cr0);
  push(object);
  LoadMap(object, object);
  CompareInstanceTypeRange(object, object, r0, FIRST_CALLABLE_JS_FUNCTION_TYPE,
                           LAST_CALLABLE_JS_FUNCTION_TYPE);
  pop(object);
  Check(le, AbortReason::kOperandIsNotACallableFunction);
}

void MacroAssembler::AssertBoundFunction(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object, r0);
    Check(ne, AbortReason::kOperandIsASmiAndNotABoundFunction, cr0);
    push(object);
    CompareObjectType(object, object, object, JS_BOUND_FUNCTION_TYPE);
    pop(object);
    Check(eq, AbortReason::kOperandIsNotABoundFunction);
  }
}

void MacroAssembler::AssertGeneratorObject(Register object) {
  if (!v8_flags.debug_code) return;
  TestIfSmi(object, r0);
  Check(ne, AbortReason::kOperandIsASmiAndNotAGeneratorObject, cr0);

  // Load map
  Register map = object;
  push(object);
  LoadMap(map, object);

  // Check if JSGeneratorObject
  Register instance_type = object;
  CompareInstanceTypeRange(map, instance_type, r0,
                           FIRST_JS_GENERATOR_OBJECT_TYPE,
                           LAST_JS_GENERATOR_OBJECT_TYPE);
  // Restore generator object to register and perform assertion
  pop(object);
  Check(le, AbortReason::kOperandIsNotAGeneratorObject);
}

void MacroAssembler::AssertUndefinedOrAllocationSite(Register object,
                                                     Register scratch) {
  if (v8_flags.debug_code) {
    Label done_checking;
    AssertNotSmi(object);
    CompareRoot(object, RootIndex::kUndefinedValue);
    beq(&done_checking);
    LoadMap(scratch, object);
    CompareInstanceType(scratch, scratch, ALLOCATION_SITE_TYPE);
    Assert(eq, AbortReason::kExpectedUndefinedOrCell);
    bind(&done_checking);
  }
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
  ble(&ok);

  CompareInstanceType(map_tmp, tmp, FIRST_JS_RECEIVER_TYPE);
  bge(&ok);

  CompareRoot(map_tmp, RootIndex::kHeapNumberMap);
  beq(&ok);

  CompareRoot(map_tmp, RootIndex::kBigIntMap);
  beq(&ok);

  CompareRoot(object, RootIndex::kUndefinedValue);
  beq(&ok);

  CompareRoot(object, RootIndex::kTrueValue);
  beq(&ok);

  CompareRoot(object, RootIndex::kFalseValue);
  beq(&ok);

  CompareRoot(object, RootIndex::kNullValue);
  beq(&ok);

  Abort(abort_reason);

  bind(&ok);
}

#endif  // V8_ENABLE_DEBUG_CODE

int MacroAssembler::CalculateStackPassedWords(int num_reg_arguments,
                                              int num_double_arguments) {
  int stack_passed_words = 0;
  if (num_double_arguments > DoubleRegister::kNumRegisters) {
    stack_passed_words +=
        2 * (num_double_arguments - DoubleRegister::kNumRegisters);
  }
  // Up to 8 simple arguments are passed in registers r3..r10.
  if (num_reg_arguments > kRegisterPassedArguments) {
    stack_passed_words += num_reg_arguments - kRegisterPassedArguments;
  }
  return stack_passed_words;
}

void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          int num_double_arguments,
                                          Register scratch) {
  int frame_alignment = ActivationFrameAlignment();
  int stack_passed_arguments =
      CalculateStackPassedWords(num_reg_arguments, num_double_arguments);
  int stack_space = kNumRequiredStackFrameSlots;

  if (frame_alignment > kSystemPointerSize) {
    // Make stack end at alignment and make room for stack arguments
    // -- preserving original value of sp.
    mr(scratch, sp);
    AddS64(sp, sp, Operand(-(stack_passed_arguments + 1) * kSystemPointerSize),
           scratch);
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    ClearRightImm(sp, sp,
                  Operand(base::bits::WhichPowerOfTwo(frame_alignment)));
    StoreU64(scratch,
             MemOperand(sp, stack_passed_arguments * kSystemPointerSize));
  } else {
    // Make room for stack arguments
    stack_space += stack_passed_arguments;
  }

  // Allocate frame with required slots to make ABI work.
  li(r0, Operand::Zero());
  StoreU64WithUpdate(r0, MemOperand(sp, -stack_space * kSystemPointerSize));
}

void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          Register scratch) {
  PrepareCallCFunction(num_reg_arguments, 0, scratch);
}

void MacroAssembler::MovToFloatParameter(DoubleRegister src) { Move(d1, src); }

void MacroAssembler::MovToFloatResult(DoubleRegister src) { Move(d1, src); }

void MacroAssembler::MovToFloatParameters(DoubleRegister src1,
                                          DoubleRegister src2) {
  if (src2 == d1) {
    DCHECK(src1 != d2);
    Move(d2, src2);
    Move(d1, src1);
  } else {
    Move(d1, src1);
    Move(d2, src2);
  }
}

int MacroAssembler::CallCFunction(ExternalReference function,
                                  int num_reg_arguments,
                                  int num_double_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  bool has_function_descriptor) {
  Move(ip, function);
  return CallCFunction(ip, num_reg_arguments, num_double_arguments,
                       set_isolate_data_slots, has_function_descriptor);
}

int MacroAssembler::CallCFunction(Register function, int num_reg_arguments,
                                  int num_double_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  bool has_function_descriptor) {
  ASM_CODE_COMMENT(this);
  DCHECK_LE(num_reg_arguments + num_double_arguments, kMaxCParameters);
  DCHECK(has_frame());

  Label start_call;
  Register pc_scratch = r11;
  DCHECK(!AreAliased(pc_scratch, function));
  LoadPC(pc_scratch);
  bind(&start_call);
  int start_pc_offset = pc_offset();
  // We are going to patch this instruction after emitting
  // Call, using a zero offset here as placeholder for now.
  // patch_pc_address assumes `addi` is used here to
  // add the offset to pc.
  addi(pc_scratch, pc_scratch, Operand::Zero());

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // Save the frame pointer and PC so that the stack layout remains iterable,
    // even without an ExitFrame which normally exists between JS and C frames.
    Register scratch = r8;
    Push(scratch);
    mflr(scratch);
    CHECK(root_array_available());
    StoreU64(pc_scratch,
             ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC));
    StoreU64(fp,
             ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
    mtlr(scratch);
    Pop(scratch);
  }

  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
  Register dest = function;
  if (ABI_USES_FUNCTION_DESCRIPTORS && has_function_descriptor) {
    // AIX/PPC64BE Linux uses a function descriptor. When calling C code be
    // aware of this descriptor and pick up values from it
    LoadU64(ToRegister(ABI_TOC_REGISTER),
            MemOperand(function, kSystemPointerSize));
    LoadU64(ip, MemOperand(function, 0));
    dest = ip;
  } else if (ABI_CALL_VIA_IP) {
    // pLinux and Simualtor, not AIX
    Move(ip, function);
    dest = ip;
  }

  Call(dest);
  int call_pc_offset = pc_offset();
  int offset_since_start_call = SizeOfCodeGeneratedSince(&start_call);
  // Here we are going to patch the `addi` instruction above to use the
  // correct offset.
  // LoadPC emits two instructions and pc is the address of its second emitted
  // instruction. Add one more to the offset to point to after the Call.
  offset_since_start_call += kInstrSize;
  patch_pc_address(pc_scratch, start_pc_offset, offset_since_start_call);

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // We don't unset the PC; the FP is the source of truth.
    Register zero_scratch = r0;
    mov(zero_scratch, Operand::Zero());

    StoreU64(zero_scratch,
             ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
  }

  // Remove frame bought in PrepareCallCFunction
  int stack_passed_arguments =
      CalculateStackPassedWords(num_reg_arguments, num_double_arguments);
  int stack_space = kNumRequiredStackFrameSlots + stack_passed_arguments;
  if (ActivationFrameAlignment() > kSystemPointerSize) {
    LoadU64(sp, MemOperand(sp, stack_space * kSystemPointerSize), r0);
  } else {
    AddS64(sp, sp, Operand(stack_space * kSystemPointerSize), r0);
  }

  return call_pc_offset;
}

int MacroAssembler::CallCFunction(ExternalReference function, int num_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  bool has_function_descriptor) {
  return CallCFunction(function, num_arguments, 0, set_isolate_data_slots,
                       has_function_descriptor);
}

int MacroAssembler::CallCFunction(Register function, int num_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  bool has_function_descriptor) {
  return CallCFunction(function, num_arguments, 0, set_isolate_data_slots,
                       has_function_descriptor);
}

void MacroAssembler::CheckPageFlag(
    Register object,
    Register scratch,  // scratch may be same register as object
    int mask, Condition cc, Label* condition_met) {
  DCHECK(cc == ne || cc == eq);
  DCHECK(scratch != r0);
  ClearRightImm(scratch, object, Operand(kPageSizeBits));
  LoadU64(scratch, MemOperand(scratch, MemoryChunkLayout::kFlagsOffset), r0);

  mov(r0, Operand(mask));
  and_(r0, scratch, r0, SetRC);

  if (cc == ne) {
    bne(condition_met, cr0);
  }
  if (cc == eq) {
    beq(condition_met, cr0);
  }
}

void MacroAssembler::SetRoundingMode(FPRoundingMode RN) { mtfsfi(7, RN); }

void MacroAssembler::ResetRoundingMode() {
  mtfsfi(7, kRoundToNearest);  // reset (default is kRoundToNearest)
}

////////////////////////////////////////////////////////////////////////////////
//
// New MacroAssembler Interfaces added for PPC
//
////////////////////////////////////////////////////////////////////////////////
void MacroAssembler::LoadIntLiteral(Register dst, int value) {
  mov(dst, Operand(value));
}

void MacroAssembler::LoadSmiLiteral(Register dst, Tagged<Smi> smi) {
  mov(dst, Operand(smi));
}

void MacroAssembler::LoadDoubleLiteral(DoubleRegister result,
                                       base::Double value, Register scratch) {
  if (V8_EMBEDDED_CONSTANT_POOL_BOOL && is_constant_pool_available() &&
      !(scratch == r0 && ConstantPoolAccessIsInOverflow())) {
    ConstantPoolEntry::Access access = ConstantPoolAddEntry(value);
    if (access == ConstantPoolEntry::OVERFLOWED) {
      addis(scratch, kConstantPoolRegister, Operand::Zero());
      lfd(result, MemOperand(scratch, 0));
    } else {
      lfd(result, MemOperand(kConstantPoolRegister, 0));
    }
    return;
  }

  // avoid gcc strict aliasing error using union cast
  union {
    uint64_t dval;
#if V8_TARGET_ARCH_PPC64
    intptr_t ival;
#else
    intptr_t ival[2];
#endif
  } litVal;

  litVal.dval = value.AsUint64();

#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mov(scratch, Operand(litVal.ival));
    mtfprd(result, scratch);
    return;
  }
#endif

  addi(sp, sp, Operand(-kDoubleSize));
#if V8_TARGET_ARCH_PPC64
  mov(scratch, Operand(litVal.ival));
  std(scratch, MemOperand(sp));
#else
  LoadIntLiteral(scratch, litVal.ival[0]);
  stw(scratch, MemOperand(sp, 0));
  LoadIntLiteral(scratch, litVal.ival[1]);
  stw(scratch, MemOperand(sp, 4));
#endif
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lfd(result, MemOperand(sp, 0));
  addi(sp, sp, Operand(kDoubleSize));
}

void MacroAssembler::MovIntToDouble(DoubleRegister dst, Register src,
                                    Register scratch) {
// sign-extend src to 64-bit
#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mtfprwa(dst, src);
    return;
  }
#endif

  DCHECK(src != scratch);
  subi(sp, sp, Operand(kDoubleSize));
#if V8_TARGET_ARCH_PPC64
  extsw(scratch, src);
  std(scratch, MemOperand(sp, 0));
#else
  srawi(scratch, src, 31);
  stw(scratch, MemOperand(sp, Register::kExponentOffset));
  stw(src, MemOperand(sp, Register::kMantissaOffset));
#endif
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lfd(dst, MemOperand(sp, 0));
  addi(sp, sp, Operand(kDoubleSize));
}

void MacroAssembler::MovUnsignedIntToDouble(DoubleRegister dst, Register src,
                                            Register scratch) {
// zero-extend src to 64-bit
#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mtfprwz(dst, src);
    return;
  }
#endif

  DCHECK(src != scratch);
  subi(sp, sp, Operand(kDoubleSize));
#if V8_TARGET_ARCH_PPC64
  clrldi(scratch, src, Operand(32));
  std(scratch, MemOperand(sp, 0));
#else
  li(scratch, Operand::Zero());
  stw(scratch, MemOperand(sp, Register::kExponentOffset));
  stw(src, MemOperand(sp, Register::kMantissaOffset));
#endif
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lfd(dst, MemOperand(sp, 0));
  addi(sp, sp, Operand(kDoubleSize));
}

void MacroAssembler::MovInt64ToDouble(DoubleRegister dst,
#if !V8_TARGET_ARCH_PPC64
                                      Register src_hi,
#endif
                                      Register src) {
#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mtfprd(dst, src);
    return;
  }
#endif

  subi(sp, sp, Operand(kDoubleSize));
#if V8_TARGET_ARCH_PPC64
  std(src, MemOperand(sp, 0));
#else
  stw(src_hi, MemOperand(sp, Register::kExponentOffset));
  stw(src, MemOperand(sp, Register::kMantissaOffset));
#endif
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lfd(dst, MemOperand(sp, 0));
  addi(sp, sp, Operand(kDoubleSize));
}

#if V8_TARGET_ARCH_PPC64
void MacroAssembler::MovInt64ComponentsToDouble(DoubleRegister dst,
                                                Register src_hi,
                                                Register src_lo,
                                                Register scratch) {
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    ShiftLeftU64(scratch, src_hi, Operand(32));
    rldimi(scratch, src_lo, 0, 32);
    mtfprd(dst, scratch);
    return;
  }

  subi(sp, sp, Operand(kDoubleSize));
  stw(src_hi, MemOperand(sp, Register::kExponentOffset));
  stw(src_lo, MemOperand(sp, Register::kMantissaOffset));
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lfd(dst, MemOperand(sp));
  addi(sp, sp, Operand(kDoubleSize));
}
#endif

void MacroAssembler::InsertDoubleLow(DoubleRegister dst, Register src,
                                     Register scratch) {
#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mffprd(scratch, dst);
    rldimi(scratch, src, 0, 32);
    mtfprd(dst, scratch);
    return;
  }
#endif

  subi(sp, sp, Operand(kDoubleSize));
  stfd(dst, MemOperand(sp));
  stw(src, MemOperand(sp, Register::kMantissaOffset));
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lfd(dst, MemOperand(sp));
  addi(sp, sp, Operand(kDoubleSize));
}

void MacroAssembler::InsertDoubleHigh(DoubleRegister dst, Register src,
                                      Register scratch) {
#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mffprd(scratch, dst);
    rldimi(scratch, src, 32, 0);
    mtfprd(dst, scratch);
    return;
  }
#endif

  subi(sp, sp, Operand(kDoubleSize));
  stfd(dst, MemOperand(sp));
  stw(src, MemOperand(sp, Register::kExponentOffset));
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lfd(dst, MemOperand(sp));
  addi(sp, sp, Operand(kDoubleSize));
}

void MacroAssembler::MovDoubleLowToInt(Register dst, DoubleRegister src) {
#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mffprwz(dst, src);
    return;
  }
#endif

  subi(sp, sp, Operand(kDoubleSize));
  stfd(src, MemOperand(sp));
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lwz(dst, MemOperand(sp, Register::kMantissaOffset));
  addi(sp, sp, Operand(kDoubleSize));
}

void MacroAssembler::MovDoubleHighToInt(Register dst, DoubleRegister src) {
#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mffprd(dst, src);
    srdi(dst, dst, Operand(32));
    return;
  }
#endif

  subi(sp, sp, Operand(kDoubleSize));
  stfd(src, MemOperand(sp));
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lwz(dst, MemOperand(sp, Register::kExponentOffset));
  addi(sp, sp, Operand(kDoubleSize));
}

void MacroAssembler::MovDoubleToInt64(
#if !V8_TARGET_ARCH_PPC64
    Register dst_hi,
#endif
    Register dst, DoubleRegister src) {
#if V8_TARGET_ARCH_PPC64
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    mffprd(dst, src);
    return;
  }
#endif

  subi(sp, sp, Operand(kDoubleSize));
  stfd(src, MemOperand(sp));
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
#if V8_TARGET_ARCH_PPC64
  ld(dst, MemOperand(sp, 0));
#else
  lwz(dst_hi, MemOperand(sp, Register::kExponentOffset));
  lwz(dst, MemOperand(sp, Register::kMantissaOffset));
#endif
  addi(sp, sp, Operand(kDoubleSize));
}

void MacroAssembler::MovIntToFloat(DoubleRegister dst, Register src,
                                   Register scratch) {
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    ShiftLeftU64(scratch, src, Operand(32));
    mtfprd(dst, scratch);
    xscvspdpn(dst, dst);
    return;
  }
  subi(sp, sp, Operand(kFloatSize));
  stw(src, MemOperand(sp, 0));
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lfs(dst, MemOperand(sp, 0));
  addi(sp, sp, Operand(kFloatSize));
}

void MacroAssembler::MovFloatToInt(Register dst, DoubleRegister src,
                                   DoubleRegister scratch) {
  if (CpuFeatures::IsSupported(PPC_8_PLUS)) {
    xscvdpspn(scratch, src);
    mffprwz(dst, scratch);
    return;
  }
  subi(sp, sp, Operand(kFloatSize));
  stfs(src, MemOperand(sp, 0));
  nop(GROUP_ENDING_NOP);  // LHS/RAW optimization
  lwz(dst, MemOperand(sp, 0));
  addi(sp, sp, Operand(kFloatSize));
}

void MacroAssembler::AddS64(Register dst, Register src, Register value, OEBit s,
                            RCBit r) {
  add(dst, src, value, s, r);
}

void MacroAssembler::AddS64(Register dst, Register src, const Operand& value,
                            Register scratch, OEBit s, RCBit r) {
  if (is_int16(value.immediate()) && s == LeaveOE && r == LeaveRC) {
    addi(dst, src, value);
  } else {
    mov(scratch, value);
    add(dst, src, scratch, s, r);
  }
}

void MacroAssembler::SubS64(Register dst, Register src, Register value, OEBit s,
                            RCBit r) {
  sub(dst, src, value, s, r);
}

void MacroAssembler::SubS64(Register dst, Register src, const Operand& value,
                            Register scratch, OEBit s, RCBit r) {
  if (is_int16(value.immediate()) && s == LeaveOE && r == LeaveRC) {
    subi(dst, src, value);
  } else {
    mov(scratch, value);
    sub(dst, src, scratch, s, r);
  }
}

void MacroAssembler::AddS32(Register dst, Register src, Register value,
                            RCBit r) {
  AddS64(dst, src, value, LeaveOE, r);
  extsw(dst, dst, r);
}

void MacroAssembler::AddS32(Register dst, Register src, const Operand& value,
                            Register scratch, RCBit r) {
  AddS64(dst, src, value, scratch, LeaveOE, r);
  extsw(dst, dst, r);
}

void MacroAssembler::SubS32(Register dst, Register src, Register value,
                            RCBit r) {
  SubS64(dst, src, value, LeaveOE, r);
  extsw(dst, dst, r);
}

void MacroAssembler::SubS32(Register dst, Register src, const Operand& value,
                            Register scratch, RCBit r) {
  SubS64(dst, src, value, scratch, LeaveOE, r);
  extsw(dst, dst, r);
}

void MacroAssembler::MulS64(Register dst, Register src, const Operand& value,
                            Register scratch, OEBit s, RCBit r) {
  if (is_int16(value.immediate()) && s == LeaveOE && r == LeaveRC) {
    mulli(dst, src, value);
  } else {
    mov(scratch, value);
    mulld(dst, src, scratch, s, r);
  }
}

void MacroAssembler::MulS64(Register dst, Register src, Register value, OEBit s,
                            RCBit r) {
  mulld(dst, src, value, s, r);
}

void MacroAssembler::MulS32(Register dst, Register src, const Operand& value,
                            Register scratch, OEBit s, RCBit r) {
  MulS64(dst, src, value, scratch, s, r);
  extsw(dst, dst, r);
}

void MacroAssembler::MulS32(Register dst, Register src, Register value, OEBit s,
                            RCBit r) {
  MulS64(dst, src, value, s, r);
  extsw(dst, dst, r);
}

void MacroAssembler::DivS64(Register dst, Register src, Register value, OEBit s,
                            RCBit r) {
  divd(dst, src, value, s, r);
}

void MacroAssembler::DivU64(Register dst, Register src, Register value, OEBit s,
                            RCBit r) {
  divdu(dst, src, value, s, r);
}

void MacroAssembler::DivS32(Register dst, Register src, Register value, OEBit s,
                            RCBit r) {
  divw(dst, src, value, s, r);
  extsw(dst, dst);
}
void MacroAssembler::DivU32(Register dst, Register src, Register value, OEBit s,
                            RCBit r) {
  divwu(dst, src, value, s, r);
  ZeroExtWord32(dst, dst);
}

void MacroAssembler::ModS64(Register dst, Register src, Register value) {
  if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
    modsd(dst, src, value);
  } else {
    Register scratch = GetRegisterThatIsNotOneOf(dst, src, value);
    Push(scratch);
    divd(scratch, src, value);
    mulld(scratch, scratch, value);
    sub(dst, src, scratch);
    Pop(scratch);
  }
}

void MacroAssembler::ModU64(Register dst, Register src, Register value) {
  if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
    modud(dst, src, value);
  } else {
    Register scratch = GetRegisterThatIsNotOneOf(dst, src, value);
    Push(scratch);
    divdu(scratch, src, value);
    mulld(scratch, scratch, value);
    sub(dst, src, scratch);
    Pop(scratch);
  }
}

void MacroAssembler::ModS32(Register dst, Register src, Register value) {
  if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
    modsw(dst, src, value);
  } else {
    Register scratch = GetRegisterThatIsNotOneOf(dst, src, value);
    Push(scratch);
    divw(scratch, src, value);
    mullw(scratch, scratch, value);
    sub(dst, src, scratch);
    Pop(scratch);
  }
  extsw(dst, dst);
}
void MacroAssembler::ModU32(Register dst, Register src, Register value) {
  if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
    moduw(dst, src, value);
  } else {
    Register scratch = GetRegisterThatIsNotOneOf(dst, src, value);
    Push(scratch);
    divwu(scratch, src, value);
    mullw(scratch, scratch, value);
    sub(dst, src, scratch);
    Pop(scratch);
  }
  ZeroExtWord32(dst, dst);
}

void MacroAssembler::AndU64(Register dst, Register src, const Operand& value,
                            Register scratch, RCBit r) {
  if (is_uint16(value.immediate()) && r == SetRC) {
    andi(dst, src, value);
  } else {
    mov(scratch, value);
    and_(dst, src, scratch, r);
  }
}

void MacroAssembler::AndU64(Register dst, Register src, Register value,
                            RCBit r) {
  and_(dst, src, value, r);
}

void MacroAssembler::OrU64(Register dst, Register src, const Operand& value,
                           Register scratch, RCBit r) {
  if (is_int16(value.immediate()) && r == LeaveRC) {
    ori(dst, src, value);
  } else {
    mov(scratch, value);
    orx(dst, src, scratch, r);
  }
}

void MacroAssembler::OrU64(Register dst, Register src, Register value,
                           RCBit r) {
  orx(dst, src, value, r);
}

void MacroAssembler::XorU64(Register dst, Register src, const Operand& value,
                            Register scratch, RCBit r) {
  if (is_int16(value.immediate()) && r == LeaveRC) {
    xori(dst, src, value);
  } else {
    mov(scratch, value);
    xor_(dst, src, scratch, r);
  }
}

void MacroAssembler::XorU64(Register dst, Register src, Register value,
                            RCBit r) {
  xor_(dst, src, value, r);
}

void MacroAssembler::AndU32(Register dst, Register src, const Operand& value,
                            Register scratch, RCBit r) {
  AndU64(dst, src, value, scratch, r);
  extsw(dst, dst, r);
}

void MacroAssembler::AndU32(Register dst, Register src, Register value,
                            RCBit r) {
  AndU64(dst, src, value, r);
  extsw(dst, dst, r);
}

void MacroAssembler::OrU32(Register dst, Register src, const Operand& value,
                           Register scratch, RCBit r) {
  OrU64(dst, src, value, scratch, r);
  extsw(dst, dst, r);
}

void MacroAssembler::OrU32(Register dst, Register src, Register value,
                           RCBit r) {
  OrU64(dst, src, value, r);
  extsw(dst, dst, r);
}

void MacroAssembler::XorU32(Register dst, Register src, const Operand& value,
                            Register scratch, RCBit r) {
  XorU64(dst, src, value, scratch, r);
  extsw(dst, dst, r);
}

void MacroAssembler::XorU32(Register dst, Register src, Register value,
                            RCBit r) {
  XorU64(dst, src, value, r);
  extsw(dst, dst, r);
}

void MacroAssembler::ShiftLeftU64(Register dst, Register src,
                                  const Operand& value, RCBit r) {
  sldi(dst, src, value, r);
}

void MacroAssembler::ShiftRightU64(Register dst, Register src,
                                   const Operand& value, RCBit r) {
  srdi(dst, src, value, r);
}

void MacroAssembler::ShiftRightS64(Register dst, Register src,
                                   const Operand& value, RCBit r) {
  sradi(dst, src, value.immediate(), r);
}

void MacroAssembler::ShiftLeftU32(Register dst, Register src,
                                  const Operand& value, RCBit r) {
  slwi(dst, src, value, r);
}

void MacroAssembler::ShiftRightU32(Register dst, Register src,
                                   const Operand& value, RCBit r) {
  srwi(dst, src, value, r);
}

void MacroAssembler::ShiftRightS32(Register dst, Register src,
                                   const Operand& value, RCBit r) {
  srawi(dst, src, value.immediate(), r);
}

void MacroAssembler::ShiftLeftU64(Register dst, Register src, Register value,
                                  RCBit r) {
  sld(dst, src, value, r);
}

void MacroAssembler::ShiftRightU64(Register dst, Register src, Register value,
                                   RCBit r) {
  srd(dst, src, value, r);
}

void MacroAssembler::ShiftRightS64(Register dst, Register src, Register value,
                                   RCBit r) {
  srad(dst, src, value, r);
}

void MacroAssembler::ShiftLeftU32(Register dst, Register src, Register value,
                                  RCBit r) {
  slw(dst, src, value, r);
}

void MacroAssembler::ShiftRightU32(Register dst, Register src, Register value,
                                   RCBit r) {
  srw(dst, src, value, r);
}

void MacroAssembler::ShiftRightS32(Register dst, Register src, Register value,
                                   RCBit r) {
  sraw(dst, src, value, r);
}

void MacroAssembler::CmpS64(Register src1, Register src2, CRegister cr) {
  cmp(src1, src2, cr);
}

void MacroAssembler::CmpS64(Register src1, const Operand& src2,
                            Register scratch, CRegister cr) {
  intptr_t value = src2.immediate();
  if (is_int16(value)) {
    cmpi(src1, src2, cr);
  } else {
    mov(scratch, src2);
    CmpS64(src1, scratch, cr);
  }
}

void MacroAssembler::CmpU64(Register src1, const Operand& src2,
                            Register scratch, CRegister cr) {
  intptr_t value = src2.immediate();
  if (is_uint16(value)) {
    cmpli(src1, src2, cr);
  } else {
    mov(scratch, src2);
    CmpU64(src1, scratch, cr);
  }
}

void MacroAssembler::CmpU64(Register src1, Register src2, CRegister cr) {
  cmpl(src1, src2, cr);
}

void MacroAssembler::CmpS32(Register src1, const Operand& src2,
                            Register scratch, CRegister cr) {
  intptr_t value = src2.immediate();
  if (is_int16(value)) {
    cmpwi(src1, src2, cr);
  } else {
    mov(scratch, src2);
    CmpS32(src1, scratch, cr);
  }
}

void MacroAssembler::CmpS32(Register src1, Register src2, CRegister cr) {
  cmpw(src1, src2, cr);
}

void MacroAssembler::CmpU32(Register src1, const Operand& src2,
                            Register scratch, CRegister cr) {
  intptr_t value = src2.immediate();
  if (is_uint16(value)) {
    cmplwi(src1, src2, cr);
  } else {
    mov(scratch, src2);
    cmplw(src1, scratch, cr);
  }
}

void MacroAssembler::CmpU32(Register src1, Register src2, CRegister cr) {
  cmplw(src1, src2, cr);
}

void MacroAssembler::AddF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, RCBit r) {
  fadd(dst, lhs, rhs, r);
}

void MacroAssembler::SubF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, RCBit r) {
  fsub(dst, lhs, rhs, r);
}

void MacroAssembler::MulF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, RCBit r) {
  fmul(dst, lhs, rhs, r);
}

void MacroAssembler::DivF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, RCBit r) {
  fdiv(dst, lhs, rhs, r);
}

void MacroAssembler::AddF32(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, RCBit r) {
  fadd(dst, lhs, rhs, r);
  frsp(dst, dst, r);
}

void MacroAssembler::SubF32(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, RCBit r) {
  fsub(dst, lhs, rhs, r);
  frsp(dst, dst, r);
}

void MacroAssembler::MulF32(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, RCBit r) {
  fmul(dst, lhs, rhs, r);
  frsp(dst, dst, r);
}

void MacroAssembler::DivF32(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs, RCBit r) {
  fdiv(dst, lhs, rhs, r);
  frsp(dst, dst, r);
}

void MacroAssembler::CopySignF64(DoubleRegister dst, DoubleRegister lhs,
                                 DoubleRegister rhs, RCBit r) {
  fcpsgn(dst, rhs, lhs, r);
}

void MacroAssembler::CmpSmiLiteral(Register src1, Tagged<Smi> smi,
                                   Register scratch, CRegister cr) {
#if defined(V8_COMPRESS_POINTERS) || defined(V8_31BIT_SMIS_ON_64BIT_ARCH)
  CmpS32(src1, Operand(smi), scratch, cr);
#else
  LoadSmiLiteral(scratch, smi);
  CmpS64(src1, scratch, cr);
#endif
}

void MacroAssembler::CmplSmiLiteral(Register src1, Tagged<Smi> smi,
                                    Register scratch, CRegister cr) {
#if defined(V8_COMPRESS_POINTERS) || defined(V8_31BIT_SMIS_ON_64BIT_ARCH)
  CmpU64(src1, Operand(smi), scratch, cr);
#else
  LoadSmiLiteral(scratch, smi);
  CmpU64(src1, scratch, cr);
#endif
}

void MacroAssembler::AddSmiLiteral(Register dst, Register src, Tagged<Smi> smi,
                                   Register scratch) {
#if defined(V8_COMPRESS_POINTERS) || defined(V8_31BIT_SMIS_ON_64BIT_ARCH)
  AddS64(dst, src, Operand(smi.ptr()), scratch);
#else
  LoadSmiLiteral(scratch, smi);
  add(dst, src, scratch);
#endif
}

void MacroAssembler::SubSmiLiteral(Register dst, Register src, Tagged<Smi> smi,
                                   Register scratch) {
#if defined(V8_COMPRESS_POINTERS) || defined(V8_31BIT_SMIS_ON_64BIT_ARCH)
  AddS64(dst, src, Operand(-(static_cast<intptr_t>(smi.ptr()))), scratch);
#else
  LoadSmiLiteral(scratch, smi);
  sub(dst, src, scratch);
#endif
}

void MacroAssembler::AndSmiLiteral(Register dst, Register src, Tagged<Smi> smi,
                                   Register scratch, RCBit rc) {
#if defined(V8_COMPRESS_POINTERS) || defined(V8_31BIT_SMIS_ON_64BIT_ARCH)
  AndU64(dst, src, Operand(smi), scratch, rc);
#else
  LoadSmiLiteral(scratch, smi);
  and_(dst, src, scratch, rc);
#endif
}

#define GenerateMemoryOperation(reg, mem, ri_op, rr_op) \
  {                                                     \
    int64_t offset = mem.offset();                      \
                                                        \
    if (mem.rb() == no_reg) {                           \
      if (!is_int16(offset)) {                          \
        /* cannot use d-form */                         \
        CHECK_NE(scratch, no_reg);                      \
        mov(scratch, Operand(offset));                  \
        rr_op(reg, MemOperand(mem.ra(), scratch));      \
      } else {                                          \
        ri_op(reg, mem);                                \
      }                                                 \
    } else {                                            \
      if (offset == 0) {                                \
        rr_op(reg, mem);                                \
      } else if (is_int16(offset)) {                    \
        CHECK_NE(scratch, no_reg);                      \
        addi(scratch, mem.rb(), Operand(offset));       \
        rr_op(reg, MemOperand(mem.ra(), scratch));      \
      } else {                                          \
        CHECK_NE(scratch, no_reg);                      \
        mov(scratch, Operand(offset));                  \
        add(scratch, scratch, mem.rb());                \
        rr_op(reg, MemOperand(mem.ra(), scratch));      \
      }                                                 \
    }                                                   \
  }

#define GenerateMemoryOperationRR(reg, mem, op)                \
  {                                                            \
    if (mem.offset() == 0) {                                   \
      if (mem.rb() != no_reg)                                  \
        op(reg, mem);                                          \
      else                                                     \
        op(reg, MemOperand(r0, mem.ra()));                     \
    } else if (is_int16(mem.offset())) {                       \
      if (mem.rb() != no_reg)                                  \
        addi(scratch, mem.rb(), Operand(mem.offset()));        \
      else                                                     \
        mov(scratch, Operand(mem.offset()));                   \
      op(reg, MemOperand(mem.ra(), scratch));                  \
    } else {                                                   \
      mov(scratch, Operand(mem.offset()));                     \
      if (mem.rb() != no_reg) add(scratch, scratch, mem.rb()); \
      op(reg, MemOperand(mem.ra(), scratch));                  \
    }                                                          \
  }

#define GenerateMemoryOperationPrefixed(reg, mem, ri_op, rip_op, rr_op)       \
  {                                                                           \
    int64_t offset = mem.offset();                                            \
                                                                              \
    if (mem.rb() == no_reg) {                                                 \
      if (is_int16(offset)) {                                                 \
        ri_op(reg, mem);                                                      \
      } else if (is_int34(offset) && CpuFeatures::IsSupported(PPC_10_PLUS)) { \
        rip_op(reg, mem);                                                     \
      } else {                                                                \
        /* cannot use d-form */                                               \
        CHECK_NE(scratch, no_reg);                                            \
        mov(scratch, Operand(offset));                                        \
        rr_op(reg, MemOperand(mem.ra(), scratch));                            \
      }                                                                       \
    } else {                                                                  \
      if (offset == 0) {                                                      \
        rr_op(reg, mem);                                                      \
      } else if (is_int16(offset)) {                                          \
        CHECK_NE(scratch, no_reg);                                            \
        addi(scratch, mem.rb(), Operand(offset));                             \
        rr_op(reg, MemOperand(mem.ra(), scratch));                            \
      } else {                                                                \
        CHECK_NE(scratch, no_reg);                                            \
        mov(scratch, Operand(offset));                                        \
        add(scratch, scratch, mem.rb());                                      \
        rr_op(reg, MemOperand(mem.ra(), scratch));                            \
      }                                                                       \
    }                                                                         \
  }

#define GenerateMemoryOperationWithAlign(reg, mem, ri_op, rr_op) \
  {                                                              \
    int64_t offset = mem.offset();                               \
    int misaligned = (offset & 3);                               \
                                                                 \
    if (mem.rb() == no_reg) {                                    \
      if (!is_int16(offset) || misaligned) {                     \
        /* cannot use d-form */                                  \
        CHECK_NE(scratch, no_reg);                               \
        mov(scratch, Operand(offset));                           \
        rr_op(reg, MemOperand(mem.ra(), scratch));               \
      } else {                                                   \
        ri_op(reg, mem);                                         \
      }                                                          \
    } else {                                                     \
      if (offset == 0) {                                         \
        rr_op(reg, mem);                                         \
      } else if (is_int16(offset)) {                             \
        CHECK_NE(scratch, no_reg);                               \
        addi(scratch, mem.rb(), Operand(offset));                \
        rr_op(reg, MemOperand(mem.ra(), scratch));               \
      } else {                                                   \
        CHECK_NE(scratch, no_reg);                               \
        mov(scratch, Operand(offset));                           \
        add(scratch, scratch, mem.rb());                         \
        rr_op(reg, MemOperand(mem.ra(), scratch));               \
      }                                                          \
    }                                                            \
  }

#define GenerateMemoryOperationWithAlignPrefixed(reg, mem, ri_op, rip_op,     \
                                                 rr_op)                       \
  {                                                                           \
    int64_t offset = mem.offset();                                            \
    int misaligned = (offset & 3);                                            \
                                                                              \
    if (mem.rb() == no_reg) {                                                 \
      if (is_int16(offset) && !misaligned) {                                  \
        ri_op(reg, mem);                                                      \
      } else if (is_int34(offset) && CpuFeatures::IsSupported(PPC_10_PLUS)) { \
        rip_op(reg, mem);                                                     \
      } else {                                                                \
        /* cannot use d-form */                                               \
        CHECK_NE(scratch, no_reg);                                            \
        mov(scratch, Operand(offset));                                        \
        rr_op(reg, MemOperand(mem.ra(), scratch));                            \
      }                                                                       \
    } else {                                                                  \
      if (offset == 0) {                                                      \
        rr_op(reg, mem);                                                      \
      } else if (is_int16(offset)) {                                          \
        CHECK_NE(scratch, no_reg);                                            \
        addi(scratch, mem.rb(), Operand(offset));                             \
        rr_op(reg, MemOperand(mem.ra(), scratch));                            \
      } else {                                                                \
        CHECK_NE(scratch, no_reg);                                            \
        mov(scratch, Operand(offset));                                        \
        add(scratch, scratch, mem.rb());                                      \
        rr_op(reg, MemOperand(mem.ra(), scratch));                            \
      }                                                                       \
    }                                                                         \
  }

#define MEM_OP_WITH_ALIGN_LIST(V) \
  V(LoadU64WithUpdate, ldu, ldux) \
  V(StoreU64WithUpdate, stdu, stdux)

#define MEM_OP_WITH_ALIGN_FUNCTION(name, ri_op, rr_op)           \
  void MacroAssembler::name(Register reg, const MemOperand& mem, \
                            Register scratch) {                  \
    GenerateMemoryOperationWithAlign(reg, mem, ri_op, rr_op);    \
  }
MEM_OP_WITH_ALIGN_LIST(MEM_OP_WITH_ALIGN_FUNCTION)
#undef MEM_OP_WITH_ALIGN_LIST
#undef MEM_OP_WITH_ALIGN_FUNCTION

#define MEM_OP_WITH_ALIGN_PREFIXED_LIST(V) \
  V(LoadS32, lwa, plwa, lwax)              \
  V(LoadU64, ld, pld, ldx)                 \
  V(StoreU64, std, pstd, stdx)

#define MEM_OP_WITH_ALIGN_PREFIXED_FUNCTION(name, ri_op, rip_op, rr_op)       \
  void MacroAssembler::name(Register reg, const MemOperand& mem,              \
                            Register scratch) {                               \
    GenerateMemoryOperationWithAlignPrefixed(reg, mem, ri_op, rip_op, rr_op); \
  }
MEM_OP_WITH_ALIGN_PREFIXED_LIST(MEM_OP_WITH_ALIGN_PREFIXED_FUNCTION)
#undef MEM_OP_WITH_ALIGN_PREFIXED_LIST
#undef MEM_OP_WITH_ALIGN_PREFIXED_FUNCTION

#define MEM_OP_LIST(V)                                 \
  V(LoadF64WithUpdate, DoubleRegister, lfdu, lfdux)    \
  V(LoadF32WithUpdate, DoubleRegister, lfsu, lfsux)    \
  V(StoreF64WithUpdate, DoubleRegister, stfdu, stfdux) \
  V(StoreF32WithUpdate, DoubleRegister, stfsu, stfsux)

#define MEM_OP_FUNCTION(name, result_t, ri_op, rr_op)            \
  void MacroAssembler::name(result_t reg, const MemOperand& mem, \
                            Register scratch) {                  \
    GenerateMemoryOperation(reg, mem, ri_op, rr_op);             \
  }
MEM_OP_LIST(MEM_OP_FUNCTION)
#undef MEM_OP_LIST
#undef MEM_OP_FUNCTION

#define MEM_OP_PREFIXED_LIST(V)                   \
  V(LoadU32, Register, lwz, plwz, lwzx)           \
  V(LoadS16, Register, lha, plha, lhax)           \
  V(LoadU16, Register, lhz, plhz, lhzx)           \
  V(LoadU8, Register, lbz, plbz, lbzx)            \
  V(StoreU32, Register, stw, pstw, stwx)          \
  V(StoreU16, Register, sth, psth, sthx)          \
  V(StoreU8, Register, stb, pstb, stbx)           \
  V(LoadF64, DoubleRegister, lfd, plfd, lfdx)     \
  V(LoadF32, DoubleRegister, lfs, plfs, lfsx)     \
  V(StoreF64, DoubleRegister, stfd, pstfd, stfdx) \
  V(StoreF32, DoubleRegister, stfs, pstfs, stfsx)

#define MEM_OP_PREFIXED_FUNCTION(name, result_t, ri_op, rip_op, rr_op) \
  void MacroAssembler::name(result_t reg, const MemOperand& mem,       \
                            Register scratch) {                        \
    GenerateMemoryOperationPrefixed(reg, mem, ri_op, rip_op, rr_op);   \
  }
MEM_OP_PREFIXED_LIST(MEM_OP_PREFIXED_FUNCTION)
#undef MEM_OP_PREFIXED_LIST
#undef MEM_OP_PREFIXED_FUNCTION

#define MEM_OP_SIMD_LIST(V)      \
  V(LoadSimd128, lxvx)           \
  V(StoreSimd128, stxvx)         \
  V(LoadSimd128Uint64, lxsdx)    \
  V(LoadSimd128Uint32, lxsiwzx)  \
  V(LoadSimd128Uint16, lxsihzx)  \
  V(LoadSimd128Uint8, lxsibzx)   \
  V(StoreSimd128Uint64, stxsdx)  \
  V(StoreSimd128Uint32, stxsiwx) \
  V(StoreSimd128Uint16, stxsihx) \
  V(StoreSimd128Uint8, stxsibx)

#define MEM_OP_SIMD_FUNCTION(name, rr_op)                               \
  void MacroAssembler::name(Simd128Register reg, const MemOperand& mem, \
                            Register scratch) {                         \
    GenerateMemoryOperationRR(reg, mem, rr_op);                         \
  }
MEM_OP_SIMD_LIST(MEM_OP_SIMD_FUNCTION)
#undef MEM_OP_SIMD_LIST
#undef MEM_OP_SIMD_FUNCTION

void MacroAssembler::LoadS8(Register dst, const MemOperand& mem,
                            Register scratch) {
  LoadU8(dst, mem, scratch);
  extsb(dst, dst);
}

#define MEM_LE_OP_LIST(V) \
  V(LoadU64, ldbrx)       \
  V(LoadU32, lwbrx)       \
  V(LoadU16, lhbrx)       \
  V(StoreU64, stdbrx)     \
  V(StoreU32, stwbrx)     \
  V(StoreU16, sthbrx)

#ifdef V8_TARGET_BIG_ENDIAN
#define MEM_LE_OP_FUNCTION(name, op)                                 \
  void MacroAssembler::name##LE(Register reg, const MemOperand& mem, \
                                Register scratch) {                  \
    GenerateMemoryOperationRR(reg, mem, op);                         \
  }
#else
#define MEM_LE_OP_FUNCTION(name, op)                                 \
  void MacroAssembler::name##LE(Register reg, const MemOperand& mem, \
                                Register scratch) {                  \
    name(reg, mem, scratch);                                         \
  }
#endif

MEM_LE_OP_LIST(MEM_LE_OP_FUNCTION)
#undef MEM_LE_OP_FUNCTION
#undef MEM_LE_OP_LIST

void MacroAssembler::LoadS32LE(Register dst, const MemOperand& mem,
                               Register scratch) {
#ifdef V8_TARGET_BIG_ENDIAN
  LoadU32LE(dst, mem, scratch);
  extsw(dst, dst);
#else
  LoadS32(dst, mem, scratch);
#endif
}

void MacroAssembler::LoadS16LE(Register dst, const MemOperand& mem,
                               Register scratch) {
#ifdef V8_TARGET_BIG_ENDIAN
  LoadU16LE(dst, mem, scratch);
  extsh(dst, dst);
#else
  LoadS16(dst, mem, scratch);
#endif
}

void MacroAssembler::LoadF64LE(DoubleRegister dst, const MemOperand& mem,
                               Register scratch, Register scratch2) {
#ifdef V8_TARGET_BIG_ENDIAN
  LoadU64LE(scratch, mem, scratch2);
  push(scratch);
  LoadF64(dst, MemOperand(sp), scratch2);
  pop(scratch);
#else
  LoadF64(dst, mem, scratch);
#endif
}

void MacroAssembler::LoadF32LE(DoubleRegister dst, const MemOperand& mem,
                               Register scratch, Register scratch2) {
#ifdef V8_TARGET_BIG_ENDIAN
  LoadU32LE(scratch, mem, scratch2);
  push(scratch);
  LoadF32(dst, MemOperand(sp, 4), scratch2);
  pop(scratch);
#else
  LoadF32(dst, mem, scratch);
#endif
}

void MacroAssembler::StoreF64LE(DoubleRegister dst, const MemOperand& mem,
                                Register scratch, Register scratch2) {
#ifdef V8_TARGET_BIG_ENDIAN
  StoreF64(dst, mem, scratch2);
  LoadU64(scratch, mem, scratch2);
  StoreU64LE(scratch, mem, scratch2);
#else
  StoreF64(dst, mem, scratch);
#endif
}

void MacroAssembler::StoreF32LE(DoubleRegister dst, const MemOperand& mem,
                                Register scratch, Register scratch2) {
#ifdef V8_TARGET_BIG_ENDIAN
  StoreF32(dst, mem, scratch2);
  LoadU32(scratch, mem, scratch2);
  StoreU32LE(scratch, mem, scratch2);
#else
  StoreF32(dst, mem, scratch);
#endif
}

// Simd Support.
#define SIMD_BINOP_LIST(V)         \
  V(F64x2Add, xvadddp)             \
  V(F64x2Sub, xvsubdp)             \
  V(F64x2Mul, xvmuldp)             \
  V(F64x2Div, xvdivdp)             \
  V(F64x2Eq, xvcmpeqdp)            \
  V(F32x4Add, vaddfp)              \
  V(F32x4Sub, vsubfp)              \
  V(F32x4Mul, xvmulsp)             \
  V(F32x4Div, xvdivsp)             \
  V(F32x4Min, vminfp)              \
  V(F32x4Max, vmaxfp)              \
  V(F32x4Eq, xvcmpeqsp)            \
  V(I64x2Add, vaddudm)             \
  V(I64x2Sub, vsubudm)             \
  V(I64x2Eq, vcmpequd)             \
  V(I64x2GtS, vcmpgtsd)            \
  V(I32x4Add, vadduwm)             \
  V(I32x4Sub, vsubuwm)             \
  V(I32x4Mul, vmuluwm)             \
  V(I32x4MinS, vminsw)             \
  V(I32x4MinU, vminuw)             \
  V(I32x4MaxS, vmaxsw)             \
  V(I32x4MaxU, vmaxuw)             \
  V(I32x4Eq, vcmpequw)             \
  V(I32x4GtS, vcmpgtsw)            \
  V(I32x4GtU, vcmpgtuw)            \
  V(I16x8Add, vadduhm)             \
  V(I16x8Sub, vsubuhm)             \
  V(I16x8MinS, vminsh)             \
  V(I16x8MinU, vminuh)             \
  V(I16x8MaxS, vmaxsh)             \
  V(I16x8MaxU, vmaxuh)             \
  V(I16x8Eq, vcmpequh)             \
  V(I16x8GtS, vcmpgtsh)            \
  V(I16x8GtU, vcmpgtuh)            \
  V(I16x8AddSatS, vaddshs)         \
  V(I16x8SubSatS, vsubshs)         \
  V(I16x8AddSatU, vadduhs)         \
  V(I16x8SubSatU, vsubuhs)         \
  V(I16x8RoundingAverageU, vavguh) \
  V(I8x16Add, vaddubm)             \
  V(I8x16Sub, vsububm)             \
  V(I8x16MinS, vminsb)             \
  V(I8x16MinU, vminub)             \
  V(I8x16MaxS, vmaxsb)             \
  V(I8x16MaxU, vmaxub)             \
  V(I8x16Eq, vcmpequb)             \
  V(I8x16GtS, vcmpgtsb)            \
  V(I8x16GtU, vcmpgtub)            \
  V(I8x16AddSatS, vaddsbs)         \
  V(I8x16SubSatS, vsubsbs)         \
  V(I8x16AddSatU, vaddubs)         \
  V(I8x16SubSatU, vsububs)         \
  V(I8x16RoundingAverageU, vavgub) \
  V(S128And, vand)                 \
  V(S128Or, vor)                   \
  V(S128Xor, vxor)                 \
  V(S128AndNot, vandc)

#define EMIT_SIMD_BINOP(name, op)                                      \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1, \
                            Simd128Register src2) {                    \
    op(dst, src1, src2);                                               \
  }
SIMD_BINOP_LIST(EMIT_SIMD_BINOP)
#undef EMIT_SIMD_BINOP
#undef SIMD_BINOP_LIST

#define SIMD_SHIFT_LIST(V) \
  V(I64x2Shl, vsld)        \
  V(I64x2ShrS, vsrad)      \
  V(I64x2ShrU, vsrd)       \
  V(I32x4Shl, vslw)        \
  V(I32x4ShrS, vsraw)      \
  V(I32x4ShrU, vsrw)       \
  V(I16x8Shl, vslh)        \
  V(I16x8ShrS, vsrah)      \
  V(I16x8ShrU, vsrh)       \
  V(I8x16Shl, vslb)        \
  V(I8x16ShrS, vsrab)      \
  V(I8x16ShrU, vsrb)

#define EMIT_SIMD_SHIFT(name, op)                                      \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1, \
                            Register src2, Simd128Register scratch) {  \
    mtvsrd(scratch, src2);                                             \
    vspltb(scratch, scratch, Operand(7));                              \
    op(dst, src1, scratch);                                            \
  }                                                                    \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1, \
                            const Operand& src2, Register scratch1,    \
                            Simd128Register scratch2) {                \
    mov(scratch1, src2);                                               \
    name(dst, src1, scratch1, scratch2);                               \
  }
SIMD_SHIFT_LIST(EMIT_SIMD_SHIFT)
#undef EMIT_SIMD_SHIFT
#undef SIMD_SHIFT_LIST

#define SIMD_UNOP_LIST(V)            \
  V(F64x2Abs, xvabsdp)               \
  V(F64x2Neg, xvnegdp)               \
  V(F64x2Sqrt, xvsqrtdp)             \
  V(F64x2Ceil, xvrdpip)              \
  V(F64x2Floor, xvrdpim)             \
  V(F64x2Trunc, xvrdpiz)             \
  V(F32x4Abs, xvabssp)               \
  V(F32x4Neg, xvnegsp)               \
  V(F32x4Sqrt, xvsqrtsp)             \
  V(F32x4Ceil, xvrspip)              \
  V(F32x4Floor, xvrspim)             \
  V(F32x4Trunc, xvrspiz)             \
  V(F32x4SConvertI32x4, xvcvsxwsp)   \
  V(F32x4UConvertI32x4, xvcvuxwsp)   \
  V(I64x2Neg, vnegd)                 \
  V(I64x2SConvertI32x4Low, vupklsw)  \
  V(I64x2SConvertI32x4High, vupkhsw) \
  V(I32x4Neg, vnegw)                 \
  V(I32x4SConvertI16x8Low, vupklsh)  \
  V(I32x4SConvertI16x8High, vupkhsh) \
  V(I32x4UConvertF32x4, xvcvspuxws)  \
  V(I16x8SConvertI8x16Low, vupklsb)  \
  V(I16x8SConvertI8x16High, vupkhsb) \
  V(I8x16Popcnt, vpopcntb)

#define EMIT_SIMD_UNOP(name, op)                                        \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src) { \
    op(dst, src);                                                       \
  }
SIMD_UNOP_LIST(EMIT_SIMD_UNOP)
#undef EMIT_SIMD_UNOP
#undef SIMD_UNOP_LIST

#define EXT_MUL(dst_even, dst_odd, mul_even, mul_odd) \
  mul_even(dst_even, src1, src2);                     \
  mul_odd(dst_odd, src1, src2);
#define SIMD_EXT_MUL_LIST(V)                         \
  V(I32x4ExtMulLowI16x8S, vmulesh, vmulosh, vmrglw)  \
  V(I32x4ExtMulHighI16x8S, vmulesh, vmulosh, vmrghw) \
  V(I32x4ExtMulLowI16x8U, vmuleuh, vmulouh, vmrglw)  \
  V(I32x4ExtMulHighI16x8U, vmuleuh, vmulouh, vmrghw) \
  V(I16x8ExtMulLowI8x16S, vmulesb, vmulosb, vmrglh)  \
  V(I16x8ExtMulHighI8x16S, vmulesb, vmulosb, vmrghh) \
  V(I16x8ExtMulLowI8x16U, vmuleub, vmuloub, vmrglh)  \
  V(I16x8ExtMulHighI8x16U, vmuleub, vmuloub, vmrghh)

#define EMIT_SIMD_EXT_MUL(name, mul_even, mul_odd, merge)                    \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1,       \
                            Simd128Register src2, Simd128Register scratch) { \
    EXT_MUL(scratch, dst, mul_even, mul_odd)                                 \
    merge(dst, scratch, dst);                                                \
  }
SIMD_EXT_MUL_LIST(EMIT_SIMD_EXT_MUL)
#undef EMIT_SIMD_EXT_MUL
#undef SIMD_EXT_MUL_LIST

#define SIMD_ALL_TRUE_LIST(V) \
  V(I64x2AllTrue, vcmpgtud)   \
  V(I32x4AllTrue, vcmpgtuw)   \
  V(I16x8AllTrue, vcmpgtuh)   \
  V(I8x16AllTrue, vcmpgtub)

#define EMIT_SIMD_ALL_TRUE(name, op)                              \
  void MacroAssembler::name(Register dst, Simd128Register src,    \
                            Register scratch1, Register scratch2, \
                            Simd128Register scratch3) {           \
    constexpr uint8_t fxm = 0x2; /* field mask. */                \
    constexpr int bit_number = 24;                                \
    li(scratch1, Operand(0));                                     \
    li(scratch2, Operand(1));                                     \
    /* Check if all lanes > 0, if not then return false.*/        \
    vxor(scratch3, scratch3, scratch3);                           \
    mtcrf(scratch1, fxm); /* Clear cr6.*/                         \
    op(scratch3, src, scratch3, SetRC);                           \
    isel(dst, scratch2, scratch1, bit_number);                    \
  }
SIMD_ALL_TRUE_LIST(EMIT_SIMD_ALL_TRUE)
#undef EMIT_SIMD_ALL_TRUE
#undef SIMD_ALL_TRUE_LIST

#define SIMD_BITMASK_LIST(V)                      \
  V(I64x2BitMask, vextractdm, 0x8080808080800040) \
  V(I32x4BitMask, vextractwm, 0x8080808000204060) \
  V(I16x8BitMask, vextracthm, 0x10203040506070)

#define EMIT_SIMD_BITMASK(name, op, indicies)                              \
  void MacroAssembler::name(Register dst, Simd128Register src,             \
                            Register scratch1, Simd128Register scratch2) { \
    if (CpuFeatures::IsSupported(PPC_10_PLUS)) {                           \
      op(dst, src);                                                        \
    } else {                                                               \
      mov(scratch1, Operand(indicies)); /* Select 0 for the high bits. */  \
      mtvsrd(scratch2, scratch1);                                          \
      vbpermq(scratch2, src, scratch2);                                    \
      vextractub(scratch2, scratch2, Operand(6));                          \
      mfvsrd(dst, scratch2);                                               \
    }                                                                      \
  }
SIMD_BITMASK_LIST(EMIT_SIMD_BITMASK)
#undef EMIT_SIMD_BITMASK
#undef SIMD_BITMASK_LIST

#define SIMD_QFM_LIST(V)   \
  V(F64x2Qfma, xvmaddmdp)  \
  V(F64x2Qfms, xvnmsubmdp) \
  V(F32x4Qfma, xvmaddmsp)  \
  V(F32x4Qfms, xvnmsubmsp)

#define EMIT_SIMD_QFM(name, op)                                         \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1,  \
                            Simd128Register src2, Simd128Register src3, \
                            Simd128Register scratch) {                  \
    Simd128Register dest = dst;                                         \
    if (dst != src1) {                                                  \
      vor(scratch, src1, src1);                                         \
      dest = scratch;                                                   \
    }                                                                   \
    op(dest, src2, src3);                                               \
    if (dest != dst) {                                                  \
      vor(dst, dest, dest);                                             \
    }                                                                   \
  }
SIMD_QFM_LIST(EMIT_SIMD_QFM)
#undef EMIT_SIMD_QFM
#undef SIMD_QFM_LIST

void MacroAssembler::I64x2ExtMulLowI32x4S(Simd128Register dst,
                                          Simd128Register src1,
                                          Simd128Register src2,
                                          Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 8;
  EXT_MUL(scratch, dst, vmulesw, vmulosw)
  vextractd(scratch, scratch, Operand(1 * lane_width_in_bytes));
  vinsertd(dst, scratch, Operand(0));
}

void MacroAssembler::I64x2ExtMulHighI32x4S(Simd128Register dst,
                                           Simd128Register src1,
                                           Simd128Register src2,
                                           Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 8;
  EXT_MUL(scratch, dst, vmulesw, vmulosw)
  vinsertd(scratch, dst, Operand(1 * lane_width_in_bytes));
  vor(dst, scratch, scratch);
}

void MacroAssembler::I64x2ExtMulLowI32x4U(Simd128Register dst,
                                          Simd128Register src1,
                                          Simd128Register src2,
                                          Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 8;
  EXT_MUL(scratch, dst, vmuleuw, vmulouw)
  vextractd(scratch, scratch, Operand(1 * lane_width_in_bytes));
  vinsertd(dst, scratch, Operand(0));
}

void MacroAssembler::I64x2ExtMulHighI32x4U(Simd128Register dst,
                                           Simd128Register src1,
                                           Simd128Register src2,
                                           Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 8;
  EXT_MUL(scratch, dst, vmuleuw, vmulouw)
  vinsertd(scratch, dst, Operand(1 * lane_width_in_bytes));
  vor(dst, scratch, scratch);
}
#undef EXT_MUL

void MacroAssembler::LoadSimd128LE(Simd128Register dst, const MemOperand& mem,
                                   Register scratch) {
#ifdef V8_TARGET_BIG_ENDIAN
  LoadSimd128(dst, mem, scratch);
  xxbrq(dst, dst);
#else
  LoadSimd128(dst, mem, scratch);
#endif
}

void MacroAssembler::StoreSimd128LE(Simd128Register src, const MemOperand& mem,
                                    Register scratch1,
                                    Simd128Register scratch2) {
#ifdef V8_TARGET_BIG_ENDIAN
  xxbrq(scratch2, src);
  StoreSimd128(scratch2, mem, scratch1);
#else
  StoreSimd128(src, mem, scratch1);
#endif
}

void MacroAssembler::F64x2Splat(Simd128Register dst, DoubleRegister src,
                                Register scratch) {
  constexpr int lane_width_in_bytes = 8;
  MovDoubleToInt64(scratch, src);
  mtvsrd(dst, scratch);
  vinsertd(dst, dst, Operand(1 * lane_width_in_bytes));
}

void MacroAssembler::F32x4Splat(Simd128Register dst, DoubleRegister src,
                                DoubleRegister scratch1, Register scratch2) {
  MovFloatToInt(scratch2, src, scratch1);
  mtvsrd(dst, scratch2);
  vspltw(dst, dst, Operand(1));
}

void MacroAssembler::I64x2Splat(Simd128Register dst, Register src) {
  constexpr int lane_width_in_bytes = 8;
  mtvsrd(dst, src);
  vinsertd(dst, dst, Operand(1 * lane_width_in_bytes));
}

void MacroAssembler::I32x4Splat(Simd128Register dst, Register src) {
  mtvsrd(dst, src);
  vspltw(dst, dst, Operand(1));
}

void MacroAssembler::I16x8Splat(Simd128Register dst, Register src) {
  mtvsrd(dst, src);
  vsplth(dst, dst, Operand(3));
}

void MacroAssembler::I8x16Splat(Simd128Register dst, Register src) {
  mtvsrd(dst, src);
  vspltb(dst, dst, Operand(7));
}

void MacroAssembler::F64x2ExtractLane(DoubleRegister dst, Simd128Register src,
                                      uint8_t imm_lane_idx,
                                      Simd128Register scratch1,
                                      Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  vextractd(scratch1, src, Operand((1 - imm_lane_idx) * lane_width_in_bytes));
  mfvsrd(scratch2, scratch1);
  MovInt64ToDouble(dst, scratch2);
}

void MacroAssembler::F32x4ExtractLane(DoubleRegister dst, Simd128Register src,
                                      uint8_t imm_lane_idx,
                                      Simd128Register scratch1,
                                      Register scratch2, Register scratch3) {
  constexpr int lane_width_in_bytes = 4;
  vextractuw(scratch1, src, Operand((3 - imm_lane_idx) * lane_width_in_bytes));
  mfvsrd(scratch2, scratch1);
  MovIntToFloat(dst, scratch2, scratch3);
}

void MacroAssembler::I64x2ExtractLane(Register dst, Simd128Register src,
                                      uint8_t imm_lane_idx,
                                      Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 8;
  vextractd(scratch, src, Operand((1 - imm_lane_idx) * lane_width_in_bytes));
  mfvsrd(dst, scratch);
}

void MacroAssembler::I32x4ExtractLane(Register dst, Simd128Register src,
                                      uint8_t imm_lane_idx,
                                      Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 4;
  vextractuw(scratch, src, Operand((3 - imm_lane_idx) * lane_width_in_bytes));
  mfvsrd(dst, scratch);
}

void MacroAssembler::I16x8ExtractLaneU(Register dst, Simd128Register src,
                                       uint8_t imm_lane_idx,
                                       Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 2;
  vextractuh(scratch, src, Operand((7 - imm_lane_idx) * lane_width_in_bytes));
  mfvsrd(dst, scratch);
}

void MacroAssembler::I16x8ExtractLaneS(Register dst, Simd128Register src,
                                       uint8_t imm_lane_idx,
                                       Simd128Register scratch) {
  I16x8ExtractLaneU(dst, src, imm_lane_idx, scratch);
  extsh(dst, dst);
}

void MacroAssembler::I8x16ExtractLaneU(Register dst, Simd128Register src,
                                       uint8_t imm_lane_idx,
                                       Simd128Register scratch) {
  vextractub(scratch, src, Operand(15 - imm_lane_idx));
  mfvsrd(dst, scratch);
}

void MacroAssembler::I8x16ExtractLaneS(Register dst, Simd128Register src,
                                       uint8_t imm_lane_idx,
                                       Simd128Register scratch) {
  I8x16ExtractLaneU(dst, src, imm_lane_idx, scratch);
  extsb(dst, dst);
}

void MacroAssembler::F64x2ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      DoubleRegister src2, uint8_t imm_lane_idx,
                                      Register scratch1,
                                      Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  if (src1 != dst) {
    vor(dst, src1, src1);
  }
  MovDoubleToInt64(scratch1, src2);
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    vinsd(dst, scratch1, Operand((1 - imm_lane_idx) * lane_width_in_bytes));
  } else {
    mtvsrd(scratch2, scratch1);
    vinsertd(dst, scratch2, Operand((1 - imm_lane_idx) * lane_width_in_bytes));
  }
}

void MacroAssembler::F32x4ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      DoubleRegister src2, uint8_t imm_lane_idx,
                                      Register scratch1,
                                      DoubleRegister scratch2,
                                      Simd128Register scratch3) {
  constexpr int lane_width_in_bytes = 4;
  if (src1 != dst) {
    vor(dst, src1, src1);
  }
  MovFloatToInt(scratch1, src2, scratch2);
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    vinsw(dst, scratch1, Operand((3 - imm_lane_idx) * lane_width_in_bytes));
  } else {
    mtvsrd(scratch3, scratch1);
    vinsertw(dst, scratch3, Operand((3 - imm_lane_idx) * lane_width_in_bytes));
  }
}

void MacroAssembler::I64x2ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      Register src2, uint8_t imm_lane_idx,
                                      Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 8;
  if (src1 != dst) {
    vor(dst, src1, src1);
  }
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    vinsd(dst, src2, Operand((1 - imm_lane_idx) * lane_width_in_bytes));
  } else {
    mtvsrd(scratch, src2);
    vinsertd(dst, scratch, Operand((1 - imm_lane_idx) * lane_width_in_bytes));
  }
}

void MacroAssembler::I32x4ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      Register src2, uint8_t imm_lane_idx,
                                      Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 4;
  if (src1 != dst) {
    vor(dst, src1, src1);
  }
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    vinsw(dst, src2, Operand((3 - imm_lane_idx) * lane_width_in_bytes));
  } else {
    mtvsrd(scratch, src2);
    vinsertw(dst, scratch, Operand((3 - imm_lane_idx) * lane_width_in_bytes));
  }
}

void MacroAssembler::I16x8ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      Register src2, uint8_t imm_lane_idx,
                                      Simd128Register scratch) {
  constexpr int lane_width_in_bytes = 2;
  if (src1 != dst) {
    vor(dst, src1, src1);
  }
  mtvsrd(scratch, src2);
  vinserth(dst, scratch, Operand((7 - imm_lane_idx) * lane_width_in_bytes));
}

void MacroAssembler::I8x16ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      Register src2, uint8_t imm_lane_idx,
                                      Simd128Register scratch) {
  if (src1 != dst) {
    vor(dst, src1, src1);
  }
  mtvsrd(scratch, src2);
  vinsertb(dst, scratch, Operand(15 - imm_lane_idx));
}

void MacroAssembler::I64x2Mul(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Register scratch1,
                              Register scratch2, Register scratch3,
                              Simd128Register scratch4) {
  constexpr int lane_width_in_bytes = 8;
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    vmulld(dst, src1, src2);
  } else {
    Register scratch_1 = scratch1;
    Register scratch_2 = scratch2;
    for (int i = 0; i < 2; i++) {
      if (i > 0) {
        vextractd(scratch4, src1, Operand(1 * lane_width_in_bytes));
        vextractd(dst, src2, Operand(1 * lane_width_in_bytes));
        src1 = scratch4;
        src2 = dst;
      }
      mfvsrd(scratch_1, src1);
      mfvsrd(scratch_2, src2);
      mulld(scratch_1, scratch_1, scratch_2);
      scratch_1 = scratch2;
      scratch_2 = scratch3;
    }
    mtvsrdd(dst, scratch1, scratch2);
  }
}

void MacroAssembler::I16x8Mul(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2) {
  vxor(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  vmladduhm(dst, src1, src2, kSimd128RegZero);
}

#define F64X2_MIN_MAX_NAN(result)                        \
  xvcmpeqdp(scratch2, src1, src1);                       \
  vsel(result, src1, result, scratch2);                  \
  xvcmpeqdp(scratch2, src2, src2);                       \
  vsel(dst, src2, result, scratch2);                     \
  /* Use xvmindp to turn any selected SNANs to QNANs. */ \
  xvmindp(dst, dst, dst);
void MacroAssembler::F64x2Min(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch1,
                              Simd128Register scratch2) {
  xvmindp(scratch1, src1, src2);
  // We need to check if an input is NAN and preserve it.
  F64X2_MIN_MAX_NAN(scratch1)
}

void MacroAssembler::F64x2Max(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch1,
                              Simd128Register scratch2) {
  xvmaxdp(scratch1, src1, src2);
  // We need to check if an input is NAN and preserve it.
  F64X2_MIN_MAX_NAN(scratch1)
}
#undef F64X2_MIN_MAX_NAN

void MacroAssembler::F64x2Lt(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  xvcmpgtdp(dst, src2, src1);
}

void MacroAssembler::F64x2Le(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  xvcmpgedp(dst, src2, src1);
}

void MacroAssembler::F64x2Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2, Simd128Register scratch) {
  xvcmpeqdp(scratch, src1, src2);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::F32x4Lt(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  xvcmpgtsp(dst, src2, src1);
}

void MacroAssembler::F32x4Le(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  xvcmpgesp(dst, src2, src1);
}

void MacroAssembler::F32x4Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2, Simd128Register scratch) {
  xvcmpeqsp(scratch, src1, src2);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I64x2Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2, Simd128Register scratch) {
  vcmpequd(scratch, src1, src2);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I64x2GeS(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vcmpgtsd(scratch, src2, src1);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I32x4Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2, Simd128Register scratch) {
  vcmpequw(scratch, src1, src2);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I32x4GeS(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vcmpgtsw(scratch, src2, src1);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I32x4GeU(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vcmpequw(scratch, src1, src2);
  vcmpgtuw(dst, src1, src2);
  vor(dst, dst, scratch);
}

void MacroAssembler::I16x8Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2, Simd128Register scratch) {
  vcmpequh(scratch, src1, src2);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I16x8GeS(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vcmpgtsh(scratch, src2, src1);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I16x8GeU(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vcmpequh(scratch, src1, src2);
  vcmpgtuh(dst, src1, src2);
  vor(dst, dst, scratch);
}

void MacroAssembler::I8x16Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2, Simd128Register scratch) {
  vcmpequb(scratch, src1, src2);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I8x16GeS(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vcmpgtsb(scratch, src2, src1);
  vnor(dst, scratch, scratch);
}

void MacroAssembler::I8x16GeU(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vcmpequb(scratch, src1, src2);
  vcmpgtub(dst, src1, src2);
  vor(dst, dst, scratch);
}

void MacroAssembler::I64x2Abs(Simd128Register dst, Simd128Register src,
                              Simd128Register scratch) {
  constexpr int shift_bits = 63;
  xxspltib(scratch, Operand(shift_bits));
  vsrad(scratch, src, scratch);
  vxor(dst, src, scratch);
  vsubudm(dst, dst, scratch);
}
void MacroAssembler::I32x4Abs(Simd128Register dst, Simd128Register src,
                              Simd128Register scratch) {
  constexpr int shift_bits = 31;
  xxspltib(scratch, Operand(shift_bits));
  vsraw(scratch, src, scratch);
  vxor(dst, src, scratch);
  vsubuwm(dst, dst, scratch);
}
void MacroAssembler::I16x8Abs(Simd128Register dst, Simd128Register src,
                              Simd128Register scratch) {
  constexpr int shift_bits = 15;
  xxspltib(scratch, Operand(shift_bits));
  vsrah(scratch, src, scratch);
  vxor(dst, src, scratch);
  vsubuhm(dst, dst, scratch);
}
void MacroAssembler::I16x8Neg(Simd128Register dst, Simd128Register src,
                              Simd128Register scratch) {
  vspltish(scratch, Operand(1));
  vnor(dst, src, src);
  vadduhm(dst, scratch, dst);
}
void MacroAssembler::I8x16Abs(Simd128Register dst, Simd128Register src,
                              Simd128Register scratch) {
  constexpr int shift_bits = 7;
  xxspltib(scratch, Operand(shift_bits));
  vsrab(scratch, src, scratch);
  vxor(dst, src, scratch);
  vsububm(dst, dst, scratch);
}
void MacroAssembler::I8x16Neg(Simd128Register dst, Simd128Register src,
                              Simd128Register scratch) {
  xxspltib(scratch, Operand(1));
  vnor(dst, src, src);
  vaddubm(dst, scratch, dst);
}

void MacroAssembler::F64x2Pmin(Simd128Register dst, Simd128Register src1,
                               Simd128Register src2, Simd128Register scratch) {
  xvcmpgtdp(kScratchSimd128Reg, src1, src2);
  vsel(dst, src1, src2, kScratchSimd128Reg);
}

void MacroAssembler::F64x2Pmax(Simd128Register dst, Simd128Register src1,
                               Simd128Register src2, Simd128Register scratch) {
  xvcmpgtdp(kScratchSimd128Reg, src2, src1);
  vsel(dst, src1, src2, kScratchSimd128Reg);
}

void MacroAssembler::F32x4Pmin(Simd128Register dst, Simd128Register src1,
                               Simd128Register src2, Simd128Register scratch) {
  xvcmpgtsp(kScratchSimd128Reg, src1, src2);
  vsel(dst, src1, src2, kScratchSimd128Reg);
}

void MacroAssembler::F32x4Pmax(Simd128Register dst, Simd128Register src1,
                               Simd128Register src2, Simd128Register scratch) {
  xvcmpgtsp(kScratchSimd128Reg, src2, src1);
  vsel(dst, src1, src2, kScratchSimd128Reg);
}

void MacroAssembler::I32x4SConvertF32x4(Simd128Register dst,
                                        Simd128Register src,
                                        Simd128Register scratch) {
  // NaN to 0
  xvcmpeqsp(scratch, src, src);
  vand(scratch, src, scratch);
  xvcvspsxws(dst, scratch);
}

void MacroAssembler::I16x8SConvertI32x4(Simd128Register dst,
                                        Simd128Register src1,
                                        Simd128Register src2) {
  vpkswss(dst, src2, src1);
}

void MacroAssembler::I16x8UConvertI32x4(Simd128Register dst,
                                        Simd128Register src1,
                                        Simd128Register src2) {
  vpkswus(dst, src2, src1);
}

void MacroAssembler::I8x16SConvertI16x8(Simd128Register dst,
                                        Simd128Register src1,
                                        Simd128Register src2) {
  vpkshss(dst, src2, src1);
}

void MacroAssembler::I8x16UConvertI16x8(Simd128Register dst,
                                        Simd128Register src1,
                                        Simd128Register src2) {
  vpkshus(dst, src2, src1);
}

void MacroAssembler::F64x2ConvertLowI32x4S(Simd128Register dst,
                                           Simd128Register src) {
  vupklsw(dst, src);
  xvcvsxddp(dst, dst);
}

void MacroAssembler::F64x2ConvertLowI32x4U(Simd128Register dst,
                                           Simd128Register src,
                                           Register scratch1,
                                           Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  vupklsw(dst, src);
  // Zero extend.
  mov(scratch1, Operand(0xFFFFFFFF));
  mtvsrd(scratch2, scratch1);
  vinsertd(scratch2, scratch2, Operand(1 * lane_width_in_bytes));
  vand(dst, scratch2, dst);
  xvcvuxddp(dst, dst);
}

void MacroAssembler::I64x2UConvertI32x4Low(Simd128Register dst,
                                           Simd128Register src,
                                           Register scratch1,
                                           Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  vupklsw(dst, src);
  // Zero extend.
  mov(scratch1, Operand(0xFFFFFFFF));
  mtvsrd(scratch2, scratch1);
  vinsertd(scratch2, scratch2, Operand(1 * lane_width_in_bytes));
  vand(dst, scratch2, dst);
}

void MacroAssembler::I64x2UConvertI32x4High(Simd128Register dst,
                                            Simd128Register src,
                                            Register scratch1,
                                            Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  vupkhsw(dst, src);
  // Zero extend.
  mov(scratch1, Operand(0xFFFFFFFF));
  mtvsrd(scratch2, scratch1);
  vinsertd(scratch2, scratch2, Operand(1 * lane_width_in_bytes));
  vand(dst, scratch2, dst);
}

void MacroAssembler::I32x4UConvertI16x8Low(Simd128Register dst,
                                           Simd128Register src,
                                           Register scratch1,
                                           Simd128Register scratch2) {
  vupklsh(dst, src);
  // Zero extend.
  mov(scratch1, Operand(0xFFFF));
  mtvsrd(scratch2, scratch1);
  vspltw(scratch2, scratch2, Operand(1));
  vand(dst, scratch2, dst);
}

void MacroAssembler::I32x4UConvertI16x8High(Simd128Register dst,
                                            Simd128Register src,
                                            Register scratch1,
                                            Simd128Register scratch2) {
  vupkhsh(dst, src);
  // Zero extend.
  mov(scratch1, Operand(0xFFFF));
  mtvsrd(scratch2, scratch1);
  vspltw(scratch2, scratch2, Operand(1));
  vand(dst, scratch2, dst);
}

void MacroAssembler::I16x8UConvertI8x16Low(Simd128Register dst,
                                           Simd128Register src,
                                           Register scratch1,
                                           Simd128Register scratch2) {
  vupklsb(dst, src);
  // Zero extend.
  li(scratch1, Operand(0xFF));
  mtvsrd(scratch2, scratch1);
  vsplth(scratch2, scratch2, Operand(3));
  vand(dst, scratch2, dst);
}

void MacroAssembler::I16x8UConvertI8x16High(Simd128Register dst,
                                            Simd128Register src,
                                            Register scratch1,
                                            Simd128Register scratch2) {
  vupkhsb(dst, src);
  // Zero extend.
  li(scratch1, Operand(0xFF));
  mtvsrd(scratch2, scratch1);
  vsplth(scratch2, scratch2, Operand(3));
  vand(dst, scratch2, dst);
}

void MacroAssembler::I8x16BitMask(Register dst, Simd128Register src,
                                  Register scratch1, Register scratch2,
                                  Simd128Register scratch3) {
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    vextractbm(dst, src);
  } else {
    mov(scratch1, Operand(0x8101820283038));
    mov(scratch2, Operand(0x4048505860687078));
    mtvsrdd(scratch3, scratch1, scratch2);
    vbpermq(scratch3, src, scratch3);
    mfvsrd(dst, scratch3);
  }
}

void MacroAssembler::I32x4DotI16x8S(Simd128Register dst, Simd128Register src1,
                                    Simd128Register src2) {
  vxor(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  vmsumshm(dst, src1, src2, kSimd128RegZero);
}

void MacroAssembler::I32x4DotI8x16AddS(Simd128Register dst,
                                       Simd128Register src1,
                                       Simd128Register src2,
                                       Simd128Register src3) {
  vmsummbm(dst, src1, src2, src3);
}

void MacroAssembler::I16x8DotI8x16S(Simd128Register dst, Simd128Register src1,
                                    Simd128Register src2,
                                    Simd128Register scratch) {
  vmulesb(scratch, src1, src2);
  vmulosb(dst, src1, src2);
  vadduhm(dst, scratch, dst);
}

void MacroAssembler::I16x8Q15MulRSatS(Simd128Register dst, Simd128Register src1,
                                      Simd128Register src2) {
  vxor(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  vmhraddshs(dst, src1, src2, kSimd128RegZero);
}

void MacroAssembler::I8x16Swizzle(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch) {
  // Saturate the indices to 5 bits. Input indices more than 31 should
  // return 0.
  xxspltib(scratch, Operand(31));
  vminub(scratch, src2, scratch);
  // Input needs to be reversed.
  xxbrq(dst, src1);
  vxor(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
  vperm(dst, dst, kSimd128RegZero, scratch);
}

void MacroAssembler::I8x16Shuffle(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2, uint64_t high,
                                  uint64_t low, Register scratch1,
                                  Register scratch2, Simd128Register scratch3) {
  mov(scratch1, Operand(low));
  mov(scratch2, Operand(high));
  mtvsrdd(scratch3, scratch2, scratch1);
  vperm(dst, src1, src2, scratch3);
}

#define EXT_ADD_PAIRWISE(splat, mul_even, mul_odd, add) \
  splat(scratch1, Operand(1));                          \
  mul_even(scratch2, src, scratch1);                    \
  mul_odd(scratch1, src, scratch1);                     \
  add(dst, scratch2, scratch1);
void MacroAssembler::I32x4ExtAddPairwiseI16x8S(Simd128Register dst,
                                               Simd128Register src,
                                               Simd128Register scratch1,
                                               Simd128Register scratch2) {
  EXT_ADD_PAIRWISE(vspltish, vmulesh, vmulosh, vadduwm)
}
void MacroAssembler::I32x4ExtAddPairwiseI16x8U(Simd128Register dst,
                                               Simd128Register src,
                                               Simd128Register scratch1,
                                               Simd128Register scratch2) {
  EXT_ADD_PAIRWISE(vspltish, vmuleuh, vmulouh, vadduwm)
}
void MacroAssembler::I16x8ExtAddPairwiseI8x16S(Simd128Register dst,
                                               Simd128Register src,
                                               Simd128Register scratch1,
                                               Simd128Register scratch2) {
  EXT_ADD_PAIRWISE(xxspltib, vmulesb, vmulosb, vadduhm)
}
void MacroAssembler::I16x8ExtAddPairwiseI8x16U(Simd128Register dst,
                                               Simd128Register src,
                                               Simd128Register scratch1,
                                               Simd128Register scratch2) {
  EXT_ADD_PAIRWISE(xxspltib, vmuleub, vmuloub, vadduhm)
}
#undef EXT_ADD_PAIRWISE

void MacroAssembler::F64x2PromoteLowF32x4(Simd128Register dst,
                                          Simd128Register src) {
  constexpr int lane_number = 8;
  vextractd(dst, src, Operand(lane_number));
  vinsertw(dst, dst, Operand(lane_number));
  xvcvspdp(dst, dst);
}

void MacroAssembler::F32x4DemoteF64x2Zero(Simd128Register dst,
                                          Simd128Register src,
                                          Simd128Register scratch) {
  constexpr int lane_number = 8;
  xvcvdpsp(scratch, src);
  vextractuw(dst, scratch, Operand(lane_number));
  vinsertw(scratch, dst, Operand(4));
  vxor(dst, dst, dst);
  vinsertd(dst, scratch, Operand(lane_number));
}

void MacroAssembler::I32x4TruncSatF64x2SZero(Simd128Register dst,
                                             Simd128Register src,
                                             Simd128Register scratch) {
  constexpr int lane_number = 8;
  // NaN to 0.
  xvcmpeqdp(scratch, src, src);
  vand(scratch, src, scratch);
  xvcvdpsxws(scratch, scratch);
  vextractuw(dst, scratch, Operand(lane_number));
  vinsertw(scratch, dst, Operand(4));
  vxor(dst, dst, dst);
  vinsertd(dst, scratch, Operand(lane_number));
}

void MacroAssembler::I32x4TruncSatF64x2UZero(Simd128Register dst,
                                             Simd128Register src,
                                             Simd128Register scratch) {
  constexpr int lane_number = 8;
  xvcvdpuxws(scratch, src);
  vextractuw(dst, scratch, Operand(lane_number));
  vinsertw(scratch, dst, Operand(4));
  vxor(dst, dst, dst);
  vinsertd(dst, scratch, Operand(lane_number));
}

#if V8_TARGET_BIG_ENDIAN
#define MAYBE_REVERSE_BYTES(reg, instr) instr(reg, reg);
#else
#define MAYBE_REVERSE_BYTES(reg, instr)
#endif
void MacroAssembler::LoadLane64LE(Simd128Register dst, const MemOperand& mem,
                                  int lane, Register scratch1,
                                  Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  LoadSimd128Uint64(scratch2, mem, scratch1);
  MAYBE_REVERSE_BYTES(scratch2, xxbrd)
  vinsertd(dst, scratch2, Operand((1 - lane) * lane_width_in_bytes));
}

void MacroAssembler::LoadLane32LE(Simd128Register dst, const MemOperand& mem,
                                  int lane, Register scratch1,
                                  Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 4;
  LoadSimd128Uint32(scratch2, mem, scratch1);
  MAYBE_REVERSE_BYTES(scratch2, xxbrw)
  vinsertw(dst, scratch2, Operand((3 - lane) * lane_width_in_bytes));
}

void MacroAssembler::LoadLane16LE(Simd128Register dst, const MemOperand& mem,
                                  int lane, Register scratch1,
                                  Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 2;
  LoadSimd128Uint16(scratch2, mem, scratch1);
  MAYBE_REVERSE_BYTES(scratch2, xxbrh)
  vinserth(dst, scratch2, Operand((7 - lane) * lane_width_in_bytes));
}

void MacroAssembler::LoadLane8LE(Simd128Register dst, const MemOperand& mem,
                                 int lane, Register scratch1,
                                 Simd128Register scratch2) {
  LoadSimd128Uint8(scratch2, mem, scratch1);
  vinsertb(dst, scratch2, Operand((15 - lane)));
}

void MacroAssembler::StoreLane64LE(Simd128Register src, const MemOperand& mem,
                                   int lane, Register scratch1,
                                   Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  vextractd(scratch2, src, Operand((1 - lane) * lane_width_in_bytes));
  MAYBE_REVERSE_BYTES(scratch2, xxbrd)
  StoreSimd128Uint64(scratch2, mem, scratch1);
}

void MacroAssembler::StoreLane32LE(Simd128Register src, const MemOperand& mem,
                                   int lane, Register scratch1,
                                   Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 4;
  vextractuw(scratch2, src, Operand((3 - lane) * lane_width_in_bytes));
  MAYBE_REVERSE_BYTES(scratch2, xxbrw)
  StoreSimd128Uint32(scratch2, mem, scratch1);
}

void MacroAssembler::StoreLane16LE(Simd128Register src, const MemOperand& mem,
                                   int lane, Register scratch1,
                                   Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 2;
  vextractuh(scratch2, src, Operand((7 - lane) * lane_width_in_bytes));
  MAYBE_REVERSE_BYTES(scratch2, xxbrh)
  StoreSimd128Uint16(scratch2, mem, scratch1);
}

void MacroAssembler::StoreLane8LE(Simd128Register src, const MemOperand& mem,
                                  int lane, Register scratch1,
                                  Simd128Register scratch2) {
  vextractub(scratch2, src, Operand(15 - lane));
  StoreSimd128Uint8(scratch2, mem, scratch1);
}

void MacroAssembler::LoadAndSplat64x2LE(Simd128Register dst,
                                        const MemOperand& mem,
                                        Register scratch) {
  constexpr int lane_width_in_bytes = 8;
  LoadSimd128Uint64(dst, mem, scratch);
  MAYBE_REVERSE_BYTES(dst, xxbrd)
  vinsertd(dst, dst, Operand(1 * lane_width_in_bytes));
}

void MacroAssembler::LoadAndSplat32x4LE(Simd128Register dst,
                                        const MemOperand& mem,
                                        Register scratch) {
  LoadSimd128Uint32(dst, mem, scratch);
  MAYBE_REVERSE_BYTES(dst, xxbrw)
  vspltw(dst, dst, Operand(1));
}

void MacroAssembler::LoadAndSplat16x8LE(Simd128Register dst,
                                        const MemOperand& mem,
                                        Register scratch) {
  LoadSimd128Uint16(dst, mem, scratch);
  MAYBE_REVERSE_BYTES(dst, xxbrh)
  vsplth(dst, dst, Operand(3));
}

void MacroAssembler::LoadAndSplat8x16LE(Simd128Register dst,
                                        const MemOperand& mem,
                                        Register scratch) {
  LoadSimd128Uint8(dst, mem, scratch);
  vspltb(dst, dst, Operand(7));
}

void MacroAssembler::LoadAndExtend32x2SLE(Simd128Register dst,
                                          const MemOperand& mem,
                                          Register scratch) {
  LoadSimd128Uint64(dst, mem, scratch);
  MAYBE_REVERSE_BYTES(dst, xxbrd)
  vupkhsw(dst, dst);
}

void MacroAssembler::LoadAndExtend32x2ULE(Simd128Register dst,
                                          const MemOperand& mem,
                                          Register scratch1,
                                          Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  LoadAndExtend32x2SLE(dst, mem, scratch1);
  // Zero extend.
  mov(scratch1, Operand(0xFFFFFFFF));
  mtvsrd(scratch2, scratch1);
  vinsertd(scratch2, scratch2, Operand(1 * lane_width_in_bytes));
  vand(dst, scratch2, dst);
}

void MacroAssembler::LoadAndExtend16x4SLE(Simd128Register dst,
                                          const MemOperand& mem,
                                          Register scratch) {
  LoadSimd128Uint64(dst, mem, scratch);
  MAYBE_REVERSE_BYTES(dst, xxbrd)
  vupkhsh(dst, dst);
}

void MacroAssembler::LoadAndExtend16x4ULE(Simd128Register dst,
                                          const MemOperand& mem,
                                          Register scratch1,
                                          Simd128Register scratch2) {
  LoadAndExtend16x4SLE(dst, mem, scratch1);
  // Zero extend.
  mov(scratch1, Operand(0xFFFF));
  mtvsrd(scratch2, scratch1);
  vspltw(scratch2, scratch2, Operand(1));
  vand(dst, scratch2, dst);
}

void MacroAssembler::LoadAndExtend8x8SLE(Simd128Register dst,
                                         const MemOperand& mem,
                                         Register scratch) {
  LoadSimd128Uint64(dst, mem, scratch);
  MAYBE_REVERSE_BYTES(dst, xxbrd)
  vupkhsb(dst, dst);
}

void MacroAssembler::LoadAndExtend8x8ULE(Simd128Register dst,
                                         const MemOperand& mem,
                                         Register scratch1,
                                         Simd128Register scratch2) {
  LoadAndExtend8x8SLE(dst, mem, scratch1);
  // Zero extend.
  li(scratch1, Operand(0xFF));
  mtvsrd(scratch2, scratch1);
  vsplth(scratch2, scratch2, Operand(3));
  vand(dst, scratch2, dst);
}

void MacroAssembler::LoadV64ZeroLE(Simd128Register dst, const MemOperand& mem,
                                   Register scratch1,
                                   Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 8;
  LoadSimd128Uint64(scratch2, mem, scratch1);
  MAYBE_REVERSE_BYTES(scratch2, xxbrd)
  vxor(dst, dst, dst);
  vinsertd(dst, scratch2, Operand(1 * lane_width_in_bytes));
}

void MacroAssembler::LoadV32ZeroLE(Simd128Register dst, const MemOperand& mem,
                                   Register scratch1,
                                   Simd128Register scratch2) {
  constexpr int lane_width_in_bytes = 4;
  LoadSimd128Uint32(scratch2, mem, scratch1);
  MAYBE_REVERSE_BYTES(scratch2, xxbrw)
  vxor(dst, dst, dst);
  vinsertw(dst, scratch2, Operand(3 * lane_width_in_bytes));
}
#undef MAYBE_REVERSE_BYTES

void MacroAssembler::V128AnyTrue(Register dst, Simd128Register src,
                                 Register scratch1, Register scratch2,
                                 Simd128Register scratch3) {
  constexpr uint8_t fxm = 0x2;  // field mask.
  constexpr int bit_number = 24;
  li(scratch1, Operand(0));
  li(scratch2, Operand(1));
  // Check if both lanes are 0, if so then return false.
  vxor(scratch3, scratch3, scratch3);
  mtcrf(scratch1, fxm);  // Clear cr6.
  vcmpequd(scratch3, src, scratch3, SetRC);
  isel(dst, scratch1, scratch2, bit_number);
}

void MacroAssembler::S128Not(Simd128Register dst, Simd128Register src) {
  vnor(dst, src, src);
}

void MacroAssembler::S128Const(Simd128Register dst, uint64_t high, uint64_t low,
                               Register scratch1, Register scratch2) {
  mov(scratch1, Operand(low));
  mov(scratch2, Operand(high));
  mtvsrdd(dst, scratch2, scratch1);
}

void MacroAssembler::S128Select(Simd128Register dst, Simd128Register src1,
                                Simd128Register src2, Simd128Register mask) {
  vsel(dst, src2, src1, mask);
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

void MacroAssembler::SwapP(Register src, Register dst, Register scratch) {
  if (src == dst) return;
  DCHECK(!AreAliased(src, dst, scratch));
  mr(scratch, src);
  mr(src, dst);
  mr(dst, scratch);
}

void MacroAssembler::SwapP(Register src, MemOperand dst, Register scratch) {
  if (dst.ra() != r0 && dst.ra().is_valid())
    DCHECK(!AreAliased(src, dst.ra(), scratch));
  if (dst.rb() != r0 && dst.rb().is_valid())
    DCHECK(!AreAliased(src, dst.rb(), scratch));
  DCHECK(!AreAliased(src, scratch));
  mr(scratch, src);
  LoadU64(src, dst, r0);
  StoreU64(scratch, dst, r0);
}

void MacroAssembler::SwapP(MemOperand src, MemOperand dst, Register scratch_0,
                           Register scratch_1) {
  if (src.ra() != r0 && src.ra().is_valid())
    DCHECK(!AreAliased(src.ra(), scratch_0, scratch_1));
  if (src.rb() != r0 && src.rb().is_valid())
    DCHECK(!AreAliased(src.rb(), scratch_0, scratch_1));
  if (dst.ra() != r0 && dst.ra().is_valid())
    DCHECK(!AreAliased(dst.ra(), scratch_0, scratch_1));
  if (dst.rb() != r0 && dst.rb().is_valid())
    DCHECK(!AreAliased(dst.rb(), scratch_0, scratch_1));
  DCHECK(!AreAliased(scratch_0, scratch_1));
  if (is_int16(src.offset()) || is_int16(dst.offset())) {
    if (!is_int16(src.offset())) {
      // swap operand
      MemOperand temp = src;
      src = dst;
      dst = temp;
    }
    LoadU64(scratch_1, dst, scratch_0);
    LoadU64(scratch_0, src);
    StoreU64(scratch_1, src);
    StoreU64(scratch_0, dst, scratch_1);
  } else {
    LoadU64(scratch_1, dst, scratch_0);
    push(scratch_1);
    LoadU64(scratch_0, src, scratch_1);
    StoreU64(scratch_0, dst, scratch_1);
    pop(scratch_1);
    StoreU64(scratch_1, src, scratch_0);
  }
}

void MacroAssembler::SwapFloat32(DoubleRegister src, DoubleRegister dst,
                                 DoubleRegister scratch) {
  if (src == dst) return;
  DCHECK(!AreAliased(src, dst, scratch));
  fmr(scratch, src);
  fmr(src, dst);
  fmr(dst, scratch);
}

void MacroAssembler::SwapFloat32(DoubleRegister src, MemOperand dst,
                                 DoubleRegister scratch) {
  DCHECK(!AreAliased(src, scratch));
  fmr(scratch, src);
  LoadF32(src, dst, r0);
  StoreF32(scratch, dst, r0);
}

void MacroAssembler::SwapFloat32(MemOperand src, MemOperand dst,
                                 DoubleRegister scratch_0,
                                 DoubleRegister scratch_1) {
  DCHECK(!AreAliased(scratch_0, scratch_1));
  LoadF32(scratch_0, src, r0);
  LoadF32(scratch_1, dst, r0);
  StoreF32(scratch_0, dst, r0);
  StoreF32(scratch_1, src, r0);
}

void MacroAssembler::SwapDouble(DoubleRegister src, DoubleRegister dst,
                                DoubleRegister scratch) {
  if (src == dst) return;
  DCHECK(!AreAliased(src, dst, scratch));
  fmr(scratch, src);
  fmr(src, dst);
  fmr(dst, scratch);
}

void MacroAssembler::SwapDouble(DoubleRegister src, MemOperand dst,
                                DoubleRegister scratch) {
  DCHECK(!AreAliased(src, scratch));
  fmr(scratch, src);
  LoadF64(src, dst, r0);
  StoreF64(scratch, dst, r0);
}

void MacroAssembler::SwapDouble(MemOperand src, MemOperand dst,
                                DoubleRegister scratch_0,
                                DoubleRegister scratch_1) {
  DCHECK(!AreAliased(scratch_0, scratch_1));
  LoadF64(scratch_0, src, r0);
  LoadF64(scratch_1, dst, r0);
  StoreF64(scratch_0, dst, r0);
  StoreF64(scratch_1, src, r0);
}

void MacroAssembler::SwapSimd128(Simd128Register src, Simd128Register dst,
                                 Simd128Register scratch) {
  if (src == dst) return;
  vor(scratch, src, src);
  vor(src, dst, dst);
  vor(dst, scratch, scratch);
}

void MacroAssembler::SwapSimd128(Simd128Register src, MemOperand dst,
                                 Simd128Register scratch1, Register scratch2) {
  DCHECK(src != scratch1);
  LoadSimd128(scratch1, dst, scratch2);
  StoreSimd128(src, dst, scratch2);
  vor(src, scratch1, scratch1);
}

void MacroAssembler::SwapSimd128(MemOperand src, MemOperand dst,
                                 Simd128Register scratch1,
                                 Simd128Register scratch2, Register scratch3) {
  LoadSimd128(scratch1, src, scratch3);
  LoadSimd128(scratch2, dst, scratch3);

  StoreSimd128(scratch1, dst, scratch3);
  StoreSimd128(scratch2, src, scratch3);
}

void MacroAssembler::ByteReverseU16(Register dst, Register val,
                                    Register scratch) {
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    brh(dst, val);
    ZeroExtHalfWord(dst, dst);
    return;
  }
  rlwinm(scratch, val, 8, 16, 23);
  rlwinm(dst, val, 24, 24, 31);
  orx(dst, scratch, dst);
  ZeroExtHalfWord(dst, dst);
}

void MacroAssembler::ByteReverseU32(Register dst, Register val,
                                    Register scratch) {
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    brw(dst, val);
    ZeroExtWord32(dst, dst);
    return;
  }
  rotlwi(scratch, val, 8);
  rlwimi(scratch, val, 24, 0, 7);
  rlwimi(scratch, val, 24, 16, 23);
  ZeroExtWord32(dst, scratch);
}

void MacroAssembler::ByteReverseU64(Register dst, Register val, Register) {
  if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
    brd(dst, val);
    return;
  }
  subi(sp, sp, Operand(kSystemPointerSize));
  std(val, MemOperand(sp));
  ldbrx(dst, MemOperand(r0, sp));
  addi(sp, sp, Operand(kSystemPointerSize));
}

void MacroAssembler::JumpIfEqual(Register x, int32_t y, Label* dest) {
  CmpS32(x, Operand(y), r0);
  beq(dest);
}

void MacroAssembler::JumpIfLessThan(Register x, int32_t y, Label* dest) {
  CmpS32(x, Operand(y), r0);
  blt(dest);
}

void MacroAssembler::LoadEntryFromBuiltinIndex(Register builtin_index,
                                               Register target) {
  static_assert(kSystemPointerSize == 8);
  static_assert(kSmiTagSize == 1);
  static_assert(kSmiTag == 0);

  // The builtin_index register contains the builtin index as a Smi.
  if (SmiValuesAre32Bits()) {
    ShiftRightS64(target, builtin_index,
                  Operand(kSmiShift - kSystemPointerSizeLog2));
  } else {
    DCHECK(SmiValuesAre31Bits());
    ShiftLeftU64(target, builtin_index,
                 Operand(kSystemPointerSizeLog2 - kSmiShift));
  }
  AddS64(target, target, Operand(IsolateData::builtin_entry_table_offset()));
  LoadU64(target, MemOperand(kRootRegister, target));
}

void MacroAssembler::CallBuiltinByIndex(Register builtin_index,
                                        Register target) {
  LoadEntryFromBuiltinIndex(builtin_index, target);
  Call(target);
}

void MacroAssembler::LoadEntryFromBuiltin(Builtin builtin,
                                          Register destination) {
  ASM_CODE_COMMENT(this);
  LoadU64(destination, EntryFromBuiltinAsOperand(builtin));
}

MemOperand MacroAssembler::EntryFromBuiltinAsOperand(Builtin builtin) {
  ASM_CODE_COMMENT(this);
  DCHECK(root_array_available());
  return MemOperand(kRootRegister,
                    IsolateData::BuiltinEntrySlotOffset(builtin));
}

void MacroAssembler::LoadCodeInstructionStart(Register destination,
                                              Register code_object,
                                              CodeEntrypointTag tag) {
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  LoadCodeEntrypointViaCodePointer(
      destination,
      FieldMemOperand(code_object, Code::kSelfIndirectPointerOffset), r0);
#else
  LoadU64(destination,
          FieldMemOperand(code_object, Code::kInstructionStartOffset), r0);
#endif
}

void MacroAssembler::CallCodeObject(Register code_object) {
  ASM_CODE_COMMENT(this);
  LoadCodeInstructionStart(code_object, code_object);
  Call(code_object);
}

void MacroAssembler::JumpCodeObject(Register code_object, JumpMode jump_mode) {
  ASM_CODE_COMMENT(this);
  DCHECK_EQ(JumpMode::kJump, jump_mode);
  LoadCodeInstructionStart(code_object, code_object);
  Jump(code_object);
}

void MacroAssembler::CallJSFunction(Register function_object,
                                    Register scratch) {
  Register code = kJavaScriptCallCodeStartRegister;
#ifdef V8_ENABLE_SANDBOX
  // When the sandbox is enabled, we can directly fetch the entrypoint pointer
  // from the code pointer table instead of going through the Code object. In
  // this way, we avoid one memory load on this code path.
  LoadCodeEntrypointViaCodePointer(
      code, FieldMemOperand(function_object, JSFunction::kCodeOffset), scratch);
  Call(code);
#else
  LoadTaggedField(
      code, FieldMemOperand(function_object, JSFunction::kCodeOffset), scratch);
  CallCodeObject(code);
#endif
}

void MacroAssembler::JumpJSFunction(Register function_object, Register scratch,
                                    JumpMode jump_mode) {
  Register code = kJavaScriptCallCodeStartRegister;
#ifdef V8_ENABLE_SANDBOX
  // When the sandbox is enabled, we can directly fetch the entrypoint pointer
  // from the code pointer table instead of going through the Code object. In
  // this way, we avoid one memory load on this code path.
  LoadCodeEntrypointViaCodePointer(
      code, FieldMemOperand(function_object, JSFunction::kCodeOffset), scratch);
  DCHECK_EQ(jump_mode, JumpMode::kJump);
  DCHECK_EQ(code, r5);
  Jump(code);
#else
  LoadTaggedField(
      code, FieldMemOperand(function_object, JSFunction::kCodeOffset), scratch);
  JumpCodeObject(code, jump_mode);
#endif
}

void MacroAssembler::StoreReturnAddressAndCall(Register target) {
  // This generates the final instruction sequence for calls to C functions
  // once an exit frame has been constructed.
  //
  // Note that this assumes the caller code (i.e. the InstructionStream object
  // currently being generated) is immovable or that the callee function cannot
  // trigger GC, since the callee function will return to it.

  static constexpr int after_call_offset = 5 * kInstrSize;
  Label start_call;
  Register dest = target;

  if (ABI_USES_FUNCTION_DESCRIPTORS) {
    // AIX/PPC64BE Linux uses a function descriptor. When calling C code be
    // aware of this descriptor and pick up values from it
    LoadU64(ToRegister(ABI_TOC_REGISTER),
            MemOperand(target, kSystemPointerSize));
    LoadU64(ip, MemOperand(target, 0));
    dest = ip;
  } else if (ABI_CALL_VIA_IP && dest != ip) {
    Move(ip, target);
    dest = ip;
  }

  LoadPC(r7);
  bind(&start_call);
  addi(r7, r7, Operand(after_call_offset));
  StoreU64(r7, MemOperand(sp, kStackFrameExtraParamSlot * kSystemPointerSize));
  Call(dest);

  DCHECK_EQ(after_call_offset - kInstrSize,
            SizeOfCodeGeneratedSince(&start_call));
}

// Check if the code object is marked for deoptimization. If it is, then it
// jumps to the CompileLazyDeoptimizedCode builtin. In order to do this we need
// to:
//    1. read from memory the word that contains that bit, which can be found in
//       the flags in the referenced {Code} object;
//    2. test kMarkedForDeoptimizationBit in those flags; and
//    3. if it is not zero then it jumps to the builtin.
void MacroAssembler::BailoutIfDeoptimized() {
  int offset = InstructionStream::kCodeOffset - InstructionStream::kHeaderSize;
  LoadTaggedField(r11, MemOperand(kJavaScriptCallCodeStartRegister, offset),
                     r0);
  LoadU32(r11, FieldMemOperand(r11, Code::kFlagsOffset), r0);
  TestBit(r11, Code::kMarkedForDeoptimizationBit);
  TailCallBuiltin(Builtin::kCompileLazyDeoptimizedCode, ne, cr0);
}

void MacroAssembler::CallForDeoptimization(Builtin target, int, Label* exit,
                                           DeoptimizeKind kind, Label* ret,
                                           Label*) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  CHECK_LE(target, Builtins::kLastTier0);
  LoadU64(ip, MemOperand(kRootRegister,
                         IsolateData::BuiltinEntrySlotOffset(target)));
  Call(ip);
  DCHECK_EQ(SizeOfCodeGeneratedSince(exit),
            (kind == DeoptimizeKind::kLazy) ? Deoptimizer::kLazyDeoptExitSize
                                            : Deoptimizer::kEagerDeoptExitSize);
}

void MacroAssembler::ZeroExtByte(Register dst, Register src) {
  clrldi(dst, src, Operand(56));
}

void MacroAssembler::ZeroExtHalfWord(Register dst, Register src) {
  clrldi(dst, src, Operand(48));
}

void MacroAssembler::ZeroExtWord32(Register dst, Register src) {
  clrldi(dst, src, Operand(32));
}

void MacroAssembler::Trap() { stop(); }
void MacroAssembler::DebugBreak() { stop(); }

void MacroAssembler::Popcnt32(Register dst, Register src) { popcntw(dst, src); }

void MacroAssembler::Popcnt64(Register dst, Register src) { popcntd(dst, src); }

void MacroAssembler::CountLeadingZerosU32(Register dst, Register src, RCBit r) {
  cntlzw(dst, src, r);
}

void MacroAssembler::CountLeadingZerosU64(Register dst, Register src, RCBit r) {
  cntlzd(dst, src, r);
}

#define COUNT_TRAILING_ZEROES_SLOW(max_count, scratch1, scratch2) \
  Label loop, done;                                               \
  li(scratch1, Operand(max_count));                               \
  mtctr(scratch1);                                                \
  mr(scratch1, src);                                              \
  li(dst, Operand::Zero());                                       \
  bind(&loop); /* while ((src & 1) == 0) */                       \
  andi(scratch2, scratch1, Operand(1));                           \
  bne(&done, cr0);                                                \
  srdi(scratch1, scratch1, Operand(1)); /* src >>= 1;*/           \
  addi(dst, dst, Operand(1));           /* dst++ */               \
  bdnz(&loop);                                                    \
  bind(&done);
void MacroAssembler::CountTrailingZerosU32(Register dst, Register src,
                                           Register scratch1, Register scratch2,
                                           RCBit r) {
  if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
    cnttzw(dst, src, r);
  } else {
    COUNT_TRAILING_ZEROES_SLOW(32, scratch1, scratch2);
  }
}

void MacroAssembler::CountTrailingZerosU64(Register dst, Register src,
                                           Register scratch1, Register scratch2,
                                           RCBit r) {
  if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
    cnttzd(dst, src, r);
  } else {
    COUNT_TRAILING_ZEROES_SLOW(64, scratch1, scratch2);
  }
}
#undef COUNT_TRAILING_ZEROES_SLOW

void MacroAssembler::ClearByteU64(Register dst, int byte_idx) {
  CHECK(0 <= byte_idx && byte_idx <= 7);
  int shift = byte_idx*8;
  rldicl(dst, dst, shift, 8);
  rldicl(dst, dst, 64-shift, 0);
}

void MacroAssembler::ReverseBitsU64(Register dst, Register src,
                                    Register scratch1, Register scratch2) {
  ByteReverseU64(dst, src);
  for (int i = 0; i < 8; i++) {
    ReverseBitsInSingleByteU64(dst, dst, scratch1, scratch2, i);
  }
}

void MacroAssembler::ReverseBitsU32(Register dst, Register src,
                                    Register scratch1, Register scratch2) {
  ByteReverseU32(dst, src, scratch1);
  for (int i = 4; i < 8; i++) {
    ReverseBitsInSingleByteU64(dst, dst, scratch1, scratch2, i);
  }
}

// byte_idx=7 refers to least significant byte
void MacroAssembler::ReverseBitsInSingleByteU64(Register dst, Register src,
                                                Register scratch1,
                                                Register scratch2,
                                                int byte_idx) {
  CHECK(0 <= byte_idx && byte_idx <= 7);
  int j = byte_idx;
  // zero all bits of scratch1
  li(scratch2, Operand(0));
  for (int i = 0; i <= 7; i++) {
    // zero all bits of scratch1
    li(scratch1, Operand(0));
    // move bit (j+1)*8-i-1 of src to bit j*8+i of scratch1, erase bits
    // (j*8+i+1):end of scratch1
    int shift = 7 - (2*i);
    if (shift < 0) shift += 64;
    rldicr(scratch1, src, shift, j*8+i);
    // erase bits start:(j*8-1+i) of scratch1 (inclusive)
    rldicl(scratch1, scratch1, 0, j*8+i);
    // scratch2 = scratch2|scratch1
    orx(scratch2, scratch2, scratch1);
  }
  // clear jth byte of dst and insert jth byte of scratch2
  ClearByteU64(dst, j);
  orx(dst, dst, scratch2);
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

  // Additional parameter is the address of the actual callback.
  Register return_value = r3;
  Register scratch = ip;
  Register scratch2 = r0;

  // Allocate HandleScope in callee-saved registers.
  // We will need to restore the HandleScope after the call to the API function,
  // by allocating it in callee-saved registers it'll be preserved by C code.
  Register prev_next_address_reg = r14;
  Register prev_limit_reg = r15;
  Register prev_level_reg = r16;

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
    __ LoadU64(prev_next_address_reg, next_mem_op);
    __ LoadU64(prev_limit_reg, limit_mem_op);
    __ lwz(prev_level_reg, level_mem_op);
    __ addi(scratch, prev_level_reg, Operand(1));
    __ stw(scratch, level_mem_op);
  }

  Label profiler_or_side_effects_check_enabled, done_api_call;
  if (with_profiling) {
    __ RecordComment("Check if profiler or side effects check is enabled");
    __ lbz(scratch,
           __ ExternalReferenceAsOperand(IsolateFieldId::kExecutionMode));
    __ cmpi(scratch, Operand::Zero());
    __ bne(&profiler_or_side_effects_check_enabled);
#ifdef V8_RUNTIME_CALL_STATS
    __ RecordComment("Check if RCS is enabled");
    __ Move(scratch, ER::address_of_runtime_stats_flag());
    __ lwz(scratch, MemOperand(scratch, 0));
    __ cmpi(scratch, Operand::Zero());
    __ bne(&profiler_or_side_effects_check_enabled);
#endif  // V8_RUNTIME_CALL_STATS
  }

  __ RecordComment("Call the api function directly.");
  __ StoreReturnAddressAndCall(function_address);
  __ bind(&done_api_call);

  Label propagate_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;

  // load value from ReturnValue
  __ RecordComment("Load the value from ReturnValue");
  __ LoadU64(r3, return_value_operand);

  {
    ASM_CODE_COMMENT_STRING(
        masm,
        "No more valid handles (the result handle was the last one)."
        "Restore previous handle scope.");
    __ StoreU64(prev_next_address_reg, next_mem_op);
    if (v8_flags.debug_code) {
      __ lwz(scratch, level_mem_op);
      __ subi(scratch, scratch, Operand(1));
      __ CmpS64(scratch, prev_level_reg);
      __ Check(eq, AbortReason::kUnexpectedLevelAfterReturnFromApiCall);
    }
    __ stw(prev_level_reg, level_mem_op);
    __ LoadU64(scratch, limit_mem_op);
    __ CmpS64(scratch, prev_limit_reg);
    __ bne(&delete_allocated_handles);
  }

  __ RecordComment("Leave the API exit frame.");
  __ bind(&leave_exit_frame);
  Register argc_reg = prev_limit_reg;
  if (argc_operand != nullptr) {
    // Load the number of stack slots to drop before LeaveExitFrame modifies sp.
    __ LoadU64(argc_reg, *argc_operand);
  }
  __ LeaveExitFrame(scratch);

  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Check if the function scheduled an exception.");
    __ LoadRoot(scratch, RootIndex::kTheHoleValue);
    __ LoadU64(scratch2, __ ExternalReferenceAsOperand(
                             ER::exception_address(isolate), no_reg));
    __ CmpS64(scratch, scratch2);
    __ bne(&propagate_exception);
  }

  __ AssertJSAny(return_value, scratch, scratch2,
                 AbortReason::kAPICallReturnedInvalidObject);

  if (argc_operand == nullptr) {
    DCHECK_NE(slots_to_drop_on_return, 0);
    __ AddS64(sp, sp, Operand(slots_to_drop_on_return * kSystemPointerSize));

  } else {
    // {argc_operand} was loaded into {argc_reg} above.
    __ AddS64(sp, sp, Operand(slots_to_drop_on_return * kSystemPointerSize));
    __ ShiftLeftU64(r0, argc_reg, Operand(kSystemPointerSizeLog2));
    __ AddS64(sp, sp, r0);
  }

  __ blr();

  if (with_profiling) {
    ASM_CODE_COMMENT_STRING(masm, "Call the api function via the thunk.");
    __ bind(&profiler_or_side_effects_check_enabled);
    // Additional parameter is the address of the actual callback function.
    if (thunk_arg.is_valid()) {
      MemOperand thunk_arg_mem_op = __ ExternalReferenceAsOperand(
          IsolateFieldId::kApiCallbackThunkArgument);
      __ StoreU64(thunk_arg, thunk_arg_mem_op);
    }
    __ Move(scratch, thunk_ref);
    __ StoreReturnAddressAndCall(scratch);
    __ b(&done_api_call);
  }

  __ RecordComment("An exception was thrown. Propagate it.");
  __ bind(&propagate_exception);
  __ TailCallRuntime(Runtime::kPropagateException);

  {
    ASM_CODE_COMMENT_STRING(
        masm, "HandleScope limit has changed. Delete allocated extensions.");
    __ bind(&delete_allocated_handles);
    __ StoreU64(prev_limit_reg, limit_mem_op);
    // Save the return value in a callee-save register.
    Register saved_result = prev_limit_reg;
    __ mr(saved_result, return_value);
    __ PrepareCallCFunction(1, scratch);
    __ Move(kCArgRegs[0], ER::isolate_address());
    __ CallCFunction(ER::delete_handle_scope_extensions(), 1);
    __ mr(return_value, saved_result);
    __ b(&leave_exit_frame);
  }
}

}  // namespace internal
}  // namespace v8

#undef __

#endif  // V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
