// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDED_FROM_MACRO_ASSEMBLER_H
#error This header must be included via macro-assembler.h
#endif

#ifndef V8_CODEGEN_ARM_MACRO_ASSEMBLER_ARM_H_
#define V8_CODEGEN_ARM_MACRO_ASSEMBLER_ARM_H_

#include "src/base/platform/platform.h"
#include "src/codegen/arm/assembler-arm.h"
#include "src/codegen/bailout-reason.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/objects/tagged-index.h"

namespace v8 {
namespace internal {

// TODO(victorgomes): Move definition to macro-assembler.h, once all other
// platforms are updated.
enum class StackLimitKind { kInterruptStackLimit, kRealStackLimit };

// ----------------------------------------------------------------------------
// Static helper functions

// Generate a MemOperand for loading a field from an object.
inline MemOperand FieldMemOperand(Register object, int offset) {
  return MemOperand(object, offset - kHeapObjectTag);
}

enum LinkRegisterStatus { kLRHasNotBeenSaved, kLRHasBeenSaved };

Register GetRegisterThatIsNotOneOf(Register reg1, Register reg2 = no_reg,
                                   Register reg3 = no_reg,
                                   Register reg4 = no_reg,
                                   Register reg5 = no_reg,
                                   Register reg6 = no_reg);

enum TargetAddressStorageMode {
  CAN_INLINE_TARGET_ADDRESS,
  NEVER_INLINE_TARGET_ADDRESS
};

class V8_EXPORT_PRIVATE MacroAssembler : public MacroAssemblerBase {
 public:
  using MacroAssemblerBase::MacroAssemblerBase;

  // Activation support.
  void EnterFrame(StackFrame::Type type,
                  bool load_constant_pool_pointer_reg = false);
  // Returns the pc offset at which the frame ends.
  int LeaveFrame(StackFrame::Type type);

// Allocate stack space of given size (i.e. decrement {sp} by the value
// stored in the given register, or by a constant). If you need to perform a
// stack check, do it before calling this function because this function may
// write into the newly allocated space. It may also overwrite the given
// register's value, in the version that takes a register.
#ifdef V8_OS_WIN
  void AllocateStackSpace(Register bytes_scratch);
  void AllocateStackSpace(int bytes);
#else
  void AllocateStackSpace(Register bytes) { sub(sp, sp, bytes); }
  void AllocateStackSpace(int bytes) {
    DCHECK_GE(bytes, 0);
    if (bytes == 0) return;
    sub(sp, sp, Operand(bytes));
  }
#endif

  // Push a fixed frame, consisting of lr, fp
  void PushCommonFrame(Register marker_reg = no_reg);

  // Generates function and stub prologue code.
  void StubPrologue(StackFrame::Type type);
  void Prologue();

  enum ArgumentsCountMode { kCountIncludesReceiver, kCountExcludesReceiver };
  enum ArgumentsCountType { kCountIsInteger, kCountIsSmi, kCountIsBytes };
  void DropArguments(Register count, ArgumentsCountType type,
                     ArgumentsCountMode mode);
  void DropArgumentsAndPushNewReceiver(Register argc, Register receiver,
                                       ArgumentsCountType type,
                                       ArgumentsCountMode mode);

  // Push a standard frame, consisting of lr, fp, context and JS function
  void PushStandardFrame(Register function_reg);

  void InitializeRootRegister();

  void Push(Register src) { push(src); }

  void Push(Handle<HeapObject> handle);
  void Push(Tagged<Smi> smi);
  void Push(Tagged<TaggedIndex> index);

  // Push two registers.  Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Condition cond = al) {
    if (src1.code() > src2.code()) {
      stm(db_w, sp, {src1, src2}, cond);
    } else {
      str(src1, MemOperand(sp, 4, NegPreIndex), cond);
      str(src2, MemOperand(sp, 4, NegPreIndex), cond);
    }
  }

