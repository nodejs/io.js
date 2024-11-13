// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/mips64/constants-mips64.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/code-generator-impl.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/gap-resolver.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/osr.h"
#include "src/heap/mutable-page-metadata.h"

namespace v8 {
namespace internal {
namespace compiler {

#define __ masm()->

// TODO(plind): consider renaming these macros.
#define TRACE_MSG(msg)                                                      \
  PrintF("code_gen: \'%s\' in function %s at line %d\n", msg, __FUNCTION__, \
         __LINE__)

#define TRACE_UNIMPL()                                                       \
  PrintF("UNIMPLEMENTED code_generator_mips: %s at line %d\n", __FUNCTION__, \
         __LINE__)

// Adds Mips-specific methods to convert InstructionOperands.
class MipsOperandConverter final : public InstructionOperandConverter {
 public:
  MipsOperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  FloatRegister OutputSingleRegister(size_t index = 0) {
    return ToSingleRegister(instr_->OutputAt(index));
  }

  FloatRegister InputSingleRegister(size_t index) {
    return ToSingleRegister(instr_->InputAt(index));
  }

  FloatRegister ToSingleRegister(InstructionOperand* op) {
    // Single (Float) and Double register namespace is same on MIPS,
    // both are typedefs of FPURegister.
    return ToDoubleRegister(op);
  }

  Register InputOrZeroRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) {
      DCHECK_EQ(0, InputInt32(index));
      return zero_reg;
    }
    return InputRegister(index);
  }

  DoubleRegister InputOrZeroDoubleRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) return kDoubleRegZero;

    return InputDoubleRegister(index);
  }

  DoubleRegister InputOrZeroSingleRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) return kDoubleRegZero;

    return InputSingleRegister(index);
  }

  Operand InputImmediate(size_t index) {
    Constant constant = ToConstant(instr_->InputAt(index));
    switch (constant.type()) {
      case Constant::kInt32:
        return Operand(constant.ToInt32());
      case Constant::kInt64:
        return Operand(constant.ToInt64());
      case Constant::kFloat32:
        return Operand::EmbeddedNumber(constant.ToFloat32());
      case Constant::kFloat64:
        return Operand::EmbeddedNumber(constant.ToFloat64().value());
      case Constant::kExternalReference:
      case Constant::kCompressedHeapObject:
      case Constant::kHeapObject:
        // TODO(plind): Maybe we should handle ExtRef & HeapObj here?
        //    maybe not done on arm due to const pool ??
        break;
      case Constant::kRpoNumber:
        UNREACHABLE();  // TODO(titzer): RPO immediates on mips?
    }
    UNREACHABLE();
  }

  Operand InputOperand(size_t index) {
    InstructionOperand* op = instr_->InputAt(index);
    if (op->IsRegister()) {
      return Operand(ToRegister(op));
    }
    return InputImmediate(index);
  }

  MemOperand MemoryOperand(size_t* first_index) {
    const size_t index = *first_index;
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_None:
        break;
      case kMode_Root:
        *first_index += 1;
        return MemOperand(kRootRegister, InputInt32(index));
      case kMode_MRI:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputInt32(index + 1));
      case kMode_MRR:
        // TODO(plind): r6 address mode, to be implemented ...
        UNREACHABLE();
    }
    UNREACHABLE();
  }

  MemOperand MemoryOperand(size_t index = 0) { return MemoryOperand(&index); }

  MemOperand ToMemOperand(InstructionOperand* op) const {
    DCHECK_NOT_NULL(op);
    DCHECK(op->IsStackSlot() || op->IsFPStackSlot());
    return SlotToMemOperand(AllocatedOperand::cast(op)->index());
  }

  MemOperand SlotToMemOperand(int slot) const {
    FrameOffset offset = frame_access_state()->GetFrameOffset(slot);
    return MemOperand(offset.from_stack_pointer() ? sp : fp, offset.offset());
  }
};

static inline bool HasRegisterInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsRegister();
}

namespace {

class OutOfLineRecordWrite final : public OutOfLineCode {
 public:
  OutOfLineRecordWrite(CodeGenerator* gen, Register object, Register index,
                       Register value, Register scratch0, Register scratch1,
                       RecordWriteMode mode, StubCallMode stub_mode)
      : OutOfLineCode(gen),
        object_(object),
        index_(index),
        value_(value),
        scratch0_(scratch0),
        scratch1_(scratch1),
        mode_(mode),
#if V8_ENABLE_WEBASSEMBLY
        stub_mode_(stub_mode),
#endif  // V8_ENABLE_WEBASSEMBLY
        must_save_lr_(!gen->frame_access_state()->has_frame()),
        zone_(gen->zone()) {
    DCHECK(!AreAliased(object, index, scratch0, scratch1));
    DCHECK(!AreAliased(value, index, scratch0, scratch1));
  }

  void Generate() final {
    __ CheckPageFlag(value_, scratch0_,
                     MemoryChunk::kPointersToHereAreInterestingMask, eq,
                     exit());
    __ Daddu(scratch1_, object_, index_);
    SaveFPRegsMode const save_fp_mode = frame()->DidAllocateDoubleRegisters()
                                            ? SaveFPRegsMode::kSave
                                            : SaveFPRegsMode::kIgnore;
    if (must_save_lr_) {
      // We need to save and restore ra if the frame was elided.
      __ Push(ra);
    }
    if (mode_ == RecordWriteMode::kValueIsEphemeronKey) {
      __ CallEphemeronKeyBarrier(object_, scratch1_, save_fp_mode);
#if V8_ENABLE_WEBASSEMBLY
    } else if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      // A direct call to a wasm runtime stub defined in this module.
      // Just encode the stub index. This will be patched when the code
      // is added to the native module and copied into wasm code space.
      __ CallRecordWriteStubSaveRegisters(object_, scratch1_, save_fp_mode,
                                          StubCallMode::kCallWasmRuntimeStub);
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      __ CallRecordWriteStubSaveRegisters(object_, scratch1_, save_fp_mode);
    }
    if (must_save_lr_) {
      __ Pop(ra);
    }
  }

 private:
  Register const object_;
  Register const index_;
  Register const value_;
  Register const scratch0_;
  Register const scratch1_;
  RecordWriteMode const mode_;
#if V8_ENABLE_WEBASSEMBLY
  StubCallMode const stub_mode_;
#endif  // V8_ENABLE_WEBASSEMBLY
  bool must_save_lr_;
  Zone* zone_;
};

#define CREATE_OOL_CLASS(ool_name, tasm_ool_name, T)                 \
  class ool_name final : public OutOfLineCode {                      \
   public:                                                           \
    ool_name(CodeGenerator* gen, T dst, T src1, T src2)              \
        : OutOfLineCode(gen), dst_(dst), src1_(src1), src2_(src2) {} \
                                                                     \
    void Generate() final { __ tasm_ool_name(dst_, src1_, src2_); }  \
                                                                     \
   private:                                                          \
    T const dst_;                                                    \
    T const src1_;                                                   \
    T const src2_;                                                   \
  }

CREATE_OOL_CLASS(OutOfLineFloat32Max, Float32MaxOutOfLine, FPURegister);
CREATE_OOL_CLASS(OutOfLineFloat32Min, Float32MinOutOfLine, FPURegister);
CREATE_OOL_CLASS(OutOfLineFloat64Max, Float64MaxOutOfLine, FPURegister);
CREATE_OOL_CLASS(OutOfLineFloat64Min, Float64MinOutOfLine, FPURegister);

#undef CREATE_OOL_CLASS

Condition FlagsConditionToConditionCmp(FlagsCondition condition) {
  switch (condition) {
    case kEqual:
      return eq;
    case kNotEqual:
      return ne;
    case kSignedLessThan:
      return lt;
    case kSignedGreaterThanOrEqual:
      return ge;
    case kSignedLessThanOrEqual:
      return le;
    case kSignedGreaterThan:
      return gt;
    case kUnsignedLessThan:
      return lo;
    case kUnsignedGreaterThanOrEqual:
      return hs;
    case kUnsignedLessThanOrEqual:
      return ls;
    case kUnsignedGreaterThan:
      return hi;
    case kUnorderedEqual:
    case kUnorderedNotEqual:
      break;
    default:
      break;
  }
  UNREACHABLE();
}

Condition FlagsConditionToConditionTst(FlagsCondition condition) {
  switch (condition) {
    case kNotEqual:
      return ne;
    case kEqual:
      return eq;
    default:
      break;
  }
  UNREACHABLE();
}

Condition FlagsConditionToConditionOvf(FlagsCondition condition) {
  switch (condition) {
    case kOverflow:
      return ne;
    case kNotOverflow:
      return eq;
    default:
      break;
  }
  UNREACHABLE();
}

FPUCondition FlagsConditionToConditionCmpFPU(bool* predicate,
                                             FlagsCondition condition) {
  switch (condition) {
    case kEqual:
      *predicate = true;
      return EQ;
    case kNotEqual:
      *predicate = false;
      return EQ;
    case kUnsignedLessThan:
    case kFloatLessThan:
      *predicate = true;
      return OLT;
    case kUnsignedGreaterThanOrEqual:
      *predicate = false;
      return OLT;
    case kUnsignedLessThanOrEqual:
    case kFloatLessThanOrEqual:
      *predicate = true;
      return OLE;
    case kUnsignedGreaterThan:
      *predicate = false;
      return OLE;
    case kFloatGreaterThan:
      *predicate = false;
      return ULE;
    case kFloatGreaterThanOrEqual:
      *predicate = false;
      return ULT;
    case kFloatLessThanOrUnordered:
      *predicate = true;
      return ULT;
    case kFloatGreaterThanOrUnordered:
      *predicate = false;
      return OLE;
    case kFloatGreaterThanOrEqualOrUnordered:
      *predicate = false;
      return OLT;
    case kFloatLessThanOrEqualOrUnordered:
      *predicate = true;
      return ULE;
    case kUnorderedEqual:
    case kUnorderedNotEqual:
      *predicate = true;
      break;
    default:
      *predicate = true;
      break;
  }
  UNREACHABLE();
}

}  // namespace

#define ASSEMBLE_ATOMIC_LOAD_INTEGER(asm_instr)          \
  do {                                                   \
    __ asm_instr(i.OutputRegister(), i.MemoryOperand()); \
    __ sync();                                           \
  } while (0)

