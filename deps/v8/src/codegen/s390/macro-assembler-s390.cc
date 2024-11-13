// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>  // For assert
#include <limits.h>  // For LONG_MIN, LONG_MAX.

#if V8_TARGET_ARCH_S390

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
#include "src/objects/smi.h"
#include "src/runtime/runtime.h"
#include "src/snapshot/snapshot.h"

// Satisfy cpplint check, but don't include platform-specific header. It is
// included recursively via macro-assembler.h.
#if 0
#include "src/codegen/s390/macro-assembler-s390.h"
#endif

#define __ ACCESS_MASM(masm)

namespace v8 {
namespace internal {

namespace {

// For WebAssembly we care about the full floating point (Simd) registers. If we
// are not running Wasm, we can get away with saving half of those (F64)
// registers.
#if V8_ENABLE_WEBASSEMBLY
constexpr int kStackSavedSavedFPSizeInBytes =
    kNumCallerSavedDoubles * kSimd128Size;
#else
constexpr int kStackSavedSavedFPSizeInBytes =
    kNumCallerSavedDoubles * kDoubleSize;
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace

void MacroAssembler::DoubleMax(DoubleRegister result_reg,
                               DoubleRegister left_reg,
                               DoubleRegister right_reg) {
  if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_1)) {
    vfmax(result_reg, left_reg, right_reg, Condition(1), Condition(8),
          Condition(3));
    return;
  }

  Label check_zero, return_left, return_right, return_nan, done;
  cdbr(left_reg, right_reg);
  bunordered(&return_nan, Label::kNear);
  beq(&check_zero);
  bge(&return_left, Label::kNear);
  b(&return_right, Label::kNear);

  bind(&check_zero);
  lzdr(kDoubleRegZero);
  cdbr(left_reg, kDoubleRegZero);
  /* left == right != 0. */
  bne(&return_left, Label::kNear);
  /* At this point, both left and right are either 0 or -0. */
  /* N.B. The following works because +0 + -0 == +0 */
  /* For max we want logical-and of sign bit: (L + R) */
  ldr(result_reg, left_reg);
  adbr(result_reg, right_reg);
  b(&done, Label::kNear);

  bind(&return_nan);
  /* If left or right are NaN, adbr propagates the appropriate one.*/
  adbr(left_reg, right_reg);
  b(&return_left, Label::kNear);

  bind(&return_right);
  if (right_reg != result_reg) {
    ldr(result_reg, right_reg);
  }
  b(&done, Label::kNear);

  bind(&return_left);
  if (left_reg != result_reg) {
    ldr(result_reg, left_reg);
  }
  bind(&done);
}

void MacroAssembler::DoubleMin(DoubleRegister result_reg,
                               DoubleRegister left_reg,
                               DoubleRegister right_reg) {
  if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_1)) {
    vfmin(result_reg, left_reg, right_reg, Condition(1), Condition(8),
          Condition(3));
    return;
  }
  Label check_zero, return_left, return_right, return_nan, done;
  cdbr(left_reg, right_reg);
  bunordered(&return_nan, Label::kNear);
  beq(&check_zero);
  ble(&return_left, Label::kNear);
  b(&return_right, Label::kNear);

  bind(&check_zero);
  lzdr(kDoubleRegZero);
  cdbr(left_reg, kDoubleRegZero);
  /* left == right != 0. */
  bne(&return_left, Label::kNear);
  /* At this point, both left and right are either 0 or -0. */
  /* N.B. The following works because +0 + -0 == +0 */
  /* For min we want logical-or of sign bit: -(-L + -R) */
  lcdbr(left_reg, left_reg);
  ldr(result_reg, left_reg);
  if (left_reg == right_reg) {
    adbr(result_reg, right_reg);
  } else {
    sdbr(result_reg, right_reg);
  }
  lcdbr(result_reg, result_reg);
  b(&done, Label::kNear);

  bind(&return_nan);
  /* If left or right are NaN, adbr propagates the appropriate one.*/
  adbr(left_reg, right_reg);
  b(&return_left, Label::kNear);

  bind(&return_right);
  if (right_reg != result_reg) {
    ldr(result_reg, right_reg);
  }
  b(&done, Label::kNear);

  bind(&return_left);
  if (left_reg != result_reg) {
    ldr(result_reg, left_reg);
  }
  bind(&done);
}

void MacroAssembler::FloatMax(DoubleRegister result_reg,
                              DoubleRegister left_reg,
                              DoubleRegister right_reg) {
  if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_1)) {
    vfmax(result_reg, left_reg, right_reg, Condition(1), Condition(8),
          Condition(2));
    return;
  }
  Label check_zero, return_left, return_right, return_nan, done;
  cebr(left_reg, right_reg);
  bunordered(&return_nan, Label::kNear);
  beq(&check_zero);
  bge(&return_left, Label::kNear);
  b(&return_right, Label::kNear);

  bind(&check_zero);
  lzdr(kDoubleRegZero);
  cebr(left_reg, kDoubleRegZero);
  /* left == right != 0. */
  bne(&return_left, Label::kNear);
  /* At this point, both left and right are either 0 or -0. */
  /* N.B. The following works because +0 + -0 == +0 */
  /* For max we want logical-and of sign bit: (L + R) */
  ldr(result_reg, left_reg);
  aebr(result_reg, right_reg);
  b(&done, Label::kNear);

  bind(&return_nan);
  /* If left or right are NaN, aebr propagates the appropriate one.*/
  aebr(left_reg, right_reg);
  b(&return_left, Label::kNear);

  bind(&return_right);
  if (right_reg != result_reg) {
    ldr(result_reg, right_reg);
  }
  b(&done, Label::kNear);

  bind(&return_left);
  if (left_reg != result_reg) {
    ldr(result_reg, left_reg);
  }
  bind(&done);
}

void MacroAssembler::FloatMin(DoubleRegister result_reg,
                              DoubleRegister left_reg,
                              DoubleRegister right_reg) {
  if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_1)) {
    vfmin(result_reg, left_reg, right_reg, Condition(1), Condition(8),
          Condition(2));
    return;
  }

  Label check_zero, return_left, return_right, return_nan, done;
  cebr(left_reg, right_reg);
  bunordered(&return_nan, Label::kNear);
  beq(&check_zero);
  ble(&return_left, Label::kNear);
  b(&return_right, Label::kNear);

  bind(&check_zero);
  lzdr(kDoubleRegZero);
  cebr(left_reg, kDoubleRegZero);
  /* left == right != 0. */
  bne(&return_left, Label::kNear);
  /* At this point, both left and right are either 0 or -0. */
  /* N.B. The following works because +0 + -0 == +0 */
  /* For min we want logical-or of sign bit: -(-L + -R) */
  lcebr(left_reg, left_reg);
  ldr(result_reg, left_reg);
  if (left_reg == right_reg) {
    aebr(result_reg, right_reg);
  } else {
    sebr(result_reg, right_reg);
  }
  lcebr(result_reg, result_reg);
  b(&done, Label::kNear);

  bind(&return_nan);
  /* If left or right are NaN, aebr propagates the appropriate one.*/
  aebr(left_reg, right_reg);
  b(&return_left, Label::kNear);

  bind(&return_right);
  if (right_reg != result_reg) {
    ldr(result_reg, right_reg);
  }
  b(&done, Label::kNear);

  bind(&return_left);
  if (left_reg != result_reg) {
    ldr(result_reg, left_reg);
  }
  bind(&done);
}

void MacroAssembler::CeilF32(DoubleRegister dst, DoubleRegister src) {
  fiebra(ROUND_TOWARD_POS_INF, dst, src);
}

void MacroAssembler::CeilF64(DoubleRegister dst, DoubleRegister src) {
  fidbra(ROUND_TOWARD_POS_INF, dst, src);
}

void MacroAssembler::FloorF32(DoubleRegister dst, DoubleRegister src) {
  fiebra(ROUND_TOWARD_NEG_INF, dst, src);
}

void MacroAssembler::FloorF64(DoubleRegister dst, DoubleRegister src) {
  fidbra(ROUND_TOWARD_NEG_INF, dst, src);
}

void MacroAssembler::TruncF32(DoubleRegister dst, DoubleRegister src) {
  fiebra(ROUND_TOWARD_0, dst, src);
}

void MacroAssembler::TruncF64(DoubleRegister dst, DoubleRegister src) {
  fidbra(ROUND_TOWARD_0, dst, src);
}

void MacroAssembler::NearestIntF32(DoubleRegister dst, DoubleRegister src) {
  fiebra(ROUND_TO_NEAREST_TO_EVEN, dst, src);
}

void MacroAssembler::NearestIntF64(DoubleRegister dst, DoubleRegister src) {
  fidbra(ROUND_TO_NEAREST_TO_EVEN, dst, src);
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
    bytes += kStackSavedSavedFPSizeInBytes;
  }

  return bytes;
}

int MacroAssembler::PushCallerSaved(SaveFPRegsMode fp_mode, Register scratch,
                                    Register exclusion1, Register exclusion2,
                                    Register exclusion3) {
  int bytes = 0;

  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  MultiPush(list);
  bytes += list.Count() * kSystemPointerSize;

  if (fp_mode == SaveFPRegsMode::kSave) {
    MultiPushF64OrV128(kCallerSavedDoubles, scratch);
    bytes += kStackSavedSavedFPSizeInBytes;
  }

  return bytes;
}

int MacroAssembler::PopCallerSaved(SaveFPRegsMode fp_mode, Register scratch,
                                   Register exclusion1, Register exclusion2,
                                   Register exclusion3) {
  int bytes = 0;
  if (fp_mode == SaveFPRegsMode::kSave) {
    MultiPopF64OrV128(kCallerSavedDoubles, scratch);
    bytes += kStackSavedSavedFPSizeInBytes;
  }

  RegList exclusions = {exclusion1, exclusion2, exclusion3};
  RegList list = kJSCallerSaved - exclusions;
  MultiPop(list);
  bytes += list.Count() * kSystemPointerSize;

  return bytes;
}

void MacroAssembler::LoadFromConstantsTable(Register destination,
                                            int constant_index) {
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kBuiltinsConstantsTable));

  const uint32_t offset = FixedArray::kHeaderSize +
                          constant_index * kSystemPointerSize - kHeapObjectTag;

  CHECK(is_uint19(offset));
  DCHECK_NE(destination, r0);
  LoadRoot(destination, RootIndex::kBuiltinsConstantsTable);
  LoadTaggedField(destination,
                  FieldMemOperand(destination, FixedArray::OffsetOfElementAt(
                                                   constant_index)),
                  r1);
}

void MacroAssembler::LoadRootRelative(Register destination, int32_t offset) {
  LoadU64(destination, MemOperand(kRootRegister, offset));
}

void MacroAssembler::StoreRootRelative(int32_t offset, Register value) {
  StoreU64(value, MemOperand(kRootRegister, offset));
}

void MacroAssembler::LoadRootRegisterOffset(Register destination,
                                            intptr_t offset) {
  if (offset == 0) {
    mov(destination, kRootRegister);
  } else if (is_uint12(offset)) {
    la(destination, MemOperand(kRootRegister, offset));
  } else {
    DCHECK(is_int20(offset));
    lay(destination, MemOperand(kRootRegister, offset));
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

void MacroAssembler::Jump(Register target, Condition cond) { b(cond, target); }

void MacroAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond) {
  Label skip;

  if (cond != al) b(NegateCondition(cond), &skip);

  mov(ip, Operand(target, rmode));
  b(ip);

  bind(&skip);
}

void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode,
                          Condition cond) {
  DCHECK(!RelocInfo::IsCodeTarget(rmode));
  Jump(static_cast<intptr_t>(target), rmode, cond);
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
  jump(code, RelocInfo::RELATIVE_CODE_TARGET, cond);
}

void MacroAssembler::Jump(const ExternalReference& reference) {
#if V8_OS_ZOS
  // Place reference into scratch r12 ip register
  Move(ip, reference);
  // z/OS uses function descriptors, extract code entry into r6
  LoadMultipleP(r5, r6, MemOperand(ip));
  // Preserve return address into r14
  mov(r14, r7);
  // Call C Function
  StoreReturnAddressAndCall(r6);
  // Branch to return address in r14
  b(r14);
#else
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Move(scratch, reference);
  Jump(scratch);
#endif
}

void MacroAssembler::Call(Register target) {
  // Branch to target via indirect branch
  basr(r14, target);
}

void MacroAssembler::CallJSEntry(Register target) {
  DCHECK(target == r4);
  Call(target);
}

int MacroAssembler::CallSizeNotPredictableCodeSize(Address target,
                                                   RelocInfo::Mode rmode,
                                                   Condition cond) {
  // S390 Assembler::move sequence is IILF / IIHF
  int size;
#if V8_TARGET_ARCH_S390X
  size = 14;  // IILF + IIHF + BASR
#else
  size = 8;  // IILF + BASR
#endif
  return size;
}

void MacroAssembler::Call(Address target, RelocInfo::Mode rmode,
                          Condition cond) {
  DCHECK(cond == al);

  mov(ip, Operand(target, rmode));
  basr(r14, ip);
}

void MacroAssembler::Call(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond) {
  DCHECK(RelocInfo::IsCodeTarget(rmode) && cond == al);

  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code));

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    CallBuiltin(builtin);
    return;
  }
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  call(code, rmode);
}

void MacroAssembler::CallBuiltin(Builtin builtin, Condition cond) {
  ASM_CODE_COMMENT_STRING(this, CommentForOffHeapTrampoline("call", builtin));
  // Use ip directly instead of using UseScratchRegisterScope, as we do not
  // preserve scratch registers across calls.
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      mov(ip, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Call(ip);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      UNREACHABLE();
    case BuiltinCallJumpMode::kIndirect:
      LoadU64(ip, EntryFromBuiltinAsOperand(builtin));
      Call(ip);
      break;
    case BuiltinCallJumpMode::kForMksnapshot: {
      Handle<Code> code = isolate()->builtins()->code_handle(builtin);
      call(code, RelocInfo::CODE_TARGET);
      break;
    }
  }
}

void MacroAssembler::TailCallBuiltin(Builtin builtin, Condition cond) {
  ASM_CODE_COMMENT_STRING(this,
                          CommentForOffHeapTrampoline("tail call", builtin));
  // Use ip directly instead of using UseScratchRegisterScope, as we do not
  // preserve scratch registers across calls.
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      mov(ip, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Jump(ip, cond);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      UNREACHABLE();
    case BuiltinCallJumpMode::kIndirect:
      LoadU64(ip, EntryFromBuiltinAsOperand(builtin));
      Jump(ip, cond);
      break;
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        jump(code, RelocInfo::RELATIVE_CODE_TARGET, cond);
      } else {
        LoadU64(ip, EntryFromBuiltinAsOperand(builtin));
        Jump(ip, cond);
      }
      break;
    }
  }
}

void MacroAssembler::Drop(int count) {
  if (count > 0) {
    int total = count * kSystemPointerSize;
    if (is_uint12(total)) {
      la(sp, MemOperand(sp, total));
    } else if (is_int20(total)) {
      lay(sp, MemOperand(sp, total));
    } else {
      AddS64(sp, Operand(total));
    }
  }
}

void MacroAssembler::Drop(Register count, Register scratch) {
  ShiftLeftU64(scratch, count, Operand(kSystemPointerSizeLog2));
  AddS64(sp, sp, scratch);
}

void MacroAssembler::TestCodeIsMarkedForDeoptimization(Register code,
                                                       Register scratch) {
  LoadU32(scratch, FieldMemOperand(code, Code::kFlagsOffset));
  TestBit(scratch, Code::kMarkedForDeoptimizationBit, scratch);
}

Operand MacroAssembler::ClearedValue() const {
  return Operand(static_cast<int32_t>(i::ClearedValue(isolate()).ptr()));
}

void MacroAssembler::Call(Label* target) { b(r14, target); }

void MacroAssembler::Push(Handle<HeapObject> handle) {
  mov(r0, Operand(handle));
  push(r0);
}

void MacroAssembler::Push(Tagged<Smi> smi) {
  mov(r0, Operand(smi));
  push(r0);
}

void MacroAssembler::Push(Tagged<TaggedIndex> index) {
  // TaggedIndex is the same as Smi for 32 bit archs.
  mov(r0, Operand(static_cast<uint32_t>(index.value())));
  push(r0);
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
  if (dst != src) {
    if (cond == al) {
      mov(dst, src);
    } else {
      LoadOnConditionP(cond, dst, src);
    }
  }
}

void MacroAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  if (dst != src) {
    ldr(dst, src);
  }
}

void MacroAssembler::Move(Register dst, const MemOperand& src) {
  LoadU64(dst, src);
}

// Wrapper around Assembler::mvc (SS-a format)
void MacroAssembler::MoveChar(const MemOperand& opnd1, const MemOperand& opnd2,
                              const Operand& length) {
  mvc(opnd1, opnd2, Operand(static_cast<intptr_t>(length.immediate() - 1)));
}

// Wrapper around Assembler::clc (SS-a format)
void MacroAssembler::CompareLogicalChar(const MemOperand& opnd1,
                                        const MemOperand& opnd2,
                                        const Operand& length) {
  clc(opnd1, opnd2, Operand(static_cast<intptr_t>(length.immediate() - 1)));
}

// Wrapper around Assembler::xc (SS-a format)
void MacroAssembler::ExclusiveOrChar(const MemOperand& opnd1,
                                     const MemOperand& opnd2,
                                     const Operand& length) {
  xc(opnd1, opnd2, Operand(static_cast<intptr_t>(length.immediate() - 1)));
}

// Wrapper around Assembler::risbg(n) (RIE-f)
void MacroAssembler::RotateInsertSelectBits(Register dst, Register src,
                                            const Operand& startBit,
                                            const Operand& endBit,
                                            const Operand& shiftAmt,
                                            bool zeroBits) {
  if (zeroBits)
    // High tag the top bit of I4/EndBit to zero out any unselected bits
    risbg(dst, src, startBit,
          Operand(static_cast<intptr_t>(endBit.immediate() | 0x80)), shiftAmt);
  else
    risbg(dst, src, startBit, endBit, shiftAmt);
}

void MacroAssembler::BranchRelativeOnIdxHighP(Register dst, Register inc,
                                              Label* L) {
#if V8_TARGET_ARCH_S390X
  brxhg(dst, inc, L);
#else
  brxh(dst, inc, L);
#endif  // V8_TARGET_ARCH_S390X
}

void MacroAssembler::PushArray(Register array, Register size, Register scratch,
                               Register scratch2, PushArrayOrder order) {
  Label loop, done;

  if (order == kNormal) {
    ShiftLeftU64(scratch, size, Operand(kSystemPointerSizeLog2));
    lay(scratch, MemOperand(array, scratch));
    bind(&loop);
    CmpS64(array, scratch);
    bge(&done);
    lay(scratch, MemOperand(scratch, -kSystemPointerSize));
    lay(sp, MemOperand(sp, -kSystemPointerSize));
    MoveChar(MemOperand(sp), MemOperand(scratch), Operand(kSystemPointerSize));
    b(&loop);
    bind(&done);
  } else {
    DCHECK_NE(scratch2, r0);
    ShiftLeftU64(scratch, size, Operand(kSystemPointerSizeLog2));
    lay(scratch, MemOperand(array, scratch));
    mov(scratch2, array);
    bind(&loop);
    CmpS64(scratch2, scratch);
    bge(&done);
    lay(sp, MemOperand(sp, -kSystemPointerSize));
    MoveChar(MemOperand(sp), MemOperand(scratch2), Operand(kSystemPointerSize));
    lay(scratch2, MemOperand(scratch2, kSystemPointerSize));
    b(&loop);
    bind(&done);
  }
}

void MacroAssembler::MultiPush(RegList regs, Register location) {
  int16_t num_to_push = regs.Count();
  int16_t stack_offset = num_to_push * kSystemPointerSize;

  SubS64(location, location, Operand(stack_offset));
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
  AddS64(location, location, Operand(stack_offset));
}

void MacroAssembler::MultiPushDoubles(DoubleRegList dregs, Register location) {
  int16_t num_to_push = dregs.Count();
  int16_t stack_offset = num_to_push * kDoubleSize;

  SubS64(location, location, Operand(stack_offset));
  for (int16_t i = DoubleRegister::kNumRegisters - 1; i >= 0; i--) {
    if ((dregs.bits() & (1 << i)) != 0) {
      DoubleRegister dreg = DoubleRegister::from_code(i);
      stack_offset -= kDoubleSize;
      StoreF64(dreg, MemOperand(location, stack_offset));
    }
  }
}

void MacroAssembler::MultiPushV128(DoubleRegList dregs, Register scratch,
                                   Register location) {
  int16_t num_to_push = dregs.Count();
  int16_t stack_offset = num_to_push * kSimd128Size;

  SubS64(location, location, Operand(stack_offset));
  for (int16_t i = Simd128Register::kNumRegisters - 1; i >= 0; i--) {
    if ((dregs.bits() & (1 << i)) != 0) {
      Simd128Register dreg = Simd128Register::from_code(i);
      stack_offset -= kSimd128Size;
      StoreV128(dreg, MemOperand(location, stack_offset), scratch);
    }
  }
}

void MacroAssembler::MultiPopDoubles(DoubleRegList dregs, Register location) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < DoubleRegister::kNumRegisters; i++) {
    if ((dregs.bits() & (1 << i)) != 0) {
      DoubleRegister dreg = DoubleRegister::from_code(i);
      LoadF64(dreg, MemOperand(location, stack_offset));
      stack_offset += kDoubleSize;
    }
  }
  AddS64(location, location, Operand(stack_offset));
}

void MacroAssembler::MultiPopV128(DoubleRegList dregs, Register scratch,
                                  Register location) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < Simd128Register::kNumRegisters; i++) {
    if ((dregs.bits() & (1 << i)) != 0) {
      Simd128Register dreg = Simd128Register::from_code(i);
      LoadV128(dreg, MemOperand(location, stack_offset), scratch);
      stack_offset += kSimd128Size;
    }
  }
  AddS64(location, location, Operand(stack_offset));
}

void MacroAssembler::MultiPushF64OrV128(DoubleRegList dregs, Register scratch,
                                        Register location) {
#if V8_ENABLE_WEBASSEMBLY
  bool generating_bultins =
      isolate() && isolate()->IsGeneratingEmbeddedBuiltins();
  if (generating_bultins) {
    Label push_doubles, simd_pushed;
    Move(r1, ExternalReference::supports_wasm_simd_128_address());
    LoadU8(r1, MemOperand(r1));
    LoadAndTestP(r1, r1);  // If > 0 then simd is available.
    ble(&push_doubles, Label::kNear);
    // Save vector registers, don't save double registers anymore.
    MultiPushV128(dregs, scratch);
    b(&simd_pushed);
    bind(&push_doubles);
    // Simd not supported, only save double registers.
    MultiPushDoubles(dregs);
    // We still need to allocate empty space on the stack as if
    // Simd rgeisters were saved (see kFixedFrameSizeFromFp).
    lay(sp, MemOperand(sp, -(dregs.Count() * kDoubleSize)));
    bind(&simd_pushed);
  } else {
    if (CpuFeatures::SupportsWasmSimd128()) {
      MultiPushV128(dregs, scratch);
    } else {
      MultiPushDoubles(dregs);
      lay(sp, MemOperand(sp, -(dregs.Count() * kDoubleSize)));
    }
  }
#else
  MultiPushDoubles(dregs);
#endif
}

void MacroAssembler::MultiPopF64OrV128(DoubleRegList dregs, Register scratch,
                                       Register location) {
#if V8_ENABLE_WEBASSEMBLY
  bool generating_bultins =
      isolate() && isolate()->IsGeneratingEmbeddedBuiltins();
  if (generating_bultins) {
    Label pop_doubles, simd_popped;
    Move(r1, ExternalReference::supports_wasm_simd_128_address());
    LoadU8(r1, MemOperand(r1));
    LoadAndTestP(r1, r1);  // If > 0 then simd is available.
    ble(&pop_doubles, Label::kNear);
    // Pop vector registers, don't pop double registers anymore.
    MultiPopV128(dregs, scratch);
    b(&simd_popped);
    bind(&pop_doubles);
    // Simd not supported, only pop double registers.
    lay(sp, MemOperand(sp, dregs.Count() * kDoubleSize));
    MultiPopDoubles(dregs);
    bind(&simd_popped);
  } else {
    if (CpuFeatures::SupportsWasmSimd128()) {
      MultiPopV128(dregs, scratch);
    } else {
      lay(sp, MemOperand(sp, dregs.Count() * kDoubleSize));
      MultiPopDoubles(dregs);
    }
  }
#else
  MultiPopDoubles(dregs);
#endif
}

void MacroAssembler::PushAll(RegList registers) {
  if (registers.is_empty()) return;
  ASM_CODE_COMMENT(this);
  // TODO(victorgomes): {stm/ldm} pushes/pops registers in the opposite order
  // as expected by Maglev frame. Consider massaging Maglev to accept this
  // order instead.
  // Can not use MultiPush(registers, sp) due to orders
  for (Register reg : registers) {
    Push(reg);
  }
}

void MacroAssembler::PopAll(RegList registers) {
  if (registers.is_empty()) return;
  ASM_CODE_COMMENT(this);
  // Can not use MultiPop(registers, sp);
  for (Register reg : base::Reversed(registers)) {
    Pop(reg);
  }
}

void MacroAssembler::PushAll(DoubleRegList registers, int stack_slot_size) {
  if (registers.is_empty()) return;
  ASM_CODE_COMMENT(this);
  MultiPushDoubles(registers, sp);
}

void MacroAssembler::PopAll(DoubleRegList registers, int stack_slot_size) {
  if (registers.is_empty()) return;
  ASM_CODE_COMMENT(this);
  MultiPopDoubles(registers, sp);
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
                              Condition) {
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
void MacroAssembler::LoadTaggedFieldWithoutDecompressing(
    const Register& destination, const MemOperand& field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    llgf(destination, field_operand);
  } else {
    LoadU64(destination, field_operand);
  }
}
void MacroAssembler::SmiUntag(Register dst, const MemOperand& src) {
  if (SmiValuesAre31Bits()) {
    LoadS32(dst, src);
  } else {
    LoadU64(dst, src);
  }
  SmiUntag(dst);
}

void MacroAssembler::SmiUntagField(Register dst, const MemOperand& src) {
  SmiUntag(dst, src);
}

