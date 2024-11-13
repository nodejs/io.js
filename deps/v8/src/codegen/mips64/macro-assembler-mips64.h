// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDED_FROM_MACRO_ASSEMBLER_H
#error This header must be included via macro-assembler.h
#endif

#ifndef V8_CODEGEN_MIPS64_MACRO_ASSEMBLER_MIPS64_H_
#define V8_CODEGEN_MIPS64_MACRO_ASSEMBLER_MIPS64_H_

#include <optional>

#include "src/codegen/assembler.h"
#include "src/codegen/mips64/assembler-mips64.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/objects/tagged-index.h"

namespace v8 {
namespace internal {

// Forward declarations.
enum class AbortReason : uint8_t;

// Reserved Register Usage Summary.
//
// Registers t8, t9, and at are reserved for use by the MacroAssembler.
//
// The programmer should know that the MacroAssembler may clobber these three,
// but won't touch other registers except in special cases.
//
// Per the MIPS ABI, register t9 must be used for indirect function call
// via 'jalr t9' or 'jr t9' instructions. This is relied upon by gcc when
// trying to update gp register for position-independent-code. Whenever
// MIPS generated code calls C code, it must be via t9 register.

// Allow programmer to use Branch Delay Slot of Branches, Jumps, Calls.
enum BranchDelaySlot { USE_DELAY_SLOT, PROTECT };

// Flags used for the li macro-assembler function.
enum LiFlags {
  // If the constant value can be represented in just 16 bits, then
  // optimize the li to use a single instruction, rather than lui/ori/dsll
  // sequence. A number of other optimizations that emits less than
  // maximum number of instructions exists.
  OPTIMIZE_SIZE = 0,
  // Always use 6 instructions (lui/ori/dsll sequence) for release 2 or 4
  // instructions for release 6 (lui/ori/dahi/dati), even if the constant
  // could be loaded with just one, so that this value is patchable later.
  CONSTANT_SIZE = 1,
  // For address loads only 4 instruction are required. Used to mark
  // constant load that will be used as address without relocation
  // information. It ensures predictable code size, so specific sites
  // in code are patchable.
  ADDRESS_LOAD = 2
};

enum RAStatus { kRAHasNotBeenSaved, kRAHasBeenSaved };

Register GetRegisterThatIsNotOneOf(Register reg1, Register reg2 = no_reg,
                                   Register reg3 = no_reg,
                                   Register reg4 = no_reg,
                                   Register reg5 = no_reg,
                                   Register reg6 = no_reg);

// -----------------------------------------------------------------------------
// Static helper functions.

#if defined(V8_TARGET_LITTLE_ENDIAN)
#define SmiWordOffset(offset) (offset + kPointerSize / 2)
#else
#define SmiWordOffset(offset) offset
#endif

// Generate a MemOperand for loading a field from an object.
inline MemOperand FieldMemOperand(Register object, int offset) {
  return MemOperand(object, offset - kHeapObjectTag);
}

// Generate a MemOperand for storing arguments 5..N on the stack
// when calling CallCFunction().
// TODO(plind): Currently ONLY used for O32. Should be fixed for
//              n64, and used in RegExp code, and other places
//              with more than 8 arguments.
inline MemOperand CFunctionArgumentOperand(int index) {
  DCHECK_GT(index, kCArgSlotCount);
  // Argument 5 takes the slot just past the four Arg-slots.
  int offset = (index - 5) * kPointerSize + kCArgsSlotsSize;
  return MemOperand(sp, offset);
}

class V8_EXPORT_PRIVATE MacroAssembler : public MacroAssemblerBase {
 public:
  using MacroAssemblerBase::MacroAssemblerBase;

  // Activation support.
  void EnterFrame(StackFrame::Type type);
  void EnterFrame(StackFrame::Type type, bool load_constant_pool_pointer_reg) {
    // Out-of-line constant pool not implemented on mips.
    UNREACHABLE();
  }
  void LeaveFrame(StackFrame::Type type);

  void AllocateStackSpace(Register bytes) { Dsubu(sp, sp, bytes); }

  void AllocateStackSpace(int bytes) {
    DCHECK_GE(bytes, 0);
    if (bytes == 0) return;
    Dsubu(sp, sp, Operand(bytes));
  }

  // Generates function and stub prologue code.
  void StubPrologue(StackFrame::Type type);
  void Prologue();

  void InitializeRootRegister() {
    ExternalReference isolate_root = ExternalReference::isolate_root(isolate());
    li(kRootRegister, Operand(isolate_root));
  }

  // Jump unconditionally to given label.
  // We NEED a nop in the branch delay slot, as it used by v8, for example in
  // CodeGenerator::ProcessDeferred().
  // Currently the branch delay slot is filled by the MacroAssembler.
  // Use rather b(Label) for code generation.
  void jmp(Label* L) { Branch(L); }

  // -------------------------------------------------------------------------
  // Debugging.

  void Trap();
  void DebugBreak();

  // Calls Abort(msg) if the condition cc is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cc, AbortReason reason, Register rs,
              Operand rt) NOOP_UNLESS_DEBUG_CODE;

  void AssertJSAny(Register object, Register map_tmp, Register tmp,
                   AbortReason abort_reason) NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but always enabled.
  void Check(Condition cc, AbortReason reason, Register rs, Operand rt);

  // Print a message to stdout and abort execution.
  void Abort(AbortReason msg);

  // Arguments macros.
#define COND_TYPED_ARGS Condition cond, Register r1, const Operand &r2
#define COND_ARGS cond, r1, r2

  // Cases when relocation is not needed.
#define DECLARE_NORELOC_PROTOTYPE(Name, target_type)                          \
  void Name(target_type target, BranchDelaySlot bd = PROTECT);                \
  inline void Name(BranchDelaySlot bd, target_type target) {                  \
    Name(target, bd);                                                         \
  }                                                                           \
  void Name(target_type target, COND_TYPED_ARGS,                              \
            BranchDelaySlot bd = PROTECT);                                    \
  inline void Name(BranchDelaySlot bd, target_type target, COND_TYPED_ARGS) { \
    Name(target, COND_ARGS, bd);                                              \
  }

#define DECLARE_BRANCH_PROTOTYPES(Name)   \
  DECLARE_NORELOC_PROTOTYPE(Name, Label*) \
  DECLARE_NORELOC_PROTOTYPE(Name, int32_t)

