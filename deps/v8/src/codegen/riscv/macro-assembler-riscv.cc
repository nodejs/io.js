// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>  // For LONG_MIN, LONG_MAX.

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
#include "src/wasm/wasm-code-manager.h"

// Satisfy cpplint check, but don't include platform-specific header. It is
// included recursively via macro-assembler.h.
#if 0
#include "src/codegen/riscv/macro-assembler-riscv.h"
#endif

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
  bytes += list.Count() * kSystemPointerSize;

  if (fp_mode == SaveFPRegsMode::kSave) {
    bytes += kCallerSavedFPU.Count() * kDoubleSize;
  }

  return bytes;
}

int MacroAssembler::PushCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1,
                                    Register exclusion2, Register exclusion3) {
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

#define __ ACCESS_MASM(masm)
namespace {
static void TailCallOptimizedCodeSlot(MacroAssembler* masm,
                                      Register optimized_code_entry,
                                      Register scratch1, Register scratch2) {
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
  __ LoadCodePointerField(
      optimized_code_entry,
      FieldMemOperand(optimized_code_entry, CodeWrapper::kCodeOffset));

  // Check if the optimized code is marked for deopt. If it is, call the
  // runtime to clear it.
  __ JumpIfCodeIsMarkedForDeoptimization(optimized_code_entry, scratch1,
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
void MacroAssembler::AssertUnreachable(AbortReason reason) {
  if (v8_flags.debug_code) Abort(reason);
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
    // Use the return value before restoring a0
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
  DCHECK(!AreAliased(flags, feedback_vector));
  DCHECK(CodeKindCanTierUp(current_code_kind));
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
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
  DCHECK(!AreAliased(flags, feedback_vector));
  UseScratchRegisterScope temps(this);
  temps.Include(t0, t1);
  Label maybe_has_optimized_code, maybe_needs_logging;
  // Check if optimized code is available.
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    And(scratch, flags,
        Operand(FeedbackVector::kFlagsTieringStateIsAnyRequested));
    Branch(&maybe_needs_logging, eq, scratch, Operand(zero_reg),
           Label::Distance::kNear);
  }
  GenerateTailCallToReturnedCode(Runtime::kCompileOptimized);

  bind(&maybe_needs_logging);
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    And(scratch, flags, Operand(FeedbackVector::LogNextExecutionBit::kMask));
    Branch(&maybe_has_optimized_code, eq, scratch, Operand(zero_reg),
           Label::Distance::kNear);
  }

  GenerateTailCallToReturnedCode(Runtime::kFunctionLogNextExecution);

  bind(&maybe_has_optimized_code);
  Register optimized_code_entry = flags;
  LoadTaggedField(optimized_code_entry,
                  FieldMemOperand(feedback_vector,
                                  FeedbackVector::kMaybeOptimizedCodeOffset));
  TailCallOptimizedCodeSlot(this, optimized_code_entry, temps.Acquire(),
                            temps.Acquire());
}

void MacroAssembler::LoadIsolateField(const Register& rd, IsolateFieldId id) {
  li(rd, ExternalReference::Create(id));
}

void MacroAssembler::LoadRoot(Register destination, RootIndex index) {
#if V8_TARGET_ARCH_RISCV64
  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index) &&
      is_int12(ReadOnlyRootPtr(index))) {
    DecompressTagged(destination, ReadOnlyRootPtr(index));
    return;
  }
#endif
  // Many roots have addresses that are too large to fit into addition immediate
  // operands. Evidence suggests that the extra instruction for decompression
  // costs us more than the load.
  LoadWord(destination,
           MemOperand(kRootRegister, RootRegisterOffsetForRootIndex(index)));
}

void MacroAssembler::LoadTaggedRoot(Register destination, RootIndex index) {
  if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index) &&
      is_int12(ReadOnlyRootPtr(index))) {
    li(destination, (int32_t)ReadOnlyRootPtr(index));
    return;
  }
  LoadWord(destination,
           MemOperand(kRootRegister, RootRegisterOffsetForRootIndex(index)));
}
void MacroAssembler::PushCommonFrame(Register marker_reg) {
  if (marker_reg.is_valid()) {
    Push(ra, fp, marker_reg);
    AddWord(fp, sp, Operand(kSystemPointerSize));
  } else {
    Push(ra, fp);
    Mv(fp, sp);
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
  AddWord(fp, sp, Operand(offset));
}

int MacroAssembler::SafepointRegisterStackIndex(int reg_code) {
  // The registers are pushed starting with the highest encoding,
  // which means that lowest encodings are closest to the stack pointer.
  return kSafepointRegisterStackIndexMap[reg_code];
}

// Clobbers object, dst, value, and ra, if (ra_status == kRAHasBeenSaved)
// The register 'object' contains a heap object pointer.  The heap object
// tag is shifted away.
void MacroAssembler::RecordWriteField(Register object, int offset,
                                      Register value, RAStatus ra_status,
                                      SaveFPRegsMode save_fp,
                                      SmiCheck smi_check, SlotDescriptor slot) {
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
    Label ok;
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    DCHECK(!AreAliased(object, value, scratch));
    AddWord(scratch, object, offset - kHeapObjectTag);
    And(scratch, scratch, Operand(kTaggedSize - 1));
    BranchShort(&ok, eq, scratch, Operand(zero_reg));
    Abort(AbortReason::kUnalignedCellInWriteBarrier);
    bind(&ok);
  }

  RecordWrite(object, Operand(offset - kHeapObjectTag), value, ra_status,
              save_fp, SmiCheck::kOmit, slot);

  bind(&done);
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

#ifdef V8_ENABLE_SANDBOX
void MacroAssembler::ResolveIndirectPointerHandle(Register destination,
                                                  Register handle,
                                                  IndirectPointerTag tag) {
  ASM_CODE_COMMENT(this);
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
  ASM_CODE_COMMENT(this);
  DCHECK_NE(tag, kCodeIndirectPointerTag);
  DCHECK(!AreAliased(handle, destination));

  Register table = destination;
  DCHECK(root_array_available_);
  LoadWord(table, MemOperand{kRootRegister,
                             IsolateData::trusted_pointer_table_offset()});
  SrlWord(handle, handle, kTrustedPointerHandleShift);
  CalcScaledAddress(destination, table, handle,
                    kTrustedPointerTableEntrySizeLog2);
  LoadWord(destination, MemOperand(destination, 0));
  // The LSB is used as marking bit by the trusted pointer table, so here we
  // have to set it using a bitwise OR as it may or may not be set.
  // Untag the pointer and remove the marking bit in one operation.
  Register tag_reg = handle;
  li(tag_reg, Operand(~(tag | kTrustedPointerTableMarkBit)));
  and_(destination, destination, tag_reg);
}

void MacroAssembler::ResolveCodePointerHandle(Register destination,
                                              Register handle) {
  ASM_CODE_COMMENT(this);
  DCHECK(!AreAliased(handle, destination));

  Register table = destination;
  li(table, ExternalReference::code_pointer_table_address());
  SrlWord(handle, handle, kCodePointerHandleShift);
  CalcScaledAddress(destination, table, handle, kCodePointerTableEntrySizeLog2);
  LoadWord(destination,
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
  Lwu(destination, field_operand);
  SrlWord(destination, destination, kCodePointerHandleShift);
  SllWord(destination, destination, kCodePointerTableEntrySizeLog2);
  AddWord(scratch, scratch, destination);
  LoadWord(destination, MemOperand(scratch, 0));
  if (tag != 0) {
    li(scratch, Operand(tag));
    xor_(destination, destination, scratch);
  }
}
#endif  // V8_ENABLE_SANDBOX

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
  LoadWord(external_table,
           MemOperand(isolate_root,
                      IsolateData::external_pointer_table_offset() +
                          Internals::kExternalPointerTableBasePointerOffset));
  Lwu(destination, field_operand);
  srli(destination, destination, kExternalPointerIndexShift);
  slli(destination, destination, kExternalPointerTableEntrySizeLog2);
  AddWord(external_table, external_table, destination);
  LoadWord(destination, MemOperand(external_table, 0));
  temps.Include(external_table);
  external_table = no_reg;
  And(destination, destination, Operand(~tag));
#else
  LoadWord(destination, field_operand);
#endif  // V8_ENABLE_SANDBOX
}

#ifdef V8_TARGET_ARCH_RISCV64
void MacroAssembler::LoadIndirectPointerField(Register destination,
                                              MemOperand field_operand,
                                              IndirectPointerTag tag) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register handle = t6;
  DCHECK_NE(handle, destination);
  Lwu(handle, field_operand);

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
  Lw(scratch,
     FieldMemOperand(value, ExposedTrustedObject::kSelfIndirectPointerOffset));
  Sw(scratch, dst_field_operand);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}
#endif  // V8_TARGET_ARCH_RISCV64

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
  if (mode == StubCallMode::kCallWasmRuntimeStub) {
    auto wasm_target =
        static_cast<Address>(wasm::WasmCode::GetRecordWriteBuiltin(fp_mode));
    Call(wasm_target, RelocInfo::WASM_STUB_CALL);
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
    AddWord(dst_slot, object, offset);
    mv(dst_object, object);
    return;
  }

  DCHECK_EQ(dst_slot, object);

  // If the destination object register does not overlap with the offset
  // register, we can overwrite it.
  if (offset.IsImmediate() || (offset.rm() != dst_object)) {
    mv(dst_object, dst_slot);
    AddWord(dst_slot, dst_slot, offset);
    return;
  }

  DCHECK_EQ(dst_object, offset.rm());

  // We only have `dst_slot` and `dst_object` left as distinct registers so we
  // have to swap them. We write this as a add+sub sequence to avoid using a
  // scratch register.
  AddWord(dst_slot, dst_slot, dst_object);
  SubWord(dst_object, dst_slot, dst_object);
}

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
    Register temp = temps.Acquire();
    DCHECK(!AreAliased(object, value, temp));
    AddWord(temp, object, offset);
#ifdef V8_TARGET_ARCH_RISCV64
    if (slot.contains_indirect_pointer()) {
      LoadIndirectPointerField(temp, MemOperand(temp, 0),
                               slot.indirect_pointer_tag());
    } else {
      DCHECK(slot.contains_direct_pointer());
      LoadTaggedField(temp, MemOperand(temp, 0));
    }
#else
    LoadTaggedField(temp, MemOperand(temp));
#endif
    Assert(eq, AbortReason::kWrongAddressOrValuePassedToRecordWrite, temp,
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

  {
    UseScratchRegisterScope temps(this);
    CheckPageFlag(value, MemoryChunk::kPointersToHereAreInterestingMask,
                  eq,  // In RISC-V, it uses cc for a comparison with 0, so if
                       // no bits are set, and cc is eq, it will branch to done
                  &done);

    CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask,
                  eq,  // In RISC-V, it uses cc for a comparison with 0, so if
                       // no bits are set, and cc is eq, it will branch to done
                  &done);
  }
  // Record the actual write.
  if (ra_status == kRAHasNotBeenSaved) {
    push(ra);
  }
  Register slot_address = WriteBarrierDescriptor::SlotAddressRegister();
  DCHECK(!AreAliased(object, slot_address, value));
  // TODO(cbruni): Turn offset into int.
  if (slot.contains_direct_pointer()) {
    DCHECK(offset.IsImmediate());
    AddWord(slot_address, object, offset);
    CallRecordWriteStub(object, slot_address, fp_mode,
                        StubCallMode::kCallBuiltinPointer);
  } else {
    DCHECK(slot.contains_indirect_pointer());
    CallIndirectPointerBarrier(object, offset, fp_mode,
                               slot.indirect_pointer_tag());
  }
  if (ra_status == kRAHasNotBeenSaved) {
    pop(ra);
  }
  if (v8_flags.debug_code) li(slot_address, Operand(kZapValue));

  bind(&done);
}

// ---------------------------------------------------------------------------
// Instruction macros.
#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::DecodeSandboxedPointer(Register value) {
  ASM_CODE_COMMENT(this);
#ifdef V8_ENABLE_SANDBOX
  srli(value, value, kSandboxedPointerShift);
  AddWord(value, value, kPtrComprCageBaseRegister);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::LoadSandboxedPointerField(
    Register destination, const MemOperand& field_operand) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  LoadWord(destination, field_operand);
  DecodeSandboxedPointer(destination);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::StoreSandboxedPointerField(
    Register value, const MemOperand& dst_field_operand) {
#ifdef V8_ENABLE_SANDBOX
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  SubWord(scratch, value, kPtrComprCageBaseRegister);
  slli(scratch, scratch, kSandboxedPointerShift);
  StoreWord(scratch, dst_field_operand);
#else
  UNREACHABLE();
#endif
}

void MacroAssembler::Add32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        ((rd.code() & 0b11000) == 0b01000) &&
        ((rt.rm().code() & 0b11000) == 0b01000)) {
      c_addw(rd, rt.rm());
    } else {
      addw(rd, rs, rt.rm());
    }
  } else {
    if (v8_flags.riscv_c_extension && is_int6(rt.immediate()) &&
        (rd.code() == rs.code()) && (rd != zero_reg) &&
        !MustUseReg(rt.rmode())) {
      c_addiw(rd, static_cast<int8_t>(rt.immediate()));
    } else if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      addiw(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else if ((-4096 <= rt.immediate() && rt.immediate() <= -2049) ||
               (2048 <= rt.immediate() && rt.immediate() <= 4094)) {
      addiw(rd, rs, rt.immediate() / 2);
      addiw(rd, rd, rt.immediate() - (rt.immediate() / 2));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      Li(scratch, rt.immediate());
      addw(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Sub32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        ((rd.code() & 0b11000) == 0b01000) &&
        ((rt.rm().code() & 0b11000) == 0b01000)) {
      c_subw(rd, rt.rm());
    } else {
      subw(rd, rs, rt.rm());
    }
  } else {
    DCHECK(is_int32(rt.immediate()));
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        (rd != zero_reg) && is_int6(-rt.immediate()) &&
        !MustUseReg(rt.rmode())) {
      c_addiw(
          rd,
          static_cast<int8_t>(
              -rt.immediate()));  // No c_subiw instr, use c_addiw(x, y, -imm).
    } else if (is_int12(-rt.immediate()) && !MustUseReg(rt.rmode())) {
      addiw(rd, rs,
            static_cast<int32_t>(
                -rt.immediate()));  // No subiw instr, use addiw(x, y, -imm).
    } else if ((-4096 <= -rt.immediate() && -rt.immediate() <= -2049) ||
               (2048 <= -rt.immediate() && -rt.immediate() <= 4094)) {
      addiw(rd, rs, -rt.immediate() / 2);
      addiw(rd, rd, -rt.immediate() - (-rt.immediate() / 2));
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      if (-rt.immediate() >> 12 == 0 && !MustUseReg(rt.rmode())) {
        // Use load -imm and addu when loading -imm generates one instruction.
        Li(scratch, -rt.immediate());
        addw(rd, rs, scratch);
      } else {
        // li handles the relocation.
        Li(scratch, rt.immediate());
        subw(rd, rs, scratch);
      }
    }
  }
}

void MacroAssembler::AddWord(Register rd, Register rs, const Operand& rt) {
  Add64(rd, rs, rt);
}

void MacroAssembler::SubWord(Register rd, Register rs, const Operand& rt) {
  Sub64(rd, rs, rt);
}

void MacroAssembler::Sub64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        ((rd.code() & 0b11000) == 0b01000) &&
        ((rt.rm().code() & 0b11000) == 0b01000)) {
      c_sub(rd, rt.rm());
    } else {
      sub(rd, rs, rt.rm());
    }
  } else if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
             (rd != zero_reg) && is_int6(-rt.immediate()) &&
             (rt.immediate() != 0) && !MustUseReg(rt.rmode())) {
    c_addi(rd,
           static_cast<int8_t>(
               -rt.immediate()));  // No c_subi instr, use c_addi(x, y, -imm).

  } else if (v8_flags.riscv_c_extension && is_int10(-rt.immediate()) &&
             (rt.immediate() != 0) && ((rt.immediate() & 0xf) == 0) &&
             (rd.code() == rs.code()) && (rd == sp) &&
             !MustUseReg(rt.rmode())) {
    c_addi16sp(static_cast<int16_t>(-rt.immediate()));
  } else if (is_int12(-rt.immediate()) && !MustUseReg(rt.rmode())) {
    addi(rd, rs,
         static_cast<int32_t>(
             -rt.immediate()));  // No subi instr, use addi(x, y, -imm).
  } else if ((-4096 <= -rt.immediate() && -rt.immediate() <= -2049) ||
             (2048 <= -rt.immediate() && -rt.immediate() <= 4094)) {
    addi(rd, rs, -rt.immediate() / 2);
    addi(rd, rd, -rt.immediate() - (-rt.immediate() / 2));
  } else {
    int li_count = InstrCountForLi64Bit(rt.immediate());
    int li_neg_count = InstrCountForLi64Bit(-rt.immediate());
    if (li_neg_count < li_count && !MustUseReg(rt.rmode())) {
      // Use load -imm and add when loading -imm generates one instruction.
      DCHECK(rt.immediate() != std::numeric_limits<int32_t>::min());
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, Operand(-rt.immediate()));
      add(rd, rs, scratch);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, rt);
      sub(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Add64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        (rt.rm() != zero_reg) && (rs != zero_reg)) {
      c_add(rd, rt.rm());
    } else {
      add(rd, rs, rt.rm());
    }
  } else {
    if (v8_flags.riscv_c_extension && is_int6(rt.immediate()) &&
        (rd.code() == rs.code()) && (rd != zero_reg) && (rt.immediate() != 0) &&
        !MustUseReg(rt.rmode())) {
      c_addi(rd, static_cast<int8_t>(rt.immediate()));
    } else if (v8_flags.riscv_c_extension && is_int10(rt.immediate()) &&
               (rt.immediate() != 0) && ((rt.immediate() & 0xf) == 0) &&
               (rd.code() == rs.code()) && (rd == sp) &&
               !MustUseReg(rt.rmode())) {
      c_addi16sp(static_cast<int16_t>(rt.immediate()));
    } else if (v8_flags.riscv_c_extension &&
               ((rd.code() & 0b11000) == 0b01000) && (rs == sp) &&
               is_uint10(rt.immediate()) && (rt.immediate() != 0) &&
               !MustUseReg(rt.rmode())) {
      c_addi4spn(rd, static_cast<uint16_t>(rt.immediate()));
    } else if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      addi(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else if ((-4096 <= rt.immediate() && rt.immediate() <= -2049) ||
               (2048 <= rt.immediate() && rt.immediate() <= 4094)) {
      addi(rd, rs, rt.immediate() / 2);
      addi(rd, rd, rt.immediate() - (rt.immediate() / 2));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      BlockTrampolinePoolScope block_trampoline_pool(this);
      li(scratch, rt);
      add(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Mul32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mulw(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    mulw(rd, rs, scratch);
  }
}

void MacroAssembler::Mulh32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mul(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    mul(rd, rs, scratch);
  }
  srai(rd, rd, 32);
}

void MacroAssembler::Mulhu32(Register rd, Register rs, const Operand& rt,
                             Register rsz, Register rtz) {
  slli(rsz, rs, 32);
  if (rt.is_reg()) {
    slli(rtz, rt.rm(), 32);
  } else {
    Li(rtz, rt.immediate() << 32);
  }
  mulhu(rd, rsz, rtz);
  srai(rd, rd, 32);
}

void MacroAssembler::Mul64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mul(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    mul(rd, rs, scratch);
  }
}

void MacroAssembler::Mulh64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mulh(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    mulh(rd, rs, scratch);
  }
}

void MacroAssembler::Mulhu64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mulhu(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    mulhu(rd, rs, scratch);
  }
}

void MacroAssembler::Div32(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    divw(res, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    divw(res, rs, scratch);
  }
}

void MacroAssembler::Mod32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    remw(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    remw(rd, rs, scratch);
  }
}

void MacroAssembler::Modu32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    remuw(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    remuw(rd, rs, scratch);
  }
}

void MacroAssembler::Div64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    div(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    div(rd, rs, scratch);
  }
}

void MacroAssembler::Divu32(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    divuw(res, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    divuw(res, rs, scratch);
  }
}

void MacroAssembler::Divu64(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    divu(res, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    divu(res, rs, scratch);
  }
}

void MacroAssembler::Mod64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    rem(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    rem(rd, rs, scratch);
  }
}

void MacroAssembler::Modu64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    remu(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    remu(rd, rs, scratch);
  }
}
#elif V8_TARGET_ARCH_RISCV32
void MacroAssembler::AddWord(Register rd, Register rs, const Operand& rt) {
  Add32(rd, rs, rt);
}

void MacroAssembler::Add32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        (rt.rm() != zero_reg) && (rs != zero_reg)) {
      c_add(rd, rt.rm());
    } else {
      add(rd, rs, rt.rm());
    }
  } else {
    if (v8_flags.riscv_c_extension && is_int6(rt.immediate()) &&
        (rd.code() == rs.code()) && (rd != zero_reg) && (rt.immediate() != 0) &&
        !MustUseReg(rt.rmode())) {
      c_addi(rd, static_cast<int8_t>(rt.immediate()));
    } else if (v8_flags.riscv_c_extension && is_int10(rt.immediate()) &&
               (rt.immediate() != 0) && ((rt.immediate() & 0xf) == 0) &&
               (rd.code() == rs.code()) && (rd == sp) &&
               !MustUseReg(rt.rmode())) {
      c_addi16sp(static_cast<int16_t>(rt.immediate()));
    } else if (v8_flags.riscv_c_extension &&
               ((rd.code() & 0b11000) == 0b01000) && (rs == sp) &&
               is_uint10(rt.immediate()) && (rt.immediate() != 0) &&
               !MustUseReg(rt.rmode())) {
      c_addi4spn(rd, static_cast<uint16_t>(rt.immediate()));
    } else if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      addi(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else if ((-4096 <= rt.immediate() && rt.immediate() <= -2049) ||
               (2048 <= rt.immediate() && rt.immediate() <= 4094)) {
      addi(rd, rs, rt.immediate() / 2);
      addi(rd, rd, rt.immediate() - (rt.immediate() / 2));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      BlockTrampolinePoolScope block_trampoline_pool(this);
      li(scratch, rt);
      add(rd, rs, scratch);
    }
  }
}

void MacroAssembler::SubWord(Register rd, Register rs, const Operand& rt) {
  Sub32(rd, rs, rt);
}

void MacroAssembler::Sub32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        ((rd.code() & 0b11000) == 0b01000) &&
        ((rt.rm().code() & 0b11000) == 0b01000)) {
      c_sub(rd, rt.rm());
    } else {
      sub(rd, rs, rt.rm());
    }
  } else if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
             (rd != zero_reg) && is_int6(-rt.immediate()) &&
             (rt.immediate() != 0) && !MustUseReg(rt.rmode())) {
    c_addi(rd,
           static_cast<int8_t>(
               -rt.immediate()));  // No c_subi instr, use c_addi(x, y, -imm).

  } else if (v8_flags.riscv_c_extension && is_int10(-rt.immediate()) &&
             (rt.immediate() != 0) && ((rt.immediate() & 0xf) == 0) &&
             (rd.code() == rs.code()) && (rd == sp) &&
             !MustUseReg(rt.rmode())) {
    c_addi16sp(static_cast<int16_t>(-rt.immediate()));
  } else if (is_int12(-rt.immediate()) && !MustUseReg(rt.rmode())) {
    addi(rd, rs,
         static_cast<int32_t>(
             -rt.immediate()));  // No subi instr, use addi(x, y, -imm).
  } else if ((-4096 <= -rt.immediate() && -rt.immediate() <= -2049) ||
             (2048 <= -rt.immediate() && -rt.immediate() <= 4094)) {
    addi(rd, rs, -rt.immediate() / 2);
    addi(rd, rd, -rt.immediate() - (-rt.immediate() / 2));
  } else {
    // RV32G todo: imm64 or imm32 here
    int li_count = InstrCountForLi64Bit(rt.immediate());
    int li_neg_count = InstrCountForLi64Bit(-rt.immediate());
    if (li_neg_count < li_count && !MustUseReg(rt.rmode())) {
      // Use load -imm and add when loading -imm generates one instruction.
      DCHECK(rt.immediate() != std::numeric_limits<int32_t>::min());
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, Operand(-rt.immediate()));
      add(rd, rs, scratch);
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, rt);
      sub(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Mul32(Register rd, Register rs, const Operand& rt) {
  Mul(rd, rs, rt);
}

void MacroAssembler::Mul(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mul(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    mul(rd, rs, scratch);
  }
}

void MacroAssembler::Mulh(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    mulh(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    mulh(rd, rs, scratch);
  }
}

void MacroAssembler::Mulhu(Register rd, Register rs, const Operand& rt,
                           Register rsz, Register rtz) {
  if (rt.is_reg()) {
    mulhu(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    mulhu(rd, rs, scratch);
  }
}

void MacroAssembler::Div(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    div(res, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    div(res, rs, scratch);
  }
}

void MacroAssembler::Mod(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    rem(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    rem(rd, rs, scratch);
  }
}

void MacroAssembler::Modu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    remu(rd, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    remu(rd, rs, scratch);
  }
}

void MacroAssembler::Divu(Register res, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    divu(res, rs, rt.rm());
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Li(scratch, rt.immediate());
    divu(res, rs, scratch);
  }
}