void MacroAssembler::StoreTaggedField(const Register& value,
                                      const MemOperand& dst_field_operand,
                                      const Register& scratch) {
  if (COMPRESS_POINTERS_BOOL) {
    RecordComment("[ StoreTagged");
    StoreU32(value, dst_field_operand);
    RecordComment("]");
  } else {
    StoreU64(value, dst_field_operand, scratch);
  }
}

void MacroAssembler::DecompressTaggedSigned(Register destination,
                                            Register src) {
  RecordComment("[ DecompressTaggedSigned");
  llgfr(destination, src);
  RecordComment("]");
}

void MacroAssembler::DecompressTaggedSigned(Register destination,
                                            MemOperand field_operand) {
  RecordComment("[ DecompressTaggedSigned");
  llgf(destination, field_operand);
  RecordComment("]");
}

void MacroAssembler::DecompressTagged(Register destination, Register source) {
  RecordComment("[ DecompressTagged");
  llgfr(destination, source);
  agr(destination, kPtrComprCageBaseRegister);
  RecordComment("]");
}

void MacroAssembler::DecompressTagged(Register destination,
                                      MemOperand field_operand) {
  RecordComment("[ DecompressTagged");
  llgf(destination, field_operand);
  agr(destination, kPtrComprCageBaseRegister);
  RecordComment("]");
}

void MacroAssembler::DecompressTagged(const Register& destination,
                                      Tagged_t immediate) {
  ASM_CODE_COMMENT(this);
  mov(destination, Operand(immediate, RelocInfo::NO_INFO));
  agr(destination, kRootRegister);
}

void MacroAssembler::LoadTaggedSignedField(Register destination,
                                           MemOperand field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTaggedSigned(destination, field_operand);
  } else {
    LoadU64(destination, field_operand);
  }
}

void MacroAssembler::RecordWriteField(Register object, int offset,
                                      Register value, Register slot_address,
                                      LinkRegisterStatus lr_status,
                                      SaveFPRegsMode save_fp,
                                      SmiCheck smi_check) {
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

  lay(slot_address, MemOperand(object, offset - kHeapObjectTag));
  if (v8_flags.debug_code) {
    Label ok;
    AndP(r0, slot_address, Operand(kTaggedSize - 1));
    beq(&ok, Label::kNear);
    stop();
    bind(&ok);
  }

  RecordWrite(object, slot_address, value, lr_status, save_fp, SmiCheck::kOmit);

  bind(&done);

  // Clobber clobbered input registers when running with the debug-code flag
  // turned on to provoke errors.
  if (v8_flags.debug_code) {
    mov(value, Operand(base::bit_cast<intptr_t>(kZapValue + 4)));
    mov(slot_address, Operand(base::bit_cast<intptr_t>(kZapValue + 8)));
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

// Will clobber 4 registers: object, address, scratch, ip.  The
// register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWrite(Register object, Register slot_address,
                                 Register value, LinkRegisterStatus lr_status,
                                 SaveFPRegsMode fp_mode, SmiCheck smi_check) {
  DCHECK(!AreAliased(object, slot_address, value));
  if (v8_flags.debug_code) {
    LoadTaggedField(r0, MemOperand(slot_address));
    CmpS64(value, r0);
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
    push(r14);
  }
  CallRecordWriteStubSaveRegisters(object, slot_address, fp_mode);
  if (lr_status == kLRHasNotBeenSaved) {
    pop(r14);
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
  ASM_CODE_COMMENT(this);
  int fp_delta = 0;
  CleanseP(r14);
  if (marker_reg.is_valid()) {
    Push(r14, fp, marker_reg);
    fp_delta = 1;
  } else {
    Push(r14, fp);
    fp_delta = 0;
  }
  la(fp, MemOperand(sp, fp_delta * kSystemPointerSize));
}

void MacroAssembler::PopCommonFrame(Register marker_reg) {
  if (marker_reg.is_valid()) {
    Pop(r14, fp, marker_reg);
  } else {
    Pop(r14, fp);
  }
}

void MacroAssembler::PushStandardFrame(Register function_reg) {
  int fp_delta = 0;
  CleanseP(r14);
  if (function_reg.is_valid()) {
    Push(r14, fp, cp, function_reg);
    fp_delta = 2;
  } else {
    Push(r14, fp, cp);
    fp_delta = 1;
  }
  la(fp, MemOperand(sp, fp_delta * kSystemPointerSize));
  Push(kJavaScriptCallArgCountRegister);
}

void MacroAssembler::RestoreFrameStateForTailCall() {
  // if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
  //   LoadU64(kConstantPoolRegister,
  //         MemOperand(fp, StandardFrameConstants::kConstantPoolOffset));
  //   set_constant_pool_available(false);
  // }
  DCHECK(!V8_EMBEDDED_CONSTANT_POOL_BOOL);
  LoadU64(r14, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
  LoadU64(fp, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
}

void MacroAssembler::CanonicalizeNaN(const DoubleRegister dst,
                                     const DoubleRegister src) {
  // Turn potential sNaN into qNaN
  if (dst != src) ldr(dst, src);
  lzdr(kDoubleRegZero);
  sdbr(dst, kDoubleRegZero);
}

void MacroAssembler::ConvertIntToDouble(DoubleRegister dst, Register src) {
  cdfbr(dst, src);
}

void MacroAssembler::ConvertUnsignedIntToDouble(DoubleRegister dst,
                                                Register src) {
  if (CpuFeatures::IsSupported(FLOATING_POINT_EXT)) {
    cdlfbr(Condition(5), Condition(0), dst, src);
  } else {
    // zero-extend src
    llgfr(src, src);
    // convert to double
    cdgbr(dst, src);
  }
}

void MacroAssembler::ConvertIntToFloat(DoubleRegister dst, Register src) {
  cefbra(Condition(4), dst, src);
}

void MacroAssembler::ConvertUnsignedIntToFloat(DoubleRegister dst,
                                               Register src) {
  celfbr(Condition(4), Condition(0), dst, src);
}

void MacroAssembler::ConvertInt64ToFloat(DoubleRegister double_dst,
                                         Register src) {
  cegbr(double_dst, src);
}

void MacroAssembler::ConvertInt64ToDouble(DoubleRegister double_dst,
                                          Register src) {
  cdgbr(double_dst, src);
}

void MacroAssembler::ConvertUnsignedInt64ToFloat(DoubleRegister double_dst,
                                                 Register src) {
  celgbr(Condition(0), Condition(0), double_dst, src);
}

void MacroAssembler::ConvertUnsignedInt64ToDouble(DoubleRegister double_dst,
                                                  Register src) {
  cdlgbr(Condition(0), Condition(0), double_dst, src);
}

void MacroAssembler::ConvertFloat32ToInt64(const Register dst,
                                           const DoubleRegister double_input,
                                           FPRoundingMode rounding_mode) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      UNIMPLEMENTED();
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
  }
  cgebr(m, dst, double_input);
}

void MacroAssembler::ConvertDoubleToInt64(const Register dst,
                                          const DoubleRegister double_input,
                                          FPRoundingMode rounding_mode) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      UNIMPLEMENTED();
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
  }
  cgdbr(m, dst, double_input);
}

void MacroAssembler::ConvertDoubleToInt32(const Register dst,
                                          const DoubleRegister double_input,
                                          FPRoundingMode rounding_mode) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      m = Condition(4);
      break;
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
  }
#ifdef V8_TARGET_ARCH_S390X
  lghi(dst, Operand::Zero());
#endif
  cfdbr(m, dst, double_input);
}

void MacroAssembler::ConvertFloat32ToInt32(const Register result,
                                           const DoubleRegister double_input,
                                           FPRoundingMode rounding_mode) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      m = Condition(4);
      break;
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
  }
#ifdef V8_TARGET_ARCH_S390X
  lghi(result, Operand::Zero());
#endif
  cfebr(m, result, double_input);
}

void MacroAssembler::ConvertFloat32ToUnsignedInt32(
    const Register result, const DoubleRegister double_input,
    FPRoundingMode rounding_mode) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      UNIMPLEMENTED();
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
  }
#ifdef V8_TARGET_ARCH_S390X
  lghi(result, Operand::Zero());
#endif
  clfebr(m, Condition(0), result, double_input);
}

void MacroAssembler::ConvertFloat32ToUnsignedInt64(
    const Register result, const DoubleRegister double_input,
    FPRoundingMode rounding_mode) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      UNIMPLEMENTED();
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
  }
  clgebr(m, Condition(0), result, double_input);
}

void MacroAssembler::ConvertDoubleToUnsignedInt64(
    const Register dst, const DoubleRegister double_input,
    FPRoundingMode rounding_mode) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      UNIMPLEMENTED();
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
  }
  clgdbr(m, Condition(0), dst, double_input);
}

void MacroAssembler::ConvertDoubleToUnsignedInt32(
    const Register dst, const DoubleRegister double_input,
    FPRoundingMode rounding_mode) {
  Condition m = Condition(0);
  switch (rounding_mode) {
    case kRoundToZero:
      m = Condition(5);
      break;
    case kRoundToNearest:
      UNIMPLEMENTED();
    case kRoundToPlusInf:
      m = Condition(6);
      break;
    case kRoundToMinusInf:
      m = Condition(7);
      break;
    default:
      UNIMPLEMENTED();
  }
#ifdef V8_TARGET_ARCH_S390X
  lghi(dst, Operand::Zero());
#endif
  clfdbr(m, Condition(0), dst, double_input);
}

void MacroAssembler::MovDoubleToInt64(Register dst, DoubleRegister src) {
  lgdr(dst, src);
}

void MacroAssembler::MovInt64ToDouble(DoubleRegister dst, Register src) {
  ldgr(dst, src);
}

void MacroAssembler::StubPrologue(StackFrame::Type type, Register base,
                                  int prologue_offset) {
  {
    ConstantPoolUnavailableScope constant_pool_unavailable(this);
    mov(r1, Operand(StackFrame::TypeToMarker(type)));
    PushCommonFrame(r1);
  }
}

void MacroAssembler::Prologue(Register base, int prologue_offset) {
  DCHECK(base != no_reg);
  PushStandardFrame(r3);
}

void MacroAssembler::DropArguments(Register count) {
  ShiftLeftU64(ip, count, Operand(kSystemPointerSizeLog2));
  lay(sp, MemOperand(sp, ip));
}

void MacroAssembler::DropArgumentsAndPushNewReceiver(Register argc,
                                                     Register receiver) {
  DCHECK(!AreAliased(argc, receiver));
  DropArguments(argc);
  push(receiver);
}

void MacroAssembler::EnterFrame(StackFrame::Type type,
                                bool load_constant_pool_pointer_reg) {
  ASM_CODE_COMMENT(this);
  // We create a stack frame with:
  //    Return Addr <-- old sp
  //    Old FP      <-- new fp
  //    CP
  //    type
  //    CodeObject  <-- new sp

  Register scratch = no_reg;
  if (!StackFrame::IsJavaScript(type)) {
    scratch = ip;
    mov(scratch, Operand(StackFrame::TypeToMarker(type)));
  }
  PushCommonFrame(scratch);
#if V8_ENABLE_WEBASSEMBLY
  if (type == StackFrame::WASM) Push(kWasmInstanceRegister);
#endif  // V8_ENABLE_WEBASSEMBLY
}

int MacroAssembler::LeaveFrame(StackFrame::Type type, int stack_adjustment) {
  ASM_CODE_COMMENT(this);
  // Drop the execution stack down to the frame pointer and restore
  // the caller frame pointer, return address and constant pool pointer.
  LoadU64(r14, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
  if (is_int20(StandardFrameConstants::kCallerSPOffset + stack_adjustment)) {
    lay(r1, MemOperand(fp, StandardFrameConstants::kCallerSPOffset +
                               stack_adjustment));
  } else {
    AddS64(r1, fp,
           Operand(StandardFrameConstants::kCallerSPOffset + stack_adjustment));
  }
  LoadU64(fp, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  mov(sp, r1);
  int frame_ends = pc_offset();
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
// in the fp register (r11)
// Then - we buy a new frame

// r14
// oldFP <- newFP
// SP
// Floats
// gaps
// Args
// ABIRes <- newSP
void MacroAssembler::EnterExitFrame(Register scratch, int stack_space,
                                    StackFrame::Type frame_type) {
  DCHECK(frame_type == StackFrame::EXIT ||
         frame_type == StackFrame::BUILTIN_EXIT ||
         frame_type == StackFrame::API_ACCESSOR_EXIT ||
         frame_type == StackFrame::API_CALLBACK_EXIT);

  // Set up the frame structure on the stack.
  DCHECK_EQ(2 * kSystemPointerSize, ExitFrameConstants::kCallerSPDisplacement);
  DCHECK_EQ(1 * kSystemPointerSize, ExitFrameConstants::kCallerPCOffset);
  DCHECK_EQ(0 * kSystemPointerSize, ExitFrameConstants::kCallerFPOffset);

  using ER = ExternalReference;

  // This is an opportunity to build a frame to wrap
  // all of the pushes that have happened inside of V8
  // since we were called from C code
  CleanseP(r14);
  mov(r1, Operand(StackFrame::TypeToMarker(frame_type)));
  PushCommonFrame(r1);
  // Reserve room for saved entry sp.
  lay(sp, MemOperand(fp, -ExitFrameConstants::kFixedFrameSizeFromFp));

  if (v8_flags.debug_code) {
    StoreU64(MemOperand(fp, ExitFrameConstants::kSPOffset), Operand::Zero(),
             r1);
  }

  // Save the frame pointer and the context in top.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  StoreU64(fp, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));

  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  StoreU64(cp, ExternalReferenceAsOperand(context_address, no_reg));

  lay(sp, MemOperand(sp, -(stack_space + 1) * kSystemPointerSize));

  // Allocate and align the frame preparing for calling the runtime
  // function.
  const int frame_alignment = MacroAssembler::ActivationFrameAlignment();
  if (frame_alignment > 0) {
    DCHECK_EQ(frame_alignment, 8);
    ClearRightImm(sp, sp, Operand(3));  // equivalent to &= -8
  }

  lay(sp, MemOperand(sp, -kNumRequiredStackFrameSlots * kSystemPointerSize));
  StoreU64(MemOperand(sp), Operand::Zero(), r0);
  // Set the exit frame sp value to point just before the return address
  // location.
  lay(r1, MemOperand(sp, kStackFrameSPSlot * kSystemPointerSize));
  StoreU64(r1, MemOperand(fp, ExitFrameConstants::kSPOffset));
}

int MacroAssembler::ActivationFrameAlignment() {
#if !defined(USE_SIMULATOR)
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one S390
  // platform for another S390 platform with a different alignment.
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
  Move(dst, d0);
}

void MacroAssembler::MovFromFloatParameter(const DoubleRegister dst) {
  Move(dst, d0);
}

MemOperand MacroAssembler::StackLimitAsMemOperand(StackLimitKind kind) {
  DCHECK(root_array_available());
  Isolate* isolate = this->isolate();
  ExternalReference limit =
      kind == StackLimitKind::kRealStackLimit
          ? ExternalReference::address_of_real_jslimit(isolate)
          : ExternalReference::address_of_jslimit(isolate);
  DCHECK(MacroAssembler::IsAddressableThroughRootRegister(isolate, limit));

  intptr_t offset =
      MacroAssembler::RootRegisterOffsetForExternalReference(isolate, limit);
  CHECK(is_int32(offset));
  return MemOperand(kRootRegister, offset);
}

void MacroAssembler::StackOverflowCheck(Register num_args, Register scratch,
                                        Label* stack_overflow) {
  // Check the stack for overflow. We are not trying to catch
  // interruptions (e.g. debug break and preemption) here, so the "real stack
  // limit" is checked.
  LoadU64(scratch, StackLimitAsMemOperand(StackLimitKind::kRealStackLimit));
  // Make scratch the space we have left. The stack might already be overflowed
  // here which will cause scratch to become negative.
  SubS64(scratch, sp, scratch);
  // Check if the arguments will overflow the stack.
  ShiftLeftU64(r0, num_args, Operand(kSystemPointerSizeLog2));
  CmpS64(scratch, r0);
  ble(stack_overflow);  // Signed comparison.
}

void MacroAssembler::InvokePrologue(Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    Label* done, InvokeType type) {
  Label regular_invoke;

  //  r2: actual arguments count
  //  r3: function (passed through to callee)
  //  r4: expected arguments count

  DCHECK_EQ(actual_parameter_count, r2);
  DCHECK_EQ(expected_parameter_count, r4);

  // If overapplication or if the actual argument count is equal to the
  // formal parameter count, no need to push extra undefined values.
  SubS64(expected_parameter_count, expected_parameter_count,
         actual_parameter_count);
  ble(&regular_invoke);

  Label stack_overflow;
  Register scratch = r6;
  StackOverflowCheck(expected_parameter_count, scratch, &stack_overflow);

  // Underapplication. Move the arguments already in the stack, including the
  // receiver and the return address.
  {
    Label copy, check;
    Register num = r7, src = r8, dest = ip;  // r7 and r8 are context and root.
    mov(src, sp);
    // Update stack pointer.
    ShiftLeftU64(scratch, expected_parameter_count,
                 Operand(kSystemPointerSizeLog2));
    SubS64(sp, sp, scratch);
    mov(dest, sp);
    ltgr(num, actual_parameter_count);
    b(&check);
    bind(&copy);
    LoadU64(r0, MemOperand(src));
    lay(src, MemOperand(src, kSystemPointerSize));
    StoreU64(r0, MemOperand(dest));
    lay(dest, MemOperand(dest, kSystemPointerSize));
    SubS64(num, num, Operand(1));
    bind(&check);
    b(gt, &copy);
  }

  // Fill remaining expected arguments with undefined values.
  LoadRoot(scratch, RootIndex::kUndefinedValue);
  {
    Label loop;
    bind(&loop);
    StoreU64(scratch, MemOperand(ip));
    lay(ip, MemOperand(ip, kSystemPointerSize));
    SubS64(expected_parameter_count, expected_parameter_count, Operand(1));
    bgt(&loop);
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
  Move(r6, debug_hook_active);
  tm(MemOperand(r6), Operand(0xFF));
  beq(&skip_hook);

  {
    // Load receiver to pass it later to DebugOnFunctionCall hook.
    LoadReceiver(r6);
    FrameScope frame(
        this, has_frame() ? StackFrame::NO_FRAME_TYPE : StackFrame::INTERNAL);

    SmiTag(expected_parameter_count);
    Push(expected_parameter_count);

    SmiTag(actual_parameter_count);
    Push(actual_parameter_count);

    if (new_target.is_valid()) {
      Push(new_target);
    }
    Push(fun, fun, r6);
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
  DCHECK_EQ(function, r3);
  DCHECK_IMPLIES(new_target.is_valid(), new_target == r5);

  // On function call, call into the debugger if necessary.
  CheckDebugHook(function, new_target, expected_parameter_count,
                 actual_parameter_count);

  // Clear the new.target register if not given.
  if (!new_target.is_valid()) {
    LoadRoot(r5, RootIndex::kUndefinedValue);
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
    Register fun, Register new_target, Register actual_parameter_count,
    InvokeType type) {
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());

  // Contract with called JS functions requires that function is passed in r3.
  DCHECK_EQ(fun, r3);

  Register expected_reg = r4;
  Register temp_reg = r6;
  LoadTaggedField(cp, FieldMemOperand(fun, JSFunction::kContextOffset));
  LoadTaggedField(temp_reg,
                  FieldMemOperand(fun, JSFunction::kSharedFunctionInfoOffset));
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

  // Contract with called JS functions requires that function is passed in r3.
  DCHECK_EQ(function, r3);

  // Get the function and setup the context.
  LoadTaggedField(cp, FieldMemOperand(function, JSFunction::kContextOffset));

  InvokeFunctionCode(r3, no_reg, expected_parameter_count,
                     actual_parameter_count, type);
}

void MacroAssembler::PushStackHandler() {
  // Adjust this code if not the case.
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize);
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize);

  // Link the current handler as the next handler.
  Move(r7,
       ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));

  // Buy the full stack frame for 5 slots.
  lay(sp, MemOperand(sp, -StackHandlerConstants::kSize));

  // Store padding.
  lghi(r0, Operand::Zero());
  StoreU64(r0, MemOperand(sp));  // Padding.

  // Copy the old handler into the next handler slot.
  MoveChar(MemOperand(sp, StackHandlerConstants::kNextOffset), MemOperand(r7),
           Operand(kSystemPointerSize));
  // Set this new handler as the current one.
  StoreU64(sp, MemOperand(r7));
}

void MacroAssembler::PopStackHandler() {
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize);
  static_assert(StackHandlerConstants::kNextOffset == 0);

  // Pop the Next Handler into r3 and store it into Handler Address reference.
  Pop(r3);
  Move(ip,
       ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  StoreU64(r3, MemOperand(ip));

  Drop(1);  // Drop padding.
}

void MacroAssembler::IsObjectType(Register object, Register scratch1,
                                  Register scratch2, InstanceType type) {
  ASM_CODE_COMMENT(this);
  CompareObjectType(object, scratch1, scratch2, type);
}

void MacroAssembler::CompareObjectTypeRange(Register object, Register map,
                                            Register type_reg, Register scratch,
                                            InstanceType lower_limit,
                                            InstanceType upper_limit) {
  ASM_CODE_COMMENT(this);
  LoadMap(map, object);
  CompareInstanceTypeRange(map, type_reg, scratch, lower_limit, upper_limit);
}

void MacroAssembler::CompareRange(Register value, Register scratch,
                                  unsigned lower_limit, unsigned higher_limit) {
  ASM_CODE_COMMENT(this);
  DCHECK_LT(lower_limit, higher_limit);
  if (lower_limit != 0) {
    mov(scratch, value);
    slgfi(scratch, Operand(lower_limit));
    CmpU64(scratch, Operand(higher_limit - lower_limit));
  } else {
    CmpU64(value, Operand(higher_limit));
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

void MacroAssembler::CompareRoot(Register obj, RootIndex index) {
  if (!base::IsInRange(index, RootIndex::kFirstStrongOrReadOnlyRoot,
                       RootIndex::kLastStrongOrReadOnlyRoot)) {
    // Some smi roots contain system pointer size values like stack limits.
    LoadRoot(r0, index);
    CmpU64(obj, r0);
    return;
  }
  return CompareTaggedRoot(obj, index);
}

void MacroAssembler::CompareTaggedRoot(Register obj, RootIndex index) {
  if (CanBeImmediate(index)) {
    CompareTagged(obj, Operand(ReadOnlyRootPtr(index)));
    return;
  }
  int32_t offset = RootRegisterOffsetForRootIndex(index);
#ifdef V8_TARGET_BIG_ENDIAN
  offset += (COMPRESS_POINTERS_BOOL ? kTaggedSize : 0);
#endif
  CompareTagged(obj, MemOperand(kRootRegister, offset));
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
  push(r14);
  // Put input on stack.
  lay(sp, MemOperand(sp, -kDoubleSize));
  StoreF64(double_input, MemOperand(sp));

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

  LoadU64(result, MemOperand(sp, 0));
  la(sp, MemOperand(sp, kDoubleSize));
  pop(r14);

  bind(&done);
}

void MacroAssembler::TryInlineTruncateDoubleToI(Register result,
                                                DoubleRegister double_input,
                                                Label* done) {
  ConvertDoubleToInt64(result, double_input);

  // Test for overflow
  TestIfInt32(result);
  beq(done);
}

namespace {

void TailCallOptimizedCodeSlot(MacroAssembler* masm,
                               Register optimized_code_entry,
                               Register scratch) {
  // ----------- S t a t e -------------
  //  -- r2 : actual argument count
  //  -- r5 : new target (preserved for callee if needed, and caller)
  //  -- r3 : target function (preserved for callee if needed, and caller)
  // -----------------------------------
  DCHECK(!AreAliased(r3, r5, optimized_code_entry, scratch));

  Register closure = r3;
  Label heal_optimized_code_slot;

  // If the optimized code is cleared, go to runtime to update the optimization
  // marker field.
  __ LoadWeakValue(optimized_code_entry, optimized_code_entry,
                   &heal_optimized_code_slot);

  // The entry references a CodeWrapper object. Unwrap it now.
  __ LoadTaggedField(
      optimized_code_entry,
      FieldMemOperand(optimized_code_entry, CodeWrapper::kCodeOffset));

  // Check if the optimized code is marked for deopt. If it is, call the
  // runtime to clear it.
  {
    __ TestCodeIsMarkedForDeoptimization(optimized_code_entry, scratch);
    __ bne(&heal_optimized_code_slot);
  }

  // Optimized code is good, get it into the closure and link the closure
  // into the optimized functions list, then tail call the optimized code.
  __ ReplaceClosureCodeWithOptimizedCode(optimized_code_entry, closure, scratch,
                                         r7);
  static_assert(kJavaScriptCallCodeStartRegister == r4, "ABI mismatch");
  __ LoadCodeInstructionStart(r4, optimized_code_entry);
  __ Jump(r4);

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
void MacroAssembler::AssertFeedbackVector(Register object) {
  if (v8_flags.debug_code) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
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
  StoreTaggedField(optimized_code,
                   FieldMemOperand(closure, JSFunction::kCodeOffset), r0);
  // Write barrier clobbers scratch1 below.
  Register value = scratch1;
  mov(value, optimized_code);

  RecordWriteField(closure, JSFunction::kCodeOffset, value, slot_address,
                   kLRHasNotBeenSaved, SaveFPRegsMode::kIgnore,
                   SmiCheck::kOmit);
}

void MacroAssembler::GenerateTailCallToReturnedCode(
    Runtime::FunctionId function_id) {
  // ----------- S t a t e -------------
  //  -- r2 : actual argument count
  //  -- r3 : target function (preserved for callee)
  //  -- r5 : new target (preserved for callee)
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
    mov(r4, r2);

    // Restore target function, new target and actual argument count.
    Pop(kJavaScriptCallTargetRegister, kJavaScriptCallNewTargetRegister,
        kJavaScriptCallArgCountRegister);
    SmiUntag(kJavaScriptCallArgCountRegister);
  }
  static_assert(kJavaScriptCallCodeStartRegister == r4, "ABI mismatch");
  JumpCodeObject(r4);
}

// Read off the flags in the feedback vector and check if there
// is optimized code or a tiering state that needs to be processed.
Condition MacroAssembler::LoadFeedbackVectorFlagsAndCheckIfNeedsProcessing(
    Register flags, Register feedback_vector, CodeKind current_code_kind) {
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
  tmll(flags, Operand(kFlagsMask));
  return Condition(7);
}

// Read off the flags in the feedback vector and check if there
// is optimized code or a tiering state that needs to be processed.
void MacroAssembler::LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
    Register flags, Register feedback_vector, CodeKind current_code_kind,
    Label* flags_need_processing) {
  ASM_CODE_COMMENT(this);
  b(LoadFeedbackVectorFlagsAndCheckIfNeedsProcessing(flags, feedback_vector,
                                                     current_code_kind),
    flags_need_processing);
}

void MacroAssembler::OptimizeCodeOrTailCallOptimizedCodeSlot(
    Register flags, Register feedback_vector) {
  DCHECK(!AreAliased(flags, feedback_vector));
  Label maybe_has_optimized_code, maybe_needs_logging;
  // Check if optimized code is available
  TestBitMask(flags, FeedbackVector::kFlagsTieringStateIsAnyRequested, r0);
  beq(&maybe_needs_logging);

  GenerateTailCallToReturnedCode(Runtime::kCompileOptimized);

  bind(&maybe_needs_logging);
  TestBitMask(flags, FeedbackVector::LogNextExecutionBit::kMask, r0);
  beq(&maybe_has_optimized_code);
  GenerateTailCallToReturnedCode(Runtime::kFunctionLogNextExecution);

  bind(&maybe_has_optimized_code);
  Register optimized_code_entry = flags;
  LoadTaggedField(optimized_code_entry,
                  FieldMemOperand(feedback_vector,
                                  FeedbackVector::kMaybeOptimizedCodeOffset));
  TailCallOptimizedCodeSlot(this, optimized_code_entry, r1);
}

void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments) {
  // All parameters are on the stack.  r2 has the return value after call.

  // If the expected number of arguments of the runtime function is
  // constant, we check that the actual number of arguments match the
  // expectation.
  CHECK(f->nargs < 0 || f->nargs == num_arguments);

  // TODO(1236192): Most runtime routines don't need the number of
  // arguments passed in because it is constant. At some point we
  // should remove this need and make the runtime routine entry code
  // smarter.
  mov(r2, Operand(num_arguments));
  Move(r3, ExternalReference::Create(f));
#if V8_TARGET_ARCH_S390X
  CallBuiltin(Builtins::RuntimeCEntry(f->result_size));
#else
  CallBuiltin(Builtins::RuntimeCEntry(1));
#endif
}

void MacroAssembler::TailCallRuntime(Runtime::FunctionId fid) {
  const Runtime::Function* function = Runtime::FunctionForId(fid);
  DCHECK_EQ(1, function->result_size);
  if (function->nargs >= 0) {
    mov(r2, Operand(function->nargs));
  }
  JumpToExternalReference(ExternalReference::Create(fid));
}

void MacroAssembler::JumpToExternalReference(const ExternalReference& builtin,
                                             bool builtin_exit_frame) {
  Move(r3, builtin);
  TailCallBuiltin(Builtins::CEntry(1, ArgvMode::kStack, builtin_exit_frame));
}

void MacroAssembler::LoadWeakValue(Register out, Register in,
                                   Label* target_if_cleared) {
  CmpS32(in, Operand(kClearedWeakHeapObjectLower32));
  beq(target_if_cleared);

  AndP(out, in, Operand(~kWeakHeapObjectMask));
}

void MacroAssembler::EmitIncrementCounter(StatsCounter* counter, int value,
                                          Register scratch1,
                                          Register scratch2) {
  DCHECK(value > 0 && is_int8(value));
  if (v8_flags.native_code_counters && counter->Enabled()) {
    Move(scratch2, ExternalReference::Create(counter));
    // @TODO(john.yan): can be optimized by asi()
    LoadS32(scratch1, MemOperand(scratch2));
    AddS64(scratch1, Operand(value));
    StoreU32(scratch1, MemOperand(scratch2));
  }
}

void MacroAssembler::EmitDecrementCounter(StatsCounter* counter, int value,
                                          Register scratch1,
                                          Register scratch2) {
  DCHECK(value > 0 && is_int8(value));
  if (v8_flags.native_code_counters && counter->Enabled()) {
    Move(scratch2, ExternalReference::Create(counter));
    // @TODO(john.yan): can be optimized by asi()
    LoadS32(scratch1, MemOperand(scratch2));
    AddS64(scratch1, Operand(-value));
    StoreU32(scratch1, MemOperand(scratch2));
  }
}

void MacroAssembler::Check(Condition cond, AbortReason reason, CRegister cr) {
  Label L;
  b(to_condition(cond), &L);
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
    lgfi(r2, Operand(static_cast<int>(reason)));
    PrepareCallCFunction(1, 0, r3);
#if V8_OS_ZOS
    CallCFunction(ExternalReference::abort_with_reason(), 1, 0);
#else
    Move(r3, ExternalReference::abort_with_reason());
    // Use Call directly to avoid any unneeded overhead. The function won't
    // return anyway.
    Call(r3);
#endif
    return;
  }

  LoadSmiLiteral(r3, Smi::FromInt(static_cast<int>(reason)));

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
  LoadTaggedField(destination, FieldMemOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadFeedbackVector(Register dst, Register closure,
                                        Register scratch, Label* fbv_undef) {
  Label done;

  // Load the feedback vector from the closure.
  LoadTaggedField(dst,
                  FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  LoadTaggedField(dst, FieldMemOperand(dst, FeedbackCell::kValueOffset));

  // Check if feedback vector is valid.
  IsObjectType(dst, scratch, scratch, FEEDBACK_VECTOR_TYPE);
  b(eq, &done);

  // Not valid, load undefined.
  LoadRoot(dst, RootIndex::kUndefinedValue);
  b(fbv_undef);

  bind(&done);
}

void MacroAssembler::LoadNativeContextSlot(Register dst, int index) {
  LoadMap(dst, cp);
  LoadTaggedField(
      dst, FieldMemOperand(
               dst, Map::kConstructorOrBackPointerOrNativeContextOffset));
  LoadTaggedField(dst, MemOperand(dst, Context::SlotOffset(index)));
}

#ifdef V8_ENABLE_DEBUG_CODE
void MacroAssembler::Assert(Condition cond, AbortReason reason, CRegister cr) {
  if (v8_flags.debug_code) Check(cond, reason, cr);
}

void MacroAssembler::AssertUnreachable(AbortReason reason) {
  if (v8_flags.debug_code) Abort(reason);
}

void MacroAssembler::AssertZeroExtended(Register int32_register) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  mov(r0, Operand(kMaxUInt32));
  CmpS64(int32_register, r0);
  Check(le, AbortReason::k32BitValueInRegisterIsNotZeroExtended);
}

void MacroAssembler::AssertMap(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  TestIfSmi(object);
  Check(ne, AbortReason::kOperandIsNotAMap);
  Push(object);
  LoadMap(object, object);
  CompareInstanceType(object, object, MAP_TYPE);
  Pop(object);
  Check(eq, AbortReason::kOperandIsNotAMap);
}

void MacroAssembler::AssertNotSmi(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object);
    Check(ne, AbortReason::kOperandIsASmi, cr0);
  }
}

void MacroAssembler::AssertSmi(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object);
    Check(eq, AbortReason::kOperandIsNotASmi, cr0);
  }
}