  DECLARE_BRANCH_PROTOTYPES(Branch)
  DECLARE_BRANCH_PROTOTYPES(BranchAndLink)
  DECLARE_BRANCH_PROTOTYPES(BranchShort)

#undef DECLARE_BRANCH_PROTOTYPES
#undef COND_TYPED_ARGS
#undef COND_ARGS

  // Floating point branches
  void CompareF32(FPUCondition cc, FPURegister cmp1, FPURegister cmp2) {
    CompareF(S, cc, cmp1, cmp2);
  }

  void CompareIsNanF32(FPURegister cmp1, FPURegister cmp2) {
    CompareIsNanF(S, cmp1, cmp2);
  }

  void CompareF64(FPUCondition cc, FPURegister cmp1, FPURegister cmp2) {
    CompareF(D, cc, cmp1, cmp2);
  }

  void CompareIsNanF64(FPURegister cmp1, FPURegister cmp2) {
    CompareIsNanF(D, cmp1, cmp2);
  }

  void BranchTrueShortF(Label* target, BranchDelaySlot bd = PROTECT);
  void BranchFalseShortF(Label* target, BranchDelaySlot bd = PROTECT);

  void BranchTrueF(Label* target, BranchDelaySlot bd = PROTECT);
  void BranchFalseF(Label* target, BranchDelaySlot bd = PROTECT);

  // MSA branches
  void BranchMSA(Label* target, MSABranchDF df, MSABranchCondition cond,
                 MSARegister wt, BranchDelaySlot bd = PROTECT);

  void CompareWord(Condition cond, Register dst, Register lhs,
                   const Operand& rhs);

  void BranchLong(int32_t offset, BranchDelaySlot bdslot = PROTECT);
  void Branch(Label* L, Condition cond, Register rs, RootIndex index,
              BranchDelaySlot bdslot = PROTECT);

  static int InstrCountForLi64Bit(int64_t value);
  inline void LiLower32BitHelper(Register rd, Operand j);
  void li_optimized(Register rd, Operand j, LiFlags mode = OPTIMIZE_SIZE);
  // Load int32 in the rd register.
  void li(Register rd, Operand j, LiFlags mode = OPTIMIZE_SIZE);
  inline void li(Register rd, int64_t j, LiFlags mode = OPTIMIZE_SIZE) {
    li(rd, Operand(j), mode);
  }
  // inline void li(Register rd, int32_t j, LiFlags mode = OPTIMIZE_SIZE) {
  //   li(rd, Operand(static_cast<int64_t>(j)), mode);
  // }
  void li(Register dst, Handle<HeapObject> value, LiFlags mode = OPTIMIZE_SIZE);
  void li(Register dst, ExternalReference value, LiFlags mode = OPTIMIZE_SIZE);

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
  MemOperand ExternalReferenceAsOperand(IsolateFieldId id) {
    return ExternalReferenceAsOperand(ExternalReference::Create(id), no_reg);
  }

  inline void Move(Register output, MemOperand operand) { Ld(output, operand); }

// Jump, Call, and Ret pseudo instructions implementing inter-working.
#define COND_ARGS                                  \
  Condition cond = al, Register rs = zero_reg,     \
            const Operand &rt = Operand(zero_reg), \
            BranchDelaySlot bd = PROTECT

  void Jump(Register target, COND_ARGS);
  void Jump(intptr_t target, RelocInfo::Mode rmode, COND_ARGS);
  void Jump(Address target, RelocInfo::Mode rmode, COND_ARGS);
  // Deffer from li, this method save target to the memory, and then load
  // it to register use ld, it can be used in wasm jump table for concurrent
  // patching.
  void PatchAndJump(Address target);
  void Jump(Handle<Code> code, RelocInfo::Mode rmode, COND_ARGS);
  void Jump(const ExternalReference& reference);
  void Call(Register target, COND_ARGS);
  void Call(Address target, RelocInfo::Mode rmode, COND_ARGS);
  void Call(Handle<Code> code, RelocInfo::Mode rmode = RelocInfo::CODE_TARGET,
            COND_ARGS);
  void Call(Label* target);
  void LoadAddress(Register dst, Label* target);
  void LoadAddressPCRelative(Register dst, Label* target);

  // Load the builtin given by the Smi in |builtin_index| into |target|.
  void LoadEntryFromBuiltinIndex(Register builtin_index, Register target);
  void LoadEntryFromBuiltin(Builtin builtin, Register destination);
  MemOperand EntryFromBuiltinAsOperand(Builtin builtin);

  void CallBuiltinByIndex(Register builtin_index, Register target);
  void CallBuiltin(Builtin builtin);
  void TailCallBuiltin(Builtin builtin);
  void TailCallBuiltin(Builtin builtin, Condition cond, Register type,
                       Operand range);

  // Load the code entry point from the Code object.
  void LoadCodeInstructionStart(Register destination,
                                Register code_data_container_object,
                                CodeEntrypointTag tag);
  void CallCodeObject(Register code_data_container_object,
                      CodeEntrypointTag tag);
  void JumpCodeObject(Register code_data_container_object,
                      CodeEntrypointTag tag,
                      JumpMode jump_mode = JumpMode::kJump);

  // Convenience functions to call/jmp to the code of a JSFunction object.
  void CallJSFunction(Register function_object);
  void JumpJSFunction(Register function_object,
                      JumpMode jump_mode = JumpMode::kJump);

  // Generates an instruction sequence s.t. the return address points to the
  // instruction following the call.
  // The return address on the stack is used by frame iteration.
  void StoreReturnAddressAndCall(Register target);

  void CallForDeoptimization(Builtin target, int deopt_id, Label* exit,
                             DeoptimizeKind kind, Label* ret,
                             Label* jump_deoptimization_entry_label);

  void Ret(COND_ARGS);
  inline void Ret(BranchDelaySlot bd, Condition cond = al,
                  Register rs = zero_reg,
                  const Operand& rt = Operand(zero_reg)) {
    Ret(cond, rs, rt, bd);
  }

  // Emit code to discard a non-negative number of pointer-sized elements
  // from the stack, clobbering only the sp register.
  void Drop(int count, Condition cond = cc_always, Register reg = no_reg,
            const Operand& op = Operand(no_reg));

  void DropArguments(Register count);
  void DropArgumentsAndPushNewReceiver(Register argc, Register receiver);

  // Trivial case of DropAndRet that utilizes the delay slot.
  void DropAndRet(int drop);

  void DropAndRet(int drop, Condition cond, Register reg, const Operand& op);