#endif

void MacroAssembler::And(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        ((rd.code() & 0b11000) == 0b01000) &&
        ((rt.rm().code() & 0b11000) == 0b01000)) {
      c_and(rd, rt.rm());
    } else {
      and_(rd, rs, rt.rm());
    }
  } else {
    if (v8_flags.riscv_c_extension && is_int6(rt.immediate()) &&
        !MustUseReg(rt.rmode()) && (rd.code() == rs.code()) &&
        ((rd.code() & 0b11000) == 0b01000)) {
      c_andi(rd, static_cast<int8_t>(rt.immediate()));
    } else if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      andi(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      Li(scratch, rt.immediate());
      and_(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Or(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        ((rd.code() & 0b11000) == 0b01000) &&
        ((rt.rm().code() & 0b11000) == 0b01000)) {
      c_or(rd, rt.rm());
    } else {
      or_(rd, rs, rt.rm());
    }
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      ori(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      Li(scratch, rt.immediate());
      or_(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Xor(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        ((rd.code() & 0b11000) == 0b01000) &&
        ((rt.rm().code() & 0b11000) == 0b01000)) {
      c_xor(rd, rt.rm());
    } else {
      xor_(rd, rs, rt.rm());
    }
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      xori(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      Li(scratch, rt.immediate());
      xor_(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Nor(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    or_(rd, rs, rt.rm());
    not_(rd, rd);
  } else {
    Or(rd, rs, rt);
    not_(rd, rd);
  }
}

void MacroAssembler::Neg(Register rs, const Operand& rt) {
  DCHECK(rt.is_reg());
  neg(rs, rt.rm());
}

void MacroAssembler::Seqz(Register rd, const Operand& rt) {
  if (rt.is_reg()) {
    seqz(rd, rt.rm());
  } else {
    li(rd, rt.immediate() == 0);
  }
}

void MacroAssembler::Snez(Register rd, const Operand& rt) {
  if (rt.is_reg()) {
    snez(rd, rt.rm());
  } else {
    li(rd, rt.immediate() != 0);
  }
}

void MacroAssembler::Seq(Register rd, Register rs, const Operand& rt) {
  if (rs == zero_reg) {
    Seqz(rd, rt);
  } else if (IsZero(rt)) {
    seqz(rd, rs);
  } else {
    SubWord(rd, rs, rt);
    seqz(rd, rd);
  }
}

void MacroAssembler::Sne(Register rd, Register rs, const Operand& rt) {
  if (rs == zero_reg) {
    Snez(rd, rt);
  } else if (IsZero(rt)) {
    snez(rd, rs);
  } else {
    SubWord(rd, rs, rt);
    snez(rd, rd);
  }
}

void MacroAssembler::Slt(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    slt(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      slti(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Li(scratch, rt.immediate());
      slt(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Sltu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sltu(rd, rs, rt.rm());
  } else {
    if (is_int12(rt.immediate()) && !MustUseReg(rt.rmode())) {
      sltiu(rd, rs, static_cast<int32_t>(rt.immediate()));
    } else {
      // li handles the relocation.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      BlockTrampolinePoolScope block_trampoline_pool(this);
      Li(scratch, rt.immediate());
      sltu(rd, rs, scratch);
    }
  }
}

void MacroAssembler::Sle(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    slt(rd, rt.rm(), rs);
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Li(scratch, rt.immediate());
    slt(rd, scratch, rs);
  }
  xori(rd, rd, 1);
}

void MacroAssembler::Sleu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sltu(rd, rt.rm(), rs);
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Li(scratch, rt.immediate());
    sltu(rd, scratch, rs);
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
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Li(scratch, rt.immediate());
    slt(rd, scratch, rs);
  }
}

void MacroAssembler::Sgtu(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sltu(rd, rt.rm(), rs);
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Li(scratch, rt.immediate());
    sltu(rd, scratch, rs);
  }
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Sll32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sllw(rd, rs, rt.rm());
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    slliw(rd, rs, shamt);
  }
}

void MacroAssembler::Sra32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sraw(rd, rs, rt.rm());
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    sraiw(rd, rs, shamt);
  }
}

void MacroAssembler::Srl32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    srlw(rd, rs, rt.rm());
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    srliw(rd, rs, shamt);
  }
}

void MacroAssembler::SraWord(Register rd, Register rs, const Operand& rt) {
  Sra64(rd, rs, rt);
}

void MacroAssembler::Sra64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sra(rd, rs, rt.rm());
  } else if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
             ((rd.code() & 0b11000) == 0b01000) && is_int6(rt.immediate())) {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    c_srai(rd, shamt);
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    srai(rd, rs, shamt);
  }
}

void MacroAssembler::SrlWord(Register rd, Register rs, const Operand& rt) {
  Srl64(rd, rs, rt);
}

void MacroAssembler::Srl64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    srl(rd, rs, rt.rm());
  } else if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
             ((rd.code() & 0b11000) == 0b01000) && is_int6(rt.immediate())) {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    c_srli(rd, shamt);
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    srli(rd, rs, shamt);
  }
}

void MacroAssembler::SllWord(Register rd, Register rs, const Operand& rt) {
  Sll64(rd, rs, rt);
}

void MacroAssembler::Sll64(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sll(rd, rs, rt.rm());
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    if (v8_flags.riscv_c_extension && (rd.code() == rs.code()) &&
        (rd != zero_reg) && (shamt != 0) && is_uint6(shamt)) {
      c_slli(rd, shamt);
    } else {
      slli(rd, rs, shamt);
    }
  }
}

void MacroAssembler::Ror(Register rd, Register rs, const Operand& rt) {
  if (CpuFeatures::IsSupported(ZBB)) {
    if (rt.is_reg()) {
      rorw(rd, rs, rt.rm());
    } else {
      int64_t ror_value = rt.immediate() % 32;
      if (ror_value < 0) {
        ror_value += 32;
      }
      roriw(rd, rs, ror_value);
    }
    return;
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (rt.is_reg()) {
    negw(scratch, rt.rm());
    sllw(scratch, rs, scratch);
    srlw(rd, rs, rt.rm());
    or_(rd, scratch, rd);
    sext_w(rd, rd);
  } else {
    int64_t ror_value = rt.immediate() % 32;
    if (ror_value == 0) {
      Mv(rd, rs);
      return;
    } else if (ror_value < 0) {
      ror_value += 32;
    }
    srliw(scratch, rs, ror_value);
    slliw(rd, rs, 32 - ror_value);
    or_(rd, scratch, rd);
    sext_w(rd, rd);
  }
}

void MacroAssembler::Dror(Register rd, Register rs, const Operand& rt) {
  if (CpuFeatures::IsSupported(ZBB)) {
    if (rt.is_reg()) {
      ror(rd, rs, rt.rm());
    } else {
      int64_t dror_value = rt.immediate() % 64;
      if (dror_value < 0) {
        dror_value += 64;
      }
      rori(rd, rs, dror_value);
    }
    return;
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (rt.is_reg()) {
    negw(scratch, rt.rm());
    sll(scratch, rs, scratch);
    srl(rd, rs, rt.rm());
    or_(rd, scratch, rd);
  } else {
    int64_t dror_value = rt.immediate() % 64;
    if (dror_value == 0) {
      Mv(rd, rs);
      return;
    } else if (dror_value < 0) {
      dror_value += 64;
    }
    srli(scratch, rs, dror_value);
    slli(rd, rs, 64 - dror_value);
    or_(rd, scratch, rd);
  }
}
#elif V8_TARGET_ARCH_RISCV32
void MacroAssembler::SllWord(Register rd, Register rs, const Operand& rt) {
  Sll32(rd, rs, rt);
}

void MacroAssembler::Sll32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sll(rd, rs, rt.rm());
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    slli(rd, rs, shamt);
  }
}

void MacroAssembler::SraWord(Register rd, Register rs, const Operand& rt) {
  Sra32(rd, rs, rt);
}

void MacroAssembler::Sra32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    sra(rd, rs, rt.rm());
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    srai(rd, rs, shamt);
  }
}

void MacroAssembler::SrlWord(Register rd, Register rs, const Operand& rt) {
  Srl32(rd, rs, rt);
}

void MacroAssembler::Srl32(Register rd, Register rs, const Operand& rt) {
  if (rt.is_reg()) {
    srl(rd, rs, rt.rm());
  } else {
    uint8_t shamt = static_cast<uint8_t>(rt.immediate());
    srli(rd, rs, shamt);
  }
}

void MacroAssembler::Ror(Register rd, Register rs, const Operand& rt) {
  if (CpuFeatures::IsSupported(ZBB)) {
    if (rt.is_reg()) {
      ror(rd, rs, rt.rm());
    } else {
      int32_t ror_value = rt.immediate() % 32;
      if (ror_value < 0) {
        ror_value += 32;
      }
      rori(rd, rs, ror_value);
    }
    return;
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (rt.is_reg()) {
    neg(scratch, rt.rm());
    sll(scratch, rs, scratch);
    srl(rd, rs, rt.rm());
    or_(rd, scratch, rd);
  } else {
    int32_t ror_value = rt.immediate() % 32;
    if (ror_value == 0) {
      Mv(rd, rs);
      return;
    } else if (ror_value < 0) {
      ror_value += 32;
    }
    srli(scratch, rs, ror_value);
    slli(rd, rs, 32 - ror_value);
    or_(rd, scratch, rd);
  }
}
#endif

void MacroAssembler::Li(Register rd, intptr_t imm) {
  if (v8_flags.riscv_c_extension && (rd != zero_reg) && is_int6(imm)) {
    c_li(rd, imm);
  } else {
    RV_li(rd, imm);
  }
}

void MacroAssembler::Mv(Register rd, const Operand& rt) {
  if (v8_flags.riscv_c_extension && (rd != zero_reg) && (rt.rm() != zero_reg)) {
    c_mv(rd, rt.rm());
  } else {
    mv(rd, rt.rm());
  }
}

void MacroAssembler::CalcScaledAddress(Register rd, Register rt, Register rs,
                                       uint8_t sa) {
  DCHECK(sa >= 1 && sa <= 31);
  if (CpuFeatures::IsSupported(ZBA)) {
    switch (sa) {
      case 1:
        sh1add(rd, rs, rt);
        return;
      case 2:
        sh2add(rd, rs, rt);
        return;
      case 3:
        sh3add(rd, rs, rt);
        return;
      default:
        break;
    }
  }
  UseScratchRegisterScope temps(this);
  Register tmp = rd == rt ? temps.Acquire() : rd;
  DCHECK(tmp != rt);
  slli(tmp, rs, sa);
  AddWord(rd, rt, tmp);
  return;
}

// ------------Pseudo-instructions-------------
// Change endianness

template <int NBYTES>
void MacroAssembler::ReverseBytesHelper(Register rd, Register rs, Register tmp1,
                                        Register tmp2) {
  DCHECK(tmp1 != tmp2);
  DCHECK((rs != tmp1) && (rs != tmp2));
  DCHECK((rd != tmp1) && (rd != tmp2));

  // ByteMask - maximum value, held in byte
  constexpr int ByteMask = (1 << kBitsPerByte) - 1;
  // tmp1 = rs[0]; take least byte
  // tmp1 = tmp1 << kBitsPerByte;
  // for (nbyte = 1; nbyte < NBYTES - 1; nbyte++) {
  //   tmp2 = rs[nbyte]; take n`th byte
  //   tmp1 = (tmp2 | tmp1) << kBitsPerByte; add n`th source byte to tmp1
  // }
  // rd[0] = rs[NBYTES-1]; take upper byte
  // rd[NBYTES-1 : 1] = tmp1[NBYTES-1 : 1]; fill other bytes
  andi(tmp1, rs, ByteMask);
  slli(tmp1, tmp1, kBitsPerByte);
  for (int nbyte = 1; nbyte < NBYTES - 1; nbyte++) {
    srli(tmp2, rs, nbyte * kBitsPerByte);
    andi(tmp2, tmp2, ByteMask);
    or_(tmp1, tmp1, tmp2);
    slli(tmp1, tmp1, kBitsPerByte);
  }
  srli(rd, rs, (NBYTES - 1) * kBitsPerByte);
  andi(rd, rd, ByteMask);
  or_(rd, tmp1, rd);
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::ByteSwap(Register rd, Register rs, int operand_size,
                              Register scratch) {
  DCHECK(operand_size == 4 || operand_size == 8);
  if (CpuFeatures::IsSupported(ZBB)) {
    rev8(rd, rs);
    if (operand_size == 4) {
      srai(rd, rd, 32);
    }
    return;
  }
  DCHECK_NE(scratch, rs);
  DCHECK_NE(scratch, rd);
  if (operand_size == 4) {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    DCHECK((rd != t6) && (rs != t6));
    Register x0 = temps.Acquire();
    Register x1 = temps.Acquire();
    if (scratch == no_reg) {
      ReverseBytesHelper<8>(rd, rs, x0, x1);
      srai(rd, rd, 32);
    } else {
      // Uint32_t x1 = 0x00FF00FF;
      // x0 = (x0 << 16 | x0 >> 16);
      // x0 = (((x0 & x1) << 8)  | ((x0 & (x1 << 8)) >> 8));
      Register x2 = scratch;
      li(x1, 0x00FF00FF);
      slliw(x0, rs, 16);
      srliw(rd, rs, 16);
      or_(x0, rd, x0);   // x0 <- x0 << 16 | x0 >> 16
      and_(x2, x0, x1);  // x2 <- x0 & 0x00FF00FF
      slliw(x2, x2, 8);  // x2 <- (x0 & x1) << 8
      slliw(x1, x1, 8);  // x1 <- 0xFF00FF00
      and_(rd, x0, x1);  // x0 & 0xFF00FF00
      srliw(rd, rd, 8);
      or_(rd, rd, x2);  // (((x0 & x1) << 8)  | ((x0 & (x1 << 8)) >> 8))
    }
  } else {
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    DCHECK((rd != t6) && (rs != t6));
    Register x0 = temps.Acquire();
    Register x1 = temps.Acquire();
    if (scratch == no_reg) {
      ReverseBytesHelper<8>(rd, rs, x0, x1);
    } else {
      // uinx24_t x1 = 0x0000FFFF0000FFFFl;
      // uinx24_t x1 = 0x00FF00FF00FF00FFl;
      // x0 = (x0 << 32 | x0 >> 32);
      // x0 = (x0 & x1) << 16 | (x0 & (x1 << 16)) >> 16;
      // x0 = (x0 & x1) << 8  | (x0 & (x1 << 8)) >> 8;
      Register x2 = scratch;
      li(x1, 0x0000FFFF0000FFFFl);
      slli(x0, rs, 32);
      srli(rd, rs, 32);
      or_(x0, rd, x0);   // x0 <- x0 << 32 | x0 >> 32
      and_(x2, x0, x1);  // x2 <- x0 & 0x0000FFFF0000FFFF
      slli(x2, x2, 16);  // x2 <- (x0 & 0x0000FFFF0000FFFF) << 16
      slli(x1, x1, 16);  // x1 <- 0xFFFF0000FFFF0000
      and_(rd, x0, x1);  // rd <- x0 & 0xFFFF0000FFFF0000
      srli(rd, rd, 16);  // rd <- x0 & (x1 << 16)) >> 16
      or_(x0, rd, x2);   // (x0 & x1) << 16 | (x0 & (x1 << 16)) >> 16;
      li(x1, 0x00FF00FF00FF00FFl);
      and_(x2, x0, x1);  // x2 <- x0 & 0x00FF00FF00FF00FF
      slli(x2, x2, 8);   // x2 <- (x0 & x1) << 8
      slli(x1, x1, 8);   // x1 <- 0xFF00FF00FF00FF00
      and_(rd, x0, x1);
      srli(rd, rd, 8);  // rd <- (x0 & (x1 << 8)) >> 8
      or_(rd, rd, x2);  // (((x0 & x1) << 8)  | ((x0 & (x1 << 8)) >> 8))
    }
  }
}

#elif V8_TARGET_ARCH_RISCV32
void MacroAssembler::ByteSwap(Register rd, Register rs, int operand_size,
                              Register scratch) {
  if (CpuFeatures::IsSupported(ZBB)) {
    rev8(rd, rs);
    return;
  }
  DCHECK_NE(scratch, rs);
  DCHECK_NE(scratch, rd);
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK((rd != t6) && (rs != t6));
  Register x0 = temps.Acquire();
  Register x1 = temps.Acquire();
  if (scratch == no_reg) {
    ReverseBytesHelper<4>(rd, rs, x0, x1);
  } else {
    // Uint32_t x1 = 0x00FF00FF;
    // x0 = (x0 << 16 | x0 >> 16);
    // x0 = (((x0 & x1) << 8)  | ((x0 & (x1 << 8)) >> 8));
    Register x2 = scratch;
    li(x1, 0x00FF00FF);
    slli(x0, rs, 16);
    srli(rd, rs, 16);
    or_(x0, rd, x0);   // x0 <- x0 << 16 | x0 >> 16
    and_(x2, x0, x1);  // x2 <- x0 & 0x00FF00FF
    slli(x2, x2, 8);   // x2 <- (x0 & x1) << 8
    slli(x1, x1, 8);   // x1 <- 0xFF00FF00
    and_(rd, x0, x1);  // x0 & 0xFF00FF00
    srli(rd, rd, 8);
    or_(rd, rd, x2);  // (((x0 & x1) << 8)  | ((x0 & (x1 << 8)) >> 8))
  }
}
#endif

template <int NBYTES, bool LOAD_SIGNED>
void MacroAssembler::LoadNBytes(Register rd, const MemOperand& rs,
                                Register scratch) {
  DCHECK(rd != rs.rm() && rd != scratch);
  DCHECK_LE(NBYTES, 8);

  // load the most significant byte
  if (LOAD_SIGNED) {
    lb(rd, rs.rm(), rs.offset() + (NBYTES - 1));
  } else {
    lbu(rd, rs.rm(), rs.offset() + (NBYTES - 1));
  }

  // load remaining (nbytes-1) bytes from higher to lower
  slli(rd, rd, 8 * (NBYTES - 1));
  for (int i = (NBYTES - 2); i >= 0; i--) {
    lbu(scratch, rs.rm(), rs.offset() + i);
    if (i) slli(scratch, scratch, i * 8);
    or_(rd, rd, scratch);
  }
}

template <int NBYTES, bool LOAD_SIGNED>
void MacroAssembler::LoadNBytesOverwritingBaseReg(const MemOperand& rs,
                                                  Register scratch0,
                                                  Register scratch1) {
  // This function loads nbytes from memory specified by rs and into rs.rm()
  DCHECK(rs.rm() != scratch0 && rs.rm() != scratch1 && scratch0 != scratch1);
  DCHECK_LE(NBYTES, 8);

  // load the most significant byte
  if (LOAD_SIGNED) {
    lb(scratch0, rs.rm(), rs.offset() + (NBYTES - 1));
  } else {
    lbu(scratch0, rs.rm(), rs.offset() + (NBYTES - 1));
  }

  // load remaining (nbytes-1) bytes from higher to lower
  slli(scratch0, scratch0, 8 * (NBYTES - 1));
  for (int i = (NBYTES - 2); i >= 0; i--) {
    lbu(scratch1, rs.rm(), rs.offset() + i);
    if (i) {
      slli(scratch1, scratch1, i * 8);
      or_(scratch0, scratch0, scratch1);
    } else {
      // write to rs.rm() when processing the last byte
      or_(rs.rm(), scratch0, scratch1);
    }
  }
}

template <int NBYTES, bool IS_SIGNED>
void MacroAssembler::UnalignedLoadHelper(Register rd, const MemOperand& rs) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);

  if (NeedAdjustBaseAndOffset(rs, OffsetAccessType::TWO_ACCESSES, NBYTES - 1)) {
    // Adjust offset for two accesses and check if offset + 3 fits into int12.
    MemOperand source = rs;
    Register scratch_base = temps.Acquire();
    DCHECK(scratch_base != rs.rm());
    AdjustBaseAndOffset(&source, scratch_base, OffsetAccessType::TWO_ACCESSES,
                        NBYTES - 1);

    // Since source.rm() is scratch_base, assume rd != source.rm()
    DCHECK(rd != source.rm());
    Register scratch_other = temps.Acquire();
    LoadNBytes<NBYTES, IS_SIGNED>(rd, source, scratch_other);
  } else {
    // no need to adjust base-and-offset
    if (rd != rs.rm()) {
      Register scratch = temps.Acquire();
      LoadNBytes<NBYTES, IS_SIGNED>(rd, rs, scratch);
    } else {  // rd == rs.rm()
      Register scratch = temps.Acquire();
      Register scratch2 = temps.Acquire();
      LoadNBytesOverwritingBaseReg<NBYTES, IS_SIGNED>(rs, scratch, scratch2);
    }
  }
}

#if V8_TARGET_ARCH_RISCV64
template <int NBYTES>
void MacroAssembler::UnalignedFLoadHelper(FPURegister frd, const MemOperand& rs,
                                          Register scratch_base) {
  DCHECK(NBYTES == 4 || NBYTES == 8);
  DCHECK_NE(scratch_base, rs.rm());
  BlockTrampolinePoolScope block_trampoline_pool(this);
  MemOperand source = rs;
  if (NeedAdjustBaseAndOffset(rs, OffsetAccessType::TWO_ACCESSES, NBYTES - 1)) {
    // Adjust offset for two accesses and check if offset + 3 fits into int12.
    DCHECK(scratch_base != rs.rm());
    AdjustBaseAndOffset(&source, scratch_base, OffsetAccessType::TWO_ACCESSES,
                        NBYTES - 1);
  }
  UseScratchRegisterScope temps(this);
  Register scratch_other = temps.Acquire();
  Register scratch = temps.Acquire();
  DCHECK(scratch != rs.rm() && scratch_other != scratch &&
         scratch_other != rs.rm());
  LoadNBytes<NBYTES, true>(scratch, source, scratch_other);
  if (NBYTES == 4)
    fmv_w_x(frd, scratch);
  else
    fmv_d_x(frd, scratch);
}
#elif V8_TARGET_ARCH_RISCV32
template <int NBYTES>
void MacroAssembler::UnalignedFLoadHelper(FPURegister frd, const MemOperand& rs,
                                          Register scratch_base) {
  DCHECK_EQ(NBYTES, 4);
  DCHECK_NE(scratch_base, rs.rm());
  BlockTrampolinePoolScope block_trampoline_pool(this);
  MemOperand source = rs;
  if (NeedAdjustBaseAndOffset(rs, OffsetAccessType::TWO_ACCESSES, NBYTES - 1)) {
    // Adjust offset for two accesses and check if offset + 3 fits into int12.
    DCHECK(scratch_base != rs.rm());
    AdjustBaseAndOffset(&source, scratch_base, OffsetAccessType::TWO_ACCESSES,
                        NBYTES - 1);
  }
  UseScratchRegisterScope temps(this);
  Register scratch_other = temps.Acquire();
  Register scratch = temps.Acquire();
  DCHECK(scratch != rs.rm() && scratch_other != scratch &&
         scratch_other != rs.rm());
  LoadNBytes<NBYTES, true>(scratch, source, scratch_other);
  fmv_w_x(frd, scratch);
}

void MacroAssembler::UnalignedDoubleHelper(FPURegister frd,
                                           const MemOperand& rs,
                                           Register scratch_base) {
  DCHECK_NE(scratch_base, rs.rm());
  BlockTrampolinePoolScope block_trampoline_pool(this);
  MemOperand source = rs;
  if (NeedAdjustBaseAndOffset(rs, OffsetAccessType::TWO_ACCESSES, 8 - 1)) {
    // Adjust offset for two accesses and check if offset + 3 fits into int12.
    DCHECK(scratch_base != rs.rm());
    AdjustBaseAndOffset(&source, scratch_base, OffsetAccessType::TWO_ACCESSES,
                        8 - 1);
  }
  UseScratchRegisterScope temps(this);
  Register scratch_other = temps.Acquire();
  Register scratch = temps.Acquire();
  DCHECK(scratch != rs.rm() && scratch_other != scratch &&
         scratch_other != rs.rm());
  LoadNBytes<4, true>(scratch, source, scratch_other);
  SubWord(sp, sp, 8);
  Sw(scratch, MemOperand(sp, 0));
  source.set_offset(source.offset() + 4);
  LoadNBytes<4, true>(scratch, source, scratch_other);
  Sw(scratch, MemOperand(sp, 4));
  LoadDouble(frd, MemOperand(sp, 0));
  AddWord(sp, sp, 8);
}
#endif

