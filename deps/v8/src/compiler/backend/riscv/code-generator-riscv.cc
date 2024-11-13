// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/codegen/riscv/constants-riscv.h"
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

#define TRACE_UNIMPL()                                            \
  PrintF("UNIMPLEMENTED code_generator_riscv64: %s at line %d\n", \
         __FUNCTION__, __LINE__)

// Adds RISC-V-specific methods to convert InstructionOperands.
class RiscvOperandConverter final : public InstructionOperandConverter {
 public:
  RiscvOperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  FloatRegister OutputSingleRegister(size_t index = 0) {
    return ToSingleRegister(instr_->OutputAt(index));
  }

  FloatRegister InputSingleRegister(size_t index) {
    return ToSingleRegister(instr_->InputAt(index));
  }

  FloatRegister ToSingleRegister(InstructionOperand* op) {
    // Single (Float) and Double register namespace is same on RISC-V,
    // both are typedefs of FPURegister.
    return ToDoubleRegister(op);
  }

  Register InputOrZeroRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) {
      Constant constant = ToConstant(instr_->InputAt(index));
      switch (constant.type()) {
        case Constant::kInt32:
        case Constant::kInt64:
          DCHECK_EQ(0, InputInt32(index));
          break;
        case Constant::kFloat32:
          DCHECK_EQ(0, base::bit_cast<int32_t>(InputFloat32(index)));
          break;
        case Constant::kFloat64:
          DCHECK_EQ(0, base::bit_cast<int64_t>(InputDouble(index)));
          break;
        default:
          UNREACHABLE();
      }
      return zero_reg;
    }
    return InputRegister(index);
  }

  DoubleRegister InputOrZeroDoubleRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) return kDoubleRegZero;

    return InputDoubleRegister(index);
  }

  DoubleRegister InputOrZeroSingleRegister(size_t index) {
    if (instr_->InputAt(index)->IsImmediate()) return kSingleRegZero;

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
        // TODO(plind): Maybe we should handle ExtRef & HeapObj here?
        //    maybe not done on arm due to const pool ??
        break;
      case Constant::kRpoNumber:
        UNREACHABLE();  // TODO(titzer): RPO immediates
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
      case kMode_MRI:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputInt32(index + 1));
      case kMode_Root:
        return MemOperand(kRootRegister, InputInt32(index));
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
#ifdef V8_TARGET_ARCH_RISCV64
    // When storing an indirect pointer, the value will always be a
    // full/decompressed pointer.
    if (COMPRESS_POINTERS_BOOL &&
        mode_ != RecordWriteMode::kValueIsIndirectPointer) {
      __ DecompressTagged(value_, value_);
    }
#endif
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
      return Uless;
    case kUnsignedGreaterThanOrEqual:
      return Ugreater_equal;
    case kUnsignedLessThanOrEqual:
      return Uless_equal;
    case kUnsignedGreaterThan:
      return Ugreater;
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
#if V8_TARGET_ARCH_RISCV64
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
#endif

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
      return LT;
    case kUnsignedGreaterThanOrEqual:
      *predicate = false;
      return LT;
    case kUnsignedLessThanOrEqual:
    case kFloatLessThanOrEqual:
      *predicate = true;
      return LE;
    case kUnsignedGreaterThan:
      *predicate = false;
      return LE;
    case kUnorderedEqual:
    case kUnorderedNotEqual:
      *predicate = true;
      break;
    case kFloatGreaterThan:
      *predicate = true;
      return GT;
    case kFloatGreaterThanOrEqual:
      *predicate = true;
      return GE;
    case kFloatLessThanOrUnordered:
      *predicate = true;
      return LT;
    case kFloatGreaterThanOrUnordered:
      *predicate = false;
      return LE;
    case kFloatGreaterThanOrEqualOrUnordered:
      *predicate = false;
      return LT;
    case kFloatLessThanOrEqualOrUnordered:
      *predicate = true;
      return LE;
    default:
      *predicate = true;
      break;
  }
  UNREACHABLE();
}

#if V8_ENABLE_WEBASSEMBLY
class WasmOutOfLineTrap : public OutOfLineCode {
 public:
  WasmOutOfLineTrap(CodeGenerator* gen, Instruction* instr)
      : OutOfLineCode(gen), gen_(gen), instr_(instr) {}
  void Generate() override {
    RiscvOperandConverter i(gen_, instr_);
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
}  // namespace

#define ASSEMBLE_ATOMIC_LOAD_INTEGER(asm_instr)            \
  do {                                                     \
    __ asm_instr(i.OutputRegister(), i.MemoryOperand());   \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr,    \
                           (__ pc_offset() - kInstrSize)); \
    __ sync();                                             \
  } while (0)

#define ASSEMBLE_ATOMIC_STORE_INTEGER(asm_instr)                \
  do {                                                          \
    __ sync();                                                  \
    __ asm_instr(i.InputOrZeroRegister(0), i.MemoryOperand(1)); \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr,         \
                           (__ pc_offset() - kInstrSize));      \
    __ sync();                                                  \
  } while (0)

#define ASSEMBLE_ATOMIC_BINOP(load_linked, store_conditional, bin_instr)       \
  do {                                                                         \
    Label binop;                                                               \
    __ AddWord(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    __ sync();                                                                 \
    __ bind(&binop);                                                           \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ bin_instr(i.TempRegister(1), i.OutputRegister(0),                       \
                 Operand(i.InputRegister(2)));                                 \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&binop, ne, i.TempRegister(1), Operand(zero_reg));          \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC64_LOGIC_BINOP(bin_instr, external)  \
  do {                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);           \
    __ AddWord(a0, i.InputRegister(0), i.InputRegister(1)); \
    __ PushCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);    \
    __ PrepareCallCFunction(3, 0, kScratchReg);             \
    __ CallCFunction(ExternalReference::external(), 3, 0);  \
    __ PopCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);     \
  } while (0)

#define ASSEMBLE_ATOMIC64_ARITH_BINOP(bin_instr, external)  \
  do {                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);           \
    __ AddWord(a0, i.InputRegister(0), i.InputRegister(1)); \
    __ PushCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);    \
    __ PrepareCallCFunction(3, 0, kScratchReg);             \
    __ CallCFunction(ExternalReference::external(), 3, 0);  \
    __ PopCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);     \
  } while (0)

#define ASSEMBLE_ATOMIC_BINOP_EXT(load_linked, store_conditional, sign_extend, \
                                  size, bin_instr, representation)             \
  do {                                                                         \
    Label binop;                                                               \
    __ AddWord(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    if (representation == 32) {                                                \
      __ And(i.TempRegister(3), i.TempRegister(0), 0x3);                       \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ And(i.TempRegister(3), i.TempRegister(0), 0x7);                       \
    }                                                                          \
    __ SubWord(i.TempRegister(0), i.TempRegister(0),                           \
               Operand(i.TempRegister(3)));                                    \
    __ Sll32(i.TempRegister(3), i.TempRegister(3), 3);                         \
    __ sync();                                                                 \
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
    __ BranchShort(&binop, ne, i.TempRegister(1), Operand(zero_reg));          \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(load_linked, store_conditional)       \
  do {                                                                         \
    Label exchange;                                                            \
    __ sync();                                                                 \
    __ bind(&exchange);                                                        \
    __ AddWord(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ Move(i.TempRegister(1), i.InputRegister(2));                            \
    __ store_conditional(i.TempRegister(1), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&exchange, ne, i.TempRegister(1), Operand(zero_reg));       \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(                                  \
    load_linked, store_conditional, sign_extend, size, representation)         \
  do {                                                                         \
    Label exchange;                                                            \
    __ AddWord(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    if (representation == 32) {                                                \
      __ And(i.TempRegister(1), i.TempRegister(0), 0x3);                       \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ And(i.TempRegister(1), i.TempRegister(0), 0x7);                       \
    }                                                                          \
    __ SubWord(i.TempRegister(0), i.TempRegister(0),                           \
               Operand(i.TempRegister(1)));                                    \
    __ Sll32(i.TempRegister(1), i.TempRegister(1), 3);                         \
    __ sync();                                                                 \
    __ bind(&exchange);                                                        \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.TempRegister(2), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(2), i.TempRegister(1),  \
                   size, sign_extend);                                         \
    __ InsertBits(i.TempRegister(2), i.InputRegister(2), i.TempRegister(1),    \
                  size);                                                       \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&exchange, ne, i.TempRegister(2), Operand(zero_reg));       \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(load_linked,                  \
                                                 store_conditional)            \
  do {                                                                         \
    Label compareExchange;                                                     \
    Label exit;                                                                \
    __ AddWord(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    __ sync();                                                                 \
    __ bind(&compareExchange);                                                 \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.OutputRegister(0), MemOperand(i.TempRegister(0), 0));     \
    __ BranchShort(&exit, ne, i.InputRegister(2),                              \
                   Operand(i.OutputRegister(0)));                              \
    __ Move(i.TempRegister(2), i.InputRegister(3));                            \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&compareExchange, ne, i.TempRegister(2),                    \
                   Operand(zero_reg));                                         \
    __ bind(&exit);                                                            \
    __ sync();                                                                 \
  } while (0)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(                          \
    load_linked, store_conditional, sign_extend, size, representation)         \
  do {                                                                         \
    Label compareExchange;                                                     \
    Label exit;                                                                \
    __ AddWord(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1));     \
    if (representation == 32) {                                                \
      __ And(i.TempRegister(1), i.TempRegister(0), 0x3);                       \
    } else {                                                                   \
      DCHECK_EQ(representation, 64);                                           \
      __ And(i.TempRegister(1), i.TempRegister(0), 0x7);                       \
    }                                                                          \
    __ SubWord(i.TempRegister(0), i.TempRegister(0),                           \
               Operand(i.TempRegister(1)));                                    \
    __ Sll32(i.TempRegister(1), i.TempRegister(1), 3);                         \
    __ sync();                                                                 \
    __ bind(&compareExchange);                                                 \
    RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());       \
    __ load_linked(i.TempRegister(2), MemOperand(i.TempRegister(0), 0));       \
    __ ExtractBits(i.OutputRegister(0), i.TempRegister(2), i.TempRegister(1),  \
                   size, sign_extend);                                         \
    __ ExtractBits(i.InputRegister(2), i.InputRegister(2), 0, size,            \
                   sign_extend);                                               \
    __ BranchShort(&exit, ne, i.InputRegister(2),                              \
                   Operand(i.OutputRegister(0)));                              \
    __ InsertBits(i.TempRegister(2), i.InputRegister(3), i.TempRegister(1),    \
                  size);                                                       \
    __ store_conditional(i.TempRegister(2), MemOperand(i.TempRegister(0), 0)); \
    __ BranchShort(&compareExchange, ne, i.TempRegister(2),                    \
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

#define ASSEMBLE_RVV_BINOP_INTEGER(instr, OP)                   \
  case kRiscvI8x16##instr: {                                    \
    __ VU.set(kScratchReg, E8, m1);                             \
    __ OP(i.OutputSimd128Register(), i.InputSimd128Register(0), \
          i.InputSimd128Register(1));                           \
    break;                                                      \
  }                                                             \
  case kRiscvI16x8##instr: {                                    \
    __ VU.set(kScratchReg, E16, m1);                            \
    __ OP(i.OutputSimd128Register(), i.InputSimd128Register(0), \
          i.InputSimd128Register(1));                           \
    break;                                                      \
  }                                                             \
  case kRiscvI32x4##instr: {                                    \
    __ VU.set(kScratchReg, E32, m1);                            \
    __ OP(i.OutputSimd128Register(), i.InputSimd128Register(0), \
          i.InputSimd128Register(1));                           \
    break;                                                      \
  }

#define ASSEMBLE_RVV_UNOP_INTEGER_VR(instr, OP)           \
  case kRiscvI8x16##instr: {                              \
    __ VU.set(kScratchReg, E8, m1);                       \
    __ OP(i.OutputSimd128Register(), i.InputRegister(0)); \
    break;                                                \
  }                                                       \
  case kRiscvI16x8##instr: {                              \
    __ VU.set(kScratchReg, E16, m1);                      \
    __ OP(i.OutputSimd128Register(), i.InputRegister(0)); \
    break;                                                \
  }                                                       \
  case kRiscvI32x4##instr: {                              \
    __ VU.set(kScratchReg, E32, m1);                      \
    __ OP(i.OutputSimd128Register(), i.InputRegister(0)); \
    break;                                                \
  }

#define ASSEMBLE_RVV_UNOP_INTEGER_VV(instr, OP)                  \
  case kRiscvI8x16##instr: {                                     \
    __ VU.set(kScratchReg, E8, m1);                              \
    __ OP(i.OutputSimd128Register(), i.InputSimd128Register(0)); \
    break;                                                       \
  }                                                              \
  case kRiscvI16x8##instr: {                                     \
    __ VU.set(kScratchReg, E16, m1);                             \
    __ OP(i.OutputSimd128Register(), i.InputSimd128Register(0)); \
    break;                                                       \
  }                                                              \
  case kRiscvI32x4##instr: {                                     \
    __ VU.set(kScratchReg, E32, m1);                             \
    __ OP(i.OutputSimd128Register(), i.InputSimd128Register(0)); \
    break;                                                       \
  }                                                              \
  case kRiscvI64x2##instr: {                                     \
    __ VU.set(kScratchReg, E64, m1);                             \
    __ OP(i.OutputSimd128Register(), i.InputSimd128Register(0)); \
    break;                                                       \
  }

void CodeGenerator::AssembleDeconstructFrame() {
  __ Move(sp, fp);
  __ Pop(ra, fp);
}