  void Ld(Register rd, const MemOperand& rs);
  void Sd(Register rd, const MemOperand& rs);

  void push(Register src) {
    Daddu(sp, sp, Operand(-kPointerSize));
    Sd(src, MemOperand(sp, 0));
  }
  void Push(Register src) { push(src); }
  void Push(Handle<HeapObject> handle);
  void Push(Tagged<Smi> smi);

  // Push two registers. Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2) {
    Dsubu(sp, sp, Operand(2 * kPointerSize));
    Sd(src1, MemOperand(sp, 1 * kPointerSize));
    Sd(src2, MemOperand(sp, 0 * kPointerSize));
  }

  // Push three registers. Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Register src3) {
    Dsubu(sp, sp, Operand(3 * kPointerSize));
    Sd(src1, MemOperand(sp, 2 * kPointerSize));
    Sd(src2, MemOperand(sp, 1 * kPointerSize));
    Sd(src3, MemOperand(sp, 0 * kPointerSize));
  }

  // Push four registers. Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Register src3, Register src4) {
    Dsubu(sp, sp, Operand(4 * kPointerSize));
    Sd(src1, MemOperand(sp, 3 * kPointerSize));
    Sd(src2, MemOperand(sp, 2 * kPointerSize));
    Sd(src3, MemOperand(sp, 1 * kPointerSize));
    Sd(src4, MemOperand(sp, 0 * kPointerSize));
  }

  // Push five registers. Pushes leftmost register first (to highest address).
  void Push(Register src1, Register src2, Register src3, Register src4,
            Register src5) {
    Dsubu(sp, sp, Operand(5 * kPointerSize));
    Sd(src1, MemOperand(sp, 4 * kPointerSize));
    Sd(src2, MemOperand(sp, 3 * kPointerSize));
    Sd(src3, MemOperand(sp, 2 * kPointerSize));
    Sd(src4, MemOperand(sp, 1 * kPointerSize));
    Sd(src5, MemOperand(sp, 0 * kPointerSize));
  }

  void Push(Register src, Condition cond, Register tst1, Register tst2) {
    // Since we don't have conditional execution we use a Branch.
    Branch(3, cond, tst1, Operand(tst2));
    Dsubu(sp, sp, Operand(kPointerSize));
    Sd(src, MemOperand(sp, 0));
  }

  enum PushArrayOrder { kNormal, kReverse };
  void PushArray(Register array, Register size, Register scratch,
                 Register scratch2, PushArrayOrder order = kNormal);

  void MaybeSaveRegisters(RegList registers);
  void MaybeRestoreRegisters(RegList registers);

  void CallEphemeronKeyBarrier(Register object, Register slot_address,
                               SaveFPRegsMode fp_mode);

  void CallRecordWriteStubSaveRegisters(
      Register object, Register slot_address, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);
  void CallRecordWriteStub(
      Register object, Register slot_address, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);

  // Push multiple registers on the stack.
  // Registers are saved in numerical order, with higher numbered registers
  // saved in higher memory addresses.
  void MultiPush(RegList regs);
  void MultiPushFPU(DoubleRegList regs);
  void MultiPushMSA(DoubleRegList regs);

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

  void pop(Register dst) {
    Ld(dst, MemOperand(sp, 0));
    Daddu(sp, sp, Operand(kPointerSize));
  }
  void Pop(Register dst) { pop(dst); }

  // Pop two registers. Pops rightmost register first (from lower address).
  void Pop(Register src1, Register src2) {
    DCHECK(src1 != src2);
    Ld(src2, MemOperand(sp, 0 * kPointerSize));
    Ld(src1, MemOperand(sp, 1 * kPointerSize));
    Daddu(sp, sp, 2 * kPointerSize);
  }

  // Pop three registers. Pops rightmost register first (from lower address).
  void Pop(Register src1, Register src2, Register src3) {
    Ld(src3, MemOperand(sp, 0 * kPointerSize));
    Ld(src2, MemOperand(sp, 1 * kPointerSize));
    Ld(src1, MemOperand(sp, 2 * kPointerSize));
    Daddu(sp, sp, 3 * kPointerSize);
  }

  void Pop(uint32_t count = 1) { Daddu(sp, sp, Operand(count * kPointerSize)); }

  // Pops multiple values from the stack and load them in the
  // registers specified in regs. Pop order is the opposite as in MultiPush.
  void MultiPop(RegList regs);
  void MultiPopFPU(DoubleRegList regs);
  void MultiPopMSA(DoubleRegList regs);

#define DEFINE_INSTRUCTION(instr)                          \
  void instr(Register rd, Register rs, const Operand& rt); \
  void instr(Register rd, Register rs, Register rt) {      \
    instr(rd, rs, Operand(rt));                            \
  }                                                        \
  void instr(Register rs, Register rt, int32_t j) { instr(rs, rt, Operand(j)); }

#define DEFINE_INSTRUCTION2(instr)                                 \
  void instr(Register rs, const Operand& rt);                      \
  void instr(Register rs, Register rt) { instr(rs, Operand(rt)); } \
  void instr(Register rs, int32_t j) { instr(rs, Operand(j)); }

  DEFINE_INSTRUCTION(Addu)
  DEFINE_INSTRUCTION(Daddu)
  DEFINE_INSTRUCTION(Div)
  DEFINE_INSTRUCTION(Divu)
  DEFINE_INSTRUCTION(Ddivu)
  DEFINE_INSTRUCTION(Mod)
  DEFINE_INSTRUCTION(Modu)
  DEFINE_INSTRUCTION(Ddiv)
  DEFINE_INSTRUCTION(Subu)
  DEFINE_INSTRUCTION(Dsubu)
  DEFINE_INSTRUCTION(Dmod)
  DEFINE_INSTRUCTION(Dmodu)
  DEFINE_INSTRUCTION(Mul)
  DEFINE_INSTRUCTION(Mulh)
  DEFINE_INSTRUCTION(Mulhu)
  DEFINE_INSTRUCTION(Dmul)
  DEFINE_INSTRUCTION(Dmulh)
  DEFINE_INSTRUCTION(Dmulhu)
  DEFINE_INSTRUCTION2(Mult)
  DEFINE_INSTRUCTION2(Dmult)
  DEFINE_INSTRUCTION2(Multu)
  DEFINE_INSTRUCTION2(Dmultu)
  DEFINE_INSTRUCTION2(Div)
  DEFINE_INSTRUCTION2(Ddiv)
  DEFINE_INSTRUCTION2(Divu)
  DEFINE_INSTRUCTION2(Ddivu)

  DEFINE_INSTRUCTION(And)
  DEFINE_INSTRUCTION(Or)
  DEFINE_INSTRUCTION(Xor)
  DEFINE_INSTRUCTION(Nor)
  DEFINE_INSTRUCTION2(Neg)

  DEFINE_INSTRUCTION(Slt)
  DEFINE_INSTRUCTION(Sltu)
  DEFINE_INSTRUCTION(Sle)
  DEFINE_INSTRUCTION(Sleu)
  DEFINE_INSTRUCTION(Sgt)
  DEFINE_INSTRUCTION(Sgtu)
  DEFINE_INSTRUCTION(Sge)
  DEFINE_INSTRUCTION(Sgeu)

  // MIPS32 R2 instruction macro.
  DEFINE_INSTRUCTION(Ror)
  DEFINE_INSTRUCTION(Dror)

