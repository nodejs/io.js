// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/loong64/constants-loong64.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler.h"
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

// TODO(LOONG_dev): consider renaming these macros.
#define TRACE_MSG(msg)                                                      \
  PrintF("code_gen: \'%s\' in function %s at line %d\n", msg, __FUNCTION__, \
         __LINE__)

#define TRACE_UNIMPL()                                            \
  PrintF("UNIMPLEMENTED code_generator_loong64: %s at line %d\n", \
         __FUNCTION__, __LINE__)

// Adds Loong64-specific methods to convert InstructionOperands.
class Loong64OperandConverter final : public InstructionOperandConverter {
 public:
  Loong64OperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  FloatRegister OutputSingleRegister(size_t index = 0) {
    return ToSingleRegister(instr_->OutputAt(index));
  }

  FloatRegister InputSingleRegister(size_t index) {
    return ToSingleRegister(instr_->InputAt(index));
  }

  FloatRegister ToSingleRegister(InstructionOperand* op) {
    // Single (Float) and Double register namespace is same on LOONG64,
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
      case Constant::kCompressedHeapObject: {
        RootIndex root_index;
        if (gen_->isolate()->roots_table().IsRootHandle(constant.ToHeapObject(),
                                                        &root_index)) {
          CHECK(COMPRESS_POINTERS_BOOL);
          CHECK(V8_STATIC_ROOTS_BOOL || !gen_->isolate()->bootstrapper());
          Tagged_t ptr =
              MacroAssemblerBase::ReadOnlyRootPtr(root_index, gen_->isolate());
          return Operand(ptr);
        }
        return Operand(constant.ToHeapObject());
      }
      case Constant::kExternalReference:
      case Constant::kHeapObject:
        break;
      case Constant::kRpoNumber:
        UNREACHABLE();  // TODO(titzer): RPO immediates on loong64?
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
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1));
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
  OutOfLineRecordWrite(
      CodeGenerator* gen, Register object, Operand offset, Register value,
      RecordWriteMode mode, StubCallMode stub_mode,
      IndirectPointerTag indirect_pointer_tag = kIndirectPointerNullTag)
      : OutOfLineCode(gen),
        object_(object),
        offset_(offset),
        value_(value),
        mode_(mode),
#if V8_ENABLE_WEBASSEMBLY
        stub_mode_(stub_mode),
#endif  // V8_ENABLE_WEBASSEMBLY
        must_save_lr_(!gen->frame_access_state()->has_frame()),
        zone_(gen->zone()),
        indirect_pointer_tag_(indirect_pointer_tag) {
  }

  void Generate() final {
    // When storing an indirect pointer, the value will always be a
    // full/decompressed pointer.
    if (COMPRESS_POINTERS_BOOL &&
        mode_ != RecordWriteMode::kValueIsIndirectPointer) {
      __ DecompressTagged(value_, value_);
    }

    __ CheckPageFlag(value_, MemoryChunk::kPointersToHereAreInterestingMask, eq,
                     exit());

    SaveFPRegsMode const save_fp_mode = frame()->DidAllocateDoubleRegisters()
                                            ? SaveFPRegsMode::kSave
                                            : SaveFPRegsMode::kIgnore;
    if (must_save_lr_) {
      // We need to save and restore ra if the frame was elided.
      __ Push(ra);
    }
    if (mode_ == RecordWriteMode::kValueIsEphemeronKey) {
      __ CallEphemeronKeyBarrier(object_, offset_, save_fp_mode);
    } else if (mode_ == RecordWriteMode::kValueIsIndirectPointer) {
      DCHECK(IsValidIndirectPointerTag(indirect_pointer_tag_));
      __ CallIndirectPointerBarrier(object_, offset_, save_fp_mode,
                                    indirect_pointer_tag_);
#if V8_ENABLE_WEBASSEMBLY
    } else if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      // A direct call to a wasm runtime stub defined in this module.
      // Just encode the stub index. This will be patched when the code
      // is added to the native module and copied into wasm code space.
      __ CallRecordWriteStubSaveRegisters(object_, offset_, save_fp_mode,
                                          StubCallMode::kCallWasmRuntimeStub);
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      __ CallRecordWriteStubSaveRegisters(object_, offset_, save_fp_mode);
    }
    if (must_save_lr_) {
      __ Pop(ra);
    }
  }

 private:
  Register const object_;
  Operand const offset_;
  Register const value_;
  RecordWriteMode const mode_;
#if V8_ENABLE_WEBASSEMBLY
  StubCallMode const stub_mode_;
#endif  // V8_ENABLE_WEBASSEMBLY
  bool must_save_lr_;
  Zone* zone_;
  IndirectPointerTag indirect_pointer_tag_;
};

#define CREATE_OOL_CLASS(ool_name, masm_ool_name, T)                 \
  class ool_name final : public OutOfLineCode {                      \
   public:                                                           \
    ool_name(CodeGenerator* gen, T dst, T src1, T src2)              \
        : OutOfLineCode(gen), dst_(dst), src1_(src1), src2_(src2) {} \
                                                                     \
    void Generate() final { __ masm_ool_name(dst_, src1_, src2_); }  \
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

#if V8_ENABLE_WEBASSEMBLY
class WasmOutOfLineTrap : public OutOfLineCode {
 public:
  WasmOutOfLineTrap(CodeGenerator* gen, Instruction* instr)
      : OutOfLineCode(gen), gen_(gen), instr_(instr) {}
  void Generate() override {
    Loong64OperandConverter i(gen_, instr_);
    TrapId trap_id =
        static_cast<TrapId>(i.InputInt32(instr_->InputCount() - 1));
    GenerateCallToTrap(trap_id);
  }

 protected:
  CodeGenerator* gen_;

  void GenerateWithTrapId(TrapId trap_id) { GenerateCallToTrap(trap_id); }

 private:
  void GenerateCallToTrap(TrapId trap_id) {
    gen_->AssembleSourcePosition(instr_);
    // A direct call to a wasm runtime stub defined in this module.
    // Just encode the stub index. This will be patched when the code
    // is added to the native module and copied into wasm code space.
    __ Call(static_cast<Address>(trap_id), RelocInfo::WASM_STUB_CALL);
    ReferenceMap* reference_map = gen_->zone()->New<ReferenceMap>(gen_->zone());
    gen_->RecordSafepoint(reference_map);
    __ AssertUnreachable(AbortReason::kUnexpectedReturnFromWasmTrap);
  }

  Instruction* instr_;
};

void RecordTrapInfoIfNeeded(Zone* zone, CodeGenerator* codegen,
                            InstructionCode opcode, Instruction* instr,
                            int pc) {
  const MemoryAccessMode access_mode = AccessModeField::decode(opcode);
  if (access_mode == kMemoryAccessProtectedMemOutOfBounds ||
      access_mode == kMemoryAccessProtectedNullDereference) {
    ReferenceMap* reference_map =
        codegen->zone()->New<ReferenceMap>(codegen->zone());
    // The safepoint has to be recorded at the return address of a call. Address
    // we use as the fake return address in the case of the trap handler is the
    // fault address (here `pc`) + 1. Therefore the safepoint here has to be
    // recorded at pc + 1;
    codegen->RecordSafepoint(reference_map, pc + 1);
    codegen->RecordProtectedInstruction(pc);
  }
}
#else
void RecordTrapInfoIfNeeded(Zone* zone, CodeGenerator* codegen,
                            InstructionCode opcode, Instruction* instr,
                            int pc) {
  DCHECK_EQ(kMemoryAccessDirect, AccessModeField::decode(opcode));
}
#endif  // V8_ENABLE_WEBASSEMBLY

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
      return CEQ;
    case kNotEqual:
      *predicate = false;
      return CEQ;
    case kUnsignedLessThan:
    case kFloatLessThan:
      *predicate = true;
      return CLT;
    case kUnsignedGreaterThanOrEqual:
      *predicate = false;
      return CLT;
    case kUnsignedLessThanOrEqual:
    case kFloatLessThanOrEqual:
      *predicate = true;
      return CLE;
    case kUnsignedGreaterThan:
      *predicate = false;
      return CLE;
    case kFloatGreaterThan:
      *predicate = false;
      return CULE;
    case kFloatGreaterThanOrEqual:
      *predicate = false;
      return CULT;
    case kFloatLessThanOrUnordered:
      *predicate = true;
      return CULT;
    case kFloatGreaterThanOrUnordered:
      *predicate = false;
      return CLE;
    case kFloatGreaterThanOrEqualOrUnordered:
      *predicate = false;
      return CLT;
    case kFloatLessThanOrEqualOrUnordered:
      *predicate = true;
      return CULE;
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

#define ASSEMBLE_ATOMIC_LOAD_INTEGER(asm_instr)                          \
  do {                                                                   \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
    __ asm_instr(i.OutputRegister(), i.MemoryOperand());                 \
    __ dbar(0);                                                          \
  } while (0)

// TODO(LOONG_dev): remove second dbar?
#define ASSEMBLE_ATOMIC_STORE_INTEGER(asm_instr)                         \
  do {                                                                   \
    __ dbar(0);                                                          \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset()); \
    __ asm_instr(i.InputOrZeroRegister(2), i.MemoryOperand());           \
    __ dbar(0);                                                          \
  } while (0)