template <int NBYTES>
void MacroAssembler::UnalignedStoreHelper(Register rd, const MemOperand& rs,
                                          Register scratch_other) {
  DCHECK(scratch_other != rs.rm());
  DCHECK_LE(NBYTES, 8);
  MemOperand source = rs;
  UseScratchRegisterScope temps(this);
  Register scratch_base = temps.Acquire();
  // Adjust offset for two accesses and check if offset + 3 fits into int12.
  if (NeedAdjustBaseAndOffset(rs, OffsetAccessType::TWO_ACCESSES, NBYTES - 1)) {
    DCHECK(scratch_base != rd && scratch_base != rs.rm());
    AdjustBaseAndOffset(&source, scratch_base, OffsetAccessType::TWO_ACCESSES,
                        NBYTES - 1);
  }

  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (scratch_other == no_reg) {
    if (temps.hasAvailable()) {
      scratch_other = temps.Acquire();
    } else {
      push(t2);
      scratch_other = t2;
    }
  }

  DCHECK(scratch_other != rd && scratch_other != rs.rm() &&
         scratch_other != source.rm());

  sb(rd, source.rm(), source.offset());
  for (size_t i = 1; i <= (NBYTES - 1); i++) {
    srli(scratch_other, rd, i * 8);
    sb(scratch_other, source.rm(), source.offset() + i);
  }
  if (scratch_other == t2) {
    pop(t2);
  }
}

#if V8_TARGET_ARCH_RISCV64
template <int NBYTES>
void MacroAssembler::UnalignedFStoreHelper(FPURegister frd,
                                           const MemOperand& rs,
                                           Register scratch) {
  DCHECK(NBYTES == 8 || NBYTES == 4);
  DCHECK_NE(scratch, rs.rm());
  if (NBYTES == 4) {
    fmv_x_w(scratch, frd);
  } else {
    fmv_x_d(scratch, frd);
  }
  UnalignedStoreHelper<NBYTES>(scratch, rs);
}
#elif V8_TARGET_ARCH_RISCV32
template <int NBYTES>
void MacroAssembler::UnalignedFStoreHelper(FPURegister frd,
                                           const MemOperand& rs,
                                           Register scratch) {
  DCHECK_EQ(NBYTES, 4);
  DCHECK_NE(scratch, rs.rm());
  fmv_x_w(scratch, frd);
  UnalignedStoreHelper<NBYTES>(scratch, rs);
}
void MacroAssembler::UnalignedDStoreHelper(FPURegister frd,
                                           const MemOperand& rs,
                                           Register scratch) {
  DCHECK_NE(scratch, rs.rm());
  Sub32(sp, sp, 8);
  StoreDouble(frd, MemOperand(sp, 0));
  Lw(scratch, MemOperand(sp, 0));
  UnalignedStoreHelper<4>(scratch, rs);
  Lw(scratch, MemOperand(sp, 4));
  MemOperand source = rs;
  source.set_offset(source.offset() + 4);
  UnalignedStoreHelper<4>(scratch, source);
  Add32(sp, sp, 8);
}
#endif

template <typename Reg_T, typename Func>
void MacroAssembler::AlignedLoadHelper(Reg_T target, const MemOperand& rs,
                                       Func generator) {
  MemOperand source = rs;
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (NeedAdjustBaseAndOffset(source)) {
    Register scratch = temps.Acquire();
    DCHECK(scratch != rs.rm());
    AdjustBaseAndOffset(&source, scratch);
  }
  generator(target, source);
}

template <typename Reg_T, typename Func>
void MacroAssembler::AlignedStoreHelper(Reg_T value, const MemOperand& rs,
                                        Func generator) {
  MemOperand source = rs;
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (NeedAdjustBaseAndOffset(source)) {
    Register scratch = temps.Acquire();
    // make sure scratch does not overwrite value
    if (std::is_same<Reg_T, Register>::value)
      DCHECK(scratch.code() != value.code());
    DCHECK(scratch != rs.rm());
    AdjustBaseAndOffset(&source, scratch);
  }
  generator(value, source);
}

void MacroAssembler::Ulw(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<4, true>(rd, rs);
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Ulwu(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<4, false>(rd, rs);
}
#endif
void MacroAssembler::Usw(Register rd, const MemOperand& rs) {
  UnalignedStoreHelper<4>(rd, rs);
}

void MacroAssembler::Ulh(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<2, true>(rd, rs);
}

void MacroAssembler::Ulhu(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<2, false>(rd, rs);
}

void MacroAssembler::Ush(Register rd, const MemOperand& rs) {
  UnalignedStoreHelper<2>(rd, rs);
}

void MacroAssembler::Uld(Register rd, const MemOperand& rs) {
  UnalignedLoadHelper<8, true>(rd, rs);
}
#if V8_TARGET_ARCH_RISCV64
// Load consequent 32-bit word pair in 64-bit reg. and put first word in low
// bits,
// second word in high bits.
void MacroAssembler::LoadWordPair(Register rd, const MemOperand& rs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Lwu(rd, rs);
  Lw(scratch, MemOperand(rs.rm(), rs.offset() + kSystemPointerSize / 2));
  slli(scratch, scratch, 32);
  AddWord(rd, rd, scratch);
}

// Do 64-bit store as two consequent 32-bit stores to unaligned address.
void MacroAssembler::StoreWordPair(Register rd, const MemOperand& rs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Sw(rd, rs);
  srai(scratch, rd, 32);
  Sw(scratch, MemOperand(rs.rm(), rs.offset() + kSystemPointerSize / 2));
}
#endif

void MacroAssembler::Usd(Register rd, const MemOperand& rs) {
  UnalignedStoreHelper<8>(rd, rs);
}

void MacroAssembler::ULoadFloat(FPURegister fd, const MemOperand& rs,
                                Register scratch) {
  DCHECK_NE(scratch, rs.rm());
  UnalignedFLoadHelper<4>(fd, rs, scratch);
}

void MacroAssembler::UStoreFloat(FPURegister fd, const MemOperand& rs,
                                 Register scratch) {
  DCHECK_NE(scratch, rs.rm());
  UnalignedFStoreHelper<4>(fd, rs, scratch);
}

void MacroAssembler::ULoadDouble(FPURegister fd, const MemOperand& rs,
                                 Register scratch) {
  DCHECK_NE(scratch, rs.rm());
#if V8_TARGET_ARCH_RISCV64
  UnalignedFLoadHelper<8>(fd, rs, scratch);
#elif V8_TARGET_ARCH_RISCV32
  UnalignedDoubleHelper(fd, rs, scratch);
#endif
}

void MacroAssembler::UStoreDouble(FPURegister fd, const MemOperand& rs,
                                  Register scratch) {
  DCHECK_NE(scratch, rs.rm());
#if V8_TARGET_ARCH_RISCV64
  UnalignedFStoreHelper<8>(fd, rs, scratch);
#elif V8_TARGET_ARCH_RISCV32
  UnalignedDStoreHelper(fd, rs, scratch);
#endif
}

void MacroAssembler::Lb(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->lb(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void MacroAssembler::Lbu(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->lbu(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void MacroAssembler::Sb(Register rd, const MemOperand& rs) {
  auto fn = [this](Register value, const MemOperand& source) {
    this->sb(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(rd, rs, fn);
}

void MacroAssembler::Lh(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->lh(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void MacroAssembler::Lhu(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->lhu(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}

void MacroAssembler::Sh(Register rd, const MemOperand& rs) {
  auto fn = [this](Register value, const MemOperand& source) {
    this->sh(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(rd, rs, fn);
}

void MacroAssembler::Lw(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    if (v8_flags.riscv_c_extension && ((target.code() & 0b11000) == 0b01000) &&
        ((source.rm().code() & 0b11000) == 0b01000) &&
        is_uint7(source.offset()) && ((source.offset() & 0x3) == 0)) {
      this->c_lw(target, source.rm(), source.offset());
    } else if (v8_flags.riscv_c_extension && (target != zero_reg) &&
               is_uint8(source.offset()) && (source.rm() == sp) &&
               ((source.offset() & 0x3) == 0)) {
      this->c_lwsp(target, source.offset());
    } else {
      this->lw(target, source.rm(), source.offset());
    }
  };
  AlignedLoadHelper(rd, rs, fn);
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Lwu(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    this->lwu(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(rd, rs, fn);
}
#endif
void MacroAssembler::Sw(Register rd, const MemOperand& rs) {
  auto fn = [this](Register value, const MemOperand& source) {
    if (v8_flags.riscv_c_extension && ((value.code() & 0b11000) == 0b01000) &&
        ((source.rm().code() & 0b11000) == 0b01000) &&
        is_uint7(source.offset()) && ((source.offset() & 0x3) == 0)) {
      this->c_sw(value, source.rm(), source.offset());
    } else if (v8_flags.riscv_c_extension && (source.rm() == sp) &&
               is_uint8(source.offset()) && (((source.offset() & 0x3) == 0))) {
      this->c_swsp(value, source.offset());
    } else {
      this->sw(value, source.rm(), source.offset());
    }
  };
  AlignedStoreHelper(rd, rs, fn);
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Ld(Register rd, const MemOperand& rs) {
  auto fn = [this](Register target, const MemOperand& source) {
    if (v8_flags.riscv_c_extension && ((target.code() & 0b11000) == 0b01000) &&
        ((source.rm().code() & 0b11000) == 0b01000) &&
        is_uint8(source.offset()) && ((source.offset() & 0x7) == 0)) {
      this->c_ld(target, source.rm(), source.offset());
    } else if (v8_flags.riscv_c_extension && (target != zero_reg) &&
               is_uint9(source.offset()) && (source.rm() == sp) &&
               ((source.offset() & 0x7) == 0)) {
      this->c_ldsp(target, source.offset());
    } else {
      this->ld(target, source.rm(), source.offset());
    }
  };
  AlignedLoadHelper(rd, rs, fn);
}

void MacroAssembler::Sd(Register rd, const MemOperand& rs) {
  auto fn = [this](Register value, const MemOperand& source) {
    if (v8_flags.riscv_c_extension && ((value.code() & 0b11000) == 0b01000) &&
        ((source.rm().code() & 0b11000) == 0b01000) &&
        is_uint8(source.offset()) && ((source.offset() & 0x7) == 0)) {
      this->c_sd(value, source.rm(), source.offset());
    } else if (v8_flags.riscv_c_extension && (source.rm() == sp) &&
               is_uint9(source.offset()) && ((source.offset() & 0x7) == 0)) {
      this->c_sdsp(value, source.offset());
    } else {
      this->sd(value, source.rm(), source.offset());
    }
  };
  AlignedStoreHelper(rd, rs, fn);
}
#endif
void MacroAssembler::LoadFloat(FPURegister fd, const MemOperand& src) {
  auto fn = [this](FPURegister target, const MemOperand& source) {
    this->flw(target, source.rm(), source.offset());
  };
  AlignedLoadHelper(fd, src, fn);
}

void MacroAssembler::StoreFloat(FPURegister fs, const MemOperand& src) {
  auto fn = [this](FPURegister value, const MemOperand& source) {
    this->fsw(value, source.rm(), source.offset());
  };
  AlignedStoreHelper(fs, src, fn);
}

void MacroAssembler::LoadDouble(FPURegister fd, const MemOperand& src) {
  auto fn = [this](FPURegister target, const MemOperand& source) {
    if (v8_flags.riscv_c_extension && ((target.code() & 0b11000) == 0b01000) &&
        ((source.rm().code() & 0b11000) == 0b01000) &&
        is_uint8(source.offset()) && ((source.offset() & 0x7) == 0)) {
      this->c_fld(target, source.rm(), source.offset());
    } else if (v8_flags.riscv_c_extension && (source.rm() == sp) &&
               is_uint9(source.offset()) && ((source.offset() & 0x7) == 0)) {
      this->c_fldsp(target, source.offset());
    } else {
      this->fld(target, source.rm(), source.offset());
    }
  };
  AlignedLoadHelper(fd, src, fn);
}

void MacroAssembler::StoreDouble(FPURegister fs, const MemOperand& src) {
  auto fn = [this](FPURegister value, const MemOperand& source) {
    if (v8_flags.riscv_c_extension && ((value.code() & 0b11000) == 0b01000) &&
        ((source.rm().code() & 0b11000) == 0b01000) &&
        is_uint8(source.offset()) && ((source.offset() & 0x7) == 0)) {
      this->c_fsd(value, source.rm(), source.offset());
    } else if (v8_flags.riscv_c_extension && (source.rm() == sp) &&
               is_uint9(source.offset()) && ((source.offset() & 0x7) == 0)) {
      this->c_fsdsp(value, source.offset());
    } else {
      this->fsd(value, source.rm(), source.offset());
    }
  };
  AlignedStoreHelper(fs, src, fn);
}

void MacroAssembler::Ll(Register rd, const MemOperand& rs) {
  bool is_one_instruction = rs.offset() == 0;
  if (is_one_instruction) {
    lr_w(false, false, rd, rs.rm());
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    AddWord(scratch, rs.rm(), rs.offset());
    lr_w(false, false, rd, scratch);
  }
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Lld(Register rd, const MemOperand& rs) {
  bool is_one_instruction = rs.offset() == 0;
  if (is_one_instruction) {
    lr_d(false, false, rd, rs.rm());
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    AddWord(scratch, rs.rm(), rs.offset());
    lr_d(false, false, rd, scratch);
  }
}
#endif
void MacroAssembler::Sc(Register rd, const MemOperand& rs) {
  bool is_one_instruction = rs.offset() == 0;
  if (is_one_instruction) {
    sc_w(false, false, rd, rs.rm(), rd);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    AddWord(scratch, rs.rm(), rs.offset());
    sc_w(false, false, rd, scratch, rd);
  }
}
#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Scd(Register rd, const MemOperand& rs) {
  bool is_one_instruction = rs.offset() == 0;
  if (is_one_instruction) {
    sc_d(false, false, rd, rs.rm(), rd);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    AddWord(scratch, rs.rm(), rs.offset());
    sc_d(false, false, rd, scratch, rd);
  }
}
#endif
void MacroAssembler::li(Register dst, Handle<HeapObject> value,
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
    li(dst, Operand(index, rmode));
  } else {
    DCHECK(RelocInfo::IsFullEmbeddedObject(rmode));
    li(dst, Operand(value.address(), rmode));
  }
}

void MacroAssembler::li(Register dst, ExternalReference reference,
                        LiFlags mode) {
  if (root_array_available()) {
    if (reference.IsIsolateFieldId()) {
      AddWord(dst, kRootRegister,
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
  li(dst, Operand(reference), mode);
}

static inline int InstrCountForLiLower32Bit(int64_t value) {
  int64_t Hi20 = ((value + 0x800) >> 12);
  int64_t Lo12 = value << 52 >> 52;
  if (Hi20 == 0 || Lo12 == 0) {
    return 1;
  }
  return 2;
}

int MacroAssembler::InstrCountForLi64Bit(int64_t value) {
  if (is_int32(value + 0x800)) {
    return InstrCountForLiLower32Bit(value);
  } else {
    return RV_li_count(value);
  }
  UNREACHABLE();
  return INT_MAX;
}

void MacroAssembler::li_optimized(Register rd, Operand j, LiFlags mode) {
  DCHECK(!j.is_reg());
  DCHECK(!MustUseReg(j.rmode()));
  DCHECK(mode == OPTIMIZE_SIZE);
  Li(rd, j.immediate());
}

void MacroAssembler::li(Register rd, Operand j, LiFlags mode) {
  DCHECK(!j.is_reg());
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (!MustUseReg(j.rmode()) && mode == OPTIMIZE_SIZE) {
    UseScratchRegisterScope temps(this);
    int count = RV_li_count(j.immediate(), temps.hasAvailable());
    int reverse_count = RV_li_count(~j.immediate(), temps.hasAvailable());
    if (v8_flags.riscv_constant_pool && count >= 4 && reverse_count >= 4) {
      // Ld/Lw a Address from a constant pool.
#if V8_TARGET_ARCH_RISCV32
      RecordEntry((uint32_t)j.immediate(), j.rmode());
#elif V8_TARGET_ARCH_RISCV64
      RecordEntry((uint64_t)j.immediate(), j.rmode());
#endif
      auipc(rd, 0);
      // Record a value into constant pool.
      LoadWord(rd, MemOperand(rd, 0));
    } else {
      if ((count - reverse_count) > 1) {
        Li(rd, ~j.immediate());
        not_(rd, rd);
      } else {
        Li(rd, j.immediate());
      }
    }
  } else if (MustUseReg(j.rmode())) {
    if (RelocInfo::IsWasmCanonicalSigId(j.rmode())) {
      RecordRelocInfo(j.rmode());
      DCHECK(is_int32(j.immediate()));
#if V8_TARGET_ARCH_RISCV64
      li_constant32(rd, int32_t(j.immediate()));
#elif V8_TARGET_ARCH_RISCV32
      li_constant(rd, int32_t(j.immediate()));
#endif
    } else {
      int64_t immediate;
      if (j.IsHeapNumberRequest()) {
        RequestHeapNumber(j.heap_number_request());
        immediate = 0;
      } else {
        immediate = j.immediate();
      }

      RecordRelocInfo(j.rmode(), immediate);
      li_ptr(rd, immediate);
    }
  } else if (mode == ADDRESS_LOAD) {
    // We always need the same number of instructions as we may need to patch
    // this code to load another value which may need all 6 instructions.
    RecordRelocInfo(j.rmode());
    li_ptr(rd, j.immediate());
  } else {  // Always emit the same 48 bit instruction
            // sequence.
    li_ptr(rd, j.immediate());
  }
}

static RegList t_regs = {t0, t1, t2, t3, t4, t5, t6};
static RegList a_regs = {a0, a1, a2, a3, a4, a5, a6, a7};
static RegList s_regs = {s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11};

void MacroAssembler::MultiPush(RegList regs) {
  int16_t num_to_push = regs.Count();
  int16_t stack_offset = num_to_push * kSystemPointerSize;

#define TEST_AND_PUSH_REG(reg)                    \
  if (regs.has(reg)) {                            \
    stack_offset -= kSystemPointerSize;           \
    StoreWord(reg, MemOperand(sp, stack_offset)); \
    regs.clear(reg);                              \
  }

#define T_REGS(V) V(t6) V(t5) V(t4) V(t3) V(t2) V(t1) V(t0)
#define A_REGS(V) V(a7) V(a6) V(a5) V(a4) V(a3) V(a2) V(a1) V(a0)
#define S_REGS(V) \
  V(s11) V(s10) V(s9) V(s8) V(s7) V(s6) V(s5) V(s4) V(s3) V(s2) V(s1)

  SubWord(sp, sp, Operand(stack_offset));

  // Certain usage of MultiPush requires that registers are pushed onto the
  // stack in a particular: ra, fp, sp, gp, .... (basically in the decreasing
  // order of register numbers according to MIPS register numbers)
  TEST_AND_PUSH_REG(ra);
  TEST_AND_PUSH_REG(fp);
  TEST_AND_PUSH_REG(sp);
  TEST_AND_PUSH_REG(gp);
  TEST_AND_PUSH_REG(tp);
  if (!(regs & s_regs).is_empty()) {
    S_REGS(TEST_AND_PUSH_REG)
  }
  if (!(regs & a_regs).is_empty()) {
    A_REGS(TEST_AND_PUSH_REG)
  }
  if (!(regs & t_regs).is_empty()) {
    T_REGS(TEST_AND_PUSH_REG)
  }

  DCHECK(regs.is_empty());

#undef TEST_AND_PUSH_REG
#undef T_REGS
#undef A_REGS
#undef S_REGS
}

void MacroAssembler::MultiPop(RegList regs) {
  int16_t stack_offset = 0;

#define TEST_AND_POP_REG(reg)                    \
  if (regs.has(reg)) {                           \
    LoadWord(reg, MemOperand(sp, stack_offset)); \
    stack_offset += kSystemPointerSize;          \
    regs.clear(reg);                             \
  }

#define T_REGS(V) V(t0) V(t1) V(t2) V(t3) V(t4) V(t5) V(t6)
#define A_REGS(V) V(a0) V(a1) V(a2) V(a3) V(a4) V(a5) V(a6) V(a7)
#define S_REGS(V) \
  V(s1) V(s2) V(s3) V(s4) V(s5) V(s6) V(s7) V(s8) V(s9) V(s10) V(s11)

  // MultiPop pops from the stack in reverse order as MultiPush
  if (!(regs & t_regs).is_empty()) {
    T_REGS(TEST_AND_POP_REG)
  }
  if (!(regs & a_regs).is_empty()) {
    A_REGS(TEST_AND_POP_REG)
  }
  if (!(regs & s_regs).is_empty()) {
    S_REGS(TEST_AND_POP_REG)
  }
  TEST_AND_POP_REG(tp);
  TEST_AND_POP_REG(gp);
  TEST_AND_POP_REG(sp);
  TEST_AND_POP_REG(fp);
  TEST_AND_POP_REG(ra);

  DCHECK(regs.is_empty());

  addi(sp, sp, stack_offset);

#undef TEST_AND_POP_REG
#undef T_REGS
#undef S_REGS
#undef A_REGS
}

void MacroAssembler::MultiPushFPU(DoubleRegList regs) {
  int16_t num_to_push = regs.Count();
  int16_t stack_offset = num_to_push * kDoubleSize;

  SubWord(sp, sp, Operand(stack_offset));
  for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
    if ((regs.bits() & (1 << i)) != 0) {
      stack_offset -= kDoubleSize;
      StoreDouble(FPURegister::from_code(i), MemOperand(sp, stack_offset));
    }
  }
}

void MacroAssembler::MultiPopFPU(DoubleRegList regs) {
  int16_t stack_offset = 0;

  for (int16_t i = 0; i < kNumRegisters; i++) {
    if ((regs.bits() & (1 << i)) != 0) {
      LoadDouble(FPURegister::from_code(i), MemOperand(sp, stack_offset));
      stack_offset += kDoubleSize;
    }
  }
  addi(sp, sp, stack_offset);
}

#if V8_TARGET_ARCH_RISCV32
void MacroAssembler::AddPair(Register dst_low, Register dst_high,
                             Register left_low, Register left_high,
                             Register right_low, Register right_high,
                             Register scratch1, Register scratch2) {
  UseScratchRegisterScope temps(this);
  Register scratch3 = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);

  Add32(scratch1, left_low, right_low);
  // Save the carry
  Sltu(scratch3, scratch1, left_low);
  Add32(scratch2, left_high, right_high);

  // Output higher 32 bits + carry
  Add32(dst_high, scratch2, scratch3);
  Move(dst_low, scratch1);
}

void MacroAssembler::SubPair(Register dst_low, Register dst_high,
                             Register left_low, Register left_high,
                             Register right_low, Register right_high,
                             Register scratch1, Register scratch2) {
  UseScratchRegisterScope temps(this);
  Register scratch3 = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);

  // Check if we need a borrow
  Sltu(scratch3, left_low, right_low);
  Sub32(scratch1, left_low, right_low);
  Sub32(scratch2, left_high, right_high);

  // Output higher 32 bits - borrow
  Sub32(dst_high, scratch2, scratch3);
  Move(dst_low, scratch1);
}

void MacroAssembler::AndPair(Register dst_low, Register dst_high,
                             Register left_low, Register left_high,
                             Register right_low, Register right_high) {
  And(dst_low, left_low, right_low);
  And(dst_high, left_high, right_high);
}

void MacroAssembler::OrPair(Register dst_low, Register dst_high,
                            Register left_low, Register left_high,
                            Register right_low, Register right_high) {
  Or(dst_low, left_low, right_low);
  Or(dst_high, left_high, right_high);
}
void MacroAssembler::XorPair(Register dst_low, Register dst_high,
                             Register left_low, Register left_high,
                             Register right_low, Register right_high) {
  Xor(dst_low, left_low, right_low);
  Xor(dst_high, left_high, right_high);
}

void MacroAssembler::MulPair(Register dst_low, Register dst_high,
                             Register left_low, Register left_high,
                             Register right_low, Register right_high,
                             Register scratch1, Register scratch2) {
  UseScratchRegisterScope temps(this);
  Register scratch3 = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (dst_low == right_low) {
    mv(scratch1, right_low);
  }
  Mul(scratch3, left_low, right_high);
  // NOTE: do not move these around, recommended sequence is MULH-MUL
  // LL * RL : higher 32 bits
  mulhu(scratch2, left_low, right_low);
  // LL * RL : lower 32 bits
  Mul(dst_low, left_low, right_low);
  // (LL * RH) + (LL * RL : higher 32 bits)
  Add32(scratch2, scratch2, scratch3);
  if (dst_low != right_low) {
    Mul(scratch3, left_high, right_low);
  } else {
    Mul(scratch3, left_high, scratch1);
  }
  Add32(dst_high, scratch2, scratch3);
}

void MacroAssembler::ShlPair(Register dst_low, Register dst_high,
                             Register src_low, Register src_high,
                             Register shift, Register scratch1,
                             Register scratch2) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label done;
  UseScratchRegisterScope temps(this);
  Register scratch3 = no_reg;
  if (dst_low == src_low) {
    scratch3 = temps.Acquire();
    mv(scratch3, src_low);
  }
  And(scratch1, shift, 0x1F);
  // LOW32 << shamt
  sll(dst_low, src_low, scratch1);
  // HIGH32 << shamt
  sll(dst_high, src_high, scratch1);

  // If the shift amount is 0, we're done
  Branch(&done, eq, shift, Operand(zero_reg));

  // LOW32 >> (32 - shamt)
  li(scratch2, 32);
  Sub32(scratch2, scratch2, scratch1);
  if (dst_low == src_low) {
    srl(scratch1, scratch3, scratch2);
  } else {
    srl(scratch1, src_low, scratch2);
  }

  // (HIGH32 << shamt) | (LOW32 >> (32 - shamt))
  Or(dst_high, dst_high, scratch1);

  // If the shift amount is < 32, we're done
  // Note: the shift amount is always < 64, so we can just test if the 6th bit
  // is set
  And(scratch1, shift, 32);
  Branch(&done, eq, scratch1, Operand(zero_reg));
  Move(dst_high, dst_low);
  Move(dst_low, zero_reg);

  bind(&done);
}

void MacroAssembler::ShlPair(Register dst_low, Register dst_high,
                             Register src_low, Register src_high, int32_t shift,
                             Register scratch1, Register scratch2) {
  DCHECK_GE(63, shift);
  DCHECK_NE(dst_low, src_low);
  DCHECK_NE(dst_high, src_low);
  shift &= 0x3F;
  if (shift == 0) {
    Move(dst_high, src_high);
    Move(dst_low, src_low);
  } else if (shift == 32) {
    Move(dst_high, src_low);
    li(dst_low, Operand(0));
  } else if (shift > 32) {
    shift &= 0x1F;
    slli(dst_high, src_low, shift);
    li(dst_low, Operand(0));
  } else {
    slli(dst_high, src_high, shift);
    slli(dst_low, src_low, shift);
    srli(scratch1, src_low, 32 - shift);
    Or(dst_high, dst_high, scratch1);
  }
}

void MacroAssembler::ShrPair(Register dst_low, Register dst_high,
                             Register src_low, Register src_high,
                             Register shift, Register scratch1,
                             Register scratch2) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label done;
  UseScratchRegisterScope temps(this);
  Register scratch3 = no_reg;
  if (dst_high == src_high) {
    scratch3 = temps.Acquire();
    mv(scratch3, src_high);
  }
  And(scratch1, shift, 0x1F);
  // HIGH32 >> shamt
  srl(dst_high, src_high, scratch1);
  // LOW32 >> shamt
  srl(dst_low, src_low, scratch1);

  // If the shift amount is 0, we're done
  Branch(&done, eq, shift, Operand(zero_reg));

  // HIGH32 << (32 - shamt)
  li(scratch2, 32);
  Sub32(scratch2, scratch2, scratch1);
  if (dst_high == src_high) {
    sll(scratch1, scratch3, scratch2);
  } else {
    sll(scratch1, src_high, scratch2);
  }

  // (HIGH32 << (32 - shamt)) | (LOW32 >> shamt)
  Or(dst_low, dst_low, scratch1);

  // If the shift amount is < 32, we're done
  // Note: the shift amount is always < 64, so we can just test if the 6th bit
  // is set
  And(scratch1, shift, 32);
  Branch(&done, eq, scratch1, Operand(zero_reg));
  Move(dst_low, dst_high);
  Move(dst_high, zero_reg);

  bind(&done);
}

void MacroAssembler::ShrPair(Register dst_low, Register dst_high,
                             Register src_low, Register src_high, int32_t shift,
                             Register scratch1, Register scratch2) {
  DCHECK_GE(63, shift);
  DCHECK_NE(dst_low, src_high);
  DCHECK_NE(dst_high, src_high);
  shift &= 0x3F;
  if (shift == 32) {
    mv(dst_low, src_high);
    li(dst_high, Operand(0));
  } else if (shift > 32) {
    shift &= 0x1F;
    srli(dst_low, src_high, shift);
    li(dst_high, Operand(0));
  } else if (shift == 0) {
    Move(dst_low, src_low);
    Move(dst_high, src_high);
  } else {
    srli(dst_low, src_low, shift);
    srli(dst_high, src_high, shift);
    slli(scratch1, src_high, 32 - shift);
    Or(dst_low, dst_low, scratch1);
  }
}

void MacroAssembler::SarPair(Register dst_low, Register dst_high,
                             Register src_low, Register src_high,
                             Register shift, Register scratch1,
                             Register scratch2) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Label done;
  UseScratchRegisterScope temps(this);
  Register scratch3 = no_reg;
  if (dst_high == src_high) {
    scratch3 = temps.Acquire();
    mv(scratch3, src_high);
  }
  And(scratch1, shift, 0x1F);
  // HIGH32 >> shamt (arithmetic)
  sra(dst_high, src_high, scratch1);
  // LOW32 >> shamt (logical)
  srl(dst_low, src_low, scratch1);

  // If the shift amount is 0, we're done
  Branch(&done, eq, shift, Operand(zero_reg));

  // HIGH32 << (32 - shamt)
  li(scratch2, 32);
  Sub32(scratch2, scratch2, scratch1);
  if (dst_high == src_high) {
    sll(scratch1, scratch3, scratch2);
  } else {
    sll(scratch1, src_high, scratch2);
  }
  // (HIGH32 << (32 - shamt)) | (LOW32 >> shamt)
  Or(dst_low, dst_low, scratch1);

  // If the shift amount is < 32, we're done
  // Note: the shift amount is always < 64, so we can just test if the 6th bit
  // is set
  And(scratch1, shift, 32);
  Branch(&done, eq, scratch1, Operand(zero_reg));
  Move(dst_low, dst_high);
  Sra32(dst_high, dst_high, 31);

  bind(&done);
}

void MacroAssembler::SarPair(Register dst_low, Register dst_high,
                             Register src_low, Register src_high, int32_t shift,
                             Register scratch1, Register scratch2) {
  DCHECK_GE(63, shift);
  DCHECK_NE(dst_low, src_high);
  DCHECK_NE(dst_high, src_high);
  shift = shift & 0x3F;
  if (shift == 0) {
    mv(dst_low, src_low);
    mv(dst_high, src_high);
  } else if (shift < 32) {
    srli(dst_low, src_low, shift);
    srai(dst_high, src_high, shift);
    slli(scratch1, src_high, 32 - shift);
    Or(dst_low, dst_low, scratch1);
  } else if (shift == 32) {
    srai(dst_high, src_high, 31);
    mv(dst_low, src_high);
  } else {
    srai(dst_high, src_high, 31);
    srai(dst_low, src_high, shift - 32);
  }
}
#endif

void MacroAssembler::ExtractBits(Register rt, Register rs, uint16_t pos,
                                 uint16_t size, bool sign_extend) {
#if V8_TARGET_ARCH_RISCV64
  DCHECK(pos < 64 && 0 < size && size <= 64 && 0 < pos + size &&
         pos + size <= 64);
  slli(rt, rs, 64 - (pos + size));
  if (sign_extend) {
    srai(rt, rt, 64 - size);
  } else {
    srli(rt, rt, 64 - size);
  }
#elif V8_TARGET_ARCH_RISCV32
  DCHECK_LT(pos, 32);
  DCHECK_GT(size, 0);
  DCHECK_LE(size, 32);
  DCHECK_GT(pos + size, 0);
  DCHECK_LE(pos + size, 32);
  slli(rt, rs, 32 - (pos + size));
  if (sign_extend) {
    srai(rt, rt, 32 - size);
  } else {
    srli(rt, rt, 32 - size);
  }
#endif
}

void MacroAssembler::InsertBits(Register dest, Register source, Register pos,
                                int size) {
#if V8_TARGET_ARCH_RISCV64
  DCHECK_LT(size, 64);
#elif V8_TARGET_ARCH_RISCV32
  DCHECK_LT(size, 32);
#endif
  UseScratchRegisterScope temps(this);
  Register mask = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register source_ = temps.Acquire();
  // Create a mask of the length=size.
  li(mask, 1);
  slli(mask, mask, size);
  addi(mask, mask, -1);
  and_(source_, mask, source);
  sll(source_, source_, pos);
  // Make a mask containing 0's. 0's start at "pos" with length=size.
  sll(mask, mask, pos);
  not_(mask, mask);
  // cut area for insertion of source.
  and_(dest, mask, dest);
  // insert source
  or_(dest, dest, source_);
}

void MacroAssembler::Neg_s(FPURegister fd, FPURegister fs) { fneg_s(fd, fs); }

void MacroAssembler::Neg_d(FPURegister fd, FPURegister fs) { fneg_d(fd, fs); }

void MacroAssembler::Cvt_d_uw(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  fcvt_d_wu(fd, rs);
}

void MacroAssembler::Cvt_d_w(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  fcvt_d_w(fd, rs);
}
#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Cvt_d_ul(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  fcvt_d_lu(fd, rs);
}
#endif
void MacroAssembler::Cvt_s_uw(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  fcvt_s_wu(fd, rs);
}

void MacroAssembler::Cvt_s_w(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  fcvt_s_w(fd, rs);
}
#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Cvt_s_ul(FPURegister fd, Register rs) {
  // Convert rs to a FP value in fd.
  fcvt_s_lu(fd, rs);
}
#endif
template <typename CvtFunc>
void MacroAssembler::RoundFloatingPointToInteger(Register rd, FPURegister fs,
                                                 Register result,
                                                 CvtFunc fcvt_generator) {
  // Save csr_fflags to scratch & clear exception flags
  if (result.is_valid()) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    int exception_flags = kInvalidOperation;
    csrrci(scratch, csr_fflags, exception_flags);

    // actual conversion instruction
    fcvt_generator(this, rd, fs);

    // check kInvalidOperation flag (out-of-range, NaN)
    // set result to 1 if normal, otherwise set result to 0 for abnormal
    frflags(result);
    andi(result, result, exception_flags);
    seqz(result, result);  // result <-- 1 (normal), result <-- 0 (abnormal)

    // restore csr_fflags
    csrw(csr_fflags, scratch);
  } else {
    // actual conversion instruction
    fcvt_generator(this, rd, fs);
  }
}

void MacroAssembler::Clear_if_nan_d(Register rd, FPURegister fs) {
  Label no_nan;
  DCHECK_NE(kScratchReg, rd);
  feq_d(kScratchReg, fs, fs);
  bnez(kScratchReg, &no_nan);
  Move(rd, zero_reg);
  bind(&no_nan);
}

void MacroAssembler::Clear_if_nan_s(Register rd, FPURegister fs) {
  Label no_nan;
  DCHECK_NE(kScratchReg, rd);
  feq_s(kScratchReg, fs, fs);
  bnez(kScratchReg, &no_nan);
  Move(rd, zero_reg);
  bind(&no_nan);
}

void MacroAssembler::Trunc_uw_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_wu_d(dst, src, RTZ);
      });
}

void MacroAssembler::Trunc_w_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_w_d(dst, src, RTZ);
      });
}

void MacroAssembler::Trunc_uw_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_wu_s(dst, src, RTZ);
      });
}