#define ASSEMBLE_ATOMIC_STORE_INTEGER(asm_instr)               \
  do {                                                         \
    __ sync();                                                 \
    __ asm_instr(i.InputOrZeroRegister(2), i.MemoryOperand()); \
    __ sync();                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_BINOP(load_linked, store_conditional, bin_instr)       \
  do {                                                                         \
    Label binop;                                                               \
    __ Daddu(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    __ sync();                                                                 \
    __ bind(&binop);                                                           \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ bin_instr(i.TempRegister(1), i.OutputRegister(0),                       \
                 Operand(i.InputRegister(2)));                                 \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&binop, eq, i.TempRegister(1), Operand(zero_reg));          \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_BINOP_EXT(load_linked, store_conditional, sign_extend, \
                                  size, bin_instr, representation)             \
  do {                                                                         \
    Label binop;                                                               \
    __ daddu(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(3), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(3), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Dsubu(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(3)));                                      \
    __ sll(i.TempRegister(3), i.TempRegister(3), 3);                           \
    __ sync();                                                                 \
    __ bind(&binop);                                                           \
    __ load_linked(i.TempRegister(1), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(1), i.TempRegister(3),  \
                   size, sign_extend);                                         \
    __ bin_instr(i.TempRegister(2), i.OutputRegister(0),                       \
                 Operand(i.InputRegister(2)));                                 \
    __ InsertBits(i.TempRegister(1), i.TempRegister(2), i.TempRegister(3),     \
                  size);                                                       \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&binop, eq, i.TempRegister(1), Operand(zero_reg));          \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(load_linked, store_conditional)       \
  do {                                                                         \
    Label exchange;                                                            \
    __ sync();                                                                 \
    __ bind(&exchange);                                                        \
    __ daddu(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ mov(i.TempRegister(1), i.InputRegister(2));                             \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&exchange, eq, i.TempRegister(1), Operand(zero_reg));       \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(                                  \
    load_linked, store_conditional, sign_extend, size, representation)         \
  do {                                                                         \
    Label exchange;                                                            \
    __ daddu(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Dsubu(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(1)));                                      \
    __ sll(i.TempRegister(1), i.TempRegister(1), 3);                           \
    __ sync();                                                                 \
    __ bind(&exchange);                                                        \
    __ load_linked(i.TempRegister(2), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(2), i.TempRegister(1),  \
                   size, sign_extend);                                         \
    __ InsertBits(i.TempRegister(2), i.InputRegister(2), i.TempRegister(1),    \
                  size);                                                       \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&exchange, eq, i.TempRegister(2), Operand(zero_reg));       \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(load_linked,                  \
                                                 store_conditional)            \
  do {                                                                         \
    Label compareExchange;                                                     \
    Label exit;                                                                \
    __ daddu(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    __ sync();                                                                 \
    __ bind(&compareExchange);                                                 \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ BranchShort(&exit, ne, i.InputRegister(2),                              \
                   Operand(i.OutputRegister(0)));                              \
    __ mov(i.TempRegister(2), i.InputRegister(3));                             \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&compareExchange, eq, i.TempRegister(2),                    \
                   Operand(zero_reg));                                         \
    __ bind(&exit);                                                            \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(                          \
    load_linked, store_conditional, sign_extend, size, representation)         \
  do {                                                                         \
    Label compareExchange;                                                     \
    Label exit;                                                                \
    __ daddu(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Dsubu(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(1)));                                      \
    __ sll(i.TempRegister(1), i.TempRegister(1), 3);                           \
    __ sync();                                                                 \
    __ bind(&compareExchange);                                                 \
    __ load_linked(i.TempRegister(2), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(2), i.TempRegister(1),  \
                   size, sign_extend);                                         \
    __ ExtractBits(i.TempRegister(2), i.InputRegister(2), zero_reg, size,      \
                   sign_extend);                                               \
    __ BranchShort(&exit, ne, i.TempRegister(2),                               \
                   Operand(i.OutputRegister(0)));                              \
    __ InsertBits(i.TempRegister(2), i.InputRegister(3), i.TempRegister(1),    \
                  size);                                                       \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&compareExchange, eq, i.TempRegister(2),                    \
                   Operand(zero_reg));                                         \
    __ bind(&exit);                                                            \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_IEEE754_BINOP(name)                                        \
  do {                                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                           \
    __ PrepareCallCFunction(0, 2, kScratchReg);                             \
    __ MovToFloatParameters(i.InputDoubleRegister(0),                       \
                            i.InputDoubleRegister(1));                      \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 2); \
    /* Move the result in the double result register. */                    \
    __ MovFromFloatResult(i.OutputDoubleRegister());                        \
  } while (0)

#define ASSEMBLE_IEEE754_UNOP(name)                                         \
  do {                                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                           \
    __ PrepareCallCFunction(0, 1, kScratchReg);                             \
    __ MovToFloatParameter(i.InputDoubleRegister(0));                       \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 1); \
    /* Move the result in the double result register. */                    \
    __ MovFromFloatResult(i.OutputDoubleRegister());                        \
  } while (0)

#define ASSEMBLE_F64X2_ARITHMETIC_BINOP(op)                     \
  do {                                                          \
    __ op(i.OutputSimd128Register(), i.InputSimd128Register(0), \
          i.InputSimd128Register(1));                           \
  } while (0)

void CodeGenerator::AssembleDeconstructFrame() {
  __ mov(sp, fp);
  __ Pop(ra, fp);
}

void CodeGenerator::AssemblePrepareTailCall() {
  if (frame_access_state()->has_frame()) {
    __ Ld(ra, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
    __ Ld(fp, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  }
  frame_access_state()->SetFrameAccessToSP();
}

namespace {

void AdjustStackPointerForTailCall(MacroAssembler* masm,
                                   FrameAccessState* state,
                                   int new_slot_above_sp,
                                   bool allow_shrinkage = true) {
  int current_sp_offset = state->GetSPToFPSlotCount() +
                          StandardFrameConstants::kFixedSlotCountAboveFp;
  int stack_slot_delta = new_slot_above_sp - current_sp_offset;
  if (stack_slot_delta > 0) {
    masm->Dsubu(sp, sp, stack_slot_delta * kSystemPointerSize);
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    masm->Daddu(sp, sp, -stack_slot_delta * kSystemPointerSize);
    state->IncreaseSPDelta(stack_slot_delta);
  }
}

}  // namespace

void CodeGenerator::AssembleTailCallBeforeGap(Instruction* instr,
                                              int first_unused_slot_offset) {
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset, false);
}

void CodeGenerator::AssembleTailCallAfterGap(Instruction* instr,
                                             int first_unused_slot_offset) {
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset);
}

// Check that {kJavaScriptCallCodeStartRegister} is correct.
void CodeGenerator::AssembleCodeStartRegisterCheck() {
  __ ComputeCodeStartAddress(kScratchReg);
  __ Assert(eq, AbortReason::kWrongFunctionCodeStart,
            kJavaScriptCallCodeStartRegister, Operand(kScratchReg));
}

// Check if the code object is marked for deoptimization. If it is, then it
// jumps to the CompileLazyDeoptimizedCode builtin. In order to do this we need
// to:
//    1. read from memory the word that contains that bit, which can be found in
//       the flags in the referenced {Code} object;
//    2. test kMarkedForDeoptimizationBit in those flags; and
//    3. if it is not zero then it jumps to the builtin.
void CodeGenerator::BailoutIfDeoptimized() {
  int offset = InstructionStream::kCodeOffset - InstructionStream::kHeaderSize;
  __ Ld(kScratchReg, MemOperand(kJavaScriptCallCodeStartRegister, offset));
  __ Lwu(kScratchReg, FieldMemOperand(kScratchReg, Code::kFlagsOffset));
  __ And(kScratchReg, kScratchReg,
         Operand(1 << Code::kMarkedForDeoptimizationBit));
  __ TailCallBuiltin(Builtin::kCompileLazyDeoptimizedCode, ne, kScratchReg,
                     Operand(zero_reg));
}

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  MipsOperandConverter i(this, instr);
  InstructionCode opcode = instr->opcode();
  ArchOpcode arch_opcode = ArchOpcodeField::decode(opcode);
  switch (arch_opcode) {
    case kArchCallCodeObject: {
      if (instr->InputAt(0)->IsImmediate()) {
        __ Call(i.InputCode(0), RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        CodeEntrypointTag tag =
            i.InputCodeEntrypointTag(instr->CodeEnrypointTagInputIndex());
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ CallCodeObject(reg, tag);
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchCallBuiltinPointer: {
      DCHECK(!instr->InputAt(0)->IsImmediate());
      Register builtin_index = i.InputRegister(0);
      Register target =
          instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister)
              ? kJavaScriptCallCodeStartRegister
              : builtin_index;
      __ CallBuiltinByIndex(builtin_index, target);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
#if V8_ENABLE_WEBASSEMBLY
    case kArchCallWasmFunction: {
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Call(wasm_code, constant.rmode());
      } else {
        __ daddiu(kScratchReg, i.InputRegister(0), 0);
        __ Call(kScratchReg);
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchTailCallWasm: {
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Jump(wasm_code, constant.rmode());
      } else {
        __ daddiu(kScratchReg, i.InputRegister(0), 0);
        __ Jump(kScratchReg);
      }
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchTailCallCodeObject: {
      if (instr->InputAt(0)->IsImmediate()) {
        __ Jump(i.InputCode(0), RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputRegister(0);
        CodeEntrypointTag tag =
            i.InputCodeEntrypointTag(instr->CodeEnrypointTagInputIndex());
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ JumpCodeObject(reg, tag);
      }
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchTailCallAddress: {
      CHECK(!instr->InputAt(0)->IsImmediate());
      Register reg = i.InputRegister(0);
      DCHECK_IMPLIES(
          instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
          reg == kJavaScriptCallCodeStartRegister);
      __ Jump(reg);
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchCallJSFunction: {
      Register func = i.InputRegister(0);
      if (v8_flags.debug_code) {
        // Check the function's context matches the context argument.
        __ Ld(kScratchReg, FieldMemOperand(func, JSFunction::kContextOffset));
        __ Assert(eq, AbortReason::kWrongFunctionContext, cp,
                  Operand(kScratchReg));
      }
      __ CallJSFunction(func);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchPrepareCallCFunction: {
      int const num_parameters = MiscField::decode(instr->opcode());
      __ PrepareCallCFunction(num_parameters, kScratchReg);
      // Frame alignment requires using FP-relative frame addressing.
      frame_access_state()->SetFrameAccessToFP();
      break;
    }
    case kArchSaveCallerRegisters: {
      fp_mode_ =
          static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode()));
      DCHECK(fp_mode_ == SaveFPRegsMode::kIgnore ||
             fp_mode_ == SaveFPRegsMode::kSave);
      // kReturnRegister0 should have been saved before entering the stub.
      int bytes = __ PushCallerSaved(fp_mode_, kReturnRegister0);
      DCHECK(IsAligned(bytes, kSystemPointerSize));
      DCHECK_EQ(0, frame_access_state()->sp_delta());
      frame_access_state()->IncreaseSPDelta(bytes / kSystemPointerSize);
      DCHECK(!caller_registers_saved_);
      caller_registers_saved_ = true;
      break;
    }
    case kArchRestoreCallerRegisters: {
      DCHECK(fp_mode_ ==
             static_cast<SaveFPRegsMode>(MiscField::decode(instr->opcode())));
      DCHECK(fp_mode_ == SaveFPRegsMode::kIgnore ||
             fp_mode_ == SaveFPRegsMode::kSave);
      // Don't overwrite the returned value.
      int bytes = __ PopCallerSaved(fp_mode_, kReturnRegister0);
      frame_access_state()->IncreaseSPDelta(-(bytes / kSystemPointerSize));
      DCHECK_EQ(0, frame_access_state()->sp_delta());
      DCHECK(caller_registers_saved_);
      caller_registers_saved_ = false;
      break;
    }
    case kArchPrepareTailCall:
      AssemblePrepareTailCall();
      break;
    case kArchCallCFunctionWithFrameState:
    case kArchCallCFunction: {
      int const num_gp_parameters = ParamField::decode(instr->opcode());
      int const num_fp_parameters = FPParamField::decode(instr->opcode());
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes;
      Label return_location;
#if V8_ENABLE_WEBASSEMBLY
      bool isWasmCapiFunction =
          linkage()->GetIncomingDescriptor()->IsWasmCapiFunction();
      if (isWasmCapiFunction) {
        // Put the return address in a stack slot.
        __ LoadAddressPCRelative(kScratchReg, &return_location);
        __ sd(kScratchReg,
              MemOperand(fp, WasmExitFrameConstants::kCallingPCOffset));
        set_isolate_data_slots = SetIsolateDataSlots::kNo;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      int pc_offset;
      if (instr->InputAt(0)->IsImmediate()) {
        ExternalReference ref = i.InputExternalReference(0);
        pc_offset = __ CallCFunction(ref, num_gp_parameters, num_fp_parameters,
                                     set_isolate_data_slots, &return_location);
      } else {
        Register func = i.InputRegister(0);
        pc_offset = __ CallCFunction(func, num_gp_parameters, num_fp_parameters,
                                     set_isolate_data_slots, &return_location);
      }
      RecordSafepoint(instr->reference_map(), pc_offset);

      bool const needs_frame_state =
          (arch_opcode == kArchCallCFunctionWithFrameState);
      if (needs_frame_state) {
        RecordDeoptInfo(instr, pc_offset);
      }

      frame_access_state()->SetFrameAccessToDefault();
      // Ideally, we should decrement SP delta to match the change of stack
      // pointer in CallCFunction. However, for certain architectures (e.g.
      // ARM), there may be more strict alignment requirement, causing old SP
      // to be saved on the stack. In those cases, we can not calculate the SP
      // delta statically.
      frame_access_state()->ClearSPDelta();
      if (caller_registers_saved_) {
        // Need to re-sync SP delta introduced in kArchSaveCallerRegisters.
        // Here, we assume the sequence to be:
        //   kArchSaveCallerRegisters;
        //   kArchCallCFunction;
        //   kArchRestoreCallerRegisters;
        int bytes =
            __ RequiredStackSizeForCallerSaved(fp_mode_, kReturnRegister0);
        frame_access_state()->IncreaseSPDelta(bytes / kSystemPointerSize);
      }
      break;
    }
    case kArchJmp:
      AssembleArchJump(i.InputRpo(0));
      break;
    case kArchBinarySearchSwitch:
      AssembleArchBinarySearchSwitch(instr);
      break;
    case kArchTableSwitch:
      AssembleArchTableSwitch(instr);
      break;
    case kArchAbortCSADcheck:
      DCHECK(i.InputRegister(0) == a0);
      {
        // We don't actually want to generate a pile of code for this, so just
        // claim there is a stack frame, without generating one.
        FrameScope scope(masm(), StackFrame::NO_FRAME_TYPE);
        __ CallBuiltin(Builtin::kAbortCSADcheck);
      }
      __ stop();
      break;
    case kArchDebugBreak:
      __ DebugBreak();
      break;
    case kArchComment:
      __ RecordComment(reinterpret_cast<const char*>(i.InputInt64(0)),
                       SourceLocation());
      break;
    case kArchNop:
    case kArchThrowTerminator:
      // don't emit code for nops.
      break;
    case kArchDeoptimize: {
      DeoptimizationExit* exit =
          BuildTranslation(instr, -1, 0, 0, OutputFrameStateCombine::Ignore());
      __ Branch(exit->label());
      break;
    }
    case kArchRet:
      AssembleReturn(instr->InputAt(0));
      break;
#if V8_ENABLE_WEBASSEMBLY
    case kArchStackPointer:
      // The register allocator expects an allocatable register for the output,
      // we cannot use sp directly.
      __ mov(i.OutputRegister(), sp);
      break;
    case kArchSetStackPointer: {
      DCHECK(instr->InputAt(0)->IsRegister());
      __ mov(sp, i.InputRegister(0));
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchStackPointerGreaterThan: {
      Register lhs_register = sp;
      uint32_t offset;
      if (ShouldApplyOffsetToStackCheck(instr, &offset)) {
        lhs_register = i.TempRegister(1);
        __ Dsubu(lhs_register, sp, offset);
      }
      __ Sltu(i.TempRegister(0), i.InputRegister(0), lhs_register);
      break;
    }
    case kArchStackCheckOffset:
      __ Move(i.OutputRegister(), Smi::FromInt(GetStackCheckOffset()));
      break;
    case kArchFramePointer:
      __ mov(i.OutputRegister(), fp);
      break;
    case kArchParentFramePointer:
      if (frame_access_state()->has_frame()) {
        __ Ld(i.OutputRegister(), MemOperand(fp, 0));
      } else {
        __ mov(i.OutputRegister(), fp);
      }
      break;
    case kArchTruncateDoubleToI:
      __ TruncateDoubleToI(isolate(), zone(), i.OutputRegister(),
                           i.InputDoubleRegister(0), DetermineStubCallMode());
      break;
    case kArchStoreWithWriteBarrier:  // Fall through.
    case kArchAtomicStoreWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      Register object = i.InputRegister(0);
      Register index = i.InputRegister(1);
      Register value = i.InputRegister(2);
      Register scratch0 = i.TempRegister(0);
      Register scratch1 = i.TempRegister(1);
      auto ool = zone()->New<OutOfLineRecordWrite>(this, object, index, value,
                                                   scratch0, scratch1, mode,
                                                   DetermineStubCallMode());
      __ Daddu(kScratchReg, object, index);
      if (arch_opcode == kArchStoreWithWriteBarrier) {
        __ Sd(value, MemOperand(kScratchReg));
      } else {
        DCHECK_EQ(kArchAtomicStoreWithWriteBarrier, arch_opcode);
        __ sync();
        __ Sd(value, MemOperand(kScratchReg));
        __ sync();
      }
      if (mode > RecordWriteMode::kValueIsPointer) {
        __ JumpIfSmi(value, ool->exit());
      }
      __ CheckPageFlag(object, scratch0,
                       MemoryChunk::kPointersFromHereAreInterestingMask, ne,
                       ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchStoreIndirectWithWriteBarrier:
      UNREACHABLE();
    case kArchStackSlot: {
      FrameOffset offset =
          frame_access_state()->GetFrameOffset(i.InputInt32(0));
      Register base_reg = offset.from_stack_pointer() ? sp : fp;
      __ Daddu(i.OutputRegister(), base_reg, Operand(offset.offset()));
      if (v8_flags.debug_code) {
        // Verify that the output_register is properly aligned
        __ And(kScratchReg, i.OutputRegister(),
               Operand(kSystemPointerSize - 1));
        __ Assert(eq, AbortReason::kAllocationIsNotDoubleAligned, kScratchReg,
                  Operand(zero_reg));
      }
      break;
    }
    case kIeee754Float64Acos:
      ASSEMBLE_IEEE754_UNOP(acos);
      break;
    case kIeee754Float64Acosh:
      ASSEMBLE_IEEE754_UNOP(acosh);
      break;
    case kIeee754Float64Asin:
      ASSEMBLE_IEEE754_UNOP(asin);
      break;
    case kIeee754Float64Asinh:
      ASSEMBLE_IEEE754_UNOP(asinh);
      break;
    case kIeee754Float64Atan:
      ASSEMBLE_IEEE754_UNOP(atan);
      break;
    case kIeee754Float64Atanh:
      ASSEMBLE_IEEE754_UNOP(atanh);
      break;
    case kIeee754Float64Atan2:
      ASSEMBLE_IEEE754_BINOP(atan2);
      break;
    case kIeee754Float64Cos:
      ASSEMBLE_IEEE754_UNOP(cos);
      break;
    case kIeee754Float64Cosh:
      ASSEMBLE_IEEE754_UNOP(cosh);
      break;
    case kIeee754Float64Cbrt:
      ASSEMBLE_IEEE754_UNOP(cbrt);
      break;
    case kIeee754Float64Exp:
      ASSEMBLE_IEEE754_UNOP(exp);
      break;
    case kIeee754Float64Expm1:
      ASSEMBLE_IEEE754_UNOP(expm1);
      break;
    case kIeee754Float64Log:
      ASSEMBLE_IEEE754_UNOP(log);
      break;
    case kIeee754Float64Log1p:
      ASSEMBLE_IEEE754_UNOP(log1p);
      break;
    case kIeee754Float64Log2:
      ASSEMBLE_IEEE754_UNOP(log2);
      break;
    case kIeee754Float64Log10:
      ASSEMBLE_IEEE754_UNOP(log10);
      break;
    case kIeee754Float64Pow:
      ASSEMBLE_IEEE754_BINOP(pow);
      break;
    case kIeee754Float64Sin:
      ASSEMBLE_IEEE754_UNOP(sin);
      break;
    case kIeee754Float64Sinh:
      ASSEMBLE_IEEE754_UNOP(sinh);
      break;
    case kIeee754Float64Tan:
      ASSEMBLE_IEEE754_UNOP(tan);
      break;
    case kIeee754Float64Tanh:
      ASSEMBLE_IEEE754_UNOP(tanh);
      break;
    case kMips64Add:
      __ Addu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Dadd:
      __ Daddu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64DaddOvf:
      __ DaddOverflow(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1),
                      kScratchReg);
      break;
    case kMips64Sub:
      __ Subu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Dsub:
      __ Dsubu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64DsubOvf:
      __ DsubOverflow(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1),
                      kScratchReg);
      break;
    case kMips64Mul:
      __ Mul(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64MulOvf:
      __ MulOverflow(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1),
                     kScratchReg);
      break;
    case kMips64DMulOvf:
      __ DMulOverflow(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1),
                      kScratchReg);
      break;
    case kMips64MulHigh:
      __ Mulh(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64MulHighU:
      __ Mulhu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64DMulHigh:
      __ Dmulh(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64DMulHighU:
      __ Dmulhu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Div:
      __ Div(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      if (kArchVariant == kMips64r6) {
        __ selnez(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        __ Movz(i.OutputRegister(), i.InputRegister(1), i.InputRegister(1));
      }
      break;
    case kMips64DivU:
      __ Divu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      if (kArchVariant == kMips64r6) {
        __ selnez(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        __ Movz(i.OutputRegister(), i.InputRegister(1), i.InputRegister(1));
      }
      break;
    case kMips64Mod:
      __ Mod(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64ModU:
      __ Modu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Dmul:
      __ Dmul(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Ddiv:
      __ Ddiv(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      if (kArchVariant == kMips64r6) {
        __ selnez(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        __ Movz(i.OutputRegister(), i.InputRegister(1), i.InputRegister(1));
      }
      break;
    case kMips64DdivU:
      __ Ddivu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      if (kArchVariant == kMips64r6) {
        __ selnez(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        __ Movz(i.OutputRegister(), i.InputRegister(1), i.InputRegister(1));
      }
      break;
    case kMips64Dmod:
      __ Dmod(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64DmodU:
      __ Dmodu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Dlsa:
      DCHECK(instr->InputAt(2)->IsImmediate());
      __ Dlsa(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
              i.InputInt8(2));
      break;
    case kMips64Lsa:
      DCHECK(instr->InputAt(2)->IsImmediate());
      __ Lsa(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
             i.InputInt8(2));
      break;
    case kMips64And:
      __ And(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64And32:
        __ And(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Or:
      __ Or(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Or32:
        __ Or(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Nor:
      if (instr->InputAt(1)->IsRegister()) {
        __ Nor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      } else {
        DCHECK_EQ(0, i.InputOperand(1).immediate());
        __ Nor(i.OutputRegister(), i.InputRegister(0), zero_reg);
      }
      break;
    case kMips64Nor32:
      if (instr->InputAt(1)->IsRegister()) {
        __ Nor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      } else {
        DCHECK_EQ(0, i.InputOperand(1).immediate());
        __ Nor(i.OutputRegister(), i.InputRegister(0), zero_reg);
      }
      break;
    case kMips64Xor:
      __ Xor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Xor32:
        __ Xor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
        __ sll(i.OutputRegister(), i.OutputRegister(), 0x0);
      break;
    case kMips64Clz:
      __ Clz(i.OutputRegister(), i.InputRegister(0));
      break;
    case kMips64Dclz:
      __ dclz(i.OutputRegister(), i.InputRegister(0));
      break;
    case kMips64Ctz: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Ctz(dst, src);
    } break;
    case kMips64Dctz: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Dctz(dst, src);
    } break;
    case kMips64Popcnt: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Popcnt(dst, src);
    } break;
    case kMips64Dpopcnt: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Dpopcnt(dst, src);
    } break;
    case kMips64Shl:
      if (instr->InputAt(1)->IsRegister()) {
        __ sllv(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ sll(i.OutputRegister(), i.InputRegister(0),
               static_cast<uint16_t>(imm));
      }
      break;
    case kMips64Shr:
      if (instr->InputAt(1)->IsRegister()) {
        __ srlv(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ srl(i.OutputRegister(), i.InputRegister(0),
               static_cast<uint16_t>(imm));
      }
      break;
    case kMips64Sar:
      if (instr->InputAt(1)->IsRegister()) {
        __ srav(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ sra(i.OutputRegister(), i.InputRegister(0),
               static_cast<uint16_t>(imm));
      }
      break;
    case kMips64Ext:
      __ Ext(i.OutputRegister(), i.InputRegister(0), i.InputInt8(1),
             i.InputInt8(2));
      break;
    case kMips64Ins:
      if (instr->InputAt(1)->IsImmediate() && i.InputInt8(1) == 0) {
        __ Ins(i.OutputRegister(), zero_reg, i.InputInt8(1), i.InputInt8(2));
      } else {
        __ Ins(i.OutputRegister(), i.InputRegister(0), i.InputInt8(1),
               i.InputInt8(2));
      }
      break;
    case kMips64Dext: {
      __ Dext(i.OutputRegister(), i.InputRegister(0), i.InputInt8(1),
              i.InputInt8(2));
      break;
    }
    case kMips64Dins:
      if (instr->InputAt(1)->IsImmediate() && i.InputInt8(1) == 0) {
        __ Dins(i.OutputRegister(), zero_reg, i.InputInt8(1), i.InputInt8(2));
      } else {
        __ Dins(i.OutputRegister(), i.InputRegister(0), i.InputInt8(1),
                i.InputInt8(2));
      }
      break;
    case kMips64Dshl:
      if (instr->InputAt(1)->IsRegister()) {
        __ dsllv(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        if (imm < 32) {
          __ dsll(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
        } else {
          __ dsll32(i.OutputRegister(), i.InputRegister(0),
                    static_cast<uint16_t>(imm - 32));
        }
      }
      break;
    case kMips64Dshr:
      if (instr->InputAt(1)->IsRegister()) {
        __ dsrlv(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        if (imm < 32) {
          __ dsrl(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
        } else {
          __ dsrl32(i.OutputRegister(), i.InputRegister(0),
                    static_cast<uint16_t>(imm - 32));
        }
      }
      break;
    case kMips64Dsar:
      if (instr->InputAt(1)->IsRegister()) {
        __ dsrav(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        if (imm < 32) {
          __ dsra(i.OutputRegister(), i.InputRegister(0), imm);
        } else {
          __ dsra32(i.OutputRegister(), i.InputRegister(0), imm - 32);
        }
      }
      break;
    case kMips64Ror:
      __ Ror(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Dror:
      __ Dror(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kMips64Tst:
      __ And(kScratchReg, i.InputRegister(0), i.InputOperand(1));
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kMips64Cmp:
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kMips64Mov:
      // TODO(plind): Should we combine mov/li like this, or use separate instr?
      //    - Also see x64 ASSEMBLE_BINOP & RegisterOrOperandType
      if (HasRegisterInput(instr, 0)) {
        __ mov(i.OutputRegister(), i.InputRegister(0));
      } else {
        __ li(i.OutputRegister(), i.InputOperand(0));
      }
      break;

    case kMips64CmpS: {
      FPURegister left = i.InputOrZeroSingleRegister(0);
      FPURegister right = i.InputOrZeroSingleRegister(1);
      bool predicate;
      FPUCondition cc =
          FlagsConditionToConditionCmpFPU(&predicate, instr->flags_condition());

      if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
          !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ CompareF32(cc, left, right);
    } break;
    case kMips64AddS:
      // TODO(plind): add special case: combine mult & add.
      __ add_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64SubS:
      __ sub_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64MulS:
      // TODO(plind): add special case: right op is -1.0, see arm port.
      __ mul_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64DivS:
      __ div_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64AbsS:
      if (kArchVariant == kMips64r6) {
        __ abs_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      } else {
        __ mfc1(kScratchReg, i.InputSingleRegister(0));
        __ Dins(kScratchReg, zero_reg, 31, 1);
        __ mtc1(kScratchReg, i.OutputSingleRegister());
      }
      break;
    case kMips64NegS:
      __ Neg_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    case kMips64SqrtS: {
      __ sqrt_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kMips64MaxS:
      __ max_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64MinS:
      __ min_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64CmpD: {
      FPURegister left = i.InputOrZeroDoubleRegister(0);
      FPURegister right = i.InputOrZeroDoubleRegister(1);
      bool predicate;
      FPUCondition cc =
          FlagsConditionToConditionCmpFPU(&predicate, instr->flags_condition());
      if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
          !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }
      __ CompareF64(cc, left, right);
    } break;
    case kMips64AddD:
      // TODO(plind): add special case: combine mult & add.
      __ add_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64SubD:
      __ sub_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64MulD:
      // TODO(plind): add special case: right op is -1.0, see arm port.
      __ mul_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64DivD:
      __ div_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64ModD: {
      // TODO(bmeurer): We should really get rid of this special instruction,
      // and generate a CallAddress instruction instead.
      FrameScope scope(masm(), StackFrame::MANUAL);
      __ PrepareCallCFunction(0, 2, kScratchReg);
      __ MovToFloatParameters(i.InputDoubleRegister(0),
                              i.InputDoubleRegister(1));
      __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2);
      // Move the result in the double result register.
      __ MovFromFloatResult(i.OutputDoubleRegister());
      break;
    }
    case kMips64AbsD:
      if (kArchVariant == kMips64r6) {
        __ abs_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      } else {
        __ dmfc1(kScratchReg, i.InputDoubleRegister(0));
        __ Dins(kScratchReg, zero_reg, 63, 1);
        __ dmtc1(kScratchReg, i.OutputDoubleRegister());
      }
      break;
    case kMips64NegD:
      __ Neg_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kMips64SqrtD: {
      __ sqrt_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kMips64MaxD:
      __ max_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64MinD:
      __ min_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
               i.InputDoubleRegister(1));
      break;
    case kMips64Float64RoundDown: {
      __ Floor_d_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kMips64Float32RoundDown: {
      __ Floor_s_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kMips64Float64RoundTruncate: {
      __ Trunc_d_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kMips64Float32RoundTruncate: {
      __ Trunc_s_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kMips64Float64RoundUp: {
      __ Ceil_d_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kMips64Float32RoundUp: {
      __ Ceil_s_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kMips64Float64RoundTiesEven: {
      __ Round_d_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kMips64Float32RoundTiesEven: {
      __ Round_s_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kMips64Float32Max: {
      FPURegister dst = i.OutputSingleRegister();
      FPURegister src1 = i.InputSingleRegister(0);
      FPURegister src2 = i.InputSingleRegister(1);
      auto ool = zone()->New<OutOfLineFloat32Max>(this, dst, src1, src2);
      __ Float32Max(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kMips64Float64Max: {
      FPURegister dst = i.OutputDoubleRegister();
      FPURegister src1 = i.InputDoubleRegister(0);
      FPURegister src2 = i.InputDoubleRegister(1);
      auto ool = zone()->New<OutOfLineFloat64Max>(this, dst, src1, src2);
      __ Float64Max(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kMips64Float32Min: {
      FPURegister dst = i.OutputSingleRegister();
      FPURegister src1 = i.InputSingleRegister(0);
      FPURegister src2 = i.InputSingleRegister(1);
      auto ool = zone()->New<OutOfLineFloat32Min>(this, dst, src1, src2);
      __ Float32Min(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kMips64Float64Min: {
      FPURegister dst = i.OutputDoubleRegister();
      FPURegister src1 = i.InputDoubleRegister(0);
      FPURegister src2 = i.InputDoubleRegister(1);
      auto ool = zone()->New<OutOfLineFloat64Min>(this, dst, src1, src2);
      __ Float64Min(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kMips64Float64SilenceNaN:
      __ FPUCanonicalizeNaN(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kMips64CvtSD:
      __ cvt_s_d(i.OutputSingleRegister(), i.InputDoubleRegister(0));
      break;
    case kMips64CvtDS:
      __ cvt_d_s(i.OutputDoubleRegister(), i.InputSingleRegister(0));
      break;
    case kMips64CvtDW: {
      FPURegister scratch = kScratchDoubleReg;
      __ mtc1(i.InputRegister(0), scratch);
      __ cvt_d_w(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kMips64CvtSW: {
      FPURegister scratch = kScratchDoubleReg;
      __ mtc1(i.InputRegister(0), scratch);
      __ cvt_s_w(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kMips64CvtSUw: {
      __ Cvt_s_uw(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kMips64CvtSL: {
      FPURegister scratch = kScratchDoubleReg;
      __ dmtc1(i.InputRegister(0), scratch);
      __ cvt_s_l(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kMips64CvtDL: {
      FPURegister scratch = kScratchDoubleReg;
      __ dmtc1(i.InputRegister(0), scratch);
      __ cvt_d_l(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kMips64CvtDUw: {
      __ Cvt_d_uw(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kMips64CvtDUl: {
      __ Cvt_d_ul(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kMips64CvtSUl: {
      __ Cvt_s_ul(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kMips64FloorWD: {
      FPURegister scratch = kScratchDoubleReg;
      __ floor_w_d(scratch, i.InputDoubleRegister(0));
      __ mfc1(i.OutputRegister(), scratch);
      break;
    }
    case kMips64CeilWD: {
      FPURegister scratch = kScratchDoubleReg;
      __ ceil_w_d(scratch, i.InputDoubleRegister(0));
      __ mfc1(i.OutputRegister(), scratch);
      break;
    }
    case kMips64RoundWD: {
      FPURegister scratch = kScratchDoubleReg;
      __ round_w_d(scratch, i.InputDoubleRegister(0));
      __ mfc1(i.OutputRegister(), scratch);
      break;
    }
    case kMips64TruncWD: {
      FPURegister scratch = kScratchDoubleReg;
      // Other arches use round to zero here, so we follow.
      __ trunc_w_d(scratch, i.InputDoubleRegister(0));
      __ mfc1(i.OutputRegister(), scratch);
      if (instr->OutputCount() > 1) {
        // Check for inputs below INT32_MIN and NaN.
        __ li(i.OutputRegister(1), 1);
        __ Move(scratch, static_cast<double>(INT32_MIN));
        __ CompareF64(LE, scratch, i.InputDoubleRegister(0));
        __ LoadZeroIfNotFPUCondition(i.OutputRegister(1));
        __ Move(scratch, static_cast<double>(INT32_MAX) + 1);
        __ CompareF64(LE, scratch, i.InputDoubleRegister(0));
        __ LoadZeroIfFPUCondition(i.OutputRegister(1));
      }
      break;
    }
    case kMips64FloorWS: {
      FPURegister scratch = kScratchDoubleReg;
      __ floor_w_s(scratch, i.InputDoubleRegister(0));
      __ mfc1(i.OutputRegister(), scratch);
      break;
    }
    case kMips64CeilWS: {
      FPURegister scratch = kScratchDoubleReg;
      __ ceil_w_s(scratch, i.InputDoubleRegister(0));
      __ mfc1(i.OutputRegister(), scratch);
      break;
    }
    case kMips64RoundWS: {
      FPURegister scratch = kScratchDoubleReg;
      __ round_w_s(scratch, i.InputDoubleRegister(0));
      __ mfc1(i.OutputRegister(), scratch);
      break;
    }
    case kMips64TruncWS: {
      FPURegister scratch = kScratchDoubleReg;
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      __ trunc_w_s(scratch, i.InputDoubleRegister(0));
      __ mfc1(i.OutputRegister(), scratch);
      if (set_overflow_to_min_i32) {
        // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
        // because INT32_MIN allows easier out-of-bounds detection.
        __ addiu(kScratchReg, i.OutputRegister(), 1);
        __ slt(kScratchReg2, kScratchReg, i.OutputRegister());
        __ Movn(i.OutputRegister(), kScratchReg, kScratchReg2);
      }
      break;
    }
    case kMips64TruncLS: {
      FPURegister scratch = kScratchDoubleReg;
      Register result = kScratchReg;

      bool load_status = instr->OutputCount() > 1;
      // Other arches use round to zero here, so we follow.
      __ trunc_l_s(scratch, i.InputDoubleRegister(0));
      __ dmfc1(i.OutputRegister(), scratch);
      if (load_status) {
        __ cfc1(result, FCSR);
        // Check for overflow and NaNs.
        __ And(result, result,
               (kFCSROverflowCauseMask | kFCSRInvalidOpCauseMask));
        __ Slt(result, zero_reg, result);
        __ xori(result, result, 1);
        __ mov(i.OutputRegister(1), result);
      }
      break;
    }
    case kMips64TruncLD: {
      FPURegister scratch = kScratchDoubleReg;
      Register result = kScratchReg;

      bool set_overflow_to_min_i64 = MiscField::decode(instr->opcode());
      bool load_status = instr->OutputCount() > 1;
      DCHECK_IMPLIES(set_overflow_to_min_i64, instr->OutputCount() == 1);
      // Other arches use round to zero here, so we follow.
      __ trunc_l_d(scratch, i.InputDoubleRegister(0));
      __ dmfc1(i.OutputRegister(0), scratch);
      if (load_status) {
        __ cfc1(result, FCSR);
        // Check for overflow and NaNs.
        __ And(result, result,
               (kFCSROverflowCauseMask | kFCSRInvalidOpCauseMask));
        __ Slt(result, zero_reg, result);
        __ xori(result, result, 1);
        __ mov(i.OutputRegister(1), result);
      }
      if (set_overflow_to_min_i64) {
        // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
        // because INT64_MIN allows easier out-of-bounds detection.
        __ Daddu(kScratchReg, i.OutputRegister(), 1);
        __ slt(kScratchReg2, kScratchReg, i.OutputRegister());
        __ Movn(i.OutputRegister(), kScratchReg, kScratchReg2);
      }
      break;
    }
    case kMips64TruncUwD: {
      FPURegister scratch = kScratchDoubleReg;
      __ Trunc_uw_d(i.OutputRegister(), i.InputDoubleRegister(0), scratch);
      if (instr->OutputCount() > 1) {
        __ li(i.OutputRegister(1), 1);
        __ Move(scratch, static_cast<double>(-1.0));
        __ CompareF64(LT, scratch, i.InputDoubleRegister(0));
        __ LoadZeroIfNotFPUCondition(i.OutputRegister(1));
        __ Move(scratch, static_cast<double>(UINT32_MAX) + 1);
        __ CompareF64(LE, scratch, i.InputDoubleRegister(0));
        __ LoadZeroIfFPUCondition(i.OutputRegister(1));
      }
      break;
    }
    case kMips64TruncUwS: {
      FPURegister scratch = kScratchDoubleReg;
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      __ Trunc_uw_s(i.OutputRegister(), i.InputDoubleRegister(0), scratch);
      if (set_overflow_to_min_i32) {
        // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
        // because 0 allows easier out-of-bounds detection.
        __ addiu(kScratchReg, i.OutputRegister(), 1);
        __ Movz(i.OutputRegister(), zero_reg, kScratchReg);
      }
      break;
    }
    case kMips64TruncUlS: {
      FPURegister scratch = kScratchDoubleReg;
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Trunc_ul_s(i.OutputRegister(), i.InputDoubleRegister(0), scratch,
                    result);
      break;
    }
    case kMips64TruncUlD: {
      FPURegister scratch = kScratchDoubleReg;
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Trunc_ul_d(i.OutputRegister(0), i.InputDoubleRegister(0), scratch,
                    result);
      break;
    }
    case kMips64BitcastDL:
      __ dmfc1(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kMips64BitcastLD:
      __ dmtc1(i.InputRegister(0), i.OutputDoubleRegister());
      break;
    case kMips64Float64ExtractLowWord32:
      __ FmoveLow(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kMips64Float64ExtractHighWord32:
      __ FmoveHigh(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kMips64Float64FromWord32Pair:
      __ Move(i.OutputDoubleRegister(), i.InputRegister(1), i.InputRegister(0));
      break;
    case kMips64Float64InsertLowWord32:
      __ FmoveLow(i.OutputDoubleRegister(), i.InputRegister(1));
      break;
    case kMips64Float64InsertHighWord32:
      __ FmoveHigh(i.OutputDoubleRegister(), i.InputRegister(1));
      break;
    // ... more basic instructions ...

    case kMips64Seb:
      __ seb(i.OutputRegister(), i.InputRegister(0));
      break;
    case kMips64Seh:
      __ seh(i.OutputRegister(), i.InputRegister(0));
      break;
    case kMips64Lbu:
      __ Lbu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Lb:
      __ Lb(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Sb: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ Sb(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kMips64Lhu:
      __ Lhu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Ulhu:
      __ Ulhu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Lh:
      __ Lh(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Ulh:
      __ Ulh(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Sh: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ Sh(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kMips64Ush: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ Ush(i.InputOrZeroRegister(index), mem, kScratchReg);
      break;
    }
    case kMips64Lw:
      __ Lw(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Ulw:
      __ Ulw(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Lwu:
      __ Lwu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Ulwu:
      __ Ulwu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Ld:
      __ Ld(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Uld:
      __ Uld(i.OutputRegister(), i.MemoryOperand());
      break;
    case kMips64Sw: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ Sw(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kMips64Usw: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ Usw(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kMips64Sd: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ Sd(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kMips64Usd: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ Usd(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kMips64Lwc1: {
      __ Lwc1(i.OutputSingleRegister(), i.MemoryOperand());
      break;
    }
    case kMips64Ulwc1: {
      __ Ulwc1(i.OutputSingleRegister(), i.MemoryOperand(), kScratchReg);
      break;
    }
    case kMips64Swc1: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroSingleRegister(index);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }
      __ Swc1(ft, operand);
      break;
    }
    case kMips64Uswc1: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroSingleRegister(index);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }
      __ Uswc1(ft, operand, kScratchReg);
      break;
    }
    case kMips64Ldc1:
      __ Ldc1(i.OutputDoubleRegister(), i.MemoryOperand());
      break;
    case kMips64Uldc1:
      __ Uldc1(i.OutputDoubleRegister(), i.MemoryOperand(), kScratchReg);
      break;
    case kMips64Sdc1: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroDoubleRegister(index);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }
      __ Sdc1(ft, operand);
      break;
    }
    case kMips64Usdc1: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroDoubleRegister(index);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }
      __ Usdc1(ft, operand, kScratchReg);
      break;
    }
    case kMips64Sync: {
      __ sync();
      break;
    }
    case kMips64Push:
      if (instr->InputAt(0)->IsFPRegister()) {
        __ Sdc1(i.InputDoubleRegister(0), MemOperand(sp, -kDoubleSize));
        __ Subu(sp, sp, Operand(kDoubleSize));
        frame_access_state()->IncreaseSPDelta(kDoubleSize / kSystemPointerSize);
      } else {
        __ Push(i.InputRegister(0));
        frame_access_state()->IncreaseSPDelta(1);
      }
      break;
    case kMips64Peek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ Ldc1(i.OutputDoubleRegister(), MemOperand(fp, offset));
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ Lwc1(
              i.OutputSingleRegister(0),
              MemOperand(fp, offset + kLessSignificantWordInDoublewordOffset));
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          __ ld_b(i.OutputSimd128Register(), MemOperand(fp, offset));
        }
      } else {
        __ Ld(i.OutputRegister(0), MemOperand(fp, offset));
      }
      break;
    }
    case kMips64StackClaim: {
      __ Dsubu(sp, sp, Operand(i.InputInt32(0)));
      frame_access_state()->IncreaseSPDelta(i.InputInt32(0) /
                                            kSystemPointerSize);
      break;
    }
    case kMips64StoreToStackSlot: {
      if (instr->InputAt(0)->IsFPRegister()) {
        if (instr->InputAt(0)->IsSimd128Register()) {
          CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
          __ st_b(i.InputSimd128Register(0), MemOperand(sp, i.InputInt32(1)));
        } else {
          __ Sdc1(i.InputDoubleRegister(0), MemOperand(sp, i.InputInt32(1)));
        }
      } else {
        __ Sd(i.InputRegister(0), MemOperand(sp, i.InputInt32(1)));
      }
      break;
    }
    case kMips64ByteSwap64: {
      __ ByteSwapSigned(i.OutputRegister(0), i.InputRegister(0), 8);
      break;
    }
    case kMips64ByteSwap32: {
      __ ByteSwapSigned(i.OutputRegister(0), i.InputRegister(0), 4);
      break;
    }
    case kMips64S128LoadSplat: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      auto sz = static_cast<MSASize>(MiscField::decode(instr->opcode()));
      __ LoadSplat(sz, i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kMips64S128Load8x8S: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register scratch = kSimd128ScratchReg;
      __ Ld(kScratchReg, i.MemoryOperand());
      __ fill_d(dst, kScratchReg);
      __ clti_s_b(scratch, dst, 0);
      __ ilvr_b(dst, scratch, dst);
      break;
    }
    case kMips64S128Load8x8U: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ Ld(kScratchReg, i.MemoryOperand());
      __ fill_d(dst, kScratchReg);
      __ ilvr_b(dst, kSimd128RegZero, dst);
      break;
    }
    case kMips64S128Load16x4S: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register scratch = kSimd128ScratchReg;
      __ Ld(kScratchReg, i.MemoryOperand());
      __ fill_d(dst, kScratchReg);
      __ clti_s_h(scratch, dst, 0);
      __ ilvr_h(dst, scratch, dst);
      break;
    }
    case kMips64S128Load16x4U: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ Ld(kScratchReg, i.MemoryOperand());
      __ fill_d(dst, kScratchReg);
      __ ilvr_h(dst, kSimd128RegZero, dst);
      break;
    }
    case kMips64S128Load32x2S: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register scratch = kSimd128ScratchReg;
      __ Ld(kScratchReg, i.MemoryOperand());
      __ fill_d(dst, kScratchReg);
      __ clti_s_w(scratch, dst, 0);
      __ ilvr_w(dst, scratch, dst);
      break;
    }
    case kMips64S128Load32x2U: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ Ld(kScratchReg, i.MemoryOperand());
      __ fill_d(dst, kScratchReg);
      __ ilvr_w(dst, kSimd128RegZero, dst);
      break;
    }
    case kMips64S128Load32Zero: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ xor_v(dst, dst, dst);
      __ Lwu(kScratchReg, i.MemoryOperand());
      __ insert_w(dst, 0, kScratchReg);
      break;
    }
    case kMips64S128Load64Zero: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ xor_v(dst, dst, dst);
      __ Ld(kScratchReg, i.MemoryOperand());
      __ insert_d(dst, 0, kScratchReg);
      break;
    }
    case kMips64S128LoadLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      auto sz = static_cast<MSASize>(MiscField::decode(instr->opcode()));
      __ LoadLane(sz, dst, i.InputUint8(1), i.MemoryOperand(2));
      break;
    }
    case kMips64S128StoreLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register src = i.InputSimd128Register(0);
      auto sz = static_cast<MSASize>(MiscField::decode(instr->opcode()));
      __ StoreLane(sz, src, i.InputUint8(1), i.MemoryOperand(2));
      break;
    }
    case kAtomicLoadInt8:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lb);
      break;
    case kAtomicLoadUint8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lbu);
      break;
    case kAtomicLoadInt16:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lh);
      break;
    case kAtomicLoadUint16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lhu);
      break;
    case kAtomicLoadWord32:
      if (AtomicWidthField::decode(opcode) == AtomicWidth::kWord32)
        ASSEMBLE_ATOMIC_LOAD_INTEGER(Lw);
      else
        ASSEMBLE_ATOMIC_LOAD_INTEGER(Lwu);
      break;
    case kMips64Word64AtomicLoadUint64:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld);
      break;
    case kAtomicStoreWord8:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Sb);
      break;
    case kAtomicStoreWord16:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Sh);
      break;
    case kAtomicStoreWord32:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Sw);
      break;
    case kMips64StoreCompressTagged:
    case kMips64Word64AtomicStoreWord64:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Sd);
      break;
    case kAtomicExchangeInt8:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll, Sc, true, 8, 32);
      break;
    case kAtomicExchangeUint8:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll, Sc, false, 8, 32);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 8, 64);
          break;
      }
      break;
    case kAtomicExchangeInt16:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll, Sc, true, 16, 32);
      break;
    case kAtomicExchangeUint16:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll, Sc, false, 16, 32);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 16, 64);
          break;
      }
      break;
    case kAtomicExchangeWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(Ll, Sc);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 32, 64);
          break;
      }
      break;
    case kMips64Word64AtomicExchangeUint64:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(Lld, Scd);
      break;
    case kAtomicCompareExchangeInt8:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, true, 8, 32);
      break;
    case kAtomicCompareExchangeUint8:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, false, 8, 32);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 8, 64);
          break;
      }
      break;
    case kAtomicCompareExchangeInt16:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, true, 16, 32);
      break;
    case kAtomicCompareExchangeUint16:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, false, 16, 32);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 16, 64);
          break;
      }
      break;
    case kAtomicCompareExchangeWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          __ sll(i.InputRegister(2), i.InputRegister(2), 0);
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll, Sc);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 32, 64);
          break;
      }
      break;
    case kMips64Word64AtomicCompareExchangeUint64:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Lld, Scd);
      break;
#define ATOMIC_BINOP_CASE(op, inst32, inst64)                          \
  case kAtomic##op##Int8:                                              \
    DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32); \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, true, 8, inst32, 32);            \
    break;                                                             \
  case kAtomic##op##Uint8:                                             \
    switch (AtomicWidthField::decode(opcode)) {                        \
      case AtomicWidth::kWord32:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, false, 8, inst32, 32);       \
        break;                                                         \
      case AtomicWidth::kWord64:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Lld, Scd, false, 8, inst64, 64);     \
        break;                                                         \
    }                                                                  \
    break;                                                             \
  case kAtomic##op##Int16:                                             \
    DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32); \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, true, 16, inst32, 32);           \
    break;                                                             \
  case kAtomic##op##Uint16:                                            \
    switch (AtomicWidthField::decode(opcode)) {                        \
      case AtomicWidth::kWord32:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, false, 16, inst32, 32);      \
        break;                                                         \
      case AtomicWidth::kWord64:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Lld, Scd, false, 16, inst64, 64);    \
        break;                                                         \
    }                                                                  \
    break;                                                             \
  case kAtomic##op##Word32:                                            \
    switch (AtomicWidthField::decode(opcode)) {                        \
      case AtomicWidth::kWord32:                                       \
        ASSEMBLE_ATOMIC_BINOP(Ll, Sc, inst32);                         \
        break;                                                         \
      case AtomicWidth::kWord64:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Lld, Scd, false, 32, inst64, 64);    \
        break;                                                         \
    }                                                                  \
    break;                                                             \
  case kMips64Word64Atomic##op##Uint64:                                \
    ASSEMBLE_ATOMIC_BINOP(Lld, Scd, inst64);                           \
    break;
      ATOMIC_BINOP_CASE(Add, Addu, Daddu)
      ATOMIC_BINOP_CASE(Sub, Subu, Dsubu)
      ATOMIC_BINOP_CASE(And, And, And)
      ATOMIC_BINOP_CASE(Or, Or, Or)
      ATOMIC_BINOP_CASE(Xor, Xor, Xor)
#undef ATOMIC_BINOP_CASE
    case kMips64AssertEqual:
      __ Assert(eq, static_cast<AbortReason>(i.InputOperand(2).immediate()),
                i.InputRegister(0), Operand(i.InputRegister(1)));
      break;
    case kMips64S128Const: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      uint64_t imm1 = make_uint64(i.InputUint32(1), i.InputUint32(0));
      uint64_t imm2 = make_uint64(i.InputUint32(3), i.InputUint32(2));
      __ li(kScratchReg, imm1);
      __ insert_d(dst, 0, kScratchReg);
      __ li(kScratchReg, imm2);
      __ insert_d(dst, 1, kScratchReg);
      break;
    }
    case kMips64S128Zero: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ xor_v(dst, dst, dst);
      break;
    }
    case kMips64S128AllOnes: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ ceq_d(dst, dst, dst);
      break;
    }
    case kMips64I32x4Splat: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fill_w(i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kMips64I32x4ExtractLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ copy_s_w(i.OutputRegister(), i.InputSimd128Register(0),
                  i.InputInt8(1));
      break;
    }
    case kMips64I32x4ReplaceLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      if (src != dst) {
        __ move_v(dst, src);
      }
      __ insert_w(dst, i.InputInt8(1), i.InputRegister(2));
      break;
    }
    case kMips64I32x4Add: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ addv_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I32x4Sub: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ subv_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F64x2Abs: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ bclri_d(i.OutputSimd128Register(), i.InputSimd128Register(0), 63);
      break;
    }
    case kMips64F64x2Neg: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ bnegi_d(i.OutputSimd128Register(), i.InputSimd128Register(0), 63);
      break;
    }
    case kMips64F64x2Sqrt: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fsqrt_d(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kMips64F64x2Add: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      ASSEMBLE_F64X2_ARITHMETIC_BINOP(fadd_d);
      break;
    }
    case kMips64F64x2Sub: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      ASSEMBLE_F64X2_ARITHMETIC_BINOP(fsub_d);
      break;
    }
    case kMips64F64x2Mul: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      ASSEMBLE_F64X2_ARITHMETIC_BINOP(fmul_d);
      break;
    }
    case kMips64F64x2Div: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      ASSEMBLE_F64X2_ARITHMETIC_BINOP(fdiv_d);
      break;
    }
    case kMips64F64x2Min: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      Simd128Register scratch0 = kSimd128RegZero;
      Simd128Register scratch1 = kSimd128ScratchReg;

      // If inputs are -0.0. and +0.0, then write -0.0 to scratch1.
      // scratch1 = (src0 == src1) ?  (src0 | src1) : (src1 | src1).
      __ fseq_d(scratch0, src0, src1);
      __ bsel_v(scratch0, src1, src0);
      __ or_v(scratch1, scratch0, src1);
      // scratch0 = isNaN(src0) ? src0 : scratch1.
      __ fseq_d(scratch0, src0, src0);
      __ bsel_v(scratch0, src0, scratch1);
      // scratch1 = (src0 < scratch0) ? src0 : scratch0.
      __ fslt_d(scratch1, src0, scratch0);
      __ bsel_v(scratch1, scratch0, src0);
      // Canonicalize the result.
      __ fmin_d(dst, scratch1, scratch1);
      break;
    }
    case kMips64F64x2Max: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      Simd128Register scratch0 = kSimd128RegZero;
      Simd128Register scratch1 = kSimd128ScratchReg;

      // If inputs are -0.0. and +0.0, then write +0.0 to scratch1.
      // scratch1 = (src0 == src1) ?  (src0 & src1) : (src1 & src1).
      __ fseq_d(scratch0, src0, src1);
      __ bsel_v(scratch0, src1, src0);
      __ and_v(scratch1, scratch0, src1);
      // scratch0 = isNaN(src0) ? src0 : scratch1.
      __ fseq_d(scratch0, src0, src0);
      __ bsel_v(scratch0, src0, scratch1);
      // scratch1 = (scratch0 < src0) ? src0 : scratch0.
      __ fslt_d(scratch1, scratch0, src0);
      __ bsel_v(scratch1, scratch0, src0);
      // Canonicalize the result.
      __ fmax_d(dst, scratch1, scratch1);
      break;
    }
    case kMips64F64x2Eq: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fceq_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F64x2Ne: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fcune_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64F64x2Lt: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fclt_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F64x2Le: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fcle_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F64x2Splat: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ Move(kScratchReg, i.InputDoubleRegister(0));
      __ fill_d(i.OutputSimd128Register(), kScratchReg);
      break;
    }
    case kMips64F64x2ExtractLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ copy_s_d(kScratchReg, i.InputSimd128Register(0), i.InputInt8(1));
      __ Move(i.OutputDoubleRegister(), kScratchReg);
      break;
    }
    case kMips64F64x2ReplaceLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      __ Move(kScratchReg, i.InputDoubleRegister(2));
      if (dst != src) {
        __ move_v(dst, src);
      }
      __ insert_d(dst, i.InputInt8(1), kScratchReg);
      break;
    }
    case kMips64I64x2Splat: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fill_d(i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kMips64I64x2ExtractLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ copy_s_d(i.OutputRegister(), i.InputSimd128Register(0),
                  i.InputInt8(1));
      break;
    }
    case kMips64F64x2Pmin: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      // dst = rhs < lhs ? rhs : lhs
      __ fclt_d(dst, rhs, lhs);
      __ bsel_v(dst, lhs, rhs);
      break;
    }
    case kMips64F64x2Pmax: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      // dst = lhs < rhs ? rhs : lhs
      __ fclt_d(dst, lhs, rhs);
      __ bsel_v(dst, lhs, rhs);
      break;
    }
    case kMips64F64x2Ceil: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ MSARoundD(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kRoundToPlusInf);
      break;
    }
    case kMips64F64x2Floor: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ MSARoundD(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kRoundToMinusInf);
      break;
    }
    case kMips64F64x2Trunc: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ MSARoundD(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kRoundToZero);
      break;
    }
    case kMips64F64x2NearestInt: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ MSARoundD(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kRoundToNearest);
      break;
    }
    case kMips64F64x2ConvertLowI32x4S: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ilvr_w(kSimd128RegZero, kSimd128RegZero, i.InputSimd128Register(0));
      __ slli_d(kSimd128RegZero, kSimd128RegZero, 32);
      __ srai_d(kSimd128RegZero, kSimd128RegZero, 32);
      __ ffint_s_d(i.OutputSimd128Register(), kSimd128RegZero);
      break;
    }
    case kMips64F64x2ConvertLowI32x4U: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ilvr_w(kSimd128RegZero, kSimd128RegZero, i.InputSimd128Register(0));
      __ ffint_u_d(i.OutputSimd128Register(), kSimd128RegZero);
      break;
    }
    case kMips64F64x2PromoteLowF32x4: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fexupr_d(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kMips64I64x2ReplaceLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      if (src != dst) {
        __ move_v(dst, src);
      }
      __ insert_d(dst, i.InputInt8(1), i.InputRegister(2));
      break;
    }
    case kMips64I64x2Add: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ addv_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I64x2Sub: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ subv_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I64x2Mul: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ mulv_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I64x2Neg: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ subv_d(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I64x2Shl: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_d(kSimd128ScratchReg, i.InputRegister(1));
        __ sll_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ slli_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt6(1));
      }
      break;
    }
    case kMips64I64x2ShrS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_d(kSimd128ScratchReg, i.InputRegister(1));
        __ sra_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ srai_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt6(1));
      }
      break;
    }
    case kMips64I64x2ShrU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_d(kSimd128ScratchReg, i.InputRegister(1));
        __ srl_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ srli_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt6(1));
      }
      break;
    }
    case kMips64I64x2BitMask: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register scratch0 = kSimd128RegZero;
      Simd128Register scratch1 = kSimd128ScratchReg;
      __ srli_d(scratch0, src, 63);
      __ shf_w(scratch1, scratch0, 0x02);
      __ slli_d(scratch1, scratch1, 1);
      __ or_v(scratch0, scratch0, scratch1);
      __ copy_u_b(dst, scratch0, 0);
      break;
    }
    case kMips64I64x2Eq: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ceq_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kMips64I64x2Ne: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ceq_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      __ nor_v(i.OutputSimd128Register(), i.OutputSimd128Register(),
               i.OutputSimd128Register());
      break;
    }
    case kMips64I64x2GtS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ clt_s_d(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I64x2GeS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ cle_s_d(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I64x2Abs: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ add_a_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128RegZero);
      break;
    }
    case kMips64I64x2SConvertI32x4Low: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ ilvr_w(kSimd128ScratchReg, src, src);
      __ slli_d(dst, kSimd128ScratchReg, 32);
      __ srai_d(dst, dst, 32);
      break;
    }
    case kMips64I64x2SConvertI32x4High: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ ilvl_w(kSimd128ScratchReg, src, src);
      __ slli_d(dst, kSimd128ScratchReg, 32);
      __ srai_d(dst, dst, 32);
      break;
    }
    case kMips64I64x2UConvertI32x4Low: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ilvr_w(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I64x2UConvertI32x4High: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ilvl_w(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64ExtMulLow: {
      auto dt = static_cast<MSADataType>(MiscField::decode(instr->opcode()));
      __ ExtMulLow(dt, i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1));
      break;
    }
    case kMips64ExtMulHigh: {
      auto dt = static_cast<MSADataType>(MiscField::decode(instr->opcode()));
      __ ExtMulHigh(dt, i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputSimd128Register(1));
      break;
    }
    case kMips64ExtAddPairwise: {
      auto dt = static_cast<MSADataType>(MiscField::decode(instr->opcode()));
      __ ExtAddPairwise(dt, i.OutputSimd128Register(),
                        i.InputSimd128Register(0));
      break;
    }
    case kMips64F32x4Splat: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ FmoveLow(kScratchReg, i.InputSingleRegister(0));
      __ fill_w(i.OutputSimd128Register(), kScratchReg);
      break;
    }
    case kMips64F32x4ExtractLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ copy_u_w(kScratchReg, i.InputSimd128Register(0), i.InputInt8(1));
      __ FmoveLow(i.OutputSingleRegister(), kScratchReg);
      break;
    }
    case kMips64F32x4ReplaceLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      __ FmoveLow(kScratchReg, i.InputSingleRegister(2));
      if (dst != src) {
        __ move_v(dst, src);
      }
      __ insert_w(dst, i.InputInt8(1), kScratchReg);
      break;
    }
    case kMips64F32x4SConvertI32x4: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ffint_s_w(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kMips64F32x4UConvertI32x4: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ffint_u_w(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4Mul: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ mulv_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I32x4MaxS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ max_s_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I32x4MinS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ min_s_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I32x4Eq: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ceq_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kMips64I32x4Ne: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ ceq_w(dst, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ nor_v(dst, dst, dst);
      break;
    }
    case kMips64I32x4Shl: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_w(kSimd128ScratchReg, i.InputRegister(1));
        __ sll_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ slli_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt5(1));
      }
      break;
    }
    case kMips64I32x4ShrS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_w(kSimd128ScratchReg, i.InputRegister(1));
        __ sra_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ srai_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt5(1));
      }
      break;
    }
    case kMips64I32x4ShrU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_w(kSimd128ScratchReg, i.InputRegister(1));
        __ srl_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ srli_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt5(1));
      }
      break;
    }
    case kMips64I32x4MaxU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ max_u_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I32x4MinU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ min_u_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64S128Select: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      DCHECK(i.OutputSimd128Register() == i.InputSimd128Register(0));
      __ bsel_v(i.OutputSimd128Register(), i.InputSimd128Register(2),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64S128AndNot: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register scratch = kSimd128ScratchReg,
                      dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      __ nor_v(scratch, src1, src1);
      __ and_v(dst, scratch, src0);
      break;
    }
    case kMips64F32x4Abs: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ bclri_w(i.OutputSimd128Register(), i.InputSimd128Register(0), 31);
      break;
    }
    case kMips64F32x4Neg: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ bnegi_w(i.OutputSimd128Register(), i.InputSimd128Register(0), 31);
      break;
    }
    case kMips64F32x4Add: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fadd_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F32x4Sub: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fsub_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F32x4Mul: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fmul_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F32x4Div: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fdiv_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F32x4Max: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      Simd128Register scratch0 = kSimd128RegZero;
      Simd128Register scratch1 = kSimd128ScratchReg;

      // If inputs are -0.0. and +0.0, then write +0.0 to scratch1.
      // scratch1 = (src0 == src1) ?  (src0 & src1) : (src1 & src1).
      __ fseq_w(scratch0, src0, src1);
      __ bsel_v(scratch0, src1, src0);
      __ and_v(scratch1, scratch0, src1);
      // scratch0 = isNaN(src0) ? src0 : scratch1.
      __ fseq_w(scratch0, src0, src0);
      __ bsel_v(scratch0, src0, scratch1);
      // scratch1 = (scratch0 < src0) ? src0 : scratch0.
      __ fslt_w(scratch1, scratch0, src0);
      __ bsel_v(scratch1, scratch0, src0);
      // Canonicalize the result.
      __ fmax_w(dst, scratch1, scratch1);
      break;
    }
    case kMips64F32x4Min: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      Simd128Register scratch0 = kSimd128RegZero;
      Simd128Register scratch1 = kSimd128ScratchReg;

      // If inputs are -0.0. and +0.0, then write -0.0 to scratch1.
      // scratch1 = (src0 == src1) ?  (src0 | src1) : (src1 | src1).
      __ fseq_w(scratch0, src0, src1);
      __ bsel_v(scratch0, src1, src0);
      __ or_v(scratch1, scratch0, src1);
      // scratch0 = isNaN(src0) ? src0 : scratch1.
      __ fseq_w(scratch0, src0, src0);
      __ bsel_v(scratch0, src0, scratch1);
      // scratch1 = (src0 < scratch0) ? src0 : scratch0.
      __ fslt_w(scratch1, src0, scratch0);
      __ bsel_v(scratch1, scratch0, src0);
      // Canonicalize the result.
      __ fmin_w(dst, scratch1, scratch1);
      break;
    }
    case kMips64F32x4Eq: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fceq_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F32x4Ne: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fcune_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64F32x4Lt: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fclt_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F32x4Le: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fcle_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64F32x4Pmin: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      // dst = rhs < lhs ? rhs : lhs
      __ fclt_w(dst, rhs, lhs);
      __ bsel_v(dst, lhs, rhs);
      break;
    }
    case kMips64F32x4Pmax: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register lhs = i.InputSimd128Register(0);
      Simd128Register rhs = i.InputSimd128Register(1);
      // dst = lhs < rhs ? rhs : lhs
      __ fclt_w(dst, lhs, rhs);
      __ bsel_v(dst, lhs, rhs);
      break;
    }
    case kMips64F32x4Ceil: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ MSARoundW(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kRoundToPlusInf);
      break;
    }
    case kMips64F32x4Floor: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ MSARoundW(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kRoundToMinusInf);
      break;
    }
    case kMips64F32x4Trunc: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ MSARoundW(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kRoundToZero);
      break;
    }
    case kMips64F32x4NearestInt: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ MSARoundW(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kRoundToNearest);
      break;
    }
    case kMips64F32x4DemoteF64x2Zero: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ fexdo_w(i.OutputSimd128Register(), kSimd128RegZero,
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4SConvertF32x4: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ftrunc_s_w(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4UConvertF32x4: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ftrunc_u_w(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kMips64F32x4Sqrt: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fsqrt_w(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4Neg: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ subv_w(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4GtS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ clt_s_w(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4GeS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ cle_s_w(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4GtU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ clt_u_w(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4GeU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ cle_u_w(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4Abs: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ asub_s_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  kSimd128RegZero);
      break;
    }
    case kMips64I32x4BitMask: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register scratch0 = kSimd128RegZero;
      Simd128Register scratch1 = kSimd128ScratchReg;
      __ srli_w(scratch0, src, 31);
      __ srli_d(scratch1, scratch0, 31);
      __ or_v(scratch0, scratch0, scratch1);
      __ shf_w(scratch1, scratch0, 0x0E);
      __ slli_d(scratch1, scratch1, 2);
      __ or_v(scratch0, scratch0, scratch1);
      __ copy_u_b(dst, scratch0, 0);
      break;
    }
    case kMips64I32x4DotI16x8S: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ dotp_s_w(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I32x4TruncSatF64x2SZero: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ftrunc_s_d(kSimd128ScratchReg, i.InputSimd128Register(0));
      __ sat_s_d(kSimd128ScratchReg, kSimd128ScratchReg, 31);
      __ pckev_w(i.OutputSimd128Register(), kSimd128RegZero,
                 kSimd128ScratchReg);
      break;
    }
    case kMips64I32x4TruncSatF64x2UZero: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ftrunc_u_d(kSimd128ScratchReg, i.InputSimd128Register(0));
      __ sat_u_d(kSimd128ScratchReg, kSimd128ScratchReg, 31);
      __ pckev_w(i.OutputSimd128Register(), kSimd128RegZero,
                 kSimd128ScratchReg);
      break;
    }
    case kMips64I16x8Splat: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fill_h(i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kMips64I16x8ExtractLaneU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ copy_u_h(i.OutputRegister(), i.InputSimd128Register(0),
                  i.InputInt8(1));
      break;
    }
    case kMips64I16x8ExtractLaneS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ copy_s_h(i.OutputRegister(), i.InputSimd128Register(0),
                  i.InputInt8(1));
      break;
    }
    case kMips64I16x8ReplaceLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      if (src != dst) {
        __ move_v(dst, src);
      }
      __ insert_h(dst, i.InputInt8(1), i.InputRegister(2));
      break;
    }
    case kMips64I16x8Neg: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ subv_h(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I16x8Shl: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_h(kSimd128ScratchReg, i.InputRegister(1));
        __ sll_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ slli_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt4(1));
      }
      break;
    }
    case kMips64I16x8ShrS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_h(kSimd128ScratchReg, i.InputRegister(1));
        __ sra_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ srai_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt4(1));
      }
      break;
    }
    case kMips64I16x8ShrU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_h(kSimd128ScratchReg, i.InputRegister(1));
        __ srl_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ srli_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt4(1));
      }
      break;
    }
    case kMips64I16x8Add: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ addv_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8AddSatS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ adds_s_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8Sub: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ subv_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8SubSatS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ subs_s_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8Mul: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ mulv_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8MaxS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ max_s_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8MinS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ min_s_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8Eq: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ceq_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8Ne: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ ceq_h(dst, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ nor_v(dst, dst, dst);
      break;
    }
    case kMips64I16x8GtS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ clt_s_h(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I16x8GeS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ cle_s_h(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I16x8AddSatU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ adds_u_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8SubSatU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ subs_u_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8MaxU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ max_u_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8MinU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ min_u_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I16x8GtU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ clt_u_h(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I16x8GeU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ cle_u_h(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I16x8RoundingAverageU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ aver_u_h(i.OutputSimd128Register(), i.InputSimd128Register(1),
                  i.InputSimd128Register(0));
      break;
    }
    case kMips64I16x8Abs: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ asub_s_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  kSimd128RegZero);
      break;
    }
    case kMips64I16x8BitMask: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register scratch0 = kSimd128RegZero;
      Simd128Register scratch1 = kSimd128ScratchReg;
      __ srli_h(scratch0, src, 15);
      __ srli_w(scratch1, scratch0, 15);
      __ or_v(scratch0, scratch0, scratch1);
      __ srli_d(scratch1, scratch0, 30);
      __ or_v(scratch0, scratch0, scratch1);
      __ shf_w(scratch1, scratch0, 0x0E);
      __ slli_d(scratch1, scratch1, 4);
      __ or_v(scratch0, scratch0, scratch1);
      __ copy_u_b(dst, scratch0, 0);
      break;
    }
    case kMips64I16x8Q15MulRSatS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ mulr_q_h(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16Splat: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ fill_b(i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kMips64I8x16ExtractLaneU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ copy_u_b(i.OutputRegister(), i.InputSimd128Register(0),
                  i.InputInt8(1));
      break;
    }
    case kMips64I8x16ExtractLaneS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ copy_s_b(i.OutputRegister(), i.InputSimd128Register(0),
                  i.InputInt8(1));
      break;
    }
    case kMips64I8x16ReplaceLane: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      if (src != dst) {
        __ move_v(dst, src);
      }
      __ insert_b(dst, i.InputInt8(1), i.InputRegister(2));
      break;
    }
    case kMips64I8x16Neg: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ subv_b(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I8x16Shl: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_b(kSimd128ScratchReg, i.InputRegister(1));
        __ sll_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ slli_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt3(1));
      }
      break;
    }
    case kMips64I8x16ShrS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_b(kSimd128ScratchReg, i.InputRegister(1));
        __ sra_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ srai_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt3(1));
      }
      break;
    }
    case kMips64I8x16Add: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ addv_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16AddSatS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ adds_s_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16Sub: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ subv_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16SubSatS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ subs_s_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16MaxS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ max_s_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16MinS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ min_s_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16Eq: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ceq_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16Ne: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      __ ceq_b(dst, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ nor_v(dst, dst, dst);
      break;
    }
    case kMips64I8x16GtS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ clt_s_b(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I8x16GeS: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ cle_s_b(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I8x16ShrU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (instr->InputAt(1)->IsRegister()) {
        __ fill_b(kSimd128ScratchReg, i.InputRegister(1));
        __ srl_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kSimd128ScratchReg);
      } else {
        __ srli_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputInt3(1));
      }
      break;
    }
    case kMips64I8x16AddSatU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ adds_u_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16SubSatU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ subs_u_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16MaxU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ max_u_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16MinU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ min_u_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kMips64I8x16GtU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ clt_u_b(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I8x16GeU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ cle_u_b(i.OutputSimd128Register(), i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      break;
    }
    case kMips64I8x16RoundingAverageU: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ aver_u_b(i.OutputSimd128Register(), i.InputSimd128Register(1),
                  i.InputSimd128Register(0));
      break;
    }
    case kMips64I8x16Abs: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ asub_s_b(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  kSimd128RegZero);
      break;
    }
    case kMips64I8x16Popcnt: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ pcnt_b(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kMips64I8x16BitMask: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register scratch0 = kSimd128RegZero;
      Simd128Register scratch1 = kSimd128ScratchReg;
      __ srli_b(scratch0, src, 7);
      __ srli_h(scratch1, scratch0, 7);
      __ or_v(scratch0, scratch0, scratch1);
      __ srli_w(scratch1, scratch0, 14);
      __ or_v(scratch0, scratch0, scratch1);
      __ srli_d(scratch1, scratch0, 28);
      __ or_v(scratch0, scratch0, scratch1);
      __ shf_w(scratch1, scratch0, 0x0E);
      __ ilvev_b(scratch0, scratch1, scratch0);
      __ copy_u_h(dst, scratch0, 0);
      break;
    }
    case kMips64S128And: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ and_v(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kMips64S128Or: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ or_v(i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputSimd128Register(1));
      break;
    }
    case kMips64S128Xor: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(1));
      break;
    }
    case kMips64S128Not: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ nor_v(i.OutputSimd128Register(), i.InputSimd128Register(0),
               i.InputSimd128Register(0));
      break;
    }
    case kMips64V128AnyTrue: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Label all_false;
      __ BranchMSA(&all_false, MSA_BRANCH_V, all_zero,
                   i.InputSimd128Register(0), USE_DELAY_SLOT);
      __ li(dst, 0l);  // branch delay slot
      __ li(dst, 1);
      __ bind(&all_false);
      break;
    }
    case kMips64I64x2AllTrue: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Label all_true;
      __ BranchMSA(&all_true, MSA_BRANCH_D, all_not_zero,
                   i.InputSimd128Register(0), USE_DELAY_SLOT);
      __ li(dst, 1);  // branch delay slot
      __ li(dst, 0l);
      __ bind(&all_true);
      break;
    }
    case kMips64I32x4AllTrue: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Label all_true;
      __ BranchMSA(&all_true, MSA_BRANCH_W, all_not_zero,
                   i.InputSimd128Register(0), USE_DELAY_SLOT);
      __ li(dst, 1);  // branch delay slot
      __ li(dst, 0l);
      __ bind(&all_true);
      break;
    }
    case kMips64I16x8AllTrue: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Label all_true;
      __ BranchMSA(&all_true, MSA_BRANCH_H, all_not_zero,
                   i.InputSimd128Register(0), USE_DELAY_SLOT);
      __ li(dst, 1);  // branch delay slot
      __ li(dst, 0l);
      __ bind(&all_true);
      break;
    }
    case kMips64I8x16AllTrue: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Register dst = i.OutputRegister();
      Label all_true;
      __ BranchMSA(&all_true, MSA_BRANCH_B, all_not_zero,
                   i.InputSimd128Register(0), USE_DELAY_SLOT);
      __ li(dst, 1);  // branch delay slot
      __ li(dst, 0l);
      __ bind(&all_true);
      break;
    }
    case kMips64MsaLd: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ ld_b(i.OutputSimd128Register(), i.MemoryOperand());
      break;
    }
    case kMips64MsaSt: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ st_b(i.InputSimd128Register(2), i.MemoryOperand());
      break;
    }
    case kMips64S32x4InterleaveRight: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [7, 6, 5, 4], src0 = [3, 2, 1, 0]
      // dst = [5, 1, 4, 0]
      __ ilvr_w(dst, src1, src0);
      break;
    }
    case kMips64S32x4InterleaveLeft: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [7, 6, 5, 4], src0 = [3, 2, 1, 0]
      // dst = [7, 3, 6, 2]
      __ ilvl_w(dst, src1, src0);
      break;
    }
    case kMips64S32x4PackEven: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [7, 6, 5, 4], src0 = [3, 2, 1, 0]
      // dst = [6, 4, 2, 0]
      __ pckev_w(dst, src1, src0);
      break;
    }
    case kMips64S32x4PackOdd: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [7, 6, 5, 4], src0 = [3, 2, 1, 0]
      // dst = [7, 5, 3, 1]
      __ pckod_w(dst, src1, src0);
      break;
    }
    case kMips64S32x4InterleaveEven: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [7, 6, 5, 4], src0 = [3, 2, 1, 0]
      // dst = [6, 2, 4, 0]
      __ ilvev_w(dst, src1, src0);
      break;
    }
    case kMips64S32x4InterleaveOdd: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [7, 6, 5, 4], src0 = [3, 2, 1, 0]
      // dst = [7, 3, 5, 1]
      __ ilvod_w(dst, src1, src0);
      break;
    }
    case kMips64S32x4Shuffle: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);

      int32_t shuffle = i.InputInt32(2);

      if (src0 == src1) {
        // Unary S32x4 shuffles are handled with shf.w instruction
        unsigned lane = shuffle & 0xFF;
        if (v8_flags.debug_code) {
          // range of all four lanes, for unary instruction,
          // should belong to the same range, which can be one of these:
          // [0, 3] or [4, 7]
          if (lane >= 4) {
            int32_t shuffle_helper = shuffle;
            for (int i = 0; i < 4; ++i) {
              lane = shuffle_helper & 0xFF;
              CHECK_GE(lane, 4);
              shuffle_helper >>= 8;
            }
          }
        }
        uint32_t i8 = 0;
        for (int i = 0; i < 4; i++) {
          lane = shuffle & 0xFF;
          if (lane >= 4) {
            lane -= 4;
          }
          DCHECK_GT(4, lane);
          i8 |= lane << (2 * i);
          shuffle >>= 8;
        }
        __ shf_w(dst, src0, i8);
      } else {
        // For binary shuffles use vshf.w instruction
        if (dst == src0) {
          __ move_v(kSimd128ScratchReg, src0);
          src0 = kSimd128ScratchReg;
        } else if (dst == src1) {
          __ move_v(kSimd128ScratchReg, src1);
          src1 = kSimd128ScratchReg;
        }

        __ li(kScratchReg, i.InputInt32(2));
        __ insert_w(dst, 0, kScratchReg);
        __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
        __ ilvr_b(dst, kSimd128RegZero, dst);
        __ ilvr_h(dst, kSimd128RegZero, dst);
        __ vshf_w(dst, src1, src0);
      }
      break;
    }
    case kMips64S16x8InterleaveRight: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [15, ... 11, 10, 9, 8], src0 = [7, ... 3, 2, 1, 0]
      // dst = [11, 3, 10, 2, 9, 1, 8, 0]
      __ ilvr_h(dst, src1, src0);
      break;
    }
    case kMips64S16x8InterleaveLeft: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [15, ... 11, 10, 9, 8], src0 = [7, ... 3, 2, 1, 0]
      // dst = [15, 7, 14, 6, 13, 5, 12, 4]
      __ ilvl_h(dst, src1, src0);
      break;
    }
    case kMips64S16x8PackEven: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [15, ... 11, 10, 9, 8], src0 = [7, ... 3, 2, 1, 0]
      // dst = [14, 12, 10, 8, 6, 4, 2, 0]
      __ pckev_h(dst, src1, src0);
      break;
    }
    case kMips64S16x8PackOdd: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [15, ... 11, 10, 9, 8], src0 = [7, ... 3, 2, 1, 0]
      // dst = [15, 13, 11, 9, 7, 5, 3, 1]
      __ pckod_h(dst, src1, src0);
      break;
    }
    case kMips64S16x8InterleaveEven: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [15, ... 11, 10, 9, 8], src0 = [7, ... 3, 2, 1, 0]
      // dst = [14, 6, 12, 4, 10, 2, 8, 0]
      __ ilvev_h(dst, src1, src0);
      break;
    }
    case kMips64S16x8InterleaveOdd: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [15, ... 11, 10, 9, 8], src0 = [7, ... 3, 2, 1, 0]
      // dst = [15, 7, ... 11, 3, 9, 1]
      __ ilvod_h(dst, src1, src0);
      break;
    }
    case kMips64S16x4Reverse: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      // src = [7, 6, 5, 4, 3, 2, 1, 0], dst = [4, 5, 6, 7, 0, 1, 2, 3]
      // shf.df imm field: 0 1 2 3 = 00011011 = 0x1B
      __ shf_h(i.OutputSimd128Register(), i.InputSimd128Register(0), 0x1B);
      break;
    }
    case kMips64S16x2Reverse: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      // src = [7, 6, 5, 4, 3, 2, 1, 0], dst = [6, 7, 4, 5, 3, 2, 0, 1]
      // shf.df imm field: 2 3 0 1 = 10110001 = 0xB1
      __ shf_h(i.OutputSimd128Register(), i.InputSimd128Register(0), 0xB1);
      break;
    }
    case kMips64S8x16InterleaveRight: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [31, ... 19, 18, 17, 16], src0 = [15, ... 3, 2, 1, 0]
      // dst = [23, 7, ... 17, 1, 16, 0]
      __ ilvr_b(dst, src1, src0);
      break;
    }
    case kMips64S8x16InterleaveLeft: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [31, ... 19, 18, 17, 16], src0 = [15, ... 3, 2, 1, 0]
      // dst = [31, 15, ... 25, 9, 24, 8]
      __ ilvl_b(dst, src1, src0);
      break;
    }
    case kMips64S8x16PackEven: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [31, ... 19, 18, 17, 16], src0 = [15, ... 3, 2, 1, 0]
      // dst = [30, 28, ... 6, 4, 2, 0]
      __ pckev_b(dst, src1, src0);
      break;
    }
    case kMips64S8x16PackOdd: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [31, ... 19, 18, 17, 16], src0 = [15, ... 3, 2, 1, 0]
      // dst = [31, 29, ... 7, 5, 3, 1]
      __ pckod_b(dst, src1, src0);
      break;
    }
    case kMips64S8x16InterleaveEven: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [31, ... 19, 18, 17, 16], src0 = [15, ... 3, 2, 1, 0]
      // dst = [30, 14, ... 18, 2, 16, 0]
      __ ilvev_b(dst, src1, src0);
      break;
    }
    case kMips64S8x16InterleaveOdd: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);
      // src1 = [31, ... 19, 18, 17, 16], src0 = [15, ... 3, 2, 1, 0]
      // dst = [31, 15, ... 19, 3, 17, 1]
      __ ilvod_b(dst, src1, src0);
      break;
    }
    case kMips64S8x16Concat: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      DCHECK(dst == i.InputSimd128Register(0));
      __ sldi_b(dst, i.InputSimd128Register(1), i.InputInt4(2));
      break;
    }
    case kMips64I8x16Shuffle: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register(),
                      src0 = i.InputSimd128Register(0),
                      src1 = i.InputSimd128Register(1);

      if (dst == src0) {
        __ move_v(kSimd128ScratchReg, src0);
        src0 = kSimd128ScratchReg;
      } else if (dst == src1) {
        __ move_v(kSimd128ScratchReg, src1);
        src1 = kSimd128ScratchReg;
      }

      int64_t control_low =
          static_cast<int64_t>(i.InputInt32(3)) << 32 | i.InputInt32(2);
      int64_t control_hi =
          static_cast<int64_t>(i.InputInt32(5)) << 32 | i.InputInt32(4);
      __ li(kScratchReg, control_low);
      __ insert_d(dst, 0, kScratchReg);
      __ li(kScratchReg, control_hi);
      __ insert_d(dst, 1, kScratchReg);
      __ vshf_b(dst, src1, src0);
      break;
    }
    case kMips64I8x16Swizzle: {
      Simd128Register dst = i.OutputSimd128Register(),
                      tbl = i.InputSimd128Register(0),
                      ctl = i.InputSimd128Register(1);
      DCHECK(dst != ctl && dst != tbl);
      Simd128Register zeroReg = i.TempSimd128Register(0);
      __ xor_v(zeroReg, zeroReg, zeroReg);
      __ move_v(dst, ctl);
      __ vshf_b(dst, zeroReg, tbl);
      break;
    }
    case kMips64S8x8Reverse: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      // src = [15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0]
      // dst = [8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7]
      // [A B C D] => [B A D C]: shf.w imm: 2 3 0 1 = 10110001 = 0xB1
      // C: [7, 6, 5, 4] => A': [4, 5, 6, 7]: shf.b imm: 00011011 = 0x1B
      __ shf_w(kSimd128ScratchReg, i.InputSimd128Register(0), 0xB1);
      __ shf_b(i.OutputSimd128Register(), kSimd128ScratchReg, 0x1B);
      break;
    }
    case kMips64S8x4Reverse: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      // src = [15, 14, ... 3, 2, 1, 0], dst = [12, 13, 14, 15, ... 0, 1, 2, 3]
      // shf.df imm field: 0 1 2 3 = 00011011 = 0x1B
      __ shf_b(i.OutputSimd128Register(), i.InputSimd128Register(0), 0x1B);
      break;
    }
    case kMips64S8x2Reverse: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      // src = [15, 14, ... 3, 2, 1, 0], dst = [14, 15, 12, 13, ... 2, 3, 0, 1]
      // shf.df imm field: 2 3 0 1 = 10110001 = 0xB1
      __ shf_b(i.OutputSimd128Register(), i.InputSimd128Register(0), 0xB1);
      break;
    }
    case kMips64I32x4SConvertI16x8Low: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ ilvr_h(kSimd128ScratchReg, src, src);
      __ slli_w(dst, kSimd128ScratchReg, 16);
      __ srai_w(dst, dst, 16);
      break;
    }
    case kMips64I32x4SConvertI16x8High: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ ilvl_h(kSimd128ScratchReg, src, src);
      __ slli_w(dst, kSimd128ScratchReg, 16);
      __ srai_w(dst, dst, 16);
      break;
    }
    case kMips64I32x4UConvertI16x8Low: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ilvr_h(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I32x4UConvertI16x8High: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ilvl_h(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I16x8SConvertI8x16Low: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ ilvr_b(kSimd128ScratchReg, src, src);
      __ slli_h(dst, kSimd128ScratchReg, 8);
      __ srai_h(dst, dst, 8);
      break;
    }
    case kMips64I16x8SConvertI8x16High: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src = i.InputSimd128Register(0);
      __ ilvl_b(kSimd128ScratchReg, src, src);
      __ slli_h(dst, kSimd128ScratchReg, 8);
      __ srai_h(dst, dst, 8);
      break;
    }
    case kMips64I16x8SConvertI32x4: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      __ sat_s_w(kSimd128ScratchReg, src0, 15);
      __ sat_s_w(kSimd128RegZero, src1, 15);  // kSimd128RegZero as scratch
      __ pckev_h(dst, kSimd128RegZero, kSimd128ScratchReg);
      break;
    }
    case kMips64I16x8UConvertI32x4: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ max_s_w(kSimd128ScratchReg, kSimd128RegZero, src0);
      __ sat_u_w(kSimd128ScratchReg, kSimd128ScratchReg, 15);
      __ max_s_w(dst, kSimd128RegZero, src1);
      __ sat_u_w(dst, dst, 15);
      __ pckev_h(dst, dst, kSimd128ScratchReg);
      break;
    }
    case kMips64I16x8UConvertI8x16Low: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ilvr_b(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I16x8UConvertI8x16High: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ ilvl_b(i.OutputSimd128Register(), kSimd128RegZero,
                i.InputSimd128Register(0));
      break;
    }
    case kMips64I8x16SConvertI16x8: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      __ sat_s_h(kSimd128ScratchReg, src0, 7);
      __ sat_s_h(kSimd128RegZero, src1, 7);  // kSimd128RegZero as scratch
      __ pckev_b(dst, kSimd128RegZero, kSimd128ScratchReg);
      break;
    }
    case kMips64I8x16UConvertI16x8: {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register src0 = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      __ xor_v(kSimd128RegZero, kSimd128RegZero, kSimd128RegZero);
      __ max_s_h(kSimd128ScratchReg, kSimd128RegZero, src0);
      __ sat_u_h(kSimd128ScratchReg, kSimd128ScratchReg, 7);
      __ max_s_h(dst, kSimd128RegZero, src1);
      __ sat_u_h(dst, dst, 7);
      __ pckev_b(dst, dst, kSimd128ScratchReg);
      break;
    }
  }
  return kSuccess;
}