  // Push three registers.  Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Register src3, Condition cond = al) {
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        stm(db_w, sp, {src1, src2, src3}, cond);
      } else {
        stm(db_w, sp, {src1, src2}, cond);
        str(src3, MemOperand(sp, 4, NegPreIndex), cond);
      }
    } else {
      str(src1, MemOperand(sp, 4, NegPreIndex), cond);
      Push(src2, src3, cond);
    }
  }

  // Push four registers.  Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Register src3, Register src4,
            Condition cond = al) {
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        if (src3.code() > src4.code()) {
          stm(db_w, sp, {src1, src2, src3, src4}, cond);
        } else {
          stm(db_w, sp, {src1, src2, src3}, cond);
          str(src4, MemOperand(sp, 4, NegPreIndex), cond);
        }
      } else {
        stm(db_w, sp, {src1, src2}, cond);
        Push(src3, src4, cond);
      }
    } else {
      str(src1, MemOperand(sp, 4, NegPreIndex), cond);
      Push(src2, src3, src4, cond);
    }
  }

  // Push five registers.  Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Register src3, Register src4,
            Register src5, Condition cond = al) {
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        if (src3.code() > src4.code()) {
          if (src4.code() > src5.code()) {
            stm(db_w, sp, {src1, src2, src3, src4, src5}, cond);
          } else {
            stm(db_w, sp, {src1, src2, src3, src4}, cond);
            str(src5, MemOperand(sp, 4, NegPreIndex), cond);
          }
        } else {
          stm(db_w, sp, {src1, src2, src3}, cond);
          Push(src4, src5, cond);
        }
      } else {
        stm(db_w, sp, {src1, src2}, cond);
        Push(src3, src4, src5, cond);
      }
    } else {
      str(src1, MemOperand(sp, 4, NegPreIndex), cond);
      Push(src2, src3, src4, src5, cond);
    }
  }

  enum class PushArrayOrder { kNormal, kReverse };
  // `array` points to the first element (the lowest address).
  // `array` and `size` are not modified.
  void PushArray(Register array, Register size, Register scratch,
                 PushArrayOrder order = PushArrayOrder::kNormal);

  void Pop(Register dst) { pop(dst); }

  // Pop two registers. Pops rightmost register first (from lower address).
  void Pop(Register src1, Register src2, Condition cond = al) {
    DCHECK(src1 != src2);
    if (src1.code() > src2.code()) {
      ldm(ia_w, sp, {src1, src2}, cond);
    } else {
      ldr(src2, MemOperand(sp, 4, PostIndex), cond);
      ldr(src1, MemOperand(sp, 4, PostIndex), cond);
    }
  }

  // Pop three registers.  Pops rightmost register first (from lower address).
  void Pop(Register src1, Register src2, Register src3, Condition cond = al) {
    DCHECK(!AreAliased(src1, src2, src3));
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        ldm(ia_w, sp, {src1, src2, src3}, cond);
      } else {
        ldr(src3, MemOperand(sp, 4, PostIndex), cond);
        ldm(ia_w, sp, {src1, src2}, cond);
      }
    } else {
      Pop(src2, src3, cond);
      ldr(src1, MemOperand(sp, 4, PostIndex), cond);
    }
  }

  // Pop four registers.  Pops rightmost register first (from lower address).
  void Pop(Register src1, Register src2, Register src3, Register src4,
           Condition cond = al) {
    DCHECK(!AreAliased(src1, src2, src3, src4));
    if (src1.code() > src2.code()) {
      if (src2.code() > src3.code()) {
        if (src3.code() > src4.code()) {
          ldm(ia_w, sp, {src1, src2, src3, src4}, cond);
        } else {
          ldr(src4, MemOperand(sp, 4, PostIndex), cond);
          ldm(ia_w, sp, {src1, src2, src3}, cond);
        }
      } else {
        Pop(src3, src4, cond);
        ldm(ia_w, sp, {src1, src2}, cond);
      }
    } else {
      Pop(src2, src3, src4, cond);
      ldr(src1, MemOperand(sp, 4, PostIndex), cond);
    }
  }

  // Before calling a C-function from generated code, align arguments on stack.
  // After aligning the frame, non-register arguments must be stored in
  // sp[0], sp[4], etc., not pushed. The argument count assumes all arguments
  // are word sized. If double arguments are used, this function assumes that
  // all double arguments are stored before core registers; otherwise the
  // correct alignment of the double values is not guaranteed.
  // Some compilers/platforms require the stack to be aligned when calling
  // C++ code.
  // Needs a scratch register to do some arithmetic. This register will be
  // trashed.
  void PrepareCallCFunction(int num_reg_arguments, int num_double_registers = 0,
                            Register scratch = no_reg);

  // There are two ways of passing double arguments on ARM, depending on
  // whether soft or hard floating point ABI is used. These functions
  // abstract parameter passing for the three different ways we call
  // C functions from generated code.
  void MovToFloatParameter(DwVfpRegister src);
  void MovToFloatParameters(DwVfpRegister src1, DwVfpRegister src2);
  void MovToFloatResult(DwVfpRegister src);

  // Calls a C function and cleans up the space for arguments allocated
  // by PrepareCallCFunction. The called function is not allowed to trigger a
  // garbage collection, since that might move the code and invalidate the
  // return address (unless this is somehow accounted for by the called
  // function).
  int CallCFunction(
      ExternalReference function, int num_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_label = nullptr);
  int CallCFunction(
      Register function, int num_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_label = nullptr);
  int CallCFunction(
      ExternalReference function, int num_reg_arguments,
      int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_label = nullptr);
  int CallCFunction(
      Register function, int num_reg_arguments, int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_label = nullptr);

  void MovFromFloatParameter(DwVfpRegister dst);
  void MovFromFloatResult(DwVfpRegister dst);

  void Trap();
  void DebugBreak();

  // Calls Abort(msg) if the condition cond is not satisfied.
  // Use --debug-code to enable.
  void Assert(Condition cond, AbortReason reason) NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but without condition.
  // Use --debug-code to enable.
  void AssertUnreachable(AbortReason reason) NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but always enabled.
  void Check(Condition cond, AbortReason reason);

  // Print a message to stdout and abort execution.
  void Abort(AbortReason msg);

  void LslPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, Register shift);
  void LslPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, uint32_t shift);
  void LsrPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, Register shift);
  void LsrPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, uint32_t shift);
  void AsrPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, Register shift);
  void AsrPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, uint32_t shift);

  void LoadFromConstantsTable(Register destination, int constant_index) final;
  void LoadRootRegisterOffset(Register destination, intptr_t offset) final;
  void LoadRootRelative(Register destination, int32_t offset) final;
  void StoreRootRelative(int32_t offset, Register value) final;

  // Operand pointing to an external reference.
  // May emit code to set up the scratch register. The operand is
  // only guaranteed to be correct as long as the scratch register
  // isn't changed.
  // If the operand is used more than once, use a scratch register
  // that is guaranteed not to be clobbered.
  MemOperand ExternalReferenceAsOperand(ExternalReference reference,
                                        Register scratch);

  // Jump, Call, and Ret pseudo instructions implementing inter-working.
  void Call(Register target, Condition cond = al);
  void Call(Address target, RelocInfo::Mode rmode, Condition cond = al,
            TargetAddressStorageMode mode = CAN_INLINE_TARGET_ADDRESS,
            bool check_constant_pool = true);
  void Call(Handle<Code> code, RelocInfo::Mode rmode = RelocInfo::CODE_TARGET,
            Condition cond = al,
            TargetAddressStorageMode mode = CAN_INLINE_TARGET_ADDRESS,
            bool check_constant_pool = true);
  void Call(Label* target);

  MemOperand EntryFromBuiltinAsOperand(Builtin builtin);
  void LoadEntryFromBuiltin(Builtin builtin, Register destination);
  // Load the builtin given by the Smi in |builtin| into |target|.
  void LoadEntryFromBuiltinIndex(Register builtin_index, Register target);
  void CallBuiltinByIndex(Register builtin_index, Register target);
  void CallBuiltin(Builtin builtin, Condition cond = al);
  void TailCallBuiltin(Builtin builtin, Condition cond = al);

  // Load the code entry point from the Code object.
  void LoadCodeInstructionStart(
      Register destination, Register code_object,
      CodeEntrypointTag tag = kDefaultCodeEntrypointTag);
  void CallCodeObject(Register code_object);
  void JumpCodeObject(Register code_object,
                      JumpMode jump_mode = JumpMode::kJump);

  // Convenience functions to call/jmp to the code of a JSFunction object.
  void CallJSFunction(Register function_object);
  void JumpJSFunction(Register function_object,
                      JumpMode jump_mode = JumpMode::kJump);

  // Generates an instruction sequence s.t. the return address points to the
  // instruction following the call.
  // The return address on the stack is used by frame iteration.
  void StoreReturnAddressAndCall(Register target);

  // Enforce platform specific stack alignment.
  void EnforceStackAlignment();

  void BailoutIfDeoptimized();
  void CallForDeoptimization(Builtin target, int deopt_id, Label* exit,
                             DeoptimizeKind kind, Label* ret,
                             Label* jump_deoptimization_entry_label);

  // Emit code to discard a non-negative number of pointer-sized elements
  // from the stack, clobbering only the sp register.
  void Drop(int count, Condition cond = al);
  void Drop(Register count, Condition cond = al);

  void Ret(Condition cond = al);

  // Compare single values and move the result to the normal condition flags.
  void VFPCompareAndSetFlags(const SwVfpRegister src1, const SwVfpRegister src2,
                             const Condition cond = al);
  void VFPCompareAndSetFlags(const SwVfpRegister src1, const float src2,
                             const Condition cond = al);

  // Compare double values and move the result to the normal condition flags.
  void VFPCompareAndSetFlags(const DwVfpRegister src1, const DwVfpRegister src2,
                             const Condition cond = al);
  void VFPCompareAndSetFlags(const DwVfpRegister src1, const double src2,
                             const Condition cond = al);

  // If the value is a NaN, canonicalize the value else, do nothing.
  void VFPCanonicalizeNaN(const DwVfpRegister dst, const DwVfpRegister src,
                          const Condition cond = al);
  void VFPCanonicalizeNaN(const DwVfpRegister value,
                          const Condition cond = al) {
    VFPCanonicalizeNaN(value, value, cond);
  }

  void VmovHigh(Register dst, DwVfpRegister src);
  void VmovHigh(DwVfpRegister dst, Register src);
  void VmovLow(Register dst, DwVfpRegister src);
  void VmovLow(DwVfpRegister dst, Register src);

  void CheckPageFlag(Register object, int mask, Condition cc,
                     Label* condition_met);
  void CheckPageFlag(Register object, Register scratch, int mask, Condition cc,
                     Label* condition_met) {
    CheckPageFlag(object, mask, cc, condition_met);
  }

  // Check whether d16-d31 are available on the CPU. The result is given by the
  // Z condition flag: Z==0 if d16-d31 available, Z==1 otherwise.
  void CheckFor32DRegs(Register scratch);

  void MaybeSaveRegisters(RegList registers);
  void MaybeRestoreRegisters(RegList registers);

  void CallEphemeronKeyBarrier(Register object, Operand offset,
                               SaveFPRegsMode fp_mode);

  void CallRecordWriteStubSaveRegisters(
      Register object, Operand offset, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);
  void CallRecordWriteStub(
      Register object, Register slot_address, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);

  // For a given |object| and |offset|:
  //   - Move |object| to |dst_object|.
  //   - Compute the address of the slot pointed to by |offset| in |object| and
  //     write it to |dst_slot|. |offset| can be either an immediate or a
  //     register.
  // This method makes sure |object| and |offset| are allowed to overlap with
  // the destination registers.
  void MoveObjectAndSlot(Register dst_object, Register dst_slot,
                         Register object, Operand offset);

  // Does a runtime check for 16/32 FP registers. Either way, pushes 32 double
  // values to location, saving [d0..(d15|d31)].
  void SaveFPRegs(Register location, Register scratch);

  // Does a runtime check for 16/32 FP registers. Either way, pops 32 double
  // values to location, restoring [d0..(d15|d31)].
  void RestoreFPRegs(Register location, Register scratch);

  // As above, but with heap semantics instead of stack semantics, i.e.: the
  // location starts at the lowest address and grows towards higher addresses,
  // for both saves and restores.
  void SaveFPRegsToHeap(Register location, Register scratch);
  void RestoreFPRegsFromHeap(Register location, Register scratch);

  // Calculate how much stack space (in bytes) are required to store caller
  // registers excluding those specified in the arguments.
  int RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                      Register exclusion1 = no_reg,
                                      Register exclusion2 = no_reg,
                                      Register exclusion3 = no_reg) const;

  // Push caller saved registers on the stack, and return the number of bytes
  // stack pointer is adjusted.
  int PushCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1 = no_reg,
                      Register exclusion2 = no_reg,
                      Register exclusion3 = no_reg);
  // Restore caller saved registers from the stack, and return the number of
  // bytes stack pointer is adjusted.
  int PopCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1 = no_reg,
                     Register exclusion2 = no_reg,
                     Register exclusion3 = no_reg);
  void Jump(Register target, Condition cond = al);
  void Jump(Address target, RelocInfo::Mode rmode, Condition cond = al);
  void Jump(Handle<Code> code, RelocInfo::Mode rmode, Condition cond = al);
  void Jump(const ExternalReference& reference);

  void GetLabelAddress(Register dst, Label* target);

  // Perform a floating-point min or max operation with the
  // (IEEE-754-compatible) semantics of ARM64's fmin/fmax. Some cases, typically
  // NaNs or +/-0.0, are expected to be rare and are handled in out-of-line
  // code. The specific behaviour depends on supported instructions.
  //
  // These functions assume (and assert) that left!=right. It is permitted
  // for the result to alias either input register.
  void FloatMax(SwVfpRegister result, SwVfpRegister left, SwVfpRegister right,
                Label* out_of_line);
  void FloatMin(SwVfpRegister result, SwVfpRegister left, SwVfpRegister right,
                Label* out_of_line);
  void FloatMax(DwVfpRegister result, DwVfpRegister left, DwVfpRegister right,
                Label* out_of_line);
  void FloatMin(DwVfpRegister result, DwVfpRegister left, DwVfpRegister right,
                Label* out_of_line);

  // Generate out-of-line cases for the macros above.
  void FloatMaxOutOfLine(SwVfpRegister result, SwVfpRegister left,
                         SwVfpRegister right);
  void FloatMinOutOfLine(SwVfpRegister result, SwVfpRegister left,
                         SwVfpRegister right);
  void FloatMaxOutOfLine(DwVfpRegister result, DwVfpRegister left,
                         DwVfpRegister right);
  void FloatMinOutOfLine(DwVfpRegister result, DwVfpRegister left,
                         DwVfpRegister right);

  void ExtractLane(Register dst, QwNeonRegister src, NeonDataType dt, int lane);
  void ExtractLane(Register dst, DwVfpRegister src, NeonDataType dt, int lane);
  void ExtractLane(SwVfpRegister dst, QwNeonRegister src, int lane);
  void ExtractLane(DwVfpRegister dst, QwNeonRegister src, int lane);
  void ReplaceLane(QwNeonRegister dst, QwNeonRegister src, Register src_lane,
                   NeonDataType dt, int lane);
  void ReplaceLane(QwNeonRegister dst, QwNeonRegister src,
                   SwVfpRegister src_lane, int lane);
  void ReplaceLane(QwNeonRegister dst, QwNeonRegister src,
                   DwVfpRegister src_lane, int lane);

  void LoadLane(NeonSize sz, NeonListOperand dst_list, uint8_t lane,
                NeonMemOperand src);
  void StoreLane(NeonSize sz, NeonListOperand src_list, uint8_t lane,
                 NeonMemOperand dst);

  // Register move. May do nothing if the registers are identical.
  void Move(Register dst, Tagged<Smi> smi);
  void Move(Register dst, Handle<HeapObject> value);
  void Move(Register dst, ExternalReference reference);
  void Move(Register dst, Register src, Condition cond = al);
  void Move(Register dst, const MemOperand& src) { ldr(dst, src); }
  void Move(Register dst, const Operand& src, SBit sbit = LeaveCC,
            Condition cond = al) {
    if (!src.IsRegister() || src.rm() != dst || sbit != LeaveCC) {
      mov(dst, src, sbit, cond);
    }
  }
  // Move src0 to dst0 and src1 to dst1, handling possible overlaps.
  void MovePair(Register dst0, Register src0, Register dst1, Register src1);

  void Move(SwVfpRegister dst, SwVfpRegister src, Condition cond = al);
  void Move(DwVfpRegister dst, DwVfpRegister src, Condition cond = al);
  void Move(QwNeonRegister dst, QwNeonRegister src);

  // Simulate s-register moves for imaginary s32 - s63 registers.
  void VmovExtended(Register dst, int src_code);
  void VmovExtended(int dst_code, Register src);
  // Move between s-registers and imaginary s-registers.
  void VmovExtended(int dst_code, int src_code);
  void VmovExtended(int dst_code, const MemOperand& src);
  void VmovExtended(const MemOperand& dst, int src_code);

  // Register swap. Note that the register operands should be distinct.
  void Swap(Register srcdst0, Register srcdst1);
  void Swap(DwVfpRegister srcdst0, DwVfpRegister srcdst1);
  void Swap(QwNeonRegister srcdst0, QwNeonRegister srcdst1);

  // Get the actual activation frame alignment for target environment.
  static int ActivationFrameAlignment();

  void Bfc(Register dst, Register src, int lsb, int width, Condition cond = al);

  void SmiUntag(Register reg, SBit s = LeaveCC) {
    mov(reg, Operand::SmiUntag(reg), s);
  }
  void SmiUntag(Register dst, Register src, SBit s = LeaveCC) {
    mov(dst, Operand::SmiUntag(src), s);
  }

  void SmiToInt32(Register smi) { SmiUntag(smi); }
  void SmiToInt32(Register dst, Register smi) { SmiUntag(dst, smi); }

  // Load an object from the root table.
  void LoadTaggedRoot(Register destination, RootIndex index) {
    LoadRoot(destination, index);
  }
  void LoadRoot(Register destination, RootIndex index) final {
    LoadRoot(destination, index, al);
  }
  void LoadRoot(Register destination, RootIndex index, Condition cond);

  // Jump if the register contains a smi.
  void JumpIfSmi(Register value, Label* smi_label);

  void JumpIfEqual(Register x, int32_t y, Label* dest);

  void JumpIfLessThan(Register x, int32_t y, Label* dest);

  void LoadMap(Register destination, Register object);

  void LoadFeedbackVector(Register dst, Register closure, Register scratch,
                          Label* fbv_undef);

  void PushAll(RegList registers) {
    if (registers.is_empty()) return;
    ASM_CODE_COMMENT(this);
    // stm(db_w, sp, registers);
    // TODO(victorgomes): {stm/ldm} pushes/pops registers in the opposite order
    // as expected by Maglev frame. Consider massaging Maglev to accept this
    // order instead.
    for (Register reg : registers) {
      push(reg);
    }
  }

  void PopAll(RegList registers) {
    if (registers.is_empty()) return;
    ASM_CODE_COMMENT(this);
    // ldm(ia_w, sp, registers);
    for (Register reg : base::Reversed(registers)) {
      pop(reg);
    }
  }

  void PushAll(DoubleRegList registers, int stack_slot_size = kDoubleSize) {
    if (registers.is_empty()) return;
    ASM_CODE_COMMENT(this);
    // TODO(victorgomes): vstm only works for consecutive double registers. We
    // could check if it is the case and optimize here.
    for (DoubleRegister reg : registers) {
      vpush(reg);
    }
  }

  void PopAll(DoubleRegList registers, int stack_slot_size = kDoubleSize) {
    if (registers.is_empty()) return;
    ASM_CODE_COMMENT(this);
    // TODO(victorgomes): vldm only works for consecutive double registers. We
    // could check if it is the case and optimize here.
    for (DoubleRegister reg : base::Reversed(registers)) {
      vpop(reg);
    }
  }

  inline void Cmp(const Register& rn, int imm) { cmp(rn, Operand(imm)); }

  inline void CmpTagged(const Register& r1, const Register& r2) { cmp(r1, r2); }

  // Functions performing a check on a known or potential smi. Returns
  // a condition that is satisfied if the check is successful.
  Condition CheckSmi(Register src) {
    SmiTst(src);
    return eq;
  }

  void Zero(const MemOperand& dest);
  void Zero(const MemOperand& dest1, const MemOperand& dest2);

  void DecompressTagged(const Register& destination,
                        const MemOperand& field_operand) {
    // No pointer compression on arm, we do just a simple load.
    LoadTaggedField(destination, field_operand);
  }

  void DecompressTagged(const Register& destination, const Register& source) {
    // No pointer compression on arm. Do nothing.
  }

  void AssertMap(Register object) NOOP_UNLESS_DEBUG_CODE;

  void LoadTaggedField(const Register& destination,
                       const MemOperand& field_operand) {
    ldr(destination, field_operand);
  }

  void LoadTaggedFieldWithoutDecompressing(const Register& destination,
                                           const MemOperand& field_operand) {
    LoadTaggedField(destination, field_operand);
  }

  void SmiUntagField(Register dst, const MemOperand& src) {
    LoadTaggedField(dst, src);
    SmiUntag(dst);
  }

  void StoreTaggedField(const Register& value,
                        const MemOperand& dst_field_operand) {
    str(value, dst_field_operand);
  }

  // For compatibility with platform-independent code.
  void StoreTaggedField(const MemOperand& dst_field_operand,
                        const Register& value) {
    StoreTaggedField(value, dst_field_operand);
  }

  void Switch(Register scratch, Register value, int case_value_base,
              Label** labels, int num_labels);

  void JumpIfCodeIsMarkedForDeoptimization(Register code, Register scratch,
                                           Label* if_marked_for_deoptimization);

  void JumpIfCodeIsTurbofanned(Register code, Register scratch,
                               Label* if_turbofanned);

  // Falls through and sets scratch_and_result to 0 on failure, jumps to
  // on_result on success.
  void TryLoadOptimizedOsrCode(Register scratch_and_result,
                               CodeKind min_opt_level, Register feedback_vector,
                               FeedbackSlot slot, Label* on_result,
                               Label::Distance distance);

  void AssertZeroExtended(Register int32_register) {
    // In arm32, we don't have top 32 bits, so do nothing.
  }

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32. Goes to 'done' if it
  // succeeds, otherwise falls through if result is saturated. On return
  // 'result' either holds answer, or is clobbered on fall through.
  void TryInlineTruncateDoubleToI(Register result, DwVfpRegister input,
                                  Label* done);

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32.
  // Exits with 'result' holding the answer.
  void TruncateDoubleToI(Isolate* isolate, Zone* zone, Register result,
                         DwVfpRegister double_input, StubCallMode stub_mode);

  // EABI variant for double arguments in use.
  bool use_eabi_hardfloat() {
#ifdef __arm__
    return base::OS::ArmUsingHardFloat();
#elif USE_EABI_HARDFLOAT
    return true;
#else
    return false;
#endif
  }

  // Compute the start of the generated instruction stream from the current PC.
  // This is an alternative to embedding the {CodeObject} handle as a reference.
  void ComputeCodeStartAddress(Register dst);

  // Control-flow integrity:

  // Define a function entrypoint. This doesn't emit any code for this
  // architecture, as control-flow integrity is not supported for it.
  void CodeEntry() {}
  // Define an exception handler.
  void ExceptionHandler() {}
  // Define an exception handler and bind a label.
  void BindExceptionHandler(Label* label) { bind(label); }

  // Wasm SIMD helpers. These instructions don't have direct lowering to native
  // instructions. These helpers allow us to define the optimal code sequence,
  // and be used in both TurboFan and Liftoff.
  void I64x2BitMask(Register dst, QwNeonRegister src);
  void I64x2Eq(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void I64x2Ne(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void I64x2GtS(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void I64x2GeS(QwNeonRegister dst, QwNeonRegister src1, QwNeonRegister src2);
  void I64x2AllTrue(Register dst, QwNeonRegister src);
  void I64x2Abs(QwNeonRegister dst, QwNeonRegister src);
  void F64x2ConvertLowI32x4S(QwNeonRegister dst, QwNeonRegister src);
  void F64x2ConvertLowI32x4U(QwNeonRegister dst, QwNeonRegister src);
  void F64x2PromoteLowF32x4(QwNeonRegister dst, QwNeonRegister src);

  void Mls(Register dst, Register src1, Register src2, Register srcA,
           Condition cond = al);
  void And(Register dst, Register src1, const Operand& src2,
           Condition cond = al);
  void Ubfx(Register dst, Register src, int lsb, int width,
            Condition cond = al);
  void Sbfx(Register dst, Register src, int lsb, int width,
            Condition cond = al);

  // ---------------------------------------------------------------------------
  // GC Support

  // Notify the garbage collector that we wrote a pointer into an object.
  // |object| is the object being stored into, |value| is the object being
  // stored.
  // The offset is the offset from the start of the object, not the offset from
  // the tagged HeapObject pointer.  For use with FieldMemOperand(reg, off).
  void RecordWriteField(Register object, int offset, Register value,
                        LinkRegisterStatus lr_status, SaveFPRegsMode save_fp,
                        SmiCheck smi_check = SmiCheck::kInline);

  // For a given |object| notify the garbage collector that the slot at |offset|
  // has been written. |value| is the object being stored.
  void RecordWrite(Register object, Operand offset, Register value,
                   LinkRegisterStatus lr_status, SaveFPRegsMode save_fp,
                   SmiCheck smi_check = SmiCheck::kInline);

  // Enter exit frame.
  // stack_space - extra stack space, used for alignment before call to C.
  void EnterExitFrame(int stack_space, StackFrame::Type frame_type);

  // Leave the current exit frame. Expects the return value in r0.
  // Expect the number of values, pushed prior to the exit frame, to
  // remove in a register (or no_reg, if there is nothing to remove).
  void LeaveExitFrame(Register argument_count, bool argument_count_is_length);

  // Load the global proxy from the current context.
  void LoadGlobalProxy(Register dst);

  void LoadNativeContextSlot(Register dst, int index);

  // ---------------------------------------------------------------------------
  // JavaScript invokes

  // Invoke the JavaScript function code by either calling or jumping.
  void InvokeFunctionCode(Register function, Register new_target,
                          Register expected_parameter_count,
                          Register actual_parameter_count, InvokeType type);

  // On function call, call into the debugger.
  void CallDebugOnFunctionCall(Register fun, Register new_target,
                               Register expected_parameter_count,
                               Register actual_parameter_count);

  // Invoke the JavaScript function in the given register. Changes the
  // current context to the context in the function before invoking.
  void InvokeFunctionWithNewTarget(Register function, Register new_target,
                                   Register actual_parameter_count,
                                   InvokeType type);

  void InvokeFunction(Register function, Register expected_parameter_count,
                      Register actual_parameter_count, InvokeType type);

  // Exception handling

  // Push a new stack handler and link into stack handler chain.
  void PushStackHandler();

  // Unlink the stack handler on top of the stack from the stack handler chain.
  // Must preserve the result register.
  void PopStackHandler();

  // ---------------------------------------------------------------------------
  // Support functions.

  // Compare object type for heap object.  heap_object contains a non-Smi
  // whose object type should be compared with the given type.  This both
  // sets the flags and leaves the object type in the type_reg register.
  // It leaves the map in the map register (unless the type_reg and map register
  // are the same register).  It leaves the heap object in the heap_object
  // register unless the heap_object register is the same register as one of the
  // other registers.
  // Type_reg can be no_reg. In that case a scratch register is used.
  void CompareObjectType(Register heap_object, Register map, Register type_reg,
                         InstanceType type);

  // Compare instance type in a map.  map contains a valid map object whose
  // object type should be compared with the given type.  This both
  // sets the flags and leaves the object type in the type_reg register.
  void CompareInstanceType(Register map, Register type_reg, InstanceType type);

  // Compare instance type ranges for a map (lower_limit and higher_limit
  // inclusive).
  //
  // Always use unsigned comparisons: ls for a positive result.
  void CompareInstanceTypeRange(Register map, Register type_reg,
                                InstanceType lower_limit,
                                InstanceType higher_limit);

  // Compare the object in a register to a value from the root list.
  // Acquires a scratch register.
  void CompareRoot(Register obj, RootIndex index);
  void CompareTaggedRoot(Register with, RootIndex index);
  void PushRoot(RootIndex index) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    LoadRoot(scratch, index);
    Push(scratch);
  }

  // Compare the object in a register to a value and jump if they are equal.
  void JumpIfRoot(Register with, RootIndex index, Label* if_equal) {
    CompareRoot(with, index);
    b(eq, if_equal);
  }

  // Compare the object in a register to a value and jump if they are not equal.
  void JumpIfNotRoot(Register with, RootIndex index, Label* if_not_equal) {
    CompareRoot(with, index);
    b(ne, if_not_equal);
  }

  // Checks if value is in range [lower_limit, higher_limit] using a single
  // comparison. Flags C=0 or Z=1 indicate the value is in the range (condition
  // ls).
  void CompareRange(Register value, unsigned lower_limit,
                    unsigned higher_limit);
  void JumpIfIsInRange(Register value, unsigned lower_limit,
                       unsigned higher_limit, Label* on_in_range);

  // It assumes that the arguments are located below the stack pointer.
  MemOperand ReceiverOperand() { return MemOperand(sp, 0); }

  // Tiering support.
  void AssertFeedbackCell(Register object,
                          Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void AssertFeedbackVector(Register object,
                            Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void ReplaceClosureCodeWithOptimizedCode(Register optimized_code,
                                           Register closure);
  void GenerateTailCallToReturnedCode(Runtime::FunctionId function_id);
  Condition LoadFeedbackVectorFlagsAndCheckIfNeedsProcessing(
      Register flags, Register feedback_vector, CodeKind current_code_kind);
  void LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      Register flags, Register feedback_vector, CodeKind current_code_kind,
      Label* flags_need_processing);
  void OptimizeCodeOrTailCallOptimizedCodeSlot(Register flags,
                                               Register feedback_vector);

  // ---------------------------------------------------------------------------
  // Runtime calls

  // Call a runtime routine.
  void CallRuntime(const Runtime::Function* f, int num_arguments);

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid) {
    const Runtime::Function* function = Runtime::FunctionForId(fid);
    CallRuntime(function, function->nargs);
  }

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid, int num_arguments) {
    CallRuntime(Runtime::FunctionForId(fid), num_arguments);
  }

  // Convenience function: tail call a runtime routine (jump).
  void TailCallRuntime(Runtime::FunctionId fid);

  // Jump to a runtime routine.
  void JumpToExternalReference(const ExternalReference& builtin,
                               bool builtin_exit_frame = false);

  // ---------------------------------------------------------------------------
  // In-place weak references.
  void LoadWeakValue(Register out, Register in, Label* target_if_cleared);

  // ---------------------------------------------------------------------------
  // StatsCounter support

  void IncrementCounter(StatsCounter* counter, int value, Register scratch1,
                        Register scratch2) {
    if (!v8_flags.native_code_counters) return;
    EmitIncrementCounter(counter, value, scratch1, scratch2);
  }
  void EmitIncrementCounter(StatsCounter* counter, int value, Register scratch1,
                            Register scratch2);
  void DecrementCounter(StatsCounter* counter, int value, Register scratch1,
                        Register scratch2) {
    if (!v8_flags.native_code_counters) return;
    EmitDecrementCounter(counter, value, scratch1, scratch2);
  }
  void EmitDecrementCounter(StatsCounter* counter, int value, Register scratch1,
                            Register scratch2);

  // ---------------------------------------------------------------------------
  // Stack limit utilities
  void LoadStackLimit(Register destination, StackLimitKind kind);
  void StackOverflowCheck(Register num_args, Register scratch,
                          Label* stack_overflow);

  // ---------------------------------------------------------------------------
  // Smi utilities

  void SmiTag(Register reg, SBit s = LeaveCC);
  void SmiTag(Register dst, Register src, SBit s = LeaveCC);

  // Test if the register contains a smi (Z == 0 (eq) if true).
  void SmiTst(Register value);
  // Jump if either of the registers contain a non-smi.
  void JumpIfNotSmi(Register value, Label* not_smi_label);

  // Abort execution if argument is a smi, enabled via --debug-code.
  void AssertNotSmi(Register object,
                    AbortReason reason = AbortReason::kOperandIsASmi)
      NOOP_UNLESS_DEBUG_CODE;
  void AssertSmi(Register object,
                 AbortReason reason = AbortReason::kOperandIsNotASmi)
      NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a Constructor, enabled via --debug-code.
  void AssertConstructor(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a JSFunction, enabled via --debug-code.
  void AssertFunction(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a callable JSFunction, enabled via
  // --debug-code.
  void AssertCallableFunction(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a JSBoundFunction,
  // enabled via --debug-code.
  void AssertBoundFunction(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a JSGeneratorObject (or subclass),
  // enabled via --debug-code.
  void AssertGeneratorObject(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not undefined or an AllocationSite, enabled
  // via --debug-code.
  void AssertUndefinedOrAllocationSite(Register object,
                                       Register scratch) NOOP_UNLESS_DEBUG_CODE;

  void AssertJSAny(Register object, Register map_tmp, Register tmp,
                   AbortReason abort_reason) NOOP_UNLESS_DEBUG_CODE;

  template <typename Field>
  void DecodeField(Register dst, Register src) {
    Ubfx(dst, src, Field::kShift, Field::kSize);
  }

  template <typename Field>
  void DecodeField(Register reg) {
    DecodeField<Field>(reg, reg);
  }

  void TestCodeIsMarkedForDeoptimization(Register code, Register scratch);
  Operand ClearedValue() const;

 private:
  // Helper functions for generating invokes.
  void InvokePrologue(Register expected_parameter_count,
                      Register actual_parameter_count, Label* done,
                      InvokeType type);

  // Compare single values and then load the fpscr flags to a register.
  void VFPCompareAndLoadFlags(const SwVfpRegister src1,
                              const SwVfpRegister src2,
                              const Register fpscr_flags,
                              const Condition cond = al);
  void VFPCompareAndLoadFlags(const SwVfpRegister src1, const float src2,
                              const Register fpscr_flags,
                              const Condition cond = al);

  // Compare double values and then load the fpscr flags to a register.
  void VFPCompareAndLoadFlags(const DwVfpRegister src1,
                              const DwVfpRegister src2,
                              const Register fpscr_flags,
                              const Condition cond = al);
  void VFPCompareAndLoadFlags(const DwVfpRegister src1, const double src2,
                              const Register fpscr_flags,
                              const Condition cond = al);

  void Jump(intptr_t target, RelocInfo::Mode rmode, Condition cond = al);

  // Implementation helpers for FloatMin and FloatMax.
  template <typename T>
  void FloatMaxHelper(T result, T left, T right, Label* out_of_line);
  template <typename T>
  void FloatMinHelper(T result, T left, T right, Label* out_of_line);
  template <typename T>
  void FloatMaxOutOfLineHelper(T result, T left, T right);
  template <typename T>
  void FloatMinOutOfLineHelper(T result, T left, T right);

  int CalculateStackPassedWords(int num_reg_arguments,
                                int num_double_arguments);

  DISALLOW_IMPLICIT_CONSTRUCTORS(MacroAssembler);
};

struct MoveCycleState {
  // List of scratch registers reserved for pending moves in a move cycle, and
  // which should therefore not be used as a temporary location by
  // {MoveToTempLocation}. The GP scratch register is implicitly reserved.
  VfpRegList scratch_v_reglist = 0;
  // Available scratch registers during the move cycle resolution scope.
  base::Optional<UseScratchRegisterScope> temps;
  // InstructionStream of the scratch register picked by {MoveToTempLocation}.
  int scratch_reg_code = -1;
};

// Provides access to exit frame parameters (GC-ed).
inline MemOperand ExitFrameStackSlotOperand(int offset) {
  // The slot at [sp] is reserved in all ExitFrames for storing the return
  // address before doing the actual call, it's necessary for frame iteration
  // (see StoreReturnAddressAndCall for details).
  static constexpr int kSPOffset = 1 * kPointerSize;
  return MemOperand(sp, kSPOffset + offset);
}

// Provides access to exit frame stack space (not GC-ed).
inline MemOperand ExitFrameCallerStackSlotOperand(int index) {
  return MemOperand(
      fp, (BuiltinExitFrameConstants::kFixedSlotCountAboveFp + index) *
              kSystemPointerSize);
}

// Calls an API function.  Allocates HandleScope, extracts returned value
// from handle and propagates exceptions.  Restores context.  On return removes
// *stack_space_operand * kSystemPointerSize or stack_space * kSystemPointerSize
// (GCed, includes the call JS arguments space and the additional space
// allocated for the fast call).
void CallApiFunctionAndReturn(MacroAssembler* masm, bool with_profiling,
                              Register function_address,
                              ExternalReference thunk_ref, Register thunk_arg,
                              int stack_space, MemOperand* stack_space_operand,
                              MemOperand return_value_operand);

#define ACCESS_MASM(masm) masm->

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM_MACRO_ASSEMBLER_ARM_H_