// only use for sub_w and sub_d
#define ASSEMBLE_ATOMIC_BINOP(load_linked, store_conditional, bin_instr)       \
  do {                                                                         \
    Label binop;                                                               \
    __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    __ dbar(0);                                                                \
    __ bind(&binop);                                                           \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ bin_instr(i.TempRegister(1), i.OutputRegister(0),                       \
                 Operand(i.InputRegister(2)));                                 \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&binop, eq, i.TempRegister(1), Operand(zero_reg));          \
    __ dbar(0);                                                                \
  } while (0)

// TODO(LOONG_dev): remove second dbar?
#define ASSEMBLE_ATOMIC_BINOP_EXT(load_linked, store_conditional, sign_extend, \
                                  size, bin_instr, representation)             \
  do {                                                                         \
    Label binop;                                                               \
    __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(3), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(3), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Sub_d(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(3)));                                      \
    __ slli_w(i.TempRegister(3), i.TempRegister(3), 3);                        \
    __ dbar(0);                                                                \
    __ bind(&binop);                                                           \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.TempRegister(1), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(1), i.TempRegister(3),  \
                   size, sign_extend);                                         \
    __ bin_instr(i.TempRegister(2), i.OutputRegister(0),                       \
                 Operand(i.InputRegister(2)));                                 \
    __ InsertBits(i.TempRegister(1), i.TempRegister(2), i.TempRegister(3),     \
                  size);                                                       \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&binop, eq, i.TempRegister(1), Operand(zero_reg));          \
    __ dbar(0);                                                                \
  } while (0)

// TODO(LOONG_dev): remove second dbar?
#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(                                  \
    load_linked, store_conditional, sign_extend, size, representation)         \
  do {                                                                         \
    Label exchange;                                                            \
    __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Sub_d(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(1)));                                      \
    __ slli_w(i.TempRegister(1), i.TempRegister(1), 3);                        \
    __ dbar(0);                                                                \
    __ bind(&exchange);                                                        \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.TempRegister(2), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(2), i.TempRegister(1),  \
                   size, sign_extend);                                         \
    __ InsertBits(i.TempRegister(2), i.InputRegister(2), i.TempRegister(1),    \
                  size);                                                       \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&exchange, eq, i.TempRegister(2), Operand(zero_reg));       \
    __ dbar(0);                                                                \
  } while (0)

// TODO(LOONG_dev): remove second dbar?
#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(load_linked,                  \
                                                 store_conditional)            \
  do {                                                                         \
    Label compareExchange;                                                     \
    Label exit;                                                                \
    __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    __ dbar(0);                                                                \
    __ bind(&compareExchange);                                                 \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ BranchShort(&exit, ne, i.InputRegister(2),                              \
                   Operand(i.OutputRegister(0)));                              \
    __ mov(i.TempRegister(2), i.InputRegister(3));                             \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&compareExchange, eq, i.TempRegister(2),                    \
                   Operand(zero_reg));                                         \
    __ bind(&exit);                                                            \
    __ dbar(0);                                                                \
  } while (0)

// TODO(LOONG_dev): remove second dbar?
#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(                          \
    load_linked, store_conditional, sign_extend, size, representation)         \
  do {                                                                         \
    Label compareExchange;                                                     \
    Label exit;                                                                \
    __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));       \
    if (representation == 32) {                                                \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x3);                      \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ andi(i.TempRegister(1), i.TempRegister(0), 0x7);                      \
    }                                                                          \
    __ Sub_d(i.TempRegister(0), i.TempRegister(0),                             \
             Operand(i.TempRegister(1)));                                      \
    __ slli_w(i.TempRegister(1), i.TempRegister(1), 3);                        \
    __ dbar(0);                                                                \
    __ bind(&compareExchange);                                                 \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
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
    __ dbar(0);                                                                \
  } while (0)

#define ASSEMBLE_IEEE754_BINOP(name)                                        \
  do {                                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                           \
    UseScratchRegisterScope temps(masm());                                  \
    Register scratch = temps.Acquire();                                     \
    __ PrepareCallCFunction(0, 2, scratch);                                 \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 2); \
  } while (0)