void MacroAssembler::AssertConstructor(Register object, Register scratch) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object);
    Check(ne, AbortReason::kOperandIsASmiAndNotAConstructor);
    LoadMap(scratch, object);
    tm(FieldMemOperand(scratch, Map::kBitFieldOffset),
       Operand(Map::Bits1::IsConstructorBit::kMask));
    Check(ne, AbortReason::kOperandIsNotAConstructor);
  }
}

void MacroAssembler::AssertFunction(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object);
    Check(ne, AbortReason::kOperandIsASmiAndNotAFunction, cr0);
    push(object);
    LoadMap(object, object);
    CompareInstanceTypeRange(object, object, object, FIRST_JS_FUNCTION_TYPE,
                             LAST_JS_FUNCTION_TYPE);
    pop(object);
    Check(le, AbortReason::kOperandIsNotAFunction);
  }
}

void MacroAssembler::AssertCallableFunction(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  static_assert(kSmiTag == 0);
  TestIfSmi(object);
  Check(ne, AbortReason::kOperandIsASmiAndNotAFunction);
  push(object);
  LoadMap(object, object);
  CompareInstanceTypeRange(object, object, object,
                           FIRST_CALLABLE_JS_FUNCTION_TYPE,
                           LAST_CALLABLE_JS_FUNCTION_TYPE);
  pop(object);
  Check(le, AbortReason::kOperandIsNotACallableFunction);
}

void MacroAssembler::AssertBoundFunction(Register object) {
  if (v8_flags.debug_code) {
    static_assert(kSmiTag == 0);
    TestIfSmi(object);
    Check(ne, AbortReason::kOperandIsASmiAndNotABoundFunction, cr0);
    push(object);
    IsObjectType(object, object, object, JS_BOUND_FUNCTION_TYPE);
    pop(object);
    Check(eq, AbortReason::kOperandIsNotABoundFunction);
  }
}

void MacroAssembler::AssertGeneratorObject(Register object) {
  if (!v8_flags.debug_code) return;
  TestIfSmi(object);
  Check(ne, AbortReason::kOperandIsASmiAndNotAGeneratorObject, cr0);

  // Load map
  Register map = object;
  push(object);
  LoadMap(map, object);

  // Check if JSGeneratorObject
  Register scratch = object;
  CompareInstanceTypeRange(map, scratch, scratch,
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
    beq(&done_checking, Label::kNear);
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
  // Up to five simple arguments are passed in registers r2..r6
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
    mov(scratch, sp);
    lay(sp, MemOperand(sp, -(stack_passed_arguments + 1) * kSystemPointerSize));
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    ClearRightImm(sp, sp,
                  Operand(base::bits::WhichPowerOfTwo(frame_alignment)));
    StoreU64(scratch,
             MemOperand(sp, (stack_passed_arguments)*kSystemPointerSize));
  } else {
    stack_space += stack_passed_arguments;
  }
  lay(sp, MemOperand(sp, (-stack_space) * kSystemPointerSize));
}

void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          Register scratch) {
  PrepareCallCFunction(num_reg_arguments, 0, scratch);
}

void MacroAssembler::MovToFloatParameter(DoubleRegister src) { Move(d0, src); }

void MacroAssembler::MovToFloatResult(DoubleRegister src) { Move(d0, src); }

void MacroAssembler::MovToFloatParameters(DoubleRegister src1,
                                          DoubleRegister src2) {
  if (src2 == d0) {
    DCHECK(src1 != d2);
    Move(d2, src2);
    Move(d0, src1);
  } else {
    Move(d0, src1);
    Move(d2, src2);
  }
}

int MacroAssembler::CallCFunction(ExternalReference function,
                                  int num_reg_arguments,
                                  int num_double_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  bool has_function_descriptor,
                                  Label* return_label) {
  Move(ip, function);
  return CallCFunction(ip, num_reg_arguments, num_double_arguments,
                       set_isolate_data_slots, has_function_descriptor,
                       return_label);
}

int MacroAssembler::CallCFunction(Register function, int num_reg_arguments,
                                  int num_double_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  bool has_function_descriptor,
                                  Label* return_label) {
  ASM_CODE_COMMENT(this);
  DCHECK_LE(num_reg_arguments + num_double_arguments, kMaxCParameters);
  DCHECK(has_frame());

#if V8_OS_ZOS
  // Shuffle input arguments
  mov(r1, r2);
  mov(r2, r3);
  mov(r3, r4);

  // XPLINK treats r7 as voliatile return register, but r14 as preserved
  // Since Linux is the other way around, perserve r7 value in r14 across
  // the call.
  mov(r14, r7);

  // XPLINK linkage requires args in r5,r6,r7,r8,r9 to be passed on the stack.
  // However, for DirectAPI C calls, there may not be stack slots
  // for these 4th and 5th parameters if num_reg_arguments are less
  // than 3.  In that case, we need to still preserve r5/r6 into
  // register save area, as they are considered volatile in XPLINK.
  if (num_reg_arguments == 4) {
    StoreU64(r5, MemOperand(sp, 19 * kSystemPointerSize));
    StoreU64(r6, MemOperand(sp, 6 * kSystemPointerSize));
  } else if (num_reg_arguments >= 5) {
    // Save original r5 - r6  to Stack, r7 - r9 already saved to Stack
    StoreMultipleP(r5, r6, MemOperand(sp, 19 * kSystemPointerSize));
  } else {
    StoreMultipleP(r5, r6, MemOperand(sp, 5 * kSystemPointerSize));
  }
#endif

  Label get_pc;

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // Save the frame pointer and PC so that the stack layout remains iterable,
    // even without an ExitFrame which normally exists between JS and C frames.
    // See x64 code for reasoning about how to address the isolate data fields.
    larl(r0, &get_pc);
    CHECK(root_array_available());
    StoreU64(r0,
             ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC));
    StoreU64(fp,
             ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
  }

#if V8_OS_ZOS
  // Set up the system stack pointer with the XPLINK bias.
  lay(r4, MemOperand(sp, -kStackPointerBias));

  Register dest = function;
  if (has_function_descriptor) {
    LoadMultipleP(r5, r6, MemOperand(function));
    dest = r6;
  }
#else
  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
  Register dest = function;
  if (ABI_CALL_VIA_IP) {
    Move(ip, function);
    dest = ip;
  }
#endif

#if V8_OS_ZOS
  if (has_function_descriptor) {
    // Branch to target via indirect branch
    basr(r7, dest);
    nop(BASR_CALL_TYPE_NOP);
  } else {
    basr(r7, dest);
  }

  // Restore r5-r9 from the appropriate stack locations (see notes above).
  if (num_reg_arguments == 4) {
    LoadU64(r5, MemOperand(sp, 19 * kSystemPointerSize));
    LoadU64(r6, MemOperand(sp, 6 * kSystemPointerSize));
  } else if (num_reg_arguments >= 5) {
    LoadMultipleP(r5, r6, MemOperand(sp, 19 * kSystemPointerSize));
  } else {
    LoadMultipleP(r5, r6, MemOperand(sp, 5 * kSystemPointerSize));
  }

  // Restore original r7
  mov(r7, r14);

  // Shuffle the result
  mov(r2, r3);
#else
  Call(dest);
#endif

  int call_pc_offset = pc_offset();
  bind(&get_pc);
  if (return_label) bind(return_label);

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // We don't unset the PC; the FP is the source of truth.
    Register zero_scratch = r0;
    lghi(zero_scratch, Operand::Zero());

    StoreU64(zero_scratch,
             ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
  }

  int stack_passed_arguments =
      CalculateStackPassedWords(num_reg_arguments, num_double_arguments);
  int stack_space = kNumRequiredStackFrameSlots + stack_passed_arguments;
  if (ActivationFrameAlignment() > kSystemPointerSize) {
    // Load the original stack pointer (pre-alignment) from the stack
    LoadU64(sp, MemOperand(sp, stack_space * kSystemPointerSize));
  } else {
    la(sp, MemOperand(sp, stack_space * kSystemPointerSize));
  }

  return call_pc_offset;
}

int MacroAssembler::CallCFunction(ExternalReference function, int num_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  bool has_function_descriptor,
                                  Label* return_label) {
  return CallCFunction(function, num_arguments, 0, set_isolate_data_slots,
                       has_function_descriptor, return_label);
}

int MacroAssembler::CallCFunction(Register function, int num_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  bool has_function_descriptor,
                                  Label* return_label) {
  return CallCFunction(function, num_arguments, 0, set_isolate_data_slots,
                       has_function_descriptor, return_label);
}

void MacroAssembler::CheckPageFlag(
    Register object,
    Register scratch,  // scratch may be same register as object
    int mask, Condition cc, Label* condition_met) {
  DCHECK(cc == ne || cc == eq);
  ClearRightImm(scratch, object, Operand(kPageSizeBits));

  if (base::bits::IsPowerOfTwo(mask)) {
    // If it's a power of two, we can use Test-Under-Mask Memory-Imm form
    // which allows testing of a single byte in memory.
    int32_t byte_offset = 4;
    uint32_t shifted_mask = mask;
    // Determine the byte offset to be tested
    if (mask <= 0x80) {
      byte_offset = kSystemPointerSize - 1;
    } else if (mask < 0x8000) {
      byte_offset = kSystemPointerSize - 2;
      shifted_mask = mask >> 8;
    } else if (mask < 0x800000) {
      byte_offset = kSystemPointerSize - 3;
      shifted_mask = mask >> 16;
    } else {
      byte_offset = kSystemPointerSize - 4;
      shifted_mask = mask >> 24;
    }
#if V8_TARGET_LITTLE_ENDIAN
    // Reverse the byte_offset if emulating on little endian platform
    byte_offset = kSystemPointerSize - byte_offset - 1;
#endif
    tm(MemOperand(scratch, MemoryChunkLayout::kFlagsOffset + byte_offset),
       Operand(shifted_mask));
  } else {
    LoadU64(scratch, MemOperand(scratch, MemoryChunkLayout::kFlagsOffset));
    AndP(r0, scratch, Operand(mask));
  }
  // Should be okay to remove rc

  if (cc == ne) {
    bne(condition_met);
  }
  if (cc == eq) {
    beq(condition_met);
  }
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

void MacroAssembler::mov(Register dst, Register src) { lgr(dst, src); }

void MacroAssembler::mov(Register dst, const Operand& src) {
  int64_t value = 0;

  if (src.is_heap_number_request()) {
    RequestHeapNumber(src.heap_number_request());
  } else {
    value = src.immediate();
  }

  if (src.rmode() != RelocInfo::NO_INFO) {
    // some form of relocation needed
    RecordRelocInfo(src.rmode(), value);
  }

  int32_t hi_32 = static_cast<int32_t>(value >> 32);
  int32_t lo_32 = static_cast<int32_t>(value);

  if (src.rmode() == RelocInfo::NO_INFO) {
    if (hi_32 == 0) {
      if (is_uint16(lo_32)) {
        llill(dst, Operand(lo_32));
        return;
      }
      llilf(dst, Operand(lo_32));
      return;
    } else if (lo_32 == 0) {
      if (is_uint16(hi_32)) {
        llihl(dst, Operand(hi_32));
        return;
      }
      llihf(dst, Operand(hi_32));
      return;
    } else if (is_int16(value)) {
      lghi(dst, Operand(value));
      return;
    } else if (is_int32(value)) {
      lgfi(dst, Operand(value));
      return;
    }
  } else if (src.rmode() == RelocInfo::WASM_CANONICAL_SIG_ID) {
    CHECK(is_int32(value));
    // If this is changed then also change `uint32_constant_at` and
    // `set_uint32_constant_at`.
    lgfi(dst, Operand(value));
    return;
  }

  iihf(dst, Operand(hi_32));
  iilf(dst, Operand(lo_32));
}

void MacroAssembler::MulS32(Register dst, const MemOperand& src1) {
  if (is_uint12(src1.offset())) {
    ms(dst, src1);
  } else if (is_int20(src1.offset())) {
    msy(dst, src1);
  } else {
    UNIMPLEMENTED();
  }
}

void MacroAssembler::MulS32(Register dst, Register src1) { msr(dst, src1); }

void MacroAssembler::MulS32(Register dst, const Operand& src1) {
  msfi(dst, src1);
}

#define Generate_MulHigh32(instr) \
  {                               \
    lgfr(dst, src1);              \
    instr(dst, src2);             \
    srlg(dst, dst, Operand(32));  \
  }

void MacroAssembler::MulHighS32(Register dst, Register src1,
                                const MemOperand& src2) {
  Generate_MulHigh32(msgf);
}

void MacroAssembler::MulHighS32(Register dst, Register src1, Register src2) {
  if (dst == src2) {
    std::swap(src1, src2);
  }
  Generate_MulHigh32(msgfr);
}

void MacroAssembler::MulHighS32(Register dst, Register src1,
                                const Operand& src2) {
  Generate_MulHigh32(msgfi);
}

#undef Generate_MulHigh32

#define Generate_MulHighU32(instr) \
  {                                \
    lr(r1, src1);                  \
    instr(r0, src2);               \
    LoadU32(dst, r0);              \
  }

void MacroAssembler::MulHighU32(Register dst, Register src1,
                                const MemOperand& src2) {
  Generate_MulHighU32(ml);
}

void MacroAssembler::MulHighU32(Register dst, Register src1, Register src2) {
  Generate_MulHighU32(mlr);
}

void MacroAssembler::MulHighU32(Register dst, Register src1,
                                const Operand& src2) {
  USE(dst);
  USE(src1);
  USE(src2);
  UNREACHABLE();
}

#undef Generate_MulHighU32

#define Generate_Mul32WithOverflowIfCCUnequal(instr) \
  {                                                  \
    lgfr(dst, src1);                                 \
    instr(dst, src2);                                \
    cgfr(dst, dst);                                  \
  }

void MacroAssembler::Mul32WithOverflowIfCCUnequal(Register dst, Register src1,
                                                  const MemOperand& src2) {
  Register result = dst;
  if (src2.rx() == dst || src2.rb() == dst) dst = r0;
  Generate_Mul32WithOverflowIfCCUnequal(msgf);
  if (result != dst) llgfr(result, dst);
}

void MacroAssembler::Mul32WithOverflowIfCCUnequal(Register dst, Register src1,
                                                  Register src2) {
  if (dst == src2) {
    std::swap(src1, src2);
  }
  Generate_Mul32WithOverflowIfCCUnequal(msgfr);
}

void MacroAssembler::Mul32WithOverflowIfCCUnequal(Register dst, Register src1,
                                                  const Operand& src2) {
  Generate_Mul32WithOverflowIfCCUnequal(msgfi);
}

#undef Generate_Mul32WithOverflowIfCCUnequal

#define Generate_Div32(instr) \
  {                           \
    lgfr(r1, src1);           \
    instr(r0, src2);          \
    LoadU32(dst, r1);         \
  }

void MacroAssembler::DivS32(Register dst, Register src1,
                            const MemOperand& src2) {
  Generate_Div32(dsgf);
}

void MacroAssembler::DivS32(Register dst, Register src1, Register src2) {
  Generate_Div32(dsgfr);
}

#undef Generate_Div32

#define Generate_DivU32(instr) \
  {                            \
    lr(r0, src1);              \
    srdl(r0, Operand(32));     \
    instr(r0, src2);           \
    LoadU32(dst, r1);          \
  }

void MacroAssembler::DivU32(Register dst, Register src1,
                            const MemOperand& src2) {
  Generate_DivU32(dl);
}

void MacroAssembler::DivU32(Register dst, Register src1, Register src2) {
  Generate_DivU32(dlr);
}

#undef Generate_DivU32

#define Generate_Div64(instr) \
  {                           \
    lgr(r1, src1);            \
    instr(r0, src2);          \
    lgr(dst, r1);             \
  }

void MacroAssembler::DivS64(Register dst, Register src1,
                            const MemOperand& src2) {
  Generate_Div64(dsg);
}

void MacroAssembler::DivS64(Register dst, Register src1, Register src2) {
  Generate_Div64(dsgr);
}

#undef Generate_Div64

#define Generate_DivU64(instr) \
  {                            \
    lgr(r1, src1);             \
    lghi(r0, Operand::Zero()); \
    instr(r0, src2);           \
    lgr(dst, r1);              \
  }

void MacroAssembler::DivU64(Register dst, Register src1,
                            const MemOperand& src2) {
  Generate_DivU64(dlg);
}

void MacroAssembler::DivU64(Register dst, Register src1, Register src2) {
  Generate_DivU64(dlgr);
}

#undef Generate_DivU64

#define Generate_Mod32(instr) \
  {                           \
    lgfr(r1, src1);           \
    instr(r0, src2);          \
    LoadU32(dst, r0);         \
  }

void MacroAssembler::ModS32(Register dst, Register src1,
                            const MemOperand& src2) {
  Generate_Mod32(dsgf);
}

void MacroAssembler::ModS32(Register dst, Register src1, Register src2) {
  Generate_Mod32(dsgfr);
}

#undef Generate_Mod32

#define Generate_ModU32(instr) \
  {                            \
    lr(r0, src1);              \
    srdl(r0, Operand(32));     \
    instr(r0, src2);           \
    LoadU32(dst, r0);          \
  }

void MacroAssembler::ModU32(Register dst, Register src1,
                            const MemOperand& src2) {
  Generate_ModU32(dl);
}

void MacroAssembler::ModU32(Register dst, Register src1, Register src2) {
  Generate_ModU32(dlr);
}

#undef Generate_ModU32

#define Generate_Mod64(instr) \
  {                           \
    lgr(r1, src1);            \
    instr(r0, src2);          \
    lgr(dst, r0);             \
  }

void MacroAssembler::ModS64(Register dst, Register src1,
                            const MemOperand& src2) {
  Generate_Mod64(dsg);
}

void MacroAssembler::ModS64(Register dst, Register src1, Register src2) {
  Generate_Mod64(dsgr);
}

#undef Generate_Mod64

#define Generate_ModU64(instr) \
  {                            \
    lgr(r1, src1);             \
    lghi(r0, Operand::Zero()); \
    instr(r0, src2);           \
    lgr(dst, r0);              \
  }

void MacroAssembler::ModU64(Register dst, Register src1,
                            const MemOperand& src2) {
  Generate_ModU64(dlg);
}

void MacroAssembler::ModU64(Register dst, Register src1, Register src2) {
  Generate_ModU64(dlgr);
}

#undef Generate_ModU64

void MacroAssembler::MulS64(Register dst, const Operand& opnd) {
  msgfi(dst, opnd);
}

void MacroAssembler::MulS64(Register dst, Register src) { msgr(dst, src); }

void MacroAssembler::MulS64(Register dst, const MemOperand& opnd) {
  msg(dst, opnd);
}

void MacroAssembler::MulHighS64(Register dst, Register src1, Register src2) {
  if (CpuFeatures::IsSupported(MISC_INSTR_EXT2)) {
    mgrk(r0, src1, src2);
    lgr(dst, r0);
  } else {
    SaveFPRegsMode fp_mode = SaveFPRegsMode::kSave;
    PushCallerSaved(fp_mode, ip);
    Push(src1, src2);
    Pop(r2, r3);
    {
      FrameScope scope(this, StackFrame::INTERNAL);
      PrepareCallCFunction(2, 0, r0);
      CallCFunction(ExternalReference::int64_mul_high_function(), 2, 0);
    }
    mov(r0, r2);
    PopCallerSaved(fp_mode, ip);
    mov(dst, r0);
  }
}

void MacroAssembler::MulHighS64(Register dst, Register src1,
                                const MemOperand& src2) {
  // TODO(v8): implement this.
  UNIMPLEMENTED();
}

void MacroAssembler::MulHighU64(Register dst, Register src1, Register src2) {
  lgr(r1, src1);
  mlgr(r0, src2);
  lgr(dst, r0);
}

void MacroAssembler::MulHighU64(Register dst, Register src1,
                                const MemOperand& src2) {
  // TODO(v8): implement this.
  UNIMPLEMENTED();
}

void MacroAssembler::Sqrt(DoubleRegister result, DoubleRegister input) {
  sqdbr(result, input);
}
void MacroAssembler::Sqrt(DoubleRegister result, const MemOperand& input) {
  if (is_uint12(input.offset())) {
    sqdb(result, input);
  } else {
    ldy(result, input);
    sqdbr(result, result);
  }
}
//----------------------------------------------------------------------------
//  Add Instructions
//----------------------------------------------------------------------------

// Add 32-bit (Register dst = Register dst + Immediate opnd)
void MacroAssembler::AddS32(Register dst, const Operand& opnd) {
  if (is_int16(opnd.immediate()))
    ahi(dst, opnd);
  else
    afi(dst, opnd);
}

// Add Pointer Size (Register dst = Register dst + Immediate opnd)
void MacroAssembler::AddS64(Register dst, const Operand& opnd) {
  if (is_int16(opnd.immediate()))
    aghi(dst, opnd);
  else
    agfi(dst, opnd);
}

void MacroAssembler::AddS32(Register dst, Register src, int32_t opnd) {
  AddS32(dst, src, Operand(opnd));
}

// Add 32-bit (Register dst = Register src + Immediate opnd)
void MacroAssembler::AddS32(Register dst, Register src, const Operand& opnd) {
  if (dst != src) {
    if (CpuFeatures::IsSupported(DISTINCT_OPS) && is_int16(opnd.immediate())) {
      ahik(dst, src, opnd);
      return;
    }
    lr(dst, src);
  }
  AddS32(dst, opnd);
}

void MacroAssembler::AddS64(Register dst, Register src, int32_t opnd) {
  AddS64(dst, src, Operand(opnd));
}

// Add Pointer Size (Register dst = Register src + Immediate opnd)
void MacroAssembler::AddS64(Register dst, Register src, const Operand& opnd) {
  if (dst != src) {
    if (CpuFeatures::IsSupported(DISTINCT_OPS) && is_int16(opnd.immediate())) {
      aghik(dst, src, opnd);
      return;
    }
    mov(dst, src);
  }
  AddS64(dst, opnd);
}

// Add 32-bit (Register dst = Register dst + Register src)
void MacroAssembler::AddS32(Register dst, Register src) { ar(dst, src); }

// Add Pointer Size (Register dst = Register dst + Register src)
void MacroAssembler::AddS64(Register dst, Register src) { agr(dst, src); }

// Add 32-bit (Register dst = Register src1 + Register src2)
void MacroAssembler::AddS32(Register dst, Register src1, Register src2) {
  if (dst != src1 && dst != src2) {
    // We prefer to generate AR/AGR, over the non clobbering ARK/AGRK
    // as AR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      ark(dst, src1, src2);
      return;
    } else {
      lr(dst, src1);
    }
  } else if (dst == src2) {
    src2 = src1;
  }
  ar(dst, src2);
}