#undef DEFINE_INSTRUCTION
#undef DEFINE_INSTRUCTION2
#undef DEFINE_INSTRUCTION3

  void SmiTag(Register dst, Register src) {
    static_assert(kSmiTag == 0);
    if (SmiValuesAre32Bits()) {
      dsll32(dst, src, 0);
    } else {
      DCHECK(SmiValuesAre31Bits());
      Addu(dst, src, src);
    }
  }

  void SmiTag(Register reg) { SmiTag(reg, reg); }

  void SmiUntag(Register dst, const MemOperand& src);
  void SmiUntag(Register dst, Register src) {
    if (SmiValuesAre32Bits()) {
      dsra32(dst, src, kSmiShift - 32);
    } else {
      DCHECK(SmiValuesAre31Bits());
      sra(dst, src, kSmiShift);
    }
  }

  void SmiUntag(Register reg) { SmiUntag(reg, reg); }

  // Left-shifted from int32 equivalent of Smi.
  void SmiScale(Register dst, Register src, int scale) {
    if (SmiValuesAre32Bits()) {
      // The int portion is upper 32-bits of 64-bit word.
      dsra(dst, src, kSmiShift - scale);
    } else {
      DCHECK(SmiValuesAre31Bits());
      DCHECK_GE(scale, kSmiTagSize);
      sll(dst, src, scale - kSmiTagSize);
    }
  }

  // On MIPS64, we should sign-extend 32-bit values.
  void SmiToInt32(Register smi) {
    if (v8_flags.enable_slow_asserts) {
      AssertSmi(smi);
    }
    DCHECK(SmiValuesAre32Bits() || SmiValuesAre31Bits());
    SmiUntag(smi);
  }

  // Abort execution if argument is a smi, enabled via --debug-code.
  void AssertNotSmi(Register object) NOOP_UNLESS_DEBUG_CODE;
  void AssertSmi(Register object) NOOP_UNLESS_DEBUG_CODE;

  int CalculateStackPassedWords(int num_reg_arguments,
                                int num_double_arguments);

  // Before calling a C-function from generated code, align arguments on stack
  // and add space for the four mips argument slots.
  // After aligning the frame, non-register arguments must be stored on the
  // stack, after the argument-slots using helper: CFunctionArgumentOperand().
  // The argument count assumes all arguments are word sized.
  // Some compilers/platforms require the stack to be aligned when calling
  // C++ code.
  // Needs a scratch register to do some arithmetic. This register will be
  // trashed.
  void PrepareCallCFunction(int num_reg_arguments, int num_double_registers,
                            Register scratch);
  void PrepareCallCFunction(int num_reg_arguments, Register scratch);

  // Arguments 1-4 are placed in registers a0 through a3 respectively.
  // Arguments 5..n are stored to stack using following:
  //  Sw(a4, CFunctionArgumentOperand(5));

  // Calls a C function and cleans up the space for arguments allocated
  // by PrepareCallCFunction. The called function is not allowed to trigger a
  // garbage collection, since that might move the code and invalidate the
  // return address (unless this is somehow accounted for by the called
  // function).
  int CallCFunction(
      ExternalReference function, int num_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  int CallCFunction(
      Register function, int num_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  int CallCFunction(
      ExternalReference function, int num_reg_arguments,
      int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  int CallCFunction(
      Register function, int num_reg_arguments, int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  void MovFromFloatResult(DoubleRegister dst);
  void MovFromFloatParameter(DoubleRegister dst);

  // There are two ways of passing double arguments on MIPS, depending on
  // whether soft or hard floating point ABI is used. These functions
  // abstract parameter passing for the three different ways we call
  // C functions from generated code.
  void MovToFloatParameter(DoubleRegister src);
  void MovToFloatParameters(DoubleRegister src1, DoubleRegister src2);
  void MovToFloatResult(DoubleRegister src);

  // See comments at the beginning of Builtins::Generate_CEntry.
  inline void PrepareCEntryArgs(int num_args) { li(a0, num_args); }
  inline void PrepareCEntryFunction(const ExternalReference& ref) {
    li(a1, ref);
  }

  void CheckPageFlag(Register object, Register scratch, int mask, Condition cc,
                     Label* condition_met);
#undef COND_ARGS

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32.
  // Exits with 'result' holding the answer.
  void TruncateDoubleToI(Isolate* isolate, Zone* zone, Register result,
                         DoubleRegister double_input, StubCallMode stub_mode);

  // Conditional move.
  void Movz(Register rd, Register rs, Register rt);
  void Movn(Register rd, Register rs, Register rt);
  void Movt(Register rd, Register rs, uint16_t cc = 0);
  void Movf(Register rd, Register rs, uint16_t cc = 0);

  void LoadZeroIfFPUCondition(Register dest);
  void LoadZeroIfNotFPUCondition(Register dest);

  void LoadZeroIfConditionNotZero(Register dest, Register condition);
  void LoadZeroIfConditionZero(Register dest, Register condition);

  void Clz(Register rd, Register rs);
  void Dclz(Register rd, Register rs);
  void Ctz(Register rd, Register rs);
  void Dctz(Register rd, Register rs);
  void Popcnt(Register rd, Register rs);
  void Dpopcnt(Register rd, Register rs);

  // MIPS64 R2 instruction macro.
  void Ext(Register rt, Register rs, uint16_t pos, uint16_t size);
  void Dext(Register rt, Register rs, uint16_t pos, uint16_t size);
  void Ins(Register rt, Register rs, uint16_t pos, uint16_t size);
  void Dins(Register rt, Register rs, uint16_t pos, uint16_t size);
  void ExtractBits(Register dest, Register source, Register pos, int size,
                   bool sign_extend = false);
  void InsertBits(Register dest, Register source, Register pos, int size);
  void Neg_s(FPURegister fd, FPURegister fs);
  void Neg_d(FPURegister fd, FPURegister fs);

  // MIPS64 R6 instruction macros.
  void Bovc(Register rt, Register rs, Label* L);
  void Bnvc(Register rt, Register rs, Label* L);

  // Convert single to unsigned word.
  void Trunc_uw_s(FPURegister fd, FPURegister fs, FPURegister scratch);
  void Trunc_uw_s(Register rd, FPURegister fs, FPURegister scratch);

  // Change endianness
  void ByteSwapSigned(Register dest, Register src, int operand_size);
  void ByteSwapUnsigned(Register dest, Register src, int operand_size);

  void Ulh(Register rd, const MemOperand& rs);
  void Ulhu(Register rd, const MemOperand& rs);
  void Ush(Register rd, const MemOperand& rs, Register scratch);

  void Ulw(Register rd, const MemOperand& rs);
  void Ulwu(Register rd, const MemOperand& rs);
  void Usw(Register rd, const MemOperand& rs);

  void Uld(Register rd, const MemOperand& rs);
  void Usd(Register rd, const MemOperand& rs);

  void Ulwc1(FPURegister fd, const MemOperand& rs, Register scratch);
  void Uswc1(FPURegister fd, const MemOperand& rs, Register scratch);

  void Uldc1(FPURegister fd, const MemOperand& rs, Register scratch);
  void Usdc1(FPURegister fd, const MemOperand& rs, Register scratch);

  void Lb(Register rd, const MemOperand& rs);
  void Lbu(Register rd, const MemOperand& rs);
  void Sb(Register rd, const MemOperand& rs);

  void Lh(Register rd, const MemOperand& rs);
  void Lhu(Register rd, const MemOperand& rs);
  void Sh(Register rd, const MemOperand& rs);

  void Lw(Register rd, const MemOperand& rs);
  void Lwu(Register rd, const MemOperand& rs);
  void Sw(Register rd, const MemOperand& rs);

  void Lwc1(FPURegister fd, const MemOperand& src);
  void Swc1(FPURegister fs, const MemOperand& dst);

  void Ldc1(FPURegister fd, const MemOperand& src);
  void Sdc1(FPURegister fs, const MemOperand& dst);

  void Ll(Register rd, const MemOperand& rs);
  void Sc(Register rd, const MemOperand& rs);

  void Lld(Register rd, const MemOperand& rs);
  void Scd(Register rd, const MemOperand& rs);

  // Perform a floating-point min or max operation with the
  // (IEEE-754-compatible) semantics of MIPS32's Release 6 MIN.fmt/MAX.fmt.
  // Some cases, typically NaNs or +/-0.0, are expected to be rare and are
  // handled in out-of-line code. The specific behaviour depends on supported
  // instructions.
  //
  // These functions assume (and assert) that src1!=src2. It is permitted
  // for the result to alias either input register.
  void Float32Max(FPURegister dst, FPURegister src1, FPURegister src2,
                  Label* out_of_line);
  void Float32Min(FPURegister dst, FPURegister src1, FPURegister src2,
                  Label* out_of_line);
  void Float64Max(FPURegister dst, FPURegister src1, FPURegister src2,
                  Label* out_of_line);
  void Float64Min(FPURegister dst, FPURegister src1, FPURegister src2,
                  Label* out_of_line);

  // Generate out-of-line cases for the macros above.
  void Float32MaxOutOfLine(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float32MinOutOfLine(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float64MaxOutOfLine(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float64MinOutOfLine(FPURegister dst, FPURegister src1, FPURegister src2);

  bool IsDoubleZeroRegSet() { return has_double_zero_reg_set_; }

  void LoadIsolateField(Register dst, IsolateFieldId id);

  void mov(Register rd, Register rt) { or_(rd, rt, zero_reg); }

  inline void Move(Register dst, Handle<HeapObject> handle) { li(dst, handle); }
  inline void Move(Register dst, Tagged<Smi> value) { li(dst, Operand(value)); }

  inline void Move(Register dst, Register src) {
    if (dst != src) {
      mov(dst, src);
    }
  }

  inline void Move(FPURegister dst, FPURegister src) { Move_d(dst, src); }

  inline void Move(Register dst_low, Register dst_high, FPURegister src) {
    mfc1(dst_low, src);
    mfhc1(dst_high, src);
  }

  inline void Move(Register dst, FPURegister src) { dmfc1(dst, src); }

  inline void Move(FPURegister dst, Register src) { dmtc1(src, dst); }

  inline void FmoveHigh(Register dst_high, FPURegister src) {
    mfhc1(dst_high, src);
  }

  inline void FmoveHigh(FPURegister dst, Register src_high) {
    mthc1(src_high, dst);
  }

  inline void FmoveLow(Register dst_low, FPURegister src) {
    mfc1(dst_low, src);
  }

  void FmoveLow(FPURegister dst, Register src_low);

  inline void Move(FPURegister dst, Register src_low, Register src_high) {
    mtc1(src_low, dst);
    mthc1(src_high, dst);
  }

  inline void Move_d(FPURegister dst, FPURegister src) {
    if (dst != src) {
      mov_d(dst, src);
    }
  }

  inline void Move_s(FPURegister dst, FPURegister src) {
    if (dst != src) {
      mov_s(dst, src);
    }
  }

  void Move(FPURegister dst, float imm) {
    Move(dst, base::bit_cast<uint32_t>(imm));
  }
  void Move(FPURegister dst, double imm) {
    Move(dst, base::bit_cast<uint64_t>(imm));
  }
  void Move(FPURegister dst, uint32_t src);
  void Move(FPURegister dst, uint64_t src);

  // DaddOverflow sets overflow register to a negative value if
  // overflow occured, otherwise it is zero or positive
  void DaddOverflow(Register dst, Register left, const Operand& right,
                    Register overflow);
  // DsubOverflow sets overflow register to a negative value if
  // overflow occured, otherwise it is zero or positive
  void DsubOverflow(Register dst, Register left, const Operand& right,
                    Register overflow);
  // [D]MulOverflow set overflow register to zero if no overflow occured
  void MulOverflow(Register dst, Register left, const Operand& right,
                   Register overflow);
  void DMulOverflow(Register dst, Register left, const Operand& right,
                    Register overflow);

// Number of instructions needed for calculation of switch table entry address
#ifdef _MIPS_ARCH_MIPS64R6
  static const int kSwitchTablePrologueSize = 6;
#else
  static const int kSwitchTablePrologueSize = 11;
#endif

  // GetLabelFunction must be lambda '[](size_t index) -> Label*' or a
  // functor/function with 'Label *func(size_t index)' declaration.
  template <typename Func>
  void GenerateSwitchTable(Register index, size_t case_count,
                           Func GetLabelFunction);

  // Load an object from the root table.
  void LoadRoot(Register destination, RootIndex index) final;
  void LoadRoot(Register destination, RootIndex index, Condition cond,
                Register src1, const Operand& src2);

  void LoadMap(Register destination, Register object);

  void LoadFeedbackVector(Register dst, Register closure, Register scratch,
                          Label* fbv_undef);

  // If the value is a NaN, canonicalize the value else, do nothing.
  void FPUCanonicalizeNaN(const DoubleRegister dst, const DoubleRegister src);

  // ---------------------------------------------------------------------------
  // FPU macros. These do not handle special cases like NaN or +- inf.

  // Convert unsigned word to double.
  void Cvt_d_uw(FPURegister fd, FPURegister fs);
  void Cvt_d_uw(FPURegister fd, Register rs);

  // Convert unsigned long to double.
  void Cvt_d_ul(FPURegister fd, FPURegister fs);
  void Cvt_d_ul(FPURegister fd, Register rs);

  // Convert unsigned word to float.
  void Cvt_s_uw(FPURegister fd, FPURegister fs);
  void Cvt_s_uw(FPURegister fd, Register rs);

  // Convert unsigned long to float.
  void Cvt_s_ul(FPURegister fd, FPURegister fs);
  void Cvt_s_ul(FPURegister fd, Register rs);

  // Convert double to unsigned word.
  void Trunc_uw_d(FPURegister fd, FPURegister fs, FPURegister scratch);
  void Trunc_uw_d(Register rd, FPURegister fs, FPURegister scratch);

  // Convert double to unsigned long.
  void Trunc_ul_d(FPURegister fd, FPURegister fs, FPURegister scratch,
                  Register result = no_reg);
  void Trunc_ul_d(Register rd, FPURegister fs, FPURegister scratch,
                  Register result = no_reg);

  // Convert single to unsigned long.
  void Trunc_ul_s(FPURegister fd, FPURegister fs, FPURegister scratch,
                  Register result = no_reg);
  void Trunc_ul_s(Register rd, FPURegister fs, FPURegister scratch,
                  Register result = no_reg);

  // Round double functions
  void Trunc_d_d(FPURegister fd, FPURegister fs);
  void Round_d_d(FPURegister fd, FPURegister fs);
  void Floor_d_d(FPURegister fd, FPURegister fs);
  void Ceil_d_d(FPURegister fd, FPURegister fs);

  // Round float functions
  void Trunc_s_s(FPURegister fd, FPURegister fs);
  void Round_s_s(FPURegister fd, FPURegister fs);
  void Floor_s_s(FPURegister fd, FPURegister fs);
  void Ceil_s_s(FPURegister fd, FPURegister fs);

  void LoadLane(MSASize sz, MSARegister dst, uint8_t laneidx, MemOperand src);
  void StoreLane(MSASize sz, MSARegister src, uint8_t laneidx, MemOperand dst);
  void ExtMulLow(MSADataType type, MSARegister dst, MSARegister src1,
                 MSARegister src2);
  void ExtMulHigh(MSADataType type, MSARegister dst, MSARegister src1,
                  MSARegister src2);
  void LoadSplat(MSASize sz, MSARegister dst, MemOperand src);
  void ExtAddPairwise(MSADataType type, MSARegister dst, MSARegister src);
  void MSARoundW(MSARegister dst, MSARegister src, FPURoundingMode mode);
  void MSARoundD(MSARegister dst, MSARegister src, FPURoundingMode mode);

  // Jump the register contains a smi.
  void JumpIfSmi(Register value, Label* smi_label,
                 BranchDelaySlot bd = PROTECT);

  void JumpIfEqual(Register a, int32_t b, Label* dest) {
    li(kScratchReg, Operand(b));
    Branch(dest, eq, a, Operand(kScratchReg));
  }

  void JumpIfLessThan(Register a, int32_t b, Label* dest) {
    li(kScratchReg, Operand(b));
    Branch(dest, lt, a, Operand(kScratchReg));
  }

  // Push a standard frame, consisting of ra, fp, context and JS function.
  void PushStandardFrame(Register function_reg);

  // Get the actual activation frame alignment for target environment.
  static int ActivationFrameAlignment();

  // Load Scaled Address instructions. Parameter sa (shift argument) must be
  // between [1, 31] (inclusive). On pre-r6 architectures the scratch register
  // may be clobbered.
  void Lsa(Register rd, Register rs, Register rt, uint8_t sa,
           Register scratch = at);
  void Dlsa(Register rd, Register rs, Register rt, uint8_t sa,
            Register scratch = at);

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

  // It assumes that the arguments are located below the stack pointer.
  void LoadReceiver(Register dest) { Ld(dest, MemOperand(sp, 0)); }
  void StoreReceiver(Register rec) { Sd(rec, MemOperand(sp, 0)); }

  bool IsNear(Label* L, Condition cond, int rs_reg);

  // Swap two registers.  If the scratch register is omitted then a slightly
  // less efficient form using xor instead of mov is emitted.
  void Swap(Register reg1, Register reg2, Register scratch = no_reg);

  void TestCodeIsMarkedForDeoptimizationAndJump(Register code_data_container,
                                                Register scratch,
                                                Condition cond, Label* target);
  Operand ClearedValue() const;

  void PushRoot(RootIndex index) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    LoadRoot(scratch, index);
    Push(scratch);
  }

  // Compare the object in a register to a value and jump if they are equal.
  void JumpIfRoot(Register with, RootIndex index, Label* if_equal) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    LoadRoot(scratch, index);
    Branch(if_equal, eq, with, Operand(scratch));
  }

  // Compare the object in a register to a value and jump if they are not equal.
  void JumpIfNotRoot(Register with, RootIndex index, Label* if_not_equal) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    LoadRoot(scratch, index);
    Branch(if_not_equal, ne, with, Operand(scratch));
  }

  // Checks if value is in range [lower_limit, higher_limit] using a single
  // comparison.
  void JumpIfIsInRange(Register value, unsigned lower_limit,
                       unsigned higher_limit, Label* on_in_range);

  // ---------------------------------------------------------------------------
  // GC Support

  // Notify the garbage collector that we wrote a pointer into an object.
  // |object| is the object being stored into, |value| is the object being
  // stored.  value and scratch registers are clobbered by the operation.
  // The offset is the offset from the start of the object, not the offset from
  // the tagged HeapObject pointer.  For use with FieldOperand(reg, off).
  void RecordWriteField(Register object, int offset, Register value,
                        Register scratch, RAStatus ra_status,
                        SaveFPRegsMode save_fp,
                        SmiCheck smi_check = SmiCheck::kInline);

  // For a given |object| notify the garbage collector that the slot |address|
  // has been written.  |value| is the object being stored. The value and
  // address registers are clobbered by the operation.
  void RecordWrite(Register object, Register address, Register value,
                   RAStatus ra_status, SaveFPRegsMode save_fp,
                   SmiCheck smi_check = SmiCheck::kInline);

  void Pref(int32_t hint, const MemOperand& rs);

  // ---------------------------------------------------------------------------
  // Pseudo-instructions.

  void LoadWordPair(Register rd, const MemOperand& rs, Register scratch = at);
  void StoreWordPair(Register rd, const MemOperand& rs, Register scratch = at);

  // Convert double to unsigned long.
  void Trunc_l_ud(FPURegister fd, FPURegister fs, FPURegister scratch);

  void Trunc_l_d(FPURegister fd, FPURegister fs);
  void Round_l_d(FPURegister fd, FPURegister fs);
  void Floor_l_d(FPURegister fd, FPURegister fs);
  void Ceil_l_d(FPURegister fd, FPURegister fs);

  void Trunc_w_d(FPURegister fd, FPURegister fs);
  void Round_w_d(FPURegister fd, FPURegister fs);
  void Floor_w_d(FPURegister fd, FPURegister fs);
  void Ceil_w_d(FPURegister fd, FPURegister fs);

  void Madd_s(FPURegister fd, FPURegister fr, FPURegister fs, FPURegister ft,
              FPURegister scratch);
  void Madd_d(FPURegister fd, FPURegister fr, FPURegister fs, FPURegister ft,
              FPURegister scratch);
  void Msub_s(FPURegister fd, FPURegister fr, FPURegister fs, FPURegister ft,
              FPURegister scratch);
  void Msub_d(FPURegister fd, FPURegister fr, FPURegister fs, FPURegister ft,
              FPURegister scratch);

  // Enter exit frame.
  // stack_space - extra stack space.
  void EnterExitFrame(Register scratch, int stack_space,
                      StackFrame::Type frame_type);

  // Leave the current exit frame.
  void LeaveExitFrame(Register scratch);

  // Make sure the stack is aligned. Only emits code in debug mode.
  void AssertStackIsAligned() NOOP_UNLESS_DEBUG_CODE;

  // Load the global proxy from the current context.
  void LoadGlobalProxy(Register dst) {
    LoadNativeContextSlot(dst, Context::GLOBAL_PROXY_INDEX);
  }

  void LoadNativeContextSlot(Register dst, int index);

  // Load the initial map from the global function. The registers
  // function and map can be the same, function is then overwritten.
  void LoadGlobalFunctionInitialMap(Register function, Register map,
                                    Register scratch);

  // -------------------------------------------------------------------------
  // JavaScript invokes.

  // Invoke the JavaScript function code by either calling or jumping.
  void InvokeFunctionCode(Register function, Register new_target,
                          Register expected_parameter_count,
                          Register actual_parameter_count, InvokeType type);

  // On function call, call into the debugger if necessary.
  void CheckDebugHook(Register fun, Register new_target,
                      Register expected_parameter_count,
                      Register actual_parameter_count);

  // Invoke the JavaScript function in the given register. Changes the
  // current context to the context in the function before invoking.
  void InvokeFunctionWithNewTarget(Register function, Register new_target,
                                   Register actual_parameter_count,
                                   InvokeType type);
  void InvokeFunction(Register function, Register expected_parameter_count,
                      Register actual_parameter_count, InvokeType type);

  // Exception handling.

  // Push a new stack handler and link into stack handler chain.
  void PushStackHandler();

  // Unlink the stack handler on top of the stack from the stack handler chain.
  // Must preserve the result register.
  void PopStackHandler();

  // -------------------------------------------------------------------------
  // Support functions.

  void GetObjectType(Register function, Register map, Register type_reg);

  void GetInstanceTypeRange(Register map, Register type_reg,
                            InstanceType lower_limit, Register range);

  // -------------------------------------------------------------------------
  // Runtime calls.

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

  // Jump to the builtin routine.
  void JumpToExternalReference(const ExternalReference& builtin,
                               bool builtin_exit_frame = false);

  // ---------------------------------------------------------------------------
  // In-place weak references.
  void LoadWeakValue(Register out, Register in, Label* target_if_cleared);

  // -------------------------------------------------------------------------
  // StatsCounter support.

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

  // -------------------------------------------------------------------------
  // Stack limit utilities

  enum StackLimitKind { kInterruptStackLimit, kRealStackLimit };
  void LoadStackLimit(Register destination, StackLimitKind kind);
  void StackOverflowCheck(Register num_args, Register scratch1,
                          Register scratch2, Label* stack_overflow);

  // ---------------------------------------------------------------------------
  // Smi utilities.

  // Test if the register contains a smi.
  inline void SmiTst(Register value, Register scratch) {
    And(scratch, value, Operand(kSmiTagMask));
  }

  // Jump if the register contains a non-smi.
  void JumpIfNotSmi(Register value, Label* not_smi_label,
                    BranchDelaySlot bd = PROTECT);

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

  // ---------------------------------------------------------------------------
  // Tiering support.
  void AssertFeedbackCell(Register object,
                          Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void AssertFeedbackVector(Register object,
                            Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void ReplaceClosureCodeWithOptimizedCode(Register optimized_code,
                                           Register closure, Register scratch1,
                                           Register scratch2);
  void GenerateTailCallToReturnedCode(Runtime::FunctionId function_id);
  void LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      Register flags, Register feedback_vector, CodeKind current_code_kind,
      Label* flags_need_processing);
  void OptimizeCodeOrTailCallOptimizedCodeSlot(Register flags,
                                               Register feedback_vector);

  template <typename Field>
  void DecodeField(Register dst, Register src) {
    Ext(dst, src, Field::kShift, Field::kSize);
  }

  template <typename Field>
  void DecodeField(Register reg) {
    DecodeField<Field>(reg, reg);
  }

 protected:
  inline Register GetRtAsRegisterHelper(const Operand& rt, Register scratch);
  inline int32_t GetOffset(int32_t offset, Label* L, OffsetSize bits);

 private:
  bool has_double_zero_reg_set_ = false;

  // Helper functions for generating invokes.
  void InvokePrologue(Register expected_parameter_count,
                      Register actual_parameter_count, Label* done,
                      InvokeType type);

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32. Goes to 'done' if it
  // succeeds, otherwise falls through if result is saturated. On return
  // 'result' either holds answer, or is clobbered on fall through.
  void TryInlineTruncateDoubleToI(Register result, DoubleRegister input,
                                  Label* done);

  void CompareF(SecondaryField sizeField, FPUCondition cc, FPURegister cmp1,
                FPURegister cmp2);

  void CompareIsNanF(SecondaryField sizeField, FPURegister cmp1,
                     FPURegister cmp2);

  void BranchShortMSA(MSABranchDF df, Label* target, MSABranchCondition cond,
                      MSARegister wt, BranchDelaySlot bd = PROTECT);

  int CallCFunctionHelper(
      Register function, int num_reg_arguments, int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);

  // TODO(mips) Reorder parameters so out parameters come last.
  bool CalculateOffset(Label* L, int32_t* offset, OffsetSize bits);
  bool CalculateOffset(Label* L, int32_t* offset, OffsetSize bits,
                       Register* scratch, const Operand& rt);

  void BranchShortHelperR6(int32_t offset, Label* L);
  void BranchShortHelper(int16_t offset, Label* L, BranchDelaySlot bdslot);
  bool BranchShortHelperR6(int32_t offset, Label* L, Condition cond,
                           Register rs, const Operand& rt);
  bool BranchShortHelper(int16_t offset, Label* L, Condition cond, Register rs,
                         const Operand& rt, BranchDelaySlot bdslot);
  bool BranchShortCheck(int32_t offset, Label* L, Condition cond, Register rs,
                        const Operand& rt, BranchDelaySlot bdslot);

  void BranchAndLinkShortHelperR6(int32_t offset, Label* L);
  void BranchAndLinkShortHelper(int16_t offset, Label* L,
                                BranchDelaySlot bdslot);
  void BranchAndLinkShort(int32_t offset, BranchDelaySlot bdslot = PROTECT);
  void BranchAndLinkShort(Label* L, BranchDelaySlot bdslot = PROTECT);
  bool BranchAndLinkShortHelperR6(int32_t offset, Label* L, Condition cond,
                                  Register rs, const Operand& rt);
  bool BranchAndLinkShortHelper(int16_t offset, Label* L, Condition cond,
                                Register rs, const Operand& rt,
                                BranchDelaySlot bdslot);
  bool BranchAndLinkShortCheck(int32_t offset, Label* L, Condition cond,
                               Register rs, const Operand& rt,
                               BranchDelaySlot bdslot);
  void BranchLong(Label* L, BranchDelaySlot bdslot);
  void BranchAndLinkLong(Label* L, BranchDelaySlot bdslot);

  template <typename RoundFunc>
  void RoundDouble(FPURegister dst, FPURegister src, FPURoundingMode mode,
                   RoundFunc round);

  template <typename RoundFunc>
  void RoundFloat(FPURegister dst, FPURegister src, FPURoundingMode mode,
                  RoundFunc round);

  // Push a fixed frame, consisting of ra, fp.
  void PushCommonFrame(Register marker_reg = no_reg);

  DISALLOW_IMPLICIT_CONSTRUCTORS(MacroAssembler);
};

template <typename Func>
void MacroAssembler::GenerateSwitchTable(Register index, size_t case_count,
                                         Func GetLabelFunction) {
  // Ensure that dd-ed labels following this instruction use 8 bytes aligned
  // addresses.
  BlockTrampolinePoolFor(static_cast<int>(case_count) * 2 +
                         kSwitchTablePrologueSize);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  if (kArchVariant >= kMips64r6) {
    // Opposite of Align(8) as we have odd number of instructions in this case.
    if ((pc_offset() & 7) == 0) {
      nop();
    }
    addiupc(scratch, 5);
    Dlsa(scratch, scratch, index, kPointerSizeLog2);
    Ld(scratch, MemOperand(scratch));
  } else {
    Label here;
    Align(8);
    push(ra);
    bal(&here);
    dsll(scratch, index, kPointerSizeLog2);  // Branch delay slot.
    bind(&here);
    daddu(scratch, scratch, ra);
    pop(ra);
    Ld(scratch, MemOperand(scratch, 6 * v8::internal::kInstrSize));
  }
  jr(scratch);
  nop();  // Branch delay slot nop.
  for (size_t index = 0; index < case_count; ++index) {
    dd(GetLabelFunction(index));
  }
}

struct MoveCycleState {
  // List of scratch registers reserved for pending moves in a move cycle, and
  // which should therefore not be used as a temporary location by
  // {MoveToTempLocation}.
  RegList scratch_regs;
  // Available scratch registers during the move cycle resolution scope.
  std::optional<UseScratchRegisterScope> temps;
  // Scratch register picked by {MoveToTempLocation}.
  std::optional<Register> scratch_reg;
};

// Provides access to exit frame parameters (GC-ed).
inline MemOperand ExitFrameStackSlotOperand(int offset) {
  // The slot at [sp] is reserved in all ExitFrames for storing the return
  // address before doing the actual call, it's necessary for frame iteration
  // (see StoreReturnAddressAndCall for details).
  static constexpr int kSPOffset = 1 * kSystemPointerSize;
  return MemOperand(sp, kSPOffset + offset);
}

// Provides access to exit frame parameters (GC-ed).
inline MemOperand ExitFrameCallerStackSlotOperand(int index) {
  return MemOperand(fp, (ExitFrameConstants::kFixedSlotCountAboveFp + index) *
                            kSystemPointerSize);
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
                              MemOperand return_value_operand);

}  // namespace internal
}  // namespace v8

#define ACCESS_MASM(masm) masm->

#endif  // V8_CODEGEN_MIPS64_MACRO_ASSEMBLER_MIPS64_H_