#define UNSUPPORTED_COND(opcode, condition)                                    \
  StdoutStream{} << "Unsupported " << #opcode << " condition: \"" << condition \
                 << "\"";                                                      \
  UNIMPLEMENTED();

void AssembleBranchToLabels(CodeGenerator* gen, MacroAssembler* masm,
                            Instruction* instr, FlagsCondition condition,
                            Label* tlabel, Label* flabel, bool fallthru) {
#undef __
#define __ masm->
  MipsOperandConverter i(gen, instr);

  // MIPS does not have condition code flags, so compare and branch are
  // implemented differently than on the other arch's. The compare operations
  // emit mips pseudo-instructions, which are handled here by branch
  // instructions that do the actual comparison. Essential that the input
  // registers to compare pseudo-op are not modified before this branch op, as
  // they are tested here.

  if (instr->arch_opcode() == kMips64Tst) {
    Condition cc = FlagsConditionToConditionTst(condition);
    __ Branch(tlabel, cc, kScratchReg, Operand(zero_reg));
  } else if (instr->arch_opcode() == kMips64Dadd ||
             instr->arch_opcode() == kMips64Dsub) {
    Condition cc = FlagsConditionToConditionOvf(condition);
    __ dsra32(kScratchReg, i.OutputRegister(), 0);
    __ sra(kScratchReg2, i.OutputRegister(), 31);
    __ Branch(tlabel, cc, kScratchReg2, Operand(kScratchReg));
  } else if (instr->arch_opcode() == kMips64DaddOvf ||
             instr->arch_opcode() == kMips64DsubOvf) {
    switch (condition) {
      // Overflow occurs if overflow register is negative
      case kOverflow:
        __ Branch(tlabel, lt, kScratchReg, Operand(zero_reg));
        break;
      case kNotOverflow:
        __ Branch(tlabel, ge, kScratchReg, Operand(zero_reg));
        break;
      default:
        UNSUPPORTED_COND(instr->arch_opcode(), condition);
    }
  } else if (instr->arch_opcode() == kMips64MulOvf ||
             instr->arch_opcode() == kMips64DMulOvf) {
    // Overflow occurs if overflow register is not zero
    switch (condition) {
      case kOverflow:
        __ Branch(tlabel, ne, kScratchReg, Operand(zero_reg));
        break;
      case kNotOverflow:
        __ Branch(tlabel, eq, kScratchReg, Operand(zero_reg));
        break;
      default:
        UNSUPPORTED_COND(instr->arch_opcode(), condition);
    }
  } else if (instr->arch_opcode() == kMips64Cmp) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    __ Branch(tlabel, cc, i.InputRegister(0), i.InputOperand(1));
  } else if (instr->arch_opcode() == kArchStackPointerGreaterThan) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    DCHECK((cc == ls) || (cc == hi));
    if (cc == ls) {
      __ xori(i.TempRegister(0), i.TempRegister(0), 1);
    }
    __ Branch(tlabel, ne, i.TempRegister(0), Operand(zero_reg));
  } else if (instr->arch_opcode() == kMips64CmpS ||
             instr->arch_opcode() == kMips64CmpD) {
    bool predicate;
    FlagsConditionToConditionCmpFPU(&predicate, condition);
    if (predicate) {
      __ BranchTrueF(tlabel);
    } else {
      __ BranchFalseF(tlabel);
    }
  } else {
    PrintF("AssembleArchBranch Unimplemented arch_opcode: %d\n",
           instr->arch_opcode());
    UNIMPLEMENTED();
  }
  if (!fallthru) __ Branch(flabel);  // no fallthru to flabel.