void CodeGenerator::AssemblePrepareTailCall() {
  if (frame_access_state()->has_frame()) {
    __ LoadWord(ra, MemOperand(fp, StandardFrameConstants::kCallerPCOffset));
    __ LoadWord(fp, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  }
  frame_access_state()->SetFrameAccessToSP();
}

void CodeGenerator::AssembleArchSelect(Instruction* instr,
                                       FlagsCondition condition) {
  UNIMPLEMENTED();
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
    masm->SubWord(sp, sp, stack_slot_delta * kSystemPointerSize);
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    masm->AddWord(sp, sp, -stack_slot_delta * kSystemPointerSize);
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
  __ LoadProtectedPointerField(
      kScratchReg, MemOperand(kJavaScriptCallCodeStartRegister, offset));
  __ Lw(kScratchReg, FieldMemOperand(kScratchReg, Code::kFlagsOffset));
  __ And(kScratchReg, kScratchReg,
         Operand(1 << Code::kMarkedForDeoptimizationBit));
  __ TailCallBuiltin(Builtin::kCompileLazyDeoptimizedCode, ne, kScratchReg,
                     Operand(zero_reg));
}

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  RiscvOperandConverter i(this, instr);
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
    case kArchCallWasmFunction: {
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Call(wasm_code, constant.rmode());
      } else {
        __ AddWord(t6, i.InputOrZeroRegister(0), 0);
        __ Call(t6);
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchTailCallCodeObject: {
      if (instr->InputAt(0)->IsImmediate()) {
        __ Jump(i.InputCode(0), RelocInfo::CODE_TARGET);
      } else {
        Register reg = i.InputOrZeroRegister(0);
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
    case kArchTailCallWasm: {
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Jump(wasm_code, constant.rmode());
      } else {
        __ AddWord(kScratchReg, i.InputOrZeroRegister(0), 0);
        __ Jump(kScratchReg);
      }
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchTailCallAddress: {
      CHECK(!instr->InputAt(0)->IsImmediate());
      Register reg = i.InputOrZeroRegister(0);
      DCHECK_IMPLIES(
          instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
          reg == kJavaScriptCallCodeStartRegister);
      __ Jump(reg);
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
    case kArchCallJSFunction: {
      Register func = i.InputOrZeroRegister(0);
      if (v8_flags.debug_code) {
        // Check the function's context matches the context argument.
        __ LoadTaggedField(kScratchReg,
                           FieldMemOperand(func, JSFunction::kContextOffset));
        __ Assert(eq, AbortReason::kWrongFunctionContext, cp,
                  Operand(kScratchReg));
      }
      __ CallJSFunction(func);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchPrepareCallCFunction: {
#ifdef V8_TARGET_ARCH_RISCV64
      int const num_gp_parameters = ParamField::decode(instr->opcode());
      int const num_fp_parameters = FPParamField::decode(instr->opcode());
      __ PrepareCallCFunction(num_gp_parameters, num_fp_parameters,
                              kScratchReg);
#else
      int const num_parameters = MiscField::decode(instr->opcode());
      __ PrepareCallCFunction(num_parameters, kScratchReg);
#endif
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
      Label return_location;
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes;
#if V8_ENABLE_WEBASSEMBLY
      bool isWasmCapiFunction =
          linkage()->GetIncomingDescriptor()->IsWasmCapiFunction();
      if (isWasmCapiFunction) {
        // Put the return address in a stack slot.
        __ LoadAddress(kScratchReg, &return_location,
                       RelocInfo::EXTERNAL_REFERENCE);
        __ StoreWord(kScratchReg,
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
      __ Move(i.OutputRegister(), sp);
      break;
    case kArchSetStackPointer: {
      DCHECK(instr->InputAt(0)->IsRegister());
      __ Move(sp, i.InputRegister(0));
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchStackPointerGreaterThan:
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kArchStackCheckOffset:
      __ Move(i.OutputRegister(), Smi::FromInt(GetStackCheckOffset()));
      break;
    case kArchFramePointer:
      __ Move(i.OutputRegister(), fp);
      break;
    case kArchParentFramePointer:
      if (frame_access_state()->has_frame()) {
        __ LoadWord(i.OutputRegister(), MemOperand(fp, 0));
      } else {
        __ Move(i.OutputRegister(), fp);
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
      Register object = i.InputRegister(0);
      Register value = i.InputRegister(2);
      auto ool = zone()->New<OutOfLineRecordWrite>(
          this, object, Operand(i.InputRegister(1)), value, mode,
          DetermineStubCallMode());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ AddWord(kScratchReg, object, i.InputRegister(1));
      __ StoreTaggedField(value, MemOperand(kScratchReg, 0));
      if (mode > RecordWriteMode::kValueIsIndirectPointer) {
        __ JumpIfSmi(value, ool->exit());
      }
      __ CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask,
                       ne, ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchAtomicStoreWithWriteBarrier: {
#ifdef V8_TARGET_ARCH_RISCV64
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      // Indirect pointer writes must use a different opcode.
      DCHECK_NE(mode, RecordWriteMode::kValueIsIndirectPointer);
      Register object = i.InputRegister(0);
      Register offset = i.InputRegister(1);
      Register value = i.InputRegister(2);

      auto ool = zone()->New<OutOfLineRecordWrite>(
          this, object, Operand(offset), value, mode, DetermineStubCallMode());
      __ AddWord(kScratchReg, object, offset);
      __ AtomicStoreTaggedField(value, MemOperand(kScratchReg, 0));
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
#else
      UNREACHABLE();
#endif
    }
    case kArchStoreIndirectWithWriteBarrier: {
#ifdef V8_TARGET_ARCH_RISCV64
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      DCHECK_EQ(mode, RecordWriteMode::kValueIsIndirectPointer);
      IndirectPointerTag tag = static_cast<IndirectPointerTag>(i.InputInt64(3));
      DCHECK(IsValidIndirectPointerTag(tag));
      Register object = i.InputRegister(0);
      Register value = i.InputRegister(2);
      auto ool = zone()->New<OutOfLineRecordWrite>(
          this, object, Operand(i.InputRegister(1)), value, mode,
          DetermineStubCallMode(), tag);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ AddWord(kScratchReg, object, i.InputRegister(1));
      __ StoreIndirectPointerField(value, MemOperand(kScratchReg, 0));
      __ CheckPageFlag(object, MemoryChunk::kPointersFromHereAreInterestingMask,
                       ne, ool->entry());
      __ bind(ool->exit());
      break;
#else
      UNREACHABLE();
#endif
    }
    case kArchStackSlot: {
      FrameOffset offset =
          frame_access_state()->GetFrameOffset(i.InputInt32(0));
      Register base_reg = offset.from_stack_pointer() ? sp : fp;
      __ AddWord(i.OutputRegister(), base_reg, Operand(offset.offset()));
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
    case kRiscvAdd32:
      __ Add32(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvSub32:
      __ Sub32(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvMul32:
      __ Mul32(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvMulOvf32:
      __ MulOverflow32(i.OutputRegister(), i.InputOrZeroRegister(0),
                       i.InputOperand(1), kScratchReg);
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvAdd64:
      __ AddWord(i.OutputRegister(), i.InputOrZeroRegister(0),
                 i.InputOperand(1));
      break;
    case kRiscvAddOvf64:
      __ AddOverflow64(i.OutputRegister(), i.InputOrZeroRegister(0),
                       i.InputOperand(1), kScratchReg);
      break;
    case kRiscvSub64:
      __ Sub64(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvSubOvf64:
      __ SubOverflow64(i.OutputRegister(), i.InputOrZeroRegister(0),
                       i.InputOperand(1), kScratchReg);
      break;
    case kRiscvMulHigh32:
      __ Mulh32(i.OutputRegister(), i.InputOrZeroRegister(0),
                i.InputOperand(1));
      break;
    case kRiscvMulHighU32:
      __ Mulhu32(i.OutputRegister(), i.InputOrZeroRegister(0),
                 i.InputOperand(1), kScratchReg, kScratchReg2);
      break;
    case kRiscvMulHigh64:
      __ Mulh64(i.OutputRegister(), i.InputOrZeroRegister(0),
                i.InputOperand(1));
      break;
    case kRiscvMulHighU64:
      __ Mulhu64(i.OutputRegister(), i.InputOrZeroRegister(0),
                 i.InputOperand(1));
      break;
    case kRiscvMulOvf64:
      __ MulOverflow64(i.OutputRegister(), i.InputOrZeroRegister(0),
                       i.InputOperand(1), kScratchReg);
      break;
    case kRiscvDiv32: {
      DCHECK_NE(i.OutputRegister(), i.InputRegister(1));
      __ Div32(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      // Set ouput to zero if divisor == 0
      __ LoadZeroIfConditionZero(i.OutputRegister(), i.InputRegister(1));
      break;
    }
    case kRiscvDivU32: {
      DCHECK_NE(i.OutputRegister(), i.InputRegister(1));
      __ Divu32(i.OutputRegister(), i.InputOrZeroRegister(0),
                i.InputOperand(1));
      // Set ouput to zero if divisor == 0
      __ LoadZeroIfConditionZero(i.OutputRegister(), i.InputRegister(1));
      break;
    }
    case kRiscvMod32:
      __ Mod32(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvModU32:
      __ Modu32(i.OutputRegister(), i.InputOrZeroRegister(0),
                i.InputOperand(1));
      break;
    case kRiscvMul64:
      __ Mul64(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvDiv64: {
      DCHECK_NE(i.OutputRegister(), i.InputRegister(1));
      __ Div64(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      // Set ouput to zero if divisor == 0
      __ LoadZeroIfConditionZero(i.OutputRegister(), i.InputRegister(1));
      break;
    }
    case kRiscvDivU64: {
      DCHECK_NE(i.OutputRegister(), i.InputRegister(1));
      __ Divu64(i.OutputRegister(), i.InputOrZeroRegister(0),
                i.InputOperand(1));
      // Set ouput to zero if divisor == 0
      __ LoadZeroIfConditionZero(i.OutputRegister(), i.InputRegister(1));
      break;
    }
    case kRiscvMod64:
      __ Mod64(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvModU64:
      __ Modu64(i.OutputRegister(), i.InputOrZeroRegister(0),
                i.InputOperand(1));
      break;
#elif V8_TARGET_ARCH_RISCV32
    case kRiscvAddOvf:
      __ AddOverflow(i.OutputRegister(), i.InputOrZeroRegister(0),
                     i.InputOperand(1), kScratchReg);
      break;
    case kRiscvSubOvf:
      __ SubOverflow(i.OutputRegister(), i.InputOrZeroRegister(0),
                     i.InputOperand(1), kScratchReg);
      break;
    case kRiscvMulHigh32:
      __ Mulh(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvMulHighU32:
      __ Mulhu(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1),
               kScratchReg, kScratchReg2);
      break;
    case kRiscvDiv32: {
      __ Div(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      // Set ouput to zero if divisor == 0
      __ LoadZeroIfConditionZero(i.OutputRegister(), i.InputRegister(1));
      break;
    }
    case kRiscvDivU32: {
      __ Divu(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      // Set ouput to zero if divisor == 0
      __ LoadZeroIfConditionZero(i.OutputRegister(), i.InputRegister(1));
      break;
    }
    case kRiscvMod32:
      __ Mod(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvModU32:
      __ Modu(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
#endif
    case kRiscvAnd:
      __ And(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvAnd32:
      __ And(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      __ Sll32(i.OutputRegister(), i.OutputRegister(), 0x0);
      break;
    case kRiscvOr:
      __ Or(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvOr32:
      __ Or(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      __ Sll32(i.OutputRegister(), i.OutputRegister(), 0x0);
      break;
    case kRiscvXor:
      __ Xor(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      break;
    case kRiscvXor32:
      __ Xor(i.OutputRegister(), i.InputOrZeroRegister(0), i.InputOperand(1));
      __ Sll32(i.OutputRegister(), i.OutputRegister(), 0x0);
      break;
    case kRiscvClz32:
      __ Clz32(i.OutputRegister(), i.InputOrZeroRegister(0));
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvClz64:
      __ Clz64(i.OutputRegister(), i.InputOrZeroRegister(0));
      break;
    case kRiscvCtz64: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Ctz64(dst, src);
    } break;
    case kRiscvPopcnt64: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Popcnt64(dst, src, kScratchReg);
    } break;
#endif
    case kRiscvCtz32: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Ctz32(dst, src);
    } break;
    case kRiscvPopcnt32: {
      Register src = i.InputRegister(0);
      Register dst = i.OutputRegister();
      __ Popcnt32(dst, src, kScratchReg);
    } break;
    case kRiscvShl32:
      if (instr->InputAt(1)->IsRegister()) {
        __ Sll32(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ Sll32(i.OutputRegister(), i.InputRegister(0),
                 static_cast<uint16_t>(imm));
      }
      break;
    case kRiscvShr32:
      if (instr->InputAt(1)->IsRegister()) {
        __ Srl32(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ Srl32(i.OutputRegister(), i.InputRegister(0),
                 static_cast<uint16_t>(imm));
      }
      break;
    case kRiscvSar32:
      if (instr->InputAt(1)->IsRegister()) {
        __ Sra32(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        int64_t imm = i.InputOperand(1).immediate();
        __ Sra32(i.OutputRegister(), i.InputRegister(0),
                 static_cast<uint16_t>(imm));
      }
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvZeroExtendWord: {
      __ ZeroExtendWord(i.OutputRegister(), i.InputRegister(0));
      break;
    }
    case kRiscvSignExtendWord: {
      __ SignExtendWord(i.OutputRegister(), i.InputRegister(0));
      break;
    }
    case kRiscvShl64:
      __ Sll64(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kRiscvShr64:
      __ Srl64(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kRiscvSar64:
      __ Sra64(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kRiscvRor64:
      __ Dror(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kRiscvTst64:
      __ And(kScratchReg, i.InputRegister(0), i.InputOperand(1));
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
#endif
    case kRiscvRev8:
      __ rev8(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvAndn:
      __ andn(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kRiscvOrn:
      __ orn(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kRiscvXnor:
      __ xnor(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kRiscvClz:
      __ clz(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvCtz:
      __ ctz(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvCpop:
      __ cpop(i.OutputRegister(), i.InputRegister(0));
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvClzw:
      __ clzw(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvCtzw:
      __ ctzw(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvCpopw:
      __ cpopw(i.OutputRegister(), i.InputRegister(0));
      break;
#endif
    case kRiscvMax:
      __ max(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kRiscvMaxu:
      __ maxu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kRiscvMin:
      __ min(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kRiscvMinu:
      __ minu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      break;
    case kRiscvSextb:
      __ sextb(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvSexth:
      __ sexth(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvZexth:
      __ zexth(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvTst32:
      __ And(kScratchReg, i.InputRegister(0), i.InputOperand(1));
      __ Sll32(kScratchReg, kScratchReg, 0x0);
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kRiscvRor32:
      __ Ror(i.OutputRegister(), i.InputRegister(0), i.InputOperand(1));
      break;
    case kRiscvCmp:
#ifdef V8_TARGET_ARCH_RISCV64
    case kRiscvCmp32:
    case kRiscvCmpZero32:
#endif
      // Pseudo-instruction used for cmp/branch. No opcode emitted here.
      break;
    case kRiscvCmpZero:
      // Pseudo-instruction used for cmpzero/branch. No opcode emitted here.
      break;
    case kRiscvMov:
      // TODO(plind): Should we combine mov/li like this, or use separate instr?
      //    - Also see x64 ASSEMBLE_BINOP & RegisterOrOperandType
      if (HasRegisterInput(instr, 0)) {
        __ Move(i.OutputRegister(), i.InputRegister(0));
      } else {
        __ li(i.OutputRegister(), i.InputOperand(0));
      }
      break;

    case kRiscvCmpS: {
      FPURegister left = i.InputOrZeroSingleRegister(0);
      FPURegister right = i.InputOrZeroSingleRegister(1);
      bool predicate;
      FPUCondition cc =
          FlagsConditionToConditionCmpFPU(&predicate, instr->flags_condition());

      if ((left == kSingleRegZero || right == kSingleRegZero) &&
          !__ IsSingleZeroRegSet()) {
        __ LoadFPRImmediate(kSingleRegZero, 0.0f);
      }
      // compare result set to kScratchReg
      __ CompareF32(kScratchReg, cc, left, right);
    } break;
    case kRiscvAddS:
      // TODO(plind): add special case: combine mult & add.
      __ fadd_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvSubS:
      __ fsub_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvMulS:
      // TODO(plind): add special case: right op is -1.0, see arm port.
      __ fmul_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvDivS:
      __ fdiv_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvModS: {
      // TODO(bmeurer): We should really get rid of this special instruction,
      // and generate a CallAddress instruction instead.
      FrameScope scope(masm(), StackFrame::MANUAL);
      __ PrepareCallCFunction(0, 2, kScratchReg);
      __ MovToFloatParameters(i.InputDoubleRegister(0),
                              i.InputDoubleRegister(1));
      // TODO(balazs.kilvady): implement mod_two_floats_operation(isolate())
      __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2);
      // Move the result in the double result register.
      __ MovFromFloatResult(i.OutputSingleRegister());
      break;
    }
    case kRiscvAbsS:
      __ fabs_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    case kRiscvNegS:
      __ Neg_s(i.OutputSingleRegister(), i.InputSingleRegister(0));
      break;
    case kRiscvSqrtS: {
      __ fsqrt_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kRiscvMaxS:
      __ fmax_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvMinS:
      __ fmin_s(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvCmpD: {
      FPURegister left = i.InputOrZeroDoubleRegister(0);
      FPURegister right = i.InputOrZeroDoubleRegister(1);
      bool predicate;
      FPUCondition cc =
          FlagsConditionToConditionCmpFPU(&predicate, instr->flags_condition());
      if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
          !__ IsDoubleZeroRegSet()) {
        __ LoadFPRImmediate(kDoubleRegZero, 0.0);
      }
      // compare result set to kScratchReg
      __ CompareF64(kScratchReg, cc, left, right);
    } break;
#if V8_TARGET_ARCH_RISCV32
    case kRiscvAddPair:
      __ AddPair(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
                 i.InputRegister(1), i.InputRegister(2), i.InputRegister(3),
                 kScratchReg, kScratchReg2);
      break;
    case kRiscvSubPair:
      __ SubPair(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
                 i.InputRegister(1), i.InputRegister(2), i.InputRegister(3),
                 kScratchReg, kScratchReg2);
      break;
    case kRiscvAndPair:
      __ AndPair(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
                 i.InputRegister(1), i.InputRegister(2), i.InputRegister(3));
      break;
    case kRiscvOrPair:
      __ OrPair(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
                i.InputRegister(1), i.InputRegister(2), i.InputRegister(3));
      break;
    case kRiscvXorPair:
      __ XorPair(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
                 i.InputRegister(1), i.InputRegister(2), i.InputRegister(3));
      break;
    case kRiscvMulPair:
      __ MulPair(i.OutputRegister(0), i.OutputRegister(1), i.InputRegister(0),
                 i.InputRegister(1), i.InputRegister(2), i.InputRegister(3),
                 kScratchReg, kScratchReg2);
      break;
    case kRiscvShlPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsRegister()) {
        __ ShlPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputRegister(2), kScratchReg,
                   kScratchReg2);
      } else {
        uint32_t imm = i.InputOperand(2).immediate();
        __ ShlPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), imm, kScratchReg, kScratchReg2);
      }
    } break;
    case kRiscvShrPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsRegister()) {
        __ ShrPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputRegister(2), kScratchReg,
                   kScratchReg2);
      } else {
        uint32_t imm = i.InputOperand(2).immediate();
        __ ShrPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), imm, kScratchReg, kScratchReg2);
      }
    } break;
    case kRiscvSarPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsRegister()) {
        __ SarPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), i.InputRegister(2), kScratchReg,
                   kScratchReg2);
      } else {
        uint32_t imm = i.InputOperand(2).immediate();
        __ SarPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                   i.InputRegister(1), imm, kScratchReg, kScratchReg2);
      }
    } break;
#endif
    case kRiscvAddD:
      // TODO(plind): add special case: combine mult & add.
      __ fadd_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvSubD:
      __ fsub_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvMulD:
      // TODO(plind): add special case: right op is -1.0, see arm port.
      __ fmul_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvDivD:
      __ fdiv_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvModD: {
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
    case kRiscvAbsD:
      __ fabs_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kRiscvNegD:
      __ Neg_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kRiscvSqrtD: {
      __ fsqrt_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    }
    case kRiscvMaxD:
      __ fmax_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
    case kRiscvMinD:
      __ fmin_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1));
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvFloat64RoundDown: {
      __ Floor_d_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                   kScratchDoubleReg);
      break;
    }
    case kRiscvFloat64RoundTruncate: {
      __ Trunc_d_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                   kScratchDoubleReg);
      break;
    }
    case kRiscvFloat64RoundUp: {
      __ Ceil_d_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                  kScratchDoubleReg);
      break;
    }
    case kRiscvFloat64RoundTiesEven: {
      __ Round_d_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                   kScratchDoubleReg);
      break;
    }
#endif
    case kRiscvFloat32RoundDown: {
      __ Floor_s_s(i.OutputSingleRegister(), i.InputSingleRegister(0),
                   kScratchDoubleReg);
      break;
    }
    case kRiscvFloat32RoundTruncate: {
      __ Trunc_s_s(i.OutputSingleRegister(), i.InputSingleRegister(0),
                   kScratchDoubleReg);
      break;
    }
    case kRiscvFloat32RoundUp: {
      __ Ceil_s_s(i.OutputSingleRegister(), i.InputSingleRegister(0),
                  kScratchDoubleReg);
      break;
    }
    case kRiscvFloat32RoundTiesEven: {
      __ Round_s_s(i.OutputSingleRegister(), i.InputSingleRegister(0),
                   kScratchDoubleReg);
      break;
    }
    case kRiscvFloat32Max: {
      __ Float32Max(i.OutputSingleRegister(), i.InputSingleRegister(0),
                    i.InputSingleRegister(1));
      break;
    }
    case kRiscvFloat64Max: {
      __ Float64Max(i.OutputSingleRegister(), i.InputSingleRegister(0),
                    i.InputSingleRegister(1));
      break;
    }
    case kRiscvFloat32Min: {
      __ Float32Min(i.OutputSingleRegister(), i.InputSingleRegister(0),
                    i.InputSingleRegister(1));
      break;
    }
    case kRiscvFloat64Min: {
      __ Float64Min(i.OutputSingleRegister(), i.InputSingleRegister(0),
                    i.InputSingleRegister(1));
      break;
    }
    case kRiscvFloat64SilenceNaN:
      __ FPUCanonicalizeNaN(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kRiscvCvtSD: {
      Label done;
      __ feq_d(kScratchReg, i.InputDoubleRegister(0), i.InputDoubleRegister(0));
#if V8_TARGET_ARCH_RISCV64
      __ fmv_x_d(kScratchReg2, i.InputDoubleRegister(0));
#elif V8_TARGET_ARCH_RISCV32
      __ StoreDouble(i.InputDoubleRegister(0),
                     MemOperand(sp, -kDoubleSize));  // store whole 64 bit
#endif
      __ fcvt_s_d(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      __ Branch(&done, ne, kScratchReg, Operand(zero_reg));
#if V8_TARGET_ARCH_RISCV64
      __ And(kScratchReg2, kScratchReg2, Operand(0x8000000000000000));
      __ srai(kScratchReg2, kScratchReg2, 32);
      __ fmv_d_x(kScratchDoubleReg, kScratchReg2);
#elif V8_TARGET_ARCH_RISCV32
      __ Lw(kScratchReg2,
            MemOperand(sp,
                       -kDoubleSize /
                           2));  // only load the high half to get the sign bit
      __ fmv_w_x(kScratchDoubleReg, kScratchReg2);
#endif
      __ fsgnj_s(i.OutputDoubleRegister(), i.OutputDoubleRegister(),
                 kScratchDoubleReg);
      __ bind(&done);
      break;
    }
    case kRiscvCvtDS: {
      Label done;
      __ feq_s(kScratchReg, i.InputDoubleRegister(0), i.InputDoubleRegister(0));
#if V8_TARGET_ARCH_RISCV64
      __ fmv_x_d(kScratchReg2, i.InputDoubleRegister(0));
#elif V8_TARGET_ARCH_RISCV32
      __ StoreFloat(i.InputDoubleRegister(0), MemOperand(sp, -kFloatSize));
#endif
      __ fcvt_d_s(i.OutputDoubleRegister(), i.InputSingleRegister(0));
      __ Branch(&done, ne, kScratchReg, Operand(zero_reg));
#if V8_TARGET_ARCH_RISCV64
      __ And(kScratchReg2, kScratchReg2, Operand(0x80000000));
      __ slli(kScratchReg2, kScratchReg2, 32);
      __ fmv_d_x(kScratchDoubleReg, kScratchReg2);
#elif V8_TARGET_ARCH_RISCV32
      __ Lw(kScratchReg2, MemOperand(sp, -kFloatSize));
      __ fcvt_d_w(kScratchDoubleReg, kScratchReg2);
#endif
      __ fsgnj_d(i.OutputDoubleRegister(), i.OutputDoubleRegister(),
                 kScratchDoubleReg);
      __ bind(&done);
      break;
    }
    case kRiscvCvtDW: {
      __ fcvt_d_w(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kRiscvCvtSW: {
      __ fcvt_s_w(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kRiscvCvtSUw: {
      __ Cvt_s_uw(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case kRiscvCvtSL: {
      __ fcvt_s_l(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kRiscvCvtDL: {
      __ fcvt_d_l(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kRiscvCvtDUl: {
      __ Cvt_d_ul(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kRiscvCvtSUl: {
      __ Cvt_s_ul(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
#endif
    case kRiscvCvtDUw: {
      __ Cvt_d_uw(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    }
    case kRiscvFloorWD: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Floor_w_d(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvCeilWD: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Ceil_w_d(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvRoundWD: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Round_w_d(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvTruncWD: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Trunc_w_d(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvFloorWS: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Floor_w_s(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvCeilWS: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Ceil_w_s(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvRoundWS: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Round_w_s(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvTruncWS: {
      Label done;
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      __ Trunc_w_s(i.OutputRegister(), i.InputDoubleRegister(0), result);

      // On RISCV, if the input value exceeds INT32_MAX, the result of fcvt
      // is INT32_MAX. Note that, since INT32_MAX means the lower 31-bits are
      // all 1s, INT32_MAX cannot be represented precisely as a float, so an
      // fcvt result of INT32_MAX always indicate overflow.
      //
      // In wasm_compiler, to detect overflow in converting a FP value, fval, to
      // integer, V8 checks whether I2F(F2I(fval)) equals fval. However, if fval
      // == INT32_MAX+1, the value of I2F(F2I(fval)) happens to be fval. So,
      // INT32_MAX is not a good value to indicate overflow. Instead, we will
      // use INT32_MIN as the converted result of an out-of-range FP value,
      // exploiting the fact that INT32_MAX+1 is INT32_MIN.
      //
      // If the result of conversion overflow, the result will be set to
      // INT32_MIN. Here we detect overflow by testing whether output + 1 <
      // output (i.e., kScratchReg  < output)
      if (set_overflow_to_min_i32) {
        __ Add32(kScratchReg, i.OutputRegister(), 1);
        __ BranchShort(&done, lt, i.OutputRegister(), Operand(kScratchReg));
        __ Move(i.OutputRegister(), kScratchReg);
        __ bind(&done);
      }
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case kRiscvTruncLS: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Trunc_l_s(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvTruncLD: {
      Label done;
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      bool set_overflow_to_min_i64 = MiscField::decode(instr->opcode());
      __ Trunc_l_d(i.OutputRegister(), i.InputDoubleRegister(0), result);
      if (set_overflow_to_min_i64) {
        __ AddWord(kScratchReg, i.OutputRegister(), 1);
        __ BranchShort(&done, lt, i.OutputRegister(), Operand(kScratchReg));
        __ Move(i.OutputRegister(), kScratchReg);
        __ bind(&done);
      }
      break;
    }
#endif
    case kRiscvTruncUwD: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Trunc_uw_d(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvTruncUwS: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      bool set_overflow_to_min_u32 = MiscField::decode(instr->opcode());
      __ Trunc_uw_s(i.OutputRegister(), i.InputDoubleRegister(0), result);

      // On RISCV, if the input value exceeds UINT32_MAX, the result of fcvt
      // is UINT32_MAX. Note that, since UINT32_MAX means all 32-bits are 1s,
      // UINT32_MAX cannot be represented precisely as float, so an fcvt result
      // of UINT32_MAX always indicates overflow.
      //
      // In wasm_compiler.cc, to detect overflow in converting a FP value, fval,
      // to integer, V8 checks whether I2F(F2I(fval)) equals fval. However, if
      // fval == UINT32_MAX+1, the value of I2F(F2I(fval)) happens to be fval.
      // So, UINT32_MAX is not a good value to indicate overflow. Instead, we
      // will use 0 as the converted result of an out-of-range FP value,
      // exploiting the fact that UINT32_MAX+1 is 0.
      if (set_overflow_to_min_u32) {
        __ Add32(kScratchReg, i.OutputRegister(), 1);
        // Set ouput to zero if result overflows (i.e., UINT32_MAX)
        __ LoadZeroIfConditionZero(i.OutputRegister(), kScratchReg);
      }
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case kRiscvTruncUlS: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Trunc_ul_s(i.OutputRegister(), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvTruncUlD: {
      Register result = instr->OutputCount() > 1 ? i.OutputRegister(1) : no_reg;
      __ Trunc_ul_d(i.OutputRegister(0), i.InputDoubleRegister(0), result);
      break;
    }
    case kRiscvBitcastDL:
      __ fmv_x_d(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kRiscvBitcastLD:
      __ fmv_d_x(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
#endif
    case kRiscvBitcastInt32ToFloat32:
      __ fmv_w_x(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kRiscvBitcastFloat32ToInt32:
      __ fmv_x_w(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kRiscvFloat64ExtractLowWord32:
      __ ExtractLowWordFromF64(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kRiscvFloat64ExtractHighWord32:
      __ ExtractHighWordFromF64(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kRiscvFloat64InsertLowWord32:
      __ InsertLowWordF64(i.OutputDoubleRegister(), i.InputRegister(1));
      break;
    case kRiscvFloat64InsertHighWord32:
      __ InsertHighWordF64(i.OutputDoubleRegister(), i.InputRegister(1));
      break;
      // ... more basic instructions ...

    case kRiscvSignExtendByte:
      __ SignExtendByte(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvSignExtendShort:
      __ SignExtendShort(i.OutputRegister(), i.InputRegister(0));
      break;
    case kRiscvLbu:
      __ Lbu(i.OutputRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvLb:
      __ Lb(i.OutputRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvSb:
      __ Sb(i.InputOrZeroRegister(0), i.MemoryOperand(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvLhu:
      __ Lhu(i.OutputRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvUlhu:
      __ Ulhu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kRiscvLh:
      __ Lh(i.OutputRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvUlh:
      __ Ulh(i.OutputRegister(), i.MemoryOperand());
      break;
    case kRiscvSh:
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ Sh(i.InputOrZeroRegister(0), i.MemoryOperand(1));
      break;
    case kRiscvUsh:
      __ Ush(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kRiscvLw:
      __ Lw(i.OutputRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvUlw:
      __ Ulw(i.OutputRegister(), i.MemoryOperand());
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvLwu:
      __ Lwu(i.OutputRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvUlwu:
      __ Ulwu(i.OutputRegister(), i.MemoryOperand());
      break;
    case kRiscvLd:
      __ Ld(i.OutputRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvUld:
      __ Uld(i.OutputRegister(), i.MemoryOperand());
      break;
    case kRiscvSd:
      __ Sd(i.InputOrZeroRegister(0), i.MemoryOperand(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvUsd:
      __ Usd(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
#endif
    case kRiscvSw:
      __ Sw(i.InputOrZeroRegister(0), i.MemoryOperand(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvUsw:
      __ Usw(i.InputOrZeroRegister(2), i.MemoryOperand());
      break;
    case kRiscvLoadFloat: {
      __ LoadFloat(i.OutputSingleRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    }
    case kRiscvULoadFloat: {
      __ ULoadFloat(i.OutputSingleRegister(), i.MemoryOperand(), kScratchReg);
      break;
    }
    case kRiscvStoreFloat: {
      MemOperand operand = i.MemoryOperand(1);
      FPURegister ft = i.InputOrZeroSingleRegister(0);
      if (ft == kSingleRegZero && !__ IsSingleZeroRegSet()) {
        __ LoadFPRImmediate(kSingleRegZero, 0.0f);
      }
      __ StoreFloat(ft, operand);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    }
    case kRiscvUStoreFloat: {
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&index);
      FPURegister ft = i.InputOrZeroSingleRegister(index);
      if (ft == kSingleRegZero && !__ IsSingleZeroRegSet()) {
        __ LoadFPRImmediate(kSingleRegZero, 0.0f);
      }
      __ UStoreFloat(ft, operand, kScratchReg);
      break;
    }
    case kRiscvLoadDouble:
      __ LoadDouble(i.OutputDoubleRegister(), i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    case kRiscvULoadDouble:
      __ ULoadDouble(i.OutputDoubleRegister(), i.MemoryOperand(), kScratchReg);
      break;
    case kRiscvStoreDouble: {
      FPURegister ft = i.InputOrZeroDoubleRegister(0);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ LoadFPRImmediate(kDoubleRegZero, 0.0);
      }
      __ StoreDouble(ft, i.MemoryOperand(1));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    }
    case kRiscvUStoreDouble: {
      FPURegister ft = i.InputOrZeroDoubleRegister(2);
      if (ft == kDoubleRegZero && !__ IsDoubleZeroRegSet()) {
        __ LoadFPRImmediate(kDoubleRegZero, 0.0);
      }
      __ UStoreDouble(ft, i.MemoryOperand(), kScratchReg);
      break;
    }
    case kRiscvSync: {
      __ sync();
      break;
    }
    case kRiscvPush:
      if (instr->InputAt(0)->IsFPRegister()) {
        __ StoreDouble(i.InputDoubleRegister(0), MemOperand(sp, -kDoubleSize));
        __ Sub32(sp, sp, Operand(kDoubleSize));
        frame_access_state()->IncreaseSPDelta(kDoubleSize / kSystemPointerSize);
      } else {
        __ Push(i.InputOrZeroRegister(0));
        frame_access_state()->IncreaseSPDelta(1);
      }
      break;
    case kRiscvPeek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ LoadDouble(i.OutputDoubleRegister(), MemOperand(fp, offset));
        } else {
          DCHECK_EQ(op->representation(), MachineRepresentation::kFloat32);
          __ LoadFloat(
              i.OutputSingleRegister(0),
              MemOperand(fp, offset + kLessSignificantWordInDoublewordOffset));
        }
      } else {
        __ LoadWord(i.OutputRegister(0), MemOperand(fp, offset));
      }
      break;
    }
    case kRiscvStackClaim: {
      __ SubWord(sp, sp, Operand(i.InputInt32(0)));
      frame_access_state()->IncreaseSPDelta(i.InputInt32(0) /
                                            kSystemPointerSize);
      break;
    }
    case kRiscvStoreToStackSlot: {
      if (instr->InputAt(0)->IsFPRegister()) {
        if (instr->InputAt(0)->IsSimd128Register()) {
          Register dst = sp;
          if (i.InputInt32(1) != 0) {
            dst = kScratchReg2;
            __ AddWord(kScratchReg2, sp, Operand(i.InputInt32(1)));
          }
          __ VU.set(kScratchReg, E8, m1);
          __ vs(i.InputSimd128Register(0), dst, 0, E8);
        } else {
#if V8_TARGET_ARCH_RISCV64
          __ StoreDouble(i.InputDoubleRegister(0),
                         MemOperand(sp, i.InputInt32(1)));
#elif V8_TARGET_ARCH_RISCV32
          if (instr->InputAt(0)->IsDoubleRegister()) {
            __ StoreDouble(i.InputDoubleRegister(0),
                           MemOperand(sp, i.InputInt32(1)));
          } else if (instr->InputAt(0)->IsFloatRegister()) {
            __ StoreFloat(i.InputSingleRegister(0),
                          MemOperand(sp, i.InputInt32(1)));
          }
#endif
        }
      } else {
        __ StoreWord(i.InputOrZeroRegister(0), MemOperand(sp, i.InputInt32(1)));
      }
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case kRiscvByteSwap64: {
      __ ByteSwap(i.OutputRegister(0), i.InputRegister(0), 8, kScratchReg);
      break;
    }
#endif
    case kRiscvByteSwap32: {
      __ ByteSwap(i.OutputRegister(0), i.InputRegister(0), 4, kScratchReg);
      break;
    }
    case kAtomicLoadInt8:
#if V8_TARGET_ARCH_RISCV64
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
#endif
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lb);
      break;
    case kAtomicLoadUint8:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lbu);
      break;
    case kAtomicLoadInt16:
#if V8_TARGET_ARCH_RISCV64
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
#endif
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lh);
      break;
    case kAtomicLoadUint16:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lhu);
      break;
    case kAtomicLoadWord32:
#if V8_TARGET_ARCH_RISCV64
      if (AtomicWidthField::decode(opcode) == AtomicWidth::kWord64) {
        ASSEMBLE_ATOMIC_LOAD_INTEGER(Lwu);
        break;
      }
#endif  // V8_TARGET_ARCH_RISCV64
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Lw);
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvWord64AtomicLoadUint64:
      ASSEMBLE_ATOMIC_LOAD_INTEGER(Ld);
      break;
    case kRiscvWord64AtomicStoreWord64:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Sd);
      break;
#endif
    case kAtomicStoreWord8:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Sb);
      break;
    case kAtomicStoreWord16:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Sh);
      break;
    case kAtomicStoreWord32:
      ASSEMBLE_ATOMIC_STORE_INTEGER(Sw);
      break;
#if V8_TARGET_ARCH_RISCV32
    case kRiscvWord32AtomicPairLoad: {
      FrameScope scope(masm(), StackFrame::MANUAL);
      __ AddWord(a0, i.InputRegister(0), i.InputRegister(1));
      __ PushCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);
      __ PrepareCallCFunction(1, 0, kScratchReg);
      __ CallCFunction(ExternalReference::atomic_pair_load_function(), 1, 0);
      __ PopCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);
      break;
    }
    case kRiscvWord32AtomicPairStore: {
      FrameScope scope(masm(), StackFrame::MANUAL);
      __ AddWord(a0, i.InputRegister(0), i.InputRegister(1));
      __ PushCallerSaved(SaveFPRegsMode::kIgnore);
      __ PrepareCallCFunction(3, 0, kScratchReg);
      __ CallCFunction(ExternalReference::atomic_pair_store_function(), 3, 0);
      __ PopCallerSaved(SaveFPRegsMode::kIgnore);
      break;
    }
#define ATOMIC64_BINOP_ARITH_CASE(op, instr, external) \
  case kRiscvWord32AtomicPair##op:                     \
    ASSEMBLE_ATOMIC64_ARITH_BINOP(instr, external);    \
    break;
      ATOMIC64_BINOP_ARITH_CASE(Add, AddPair, atomic_pair_add_function)
      ATOMIC64_BINOP_ARITH_CASE(Sub, SubPair, atomic_pair_sub_function)
#undef ATOMIC64_BINOP_ARITH_CASE
#define ATOMIC64_BINOP_LOGIC_CASE(op, instr, external) \
  case kRiscvWord32AtomicPair##op:                     \
    ASSEMBLE_ATOMIC64_LOGIC_BINOP(instr, external);    \
    break;
      ATOMIC64_BINOP_LOGIC_CASE(And, AndPair, atomic_pair_and_function)
      ATOMIC64_BINOP_LOGIC_CASE(Or, OrPair, atomic_pair_or_function)
      ATOMIC64_BINOP_LOGIC_CASE(Xor, XorPair, atomic_pair_xor_function)
    case kRiscvWord32AtomicPairExchange: {
      FrameScope scope(masm(), StackFrame::MANUAL);
      __ PushCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);
      __ PrepareCallCFunction(3, 0, kScratchReg);
      __ AddWord(a0, i.InputRegister(0), i.InputRegister(1));
      __ CallCFunction(ExternalReference::atomic_pair_exchange_function(), 3,
                       0);
      __ PopCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);
      break;
    }
    case kRiscvWord32AtomicPairCompareExchange: {
      FrameScope scope(masm(), StackFrame::MANUAL);
      __ PushCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);
      __ PrepareCallCFunction(5, 0, kScratchReg);
      __ add(a0, i.InputRegister(0), i.InputRegister(1));
      __ CallCFunction(
          ExternalReference::atomic_pair_compare_exchange_function(), 5, 0);
      __ PopCallerSaved(SaveFPRegsMode::kIgnore, a0, a1);
      break;
    }
#endif
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
#if V8_TARGET_ARCH_RISCV64
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 8, 64);
          break;
#endif
        default:
          UNREACHABLE();
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
#if V8_TARGET_ARCH_RISCV64
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 16, 64);
          break;
#endif
        default:
          UNREACHABLE();
      }
      break;
    case kAtomicExchangeWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(Ll, Sc);
          break;
#if V8_TARGET_ARCH_RISCV64
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 32, 64);
          break;
#endif
        default:
          UNREACHABLE();
      }
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvWord64AtomicExchangeUint64:
      ASSEMBLE_ATOMIC_EXCHANGE_INTEGER(Lld, Scd);
      break;
#endif
    case kAtomicCompareExchangeInt8:
      DCHECK_EQ(AtomicWidthField::decode(opcode), AtomicWidth::kWord32);
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, true, 8, 32);
      break;
    case kAtomicCompareExchangeUint8:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Ll, Sc, false, 8, 32);
          break;
#if V8_TARGET_ARCH_RISCV64
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 8, 64);
          break;
#endif
        default:
          UNREACHABLE();
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
#if V8_TARGET_ARCH_RISCV64
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 16, 64);
          break;
#endif
        default:
          UNREACHABLE();
      }
      break;
    case kAtomicCompareExchangeWord32:
      switch (AtomicWidthField::decode(opcode)) {
        case AtomicWidth::kWord32:
          __ Sll32(i.InputRegister(2), i.InputRegister(2), 0);
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER(Ll, Sc);
          break;
#if V8_TARGET_ARCH_RISCV64
        case AtomicWidth::kWord64:
          ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_INTEGER_EXT(Lld, Scd, false, 32, 64);
          break;
#endif
        default:
          UNREACHABLE();
      }
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvWord64AtomicCompareExchangeUint64:
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
  case kRiscvWord64Atomic##op##Uint64:                                 \
    ASSEMBLE_ATOMIC_BINOP(Lld, Scd, inst64);                           \
    break;
      ATOMIC_BINOP_CASE(Add, Add32, AddWord)
      ATOMIC_BINOP_CASE(Sub, Sub32, Sub64)
      ATOMIC_BINOP_CASE(And, And, And)
      ATOMIC_BINOP_CASE(Or, Or, Or)
      ATOMIC_BINOP_CASE(Xor, Xor, Xor)
#undef ATOMIC_BINOP_CASE
#elif V8_TARGET_ARCH_RISCV32
#define ATOMIC_BINOP_CASE(op, inst32, inst64, amoinst32)                   \
  case kAtomic##op##Int8:                                                  \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, true, 8, inst32, 32);                \
    break;                                                                 \
  case kAtomic##op##Uint8:                                                 \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, false, 8, inst32, 32);               \
    break;                                                                 \
  case kAtomic##op##Int16:                                                 \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, true, 16, inst32, 32);               \
    break;                                                                 \
  case kAtomic##op##Uint16:                                                \
    ASSEMBLE_ATOMIC_BINOP_EXT(Ll, Sc, false, 16, inst32, 32);              \
    break;                                                                 \
  case kAtomic##op##Word32:                                                \
    __ AddWord(i.TempRegister(0), i.InputRegister(0), i.InputRegister(1)); \
    __ amoinst32(true, true, i.OutputRegister(0), i.TempRegister(0),       \
                 i.InputRegister(2));                                      \
    break;
      ATOMIC_BINOP_CASE(Add, Add32, Add64, amoadd_w)  // todo: delete 64
      ATOMIC_BINOP_CASE(Sub, Sub32, Sub64, Amosub_w)  // todo: delete 64
      ATOMIC_BINOP_CASE(And, And, And, amoand_w)
      ATOMIC_BINOP_CASE(Or, Or, Or, amoor_w)
      ATOMIC_BINOP_CASE(Xor, Xor, Xor, amoxor_w)
#undef ATOMIC_BINOP_CASE
#endif
    case kRiscvAssertEqual:
      __ Assert(eq, static_cast<AbortReason>(i.InputOperand(2).immediate()),
                i.InputRegister(0), Operand(i.InputRegister(1)));
      break;
#if V8_TARGET_ARCH_RISCV64
    case kRiscvStoreCompressTagged: {
      MemOperand mem = i.MemoryOperand(1);
      __ StoreTaggedField(i.InputOrZeroRegister(0), mem);
      break;
    }
    case kRiscvLoadDecompressTaggedSigned: {
      CHECK(instr->HasOutput());
      Register result = i.OutputRegister();
      MemOperand operand = i.MemoryOperand();
      __ DecompressTaggedSigned(result, operand);
      break;
    }
    case kRiscvLoadDecompressTagged: {
      CHECK(instr->HasOutput());
      Register result = i.OutputRegister();
      MemOperand operand = i.MemoryOperand();
      __ DecompressTagged(result, operand);
      break;
    }
    case kRiscvLoadDecodeSandboxedPointer:
      __ LoadSandboxedPointerField(i.OutputRegister(), i.MemoryOperand());
      break;
    case kRiscvStoreEncodeSandboxedPointer: {
      MemOperand mem = i.MemoryOperand(1);
      __ StoreSandboxedPointerField(i.InputOrZeroRegister(0), mem);
      break;
    }
    case kRiscvStoreIndirectPointer: {
      MemOperand mem = i.MemoryOperand(1);
      __ StoreIndirectPointerField(i.InputOrZeroRegister(0), mem);
      break;
    }
    case kRiscvAtomicLoadDecompressTaggedSigned:
      __ AtomicDecompressTaggedSigned(i.OutputRegister(), i.MemoryOperand());
      break;
    case kRiscvAtomicLoadDecompressTagged:
      __ AtomicDecompressTagged(i.OutputRegister(), i.MemoryOperand());
      break;
    case kRiscvAtomicStoreCompressTagged: {
      size_t index = 0;
      MemOperand mem = i.MemoryOperand(&index);
      __ AtomicStoreTaggedField(i.InputOrZeroRegister(index), mem);
      break;
    }
    case kRiscvLoadDecompressProtected: {
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ DecompressProtected(i.OutputRegister(), i.MemoryOperand());
      break;
    }
#endif
    case kRiscvRvvSt: {
      (__ VU).set(kScratchReg, VSew::E8, Vlmul::m1);
      auto memOperand = i.MemoryOperand(1);
      Register dst = memOperand.offset() == 0 ? memOperand.rm() : kScratchReg;
      if (memOperand.offset() != 0) {
        __ AddWord(dst, memOperand.rm(), memOperand.offset());
      }
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ vs(i.InputSimd128Register(0), dst, 0, VSew::E8);
      break;
    }
    case kRiscvRvvLd: {
      (__ VU).set(kScratchReg, VSew::E8, Vlmul::m1);
      Register src = i.MemoryOperand().offset() == 0 ? i.MemoryOperand().rm()
                                                     : kScratchReg;
      if (i.MemoryOperand().offset() != 0) {
        __ AddWord(src, i.MemoryOperand().rm(), i.MemoryOperand().offset());
      }
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ vl(i.OutputSimd128Register(), src, 0, VSew::E8);
      break;
    }
    case kRiscvS128Zero: {
      Simd128Register dst = i.OutputSimd128Register();
      __ VU.set(kScratchReg, E8, m1);
      __ vmv_vx(dst, zero_reg);
      break;
    }
    case kRiscvS128Load32Zero: {
      Simd128Register dst = i.OutputSimd128Register();
      __ VU.set(kScratchReg, E32, m1);
      __ Load32U(kScratchReg, i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      __ vmv_sx(dst, kScratchReg);
      break;
    }
    case kRiscvS128Load64Zero: {
      Simd128Register dst = i.OutputSimd128Register();
      __ VU.set(kScratchReg, E64, m1);
#if V8_TARGET_ARCH_RISCV64
      __ LoadWord(kScratchReg, i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      __ vmv_sx(dst, kScratchReg);
#elif V8_TARGET_ARCH_RISCV32
      __ LoadDouble(kScratchDoubleReg, i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      __ vfmv_sf(dst, kScratchDoubleReg);
#endif
      break;
    }
    case kRiscvS128LoadLane: {
      Simd128Register dst = i.OutputSimd128Register();
      DCHECK_EQ(dst, i.InputSimd128Register(0));
      auto sz = LaneSizeField::decode(opcode);
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr, __ pc_offset());
      __ LoadLane(sz, dst, i.InputUint8(1), i.MemoryOperand(2));
      break;
    }
    case kRiscvS128StoreLane: {
      Simd128Register src = i.InputSimd128Register(0);
      DCHECK_EQ(src, i.InputSimd128Register(0));
      auto sz = LaneSizeField::decode(opcode);
      __ StoreLane(sz, src, i.InputUint8(1), i.MemoryOperand(2));
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      break;
    }
    case kRiscvS128Load64ExtendS: {
      __ VU.set(kScratchReg, E64, m1);
#if V8_TARGET_ARCH_RISCV64
      __ LoadWord(kScratchReg, i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      __ vmv_vx(kSimd128ScratchReg, kScratchReg);
#elif V8_TARGET_ARCH_RISCV32
      __ LoadDouble(kScratchDoubleReg, i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      __ vfmv_vf(kSimd128ScratchReg, kScratchDoubleReg);
#endif
      __ VU.set(kScratchReg, i.InputInt8(2), m1);
      __ vsext_vf2(i.OutputSimd128Register(), kSimd128ScratchReg);
      break;
    }
    case kRiscvS128Load64ExtendU: {
      __ VU.set(kScratchReg, E64, m1);
#if V8_TARGET_ARCH_RISCV64
      __ LoadWord(kScratchReg, i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      __ vmv_vx(kSimd128ScratchReg, kScratchReg);
#elif V8_TARGET_ARCH_RISCV32
      __ LoadDouble(kScratchDoubleReg, i.MemoryOperand());
      RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                             (__ pc_offset() - kInstrSize));
      __ vfmv_vf(kSimd128ScratchReg, kScratchDoubleReg);
#endif
      __ VU.set(kScratchReg, i.InputInt8(2), m1);
      __ vzext_vf2(i.OutputSimd128Register(), kSimd128ScratchReg);
      break;
    }
    case kRiscvS128LoadSplat: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      switch (i.InputInt8(2)) {
        case E8:
          __ Lb(kScratchReg, i.MemoryOperand());
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                                 (__ pc_offset() - kInstrSize));
          __ vmv_vx(i.OutputSimd128Register(), kScratchReg);
          break;
        case E16:
          __ Lh(kScratchReg, i.MemoryOperand());
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                                 (__ pc_offset() - kInstrSize));
          __ vmv_vx(i.OutputSimd128Register(), kScratchReg);
          break;
        case E32:
          __ Lw(kScratchReg, i.MemoryOperand());
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                                 (__ pc_offset() - kInstrSize));
          __ vmv_vx(i.OutputSimd128Register(), kScratchReg);
          break;
        case E64:
#if V8_TARGET_ARCH_RISCV64
          __ LoadWord(kScratchReg, i.MemoryOperand());
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                                 (__ pc_offset() - kInstrSize));
          __ vmv_vx(i.OutputSimd128Register(), kScratchReg);
#elif V8_TARGET_ARCH_RISCV32
          __ LoadDouble(kScratchDoubleReg, i.MemoryOperand());
          RecordTrapInfoIfNeeded(zone(), this, opcode, instr,
                                 (__ pc_offset() - kInstrSize));
          __ vfmv_vf(i.OutputSimd128Register(), kScratchDoubleReg);
#endif
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
    case kRiscvS128AllOnes: {
      __ VU.set(kScratchReg, E8, m1);
      __ vmv_vx(i.OutputSimd128Register(), zero_reg);
      __ vnot_vv(i.OutputSimd128Register(), i.OutputSimd128Register());
      break;
    }
    case kRiscvS128Select: {
      __ VU.set(kScratchReg, E8, m1);
      __ vand_vv(kSimd128ScratchReg, i.InputSimd128Register(1),
                 i.InputSimd128Register(0));
      __ vnot_vv(kSimd128ScratchReg2, i.InputSimd128Register(0));
      __ vand_vv(kSimd128ScratchReg2, i.InputSimd128Register(2),
                 kSimd128ScratchReg2);
      __ vor_vv(i.OutputSimd128Register(), kSimd128ScratchReg,
                kSimd128ScratchReg2);
      break;
    }
    case kRiscvVnot: {
      (__ VU).set(kScratchReg, VSew::E8, Vlmul::m1);
      __ vnot_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvS128Const: {
      Simd128Register dst = i.OutputSimd128Register();
      uint8_t imm[16];
      *reinterpret_cast<uint64_t*>(imm) =
          make_uint64(i.InputUint32(1), i.InputUint32(0));
      *(reinterpret_cast<uint64_t*>(imm) + 1) =
          make_uint64(i.InputUint32(3), i.InputUint32(2));
      __ WasmRvvS128const(dst, imm);
      break;
    }
    case kRiscvVrgather: {
      Simd128Register index = i.InputSimd128Register(0);
      if (!(instr->InputAt(1)->IsImmediate())) {
        index = i.InputSimd128Register(1);
      } else {
#if V8_TARGET_ARCH_RISCV64
        __ VU.set(kScratchReg, E64, m1);
        __ li(kScratchReg, i.InputInt64(1));
        __ vmv_vi(kSimd128ScratchReg3, -1);
        __ vmv_sx(kSimd128ScratchReg3, kScratchReg);
        index = kSimd128ScratchReg3;
#elif V8_TARGET_ARCH_RISCV32
        int64_t intput_int64 = i.InputInt64(1);
        int32_t input_int32[2];
        memcpy(input_int32, &intput_int64, sizeof(intput_int64));
        __ VU.set(kScratchReg, E32, m1);
        __ li(kScratchReg, input_int32[1]);
        __ vmv_vx(kSimd128ScratchReg3, kScratchReg);
        __ li(kScratchReg, input_int32[0]);
        __ vmv_sx(kSimd128ScratchReg3, kScratchReg);
        index = kSimd128ScratchReg3;
#endif
      }
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (i.OutputSimd128Register() == i.InputSimd128Register(0)) {
        __ vrgather_vv(kSimd128ScratchReg, i.InputSimd128Register(0), index);
        __ vmv_vv(i.OutputSimd128Register(), kSimd128ScratchReg);
      } else {
        __ vrgather_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                       index);
      }
      break;
    }
    case kRiscvVslidedown: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (instr->InputAt(1)->IsImmediate()) {
        DCHECK(is_uint5(i.InputInt32(1)));
        __ vslidedown_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                         i.InputInt5(1));
      } else {
        __ vslidedown_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                         i.InputRegister(1));
      }
      break;
    }
    case kRiscvI8x16ExtractLaneU: {
      __ VU.set(kScratchReg, E8, m1);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0),
                       i.InputInt8(1));
      __ vmv_xs(i.OutputRegister(), kSimd128ScratchReg);
      __ slli(i.OutputRegister(), i.OutputRegister(), sizeof(void*) * 8 - 8);
      __ srli(i.OutputRegister(), i.OutputRegister(), sizeof(void*) * 8 - 8);
      break;
    }
    case kRiscvI8x16ExtractLaneS: {
      __ VU.set(kScratchReg, E8, m1);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0),
                       i.InputInt8(1));
      __ vmv_xs(i.OutputRegister(), kSimd128ScratchReg);
      break;
    }
    case kRiscvI16x8ExtractLaneU: {
      __ VU.set(kScratchReg, E16, m1);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0),
                       i.InputInt8(1));
      __ vmv_xs(i.OutputRegister(), kSimd128ScratchReg);
      __ slli(i.OutputRegister(), i.OutputRegister(), sizeof(void*) * 8 - 16);
      __ srli(i.OutputRegister(), i.OutputRegister(), sizeof(void*) * 8 - 16);
      break;
    }
    case kRiscvI16x8ExtractLaneS: {
      __ VU.set(kScratchReg, E16, m1);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0),
                       i.InputInt8(1));
      __ vmv_xs(i.OutputRegister(), kSimd128ScratchReg);
      break;
    }
    case kRiscvI8x16ShrU: {
      __ VU.set(kScratchReg, E8, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ andi(i.InputRegister(1), i.InputRegister(1), 8 - 1);
        __ vsrl_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsrl_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 8);
      }
      break;
    }
    case kRiscvI16x8ShrU: {
      __ VU.set(kScratchReg, E16, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ andi(i.InputRegister(1), i.InputRegister(1), 16 - 1);
        __ vsrl_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsrl_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 16);
      }
      break;
    }
    case kRiscvI32x4TruncSatF64x2SZero: {
      __ VU.set(kScratchReg, E64, m1);
      __ vmv_vx(kSimd128ScratchReg, zero_reg);
      __ vmfeq_vv(v0, i.InputSimd128Register(0), i.InputSimd128Register(0));
      __ vmv_vv(kSimd128ScratchReg3, i.InputSimd128Register(0));
      __ VU.set(kScratchReg, E32, m1);
      __ VU.set(FPURoundingMode::RTZ);
      __ vfncvt_x_f_w(kSimd128ScratchReg, kSimd128ScratchReg3, MaskType::Mask);
      __ vmv_vv(i.OutputSimd128Register(), kSimd128ScratchReg);
      break;
    }
    case kRiscvI32x4TruncSatF64x2UZero: {
      __ VU.set(kScratchReg, E64, m1);
      __ vmv_vx(kSimd128ScratchReg, zero_reg);
      __ vmfeq_vv(v0, i.InputSimd128Register(0), i.InputSimd128Register(0));
      __ vmv_vv(kSimd128ScratchReg3, i.InputSimd128Register(0));
      __ VU.set(kScratchReg, E32, m1);
      __ VU.set(FPURoundingMode::RTZ);
      __ vfncvt_xu_f_w(kSimd128ScratchReg, kSimd128ScratchReg3, MaskType::Mask);
      __ vmv_vv(i.OutputSimd128Register(), kSimd128ScratchReg);
      break;
    }
    case kRiscvI32x4ShrU: {
      __ VU.set(kScratchReg, E32, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsrl_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsrl_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 32);
      }
      break;
    }
    case kRiscvI64x2ShrU: {
      __ VU.set(kScratchReg, E64, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsrl_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        if (is_uint5(i.InputInt6(1) % 64)) {
          __ vsrl_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputInt6(1) % 64);
        } else {
          __ li(kScratchReg, i.InputInt6(1) % 64);
          __ vsrl_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     kScratchReg);
        }
      }
      break;
    }
    case kRiscvI8x16ShrS: {
      __ VU.set(kScratchReg, E8, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsra_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsra_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 8);
      }
      break;
    }
    case kRiscvI16x8ShrS: {
      __ VU.set(kScratchReg, E16, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsra_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsra_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 16);
      }
      break;
    }
    case kRiscvI32x4ShrS: {
      __ VU.set(kScratchReg, E32, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsra_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsra_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 32);
      }
      break;
    }
    case kRiscvI64x2ShrS: {
      __ VU.set(kScratchReg, E64, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsra_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        if (is_uint5(i.InputInt6(1) % 64)) {
          __ vsra_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputInt6(1) % 64);
        } else {
          __ li(kScratchReg, i.InputInt6(1) % 64);
          __ vsra_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     kScratchReg);
        }
      }
      break;
    }
    case kRiscvI32x4ExtractLane: {
      __ WasmRvvExtractLane(i.OutputRegister(), i.InputSimd128Register(0),
                            i.InputInt8(1), E32, m1);
      break;
    }
    case kRiscvVAbs: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      __ vmv_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      __ vmslt_vx(v0, i.InputSimd128Register(0), zero_reg);
      __ vneg_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 MaskType::Mask);
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case kRiscvI64x2ExtractLane: {
      __ WasmRvvExtractLane(i.OutputRegister(), i.InputSimd128Register(0),
                            i.InputInt8(1), E64, m1);
      break;
    }
#elif V8_TARGET_ARCH_RISCV32
    case kRiscvI64x2ExtractLane: {
      uint8_t imm_lane_idx = i.InputInt8(1);
      __ VU.set(kScratchReg, E32, m1);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0),
                       (imm_lane_idx << 0x1) + 1);
      __ vmv_xs(i.OutputRegister(1), kSimd128ScratchReg);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0),
                       (imm_lane_idx << 0x1));
      __ vmv_xs(i.OutputRegister(0), kSimd128ScratchReg);
      break;
    }
#endif
    case kRiscvI8x16Shl: {
      __ VU.set(kScratchReg, E8, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsll_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsll_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 8);
      }
      break;
    }
    case kRiscvI16x8Shl: {
      __ VU.set(kScratchReg, E16, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsll_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsll_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 16);
      }
      break;
    }
    case kRiscvI32x4Shl: {
      __ VU.set(kScratchReg, E32, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsll_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        __ vsll_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputInt5(1) % 32);
      }
      break;
    }
    case kRiscvI64x2Shl: {
      __ VU.set(kScratchReg, E64, m1);
      if (instr->InputAt(1)->IsRegister()) {
        __ vsll_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else {
        if (is_int5(i.InputInt6(1) % 64)) {
          __ vsll_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputInt6(1) % 64);
        } else {
          __ li(kScratchReg, i.InputInt6(1) % 64);
          __ vsll_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     kScratchReg);
        }
      }
      break;
    }
    case kRiscvI8x16ReplaceLane: {
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      __ VU.set(kScratchReg, E64, m1);
      __ li(kScratchReg, 0x1 << i.InputInt8(1));
      __ vmv_sx(v0, kScratchReg);
      __ VU.set(kScratchReg, E8, m1);
      __ vmerge_vx(dst, i.InputRegister(2), src);
      break;
    }
    case kRiscvI16x8ReplaceLane: {
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      __ VU.set(kScratchReg, E16, m1);
      __ li(kScratchReg, 0x1 << i.InputInt8(1));
      __ vmv_sx(v0, kScratchReg);
      __ vmerge_vx(dst, i.InputRegister(2), src);
      break;
    }
#if V8_TARGET_ARCH_RISCV64
    case kRiscvI64x2ReplaceLane: {
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      __ VU.set(kScratchReg, E64, m1);
      __ li(kScratchReg, 0x1 << i.InputInt8(1));
      __ vmv_sx(v0, kScratchReg);
      __ vmerge_vx(dst, i.InputRegister(2), src);
      break;
    }
#elif V8_TARGET_ARCH_RISCV32
    case kRiscvI64x2ReplaceLaneI32Pair: {
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      Register int64_low = i.InputRegister(2);
      Register int64_high = i.InputRegister(3);
      __ VU.set(kScratchReg, E32, m1);
      __ vmv_vx(kSimd128ScratchReg, int64_high);
      __ vmv_sx(kSimd128ScratchReg, int64_low);
      __ VU.set(kScratchReg, E64, m1);
      __ li(kScratchReg, 0x1 << i.InputInt8(1));
      __ vmv_sx(v0, kScratchReg);
      __ vfmv_fs(kScratchDoubleReg, kSimd128ScratchReg);
      __ vfmerge_vf(dst, kScratchDoubleReg, src);
      break;
    }
#endif
    case kRiscvI32x4ReplaceLane: {
      Simd128Register src = i.InputSimd128Register(0);
      Simd128Register dst = i.OutputSimd128Register();
      __ VU.set(kScratchReg, E32, m1);
      __ li(kScratchReg, 0x1 << i.InputInt8(1));
      __ vmv_sx(v0, kScratchReg);
      __ vmerge_vx(dst, i.InputRegister(2), src);
      break;
    }
    case kRiscvV128AnyTrue: {
      __ VU.set(kScratchReg, E8, m1);
      Register dst = i.OutputRegister();
      Label t;
      __ vmv_sx(kSimd128ScratchReg, zero_reg);
      __ vredmaxu_vs(kSimd128ScratchReg, i.InputSimd128Register(0),
                     kSimd128ScratchReg);
      __ vmv_xs(dst, kSimd128ScratchReg);
      __ beq(dst, zero_reg, &t);
      __ li(dst, 1);
      __ bind(&t);
      break;
    }
    case kRiscvVAllTrue: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      Register dst = i.OutputRegister();
      Label notalltrue;
      __ vmv_vi(kSimd128ScratchReg, -1);
      __ vredminu_vs(kSimd128ScratchReg, i.InputSimd128Register(0),
                     kSimd128ScratchReg);
      __ vmv_xs(dst, kSimd128ScratchReg);
      __ beqz(dst, &notalltrue);
      __ li(dst, 1);
      __ bind(&notalltrue);
      break;
    }
    case kRiscvI8x16Shuffle: {
      VRegister dst = i.OutputSimd128Register(),
                src0 = i.InputSimd128Register(0),
                src1 = i.InputSimd128Register(1);

#if V8_TARGET_ARCH_RISCV64
      int64_t imm1 = make_uint64(i.InputInt32(3), i.InputInt32(2));
      int64_t imm2 = make_uint64(i.InputInt32(5), i.InputInt32(4));
      __ VU.set(kScratchReg, VSew::E64, Vlmul::m1);
      __ li(kScratchReg, imm2);
      __ vmv_sx(kSimd128ScratchReg2, kScratchReg);
      __ vslideup_vi(kSimd128ScratchReg, kSimd128ScratchReg2, 1);
      __ li(kScratchReg, imm1);
      __ vmv_sx(kSimd128ScratchReg, kScratchReg);
#elif V8_TARGET_ARCH_RISCV32
      __ VU.set(kScratchReg, VSew::E32, Vlmul::m1);
      __ li(kScratchReg, i.InputInt32(5));
      __ vmv_vx(kSimd128ScratchReg2, kScratchReg);
      __ li(kScratchReg, i.InputInt32(4));
      __ vmv_sx(kSimd128ScratchReg2, kScratchReg);
      __ li(kScratchReg, i.InputInt32(3));
      __ vmv_vx(kSimd128ScratchReg, kScratchReg);
      __ li(kScratchReg, i.InputInt32(2));
      __ vmv_sx(kSimd128ScratchReg, kScratchReg);
      __ vslideup_vi(kSimd128ScratchReg, kSimd128ScratchReg2, 2);
#endif

      __ VU.set(kScratchReg, E8, m1);
      if (dst == src0) {
        __ vmv_vv(kSimd128ScratchReg2, src0);
        src0 = kSimd128ScratchReg2;
      } else if (dst == src1) {
        __ vmv_vv(kSimd128ScratchReg2, src1);
        src1 = kSimd128ScratchReg2;
      }
      __ vrgather_vv(dst, src0, kSimd128ScratchReg);
      __ vadd_vi(kSimd128ScratchReg, kSimd128ScratchReg, -16);
      __ vrgather_vv(kSimd128ScratchReg3, src1, kSimd128ScratchReg);
      __ vor_vv(dst, dst, kSimd128ScratchReg3);
      break;
    }
    case kRiscvI8x16Popcnt: {
      VRegister dst = i.OutputSimd128Register(),
                src = i.InputSimd128Register(0);
      Label t;

      __ VU.set(kScratchReg, E8, m1);
      __ vmv_vv(kSimd128ScratchReg, src);
      __ vmv_vv(dst, kSimd128RegZero);

      __ bind(&t);
      __ vmsne_vv(v0, kSimd128ScratchReg, kSimd128RegZero);
      __ vadd_vi(dst, dst, 1, Mask);
      __ vadd_vi(kSimd128ScratchReg2, kSimd128ScratchReg, -1, Mask);
      __ vand_vv(kSimd128ScratchReg, kSimd128ScratchReg, kSimd128ScratchReg2);
      // kScratchReg = -1 if kSimd128ScratchReg == 0 i.e. no active element
      __ vfirst_m(kScratchReg, kSimd128ScratchReg);
      __ bgez(kScratchReg, &t);
      break;
    }
    case kRiscvF64x2NearestInt: {
      __ Round_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kScratchReg, kSimd128ScratchReg);
      break;
    }
    case kRiscvF64x2Trunc: {
      __ Trunc_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kScratchReg, kSimd128ScratchReg);
      break;
    }
    case kRiscvF64x2Sqrt: {
      __ VU.set(kScratchReg, E64, m1);
      __ vfsqrt_v(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF64x2Abs: {
      __ VU.set(kScratchReg, VSew::E64, Vlmul::m1);
      __ vfabs_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF64x2Ceil: {
      __ Ceil_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                kScratchReg, kSimd128ScratchReg);
      break;
    }
    case kRiscvF64x2Floor: {
      __ Floor_d(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kScratchReg, kSimd128ScratchReg);
      break;
    }
    case kRiscvF64x2ReplaceLane: {
      __ VU.set(kScratchReg, E64, m1);
      __ li(kScratchReg, 0x1 << i.InputInt8(1));
      __ vmv_sx(v0, kScratchReg);
      __ vfmerge_vf(i.OutputSimd128Register(), i.InputSingleRegister(2),
                    i.InputSimd128Register(0));
      break;
    }
    case kRiscvF64x2Pmax: {
      __ VU.set(kScratchReg, E64, m1);
      __ vmflt_vv(v0, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ vmerge_vv(i.OutputSimd128Register(), i.InputSimd128Register(1),
                   i.InputSimd128Register(0));
      break;
    }
    case kRiscvF64x2Pmin: {
      __ VU.set(kScratchReg, E64, m1);
      __ vmflt_vv(v0, i.InputSimd128Register(1), i.InputSimd128Register(0));
      __ vmerge_vv(i.OutputSimd128Register(), i.InputSimd128Register(1),
                   i.InputSimd128Register(0));
      break;
    }
    case kRiscvF64x2ExtractLane: {
      __ VU.set(kScratchReg, E64, m1);
      if (is_uint5(i.InputInt8(1))) {
        __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0),
                         i.InputInt8(1));
      } else {
        __ li(kScratchReg, i.InputInt8(1));
        __ vslidedown_vx(kSimd128ScratchReg, i.InputSimd128Register(0),
                         kScratchReg);
      }
      __ vfmv_fs(i.OutputDoubleRegister(), kSimd128ScratchReg);
      break;
    }
    case kRiscvF64x2PromoteLowF32x4: {
      __ VU.set(kScratchReg, E32, mf2);
      if (i.OutputSimd128Register() != i.InputSimd128Register(0)) {
        __ vfwcvt_f_f_v(i.OutputSimd128Register(), i.InputSimd128Register(0));
      } else {
        __ vfwcvt_f_f_v(kSimd128ScratchReg3, i.InputSimd128Register(0));
        __ VU.set(kScratchReg, E64, m1);
        __ vmv_vv(i.OutputSimd128Register(), kSimd128ScratchReg3);
      }
      break;
    }
    case kRiscvF64x2ConvertLowI32x4S: {
      __ VU.set(kScratchReg, E32, mf2);
      if (i.OutputSimd128Register() != i.InputSimd128Register(0)) {
        __ vfwcvt_f_x_v(i.OutputSimd128Register(), i.InputSimd128Register(0));
      } else {
        __ vfwcvt_f_x_v(kSimd128ScratchReg3, i.InputSimd128Register(0));
        __ VU.set(kScratchReg, E64, m1);
        __ vmv_vv(i.OutputSimd128Register(), kSimd128ScratchReg3);
      }
      break;
    }
    case kRiscvF64x2ConvertLowI32x4U: {
      __ VU.set(kScratchReg, E32, mf2);
      if (i.OutputSimd128Register() != i.InputSimd128Register(0)) {
        __ vfwcvt_f_xu_v(i.OutputSimd128Register(), i.InputSimd128Register(0));
      } else {
        __ vfwcvt_f_xu_v(kSimd128ScratchReg3, i.InputSimd128Register(0));
        __ VU.set(kScratchReg, E64, m1);
        __ vmv_vv(i.OutputSimd128Register(), kSimd128ScratchReg3);
      }
      break;
    }
    case kRiscvF64x2Qfma: {
      __ VU.set(kScratchReg, E64, m1);
      __ vfmadd_vv(i.InputSimd128Register(0), i.InputSimd128Register(1),
                   i.InputSimd128Register(2));
      __ vmv_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF64x2Qfms: {
      __ VU.set(kScratchReg, E64, m1);
      __ vfnmsub_vv(i.InputSimd128Register(0), i.InputSimd128Register(1),
                    i.InputSimd128Register(2));
      __ vmv_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4ExtractLane: {
      __ VU.set(kScratchReg, E32, m1);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0),
                       i.InputInt8(1));
      __ vfmv_fs(i.OutputDoubleRegister(), kSimd128ScratchReg);
      break;
    }
    case kRiscvF32x4Trunc: {
      __ Trunc_f(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kScratchReg, kSimd128ScratchReg);
      break;
    }
    case kRiscvF32x4NearestInt: {
      __ Round_f(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kScratchReg, kSimd128ScratchReg);
      break;
    }
    case kRiscvF32x4DemoteF64x2Zero: {
      __ VU.set(kScratchReg, E32, mf2);
      __ vfncvt_f_f_w(i.OutputSimd128Register(), i.InputSimd128Register(0));
      __ VU.set(kScratchReg, E32, m1);
      __ vmv_vi(v0, 12);
      __ vmerge_vx(i.OutputSimd128Register(), zero_reg,
                   i.OutputSimd128Register());
      break;
    }
    case kRiscvF32x4Abs: {
      __ VU.set(kScratchReg, VSew::E32, Vlmul::m1);
      __ vfabs_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4Ceil: {
      __ Ceil_f(i.OutputSimd128Register(), i.InputSimd128Register(0),
                kScratchReg, kSimd128ScratchReg);
      break;
    }
    case kRiscvF32x4Floor: {
      __ Floor_f(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 kScratchReg, kSimd128ScratchReg);
      break;
    }
    case kRiscvF32x4UConvertI32x4: {
      __ VU.set(kScratchReg, E32, m1);
      __ VU.set(FPURoundingMode::RTZ);
      __ vfcvt_f_xu_v(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4SConvertI32x4: {
      __ VU.set(kScratchReg, E32, m1);
      __ VU.set(FPURoundingMode::RTZ);
      __ vfcvt_f_x_v(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4ReplaceLane: {
      __ VU.set(kScratchReg, E32, m1);
      __ li(kScratchReg, 0x1 << i.InputInt8(1));
      __ vmv_sx(v0, kScratchReg);
      __ fmv_x_w(kScratchReg, i.InputSingleRegister(2));
      __ vmerge_vx(i.OutputSimd128Register(), kScratchReg,
                   i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4Pmax: {
      __ VU.set(kScratchReg, E32, m1);
      __ vmflt_vv(v0, i.InputSimd128Register(0), i.InputSimd128Register(1));
      __ vmerge_vv(i.OutputSimd128Register(), i.InputSimd128Register(1),
                   i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4Pmin: {
      __ VU.set(kScratchReg, E32, m1);
      __ vmflt_vv(v0, i.InputSimd128Register(1), i.InputSimd128Register(0));
      __ vmerge_vv(i.OutputSimd128Register(), i.InputSimd128Register(1),
                   i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4Sqrt: {
      __ VU.set(kScratchReg, E32, m1);
      __ vfsqrt_v(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4Qfma: {
      __ VU.set(kScratchReg, E32, m1);
      __ vfmadd_vv(i.InputSimd128Register(0), i.InputSimd128Register(1),
                   i.InputSimd128Register(2));
      __ vmv_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvF32x4Qfms: {
      __ VU.set(kScratchReg, E32, m1);
      __ vfnmsub_vv(i.InputSimd128Register(0), i.InputSimd128Register(1),
                    i.InputSimd128Register(2));
      __ vmv_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvI64x2SConvertI32x4Low: {
      __ VU.set(kScratchReg, E64, m1);
      __ vmv_vv(kSimd128ScratchReg, i.InputSimd128Register(0));
      __ vsext_vf2(i.OutputSimd128Register(), kSimd128ScratchReg);

      break;
    }
    case kRiscvI64x2SConvertI32x4High: {
      __ VU.set(kScratchReg, E32, m1);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0), 2);
      __ VU.set(kScratchReg, E64, m1);
      __ vsext_vf2(i.OutputSimd128Register(), kSimd128ScratchReg);
      break;
    }
    case kRiscvI64x2UConvertI32x4Low: {
      __ VU.set(kScratchReg, E64, m1);
      __ vmv_vv(kSimd128ScratchReg, i.InputSimd128Register(0));
      __ vzext_vf2(i.OutputSimd128Register(), kSimd128ScratchReg);
      break;
    }
    case kRiscvI64x2UConvertI32x4High: {
      __ VU.set(kScratchReg, E32, m1);
      __ vslidedown_vi(kSimd128ScratchReg, i.InputSimd128Register(0), 2);
      __ VU.set(kScratchReg, E64, m1);
      __ vzext_vf2(i.OutputSimd128Register(), kSimd128ScratchReg);
      break;
    }
    case kRiscvI32x4SConvertF32x4: {
      __ VU.set(kScratchReg, E32, m1);
      __ VU.set(FPURoundingMode::RTZ);
      __ vmfeq_vv(v0, i.InputSimd128Register(0), i.InputSimd128Register(0));
      if (i.OutputSimd128Register() != i.InputSimd128Register(0)) {
        __ vmv_vx(i.OutputSimd128Register(), zero_reg);
        __ vfcvt_x_f_v(i.OutputSimd128Register(), i.InputSimd128Register(0),
                       Mask);
      } else {
        __ vmv_vx(kSimd128ScratchReg, zero_reg);
        __ vfcvt_x_f_v(kSimd128ScratchReg, i.InputSimd128Register(0), Mask);
        __ vmv_vv(i.OutputSimd128Register(), kSimd128ScratchReg);
      }
      break;
    }
    case kRiscvI32x4UConvertF32x4: {
      __ VU.set(kScratchReg, E32, m1);
      __ VU.set(FPURoundingMode::RTZ);
      __ vmfeq_vv(v0, i.InputSimd128Register(0), i.InputSimd128Register(0));
      if (i.OutputSimd128Register() != i.InputSimd128Register(0)) {
        __ vmv_vx(i.OutputSimd128Register(), zero_reg);
        __ vfcvt_xu_f_v(i.OutputSimd128Register(), i.InputSimd128Register(0),
                        Mask);
      } else {
        __ vmv_vx(kSimd128ScratchReg, zero_reg);
        __ vfcvt_xu_f_v(kSimd128ScratchReg, i.InputSimd128Register(0), Mask);
        __ vmv_vv(i.OutputSimd128Register(), kSimd128ScratchReg);
      }
      break;
    }
#if V8_TARGET_ARCH_RISCV32
    case kRiscvI64x2SplatI32Pair: {
      __ VU.set(kScratchReg, E32, m1);
      __ vmv_vi(v0, 0b0101);
      __ vmv_vx(kSimd128ScratchReg, i.InputRegister(1));
      __ vmerge_vx(i.OutputSimd128Register(), i.InputRegister(0),
                   kSimd128ScratchReg);
      break;
    }
#endif
    case kRiscvVwaddVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vwadd_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVwadduVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vwaddu_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1));
      break;
    }
    case kRiscvVwadduWx: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (instr->InputAt(1)->IsRegister()) {
        __ vwaddu_wx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputRegister(1));
      } else {
        __ li(kScratchReg, i.InputInt64(1));
        __ vwaddu_wx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     kScratchReg);
      }
      break;
    }
    case kRiscvVdivu: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (instr->InputAt(1)->IsSimd128Register()) {
        __ vdivu_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputSimd128Register(1));
      } else if ((instr->InputAt(1)->IsRegister())) {
        __ vdivu_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputRegister(1));
      } else {
        __ li(kScratchReg, i.InputInt64(1));
        __ vdivu_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    kScratchReg);
      }
      break;
    }
    case kRiscvVnclipu: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ VU.set(FPURoundingMode(i.InputInt8(4)));
      if (instr->InputAt(1)->IsSimd128Register()) {
        __ vnclipu_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      i.InputSimd128Register(1));
      } else if (instr->InputAt(1)->IsRegister()) {
        __ vnclipu_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      i.InputRegister(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        __ vnclipu_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      i.InputInt8(1));
      }
      break;
    }
    case kRiscvVnclip: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ VU.set(FPURoundingMode(i.InputInt8(4)));
      if (instr->InputAt(1)->IsSimd128Register()) {
        __ vnclip_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1));
      } else if (instr->InputAt(1)->IsRegister()) {
        __ vnclip_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputRegister(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        __ vnclip_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputInt8(1));
      }
      break;
    }
    case kRiscvVwmul: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vwmul_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVwmulu: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vwmulu_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1));
      break;
    }
    case kRiscvVmvSx: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      if (instr->InputAt(0)->IsRegister()) {
        __ vmv_sx(i.OutputSimd128Register(), i.InputRegister(0));
      } else {
        DCHECK(instr->InputAt(0)->IsImmediate());
        __ li(kScratchReg, i.InputInt64(0));
        __ vmv_sx(i.OutputSimd128Register(), kScratchReg);
      }
      break;
    }
    case kRiscvVmvXs: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      __ vmv_xs(i.OutputRegister(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvVcompress: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (instr->InputAt(1)->IsSimd128Register()) {
        __ vcompress_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                        i.InputSimd128Register(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        __ li(kScratchReg, i.InputInt64(1));
        __ vmv_sx(v0, kScratchReg);
        __ vcompress_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                        v0);
      }
      break;
    }
    case kRiscvVsll: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (instr->InputAt(1)->IsRegister()) {
        __ vsll_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));
      } else if (instr->InputAt(1)->IsSimd128Register()) {
        __ vsll_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        if (is_int5(i.InputInt64(1))) {
          __ vsll_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputInt8(1));
        } else {
          __ li(kScratchReg, i.InputInt64(1));
          __ vsll_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     kScratchReg);
        }
      }
      break;
    }
    case kRiscvVmslt: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (i.InputInt8(4)) {
        DCHECK(i.OutputSimd128Register() != i.InputSimd128Register(0));
        __ vmv_vx(i.OutputSimd128Register(), zero_reg);
      }
      if (instr->InputAt(1)->IsRegister()) {
        __ vmslt_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputRegister(1));
      } else if (instr->InputAt(1)->IsSimd128Register()) {
        __ vmslt_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputSimd128Register(1));
      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        if (is_int5(i.InputInt64(1))) {
          __ vmslt_vi(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      i.InputInt8(1));
        } else {
          __ li(kScratchReg, i.InputInt64(1));
          __ vmslt_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      kScratchReg);
        }
      }
      break;
    }
    case kRiscvVaddVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vadd_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kRiscvVsubVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vsub_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kRiscvVmv: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      if (instr->InputAt(0)->IsSimd128Register()) {
        __ vmv_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      } else if (instr->InputAt(0)->IsRegister()) {
        __ vmv_vx(i.OutputSimd128Register(), i.InputRegister(0));
      } else {
        if (i.ToConstant(instr->InputAt(0)).FitsInInt32() &&
            is_int8(i.InputInt32(0))) {
          __ vmv_vi(i.OutputSimd128Register(), i.InputInt8(0));
        } else {
          __ li(kScratchReg, i.InputInt64(0));
          __ vmv_vx(i.OutputSimd128Register(), kScratchReg);
        }
      }
      break;
    }
    case kRiscvVfmvVf: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      __ vfmv_vf(i.OutputSimd128Register(), i.InputDoubleRegister(0));
      break;
    }
    case kRiscvVnegVv: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      __ vneg_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvVfnegVv: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      __ vfneg_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvVmaxuVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vmaxu_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVmax: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (instr->InputAt(1)->IsSimd128Register()) {
        __ vmax_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1));
      } else if (instr->InputAt(1)->IsRegister()) {
        __ vmax_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputRegister(1));

      } else {
        DCHECK(instr->InputAt(1)->IsImmediate());
        __ li(kScratchReg, i.InputInt64(1));
        __ vmax_vx(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   kScratchReg);
      }
      break;
    }
    case kRiscvVminuVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vminu_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVminsVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vmin_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kRiscvVmulVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vmul_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kRiscvVgtsVv: {
      __ WasmRvvGtS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputSimd128Register(1), VSew(i.InputInt8(2)),
                    Vlmul(i.InputInt8(3)));
      break;
    }
    case kRiscvVgesVv: {
      __ WasmRvvGeS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputSimd128Register(1), VSew(i.InputInt8(2)),
                    Vlmul(i.InputInt8(3)));
      break;
    }
    case kRiscvVgeuVv: {
      __ WasmRvvGeU(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputSimd128Register(1), VSew(i.InputInt8(2)),
                    Vlmul(i.InputInt8(3)));
      break;
    }
    case kRiscvVgtuVv: {
      __ WasmRvvGtU(i.OutputSimd128Register(), i.InputSimd128Register(0),
                    i.InputSimd128Register(1), VSew(i.InputInt8(2)),
                    Vlmul(i.InputInt8(3)));
      break;
    }
    case kRiscvVeqVv: {
      __ WasmRvvEq(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1), VSew(i.InputInt8(2)),
                   Vlmul(i.InputInt8(3)));
      break;
    }
    case kRiscvVneVv: {
      __ WasmRvvNe(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1), VSew(i.InputInt8(2)),
                   Vlmul(i.InputInt8(3)));
      break;
    }
    case kRiscvVaddSatSVv: {
      (__ VU).set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vsadd_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVaddSatUVv: {
      (__ VU).set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vsaddu_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1));
      break;
    }
    case kRiscvVsubSatSVv: {
      (__ VU).set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vssub_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVsubSatUVv: {
      (__ VU).set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vssubu_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                   i.InputSimd128Register(1));
      break;
    }
    case kRiscvVfaddVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vfadd_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVfsubVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vfsub_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVfmulVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vfmul_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVfdivVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vfdiv_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVmfeqVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vmfeq_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVmfneVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vmfne_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVmfltVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vmflt_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVmfleVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vmfle_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVfminVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vfmin_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), MaskType(i.InputInt8(4)));
      break;
    }
    case kRiscvVfmaxVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vfmax_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), MaskType(i.InputInt8(4)));
      break;
    }
    case kRiscvVandVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vand_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kRiscvVorVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vor_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                i.InputSimd128Register(1));
      break;
    }
    case kRiscvVxorVv: {
      (__ VU).set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vxor_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                 i.InputSimd128Register(1));
      break;
    }
    case kRiscvVnotVv: {
      (__ VU).set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      __ vnot_vv(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvVmergeVx: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      if (instr->InputAt(0)->IsRegister()) {
        __ vmerge_vx(i.OutputSimd128Register(), i.InputRegister(0),
                     i.InputSimd128Register(1));
      } else {
        DCHECK(is_int5(i.InputInt32(0)));
        __ vmerge_vi(i.OutputSimd128Register(), i.InputInt8(0),
                     i.InputSimd128Register(1));
      }
      break;
    }
    case kRiscvVsmulVv: {
      __ VU.set(kScratchReg, i.InputInt8(2), i.InputInt8(3));
      __ vsmul_vv(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1));
      break;
    }
    case kRiscvVredminuVs: {
      __ vredminu_vs(i.OutputSimd128Register(), i.InputSimd128Register(0),
                     i.InputSimd128Register(1));
      break;
    }
    case kRiscvVzextVf2: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      __ vzext_vf2(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kRiscvVsextVf2: {
      __ VU.set(kScratchReg, i.InputInt8(1), i.InputInt8(2));
      __ vsext_vf2(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    default:
#ifdef DEBUG
      switch (arch_opcode) {
#define Print(name)       \
  case k##name:           \
    printf("k%s", #name); \
    break;
        ARCH_OPCODE_LIST(Print);
#undef Print
        default:
          break;
      }
#endif
      UNIMPLEMENTED();
  }
  return kSuccess;
}

#define UNSUPPORTED_COND(opcode, condition)                                    \
  StdoutStream{} << "Unsupported " << #opcode << " condition: \"" << condition \
                 << "\"";                                                      \
  UNIMPLEMENTED();

bool IsInludeEqual(Condition cc) {
  switch (cc) {
    case equal:
    case greater_equal:
    case less_equal:
    case Uless_equal:
    case Ugreater_equal:
      return true;
    default:
      return false;
  }
}

void AssembleBranchToLabels(CodeGenerator* gen, MacroAssembler* masm,
                            Instruction* instr, FlagsCondition condition,
                            Label* tlabel, Label* flabel, bool fallthru) {
#undef __
#define __ masm->
  RiscvOperandConverter i(gen, instr);

  // RISC-V does not have condition code flags, so compare and branch are
  // implemented differently than on the other arch's. The compare operations
  // emit riscv64 pseudo-instructions, which are handled here by branch
  // instructions that do the actual comparison. Essential that the input
  // registers to compare pseudo-op are not modified before this branch op, as
  // they are tested here.
#if V8_TARGET_ARCH_RISCV64
  if (instr->arch_opcode() == kRiscvTst64 ||
      instr->arch_opcode() == kRiscvTst32) {
#elif V8_TARGET_ARCH_RISCV32
  if (instr->arch_opcode() == kRiscvTst32) {
#endif
    Condition cc = FlagsConditionToConditionTst(condition);
    __ Branch(tlabel, cc, kScratchReg, Operand(zero_reg));
#if V8_TARGET_ARCH_RISCV64
  } else if (instr->arch_opcode() == kRiscvAdd64 ||
             instr->arch_opcode() == kRiscvSub64) {
    Condition cc = FlagsConditionToConditionOvf(condition);
    __ Sra64(kScratchReg, i.OutputRegister(), 32);
    __ Sra32(kScratchReg2, i.OutputRegister(), 31);
    __ Branch(tlabel, cc, kScratchReg2, Operand(kScratchReg));
  } else if (instr->arch_opcode() == kRiscvAddOvf64 ||
             instr->arch_opcode() == kRiscvSubOvf64) {
#elif V8_TARGET_ARCH_RISCV32
  } else if (instr->arch_opcode() == kRiscvAddOvf ||
             instr->arch_opcode() == kRiscvSubOvf) {
#endif
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
#if V8_TARGET_ARCH_RISCV64
    // kRiscvMulOvf64 is only for RISCV64
  } else if (instr->arch_opcode() == kRiscvMulOvf32 ||
             instr->arch_opcode() == kRiscvMulOvf64) {
#elif V8_TARGET_ARCH_RISCV32
  } else if (instr->arch_opcode() == kRiscvMulOvf32) {
#endif
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
#if V8_TARGET_ARCH_RISCV64
  } else if (instr->arch_opcode() == kRiscvCmp ||
             instr->arch_opcode() == kRiscvCmp32) {
#elif V8_TARGET_ARCH_RISCV32
  } else if (instr->arch_opcode() == kRiscvCmp) {
#endif
    Condition cc = FlagsConditionToConditionCmp(condition);
    Register left = i.InputRegister(0);
    Operand right = i.InputOperand(1);
    // Word32Compare has two temp registers.
#if V8_TARGET_ARCH_RISCV64
    if (COMPRESS_POINTERS_BOOL && (instr->arch_opcode() == kRiscvCmp32)) {
      Register temp0 = i.TempRegister(0);
      Register temp1 = right.is_reg() ? i.TempRegister(1) : no_reg;
      __ slliw(temp0, left, 0);
      left = temp0;
      if (temp1 != no_reg) {
        __ slliw(temp1, right.rm(), 0);
        right = Operand(temp1);
      }
    }
#endif
    __ Branch(tlabel, cc, left, right);
  } else if (instr->arch_opcode() == kRiscvCmpZero) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    if (i.InputOrZeroRegister(0) == zero_reg && IsInludeEqual(cc)) {
      __ Branch(tlabel);
    } else if (i.InputOrZeroRegister(0) != zero_reg) {
      __ Branch(tlabel, cc, i.InputRegister(0), Operand(zero_reg));
    }
#ifdef V8_TARGET_ARCH_RISCV64
  } else if (instr->arch_opcode() == kRiscvCmpZero32) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    if (i.InputOrZeroRegister(0) == zero_reg && IsInludeEqual(cc)) {
      __ Branch(tlabel);
    } else if (i.InputOrZeroRegister(0) != zero_reg) {
      Register temp0 = i.TempRegister(0);
      __ slliw(temp0, i.InputRegister(0), 0);
      __ Branch(tlabel, cc, temp0, Operand(zero_reg));
    }
#endif
  } else if (instr->arch_opcode() == kArchStackPointerGreaterThan) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    Register lhs_register = sp;
    uint32_t offset;
    if (gen->ShouldApplyOffsetToStackCheck(instr, &offset)) {
      lhs_register = i.TempRegister(0);
      __ SubWord(lhs_register, sp, offset);
    }
    __ Branch(tlabel, cc, lhs_register, Operand(i.InputRegister(0)));
  } else if (instr->arch_opcode() == kRiscvCmpS ||
             instr->arch_opcode() == kRiscvCmpD) {
    bool predicate;
    FlagsConditionToConditionCmpFPU(&predicate, condition);
    // floating-point compare result is set in kScratchReg
    if (predicate) {
      __ BranchTrueF(kScratchReg, tlabel);
    } else {
      __ BranchFalseF(kScratchReg, tlabel);
    }
  } else {
    std::cout << "AssembleArchBranch Unimplemented arch_opcode:"
              << instr->arch_opcode() << " " << condition << std::endl;
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
    void Generate() override {
      RiscvOperandConverter i(gen_, instr_);
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
  RiscvOperandConverter i(this, instr);

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Register result = i.OutputRegister(instr->OutputCount() - 1);
  // RISC-V does not have condition code flags, so compare and branch are
  // implemented differently than on the other arch's. The compare operations
  // emit riscv64 pseudo-instructions, which are checked and handled here.

#if V8_TARGET_ARCH_RISCV64
  if (instr->arch_opcode() == kRiscvTst64 ||
      instr->arch_opcode() == kRiscvTst32) {
#elif V8_TARGET_ARCH_RISCV32
  if (instr->arch_opcode() == kRiscvTst32) {
#endif
    Condition cc = FlagsConditionToConditionTst(condition);
    if (cc == eq) {
      __ Sltu(result, kScratchReg, 1);
    } else {
      __ Sltu(result, zero_reg, kScratchReg);
    }
    return;
#if V8_TARGET_ARCH_RISCV64
  } else if (instr->arch_opcode() == kRiscvAdd64 ||
             instr->arch_opcode() == kRiscvSub64) {
    Condition cc = FlagsConditionToConditionOvf(condition);
    // Check for overflow creates 1 or 0 for result.
    __ Srl64(kScratchReg, i.OutputRegister(), 63);
    __ Srl32(kScratchReg2, i.OutputRegister(), 31);
    __ Xor(result, kScratchReg, kScratchReg2);
    if (cc == eq)  // Toggle result for not overflow.
      __ Xor(result, result, 1);
    return;
  } else if (instr->arch_opcode() == kRiscvAddOvf64 ||
             instr->arch_opcode() == kRiscvSubOvf64) {
#elif V8_TARGET_ARCH_RISCV32
  } else if (instr->arch_opcode() == kRiscvAddOvf ||
             instr->arch_opcode() == kRiscvSubOvf) {
#endif
    // Overflow occurs if overflow register is negative
    __ Slt(result, kScratchReg, zero_reg);
#if V8_TARGET_ARCH_RISCV64
    // kRiscvMulOvf64 is only for RISCV64
  } else if (instr->arch_opcode() == kRiscvMulOvf32 ||
             instr->arch_opcode() == kRiscvMulOvf64) {
#elif V8_TARGET_ARCH_RISCV32
  } else if (instr->arch_opcode() == kRiscvMulOvf32) {
#endif
    // Overflow occurs if overflow register is not zero
    __ Sgtu(result, kScratchReg, zero_reg);
#if V8_TARGET_ARCH_RISCV64
  } else if (instr->arch_opcode() == kRiscvCmp ||
             instr->arch_opcode() == kRiscvCmp32) {
#elif V8_TARGET_ARCH_RISCV32
  } else if (instr->arch_opcode() == kRiscvCmp) {
#endif
    Condition cc = FlagsConditionToConditionCmp(condition);
    Register left = i.InputRegister(0);
    Operand right = i.InputOperand(1);
#if V8_TARGET_ARCH_RISCV64
    if (COMPRESS_POINTERS_BOOL && (instr->arch_opcode() == kRiscvCmp32)) {
      Register temp0 = i.TempRegister(0);
      Register temp1 = right.is_reg() ? i.TempRegister(1) : no_reg;
      __ slliw(temp0, left, 0);
      left = temp0;
      if (temp1 != no_reg) {
        __ slliw(temp1, right.rm(), 0);
        right = Operand(temp1);
      }
    }
#endif
    switch (cc) {
      case eq:
      case ne: {
        if (instr->InputAt(1)->IsImmediate()) {
          if (is_int12(-right.immediate())) {
            if (right.immediate() == 0) {
              if (cc == eq) {
                __ Sltu(result, left, 1);
              } else {
                __ Sltu(result, zero_reg, left);
              }
            } else {
              __ AddWord(result, left, Operand(-right.immediate()));
              if (cc == eq) {
                __ Sltu(result, result, 1);
              } else {
                __ Sltu(result, zero_reg, result);
              }
            }
          } else {
            if (is_uint12(right.immediate())) {
              __ Xor(result, left, right);
            } else {
              __ li(kScratchReg, right);
              __ Xor(result, left, kScratchReg);
            }
            if (cc == eq) {
              __ Sltu(result, result, 1);
            } else {
              __ Sltu(result, zero_reg, result);
            }
          }
        } else {
          __ Xor(result, left, right);
          if (cc == eq) {
            __ Sltu(result, result, 1);
          } else {
            __ Sltu(result, zero_reg, result);
          }
        }
      } break;
      case lt:
      case ge: {
        Register left = i.InputOrZeroRegister(0);
        Operand right = i.InputOperand(1);
        __ Slt(result, left, right);
        if (cc == ge) {
          __ Xor(result, result, 1);
        }
      } break;
      case gt:
      case le: {
        Register left = i.InputOrZeroRegister(1);
        Operand right = i.InputOperand(0);
        __ Slt(result, left, right);
        if (cc == le) {
          __ Xor(result, result, 1);
        }
      } break;
      case Uless:
      case Ugreater_equal: {
        Register left = i.InputOrZeroRegister(0);
        Operand right = i.InputOperand(1);
        __ Sltu(result, left, right);
        if (cc == Ugreater_equal) {
          __ Xor(result, result, 1);
        }
      } break;
      case Ugreater:
      case Uless_equal: {
        Register left = i.InputRegister(1);
        Operand right = i.InputOperand(0);
        __ Sltu(result, left, right);
        if (cc == Uless_equal) {
          __ Xor(result, result, 1);
        }
      } break;
      default:
        UNREACHABLE();
    }
    return;
  } else if (instr->arch_opcode() == kRiscvCmpZero) {
    Condition cc = FlagsConditionToConditionCmp(condition);
    switch (cc) {
      case eq: {
        Register left = i.InputOrZeroRegister(0);
        __ Sltu(result, left, 1);
        break;
      }
      case ne: {
        Register left = i.InputOrZeroRegister(0);
        __ Sltu(result, zero_reg, left);
        break;
      }
      case lt:
      case ge: {
        Register left = i.InputOrZeroRegister(0);
        Operand right = Operand(zero_reg);
        __ Slt(result, left, right);
        if (cc == ge) {
          __ Xor(result, result, 1);
        }
      } break;
      case gt:
      case le: {
        Operand left = i.InputOperand(0);
        __ Slt(result, zero_reg, left);
        if (cc == le) {
          __ Xor(result, result, 1);
        }
      } break;
      case Uless:
      case Ugreater_equal: {
        Register left = i.InputOrZeroRegister(0);
        Operand right = Operand(zero_reg);
        __ Sltu(result, left, right);
        if (cc == Ugreater_equal) {
          __ Xor(result, result, 1);
        }
      } break;
      case Ugreater:
      case Uless_equal: {
        Register left = zero_reg;
        Operand right = i.InputOperand(0);
        __ Sltu(result, left, right);
        if (cc == Uless_equal) {
          __ Xor(result, result, 1);
        }
      } break;
      default:
        UNREACHABLE();
    }
    return;
#ifdef V8_TARGET_ARCH_RISCV64
  } else if (instr->arch_opcode() == kRiscvCmpZero32) {
    auto trim_reg = [&](Register in) -> Register {
        Register temp = i.TempRegister(0);
        __ slliw(temp, in, 0);
        return temp;
    };
    auto trim_op = [&](Operand in) -> Register {
        Register temp = i.TempRegister(0);
        if (in.is_reg()) {
          __ slliw(temp, in.rm(), 0);
        } else {
          __ Li(temp, in.immediate());
          __ slliw(temp, temp, 0);
        }
        return temp;
    };
    Condition cc = FlagsConditionToConditionCmp(condition);
    switch (cc) {
      case eq: {
        auto left = trim_reg(i.InputOrZeroRegister(0));
        __ Sltu(result, left, 1);
        break;
      }
      case ne: {
        auto left = trim_reg(i.InputOrZeroRegister(0));
        __ Sltu(result, zero_reg, left);
        break;
      }
      case lt:
      case ge: {
        auto left = trim_reg(i.InputOrZeroRegister(0));
        __ Slt(result, left, zero_reg);
        if (cc == ge) {
          __ Xor(result, result, 1);
        }
      } break;
      case gt:
      case le: {
        auto left = trim_op(i.InputOperand(0));
        __ Slt(result, zero_reg, left);
        if (cc == le) {
          __ Xor(result, result, 1);
        }
      } break;
      case Uless:
      case Ugreater_equal: {
        auto left = trim_reg(i.InputOrZeroRegister(0));
        __ Sltu(result, left, zero_reg);
        if (cc == Ugreater_equal) {
          __ Xor(result, result, 1);
        }
      } break;
      case Ugreater:
      case Uless_equal: {
        auto right = trim_op(i.InputOperand(0));
        __ Sltu(result, zero_reg, right);
        if (cc == Uless_equal) {
          __ Xor(result, result, 1);
        }
      } break;
      default:
        UNREACHABLE();
    }
    return;
#endif
  } else if (instr->arch_opcode() == kArchStackPointerGreaterThan) {
    Register lhs_register = sp;
    uint32_t offset;
    if (ShouldApplyOffsetToStackCheck(instr, &offset)) {
      lhs_register = i.TempRegister(0);
      __ SubWord(lhs_register, sp, offset);
    }
    __ Sgtu(result, lhs_register, Operand(i.InputRegister(0)));
    return;
  } else if (instr->arch_opcode() == kRiscvCmpD ||
             instr->arch_opcode() == kRiscvCmpS) {
    if (instr->arch_opcode() == kRiscvCmpD) {
      FPURegister left = i.InputOrZeroDoubleRegister(0);
      FPURegister right = i.InputOrZeroDoubleRegister(1);
      if ((left == kDoubleRegZero || right == kDoubleRegZero) &&
          !__ IsDoubleZeroRegSet()) {
        __ LoadFPRImmediate(kDoubleRegZero, 0.0);
      }
    } else {
      FPURegister left = i.InputOrZeroSingleRegister(0);
      FPURegister right = i.InputOrZeroSingleRegister(1);
      if ((left == kSingleRegZero || right == kSingleRegZero) &&
          !__ IsSingleZeroRegSet()) {
        __ LoadFPRImmediate(kSingleRegZero, 0.0f);
      }
    }
    bool predicate;
    FlagsConditionToConditionCmpFPU(&predicate, condition);
    // RISCV compare returns 0 or 1, do nothing when predicate; otherwise
    // toggle kScratchReg (i.e., 0 -> 1, 1 -> 0)
    if (predicate) {
      __ Move(result, kScratchReg);
    } else {
      __ Xor(result, kScratchReg, 1);
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
  RiscvOperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }
  AssembleArchBinarySearchSwitchRange(input, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  RiscvOperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  size_t const case_count = instr->InputCount() - 2;

  __ Branch(GetLabel(i.InputRpo(1)), Ugreater_equal, input,
            Operand(case_count));
  __ GenerateSwitchTable(input, case_count, [&i, this](size_t index) {
    return GetLabel(i.InputRpo(index + 2));
  });
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
      if (info()->GetOutputStackFrameType() == StackFrame::C_WASM_ENTRY) {
        __ StubPrologue(StackFrame::C_WASM_ENTRY);
        // Reserve stack space for saving the c_entry_fp later.
        __ SubWord(sp, sp, Operand(kSystemPointerSize));
      } else {
        __ Push(ra, fp);
        __ Move(fp, sp);
      }
    } else if (call_descriptor->IsJSFunctionCall()) {
      __ Prologue();
    } else {
      __ StubPrologue(info()->GetOutputStackFrameType());
      if (call_descriptor->IsWasmFunctionCall() ||
          call_descriptor->IsWasmImportWrapper() ||
          call_descriptor->IsWasmCapiFunction()) {
        __ Push(kWasmInstanceRegister);
      }
      if (call_descriptor->IsWasmImportWrapper()) {
        // If the wrapper is running on a secondary stack, it will switch to the
        // central stack and fill these slots with the central stack pointer and
        // secondary stack limit. Otherwise the slots remain empty.
#if V8_TARGET_ARCH_RISCV64
        static_assert(WasmImportWrapperFrameConstants::kCentralStackSPOffset ==
                      -24);
        static_assert(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset == -32);
#elif V8_TARGET_ARCH_RISCV32
        static_assert(WasmImportWrapperFrameConstants::kCentralStackSPOffset ==
                      -12);
        static_assert(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset == -16);
#endif
        __ push(zero_reg);
        __ push(zero_reg);
      } else if (call_descriptor->IsWasmCapiFunction()) {
        // Reserve space for saving the PC later.
        __ SubWord(sp, sp, Operand(kSystemPointerSize));
      }
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
    if (info()->IsWasm() && required_slots > 128) {
      // For WebAssembly functions with big frames we have to do the stack
      // overflow check before we construct the frame. Otherwise we may not
      // have enough space on the stack to call the runtime for the stack
      // overflow.
      Label done;

      // If the frame is bigger than the stack, we throw the stack overflow
      // exception unconditionally. Thereby we can avoid the integer overflow
      // check in the condition code.
      if ((required_slots * kSystemPointerSize) <
          (v8_flags.stack_size * KB)) {
        UseScratchRegisterScope temps(masm());
        Register stack_limit = temps.Acquire();
        __ LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit);
        __ AddWord(stack_limit, stack_limit,
                 Operand(required_slots * kSystemPointerSize));
        __ Branch(&done, uge, sp, Operand(stack_limit));
      }

      __ Call(static_cast<intptr_t>(Builtin::kWasmStackOverflow),
              RelocInfo::WASM_STUB_CALL);
      // We come from WebAssembly, there are no references for the GC.
      ReferenceMap* reference_map = zone()->New<ReferenceMap>(zone());
      RecordSafepoint(reference_map);
      if (v8_flags.debug_code) {
        __ stop();
      }

      __ bind(&done);
    }
  }

  const int returns = frame()->GetReturnSlotCount();

  // Skip callee-saved and return slots, which are pushed below.
  required_slots -= saves.Count();
  required_slots -= saves_fpu.Count() * (kDoubleSize / kSystemPointerSize);
  required_slots -= returns;
  if (required_slots > 0) {
    __ SubWord(sp, sp, Operand(required_slots * kSystemPointerSize));
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
    __ SubWord(sp, sp, Operand(returns * kSystemPointerSize));
  }

  for (int spill_slot : frame()->tagged_slots()) {
    FrameOffset offset = frame_access_state()->GetFrameOffset(spill_slot);
    DCHECK(offset.from_frame_pointer());
    __ StoreWord(zero_reg, MemOperand(fp, offset.offset()));
  }
}

void CodeGenerator::AssembleReturn(InstructionOperand* additional_pop_count) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const int returns = frame()->GetReturnSlotCount();
  if (returns != 0) {
    __ AddWord(sp, sp, Operand(returns * kSystemPointerSize));
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

  RiscvOperandConverter g(this, nullptr);

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
                Operand(static_cast<intptr_t>(0)));
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
      __ LoadWord(t0, MemOperand(fp, StandardFrameConstants::kArgCOffset));
    }
    AssembleDeconstructFrame();
  }
  if (drop_jsargs) {
    // We must pop all arguments from the stack (including the receiver). This
    // number of arguments is given by max(1 + argc_reg, parameter_slots).
    if (parameter_slots > 1) {
      Label done;
      __ li(kScratchReg, parameter_slots);
      __ BranchShort(&done, ge, t0, Operand(kScratchReg));
      __ Move(t0, kScratchReg);
      __ bind(&done);
    }
    __ SllWord(t0, t0, kSystemPointerSizeLog2);
    __ AddWord(sp, sp, t0);
  } else if (additional_pop_count->IsImmediate()) {
    // it should be a kInt32 or a kInt64
    DCHECK_LE(g.ToConstant(additional_pop_count).type(), Constant::kInt64);
    int additional_count = g.ToConstant(additional_pop_count).ToInt32();
    __ Drop(parameter_slots + additional_count);
  } else {
    Register pop_reg = g.ToRegister(additional_pop_count);
    __ Drop(parameter_slots);
    __ SllWord(pop_reg, pop_reg, kSystemPointerSizeLog2);
    __ AddWord(sp, sp, pop_reg);
  }
  __ Ret();
}

