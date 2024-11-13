// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDED_FROM_MACRO_ASSEMBLER_H
#error This header must be included via macro-assembler.h
#endif

#ifndef V8_CODEGEN_X64_MACRO_ASSEMBLER_X64_H_
#define V8_CODEGEN_X64_MACRO_ASSEMBLER_X64_H_

#include "src/base/flags.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/shared-ia32-x64/macro-assembler-shared-ia32-x64.h"
#include "src/codegen/x64/assembler-x64.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/execution/isolate-data.h"
#include "src/objects/contexts.h"
#include "src/objects/tagged-index.h"

namespace v8 {
namespace internal {

// Convenience for platform-independent signatures.
using MemOperand = Operand;

struct SmiIndex {
  SmiIndex(Register index_register, ScaleFactor scale)
      : reg(index_register), scale(scale) {}
  Register reg;
  ScaleFactor scale;
};

// TODO(victorgomes): Move definition to macro-assembler.h, once all other
// platforms are updated.
enum class StackLimitKind { kInterruptStackLimit, kRealStackLimit };

// Convenient class to access arguments below the stack pointer.
class StackArgumentsAccessor {
 public:
  // argc = the number of arguments not including the receiver.
  explicit StackArgumentsAccessor(Register argc) : argc_(argc) {
    DCHECK_NE(argc_, no_reg);
  }

  // Argument 0 is the receiver (despite argc not including the receiver).
  Operand operator[](int index) const { return GetArgumentOperand(index); }

  Operand GetArgumentOperand(int index) const;
  Operand GetReceiverOperand() const { return GetArgumentOperand(0); }