// Add Pointer Size (Register dst = Register src1 + Register src2)
void MacroAssembler::AddS64(Register dst, Register src1, Register src2) {
  if (dst != src1 && dst != src2) {
    // We prefer to generate AR/AGR, over the non clobbering ARK/AGRK
    // as AR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      agrk(dst, src1, src2);
      return;
    } else {
      mov(dst, src1);
    }
  } else if (dst == src2) {
    src2 = src1;
  }
  agr(dst, src2);
}

// Add 32-bit (Register-Memory)
void MacroAssembler::AddS32(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    a(dst, opnd);
  else
    ay(dst, opnd);
}

// Add Pointer Size (Register-Memory)
void MacroAssembler::AddS64(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  ag(dst, opnd);
}

// Add 32-bit (Memory - Immediate)
void MacroAssembler::AddS32(const MemOperand& opnd, const Operand& imm) {
  DCHECK(is_int8(imm.immediate()));
  DCHECK(is_int20(opnd.offset()));
  DCHECK(CpuFeatures::IsSupported(GENERAL_INSTR_EXT));
  asi(opnd, imm);
}

// Add Pointer-sized (Memory - Immediate)
void MacroAssembler::AddS64(const MemOperand& opnd, const Operand& imm) {
  DCHECK(is_int8(imm.immediate()));
  DCHECK(is_int20(opnd.offset()));
  DCHECK(CpuFeatures::IsSupported(GENERAL_INSTR_EXT));
  agsi(opnd, imm);
}

//----------------------------------------------------------------------------
//  Add Logical Instructions
//----------------------------------------------------------------------------

// Add Logical 32-bit (Register dst = Register src1 + Register src2)
void MacroAssembler::AddU32(Register dst, Register src1, Register src2) {
  if (dst != src2 && dst != src1) {
    lr(dst, src1);
    alr(dst, src2);
  } else if (dst != src2) {
    // dst == src1
    DCHECK(dst == src1);
    alr(dst, src2);
  } else {
    // dst == src2
    DCHECK(dst == src2);
    alr(dst, src1);
  }
}

// Add Logical 32-bit (Register dst = Register dst + Immediate opnd)
void MacroAssembler::AddU32(Register dst, const Operand& imm) {
  alfi(dst, imm);
}

// Add Logical Pointer Size (Register dst = Register dst + Immediate opnd)
void MacroAssembler::AddU64(Register dst, const Operand& imm) {
  algfi(dst, imm);
}

void MacroAssembler::AddU64(Register dst, Register src1, Register src2) {
  if (dst != src2 && dst != src1) {
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      algrk(dst, src1, src2);
    } else {
      lgr(dst, src1);
      algr(dst, src2);
    }
  } else if (dst != src2) {
    // dst == src1
    DCHECK(dst == src1);
    algr(dst, src2);
  } else {
    // dst == src2
    DCHECK(dst == src2);
    algr(dst, src1);
  }
}

// Add Logical 32-bit (Register-Memory)
void MacroAssembler::AddU32(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    al_z(dst, opnd);
  else
    aly(dst, opnd);
}

// Add Logical Pointer Size (Register-Memory)
void MacroAssembler::AddU64(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  alg(dst, opnd);
}

//----------------------------------------------------------------------------
//  Subtract Instructions
//----------------------------------------------------------------------------

// Subtract Logical 32-bit (Register dst = Register src1 - Register src2)
void MacroAssembler::SubU32(Register dst, Register src1, Register src2) {
  if (dst != src2 && dst != src1) {
    lr(dst, src1);
    slr(dst, src2);
  } else if (dst != src2) {
    // dst == src1
    DCHECK(dst == src1);
    slr(dst, src2);
  } else {
    // dst == src2
    DCHECK(dst == src2);
    lr(r0, dst);
    SubU32(dst, src1, r0);
  }
}

// Subtract 32-bit (Register dst = Register dst - Immediate opnd)
void MacroAssembler::SubS32(Register dst, const Operand& imm) {
  AddS32(dst, Operand(-(imm.immediate())));
}

// Subtract Pointer Size (Register dst = Register dst - Immediate opnd)
void MacroAssembler::SubS64(Register dst, const Operand& imm) {
  AddS64(dst, Operand(-(imm.immediate())));
}

void MacroAssembler::SubS32(Register dst, Register src, int32_t imm) {
  SubS32(dst, src, Operand(imm));
}

// Subtract 32-bit (Register dst = Register src - Immediate opnd)
void MacroAssembler::SubS32(Register dst, Register src, const Operand& imm) {
  AddS32(dst, src, Operand(-(imm.immediate())));
}

void MacroAssembler::SubS64(Register dst, Register src, int32_t imm) {
  SubS64(dst, src, Operand(imm));
}

// Subtract Pointer Sized (Register dst = Register src - Immediate opnd)
void MacroAssembler::SubS64(Register dst, Register src, const Operand& imm) {
  AddS64(dst, src, Operand(-(imm.immediate())));
}

// Subtract 32-bit (Register dst = Register dst - Register src)
void MacroAssembler::SubS32(Register dst, Register src) { sr(dst, src); }

// Subtract Pointer Size (Register dst = Register dst - Register src)
void MacroAssembler::SubS64(Register dst, Register src) { sgr(dst, src); }

// Subtract 32-bit (Register = Register - Register)
void MacroAssembler::SubS32(Register dst, Register src1, Register src2) {
  // Use non-clobbering version if possible
  if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    srk(dst, src1, src2);
    return;
  }
  if (dst != src1 && dst != src2) lr(dst, src1);
  // In scenario where we have dst = src - dst, we need to swap and negate
  if (dst != src1 && dst == src2) {
    Label done;
    lcr(dst, dst);  // dst = -dst
    b(overflow, &done);
    ar(dst, src1);  // dst = dst + src
    bind(&done);
  } else {
    sr(dst, src2);
  }
}

// Subtract Pointer Sized (Register = Register - Register)
void MacroAssembler::SubS64(Register dst, Register src1, Register src2) {
  // Use non-clobbering version if possible
  if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    sgrk(dst, src1, src2);
    return;
  }
  if (dst != src1 && dst != src2) mov(dst, src1);
  // In scenario where we have dst = src - dst, we need to swap and negate
  if (dst != src1 && dst == src2) {
    Label done;
    lcgr(dst, dst);  // dst = -dst
    b(overflow, &done);
    AddS64(dst, src1);  // dst = dst + src
    bind(&done);
  } else {
    SubS64(dst, src2);
  }
}

// Subtract 32-bit (Register-Memory)
void MacroAssembler::SubS32(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    s(dst, opnd);
  else
    sy(dst, opnd);
}

// Subtract Pointer Sized (Register - Memory)
void MacroAssembler::SubS64(Register dst, const MemOperand& opnd) {
#if V8_TARGET_ARCH_S390X
  sg(dst, opnd);
#else
  SubS32(dst, opnd);
#endif
}

void MacroAssembler::MovIntToFloat(DoubleRegister dst, Register src) {
  sllg(r0, src, Operand(32));
  ldgr(dst, r0);
}

void MacroAssembler::MovFloatToInt(Register dst, DoubleRegister src) {
  lgdr(dst, src);
  srlg(dst, dst, Operand(32));
}

// Load And Subtract 32-bit (similar to laa/lan/lao/lax)
void MacroAssembler::LoadAndSub32(Register dst, Register src,
                                  const MemOperand& opnd) {
  lcr(dst, src);
  laa(dst, dst, opnd);
}

void MacroAssembler::LoadAndSub64(Register dst, Register src,
                                  const MemOperand& opnd) {
  lcgr(dst, src);
  laag(dst, dst, opnd);
}

//----------------------------------------------------------------------------
//  Subtract Logical Instructions
//----------------------------------------------------------------------------

// Subtract Logical 32-bit (Register - Memory)
void MacroAssembler::SubU32(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    sl(dst, opnd);
  else
    sly(dst, opnd);
}

// Subtract Logical Pointer Sized (Register - Memory)
void MacroAssembler::SubU64(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  slgf(dst, opnd);
#else
  SubU32(dst, opnd);
#endif
}

//----------------------------------------------------------------------------
//  Bitwise Operations
//----------------------------------------------------------------------------

// AND 32-bit - dst = dst & src
void MacroAssembler::And(Register dst, Register src) { nr(dst, src); }

// AND Pointer Size - dst = dst & src
void MacroAssembler::AndP(Register dst, Register src) { ngr(dst, src); }

// Non-clobbering AND 32-bit - dst = src1 & src1
void MacroAssembler::And(Register dst, Register src1, Register src2) {
  if (dst != src1 && dst != src2) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      nrk(dst, src1, src2);
      return;
    } else {
      lr(dst, src1);
    }
  } else if (dst == src2) {
    src2 = src1;
  }
  And(dst, src2);
}

// Non-clobbering AND pointer size - dst = src1 & src1
void MacroAssembler::AndP(Register dst, Register src1, Register src2) {
  if (dst != src1 && dst != src2) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      ngrk(dst, src1, src2);
      return;
    } else {
      mov(dst, src1);
    }
  } else if (dst == src2) {
    src2 = src1;
  }
  AndP(dst, src2);
}

// AND 32-bit (Reg - Mem)
void MacroAssembler::And(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    n(dst, opnd);
  else
    ny(dst, opnd);
}

// AND Pointer Size (Reg - Mem)
void MacroAssembler::AndP(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  ng(dst, opnd);
#else
  And(dst, opnd);
#endif
}

// AND 32-bit - dst = dst & imm
void MacroAssembler::And(Register dst, const Operand& opnd) { nilf(dst, opnd); }

// AND Pointer Size - dst = dst & imm
void MacroAssembler::AndP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  intptr_t value = opnd.immediate();
  if (value >> 32 != -1) {
    // this may not work b/c condition code won't be set correctly
    nihf(dst, Operand(value >> 32));
  }
  nilf(dst, Operand(value & 0xFFFFFFFF));
#else
  And(dst, opnd);
#endif
}

// AND 32-bit - dst = src & imm
void MacroAssembler::And(Register dst, Register src, const Operand& opnd) {
  if (dst != src) lr(dst, src);
  nilf(dst, opnd);
}

// AND Pointer Size - dst = src & imm
void MacroAssembler::AndP(Register dst, Register src, const Operand& opnd) {
  // Try to exploit RISBG first
  intptr_t value = opnd.immediate();
  if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT)) {
    intptr_t shifted_value = value;
    int trailing_zeros = 0;

    // We start checking how many trailing zeros are left at the end.
    while ((0 != shifted_value) && (0 == (shifted_value & 1))) {
      trailing_zeros++;
      shifted_value >>= 1;
    }

    // If temp (value with right-most set of zeros shifted out) is 1 less
    // than power of 2, we have consecutive bits of 1.
    // Special case: If shift_value is zero, we cannot use RISBG, as it requires
    //               selection of at least 1 bit.
    if ((0 != shifted_value) && base::bits::IsPowerOfTwo(shifted_value + 1)) {
      int startBit =
          base::bits::CountLeadingZeros64(shifted_value) - trailing_zeros;
      int endBit = 63 - trailing_zeros;
      // Start: startBit, End: endBit, Shift = 0, true = zero unselected bits.
      RotateInsertSelectBits(dst, src, Operand(startBit), Operand(endBit),
                             Operand::Zero(), true);
      return;
    } else if (-1 == shifted_value) {
      // A Special case in which all top bits up to MSB are 1's.  In this case,
      // we can set startBit to be 0.
      int endBit = 63 - trailing_zeros;
      RotateInsertSelectBits(dst, src, Operand::Zero(), Operand(endBit),
                             Operand::Zero(), true);
      return;
    }
  }

  // If we are &'ing zero, we can just whack the dst register and skip copy
  if (dst != src && (0 != value)) mov(dst, src);
  AndP(dst, opnd);
}

// OR 32-bit - dst = dst & src
void MacroAssembler::Or(Register dst, Register src) { or_z(dst, src); }

// OR Pointer Size - dst = dst & src
void MacroAssembler::OrP(Register dst, Register src) { ogr(dst, src); }

// Non-clobbering OR 32-bit - dst = src1 & src1
void MacroAssembler::Or(Register dst, Register src1, Register src2) {
  if (dst != src1 && dst != src2) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      ork(dst, src1, src2);
      return;
    } else {
      lr(dst, src1);
    }
  } else if (dst == src2) {
    src2 = src1;
  }
  Or(dst, src2);
}

// Non-clobbering OR pointer size - dst = src1 & src1
void MacroAssembler::OrP(Register dst, Register src1, Register src2) {
  if (dst != src1 && dst != src2) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      ogrk(dst, src1, src2);
      return;
    } else {
      mov(dst, src1);
    }
  } else if (dst == src2) {
    src2 = src1;
  }
  OrP(dst, src2);
}

// OR 32-bit (Reg - Mem)
void MacroAssembler::Or(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    o(dst, opnd);
  else
    oy(dst, opnd);
}

// OR Pointer Size (Reg - Mem)
void MacroAssembler::OrP(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  og(dst, opnd);
#else
  Or(dst, opnd);
#endif
}

// OR 32-bit - dst = dst & imm
void MacroAssembler::Or(Register dst, const Operand& opnd) { oilf(dst, opnd); }

// OR Pointer Size - dst = dst & imm
void MacroAssembler::OrP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  intptr_t value = opnd.immediate();
  if (value >> 32 != 0) {
    // this may not work b/c condition code won't be set correctly
    oihf(dst, Operand(value >> 32));
  }
  oilf(dst, Operand(value & 0xFFFFFFFF));
#else
  Or(dst, opnd);
#endif
}

// OR 32-bit - dst = src & imm
void MacroAssembler::Or(Register dst, Register src, const Operand& opnd) {
  if (dst != src) lr(dst, src);
  oilf(dst, opnd);
}

// OR Pointer Size - dst = src & imm
void MacroAssembler::OrP(Register dst, Register src, const Operand& opnd) {
  if (dst != src) mov(dst, src);
  OrP(dst, opnd);
}

// XOR 32-bit - dst = dst & src
void MacroAssembler::Xor(Register dst, Register src) { xr(dst, src); }

// XOR Pointer Size - dst = dst & src
void MacroAssembler::XorP(Register dst, Register src) { xgr(dst, src); }

// Non-clobbering XOR 32-bit - dst = src1 & src1
void MacroAssembler::Xor(Register dst, Register src1, Register src2) {
  if (dst != src1 && dst != src2) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      xrk(dst, src1, src2);
      return;
    } else {
      lr(dst, src1);
    }
  } else if (dst == src2) {
    src2 = src1;
  }
  Xor(dst, src2);
}

// Non-clobbering XOR pointer size - dst = src1 & src1
void MacroAssembler::XorP(Register dst, Register src1, Register src2) {
  if (dst != src1 && dst != src2) {
    // We prefer to generate XR/XGR, over the non clobbering XRK/XRK
    // as XR is a smaller instruction
    if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
      xgrk(dst, src1, src2);
      return;
    } else {
      mov(dst, src1);
    }
  } else if (dst == src2) {
    src2 = src1;
  }
  XorP(dst, src2);
}

// XOR 32-bit (Reg - Mem)
void MacroAssembler::Xor(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    x(dst, opnd);
  else
    xy(dst, opnd);
}

// XOR Pointer Size (Reg - Mem)
void MacroAssembler::XorP(Register dst, const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  xg(dst, opnd);
#else
  Xor(dst, opnd);
#endif
}

// XOR 32-bit - dst = dst & imm
void MacroAssembler::Xor(Register dst, const Operand& opnd) { xilf(dst, opnd); }

// XOR Pointer Size - dst = dst & imm
void MacroAssembler::XorP(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  intptr_t value = opnd.immediate();
  xihf(dst, Operand(value >> 32));
  xilf(dst, Operand(value & 0xFFFFFFFF));
#else
  Xor(dst, opnd);
#endif
}

// XOR 32-bit - dst = src & imm
void MacroAssembler::Xor(Register dst, Register src, const Operand& opnd) {
  if (dst != src) lr(dst, src);
  xilf(dst, opnd);
}

// XOR Pointer Size - dst = src & imm
void MacroAssembler::XorP(Register dst, Register src, const Operand& opnd) {
  if (dst != src) mov(dst, src);
  XorP(dst, opnd);
}

void MacroAssembler::Not32(Register dst, Register src) {
  if (src != no_reg && src != dst) lr(dst, src);
  xilf(dst, Operand(0xFFFFFFFF));
}

void MacroAssembler::Not64(Register dst, Register src) {
  if (src != no_reg && src != dst) lgr(dst, src);
  xihf(dst, Operand(0xFFFFFFFF));
  xilf(dst, Operand(0xFFFFFFFF));
}

void MacroAssembler::NotP(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  Not64(dst, src);
#else
  Not32(dst, src);
#endif
}

void MacroAssembler::LoadPositiveP(Register result, Register input) {
#if V8_TARGET_ARCH_S390X
  lpgr(result, input);
#else
  lpr(result, input);
#endif
}

void MacroAssembler::LoadPositive32(Register result, Register input) {
  lpr(result, input);
  lgfr(result, result);
}

//-----------------------------------------------------------------------------
//  Compare Helpers
//-----------------------------------------------------------------------------

// Compare 32-bit Register vs Register
void MacroAssembler::CmpS32(Register src1, Register src2) { cr_z(src1, src2); }

// Compare Pointer Sized Register vs Register
void MacroAssembler::CmpS64(Register src1, Register src2) { cgr(src1, src2); }

// Compare 32-bit Register vs Immediate
// This helper will set up proper relocation entries if required.
void MacroAssembler::CmpS32(Register dst, const Operand& opnd) {
  if (opnd.rmode() == RelocInfo::NO_INFO) {
    intptr_t value = opnd.immediate();
    if (is_int16(value))
      chi(dst, opnd);
    else
      cfi(dst, opnd);
  } else {
    // Need to generate relocation record here
    RecordRelocInfo(opnd.rmode(), opnd.immediate());
    cfi(dst, opnd);
  }
}

// Compare Pointer Sized  Register vs Immediate
// This helper will set up proper relocation entries if required.
void MacroAssembler::CmpS64(Register dst, const Operand& opnd) {
  if (opnd.rmode() == RelocInfo::NO_INFO) {
    cgfi(dst, opnd);
  } else {
    mov(r0, opnd);  // Need to generate 64-bit relocation
    cgr(dst, r0);
  }
}

// Compare 32-bit Register vs Memory
void MacroAssembler::CmpS32(Register dst, const MemOperand& opnd) {
  // make sure offset is within 20 bit range
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    c(dst, opnd);
  else
    cy(dst, opnd);
}

// Compare Pointer Size Register vs Memory
void MacroAssembler::CmpS64(Register dst, const MemOperand& opnd) {
  // make sure offset is within 20 bit range
  DCHECK(is_int20(opnd.offset()));
  cg(dst, opnd);
}

void MacroAssembler::CmpF32(DoubleRegister src1, DoubleRegister src2) {
  cebr(src1, src2);
}

void MacroAssembler::CmpF64(DoubleRegister src1, DoubleRegister src2) {
  cdbr(src1, src2);
}

void MacroAssembler::CmpF32(DoubleRegister src1, const MemOperand& src2) {
  DCHECK(is_int12(src2.offset()));
  ceb(src1, src2);
}

void MacroAssembler::CmpF64(DoubleRegister src1, const MemOperand& src2) {
  DCHECK(is_int12(src2.offset()));
  cdb(src1, src2);
}

// Using cs or scy based on the offset
void MacroAssembler::CmpAndSwap(Register old_val, Register new_val,
                                const MemOperand& opnd) {
  if (is_uint12(opnd.offset())) {
    cs(old_val, new_val, opnd);
  } else {
    csy(old_val, new_val, opnd);
  }
}

void MacroAssembler::CmpAndSwap64(Register old_val, Register new_val,
                                  const MemOperand& opnd) {
  DCHECK(is_int20(opnd.offset()));
  csg(old_val, new_val, opnd);
}

//-----------------------------------------------------------------------------
// Compare Logical Helpers
//-----------------------------------------------------------------------------

// Compare Logical 32-bit Register vs Register
void MacroAssembler::CmpU32(Register dst, Register src) { clr(dst, src); }

// Compare Logical Pointer Sized Register vs Register
void MacroAssembler::CmpU64(Register dst, Register src) {
#ifdef V8_TARGET_ARCH_S390X
  clgr(dst, src);
#else
  CmpU32(dst, src);
#endif
}

// Compare Logical 32-bit Register vs Immediate
void MacroAssembler::CmpU32(Register dst, const Operand& opnd) {
  clfi(dst, opnd);
}

// Compare Logical Pointer Sized Register vs Immediate
void MacroAssembler::CmpU64(Register dst, const Operand& opnd) {
#if V8_TARGET_ARCH_S390X
  DCHECK_EQ(static_cast<uint32_t>(opnd.immediate() >> 32), 0);
  clgfi(dst, opnd);
#else
  CmpU32(dst, opnd);
#endif
}

// Compare Logical 32-bit Register vs Memory
void MacroAssembler::CmpU32(Register dst, const MemOperand& opnd) {
  // make sure offset is within 20 bit range
  DCHECK(is_int20(opnd.offset()));
  if (is_uint12(opnd.offset()))
    cl(dst, opnd);
  else
    cly(dst, opnd);
}

// Compare Logical Pointer Sized Register vs Memory
void MacroAssembler::CmpU64(Register dst, const MemOperand& opnd) {
  // make sure offset is within 20 bit range
  DCHECK(is_int20(opnd.offset()));
#if V8_TARGET_ARCH_S390X
  clg(dst, opnd);
#else
  CmpU32(dst, opnd);
#endif
}

void MacroAssembler::Branch(Condition c, const Operand& opnd) {
  intptr_t value = opnd.immediate();
  if (is_int16(value))
    brc(c, opnd);
  else
    brcl(c, opnd);
}

// Branch On Count.  Decrement R1, and branch if R1 != 0.
void MacroAssembler::BranchOnCount(Register r1, Label* l) {
  int32_t offset = branch_offset(l);
  if (is_int16(offset)) {
#if V8_TARGET_ARCH_S390X
    brctg(r1, Operand(offset));
#else
    brct(r1, Operand(offset));
#endif
  } else {
    AddS64(r1, Operand(-1));
    Branch(ne, Operand(offset));
  }
}

void MacroAssembler::LoadSmiLiteral(Register dst, Tagged<Smi> smi) {
  intptr_t value = static_cast<intptr_t>(smi.ptr());
#if defined(V8_COMPRESS_POINTERS) || defined(V8_31BIT_SMIS_ON_64BIT_ARCH)
  llilf(dst, Operand(value));
#else
  DCHECK_EQ(value & 0xFFFFFFFF, 0);
  // The smi value is loaded in upper 32-bits.  Lower 32-bit are zeros.
  llihf(dst, Operand(value >> 32));
#endif
}