void CodeGenerator::FinishCode() { __ ForceConstantPoolEmissionWithoutJump(); }

void CodeGenerator::PrepareForDeoptimizationExits(
    ZoneDeque<DeoptimizationExit*>* exits) {
  __ ForceConstantPoolEmissionWithoutJump();
  int total_size = 0;
  for (DeoptimizationExit* exit : deoptimization_exits_) {
    total_size += (exit->kind() == DeoptimizeKind::kLazy)
                      ? Deoptimizer::kLazyDeoptExitSize
                      : Deoptimizer::kEagerDeoptExitSize;
  }

  __ CheckTrampolinePoolQuick(total_size);
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
    RiscvOperandConverter g(this, nullptr);
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

void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  RiscvOperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  if (source->IsRegister()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      __ Move(g.ToRegister(destination), src);
    } else {
      __ StoreWord(src, g.ToMemOperand(destination));
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsRegister()) {
      __ LoadWord(g.ToRegister(destination), src);
    } else {
      Register temp = kScratchReg;
      __ LoadWord(temp, src);
      __ StoreWord(temp, g.ToMemOperand(destination));
    }
  } else if (source->IsConstant()) {
    Constant src = g.ToConstant(source);
    if (destination->IsRegister() || destination->IsStackSlot()) {
      Register dst =
          destination->IsRegister() ? g.ToRegister(destination) : kScratchReg;
      switch (src.type()) {
        case Constant::kInt32:
          if (src.ToInt32() == 0 && destination->IsStackSlot() &&
              RelocInfo::IsNoInfo(src.rmode())) {
            dst = zero_reg;
          } else {
            __ li(dst, Operand(src.ToInt32(), src.rmode()));
          }
          break;
        case Constant::kFloat32:
          __ li(dst, Operand::EmbeddedNumber(src.ToFloat32()));
          break;
        case Constant::kInt64:
          if (src.ToInt64() == 0 && destination->IsStackSlot() &&
              RelocInfo::IsNoInfo(src.rmode())) {
            dst = zero_reg;
          } else {
            __ li(dst, Operand(src.ToInt64(), src.rmode()));
          }
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
          UNREACHABLE();  // TODO(titzer): loading RPO numbers
      }
      if (destination->IsStackSlot()) {
        __ StoreWord(dst, g.ToMemOperand(destination));
      }
    } else if (src.type() == Constant::kFloat32) {
      if (destination->IsFPStackSlot()) {
        MemOperand dst = g.ToMemOperand(destination);
        if (base::bit_cast<int32_t>(src.ToFloat32()) == 0) {
          __ Sw(zero_reg, dst);
        } else {
          __ li(kScratchReg, Operand(base::bit_cast<int32_t>(src.ToFloat32())));
          __ Sw(kScratchReg, dst);
        }
      } else {
        DCHECK(destination->IsFPRegister());
        FloatRegister dst = g.ToSingleRegister(destination);
        __ LoadFPRImmediate(dst, src.ToFloat32());
      }
    } else {
      DCHECK_EQ(Constant::kFloat64, src.type());
      DoubleRegister dst = destination->IsFPRegister()
                               ? g.ToDoubleRegister(destination)
                               : kScratchDoubleReg;
      __ LoadFPRImmediate(dst, src.ToFloat64().value());
      if (destination->IsFPStackSlot()) {
        __ StoreDouble(dst, g.ToMemOperand(destination));
      }
    }
  } else if (source->IsFPRegister()) {
    MachineRepresentation rep = LocationOperand::cast(source)->representation();
    if (rep == MachineRepresentation::kSimd128) {
      VRegister src = g.ToSimd128Register(source);
      if (destination->IsSimd128Register()) {
        VRegister dst = g.ToSimd128Register(destination);
        __ VU.set(kScratchReg, E8, m1);
        __ vmv_vv(dst, src);
      } else {
        DCHECK(destination->IsSimd128StackSlot());
        __ VU.set(kScratchReg, E8, m1);
        MemOperand dst = g.ToMemOperand(destination);
        Register dst_r = dst.rm();
        if (dst.offset() != 0) {
          dst_r = kScratchReg;
          __ AddWord(dst_r, dst.rm(), dst.offset());
        }
        __ vs(src, dst_r, 0, E8);
      }
    } else {
      FPURegister src = g.ToDoubleRegister(source);
      if (destination->IsFPRegister()) {
        FPURegister dst = g.ToDoubleRegister(destination);
        if (rep == MachineRepresentation::kFloat32) {
          // In src/builtins/wasm-to-js.tq:193
          //*toRef =
          //Convert<intptr>(Bitcast<uint32>(WasmTaggedToFloat32(retVal))); so
          // high 32 of src is 0. fmv.s can't NaNBox src.
          __ fmv_x_w(kScratchReg, src);
          __ fmv_w_x(dst, kScratchReg);
        } else {
          __ MoveDouble(dst, src);
        }
      } else {
        DCHECK(destination->IsFPStackSlot());
        if (rep == MachineRepresentation::kFloat32) {
          __ StoreFloat(src, g.ToMemOperand(destination));
        } else {
          DCHECK_EQ(rep, MachineRepresentation::kFloat64);
          __ StoreDouble(src, g.ToMemOperand(destination));
        }
      }
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPRegister() || destination->IsFPStackSlot());
    MemOperand src = g.ToMemOperand(source);
    MachineRepresentation rep = LocationOperand::cast(source)->representation();
    if (rep == MachineRepresentation::kSimd128) {
      __ VU.set(kScratchReg, E8, m1);
      Register src_r = src.rm();
      if (src.offset() != 0) {
        src_r = kScratchReg;
        __ AddWord(src_r, src.rm(), src.offset());
      }
      if (destination->IsSimd128Register()) {
        __ vl(g.ToSimd128Register(destination), src_r, 0, E8);
      } else {
        DCHECK(destination->IsSimd128StackSlot());
        VRegister temp = kSimd128ScratchReg;
        MemOperand dst = g.ToMemOperand(destination);
        Register dst_r = dst.rm();
        if (dst.offset() != 0) {
          dst_r = kScratchReg2;
          __ AddWord(dst_r, dst.rm(), dst.offset());
        }
        __ vl(temp, src_r, 0, E8);
        __ vs(temp, dst_r, 0, E8);
      }
    } else {
      if (destination->IsFPRegister()) {
        if (rep == MachineRepresentation::kFloat32) {
          __ LoadFloat(g.ToDoubleRegister(destination), src);
        } else {
          DCHECK_EQ(rep, MachineRepresentation::kFloat64);
          __ LoadDouble(g.ToDoubleRegister(destination), src);
        }
      } else {
        DCHECK(destination->IsFPStackSlot());
        FPURegister temp = kScratchDoubleReg;
        if (rep == MachineRepresentation::kFloat32) {
          __ LoadFloat(temp, src);
          __ StoreFloat(temp, g.ToMemOperand(destination));
        } else {
          DCHECK_EQ(rep, MachineRepresentation::kFloat64);
          __ LoadDouble(temp, src);
          __ StoreDouble(temp, g.ToMemOperand(destination));
        }
      }
    }
  } else {
    UNREACHABLE();
  }
}