 private:
  const Register argc_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(StackArgumentsAccessor);
};

class V8_EXPORT_PRIVATE MacroAssembler
    : public SharedMacroAssembler<MacroAssembler> {
 public:
  using SharedMacroAssembler<MacroAssembler>::SharedMacroAssembler;

  void PushReturnAddressFrom(Register src) { pushq(src); }
  void PopReturnAddressTo(Register dst) { popq(dst); }

  void Ret();

  // Call incsspq with {number_of_words} only if the cpu supports it.
  // NOTE: This shouldn't be embedded in optimized code, since the check
  // for CPU support would be redundant (we could check at compiler time).
  void IncsspqIfSupported(Register number_of_words, Register scratch);

  // Return and drop arguments from stack, where the number of arguments
  // may be bigger than 2^16 - 1.  Requires a scratch register.
  void Ret(int bytes_dropped, Register scratch);

  // Operations on roots in the root-array.
  Operand RootAsOperand(RootIndex index);
  void LoadTaggedRoot(Register destination, RootIndex index);
  void LoadRoot(Register destination, RootIndex index) final;
  void LoadRoot(Operand destination, RootIndex index) {
    LoadRoot(kScratchRegister, index);
    movq(destination, kScratchRegister);
  }

  void Push(Register src);
  void Push(Operand src);
  void Push(Immediate value);
  void Push(Tagged<Smi> smi);
  void Push(Tagged<TaggedIndex> index) {
    Push(Immediate(static_cast<uint32_t>(index.ptr())));
  }
  void Push(Handle<HeapObject> source);

  enum class PushArrayOrder { kNormal, kReverse };
  // `array` points to the first element (the lowest address).
  // `array` and `size` are not modified.
  void PushArray(Register array, Register size, Register scratch,
                 PushArrayOrder order = PushArrayOrder::kNormal);

  // Before calling a C-function from generated code, align arguments on stack.
  // After aligning the frame, arguments must be stored in rsp[0], rsp[8],
  // etc., not pushed. The argument count assumes all arguments are word sized.
  // The number of slots reserved for arguments depends on platform. On Windows
  // stack slots are reserved for the arguments passed in registers. On other
  // platforms stack slots are only reserved for the arguments actually passed
  // on the stack.
  void PrepareCallCFunction(int num_arguments);

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

  // Calculate the number of stack slots to reserve for arguments when calling a
  // C function.
  static int ArgumentStackSlotsForCFunctionCall(int num_arguments);

  void MemoryChunkHeaderFromObject(Register object, Register header);
  void CheckPageFlag(Register object, Register scratch, int mask, Condition cc,
                     Label* condition_met,
                     Label::Distance condition_met_distance = Label::kFar);
  void CheckMarkBit(Register object, Register scratch0, Register scratch1,
                    Condition cc, Label* condition_met,
                    Label::Distance condition_met_distance = Label::kFar);
  void JumpIfMarking(Label* is_marking,
                     Label::Distance condition_met_distance = Label::kFar);
  void JumpIfNotMarking(Label* not_marking,
                        Label::Distance condition_met_distance = Label::kFar);

  // Define movq here instead of using AVX_OP. movq is defined using templates
  // and there is a function template `void movq(P1)`, while technically
  // impossible, will be selected when deducing the arguments for AvxHelper.
  void Movq(XMMRegister dst, Register src);
  void Movq(Register dst, XMMRegister src);

  void Cvtss2sd(XMMRegister dst, XMMRegister src);
  void Cvtss2sd(XMMRegister dst, Operand src);
  void Cvtsd2ss(XMMRegister dst, XMMRegister src);
  void Cvtsd2ss(XMMRegister dst, Operand src);
  void Cvttsd2si(Register dst, XMMRegister src);
  void Cvttsd2si(Register dst, Operand src);
  void Cvttsd2siq(Register dst, XMMRegister src);
  void Cvttsd2siq(Register dst, Operand src);
  void Cvttss2si(Register dst, XMMRegister src);
  void Cvttss2si(Register dst, Operand src);
  void Cvttss2siq(Register dst, XMMRegister src);
  void Cvttss2siq(Register dst, Operand src);
  void Cvtlui2ss(XMMRegister dst, Register src);
  void Cvtlui2ss(XMMRegister dst, Operand src);
  void Cvtlui2sd(XMMRegister dst, Register src);
  void Cvtlui2sd(XMMRegister dst, Operand src);
  void Cvtqui2ss(XMMRegister dst, Register src);
  void Cvtqui2ss(XMMRegister dst, Operand src);
  void Cvtqui2sd(XMMRegister dst, Register src);
  void Cvtqui2sd(XMMRegister dst, Operand src);
  void Cvttsd2uiq(Register dst, Operand src, Label* fail = nullptr);
  void Cvttsd2uiq(Register dst, XMMRegister src, Label* fail = nullptr);
  void Cvttsd2ui(Register dst, Operand src, Label* fail = nullptr);
  void Cvttsd2ui(Register dst, XMMRegister src, Label* fail = nullptr);
  void Cvttss2uiq(Register dst, Operand src, Label* fail = nullptr);
  void Cvttss2uiq(Register dst, XMMRegister src, Label* fail = nullptr);
  void Cvttss2ui(Register dst, Operand src, Label* fail = nullptr);
  void Cvttss2ui(Register dst, XMMRegister src, Label* fail = nullptr);

  // cvtsi2sd and cvtsi2ss instructions only write to the low 64/32-bit of dst
  // register, which hinders register renaming and makes dependence chains
  // longer. So we use xorpd to clear the dst register before cvtsi2sd for
  // non-AVX and a scratch XMM register as first src for AVX to solve this
  // issue.
  void Cvtqsi2ss(XMMRegister dst, Register src);
  void Cvtqsi2ss(XMMRegister dst, Operand src);
  void Cvtqsi2sd(XMMRegister dst, Register src);
  void Cvtqsi2sd(XMMRegister dst, Operand src);
  void Cvtlsi2ss(XMMRegister dst, Register src);
  void Cvtlsi2ss(XMMRegister dst, Operand src);
  void Cvtlsi2sd(XMMRegister dst, Register src);
  void Cvtlsi2sd(XMMRegister dst, Operand src);

  void Cmpeqss(XMMRegister dst, XMMRegister src);
  void Cmpeqsd(XMMRegister dst, XMMRegister src);

  void PextrdPreSse41(Register dst, XMMRegister src, uint8_t imm8);
  void Pextrq(Register dst, XMMRegister src, int8_t imm8);

  void PinsrdPreSse41(XMMRegister dst, Register src2, uint8_t imm8,
                      uint32_t* load_pc_offset = nullptr);
  void PinsrdPreSse41(XMMRegister dst, Operand src2, uint8_t imm8,
                      uint32_t* load_pc_offset = nullptr);

  void Pinsrq(XMMRegister dst, XMMRegister src1, Register src2, uint8_t imm8,
              uint32_t* load_pc_offset = nullptr);
  void Pinsrq(XMMRegister dst, XMMRegister src1, Operand src2, uint8_t imm8,
              uint32_t* load_pc_offset = nullptr);

  void Lzcntq(Register dst, Register src);
  void Lzcntq(Register dst, Operand src);
  void Lzcntl(Register dst, Register src);
  void Lzcntl(Register dst, Operand src);
  void Tzcntq(Register dst, Register src);
  void Tzcntq(Register dst, Operand src);
  void Tzcntl(Register dst, Register src);
  void Tzcntl(Register dst, Operand src);
  void Popcntl(Register dst, Register src);
  void Popcntl(Register dst, Operand src);
  void Popcntq(Register dst, Register src);
  void Popcntq(Register dst, Operand src);

  void Cmp(Register dst, Tagged<Smi> src);
  void Cmp(Operand dst, Tagged<Smi> src);
  void Cmp(Register dst, int32_t src);

  void CmpTagged(const Register& src1, const Register& src2) {
    cmp_tagged(src1, src2);
  }

  // SIMD256
  void I64x4Mul(YMMRegister dst, YMMRegister lhs, YMMRegister rhs,
                YMMRegister tmp1, YMMRegister tmp2);
  void F64x4Min(YMMRegister dst, YMMRegister lhs, YMMRegister rhs,
                YMMRegister scratch);
  void F64x4Max(YMMRegister dst, YMMRegister lhs, YMMRegister rhs,
                YMMRegister scratch);
  void F32x8Min(YMMRegister dst, YMMRegister lhs, YMMRegister rhs,
                YMMRegister scratch);
  void F32x8Max(YMMRegister dst, YMMRegister lhs, YMMRegister rhs,
                YMMRegister scratch);
  void F16x8Min(YMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                YMMRegister scratch, YMMRegister scratch2);
  void F16x8Max(YMMRegister dst, XMMRegister lhs, XMMRegister rhs,
                YMMRegister scratch, YMMRegister scratch2);
  void I64x4ExtMul(YMMRegister dst, XMMRegister src1, XMMRegister src2,
                   YMMRegister scratch, bool is_signed);
  void I32x8ExtMul(YMMRegister dst, XMMRegister src1, XMMRegister src2,
                   YMMRegister scratch, bool is_signed);
  void I16x16ExtMul(YMMRegister dst, XMMRegister src1, XMMRegister src2,
                    YMMRegister scratch, bool is_signed);
#define MACRO_ASM_X64_IEXTADDPAIRWISE_LIST(V) \
  V(I32x8ExtAddPairwiseI16x16S)               \
  V(I32x8ExtAddPairwiseI16x16U)               \
  V(I16x16ExtAddPairwiseI8x32S)               \
  V(I16x16ExtAddPairwiseI8x32U)

#define DECLARE_IEXTADDPAIRWISE(ExtAddPairwiseOp) \
  void ExtAddPairwiseOp(YMMRegister dst, YMMRegister src, YMMRegister scratch);
  MACRO_ASM_X64_IEXTADDPAIRWISE_LIST(DECLARE_IEXTADDPAIRWISE)
#undef DECLARE_IEXTADDPAIRWISE
#undef MACRO_ASM_X64_IEXTADDPAIRWISE_LIST

  void I32x8SConvertF32x8(YMMRegister dst, YMMRegister src, YMMRegister tmp,
                          Register scratch);
  void I16x8SConvertF16x8(YMMRegister dst, XMMRegister src, YMMRegister tmp,
                          Register scratch);
  void I16x8TruncF16x8U(YMMRegister dst, XMMRegister src, YMMRegister tmp);
  void F16x8Qfma(YMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister src3, YMMRegister tmp, YMMRegister tmp2);
  void F16x8Qfms(YMMRegister dst, XMMRegister src1, XMMRegister src2,
                 XMMRegister src3, YMMRegister tmp, YMMRegister tmp2);

  void S256Not(YMMRegister dst, YMMRegister src, YMMRegister scratch);
  void S256Select(YMMRegister dst, YMMRegister mask, YMMRegister src1,
                  YMMRegister src2, YMMRegister scratch);

// Splat
#define MACRO_ASM_X64_ISPLAT_LIST(V) \
  V(I8x32Splat, b, vmovd)            \
  V(I16x16Splat, w, vmovd)           \
  V(I32x8Splat, d, vmovd)            \
  V(I64x4Splat, q, vmovq)

#define DECLARE_ISPLAT(name, suffix, instr_mov) \
  void name(YMMRegister dst, Register src);     \
  void name(YMMRegister dst, Operand src);

  MACRO_ASM_X64_ISPLAT_LIST(DECLARE_ISPLAT)

#undef DECLARE_ISPLAT

  void F64x4Splat(YMMRegister dst, XMMRegister src);
  void F32x8Splat(YMMRegister dst, XMMRegister src);

  void F32x8Qfma(YMMRegister dst, YMMRegister src1, YMMRegister src2,
                 YMMRegister src3, YMMRegister tmp);
  void F32x8Qfms(YMMRegister dst, YMMRegister src1, YMMRegister src2,
                 YMMRegister src3, YMMRegister tmp);
  void F64x4Qfma(YMMRegister dst, YMMRegister src1, YMMRegister src2,
                 YMMRegister src3, YMMRegister tmp);
  void F64x4Qfms(YMMRegister dst, YMMRegister src1, YMMRegister src2,
                 YMMRegister src3, YMMRegister tmp);

  void I32x8DotI8x32I7x32AddS(YMMRegister dst, YMMRegister src1,
                              YMMRegister src2, YMMRegister src3,
                              YMMRegister scratch, YMMRegister splat_reg);

  // ---------------------------------------------------------------------------
  // Conversions between tagged smi values and non-tagged integer values.

  // Tag an word-size value. The result must be known to be a valid smi value.
  void SmiTag(Register reg);
  // Requires dst != src
  void SmiTag(Register dst, Register src);

  // Simple comparison of smis.  Both sides must be known smis to use these,
  // otherwise use Cmp.
  void SmiCompare(Register smi1, Register smi2);
  void SmiCompare(Register dst, Tagged<Smi> src);
  void SmiCompare(Register dst, Operand src);
  void SmiCompare(Operand dst, Register src);
  void SmiCompare(Operand dst, Tagged<Smi> src);

  // Functions performing a check on a known or potential smi. Returns
  // a condition that is satisfied if the check is successful.
  Condition CheckSmi(Register src);
  Condition CheckSmi(Operand src);

  // This can be used in testing to ensure we never rely on what is in the
  // unused smi bits.
  void ClobberDecompressedSmiBits(Register smi);

  // Abort execution if argument is a smi, enabled via --debug-code.
  void AssertNotSmi(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a smi, enabled via --debug-code.
  void AssertSmi(Register object) NOOP_UNLESS_DEBUG_CODE;
  void AssertSmi(Operand object) NOOP_UNLESS_DEBUG_CODE;

  // Test-and-jump functions. Typically combines a check function
  // above with a conditional jump.

  // Jump to label if the value is a tagged smi.
  void JumpIfSmi(Register src, Label* on_smi,
                 Label::Distance near_jump = Label::kFar);

  // Jump to label if the value is not a tagged smi.
  void JumpIfNotSmi(Register src, Label* on_not_smi,
                    Label::Distance near_jump = Label::kFar);

  // Jump to label if the value is not a tagged smi.
  void JumpIfNotSmi(Operand src, Label* on_not_smi,
                    Label::Distance near_jump = Label::kFar);

  // Operations on tagged smi values.

  // Smis represent a subset of integers. The subset is always equivalent to
  // a two's complement interpretation of a fixed number of bits.

  // Add an integer constant to a tagged smi, giving a tagged smi as result.
  // No overflow testing on the result is done.
  void SmiAddConstant(Operand dst, Tagged<Smi> constant);

  // Specialized operations

  // Converts, if necessary, a smi to a combination of number and
  // multiplier to be used as a scaled index.
  // The src register contains a *positive* smi value. The shift is the
  // power of two to multiply the index value by (e.g. to index by
  // smi-value * kSystemPointerSize, pass the smi and kSystemPointerSizeLog2).
  // The returned index register may be either src or dst, depending
  // on what is most efficient. If src and dst are different registers,
  // src is always unchanged.
  SmiIndex SmiToIndex(Register dst, Register src, int shift);

  void JumpIfEqual(Register a, int32_t b, Label* dest) {
    cmpl(a, Immediate(b));
    j(equal, dest);
  }

  void JumpIfLessThan(Register a, int32_t b, Label* dest) {
    cmpl(a, Immediate(b));
    j(less, dest);
  }

  // Caution: if {reg} is a 32-bit negative int, it should be sign-extended to
  // 64-bit before calling this function.
  void Switch(Register scrach, Register reg, int case_base_value,
              Label** labels, int num_labels);

#ifdef V8_MAP_PACKING
  void UnpackMapWord(Register r);
#endif

  void LoadMap(Register destination, Register object);
  void LoadCompressedMap(Register destination, Register object);

  void LoadFeedbackVector(Register dst, Register closure, Label* fbv_undef,
                          Label::Distance distance);

  void Move(Register dst, intptr_t x) {
    if (x == 0) {
      xorl(dst, dst);
      // The following shorter sequence for uint8 causes performance
      // regressions:
      // xorl(dst, dst); movb(dst,
      // Immediate(static_cast<uint32_t>(x)));
    } else if (is_uint32(x)) {
      movl(dst, Immediate(static_cast<uint32_t>(x)));
    } else if (is_int32(x)) {
      // "movq reg64, imm32" is sign extending.
      movq(dst, Immediate(static_cast<int32_t>(x)));
    } else {
      movq(dst, Immediate64(x));
    }
  }
  void Move(Operand dst, intptr_t x);
  void Move(Register dst, Tagged<Smi> source);

  void Move(Operand dst, Tagged<Smi> source) {
    Register constant = GetSmiConstant(source);
    movq(dst, constant);
  }

  void Move(Register dst, Tagged<TaggedIndex> source) {
    Move(dst, source.ptr());
  }

  void Move(Operand dst, Tagged<TaggedIndex> source) {
    Move(dst, source.ptr());
  }

  void Move(Register dst, ExternalReference ext);

  void Move(XMMRegister dst, uint32_t src);
  void Move(XMMRegister dst, uint64_t src);
  void Move(XMMRegister dst, float src) {
    Move(dst, base::bit_cast<uint32_t>(src));
  }
  void Move(XMMRegister dst, double src) {
    Move(dst, base::bit_cast<uint64_t>(src));
  }
  void Move(XMMRegister dst, uint64_t high, uint64_t low);

  // Move if the registers are not identical.
  void Move(Register target, Register source);
  void Move(XMMRegister target, XMMRegister source);

  void Move(Register target, Operand source);
  void Move(Register target, Immediate source);

  void Move(Register dst, Handle<HeapObject> source,
            RelocInfo::Mode rmode = RelocInfo::FULL_EMBEDDED_OBJECT);
  void Move(Operand dst, Handle<HeapObject> source,
            RelocInfo::Mode rmode = RelocInfo::FULL_EMBEDDED_OBJECT);

  // Loads a pointer into a register with a relocation mode.
  void Move(Register dst, Address ptr, RelocInfo::Mode rmode) {
    // This method must not be used with heap object references. The stored
    // address is not GC safe. Use the handle version instead.
    DCHECK(rmode == RelocInfo::NO_INFO || rmode > RelocInfo::LAST_GCED_ENUM);
    movq(dst, Immediate64(ptr, rmode));
  }

  // Move src0 to dst0 and src1 to dst1, handling possible overlaps.
  void MovePair(Register dst0, Register src0, Register dst1, Register src1);

  // Convert smi to word-size sign-extended value.
  void SmiUntag(Register reg);
  void SmiUntagUnsigned(Register reg);
  // Requires dst != src
  void SmiUntag(Register dst, Register src);
  void SmiUntag(Register dst, Operand src);
  void SmiUntagUnsigned(Register dst, Operand src);

  // Convert smi to 32-bit value.
  void SmiToInt32(Register reg);
  void SmiToInt32(Register dst, Register src);

  // Loads the address of the external reference into the destination
  // register.
  void LoadAddress(Register destination, ExternalReference source);

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
  Operand ExternalReferenceAsOperand(ExternalReference reference,
                                     Register scratch = kScratchRegister);

  Operand ExternalReferenceAsOperand(IsolateFieldId id) {
    return ExternalReferenceAsOperand(ExternalReference::Create(id), no_reg);
  }

  void Call(Register reg) { call(reg); }
  void Call(Operand op);
  void Call(Handle<Code> code_object, RelocInfo::Mode rmode);
  void Call(Address destination, RelocInfo::Mode rmode);
  void Call(ExternalReference ext);
  void Call(Label* target) { call(target); }

  Operand EntryFromBuiltinAsOperand(Builtin builtin_index);
  Operand EntryFromBuiltinIndexAsOperand(Register builtin_index);
  void CallBuiltinByIndex(Register builtin_index);
  void CallBuiltin(Builtin builtin);
  void TailCallBuiltin(Builtin builtin);
  void TailCallBuiltin(Builtin builtin, Condition cc);

  // Load the code entry point from the Code object.
  void LoadCodeInstructionStart(Register destination, Register code_object,
                                CodeEntrypointTag tag);
  void CallCodeObject(Register code_object, CodeEntrypointTag tag);
  void JumpCodeObject(Register code_object, CodeEntrypointTag tag,
                      JumpMode jump_mode = JumpMode::kJump);

  // Convenience functions to call/jmp to the code of a JSFunction object.
  void CallJSFunction(Register function_object);
  void JumpJSFunction(Register function_object,
                      JumpMode jump_mode = JumpMode::kJump);

  void Jump(Address destination, RelocInfo::Mode rmode);
  void Jump(Address destination, RelocInfo::Mode rmode, Condition cc);
  void Jump(const ExternalReference& reference);
  void Jump(Operand op);
  void Jump(Operand op, Condition cc);
  void Jump(Handle<Code> code_object, RelocInfo::Mode rmode);
  void Jump(Handle<Code> code_object, RelocInfo::Mode rmode, Condition cc);

  void BailoutIfDeoptimized(Register scratch);
  void CallForDeoptimization(Builtin target, int deopt_id, Label* exit,
                             DeoptimizeKind kind, Label* ret,
                             Label* jump_deoptimization_entry_label);

  void Trap();
  void DebugBreak();

  void CompareRoot(Register with, RootIndex index,
                   ComparisonMode mode = ComparisonMode::kDefault);
  void CompareTaggedRoot(Register with, RootIndex index);
  void CompareRoot(Operand with, RootIndex index);

  // Generates function and stub prologue code.
  void StubPrologue(StackFrame::Type type);
  void Prologue();

  // Helpers for argument handling
  void DropArguments(Register count, Register scratch);
  void DropArgumentsAndPushNewReceiver(Register argc, Register receiver,
                                       Register scratch);
  void DropArgumentsAndPushNewReceiver(Register argc, Operand receiver,
                                       Register scratch);

  // Calls Abort(msg) if the condition cc is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cc, AbortReason reason) NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but without condition.
  // Use --debug_code to enable.
  void AssertUnreachable(AbortReason reason) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if a 64 bit register containing a 32 bit payload does not
  // have zeros in the top 32 bits, enabled via --debug-code.
  void AssertZeroExtended(Register reg) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if the signed bit of smi register with pointer compression
  // is not zero, enabled via --debug-code.
  void AssertSignedBitOfSmiIsZero(Register smi) NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but always enabled.
  void Check(Condition cc, AbortReason reason);

  // Compare instance type for map.
  // Always use unsigned comparisons: above and below, not less and greater.
  void CmpInstanceType(Register map, InstanceType type);

  // Abort execution if argument is not a Map, enabled via
  // --debug-code.
  void AssertMap(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not a Code, enabled via
  // --debug-code.
  void AssertCode(Register object) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not smi nor in the main pointer compresssion
  // cage, enabled via --debug-code.
  void AssertSmiOrHeapObjectInMainCompressionCage(Register object)
      NOOP_UNLESS_DEBUG_CODE;

  // Print a message to stdout and abort execution.
  void Abort(AbortReason msg);

  void CheckStackAlignment();

  void AlignStackPointer();

  // Activation support.
  void EnterFrame(StackFrame::Type type);
  void EnterFrame(StackFrame::Type type, bool load_constant_pool_pointer_reg) {
    // Out-of-line constant pool not implemented on x64.
    UNREACHABLE();
  }
  void LeaveFrame(StackFrame::Type type);

// Allocate stack space of given size (i.e. decrement {rsp} by the value
// stored in the given register, or by a constant). If you need to perform a
// stack check, do it before calling this function because this function may
// write into the newly allocated space. It may also overwrite the given
// register's value, in the version that takes a register.
#if defined(V8_TARGET_OS_WIN) || defined(V8_TARGET_OS_MACOS)
  void AllocateStackSpace(Register bytes_scratch);
  void AllocateStackSpace(int bytes);
#else
  void AllocateStackSpace(Register bytes) { subq(rsp, bytes); }
  void AllocateStackSpace(int bytes) {
    DCHECK_GE(bytes, 0);
    if (bytes == 0) return;
    subq(rsp, Immediate(bytes));
  }
#endif

  void InitializeRootRegister() {
    ExternalReference isolate_root = ExternalReference::isolate_root(isolate());
    Move(kRootRegister, isolate_root);
#ifdef V8_COMPRESS_POINTERS
    LoadRootRelative(kPtrComprCageBaseRegister,
                     IsolateData::cage_base_offset());
#endif
  }

  void CallEphemeronKeyBarrier(Register object, Register slot_address,
                               SaveFPRegsMode fp_mode);

  void CallIndirectPointerBarrier(Register object, Register slot_address,
                                  SaveFPRegsMode fp_mode,
                                  IndirectPointerTag tag);

  void CallRecordWriteStubSaveRegisters(
      Register object, Register slot_address, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);
  void CallRecordWriteStub(
      Register object, Register slot_address, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);

#ifdef V8_IS_TSAN
  void CallTSANStoreStub(Register address, Register value,
                         SaveFPRegsMode fp_mode, int size, StubCallMode mode,
                         std::memory_order order);
  void CallTSANRelaxedLoadStub(Register address, SaveFPRegsMode fp_mode,
                               int size, StubCallMode mode);
#endif  // V8_IS_TSAN

  void MoveNumber(Register dst, double value);
  void MoveNonSmi(Register dst, double value);

  // Calculate how much stack space (in bytes) are required to store caller
  // registers excluding those specified in the arguments.
  int RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                      Register exclusion = no_reg) const;

  // PushCallerSaved and PopCallerSaved do not arrange the registers in any
  // particular order so they are not useful for calls that can cause a GC.
  // The caller can exclude a register that does not need to be saved and
  // restored.

  // Push caller saved registers on the stack, and return the number of bytes
  // stack pointer is adjusted.
  int PushCallerSaved(SaveFPRegsMode fp_mode, Register exclusion = no_reg);
  // Restore caller saved registers from the stack, and return the number of
  // bytes stack pointer is adjusted.
  int PopCallerSaved(SaveFPRegsMode fp_mode, Register exclusion = no_reg);

  int PushAll(RegList registers);
  int PopAll(RegList registers);

  int PushAll(DoubleRegList registers,
              int stack_slot_size = kStackSavedSavedFPSize);
  int PopAll(DoubleRegList registers,
             int stack_slot_size = kStackSavedSavedFPSize);

  // Compute the start of the generated instruction stream from the current PC.
  // This is an alternative to embedding the {CodeObject} handle as a reference.
  void ComputeCodeStartAddress(Register dst);

  // Control-flow integrity:

  // Define a function entrypoint which will emit a landing pad instruction if
  // required by the build config.
  void CodeEntry();
  // Define an exception handler.
  void ExceptionHandler() { CodeEntry(); }
  // Define an exception handler and bind a label.
  void BindExceptionHandler(Label* label) { BindJumpTarget(label); }
  // Bind a jump target and mark it as a valid code entry.
  void BindJumpTarget(Label* label) {
    bind(label);
    CodeEntry();
  }

  // ---------------------------------------------------------------------------
  // Pointer compression support

  // Loads a field containing any tagged value and decompresses it if necessary.
  void LoadTaggedField(Register destination, Operand field_operand);

  // Loads a field containing any tagged value but does not decompress it when
  // pointer compression is enabled.
  void LoadTaggedField(TaggedRegister destination, Operand field_operand);
  void LoadTaggedFieldWithoutDecompressing(Register destination,
                                           Operand field_operand);

  // Loads a field containing a Smi and decompresses it if pointer compression
  // is enabled.
  void LoadTaggedSignedField(Register destination, Operand field_operand);

  // Loads a field containing any tagged value, decompresses it if necessary and
  // pushes the full pointer to the stack. When pointer compression is enabled,
  // uses |scratch| to decompress the value.
  void PushTaggedField(Operand field_operand, Register scratch);

  // Loads a field containing smi value and untags it.
  void SmiUntagField(Register dst, Operand src);
  void SmiUntagFieldUnsigned(Register dst, Operand src);

  // Compresses tagged value if necessary and stores it to given on-heap
  // location.
  void StoreTaggedField(Operand dst_field_operand, Immediate immediate);
  void StoreTaggedField(Operand dst_field_operand, Register value);
  void StoreTaggedSignedField(Operand dst_field_operand, Tagged<Smi> value);
  void AtomicStoreTaggedField(Operand dst_field_operand, Register value);

  // The following macros work even when pointer compression is not enabled.
  void DecompressTaggedSigned(Register destination, Operand field_operand);
  void DecompressTagged(Register destination, Operand field_operand);
  void DecompressTagged(Register destination, Register source);
  void DecompressTagged(Register destination, Tagged_t immediate);
  void DecompressProtected(Register destination, Operand field_operand);

  // ---------------------------------------------------------------------------
  // V8 Sandbox support

  // Transform a SandboxedPointer from/to its encoded form, which is used when
  // the pointer is stored on the heap and ensures that the pointer will always
  // point into the sandbox.
  void EncodeSandboxedPointer(Register value);
  void DecodeSandboxedPointer(Register value);

  // Load and decode a SandboxedPointer from the heap.
  void LoadSandboxedPointerField(Register destination, Operand field_operand);
  // Encode and store a SandboxedPointer to the heap.
  void StoreSandboxedPointerField(Operand dst_field_operand, Register value);

  enum class IsolateRootLocation { kInScratchRegister, kInRootRegister };
  // Loads a field containing off-heap pointer and does necessary decoding
  // if sandboxed external pointers are enabled.
  void LoadExternalPointerField(Register destination, Operand field_operand,
                                ExternalPointerTag tag, Register scratch,
                                IsolateRootLocation isolateRootLocation =
                                    IsolateRootLocation::kInRootRegister);

  // Load a trusted pointer field.
  // When the sandbox is enabled, these are indirect pointers using the trusted
  // pointer table. Otherwise they are regular tagged fields.
  void LoadTrustedPointerField(Register destination, Operand field_operand,
                               IndirectPointerTag tag, Register scratch);
  // Store a trusted pointer field.
  void StoreTrustedPointerField(Operand dst_field_operand, Register value);

  // Load a code pointer field.
  // These are special versions of trusted pointers that, when the sandbox is
  // enabled, reference code objects through the code pointer table.
  void LoadCodePointerField(Register destination, Operand field_operand,
                            Register scratch) {
    LoadTrustedPointerField(destination, field_operand, kCodeIndirectPointerTag,
                            scratch);
  }
  // Store a code pointer field.
  void StoreCodePointerField(Operand dst_field_operand, Register value) {
    StoreTrustedPointerField(dst_field_operand, value);
  }

  // Load an indirect pointer field.
  // Only available when the sandbox is enabled, but always visible to avoid
  // having to place the #ifdefs into the caller.
  void LoadIndirectPointerField(Register destination, Operand field_operand,
                                IndirectPointerTag tag, Register scratch);

  // Store an indirect pointer field.
  // Only available when the sandbox is enabled, but always visible to avoid
  // having to place the #ifdefs into the caller.
  void StoreIndirectPointerField(Operand dst_field_operand, Register value);

#ifdef V8_ENABLE_SANDBOX
  // Retrieve the heap object referenced by the given indirect pointer handle,
  // which can either be a trusted pointer handle or a code pointer handle.
  void ResolveIndirectPointerHandle(Register destination, Register handle,
                                    IndirectPointerTag tag);

  // Retrieve the heap object referenced by the given trusted pointer handle.
  void ResolveTrustedPointerHandle(Register destination, Register handle,
                                   IndirectPointerTag tag);

  // Retrieve the Code object referenced by the given code pointer handle.
  void ResolveCodePointerHandle(Register destination, Register handle);

  // Load the pointer to a Code's entrypoint via a code pointer.
  // Only available when the sandbox is enabled as it requires the code pointer
  // table.
  void LoadCodeEntrypointViaCodePointer(Register destination,
                                        Operand field_operand,
                                        CodeEntrypointTag tag);
#endif  // V8_ENABLE_SANDBOX

  void LoadProtectedPointerField(Register destination, Operand field_operand);

  // Loads and stores the value of an external reference.
  // Special case code for load and store to take advantage of
  // load_rax/store_rax if possible/necessary.
  // For other operations, just use:
  //   Operand operand = ExternalReferenceAsOperand(extref);
  //   operation(operand, ..);
  void Load(Register destination, ExternalReference source);
  void Store(ExternalReference destination, Register source);

  // Pushes the address of the external reference onto the stack.
  void PushAddress(ExternalReference source);

  // Operations on roots in the root-array.
  // Load a root value where the index (or part of it) is variable.
  // The variable_offset register is added to the fixed_offset value
  // to get the index into the root-array.
  void PushRoot(RootIndex index);

  // Compare the object in a register to a value and jump if they are equal.
  void JumpIfRoot(Register with, RootIndex index, Label* if_equal,
                  Label::Distance if_equal_distance = Label::kFar) {
    CompareRoot(with, index);
    j(equal, if_equal, if_equal_distance);
  }
  void JumpIfRoot(Operand with, RootIndex index, Label* if_equal,
                  Label::Distance if_equal_distance = Label::kFar) {
    CompareRoot(with, index);
    j(equal, if_equal, if_equal_distance);
  }

  // Compare the object in a register to a value and jump if they are not equal.
  void JumpIfNotRoot(Register with, RootIndex index, Label* if_not_equal,
                     Label::Distance if_not_equal_distance = Label::kFar) {
    CompareRoot(with, index);
    j(not_equal, if_not_equal, if_not_equal_distance);
  }
  void JumpIfNotRoot(Operand with, RootIndex index, Label* if_not_equal,
                     Label::Distance if_not_equal_distance = Label::kFar) {
    CompareRoot(with, index);
    j(not_equal, if_not_equal, if_not_equal_distance);
  }

  // ---------------------------------------------------------------------------
  // GC Support

  // Notify the garbage collector that we wrote a pointer into an object.
  // |object| is the object being stored into, |value| is the object being
  // stored.  value and scratch registers are clobbered by the operation.
  // The offset is the offset from the start of the object, not the offset from
  // the tagged HeapObject pointer.  For use with FieldOperand(reg, off).
  void RecordWriteField(
      Register object, int offset, Register value, Register slot_address,
      SaveFPRegsMode save_fp, SmiCheck smi_check = SmiCheck::kInline,
      SlotDescriptor slot = SlotDescriptor::ForDirectPointerSlot());

  // For page containing |object| mark region covering |address|
  // dirty. |object| is the object being stored into, |value| is the
  // object being stored. The address and value registers are clobbered by the
  // operation.  RecordWrite filters out smis so it does not update
  // the write barrier if the value is a smi.
  void RecordWrite(
      Register object, Register slot_address, Register value,
      SaveFPRegsMode save_fp, SmiCheck smi_check = SmiCheck::kInline,
      SlotDescriptor slot = SlotDescriptor::ForDirectPointerSlot());

  // Allocates an EXIT/BUILTIN_EXIT/API_CALLBACK_EXIT frame with given number
  // of slots in non-GCed area.
  void EnterExitFrame(int extra_slots, StackFrame::Type frame_type,
                      Register c_function);
  void LeaveExitFrame();

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
  void InvokeFunction(Register function, Register new_target,
                      Register actual_parameter_count, InvokeType type);

  void InvokeFunction(Register function, Register new_target,
                      Register expected_parameter_count,
                      Register actual_parameter_count, InvokeType type);

  // ---------------------------------------------------------------------------
  // Macro instructions.

  void Cmp(Register dst, Handle<Object> source);
  void Cmp(Operand dst, Handle<Object> source);

  // Checks if value is in range [lower_limit, higher_limit] using a single
  // comparison. Flags CF=1 or ZF=1 indicate the value is in the range
  // (condition below_equal).
  void CompareRange(Register value, unsigned lower_limit,
                    unsigned higher_limit);
  void JumpIfIsInRange(Register value, unsigned lower_limit,
                       unsigned higher_limit, Label* on_in_range,
                       Label::Distance near_jump = Label::kFar);

  // Emit code to discard a non-negative number of pointer-sized elements
  // from the stack, clobbering only the rsp register.
  void Drop(int stack_elements);
  // Emit code to discard a positive number of pointer-sized elements
  // from the stack under the return address which remains on the top,
  // clobbering the rsp register.
  void DropUnderReturnAddress(int stack_elements,
                              Register scratch = kScratchRegister);
  void PushQuad(Operand src);
  void PushImm32(int32_t imm32);
  void Pop(Register dst);
  void Pop(Operand dst);
  void PopQuad(Operand dst);

  // Compare object type for heap object.
  // Always use unsigned comparisons: above and below, not less and greater.
  // Incoming register is heap_object and outgoing register is map.
  // They may be the same register, and may be kScratchRegister.
  void CmpObjectType(Register heap_object, InstanceType type, Register map);
  // Variant of the above, which only guarantees to set the correct
  // equal/not_equal flag. Map might not be loaded.
  void IsObjectType(Register heap_object, InstanceType type, Register scratch);
  // Variant of the above, which compares against a type range rather than a
  // single type (lower_limit and higher_limit are inclusive).
  //
  // Always use unsigned comparisons: below for a positive result.
  void IsObjectTypeInRange(Register heap_object, InstanceType low,
                           InstanceType high, Register scratch);
#if V8_STATIC_ROOTS_BOOL
  // Fast variant which is guaranteed to not actually load the instance type
  // from the map.
  void IsObjectTypeFast(Register heap_object, InstanceType type,
                        Register compressed_map_scratch);
  void CompareInstanceTypeWithUniqueCompressedMap(Register map,
                                                  InstanceType type);
#endif  // V8_STATIC_ROOTS_BOOL

  // Fast check if the object is a js receiver type. Assumes only primitive
  // objects or js receivers are passed.
  void JumpIfJSAnyIsNotPrimitive(
      Register heap_object, Register scratch, Label* target,
      Label::Distance distance = Label::kFar,
      Condition condition = Condition::kUnsignedGreaterThanEqual);
  void JumpIfJSAnyIsPrimitive(Register heap_object, Register scratch,
                              Label* target,
                              Label::Distance distance = Label::kFar) {
    return JumpIfJSAnyIsNotPrimitive(heap_object, scratch, target, distance,
                                     Condition::kUnsignedLessThan);
  }

  // Compare instance type ranges for a map (low and high inclusive)
  // Always use unsigned comparisons: below_equal for a positive result.
  void CmpInstanceTypeRange(Register map, Register instance_type_out,
                            InstanceType low, InstanceType high);

  template <typename Field>
  void DecodeField(Register reg) {
    static const int shift = Field::kShift;
    static const int mask = Field::kMask >> Field::kShift;
    if (shift != 0) {
      shrq(reg, Immediate(shift));
    }
    andq(reg, Immediate(mask));
  }

  void TestCodeIsMarkedForDeoptimization(Register code);
  void TestCodeIsTurbofanned(Register code);
  Immediate ClearedValue() const;

  // Tiering support.
  void AssertFeedbackCell(Register object,
                          Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void AssertFeedbackVector(Register object,
                            Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void ReplaceClosureCodeWithOptimizedCode(Register optimized_code,
                                           Register closure, Register scratch1,
                                           Register slot_address);
  void GenerateTailCallToReturnedCode(Runtime::FunctionId function_id,
                                      JumpMode jump_mode = JumpMode::kJump);
  Condition CheckFeedbackVectorFlagsNeedsProcessing(Register feedback_vector,
                                                    CodeKind current_code_kind);
  void CheckFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      Register feedback_vector, CodeKind current_code_kind,
      Label* flags_need_processing);
  void OptimizeCodeOrTailCallOptimizedCodeSlot(Register feedback_vector,
                                               Register closure,
                                               JumpMode jump_mode);
  // For compatibility with other archs.
  void OptimizeCodeOrTailCallOptimizedCodeSlot(Register flags,
                                               Register feedback_vector) {
    OptimizeCodeOrTailCallOptimizedCodeSlot(
        feedback_vector, kJSFunctionRegister, JumpMode::kJump);
  }

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
  void AssertUndefinedOrAllocationSite(Register object) NOOP_UNLESS_DEBUG_CODE;

  void AssertJSAny(Register object, Register map_tmp,
                   AbortReason abort_reason) NOOP_UNLESS_DEBUG_CODE;

  // ---------------------------------------------------------------------------
  // Exception handling

  // Push a new stack handler and link it into stack handler chain.
  void PushStackHandler();

  // Unlink the stack handler on top of the stack from the stack handler chain.
  void PopStackHandler();

  // ---------------------------------------------------------------------------
  // Support functions.

  // Load the global proxy from the current context.
  void LoadGlobalProxy(Register dst) {
    LoadNativeContextSlot(dst, Context::GLOBAL_PROXY_INDEX);
  }

  // Load the native context slot with the current index.
  void LoadNativeContextSlot(Register dst, int index);

  // Falls through and sets scratch_and_result to 0 on failure, jumps to
  // on_result on success.
  void TryLoadOptimizedOsrCode(Register scratch_and_result,
                               CodeKind min_opt_level, Register feedback_vector,
                               FeedbackSlot slot, Label* on_result,
                               Label::Distance distance);

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

  // Convenience function: tail call a runtime routine (jump)
  void TailCallRuntime(Runtime::FunctionId fid);

  // Jump to a runtime routines
  void JumpToExternalReference(const ExternalReference& ext,
                               bool builtin_exit_frame = false);

  // ---------------------------------------------------------------------------
  // StatsCounter support
  void IncrementCounter(StatsCounter* counter, int value) {
    if (!v8_flags.native_code_counters) return;
    EmitIncrementCounter(counter, value);
  }
  void EmitIncrementCounter(StatsCounter* counter, int value);
  void DecrementCounter(StatsCounter* counter, int value) {
    if (!v8_flags.native_code_counters) return;
    EmitDecrementCounter(counter, value);
  }
  void EmitDecrementCounter(StatsCounter* counter, int value);

  // ---------------------------------------------------------------------------
  // Stack limit utilities
  Operand StackLimitAsOperand(StackLimitKind kind);
  void StackOverflowCheck(
      Register num_args, Label* stack_overflow,
      Label::Distance stack_overflow_distance = Label::kFar);

  // ---------------------------------------------------------------------------
  // In-place weak references.
  void LoadWeakValue(Register in_out, Label* target_if_cleared);

 protected:
  static const int kSmiShift = kSmiTagSize + kSmiShiftSize;

  // Returns a register holding the smi value. The register MUST NOT be
  // modified. It may be the "smi 1 constant" register.
  Register GetSmiConstant(Tagged<Smi> value);

  // Drops arguments assuming that the return address was already popped.
  void DropArguments(Register count);

 private:
  // Helper functions for generating invokes.
  void InvokePrologue(Register expected_parameter_count,
                      Register actual_parameter_count, InvokeType type);

  DISALLOW_IMPLICIT_CONSTRUCTORS(MacroAssembler);
};

// -----------------------------------------------------------------------------
// Static helper functions.

// Generate an Operand for loading a field from an object.
inline Operand FieldOperand(Register object, int offset) {
  return Operand(object, offset - kHeapObjectTag);
}

// For compatibility with platform-independent code.
inline MemOperand FieldMemOperand(Register object, int offset) {
  return MemOperand(object, offset - kHeapObjectTag);
}

// Generate an Operand for loading a field from an object. Object pointer is a
// compressed pointer when pointer compression is enabled.
inline Operand FieldOperand(TaggedRegister object, int offset) {
  if (COMPRESS_POINTERS_BOOL) {
    return Operand(kPtrComprCageBaseRegister, object.reg(),
                   ScaleFactor::times_1, offset - kHeapObjectTag);
  } else {
    return Operand(object.reg(), offset - kHeapObjectTag);
  }
}

// Generate an Operand for loading an indexed field from an object.
inline Operand FieldOperand(Register object, Register index, ScaleFactor scale,
                            int offset) {
  return Operand(object, index, scale, offset - kHeapObjectTag);
}

// Provides access to exit frame stack space (not GC-ed).
inline Operand ExitFrameStackSlotOperand(int offset) {
#ifdef V8_TARGET_OS_WIN
  return Operand(rsp, offset + kWindowsHomeStackSlots * kSystemPointerSize);
#else
  return Operand(rsp, offset);
#endif
}

// Provides access to exit frame parameters (GC-ed).
inline Operand ExitFrameCallerStackSlotOperand(int index) {
  return Operand(rbp,
                 (BuiltinExitFrameConstants::kFixedSlotCountAboveFp + index) *
                     kSystemPointerSize);
}

struct MoveCycleState {
  // Whether a move in the cycle needs the scratch or double scratch register.
  bool pending_scratch_register_use = false;
  bool pending_double_scratch_register_use = false;
};

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

#define ACCESS_MASM(masm) masm->

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_X64_MACRO_ASSEMBLER_X64_H_