void MacroAssembler::Trunc_w_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_w_s(dst, src, RTZ);
      });
}
#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Trunc_ul_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_lu_d(dst, src, RTZ);
      });
}

void MacroAssembler::Trunc_l_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_l_d(dst, src, RTZ);
      });
}

void MacroAssembler::Trunc_ul_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_lu_s(dst, src, RTZ);
      });
}

void MacroAssembler::Trunc_l_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_l_s(dst, src, RTZ);
      });
}
#endif
void MacroAssembler::Round_w_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_w_s(dst, src, RNE);
      });
}

void MacroAssembler::Round_w_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_w_d(dst, src, RNE);
      });
}

void MacroAssembler::Ceil_w_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_w_s(dst, src, RUP);
      });
}

void MacroAssembler::Ceil_w_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_w_d(dst, src, RUP);
      });
}

void MacroAssembler::Floor_w_s(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_w_s(dst, src, RDN);
      });
}

void MacroAssembler::Floor_w_d(Register rd, FPURegister fs, Register result) {
  RoundFloatingPointToInteger(
      rd, fs, result, [](MacroAssembler* masm, Register dst, FPURegister src) {
        masm->fcvt_w_d(dst, src, RDN);
      });
}

// According to JS ECMA specification, for floating-point round operations, if
// the input is NaN, +/-infinity, or +/-0, the same input is returned as the
// rounded result; this differs from behavior of RISCV fcvt instructions (which
// round out-of-range values to the nearest max or min value), therefore special
// handling is needed by NaN, +/-Infinity, +/-0
#if V8_TARGET_ARCH_RISCV64
template <typename F>
void MacroAssembler::RoundHelper(FPURegister dst, FPURegister src,
                                 FPURegister fpu_scratch, FPURoundingMode frm) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();

  DCHECK((std::is_same<float, F>::value) || (std::is_same<double, F>::value));
  // Need at least two FPRs, so check against dst == src == fpu_scratch
  DCHECK(!(dst == src && dst == fpu_scratch));

  const int kFloatMantissaBits =
      sizeof(F) == 4 ? kFloat32MantissaBits : kFloat64MantissaBits;
  const int kFloatExponentBits =
      sizeof(F) == 4 ? kFloat32ExponentBits : kFloat64ExponentBits;
  const int kFloatExponentBias =
      sizeof(F) == 4 ? kFloat32ExponentBias : kFloat64ExponentBias;
  Label done;

  {
    UseScratchRegisterScope temps2(this);
    Register scratch = temps2.Acquire();
    // extract exponent value of the source floating-point to scratch
    if (std::is_same<F, double>::value) {
      fmv_x_d(scratch, src);
    } else {
      fmv_x_w(scratch, src);
    }
    ExtractBits(scratch2, scratch, kFloatMantissaBits, kFloatExponentBits);
  }

  // if src is NaN/+-Infinity/+-Zero or if the exponent is larger than # of bits
  // in mantissa, the result is the same as src, so move src to dest  (to avoid
  // generating another branch)
  if (dst != src) {
    if (std::is_same<F, double>::value) {
      fmv_d(dst, src);
    } else {
      fmv_s(dst, src);
    }
  }
  {
    Label not_NaN;
    UseScratchRegisterScope temps2(this);
    Register scratch = temps2.Acquire();
    // According to the wasm spec
    // (https://webassembly.github.io/spec/core/exec/numerics.html#aux-nans)
    // if input is canonical NaN, then output is canonical NaN, and if input is
    // any other NaN, then output is any NaN with most significant bit of
    // payload is 1. In RISC-V, feq_d will set scratch to 0 if src is a NaN. If
    // src is not a NaN, branch to the label and do nothing, but if it is,
    // fmin_d will set dst to the canonical NaN.
    if (std::is_same<F, double>::value) {
      feq_d(scratch, src, src);
      bnez(scratch, &not_NaN);
      fmin_d(dst, src, src);
    } else {
      feq_s(scratch, src, src);
      bnez(scratch, &not_NaN);
      fmin_s(dst, src, src);
    }
    bind(&not_NaN);
  }

  // If real exponent (i.e., scratch2 - kFloatExponentBias) is greater than
  // kFloat32MantissaBits, it means the floating-point value has no fractional
  // part, thus the input is already rounded, jump to done. Note that, NaN and
  // Infinity in floating-point representation sets maximal exponent value, so
  // they also satisfy (scratch2 - kFloatExponentBias >= kFloatMantissaBits),
  // and JS round semantics specify that rounding of NaN (Infinity) returns NaN
  // (Infinity), so NaN and Infinity are considered rounded value too.
  Branch(&done, greater_equal, scratch2,
         Operand(kFloatExponentBias + kFloatMantissaBits));

  // Actual rounding is needed along this path

  // old_src holds the original input, needed for the case of src == dst
  FPURegister old_src = src;
  if (src == dst) {
    DCHECK(fpu_scratch != dst);
    Move(fpu_scratch, src);
    old_src = fpu_scratch;
  }

  // Since only input whose real exponent value is less than kMantissaBits
  // (i.e., 23 or 52-bits) falls into this path, the value range of the input
  // falls into that of 23- or 53-bit integers. So we round the input to integer
  // values, then convert them back to floating-point.
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    if (std::is_same<F, double>::value) {
      fcvt_l_d(scratch, src, frm);
      fcvt_d_l(dst, scratch, frm);
    } else {
      fcvt_w_s(scratch, src, frm);
      fcvt_s_w(dst, scratch, frm);
    }
  }
  // A special handling is needed if the input is a very small positive/negative
  // number that rounds to zero. JS semantics requires that the rounded result
  // retains the sign of the input, so a very small positive (negative)
  // floating-point number should be rounded to positive (negative) 0.
  // Therefore, we use sign-bit injection to produce +/-0 correctly. Instead of
  // testing for zero w/ a branch, we just insert sign-bit for everyone on this
  // path (this is where old_src is needed)
  if (std::is_same<F, double>::value) {
    fsgnj_d(dst, dst, old_src);
  } else {
    fsgnj_s(dst, dst, old_src);
  }

  bind(&done);
}
#elif V8_TARGET_ARCH_RISCV32
// According to JS ECMA specification, for floating-point round operations, if
// the input is NaN, +/-infinity, or +/-0, the same input is returned as the
// rounded result; this differs from behavior of RISCV fcvt instructions (which
// round out-of-range values to the nearest max or min value), therefore special
// handling is needed by NaN, +/-Infinity, +/-0
void MacroAssembler::RoundFloat(FPURegister dst, FPURegister src,
                                FPURegister fpu_scratch, FPURoundingMode frm) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();

  // Need at least two FPRs, so check against dst == src == fpu_scratch
  DCHECK(!(dst == src && dst == fpu_scratch));

  const int kFloatMantissaBits = kFloat32MantissaBits;
  const int kFloatExponentBits = kFloat32ExponentBits;
  const int kFloatExponentBias = kFloat32ExponentBias;
  Label done;

  {
    UseScratchRegisterScope temps2(this);
    Register scratch = temps2.Acquire();
    // extract exponent value of the source floating-point to scratch
    fmv_x_w(scratch, src);
    ExtractBits(scratch2, scratch, kFloatMantissaBits, kFloatExponentBits);
  }

  // if src is NaN/+-Infinity/+-Zero or if the exponent is larger than # of bits
  // in mantissa, the result is the same as src, so move src to dest  (to avoid
  // generating another branch)
  if (dst != src) {
    fmv_s(dst, src);
  }
  {
    Label not_NaN;
    UseScratchRegisterScope temps2(this);
    Register scratch = temps2.Acquire();
    // According to the wasm spec
    // (https://webassembly.github.io/spec/core/exec/numerics.html#aux-nans)
    // if input is canonical NaN, then output is canonical NaN, and if input is
    // any other NaN, then output is any NaN with most significant bit of
    // payload is 1. In RISC-V, feq_d will set scratch to 0 if src is a NaN. If
    // src is not a NaN, branch to the label and do nothing, but if it is,
    // fmin_d will set dst to the canonical NaN.
    feq_s(scratch, src, src);
    bnez(scratch, &not_NaN);
    fmin_s(dst, src, src);
    bind(&not_NaN);
  }

  // If real exponent (i.e., scratch2 - kFloatExponentBias) is greater than
  // kFloat32MantissaBits, it means the floating-point value has no fractional
  // part, thus the input is already rounded, jump to done. Note that, NaN and
  // Infinity in floating-point representation sets maximal exponent value, so
  // they also satisfy (scratch2 - kFloatExponentBias >= kFloatMantissaBits),
  // and JS round semantics specify that rounding of NaN (Infinity) returns NaN
  // (Infinity), so NaN and Infinity are considered rounded value too.
  Branch(&done, greater_equal, scratch2,
         Operand(kFloatExponentBias + kFloatMantissaBits));

  // Actual rounding is needed along this path

  // old_src holds the original input, needed for the case of src == dst
  FPURegister old_src = src;
  if (src == dst) {
    DCHECK(fpu_scratch != dst);
    Move(fpu_scratch, src);
    old_src = fpu_scratch;
  }

  // Since only input whose real exponent value is less than kMantissaBits
  // (i.e., 23 or 52-bits) falls into this path, the value range of the input
  // falls into that of 23- or 53-bit integers. So we round the input to integer
  // values, then convert them back to floating-point.
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    fcvt_w_s(scratch, src, frm);
    fcvt_s_w(dst, scratch, frm);
  }
  // A special handling is needed if the input is a very small positive/negative
  // number that rounds to zero. JS semantics requires that the rounded result
  // retains the sign of the input, so a very small positive (negative)
  // floating-point number should be rounded to positive (negative) 0.
  // Therefore, we use sign-bit injection to produce +/-0 correctly. Instead of
  // testing for zero w/ a branch, we just insert sign-bit for everyone on this
  // path (this is where old_src is needed)
  fsgnj_s(dst, dst, old_src);

  bind(&done);
}
#endif  // V8_TARGET_ARCH_RISCV32
// According to JS ECMA specification, for floating-point round operations, if
// the input is NaN, +/-infinity, or +/-0, the same input is returned as the
// rounded result; this differs from behavior of RISCV fcvt instructions (which
// round out-of-range values to the nearest max or min value), therefore special
// handling is needed by NaN, +/-Infinity, +/-0
template <typename F>
void MacroAssembler::RoundHelper(VRegister dst, VRegister src, Register scratch,
                                 VRegister v_scratch, FPURoundingMode frm,
                                 bool keep_nan_same) {
  VU.set(scratch, std::is_same<F, float>::value ? E32 : E64, m1);
  // if src is NaN/+-Infinity/+-Zero or if the exponent is larger than # of bits
  // in mantissa, the result is the same as src, so move src to dest  (to avoid
  // generating another branch)

  // If real exponent (i.e., scratch2 - kFloatExponentBias) is greater than
  // kFloat32MantissaBits, it means the floating-point value has no fractional
  // part, thus the input is already rounded, jump to done. Note that, NaN and
  // Infinity in floating-point representation sets maximal exponent value, so
  // they also satisfy (scratch2 - kFloatExponentBias >= kFloatMantissaBits),
  // and JS round semantics specify that rounding of NaN (Infinity) returns NaN
  // (Infinity), so NaN and Infinity are considered rounded value too.
  const int kFloatMantissaBits =
      sizeof(F) == 4 ? kFloat32MantissaBits : kFloat64MantissaBits;
  const int kFloatExponentBits =
      sizeof(F) == 4 ? kFloat32ExponentBits : kFloat64ExponentBits;
  const int kFloatExponentBias =
      sizeof(F) == 4 ? kFloat32ExponentBias : kFloat64ExponentBias;

  // slli(rt, rs, 64 - (pos + size));
  // if (sign_extend) {
  //   srai(rt, rt, 64 - size);
  // } else {
  //   srli(rt, rt, 64 - size);
  // }
  vmv_vx(v_scratch, zero_reg);
  li(scratch, 64 - kFloatMantissaBits - kFloatExponentBits);
  vsll_vx(v_scratch, src, scratch);
  li(scratch, 64 - kFloatExponentBits);
  vsrl_vx(v_scratch, v_scratch, scratch);
  li(scratch, kFloatExponentBias + kFloatMantissaBits);
  vmslt_vx(v0, v_scratch, scratch);
  VU.set(frm);
  vmv_vv(dst, src);
  if (dst == src) {
    vmv_vv(v_scratch, src);
  }
  vfcvt_x_f_v(dst, src, MaskType::Mask);
  vfcvt_f_x_v(dst, dst, MaskType::Mask);

  // A special handling is needed if the input is a very small positive/negative
  // number that rounds to zero. JS semantics requires that the rounded result
  // retains the sign of the input, so a very small positive (negative)
  // floating-point number should be rounded to positive (negative) 0.
  if (dst == src) {
    vfsngj_vv(dst, dst, v_scratch);
  } else {
    vfsngj_vv(dst, dst, src);
  }
  if (!keep_nan_same) {
    vmfeq_vv(v0, src, src);
    vnot_vv(v0, v0);
    if (std::is_same<F, float>::value) {
      fmv_w_x(kScratchDoubleReg, zero_reg);
    } else {
#ifdef V8_TARGET_ARCH_RISCV64
      fmv_d_x(kScratchDoubleReg, zero_reg);
#elif V8_TARGET_ARCH_RISCV32
      fcvt_d_w(kScratchDoubleReg, zero_reg);
#endif
    }
    vfadd_vf(dst, src, kScratchDoubleReg, MaskType::Mask);
  }
}

void MacroAssembler::Ceil_f(VRegister vdst, VRegister vsrc, Register scratch,
                            VRegister v_scratch) {
  RoundHelper<float>(vdst, vsrc, scratch, v_scratch, RUP, false);
}

void MacroAssembler::Ceil_d(VRegister vdst, VRegister vsrc, Register scratch,
                            VRegister v_scratch) {
  RoundHelper<double>(vdst, vsrc, scratch, v_scratch, RUP, false);
}

void MacroAssembler::Floor_f(VRegister vdst, VRegister vsrc, Register scratch,
                             VRegister v_scratch) {
  RoundHelper<float>(vdst, vsrc, scratch, v_scratch, RDN, false);
}