#undef __
#define __ masm()->
}

// Assembles branches after an instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;

  AssembleBranchToLabels(this, masm(), instr, branch->condition, tlabel, flabel,
                         branch->fallthru);
}

#undef UNSUPPORTED_COND

void CodeGenerator::AssembleArchDeoptBranch(Instruction* instr,
                                            BranchInfo* branch) {
  AssembleArchBranch(instr, branch);
}

void CodeGenerator::AssembleArchJumpRegardlessOfAssemblyOrder(
    RpoNumber target) {
  __ Branch(GetLabel(target));
}

#if V8_ENABLE_WEBASSEMBLY
void CodeGenerator::AssembleArchTrap(Instruction* instr,
                                     FlagsCondition condition) {
  class OutOfLineTrap final : public OutOfLineCode {
   public:
    OutOfLineTrap(CodeGenerator* gen, Instruction* instr)
        : OutOfLineCode(gen), instr_(instr), gen_(gen) {}
    void Generate() final {
      MipsOperandConverter i(gen_, instr_);
      TrapId trap_id =
          static_cast<TrapId>(i.InputInt32(instr_->InputCount() - 1));
      GenerateCallToTrap(trap_id);
    }

   private:
    void GenerateCallToTrap(TrapId trap_id) {
      gen_->AssembleSourcePosition(instr_);
      // A direct call to a wasm runtime stub defined in this module.
      // Just encode the stub index. This will be patched when the code
      // is added to the native module and copied into wasm code space.
      __ Call(static_cast<Address>(trap_id), RelocInfo::WASM_STUB_CALL);
      ReferenceMap* reference_map =
          gen_->zone()->New<ReferenceMap>(gen_->zone());
      gen_->RecordSafepoint(reference_map);
      if (v8_flags.debug_code) {
        __ stop();
      }
    }
    Instruction* instr_;
    CodeGenerator* gen_;
  };
  auto ool = zone()->New<OutOfLineTrap>(this, instr);
  Label* tlabel = ool->entry();
  AssembleBranchToLabels(this, masm(), instr, condition, tlabel, nullptr, true);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  MipsOperandConverter i(this, instr);

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Register result = i.OutputRegister(instr->OutputCount() - 1);
  // MIPS does not have condition code flags, so compare and branch are
  // implemented differently than on the other arch's. The compare operations
  // emit mips pseudo-instructions, which are checked and handled here.

  if (instr->arch_opcode() == kMips64Tst) {
    Condition cc = FlagsConditionToConditionTst(condition);
    if (cc == eq) {
      __ Sltu(result, kScratchReg, 1);
    } else {
      __ Sltu(result, zero_reg, kScratchReg);
    }
    return;
  } else if (instr->arch_opcode() == kMips64Dadd ||
             instr->arch_opcode() == kMips64Dsub) {
    Condition cc = FlagsConditionToConditionOvf(condition);
    // Check for overflow creates 1 or 0 for result.
    __ dsrl32(kScratchReg, i.OutputRegister(), 31);
    __ srl(kScratchReg2, i.OutputRegister(), 31);
    __ xor_(result, kScratchReg, kScratchReg2);
    if (cc == eq)  // Toggle result for not overflow.
      __ xori(result, result, 1);
    return;
  } else if (instr->arch_opcode() == kMips64DaddOvf ||
             instr->arch_opcode() == kMips64DsubOvf) {
    // Overflow occurs if overflow register is negative
    __ slt(result, kScratchReg, zero_reg);
  } else if (instr->arch_opcode() == kMips64MulOvf ||
             instr->arch_opcode() == kMips64DMulOvf) {
    // Overflow occurs if overflow register is not zero
    __ Sgtu(result, kScratchReg, zero_reg);
  } else if (instr->arch_opcode() == kMips64Cmp) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    __ CompareWord(cc, result, i.InputRegister(0), i.InputOperand(1));
    return;
  } else if (instr->arch_opcode() == kMips64CmpD ||
             instr->arch_opcode() == kMips64CmpS) {
    FPURegister left = i.InputOrZeroDoubleRegister(0);
    FPURegister right = i.InputOrZeroDoubleRegister(1);
    if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
        !__ IsDoubleZeroRegSet()) {
      __ Move(kDoubleRegZero, 0.0);
    }
    bool predicate;
    FlagsConditionToConditionCmpFPU(&predicate, condition);
    if (kArchVariant != kMips64r6) {
      __ li(result, Operand(1));
      if (predicate) {
        __ Movf(result, zero_reg);
      } else {
        __ Movt(result, zero_reg);
      }
    } else {
      if (instr->arch_opcode() == kMips64CmpD) {
        __ dmfc1(result, kDoubleCompareReg);
      } else {
        DCHECK_EQ(kMips64CmpS, instr->arch_opcode());
        __ mfc1(result, kDoubleCompareReg);
      }
      if (predicate) {
        __ And(result, result, 1);  // cmp returns all 1's/0's, use only LSB.
      } else {
        __ Addu(result, result, 1);  // Toggle result for not equal.
      }
    }
    return;
  } else if (instr->arch_opcode() == kArchStackPointerGreaterThan) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    DCHECK((cc == ls) || (cc == hi));
    if (cc == ls) {
      __ xori(i.OutputRegister(), i.TempRegister(0), 1);
    }
    return;
  } else {
    PrintF("AssembleArchBranch Unimplemented arch_opcode is : %d\n",
           instr->arch_opcode());
    TRACE_UNIMPL();
    UNIMPLEMENTED();
  }
}