void MacroAssembler::CmpSmiLiteral(Register src1, Tagged<Smi> smi,
                                   Register scratch) {
#if defined(V8_COMPRESS_POINTERS) || defined(V8_31BIT_SMIS_ON_64BIT_ARCH)
  // CFI takes 32-bit immediate.
  cfi(src1, Operand(smi));
#else
  if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    cih(src1, Operand(static_cast<intptr_t>(smi.ptr()) >> 32));
  } else {
    LoadSmiLiteral(scratch, smi);
    cgr(src1, scratch);
  }
#endif
}

void MacroAssembler::LoadU64(Register dst, const MemOperand& mem,
                             Register scratch) {
  int offset = mem.offset();

  MemOperand src = mem;
  if (!is_int20(offset)) {
    DCHECK(scratch != no_reg && scratch != r0 && mem.rx() == r0);
    DCHECK(scratch != mem.rb());
    mov(scratch, Operand(offset));
    src = MemOperand(mem.rb(), scratch);
  }
  lg(dst, src);
}

// Store a "pointer" sized value to the memory location
void MacroAssembler::StoreU64(Register src, const MemOperand& mem,
                              Register scratch) {
  if (!is_int20(mem.offset())) {
    DCHECK(scratch != no_reg);
    DCHECK(scratch != r0);
    mov(scratch, Operand(mem.offset()));
    stg(src, MemOperand(mem.rb(), scratch));
  } else {
    stg(src, mem);
  }
}

// Store a "pointer" sized constant to the memory location
void MacroAssembler::StoreU64(const MemOperand& mem, const Operand& opnd,
                              Register scratch) {
  // Relocations not supported
  DCHECK_EQ(opnd.rmode(), RelocInfo::NO_INFO);

  // Try to use MVGHI/MVHI
  if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT) && is_uint12(mem.offset()) &&
      mem.getIndexRegister() == r0 && is_int16(opnd.immediate())) {
    mvghi(mem, opnd);
  } else {
    mov(scratch, opnd);
    StoreU64(scratch, mem);
  }
}

void MacroAssembler::LoadMultipleP(Register dst1, Register dst2,
                                   const MemOperand& mem) {
#if V8_TARGET_ARCH_S390X
  DCHECK(is_int20(mem.offset()));
  lmg(dst1, dst2, mem);
#else
  if (is_uint12(mem.offset())) {
    lm(dst1, dst2, mem);
  } else {
    DCHECK(is_int20(mem.offset()));
    lmy(dst1, dst2, mem);
  }
#endif
}

void MacroAssembler::StoreMultipleP(Register src1, Register src2,
                                    const MemOperand& mem) {
#if V8_TARGET_ARCH_S390X
  DCHECK(is_int20(mem.offset()));
  stmg(src1, src2, mem);
#else
  if (is_uint12(mem.offset())) {
    stm(src1, src2, mem);
  } else {
    DCHECK(is_int20(mem.offset()));
    stmy(src1, src2, mem);
  }
#endif
}

void MacroAssembler::LoadMultipleW(Register dst1, Register dst2,
                                   const MemOperand& mem) {
  if (is_uint12(mem.offset())) {
    lm(dst1, dst2, mem);
  } else {
    DCHECK(is_int20(mem.offset()));
    lmy(dst1, dst2, mem);
  }
}

void MacroAssembler::StoreMultipleW(Register src1, Register src2,
                                    const MemOperand& mem) {
  if (is_uint12(mem.offset())) {
    stm(src1, src2, mem);
  } else {
    DCHECK(is_int20(mem.offset()));
    stmy(src1, src2, mem);
  }
}

// Load 32-bits and sign extend if necessary.
void MacroAssembler::LoadS32(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  lgfr(dst, src);
#else
  if (dst != src) lr(dst, src);
#endif
}

// Load 32-bits and sign extend if necessary.
void MacroAssembler::LoadS32(Register dst, const MemOperand& mem,
                             Register scratch) {
  int offset = mem.offset();

  if (!is_int20(offset)) {
    DCHECK(scratch != no_reg);
    mov(scratch, Operand(offset));
#if V8_TARGET_ARCH_S390X
    lgf(dst, MemOperand(mem.rb(), scratch));
#else
    l(dst, MemOperand(mem.rb(), scratch));
#endif
  } else {
#if V8_TARGET_ARCH_S390X
    lgf(dst, mem);
#else
    if (is_uint12(offset)) {
      l(dst, mem);
    } else {
      ly(dst, mem);
    }
#endif
  }
}

// Load 32-bits and zero extend if necessary.
void MacroAssembler::LoadU32(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  llgfr(dst, src);
#else
  if (dst != src) lr(dst, src);
#endif
}

// Variable length depending on whether offset fits into immediate field
// MemOperand of RX or RXY format
void MacroAssembler::LoadU32(Register dst, const MemOperand& mem,
                             Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

#if V8_TARGET_ARCH_S390X
  if (is_int20(offset)) {
    llgf(dst, mem);
  } else if (scratch != no_reg) {
    // Materialize offset into scratch register.
    mov(scratch, Operand(offset));
    llgf(dst, MemOperand(base, scratch));
  } else {
    DCHECK(false);
  }
#else
  bool use_RXform = false;
  bool use_RXYform = false;
  if (is_uint12(offset)) {
    // RX-format supports unsigned 12-bits offset.
    use_RXform = true;
  } else if (is_int20(offset)) {
    // RXY-format supports signed 20-bits offset.
    use_RXYform = true;
  } else if (scratch != no_reg) {
    // Materialize offset into scratch register.
    mov(scratch, Operand(offset));
  } else {
    DCHECK(false);
  }

  if (use_RXform) {
    l(dst, mem);
  } else if (use_RXYform) {
    ly(dst, mem);
  } else {
    ly(dst, MemOperand(base, scratch));
  }
#endif
}

void MacroAssembler::LoadU16(Register dst, const MemOperand& mem) {
  // TODO(s390x): Add scratch reg
#if V8_TARGET_ARCH_S390X
  llgh(dst, mem);
#else
  llh(dst, mem);
#endif
}

void MacroAssembler::LoadU16(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  llghr(dst, src);
#else
  llhr(dst, src);
#endif
}

void MacroAssembler::LoadS8(Register dst, const MemOperand& mem) {
  // TODO(s390x): Add scratch reg
#if V8_TARGET_ARCH_S390X
  lgb(dst, mem);
#else
  lb(dst, mem);
#endif
}

void MacroAssembler::LoadS8(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  lgbr(dst, src);
#else
  lbr(dst, src);
#endif
}

void MacroAssembler::LoadU8(Register dst, const MemOperand& mem) {
  // TODO(s390x): Add scratch reg
#if V8_TARGET_ARCH_S390X
  llgc(dst, mem);
#else
  llc(dst, mem);
#endif
}

void MacroAssembler::LoadU8(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  llgcr(dst, src);
#else
  llcr(dst, src);
#endif
}

#ifdef V8_TARGET_BIG_ENDIAN
void MacroAssembler::LoadU64LE(Register dst, const MemOperand& mem,
                               Register scratch) {
  lrvg(dst, mem);
}

void MacroAssembler::LoadS32LE(Register dst, const MemOperand& opnd,
                               Register scratch) {
  lrv(dst, opnd);
  LoadS32(dst, dst);
}

void MacroAssembler::LoadU32LE(Register dst, const MemOperand& opnd,
                               Register scratch) {
  lrv(dst, opnd);
  LoadU32(dst, dst);
}

void MacroAssembler::LoadU16LE(Register dst, const MemOperand& opnd) {
  lrvh(dst, opnd);
  LoadU16(dst, dst);
}

void MacroAssembler::LoadS16LE(Register dst, const MemOperand& opnd) {
  lrvh(dst, opnd);
  LoadS16(dst, dst);
}

void MacroAssembler::LoadV128LE(DoubleRegister dst, const MemOperand& opnd,
                                Register scratch0, Register scratch1) {
  bool use_vlbr = CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2) &&
                  is_uint12(opnd.offset());
  if (use_vlbr) {
    vlbr(dst, opnd, Condition(4));
  } else {
    lrvg(scratch0, opnd);
    lrvg(scratch1,
         MemOperand(opnd.rx(), opnd.rb(), opnd.offset() + kSystemPointerSize));
    vlvgp(dst, scratch1, scratch0);
  }
}

void MacroAssembler::LoadF64LE(DoubleRegister dst, const MemOperand& opnd,
                               Register scratch) {
  lrvg(scratch, opnd);
  ldgr(dst, scratch);
}

void MacroAssembler::LoadF32LE(DoubleRegister dst, const MemOperand& opnd,
                               Register scratch) {
  lrv(scratch, opnd);
  ShiftLeftU64(scratch, scratch, Operand(32));
  ldgr(dst, scratch);
}

void MacroAssembler::StoreU64LE(Register src, const MemOperand& mem,
                                Register scratch) {
  if (!is_int20(mem.offset())) {
    DCHECK(scratch != no_reg);
    DCHECK(scratch != r0);
    mov(scratch, Operand(mem.offset()));
    strvg(src, MemOperand(mem.rb(), scratch));
  } else {
    strvg(src, mem);
  }
}

void MacroAssembler::StoreU32LE(Register src, const MemOperand& mem,
                                Register scratch) {
  if (!is_int20(mem.offset())) {
    DCHECK(scratch != no_reg);
    DCHECK(scratch != r0);
    mov(scratch, Operand(mem.offset()));
    strv(src, MemOperand(mem.rb(), scratch));
  } else {
    strv(src, mem);
  }
}

void MacroAssembler::StoreU16LE(Register src, const MemOperand& mem,
                                Register scratch) {
  if (!is_int20(mem.offset())) {
    DCHECK(scratch != no_reg);
    DCHECK(scratch != r0);
    mov(scratch, Operand(mem.offset()));
    strvh(src, MemOperand(mem.rb(), scratch));
  } else {
    strvh(src, mem);
  }
}

void MacroAssembler::StoreF64LE(DoubleRegister src, const MemOperand& opnd,
                                Register scratch) {
  DCHECK(is_uint12(opnd.offset()));
  lgdr(scratch, src);
  strvg(scratch, opnd);
}

void MacroAssembler::StoreF32LE(DoubleRegister src, const MemOperand& opnd,
                                Register scratch) {
  DCHECK(is_uint12(opnd.offset()));
  lgdr(scratch, src);
  ShiftRightU64(scratch, scratch, Operand(32));
  strv(scratch, opnd);
}

void MacroAssembler::StoreV128LE(Simd128Register src, const MemOperand& mem,
                                 Register scratch1, Register scratch2) {
  bool use_vstbr = CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2) &&
                   is_uint12(mem.offset());
  if (use_vstbr) {
    vstbr(src, mem, Condition(4));
  } else {
    vlgv(scratch1, src, MemOperand(r0, 1), Condition(3));
    vlgv(scratch2, src, MemOperand(r0, 0), Condition(3));
    strvg(scratch1, mem);
    strvg(scratch2,
          MemOperand(mem.rx(), mem.rb(), mem.offset() + kSystemPointerSize));
  }
}

#else
void MacroAssembler::LoadU64LE(Register dst, const MemOperand& mem,
                               Register scratch) {
  LoadU64(dst, mem, scratch);
}

void MacroAssembler::LoadS32LE(Register dst, const MemOperand& opnd,
                               Register scratch) {
  LoadS32(dst, opnd, scratch);
}

void MacroAssembler::LoadU32LE(Register dst, const MemOperand& opnd,
                               Register scratch) {
  LoadU32(dst, opnd, scratch);
}

void MacroAssembler::LoadU16LE(Register dst, const MemOperand& opnd) {
  LoadU16(dst, opnd);
}

void MacroAssembler::LoadS16LE(Register dst, const MemOperand& opnd) {
  LoadS16(dst, opnd);
}

void MacroAssembler::LoadV128LE(DoubleRegister dst, const MemOperand& opnd,
                                Register scratch0, Register scratch1) {
  USE(scratch1);
  LoadV128(dst, opnd, scratch0);
}

void MacroAssembler::LoadF64LE(DoubleRegister dst, const MemOperand& opnd,
                               Register scratch) {
  USE(scratch);
  LoadF64(dst, opnd);
}

void MacroAssembler::LoadF32LE(DoubleRegister dst, const MemOperand& opnd,
                               Register scratch) {
  USE(scratch);
  LoadF32(dst, opnd);
}

void MacroAssembler::StoreU64LE(Register src, const MemOperand& mem,
                                Register scratch) {
  StoreU64(src, mem, scratch);
}

void MacroAssembler::StoreU32LE(Register src, const MemOperand& mem,
                                Register scratch) {
  StoreU32(src, mem, scratch);
}

void MacroAssembler::StoreU16LE(Register src, const MemOperand& mem,
                                Register scratch) {
  StoreU16(src, mem, scratch);
}

void MacroAssembler::StoreF64LE(DoubleRegister src, const MemOperand& opnd,
                                Register scratch) {
  StoreF64(src, opnd);
}

void MacroAssembler::StoreF32LE(DoubleRegister src, const MemOperand& opnd,
                                Register scratch) {
  StoreF32(src, opnd);
}

void MacroAssembler::StoreV128LE(Simd128Register src, const MemOperand& mem,
                                 Register scratch1, Register scratch2) {
  StoreV128(src, mem, scratch1);
}

#endif

// Load And Test (Reg <- Reg)
void MacroAssembler::LoadAndTest32(Register dst, Register src) {
  ltr(dst, src);
}

// Load And Test Pointer Sized (Reg <- Reg)
void MacroAssembler::LoadAndTestP(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  ltgr(dst, src);
#else
  ltr(dst, src);
#endif
}

// Load And Test 32-bit (Reg <- Mem)
void MacroAssembler::LoadAndTest32(Register dst, const MemOperand& mem) {
  lt_z(dst, mem);
}

// Load And Test Pointer Sized (Reg <- Mem)
void MacroAssembler::LoadAndTestP(Register dst, const MemOperand& mem) {
#if V8_TARGET_ARCH_S390X
  ltg(dst, mem);
#else
  lt_z(dst, mem);
#endif
}

// Load On Condition Pointer Sized (Reg <- Reg)
void MacroAssembler::LoadOnConditionP(Condition cond, Register dst,
                                      Register src) {
#if V8_TARGET_ARCH_S390X
  locgr(cond, dst, src);
#else
  locr(cond, dst, src);
#endif
}

// Load Double Precision (64-bit) Floating Point number from memory
void MacroAssembler::LoadF64(DoubleRegister dst, const MemOperand& mem) {
  // for 32bit and 64bit we all use 64bit floating point regs
  if (is_uint12(mem.offset())) {
    ld(dst, mem);
  } else {
    ldy(dst, mem);
  }
}

// Load Single Precision (32-bit) Floating Point number from memory
void MacroAssembler::LoadF32(DoubleRegister dst, const MemOperand& mem) {
  if (is_uint12(mem.offset())) {
    le_z(dst, mem);
  } else {
    DCHECK(is_int20(mem.offset()));
    ley(dst, mem);
  }
}

void MacroAssembler::LoadV128(Simd128Register dst, const MemOperand& mem,
                              Register scratch) {
  DCHECK(scratch != r0);
  if (is_uint12(mem.offset())) {
    vl(dst, mem, Condition(0));
  } else {
    DCHECK(is_int20(mem.offset()));
    lay(scratch, mem);
    vl(dst, MemOperand(scratch), Condition(0));
  }
}

// Store Double Precision (64-bit) Floating Point number to memory
void MacroAssembler::StoreF64(DoubleRegister dst, const MemOperand& mem) {
  if (is_uint12(mem.offset())) {
    std(dst, mem);
  } else {
    stdy(dst, mem);
  }
}

// Store Single Precision (32-bit) Floating Point number to memory
void MacroAssembler::StoreF32(DoubleRegister src, const MemOperand& mem) {
  if (is_uint12(mem.offset())) {
    ste(src, mem);
  } else {
    stey(src, mem);
  }
}

void MacroAssembler::StoreV128(Simd128Register src, const MemOperand& mem,
                               Register scratch) {
  DCHECK(scratch != r0);
  if (is_uint12(mem.offset())) {
    vst(src, mem, Condition(0));
  } else {
    DCHECK(is_int20(mem.offset()));
    lay(scratch, mem);
    vst(src, MemOperand(scratch), Condition(0));
  }
}

void MacroAssembler::AddF32(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs) {
  if (dst == lhs) {
    aebr(dst, rhs);
  } else if (dst == rhs) {
    aebr(dst, lhs);
  } else {
    ler(dst, lhs);
    aebr(dst, rhs);
  }
}

void MacroAssembler::SubF32(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs) {
  if (dst == lhs) {
    sebr(dst, rhs);
  } else if (dst == rhs) {
    sebr(dst, lhs);
    lcebr(dst, dst);
  } else {
    ler(dst, lhs);
    sebr(dst, rhs);
  }
}

void MacroAssembler::MulF32(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs) {
  if (dst == lhs) {
    meebr(dst, rhs);
  } else if (dst == rhs) {
    meebr(dst, lhs);
  } else {
    ler(dst, lhs);
    meebr(dst, rhs);
  }
}

void MacroAssembler::DivF32(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs) {
  if (dst == lhs) {
    debr(dst, rhs);
  } else if (dst == rhs) {
    lay(sp, MemOperand(sp, -kSystemPointerSize));
    StoreF32(dst, MemOperand(sp));
    ler(dst, lhs);
    deb(dst, MemOperand(sp));
    la(sp, MemOperand(sp, kSystemPointerSize));
  } else {
    ler(dst, lhs);
    debr(dst, rhs);
  }
}

void MacroAssembler::AddF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs) {
  if (dst == lhs) {
    adbr(dst, rhs);
  } else if (dst == rhs) {
    adbr(dst, lhs);
  } else {
    ldr(dst, lhs);
    adbr(dst, rhs);
  }
}

void MacroAssembler::SubF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs) {
  if (dst == lhs) {
    sdbr(dst, rhs);
  } else if (dst == rhs) {
    sdbr(dst, lhs);
    lcdbr(dst, dst);
  } else {
    ldr(dst, lhs);
    sdbr(dst, rhs);
  }
}

void MacroAssembler::MulF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs) {
  if (dst == lhs) {
    mdbr(dst, rhs);
  } else if (dst == rhs) {
    mdbr(dst, lhs);
  } else {
    ldr(dst, lhs);
    mdbr(dst, rhs);
  }
}

void MacroAssembler::DivF64(DoubleRegister dst, DoubleRegister lhs,
                            DoubleRegister rhs) {
  if (dst == lhs) {
    ddbr(dst, rhs);
  } else if (dst == rhs) {
    lay(sp, MemOperand(sp, -kSystemPointerSize));
    StoreF64(dst, MemOperand(sp));
    ldr(dst, lhs);
    ddb(dst, MemOperand(sp));
    la(sp, MemOperand(sp, kSystemPointerSize));
  } else {
    ldr(dst, lhs);
    ddbr(dst, rhs);
  }
}

void MacroAssembler::AddFloat32(DoubleRegister dst, const MemOperand& opnd,
                                DoubleRegister scratch) {
  if (is_uint12(opnd.offset())) {
    aeb(dst, opnd);
  } else {
    ley(scratch, opnd);
    aebr(dst, scratch);
  }
}

void MacroAssembler::AddFloat64(DoubleRegister dst, const MemOperand& opnd,
                                DoubleRegister scratch) {
  if (is_uint12(opnd.offset())) {
    adb(dst, opnd);
  } else {
    ldy(scratch, opnd);
    adbr(dst, scratch);
  }
}

void MacroAssembler::SubFloat32(DoubleRegister dst, const MemOperand& opnd,
                                DoubleRegister scratch) {
  if (is_uint12(opnd.offset())) {
    seb(dst, opnd);
  } else {
    ley(scratch, opnd);
    sebr(dst, scratch);
  }
}

void MacroAssembler::SubFloat64(DoubleRegister dst, const MemOperand& opnd,
                                DoubleRegister scratch) {
  if (is_uint12(opnd.offset())) {
    sdb(dst, opnd);
  } else {
    ldy(scratch, opnd);
    sdbr(dst, scratch);
  }
}

void MacroAssembler::MulFloat32(DoubleRegister dst, const MemOperand& opnd,
                                DoubleRegister scratch) {
  if (is_uint12(opnd.offset())) {
    meeb(dst, opnd);
  } else {
    ley(scratch, opnd);
    meebr(dst, scratch);
  }
}

void MacroAssembler::MulFloat64(DoubleRegister dst, const MemOperand& opnd,
                                DoubleRegister scratch) {
  if (is_uint12(opnd.offset())) {
    mdb(dst, opnd);
  } else {
    ldy(scratch, opnd);
    mdbr(dst, scratch);
  }
}

void MacroAssembler::DivFloat32(DoubleRegister dst, const MemOperand& opnd,
                                DoubleRegister scratch) {
  if (is_uint12(opnd.offset())) {
    deb(dst, opnd);
  } else {
    ley(scratch, opnd);
    debr(dst, scratch);
  }
}

void MacroAssembler::DivFloat64(DoubleRegister dst, const MemOperand& opnd,
                                DoubleRegister scratch) {
  if (is_uint12(opnd.offset())) {
    ddb(dst, opnd);
  } else {
    ldy(scratch, opnd);
    ddbr(dst, scratch);
  }
}

void MacroAssembler::LoadF32AsF64(DoubleRegister dst, const MemOperand& opnd) {
  if (is_uint12(opnd.offset())) {
    ldeb(dst, opnd);
  } else {
    ley(dst, opnd);
    ldebr(dst, dst);
  }
}

// Variable length depending on whether offset fits into immediate field
// MemOperand of RX or RXY format
void MacroAssembler::StoreU32(Register src, const MemOperand& mem,
                              Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

  bool use_RXform = false;
  bool use_RXYform = false;

  if (is_uint12(offset)) {
    // RX-format supports unsigned 12-bits offset.
    use_RXform = true;
  } else if (is_int20(offset)) {
    // RXY-format supports signed 20-bits offset.
    use_RXYform = true;
  } else if (scratch != no_reg) {
    // Materialize offset into scratch register.
    mov(scratch, Operand(offset));
  } else {
    // scratch is no_reg
    DCHECK(false);
  }

  if (use_RXform) {
    st(src, mem);
  } else if (use_RXYform) {
    sty(src, mem);
  } else {
    StoreU32(src, MemOperand(base, scratch));
  }
}

void MacroAssembler::LoadS16(Register dst, Register src) {
#if V8_TARGET_ARCH_S390X
  lghr(dst, src);
#else
  lhr(dst, src);
#endif
}

// Loads 16-bits half-word value from memory and sign extends to pointer
// sized register
void MacroAssembler::LoadS16(Register dst, const MemOperand& mem,
                             Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

  if (!is_int20(offset)) {
    DCHECK(scratch != no_reg);
    mov(scratch, Operand(offset));
#if V8_TARGET_ARCH_S390X
    lgh(dst, MemOperand(base, scratch));
#else
    lh(dst, MemOperand(base, scratch));
#endif
  } else {
#if V8_TARGET_ARCH_S390X
    lgh(dst, mem);
#else
    if (is_uint12(offset)) {
      lh(dst, mem);
    } else {
      lhy(dst, mem);
    }
#endif
  }
}

// Variable length depending on whether offset fits into immediate field
// MemOperand current only supports d-form
void MacroAssembler::StoreU16(Register src, const MemOperand& mem,
                              Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

  if (is_uint12(offset)) {
    sth(src, mem);
  } else if (is_int20(offset)) {
    sthy(src, mem);
  } else {
    DCHECK(scratch != no_reg);
    mov(scratch, Operand(offset));
    sth(src, MemOperand(base, scratch));
  }
}

// Variable length depending on whether offset fits into immediate field
// MemOperand current only supports d-form
void MacroAssembler::StoreU8(Register src, const MemOperand& mem,
                             Register scratch) {
  Register base = mem.rb();
  int offset = mem.offset();

  if (is_uint12(offset)) {
    stc(src, mem);
  } else if (is_int20(offset)) {
    stcy(src, mem);
  } else {
    DCHECK(scratch != no_reg);
    mov(scratch, Operand(offset));
    stc(src, MemOperand(base, scratch));
  }
}

// Shift left logical for 32-bit integer types.
void MacroAssembler::ShiftLeftU32(Register dst, Register src,
                                  const Operand& val) {
  ShiftLeftU32(dst, src, r0, val);
}

// Shift left logical for 32-bit integer types.
void MacroAssembler::ShiftLeftU32(Register dst, Register src, Register val,
                                  const Operand& val2) {
  if (dst == src) {
    sll(dst, val, val2);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    sllk(dst, src, val, val2);
  } else {
    DCHECK(dst != val || val == r0);  // The lr/sll path clobbers val.
    lr(dst, src);
    sll(dst, val, val2);
  }
}

// Shift left logical for 32-bit integer types.
void MacroAssembler::ShiftLeftU64(Register dst, Register src,
                                  const Operand& val) {
  ShiftLeftU64(dst, src, r0, val);
}

// Shift left logical for 32-bit integer types.
void MacroAssembler::ShiftLeftU64(Register dst, Register src, Register val,
                                  const Operand& val2) {
  sllg(dst, src, val, val2);
}

// Shift right logical for 32-bit integer types.
void MacroAssembler::ShiftRightU32(Register dst, Register src,
                                   const Operand& val) {
  ShiftRightU32(dst, src, r0, val);
}

// Shift right logical for 32-bit integer types.
void MacroAssembler::ShiftRightU32(Register dst, Register src, Register val,
                                   const Operand& val2) {
  if (dst == src) {
    srl(dst, val, val2);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    srlk(dst, src, val, val2);
  } else {
    DCHECK(dst != val || val == r0);  // The lr/srl path clobbers val.
    lr(dst, src);
    srl(dst, val, val2);
  }
}

void MacroAssembler::ShiftRightU64(Register dst, Register src, Register val,
                                   const Operand& val2) {
  srlg(dst, src, val, val2);
}

// Shift right logical for 64-bit integer types.
void MacroAssembler::ShiftRightU64(Register dst, Register src,
                                   const Operand& val) {
  ShiftRightU64(dst, src, r0, val);
}