void MacroAssembler::Floor_d(VRegister vdst, VRegister vsrc, Register scratch,
                             VRegister v_scratch) {
  RoundHelper<double>(vdst, vsrc, scratch, v_scratch, RDN, false);
}

void MacroAssembler::Trunc_d(VRegister vdst, VRegister vsrc, Register scratch,
                             VRegister v_scratch) {
  RoundHelper<double>(vdst, vsrc, scratch, v_scratch, RTZ, false);
}

void MacroAssembler::Trunc_f(VRegister vdst, VRegister vsrc, Register scratch,
                             VRegister v_scratch) {
  RoundHelper<float>(vdst, vsrc, scratch, v_scratch, RTZ, false);
}

void MacroAssembler::Round_f(VRegister vdst, VRegister vsrc, Register scratch,
                             VRegister v_scratch) {
  RoundHelper<float>(vdst, vsrc, scratch, v_scratch, RNE, false);
}

void MacroAssembler::Round_d(VRegister vdst, VRegister vsrc, Register scratch,
                             VRegister v_scratch) {
  RoundHelper<double>(vdst, vsrc, scratch, v_scratch, RNE, false);
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Floor_d_d(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<double>(dst, src, fpu_scratch, RDN);
}

void MacroAssembler::Ceil_d_d(FPURegister dst, FPURegister src,
                              FPURegister fpu_scratch) {
  RoundHelper<double>(dst, src, fpu_scratch, RUP);
}

void MacroAssembler::Trunc_d_d(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<double>(dst, src, fpu_scratch, RTZ);
}

void MacroAssembler::Round_d_d(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
  RoundHelper<double>(dst, src, fpu_scratch, RNE);
}
#endif

void MacroAssembler::Floor_s_s(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
#if V8_TARGET_ARCH_RISCV64
  RoundHelper<float>(dst, src, fpu_scratch, RDN);
#elif V8_TARGET_ARCH_RISCV32
  RoundFloat(dst, src, fpu_scratch, RDN);
#endif
}

void MacroAssembler::Ceil_s_s(FPURegister dst, FPURegister src,
                              FPURegister fpu_scratch) {
#if V8_TARGET_ARCH_RISCV64
  RoundHelper<float>(dst, src, fpu_scratch, RUP);
#elif V8_TARGET_ARCH_RISCV32
  RoundFloat(dst, src, fpu_scratch, RUP);
#endif
}

void MacroAssembler::Trunc_s_s(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
#if V8_TARGET_ARCH_RISCV64
  RoundHelper<float>(dst, src, fpu_scratch, RTZ);
#elif V8_TARGET_ARCH_RISCV32
  RoundFloat(dst, src, fpu_scratch, RTZ);
#endif
}

void MacroAssembler::Round_s_s(FPURegister dst, FPURegister src,
                               FPURegister fpu_scratch) {
#if V8_TARGET_ARCH_RISCV64
  RoundHelper<float>(dst, src, fpu_scratch, RNE);
#elif V8_TARGET_ARCH_RISCV32
  RoundFloat(dst, src, fpu_scratch, RNE);
#endif
}

void MacroAssembler::Madd_s(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft) {
  fmadd_s(fd, fs, ft, fr);
}

void MacroAssembler::Madd_d(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft) {
  fmadd_d(fd, fs, ft, fr);
}

void MacroAssembler::Msub_s(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft) {
  fmsub_s(fd, fs, ft, fr);
}

void MacroAssembler::Msub_d(FPURegister fd, FPURegister fr, FPURegister fs,
                            FPURegister ft) {
  fmsub_d(fd, fs, ft, fr);
}

void MacroAssembler::CompareF32(Register rd, FPUCondition cc, FPURegister cmp1,
                                FPURegister cmp2) {
  switch (cc) {
    case EQ:
      feq_s(rd, cmp1, cmp2);
      break;
    case NE:
      feq_s(rd, cmp1, cmp2);
      NegateBool(rd, rd);
      break;
    case LT:
      flt_s(rd, cmp1, cmp2);
      break;
    case GE:
      fle_s(rd, cmp2, cmp1);
      break;
    case LE:
      fle_s(rd, cmp1, cmp2);
      break;
    case GT:
      flt_s(rd, cmp2, cmp1);
      break;
    default:
      UNREACHABLE();
  }
}

void MacroAssembler::CompareF64(Register rd, FPUCondition cc, FPURegister cmp1,
                                FPURegister cmp2) {
  switch (cc) {
    case EQ:
      feq_d(rd, cmp1, cmp2);
      break;
    case NE:
      feq_d(rd, cmp1, cmp2);
      NegateBool(rd, rd);
      break;
    case LT:
      flt_d(rd, cmp1, cmp2);
      break;
    case GE:
      fle_d(rd, cmp2, cmp1);
      break;
    case LE:
      fle_d(rd, cmp1, cmp2);
      break;
    case GT:
      flt_d(rd, cmp2, cmp1);
      break;
    default:
      UNREACHABLE();
  }
}

void MacroAssembler::CompareIsNotNanF32(Register rd, FPURegister cmp1,
                                        FPURegister cmp2) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.Acquire();

  feq_s(rd, cmp1, cmp1);       // rd <- !isNan(cmp1)
  feq_s(scratch, cmp2, cmp2);  // scratch <- !isNaN(cmp2)
  And(rd, rd, scratch);        // rd <- !isNan(cmp1) && !isNan(cmp2)
}

void MacroAssembler::CompareIsNotNanF64(Register rd, FPURegister cmp1,
                                        FPURegister cmp2) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = temps.Acquire();

  feq_d(rd, cmp1, cmp1);       // rd <- !isNan(cmp1)
  feq_d(scratch, cmp2, cmp2);  // scratch <- !isNaN(cmp2)
  And(rd, rd, scratch);        // rd <- !isNan(cmp1) && !isNan(cmp2)
}

void MacroAssembler::CompareIsNanF32(Register rd, FPURegister cmp1,
                                     FPURegister cmp2) {
  CompareIsNotNanF32(rd, cmp1, cmp2);  // rd <- !isNan(cmp1) && !isNan(cmp2)
  Xor(rd, rd, 1);                      // rd <- isNan(cmp1) || isNan(cmp2)
}

void MacroAssembler::CompareIsNanF64(Register rd, FPURegister cmp1,
                                     FPURegister cmp2) {
  CompareIsNotNanF64(rd, cmp1, cmp2);  // rd <- !isNan(cmp1) && !isNan(cmp2)
  Xor(rd, rd, 1);                      // rd <- isNan(cmp1) || isNan(cmp2)
}

void MacroAssembler::BranchTrueShortF(Register rs, Label* target) {
  Branch(target, not_equal, rs, Operand(zero_reg));
}

void MacroAssembler::BranchFalseShortF(Register rs, Label* target) {
  Branch(target, equal, rs, Operand(zero_reg));
}

void MacroAssembler::BranchTrueF(Register rs, Label* target) {
  bool long_branch =
      target->is_bound() ? !is_near(target) : is_trampoline_emitted();
  if (long_branch) {
    Label skip;
    BranchFalseShortF(rs, &skip);
    BranchLong(target);
    bind(&skip);
  } else {
    BranchTrueShortF(rs, target);
  }
}

void MacroAssembler::BranchFalseF(Register rs, Label* target) {
  bool long_branch =
      target->is_bound() ? !is_near(target) : is_trampoline_emitted();
  if (long_branch) {
    Label skip;
    BranchTrueShortF(rs, &skip);
    BranchLong(target);
    bind(&skip);
  } else {
    BranchFalseShortF(rs, target);
  }
}

void MacroAssembler::InsertHighWordF64(FPURegister dst, Register src_high) {
#if V8_TARGET_ARCH_RISCV64
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);

  DCHECK(src_high != scratch2 && src_high != scratch);

  fmv_x_d(scratch, dst);
  slli(scratch2, src_high, 32);
  slli(scratch, scratch, 32);
  srli(scratch, scratch, 32);
  or_(scratch, scratch, scratch2);
  fmv_d_x(dst, scratch);
#elif V8_TARGET_ARCH_RISCV32
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Add32(sp, sp, Operand(-8));
  StoreDouble(dst, MemOperand(sp, 0));
  Sw(src_high, MemOperand(sp, 4));
  LoadDouble(dst, MemOperand(sp, 0));
  Add32(sp, sp, Operand(8));
#endif
}

void MacroAssembler::InsertLowWordF64(FPURegister dst, Register src_low) {
#if V8_TARGET_ARCH_RISCV64
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);

  DCHECK(src_low != scratch && src_low != scratch2);
  fmv_x_d(scratch, dst);
  slli(scratch2, src_low, 32);
  srli(scratch2, scratch2, 32);
  srli(scratch, scratch, 32);
  slli(scratch, scratch, 32);
  or_(scratch, scratch, scratch2);
  fmv_d_x(dst, scratch);
#elif V8_TARGET_ARCH_RISCV32
  BlockTrampolinePoolScope block_trampoline_pool(this);
  AddWord(sp, sp, Operand(-8));
  StoreDouble(dst, MemOperand(sp, 0));
  Sw(src_low, MemOperand(sp, 0));
  LoadDouble(dst, MemOperand(sp, 0));
  AddWord(sp, sp, Operand(8));
#endif
}

void MacroAssembler::LoadFPRImmediate(FPURegister dst, uint32_t src) {
  ASM_CODE_COMMENT(this);
  // Handle special values first.
  if (src == base::bit_cast<uint32_t>(0.0f) && has_single_zero_reg_set_) {
    if (dst != kSingleRegZero) fmv_s(dst, kSingleRegZero);
  } else if (src == base::bit_cast<uint32_t>(-0.0f) &&
             has_single_zero_reg_set_) {
    Neg_s(dst, kSingleRegZero);
  } else {
    if (dst == kSingleRegZero) {
      DCHECK(src == base::bit_cast<uint32_t>(0.0f));
      fcvt_s_w(dst, zero_reg);
      has_single_zero_reg_set_ = true;
    } else {
      if (src == base::bit_cast<uint32_t>(0.0f)) {
        fcvt_s_w(dst, zero_reg);
      } else {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();
        li(scratch, Operand(static_cast<int32_t>(src)));
        fmv_w_x(dst, scratch);
      }
    }
  }
}

void MacroAssembler::LoadFPRImmediate(FPURegister dst, uint64_t src) {
  ASM_CODE_COMMENT(this);
  // Handle special values first.
  if (src == base::bit_cast<uint64_t>(0.0) && has_double_zero_reg_set_) {
    if (dst != kDoubleRegZero) fmv_d(dst, kDoubleRegZero);
  } else if (src == base::bit_cast<uint64_t>(-0.0) &&
             has_double_zero_reg_set_) {
    Neg_d(dst, kDoubleRegZero);
  } else {
#if V8_TARGET_ARCH_RISCV64
    if (dst == kDoubleRegZero) {
      DCHECK(src == base::bit_cast<uint64_t>(0.0));
      fcvt_d_l(dst, zero_reg);
      has_double_zero_reg_set_ = true;
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      if (src == base::bit_cast<uint64_t>(0.0)) {
        fcvt_d_l(dst, zero_reg);
      } else {
        li(scratch, Operand(src));
        fmv_d_x(dst, scratch);
      }
    }
#elif V8_TARGET_ARCH_RISCV32
    if (dst == kDoubleRegZero) {
      DCHECK(src == base::bit_cast<uint64_t>(0.0));
      fcvt_d_w(dst, zero_reg);
      has_double_zero_reg_set_ = true;
    } else {
      // Todo: need to clear the stack content?
      if (src == base::bit_cast<uint64_t>(0.0)) {
        fcvt_d_w(dst, zero_reg);
      } else {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();
        uint32_t low_32 = src & 0xffffffffull;
        uint32_t up_32 = src >> 32;
        AddWord(sp, sp, Operand(-8));
        li(scratch, Operand(static_cast<int32_t>(low_32)));
        Sw(scratch, MemOperand(sp, 0));
        li(scratch, Operand(static_cast<int32_t>(up_32)));
        Sw(scratch, MemOperand(sp, 4));
        LoadDouble(dst, MemOperand(sp, 0));
        AddWord(sp, sp, Operand(8));
      }
    }
#endif
  }
}

void MacroAssembler::CompareI(Register rd, Register rs, const Operand& rt,
                              Condition cond) {
  switch (cond) {
    case eq:
      Seq(rd, rs, rt);
      break;
    case ne:
      Sne(rd, rs, rt);
      break;

    // Signed comparison.
    case greater:
      Sgt(rd, rs, rt);
      break;
    case greater_equal:
      Sge(rd, rs, rt);  // rs >= rt
      break;
    case less:
      Slt(rd, rs, rt);  // rs < rt
      break;
    case less_equal:
      Sle(rd, rs, rt);  // rs <= rt
      break;

    // Unsigned comparison.
    case Ugreater:
      Sgtu(rd, rs, rt);  // rs > rt
      break;
    case Ugreater_equal:
      Sgeu(rd, rs, rt);  // rs >= rt
      break;
    case Uless:
      Sltu(rd, rs, rt);  // rs < rt
      break;
    case Uless_equal:
      Sleu(rd, rs, rt);  // rs <= rt
      break;
    case cc_always:
      UNREACHABLE();
    default:
      UNREACHABLE();
  }
}

// dest <- (condition != 0 ? zero : dest)
void MacroAssembler::LoadZeroIfConditionNotZero(Register dest,
                                                Register condition) {
  if (CpuFeatures::IsSupported(ZICOND)) {
    czero_nez(dest, dest, condition);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    seqz(scratch, condition);
    // neg + and may be more efficient than mul(dest, dest, scratch)
    neg(scratch, scratch);  // 0 is still 0, 1 becomes all 1s
    and_(dest, dest, scratch);
  }
}

// dest <- (condition == 0 ? 0 : dest)
void MacroAssembler::LoadZeroIfConditionZero(Register dest,
                                             Register condition) {
  if (CpuFeatures::IsSupported(ZICOND)) {
    czero_eqz(dest, dest, condition);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    snez(scratch, condition);
    //  neg + and may be more efficient than mul(dest, dest, scratch);
    neg(scratch, scratch);  // 0 is still 0, 1 becomes all 1s
    and_(dest, dest, scratch);
  }
}

void MacroAssembler::Clz32(Register rd, Register xx) {
  if (CpuFeatures::IsSupported(ZBB)) {
#if V8_TARGET_ARCH_RISCV64
    clzw(rd, xx);
#else
    clz(rd, xx);
#endif
  } else {
    // 32 bit unsigned in lower word: count number of leading zeros.
    //  int n = 32;
    //  unsigned y;

    //  y = x >>16; if (y != 0) { n = n -16; x = y; }
    //  y = x >> 8; if (y != 0) { n = n - 8; x = y; }
    //  y = x >> 4; if (y != 0) { n = n - 4; x = y; }
    //  y = x >> 2; if (y != 0) { n = n - 2; x = y; }
    //  y = x >> 1; if (y != 0) {rd = n - 2; return;}
    //  rd = n - x;

    Label L0, L1, L2, L3, L4;
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register x = rd;
    Register y = temps.Acquire();
    Register n = temps.Acquire();
    DCHECK(xx != y && xx != n);
    Move(x, xx);
    li(n, Operand(32));
#if V8_TARGET_ARCH_RISCV64
    srliw(y, x, 16);
    BranchShort(&L0, eq, y, Operand(zero_reg));
    Move(x, y);
    addiw(n, n, -16);
    bind(&L0);
    srliw(y, x, 8);
    BranchShort(&L1, eq, y, Operand(zero_reg));
    addiw(n, n, -8);
    Move(x, y);
    bind(&L1);
    srliw(y, x, 4);
    BranchShort(&L2, eq, y, Operand(zero_reg));
    addiw(n, n, -4);
    Move(x, y);
    bind(&L2);
    srliw(y, x, 2);
    BranchShort(&L3, eq, y, Operand(zero_reg));
    addiw(n, n, -2);
    Move(x, y);
    bind(&L3);
    srliw(y, x, 1);
    subw(rd, n, x);
    BranchShort(&L4, eq, y, Operand(zero_reg));
    addiw(rd, n, -2);
    bind(&L4);
#elif V8_TARGET_ARCH_RISCV32
    srli(y, x, 16);
    BranchShort(&L0, eq, y, Operand(zero_reg));
    Move(x, y);
    addi(n, n, -16);
    bind(&L0);
    srli(y, x, 8);
    BranchShort(&L1, eq, y, Operand(zero_reg));
    addi(n, n, -8);
    Move(x, y);
    bind(&L1);
    srli(y, x, 4);
    BranchShort(&L2, eq, y, Operand(zero_reg));
    addi(n, n, -4);
    Move(x, y);
    bind(&L2);
    srli(y, x, 2);
    BranchShort(&L3, eq, y, Operand(zero_reg));
    addi(n, n, -2);
    Move(x, y);
    bind(&L3);
    srli(y, x, 1);
    sub(rd, n, x);
    BranchShort(&L4, eq, y, Operand(zero_reg));
    addi(rd, n, -2);
    bind(&L4);
#endif
  }
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Clz64(Register rd, Register xx) {
  if (CpuFeatures::IsSupported(ZBB)) {
    clz(rd, xx);
  } else {
    // 64 bit: count number of leading zeros.
    //  int n = 64;
    //  unsigned y;

    //  y = x >>32; if (y != 0) { n = n - 32; x = y; }
    //  y = x >>16; if (y != 0) { n = n - 16; x = y; }
    //  y = x >> 8; if (y != 0) { n = n - 8; x = y; }
    //  y = x >> 4; if (y != 0) { n = n - 4; x = y; }
    //  y = x >> 2; if (y != 0) { n = n - 2; x = y; }
    //  y = x >> 1; if (y != 0) {rd = n - 2; return;}
    //  rd = n - x;

    Label L0, L1, L2, L3, L4, L5;
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register x = rd;
    Register y = temps.Acquire();
    Register n = temps.Acquire();
    DCHECK(xx != y && xx != n);
    Move(x, xx);
    li(n, Operand(64));
    srli(y, x, 32);
    BranchShort(&L0, eq, y, Operand(zero_reg));
    addiw(n, n, -32);
    Move(x, y);
    bind(&L0);
    srli(y, x, 16);
    BranchShort(&L1, eq, y, Operand(zero_reg));
    addiw(n, n, -16);
    Move(x, y);
    bind(&L1);
    srli(y, x, 8);
    BranchShort(&L2, eq, y, Operand(zero_reg));
    addiw(n, n, -8);
    Move(x, y);
    bind(&L2);
    srli(y, x, 4);
    BranchShort(&L3, eq, y, Operand(zero_reg));
    addiw(n, n, -4);
    Move(x, y);
    bind(&L3);
    srli(y, x, 2);
    BranchShort(&L4, eq, y, Operand(zero_reg));
    addiw(n, n, -2);
    Move(x, y);
    bind(&L4);
    srli(y, x, 1);
    subw(rd, n, x);
    BranchShort(&L5, eq, y, Operand(zero_reg));
    addiw(rd, n, -2);
    bind(&L5);
  }
}
#endif
void MacroAssembler::Ctz32(Register rd, Register rs) {
  if (CpuFeatures::IsSupported(ZBB)) {
#if V8_TARGET_ARCH_RISCV64
    ctzw(rd, rs);
#else
    ctz(rd, rs);
#endif
  } else {
    // Convert trailing zeroes to trailing ones, and bits to their left
    // to zeroes.

    BlockTrampolinePoolScope block_trampoline_pool(this);
    {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      AddWord(scratch, rs, -1);
      Xor(rd, scratch, rs);
      And(rd, rd, scratch);
      // Count number of leading zeroes.
    }
    Clz32(rd, rd);
    {
      // Subtract number of leading zeroes from 32 to get number of trailing
      // ones. Remember that the trailing ones were formerly trailing zeroes.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, 32);
      Sub32(rd, scratch, rd);
    }
  }
}
#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Ctz64(Register rd, Register rs) {
  if (CpuFeatures::IsSupported(ZBB)) {
    ctz(rd, rs);
  } else {
    // Convert trailing zeroes to trailing ones, and bits to their left
    // to zeroes.
    BlockTrampolinePoolScope block_trampoline_pool(this);
    {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      AddWord(scratch, rs, -1);
      Xor(rd, scratch, rs);
      And(rd, rd, scratch);
      // Count number of leading zeroes.
    }
    Clz64(rd, rd);
    {
      // Subtract number of leading zeroes from 64 to get number of trailing
      // ones. Remember that the trailing ones were formerly trailing zeroes.
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      li(scratch, 64);
      SubWord(rd, scratch, rd);
    }
  }
}
#endif
void MacroAssembler::Popcnt32(Register rd, Register rs, Register scratch) {
  if (CpuFeatures::IsSupported(ZBB)) {
#if V8_TARGET_ARCH_RISCV64
    cpopw(rd, rs);
#else
    cpop(rd, rs);
#endif
  } else {
    DCHECK_NE(scratch, rs);
    DCHECK_NE(scratch, rd);
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
    // The number of instruction is 20.
    // uint32_t B0 = 0x55555555;     // (T)~(T)0/3
    // uint32_t B1 = 0x33333333;     // (T)~(T)0/15*3
    // uint32_t B2 = 0x0F0F0F0F;     // (T)~(T)0/255*15
    // uint32_t value = 0x01010101;  // (T)~(T)0/255

    uint32_t shift = 24;
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch2 = temps.Acquire();
    Register value = temps.Acquire();
    DCHECK((rd != value) && (rs != value));
    li(value, 0x01010101);     // value = 0x01010101;
    li(scratch2, 0x55555555);  // B0 = 0x55555555;
    Srl32(scratch, rs, 1);
    And(scratch, scratch, scratch2);
    Sub32(scratch, rs, scratch);
    li(scratch2, 0x33333333);  // B1 = 0x33333333;
    slli(rd, scratch2, 4);
    or_(scratch2, scratch2, rd);
    And(rd, scratch, scratch2);
    Srl32(scratch, scratch, 2);
    And(scratch, scratch, scratch2);
    Add32(scratch, rd, scratch);
    Srl32(rd, scratch, 4);
    Add32(rd, rd, scratch);
    li(scratch2, 0xF);
    Mul32(scratch2, value, scratch2);  // B2 = 0x0F0F0F0F;
    And(rd, rd, scratch2);
    Mul32(rd, rd, value);
    Srl32(rd, rd, shift);
  }
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::Popcnt64(Register rd, Register rs, Register scratch) {
  if (CpuFeatures::IsSupported(ZBB)) {
    cpop(rd, rs);
  } else {
    DCHECK_NE(scratch, rs);
    DCHECK_NE(scratch, rd);
    // uint64_t B0 = 0x5555555555555555l;     // (T)~(T)0/3
    // uint64_t B1 = 0x3333333333333333l;     // (T)~(T)0/15*3
    // uint64_t B2 = 0x0F0F0F0F0F0F0F0Fl;     // (T)~(T)0/255*15
    // uint64_t value = 0x0101010101010101l;  // (T)~(T)0/255
    // uint64_t shift = 24;                   // (sizeof(T) - 1) * BITS_PER_BYTE
    uint64_t shift = 24;
    UseScratchRegisterScope temps(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    Register scratch2 = temps.Acquire();
    Register value = temps.Acquire();
    DCHECK((rd != value) && (rs != value));
    li(value, 0x1111111111111111l);  // value = 0x1111111111111111l;
    li(scratch2, 5);
    Mul64(scratch2, value, scratch2);  // B0 = 0x5555555555555555l;
    Srl64(scratch, rs, 1);
    And(scratch, scratch, scratch2);
    SubWord(scratch, rs, scratch);
    li(scratch2, 3);
    Mul64(scratch2, value, scratch2);  // B1 = 0x3333333333333333l;
    And(rd, scratch, scratch2);
    Srl64(scratch, scratch, 2);
    And(scratch, scratch, scratch2);
    AddWord(scratch, rd, scratch);
    Srl64(rd, scratch, 4);
    AddWord(rd, rd, scratch);
    li(scratch2, 0xF);
    li(value, 0x0101010101010101l);    // value = 0x0101010101010101l;
    Mul64(scratch2, value, scratch2);  // B2 = 0x0F0F0F0F0F0F0F0Fl;
    And(rd, rd, scratch2);
    Mul64(rd, rd, value);
    srli(rd, rd, 32 + shift);
  }
}
#endif
void MacroAssembler::TryInlineTruncateDoubleToI(Register result,
                                                DoubleRegister double_input,
                                                Label* done) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  // if scratch == 1, exception happens during truncation
  Trunc_w_d(result, double_input, scratch);
  // If we had no exceptions (i.e., scratch==1) we are done.
  Branch(done, eq, scratch, Operand(1));
}

void MacroAssembler::TruncateDoubleToI(Isolate* isolate, Zone* zone,
                                       Register result,
                                       DoubleRegister double_input,
                                       StubCallMode stub_mode) {
  Label done;

  TryInlineTruncateDoubleToI(result, double_input, &done);

  // If we fell through then inline version didn't succeed - call stub
  // instead.
  push(ra);
  SubWord(sp, sp, Operand(kDoubleSize));  // Put input on stack.
  fsd(double_input, sp, 0);
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
  LoadWord(result, MemOperand(sp, 0));

  AddWord(sp, sp, Operand(kDoubleSize));
  pop(ra);

  bind(&done);
}

// BRANCH_ARGS_CHECK checks that conditional jump arguments are correct.
#define BRANCH_ARGS_CHECK(cond, rs, rt)                                  \
  DCHECK((cond == cc_always && rs == zero_reg && rt.rm() == zero_reg) || \
         (cond != cc_always && (rs != zero_reg || rt.rm() != zero_reg)))

void MacroAssembler::Branch(int32_t offset) {
  DCHECK(is_int21(offset));
  BranchShort(offset);
}

void MacroAssembler::Branch(int32_t offset, Condition cond, Register rs,
                            const Operand& rt, Label::Distance distance) {
  bool is_near = BranchShortCheck(offset, nullptr, cond, rs, rt);
  DCHECK(is_near);
  USE(is_near);
}

void MacroAssembler::Branch(Label* L) {
  if (L->is_bound()) {
    if (is_near(L)) {
      BranchShort(L);
    } else {
      BranchLong(L);
    }
  } else {
    if (is_trampoline_emitted()) {
      BranchLong(L);
    } else {
      BranchShort(L);
    }
  }
}

void MacroAssembler::Branch(Label* L, Condition cond, Register rs,
                            const Operand& rt, Label::Distance distance) {
  if (L->is_bound()) {
    if (!BranchShortCheck(0, L, cond, rs, rt)) {
      if (cond != cc_always) {
        Label skip;
        Condition neg_cond = NegateCondition(cond);
        BranchShort(&skip, neg_cond, rs, rt);
        BranchLong(L);
        bind(&skip);
      } else {
        BranchLong(L);
        EmitConstPoolWithJumpIfNeeded();
      }
    }
  } else {
    if (is_trampoline_emitted() && distance == Label::Distance::kFar) {
      if (cond != cc_always) {
        Label skip;
        Condition neg_cond = NegateCondition(cond);
        BranchShort(&skip, neg_cond, rs, rt);
        BranchLong(L);
        bind(&skip);
      } else {
        BranchLong(L);
        EmitConstPoolWithJumpIfNeeded();
      }
    } else {
      BranchShort(L, cond, rs, rt);
    }
  }
}

void MacroAssembler::Branch(Label* L, Condition cond, Register rs,
                            RootIndex index, Label::Distance distance) {
  UseScratchRegisterScope temps(this);
  Register right = temps.Acquire();
  if (COMPRESS_POINTERS_BOOL) {
    Register left = rs;
    if (V8_STATIC_ROOTS_BOOL && RootsTable::IsReadOnly(index) &&
        is_int12(ReadOnlyRootPtr(index))) {
      left = temps.Acquire();
      Sll32(left, rs, 0);
    }
    LoadTaggedRoot(right, index);
    Branch(L, cond, left, Operand(right));
  } else {
    LoadRoot(right, index);
    Branch(L, cond, rs, Operand(right));
  }
}

void MacroAssembler::CompareTaggedAndBranch(Label* label, Condition cond,
                                            Register r1, const Operand& r2,
                                            bool need_link) {
  if (COMPRESS_POINTERS_BOOL) {
    UseScratchRegisterScope temps(this);
    Register scratch0 = temps.Acquire();
    Sll32(scratch0, r1, 0);
    if (IsZero(r2)) {
      Branch(label, cond, scratch0, Operand(zero_reg));
    } else {
      Register scratch1 = temps.Acquire();
      if (r2.is_reg()) {
        Sll32(scratch1, r2.rm(), 0);
      } else {
        li(scratch1, r2);
      }
      Branch(label, cond, scratch0, Operand(scratch1));
    }
  } else {
    Branch(label, cond, r1, r2);
  }
}

void MacroAssembler::BranchShortHelper(int32_t offset, Label* L) {
  DCHECK(L == nullptr || offset == 0);
  offset = GetOffset(offset, L, OffsetSize::kOffset21);
  j(offset);
}

void MacroAssembler::BranchShort(int32_t offset) {
  DCHECK(is_int21(offset));
  BranchShortHelper(offset, nullptr);
}

void MacroAssembler::BranchShort(Label* L) { BranchShortHelper(0, L); }

int32_t MacroAssembler::GetOffset(int32_t offset, Label* L, OffsetSize bits) {
  if (L) {
    offset = branch_offset_helper(L, bits);
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

bool MacroAssembler::BranchShortHelper(int32_t offset, Label* L, Condition cond,
                                       Register rs, const Operand& rt) {
  DCHECK(L == nullptr || offset == 0);
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register scratch = no_reg;
  if (!rt.is_reg()) {
    if (rt.immediate() == 0) {
      scratch = zero_reg;
    } else {
      scratch = temps.Acquire();
      li(scratch, rt);
    }
  } else {
    scratch = rt.rm();
  }
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    switch (cond) {
      case cc_always:
        if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
        j(offset);
        EmitConstPoolWithJumpIfNeeded();
        break;
      case eq:
        // rs == rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          beq(rs, scratch, offset);
        }
        break;
      case ne:
        // rs != rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          bne(rs, scratch, offset);
        }
        break;

      // Signed comparison.
      case greater:
        // rs > rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          bgt(rs, scratch, offset);
        }
        break;
      case greater_equal:
        // rs >= rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          bge(rs, scratch, offset);
        }
        break;
      case less:
        // rs < rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          blt(rs, scratch, offset);
        }
        break;
      case less_equal:
        // rs <= rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          ble(rs, scratch, offset);
        }
        break;

      // Unsigned comparison.
      case Ugreater:
        // rs > rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          bgtu(rs, scratch, offset);
        }
        break;
      case Ugreater_equal:
        // rs >= rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          bgeu(rs, scratch, offset);
        }
        break;
      case Uless:
        // rs < rt
        if (rt.is_reg() && rs == rt.rm()) {
          break;  // No code needs to be emitted.
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          bltu(rs, scratch, offset);
        }
        break;
      case Uless_equal:
        // rs <= rt
        if (rt.is_reg() && rs == rt.rm()) {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset21)) return false;
          j(offset);
        } else {
          if (!CalculateOffset(L, &offset, OffsetSize::kOffset13)) return false;
          bleu(rs, scratch, offset);
        }
        break;
      default:
        UNREACHABLE();
    }
  }

  CheckTrampolinePoolQuick(1);
  return true;
}