void CodeGenerator::AssembleArchConditionalBoolean(Instruction* instr) {
  UNREACHABLE();
}

void CodeGenerator::AssembleArchConditionalBranch(Instruction* instr,
                                                  BranchInfo* branch) {
  UNREACHABLE();
}

void CodeGenerator::AssembleArchBinarySearchSwitch(Instruction* instr) {
  MipsOperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }

  UseScratchRegisterScope temps(masm());
  Register scratch = temps.Acquire();
  // The input register may contains dirty data in upper 32 bits, explicitly
  // sign-extend it here.
  __ sll(scratch, input, 0);
  AssembleArchBinarySearchSwitchRange(scratch, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  MipsOperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  size_t const case_count = instr->InputCount() - 2;

  UseScratchRegisterScope temps(masm());
  Register scratch = temps.Acquire();
  // The input register may contains dirty data in upper 32 bits, explicitly
  // sign-extend it here.
  __ sll(scratch, input, 0);
  __ Branch(GetLabel(i.InputRpo(1)), hs, scratch, Operand(case_count));
  __ GenerateSwitchTable(scratch, case_count, [&i, this](size_t index) {
    return GetLabel(i.InputRpo(index + 2));
  });
}

void CodeGenerator::AssembleArchSelect(Instruction* instr,
                                       FlagsCondition condition) {
  UNIMPLEMENTED();
}