// Shift right arithmetic for 32-bit integer types.
void MacroAssembler::ShiftRightS32(Register dst, Register src,
                                   const Operand& val) {
  ShiftRightS32(dst, src, r0, val);
}

// Shift right arithmetic for 32-bit integer types.
void MacroAssembler::ShiftRightS32(Register dst, Register src, Register val,
                                   const Operand& val2) {
  if (dst == src) {
    sra(dst, val, val2);
  } else if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
    srak(dst, src, val, val2);
  } else {
    DCHECK(dst != val || val == r0);  // The lr/sra path clobbers val.
    lr(dst, src);
    sra(dst, val, val2);
  }
}

// Shift right arithmetic for 64-bit integer types.
void MacroAssembler::ShiftRightS64(Register dst, Register src,
                                   const Operand& val) {
  ShiftRightS64(dst, src, r0, val);
}

// Shift right arithmetic for 64-bit integer types.
void MacroAssembler::ShiftRightS64(Register dst, Register src, Register val,
                                   const Operand& val2) {
  srag(dst, src, val, val2);
}

// Clear right most # of bits
void MacroAssembler::ClearRightImm(Register dst, Register src,
                                   const Operand& val) {
  int numBitsToClear = val.immediate() % (kSystemPointerSize * 8);

  // Try to use RISBG if possible
  if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT)) {
    int endBit = 63 - numBitsToClear;
    RotateInsertSelectBits(dst, src, Operand::Zero(), Operand(endBit),
                           Operand::Zero(), true);
    return;
  }

  uint64_t hexMask = ~((1L << numBitsToClear) - 1);

  // S390 AND instr clobbers source.  Make a copy if necessary
  if (dst != src) mov(dst, src);

  if (numBitsToClear <= 16) {
    nill(dst, Operand(static_cast<uint16_t>(hexMask)));
  } else if (numBitsToClear <= 32) {
    nilf(dst, Operand(static_cast<uint32_t>(hexMask)));
  } else if (numBitsToClear <= 64) {
    nilf(dst, Operand(static_cast<intptr_t>(0)));
    nihf(dst, Operand(hexMask >> 32));
  }
}

void MacroAssembler::Popcnt32(Register dst, Register src) {
  DCHECK(src != r0);
  DCHECK(dst != r0);

  popcnt(dst, src);
  ShiftRightU32(r0, dst, Operand(16));
  ar(dst, r0);
  ShiftRightU32(r0, dst, Operand(8));
  ar(dst, r0);
  llgcr(dst, dst);
}

#ifdef V8_TARGET_ARCH_S390X
void MacroAssembler::Popcnt64(Register dst, Register src) {
  DCHECK(src != r0);
  DCHECK(dst != r0);

  popcnt(dst, src);
  ShiftRightU64(r0, dst, Operand(32));
  AddS64(dst, r0);
  ShiftRightU64(r0, dst, Operand(16));
  AddS64(dst, r0);
  ShiftRightU64(r0, dst, Operand(8));
  AddS64(dst, r0);
  LoadU8(dst, dst);
}
#endif

void MacroAssembler::SwapP(Register src, Register dst, Register scratch) {
  if (src == dst) return;
  DCHECK(!AreAliased(src, dst, scratch));
  mov(scratch, src);
  mov(src, dst);
  mov(dst, scratch);
}

void MacroAssembler::SwapP(Register src, MemOperand dst, Register scratch) {
  if (dst.rx() != r0) DCHECK(!AreAliased(src, dst.rx(), scratch));
  if (dst.rb() != r0) DCHECK(!AreAliased(src, dst.rb(), scratch));
  DCHECK(!AreAliased(src, scratch));
  mov(scratch, src);
  LoadU64(src, dst);
  StoreU64(scratch, dst);
}

void MacroAssembler::SwapP(MemOperand src, MemOperand dst, Register scratch_0,
                           Register scratch_1) {
  if (src.rx() != r0) DCHECK(!AreAliased(src.rx(), scratch_0, scratch_1));
  if (src.rb() != r0) DCHECK(!AreAliased(src.rb(), scratch_0, scratch_1));
  if (dst.rx() != r0) DCHECK(!AreAliased(dst.rx(), scratch_0, scratch_1));
  if (dst.rb() != r0) DCHECK(!AreAliased(dst.rb(), scratch_0, scratch_1));
  DCHECK(!AreAliased(scratch_0, scratch_1));
  LoadU64(scratch_0, src);
  LoadU64(scratch_1, dst);
  StoreU64(scratch_0, dst);
  StoreU64(scratch_1, src);
}

void MacroAssembler::SwapFloat32(DoubleRegister src, DoubleRegister dst,
                                 DoubleRegister scratch) {
  if (src == dst) return;
  DCHECK(!AreAliased(src, dst, scratch));
  ldr(scratch, src);
  ldr(src, dst);
  ldr(dst, scratch);
}

void MacroAssembler::SwapFloat32(DoubleRegister src, MemOperand dst,
                                 DoubleRegister scratch) {
  DCHECK(!AreAliased(src, scratch));
  ldr(scratch, src);
  LoadF32(src, dst);
  StoreF32(scratch, dst);
}

void MacroAssembler::SwapFloat32(MemOperand src, MemOperand dst,
                                 DoubleRegister scratch) {
  // push d0, to be used as scratch
  lay(sp, MemOperand(sp, -kDoubleSize));
  StoreF64(d0, MemOperand(sp));
  LoadF32(scratch, src);
  LoadF32(d0, dst);
  StoreF32(scratch, dst);
  StoreF32(d0, src);
  // restore d0
  LoadF64(d0, MemOperand(sp));
  lay(sp, MemOperand(sp, kDoubleSize));
}

void MacroAssembler::SwapDouble(DoubleRegister src, DoubleRegister dst,
                                DoubleRegister scratch) {
  if (src == dst) return;
  DCHECK(!AreAliased(src, dst, scratch));
  ldr(scratch, src);
  ldr(src, dst);
  ldr(dst, scratch);
}

void MacroAssembler::SwapDouble(DoubleRegister src, MemOperand dst,
                                DoubleRegister scratch) {
  DCHECK(!AreAliased(src, scratch));
  ldr(scratch, src);
  LoadF64(src, dst);
  StoreF64(scratch, dst);
}

void MacroAssembler::SwapDouble(MemOperand src, MemOperand dst,
                                DoubleRegister scratch) {
  // push d0, to be used as scratch
  lay(sp, MemOperand(sp, -kDoubleSize));
  StoreF64(d0, MemOperand(sp));
  LoadF64(scratch, src);
  LoadF64(d0, dst);
  StoreF64(scratch, dst);
  StoreF64(d0, src);
  // restore d0
  LoadF64(d0, MemOperand(sp));
  lay(sp, MemOperand(sp, kDoubleSize));
}

void MacroAssembler::SwapSimd128(Simd128Register src, Simd128Register dst,
                                 Simd128Register scratch) {
  if (src == dst) return;
  vlr(scratch, src, Condition(0), Condition(0), Condition(0));
  vlr(src, dst, Condition(0), Condition(0), Condition(0));
  vlr(dst, scratch, Condition(0), Condition(0), Condition(0));
}

void MacroAssembler::SwapSimd128(Simd128Register src, MemOperand dst,
                                 Simd128Register scratch) {
  DCHECK(!AreAliased(src, scratch));
  vlr(scratch, src, Condition(0), Condition(0), Condition(0));
  LoadV128(src, dst, ip);
  StoreV128(scratch, dst, ip);
}

void MacroAssembler::SwapSimd128(MemOperand src, MemOperand dst,
                                 Simd128Register scratch) {
  // push d0, to be used as scratch
  lay(sp, MemOperand(sp, -kSimd128Size));
  StoreV128(d0, MemOperand(sp), ip);
  LoadV128(scratch, src, ip);
  LoadV128(d0, dst, ip);
  StoreV128(scratch, dst, ip);
  StoreV128(d0, src, ip);
  // restore d0
  LoadV128(d0, MemOperand(sp), ip);
  lay(sp, MemOperand(sp, kSimd128Size));
}

void MacroAssembler::ComputeCodeStartAddress(Register dst) {
  larl(dst, Operand(-pc_offset() / 2));
}

void MacroAssembler::LoadPC(Register dst) {
  Label current_pc;
  larl(dst, &current_pc);
  bind(&current_pc);
}

void MacroAssembler::JumpIfEqual(Register x, int32_t y, Label* dest) {
  CmpS32(x, Operand(y));
  beq(dest);
}

void MacroAssembler::JumpIfLessThan(Register x, int32_t y, Label* dest) {
  CmpS32(x, Operand(y));
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
  LoadU64(target, MemOperand(kRootRegister, target,
                             IsolateData::builtin_entry_table_offset()));
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
  LoadU64(destination,
          FieldMemOperand(code_object, Code::kInstructionStartOffset));
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

void MacroAssembler::CallJSFunction(Register function_object) {
  Register code = kJavaScriptCallCodeStartRegister;
  LoadTaggedField(code,
                  FieldMemOperand(function_object, JSFunction::kCodeOffset));
  CallCodeObject(code);
}

void MacroAssembler::JumpJSFunction(Register function_object,
                                    JumpMode jump_mode) {
  Register code = kJavaScriptCallCodeStartRegister;
  LoadTaggedField(code,
                  FieldMemOperand(function_object, JSFunction::kCodeOffset));
  JumpCodeObject(code, jump_mode);
}

#if V8_OS_ZOS
// Helper for CallApiFunctionAndReturn().
void MacroAssembler::zosStoreReturnAddressAndCall(Register target,
                                                  Register scratch) {
  DCHECK(target == r3 || target == r4);
  // Shuffle the arguments from Linux arg register to XPLINK arg regs
  mov(r1, r2);
  if (target == r3) {
    mov(r2, r3);
  } else {
    mov(r2, r3);
    mov(r3, r4);
  }

  // Update System Stack Pointer with the appropriate XPLINK stack bias.
  lay(r4, MemOperand(sp, -kStackPointerBias));

  // Preserve r7 by placing into callee-saved register r13
  mov(r13, r7);

  // Load function pointer from slot 1 of fn desc.
  LoadU64(ip, MemOperand(scratch, kSystemPointerSize));
  // Load environment from slot 0 of fn desc.
  LoadU64(r5, MemOperand(scratch));

  StoreReturnAddressAndCall(ip);

  // Restore r7 from r13
  mov(r7, r13);
}
#endif  // V8_OS_ZOS

void MacroAssembler::StoreReturnAddressAndCall(Register target) {
  // This generates the final instruction sequence for calls to C functions
  // once an exit frame has been constructed.
  //
  // Note that this assumes the caller code (i.e. the InstructionStream object
  // currently being generated) is immovable or that the callee function cannot
  // trigger GC, since the callee function will return to it.

#if V8_OS_ZOS
  Register ra = r7;
#else
  Register ra = r14;
#endif
  Label return_label;
  larl(ra, &return_label);  // Generate the return addr of call later.
#if V8_OS_ZOS
  // Mimic the XPLINK expected no-op (2-byte) instruction at the return point.
  // When the C call returns, the 2 bytes are skipped and then the proper
  // instruction is executed.
  lay(ra, MemOperand(ra, -2));
#endif
  StoreU64(ra, MemOperand(sp, kStackFrameRASlot * kSystemPointerSize));

  // zLinux ABI requires caller's frame to have sufficient space for callee
  // preserved regsiter save area.
  b(target);
  bind(&return_label);
}

// Check if the code object is marked for deoptimization. If it is, then it
// jumps to the CompileLazyDeoptimizedCode builtin. In order to do this we need
// to:
//    1. read from memory the word that contains that bit, which can be found in
//       the flags in the referenced {Code} object;
//    2. test kMarkedForDeoptimizationBit in those flags; and
//    3. if it is not zero then it jumps to the builtin.
void MacroAssembler::BailoutIfDeoptimized(Register scratch) {
  int offset = InstructionStream::kCodeOffset - InstructionStream::kHeaderSize;
  LoadTaggedField(scratch,
                  MemOperand(kJavaScriptCallCodeStartRegister, offset));
  TestCodeIsMarkedForDeoptimization(scratch, scratch);
  Jump(BUILTIN_CODE(isolate(), CompileLazyDeoptimizedCode),
       RelocInfo::CODE_TARGET, ne);
}

void MacroAssembler::CallForDeoptimization(Builtin target, int, Label* exit,
                                           DeoptimizeKind kind, Label* ret,
                                           Label*) {
  ASM_CODE_COMMENT(this);
  LoadU64(ip, MemOperand(kRootRegister,
                         IsolateData::BuiltinEntrySlotOffset(target)));
  Call(ip);
  DCHECK_EQ(SizeOfCodeGeneratedSince(exit),
            (kind == DeoptimizeKind::kLazy) ? Deoptimizer::kLazyDeoptExitSize
                                            : Deoptimizer::kEagerDeoptExitSize);
}

void MacroAssembler::Trap() { stop(); }
void MacroAssembler::DebugBreak() { stop(); }

void MacroAssembler::CountLeadingZerosU32(Register dst, Register src,
                                          Register scratch_pair) {
  llgfr(dst, src);
  flogr(scratch_pair,
        dst);  // will modify a register pair scratch and scratch + 1
  AddS32(dst, scratch_pair, Operand(-32));
}

void MacroAssembler::CountLeadingZerosU64(Register dst, Register src,
                                          Register scratch_pair) {
  flogr(scratch_pair,
        src);  // will modify a register pair scratch and scratch + 1
  mov(dst, scratch_pair);
}

void MacroAssembler::CountTrailingZerosU32(Register dst, Register src,
                                           Register scratch_pair) {
  Register scratch0 = scratch_pair;
  Register scratch1 = Register::from_code(scratch_pair.code() + 1);
  DCHECK(!AreAliased(dst, scratch0, scratch1));
  DCHECK(!AreAliased(src, scratch0, scratch1));

  Label done;
  // Check if src is all zeros.
  ltr(scratch1, src);
  mov(dst, Operand(32));
  beq(&done);
  llgfr(scratch1, scratch1);
  lcgr(scratch0, scratch1);
  ngr(scratch1, scratch0);
  flogr(scratch0, scratch1);
  mov(dst, Operand(63));
  SubS64(dst, scratch0);
  bind(&done);
}

void MacroAssembler::CountTrailingZerosU64(Register dst, Register src,
                                           Register scratch_pair) {
  Register scratch0 = scratch_pair;
  Register scratch1 = Register::from_code(scratch_pair.code() + 1);
  DCHECK(!AreAliased(dst, scratch0, scratch1));
  DCHECK(!AreAliased(src, scratch0, scratch1));

  Label done;
  // Check if src is all zeros.
  ltgr(scratch1, src);
  mov(dst, Operand(64));
  beq(&done);
  lcgr(scratch0, scratch1);
  ngr(scratch0, scratch1);
  flogr(scratch0, scratch0);
  mov(dst, Operand(63));
  SubS64(dst, scratch0);
  bind(&done);
}

void MacroAssembler::AtomicCmpExchangeHelper(Register addr, Register output,
                                             Register old_value,
                                             Register new_value, int start,
                                             int end, int shift_amount,
                                             int offset, Register temp0,
                                             Register temp1) {
  LoadU32(temp0, MemOperand(addr, offset));
  llgfr(temp1, temp0);
  RotateInsertSelectBits(temp0, old_value, Operand(start), Operand(end),
                         Operand(shift_amount), false);
  RotateInsertSelectBits(temp1, new_value, Operand(start), Operand(end),
                         Operand(shift_amount), false);
  CmpAndSwap(temp0, temp1, MemOperand(addr, offset));
  RotateInsertSelectBits(output, temp0, Operand(start + shift_amount),
                         Operand(end + shift_amount),
                         Operand(64 - shift_amount), true);
}

void MacroAssembler::AtomicCmpExchangeU8(Register addr, Register output,
                                         Register old_value, Register new_value,
                                         Register temp0, Register temp1) {
#ifdef V8_TARGET_BIG_ENDIAN
#define ATOMIC_COMP_EXCHANGE_BYTE(i)                                        \
  {                                                                         \
    constexpr int idx = (i);                                                \
    static_assert(idx <= 3 && idx >= 0, "idx is out of range!");            \
    constexpr int start = 32 + 8 * idx;                                     \
    constexpr int end = start + 7;                                          \
    constexpr int shift_amount = (3 - idx) * 8;                             \
    AtomicCmpExchangeHelper(addr, output, old_value, new_value, start, end, \
                            shift_amount, -idx, temp0, temp1);              \
  }
#else
#define ATOMIC_COMP_EXCHANGE_BYTE(i)                                        \
  {                                                                         \
    constexpr int idx = (i);                                                \
    static_assert(idx <= 3 && idx >= 0, "idx is out of range!");            \
    constexpr int start = 32 + 8 * (3 - idx);                               \
    constexpr int end = start + 7;                                          \
    constexpr int shift_amount = idx * 8;                                   \
    AtomicCmpExchangeHelper(addr, output, old_value, new_value, start, end, \
                            shift_amount, -idx, temp0, temp1);              \
  }
#endif

  Label one, two, three, done;
  tmll(addr, Operand(3));
  b(Condition(1), &three);
  b(Condition(2), &two);
  b(Condition(4), &one);
  /* ending with 0b00 */
  ATOMIC_COMP_EXCHANGE_BYTE(0);
  b(&done);
  /* ending with 0b01 */
  bind(&one);
  ATOMIC_COMP_EXCHANGE_BYTE(1);
  b(&done);
  /* ending with 0b10 */
  bind(&two);
  ATOMIC_COMP_EXCHANGE_BYTE(2);
  b(&done);
  /* ending with 0b11 */
  bind(&three);
  ATOMIC_COMP_EXCHANGE_BYTE(3);
  bind(&done);
}

void MacroAssembler::AtomicCmpExchangeU16(Register addr, Register output,
                                          Register old_value,
                                          Register new_value, Register temp0,
                                          Register temp1) {
#ifdef V8_TARGET_BIG_ENDIAN
#define ATOMIC_COMP_EXCHANGE_HALFWORD(i)                                    \
  {                                                                         \
    constexpr int idx = (i);                                                \
    static_assert(idx <= 1 && idx >= 0, "idx is out of range!");            \
    constexpr int start = 32 + 16 * idx;                                    \
    constexpr int end = start + 15;                                         \
    constexpr int shift_amount = (1 - idx) * 16;                            \
    AtomicCmpExchangeHelper(addr, output, old_value, new_value, start, end, \
                            shift_amount, -idx * 2, temp0, temp1);          \
  }
#else
#define ATOMIC_COMP_EXCHANGE_HALFWORD(i)                                    \
  {                                                                         \
    constexpr int idx = (i);                                                \
    static_assert(idx <= 1 && idx >= 0, "idx is out of range!");            \
    constexpr int start = 32 + 16 * (1 - idx);                              \
    constexpr int end = start + 15;                                         \
    constexpr int shift_amount = idx * 16;                                  \
    AtomicCmpExchangeHelper(addr, output, old_value, new_value, start, end, \
                            shift_amount, -idx * 2, temp0, temp1);          \
  }
#endif

  Label two, done;
  tmll(addr, Operand(3));
  b(Condition(2), &two);
  ATOMIC_COMP_EXCHANGE_HALFWORD(0);
  b(&done);
  bind(&two);
  ATOMIC_COMP_EXCHANGE_HALFWORD(1);
  bind(&done);
}

void MacroAssembler::AtomicExchangeHelper(Register addr, Register value,
                                          Register output, int start, int end,
                                          int shift_amount, int offset,
                                          Register scratch) {
  Label do_cs;
  LoadU32(output, MemOperand(addr, offset));
  bind(&do_cs);
  llgfr(scratch, output);
  RotateInsertSelectBits(scratch, value, Operand(start), Operand(end),
                         Operand(shift_amount), false);
  csy(output, scratch, MemOperand(addr, offset));
  bne(&do_cs, Label::kNear);
  srl(output, Operand(shift_amount));
}

void MacroAssembler::AtomicExchangeU8(Register addr, Register value,
                                      Register output, Register scratch) {
#ifdef V8_TARGET_BIG_ENDIAN
#define ATOMIC_EXCHANGE_BYTE(i)                                               \
  {                                                                           \
    constexpr int idx = (i);                                                  \
    static_assert(idx <= 3 && idx >= 0, "idx is out of range!");              \
    constexpr int start = 32 + 8 * idx;                                       \
    constexpr int end = start + 7;                                            \
    constexpr int shift_amount = (3 - idx) * 8;                               \
    AtomicExchangeHelper(addr, value, output, start, end, shift_amount, -idx, \
                         scratch);                                            \
  }
#else
#define ATOMIC_EXCHANGE_BYTE(i)                                               \
  {                                                                           \
    constexpr int idx = (i);                                                  \
    static_assert(idx <= 3 && idx >= 0, "idx is out of range!");              \
    constexpr int start = 32 + 8 * (3 - idx);                                 \
    constexpr int end = start + 7;                                            \
    constexpr int shift_amount = idx * 8;                                     \
    AtomicExchangeHelper(addr, value, output, start, end, shift_amount, -idx, \
                         scratch);                                            \
  }
#endif
  Label three, two, one, done;
  tmll(addr, Operand(3));
  b(Condition(1), &three);
  b(Condition(2), &two);
  b(Condition(4), &one);

  // end with 0b00
  ATOMIC_EXCHANGE_BYTE(0);
  b(&done);

  // ending with 0b01
  bind(&one);
  ATOMIC_EXCHANGE_BYTE(1);
  b(&done);

  // ending with 0b10
  bind(&two);
  ATOMIC_EXCHANGE_BYTE(2);
  b(&done);

  // ending with 0b11
  bind(&three);
  ATOMIC_EXCHANGE_BYTE(3);

  bind(&done);
}

void MacroAssembler::AtomicExchangeU16(Register addr, Register value,
                                       Register output, Register scratch) {
#ifdef V8_TARGET_BIG_ENDIAN
#define ATOMIC_EXCHANGE_HALFWORD(i)                                     \
  {                                                                     \
    constexpr int idx = (i);                                            \
    static_assert(idx <= 1 && idx >= 0, "idx is out of range!");        \
    constexpr int start = 32 + 16 * idx;                                \
    constexpr int end = start + 15;                                     \
    constexpr int shift_amount = (1 - idx) * 16;                        \
    AtomicExchangeHelper(addr, value, output, start, end, shift_amount, \
                         -idx * 2, scratch);                            \
  }
#else
#define ATOMIC_EXCHANGE_HALFWORD(i)                                     \
  {                                                                     \
    constexpr int idx = (i);                                            \
    static_assert(idx <= 1 && idx >= 0, "idx is out of range!");        \
    constexpr int start = 32 + 16 * (1 - idx);                          \
    constexpr int end = start + 15;                                     \
    constexpr int shift_amount = idx * 16;                              \
    AtomicExchangeHelper(addr, value, output, start, end, shift_amount, \
                         -idx * 2, scratch);                            \
  }
#endif
  Label two, done;
  tmll(addr, Operand(3));
  b(Condition(2), &two);

  // end with 0b00
  ATOMIC_EXCHANGE_HALFWORD(0);
  b(&done);

  // ending with 0b10
  bind(&two);
  ATOMIC_EXCHANGE_HALFWORD(1);

  bind(&done);
}

// Simd Support.
void MacroAssembler::F64x2Splat(Simd128Register dst, Simd128Register src) {
  vrep(dst, src, Operand(0), Condition(3));
}

void MacroAssembler::F32x4Splat(Simd128Register dst, Simd128Register src) {
  vrep(dst, src, Operand(0), Condition(2));
}

void MacroAssembler::I64x2Splat(Simd128Register dst, Register src) {
  vlvg(dst, src, MemOperand(r0, 0), Condition(3));
  vrep(dst, dst, Operand(0), Condition(3));
}

void MacroAssembler::I32x4Splat(Simd128Register dst, Register src) {
  vlvg(dst, src, MemOperand(r0, 0), Condition(2));
  vrep(dst, dst, Operand(0), Condition(2));
}

void MacroAssembler::I16x8Splat(Simd128Register dst, Register src) {
  vlvg(dst, src, MemOperand(r0, 0), Condition(1));
  vrep(dst, dst, Operand(0), Condition(1));
}

void MacroAssembler::I8x16Splat(Simd128Register dst, Register src) {
  vlvg(dst, src, MemOperand(r0, 0), Condition(0));
  vrep(dst, dst, Operand(0), Condition(0));
}

void MacroAssembler::F64x2ExtractLane(DoubleRegister dst, Simd128Register src,
                                      uint8_t imm_lane_idx, Register) {
  vrep(dst, src, Operand(1 - imm_lane_idx), Condition(3));
}

void MacroAssembler::F32x4ExtractLane(DoubleRegister dst, Simd128Register src,
                                      uint8_t imm_lane_idx, Register) {
  vrep(dst, src, Operand(3 - imm_lane_idx), Condition(2));
}

void MacroAssembler::I64x2ExtractLane(Register dst, Simd128Register src,
                                      uint8_t imm_lane_idx, Register) {
  vlgv(dst, src, MemOperand(r0, 1 - imm_lane_idx), Condition(3));
}

void MacroAssembler::I32x4ExtractLane(Register dst, Simd128Register src,
                                      uint8_t imm_lane_idx, Register) {
  vlgv(dst, src, MemOperand(r0, 3 - imm_lane_idx), Condition(2));
}

void MacroAssembler::I16x8ExtractLaneU(Register dst, Simd128Register src,
                                       uint8_t imm_lane_idx, Register) {
  vlgv(dst, src, MemOperand(r0, 7 - imm_lane_idx), Condition(1));
}

void MacroAssembler::I16x8ExtractLaneS(Register dst, Simd128Register src,
                                       uint8_t imm_lane_idx, Register scratch) {
  vlgv(scratch, src, MemOperand(r0, 7 - imm_lane_idx), Condition(1));
  lghr(dst, scratch);
}

void MacroAssembler::I8x16ExtractLaneU(Register dst, Simd128Register src,
                                       uint8_t imm_lane_idx, Register) {
  vlgv(dst, src, MemOperand(r0, 15 - imm_lane_idx), Condition(0));
}

void MacroAssembler::I8x16ExtractLaneS(Register dst, Simd128Register src,
                                       uint8_t imm_lane_idx, Register scratch) {
  vlgv(scratch, src, MemOperand(r0, 15 - imm_lane_idx), Condition(0));
  lgbr(dst, scratch);
}

void MacroAssembler::F64x2ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      DoubleRegister src2, uint8_t imm_lane_idx,
                                      Register scratch) {
  vlgv(scratch, src2, MemOperand(r0, 0), Condition(3));
  if (src1 != dst) {
    vlr(dst, src1, Condition(0), Condition(0), Condition(0));
  }
  vlvg(dst, scratch, MemOperand(r0, 1 - imm_lane_idx), Condition(3));
}