bool MacroAssembler::BranchShortCheck(int32_t offset, Label* L, Condition cond,
                                      Register rs, const Operand& rt) {
  BRANCH_ARGS_CHECK(cond, rs, rt);

  if (!L) {
    DCHECK(is_int13(offset));
    return BranchShortHelper(offset, nullptr, cond, rs, rt);
  } else {
    DCHECK_EQ(offset, 0);
    return BranchShortHelper(0, L, cond, rs, rt);
  }
}

void MacroAssembler::BranchShort(int32_t offset, Condition cond, Register rs,
                                 const Operand& rt) {
  BranchShortCheck(offset, nullptr, cond, rs, rt);
}

void MacroAssembler::BranchShort(Label* L, Condition cond, Register rs,
                                 const Operand& rt) {
  BranchShortCheck(0, L, cond, rs, rt);
}

void MacroAssembler::BranchAndLink(int32_t offset) {
  BranchAndLinkShort(offset);
}

void MacroAssembler::BranchAndLink(int32_t offset, Condition cond, Register rs,
                                   const Operand& rt) {
  bool is_near = BranchAndLinkShortCheck(offset, nullptr, cond, rs, rt);
  DCHECK(is_near);
  USE(is_near);
}

void MacroAssembler::BranchAndLink(Label* L) {
  if (L->is_bound()) {
    if (is_near(L)) {
      BranchAndLinkShort(L);
    } else {
      BranchAndLinkLong(L);
    }
  } else {
    if (is_trampoline_emitted()) {
      BranchAndLinkLong(L);
    } else {
      BranchAndLinkShort(L);
    }
  }
}

void MacroAssembler::BranchAndLink(Label* L, Condition cond, Register rs,
                                   const Operand& rt) {
  if (L->is_bound()) {
    if (!BranchAndLinkShortCheck(0, L, cond, rs, rt)) {
      Label skip;
      Condition neg_cond = NegateCondition(cond);
      BranchShort(&skip, neg_cond, rs, rt);
      BranchAndLinkLong(L);
      bind(&skip);
    }
  } else {
    if (is_trampoline_emitted()) {
      Label skip;
      Condition neg_cond = NegateCondition(cond);
      BranchShort(&skip, neg_cond, rs, rt);
      BranchAndLinkLong(L);
      bind(&skip);
    } else {
      BranchAndLinkShortCheck(0, L, cond, rs, rt);
    }
  }
}

void MacroAssembler::BranchAndLinkShortHelper(int32_t offset, Label* L) {
  DCHECK(L == nullptr || offset == 0);
  offset = GetOffset(offset, L, OffsetSize::kOffset21);
  jal(offset);
}

void MacroAssembler::BranchAndLinkShort(int32_t offset) {
  DCHECK(is_int21(offset));
  BranchAndLinkShortHelper(offset, nullptr);
}

void MacroAssembler::BranchAndLinkShort(Label* L) {
  BranchAndLinkShortHelper(0, L);
}

// Pre r6 we need to use a bgezal or bltzal, but they can't be used directly
// with the slt instructions. We could use sub or add instead but we would miss
// overflow cases, so we keep slt and add an intermediate third instruction.
bool MacroAssembler::BranchAndLinkShortHelper(int32_t offset, Label* L,
                                              Condition cond, Register rs,
                                              const Operand& rt) {
  DCHECK(L == nullptr || offset == 0);
  if (!is_near(L, OffsetSize::kOffset21)) return false;

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);

  if (cond == cc_always) {
    offset = GetOffset(offset, L, OffsetSize::kOffset21);
    jal(offset);
  } else {
    Branch(kInstrSize * 2, NegateCondition(cond), rs,
           Operand(GetRtAsRegisterHelper(rt, scratch)));
    offset = GetOffset(offset, L, OffsetSize::kOffset21);
    jal(offset);
  }

  return true;
}

bool MacroAssembler::BranchAndLinkShortCheck(int32_t offset, Label* L,
                                             Condition cond, Register rs,
                                             const Operand& rt) {
  BRANCH_ARGS_CHECK(cond, rs, rt);

  if (!L) {
    DCHECK(is_int21(offset));
    return BranchAndLinkShortHelper(offset, nullptr, cond, rs, rt);
  } else {
    DCHECK_EQ(offset, 0);
    return BranchAndLinkShortHelper(0, L, cond, rs, rt);
  }
}

void MacroAssembler::LoadFromConstantsTable(Register destination,
                                            int constant_index) {
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kBuiltinsConstantsTable));
  LoadRoot(destination, RootIndex::kBuiltinsConstantsTable);
  LoadTaggedField(destination,
                  FieldMemOperand(destination, FixedArray::OffsetOfElementAt(
                                                   constant_index)));
}

void MacroAssembler::LoadRootRelative(Register destination, int32_t offset) {
  LoadWord(destination, MemOperand(kRootRegister, offset));
}

void MacroAssembler::StoreRootRelative(int32_t offset, Register value) {
  StoreWord(value, MemOperand(kRootRegister, offset));
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
        LoadWord(scratch,
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

void MacroAssembler::LoadRootRegisterOffset(Register destination,
                                            intptr_t offset) {
  if (offset == 0) {
    Move(destination, kRootRegister);
  } else {
    AddWord(destination, kRootRegister, Operand(offset));
  }
}

void MacroAssembler::Jump(Register target, Condition cond, Register rs,
                          const Operand& rt) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (cond == cc_always) {
    jr(target);
    ForceConstantPoolEmissionWithoutJump();
  } else {
    BRANCH_ARGS_CHECK(cond, rs, rt);
    Branch(kInstrSize * 2, NegateCondition(cond), rs, rt);
    jr(target);
  }
}

void MacroAssembler::Jump(intptr_t target, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt) {
  Label skip;
  if (cond != cc_always) {
    Branch(&skip, NegateCondition(cond), rs, rt);
  }
  {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    li(t6, Operand(target, rmode));
    Jump(t6, al, zero_reg, Operand(zero_reg));
    EmitConstPoolWithJumpIfNeeded();
    bind(&skip);
  }
}

void MacroAssembler::Jump(Address target, RelocInfo::Mode rmode, Condition cond,
                          Register rs, const Operand& rt) {
  DCHECK(!RelocInfo::IsCodeTarget(rmode));
  Jump(static_cast<intptr_t>(target), rmode, cond, rs, rt);
}

void MacroAssembler::Jump(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt) {
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code));

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    // Inline the trampoline.
    Label skip;
    if (cond != al) Branch(&skip, NegateCondition(cond), rs, rt);
    TailCallBuiltin(builtin);
    bind(&skip);
    return;
  }
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  if (CanUseNearCallOrJump(rmode)) {
    EmbeddedObjectIndex index = AddEmbeddedObject(code);
    DCHECK(is_int32(index));
    Label skip;
    if (cond != al) Branch(&skip, NegateCondition(cond), rs, rt);
    RecordRelocInfo(RelocInfo::RELATIVE_CODE_TARGET,
                    static_cast<int32_t>(index));
    GenPCRelativeJump(t6, static_cast<int32_t>(index));
    bind(&skip);
  } else {
    Jump(code.address(), rmode, cond);
  }
}

void MacroAssembler::Jump(const ExternalReference& reference) {
  li(t6, reference);
  Jump(t6);
}

// Note: To call gcc-compiled C code on riscv64, you must call through t6.
void MacroAssembler::Call(Register target, Condition cond, Register rs,
                          const Operand& rt) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  if (cond == cc_always) {
    jalr(ra, target, 0);
  } else {
    BRANCH_ARGS_CHECK(cond, rs, rt);
    Branch(kInstrSize * 2, NegateCondition(cond), rs, rt);
    jalr(ra, target, 0);
  }
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
  if (lower_limit != 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    SubWord(scratch, value, Operand(lower_limit));
    Branch(on_in_range, Uless_equal, scratch,
           Operand(higher_limit - lower_limit));
  } else {
    Branch(on_in_range, Uless_equal, value,
           Operand(higher_limit - lower_limit));
  }
}

void MacroAssembler::Call(Address target, RelocInfo::Mode rmode, Condition cond,
                          Register rs, const Operand& rt) {
  li(t6, Operand(static_cast<intptr_t>(target), rmode), ADDRESS_LOAD);
  Call(t6, cond, rs, rt);
}

void MacroAssembler::Call(Handle<Code> code, RelocInfo::Mode rmode,
                          Condition cond, Register rs, const Operand& rt) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  DCHECK(RelocInfo::IsCodeTarget(rmode));
  DCHECK_IMPLIES(options().isolate_independent_code,
                 Builtins::IsIsolateIndependentBuiltin(*code));

  Builtin builtin = Builtin::kNoBuiltinId;
  if (isolate()->builtins()->IsBuiltinHandle(code, &builtin)) {
    // Inline the trampoline.
    CHECK_EQ(cond, Condition::al);  // Implement if necessary.
    CallBuiltin(builtin);
    return;
  }

  DCHECK(RelocInfo::IsCodeTarget(rmode));

  if (CanUseNearCallOrJump(rmode)) {
    EmbeddedObjectIndex index = AddEmbeddedObject(code);
    DCHECK(is_int32(index));
    Label skip;
    if (cond != al) Branch(&skip, NegateCondition(cond), rs, rt);
    RecordRelocInfo(RelocInfo::RELATIVE_CODE_TARGET,
                    static_cast<int32_t>(index));
    GenPCRelativeJumpAndLink(t6, static_cast<int32_t>(index));
    bind(&skip);
  } else {
    Call(code.address(), rmode);
  }
}

void MacroAssembler::LoadEntryFromBuiltinIndex(Register builtin_index,
                                               Register target) {
#if V8_TARGET_ARCH_RISCV64
  static_assert(kSystemPointerSize == 8);
#elif V8_TARGET_ARCH_RISCV32
  static_assert(kSystemPointerSize == 4);
#endif
  static_assert(kSmiTagSize == 1);
  static_assert(kSmiTag == 0);

  // The builtin register contains the builtin index as a Smi.
  SmiUntag(target, builtin_index);
  CalcScaledAddress(target, kRootRegister, target, kSystemPointerSizeLog2);
  LoadWord(target,
           MemOperand(target, IsolateData::builtin_entry_table_offset()));
}

void MacroAssembler::CallBuiltinByIndex(Register builtin_index,
                                        Register target) {
  LoadEntryFromBuiltinIndex(builtin_index, target);
  Call(target);
}

void MacroAssembler::CallBuiltin(Builtin builtin) {
  ASM_CODE_COMMENT_STRING(this, CommentForOffHeapTrampoline("call", builtin));
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      li(t6, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Call(t6);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      near_call(static_cast<int>(builtin), RelocInfo::NEAR_BUILTIN_ENTRY);
      break;
    case BuiltinCallJumpMode::kIndirect: {
      LoadEntryFromBuiltin(builtin, t6);
      Call(t6);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        EmbeddedObjectIndex index = AddEmbeddedObject(code);
        DCHECK(is_int32(index));
        RecordRelocInfo(RelocInfo::RELATIVE_CODE_TARGET,
                        static_cast<int32_t>(index));
        GenPCRelativeJumpAndLink(t6, static_cast<int32_t>(index));
      } else {
        LoadEntryFromBuiltin(builtin, t6);
        Call(t6);
      }
      break;
    }
  }
}

void MacroAssembler::TailCallBuiltin(Builtin builtin, Condition cond,
                                     Register type, Operand range) {
  Label done;
  Branch(&done, NegateCondition(cond), type, range);
  TailCallBuiltin(builtin);
  bind(&done);
}

void MacroAssembler::TailCallBuiltin(Builtin builtin) {
  ASM_CODE_COMMENT_STRING(this,
                          CommentForOffHeapTrampoline("tail call", builtin));
  switch (options().builtin_call_jump_mode) {
    case BuiltinCallJumpMode::kAbsolute: {
      li(t6, Operand(BuiltinEntry(builtin), RelocInfo::OFF_HEAP_TARGET));
      Jump(t6);
      break;
    }
    case BuiltinCallJumpMode::kPCRelative:
      near_jump(static_cast<int>(builtin), RelocInfo::NEAR_BUILTIN_ENTRY);
      break;
    case BuiltinCallJumpMode::kIndirect: {
      LoadEntryFromBuiltin(builtin, t6);
      Jump(t6);
      break;
    }
    case BuiltinCallJumpMode::kForMksnapshot: {
      if (options().use_pc_relative_calls_and_jumps_for_mksnapshot) {
        Handle<Code> code = isolate()->builtins()->code_handle(builtin);
        EmbeddedObjectIndex index = AddEmbeddedObject(code);
        DCHECK(is_int32(index));
        RecordRelocInfo(RelocInfo::RELATIVE_CODE_TARGET,
                        static_cast<int32_t>(index));
        GenPCRelativeJump(t6, static_cast<int32_t>(index));
      } else {
        LoadEntryFromBuiltin(builtin, t6);
        Jump(t6);
      }
      break;
    }
  }
}

void MacroAssembler::LoadEntryFromBuiltin(Builtin builtin,
                                          Register destination) {
  LoadWord(destination, EntryFromBuiltinAsOperand(builtin));
}

MemOperand MacroAssembler::EntryFromBuiltinAsOperand(Builtin builtin) {
  DCHECK(root_array_available());
  return MemOperand(kRootRegister,
                    IsolateData::BuiltinEntrySlotOffset(builtin));
}

void MacroAssembler::PatchAndJump(Address target) {
  BlockTrampolinePoolScope block_trampoline_pool(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  auipc(scratch, 0);  // Load PC into scratch
  LoadWord(t6, MemOperand(scratch, kInstrSize * 4));
  jr(t6);
  nop();  // For alignment
#if V8_TARGET_ARCH_RISCV64
  DCHECK_EQ(reinterpret_cast<uint64_t>(pc_) % 8, 0);
#elif V8_TARGET_ARCH_RISCV32
  DCHECK_EQ(reinterpret_cast<uint32_t>(pc_) % 4, 0);
#endif
  *reinterpret_cast<uintptr_t*>(pc_) = target;  // pc_ should be align.
  pc_ += sizeof(uintptr_t);
}

void MacroAssembler::StoreReturnAddressAndCall(Register target) {
  // This generates the final instruction sequence for calls to C functions
  // once an exit frame has been constructed.
  //
  // Note that this assumes the caller code (i.e. the InstructionStream object
  // currently being generated) is immovable or that the callee function cannot
  // trigger GC, since the callee function will return to it.
  //
  // Compute the return address in lr to return to after the jump below. The
  // pc is already at '+ 8' from the current instruction; but return is after
  // three instructions, so add another 4 to pc to get the return address.
  //
  Assembler::BlockTrampolinePoolScope block_trampoline_pool(this);
  int kNumInstructionsToJump = 5;
  if (v8_flags.riscv_c_extension) kNumInstructionsToJump = 4;
  Label find_ra;
  // Adjust the value in ra to point to the correct return location, one
  // instruction past the real call into C code (the jalr(t6)), and push it.
  // This is the return address of the exit frame.
  auipc(ra, 0);  // Set ra the current PC
  bind(&find_ra);
  addi(ra, ra,
       (kNumInstructionsToJump + 1) *
           kInstrSize);  // Set ra to insn after the call

  // This spot was reserved in EnterExitFrame.
  StoreWord(ra, MemOperand(sp));
  addi(sp, sp, -kCArgsSlotsSize);
  // Stack is still aligned.

  // Call the C routine.
  Mv(t6,
     target);  // Function pointer to t6 to conform to ABI for PIC.
  jalr(t6);
  // Make sure the stored 'ra' points to this position.
  DCHECK_EQ(kNumInstructionsToJump, InstructionsGeneratedSince(&find_ra));
}

void MacroAssembler::Ret(Condition cond, Register rs, const Operand& rt) {
  Jump(ra, cond, rs, rt);
  if (cond == al) {
    ForceConstantPoolEmissionWithoutJump();
  }
}

void MacroAssembler::BranchLong(Label* L) {
  // Generate position independent long branch.
  BlockTrampolinePoolScope block_trampoline_pool(this);
  int32_t imm;
  imm = branch_long_offset(L);
  GenPCRelativeJump(t6, imm);
  EmitConstPoolWithJumpIfNeeded();
}

void MacroAssembler::BranchAndLinkLong(Label* L) {
  // Generate position independent long branch and link.
  BlockTrampolinePoolScope block_trampoline_pool(this);
  int32_t imm;
  imm = branch_long_offset(L);
  GenPCRelativeJumpAndLink(t6, imm);
}

void MacroAssembler::DropAndRet(int drop) {
  AddWord(sp, sp, drop * kSystemPointerSize);
  Ret();
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

  AddWord(sp, sp, Operand(count * kSystemPointerSize));

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
    Mv(scratch, reg1);
    Mv(reg1, reg2);
    Mv(reg2, scratch);
  }
}

void MacroAssembler::Call(Label* target) { BranchAndLink(target); }

void MacroAssembler::LoadAddress(Register dst, Label* target,
                                 RelocInfo::Mode rmode) {
  int32_t offset;
  if (CalculateOffset(target, &offset, OffsetSize::kOffset32)) {
    CHECK(is_int32(offset + 0x800));
    int32_t Hi20 = (((int32_t)offset + 0x800) >> 12);
    int32_t Lo12 = (int32_t)offset << 20 >> 20;
    BlockTrampolinePoolScope block_trampoline_pool(this);
    auipc(dst, Hi20);
    addi(dst, dst, Lo12);
  } else {
    uintptr_t address = jump_address(target);
    li(dst, Operand(address, rmode), ADDRESS_LOAD);
  }
}

void MacroAssembler::Push(Tagged<Smi> smi) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(smi));
  push(scratch);
}

void MacroAssembler::PushArray(Register array, Register size,
                               PushArrayOrder order) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  Label loop, entry;
  if (order == PushArrayOrder::kReverse) {
    Mv(scratch, zero_reg);
    jmp(&entry);
    bind(&loop);
    CalcScaledAddress(scratch2, array, scratch, kSystemPointerSizeLog2);
    LoadWord(scratch2, MemOperand(scratch2));
    push(scratch2);
    AddWord(scratch, scratch, Operand(1));
    bind(&entry);
    Branch(&loop, less, scratch, Operand(size));
  } else {
    Mv(scratch, size);
    jmp(&entry);
    bind(&loop);
    CalcScaledAddress(scratch2, array, scratch, kSystemPointerSizeLog2);
    LoadWord(scratch2, MemOperand(scratch2));
    push(scratch2);
    bind(&entry);
    AddWord(scratch, scratch, Operand(-1));
    Branch(&loop, greater_equal, scratch, Operand(zero_reg));
  }
}