void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  RiscvOperandConverter g(this, nullptr);
  switch (MoveType::InferSwap(source, destination)) {
    case MoveType::kRegisterToRegister:
      if (source->IsRegister()) {
        Register temp = kScratchReg;
        Register src = g.ToRegister(source);
        Register dst = g.ToRegister(destination);
        __ Move(temp, src);
        __ Move(src, dst);
        __ Move(dst, temp);
      } else {
        if (source->IsFloatRegister() || source->IsDoubleRegister()) {
          FPURegister temp = kScratchDoubleReg;
          FPURegister src = g.ToDoubleRegister(source);
          FPURegister dst = g.ToDoubleRegister(destination);
          __ Move(temp, src);
          __ Move(src, dst);
          __ Move(dst, temp);
        } else {
          DCHECK(source->IsSimd128Register());
          VRegister src = g.ToDoubleRegister(source).toV();
          VRegister dst = g.ToDoubleRegister(destination).toV();
          VRegister temp = kSimd128ScratchReg;
          __ VU.set(kScratchReg, E8, m1);
          __ vmv_vv(temp, src);
          __ vmv_vv(src, dst);
          __ vmv_vv(dst, temp);
        }
      }
      break;
    case MoveType::kRegisterToStack: {
      MemOperand dst = g.ToMemOperand(destination);
      if (source->IsRegister()) {
        Register temp = kScratchReg;
        Register src = g.ToRegister(source);
        __ mv(temp, src);
        __ LoadWord(src, dst);
        __ StoreWord(temp, dst);
      } else {
        MemOperand dst = g.ToMemOperand(destination);
        if (source->IsFloatRegister()) {
          DoubleRegister src = g.ToDoubleRegister(source);
          DoubleRegister temp = kScratchDoubleReg;
          __ fmv_s(temp, src);
          __ LoadFloat(src, dst);
          __ StoreFloat(temp, dst);
        } else if (source->IsDoubleRegister()) {
          DoubleRegister src = g.ToDoubleRegister(source);
          DoubleRegister temp = kScratchDoubleReg;
          __ fmv_d(temp, src);
          __ LoadDouble(src, dst);
          __ StoreDouble(temp, dst);
        } else {
          DCHECK(source->IsSimd128Register());
          VRegister src = g.ToDoubleRegister(source).toV();
          VRegister temp = kSimd128ScratchReg;
          __ VU.set(kScratchReg, E8, m1);
          __ vmv_vv(temp, src);
          Register dst_v = dst.rm();
          if (dst.offset() != 0) {
            dst_v = kScratchReg2;
            __ AddWord(dst_v, dst.rm(), Operand(dst.offset()));
          }
          __ vl(src, dst_v, 0, E8);
          __ vs(temp, dst_v, 0, E8);
        }
      }
    } break;
    case MoveType::kStackToStack: {
      MemOperand src = g.ToMemOperand(source);
      MemOperand dst = g.ToMemOperand(destination);
      if (source->IsSimd128StackSlot()) {
        __ VU.set(kScratchReg, E8, m1);
        Register src_v = src.rm();
        Register dst_v = dst.rm();
        if (src.offset() != 0) {
          src_v = kScratchReg;
          __ AddWord(src_v, src.rm(), Operand(src.offset()));
        }
        if (dst.offset() != 0) {
          dst_v = kScratchReg2;
          __ AddWord(dst_v, dst.rm(), Operand(dst.offset()));
        }
        __ vl(kSimd128ScratchReg, src_v, 0, E8);
        __ vl(kSimd128ScratchReg2, dst_v, 0, E8);
        __ vs(kSimd128ScratchReg, dst_v, 0, E8);
        __ vs(kSimd128ScratchReg2, src_v, 0, E8);
      } else {
#if V8_TARGET_ARCH_RISCV32
        if (source->IsFPStackSlot()) {
          DCHECK(destination->IsFPStackSlot());
          MachineRepresentation rep =
              LocationOperand::cast(source)->representation();
          if (rep == MachineRepresentation::kFloat64) {
            FPURegister temp_double = kScratchDoubleReg;
            Register temp_word32 = kScratchReg;
            MemOperand src_hi(src.rm(), src.offset() + kSystemPointerSize);
            MemOperand dst_hi(dst.rm(), dst.offset() + kSystemPointerSize);
            __ LoadDouble(temp_double, src);
            __ Lw(temp_word32, dst);
            __ Sw(temp_word32, src);
            __ Lw(temp_word32, dst_hi);
            __ Sw(temp_word32, src_hi);
            __ StoreDouble(temp_double, dst);
            break;
          }
        }
#endif
        UseScratchRegisterScope scope(masm());
        Register temp_0 = kScratchReg;
        Register temp_1 = kScratchReg2;
        __ LoadWord(temp_0, src);
        __ LoadWord(temp_1, dst);
        __ StoreWord(temp_0, dst);
        __ StoreWord(temp_1, src);
      }
    } break;
    default:
      UNREACHABLE();
  }
}