void MacroAssembler::F32x4ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      DoubleRegister src2, uint8_t imm_lane_idx,
                                      Register scratch) {
  vlgv(scratch, src2, MemOperand(r0, 0), Condition(2));
  if (src1 != dst) {
    vlr(dst, src1, Condition(0), Condition(0), Condition(0));
  }
  vlvg(dst, scratch, MemOperand(r0, 3 - imm_lane_idx), Condition(2));
}

void MacroAssembler::I64x2ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      Register src2, uint8_t imm_lane_idx,
                                      Register) {
  if (src1 != dst) {
    vlr(dst, src1, Condition(0), Condition(0), Condition(0));
  }
  vlvg(dst, src2, MemOperand(r0, 1 - imm_lane_idx), Condition(3));
}

void MacroAssembler::I32x4ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      Register src2, uint8_t imm_lane_idx,
                                      Register) {
  if (src1 != dst) {
    vlr(dst, src1, Condition(0), Condition(0), Condition(0));
  }
  vlvg(dst, src2, MemOperand(r0, 3 - imm_lane_idx), Condition(2));
}

void MacroAssembler::I16x8ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      Register src2, uint8_t imm_lane_idx,
                                      Register) {
  if (src1 != dst) {
    vlr(dst, src1, Condition(0), Condition(0), Condition(0));
  }
  vlvg(dst, src2, MemOperand(r0, 7 - imm_lane_idx), Condition(1));
}

void MacroAssembler::I8x16ReplaceLane(Simd128Register dst, Simd128Register src1,
                                      Register src2, uint8_t imm_lane_idx,
                                      Register) {
  if (src1 != dst) {
    vlr(dst, src1, Condition(0), Condition(0), Condition(0));
  }
  vlvg(dst, src2, MemOperand(r0, 15 - imm_lane_idx), Condition(0));
}

void MacroAssembler::S128Not(Simd128Register dst, Simd128Register src) {
  vno(dst, src, src, Condition(0), Condition(0), Condition(0));
}

void MacroAssembler::S128Zero(Simd128Register dst, Simd128Register src) {
  vx(dst, src, src, Condition(0), Condition(0), Condition(0));
}

void MacroAssembler::S128AllOnes(Simd128Register dst, Simd128Register src) {
  vceq(dst, src, src, Condition(0), Condition(3));
}

void MacroAssembler::S128Select(Simd128Register dst, Simd128Register src1,
                                Simd128Register src2, Simd128Register mask) {
  vsel(dst, src1, src2, mask, Condition(0), Condition(0));
}

#define SIMD_UNOP_LIST_VRR_A(V)             \
  V(F64x2Abs, vfpso, 2, 0, 3)               \
  V(F64x2Neg, vfpso, 0, 0, 3)               \
  V(F64x2Sqrt, vfsq, 0, 0, 3)               \
  V(F64x2Ceil, vfi, 6, 0, 3)                \
  V(F64x2Floor, vfi, 7, 0, 3)               \
  V(F64x2Trunc, vfi, 5, 0, 3)               \
  V(F64x2NearestInt, vfi, 4, 0, 3)          \
  V(F32x4Abs, vfpso, 2, 0, 2)               \
  V(F32x4Neg, vfpso, 0, 0, 2)               \
  V(F32x4Sqrt, vfsq, 0, 0, 2)               \
  V(F32x4Ceil, vfi, 6, 0, 2)                \
  V(F32x4Floor, vfi, 7, 0, 2)               \
  V(F32x4Trunc, vfi, 5, 0, 2)               \
  V(F32x4NearestInt, vfi, 4, 0, 2)          \
  V(I64x2Abs, vlp, 0, 0, 3)                 \
  V(I64x2Neg, vlc, 0, 0, 3)                 \
  V(I64x2SConvertI32x4Low, vupl, 0, 0, 2)   \
  V(I64x2SConvertI32x4High, vuph, 0, 0, 2)  \
  V(I64x2UConvertI32x4Low, vupll, 0, 0, 2)  \
  V(I64x2UConvertI32x4High, vuplh, 0, 0, 2) \
  V(I32x4Abs, vlp, 0, 0, 2)                 \
  V(I32x4Neg, vlc, 0, 0, 2)                 \
  V(I32x4SConvertI16x8Low, vupl, 0, 0, 1)   \
  V(I32x4SConvertI16x8High, vuph, 0, 0, 1)  \
  V(I32x4UConvertI16x8Low, vupll, 0, 0, 1)  \
  V(I32x4UConvertI16x8High, vuplh, 0, 0, 1) \
  V(I16x8Abs, vlp, 0, 0, 1)                 \
  V(I16x8Neg, vlc, 0, 0, 1)                 \
  V(I16x8SConvertI8x16Low, vupl, 0, 0, 0)   \
  V(I16x8SConvertI8x16High, vuph, 0, 0, 0)  \
  V(I16x8UConvertI8x16Low, vupll, 0, 0, 0)  \
  V(I16x8UConvertI8x16High, vuplh, 0, 0, 0) \
  V(I8x16Abs, vlp, 0, 0, 0)                 \
  V(I8x16Neg, vlc, 0, 0, 0)                 \
  V(I8x16Popcnt, vpopct, 0, 0, 0)

#define EMIT_SIMD_UNOP_VRR_A(name, op, c1, c2, c3)                      \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src) { \
    op(dst, src, Condition(c1), Condition(c2), Condition(c3));          \
  }
SIMD_UNOP_LIST_VRR_A(EMIT_SIMD_UNOP_VRR_A)
#undef EMIT_SIMD_UNOP_VRR_A
#undef SIMD_UNOP_LIST_VRR_A

#define SIMD_BINOP_LIST_VRR_B(V) \
  V(I64x2Eq, vceq, 0, 3)         \
  V(I64x2GtS, vch, 0, 3)         \
  V(I32x4Eq, vceq, 0, 2)         \
  V(I32x4GtS, vch, 0, 2)         \
  V(I32x4GtU, vchl, 0, 2)        \
  V(I16x8Eq, vceq, 0, 1)         \
  V(I16x8GtS, vch, 0, 1)         \
  V(I16x8GtU, vchl, 0, 1)        \
  V(I8x16Eq, vceq, 0, 0)         \
  V(I8x16GtS, vch, 0, 0)         \
  V(I8x16GtU, vchl, 0, 0)

#define EMIT_SIMD_BINOP_VRR_B(name, op, c1, c2)                        \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1, \
                            Simd128Register src2) {                    \
    op(dst, src1, src2, Condition(c1), Condition(c2));                 \
  }
SIMD_BINOP_LIST_VRR_B(EMIT_SIMD_BINOP_VRR_B)
#undef EMIT_SIMD_BINOP_VRR_B
#undef SIMD_BINOP_LIST_VRR_B

#define SIMD_BINOP_LIST_VRR_C(V)           \
  V(F64x2Add, vfa, 0, 0, 3)                \
  V(F64x2Sub, vfs, 0, 0, 3)                \
  V(F64x2Mul, vfm, 0, 0, 3)                \
  V(F64x2Div, vfd, 0, 0, 3)                \
  V(F64x2Min, vfmin, 1, 0, 3)              \
  V(F64x2Max, vfmax, 1, 0, 3)              \
  V(F64x2Eq, vfce, 0, 0, 3)                \
  V(F64x2Pmin, vfmin, 3, 0, 3)             \
  V(F64x2Pmax, vfmax, 3, 0, 3)             \
  V(F32x4Add, vfa, 0, 0, 2)                \
  V(F32x4Sub, vfs, 0, 0, 2)                \
  V(F32x4Mul, vfm, 0, 0, 2)                \
  V(F32x4Div, vfd, 0, 0, 2)                \
  V(F32x4Min, vfmin, 1, 0, 2)              \
  V(F32x4Max, vfmax, 1, 0, 2)              \
  V(F32x4Eq, vfce, 0, 0, 2)                \
  V(F32x4Pmin, vfmin, 3, 0, 2)             \
  V(F32x4Pmax, vfmax, 3, 0, 2)             \
  V(I64x2Add, va, 0, 0, 3)                 \
  V(I64x2Sub, vs, 0, 0, 3)                 \
  V(I32x4Add, va, 0, 0, 2)                 \
  V(I32x4Sub, vs, 0, 0, 2)                 \
  V(I32x4Mul, vml, 0, 0, 2)                \
  V(I32x4MinS, vmn, 0, 0, 2)               \
  V(I32x4MinU, vmnl, 0, 0, 2)              \
  V(I32x4MaxS, vmx, 0, 0, 2)               \
  V(I32x4MaxU, vmxl, 0, 0, 2)              \
  V(I16x8Add, va, 0, 0, 1)                 \
  V(I16x8Sub, vs, 0, 0, 1)                 \
  V(I16x8Mul, vml, 0, 0, 1)                \
  V(I16x8MinS, vmn, 0, 0, 1)               \
  V(I16x8MinU, vmnl, 0, 0, 1)              \
  V(I16x8MaxS, vmx, 0, 0, 1)               \
  V(I16x8MaxU, vmxl, 0, 0, 1)              \
  V(I16x8RoundingAverageU, vavgl, 0, 0, 1) \
  V(I8x16Add, va, 0, 0, 0)                 \
  V(I8x16Sub, vs, 0, 0, 0)                 \
  V(I8x16MinS, vmn, 0, 0, 0)               \
  V(I8x16MinU, vmnl, 0, 0, 0)              \
  V(I8x16MaxS, vmx, 0, 0, 0)               \
  V(I8x16MaxU, vmxl, 0, 0, 0)              \
  V(I8x16RoundingAverageU, vavgl, 0, 0, 0) \
  V(S128And, vn, 0, 0, 0)                  \
  V(S128Or, vo, 0, 0, 0)                   \
  V(S128Xor, vx, 0, 0, 0)                  \
  V(S128AndNot, vnc, 0, 0, 0)

#define EMIT_SIMD_BINOP_VRR_C(name, op, c1, c2, c3)                    \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1, \
                            Simd128Register src2) {                    \
    op(dst, src1, src2, Condition(c1), Condition(c2), Condition(c3));  \
  }
SIMD_BINOP_LIST_VRR_C(EMIT_SIMD_BINOP_VRR_C)
#undef EMIT_SIMD_BINOP_VRR_C
#undef SIMD_BINOP_LIST_VRR_C

#define SIMD_SHIFT_LIST(V) \
  V(I64x2Shl, veslv, 3)    \
  V(I64x2ShrS, vesrav, 3)  \
  V(I64x2ShrU, vesrlv, 3)  \
  V(I32x4Shl, veslv, 2)    \
  V(I32x4ShrS, vesrav, 2)  \
  V(I32x4ShrU, vesrlv, 2)  \
  V(I16x8Shl, veslv, 1)    \
  V(I16x8ShrS, vesrav, 1)  \
  V(I16x8ShrU, vesrlv, 1)  \
  V(I8x16Shl, veslv, 0)    \
  V(I8x16ShrS, vesrav, 0)  \
  V(I8x16ShrU, vesrlv, 0)

#define EMIT_SIMD_SHIFT(name, op, c1)                                  \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1, \
                            Register src2, Simd128Register scratch) {  \
    vlvg(scratch, src2, MemOperand(r0, 0), Condition(c1));             \
    vrep(scratch, scratch, Operand(0), Condition(c1));                 \
    op(dst, src1, scratch, Condition(0), Condition(0), Condition(c1)); \
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

#define SIMD_EXT_MUL_LIST(V)                    \
  V(I64x2ExtMulLowI32x4S, vme, vmo, vmrl, 2)    \
  V(I64x2ExtMulHighI32x4S, vme, vmo, vmrh, 2)   \
  V(I64x2ExtMulLowI32x4U, vmle, vmlo, vmrl, 2)  \
  V(I64x2ExtMulHighI32x4U, vmle, vmlo, vmrh, 2) \
  V(I32x4ExtMulLowI16x8S, vme, vmo, vmrl, 1)    \
  V(I32x4ExtMulHighI16x8S, vme, vmo, vmrh, 1)   \
  V(I32x4ExtMulLowI16x8U, vmle, vmlo, vmrl, 1)  \
  V(I32x4ExtMulHighI16x8U, vmle, vmlo, vmrh, 1) \
  V(I16x8ExtMulLowI8x16S, vme, vmo, vmrl, 0)    \
  V(I16x8ExtMulHighI8x16S, vme, vmo, vmrh, 0)   \
  V(I16x8ExtMulLowI8x16U, vmle, vmlo, vmrl, 0)  \
  V(I16x8ExtMulHighI8x16U, vmle, vmlo, vmrh, 0)

#define EMIT_SIMD_EXT_MUL(name, mul_even, mul_odd, merge, mode)                \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1,         \
                            Simd128Register src2, Simd128Register scratch) {   \
    mul_even(scratch, src1, src2, Condition(0), Condition(0),                  \
             Condition(mode));                                                 \
    mul_odd(dst, src1, src2, Condition(0), Condition(0), Condition(mode));     \
    merge(dst, scratch, dst, Condition(0), Condition(0), Condition(mode + 1)); \
  }
SIMD_EXT_MUL_LIST(EMIT_SIMD_EXT_MUL)
#undef EMIT_SIMD_EXT_MUL
#undef SIMD_EXT_MUL_LIST

#define SIMD_ALL_TRUE_LIST(V) \
  V(I64x2AllTrue, 3)          \
  V(I32x4AllTrue, 2)          \
  V(I16x8AllTrue, 1)          \
  V(I8x16AllTrue, 0)

#define EMIT_SIMD_ALL_TRUE(name, mode)                                     \
  void MacroAssembler::name(Register dst, Simd128Register src,             \
                            Register scratch1, Simd128Register scratch2) { \
    mov(scratch1, Operand(1));                                             \
    xgr(dst, dst);                                                         \
    vx(scratch2, scratch2, scratch2, Condition(0), Condition(0),           \
       Condition(2));                                                      \
    vceq(scratch2, src, scratch2, Condition(0), Condition(mode));          \
    vtm(scratch2, scratch2, Condition(0), Condition(0), Condition(0));     \
    locgr(Condition(8), dst, scratch1);                                    \
  }
SIMD_ALL_TRUE_LIST(EMIT_SIMD_ALL_TRUE)
#undef EMIT_SIMD_ALL_TRUE
#undef SIMD_ALL_TRUE_LIST

#define SIMD_QFM_LIST(V) \
  V(F64x2Qfma, vfma, 3)  \
  V(F64x2Qfms, vfnms, 3) \
  V(F32x4Qfma, vfma, 2)  \
  V(F32x4Qfms, vfnms, 2)

#define EMIT_SIMD_QFM(name, op, c1)                                       \
  void MacroAssembler::name(Simd128Register dst, Simd128Register src1,    \
                            Simd128Register src2, Simd128Register src3) { \
    op(dst, src1, src2, src3, Condition(c1), Condition(0));               \
  }
SIMD_QFM_LIST(EMIT_SIMD_QFM)
#undef EMIT_SIMD_QFM
#undef SIMD_QFM_LIST

void MacroAssembler::I64x2Mul(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Register scratch1,
                              Register scratch2, Register scratch3) {
  Register scratch_1 = scratch1;
  Register scratch_2 = scratch2;
  for (int i = 0; i < 2; i++) {
    vlgv(scratch_1, src1, MemOperand(r0, i), Condition(3));
    vlgv(scratch_2, src2, MemOperand(r0, i), Condition(3));
    MulS64(scratch_1, scratch_2);
    scratch_1 = scratch2;
    scratch_2 = scratch3;
  }
  vlvgp(dst, scratch1, scratch2);
}

void MacroAssembler::F64x2Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vfce(dst, src1, src2, Condition(0), Condition(0), Condition(3));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(3));
}

void MacroAssembler::F64x2Lt(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vfch(dst, src2, src1, Condition(0), Condition(0), Condition(3));
}

void MacroAssembler::F64x2Le(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vfche(dst, src2, src1, Condition(0), Condition(0), Condition(3));
}

void MacroAssembler::F32x4Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vfce(dst, src1, src2, Condition(0), Condition(0), Condition(2));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(2));
}

void MacroAssembler::F32x4Lt(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vfch(dst, src2, src1, Condition(0), Condition(0), Condition(2));
}

void MacroAssembler::F32x4Le(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vfche(dst, src2, src1, Condition(0), Condition(0), Condition(2));
}

void MacroAssembler::I64x2Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vceq(dst, src1, src2, Condition(0), Condition(3));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(3));
}

void MacroAssembler::I64x2GeS(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2) {
  // Compute !(B > A) which is equal to A >= B.
  vch(dst, src2, src1, Condition(0), Condition(3));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(3));
}

void MacroAssembler::I32x4Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vceq(dst, src1, src2, Condition(0), Condition(2));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(2));
}

void MacroAssembler::I32x4GeS(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2) {
  // Compute !(B > A) which is equal to A >= B.
  vch(dst, src2, src1, Condition(0), Condition(2));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(2));
}

void MacroAssembler::I32x4GeU(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vceq(scratch, src1, src2, Condition(0), Condition(2));
  vchl(dst, src1, src2, Condition(0), Condition(2));
  vo(dst, dst, scratch, Condition(0), Condition(0), Condition(2));
}

void MacroAssembler::I16x8Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vceq(dst, src1, src2, Condition(0), Condition(1));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(1));
}

void MacroAssembler::I16x8GeS(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2) {
  // Compute !(B > A) which is equal to A >= B.
  vch(dst, src2, src1, Condition(0), Condition(1));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(1));
}

void MacroAssembler::I16x8GeU(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vceq(scratch, src1, src2, Condition(0), Condition(1));
  vchl(dst, src1, src2, Condition(0), Condition(1));
  vo(dst, dst, scratch, Condition(0), Condition(0), Condition(1));
}

void MacroAssembler::I8x16Ne(Simd128Register dst, Simd128Register src1,
                             Simd128Register src2) {
  vceq(dst, src1, src2, Condition(0), Condition(0));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(0));
}

void MacroAssembler::I8x16GeS(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2) {
  // Compute !(B > A) which is equal to A >= B.
  vch(dst, src2, src1, Condition(0), Condition(0));
  vno(dst, dst, dst, Condition(0), Condition(0), Condition(0));
}

void MacroAssembler::I8x16GeU(Simd128Register dst, Simd128Register src1,
                              Simd128Register src2, Simd128Register scratch) {
  vceq(scratch, src1, src2, Condition(0), Condition(0));
  vchl(dst, src1, src2, Condition(0), Condition(0));
  vo(dst, dst, scratch, Condition(0), Condition(0), Condition(0));
}

void MacroAssembler::I64x2BitMask(Register dst, Simd128Register src,
                                  Register scratch1, Simd128Register scratch2) {
  mov(scratch1, Operand(0x8080808080800040));
  vlvg(scratch2, scratch1, MemOperand(r0, 1), Condition(3));
  vbperm(scratch2, src, scratch2, Condition(0), Condition(0), Condition(0));
  vlgv(dst, scratch2, MemOperand(r0, 7), Condition(0));
}

void MacroAssembler::I32x4BitMask(Register dst, Simd128Register src,
                                  Register scratch1, Simd128Register scratch2) {
  mov(scratch1, Operand(0x8080808000204060));
  vlvg(scratch2, scratch1, MemOperand(r0, 1), Condition(3));
  vbperm(scratch2, src, scratch2, Condition(0), Condition(0), Condition(0));
  vlgv(dst, scratch2, MemOperand(r0, 7), Condition(0));
}

void MacroAssembler::I16x8BitMask(Register dst, Simd128Register src,
                                  Register scratch1, Simd128Register scratch2) {
  mov(scratch1, Operand(0x10203040506070));
  vlvg(scratch2, scratch1, MemOperand(r0, 1), Condition(3));
  vbperm(scratch2, src, scratch2, Condition(0), Condition(0), Condition(0));
  vlgv(dst, scratch2, MemOperand(r0, 7), Condition(0));
}

void MacroAssembler::F64x2ConvertLowI32x4S(Simd128Register dst,
                                           Simd128Register src) {
  vupl(dst, src, Condition(0), Condition(0), Condition(2));
  vcdg(dst, dst, Condition(4), Condition(0), Condition(3));
}

void MacroAssembler::F64x2ConvertLowI32x4U(Simd128Register dst,
                                           Simd128Register src) {
  vupll(dst, src, Condition(0), Condition(0), Condition(2));
  vcdlg(dst, dst, Condition(4), Condition(0), Condition(3));
}

void MacroAssembler::I8x16BitMask(Register dst, Simd128Register src,
                                  Register scratch1, Register scratch2,
                                  Simd128Register scratch3) {
  mov(scratch1, Operand(0x4048505860687078));
  mov(scratch2, Operand(0x8101820283038));
  vlvgp(scratch3, scratch2, scratch1);
  vbperm(scratch3, src, scratch3, Condition(0), Condition(0), Condition(0));
  vlgv(dst, scratch3, MemOperand(r0, 3), Condition(1));
}

void MacroAssembler::V128AnyTrue(Register dst, Simd128Register src,
                                 Register scratch) {
  mov(dst, Operand(1));
  xgr(scratch, scratch);
  vtm(src, src, Condition(0), Condition(0), Condition(0));
  locgr(Condition(8), dst, scratch);
}

#define CONVERT_FLOAT_TO_INT32(convert, dst, src, scratch1, scratch2) \
  for (int index = 0; index < 4; index++) {                           \
    vlgv(scratch2, src, MemOperand(r0, index), Condition(2));         \
    MovIntToFloat(scratch1, scratch2);                                \
    convert(scratch2, scratch1, kRoundToZero);                        \
    vlvg(dst, scratch2, MemOperand(r0, index), Condition(2));         \
  }
void MacroAssembler::I32x4SConvertF32x4(Simd128Register dst,
                                        Simd128Register src,
                                        Simd128Register scratch1,
                                        Register scratch2) {
  // NaN to 0.
  vfce(scratch1, src, src, Condition(0), Condition(0), Condition(2));
  vn(dst, src, scratch1, Condition(0), Condition(0), Condition(0));
  if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2)) {
    vcgd(dst, dst, Condition(5), Condition(0), Condition(2));
  } else {
    CONVERT_FLOAT_TO_INT32(ConvertFloat32ToInt32, dst, dst, scratch1, scratch2)
  }
}

void MacroAssembler::I32x4UConvertF32x4(Simd128Register dst,
                                        Simd128Register src,
                                        Simd128Register scratch1,
                                        Register scratch2) {
  // vclgd or ConvertFloat32ToUnsignedInt32 will convert NaN to 0, negative to 0
  // automatically.
  if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2)) {
    vclgd(dst, src, Condition(5), Condition(0), Condition(2));
  } else {
    CONVERT_FLOAT_TO_INT32(ConvertFloat32ToUnsignedInt32, dst, src, scratch1,
                           scratch2)
  }
}
#undef CONVERT_FLOAT_TO_INT32

#define CONVERT_INT32_TO_FLOAT(convert, dst, src, scratch1, scratch2) \
  for (int index = 0; index < 4; index++) {                           \
    vlgv(scratch2, src, MemOperand(r0, index), Condition(2));         \
    convert(scratch1, scratch2);                                      \
    MovFloatToInt(scratch2, scratch1);                                \
    vlvg(dst, scratch2, MemOperand(r0, index), Condition(2));         \
  }
void MacroAssembler::F32x4SConvertI32x4(Simd128Register dst,
                                        Simd128Register src,
                                        Simd128Register scratch1,
                                        Register scratch2) {
  if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2)) {
    vcdg(dst, src, Condition(4), Condition(0), Condition(2));
  } else {
    CONVERT_INT32_TO_FLOAT(ConvertIntToFloat, dst, src, scratch1, scratch2)
  }
}
void MacroAssembler::F32x4UConvertI32x4(Simd128Register dst,
                                        Simd128Register src,
                                        Simd128Register scratch1,
                                        Register scratch2) {
  if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2)) {
    vcdlg(dst, src, Condition(4), Condition(0), Condition(2));
  } else {
    CONVERT_INT32_TO_FLOAT(ConvertUnsignedIntToFloat, dst, src, scratch1,
                           scratch2)
  }
}
#undef CONVERT_INT32_TO_FLOAT

void MacroAssembler::I16x8SConvertI32x4(Simd128Register dst,
                                        Simd128Register src1,
                                        Simd128Register src2) {
  vpks(dst, src2, src1, Condition(0), Condition(2));
}

void MacroAssembler::I8x16SConvertI16x8(Simd128Register dst,
                                        Simd128Register src1,
                                        Simd128Register src2) {
  vpks(dst, src2, src1, Condition(0), Condition(1));
}

#define VECTOR_PACK_UNSIGNED(dst, src1, src2, scratch, mode)       \
  vx(kDoubleRegZero, kDoubleRegZero, kDoubleRegZero, Condition(0), \
     Condition(0), Condition(mode));                               \
  vmx(scratch, src1, kDoubleRegZero, Condition(0), Condition(0),   \
      Condition(mode));                                            \
  vmx(dst, src2, kDoubleRegZero, Condition(0), Condition(0), Condition(mode));
void MacroAssembler::I16x8UConvertI32x4(Simd128Register dst,
                                        Simd128Register src1,
                                        Simd128Register src2,
                                        Simd128Register scratch) {
  // treat inputs as signed, and saturate to unsigned (negative to 0).
  VECTOR_PACK_UNSIGNED(dst, src1, src2, scratch, 2)
  vpkls(dst, dst, scratch, Condition(0), Condition(2));
}

void MacroAssembler::I8x16UConvertI16x8(Simd128Register dst,
                                        Simd128Register src1,
                                        Simd128Register src2,
                                        Simd128Register scratch) {
  // treat inputs as signed, and saturate to unsigned (negative to 0).
  VECTOR_PACK_UNSIGNED(dst, src1, src2, scratch, 1)
  vpkls(dst, dst, scratch, Condition(0), Condition(1));
}
#undef VECTOR_PACK_UNSIGNED