void MacroAssembler::Push(Handle<HeapObject> handle) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(handle));
  push(scratch);
}

// ---------------------------------------------------------------------------
// Exception handling.

void MacroAssembler::PushStackHandler() {
  // Adjust this code if not the case.
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize);
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize);

  Push(Smi::zero());  // Padding.

  // Link the current handler as the next handler.
  UseScratchRegisterScope temps(this);
  Register handler_address = temps.Acquire();
  li(handler_address,
     ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  Register handler = temps.Acquire();
  LoadWord(handler, MemOperand(handler_address));
  push(handler);

  // Set this new handler as the current one.
  StoreWord(sp, MemOperand(handler_address));
}

void MacroAssembler::PopStackHandler() {
  static_assert(StackHandlerConstants::kNextOffset == 0);
  pop(a1);
  AddWord(sp, sp,
          Operand(static_cast<intptr_t>(StackHandlerConstants::kSize -
                                        kSystemPointerSize)));
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch,
     ExternalReference::Create(IsolateAddressId::kHandlerAddress, isolate()));
  StoreWord(a1, MemOperand(scratch));
}

void MacroAssembler::FPUCanonicalizeNaN(const DoubleRegister dst,
                                        const DoubleRegister src) {
  // Subtracting 0.0 preserves all inputs except for signalling NaNs, which
  // become quiet NaNs. We use fsub rather than fadd because fsub preserves -0.0
  // inputs: -0.0 + 0.0 = 0.0, but -0.0 - 0.0 = -0.0.
  if (!IsDoubleZeroRegSet()) {
    LoadFPRImmediate(kDoubleRegZero, 0.0);
  }
  fsub_d(dst, src, kDoubleRegZero);
}

void MacroAssembler::MovFromFloatResult(const DoubleRegister dst) {
  Move(dst, fa0);  // Reg fa0 is FP return value.
}

void MacroAssembler::MovFromFloatParameter(const DoubleRegister dst) {
  Move(dst, fa0);  // Reg fa0 is FP first argument value.
}

void MacroAssembler::MovToFloatParameter(DoubleRegister src) { Move(fa0, src); }

void MacroAssembler::MovToFloatResult(DoubleRegister src) { Move(fa0, src); }

void MacroAssembler::MovToFloatParameters(DoubleRegister src1,
                                          DoubleRegister src2) {
  const DoubleRegister fparg2 = fa1;
  if (src2 == fa0) {
    DCHECK(src1 != fparg2);
    Move(fparg2, src2);
    Move(fa0, src1);
  } else {
    Move(fa0, src1);
    Move(fparg2, src2);
  }
}

// -----------------------------------------------------------------------------
// JavaScript invokes.

void MacroAssembler::LoadStackLimit(Register destination, StackLimitKind kind) {
  DCHECK(root_array_available());
  intptr_t offset = kind == StackLimitKind::kRealStackLimit
                        ? IsolateData::real_jslimit_offset()
                        : IsolateData::jslimit_offset();
  LoadWord(destination,
           MemOperand(kRootRegister, static_cast<int32_t>(offset)));
}

void MacroAssembler::StackOverflowCheck(Register num_args, Register scratch1,
                                        Register scratch2,
                                        Label* stack_overflow, Label* done) {
  // Check the stack for overflow. We are not trying to catch
  // interruptions (e.g. debug break and preemption) here, so the "real stack
  // limit" is checked.
  DCHECK(stack_overflow != nullptr || done != nullptr);
  LoadStackLimit(scratch1, StackLimitKind::kRealStackLimit);
  // Make scratch1 the space we have left. The stack might already be overflowed
  // here which will cause scratch1 to become negative.
  SubWord(scratch1, sp, scratch1);
  // Check if the arguments will overflow the stack.
  SllWord(scratch2, num_args, kSystemPointerSizeLog2);
  // Signed comparison.
  if (stack_overflow != nullptr) {
    Branch(stack_overflow, le, scratch1, Operand(scratch2));
  } else if (done != nullptr) {
    Branch(done, gt, scratch1, Operand(scratch2));
  } else {
    UNREACHABLE();
  }
}

void MacroAssembler::InvokePrologue(Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    Label* done, InvokeType type) {
  Label regular_invoke;

  //  a0: actual arguments count
  //  a1: function (passed through to callee)
  //  a2: expected arguments count

  DCHECK_EQ(actual_parameter_count, a0);
  DCHECK_EQ(expected_parameter_count, a2);

  // If overapplication or if the actual argument count is equal to the
  // formal parameter count, no need to push extra undefined values.
  SubWord(expected_parameter_count, expected_parameter_count,
          actual_parameter_count);
  Branch(&regular_invoke, le, expected_parameter_count, Operand(zero_reg));

  Label stack_overflow;
  {
    UseScratchRegisterScope temps(this);
    StackOverflowCheck(expected_parameter_count, temps.Acquire(),
                       temps.Acquire(), &stack_overflow);
  }
  // Underapplication. Move the arguments already in the stack, including the
  // receiver and the return address.
  {
    Label copy;
    Register src = a6, dest = a7;
    Move(src, sp);
    SllWord(t0, expected_parameter_count, kSystemPointerSizeLog2);
    SubWord(sp, sp, Operand(t0));
    // Update stack pointer.
    Move(dest, sp);
    Move(t0, actual_parameter_count);
    bind(&copy);
    LoadWord(t1, MemOperand(src, 0));
    StoreWord(t1, MemOperand(dest, 0));
    SubWord(t0, t0, Operand(1));
    AddWord(src, src, Operand(kSystemPointerSize));
    AddWord(dest, dest, Operand(kSystemPointerSize));
    Branch(&copy, gt, t0, Operand(zero_reg));
  }

  // Fill remaining expected arguments with undefined values.
  LoadRoot(t0, RootIndex::kUndefinedValue);
  {
    Label loop;
    bind(&loop);
    StoreWord(t0, MemOperand(a7, 0));
    SubWord(expected_parameter_count, expected_parameter_count, Operand(1));
    AddWord(a7, a7, Operand(kSystemPointerSize));
    Branch(&loop, gt, expected_parameter_count, Operand(zero_reg));
  }
  Branch(&regular_invoke);

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
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    li(scratch,
       ExternalReference::debug_hook_on_function_call_address(isolate()));
    Lb(scratch, MemOperand(scratch));
    Branch(&skip_hook, eq, scratch, Operand(zero_reg));
  }
  {
    // Load receiver to pass it later to DebugOnFunctionCall hook.
    UseScratchRegisterScope temps(this);
    Register receiver = temps.Acquire();
    LoadReceiver(receiver);

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
    Push(receiver);
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
  // You can't call a function without a valid frame.
  DCHECK_IMPLIES(type == InvokeType::kCall, has_frame());

  // Contract with called JS functions requires that function is passed in a1.
  DCHECK_EQ(function, a1);
  Register expected_parameter_count = a2;
  {
    UseScratchRegisterScope temps(this);
    Register temp_reg = temps.Acquire();
    LoadTaggedField(
        temp_reg,
        FieldMemOperand(function, JSFunction::kSharedFunctionInfoOffset));
    LoadTaggedField(cp, FieldMemOperand(function, JSFunction::kContextOffset));
    // The argument count is stored as uint16_t
    Lhu(expected_parameter_count,
        FieldMemOperand(temp_reg,
                        SharedFunctionInfo::kFormalParameterCountOffset));
  }
  InvokeFunctionCode(function, new_target, expected_parameter_count,
                     actual_parameter_count, type);
}

void MacroAssembler::InvokeFunction(Register function,
                                    Register expected_parameter_count,
                                    Register actual_parameter_count,
                                    InvokeType type) {
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
  Lhu(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
}

void MacroAssembler::GetInstanceTypeRange(Register map, Register type_reg,
                                          InstanceType lower_limit,
                                          Register range) {
  Lhu(type_reg, FieldMemOperand(map, Map::kInstanceTypeOffset));
  SubWord(range, type_reg, Operand(lower_limit));
}
//------------------------------------------------------------------------------
// Wasm
void MacroAssembler::WasmRvvEq(VRegister dst, VRegister lhs, VRegister rhs,
                               VSew sew, Vlmul lmul) {
  VU.set(kScratchReg, sew, lmul);
  vmseq_vv(v0, lhs, rhs);
  li(kScratchReg, -1);
  vmv_vx(dst, zero_reg);
  vmerge_vx(dst, kScratchReg, dst);
}

void MacroAssembler::WasmRvvNe(VRegister dst, VRegister lhs, VRegister rhs,
                               VSew sew, Vlmul lmul) {
  VU.set(kScratchReg, sew, lmul);
  vmsne_vv(v0, lhs, rhs);
  li(kScratchReg, -1);
  vmv_vx(dst, zero_reg);
  vmerge_vx(dst, kScratchReg, dst);
}

void MacroAssembler::WasmRvvGeS(VRegister dst, VRegister lhs, VRegister rhs,
                                VSew sew, Vlmul lmul) {
  VU.set(kScratchReg, sew, lmul);
  vmsle_vv(v0, rhs, lhs);
  li(kScratchReg, -1);
  vmv_vx(dst, zero_reg);
  vmerge_vx(dst, kScratchReg, dst);
}

void MacroAssembler::WasmRvvGeU(VRegister dst, VRegister lhs, VRegister rhs,
                                VSew sew, Vlmul lmul) {
  VU.set(kScratchReg, sew, lmul);
  vmsleu_vv(v0, rhs, lhs);
  li(kScratchReg, -1);
  vmv_vx(dst, zero_reg);
  vmerge_vx(dst, kScratchReg, dst);
}

void MacroAssembler::WasmRvvGtS(VRegister dst, VRegister lhs, VRegister rhs,
                                VSew sew, Vlmul lmul) {
  VU.set(kScratchReg, sew, lmul);
  vmslt_vv(v0, rhs, lhs);
  li(kScratchReg, -1);
  vmv_vx(dst, zero_reg);
  vmerge_vx(dst, kScratchReg, dst);
}

void MacroAssembler::WasmRvvGtU(VRegister dst, VRegister lhs, VRegister rhs,
                                VSew sew, Vlmul lmul) {
  VU.set(kScratchReg, sew, lmul);
  vmsltu_vv(v0, rhs, lhs);
  li(kScratchReg, -1);
  vmv_vx(dst, zero_reg);
  vmerge_vx(dst, kScratchReg, dst);
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::WasmRvvS128const(VRegister dst, const uint8_t imms[16]) {
  uint64_t vals[2];
  memcpy(vals, imms, sizeof(vals));
  VU.set(kScratchReg, E64, m1);
  li(kScratchReg, vals[1]);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  vslideup_vi(dst, kSimd128ScratchReg, 1);
  li(kScratchReg, vals[0]);
  vmv_sx(dst, kScratchReg);
}
#elif V8_TARGET_ARCH_RISCV32
void MacroAssembler::WasmRvvS128const(VRegister dst, const uint8_t imms[16]) {
  uint32_t vals[4];
  memcpy(vals, imms, sizeof(vals));
  VU.set(kScratchReg, VSew::E32, Vlmul::m1);
  li(kScratchReg, vals[3]);
  vmv_vx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, vals[2]);
  vmv_sx(kSimd128ScratchReg, kScratchReg);
  li(kScratchReg, vals[1]);
  vmv_vx(dst, kScratchReg);
  li(kScratchReg, vals[0]);
  vmv_sx(dst, kScratchReg);
  vslideup_vi(dst, kSimd128ScratchReg, 2);
}
#endif

void MacroAssembler::LoadLane(int ts, VRegister dst, uint8_t laneidx,
                              MemOperand src) {
  DCHECK_NE(kScratchReg, src.rm());
  if (ts == 8) {
    Lbu(kScratchReg2, src);
    VU.set(kScratchReg, E32, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    VU.set(kScratchReg, E8, m1);
    vmerge_vx(dst, kScratchReg2, dst);
  } else if (ts == 16) {
    Lhu(kScratchReg2, src);
    VU.set(kScratchReg, E16, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vmerge_vx(dst, kScratchReg2, dst);
  } else if (ts == 32) {
    Load32U(kScratchReg2, src);
    VU.set(kScratchReg, E32, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vmerge_vx(dst, kScratchReg2, dst);
  } else if (ts == 64) {
#if V8_TARGET_ARCH_RISCV64
    LoadWord(kScratchReg2, src);
    VU.set(kScratchReg, E64, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vmerge_vx(dst, kScratchReg2, dst);
#elif V8_TARGET_ARCH_RISCV32
    LoadDouble(kScratchDoubleReg, src);
    VU.set(kScratchReg, E64, m1);
    li(kScratchReg, 0x1 << laneidx);
    vmv_sx(v0, kScratchReg);
    vfmerge_vf(dst, kScratchDoubleReg, dst);
#endif
  } else {
    UNREACHABLE();
  }
}

void MacroAssembler::StoreLane(int sz, VRegister src, uint8_t laneidx,
                               MemOperand dst) {
  DCHECK_NE(kScratchReg, dst.rm());
  if (sz == 8) {
    VU.set(kScratchReg, E8, m1);
    vslidedown_vi(kSimd128ScratchReg, src, laneidx);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    Sb(kScratchReg, dst);
  } else if (sz == 16) {
    VU.set(kScratchReg, E16, m1);
    vslidedown_vi(kSimd128ScratchReg, src, laneidx);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    Sh(kScratchReg, dst);
  } else if (sz == 32) {
    VU.set(kScratchReg, E32, m1);
    vslidedown_vi(kSimd128ScratchReg, src, laneidx);
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    Sw(kScratchReg, dst);
  } else {
    DCHECK_EQ(sz, 64);
    VU.set(kScratchReg, E64, m1);
    vslidedown_vi(kSimd128ScratchReg, src, laneidx);
#if V8_TARGET_ARCH_RISCV64
    vmv_xs(kScratchReg, kSimd128ScratchReg);
    StoreWord(kScratchReg, dst);
#elif V8_TARGET_ARCH_RISCV32
    vfmv_fs(kScratchDoubleReg, kSimd128ScratchReg);
    StoreDouble(kScratchDoubleReg, dst);
#endif
  }
}
// -----------------------------------------------------------------------------
// Runtime calls.
#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::AddOverflow64(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
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
    add(scratch2, left, right_reg);
    xor_(overflow, scratch2, left);
    xor_(scratch, scratch2, right_reg);
    and_(overflow, overflow, scratch);
    Mv(dst, scratch2);
  } else {
    add(dst, left, right_reg);
    xor_(overflow, dst, left);
    xor_(scratch, dst, right_reg);
    and_(overflow, overflow, scratch);
  }
}

void MacroAssembler::SubOverflow64(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
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
    sub(scratch2, left, right_reg);
    xor_(overflow, left, scratch2);
    xor_(scratch, left, right_reg);
    and_(overflow, overflow, scratch);
    Mv(dst, scratch2);
  } else {
    sub(dst, left, right_reg);
    xor_(overflow, left, dst);
    xor_(scratch, left, right_reg);
    and_(overflow, overflow, scratch);
  }
}

void MacroAssembler::MulOverflow32(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  if (!right.is_reg()) {
    li(scratch, Operand(right));
    right_reg = scratch;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch2 && right_reg != scratch2 && dst != scratch2 &&
         overflow != scratch2);
  DCHECK(overflow != left && overflow != right_reg);
  sext_w(overflow, left);
  sext_w(scratch2, right_reg);

  mul(overflow, overflow, scratch2);
  sext_w(dst, overflow);
  xor_(overflow, overflow, dst);
}

void MacroAssembler::MulOverflow64(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  if (!right.is_reg()) {
    li(scratch, Operand(right));
    right_reg = scratch;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch2 && right_reg != scratch2 && dst != scratch2 &&
         overflow != scratch2);
  DCHECK(overflow != left && overflow != right_reg);
  // use this sequence of "mulh/mul" according to recommendation of ISA Spec 7.1
  // upper part
  mulh(scratch2, left, right_reg);
  // lower part
  mul(dst, left, right_reg);
  // expand the sign of the lower part to 64bit
  srai(overflow, dst, 63);
  // if the upper part is not eqaul to the expanded sign bit of the lower part,
  // overflow happens
  xor_(overflow, overflow, scratch2);
}

#elif V8_TARGET_ARCH_RISCV32
void MacroAssembler::AddOverflow(Register dst, Register left,
                                 const Operand& right, Register overflow) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
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
    add(scratch2, left, right_reg);
    xor_(overflow, scratch2, left);
    xor_(scratch, scratch2, right_reg);
    and_(overflow, overflow, scratch);
    Mv(dst, scratch2);
  } else {
    add(dst, left, right_reg);
    xor_(overflow, dst, left);
    xor_(scratch, dst, right_reg);
    and_(overflow, overflow, scratch);
  }
}

void MacroAssembler::SubOverflow(Register dst, Register left,
                                 const Operand& right, Register overflow) {
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
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
    sub(scratch2, left, right_reg);
    xor_(overflow, left, scratch2);
    xor_(scratch, left, right_reg);
    and_(overflow, overflow, scratch);
    Mv(dst, scratch2);
  } else {
    sub(dst, left, right_reg);
    xor_(overflow, left, dst);
    xor_(scratch, left, right_reg);
    and_(overflow, overflow, scratch);
  }
}

void MacroAssembler::MulOverflow32(Register dst, Register left,
                                   const Operand& right, Register overflow) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Register right_reg = no_reg;
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  if (!right.is_reg()) {
    li(scratch, Operand(right));
    right_reg = scratch;
  } else {
    right_reg = right.rm();
  }

  DCHECK(left != scratch2 && right_reg != scratch2 && dst != scratch2 &&
         overflow != scratch2);
  DCHECK(overflow != left && overflow != right_reg);
  mulh(overflow, left, right_reg);
  mul(dst, left, right_reg);
  srai(scratch2, dst, 31);
  xor_(overflow, overflow, scratch2);
}
#endif

void MacroAssembler::CallRuntime(const Runtime::Function* f,
                                 int num_arguments) {
  ASM_CODE_COMMENT(this);
  // All parameters are on the stack. a0 has the return value after call.

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
#if V8_TARGET_ARCH_RISCV64
  CallBuiltin(Builtins::RuntimeCEntry(f->result_size));
#else
  CallBuiltin(Builtins::RuntimeCEntry(1));
#endif
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
  ASM_CODE_COMMENT(this);
  PrepareCEntryFunction(builtin);
  TailCallBuiltin(Builtins::CEntry(1, ArgvMode::kStack, builtin_exit_frame));
}

void MacroAssembler::LoadWeakValue(Register out, Register in,
                                   Label* target_if_cleared) {
  ASM_CODE_COMMENT(this);
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
    // reference table redirects the counter to a uint32_t
    // dummy_stats_counter_ field.
    li(scratch2, ExternalReference::Create(counter));
    Lw(scratch1, MemOperand(scratch2));
    Add32(scratch1, scratch1, Operand(value));
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
    // reference table redirects the counter to a uint32_t
    // dummy_stats_counter_ field.
    li(scratch2, ExternalReference::Create(counter));
    Lw(scratch1, MemOperand(scratch2));
    Sub32(scratch1, scratch1, Operand(value));
    Sw(scratch1, MemOperand(scratch2));
  }
}

// -----------------------------------------------------------------------------
// Debugging.

void MacroAssembler::Trap() { stop(); }
void MacroAssembler::DebugBreak() { stop(); }

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

void MacroAssembler::Check(Condition cc, AbortReason reason, Register rs,
                           Operand rt) {
  Label L;
  BranchShort(&L, cc, rs, rt);
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
    ebreak();
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
      LoadEntryFromBuiltin(Builtin::kAbort, t6);
      Call(t6);
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
  ASM_CODE_COMMENT(this);
  LoadTaggedField(destination, FieldMemOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadCompressedMap(Register dst, Register object) {
  ASM_CODE_COMMENT(this);
  Lw(dst, FieldMemOperand(object, HeapObject::kMapOffset));
}

void MacroAssembler::LoadNativeContextSlot(Register dst, int index) {
  ASM_CODE_COMMENT(this);
  LoadMap(dst, cp);
  LoadTaggedField(
      dst, FieldMemOperand(
               dst, Map::kConstructorOrBackPointerOrNativeContextOffset));
  LoadTaggedField(dst, MemOperand(dst, Context::SlotOffset(index)));
}

void MacroAssembler::StubPrologue(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  li(scratch, Operand(StackFrame::TypeToMarker(type)));
  PushCommonFrame(scratch);
}

void MacroAssembler::Prologue() { PushStandardFrame(a1); }

void MacroAssembler::EnterFrame(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BlockTrampolinePoolScope block_trampoline_pool(this);
  Push(ra, fp);
  Move(fp, sp);
  if (!StackFrame::IsJavaScript(type)) {
    li(scratch, Operand(StackFrame::TypeToMarker(type)));
    Push(scratch);
  }
#if V8_ENABLE_WEBASSEMBLY
  if (type == StackFrame::WASM || type == StackFrame::WASM_LIFTOFF_SETUP)
    Push(kWasmInstanceRegister);
#endif  // V8_ENABLE_WEBASSEMBLY
}

void MacroAssembler::LeaveFrame(StackFrame::Type type) {
  ASM_CODE_COMMENT(this);
  addi(sp, fp, 2 * kSystemPointerSize);
  LoadWord(ra, MemOperand(fp, 1 * kSystemPointerSize));
  LoadWord(fp, MemOperand(fp, 0 * kSystemPointerSize));
}

void MacroAssembler::EnterExitFrame(Register scratch, int stack_space,
                                    StackFrame::Type frame_type) {
  ASM_CODE_COMMENT(this);
  DCHECK(frame_type == StackFrame::EXIT ||
         frame_type == StackFrame::BUILTIN_EXIT ||
         frame_type == StackFrame::API_ACCESSOR_EXIT ||
         frame_type == StackFrame::API_CALLBACK_EXIT);

  // Set up the frame structure on the stack.
  static_assert(2 * kSystemPointerSize ==
                ExitFrameConstants::kCallerSPDisplacement);
  static_assert(1 * kSystemPointerSize == ExitFrameConstants::kCallerPCOffset);
  static_assert(0 * kSystemPointerSize == ExitFrameConstants::kCallerFPOffset);

  // This is how the stack will look:
  // fp + 2 (==kCallerSPDisplacement) - old stack's end
  // [fp + 1 (==kCallerPCOffset)] - saved old ra
  // [fp + 0 (==kCallerFPOffset)] - saved old fp
  // [fp - 1 StackFrame::EXIT Smi
  // [fp - 2 (==kSPOffset)] - sp of the called function
  // fp - (2 + stack_space + alignment) == sp == [fp - kSPOffset] - top of the
  //   new stack (will contain saved ra)

  using ER = ExternalReference;

  // Save registers and reserve room for saved entry sp.
  addi(sp, sp,
       -2 * kSystemPointerSize - ExitFrameConstants::kFixedFrameSizeFromFp);
  StoreWord(ra, MemOperand(sp, 3 * kSystemPointerSize));
  StoreWord(fp, MemOperand(sp, 2 * kSystemPointerSize));

  li(scratch, Operand(StackFrame::TypeToMarker(frame_type)));
  StoreWord(scratch, MemOperand(sp, 1 * kSystemPointerSize));
  // Set up new frame pointer.
  addi(fp, sp, ExitFrameConstants::kFixedFrameSizeFromFp);

  if (v8_flags.debug_code) {
    StoreWord(zero_reg, MemOperand(fp, ExitFrameConstants::kSPOffset));
  }

  // Save the frame pointer and the context in top.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  StoreWord(fp, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));
  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  StoreWord(cp, ExternalReferenceAsOperand(context_address, no_reg));

  const int frame_alignment = MacroAssembler::ActivationFrameAlignment();

  // Reserve place for the return address, stack space and an optional slot
  // (used by DirectCEntry to hold the return value if a struct is
  // returned) and align the frame preparing for calling the runtime function.
  DCHECK_GE(stack_space, 0);
  SubWord(sp, sp, Operand((stack_space + 1) * kSystemPointerSize));
  if (frame_alignment > 0) {
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    And(sp, sp, Operand(-frame_alignment));  // Align stack.
  }

  // Set the exit frame sp value to point just before the return address
  // location.
  addi(scratch, sp, kSystemPointerSize);
  StoreWord(scratch, MemOperand(fp, ExitFrameConstants::kSPOffset));
}

void MacroAssembler::LeaveExitFrame(Register scratch) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  using ER = ExternalReference;
  // Clear top frame.
  // Restore current context from top and clear it in debug mode.
  ER context_address = ER::Create(IsolateAddressId::kContextAddress, isolate());
  LoadWord(cp, ExternalReferenceAsOperand(context_address, no_reg));

  if (v8_flags.debug_code) {
    li(scratch, Operand(Context::kInvalidContext));
    StoreWord(scratch, ExternalReferenceAsOperand(context_address, no_reg));
  }

  // Clear the top frame.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, isolate());
  StoreWord(zero_reg, ExternalReferenceAsOperand(c_entry_fp_address, no_reg));

  // Pop the arguments, restore registers, and return.
  Mv(sp, fp);  // Respect ABI stack constraint.
  LoadWord(fp, MemOperand(sp, ExitFrameConstants::kCallerFPOffset));
  LoadWord(ra, MemOperand(sp, ExitFrameConstants::kCallerPCOffset));

  addi(sp, sp, 2 * kSystemPointerSize);
}

int MacroAssembler::ActivationFrameAlignment() {
#if V8_HOST_ARCH_RISCV32 || V8_HOST_ARCH_RISCV64
  // Running on the real platform. Use the alignment as mandated by the local
  // environment.
  // Note: This will break if we ever start generating snapshots on one RISC-V
  // platform for another RISC-V platform with a different alignment.
  return base::OS::ActivationFrameAlignment();
#else   // V8_HOST_ARCH_RISCV64
  // If we are using the simulator then we should always align to the expected
  // alignment. As the simulator is used to generate snapshots we do not know
  // if the target platform will need alignment, so this is controlled from a
  // flag.
  return v8_flags.sim_stack_alignment;
#endif  // V8_HOST_ARCH_RISCV64
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
        BranchShort(&alignment_as_expected, eq, scratch, Operand(zero_reg));
      }
      // Don't use Check here, as it will call Runtime_Abort re-entering here.
      ebreak();
      bind(&alignment_as_expected);
    }
  }
}