AllocatedOperand CodeGenerator::Push(InstructionOperand* source) {
  auto rep = LocationOperand::cast(source)->representation();
  int new_slots = ElementSizeInPointers(rep);
  RiscvOperandConverter g(this, nullptr);
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
    __ LoadWord(scratch, g.ToMemOperand(source));
    __ Push(scratch);
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else {
    // No push instruction for this operand type. Bump the stack pointer and
    // assemble the move.
    __ SubWord(sp, sp, Operand(new_slots * kSystemPointerSize));
    frame_access_state()->IncreaseSPDelta(new_slots);
    AssembleMove(source, &stack_slot);
  }
  temp_slots_ += new_slots;
  return stack_slot;
}

void CodeGenerator::Pop(InstructionOperand* dest, MachineRepresentation rep) {
  int dropped_slots = ElementSizeInPointers(rep);
  RiscvOperandConverter g(this, nullptr);
  if (dest->IsRegister()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ Pop(g.ToRegister(dest));
  } else if (dest->IsStackSlot()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    __ Pop(scratch);
    __ StoreWord(scratch, g.ToMemOperand(dest));
  } else {
    int last_frame_slot_id =
        frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
    int sp_delta = frame_access_state_->sp_delta();
    int slot_id = last_frame_slot_id + sp_delta;
    AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
    AssembleMove(&stack_slot, dest);
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ AddWord(sp, sp, Operand(dropped_slots * kSystemPointerSize));
  }
  temp_slots_ -= dropped_slots;
}

void CodeGenerator::PopTempStackSlots() {
  if (temp_slots_ > 0) {
    frame_access_state()->IncreaseSPDelta(-temp_slots_);
    __ AddWord(sp, sp, Operand(temp_slots_ * kSystemPointerSize));
    temp_slots_ = 0;
  }
}

void CodeGenerator::AssembleJumpTable(Label** targets, size_t target_count) {
  // On 64-bit RISC-V we emit the jump tables inline.
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