void CodeGenerator::FinishFrame(Frame* frame) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const DoubleRegList saves_fpu = call_descriptor->CalleeSavedFPRegisters();
  if (!saves_fpu.is_empty()) {
    int count = saves_fpu.Count();
    DCHECK_EQ(kNumCalleeSavedFPU, count);
    frame->AllocateSavedCalleeRegisterSlots(count *
                                            (kDoubleSize / kSystemPointerSize));
  }

  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {
    int count = saves.Count();
    frame->AllocateSavedCalleeRegisterSlots(count);
  }
}

void CodeGenerator::AssembleConstructFrame() {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  if (frame_access_state()->has_frame()) {
    if (call_descriptor->IsCFunctionCall()) {
#if V8_ENABLE_WEBASSEMBLY
      if (info()->GetOutputStackFrameType() == StackFrame::C_WASM_ENTRY) {
        __ StubPrologue(StackFrame::C_WASM_ENTRY);
        // Reserve stack space for saving the c_entry_fp later.
        __ Dsubu(sp, sp, Operand(kSystemPointerSize));
#else
      // For balance.
      if (false) {
#endif  // V8_ENABLE_WEBASSEMBLY
      } else {
        __ Push(ra, fp);
        __ mov(fp, sp);
      }
    } else if (call_descriptor->IsJSFunctionCall()) {
      __ Prologue();
    } else {
      __ StubPrologue(info()->GetOutputStackFrameType());
#if V8_ENABLE_WEBASSEMBLY
      if (call_descriptor->IsWasmFunctionCall() ||
          call_descriptor->IsWasmImportWrapper() ||
          call_descriptor->IsWasmCapiFunction()) {
        // For import wrappers and C-API functions, this stack slot is only used
        // for printing stack traces in V8. Also, it holds a WasmImportData
        // instead of the instance itself, which is taken care of in the frames
        // accessors.
        __ Push(kWasmInstanceRegister);
      }
      if (call_descriptor->IsWasmImportWrapper()) {
        // If the wrapper is running on a secondary stack, it will switch to the
        // central stack and fill these slots with the central stack pointer and
        // secondary stack limit. Otherwise the slots remain empty.
        static_assert(WasmImportWrapperFrameConstants::kCentralStackSPOffset ==
                      -24);
        static_assert(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset == -32);
        __ Push(zero_reg, zero_reg);
      } else if (call_descriptor->IsWasmCapiFunction()) {
        // Reserve space for saving the PC later.
        __ Dsubu(sp, sp, Operand(kSystemPointerSize));
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    }
  }

  int required_slots =
      frame()->GetTotalFrameSlotCount() - frame()->GetFixedSlotCount();

  if (info()->is_osr()) {
    // TurboFan OSR-compiled functions cannot be entered directly.
    __ Abort(AbortReason::kShouldNotDirectlyEnterOsrFunction);

    // Unoptimized code jumps directly to this entrypoint while the unoptimized
    // frame is still on the stack. Optimized code uses OSR values directly from
    // the unoptimized frame. Thus, all that needs to be done is to allocate the
    // remaining stack slots.
    __ RecordComment("-- OSR entrypoint --");
    osr_pc_offset_ = __ pc_offset();
    required_slots -= osr_helper()->UnoptimizedFrameSlots();
  }

  const RegList saves = call_descriptor->CalleeSavedRegisters();
  const DoubleRegList saves_fpu = call_descriptor->CalleeSavedFPRegisters();

  if (required_slots > 0) {
    DCHECK(frame_access_state()->has_frame());
#if V8_ENABLE_WEBASSEMBLY
    if (info()->IsWasm() && required_slots * kSystemPointerSize > 4 * KB) {
      // For WebAssembly functions with big frames we have to do the stack
      // overflow check before we construct the frame. Otherwise we may not
      // have enough space on the stack to call the runtime for the stack
      // overflow.
      Label done;

      // If the frame is bigger than the stack, we throw the stack overflow
      // exception unconditionally. Thereby we can avoid the integer overflow
      // check in the condition code.
      if (required_slots * kSystemPointerSize < v8_flags.stack_size * KB) {
        __ LoadStackLimit(kScratchReg,
                          MacroAssembler::StackLimitKind::kRealStackLimit);
        __ Daddu(kScratchReg, kScratchReg,
                 Operand(required_slots * kSystemPointerSize));
        __ Branch(&done, uge, sp, Operand(kScratchReg));
      }

      __ Call(static_cast<intptr_t>(Builtin::kWasmStackOverflow),
              RelocInfo::WASM_STUB_CALL);
      // The call does not return, hence we can ignore any references and just
      // define an empty safepoint.
      ReferenceMap* reference_map = zone()->New<ReferenceMap>(zone());
      RecordSafepoint(reference_map);
      if (v8_flags.debug_code) __ stop();

      __ bind(&done);
    }
#endif  // V8_ENABLE_WEBASSEMBLY
  }

  const int returns = frame()->GetReturnSlotCount();

  // Skip callee-saved and return slots, which are pushed below.
  required_slots -= saves.Count();
  required_slots -= saves_fpu.Count();
  required_slots -= returns;
  if (required_slots > 0) {
    __ Dsubu(sp, sp, Operand(required_slots * kSystemPointerSize));
  }

  if (!saves_fpu.is_empty()) {
    // Save callee-saved FPU registers.
    __ MultiPushFPU(saves_fpu);
    DCHECK_EQ(kNumCalleeSavedFPU, saves_fpu.Count());
  }

  if (!saves.is_empty()) {
    // Save callee-saved registers.
    __ MultiPush(saves);
  }

  if (returns != 0) {
    // Create space for returns.
    __ Dsubu(sp, sp, Operand(returns * kSystemPointerSize));
  }

  for (int spill_slot : frame()->tagged_slots()) {
    FrameOffset offset = frame_access_state()->GetFrameOffset(spill_slot);
    DCHECK(offset.from_frame_pointer());
    __ Sd(zero_reg, MemOperand(fp, offset.offset()));
  }
}

void CodeGenerator::AssembleReturn(InstructionOperand* additional_pop_count) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const int returns = frame()->GetReturnSlotCount();
  if (returns != 0) {
    __ Daddu(sp, sp, Operand(returns * kSystemPointerSize));
  }

  // Restore GP registers.
  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {
    __ MultiPop(saves);
  }

  // Restore FPU registers.
  const DoubleRegList saves_fpu = call_descriptor->CalleeSavedFPRegisters();
  if (!saves_fpu.is_empty()) {
    __ MultiPopFPU(saves_fpu);
  }

  MipsOperandConverter g(this, nullptr);

  const int parameter_slots =
      static_cast<int>(call_descriptor->ParameterSlotCount());

  // {aditional_pop_count} is only greater than zero if {parameter_slots = 0}.
  // Check RawMachineAssembler::PopAndReturn.
  if (parameter_slots != 0) {
    if (additional_pop_count->IsImmediate()) {
      DCHECK_EQ(g.ToConstant(additional_pop_count).ToInt32(), 0);
    } else if (v8_flags.debug_code) {
      __ Assert(eq, AbortReason::kUnexpectedAdditionalPopValue,
                g.ToRegister(additional_pop_count),
                Operand(static_cast<int64_t>(0)));
    }
  }

  // Functions with JS linkage have at least one parameter (the receiver).
  // If {parameter_slots} == 0, it means it is a builtin with
  // kDontAdaptArgumentsSentinel, which takes care of JS arguments popping
  // itself.
  const bool drop_jsargs = frame_access_state()->has_frame() &&
                           call_descriptor->IsJSFunctionCall() &&
                           parameter_slots != 0;

  if (call_descriptor->IsCFunctionCall()) {
    AssembleDeconstructFrame();
  } else if (frame_access_state()->has_frame()) {
    // Canonicalize JSFunction return sites for now unless they have an variable
    // number of stack slot pops.
    if (additional_pop_count->IsImmediate() &&
        g.ToConstant(additional_pop_count).ToInt32() == 0) {
      if (return_label_.is_bound()) {
        __ Branch(&return_label_);
        return;
      } else {
        __ bind(&return_label_);
      }
    }
    if (drop_jsargs) {
      // Get the actual argument count
      __ Ld(t0, MemOperand(fp, StandardFrameConstants::kArgCOffset));
    }
    AssembleDeconstructFrame();
  }
  if (drop_jsargs) {
    // We must pop all arguments from the stack (including the receiver). This
    // number of arguments is given by max(1 + argc_reg, parameter_slots).
    if (parameter_slots > 1) {
      __ li(kScratchReg, parameter_slots);
      __ slt(kScratchReg2, t0, kScratchReg);
      __ movn(t0, kScratchReg, kScratchReg2);
    }
    __ Dlsa(sp, sp, t0, kSystemPointerSizeLog2);
  } else if (additional_pop_count->IsImmediate()) {
    int additional_count = g.ToConstant(additional_pop_count).ToInt32();
    __ Drop(parameter_slots + additional_count);
  } else {
    Register pop_reg = g.ToRegister(additional_pop_count);
    __ Drop(parameter_slots);
    __ Dlsa(sp, sp, pop_reg, kSystemPointerSizeLog2);
  }
  __ Ret();
}

void CodeGenerator::FinishCode() {}

void CodeGenerator::PrepareForDeoptimizationExits(
    ZoneDeque<DeoptimizationExit*>* exits) {}

AllocatedOperand CodeGenerator::Push(InstructionOperand* source) {
  auto rep = LocationOperand::cast(source)->representation();
  int new_slots = ElementSizeInPointers(rep);
  MipsOperandConverter g(this, nullptr);
  int last_frame_slot_id =
      frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
  int sp_delta = frame_access_state_->sp_delta();
  int slot_id = last_frame_slot_id + sp_delta + new_slots;
  AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
  if (source->IsRegister()) {
    __ Push(g.ToRegister(source));
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else if (source->IsStackSlot()) {
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    __ Ld(scratch, g.ToMemOperand(source));
    __ Push(scratch);
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else {
    // No push instruction for this operand type. Bump the stack pointer and
    // assemble the move.
    __ Dsubu(sp, sp, Operand(new_slots * kSystemPointerSize));
    frame_access_state()->IncreaseSPDelta(new_slots);
    AssembleMove(source, &stack_slot);
  }
  temp_slots_ += new_slots;
  return stack_slot;
}

void CodeGenerator::Pop(InstructionOperand* dest, MachineRepresentation rep) {
  MipsOperandConverter g(this, nullptr);
  int dropped_slots = ElementSizeInPointers(rep);
  if (dest->IsRegister()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ Pop(g.ToRegister(dest));
  } else if (dest->IsStackSlot()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    __ Pop(scratch);
    __ Sd(scratch, g.ToMemOperand(dest));
  } else {
    int last_frame_slot_id =
        frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
    int sp_delta = frame_access_state_->sp_delta();
    int slot_id = last_frame_slot_id + sp_delta;
    AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
    AssembleMove(&stack_slot, dest);
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ Daddu(sp, sp, Operand(dropped_slots * kSystemPointerSize));
  }
  temp_slots_ -= dropped_slots;
}

void CodeGenerator::PopTempStackSlots() {
  if (temp_slots_ > 0) {
    frame_access_state()->IncreaseSPDelta(-temp_slots_);
    __ Daddu(sp, sp, Operand(temp_slots_ * kSystemPointerSize));
    temp_slots_ = 0;
  }
}

void CodeGenerator::MoveToTempLocation(InstructionOperand* source,
                                       MachineRepresentation rep) {
  // Must be kept in sync with {MoveTempLocationTo}.
  DCHECK(!source->IsImmediate());
  move_cycle_.temps.emplace(masm());
  auto& temps = *move_cycle_.temps;
  // Temporarily exclude the reserved scratch registers while we pick one to
  // resolve the move cycle. Re-include them immediately afterwards as they
  // might be needed for the move to the temp location.
  temps.Exclude(move_cycle_.scratch_regs);
  if (!IsFloatingPoint(rep)) {
    if (temps.hasAvailable()) {
      Register scratch = move_cycle_.temps->Acquire();
      move_cycle_.scratch_reg.emplace(scratch);
    }
  }

  temps.Include(move_cycle_.scratch_regs);

  if (move_cycle_.scratch_reg.has_value()) {
    // A scratch register is available for this rep.
    // auto& scratch_reg = *move_cycle_.scratch_reg;
    AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                             move_cycle_.scratch_reg->code());
    AssembleMove(source, &scratch);
  } else {
    // The scratch registers are blocked by pending moves. Use the stack
    // instead.
    Push(source);
  }
}

void CodeGenerator::MoveTempLocationTo(InstructionOperand* dest,
                                       MachineRepresentation rep) {
  if (move_cycle_.scratch_reg.has_value()) {
    // auto& scratch_reg = *move_cycle_.scratch_reg;
    AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                             move_cycle_.scratch_reg->code());
    AssembleMove(&scratch, dest);
  } else {
    Pop(dest, rep);
  }
  // Restore the default state to release the {UseScratchRegisterScope} and to
  // prepare for the next cycle.
  move_cycle_ = MoveCycleState();
}

void CodeGenerator::SetPendingMove(MoveOperands* move) {
  InstructionOperand* src = &move->source();
  InstructionOperand* dst = &move->destination();
  UseScratchRegisterScope temps(masm());
  if (src->IsConstant() && dst->IsFPLocationOperand()) {
    Register temp = temps.Acquire();
    move_cycle_.scratch_regs.set(temp);
  } else if (src->IsAnyStackSlot() || dst->IsAnyStackSlot()) {
    MipsOperandConverter g(this, nullptr);
    bool src_need_scratch = false;
    bool dst_need_scratch = false;
    if (src->IsAnyStackSlot()) {
      MemOperand src_mem = g.ToMemOperand(src);
      src_need_scratch =
          (!is_int16(src_mem.offset())) || (((src_mem.offset() & 0b111) != 0) &&
                                            !is_int16(src_mem.offset() + 4));
    }
    if (dst->IsAnyStackSlot()) {
      MemOperand dst_mem = g.ToMemOperand(dst);
      dst_need_scratch =
          (!is_int16(dst_mem.offset())) || (((dst_mem.offset() & 0b111) != 0) &&
                                            !is_int16(dst_mem.offset() + 4));
    }
    if (src_need_scratch || dst_need_scratch) {
      Register temp = temps.Acquire();
      move_cycle_.scratch_regs.set(temp);
    }
  }
}

namespace {

bool Is32BitOperand(InstructionOperand* operand) {
  DCHECK(operand->IsStackSlot() || operand->IsRegister());
  MachineRepresentation mr = LocationOperand::cast(operand)->representation();
  return mr == MachineRepresentation::kWord32 ||
         mr == MachineRepresentation::kCompressed ||
         mr == MachineRepresentation::kCompressedPointer;
}

// When we need only 32 bits, move only 32 bits, otherwise the destination
// register' upper 32 bits may contain dirty data.
bool Use32BitMove(InstructionOperand* source, InstructionOperand* destination) {
  return Is32BitOperand(source) && Is32BitOperand(destination);
}

}  // namespace

void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  MipsOperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      __ mov(g.ToRegister(destination), src);
    } else {
      __ Sd(src, g.ToMemOperand(destination));
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsRegister()) {
      if (Use32BitMove(source, destination)) {
        __ Lw(g.ToRegister(destination), src);
      } else {
        __ Ld(g.ToRegister(destination), src);
      }
    } else {
      Register temp = kScratchReg;
      __ Ld(temp, src);
      __ Sd(temp, g.ToMemOperand(destination));
    }
  } else if (source->IsConstant()) {
    Constant src = g.ToConstant(source);
    if (destination->IsRegister() || destination->IsStackSlot()) {
      Register dst =
          destination->IsRegister() ? g.ToRegister(destination) : kScratchReg;
      switch (src.type()) {
        case Constant::kInt32:
          __ li(dst, Operand(src.ToInt32(), src.rmode()));
          break;
        case Constant::kFloat32:
          __ li(dst, Operand::EmbeddedNumber(src.ToFloat32()));
          break;
        case Constant::kInt64:
          __ li(dst, Operand(src.ToInt64(), src.rmode()));
          break;
        case Constant::kFloat64:
          __ li(dst, Operand::EmbeddedNumber(src.ToFloat64().value()));
          break;
        case Constant::kExternalReference:
          __ li(dst, src.ToExternalReference());
          break;
        case Constant::kHeapObject: {
          Handle<HeapObject> src_object = src.ToHeapObject();
          RootIndex index;
          if (IsMaterializableFromRoot(src_object, &index)) {
            __ LoadRoot(dst, index);
          } else {
            __ li(dst, src_object);
          }
          break;
        }
        case Constant::kCompressedHeapObject:
          UNREACHABLE();
        case Constant::kRpoNumber:
          UNREACHABLE();  // TODO(titzer): loading RPO numbers on mips64.
      }
      if (destination->IsStackSlot()) __ Sd(dst, g.ToMemOperand(destination));
    } else if (src.type() == Constant::kFloat32) {
      if (destination->IsFPStackSlot()) {
        MemOperand dst = g.ToMemOperand(destination);
        if (base::bit_cast<int32_t>(src.ToFloat32()) == 0) {
          __ Sd(zero_reg, dst);
        } else {
          __ li(kScratchReg, Operand(base::bit_cast<int32_t>(src.ToFloat32())));
          __ Sd(kScratchReg, dst);
        }
      } else {
        DCHECK(destination->IsFPRegister());
        FloatRegister dst = g.ToSingleRegister(destination);
        __ Move(dst, src.ToFloat32());
      }
    } else {
      DCHECK_EQ(Constant::kFloat64, src.type());
      DoubleRegister dst = destination->IsFPRegister()
                               ? g.ToDoubleRegister(destination)
                               : kScratchDoubleReg;
      __ Move(dst, src.ToFloat64().value());
      if (destination->IsFPStackSlot()) {
        __ Sdc1(dst, g.ToMemOperand(destination));
      }
    }
  } else if (source->IsFPRegister()) {
    MachineRepresentation rep = LocationOperand::cast(source)->representation();
    if (rep == MachineRepresentation::kSimd128) {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      MSARegister src = g.ToSimd128Register(source);
      if (destination->IsSimd128Register()) {
        MSARegister dst = g.ToSimd128Register(destination);
        __ move_v(dst, src);
      } else {
        DCHECK(destination->IsSimd128StackSlot());
        __ st_b(src, g.ToMemOperand(destination));
      }
    } else {
      FPURegister src = g.ToDoubleRegister(source);
      if (destination->IsFPRegister()) {
        FPURegister dst = g.ToDoubleRegister(destination);
        __ Move(dst, src);
      } else {
        DCHECK(destination->IsFPStackSlot());
        __ Sdc1(src, g.ToMemOperand(destination));
      }
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPRegister() || destination->IsFPStackSlot());
    MemOperand src = g.ToMemOperand(source);
    MachineRepresentation rep = LocationOperand::cast(source)->representation();
    if (rep == MachineRepresentation::kSimd128) {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      if (destination->IsSimd128Register()) {
        __ ld_b(g.ToSimd128Register(destination), src);
      } else {
        DCHECK(destination->IsSimd128StackSlot());
        MSARegister temp = kSimd128ScratchReg;
        __ ld_b(temp, src);
        __ st_b(temp, g.ToMemOperand(destination));
      }
    } else {
      if (destination->IsFPRegister()) {
        __ Ldc1(g.ToDoubleRegister(destination), src);
      } else {
        DCHECK(destination->IsFPStackSlot());
        FPURegister temp = kScratchDoubleReg;
        __ Ldc1(temp, src);
        __ Sdc1(temp, g.ToMemOperand(destination));
      }
    }
  } else {
    UNREACHABLE();
  }
}