void MacroAssembler::SmiUntag(Register dst, const MemOperand& src) {
  ASM_CODE_COMMENT(this);
  if (SmiValuesAre32Bits()) {
    Lw(dst, MemOperand(src.rm(), SmiWordOffset(src.offset())));
  } else {
    DCHECK(SmiValuesAre31Bits());
    if (COMPRESS_POINTERS_BOOL) {
      Lw(dst, src);
    } else {
      LoadWord(dst, src);
    }
    SmiUntag(dst);
  }
}

void MacroAssembler::SmiToInt32(Register smi) {
  ASM_CODE_COMMENT(this);
  if (v8_flags.enable_slow_asserts) {
    AssertSmi(smi);
  }
  DCHECK(SmiValuesAre32Bits() || SmiValuesAre31Bits());
  SmiUntag(smi);
}

void MacroAssembler::JumpIfSmi(Register value, Label* smi_label,
                               Label::Distance distance) {
  ASM_CODE_COMMENT(this);
  DCHECK_EQ(0, kSmiTag);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  andi(scratch, value, kSmiTagMask);
  Branch(smi_label, eq, scratch, Operand(zero_reg), distance);
}

void MacroAssembler::JumpIfCodeIsMarkedForDeoptimization(
    Register code, Register scratch, Label* if_marked_for_deoptimization) {
  Load32U(scratch, FieldMemOperand(code, Code::kFlagsOffset));
  And(scratch, scratch, Operand(1 << Code::kMarkedForDeoptimizationBit));
  Branch(if_marked_for_deoptimization, ne, scratch, Operand(zero_reg));
}

Operand MacroAssembler::ClearedValue() const {
  return Operand(static_cast<int32_t>(i::ClearedValue(isolate()).ptr()));
}

void MacroAssembler::JumpIfNotSmi(Register value, Label* not_smi_label,
                                  Label::Distance distance) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  DCHECK_EQ(0, kSmiTag);
  andi(scratch, value, kSmiTagMask);
  Branch(not_smi_label, ne, scratch, Operand(zero_reg), distance);
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

void MacroAssembler::AssertNotSmi(Register object, AbortReason reason) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    static_assert(kSmiTag == 0);
    DCHECK(object != kScratchReg);
    andi(kScratchReg, object, kSmiTagMask);
    Check(ne, reason, kScratchReg, Operand(zero_reg));
  }
}

void MacroAssembler::AssertSmi(Register object, AbortReason reason) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    static_assert(kSmiTag == 0);
    DCHECK(object != kScratchReg);
    andi(kScratchReg, object, kSmiTagMask);
    Check(eq, reason, kScratchReg, Operand(zero_reg));
  }
}

void MacroAssembler::AssertConstructor(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    DCHECK(object != kScratchReg);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    static_assert(kSmiTag == 0);
    SmiTst(object, kScratchReg);
    Check(ne, AbortReason::kOperandIsASmiAndNotAConstructor, kScratchReg,
          Operand(zero_reg));

    LoadMap(kScratchReg, object);
    Lbu(kScratchReg, FieldMemOperand(kScratchReg, Map::kBitFieldOffset));
    And(kScratchReg, kScratchReg, Operand(Map::Bits1::IsConstructorBit::kMask));
    Check(ne, AbortReason::kOperandIsNotAConstructor, kScratchReg,
          Operand(zero_reg));
  }
}

void MacroAssembler::AssertFunction(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    static_assert(kSmiTag == 0);
    DCHECK(object != kScratchReg);
    SmiTst(object, kScratchReg);
    Check(ne, AbortReason::kOperandIsASmiAndNotAFunction, kScratchReg,
          Operand(zero_reg));
    push(object);
    LoadMap(object, object);
    UseScratchRegisterScope temps(this);
    Register range = temps.Acquire();
    GetInstanceTypeRange(object, object, FIRST_JS_FUNCTION_TYPE, range);
    Check(Uless_equal, AbortReason::kOperandIsNotAFunction, range,
          Operand(LAST_JS_FUNCTION_TYPE - FIRST_JS_FUNCTION_TYPE));
    pop(object);
  }
}

void MacroAssembler::AssertCallableFunction(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  static_assert(kSmiTag == 0);
  AssertNotSmi(object, AbortReason::kOperandIsASmiAndNotAFunction);
  push(object);
  LoadMap(object, object);
  UseScratchRegisterScope temps(this);
  Register range = temps.Acquire();
  GetInstanceTypeRange(object, object, FIRST_CALLABLE_JS_FUNCTION_TYPE, range);
  Check(Uless_equal, AbortReason::kOperandIsNotACallableFunction, range,
        Operand(LAST_CALLABLE_JS_FUNCTION_TYPE -
                FIRST_CALLABLE_JS_FUNCTION_TYPE));
  pop(object);
}

void MacroAssembler::AssertBoundFunction(Register object) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    BlockTrampolinePoolScope block_trampoline_pool(this);
    static_assert(kSmiTag == 0);
    DCHECK(object != kScratchReg);
    SmiTst(object, kScratchReg);
    Check(ne, AbortReason::kOperandIsASmiAndNotABoundFunction, kScratchReg,
          Operand(zero_reg));
    GetObjectType(object, kScratchReg, kScratchReg);
    Check(eq, AbortReason::kOperandIsNotABoundFunction, kScratchReg,
          Operand(JS_BOUND_FUNCTION_TYPE));
  }
}

void MacroAssembler::AssertGeneratorObject(Register object) {
  if (!v8_flags.debug_code) return;
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  static_assert(kSmiTag == 0);
  DCHECK(object != kScratchReg);
  SmiTst(object, kScratchReg);
  Check(ne, AbortReason::kOperandIsASmiAndNotAGeneratorObject, kScratchReg,
        Operand(zero_reg));

  LoadMap(kScratchReg, object);
  GetInstanceTypeRange(kScratchReg, kScratchReg, FIRST_JS_GENERATOR_OBJECT_TYPE,
                       kScratchReg);
  Check(
      Uless_equal, AbortReason::kOperandIsNotAGeneratorObject, kScratchReg,
      Operand(LAST_JS_GENERATOR_OBJECT_TYPE - FIRST_JS_GENERATOR_OBJECT_TYPE));
}

void MacroAssembler::AssertUndefinedOrAllocationSite(Register object,
                                                     Register scratch) {
  if (v8_flags.debug_code) {
    ASM_CODE_COMMENT(this);
    Label done_checking;
    AssertNotSmi(object);
    LoadRoot(scratch, RootIndex::kUndefinedValue);
    BranchShort(&done_checking, eq, object, Operand(scratch));
    GetObjectType(object, scratch, scratch);
    Assert(eq, AbortReason::kExpectedUndefinedOrCell, scratch,
           Operand(ALLOCATION_SITE_TYPE));
    bind(&done_checking);
  }
}

template <typename F_TYPE>
void MacroAssembler::FloatMinMaxHelper(FPURegister dst, FPURegister src1,
                                       FPURegister src2, MaxMinKind kind) {
  DCHECK((std::is_same<F_TYPE, float>::value) ||
         (std::is_same<F_TYPE, double>::value));

  if (src1 == src2 && dst != src1) {
    if (std::is_same<float, F_TYPE>::value) {
      fmv_s(dst, src1);
    } else {
      fmv_d(dst, src1);
    }
    return;
  }

  Label done, nan;

  // For RISCV, fmin_s returns the other non-NaN operand as result if only one
  // operand is NaN; but for JS, if any operand is NaN, result is Nan. The
  // following handles the discrepency between handling of NaN between ISA and
  // JS semantics
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  if (std::is_same<float, F_TYPE>::value) {
    CompareIsNotNanF32(scratch, src1, src2);
  } else {
    CompareIsNotNanF64(scratch, src1, src2);
  }
  BranchFalseF(scratch, &nan);

  if (kind == MaxMinKind::kMax) {
    if (std::is_same<float, F_TYPE>::value) {
      fmax_s(dst, src1, src2);
    } else {
      fmax_d(dst, src1, src2);
    }
  } else {
    if (std::is_same<float, F_TYPE>::value) {
      fmin_s(dst, src1, src2);
    } else {
      fmin_d(dst, src1, src2);
    }
  }
  j(&done);

  bind(&nan);
  // if any operand is NaN, return NaN (fadd returns NaN if any operand is NaN)
  if (std::is_same<float, F_TYPE>::value) {
    fadd_s(dst, src1, src2);
  } else {
    fadd_d(dst, src1, src2);
  }

  bind(&done);
}

void MacroAssembler::Float32Max(FPURegister dst, FPURegister src1,
                                FPURegister src2) {
  ASM_CODE_COMMENT(this);
  FloatMinMaxHelper<float>(dst, src1, src2, MaxMinKind::kMax);
}

void MacroAssembler::Float32Min(FPURegister dst, FPURegister src1,
                                FPURegister src2) {
  ASM_CODE_COMMENT(this);
  FloatMinMaxHelper<float>(dst, src1, src2, MaxMinKind::kMin);
}

void MacroAssembler::Float64Max(FPURegister dst, FPURegister src1,
                                FPURegister src2) {
  ASM_CODE_COMMENT(this);
  FloatMinMaxHelper<double>(dst, src1, src2, MaxMinKind::kMax);
}

void MacroAssembler::Float64Min(FPURegister dst, FPURegister src1,
                                FPURegister src2) {
  ASM_CODE_COMMENT(this);
  FloatMinMaxHelper<double>(dst, src1, src2, MaxMinKind::kMin);
}

int MacroAssembler::CalculateStackPassedDWords(int num_gp_arguments,
                                               int num_fp_arguments) {
  int stack_passed_dwords = 0;

  // Up to eight integer arguments are passed in registers a0..a7 and
  // up to eight floating point arguments are passed in registers fa0..fa7
  if (num_gp_arguments > kRegisterPassedArguments) {
    stack_passed_dwords += num_gp_arguments - kRegisterPassedArguments;
  }
  if (num_fp_arguments > kRegisterPassedArguments) {
    stack_passed_dwords += num_fp_arguments - kRegisterPassedArguments;
  }
  stack_passed_dwords += kCArgSlotCount;
  return stack_passed_dwords;
}

void MacroAssembler::PrepareCallCFunction(int num_reg_arguments,
                                          int num_double_arguments,
                                          Register scratch) {
  ASM_CODE_COMMENT(this);
  int frame_alignment = ActivationFrameAlignment();

  // Up to eight simple arguments in a0..a7, fa0..fa7.
  // Remaining arguments are pushed on the stack (arg slot calculation handled
  // by CalculateStackPassedDWords()).
  int stack_passed_arguments =
      CalculateStackPassedDWords(num_reg_arguments, num_double_arguments);
  if (frame_alignment > kSystemPointerSize) {
    // Make stack end at alignment and make room for stack arguments and the
    // original value of sp.
    Mv(scratch, sp);
    SubWord(sp, sp, Operand((stack_passed_arguments + 1) * kSystemPointerSize));
    DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
    And(sp, sp, Operand(-frame_alignment));
    StoreWord(scratch,
              MemOperand(sp, stack_passed_arguments * kSystemPointerSize));
  } else {
    SubWord(sp, sp, Operand(stack_passed_arguments * kSystemPointerSize));
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
  BlockTrampolinePoolScope block_trampoline_pool(this);
  li(t6, function);
  return CallCFunctionHelper(t6, num_reg_arguments, num_double_arguments,
                             set_isolate_data_slots, return_location);
}

int MacroAssembler::CallCFunction(Register function, int num_reg_arguments,
                                  int num_double_arguments,
                                  SetIsolateDataSlots set_isolate_data_slots,
                                  Label* return_location) {
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
  ASM_CODE_COMMENT(this);
  // Make sure that the stack is aligned before calling a C function unless
  // running in the simulator. The simulator has its own alignment check which
  // provides more information.
  // The argument stots are presumed to have been set up by
  // PrepareCallCFunction.

#if V8_HOST_ARCH_RISCV32 || V8_HOST_ARCH_RISCV64
  if (v8_flags.debug_code) {
    int frame_alignment = base::OS::ActivationFrameAlignment();
    int frame_alignment_mask = frame_alignment - 1;
    if (frame_alignment > kSystemPointerSize) {
      DCHECK(base::bits::IsPowerOfTwo(frame_alignment));
      Label alignment_as_expected;
      {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();
        And(scratch, sp, Operand(frame_alignment_mask));
        BranchShort(&alignment_as_expected, eq, scratch, Operand(zero_reg));
      }
      // Don't use Check here, as it will call Runtime_Abort possibly
      // re-entering here.
      ebreak();
      bind(&alignment_as_expected);
    }
  }
#endif  // V8_HOST_ARCH_RISCV32 || V8_HOST_ARCH_RISCV64

  // Just call directly. The function called cannot cause a GC, or
  // allow preemption, so the return address in the link register
  // stays correct.
  Label get_pc;
  {
    if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
      if (function != t6) {
        Mv(t6, function);
        function = t6;
      }

      // Save the frame pointer and PC so that the stack layout remains
      // iterable, even without an ExitFrame which normally exists between JS
      // and C frames.
      // 't' registers are caller-saved so this is safe as a scratch register.
      Register pc_scratch = t1;

      LoadAddress(pc_scratch, &get_pc);
      // See x64 code for reasoning about how to address the isolate data
      // fields.
      CHECK(root_array_available());
      StoreWord(pc_scratch,
                ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC));
      StoreWord(fp,
                ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
    }
  }

  Call(function);
  int call_pc_offset = pc_offset();
  bind(&get_pc);
  if (return_location) bind(return_location);

  if (set_isolate_data_slots == SetIsolateDataSlots::kYes) {
    // We don't unset the PC; the FP is the source of truth.
    StoreWord(zero_reg,
              ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
  }

  int stack_passed_arguments =
      CalculateStackPassedDWords(num_reg_arguments, num_double_arguments);
  if (base::OS::ActivationFrameAlignment() > kSystemPointerSize) {
    LoadWord(sp, MemOperand(sp, stack_passed_arguments * kSystemPointerSize));
  } else {
    AddWord(sp, sp, Operand(stack_passed_arguments * kSystemPointerSize));
  }

  return call_pc_offset;
}

#undef BRANCH_ARGS_CHECK

void MacroAssembler::CheckPageFlag(Register object, int mask, Condition cc,
                                   Label* condition_met) {
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  temps.Include(t6);
  Register scratch = temps.Acquire();
  And(scratch, object, Operand(~MemoryChunk::GetAlignmentMaskForAssembler()));
  LoadWord(scratch, MemOperand(scratch, MemoryChunkLayout::kFlagsOffset));
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
  ASM_CODE_COMMENT(this);
  auto pc = -pc_offset();
  auipc(dst, 0);
  if (pc != 0) {
    SubWord(dst, dst, pc);
  }
}

void MacroAssembler::CallForDeoptimization(Builtin target, int, Label* exit,
                                           DeoptimizeKind kind, Label* ret,
                                           Label*) {
  ASM_CODE_COMMENT(this);
  BlockTrampolinePoolScope block_trampoline_pool(this);
  LoadWord(t6, MemOperand(kRootRegister,
                          IsolateData::BuiltinEntrySlotOffset(target)));
  Call(t6);
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
  LoadWord(destination,
           FieldMemOperand(code_object, Code::kInstructionStartOffset));
#endif
}

void MacroAssembler::LoadProtectedPointerField(Register destination,
                                               MemOperand field_operand) {
  DCHECK(root_array_available());
#ifdef V8_ENABLE_SANDBOX
  DecompressProtected(destination, field_operand);
#else
  LoadTaggedField(destination, field_operand);
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
  ASM_CODE_COMMENT(this);
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
  ASM_CODE_COMMENT(this);
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
  DCHECK_NE(code, t6);
  mv(t6, code);
  Jump(t6);
#else
  LoadTaggedField(code,
                  FieldMemOperand(function_object, JSFunction::kCodeOffset));
  JumpCodeObject(code, kJSEntrypointTag, jump_mode);
#endif
}

#if V8_TARGET_ARCH_RISCV64
void MacroAssembler::LoadTaggedField(const Register& destination,
                                     const MemOperand& field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTagged(destination, field_operand);
  } else {
    Ld(destination, field_operand);
  }
}

void MacroAssembler::LoadTaggedSignedField(const Register& destination,
                                           const MemOperand& field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    DecompressTaggedSigned(destination, field_operand);
  } else {
    Ld(destination, field_operand);
  }
}

void MacroAssembler::SmiUntagField(Register dst, const MemOperand& src) {
  SmiUntag(dst, src);
}

void MacroAssembler::StoreTaggedField(const Register& value,
                                      const MemOperand& dst_field_operand) {
  if (COMPRESS_POINTERS_BOOL) {
    Sw(value, dst_field_operand);
  } else {
    Sd(value, dst_field_operand);
  }
}

void MacroAssembler::AtomicStoreTaggedField(Register src,
                                            const MemOperand& dst) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  AddWord(scratch, dst.rm(), dst.offset());
  if (COMPRESS_POINTERS_BOOL) {
    amoswap_w(true, true, zero_reg, src, scratch);
  } else {
    amoswap_d(true, true, zero_reg, src, scratch);
  }
}

void MacroAssembler::DecompressTaggedSigned(const Register& destination,
                                            const MemOperand& field_operand) {
  ASM_CODE_COMMENT(this);
  Lwu(destination, field_operand);
  if (v8_flags.debug_code) {
    // Corrupt the top 32 bits. Made up of 16 fixed bits and 16 pc offset bits.
    AddWord(destination, destination,
            Operand(((kDebugZapValue << 16) | (pc_offset() & 0xffff)) << 32));
  }
}

void MacroAssembler::DecompressTagged(const Register& destination,
                                      const MemOperand& field_operand) {
  ASM_CODE_COMMENT(this);
  Lwu(destination, field_operand);
  AddWord(destination, kPtrComprCageBaseRegister, destination);
}

void MacroAssembler::DecompressTagged(const Register& destination,
                                      const Register& source) {
  ASM_CODE_COMMENT(this);
  And(destination, source, Operand(0xFFFFFFFF));
  AddWord(destination, kPtrComprCageBaseRegister, Operand(destination));
}

void MacroAssembler::DecompressTagged(Register dst, Tagged_t immediate) {
  ASM_CODE_COMMENT(this);
  AddWord(dst, kPtrComprCageBaseRegister, static_cast<int32_t>(immediate));
}

void MacroAssembler::DecompressProtected(const Register& destination,
                                         const MemOperand& field_operand) {
#ifdef V8_ENABLE_SANDBOX
  CHECK(V8_ENABLE_SANDBOX_BOOL);
  ASM_CODE_COMMENT(this);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Lw(destination, field_operand);
  LoadWord(scratch,
           MemOperand(kRootRegister, IsolateData::trusted_cage_base_offset()));
  Or(destination, destination, scratch);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

void MacroAssembler::AtomicDecompressTaggedSigned(Register dst,
                                                  const MemOperand& src) {
  ASM_CODE_COMMENT(this);
  Lwu(dst, src);
  sync();
  if (v8_flags.debug_code) {
    // Corrupt the top 32 bits. Made up of 16 fixed bits and 16 pc offset bits.
    AddWord(dst, dst,
            Operand(((kDebugZapValue << 16) | (pc_offset() & 0xffff)) << 32));
  }
}

void MacroAssembler::AtomicDecompressTagged(Register dst,
                                            const MemOperand& src) {
  ASM_CODE_COMMENT(this);
  Lwu(dst, src);
  sync();
  AddWord(dst, kPtrComprCageBaseRegister, dst);
}

#endif
void MacroAssembler::DropArguments(Register count) {
  CalcScaledAddress(sp, sp, count, kSystemPointerSizeLog2);
}

void MacroAssembler::DropArgumentsAndPushNewReceiver(Register argc,
                                                     Register receiver) {
  DCHECK(!AreAliased(argc, receiver));
  DropArguments(argc);
  push(receiver);
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
  Register prev_next_address_reg = kScratchReg;
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
    __ LoadWord(prev_next_address_reg, next_mem_op);
    __ LoadWord(prev_limit_reg, limit_mem_op);
    __ Lw(prev_level_reg, level_mem_op);
    __ Add32(scratch, prev_level_reg, Operand(1));
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

  __ RecordComment("Load the value from ReturnValue");
  __ LoadWord(return_value, return_value_operand);

  {
    ASM_CODE_COMMENT_STRING(
        masm,
        "No more valid handles (the result handle was the last one)."
        "Restore previous handle scope.");
    __ StoreWord(prev_next_address_reg, next_mem_op);
    if (v8_flags.debug_code) {
      __ Lw(scratch, level_mem_op);
      __ Sub32(scratch, scratch, Operand(1));
      __ Check(eq, AbortReason::kUnexpectedLevelAfterReturnFromApiCall, scratch,
               Operand(prev_level_reg));
    }
    __ Sw(prev_level_reg, level_mem_op);
    __ LoadWord(scratch, limit_mem_op);
    __ Branch(&delete_allocated_handles, ne, prev_limit_reg, Operand(scratch));
  }
  __ RecordComment("Leave the API exit frame.");
  __ bind(&leave_exit_frame);

  Register argc_reg = prev_limit_reg;
  if (argc_operand != nullptr) {
    // Load the number of stack slots to drop before LeaveExitFrame modifies sp.
    __ LoadWord(argc_reg, *argc_operand);
  }

  __ LeaveExitFrame(scratch);

  {
    ASM_CODE_COMMENT_STRING(masm,
                            "Check if the function scheduled an exception.");
    __ LoadRoot(scratch, RootIndex::kTheHoleValue);
    __ LoadWord(scratch2, __ ExternalReferenceAsOperand(
                              ER::exception_address(isolate), no_reg));
    __ Branch(&propagate_exception, ne, scratch, Operand(scratch2));
  }

  __ AssertJSAny(return_value, scratch, scratch2,
                 AbortReason::kAPICallReturnedInvalidObject);

  if (argc_operand == nullptr) {
    DCHECK_NE(slots_to_drop_on_return, 0);
    __ AddWord(sp, sp, Operand(slots_to_drop_on_return * kSystemPointerSize));
  } else {
    // {argc_operand} was loaded into {argc_reg} above.
    if (slots_to_drop_on_return != 0) {
      __ AddWord(sp, sp, Operand(slots_to_drop_on_return * kSystemPointerSize));
    }
    __ CalcScaledAddress(sp, sp, argc_reg, kSystemPointerSizeLog2);
  }

  __ Ret();

  if (with_profiling) {
    ASM_CODE_COMMENT_STRING(masm, "Call the api function via thunk wrapper.");
    __ bind(&profiler_or_side_effects_check_enabled);
    // Additional parameter is the address of the actual callback function.
    if (thunk_arg.is_valid()) {
      MemOperand thunk_arg_mem_op = __ ExternalReferenceAsOperand(
          IsolateFieldId::kApiCallbackThunkArgument);
      __ StoreWord(thunk_arg, thunk_arg_mem_op);
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
    __ StoreWord(prev_limit_reg, limit_mem_op);
    // Save the return value in a callee-save register.
    Register saved_result = prev_limit_reg;
    __ Move(saved_result, a0);
    __ PrepareCallCFunction(1, prev_level_reg);
    __ li(kCArgRegs[0], ER::isolate_address(isolate));
    __ CallCFunction(ER::delete_handle_scope_extensions(), 1);
    __ Move(kCArgRegs[0], saved_result);
    __ Branch(&leave_exit_frame);
  }
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
  Lhu(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));
  Branch(&done, eq, scratch, Operand(FEEDBACK_VECTOR_TYPE));

  // Not valid, load undefined.
  LoadRoot(dst, RootIndex::kUndefinedValue);
  Branch(fbv_undef);

  bind(&done);
}

#undef __
}  // namespace internal
}  // namespace v8