#define BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, op, extract_high, \
                      extract_low, mode)                                     \
  DCHECK(dst != scratch1 && dst != scratch2);                                \
  DCHECK(dst != src1 && dst != src2);                                        \
  extract_high(scratch1, src1, Condition(0), Condition(0), Condition(mode)); \
  extract_high(scratch2, src2, Condition(0), Condition(0), Condition(mode)); \
  op(dst, scratch1, scratch2, Condition(0), Condition(0),                    \
     Condition(mode + 1));                                                   \
  extract_low(scratch1, src1, Condition(0), Condition(0), Condition(mode));  \
  extract_low(scratch2, src2, Condition(0), Condition(0), Condition(mode));  \
  op(scratch1, scratch1, scratch2, Condition(0), Condition(0),               \
     Condition(mode + 1));
void MacroAssembler::I16x8AddSatS(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch1,
                                  Simd128Register scratch2) {
  BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, va, vuph, vupl, 1)
  vpks(dst, dst, scratch1, Condition(0), Condition(2));
}

void MacroAssembler::I16x8SubSatS(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch1,
                                  Simd128Register scratch2) {
  BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, vs, vuph, vupl, 1)
  vpks(dst, dst, scratch1, Condition(0), Condition(2));
}

void MacroAssembler::I16x8AddSatU(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch1,
                                  Simd128Register scratch2) {
  BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, va, vuplh, vupll, 1)
  vpkls(dst, dst, scratch1, Condition(0), Condition(2));
}

void MacroAssembler::I16x8SubSatU(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch1,
                                  Simd128Register scratch2) {
  BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, vs, vuplh, vupll, 1)
  // negative intermediate values to 0.
  vx(kDoubleRegZero, kDoubleRegZero, kDoubleRegZero, Condition(0), Condition(0),
     Condition(0));
  vmx(dst, kDoubleRegZero, dst, Condition(0), Condition(0), Condition(2));
  vmx(scratch1, kDoubleRegZero, scratch1, Condition(0), Condition(0),
      Condition(2));
  vpkls(dst, dst, scratch1, Condition(0), Condition(2));
}

void MacroAssembler::I8x16AddSatS(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch1,
                                  Simd128Register scratch2) {
  BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, va, vuph, vupl, 0)
  vpks(dst, dst, scratch1, Condition(0), Condition(1));
}

void MacroAssembler::I8x16SubSatS(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch1,
                                  Simd128Register scratch2) {
  BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, vs, vuph, vupl, 0)
  vpks(dst, dst, scratch1, Condition(0), Condition(1));
}

void MacroAssembler::I8x16AddSatU(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch1,
                                  Simd128Register scratch2) {
  BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, va, vuplh, vupll, 0)
  vpkls(dst, dst, scratch1, Condition(0), Condition(1));
}

void MacroAssembler::I8x16SubSatU(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2,
                                  Simd128Register scratch1,
                                  Simd128Register scratch2) {
  BINOP_EXTRACT(dst, src1, src2, scratch1, scratch2, vs, vuplh, vupll, 0)
  // negative intermediate values to 0.
  vx(kDoubleRegZero, kDoubleRegZero, kDoubleRegZero, Condition(0), Condition(0),
     Condition(0));
  vmx(dst, kDoubleRegZero, dst, Condition(0), Condition(0), Condition(1));
  vmx(scratch1, kDoubleRegZero, scratch1, Condition(0), Condition(0),
      Condition(1));
  vpkls(dst, dst, scratch1, Condition(0), Condition(1));
}
#undef BINOP_EXTRACT

void MacroAssembler::F64x2PromoteLowF32x4(Simd128Register dst,
                                          Simd128Register src,
                                          Simd128Register scratch1,
                                          Register scratch2, Register scratch3,
                                          Register scratch4) {
  Register holder = scratch3;
  for (int index = 0; index < 2; ++index) {
    vlgv(scratch2, src, MemOperand(scratch2, index + 2), Condition(2));
    MovIntToFloat(scratch1, scratch2);
    ldebr(scratch1, scratch1);
    MovDoubleToInt64(holder, scratch1);
    holder = scratch4;
  }
  vlvgp(dst, scratch3, scratch4);
}

void MacroAssembler::F32x4DemoteF64x2Zero(Simd128Register dst,
                                          Simd128Register src,
                                          Simd128Register scratch1,
                                          Register scratch2, Register scratch3,
                                          Register scratch4) {
  Register holder = scratch3;
  for (int index = 0; index < 2; ++index) {
    vlgv(scratch2, src, MemOperand(r0, index), Condition(3));
    MovInt64ToDouble(scratch1, scratch2);
    ledbr(scratch1, scratch1);
    MovFloatToInt(holder, scratch1);
    holder = scratch4;
  }
  vx(dst, dst, dst, Condition(0), Condition(0), Condition(2));
  vlvg(dst, scratch3, MemOperand(r0, 2), Condition(2));
  vlvg(dst, scratch4, MemOperand(r0, 3), Condition(2));
}

#define EXT_ADD_PAIRWISE(dst, src, scratch1, scratch2, lane_size, mul_even, \
                         mul_odd)                                           \
  CHECK_NE(src, scratch2);                                                  \
  vrepi(scratch2, Operand(1), Condition(lane_size));                        \
  mul_even(scratch1, src, scratch2, Condition(0), Condition(0),             \
           Condition(lane_size));                                           \
  mul_odd(scratch2, src, scratch2, Condition(0), Condition(0),              \
          Condition(lane_size));                                            \
  va(dst, scratch1, scratch2, Condition(0), Condition(0),                   \
     Condition(lane_size + 1));
void MacroAssembler::I32x4ExtAddPairwiseI16x8S(Simd128Register dst,
                                               Simd128Register src,
                                               Simd128Register scratch1,
                                               Simd128Register scratch2) {
  EXT_ADD_PAIRWISE(dst, src, scratch1, scratch2, 1, vme, vmo)
}

void MacroAssembler::I32x4ExtAddPairwiseI16x8U(Simd128Register dst,
                                               Simd128Register src,
                                               Simd128Register /* scratch1 */,
                                               Simd128Register /* scratch2 */) {
  // Unnamed scratch parameters are still kept to make this function
  // have the same signature as the other ExtAddPairwise functions.
  // TF and Liftoff use a uniform Macro for all of them.
  // TODO(miladfarca): Add a default argument or separate them in TF and
  // Liftoff.
  vx(kDoubleRegZero, kDoubleRegZero, kDoubleRegZero, Condition(0), Condition(0),
     Condition(3));
  vsum(dst, src, kDoubleRegZero, Condition(0), Condition(0), Condition(1));
}

void MacroAssembler::I16x8ExtAddPairwiseI8x16S(Simd128Register dst,
                                               Simd128Register src,
                                               Simd128Register scratch1,
                                               Simd128Register scratch2) {
  EXT_ADD_PAIRWISE(dst, src, scratch1, scratch2, 0, vme, vmo)
}

void MacroAssembler::I16x8ExtAddPairwiseI8x16U(Simd128Register dst,
                                               Simd128Register src,
                                               Simd128Register scratch1,
                                               Simd128Register scratch2) {
  EXT_ADD_PAIRWISE(dst, src, scratch1, scratch2, 0, vmle, vmlo)
}
#undef EXT_ADD_PAIRWISE

void MacroAssembler::I32x4TruncSatF64x2SZero(Simd128Register dst,
                                             Simd128Register src,
                                             Simd128Register scratch) {
  // NaN to 0.
  vfce(scratch, src, src, Condition(0), Condition(0), Condition(3));
  vn(scratch, src, scratch, Condition(0), Condition(0), Condition(0));
  vcgd(scratch, scratch, Condition(5), Condition(0), Condition(3));
  vx(dst, dst, dst, Condition(0), Condition(0), Condition(2));
  vpks(dst, dst, scratch, Condition(0), Condition(3));
}

void MacroAssembler::I32x4TruncSatF64x2UZero(Simd128Register dst,
                                             Simd128Register src,
                                             Simd128Register scratch) {
  vclgd(scratch, src, Condition(5), Condition(0), Condition(3));
  vx(dst, dst, dst, Condition(0), Condition(0), Condition(2));
  vpkls(dst, dst, scratch, Condition(0), Condition(3));
}

void MacroAssembler::S128Const(Simd128Register dst, uint64_t high, uint64_t low,
                               Register scratch1, Register scratch2) {
  mov(scratch1, Operand(low));
  mov(scratch2, Operand(high));
  vlvgp(dst, scratch2, scratch1);
}

void MacroAssembler::I8x16Swizzle(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2, Register scratch1,
                                  Register scratch2, Simd128Register scratch3) {
  DCHECK(!AreAliased(src1, src2, scratch3));
  // Saturate the indices to 5 bits. Input indices more than 31 should
  // return 0.
  vrepi(scratch3, Operand(31), Condition(0));
  vmnl(scratch3, src2, scratch3, Condition(0), Condition(0), Condition(0));
  // Input needs to be reversed.
  vlgv(scratch1, src1, MemOperand(r0, 0), Condition(3));
  vlgv(scratch2, src1, MemOperand(r0, 1), Condition(3));
  lrvgr(scratch1, scratch1);
  lrvgr(scratch2, scratch2);
  vlvgp(dst, scratch2, scratch1);
  vx(kDoubleRegZero, kDoubleRegZero, kDoubleRegZero, Condition(0), Condition(0),
     Condition(0));
  vperm(dst, dst, kDoubleRegZero, scratch3, Condition(0), Condition(0));
}

void MacroAssembler::I8x16Shuffle(Simd128Register dst, Simd128Register src1,
                                  Simd128Register src2, uint64_t high,
                                  uint64_t low, Register scratch1,
                                  Register scratch2, Simd128Register scratch3) {
  mov(scratch1, Operand(low));
  mov(scratch2, Operand(high));
  vlvgp(scratch3, scratch2, scratch1);
  vperm(dst, src1, src2, scratch3, Condition(0), Condition(0));
}

void MacroAssembler::I32x4DotI16x8S(Simd128Register dst, Simd128Register src1,
                                    Simd128Register src2,
                                    Simd128Register scratch) {
  vme(scratch, src1, src2, Condition(0), Condition(0), Condition(1));
  vmo(dst, src1, src2, Condition(0), Condition(0), Condition(1));
  va(dst, scratch, dst, Condition(0), Condition(0), Condition(2));
}

void MacroAssembler::I32x4DotI8x16AddS(
    Simd128Register dst, Simd128Register src1, Simd128Register src2,
    Simd128Register src3, Simd128Register scratch1, Simd128Register scratch2) {
  DCHECK_NE(dst, src3);
  // I8 -> I16.
  vme(scratch1, src1, src2, Condition(0), Condition(0), Condition(0));
  vmo(dst, src1, src2, Condition(0), Condition(0), Condition(0));
  va(dst, scratch1, dst, Condition(0), Condition(0), Condition(1));
  // I16 -> I32.
  vrepi(scratch2, Operand(1), Condition(1));
  vme(scratch1, dst, scratch2, Condition(0), Condition(0), Condition(1));
  vmo(dst, dst, scratch2, Condition(0), Condition(0), Condition(1));
  va(dst, scratch1, dst, Condition(0), Condition(0), Condition(2));
  // Add src3.
  va(dst, dst, src3, Condition(0), Condition(0), Condition(2));
}

void MacroAssembler::I16x8DotI8x16S(Simd128Register dst, Simd128Register src1,
                                    Simd128Register src2,
                                    Simd128Register scratch) {
  vme(scratch, src1, src2, Condition(0), Condition(0), Condition(0));
  vmo(dst, src1, src2, Condition(0), Condition(0), Condition(0));
  va(dst, scratch, dst, Condition(0), Condition(0), Condition(1));
}

#define Q15_MUL_ROAUND(accumulator, src1, src2, const_val, scratch, unpack) \
  unpack(scratch, src1, Condition(0), Condition(0), Condition(1));          \
  unpack(accumulator, src2, Condition(0), Condition(0), Condition(1));      \
  vml(accumulator, scratch, accumulator, Condition(0), Condition(0),        \
      Condition(2));                                                        \
  va(accumulator, accumulator, const_val, Condition(0), Condition(0),       \
     Condition(2));                                                         \
  vrepi(scratch, Operand(15), Condition(2));                                \
  vesrav(accumulator, accumulator, scratch, Condition(0), Condition(0),     \
         Condition(2));
void MacroAssembler::I16x8Q15MulRSatS(Simd128Register dst, Simd128Register src1,
                                      Simd128Register src2,
                                      Simd128Register scratch1,
                                      Simd128Register scratch2,
                                      Simd128Register scratch3) {
  DCHECK(!AreAliased(src1, src2, scratch1, scratch2, scratch3));
  vrepi(scratch1, Operand(0x4000), Condition(2));
  Q15_MUL_ROAUND(scratch2, src1, src2, scratch1, scratch3, vupl)
  Q15_MUL_ROAUND(dst, src1, src2, scratch1, scratch3, vuph)
  vpks(dst, dst, scratch2, Condition(0), Condition(2));
}
#undef Q15_MUL_ROAUND

// Vector LE Load and Transform instructions.
#ifdef V8_TARGET_BIG_ENDIAN
#define IS_BIG_ENDIAN true
#else
#define IS_BIG_ENDIAN false
#endif

#define CAN_LOAD_STORE_REVERSE \
  IS_BIG_ENDIAN&& CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2)

#define LOAD_SPLAT_LIST(V)       \
  V(64x2, vlbrrep, LoadU64LE, 3) \
  V(32x4, vlbrrep, LoadU32LE, 2) \
  V(16x8, vlbrrep, LoadU16LE, 1) \
  V(8x16, vlrep, LoadU8, 0)

#define LOAD_SPLAT(name, vector_instr, scalar_instr, condition)       \
  void MacroAssembler::LoadAndSplat##name##LE(                        \
      Simd128Register dst, const MemOperand& mem, Register scratch) { \
    if (CAN_LOAD_STORE_REVERSE && is_uint12(mem.offset())) {          \
      vector_instr(dst, mem, Condition(condition));                   \
      return;                                                         \
    }                                                                 \
    scalar_instr(scratch, mem);                                       \
    vlvg(dst, scratch, MemOperand(r0, 0), Condition(condition));      \
    vrep(dst, dst, Operand(0), Condition(condition));                 \
  }
LOAD_SPLAT_LIST(LOAD_SPLAT)
#undef LOAD_SPLAT
#undef LOAD_SPLAT_LIST

#define LOAD_EXTEND_LIST(V) \
  V(32x2U, vuplh, 2)        \
  V(32x2S, vuph, 2)         \
  V(16x4U, vuplh, 1)        \
  V(16x4S, vuph, 1)         \
  V(8x8U, vuplh, 0)         \
  V(8x8S, vuph, 0)

#define LOAD_EXTEND(name, unpack_instr, condition)                            \
  void MacroAssembler::LoadAndExtend##name##LE(                               \
      Simd128Register dst, const MemOperand& mem, Register scratch) {         \
    if (CAN_LOAD_STORE_REVERSE && is_uint12(mem.offset())) {                  \
      vlebrg(dst, mem, Condition(0));                                         \
    } else {                                                                  \
      LoadU64LE(scratch, mem);                                                \
      vlvg(dst, scratch, MemOperand(r0, 0), Condition(3));                    \
    }                                                                         \
    unpack_instr(dst, dst, Condition(0), Condition(0), Condition(condition)); \
  }
LOAD_EXTEND_LIST(LOAD_EXTEND)
#undef LOAD_EXTEND
#undef LOAD_EXTEND

void MacroAssembler::LoadV32ZeroLE(Simd128Register dst, const MemOperand& mem,
                                   Register scratch) {
  vx(dst, dst, dst, Condition(0), Condition(0), Condition(0));
  if (CAN_LOAD_STORE_REVERSE && is_uint12(mem.offset())) {
    vlebrf(dst, mem, Condition(3));
    return;
  }
  LoadU32LE(scratch, mem);
  vlvg(dst, scratch, MemOperand(r0, 3), Condition(2));
}

void MacroAssembler::LoadV64ZeroLE(Simd128Register dst, const MemOperand& mem,
                                   Register scratch) {
  vx(dst, dst, dst, Condition(0), Condition(0), Condition(0));
  if (CAN_LOAD_STORE_REVERSE && is_uint12(mem.offset())) {
    vlebrg(dst, mem, Condition(1));
    return;
  }
  LoadU64LE(scratch, mem);
  vlvg(dst, scratch, MemOperand(r0, 1), Condition(3));
}

#define LOAD_LANE_LIST(V)     \
  V(64, vlebrg, LoadU64LE, 3) \
  V(32, vlebrf, LoadU32LE, 2) \
  V(16, vlebrh, LoadU16LE, 1) \
  V(8, vleb, LoadU8, 0)

#define LOAD_LANE(name, vector_instr, scalar_instr, condition)             \
  void MacroAssembler::LoadLane##name##LE(Simd128Register dst,             \
                                          const MemOperand& mem, int lane, \
                                          Register scratch) {              \
    if (CAN_LOAD_STORE_REVERSE && is_uint12(mem.offset())) {               \
      vector_instr(dst, mem, Condition(lane));                             \
      return;                                                              \
    }                                                                      \
    scalar_instr(scratch, mem);                                            \
    vlvg(dst, scratch, MemOperand(r0, lane), Condition(condition));        \
  }
LOAD_LANE_LIST(LOAD_LANE)
#undef LOAD_LANE
#undef LOAD_LANE_LIST

#define STORE_LANE_LIST(V)      \
  V(64, vstebrg, StoreU64LE, 3) \
  V(32, vstebrf, StoreU32LE, 2) \
  V(16, vstebrh, StoreU16LE, 1) \
  V(8, vsteb, StoreU8, 0)

#define STORE_LANE(name, vector_instr, scalar_instr, condition)             \
  void MacroAssembler::StoreLane##name##LE(Simd128Register src,             \
                                           const MemOperand& mem, int lane, \
                                           Register scratch) {              \
    if (CAN_LOAD_STORE_REVERSE && is_uint12(mem.offset())) {                \
      vector_instr(src, mem, Condition(lane));                              \
      return;                                                               \
    }                                                                       \
    vlgv(scratch, src, MemOperand(r0, lane), Condition(condition));         \
    scalar_instr(scratch, mem);                                             \
  }
STORE_LANE_LIST(STORE_LANE)
#undef STORE_LANE
#undef STORE_LANE_LIST
#undef CAN_LOAD_STORE_REVERSE
#undef IS_BIG_ENDIAN

void MacroAssembler::LoadStackLimit(Register destination, StackLimitKind kind) {
  ASM_CODE_COMMENT(this);
  DCHECK(root_array_available());
  intptr_t offset = kind == StackLimitKind::kRealStackLimit
                        ? IsolateData::real_jslimit_offset()
                        : IsolateData::jslimit_offset();
  CHECK(is_int32(offset));
  LoadU64(destination, MemOperand(kRootRegister, offset));
}

void MacroAssembler::Switch(Register scratch, Register value,
                            int case_value_base, Label** labels,
                            int num_labels) {
  Label fallthrough, jump_table;
  if (case_value_base != 0) {
    SubS64(value, value, Operand(case_value_base));
  }
  CmpU64(value, Operand(num_labels));
  bge(&fallthrough);

  int entry_size_log2 = 3;
  ShiftLeftU32(value, value, Operand(entry_size_log2));
  larl(r1, &jump_table);
  lay(r1, MemOperand(value, r1));
  b(r1);

  bind(&jump_table);
  for (int i = 0; i < num_labels; ++i) {
    b(labels[i]);
    dh(0);
  }
  bind(&fallthrough);
}

void MacroAssembler::JumpIfCodeIsMarkedForDeoptimization(
    Register code, Register scratch, Label* if_marked_for_deoptimization) {
  TestCodeIsMarkedForDeoptimization(code, scratch);
  bne(if_marked_for_deoptimization);
}

void MacroAssembler::JumpIfCodeIsTurbofanned(Register code, Register scratch,
                                             Label* if_turbofanned) {
  LoadU32(scratch, FieldMemOperand(code, Code::kFlagsOffset));
  TestBit(scratch, Code::kIsTurbofannedBit, scratch);
  bne(if_turbofanned);
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
    // The entry references a CodeWrapper object. Unwrap it now.
    LoadTaggedField(
        scratch_and_result,
        FieldMemOperand(scratch_and_result, CodeWrapper::kCodeOffset));

    UseScratchRegisterScope temps(this);
    Register temp = temps.Acquire();
    JumpIfCodeIsMarkedForDeoptimization(scratch_and_result, temp, &clear_slot);
    if (min_opt_level == CodeKind::TURBOFAN) {
      JumpIfCodeIsTurbofanned(scratch_and_result, temp, on_result);
      b(&fallthrough);
    } else {
      b(on_result);
    }
  }

  bind(&clear_slot);
  mov(scratch_and_result, ClearedValue());
  StoreTaggedField(
      scratch_and_result,
      FieldMemOperand(feedback_vector,
                      FeedbackVector::OffsetOfElementAt(slot.ToInt())));

  bind(&fallthrough);
  mov(scratch_and_result, Operand::Zero());
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

  Register return_value = r2;
#if V8_OS_ZOS
  Register scratch = r6;
#else
  Register scratch = ip;
#endif
  Register scratch2 = r1;

  // Allocate HandleScope in callee-saved registers.
  // We will need to restore the HandleScope after the call to the API function,
  // by allocating it in callee-saved registers it'll be preserved by C code.
#if V8_OS_ZOS
  Register prev_next_address_reg = r14;
#else
  Register prev_next_address_reg = r6;
#endif
  Register prev_limit_reg = r7;
  Register prev_level_reg = r8;

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
    __ LoadU32(prev_level_reg, level_mem_op);
    __ AddS64(scratch, prev_level_reg, Operand(1));
    __ StoreU32(scratch, level_mem_op);
  }

  Label profiler_or_side_effects_check_enabled, done_api_call;
  if (with_profiling) {
    __ RecordComment("Check if profiler or side effects check is enabled");
    __ LoadU8(scratch,
              __ ExternalReferenceAsOperand(IsolateFieldId::kExecutionMode));
    __ CmpS64(scratch, Operand::Zero());
    __ bne(&profiler_or_side_effects_check_enabled, Label::kNear);
#ifdef V8_RUNTIME_CALL_STATS
    __ RecordComment("Check if RCS is enabled");
    __ Move(scratch, ER::address_of_runtime_stats_flag());
    __ LoadU32(scratch, MemOperand(scratch, 0));
    __ CmpS64(scratch, Operand::Zero());
    __ bne(&profiler_or_side_effects_check_enabled, Label::kNear);
#endif  // V8_RUNTIME_CALL_STATS
  }

  __ RecordComment("Call the api function directly.");
#if V8_OS_ZOS
  __ mov(scratch, function_address);
  __ zosStoreReturnAddressAndCall(function_address, scratch);
#else
  __ StoreReturnAddressAndCall(function_address);
#endif
  __ bind(&done_api_call);

  Label propagate_exception;
  Label delete_allocated_handles;
  Label leave_exit_frame;

  __ RecordComment("Load the value from ReturnValue");
  __ LoadU64(r2, return_value_operand);

  {
    ASM_CODE_COMMENT_STRING(
        masm,
        "No more valid handles (the result handle was the last one)."
        "Restore previous handle scope.");
    __ StoreU64(prev_next_address_reg, next_mem_op);
    if (v8_flags.debug_code) {
      __ LoadU32(scratch, level_mem_op);
      __ SubS64(scratch, Operand(1));
      __ CmpS64(scratch, prev_level_reg);
      __ Check(eq, AbortReason::kUnexpectedLevelAfterReturnFromApiCall);
    }
    __ StoreU32(prev_level_reg, level_mem_op);
    __ CmpS64(prev_limit_reg, limit_mem_op);
    __ bne(&delete_allocated_handles, Label::kNear);
  }

  __ RecordComment("Leave the API exit frame.");
  __ bind(&leave_exit_frame);
  Register argc_reg = prev_limit_reg;
  if (argc_operand != nullptr) {
    // Load the number of stack slots to drop before LeaveExitFrame modifies sp.
    __ LoadU64(argc_reg, *argc_operand);
  }
  __ LeaveExitFrame(scratch);

  // Check if the function scheduled an exception.
  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Check if the function scheduled an exception.");
    __ LoadU64(scratch2, __ ExternalReferenceAsOperand(
                             ER::exception_address(isolate), no_reg));
    __ CompareRoot(scratch2, RootIndex::kTheHoleValue);
    __ bne(&propagate_exception, Label::kNear);
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

  __ b(r14);

  if (with_profiling) {
    ASM_CODE_COMMENT_STRING(masm, "Call the api function via thunk wrapper.");
    __ bind(&profiler_or_side_effects_check_enabled);
    // Additional parameter is the address of the actual callback function.
    if (thunk_arg.is_valid()) {
      MemOperand thunk_arg_mem_op = __ ExternalReferenceAsOperand(
          IsolateFieldId::kApiCallbackThunkArgument);
      __ StoreU64(thunk_arg, thunk_arg_mem_op);
    }
    __ Move(scratch, thunk_ref);
#if V8_OS_ZOS
    __ zosStoreReturnAddressAndCall(function_address, scratch);
#else
    __ StoreReturnAddressAndCall(scratch);
#endif
    __ b(&done_api_call);
  }

  __ RecordComment("An exception was thrown. Propagate it.");
  __ bind(&propagate_exception);
  __ TailCallRuntime(Runtime::kPropagateException);

  // HandleScope limit has changed. Delete allocated extensions.
  {
    ASM_CODE_COMMENT_STRING(
        masm, "HandleScope limit has changed. Delete allocated extensions.");
    __ bind(&delete_allocated_handles);
    __ StoreU64(prev_limit_reg, limit_mem_op);
    // Save the return value in a callee-save register.
    Register saved_result = prev_limit_reg;
    __ mov(saved_result, return_value);
    __ PrepareCallCFunction(1, scratch);
    __ Move(kCArgRegs[0], ER::isolate_address());
    __ CallCFunction(ER::delete_handle_scope_extensions(), 1);
    __ mov(return_value, saved_result);
    __ b(&leave_exit_frame, Label::kNear);
  }
}

}  // namespace internal
}  // namespace v8

#undef __

#endif  // V8_TARGET_ARCH_S390