#define ASSEMBLE_IEEE754_UNOP(name)                                         \
  do {                                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                           \
    UseScratchRegisterScope temps(masm());                                  \
    Register scratch = temps.Acquire();                                     \
    __ PrepareCallCFunction(0, 1, scratch);                                 \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 1); \
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
    __ Ld_d(ra, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
    __ Ld_d(fp, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
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
    masm->Sub_d(sp, sp, stack_slot_delta * kSystemPointerSize);
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    masm->Add_d(sp, sp, -stack_slot_delta * kSystemPointerSize);
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
  UseScratchRegisterScope temps(masm());
  Register scratch = temps.Acquire();
  __ ComputeCodeStartAddress(scratch);
  __ Assert(eq, AbortReason::kWrongFunctionCodeStart,
            kJavaScriptCallCodeStartRegister, Operand(scratch));
}

// Check if the code object is marked for deoptimization. If it is, then it
// jumps to the CompileLazyDeoptimizedCode builtin. In order to do this we need
// to:
//    1. read from memory the word that contains that bit, which can be found in
//       the flags in the referenced {Code} object;
//    2. test kMarkedForDeoptimizationBit in those flags; and
//    3. if it is not zero then it jumps to the builtin.
void CodeGenerator::BailoutIfDeoptimized() {
  UseScratchRegisterScope temps(masm());
  Register scratch = temps.Acquire();
  int offset = InstructionStream::kCodeOffset - InstructionStream::kHeaderSize;
  __ LoadProtectedPointerField(
      scratch, MemOperand(kJavaScriptCallCodeStartRegister, offset));
  __ Ld_wu(scratch, FieldMemOperand(scratch, Code::kFlagsOffset));
  __ And(scratch, scratch, Operand(1 << Code::kMarkedForDeoptimizationBit));
  __ TailCallBuiltin(Builtin::kCompileLazyDeoptimizedCode, ne, scratch,
                     Operand(zero_reg));
}

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  Loong64OperandConverter i(this, instr);
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
        __ Call(i.InputRegister(0));
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
        __ Jump(i.InputRegister(0));
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
        UseScratchRegisterScope temps(masm());
        Register scratch = temps.Acquire();
        // Check the function's context matches the context argument.
        __ LoadTaggedField(scratch,
                           FieldMemOperand(func, JSFunction::kContextOffset));
        __ Assert(eq, AbortReason::kWrongFunctionContext, cp, Operand(scratch));
      }
      __ CallJSFunction(func);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchPrepareCallCFunction: {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      int const num_gp_parameters = ParamField::decode(instr->opcode());
      int const num_fp_parameters = FPParamField::decode(instr->opcode());
      __ PrepareCallCFunction(num_gp_parameters, num_fp_parameters, scratch);
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
        __ LoadLabelRelative(t7, &return_location);
        __ St_d(t7, MemOperand(fp, WasmExitFrameConstants::kCallingPCOffset));
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
        __ Sub_d(lhs_register, sp, offset);
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
        __ Ld_d(i.OutputRegister(), MemOperand(fp, 0));
      } else {
        __ mov(i.OutputRegister(), fp);
      }
      break;
    case kArchTruncateDoubleToI:
      __ TruncateDoubleToI(isolate(), zone(), i.OutputRegister(),
                           i.InputDoubleRegister(0), DetermineStubCallMode());
      break;
    case kArchStoreWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      // Indirect pointer writes must use a different opcode.
      DCHECK_NE(mode, RecordWriteMode::kValueIsIndirectPointer);
      AddressingMode addressing_mode =
          AddressingModeField::decode(instr->opcode());
      Register object = i.InputRegister(0);
      Register value = i.InputRegister(2);

      if (addressing_mode == kMode_MRI) {
        auto ool = zone()->New<OutOfLineRecordWrite>(
            this, object, Operand(i.InputInt64(1)), value, mode,
            DetermineStubCallMode());
        RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
        __ StoreTaggedField(value, MemOperand(object, i.InputInt64(1)));
        if (mode > RecordWriteMode::kValueIsPointer) {
          __ JumpIfSmi(value, ool->exit());
        }
        __ CheckPageFlag(object,
                         MemoryChunk::kPointersFromHereAreInterestingMask, ne,
                         ool->entry());
        __ bind(ool->exit());
      } else {
        DCHECK_EQ(addressing_mode, kMode_MRR);
        auto ool = zone()->New<OutOfLineRecordWrite>(
            this, object, Operand(i.InputRegister(1)), value, mode,
            DetermineStubCallMode());
        RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
        __ StoreTaggedField(value, MemOperand(object, i.InputRegister(1)));
        if (mode > RecordWriteMode::kValueIsIndirectPointer) {
          __ JumpIfSmi(value, ool->exit());
        }
        __ CheckPageFlag(object,
                         MemoryChunk::kPointersFromHereAreInterestingMask, ne,
                         ool->entry());
        __ bind(ool->exit());
      }
      break;
    }
    case kArchAtomicStoreWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      // Indirect pointer writes must use a different opcode.
      DCHECK_NE(mode, RecordWriteMode::kValueIsIndirectPointer);
      Register object = i.InputRegister(0);
      int64_t offset = i.InputInt64(1);
      Register value = i.InputRegister(2);

      auto ool = zone()->New<OutOfLineRecordWrite>(
          this, object, Operand(offset), value, mode, DetermineStubCallMode());
      __ AtomicStoreTaggedField(value, MemOperand(object, offset));
      // Skip the write barrier if the value is a Smi. However, this is only
      // valid if the value isn't an indirect pointer. Otherwise the value will
      // be a pointer table index, which will always look like a Smi (but
      // actually reference a pointer in the pointer table).
      if (mode > RecordWriteMode::kValueIsIndirectPointer) {
        __ JumpIfSmi(value, ool->exit());
      }
      __ CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask,
                       ne, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchStoreIndirectWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      DCHECK_EQ(mode, RecordWriteMode::kValueIsIndirectPointer);
      AddressingMode addressing_mode =
          AddressingModeField::decode(instr->opcode());
      IndirectPointerTag tag = static_cast<IndirectPointerTag>(i.InputInt64(3));
      DCHECK(IsValidIndirectPointerTag(tag));
      Register object = i.InputRegister(0);
      Register value = i.InputRegister(2);

      if (addressing_mode == kMode_MRI) {
        auto ool = zone()->New<OutOfLineRecordWrite>(
            this, object, Operand(i.InputInt32(1)), value, mode,
            DetermineStubCallMode(), tag);
        RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
        __ StoreIndirectPointerField(value,
                                     MemOperand(object, i.InputInt32(1)));
        __ CheckPageFlag(object,
                         MemoryChunk::kPointersFromHereAreInterestingMask, ne,
                         ool->entry());
        __ bind(ool->exit());
      } else {
        DCHECK_EQ(addressing_mode, kMode_MRR);
        auto ool = zone()->New<OutOfLineRecordWrite>(
            this, object, Operand(i.InputRegister(1)), value, mode,
            DetermineStubCallMode(), tag);
        RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
        __ StoreIndirectPointerField(value,
                                     MemOperand(object, i.InputRegister(1)));
        __ CheckPageFlag(object,
                         MemoryChunk::kPointersFromHereAreInterestingMask, ne,
                         ool->entry());
        __ bind(ool->exit());
      }
      break;
    }
    case kArchStackSlot: {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      FrameOffset offset =
          frame_access_state()->GetFrameOffset(i.InputInt32(0));
      Register base_reg = offset.from_stack_pointer() ? sp : fp;
      __ Add_d(i.OutputRegister(), base_reg, Operand(offset.offset()));
      if (v8_flags.debug_code) {
        // Verify that the output_register is properly aligned
        __ And(scratch, i.OutputRegister(), Operand(kSystemPointerSize - 1));
        __ Assert(eq, AbortReason::kAllocationIsNotDoubleAligned, scratch,
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
    case kLoong64Add_w:
      __ Add_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Add_d:
      __ Add_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64AddOvf_d:
      __ AddOverflow_d(i.OutputRegister(), i.InputRegister(0),
                       i.InputOperand(1), t8);
      break;
    case kLoong64Sub_w:
      __ Sub_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Sub_d:
      __ Sub_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64SubOvf_d:
      __ SubOverflow_d(i.OutputRegister(), i.InputRegister(0),
                       i.InputOperand(1), t8);
      break;
    case kLoong64Mul_w:
      __ Mul_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64MulOvf_w:
      __ MulOverflow_w(i.OutputRegister(), i.InputRegister(0),
                       i.InputOperand(1), t8);
      break;
    case kLoong64MulOvf_d:
      __ MulOverflow_d(i.OutputRegister(), i.InputRegister(0),
                       i.InputOperand(1), t8);
      break;
    case kLoong64Mulh_w:
      __ Mulh_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Mulh_wu:
      __ Mulh_wu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Mulh_d:
      __ Mulh_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Mulh_du:
      __ Mulh_du(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Div_w:
      __ Div_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ maskeqz(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kLoong64Div_wu:
      __ Div_wu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ maskeqz(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kLoong64Mod_w:
      __ Mod_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Mod_wu:
      __ Mod_wu(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Mul_d:
      __ Mul_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Div_d:
      __ Div_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ maskeqz(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kLoong64Div_du:
      __ Div_du(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      __ maskeqz(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kLoong64Mod_d:
      __ Mod_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Mod_du:
      __ Mod_du(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Alsl_d:
      DCHECK(instr->InputAt(2)->IsImmediate());
      __ Alsl_d(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.InputInt8(2), t7);
      break;
    case kLoong64Alsl_w:
      DCHECK(instr->InputAt(2)->IsImmediate());
      __ Alsl_w(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.InputInt8(2), t7);
      break;
    case kLoong64And:
    case kLoong64And32:
      __ And(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Or:
    case kLoong64Or32:
      __ Or(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Nor:
    case kLoong64Nor32:
      if (instr->InputAt(1)->IsRegister()) {
        __ Nor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      } else {
        DCHECK_EQ(0, i.InputOperand(1).immediate());
        __ Nor(i.OutputRegister(), i.InputRegister(0), zero_reg);
      }
      break;
    case kLoong64Xor:
    case kLoong64Xor32:
      __ Xor(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Clz_w:
      __ clz_w(i.OutputRegister(), i.InputRegister(0));
      break;
    case kLoong64Clz_d:
      __ clz_d(i.OutputRegister(), i.InputRegister(0));
      break;
    case kLoong64Sll_w:
      if (instr->InputAt(1)->IsRegister()) {
        __ sll_w(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ slli_w(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLoong64Srl_w:
      if (instr->InputAt(1)->IsRegister()) {
        __ srl_w(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ srli_w(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLoong64Sra_w:
      if (instr->InputAt(1)->IsRegister()) {
        __ sra_w(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ srai_w(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLoong64Bstrpick_w:
      __ bstrpick_w(i.OutputRegister(), i.InputRegister(0),
                    i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      break;
    case kLoong64Bstrins_w:
      if (instr->InputAt(1)->IsImmediate() && i.InputInt8(1) == 0) {
        __ bstrins_w(i.OutputRegister(), zero_reg,
                     i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      } else {
        __ bstrins_w(i.OutputRegister(), i.InputRegister(0),
                     i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      }
      break;
    case kLoong64Bstrpick_d: {
      __ bstrpick_d(i.OutputRegister(), i.InputRegister(0),
                    i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      break;
    }
    case kLoong64Bstrins_d:
      if (instr->InputAt(1)->IsImmediate() && i.InputInt8(1) == 0) {
        __ bstrins_d(i.OutputRegister(), zero_reg,
                     i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      } else {
        __ bstrins_d(i.OutputRegister(), i.InputRegister(0),
                     i.InputInt8(1) + i.InputInt8(2) - 1, i.InputInt8(1));
      }
      break;
    case kLoong64Sll_d:
      if (instr->InputAt(1)->IsRegister()) {
        __ sll_d(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ slli_d(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLoong64Srl_d:
      if (instr->InputAt(1)->IsRegister()) {
        __ srl_d(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ srli_d(i.OutputRegister(), i.InputRegister(0),
                  static_cast<uint16_t>(imm));
      }
      break;
    case kLoong64Sra_d:
      if (instr->InputAt(1)->IsRegister()) {
        __ sra_d(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ srai_d(i.OutputRegister(), i.InputRegister(0), imm);
      }
      break;
    case kLoong64Rotr_w:
      __ Rotr_w(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Rotr_d:
      __ Rotr_d(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kLoong64Tst:
      __ And(t8, i.InputRegister(0), i.InputOperand(1));
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kLoong64Cmp32:
    case kLoong64Cmp64:
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kLoong64Mov:
      // TODO(LOONG_dev): Should we combine mov/li, or use separate instr?
      //    - Also see x64 ASSEMBLE_BINOP & RegisterOrOperandType
      if (HasRegisterInput(instr, 0)) {
        __ mov(i.OutputRegister(), i.InputRegister(0));
      } else {
        __ li(i.OutputRegister(), i.InputOperand(0));
      }
      break;

    case kLoong64Float32Cmp: {
      FPURegister left = i.InputOrZeroSingleRegister(0);
      FPURegister right = i.InputOrZeroSingleRegister(1);
      bool predicate;
      FPUCondition cc =
          FlagsConditionToConditionCmpFPU(&predicate, instr->flags_condition());

      if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
          !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ CompareF32(left, right, cc);
    } break;
    case kLoong64Float32Add:
      __ fadd_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLoong64Float32Sub:
      __ fsub_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLoong64Float32Mul:
      __ fmul_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLoong64Float32Div:
      __ fdiv_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLoong64Float32Abs:
      __ fabs_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    case kLoong64Float32Neg:
      __ Neg_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    case kLoong64Float32Sqrt: {
      __ fsqrt_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLoong64Float32Min: {
      FPURegister dst = i.OutputSingleRegister();
      FPURegister src1 = i.InputSingleRegister(0);
      FPURegister src2 = i.InputSingleRegister(1);
      auto ool = zone()->New<OutOfLineFloat32Min>(this, dst, src1, src2);
      __ Float32Min(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kLoong64Float32Max: {
      FPURegister dst = i.OutputSingleRegister();
      FPURegister src1 = i.InputSingleRegister(0);
      FPURegister src2 = i.InputSingleRegister(1);
      auto ool = zone()->New<OutOfLineFloat32Max>(this, dst, src1, src2);
      __ Float32Max(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kLoong64Float64Cmp: {
      FPURegister left = i.InputOrZeroDoubleRegister(0);
      FPURegister right = i.InputOrZeroDoubleRegister(1);
      bool predicate;
      FPUCondition cc =
          FlagsConditionToConditionCmpFPU(&predicate, instr->flags_condition());
      if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
          !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }

      __ CompareF64(left, right, cc);
    } break;
    case kLoong64Float64Add:
      __ fadd_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLoong64Float64Sub:
      __ fsub_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLoong64Float64Mul:
      // TODO(LOONG_dev): LOONG64 add special case: right op is -1.0, see arm
      // port.
      __ fmul_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLoong64Float64Div:
      __ fdiv_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kLoong64Float64Mod: {
      // TODO(turbofan): implement directly.
      FrameScope scope(masm(), StackFrame::MANUAL);
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      __ PrepareCallCFunction(0, 2, scratch);
      __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2);
      break;
    }
    case kLoong64Float64Abs:
      __ fabs_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kLoong64Float64Neg:
      __ Neg_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kLoong64Float64Sqrt: {
      __ fsqrt_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLoong64Float64Min: {
      FPURegister dst = i.OutputDoubleRegister();
      FPURegister src1 = i.InputDoubleRegister(0);
      FPURegister src2 = i.InputDoubleRegister(1);
      auto ool = zone()->New<OutOfLineFloat64Min>(this, dst, src1, src2);
      __ Float64Min(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kLoong64Float64Max: {
      FPURegister dst = i.OutputDoubleRegister();
      FPURegister src1 = i.InputDoubleRegister(0);
      FPURegister src2 = i.InputDoubleRegister(1);
      auto ool = zone()->New<OutOfLineFloat64Max>(this, dst, src1, src2);
      __ Float64Max(dst, src1, src2, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kLoong64Float64RoundDown: {
      __ Floor_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLoong64Float32RoundDown: {
      __ Floor_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kLoong64Float64RoundTruncate: {
      __ Trunc_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLoong64Float32RoundTruncate: {
      __ Trunc_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kLoong64Float64RoundUp: {
      __ Ceil_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLoong64Float32RoundUp: {
      __ Ceil_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kLoong64Float64RoundTiesEven: {
      __ Round_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kLoong64Float32RoundTiesEven: {
      __ Round_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    }
    case kLoong64Float64SilenceNaN:
      __ FPUCanonicalizeNaN(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kLoong64Float64ToFloat32:
      __ fcvt_s_d(i.OutputSingleRegister(), i.InputDoubleRegister(0));
      break;
    case kLoong64Float32ToFloat64:
      __ fcvt_d_s(i.OutputDoubleRegister(), i.InputSingleRegister(0));
      break;
    case kLoong64Int32ToFloat64: {
      FPURegister scratch = kScratchDoubleReg;
      __ movgr2fr_w(scratch, i.InputRegister(0));
      __ ffint_d_w(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kLoong64Int32ToFloat32: {
      FPURegister scratch = kScratchDoubleReg;
      __ movgr2fr_w(scratch, i.InputRegister(0));
      __ ffint_s_w(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kLoong64Uint32ToFloat32: {
      __ Ffint_s_uw(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kLoong64Int64ToFloat32: {
      FPURegister scratch = kScratchDoubleReg;
      __ movgr2fr_d(scratch, i.InputRegister(0));
      __ ffint_s_l(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kLoong64Int64ToFloat64: {
      FPURegister scratch = kScratchDoubleReg;
      __ movgr2fr_d(scratch, i.InputRegister(0));
      __ ffint_d_l(i.OutputDoubleRegister(), scratch);
      break;
    }
    case kLoong64Uint32ToFloat64: {
      __ Ffint_d_uw(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kLoong64Uint64ToFloat64: {
      __ Ffint_d_ul(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kLoong64Uint64ToFloat32: {
      __ Ffint_s_ul(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kLoong64Float64ToInt32: {
      FPURegister scratch = kScratchDoubleReg;
      __ ftintrz_w_d(scratch, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch);
      if (instr->OutputCount() > 1) {
        // Check for inputs below INT32_MIN and NaN.
        __ li(i.OutputRegister(1), 1);
        __ Move(scratch, static_cast<double>(INT32_MIN));
        __ CompareF64(scratch, i.InputDoubleRegister(0), CLE);
        __ LoadZeroIfNotFPUCondition(i.OutputRegister(1));
        __ Move(scratch, static_cast<double>(INT32_MAX) + 1);
        __ CompareF64(scratch, i.InputDoubleRegister(0), CLE);
        __ LoadZeroIfFPUCondition(i.OutputRegister(1));
      }
      break;
    }
    case kLoong64Float32ToInt32: {
      FPURegister scratch_d = kScratchDoubleReg;
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      __ ftintrz_w_s(scratch_d, i.InputDoubleRegister(0));
      __ movfr2gr_s(i.OutputRegister(), scratch_d);
      if (set_overflow_to_min_i32) {
        UseScratchRegisterScope temps(masm());
        Register scratch = temps.Acquire();
        // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
        // because INT32_MIN allows easier out-of-bounds detection.
        __ addi_w(scratch, i.OutputRegister(), 1);
        __ slt(scratch, scratch, i.OutputRegister());
        __ add_w(i.OutputRegister(), i.OutputRegister(), scratch);
      }
      break;
    }
    case kLoong64Float32ToInt64: {
      FPURegister scratch_d = kScratchDoubleReg;

      bool load_status = instr->OutputCount() > 1;
      // Other arches use round to zero here, so we follow.
      __ ftintrz_l_s(scratch_d, i.InputDoubleRegister(0));
      __ movfr2gr_d(i.OutputRegister(), scratch_d);
      if (load_status) {
        Register output2 = i.OutputRegister(1);
        __ movfcsr2gr(output2, FCSR2);
        // Check for overflow and NaNs.
        __ And(output2, output2,
               kFCSROverflowCauseMask | kFCSRInvalidOpCauseMask);
        __ Slt(output2, zero_reg, output2);
        __ xori(output2, output2, 1);
      }
      break;
    }
    case kLoong64Float64ToInt64: {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      FPURegister scratch_d = kScratchDoubleReg;

      bool set_overflow_to_min_i64 = MiscField::decode(instr->opcode());
      bool load_status = instr->OutputCount() > 1;
      // Other arches use round to zero here, so we follow.
      __ ftintrz_l_d(scratch_d, i.InputDoubleRegister(0));
      __ movfr2gr_d(i.OutputRegister(0), scratch_d);
      if (load_status) {
        Register output2 = i.OutputRegister(1);
        __ movfcsr2gr(output2, FCSR2);
        // Check for overflow and NaNs.
        __ And(output2, output2,
               kFCSROverflowCauseMask | kFCSRInvalidOpCauseMask);
        __ Slt(output2, zero_reg, output2);
        __ xori(output2, output2, 1);
      }
      if (set_overflow_to_min_i64) {
        // Avoid INT64_MAX as an overflow indicator and use INT64_MIN instead,
        // because INT64_MIN allows easier out-of-bounds detection.
        __ addi_d(scratch, i.OutputRegister(), 1);
        __ slt(scratch, scratch, i.OutputRegister());
        __ add_d(i.OutputRegister(), i.OutputRegister(), scratch);
      }
      break;
    }
    case kLoong64Float64ToUint32: {
      FPURegister scratch = kScratchDoubleReg;
      __ Ftintrz_uw_d(i.OutputRegister(), i.InputDoubleRegister(0), scratch);
      if (instr->OutputCount() > 1) {
        __ li(i.OutputRegister(1), 1);
        __ Move(scratch, static_cast<double>(-1.0));
        __ CompareF64(scratch, i.InputDoubleRegister(0), CLT);
        __ LoadZeroIfNotFPUCondition(i.OutputRegister(1));
        __ Move(scratch, static_cast<double>(UINT32_MAX) + 1);
        __ CompareF64(scratch, i.InputDoubleRegister(0), CLE);
        __ LoadZeroIfFPUCondition(i.OutputRegister(1));
      }
      break;
    }
    case kLoong64Float32ToUint32: {
      FPURegister scratch = kScratchDoubleReg;
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      __ Ftintrz_uw_s(i.OutputRegister(), i.InputDoubleRegister(0), scratch);
      if (set_overflow_to_min_i32) {
        UseScratchRegisterScope temps(masm());
        Register scratch = temps.Acquire();
        // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
        // because 0 allows easier out-of-bounds detection.
        __ addi_w(scratch, i.OutputRegister(), 1);
        __ Movz(i.OutputRegister(), zero_reg, scratch);
      }
      break;
    }
    case kLoong64Float32ToUint64: {
      FPURegister scratch = kScratchDoubleReg;
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Ftintrz_ul_s(i.OutputRegister(), i.InputDoubleRegister(0), scratch,
                      result);
      break;
    }
    case kLoong64Float64ToUint64: {
      FPURegister scratch = kScratchDoubleReg;
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Ftintrz_ul_d(i.OutputRegister(0), i.InputDoubleRegister(0), scratch,
                      result);
      break;
    }
    case kLoong64BitcastDL:
      __ movfr2gr_d(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kLoong64BitcastLD:
      __ movgr2fr_d(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kLoong64Float64ExtractLowWord32:
      __ FmoveLow(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kLoong64Float64ExtractHighWord32:
      __ movfrh2gr_s(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kLoong64Float64FromWord32Pair:
      __ movgr2fr_w(i.OutputDoubleRegister(), i.InputRegister(1));
      __ movgr2frh_w(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kLoong64Float64InsertLowWord32:
      __ FmoveLow(i.OutputDoubleRegister(), i.InputRegister(1));
      break;
    case kLoong64Float64InsertHighWord32:
      __ movgr2frh_w(i.OutputDoubleRegister(), i.InputRegister(1));
      break;
      // ... more basic instructions ...

    case kLoong64Ext_w_b:
      __ ext_w_b(i.OutputRegister(), i.InputRegister(0));
      break;
    case kLoong64Ext_w_h:
      __ ext_w_h(i.OutputRegister(), i.InputRegister(0));
      break;
    case kLoong64Ld_bu:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ld_bu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64Ld_b:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ld_b(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64St_b: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ St_b(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kLoong64Ld_hu:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ld_hu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64Ld_h:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ld_h(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64St_h: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ St_h(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kLoong64Ld_w:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ld_w(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64Ld_wu:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ld_wu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64Ld_d:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Ld_d(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64St_w: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ St_w(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kLoong64St_d: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ St_d(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kLoong64LoadDecompressTaggedSigned:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ DecompressTaggedSigned(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64LoadDecompressTagged:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ DecompressTagged(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64LoadDecompressProtected:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ DecompressProtected(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64StoreCompressTagged: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ StoreTaggedField(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kLoong64LoadDecodeSandboxedPointer:
      __ LoadSandboxedPointerField(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64StoreEncodeSandboxedPointer: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ StoreSandboxedPointerField(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kLoong64StoreIndirectPointer: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ StoreIndirectPointerField(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kLoong64AtomicLoadDecompressTaggedSigned:
      __ AtomicDecompressTaggedSigned(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64AtomicLoadDecompressTagged:
      __ AtomicDecompressTagged(i.OutputRegister(), i.MemoryOperand());
      break;
    case kLoong64AtomicStoreCompressTagged: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ AtomicStoreTaggedField(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kLoong64Fld_s: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Fld_s(i.OutputSingleRegister(), i.MemoryOperand());
      break;
    }
    case kLoong64Fst_s: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroSingleRegister(index);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Fst_s(ft, operand);
      break;
    }
    case kLoong64Fld_d:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Fld_d(i.OutputDoubleRegister(), i.MemoryOperand());
      break;
    case kLoong64Fst_d: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroDoubleRegister(index);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ Move(kDoubleRegZero, 0.0);
      }
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Fst_d(ft, operand);
      break;
    }
    case kLoong64Dbar: {
      __ dbar(0);
      break;
    }
    case kLoong64Push:
      if (instr->InputAt(0)->IsFPRegister()) {
        __ Fst_d(i.InputDoubleRegister(0), MemOperand(sp, -kDoubleSize));
        __ Sub_d(sp, sp, Operand(kDoubleSize));
        frame_access_state()->IncreaseSPDelta(kDoubleSize / kSystemPointerSize);
      } else {
        __ Push(i.InputRegister(0));
        frame_access_state()->IncreaseSPDelta(1);
      }
      break;
    case kLoong64Peek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ Fld_d(i.OutputDoubleRegister(), MemOperand(fp, offset));
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ Fld_s(i.OutputSingleRegister(0), MemOperand(fp, offset));
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          abort();
        }
      } else {
        __ Ld_d(i.OutputRegister(0), MemOperand(fp, offset));
      }
      break;
    }
    case kLoong64StackClaim: {
      __ Sub_d(sp, sp, Operand(i.InputInt32(0)));
      frame_access_state()->IncreaseSPDelta(i.InputInt32(0) /
                                            kSystemPointerSize);
      break;
    }
    case kLoong64Poke: {
      if (instr->InputAt(0)->IsFPRegister()) {
        __ Fst_d(i.InputDoubleRegister(0), MemOperand(sp, i.InputInt32(1)));
      } else {
        __ St_d(i.InputRegister(0), MemOperand(sp, i.InputInt32(1)));
      }
      break;
    }
    case kLoong64ByteSwap64: {
      __ ByteSwap(i.OutputRegister(0), i.InputRegister(0), 8);
      break;
    }
    case kLoong64ByteSwap32: {
      __ ByteSwap(i.OutputRegister(0), i.InputRegister(0), 4);
      break;
    }
    case kAtomicLoadInt8:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_b);
      break;
    case kAtomicLoadUint8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_bu);
      break;
    case kAtomicLoadInt16:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_h);
      break;
    case kAtomicLoadUint16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_hu);
      break;
    case kAtomicLoadWord32:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_w);
      break;
    case kLoong64Word64AtomicLoadUint32:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_wu);
      break;
    case kLoong64Word64AtomicLoadUint64:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld_d);
      break;
    case kAtomicStoreWord8:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_b);
      break;
    case kAtomicStoreWord16:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_h);
      break;
    case kAtomicStoreWord32:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_w);
      break;
    case kLoong64Word64AtomicStoreWord64:
      ASSEMBLE_ATOMIC_STORE_INTEGER(St_d);
      break;
    case kAtomicExchangeInt8:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, true, 8, 32);
      break;
    case kAtomicExchangeUint8:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, false, 8, 32);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 8, 64);
          break;
      }
      break;
    case kAtomicExchangeInt16:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, true, 16, 32);
      break;
    case kAtomicExchangeUint16:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, false, 16, 32);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 16, 64);
          break;
      }
      break;
    case kAtomicExchangeWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
          __ amswap_db_w(i.OutputRegister(0), i.InputRegister(2),
                         i.TempRegister(0));
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 32, 64);
          break;
      }
      break;
    case kLoong64Word64AtomicExchangeUint64:
      __ add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ amswap_db_d(i.OutputRegister(0), i.InputRegister(2),
                     i.TempRegister(0));
      break;
    case kAtomicCompareExchangeInt8:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, true, 8, 32);
      break;
    case kAtomicCompareExchangeUint8:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, false, 8,
                                                       32);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 8,
                                                       64);
          break;
      }
      break;
    case kAtomicCompareExchangeInt16:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, true, 16, 32);
      break;
    case kAtomicCompareExchangeUint16:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_w, Sc_w, false, 16,
                                                       32);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 16,
                                                       64);
          break;
      }
      break;
    case kAtomicCompareExchangeWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          __ slli_w(i.InputRegister(2), i.InputRegister(2), 0);
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll_w, Sc_w);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll_d, Sc_d, false, 32,
                                                       64);
          break;
      }
      break;
    case kLoong64Word64AtomicCompareExchangeUint64:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll_d, Sc_d);
      break;
    case kAtomicAddWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
          __ amadd_db_w(i.OutputRegister(0), i.InputRegister(2),
                        i.TempRegister(0));
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 32, Add_d, 64);
          break;
      }
      break;
    case kAtomicSubWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_BINOP(Ll_w, Sc_w, Sub_w);
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 32, Sub_d, 64);
          break;
      }
      break;
    case kAtomicAndWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
          __ amand_db_w(i.OutputRegister(0), i.InputRegister(2),
                        i.TempRegister(0));
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 32, And, 64);
          break;
      }
      break;
    case kAtomicOrWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
          __ amor_db_w(i.OutputRegister(0), i.InputRegister(2),
                       i.TempRegister(0));
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 32, Or, 64);
          break;
      }
      break;
    case kAtomicXorWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
          __ amxor_db_w(i.OutputRegister(0), i.InputRegister(2),
                        i.TempRegister(0));
          break;
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 32, Xor, 64);
          break;
      }
      break;
#define ATOMIC_BINOP_CASE(op, inst32, inst64)                          \
  case kAtomic##op##Int8:                                              \
    DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32); \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, true, 8, inst32, 32);        \
    break;                                                             \
  case kAtomic##op##Uint8:                                             \
    switch (AtomicWidthField::decode(opcode)) {                        \
      case AtomicWidth::kWord32:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, false, 8, inst32, 32);   \
        break;                                                         \
      case AtomicWidth::kWord64:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 8, inst64, 64);   \
        break;                                                         \
    }                                                                  \
    break;                                                             \
  case kAtomic##op##Int16:                                             \
    DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32); \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, true, 16, inst32, 32);       \
    break;                                                             \
  case kAtomic##op##Uint16:                                            \
    switch (AtomicWidthField::decode(opcode)) {                        \
      case AtomicWidth::kWord32:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_w, Sc_w, false, 16, inst32, 32);  \
        break;                                                         \
      case AtomicWidth::kWord64:                                       \
        ASSEMBLE_ATOMIC_BINOP_EXT(Ll_d, Sc_d, false, 16, inst64, 64);  \
        break;                                                         \
    }                                                                  \
    break;
      ATOMIC_BINOP_CASE(Add, Add_w, Add_d)
      ATOMIC_BINOP_CASE(Sub, Sub_w, Sub_d)
      ATOMIC_BINOP_CASE(And, And, And)
      ATOMIC_BINOP_CASE(Or, Or, Or)
      ATOMIC_BINOP_CASE(Xor, Xor, Xor)
#undef ATOMIC_BINOP_CASE

    case kLoong64Word64AtomicAddUint64:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ amadd_db_d(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kLoong64Word64AtomicSubUint64:
      ASSEMBLE_ATOMIC_BINOP(Ll_d, Sc_d, Sub_d);
      break;
    case kLoong64Word64AtomicAndUint64:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ amand_db_d(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kLoong64Word64AtomicOrUint64:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ amor_db_d(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
    case kLoong64Word64AtomicXorUint64:
      __ Add_d(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ amxor_db_d(i.OutputRegister(0), i.InputRegister(2), i.TempRegister(0));
      break;
#undef ATOMIC_BINOP_CASE
    case kLoong64S128Const:
    case kLoong64S128Zero:
    case kLoong64I32x4Splat:
    case kLoong64I32x4ExtractLane:
    case kLoong64I32x4Add:
    case kLoong64I32x4ReplaceLane:
    case kLoong64I32x4Sub:
    case kLoong64F64x2Abs:
    default:
      break;
  }
  return kSuccess;
}

#define UNSUPPORTED_COND(opcode, condition)                                    \
  StdoutStream{} << "Unsupported " << #opcode << " condition: \"" << condition \
                 << "\"";                                                      \
  UNIMPLEMENTED();

void SignExtend(MacroAssembler* masm, Instruction* instr, Register* left,
                Operand* right, Register* temp0, Register* temp1) {
  bool need_signed = false;
  MachineRepresentation rep_left =
      LocationOperand::cast(instr->InputAt(0))->representation();
  need_signed = IsAnyTagged(rep_left) || IsAnyCompressed(rep_left) ||
                rep_left == MachineRepresentation::kWord64;
  if (need_signed) {
    masm->slli_w(*temp0, *left, 0);
    *left = *temp0;
  }

  if (instr->InputAt(1)->IsAnyLocationOperand()) {
    MachineRepresentation rep_right =
        LocationOperand::cast(instr->InputAt(1))->representation();
    need_signed = IsAnyTagged(rep_right) || IsAnyCompressed(rep_right) ||
                  rep_right == MachineRepresentation::kWord64;
    if (need_signed && right->is_reg()) {
      DCHECK(*temp1 != no_reg);
      masm->slli_w(*temp1, right->rm(), 0);
      *right = Operand(*temp1);
    }
  }
}

void AssembleBranchToLabels(CodeGenerator* gen, MacroAssembler* masm,
                            Instruction* instr, FlagsCondition condition,
                            Label* tlabel, Label* flabel, bool fallthru) {
#undef __
#define __ masm->
  Loong64OperandConverter i(gen, instr);

  // LOONG64 does not have condition code flags, so compare and branch are
  // implemented differently than on the other arch's. The compare operations
  // emit loong64 pseudo-instructions, which are handled here by branch
  // instructions that do the actual comparison. Essential that the input
  // registers to compare pseudo-op are not modified before this branch op, as
  // they are tested here.

  if (instr->arch_opcode() == kLoong64Tst) {
    Condition cc = FlagsConditionToConditionTst(condition);
    __ Branch(tlabel, cc, t8, Operand(zero_reg));
  } else if (instr->arch_opcode() == kLoong64Add_d ||
             instr->arch_opcode() == kLoong64Sub_d) {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.Acquire();
    Register scratch2 = temps.Acquire();
    Condition cc = FlagsConditionToConditionOvf(condition);
    __ srai_d(scratch, i.OutputRegister(), 32);
    __ srai_w(scratch2, i.OutputRegister(), 31);
    __ Branch(tlabel, cc, scratch2, Operand(scratch));
  } else if (instr->arch_opcode() == kLoong64AddOvf_d ||
             instr->arch_opcode() == kLoong64SubOvf_d) {
    switch (condition) {
      // Overflow occurs if overflow register is negative
      case kOverflow:
        __ Branch(tlabel, lt, t8, Operand(zero_reg));
        break;
      case kNotOverflow:
        __ Branch(tlabel, ge, t8, Operand(zero_reg));
        break;
      default:
        UNSUPPORTED_COND(instr->arch_opcode(), condition);
    }
  } else if (instr->arch_opcode() == kLoong64MulOvf_w ||
             instr->arch_opcode() == kLoong64MulOvf_d) {
    // Overflow occurs if overflow register is not zero
    switch (condition) {
      case kOverflow:
        __ Branch(tlabel, ne, t8, Operand(zero_reg));
        break;
      case kNotOverflow:
        __ Branch(tlabel, eq, t8, Operand(zero_reg));
        break;
      default:
        UNSUPPORTED_COND(instr->arch_opcode(), condition);
    }
  } else if (instr->arch_opcode() == kLoong64Cmp32 ||
             instr->arch_opcode() == kLoong64Cmp64) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    Register left = i.InputRegister(0);
    Operand right = i.InputOperand(1);
    // Word32Compare has two temp registers.
    if (COMPRESS_POINTERS_BOOL && (instr->arch_opcode() == kLoong64Cmp32)) {
      Register temp0 = i.TempRegister(0);
      Register temp1 = right.is_reg() ? i.TempRegister(1) : no_reg;
      SignExtend(masm, instr, &left, &right, &temp0, &temp1);
    }
    __ Branch(tlabel, cc, left, right);
  } else if (instr->arch_opcode() == kArchStackPointerGreaterThan) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    DCHECK((cc == ls) || (cc == hi));
    if (cc == ls) {
      __ xori(i.TempRegister(0), i.TempRegister(0), 1);
    }
    __ Branch(tlabel, ne, i.TempRegister(0), Operand(zero_reg));
  } else if (instr->arch_opcode() == kLoong64Float32Cmp ||
             instr->arch_opcode() == kLoong64Float64Cmp) {
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

void CodeGenerator::AssembleArchConditionalBranch(Instruction* instr,
                                                  BranchInfo* branch) {
  UNREACHABLE();
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
  auto ool = zone()->New<WasmOutOfLineTrap>(this, instr);
  Label* tlabel = ool->entry();
  AssembleBranchToLabels(this, masm(), instr, condition, tlabel, nullptr, true);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  Loong64OperandConverter i(this, instr);

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Register result = i.OutputRegister(instr->OutputCount() - 1);
  // Loong64 does not have condition code flags, so compare and branch are
  // implemented differently than on the other arch's. The compare operations
  // emit loong64 pseudo-instructions, which are checked and handled here.

  if (instr->arch_opcode() == kLoong64Tst) {
    Condition cc = FlagsConditionToConditionTst(condition);
    if (cc == eq) {
      __ Sltu(result, t8, 1);
    } else {
      __ Sltu(result, zero_reg, t8);
    }
    return;
  } else if (instr->arch_opcode() == kLoong64Add_d ||
             instr->arch_opcode() == kLoong64Sub_d) {
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    Condition cc = FlagsConditionToConditionOvf(condition);
    // Check for overflow creates 1 or 0 for result.
    __ srli_d(scratch, i.OutputRegister(), 63);
    __ srli_w(result, i.OutputRegister(), 31);
    __ xor_(result, scratch, result);
    if (cc == eq)  // Toggle result for not overflow.
      __ xori(result, result, 1);
    return;
  } else if (instr->arch_opcode() == kLoong64AddOvf_d ||
             instr->arch_opcode() == kLoong64SubOvf_d) {
    // Overflow occurs if overflow register is negative
    __ slt(result, t8, zero_reg);
  } else if (instr->arch_opcode() == kLoong64MulOvf_w ||
             instr->arch_opcode() == kLoong64MulOvf_d) {
    // Overflow occurs if overflow register is not zero
    __ Sgtu(result, t8, zero_reg);
  } else if (instr->arch_opcode() == kLoong64Cmp32 ||
             instr->arch_opcode() == kLoong64Cmp64) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    Register left = i.InputRegister(0);
    Operand right = i.InputOperand(1);
    if (COMPRESS_POINTERS_BOOL && (instr->arch_opcode() == kLoong64Cmp32)) {
      Register temp0 = i.TempRegister(0);
      Register temp1 = right.is_reg() ? i.TempRegister(1) : no_reg;
      SignExtend(masm(), instr, &left, &right, &temp0, &temp1);
    }
    __ CompareWord(cc, result, left, right);
    return;
  } else if (instr->arch_opcode() == kLoong64Float64Cmp ||
             instr->arch_opcode() == kLoong64Float32Cmp) {
    FPURegister left = i.InputOrZeroDoubleRegister(0);
    FPURegister right = i.InputOrZeroDoubleRegister(1);
    if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
        !__ IsDoubleZeroRegSet()) {
      __ Move(kDoubleRegZero, 0.0);
    }
    bool predicate;
    FlagsConditionToConditionCmpFPU(&predicate, condition);
    {
      __ movcf2gr(result, FCC0);
      if (!predicate) {
        __ xori(result, result, 1);
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

void CodeGenerator::AssembleArchBinarySearchSwitch(Instruction* instr) {
  Loong64OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }

  UseScratchRegisterScope temps(masm());
  Register scratch = temps.Acquire();
  // The input register may contains dirty data in upper 32 bits, explicitly
  // sign-extend it here.
  __ slli_w(scratch, input, 0);
  AssembleArchBinarySearchSwitchRange(scratch, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  Loong64OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  size_t const case_count = instr->InputCount() - 2;

  UseScratchRegisterScope temps(masm());
  Register scratch = temps.Acquire();
  // The input register may contains dirty data in upper 32 bits, explicitly
  // sign-extend it here.
  __ slli_w(scratch, input, 0);
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
        __ Sub_d(sp, sp, Operand(kSystemPointerSize));
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
        __ Sub_d(sp, sp, Operand(kSystemPointerSize));
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
        UseScratchRegisterScope temps(masm());
        Register stack_limit = temps.Acquire();
        __ LoadStackLimit(stack_limit,
                          MacroAssembler::StackLimitKind::kRealStackLimit);
        __ Add_d(stack_limit, stack_limit,
                 Operand(required_slots * kSystemPointerSize));
        __ Branch(&done, uge, sp, Operand(stack_limit));
      }

      __ Call(static_cast<intptr_t>(Builtin::kWasmStackOverflow),
              RelocInfo::WASM_STUB_CALL);
      // The call does not return, hence we can ignore any references and just
      // define an empty safepoint.
      ReferenceMap* reference_map = zone()->New<ReferenceMap>(zone());
      RecordSafepoint(reference_map);
      if (v8_flags.debug_code) {
        __ stop();
      }

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
    __ Sub_d(sp, sp, Operand(required_slots * kSystemPointerSize));
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
    __ Sub_d(sp, sp, Operand(returns * kSystemPointerSize));
  }

  for (int spill_slot : frame()->tagged_slots()) {
    FrameOffset offset = frame_access_state()->GetFrameOffset(spill_slot);
    DCHECK(offset.from_frame_pointer());
    __ St_d(zero_reg, MemOperand(fp, offset.offset()));
  }
}

void CodeGenerator::AssembleReturn(InstructionOperand* additional_pop_count) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const int returns = frame()->GetReturnSlotCount();
  if (returns != 0) {
    __ Add_d(sp, sp, Operand(returns * kSystemPointerSize));
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

  Loong64OperandConverter g(this, nullptr);

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
      __ Ld_d(t0, MemOperand(fp, StandardFrameConstants::kArgCOffset));
    }
    AssembleDeconstructFrame();
  }
  if (drop_jsargs) {
    // We must pop all arguments from the stack (including the receiver). This
    // number of arguments is given by max(1 + argc_reg, parameter_count).
    if (parameter_slots > 1) {
      __ li(t1, parameter_slots);
      __ slt(t2, t0, t1);
      __ Movn(t0, t1, t2);
    }
    __ Alsl_d(sp, t0, sp, kSystemPointerSizeLog2);
  } else if (additional_pop_count->IsImmediate()) {
    int additional_count = g.ToConstant(additional_pop_count).ToInt32();
    __ Drop(parameter_slots + additional_count);
  } else {
    Register pop_reg = g.ToRegister(additional_pop_count);
    __ Drop(parameter_slots);
    __ Alsl_d(sp, pop_reg, sp, kSystemPointerSizeLog2);
  }
  __ Ret();
}

void CodeGenerator::FinishCode() {}

void CodeGenerator::PrepareForDeoptimizationExits(
    ZoneDeque<DeoptimizationExit*>* exits) {}

AllocatedOperand CodeGenerator::Push(InstructionOperand* source) {
  auto rep = LocationOperand::cast(source)->representation();
  int new_slots = ElementSizeInPointers(rep);
  Loong64OperandConverter g(this, nullptr);
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
    __ Ld_d(scratch, g.ToMemOperand(source));
    __ Push(scratch);
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else {
    // No push instruction for this operand type. Bump the stack pointer and
    // assemble the move.
    __ Sub_d(sp, sp, Operand(new_slots * kSystemPointerSize));
    frame_access_state()->IncreaseSPDelta(new_slots);
    AssembleMove(source, &stack_slot);
  }
  temp_slots_ += new_slots;
  return stack_slot;
}

void CodeGenerator::Pop(InstructionOperand* dest, MachineRepresentation rep) {
  Loong64OperandConverter g(this, nullptr);
  int dropped_slots = ElementSizeInPointers(rep);
  if (dest->IsRegister()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ Pop(g.ToRegister(dest));
  } else if (dest->IsStackSlot()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    __ Pop(scratch);
    __ St_d(scratch, g.ToMemOperand(dest));
  } else {
    int last_frame_slot_id =
        frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
    int sp_delta = frame_access_state_->sp_delta();
    int slot_id = last_frame_slot_id + sp_delta;
    AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
    AssembleMove(&stack_slot, dest);
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ Add_d(sp, sp, Operand(dropped_slots * kSystemPointerSize));
  }
  temp_slots_ -= dropped_slots;
}

void CodeGenerator::PopTempStackSlots() {
  if (temp_slots_ > 0) {
    frame_access_state()->IncreaseSPDelta(-temp_slots_);
    __ Add_d(sp, sp, Operand(temp_slots_ * kSystemPointerSize));
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
  temps.ExcludeFp(move_cycle_.scratch_fpregs);
  if (!IsFloatingPoint(rep)) {
    if (temps.hasAvailable()) {
      Register scratch = move_cycle_.temps->Acquire();
      move_cycle_.scratch_reg.emplace(scratch);
    } else if (temps.hasAvailableFp()) {
      // Try to use an FP register if no GP register is available for non-FP
      // moves.
      FPURegister scratch = move_cycle_.temps->AcquireFp();
      move_cycle_.scratch_fpreg.emplace(scratch);
    }
  } else {
    DCHECK(temps.hasAvailableFp());
    FPURegister scratch = move_cycle_.temps->AcquireFp();
    move_cycle_.scratch_fpreg.emplace(scratch);
  }
  temps.Include(move_cycle_.scratch_regs);
  temps.IncludeFp(move_cycle_.scratch_fpregs);
  if (move_cycle_.scratch_reg.has_value()) {
    // A scratch register is available for this rep.
    AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                             move_cycle_.scratch_reg->code());
    AssembleMove(source, &scratch);
  } else if (move_cycle_.scratch_fpreg.has_value()) {
    // A scratch fp register is available for this rep.
    if (!IsFloatingPoint(rep)) {
      AllocatedOperand scratch(LocationOperand::REGISTER,
                               MachineRepresentation::kFloat64,
                               move_cycle_.scratch_fpreg->code());
      Loong64OperandConverter g(this, nullptr);
      if (source->IsStackSlot()) {
        __ Fld_d(g.ToDoubleRegister(&scratch), g.ToMemOperand(source));
      } else {
        DCHECK(source->IsRegister());
        __ movgr2fr_d(g.ToDoubleRegister(&scratch), g.ToRegister(source));
      }
    } else {
      AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                               move_cycle_.scratch_fpreg->code());
      AssembleMove(source, &scratch);
    }
  } else {
    // The scratch registers are blocked by pending moves. Use the stack
    // instead.
    Push(source);
  }
}

void CodeGenerator::MoveTempLocationTo(InstructionOperand* dest,
                                       MachineRepresentation rep) {
  if (move_cycle_.scratch_reg.has_value()) {
    AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                             move_cycle_.scratch_reg->code());
    AssembleMove(&scratch, dest);
  } else if (move_cycle_.scratch_fpreg.has_value()) {
    if (!IsFloatingPoint(rep)) {
      // We used a DoubleRegister to move a non-FP operand, change the
      // representation to correctly interpret the InstructionOperand's code.
      AllocatedOperand scratch(LocationOperand::REGISTER,
                               MachineRepresentation::kFloat64,
                               move_cycle_.scratch_fpreg->code());
      Loong64OperandConverter g(this, nullptr);
      if (dest->IsStackSlot()) {
        __ Fst_d(g.ToDoubleRegister(&scratch), g.ToMemOperand(dest));
      } else {
        DCHECK(dest->IsRegister());
        __ movfr2gr_d(g.ToRegister(dest), g.ToDoubleRegister(&scratch));
      }
    } else {
      AllocatedOperand scratch(LocationOperand::REGISTER, rep,
                               move_cycle_.scratch_fpreg->code());
      AssembleMove(&scratch, dest);
    }
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
  if (src->IsConstant() || (src->IsStackSlot() && dst->IsStackSlot())) {
    Register temp = temps.Acquire();
    move_cycle_.scratch_regs.set(temp);
  }
  if (src->IsAnyStackSlot() || dst->IsAnyStackSlot()) {
    Loong64OperandConverter g(this, nullptr);
    bool src_need_scratch = false;
    bool dst_need_scratch = false;
    if (src->IsStackSlot()) {
      // Doubleword load/store
      MemOperand src_mem = g.ToMemOperand(src);
      src_need_scratch =
          (!is_int16(src_mem.offset()) || (src_mem.offset() & 0b11) != 0) &&
          (!is_int12(src_mem.offset()) && !src_mem.hasIndexReg());
    } else if (src->IsFPStackSlot()) {
      // DoubleWord float-pointing load/store.
      MemOperand src_mem = g.ToMemOperand(src);
      src_need_scratch = !is_int12(src_mem.offset()) && !src_mem.hasIndexReg();
    }
    if (dst->IsStackSlot()) {
      // Doubleword load/store
      MemOperand dst_mem = g.ToMemOperand(dst);
      dst_need_scratch =
          (!is_int16(dst_mem.offset()) || (dst_mem.offset() & 0b11) != 0) &&
          (!is_int12(dst_mem.offset()) && !dst_mem.hasIndexReg());
    } else if (dst->IsFPStackSlot()) {
      // DoubleWord float-pointing load/store.
      MemOperand dst_mem = g.ToMemOperand(dst);
      dst_need_scratch = !is_int12(dst_mem.offset()) && !dst_mem.hasIndexReg();
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
  Loong64OperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      __ mov(g.ToRegister(destination), src);
    } else {
      __ St_d(src, g.ToMemOperand(destination));
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsRegister()) {
      if (Use32BitMove(source, destination)) {
        __ Ld_w(g.ToRegister(destination), src);
      } else {
        __ Ld_d(g.ToRegister(destination), src);
      }
    } else {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      __ Ld_d(scratch, src);
      __ St_d(scratch, g.ToMemOperand(destination));
    }
  } else if (source->IsConstant()) {
    Constant src = g.ToConstant(source);
    if (destination->IsRegister() || destination->IsStackSlot()) {
      UseScratchRegisterScope temps(masm());
      Register scratch = temps.Acquire();
      Register dst =
          destination->IsRegister() ? g.ToRegister(destination) : scratch;
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
        case Constant::kCompressedHeapObject: {
          Handle<HeapObject> src_object = src.ToHeapObject();
          RootIndex index;
          if (IsMaterializableFromRoot(src_object, &index)) {
            __ LoadTaggedRoot(dst, index);
          } else {
            __ li(dst, src_object, RelocInfo::COMPRESSED_EMBEDDED_OBJECT);
          }
          break;
        }
        case Constant::kRpoNumber:
          UNREACHABLE();  // TODO(titzer): loading RPO numbers on LOONG64.
      }
      if (destination->IsStackSlot()) __ St_d(dst, g.ToMemOperand(destination));
    } else if (src.type() == Constant::kFloat32) {
      if (destination->IsFPStackSlot()) {
        MemOperand dst = g.ToMemOperand(destination);
        if (base::bit_cast<int32_t>(src.ToFloat32()) == 0) {
          __ St_d(zero_reg, dst);
        } else {
          UseScratchRegisterScope temps(masm());
          Register scratch = temps.Acquire();
          __ li(scratch, Operand(base::bit_cast<int32_t>(src.ToFloat32())));
          __ St_d(scratch, dst);
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
        __ Fst_d(dst, g.ToMemOperand(destination));
      }
    }
  } else if (source->IsFPRegister()) {
    FPURegister src = g.ToDoubleRegister(source);
    if (destination->IsFPRegister()) {
      FPURegister dst = g.ToDoubleRegister(destination);
      __ Move(dst, src);
    } else {
      DCHECK(destination->IsFPStackSlot());
      __ Fst_d(src, g.ToMemOperand(destination));
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPRegister() || destination->IsFPStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsFPRegister()) {
      __ Fld_d(g.ToDoubleRegister(destination), src);
    } else {
      DCHECK(destination->IsFPStackSlot());
      FPURegister temp = kScratchDoubleReg;
      __ Fld_d(temp, src);
      __ Fst_d(temp, g.ToMemOperand(destination));
    }
  } else {
    UNREACHABLE();
  }
}

void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  Loong64OperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    // Register-register.
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      Register dst = g.ToRegister(destination);
      __ Move(scratch, src);
      __ Move(src, dst);
      __ Move(dst, scratch);
    } else {
      DCHECK(destination->IsStackSlot());
      MemOperand dst = g.ToMemOperand(destination);
      __ mov(scratch, src);
      __ Ld_d(src, dst);
      __ St_d(scratch, dst);
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsStackSlot());
    // TODO(LOONG_dev): LOONG64 Optimize scratch registers usage
    // Since the Ld instruction may need a scratch reg,
    // we should not use both of the two scratch registers in
    // UseScratchRegisterScope here.
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    FPURegister scratch_d = kScratchDoubleReg;
    MemOperand src = g.ToMemOperand(source);
    MemOperand dst = g.ToMemOperand(destination);
    __ Ld_d(scratch, src);
    __ Fld_d(scratch_d, dst);
    __ St_d(scratch, dst);
    __ Fst_d(scratch_d, src);
  } else if (source->IsFPRegister()) {
    FPURegister scratch_d = kScratchDoubleReg;
    FPURegister src = g.ToDoubleRegister(source);
    if (destination->IsFPRegister()) {
      FPURegister dst = g.ToDoubleRegister(destination);
      __ Move(scratch_d, src);
      __ Move(src, dst);
      __ Move(dst, scratch_d);
    } else {
      DCHECK(destination->IsFPStackSlot());
      MemOperand dst = g.ToMemOperand(destination);
      __ Move(scratch_d, src);
      __ Fld_d(src, dst);
      __ Fst_d(scratch_d, dst);
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPStackSlot());
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    FPURegister scratch_d = kScratchDoubleReg;
    MemOperand src = g.ToMemOperand(source);
    MemOperand dst = g.ToMemOperand(destination);
    __ Fld_d(scratch_d, src);
    __ Ld_d(scratch, dst);
    __ Fst_d(scratch_d, dst);
    __ St_d(scratch, src);
  } else {
    // No other combinations are possible.
    UNREACHABLE();
  }
}

void CodeGenerator::AssembleJumpTable(Label** targets, size_t target_count) {
  // On 64-bit LOONG64 we emit the jump tables inline.
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

#undef TRACE_MSG
#undef TRACE_UNIMPL
#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