void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  MipsOperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    // Register-register.
    Register temp = kScratchReg;
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      Register dst = g.ToRegister(destination);
      __ Move(temp, src);
      __ Move(src, dst);
      __ Move(dst, temp);
    } else {
      DCHECK(destination->IsStackSlot());
      MemOperand dst = g.ToMemOperand(destination);
      __ mov(temp, src);
      __ Ld(src, dst);
      __ Sd(temp, dst);
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsStackSlot());
    Register temp_0 = kScratchReg;
    Register temp_1 = kScratchReg2;
    MemOperand src = g.ToMemOperand(source);
    MemOperand dst = g.ToMemOperand(destination);
    __ Ld(temp_0, src);
    __ Ld(temp_1, dst);
    __ Sd(temp_0, dst);
    __ Sd(temp_1, src);
  } else if (source->IsFPRegister()) {
    MachineRepresentation rep = LocationOperand::cast(source)->representation();
    if (rep == MachineRepresentation::kSimd128) {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      MSARegister temp = kSimd128ScratchReg;
      MSARegister src = g.ToSimd128Register(source);
      if (destination->IsSimd128Register()) {
        MSARegister dst = g.ToSimd128Register(destination);
        __ move_v(temp, src);
        __ move_v(src, dst);
        __ move_v(dst, temp);
      } else {
        DCHECK(destination->IsSimd128StackSlot());
        MemOperand dst = g.ToMemOperand(destination);
        __ move_v(temp, src);
        __ ld_b(src, dst);
        __ st_b(temp, dst);
      }
    } else {
      FPURegister temp = kScratchDoubleReg;
      FPURegister src = g.ToDoubleRegister(source);
      if (destination->IsFPRegister()) {
        FPURegister dst = g.ToDoubleRegister(destination);
        __ Move(temp, src);
        __ Move(src, dst);
        __ Move(dst, temp);
      } else {
        DCHECK(destination->IsFPStackSlot());
        MemOperand dst = g.ToMemOperand(destination);
        __ Move(temp, src);
        __ Ldc1(src, dst);
        __ Sdc1(temp, dst);
      }
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPStackSlot());
    Register temp_0 = kScratchReg;
    MemOperand src0 = g.ToMemOperand(source);
    MemOperand src1(src0.rm(), src0.offset() + kInt64Size);
    MemOperand dst0 = g.ToMemOperand(destination);
    MemOperand dst1(dst0.rm(), dst0.offset() + kInt64Size);
    MachineRepresentation rep = LocationOperand::cast(source)->representation();
    if (rep == MachineRepresentation::kSimd128) {
      CpuFeatureScope msa_scope(masm(), MIPS_SIMD);
      MSARegister temp_1 = kSimd128ScratchReg;
      __ ld_b(temp_1, dst0);  // Save destination in temp_1.
      __ Ld(temp_0, src0);    // Then use temp_0 to copy source to destination.
      __ Sd(temp_0, dst0);
      __ Ld(temp_0, src1);
      __ Sd(temp_0, dst1);
      __ st_b(temp_1, src0);
    } else {
      FPURegister temp_1 = kScratchDoubleReg;
      __ Ldc1(temp_1, dst0);  // Save destination in temp_1.
      __ Ld(temp_0, src0);    // Then use temp_0 to copy source to destination.
      __ Sdc1(temp_1, src0);
      __ Sd(temp_0, dst0);
    }
  } else {
    // No other combinations are possible.
    UNREACHABLE();
  }
}

void CodeGenerator::AssembleJumpTable(Label** targets, size_t target_count) {
  // On 64-bit MIPS we emit the jump tables inline.
  UNREACHABLE();
}

#undef ASSEMBLE_ATOMIC_LOAD_INTEGER
#undef ASSEMBLE_ATOMIC_STORE_INTEGER
#undef ASSEMBLE_ATOMIC_BINOP
#undef ASSEMBLE_ATOMIC_BINOP_EXT
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER
#undef ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT
#undef ASSEMBLE_IEEE754_BINOP
#undef ASSEMBLE_IEEE754_UNOP
#undef ASSEMBLE_F64X2_ARITHMETIC_BINOP

#undef TRACE_MSG
#undef TRACE_UNIMPL
#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
