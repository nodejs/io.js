// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/code-generator-impl.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/gap-resolver.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/osr.h"
#include "src/heap/mutable-page-metadata.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

#define __ masm()->

#define kScratchReg ip

// Adds S390-specific methods to convert InstructionOperands.
class S390OperandConverter final : public InstructionOperandConverter {
 public:
  S390OperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  size_t OutputCount() { return instr_->OutputCount(); }

  bool Is64BitOperand(int index) {
    return LocationOperand::cast(instr_->InputAt(index))->representation() ==
           MachineRepresentation::kWord64;
  }

  bool Is32BitOperand(int index) {
    return LocationOperand::cast(instr_->InputAt(index))->representation() ==
           MachineRepresentation::kWord32;
  }

  bool CompareLogical() const {
    switch (instr_->flags_condition()) {
      case kUnsignedLessThan:
      case kUnsignedGreaterThanOrEqual:
      case kUnsignedLessThanOrEqual:
      case kUnsignedGreaterThan:
        return true;
      default:
        return false;
    }
    UNREACHABLE();
  }

  Operand InputImmediate(size_t index) {
    Constant constant = ToConstant(instr_->InputAt(index));
    switch (constant.type()) {
      case Constant::kInt32:
        return Operand(constant.ToInt32());
      case Constant::kFloat32:
        return Operand::EmbeddedNumber(constant.ToFloat32());
      case Constant::kFloat64:
        return Operand::EmbeddedNumber(constant.ToFloat64().value());
      case Constant::kInt64:
        return Operand(constant.ToInt64());
      case Constant::kExternalReference:
        return Operand(constant.ToExternalReference());
      case Constant::kCompressedHeapObject:
      case Constant::kHeapObject:
      case Constant::kRpoNumber:
        break;
    }
    UNREACHABLE();
  }

  MemOperand MemoryOperand(AddressingMode* mode, size_t* first_index) {
    const size_t index = *first_index;
    if (mode) *mode = AddressingModeField::decode(instr_->opcode());
    switch (AddressingModeField::decode(instr_->opcode())) {
      case kMode_None:
        break;
      case kMode_MR:
        *first_index += 1;
        return MemOperand(InputRegister(index + 0), 0);
      case kMode_MRI:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputInt32(index + 1));
      case kMode_MRR:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1));
      case kMode_MRRI:
        *first_index += 3;
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1),
                          InputInt32(index + 2));
      case kMode_Root:
        *first_index += 1;
        return MemOperand(kRootRegister, InputInt32(index));
    }
    UNREACHABLE();
  }

  MemOperand MemoryOperand(AddressingMode* mode = nullptr,
                           size_t first_index = 0) {
    return MemoryOperand(mode, &first_index);
  }

  MemOperand ToMemOperand(InstructionOperand* op) const {
    DCHECK_NOT_NULL(op);
    DCHECK(op->IsStackSlot() || op->IsFPStackSlot());
    return SlotToMemOperand(AllocatedOperand::cast(op)->index());
  }

  MemOperand SlotToMemOperand(int slot) const {
    FrameOffset offset = frame_access_state()->GetFrameOffset(slot);
    return MemOperand(offset.from_stack_pointer() ? sp : fp, offset.offset());
  }

  MemOperand InputStackSlot(size_t index) {
    InstructionOperand* op = instr_->InputAt(index);
    return SlotToMemOperand(AllocatedOperand::cast(op)->index());
  }

  MemOperand InputStackSlot32(size_t index) {
#if V8_TARGET_ARCH_S390X && !V8_TARGET_LITTLE_ENDIAN
    // We want to read the 32-bits directly from memory
    MemOperand mem = InputStackSlot(index);
    return MemOperand(mem.rx(), mem.rb(), mem.offset() + 4);
#else
    return InputStackSlot(index);
#endif
  }
};

static inline bool HasRegisterOutput(Instruction* instr, int index = 0) {
  return instr->OutputCount() > 0 && instr->OutputAt(index)->IsRegister();
}

static inline bool HasFPRegisterInput(Instruction* instr, int index) {
  return instr->InputAt(index)->IsFPRegister();
}

static inline bool HasRegisterInput(Instruction* instr, int index) {
  return instr->InputAt(index)->IsRegister() ||
         HasFPRegisterInput(instr, index);
}

static inline bool HasImmediateInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsImmediate();
}

static inline bool HasFPStackSlotInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsFPStackSlot();
}

static inline bool HasStackSlotInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsStackSlot() ||
         HasFPStackSlotInput(instr, index);
}

namespace {

class OutOfLineRecordWrite final : public OutOfLineCode {
 public:
  OutOfLineRecordWrite(CodeGenerator* gen, Register object, MemOperand operand,
                       Register value, Register scratch0, Register scratch1,
                       RecordWriteMode mode, StubCallMode stub_mode,
                       UnwindingInfoWriter* unwinding_info_writer)
      : OutOfLineCode(gen),
        object_(object),
        operand_(operand),
        value_(value),
        scratch0_(scratch0),
        scratch1_(scratch1),
        mode_(mode),
#if V8_ENABLE_WEBASSEMBLY
        stub_mode_(stub_mode),
#endif  // V8_ENABLE_WEBASSEMBLY
        must_save_lr_(!gen->frame_access_state()->has_frame()),
        unwinding_info_writer_(unwinding_info_writer),
        zone_(gen->zone()) {
    DCHECK(!AreAliased(object, scratch0, scratch1));
    DCHECK(!AreAliased(value, scratch0, scratch1));
  }

  void Generate() final {
    if (COMPRESS_POINTERS_BOOL) {
      __ DecompressTagged(value_, value_);
    }
    __ CheckPageFlag(value_, scratch0_,
                     MemoryChunk::kPointersToHereAreInterestingMask, eq,
                     exit());
    __ lay(scratch1_, operand_);
    SaveFPRegsMode const save_fp_mode = frame()->DidAllocateDoubleRegisters()
                                            ? SaveFPRegsMode::kSave
                                            : SaveFPRegsMode::kIgnore;
    if (must_save_lr_) {
      // We need to save and restore r14 if the frame was elided.
      __ Push(r14);
      unwinding_info_writer_->MarkLinkRegisterOnTopOfStack(__ pc_offset());
    }
    if (mode_ == RecordWriteMode::kValueIsEphemeronKey) {
      __ CallEphemeronKeyBarrier(object_, scratch1_, save_fp_mode);
#if V8_ENABLE_WEBASSEMBLY
    } else if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      __ CallRecordWriteStubSaveRegisters(object_, scratch1_, save_fp_mode,
                                          StubCallMode::kCallWasmRuntimeStub);
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      __ CallRecordWriteStubSaveRegisters(object_, scratch1_, save_fp_mode);
    }
    if (must_save_lr_) {
      // We need to save and restore r14 if the frame was elided.
      __ Pop(r14);
      unwinding_info_writer_->MarkPopLinkRegisterFromTopOfStack(__ pc_offset());
    }
  }

 private:
  Register const object_;
  MemOperand const operand_;
  Register const value_;
  Register const scratch0_;
  Register const scratch1_;
  RecordWriteMode const mode_;
#if V8_ENABLE_WEBASSEMBLY
  StubCallMode stub_mode_;
#endif  // V8_ENABLE_WEBASSEMBLY
  bool must_save_lr_;
  UnwindingInfoWriter* const unwinding_info_writer_;
  Zone* zone_;
};

Condition FlagsConditionToCondition(FlagsCondition condition, ArchOpcode op) {
  switch (condition) {
    case kEqual:
      return eq;
    case kNotEqual:
      return ne;
    case kUnsignedLessThan:
      // unsigned number never less than 0
      if (op == kS390_LoadAndTestWord32 || op == kS390_LoadAndTestWord64)
        return CC_NOP;
      [[fallthrough]];
    case kSignedLessThan:
      return lt;
    case kUnsignedGreaterThanOrEqual:
      // unsigned number always greater than or equal 0
      if (op == kS390_LoadAndTestWord32 || op == kS390_LoadAndTestWord64)
        return CC_ALWAYS;
      [[fallthrough]];
    case kSignedGreaterThanOrEqual:
      return ge;
    case kUnsignedLessThanOrEqual:
      // unsigned number never less than 0
      if (op == kS390_LoadAndTestWord32 || op == kS390_LoadAndTestWord64)
        return CC_EQ;
      [[fallthrough]];
    case kSignedLessThanOrEqual:
      return le;
    case kUnsignedGreaterThan:
      // unsigned number always greater than or equal 0
      if (op == kS390_LoadAndTestWord32 || op == kS390_LoadAndTestWord64)
        return ne;
      [[fallthrough]];
    case kSignedGreaterThan:
      return gt;
    case kOverflow:
      // Overflow checked for AddS64/SubS64 only.
      switch (op) {
        case kS390_Add32:
        case kS390_Add64:
        case kS390_Sub32:
        case kS390_Sub64:
        case kS390_Abs64:
        case kS390_Abs32:
        case kS390_Mul32:
        case kS390_Mul64WithOverflow:
          return overflow;
        default:
          break;
      }
      break;
    case kNotOverflow:
      switch (op) {
        case kS390_Add32:
        case kS390_Add64:
        case kS390_Sub32:
        case kS390_Sub64:
        case kS390_Abs64:
        case kS390_Abs32:
        case kS390_Mul32:
        case kS390_Mul64WithOverflow:
          return nooverflow;
        default:
          break;
      }
      break;
    default:
      break;
  }
  UNREACHABLE();
}

#define GET_MEMOPERAND32(ret, fi)                                       \
  ([&](int& ret) {                                                      \
    AddressingMode mode = AddressingModeField::decode(instr->opcode()); \
    MemOperand mem(r0);                                                 \
    if (mode != kMode_None) {                                           \
      size_t first_index = (fi);                                        \
      mem = i.MemoryOperand(&mode, &first_index);                       \
      ret = first_index;                                                \
    } else {                                                            \
      mem = i.InputStackSlot32(fi);                                     \
    }                                                                   \
    return mem;                                                         \
  })(ret)

#define GET_MEMOPERAND(ret, fi)                                         \
  ([&](int& ret) {                                                      \
    AddressingMode mode = AddressingModeField::decode(instr->opcode()); \
    MemOperand mem(r0);                                                 \
    if (mode != kMode_None) {                                           \
      size_t first_index = (fi);                                        \
      mem = i.MemoryOperand(&mode, &first_index);                       \
      ret = first_index;                                                \
    } else {                                                            \
      mem = i.InputStackSlot(fi);                                       \
    }                                                                   \
    return mem;                                                         \
  })(ret)

#define RRInstr(instr)                                \
  [&]() {                                             \
    DCHECK(i.OutputRegister() == i.InputRegister(0)); \
    __ instr(i.OutputRegister(), i.InputRegister(1)); \
    return 2;                                         \
  }
#define RIInstr(instr)                                 \
  [&]() {                                              \
    DCHECK(i.OutputRegister() == i.InputRegister(0));  \
    __ instr(i.OutputRegister(), i.InputImmediate(1)); \
    return 2;                                          \
  }
#define RMInstr(instr, GETMEM)                        \
  [&]() {                                             \
    DCHECK(i.OutputRegister() == i.InputRegister(0)); \
    int ret = 2;                                      \
    __ instr(i.OutputRegister(), GETMEM(ret, 1));     \
    return ret;                                       \
  }
#define RM32Instr(instr) RMInstr(instr, GET_MEMOPERAND32)
#define RM64Instr(instr) RMInstr(instr, GET_MEMOPERAND)

#define RRRInstr(instr)                                                   \
  [&]() {                                                                 \
    __ instr(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1)); \
    return 2;                                                             \
  }
#define RRIInstr(instr)                                                    \
  [&]() {                                                                  \
    __ instr(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1)); \
    return 2;                                                              \
  }
#define RRMInstr(instr, GETMEM)                                       \
  [&]() {                                                             \
    int ret = 2;                                                      \
    __ instr(i.OutputRegister(), i.InputRegister(0), GETMEM(ret, 1)); \
    return ret;                                                       \
  }
#define RRM32Instr(instr) RRMInstr(instr, GET_MEMOPERAND32)
#define RRM64Instr(instr) RRMInstr(instr, GET_MEMOPERAND)

#define DDInstr(instr)                                            \
  [&]() {                                                         \
    DCHECK(i.OutputDoubleRegister() == i.InputDoubleRegister(0)); \
    __ instr(i.OutputDoubleRegister(), i.InputDoubleRegister(1)); \
    return 2;                                                     \
  }

#define DMInstr(instr)                                            \
  [&]() {                                                         \
    DCHECK(i.OutputDoubleRegister() == i.InputDoubleRegister(0)); \
    int ret = 2;                                                  \
    __ instr(i.OutputDoubleRegister(), GET_MEMOPERAND(ret, 1));   \
    return ret;                                                   \
  }

#define DMTInstr(instr)                                           \
  [&]() {                                                         \
    DCHECK(i.OutputDoubleRegister() == i.InputDoubleRegister(0)); \
    int ret = 2;                                                  \
    __ instr(i.OutputDoubleRegister(), GET_MEMOPERAND(ret, 1),    \
             kScratchDoubleReg);                                  \
    return ret;                                                   \
  }

#define R_MInstr(instr)                                   \
  [&]() {                                                 \
    int ret = 2;                                          \
    __ instr(i.OutputRegister(), GET_MEMOPERAND(ret, 0)); \
    return ret;                                           \
  }

#define R_DInstr(instr)                                     \
  [&]() {                                                   \
    __ instr(i.OutputRegister(), i.InputDoubleRegister(0)); \
    return 2;                                               \
  }

#define D_DInstr(instr)                                           \
  [&]() {                                                         \
    __ instr(i.OutputDoubleRegister(), i.InputDoubleRegister(0)); \
    return 2;                                                     \
  }

#define D_MInstr(instr)                                         \
  [&]() {                                                       \
    int ret = 2;                                                \
    __ instr(i.OutputDoubleRegister(), GET_MEMOPERAND(ret, 0)); \
    return ret;                                                 \
  }

#define D_MTInstr(instr)                                       \
  [&]() {                                                      \
    int ret = 2;                                               \
    __ instr(i.OutputDoubleRegister(), GET_MEMOPERAND(ret, 0), \
             kScratchDoubleReg);                               \
    return ret;                                                \
  }

static int nullInstr() { UNREACHABLE(); }

template <int numOfOperand, class RType, class MType, class IType>
static inline int AssembleOp(Instruction* instr, RType r, MType m, IType i) {
  AddressingMode mode = AddressingModeField::decode(instr->opcode());
  if (mode != kMode_None || HasStackSlotInput(instr, numOfOperand - 1)) {
    return m();
  } else if (HasRegisterInput(instr, numOfOperand - 1)) {
    return r();
  } else if (HasImmediateInput(instr, numOfOperand - 1)) {
    return i();
  } else {
    UNREACHABLE();
  }
}

template <class _RR, class _RM, class _RI>
static inline int AssembleBinOp(Instruction* instr, _RR _rr, _RM _rm, _RI _ri) {
  return AssembleOp<2>(instr, _rr, _rm, _ri);
}

template <class _R, class _M, class _I>
static inline int AssembleUnaryOp(Instruction* instr, _R _r, _M _m, _I _i) {
  return AssembleOp<1>(instr, _r, _m, _i);
}

#define ASSEMBLE_BIN_OP(_rr, _rm, _ri) AssembleBinOp(instr, _rr, _rm, _ri)
#define ASSEMBLE_UNARY_OP(_r, _m, _i) AssembleUnaryOp(instr, _r, _m, _i)

#define CHECK_AND_ZERO_EXT_OUTPUT(num)                                \
  ([&](int index) {                                                   \
    DCHECK(HasImmediateInput(instr, (index)));                        \
    int doZeroExt = i.InputInt32(index);                              \
    if (doZeroExt) __ LoadU32(i.OutputRegister(), i.OutputRegister()); \
  })(num)

#define ASSEMBLE_BIN32_OP(_rr, _rm, _ri) \
  { CHECK_AND_ZERO_EXT_OUTPUT(AssembleBinOp(instr, _rr, _rm, _ri)); }

}  // namespace

#define ASSEMBLE_FLOAT_UNOP(asm_instr)                                \
  do {                                                                \
    __ asm_instr(i.OutputDoubleRegister(), i.InputDoubleRegister(0)); \
  } while (0)

#define ASSEMBLE_FLOAT_BINOP(asm_instr)                              \
  do {                                                               \
    __ asm_instr(i.OutputDoubleRegister(), i.InputDoubleRegister(0), \
                 i.InputDoubleRegister(1));                          \
  } while (0)

#define ASSEMBLE_COMPARE(cmp_instr, cmpl_instr)                         \
  do {                                                                  \
    AddressingMode mode = AddressingModeField::decode(instr->opcode()); \
    if (mode != kMode_None) {                                           \
      size_t first_index = 1;                                           \
      MemOperand operand = i.MemoryOperand(&mode, &first_index);        \
      if (i.CompareLogical()) {                                         \
        __ cmpl_instr(i.InputRegister(0), operand);                     \
      } else {                                                          \
        __ cmp_instr(i.InputRegister(0), operand);                      \
      }                                                                 \
    } else if (HasRegisterInput(instr, 1)) {                            \
      if (i.CompareLogical()) {                                         \
        __ cmpl_instr(i.InputRegister(0), i.InputRegister(1));          \
      } else {                                                          \
        __ cmp_instr(i.InputRegister(0), i.InputRegister(1));           \
      }                                                                 \
    } else if (HasImmediateInput(instr, 1)) {                           \
      if (i.CompareLogical()) {                                         \
        __ cmpl_instr(i.InputRegister(0), i.InputImmediate(1));         \
      } else {                                                          \
        __ cmp_instr(i.InputRegister(0), i.InputImmediate(1));          \
      }                                                                 \
    } else {                                                            \
      DCHECK(HasStackSlotInput(instr, 1));                              \
      if (i.CompareLogical()) {                                         \
        __ cmpl_instr(i.InputRegister(0), i.InputStackSlot(1));         \
      } else {                                                          \
        __ cmp_instr(i.InputRegister(0), i.InputStackSlot(1));          \
      }                                                                 \
    }                                                                   \
  } while (0)

#define ASSEMBLE_COMPARE32(cmp_instr, cmpl_instr)                       \
  do {                                                                  \
    AddressingMode mode = AddressingModeField::decode(instr->opcode()); \
    if (mode != kMode_None) {                                           \
      size_t first_index = 1;                                           \
      MemOperand operand = i.MemoryOperand(&mode, &first_index);        \
      if (i.CompareLogical()) {                                         \
        __ cmpl_instr(i.InputRegister(0), operand);                     \
      } else {                                                          \
        __ cmp_instr(i.InputRegister(0), operand);                      \
      }                                                                 \
    } else if (HasRegisterInput(instr, 1)) {                            \
      if (i.CompareLogical()) {                                         \
        __ cmpl_instr(i.InputRegister(0), i.InputRegister(1));          \
      } else {                                                          \
        __ cmp_instr(i.InputRegister(0), i.InputRegister(1));           \
      }                                                                 \
    } else if (HasImmediateInput(instr, 1)) {                           \
      if (i.CompareLogical()) {                                         \
        __ cmpl_instr(i.InputRegister(0), i.InputImmediate(1));         \
      } else {                                                          \
        __ cmp_instr(i.InputRegister(0), i.InputImmediate(1));          \
      }                                                                 \
    } else {                                                            \
      DCHECK(HasStackSlotInput(instr, 1));                              \
      if (i.CompareLogical()) {                                         \
        __ cmpl_instr(i.InputRegister(0), i.InputStackSlot32(1));       \
      } else {                                                          \
        __ cmp_instr(i.InputRegister(0), i.InputStackSlot32(1));        \
      }                                                                 \
    }                                                                   \
  } while (0)

#define ASSEMBLE_FLOAT_COMPARE(cmp_rr_instr, cmp_rm_instr, load_instr)     \
  do {                                                                     \
    AddressingMode mode = AddressingModeField::decode(instr->opcode());    \
    if (mode != kMode_None) {                                              \
      size_t first_index = 1;                                              \
      MemOperand operand = i.MemoryOperand(&mode, &first_index);           \
      __ cmp_rm_instr(i.InputDoubleRegister(0), operand);                  \
    } else if (HasFPRegisterInput(instr, 1)) {                             \
      __ cmp_rr_instr(i.InputDoubleRegister(0), i.InputDoubleRegister(1)); \
    } else {                                                               \
      USE(HasFPStackSlotInput);                                            \
      DCHECK(HasFPStackSlotInput(instr, 1));                               \
      MemOperand operand = i.InputStackSlot(1);                            \
      if (operand.offset() >= 0) {                                         \
        __ cmp_rm_instr(i.InputDoubleRegister(0), operand);                \
      } else {                                                             \
        __ load_instr(kScratchDoubleReg, operand);                         \
        __ cmp_rr_instr(i.InputDoubleRegister(0), kScratchDoubleReg);      \
      }                                                                    \
    }                                                                      \
  } while (0)

// Divide instruction dr will implicity use register pair
// r0 & r1 below.
// R0:R1 = R1 / divisor - R0 remainder
// Copy remainder to output reg
#define ASSEMBLE_MODULO(div_instr, shift_instr) \
  do {                                          \
    __ mov(r0, i.InputRegister(0));             \
    __ shift_instr(r0, Operand(32));            \
    __ div_instr(r0, i.InputRegister(1));       \
    __ LoadU32(i.OutputRegister(), r0);         \
  } while (0)

#define ASSEMBLE_FLOAT_MODULO()                                             \
  do {                                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                           \
    __ Push(r2, r3, r4, r5);                                                \
    __ PrepareCallCFunction(0, 2, kScratchReg);                             \
    __ MovToFloatParameters(i.InputDoubleRegister(0),                       \
                            i.InputDoubleRegister(1));                      \
    __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2); \
    __ MovFromFloatResult(i.OutputDoubleRegister());                        \
    __ Pop(r2, r3, r4, r5);                                                 \
  } while (0)

#define ASSEMBLE_IEEE754_UNOP(name)                                            \
  do {                                                                         \
    /* TODO(bmeurer): We should really get rid of this special instruction, */ \
    /* and generate a CallAddress instruction instead. */                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                              \
    __ Push(r2, r3, r4, r5);                                                   \
    __ PrepareCallCFunction(0, 1, kScratchReg);                                \
    __ MovToFloatParameter(i.InputDoubleRegister(0));                          \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 1);    \
    /* Move the result in the double result register. */                       \
    __ MovFromFloatResult(i.OutputDoubleRegister());                           \
    __ Pop(r2, r3, r4, r5);                                                    \
  } while (0)

#define ASSEMBLE_IEEE754_BINOP(name)                                           \
  do {                                                                         \
    /* TODO(bmeurer): We should really get rid of this special instruction, */ \
    /* and generate a CallAddress instruction instead. */                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                              \
    __ Push(r2, r3, r4, r5);                                                   \
    __ PrepareCallCFunction(0, 2, kScratchReg);                                \
    __ MovToFloatParameters(i.InputDoubleRegister(0),                          \
                            i.InputDoubleRegister(1));                         \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 2);    \
    /* Move the result in the double result register. */                       \
    __ MovFromFloatResult(i.OutputDoubleRegister());                           \
    __ Pop(r2, r3, r4, r5);                                                    \
  } while (0)

//
// Only MRI mode for these instructions available
#define ASSEMBLE_LOAD_FLOAT(asm_instr)                \
  do {                                                \
    DoubleRegister result = i.OutputDoubleRegister(); \
    AddressingMode mode = kMode_None;                 \
    MemOperand operand = i.MemoryOperand(&mode);      \
    __ asm_instr(result, operand);                    \
  } while (0)

#define ASSEMBLE_LOAD_INTEGER(asm_instr)         \
  do {                                           \
    Register result = i.OutputRegister();        \
    AddressingMode mode = kMode_None;            \
    MemOperand operand = i.MemoryOperand(&mode); \
    __ asm_instr(result, operand);               \
  } while (0)

#define ASSEMBLE_LOADANDTEST64(asm_instr_rr, asm_instr_rm)              \
  {                                                                     \
    AddressingMode mode = AddressingModeField::decode(instr->opcode()); \
    Register dst = HasRegisterOutput(instr) ? i.OutputRegister() : r0;  \
    if (mode != kMode_None) {                                           \
      size_t first_index = 0;                                           \
      MemOperand operand = i.MemoryOperand(&mode, &first_index);        \
      __ asm_instr_rm(dst, operand);                                    \
    } else if (HasRegisterInput(instr, 0)) {                            \
      __ asm_instr_rr(dst, i.InputRegister(0));                         \
    } else {                                                            \
      DCHECK(HasStackSlotInput(instr, 0));                              \
      __ asm_instr_rm(dst, i.InputStackSlot(0));                        \
    }                                                                   \
  }

#define ASSEMBLE_LOADANDTEST32(asm_instr_rr, asm_instr_rm)              \
  {                                                                     \
    AddressingMode mode = AddressingModeField::decode(instr->opcode()); \
    Register dst = HasRegisterOutput(instr) ? i.OutputRegister() : r0;  \
    if (mode != kMode_None) {                                           \
      size_t first_index = 0;                                           \
      MemOperand operand = i.MemoryOperand(&mode, &first_index);        \
      __ asm_instr_rm(dst, operand);                                    \
    } else if (HasRegisterInput(instr, 0)) {                            \
      __ asm_instr_rr(dst, i.InputRegister(0));                         \
    } else {                                                            \
      DCHECK(HasStackSlotInput(instr, 0));                              \
      __ asm_instr_rm(dst, i.InputStackSlot32(0));                      \
    }                                                                   \
  }

#define ASSEMBLE_STORE_FLOAT32()                         \
  do {                                                   \
    size_t index = 0;                                    \
    AddressingMode mode = kMode_None;                    \
    MemOperand operand = i.MemoryOperand(&mode, &index); \
    DoubleRegister value = i.InputDoubleRegister(index); \
    __ StoreF32(value, operand);                         \
  } while (0)

#define ASSEMBLE_STORE_DOUBLE()                          \
  do {                                                   \
    size_t index = 0;                                    \
    AddressingMode mode = kMode_None;                    \
    MemOperand operand = i.MemoryOperand(&mode, &index); \
    DoubleRegister value = i.InputDoubleRegister(index); \
    __ StoreF64(value, operand);                         \
  } while (0)

#define ASSEMBLE_STORE_INTEGER(asm_instr)                \
  do {                                                   \
    size_t index = 0;                                    \
    AddressingMode mode = kMode_None;                    \
    MemOperand operand = i.MemoryOperand(&mode, &index); \
    Register value = i.InputRegister(index);             \
    __ asm_instr(value, operand);                        \
  } while (0)

static inline bool is_wasm_on_be(OptimizedCompilationInfo* info) {
#if defined(V8_ENABLE_WEBASSEMBLY) && defined(V8_TARGET_BIG_ENDIAN)
  return info->IsWasm();
#else
  return false;
#endif
}

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_BYTE(load_and_ext)                   \
  do {                                                                        \
    Register old_value = i.InputRegister(0);                                  \
    Register new_value = i.InputRegister(1);                                  \
    Register output = i.OutputRegister();                                     \
    Register addr = kScratchReg;                                              \
    Register temp0 = r0;                                                      \
    Register temp1 = r1;                                                      \
    size_t index = 2;                                                         \
    AddressingMode mode = kMode_None;                                         \
    MemOperand op = i.MemoryOperand(&mode, &index);                           \
    __ lay(addr, op);                                                         \
    __ AtomicCmpExchangeU8(addr, output, old_value, new_value, temp0, temp1); \
    __ load_and_ext(output, output);                                          \
  } while (false)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_HALFWORD(load_and_ext)           \
  do {                                                                    \
    Register old_value = i.InputRegister(0);                              \
    Register new_value = i.InputRegister(1);                              \
    Register output = i.OutputRegister();                                 \
    Register addr = kScratchReg;                                          \
    Register temp0 = r0;                                                  \
    Register temp1 = r1;                                                  \
    size_t index = 2;                                                     \
    AddressingMode mode = kMode_None;                                     \
    MemOperand op = i.MemoryOperand(&mode, &index);                       \
    __ lay(addr, op);                                                     \
    if (is_wasm_on_be(info())) {                                          \
      Register temp2 =                                                    \
          GetRegisterThatIsNotOneOf(output, old_value, new_value);        \
      Register temp3 =                                                    \
          GetRegisterThatIsNotOneOf(output, old_value, new_value, temp2); \
      __ Push(temp2, temp3);                                              \
      __ lrvr(temp2, old_value);                                          \
      __ lrvr(temp3, new_value);                                          \
      __ ShiftRightU32(temp2, temp2, Operand(16));                        \
      __ ShiftRightU32(temp3, temp3, Operand(16));                        \
      __ AtomicCmpExchangeU16(addr, output, temp2, temp3, temp0, temp1);  \
      __ lrvr(output, output);                                            \
      __ ShiftRightU32(output, output, Operand(16));                      \
      __ Pop(temp2, temp3);                                               \
    } else {                                                              \
      __ AtomicCmpExchangeU16(addr, output, old_value, new_value, temp0,  \
                              temp1);                                     \
    }                                                                     \
    __ load_and_ext(output, output);                                      \
  } while (false)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_WORD()         \
  do {                                                  \
    Register new_val = i.InputRegister(1);              \
    Register output = i.OutputRegister();               \
    Register addr = kScratchReg;                        \
    size_t index = 2;                                   \
    AddressingMode mode = kMode_None;                   \
    MemOperand op = i.MemoryOperand(&mode, &index);     \
    __ lay(addr, op);                                   \
    if (is_wasm_on_be(info())) {                        \
      __ lrvr(r0, output);                              \
      __ lrvr(r1, new_val);                             \
      __ CmpAndSwap(r0, r1, MemOperand(addr));          \
      __ lrvr(output, r0);                              \
    } else {                                            \
      __ CmpAndSwap(output, new_val, MemOperand(addr)); \
    }                                                   \
    __ LoadU32(output, output);                         \
  } while (false)

#define ASSEMBLE_ATOMIC_BINOP_WORD(load_and_op, op)    \
  do {                                                 \
    Register value = i.InputRegister(2);               \
    Register result = i.OutputRegister(0);             \
    Register addr = r1;                                \
    AddressingMode mode = kMode_None;                  \
    MemOperand op = i.MemoryOperand(&mode);            \
    __ lay(addr, op);                                  \
    if (is_wasm_on_be(info())) {                       \
      Label do_cs;                                     \
      __ bind(&do_cs);                                 \
      __ LoadU32(r0, MemOperand(addr));                \
      __ lrvr(ip, r0);                                 \
      __ op(ip, ip, value);                            \
      __ lrvr(ip, ip);                                 \
      __ CmpAndSwap(r0, ip, MemOperand(addr));         \
      __ bne(&do_cs, Label::kNear);                    \
      __ lrvr(result, r0);                             \
    } else {                                           \
      __ load_and_op(result, value, MemOperand(addr)); \
    }                                                  \
    __ LoadU32(result, result);                        \
  } while (false)

#define ASSEMBLE_ATOMIC_BINOP_WORD64(load_and_op, op) \
  do {                                                \
    Register value = i.InputRegister(2);              \
    Register result = i.OutputRegister(0);            \
    Register addr = r1;                               \
    AddressingMode mode = kMode_None;                 \
    MemOperand op = i.MemoryOperand(&mode);           \
    __ lay(addr, op);                                 \
    if (is_wasm_on_be(info())) {                      \
      Label do_cs;                                    \
      __ bind(&do_cs);                                \
      __ LoadU64(r0, MemOperand(addr));               \
      __ lrvgr(ip, r0);                               \
      __ op(ip, ip, value);                           \
      __ lrvgr(ip, ip);                               \
      __ CmpAndSwap64(r0, ip, MemOperand(addr));      \
      __ bne(&do_cs, Label::kNear);                   \
      __ lrvgr(result, r0);                           \
      break;                                          \
    }                                                 \
    __ load_and_op(result, value, MemOperand(addr));  \
  } while (false)

#define ATOMIC_BIN_OP(bin_inst, offset, shift_amount, start, end,             \
                      maybe_reverse_bytes)                                    \
  do {                                                                        \
    /* At the moment this is only true when dealing with 2-byte values.*/     \
    bool reverse_bytes = maybe_reverse_bytes && is_wasm_on_be(info());        \
    USE(reverse_bytes);                                                       \
    Label do_cs;                                                              \
    __ LoadU32(prev, MemOperand(addr, offset));                               \
    __ bind(&do_cs);                                                          \
    if (reverse_bytes) {                                                      \
      Register temp2 = GetRegisterThatIsNotOneOf(value, result, prev);        \
      __ Push(temp2);                                                         \
      __ lrvr(temp2, prev);                                                   \
      __ RotateInsertSelectBits(temp2, temp2, Operand(start), Operand(end),   \
                                Operand(static_cast<intptr_t>(shift_amount)), \
                                true);                                        \
      __ RotateInsertSelectBits(temp, value, Operand(start), Operand(end),    \
                                Operand(static_cast<intptr_t>(shift_amount)), \
                                true);                                        \
      __ bin_inst(new_val, temp2, temp);                                      \
      __ lrvr(temp2, new_val);                                                \
      __ lr(temp, prev);                                                      \
      __ RotateInsertSelectBits(temp, temp2, Operand(start), Operand(end),    \
                                Operand(static_cast<intptr_t>(shift_amount)), \
                                false);                                       \
      __ Pop(temp2);                                                          \
    } else {                                                                  \
      __ RotateInsertSelectBits(temp, value, Operand(start), Operand(end),    \
                                Operand(static_cast<intptr_t>(shift_amount)), \
                                true);                                        \
      __ bin_inst(new_val, prev, temp);                                       \
      __ lr(temp, prev);                                                      \
      __ RotateInsertSelectBits(temp, new_val, Operand(start), Operand(end),  \
                                Operand::Zero(), false);                      \
    }                                                                         \
    __ CmpAndSwap(prev, temp, MemOperand(addr, offset));                      \
    __ bne(&do_cs, Label::kNear);                                             \
  } while (false)

#ifdef V8_TARGET_BIG_ENDIAN
#define ATOMIC_BIN_OP_HALFWORD(bin_inst, index, extract_result)      \
  {                                                                  \
    constexpr int offset = -(2 * index);                             \
    constexpr int shift_amount = 16 - (index * 16);                  \
    constexpr int start = 48 - shift_amount;                         \
    constexpr int end = start + 15;                                  \
    ATOMIC_BIN_OP(bin_inst, offset, shift_amount, start, end, true); \
    extract_result();                                                \
  }
#define ATOMIC_BIN_OP_BYTE(bin_inst, index, extract_result)           \
  {                                                                   \
    constexpr int offset = -(index);                                  \
    constexpr int shift_amount = 24 - (index * 8);                    \
    constexpr int start = 56 - shift_amount;                          \
    constexpr int end = start + 7;                                    \
    ATOMIC_BIN_OP(bin_inst, offset, shift_amount, start, end, false); \
    extract_result();                                                 \
  }
#else
#define ATOMIC_BIN_OP_HALFWORD(bin_inst, index, extract_result)       \
  {                                                                   \
    constexpr int offset = -(2 * index);                              \
    constexpr int shift_amount = index * 16;                          \
    constexpr int start = 48 - shift_amount;                          \
    constexpr int end = start + 15;                                   \
    ATOMIC_BIN_OP(bin_inst, offset, shift_amount, start, end, false); \
    extract_result();                                                 \
  }
#define ATOMIC_BIN_OP_BYTE(bin_inst, index, extract_result)           \
  {                                                                   \
    constexpr int offset = -(index);                                  \
    constexpr int shift_amount = index * 8;                           \
    constexpr int start = 56 - shift_amount;                          \
    constexpr int end = start + 7;                                    \
    ATOMIC_BIN_OP(bin_inst, offset, shift_amount, start, end, false); \
    extract_result();                                                 \
  }
#endif  // V8_TARGET_BIG_ENDIAN

#define ASSEMBLE_ATOMIC_BINOP_HALFWORD(bin_inst, extract_result) \
  do {                                                           \
    Register value = i.InputRegister(2);                         \
    Register result = i.OutputRegister(0);                       \
    Register prev = i.TempRegister(0);                           \
    Register new_val = r0;                                       \
    Register addr = r1;                                          \
    Register temp = kScratchReg;                                 \
    AddressingMode mode = kMode_None;                            \
    MemOperand op = i.MemoryOperand(&mode);                      \
    Label two, done;                                             \
    __ lay(addr, op);                                            \
    __ tmll(addr, Operand(3));                                   \
    __ b(Condition(2), &two);                                    \
    /* word boundary */                                          \
    ATOMIC_BIN_OP_HALFWORD(bin_inst, 0, extract_result);         \
    __ b(&done);                                                 \
    __ bind(&two);                                               \
    /* halfword boundary */                                      \
    ATOMIC_BIN_OP_HALFWORD(bin_inst, 1, extract_result);         \
    __ bind(&done);                                              \
  } while (false)

#define ASSEMBLE_ATOMIC_BINOP_BYTE(bin_inst, extract_result) \
  do {                                                       \
    Register value = i.InputRegister(2);                     \
    Register result = i.OutputRegister(0);                   \
    Register addr = i.TempRegister(0);                       \
    Register prev = r0;                                      \
    Register new_val = r1;                                   \
    Register temp = kScratchReg;                             \
    AddressingMode mode = kMode_None;                        \
    MemOperand op = i.MemoryOperand(&mode);                  \
    Label done, one, two, three;                             \
    __ lay(addr, op);                                        \
    __ tmll(addr, Operand(3));                               \
    __ b(Condition(1), &three);                              \
    __ b(Condition(2), &two);                                \
    __ b(Condition(4), &one);                                \
    /* ending with 0b00 (word boundary) */                   \
    ATOMIC_BIN_OP_BYTE(bin_inst, 0, extract_result);         \
    __ b(&done);                                             \
    /* ending with 0b01 */                                   \
    __ bind(&one);                                           \
    ATOMIC_BIN_OP_BYTE(bin_inst, 1, extract_result);         \
    __ b(&done);                                             \
    /* ending with 0b10 (hw boundary) */                     \
    __ bind(&two);                                           \
    ATOMIC_BIN_OP_BYTE(bin_inst, 2, extract_result);         \
    __ b(&done);                                             \
    /* ending with 0b11 */                                   \
    __ bind(&three);                                         \
    ATOMIC_BIN_OP_BYTE(bin_inst, 3, extract_result);         \
    __ bind(&done);                                          \
  } while (false)

#define ASSEMBLE_ATOMIC64_COMP_EXCHANGE_WORD64()          \
  do {                                                    \
    Register new_val = i.InputRegister(1);                \
    Register output = i.OutputRegister();                 \
    Register addr = kScratchReg;                          \
    size_t index = 2;                                     \
    AddressingMode mode = kMode_None;                     \
    MemOperand op = i.MemoryOperand(&mode, &index);       \
    __ lay(addr, op);                                     \
    if (is_wasm_on_be(info())) {                          \
      __ lrvgr(r0, output);                               \
      __ lrvgr(r1, new_val);                              \
      __ CmpAndSwap64(r0, r1, MemOperand(addr));          \
      __ lrvgr(output, r0);                               \
    } else {                                              \
      __ CmpAndSwap64(output, new_val, MemOperand(addr)); \
    }                                                     \
  } while (false)

void CodeGenerator::AssembleDeconstructFrame() {
  __ LeaveFrame(StackFrame::MANUAL);
  unwinding_info_writer_.MarkFrameDeconstructed(__ pc_offset());
}

void CodeGenerator::AssemblePrepareTailCall() {
  if (frame_access_state()->has_frame()) {
    __ RestoreFrameStateForTailCall();
  }
  frame_access_state()->SetFrameAccessToSP();
}

namespace {

void FlushPendingPushRegisters(MacroAssembler* masm,
                               FrameAccessState* frame_access_state,
                               ZoneVector<Register>* pending_pushes) {
  switch (pending_pushes->size()) {
    case 0:
      break;
    case 1:
      masm->Push((*pending_pushes)[0]);
      break;
    case 2:
      masm->Push((*pending_pushes)[0], (*pending_pushes)[1]);
      break;
    case 3:
      masm->Push((*pending_pushes)[0], (*pending_pushes)[1],
                 (*pending_pushes)[2]);
      break;
    default:
      UNREACHABLE();
  }
  frame_access_state->IncreaseSPDelta(pending_pushes->size());
  pending_pushes->clear();
}

void AdjustStackPointerForTailCall(
    MacroAssembler* masm, FrameAccessState* state, int new_slot_above_sp,
    ZoneVector<Register>* pending_pushes = nullptr,
    bool allow_shrinkage = true) {
  int current_sp_offset = state->GetSPToFPSlotCount() +
                          StandardFrameConstants::kFixedSlotCountAboveFp;
  int stack_slot_delta = new_slot_above_sp - current_sp_offset;
  if (stack_slot_delta > 0) {
    if (pending_pushes != nullptr) {
      FlushPendingPushRegisters(masm, state, pending_pushes);
    }
    masm->AddS64(sp, sp, Operand(-stack_slot_delta * kSystemPointerSize));
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    if (pending_pushes != nullptr) {
      FlushPendingPushRegisters(masm, state, pending_pushes);
    }
    masm->AddS64(sp, sp, Operand(-stack_slot_delta * kSystemPointerSize));
    state->IncreaseSPDelta(stack_slot_delta);
  }
}

}  // namespace

void CodeGenerator::AssembleTailCallBeforeGap(Instruction* instr,
                                              int first_unused_slot_offset) {
  ZoneVector<MoveOperands*> pushes(zone());
  GetPushCompatibleMoves(instr, kRegisterPush, &pushes);

  if (!pushes.empty() &&
      (LocationOperand::cast(pushes.back()->destination()).index() + 1 ==
       first_unused_slot_offset)) {
    S390OperandConverter g(this, instr);
    ZoneVector<Register> pending_pushes(zone());
    for (auto move : pushes) {
      LocationOperand destination_location(
          LocationOperand::cast(move->destination()));
      InstructionOperand source(move->source());
      AdjustStackPointerForTailCall(
          masm(), frame_access_state(),
          destination_location.index() - pending_pushes.size(),
          &pending_pushes);
      // Pushes of non-register data types are not supported.
      DCHECK(source.IsRegister());
      LocationOperand source_location(LocationOperand::cast(source));
      pending_pushes.push_back(source_location.GetRegister());
      // TODO(arm): We can push more than 3 registers at once. Add support in
      // the macro-assembler for pushing a list of registers.
      if (pending_pushes.size() == 3) {
        FlushPendingPushRegisters(masm(), frame_access_state(),
                                  &pending_pushes);
      }
      move->Eliminate();
    }
    FlushPendingPushRegisters(masm(), frame_access_state(), &pending_pushes);
  }
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset, nullptr, false);
}

void CodeGenerator::AssembleTailCallAfterGap(Instruction* instr,
                                             int first_unused_slot_offset) {
  AdjustStackPointerForTailCall(masm(), frame_access_state(),
                                first_unused_slot_offset);
}

// Check that {kJavaScriptCallCodeStartRegister} is correct.
void CodeGenerator::AssembleCodeStartRegisterCheck() {
  Register scratch = r1;
  __ ComputeCodeStartAddress(scratch);
  __ CmpS64(scratch, kJavaScriptCallCodeStartRegister);
  __ Assert(eq, AbortReason::kWrongFunctionCodeStart);
}

#ifdef V8_ENABLE_LEAPTIERING
void CodeGenerator::AssembleDispatchHandleRegisterCheck() {
  CHECK(!V8_JS_LINKAGE_INCLUDES_DISPATCH_HANDLE_BOOL);
}
#endif  // V8_ENABLE_LEAPTIERING

void CodeGenerator::BailoutIfDeoptimized() {
  __ BailoutIfDeoptimized(kScratchReg);
}

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  S390OperandConverter i(this, instr);
  ArchOpcode opcode = ArchOpcodeField::decode(instr->opcode());

  switch (opcode) {
    case kArchComment:
      __ RecordComment(reinterpret_cast<const char*>(i.InputInt64(0)),
                       SourceLocation());
      break;
    case kArchCallCodeObject: {
      if (HasRegisterInput(instr, 0)) {
        Register reg = i.InputRegister(0);
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ CallCodeObject(reg);
      } else {
        __ Call(i.InputCode(0), RelocInfo::CODE_TARGET);
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
    case kArchCallWasmFunction:
    case kArchCallWasmFunctionIndirect: {
      // We must not share code targets for calls to builtins for wasm code, as
      // they might need to be patched individually.
      if (instr->InputAt(0)->IsImmediate()) {
        DCHECK_EQ(opcode, kArchCallWasmFunction);
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Call(wasm_code, constant.rmode());
      } else if (opcode == kArchCallWasmFunctionIndirect) {
        __ CallWasmCodePointer(i.InputRegister(0));
      } else {
        __ Call(i.InputRegister(0));
      }
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchTailCallWasm:
    case kArchTailCallWasmIndirect: {
      // We must not share code targets for calls to builtins for wasm code, as
      // they might need to be patched individually.
      if (instr->InputAt(0)->IsImmediate()) {
        DCHECK_EQ(opcode, kArchTailCallWasm);
        Constant constant = i.ToConstant(instr->InputAt(0));
        Address wasm_code = static_cast<Address>(constant.ToInt64());
        __ Jump(wasm_code, constant.rmode());
      } else if (opcode == kArchTailCallWasmIndirect) {
        __ CallWasmCodePointer(i.InputRegister(0), CallJumpMode::kTailCall);
      } else {
        __ Jump(i.InputRegister(0));
      }
      frame_access_state()->ClearSPDelta();
      frame_access_state()->SetFrameAccessToDefault();
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchTailCallCodeObject: {
      if (HasRegisterInput(instr, 0)) {
        Register reg = i.InputRegister(0);
        DCHECK_IMPLIES(
            instr->HasCallDescriptorFlag(CallDescriptor::kFixedTargetRegister),
            reg == kJavaScriptCallCodeStartRegister);
        __ JumpCodeObject(reg);
      } else {
        // We cannot use the constant pool to load the target since
        // we've already restored the caller's frame.
        ConstantPoolUnavailableScope constant_pool_unavailable(masm());
        __ Jump(i.InputCode(0), RelocInfo::CODE_TARGET);
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
        __ LoadTaggedField(kScratchReg,
                           FieldMemOperand(func, JSFunction::kContextOffset));
        __ CmpS64(cp, kScratchReg);
        __ Assert(eq, AbortReason::kWrongFunctionContext);
      }
      uint32_t num_arguments =
          i.InputUint32(instr->JSCallArgumentCountInputIndex());
      __ CallJSFunction(func, num_arguments);
      RecordCallPosition(instr);
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchPrepareCallCFunction: {
      int const num_gp_parameters = ParamField::decode(instr->opcode());
      int const num_fp_parameters = FPParamField::decode(instr->opcode());
      __ PrepareCallCFunction(num_gp_parameters + num_fp_parameters,
                              kScratchReg);
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
      int bytes = __ PushCallerSaved(fp_mode_, ip, kReturnRegister0);
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
      int bytes = __ PopCallerSaved(fp_mode_, ip, kReturnRegister0);
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
      int const fp_param_field = FPParamField::decode(instr->opcode());
      int num_fp_parameters = fp_param_field;
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes;
      Label return_location;
      bool has_function_descriptor = false;
#if ABI_USES_FUNCTION_DESCRIPTORS
      int kNumFPParametersMask = kHasFunctionDescriptorBitMask - 1;
      num_fp_parameters = kNumFPParametersMask & fp_param_field;
      has_function_descriptor =
          (fp_param_field & kHasFunctionDescriptorBitMask) != 0;
#endif
      // Put the return address in a stack slot.
#if V8_ENABLE_WEBASSEMBLY
      if (linkage()->GetIncomingDescriptor()->IsWasmCapiFunction()) {
        // Put the return address in a stack slot.
        __ larl(r0, &return_location);
        __ StoreU64(r0,
                    MemOperand(fp, WasmExitFrameConstants::kCallingPCOffset));
        set_isolate_data_slots = SetIsolateDataSlots::kNo;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      int pc_offset;
      if (instr->InputAt(0)->IsImmediate()) {
        ExternalReference ref = i.InputExternalReference(0);
        pc_offset = __ CallCFunction(ref, num_gp_parameters, num_fp_parameters,
                                     set_isolate_data_slots,
                                     has_function_descriptor, &return_location);
      } else {
        Register func = i.InputRegister(0);
        pc_offset = __ CallCFunction(func, num_gp_parameters, num_fp_parameters,
                                     set_isolate_data_slots,
                                     has_function_descriptor, &return_location);
      }
      RecordSafepoint(instr->reference_map(), pc_offset);

      bool const needs_frame_state =
          (opcode == kArchCallCFunctionWithFrameState);
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
      DCHECK(i.InputRegister(0) == r3);
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
    case kArchNop:
    case kArchThrowTerminator:
      // don't emit code for nops.
      break;
    case kArchDeoptimize: {
      DeoptimizationExit* exit =
          BuildTranslation(instr, -1, 0, 0, OutputFrameStateCombine::Ignore());
      __ b(exit->label());
      break;
    }
    case kArchRet:
      AssembleReturn(instr->InputAt(0));
      break;
    case kArchFramePointer:
      __ mov(i.OutputRegister(), fp);
      break;
    case kArchParentFramePointer:
      if (frame_access_state()->has_frame()) {
        __ LoadU64(i.OutputRegister(), MemOperand(fp, 0));
      } else {
        __ mov(i.OutputRegister(), fp);
      }
      break;
#if V8_ENABLE_WEBASSEMBLY
    case kArchStackPointer:
      __ mov(i.OutputRegister(), sp);
      break;
    case kArchSetStackPointer: {
      DCHECK(instr->InputAt(0)->IsRegister());
      __ mov(sp, i.InputRegister(0));
      break;
    }
#endif  // V8_ENABLE_WEBASSEMBLY
    case kArchStackPointerGreaterThan: {
      // Potentially apply an offset to the current stack pointer before the
      // comparison to consider the size difference of an optimized frame versus
      // the contained unoptimized frames.

      Register lhs_register = sp;
      uint32_t offset;

      if (ShouldApplyOffsetToStackCheck(instr, &offset)) {
        lhs_register = i.TempRegister(0);
        __ SubS64(lhs_register, sp, Operand(offset));
      }

      constexpr size_t kValueIndex = 0;
      DCHECK(instr->InputAt(kValueIndex)->IsRegister());
      __ CmpU64(lhs_register, i.InputRegister(kValueIndex));
      break;
    }
    case kArchStackCheckOffset:
      __ LoadSmiLiteral(i.OutputRegister(),
                        Smi::FromInt(GetStackCheckOffset()));
      break;
    case kArchTruncateDoubleToI:
      __ TruncateDoubleToI(isolate(), zone(), i.OutputRegister(),
                           i.InputDoubleRegister(0), DetermineStubCallMode());
      break;
    case kArchStoreWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      AddressingMode addressing_mode =
          AddressingModeField::decode(instr->opcode());
      Register object = i.InputRegister(0);
      size_t index = 0;
      MemOperand operand = i.MemoryOperand(&addressing_mode, &index);
      Register value = i.InputRegister(index);
      Register scratch0 = i.TempRegister(0);
      Register scratch1 = i.TempRegister(1);

      if (v8_flags.debug_code) {
        // Checking that |value| is not a cleared weakref: our write barrier
        // does not support that for now.
        __ CmpS64(value, Operand(kClearedWeakHeapObjectLower32));
        __ Check(ne, AbortReason::kOperandIsCleared);
      }

      OutOfLineRecordWrite* ool = zone()->New<OutOfLineRecordWrite>(
          this, object, operand, value, scratch0, scratch1, mode,
          DetermineStubCallMode(), &unwinding_info_writer_);
      __ StoreTaggedField(value, operand);

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
      __ AddS64(i.OutputRegister(), offset.from_stack_pointer() ? sp : fp,
                Operand(offset.offset()));
      break;
    }
    case kS390_Peek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ LoadF64(i.OutputDoubleRegister(), MemOperand(fp, offset));
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ LoadF32(i.OutputFloatRegister(), MemOperand(fp, offset));
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          __ LoadV128(i.OutputSimd128Register(), MemOperand(fp, offset),
                      kScratchReg);
        }
      } else {
        __ LoadU64(i.OutputRegister(), MemOperand(fp, offset));
      }
      break;
    }
    case kS390_Abs32:
      // TODO(john.yan): zero-ext
      __ lpr(i.OutputRegister(0), i.InputRegister(0));
      break;
    case kS390_Abs64:
      __ lpgr(i.OutputRegister(0), i.InputRegister(0));
      break;
    case kS390_And32:
      // zero-ext
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN32_OP(RRRInstr(nrk), RM32Instr(And), RIInstr(nilf));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(nr), RM32Instr(And), RIInstr(nilf));
      }
      break;
    case kS390_And64:
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN_OP(RRRInstr(ngrk), RM64Instr(ng), nullInstr);
      } else {
        ASSEMBLE_BIN_OP(RRInstr(ngr), RM64Instr(ng), nullInstr);
      }
      break;
    case kS390_Or32:
      // zero-ext
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN32_OP(RRRInstr(ork), RM32Instr(Or), RIInstr(oilf));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(or_z), RM32Instr(Or), RIInstr(oilf));
      }
      break;
    case kS390_Or64:
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN_OP(RRRInstr(ogrk), RM64Instr(og), nullInstr);
      } else {
        ASSEMBLE_BIN_OP(RRInstr(ogr), RM64Instr(og), nullInstr);
      }
      break;
    case kS390_Xor32:
      // zero-ext
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN32_OP(RRRInstr(xrk), RM32Instr(Xor), RIInstr(xilf));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(xr), RM32Instr(Xor), RIInstr(xilf));
      }
      break;
    case kS390_Xor64:
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN_OP(RRRInstr(xgrk), RM64Instr(xg), nullInstr);
      } else {
        ASSEMBLE_BIN_OP(RRInstr(xgr), RM64Instr(xg), nullInstr);
      }
      break;
    case kS390_ShiftLeft32:
      // zero-ext
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN32_OP(RRRInstr(ShiftLeftU32), nullInstr,
                          RRIInstr(ShiftLeftU32));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(sll), nullInstr, RIInstr(sll));
      }
      break;
    case kS390_ShiftLeft64:
      ASSEMBLE_BIN_OP(RRRInstr(sllg), nullInstr, RRIInstr(sllg));
      break;
    case kS390_ShiftRight32:
      // zero-ext
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN32_OP(RRRInstr(srlk), nullInstr, RRIInstr(srlk));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(srl), nullInstr, RIInstr(srl));
      }
      break;
    case kS390_ShiftRight64:
      ASSEMBLE_BIN_OP(RRRInstr(srlg), nullInstr, RRIInstr(srlg));
      break;
    case kS390_ShiftRightArith32:
      // zero-ext
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN32_OP(RRRInstr(srak), nullInstr, RRIInstr(srak));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(sra), nullInstr, RIInstr(sra));
      }
      break;
    case kS390_ShiftRightArith64:
      ASSEMBLE_BIN_OP(RRRInstr(srag), nullInstr, RRIInstr(srag));
      break;
    case kS390_RotRight32: {
      // zero-ext
      if (HasRegisterInput(instr, 1)) {
        __ lcgr(kScratchReg, i.InputRegister(1));
        __ rll(i.OutputRegister(), i.InputRegister(0), kScratchReg);
      } else {
        __ rll(i.OutputRegister(), i.InputRegister(0),
               Operand(32 - i.InputInt32(1)));
      }
      CHECK_AND_ZERO_EXT_OUTPUT(2);
      break;
    }
    case kS390_RotRight64:
      if (HasRegisterInput(instr, 1)) {
        __ lcgr(kScratchReg, i.InputRegister(1));
        __ rllg(i.OutputRegister(), i.InputRegister(0), kScratchReg);
      } else {
        DCHECK(HasImmediateInput(instr, 1));
        __ rllg(i.OutputRegister(), i.InputRegister(0),
                Operand(64 - i.InputInt32(1)));
      }
      break;
    // TODO(john.yan): clean up kS390_RotLeftAnd...
    case kS390_RotLeftAndClear64:
      if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT)) {
        int shiftAmount = i.InputInt32(1);
        int endBit = 63 - shiftAmount;
        int startBit = 63 - i.InputInt32(2);
        __ RotateInsertSelectBits(i.OutputRegister(), i.InputRegister(0),
                                  Operand(startBit), Operand(endBit),
                                  Operand(shiftAmount), true);
      } else {
        int shiftAmount = i.InputInt32(1);
        int clearBit = 63 - i.InputInt32(2);
        __ rllg(i.OutputRegister(), i.InputRegister(0), Operand(shiftAmount));
        __ sllg(i.OutputRegister(), i.OutputRegister(), Operand(clearBit));
        __ srlg(i.OutputRegister(), i.OutputRegister(),
                Operand(clearBit + shiftAmount));
        __ sllg(i.OutputRegister(), i.OutputRegister(), Operand(shiftAmount));
      }
      break;
    case kS390_RotLeftAndClearLeft64:
      if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT)) {
        int shiftAmount = i.InputInt32(1);
        int endBit = 63;
        int startBit = 63 - i.InputInt32(2);
        __ RotateInsertSelectBits(i.OutputRegister(), i.InputRegister(0),
                                  Operand(startBit), Operand(endBit),
                                  Operand(shiftAmount), true);
      } else {
        int shiftAmount = i.InputInt32(1);
        int clearBit = 63 - i.InputInt32(2);
        __ rllg(i.OutputRegister(), i.InputRegister(0), Operand(shiftAmount));
        __ sllg(i.OutputRegister(), i.OutputRegister(), Operand(clearBit));
        __ srlg(i.OutputRegister(), i.OutputRegister(), Operand(clearBit));
      }
      break;
    case kS390_RotLeftAndClearRight64:
      if (CpuFeatures::IsSupported(GENERAL_INSTR_EXT)) {
        int shiftAmount = i.InputInt32(1);
        int endBit = 63 - i.InputInt32(2);
        int startBit = 0;
        __ RotateInsertSelectBits(i.OutputRegister(), i.InputRegister(0),
                                  Operand(startBit), Operand(endBit),
                                  Operand(shiftAmount), true);
      } else {
        int shiftAmount = i.InputInt32(1);
        int clearBit = i.InputInt32(2);
        __ rllg(i.OutputRegister(), i.InputRegister(0), Operand(shiftAmount));
        __ srlg(i.OutputRegister(), i.OutputRegister(), Operand(clearBit));
        __ sllg(i.OutputRegister(), i.OutputRegister(), Operand(clearBit));
      }
      break;
    case kS390_Add32: {
      // zero-ext
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN32_OP(RRRInstr(ark), RM32Instr(AddS32), RRIInstr(AddS32));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(ar), RM32Instr(AddS32), RIInstr(AddS32));
      }
      break;
    }
    case kS390_Add64:
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN_OP(RRRInstr(agrk), RM64Instr(ag), RRIInstr(AddS64));
      } else {
        ASSEMBLE_BIN_OP(RRInstr(agr), RM64Instr(ag), RIInstr(agfi));
      }
      break;
    case kS390_AddFloat:
      ASSEMBLE_BIN_OP(DDInstr(aebr), DMTInstr(AddFloat32), nullInstr);
      break;
    case kS390_AddDouble:
      ASSEMBLE_BIN_OP(DDInstr(adbr), DMTInstr(AddFloat64), nullInstr);
      break;
    case kS390_Sub32:
      // zero-ext
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN32_OP(RRRInstr(srk), RM32Instr(SubS32), RRIInstr(SubS32));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(sr), RM32Instr(SubS32), RIInstr(SubS32));
      }
      break;
    case kS390_Sub64:
      if (CpuFeatures::IsSupported(DISTINCT_OPS)) {
        ASSEMBLE_BIN_OP(RRRInstr(sgrk), RM64Instr(sg), RRIInstr(SubS64));
      } else {
        ASSEMBLE_BIN_OP(RRInstr(sgr), RM64Instr(sg), RIInstr(SubS64));
      }
      break;
    case kS390_SubFloat:
      ASSEMBLE_BIN_OP(DDInstr(sebr), DMTInstr(SubFloat32), nullInstr);
      break;
    case kS390_SubDouble:
      ASSEMBLE_BIN_OP(DDInstr(sdbr), DMTInstr(SubFloat64), nullInstr);
      break;
    case kS390_Mul32:
      // zero-ext
      if (CpuFeatures::IsSupported(MISC_INSTR_EXT2)) {
        ASSEMBLE_BIN32_OP(RRRInstr(msrkc), RM32Instr(msc), RIInstr(MulS32));
      } else {
        ASSEMBLE_BIN32_OP(RRInstr(MulS32), RM32Instr(MulS32), RIInstr(MulS32));
      }
      break;
    case kS390_Mul32WithOverflow:
      // zero-ext
      ASSEMBLE_BIN32_OP(RRRInstr(Mul32WithOverflowIfCCUnequal),
                        RRM32Instr(Mul32WithOverflowIfCCUnequal),
                        RRIInstr(Mul32WithOverflowIfCCUnequal));
      break;
    case kS390_Mul64:
      ASSEMBLE_BIN_OP(RRInstr(MulS64), RM64Instr(MulS64), RIInstr(MulS64));
      break;
    case kS390_Mul64WithOverflow: {
      Register dst = i.OutputRegister(), src1 = i.InputRegister(0),
               src2 = i.InputRegister(1);
      CHECK(!AreAliased(dst, src1, src2));
      if (CpuFeatures::IsSupported(MISC_INSTR_EXT2)) {
        __ msgrkc(dst, src1, src2);
      } else {
        // Mul high.
        __ MulHighS64(r1, src1, src2);
        // Mul low.
        __ mov(dst, src1);
        __ MulS64(dst, src2);
        // Test whether {high} is a sign-extension of {result}.
        __ ShiftRightS64(r0, dst, Operand(63));
        __ CmpU64(r1, r0);
      }
      break;
    }
    case kS390_MulHigh32:
      // zero-ext
      ASSEMBLE_BIN_OP(RRRInstr(MulHighS32), RRM32Instr(MulHighS32),
                      RRIInstr(MulHighS32));
      break;
    case kS390_MulHighU32:
      // zero-ext
      ASSEMBLE_BIN_OP(RRRInstr(MulHighU32), RRM32Instr(MulHighU32),
                      RRIInstr(MulHighU32));
      break;
    case kS390_MulHighU64:
      ASSEMBLE_BIN_OP(RRRInstr(MulHighU64), nullInstr, nullInstr);
      break;
    case kS390_MulHighS64:
      ASSEMBLE_BIN_OP(RRRInstr(MulHighS64), nullInstr, nullInstr);
      break;
    case kS390_MulFloat:
      ASSEMBLE_BIN_OP(DDInstr(meebr), DMTInstr(MulFloat32), nullInstr);
      break;
    case kS390_MulDouble:
      ASSEMBLE_BIN_OP(DDInstr(mdbr), DMTInstr(MulFloat64), nullInstr);
      break;
    case kS390_Div64:
      ASSEMBLE_BIN_OP(RRRInstr(DivS64), RRM64Instr(DivS64), nullInstr);
      break;
    case kS390_Div32: {
      // zero-ext
      ASSEMBLE_BIN_OP(RRRInstr(DivS32), RRM32Instr(DivS32), nullInstr);
      break;
    }
    case kS390_DivU64:
      ASSEMBLE_BIN_OP(RRRInstr(DivU64), RRM64Instr(DivU64), nullInstr);
      break;
    case kS390_DivU32: {
      // zero-ext
      ASSEMBLE_BIN_OP(RRRInstr(DivU32), RRM32Instr(DivU32), nullInstr);
      break;
    }
    case kS390_DivFloat:
      ASSEMBLE_BIN_OP(DDInstr(debr), DMTInstr(DivFloat32), nullInstr);
      break;
    case kS390_DivDouble:
      ASSEMBLE_BIN_OP(DDInstr(ddbr), DMTInstr(DivFloat64), nullInstr);
      break;
    case kS390_Mod32:
      // zero-ext
      ASSEMBLE_BIN_OP(RRRInstr(ModS32), RRM32Instr(ModS32), nullInstr);
      break;
    case kS390_ModU32:
      // zero-ext
      ASSEMBLE_BIN_OP(RRRInstr(ModU32), RRM32Instr(ModU32), nullInstr);
      break;
    case kS390_Mod64:
      ASSEMBLE_BIN_OP(RRRInstr(ModS64), RRM64Instr(ModS64), nullInstr);
      break;
    case kS390_ModU64:
      ASSEMBLE_BIN_OP(RRRInstr(ModU64), RRM64Instr(ModU64), nullInstr);
      break;
    case kS390_AbsFloat:
      __ lpebr(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_SqrtFloat:
      ASSEMBLE_UNARY_OP(D_DInstr(sqebr), nullInstr, nullInstr);
      break;
    case kS390_SqrtDouble:
      ASSEMBLE_UNARY_OP(D_DInstr(sqdbr), nullInstr, nullInstr);
      break;
    case kS390_FloorFloat:
      __ FloorF32(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_CeilFloat:
      __ CeilF32(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_TruncateFloat:
      __ TruncF32(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    //  Double operations
    case kS390_ModDouble:
      ASSEMBLE_FLOAT_MODULO();
      break;
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
    case kIeee754Float64Atanh:
      ASSEMBLE_IEEE754_UNOP(atanh);
      break;
    case kIeee754Float64Atan:
      ASSEMBLE_IEEE754_UNOP(atan);
      break;
    case kIeee754Float64Atan2:
      ASSEMBLE_IEEE754_BINOP(atan2);
      break;
    case kIeee754Float64Tan:
      ASSEMBLE_IEEE754_UNOP(tan);
      break;
    case kIeee754Float64Tanh:
      ASSEMBLE_IEEE754_UNOP(tanh);
      break;
    case kIeee754Float64Cbrt:
      ASSEMBLE_IEEE754_UNOP(cbrt);
      break;
    case kIeee754Float64Sin:
      ASSEMBLE_IEEE754_UNOP(sin);
      break;
    case kIeee754Float64Sinh:
      ASSEMBLE_IEEE754_UNOP(sinh);
      break;
    case kIeee754Float64Cos:
      ASSEMBLE_IEEE754_UNOP(cos);
      break;
    case kIeee754Float64Cosh:
      ASSEMBLE_IEEE754_UNOP(cosh);
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
    case kS390_Neg32:
      __ lcr(i.OutputRegister(), i.InputRegister(0));
      CHECK_AND_ZERO_EXT_OUTPUT(1);
      break;
    case kS390_Neg64:
      __ lcgr(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_MaxFloat:
      __ FloatMax(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                  i.InputDoubleRegister(1));
      break;
    case kS390_MaxDouble:
      __ DoubleMax(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                   i.InputDoubleRegister(1));
      break;
    case kS390_MinFloat:
      __ FloatMin(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                  i.InputDoubleRegister(1));
      break;
    case kS390_FloatNearestInt:
      __ NearestIntF32(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_MinDouble:
      __ DoubleMin(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                   i.InputDoubleRegister(1));
      break;
    case kS390_AbsDouble:
      __ lpdbr(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_FloorDouble:
      __ FloorF64(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_CeilDouble:
      __ CeilF64(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_TruncateDouble:
      __ TruncF64(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_RoundDouble:
      __ fidbra(ROUND_TO_NEAREST_AWAY_FROM_0, i.OutputDoubleRegister(),
                i.InputDoubleRegister(0));
      break;
    case kS390_DoubleNearestInt:
      __ NearestIntF64(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_NegFloat:
      ASSEMBLE_UNARY_OP(D_DInstr(lcebr), nullInstr, nullInstr);
      break;
    case kS390_NegDouble:
      ASSEMBLE_UNARY_OP(D_DInstr(lcdbr), nullInstr, nullInstr);
      break;
    case kS390_Cntlz32: {
      __ CountLeadingZerosU32(i.OutputRegister(), i.InputRegister(0), r0);
      break;
    }
    case kS390_Cntlz64: {
      __ CountLeadingZerosU64(i.OutputRegister(), i.InputRegister(0), r0);
      break;
    }
    case kS390_Popcnt32:
      __ Popcnt32(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_Popcnt64:
      __ Popcnt64(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_Cmp32:
      ASSEMBLE_COMPARE32(CmpS32, CmpU32);
      break;
    case kS390_Cmp64:
      ASSEMBLE_COMPARE(CmpS64, CmpU64);
      break;
    case kS390_CmpFloat:
      ASSEMBLE_FLOAT_COMPARE(cebr, ceb, ley);
      // __ cebr(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      break;
    case kS390_CmpDouble:
      ASSEMBLE_FLOAT_COMPARE(cdbr, cdb, ldy);
      // __ cdbr(i.InputDoubleRegister(0), i.InputDoubleRegister(1));
      break;
    case kS390_Tst32:
      if (HasRegisterInput(instr, 1)) {
        __ And(r0, i.InputRegister(0), i.InputRegister(1));
      } else {
        // detect tmlh/tmhl/tmhh case
        Operand opnd = i.InputImmediate(1);
        if (is_uint16(opnd.immediate())) {
          __ tmll(i.InputRegister(0), opnd);
        } else {
          __ lr(r0, i.InputRegister(0));
          __ nilf(r0, opnd);
        }
      }
      break;
    case kS390_Tst64:
      if (HasRegisterInput(instr, 1)) {
        __ AndP(r0, i.InputRegister(0), i.InputRegister(1));
      } else {
        Operand opnd = i.InputImmediate(1);
        if (is_uint16(opnd.immediate())) {
          __ tmll(i.InputRegister(0), opnd);
        } else {
          __ AndP(r0, i.InputRegister(0), opnd);
        }
      }
      break;
    case kS390_Float64SilenceNaN: {
      DoubleRegister value = i.InputDoubleRegister(0);
      DoubleRegister result = i.OutputDoubleRegister();
      __ CanonicalizeNaN(result, value);
      break;
    }
    case kS390_Push: {
      int stack_decrement = i.InputInt32(0);
      int slots = stack_decrement / kSystemPointerSize;
      LocationOperand* op = LocationOperand::cast(instr->InputAt(1));
      MachineRepresentation rep = op->representation();
      int pushed_slots = ElementSizeInPointers(rep);
      // Slot-sized arguments are never padded but there may be a gap if
      // the slot allocator reclaimed other padding slots. Adjust the stack
      // here to skip any gap.
      __ AllocateStackSpace((slots - pushed_slots) * kSystemPointerSize);
      switch (rep) {
        case MachineRepresentation::kFloat32:
          __ lay(sp, MemOperand(sp, -kSystemPointerSize));
          __ StoreF32(i.InputDoubleRegister(1), MemOperand(sp));
          break;
        case MachineRepresentation::kFloat64:
          __ lay(sp, MemOperand(sp, -kDoubleSize));
          __ StoreF64(i.InputDoubleRegister(1), MemOperand(sp));
          break;
        case MachineRepresentation::kSimd128:
          __ lay(sp, MemOperand(sp, -kSimd128Size));
          __ StoreV128(i.InputDoubleRegister(1), MemOperand(sp), kScratchReg);
          break;
        default:
          __ Push(i.InputRegister(1));
          break;
      }
      frame_access_state()->IncreaseSPDelta(slots);
      break;
    }
    case kS390_PushFrame: {
      int num_slots = i.InputInt32(1);
      __ lay(sp, MemOperand(sp, -num_slots * kSystemPointerSize));
      if (instr->InputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->InputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ StoreF64(i.InputDoubleRegister(0), MemOperand(sp));
        } else {
          DCHECK_EQ(MachineRepresentation::kFloat32, op->representation());
          __ StoreF32(i.InputDoubleRegister(0), MemOperand(sp));
        }
      } else {
        __ StoreU64(i.InputRegister(0), MemOperand(sp));
      }
      break;
    }
    case kS390_StoreToStackSlot: {
      int slot = i.InputInt32(1);
      if (instr->InputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->InputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ StoreF64(i.InputDoubleRegister(0),
                      MemOperand(sp, slot * kSystemPointerSize));
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ StoreF32(i.InputDoubleRegister(0),
                      MemOperand(sp, slot * kSystemPointerSize));
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          __ StoreV128(i.InputDoubleRegister(0),
                       MemOperand(sp, slot * kSystemPointerSize), kScratchReg);
        }
      } else {
        __ StoreU64(i.InputRegister(0),
                    MemOperand(sp, slot * kSystemPointerSize));
      }
      break;
    }
    case kS390_SignExtendWord8ToInt32:
      __ lbr(i.OutputRegister(), i.InputRegister(0));
      CHECK_AND_ZERO_EXT_OUTPUT(1);
      break;
    case kS390_SignExtendWord16ToInt32:
      __ lhr(i.OutputRegister(), i.InputRegister(0));
      CHECK_AND_ZERO_EXT_OUTPUT(1);
      break;
    case kS390_SignExtendWord8ToInt64:
      __ lgbr(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_SignExtendWord16ToInt64:
      __ lghr(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_SignExtendWord32ToInt64:
      __ lgfr(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_Uint32ToUint64:
      // Zero extend
      __ llgfr(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_Int64ToInt32:
      // sign extend
      __ lgfr(i.OutputRegister(), i.InputRegister(0));
      break;
    // Convert Fixed to Floating Point
    case kS390_Int64ToFloat32:
      __ ConvertInt64ToFloat(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kS390_Int64ToDouble:
      __ ConvertInt64ToDouble(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kS390_Uint64ToFloat32:
      __ ConvertUnsignedInt64ToFloat(i.OutputDoubleRegister(),
                                     i.InputRegister(0));
      break;
    case kS390_Uint64ToDouble:
      __ ConvertUnsignedInt64ToDouble(i.OutputDoubleRegister(),
                                      i.InputRegister(0));
      break;
    case kS390_Int32ToFloat32:
      __ ConvertIntToFloat(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kS390_Int32ToDouble:
      __ ConvertIntToDouble(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kS390_Uint32ToFloat32:
      __ ConvertUnsignedIntToFloat(i.OutputDoubleRegister(),
                                   i.InputRegister(0));
      break;
    case kS390_Uint32ToDouble:
      __ ConvertUnsignedIntToDouble(i.OutputDoubleRegister(),
                                    i.InputRegister(0));
      break;
    case kS390_DoubleToInt32: {
      Label done;
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand(1));
      }
      __ ConvertDoubleToInt32(i.OutputRegister(0), i.InputDoubleRegister(0),
                              kRoundToNearest);
      __ b(Condition(0xE), &done, Label::kNear);  // normal case
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand::Zero());
      } else {
        __ mov(i.OutputRegister(0), Operand::Zero());
      }
      __ bind(&done);
      break;
    }
    case kS390_DoubleToUint32: {
      Label done;
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand(1));
      }
      __ ConvertDoubleToUnsignedInt32(i.OutputRegister(0),
                                      i.InputDoubleRegister(0));
      __ b(Condition(0xE), &done, Label::kNear);  // normal case
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand::Zero());
      } else {
        __ mov(i.OutputRegister(0), Operand::Zero());
      }
      __ bind(&done);
      break;
    }
    case kS390_DoubleToInt64: {
      Label done;
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand(1));
      }
      __ ConvertDoubleToInt64(i.OutputRegister(0), i.InputDoubleRegister(0));
      __ b(Condition(0xE), &done, Label::kNear);  // normal case
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand::Zero());
      } else {
        __ mov(i.OutputRegister(0), Operand::Zero());
      }
      __ bind(&done);
      break;
    }
    case kS390_DoubleToUint64: {
      Label done;
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand(1));
      }
      __ ConvertDoubleToUnsignedInt64(i.OutputRegister(0),
                                      i.InputDoubleRegister(0));
      __ b(Condition(0xE), &done, Label::kNear);  // normal case
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand::Zero());
      } else {
        __ mov(i.OutputRegister(0), Operand::Zero());
      }
      __ bind(&done);
      break;
    }
    case kS390_Float32ToInt32: {
      Label done;
      __ ConvertFloat32ToInt32(i.OutputRegister(0), i.InputDoubleRegister(0),
                               kRoundToZero);
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      if (set_overflow_to_min_i32) {
        // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
        // because INT32_MIN allows easier out-of-bounds detection.
        __ b(Condition(0xE), &done, Label::kNear);  // normal case
        __ llilh(i.OutputRegister(0), Operand(0x8000));
      }
      __ bind(&done);
      break;
    }
    case kS390_Float32ToUint32: {
      Label done;
      __ ConvertFloat32ToUnsignedInt32(i.OutputRegister(0),
                                       i.InputDoubleRegister(0));
      bool set_overflow_to_min_u32 = MiscField::decode(instr->opcode());
      if (set_overflow_to_min_u32) {
        // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
        // because 0 allows easier out-of-bounds detection.
        __ b(Condition(0xE), &done, Label::kNear);  // normal case
        __ mov(i.OutputRegister(0), Operand::Zero());
      }
      __ bind(&done);
      break;
    }
    case kS390_Float32ToUint64: {
      Label done;
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand(1));
      }
      __ ConvertFloat32ToUnsignedInt64(i.OutputRegister(0),
                                       i.InputDoubleRegister(0));
      __ b(Condition(0xE), &done, Label::kNear);  // normal case
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand::Zero());
      } else {
        __ mov(i.OutputRegister(0), Operand::Zero());
      }
      __ bind(&done);
      break;
    }
    case kS390_Float32ToInt64: {
      Label done;
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand(1));
      }
      __ ConvertFloat32ToInt64(i.OutputRegister(0), i.InputDoubleRegister(0));
      __ b(Condition(0xE), &done, Label::kNear);  // normal case
      if (i.OutputCount() > 1) {
        __ mov(i.OutputRegister(1), Operand::Zero());
      } else {
        __ mov(i.OutputRegister(0), Operand::Zero());
      }
      __ bind(&done);
      break;
    }
    case kS390_DoubleToFloat32:
      ASSEMBLE_UNARY_OP(D_DInstr(ledbr), nullInstr, nullInstr);
      break;
    case kS390_Float32ToDouble:
      ASSEMBLE_UNARY_OP(D_DInstr(ldebr), D_MInstr(LoadF32AsF64), nullInstr);
      break;
    case kS390_DoubleExtractLowWord32:
      __ lgdr(i.OutputRegister(), i.InputDoubleRegister(0));
      __ llgfr(i.OutputRegister(), i.OutputRegister());
      break;
    case kS390_DoubleExtractHighWord32:
      __ lgdr(i.OutputRegister(), i.InputDoubleRegister(0));
      __ srlg(i.OutputRegister(), i.OutputRegister(), Operand(32));
      break;
    case kS390_DoubleFromWord32Pair:
      __ LoadU32(kScratchReg, i.InputRegister(1));
      __ ShiftLeftU64(i.TempRegister(0), i.InputRegister(0), Operand(32));
      __ OrP(i.TempRegister(0), i.TempRegister(0), kScratchReg);
      __ MovInt64ToDouble(i.OutputDoubleRegister(), i.TempRegister(0));
      break;
    case kS390_DoubleInsertLowWord32:
      __ lgdr(kScratchReg, i.InputDoubleRegister(0));
      __ lr(kScratchReg, i.InputRegister(1));
      __ ldgr(i.OutputDoubleRegister(), kScratchReg);
      break;
    case kS390_DoubleInsertHighWord32:
      __ sllg(kScratchReg, i.InputRegister(1), Operand(32));
      __ lgdr(r0, i.InputDoubleRegister(0));
      __ lr(kScratchReg, r0);
      __ ldgr(i.OutputDoubleRegister(), kScratchReg);
      break;
    case kS390_DoubleConstruct:
      __ sllg(kScratchReg, i.InputRegister(0), Operand(32));
      __ lr(kScratchReg, i.InputRegister(1));

      // Bitwise convert from GPR to FPR
      __ ldgr(i.OutputDoubleRegister(), kScratchReg);
      break;
    case kS390_LoadWordS8:
      ASSEMBLE_LOAD_INTEGER(LoadS8);
      break;
    case kS390_BitcastFloat32ToInt32:
      ASSEMBLE_UNARY_OP(R_DInstr(MovFloatToInt), R_MInstr(LoadU32), nullInstr);
      break;
    case kS390_BitcastInt32ToFloat32:
      __ MovIntToFloat(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kS390_BitcastDoubleToInt64:
      __ MovDoubleToInt64(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kS390_BitcastInt64ToDouble:
      __ MovInt64ToDouble(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
    case kS390_LoadWordU8:
      ASSEMBLE_LOAD_INTEGER(LoadU8);
      break;
    case kS390_LoadWordU16:
      ASSEMBLE_LOAD_INTEGER(LoadU16);
      break;
    case kS390_LoadWordS16:
      ASSEMBLE_LOAD_INTEGER(LoadS16);
      break;
    case kS390_LoadWordU32:
      ASSEMBLE_LOAD_INTEGER(LoadU32);
      break;
    case kS390_LoadWordS32:
      ASSEMBLE_LOAD_INTEGER(LoadS32);
      break;
    case kS390_LoadReverse16:
      ASSEMBLE_LOAD_INTEGER(lrvh);
      break;
    case kS390_LoadReverse32:
      ASSEMBLE_LOAD_INTEGER(lrv);
      break;
    case kS390_LoadReverse64:
      ASSEMBLE_LOAD_INTEGER(lrvg);
      break;
    case kS390_LoadReverse16RR:
      __ lrvr(i.OutputRegister(), i.InputRegister(0));
      __ rll(i.OutputRegister(), i.OutputRegister(), Operand(16));
      break;
    case kS390_LoadReverse32RR:
      __ lrvr(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_LoadReverse64RR:
      __ lrvgr(i.OutputRegister(), i.InputRegister(0));
      break;
    case kS390_LoadReverseSimd128RR:
      __ vlgv(r0, i.InputSimd128Register(0), MemOperand(r0, 0), Condition(3));
      __ vlgv(r1, i.InputSimd128Register(0), MemOperand(r0, 1), Condition(3));
      __ lrvgr(r0, r0);
      __ lrvgr(r1, r1);
      __ vlvg(i.OutputSimd128Register(), r0, MemOperand(r0, 1), Condition(3));
      __ vlvg(i.OutputSimd128Register(), r1, MemOperand(r0, 0), Condition(3));
      break;
    case kS390_LoadReverseSimd128: {
      AddressingMode mode = kMode_None;
      MemOperand operand = i.MemoryOperand(&mode);
      Simd128Register dst = i.OutputSimd128Register();
      if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2) &&
          is_uint12(operand.offset())) {
        __ vlbr(dst, operand, Condition(4));
      } else {
        __ lrvg(r0, operand);
        __ lrvg(r1, MemOperand(operand.rx(), operand.rb(),
                               operand.offset() + kSystemPointerSize));
        __ vlvgp(dst, r1, r0);
      }
      break;
    }
    case kS390_LoadWord64:
      ASSEMBLE_LOAD_INTEGER(lg);
      break;
    case kS390_LoadAndTestWord32: {
      ASSEMBLE_LOADANDTEST32(ltr, lt_z);
      break;
    }
    case kS390_LoadAndTestWord64: {
      ASSEMBLE_LOADANDTEST64(ltgr, ltg);
      break;
    }
    case kS390_LoadFloat32:
      ASSEMBLE_LOAD_FLOAT(LoadF32);
      break;
    case kS390_LoadDouble:
      ASSEMBLE_LOAD_FLOAT(LoadF64);
      break;
    case kS390_LoadSimd128: {
      AddressingMode mode = kMode_None;
      MemOperand operand = i.MemoryOperand(&mode);
      __ vl(i.OutputSimd128Register(), operand, Condition(0));
      break;
    }
    case kS390_StoreWord8:
      ASSEMBLE_STORE_INTEGER(StoreU8);
      break;
    case kS390_StoreWord16:
      ASSEMBLE_STORE_INTEGER(StoreU16);
      break;
    case kS390_StoreWord32:
      ASSEMBLE_STORE_INTEGER(StoreU32);
      break;
    case kS390_StoreWord64:
      ASSEMBLE_STORE_INTEGER(StoreU64);
      break;
    case kS390_StoreReverse16:
      ASSEMBLE_STORE_INTEGER(strvh);
      break;
    case kS390_StoreReverse32:
      ASSEMBLE_STORE_INTEGER(strv);
      break;
    case kS390_StoreReverse64:
      ASSEMBLE_STORE_INTEGER(strvg);
      break;
    case kS390_StoreReverseSimd128: {
      size_t index = 0;
      AddressingMode mode = kMode_None;
      MemOperand operand = i.MemoryOperand(&mode, &index);
      if (CpuFeatures::IsSupported(VECTOR_ENHANCE_FACILITY_2) &&
          is_uint12(operand.offset())) {
        __ vstbr(i.InputSimd128Register(index), operand, Condition(4));
      } else {
        __ vlgv(r0, i.InputSimd128Register(index), MemOperand(r0, 1),
                Condition(3));
        __ vlgv(r1, i.InputSimd128Register(index), MemOperand(r0, 0),
                Condition(3));
        __ strvg(r0, operand);
        __ strvg(r1, MemOperand(operand.rx(), operand.rb(),
                                operand.offset() + kSystemPointerSize));
      }
      break;
    }
    case kS390_StoreFloat32:
      ASSEMBLE_STORE_FLOAT32();
      break;
    case kS390_StoreDouble:
      ASSEMBLE_STORE_DOUBLE();
      break;
    case kS390_StoreSimd128: {
      size_t index = 0;
      AddressingMode mode = kMode_None;
      MemOperand operand = i.MemoryOperand(&mode, &index);
      __ vst(i.InputSimd128Register(index), operand, Condition(0));
      break;
    }
    case kS390_Lay: {
      MemOperand mem = i.MemoryOperand();
      if (!is_int20(mem.offset())) {
        // Add directly to the base register in case the index register (rx) is
        // r0.
        DCHECK(is_int32(mem.offset()));
        __ AddS64(ip, mem.rb(), Operand(mem.offset()));
        mem = MemOperand(mem.rx(), ip);
      }
      __ lay(i.OutputRegister(), mem);
      break;
    }
    case kAtomicExchangeInt8:
    case kAtomicExchangeUint8: {
      Register base = i.InputRegister(0);
      Register index = i.InputRegister(1);
      Register value = i.InputRegister(2);
      Register output = i.OutputRegister();
      __ la(r1, MemOperand(base, index));
      __ AtomicExchangeU8(r1, value, output, r0);
      if (opcode == kAtomicExchangeInt8) {
        __ LoadS8(output, output);
      } else {
        __ LoadU8(output, output);
      }
      break;
    }
    case kAtomicExchangeInt16:
    case kAtomicExchangeUint16: {
      Register base = i.InputRegister(0);
      Register index = i.InputRegister(1);
      Register value = i.InputRegister(2);
      Register output = i.OutputRegister();
      bool reverse_bytes = is_wasm_on_be(info());
      __ la(r1, MemOperand(base, index));
      Register value_ = value;
      if (reverse_bytes) {
        value_ = ip;
        __ lrvr(value_, value);
        __ ShiftRightU32(value_, value_, Operand(16));
      }
      __ AtomicExchangeU16(r1, value_, output, r0);
      if (reverse_bytes) {
        __ lrvr(output, output);
        __ ShiftRightU32(output, output, Operand(16));
      }
      if (opcode == kAtomicExchangeInt16) {
        __ lghr(output, output);
      } else {
        __ llghr(output, output);
      }
      break;
    }
    case kAtomicExchangeWord32: {
      Register base = i.InputRegister(0);
      Register index = i.InputRegister(1);
      Register value = i.InputRegister(2);
      Register output = i.OutputRegister();
      Label do_cs;
      bool reverse_bytes = is_wasm_on_be(info());
      __ lay(r1, MemOperand(base, index));
      Register value_ = value;
      if (reverse_bytes) {
        value_ = ip;
        __ lrvr(value_, value);
      }
      __ LoadU32(output, MemOperand(r1));
      __ bind(&do_cs);
      __ cs(output, value_, MemOperand(r1));
      __ bne(&do_cs, Label::kNear);
      if (reverse_bytes) {
        __ lrvr(output, output);
        __ LoadU32(output, output);
      }
      break;
    }
    case kAtomicCompareExchangeInt8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_BYTE(LoadS8);
      break;
    case kAtomicCompareExchangeUint8:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_BYTE(LoadU8);
      break;
    case kAtomicCompareExchangeInt16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_HALFWORD(LoadS16);
      break;
    case kAtomicCompareExchangeUint16:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_HALFWORD(LoadU16);
      break;
    case kAtomicCompareExchangeWord32:
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE_WORD();
      break;
#define ATOMIC_BINOP_CASE(op, inst)                                          \
  case kAtomic##op##Int8:                                                    \
    ASSEMBLE_ATOMIC_BINOP_BYTE(inst, [&]() {                                 \
      intptr_t shift_right = static_cast<intptr_t>(shift_amount);            \
      __ srlk(result, prev, Operand(shift_right));                           \
      __ LoadS8(result, result);                                             \
    });                                                                      \
    break;                                                                   \
  case kAtomic##op##Uint8:                                                   \
    ASSEMBLE_ATOMIC_BINOP_BYTE(inst, [&]() {                                 \
      int rotate_left = shift_amount == 0 ? 0 : 64 - shift_amount;           \
      __ RotateInsertSelectBits(result, prev, Operand(56), Operand(63),      \
                                Operand(static_cast<intptr_t>(rotate_left)), \
                                true);                                       \
    });                                                                      \
    break;                                                                   \
  case kAtomic##op##Int16:                                                   \
    ASSEMBLE_ATOMIC_BINOP_HALFWORD(inst, [&]() {                             \
      intptr_t shift_right = static_cast<intptr_t>(shift_amount);            \
      __ srlk(result, prev, Operand(shift_right));                           \
      if (is_wasm_on_be(info())) {                                           \
        __ lrvr(result, result);                                             \
        __ ShiftRightS32(result, result, Operand(16));                       \
      }                                                                      \
      __ LoadS16(result, result);                                            \
    });                                                                      \
    break;                                                                   \
  case kAtomic##op##Uint16:                                                  \
    ASSEMBLE_ATOMIC_BINOP_HALFWORD(inst, [&]() {                             \
      int rotate_left = shift_amount == 0 ? 0 : 64 - shift_amount;           \
      __ RotateInsertSelectBits(result, prev, Operand(48), Operand(63),      \
                                Operand(static_cast<intptr_t>(rotate_left)), \
                                true);                                       \
      if (is_wasm_on_be(info())) {                                           \
        __ lrvr(result, result);                                             \
        __ ShiftRightU32(result, result, Operand(16));                       \
      }                                                                      \
    });                                                                      \
    break;
      ATOMIC_BINOP_CASE(Add, AddS32)
      ATOMIC_BINOP_CASE(Sub, SubS32)
      ATOMIC_BINOP_CASE(And, And)
      ATOMIC_BINOP_CASE(Or, Or)
      ATOMIC_BINOP_CASE(Xor, Xor)
#undef ATOMIC_BINOP_CASE
    case kAtomicAddWord32:
      ASSEMBLE_ATOMIC_BINOP_WORD(laa, AddS32);
      break;
    case kAtomicSubWord32:
      ASSEMBLE_ATOMIC_BINOP_WORD(LoadAndSub32, SubS32);
      break;
    case kAtomicAndWord32:
      ASSEMBLE_ATOMIC_BINOP_WORD(lan, AndP);
      break;
    case kAtomicOrWord32:
      ASSEMBLE_ATOMIC_BINOP_WORD(lao, OrP);
      break;
    case kAtomicXorWord32:
      ASSEMBLE_ATOMIC_BINOP_WORD(lax, XorP);
      break;
    case kS390_Word64AtomicAddUint64:
      ASSEMBLE_ATOMIC_BINOP_WORD64(laag, AddS64);
      break;
    case kS390_Word64AtomicSubUint64:
      ASSEMBLE_ATOMIC_BINOP_WORD64(LoadAndSub64, SubS64);
      break;
    case kS390_Word64AtomicAndUint64:
      ASSEMBLE_ATOMIC_BINOP_WORD64(lang, AndP);
      break;
    case kS390_Word64AtomicOrUint64:
      ASSEMBLE_ATOMIC_BINOP_WORD64(laog, OrP);
      break;
    case kS390_Word64AtomicXorUint64:
      ASSEMBLE_ATOMIC_BINOP_WORD64(laxg, XorP);
      break;
    case kS390_Word64AtomicExchangeUint64: {
      Register base = i.InputRegister(0);
      Register index = i.InputRegister(1);
      Register value = i.InputRegister(2);
      Register output = i.OutputRegister();
      bool reverse_bytes = is_wasm_on_be(info());
      Label do_cs;
      Register value_ = value;
      __ la(r1, MemOperand(base, index));
      if (reverse_bytes) {
        value_ = ip;
        __ lrvgr(value_, value);
      }
      __ lg(output, MemOperand(r1));
      __ bind(&do_cs);
      __ csg(output, value_, MemOperand(r1));
      __ bne(&do_cs, Label::kNear);
      if (reverse_bytes) {
        __ lrvgr(output, output);
      }
      break;
    }
    case kS390_Word64AtomicCompareExchangeUint64:
      ASSEMBLE_ATOMIC64_COMP_EXCHANGE_WORD64();
      break;
      // Simd Support.
#define SIMD_SHIFT_LIST(V) \
  V(I64x2Shl)              \
  V(I64x2ShrS)             \
  V(I64x2ShrU)             \
  V(I32x4Shl)              \
  V(I32x4ShrS)             \
  V(I32x4ShrU)             \
  V(I16x8Shl)              \
  V(I16x8ShrS)             \
  V(I16x8ShrU)             \
  V(I8x16Shl)              \
  V(I8x16ShrS)             \
  V(I8x16ShrU)

#define EMIT_SIMD_SHIFT(name)                                     \
  case kS390_##name: {                                            \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.InputRegister(1), kScratchDoubleReg);               \
    break;                                                        \
  }
      SIMD_SHIFT_LIST(EMIT_SIMD_SHIFT)
#undef EMIT_SIMD_SHIFT
#undef SIMD_SHIFT_LIST

#define SIMD_BINOP_LIST(V) \
  V(F64x2Add)              \
  V(F64x2Sub)              \
  V(F64x2Mul)              \
  V(F64x2Div)              \
  V(F64x2Min)              \
  V(F64x2Max)              \
  V(F64x2Eq)               \
  V(F64x2Ne)               \
  V(F64x2Lt)               \
  V(F64x2Le)               \
  V(F64x2Pmin)             \
  V(F64x2Pmax)             \
  V(F32x4Add)              \
  V(F32x4Sub)              \
  V(F32x4Mul)              \
  V(F32x4Div)              \
  V(F32x4Min)              \
  V(F32x4Max)              \
  V(F32x4Eq)               \
  V(F32x4Ne)               \
  V(F32x4Lt)               \
  V(F32x4Le)               \
  V(F32x4Pmin)             \
  V(F32x4Pmax)             \
  V(I64x2Add)              \
  V(I64x2Sub)              \
  V(I64x2Eq)               \
  V(I64x2Ne)               \
  V(I64x2GtS)              \
  V(I64x2GeS)              \
  V(I32x4Add)              \
  V(I32x4Sub)              \
  V(I32x4Mul)              \
  V(I32x4Eq)               \
  V(I32x4Ne)               \
  V(I32x4GtS)              \
  V(I32x4GeS)              \
  V(I32x4GtU)              \
  V(I32x4MinS)             \
  V(I32x4MinU)             \
  V(I32x4MaxS)             \
  V(I32x4MaxU)             \
  V(I16x8Add)              \
  V(I16x8Sub)              \
  V(I16x8Mul)              \
  V(I16x8Eq)               \
  V(I16x8Ne)               \
  V(I16x8GtS)              \
  V(I16x8GeS)              \
  V(I16x8GtU)              \
  V(I16x8MinS)             \
  V(I16x8MinU)             \
  V(I16x8MaxS)             \
  V(I16x8MaxU)             \
  V(I16x8RoundingAverageU) \
  V(I8x16Add)              \
  V(I8x16Sub)              \
  V(I8x16Eq)               \
  V(I8x16Ne)               \
  V(I8x16GtS)              \
  V(I8x16GeS)              \
  V(I8x16GtU)              \
  V(I8x16MinS)             \
  V(I8x16MinU)             \
  V(I8x16MaxS)             \
  V(I8x16MaxU)             \
  V(I8x16RoundingAverageU) \
  V(S128And)               \
  V(S128Or)                \
  V(S128Xor)               \
  V(S128AndNot)

#define EMIT_SIMD_BINOP(name)                                     \
  case kS390_##name: {                                            \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.InputSimd128Register(1));                           \
    break;                                                        \
  }
      SIMD_BINOP_LIST(EMIT_SIMD_BINOP)
#undef EMIT_SIMD_BINOP
#undef SIMD_BINOP_LIST

#define SIMD_UNOP_LIST(V)                                     \
  V(F64x2Splat, Simd128Register, DoubleRegister)              \
  V(F64x2Abs, Simd128Register, Simd128Register)               \
  V(F64x2Neg, Simd128Register, Simd128Register)               \
  V(F64x2Sqrt, Simd128Register, Simd128Register)              \
  V(F64x2Ceil, Simd128Register, Simd128Register)              \
  V(F64x2Floor, Simd128Register, Simd128Register)             \
  V(F64x2Trunc, Simd128Register, Simd128Register)             \
  V(F64x2NearestInt, Simd128Register, Simd128Register)        \
  V(F32x4Splat, Simd128Register, DoubleRegister)              \
  V(F32x4Abs, Simd128Register, Simd128Register)               \
  V(F32x4Neg, Simd128Register, Simd128Register)               \
  V(F32x4Sqrt, Simd128Register, Simd128Register)              \
  V(F32x4Ceil, Simd128Register, Simd128Register)              \
  V(F32x4Floor, Simd128Register, Simd128Register)             \
  V(F32x4Trunc, Simd128Register, Simd128Register)             \
  V(F32x4NearestInt, Simd128Register, Simd128Register)        \
  V(I64x2Splat, Simd128Register, Register)                    \
  V(I64x2Abs, Simd128Register, Simd128Register)               \
  V(I64x2Neg, Simd128Register, Simd128Register)               \
  V(I64x2SConvertI32x4Low, Simd128Register, Simd128Register)  \
  V(I64x2SConvertI32x4High, Simd128Register, Simd128Register) \
  V(I64x2UConvertI32x4Low, Simd128Register, Simd128Register)  \
  V(I64x2UConvertI32x4High, Simd128Register, Simd128Register) \
  V(I32x4Splat, Simd128Register, Register)                    \
  V(I32x4Abs, Simd128Register, Simd128Register)               \
  V(I32x4Neg, Simd128Register, Simd128Register)               \
  V(I32x4SConvertI16x8Low, Simd128Register, Simd128Register)  \
  V(I32x4SConvertI16x8High, Simd128Register, Simd128Register) \
  V(I32x4UConvertI16x8Low, Simd128Register, Simd128Register)  \
  V(I32x4UConvertI16x8High, Simd128Register, Simd128Register) \
  V(I16x8Splat, Simd128Register, Register)                    \
  V(I16x8Abs, Simd128Register, Simd128Register)               \
  V(I16x8Neg, Simd128Register, Simd128Register)               \
  V(I16x8SConvertI8x16Low, Simd128Register, Simd128Register)  \
  V(I16x8SConvertI8x16High, Simd128Register, Simd128Register) \
  V(I16x8UConvertI8x16Low, Simd128Register, Simd128Register)  \
  V(I16x8UConvertI8x16High, Simd128Register, Simd128Register) \
  V(I8x16Splat, Simd128Register, Register)                    \
  V(I8x16Abs, Simd128Register, Simd128Register)               \
  V(I8x16Neg, Simd128Register, Simd128Register)               \
  V(S128Not, Simd128Register, Simd128Register)

#define EMIT_SIMD_UNOP(name, dtype, stype)         \
  case kS390_##name: {                             \
    __ name(i.Output##dtype(), i.Input##stype(0)); \
    break;                                         \
  }
      SIMD_UNOP_LIST(EMIT_SIMD_UNOP)
#undef EMIT_SIMD_UNOP
#undef SIMD_UNOP_LIST

#define SIMD_EXTRACT_LANE_LIST(V)     \
  V(F64x2ExtractLane, DoubleRegister) \
  V(F32x4ExtractLane, DoubleRegister) \
  V(I64x2ExtractLane, Register)       \
  V(I32x4ExtractLane, Register)       \
  V(I16x8ExtractLaneU, Register)      \
  V(I16x8ExtractLaneS, Register)      \
  V(I8x16ExtractLaneU, Register)      \
  V(I8x16ExtractLaneS, Register)

#define EMIT_SIMD_EXTRACT_LANE(name, dtype)                               \
  case kS390_##name: {                                                    \
    __ name(i.Output##dtype(), i.InputSimd128Register(0), i.InputInt8(1), \
            kScratchReg);                                                 \
    break;                                                                \
  }
      SIMD_EXTRACT_LANE_LIST(EMIT_SIMD_EXTRACT_LANE)
#undef EMIT_SIMD_EXTRACT_LANE
#undef SIMD_EXTRACT_LANE_LIST

#define SIMD_REPLACE_LANE_LIST(V)     \
  V(F64x2ReplaceLane, DoubleRegister) \
  V(F32x4ReplaceLane, DoubleRegister) \
  V(I64x2ReplaceLane, Register)       \
  V(I32x4ReplaceLane, Register)       \
  V(I16x8ReplaceLane, Register)       \
  V(I8x16ReplaceLane, Register)

#define EMIT_SIMD_REPLACE_LANE(name, stype)                       \
  case kS390_##name: {                                            \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.Input##stype(2), i.InputInt8(1), kScratchReg);      \
    break;                                                        \
  }
      SIMD_REPLACE_LANE_LIST(EMIT_SIMD_REPLACE_LANE)
#undef EMIT_SIMD_REPLACE_LANE
#undef SIMD_REPLACE_LANE_LIST

#define SIMD_EXT_MUL_LIST(V) \
  V(I64x2ExtMulLowI32x4S)    \
  V(I64x2ExtMulHighI32x4S)   \
  V(I64x2ExtMulLowI32x4U)    \
  V(I64x2ExtMulHighI32x4U)   \
  V(I32x4ExtMulLowI16x8S)    \
  V(I32x4ExtMulHighI16x8S)   \
  V(I32x4ExtMulLowI16x8U)    \
  V(I32x4ExtMulHighI16x8U)   \
  V(I16x8ExtMulLowI8x16S)    \
  V(I16x8ExtMulHighI8x16S)   \
  V(I16x8ExtMulLowI8x16U)    \
  V(I16x8ExtMulHighI8x16U)

#define EMIT_SIMD_EXT_MUL(name)                                   \
  case kS390_##name: {                                            \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.InputSimd128Register(1), kScratchDoubleReg);        \
    break;                                                        \
  }
      SIMD_EXT_MUL_LIST(EMIT_SIMD_EXT_MUL)
#undef EMIT_SIMD_EXT_MUL
#undef SIMD_EXT_MUL_LIST

#define SIMD_ALL_TRUE_LIST(V) \
  V(I64x2AllTrue)             \
  V(I32x4AllTrue)             \
  V(I16x8AllTrue)             \
  V(I8x16AllTrue)

#define EMIT_SIMD_ALL_TRUE(name)                                        \
  case kS390_##name: {                                                  \
    __ name(i.OutputRegister(), i.InputSimd128Register(0), kScratchReg, \
            kScratchDoubleReg);                                         \
    break;                                                              \
  }
      SIMD_ALL_TRUE_LIST(EMIT_SIMD_ALL_TRUE)
#undef EMIT_SIMD_ALL_TRUE
#undef SIMD_ALL_TRUE_LIST

#define SIMD_QFM_LIST(V) \
  V(F64x2Qfma)           \
  V(F64x2Qfms)           \
  V(F32x4Qfma)           \
  V(F32x4Qfms)

#define EMIT_SIMD_QFM(name)                                        \
  case kS390_##name: {                                             \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0),  \
            i.InputSimd128Register(1), i.InputSimd128Register(2)); \
    break;                                                         \
  }
      SIMD_QFM_LIST(EMIT_SIMD_QFM)
#undef EMIT_SIMD_QFM
#undef SIMD_QFM_LIST

#define SIMD_ADD_SUB_SAT_LIST(V) \
  V(I16x8AddSatS)                \
  V(I16x8SubSatS)                \
  V(I16x8AddSatU)                \
  V(I16x8SubSatU)                \
  V(I8x16AddSatS)                \
  V(I8x16SubSatS)                \
  V(I8x16AddSatU)                \
  V(I8x16SubSatU)

#define EMIT_SIMD_ADD_SUB_SAT(name)                               \
  case kS390_##name: {                                            \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.InputSimd128Register(1), kScratchDoubleReg,         \
            i.ToSimd128Register(instr->TempAt(0)));               \
    break;                                                        \
  }
      SIMD_ADD_SUB_SAT_LIST(EMIT_SIMD_ADD_SUB_SAT)
#undef EMIT_SIMD_ADD_SUB_SAT
#undef SIMD_ADD_SUB_SAT_LIST

#define SIMD_EXT_ADD_PAIRWISE_LIST(V) \
  V(I32x4ExtAddPairwiseI16x8S)        \
  V(I32x4ExtAddPairwiseI16x8U)        \
  V(I16x8ExtAddPairwiseI8x16S)        \
  V(I16x8ExtAddPairwiseI8x16U)

#define EMIT_SIMD_EXT_ADD_PAIRWISE(name)                               \
  case kS390_##name: {                                                 \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0),      \
            kScratchDoubleReg, i.ToSimd128Register(instr->TempAt(0))); \
    break;                                                             \
  }
      SIMD_EXT_ADD_PAIRWISE_LIST(EMIT_SIMD_EXT_ADD_PAIRWISE)
#undef EMIT_SIMD_EXT_ADD_PAIRWISE
#undef SIMD_EXT_ADD_PAIRWISE_LIST

    case kS390_I64x2Mul: {
      __ I64x2Mul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), r0, r1, ip);
      break;
    }
    case kS390_I32x4GeU: {
      __ I32x4GeU(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kS390_I16x8GeU: {
      __ I16x8GeU(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kS390_I8x16GeU: {
      __ I8x16GeU(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    // vector boolean unops
    case kS390_V128AnyTrue: {
      __ V128AnyTrue(i.OutputRegister(), i.InputSimd128Register(0),
                     kScratchReg);
      break;
    }
    // vector bitwise ops
    case kS390_S128Const: {
      uint64_t low = make_uint64(i.InputUint32(1), i.InputUint32(0));
      uint64_t high = make_uint64(i.InputUint32(3), i.InputUint32(2));
      __ S128Const(i.OutputSimd128Register(), high, low, r0, ip);
      break;
    }
    case kS390_S128Zero: {
      Simd128Register dst = i.OutputSimd128Register();
      __ S128Zero(dst, dst);
      break;
    }
    case kS390_S128AllOnes: {
      Simd128Register dst = i.OutputSimd128Register();
      __ S128AllOnes(dst, dst);
      break;
    }
    case kS390_S128Select: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register mask = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      Simd128Register src2 = i.InputSimd128Register(2);
      __ S128Select(dst, src1, src2, mask);
      break;
    }
    // vector conversions
    case kS390_I32x4SConvertF32x4: {
      __ I32x4SConvertF32x4(i.OutputSimd128Register(),
                            i.InputSimd128Register(0), kScratchDoubleReg,
                            kScratchReg);
      break;
    }
    case kS390_I32x4UConvertF32x4: {
      __ I32x4UConvertF32x4(i.OutputSimd128Register(),
                            i.InputSimd128Register(0), kScratchDoubleReg,
                            kScratchReg);
      break;
    }
    case kS390_F32x4SConvertI32x4: {
      __ F32x4SConvertI32x4(i.OutputSimd128Register(),
                            i.InputSimd128Register(0), kScratchDoubleReg,
                            kScratchReg);
      break;
    }
    case kS390_F32x4UConvertI32x4: {
      __ F32x4UConvertI32x4(i.OutputSimd128Register(),
                            i.InputSimd128Register(0), kScratchDoubleReg,
                            kScratchReg);
      break;
    }
    case kS390_I16x8SConvertI32x4: {
      __ I16x8SConvertI32x4(i.OutputSimd128Register(),
                            i.InputSimd128Register(0),
                            i.InputSimd128Register(1));
      break;
    }
    case kS390_I8x16SConvertI16x8: {
      __ I8x16SConvertI16x8(i.OutputSimd128Register(),
                            i.InputSimd128Register(0),
                            i.InputSimd128Register(1));
      break;
    }
    case kS390_I16x8UConvertI32x4: {
      __ I16x8UConvertI32x4(i.OutputSimd128Register(),
                            i.InputSimd128Register(0),
                            i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kS390_I8x16UConvertI16x8: {
      __ I8x16UConvertI16x8(i.OutputSimd128Register(),
                            i.InputSimd128Register(0),
                            i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kS390_I8x16Shuffle: {
      uint64_t low = make_uint64(i.InputUint32(3), i.InputUint32(2));
      uint64_t high = make_uint64(i.InputUint32(5), i.InputUint32(4));
      __ I8x16Shuffle(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      i.InputSimd128Register(1), high, low, r0, ip,
                      kScratchDoubleReg);
      break;
    }
    case kS390_I8x16Swizzle: {
      __ I8x16Swizzle(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      i.InputSimd128Register(1), r0, r1, kScratchDoubleReg);
      break;
    }
    case kS390_I64x2BitMask: {
      __ I64x2BitMask(i.OutputRegister(), i.InputSimd128Register(0),
                      kScratchReg, kScratchDoubleReg);
      break;
    }
    case kS390_I32x4BitMask: {
      __ I32x4BitMask(i.OutputRegister(), i.InputSimd128Register(0),
                      kScratchReg, kScratchDoubleReg);
      break;
    }
    case kS390_I16x8BitMask: {
      __ I16x8BitMask(i.OutputRegister(), i.InputSimd128Register(0),
                      kScratchReg, kScratchDoubleReg);
      break;
    }
    case kS390_I8x16BitMask: {
      __ I8x16BitMask(i.OutputRegister(), i.InputSimd128Register(0), r0, ip,
                      kScratchDoubleReg);
      break;
    }
    case kS390_I32x4DotI16x8S: {
      __ I32x4DotI16x8S(i.OutputSimd128Register(), i.InputSimd128Register(0),
                        i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }

    case kS390_I16x8DotI8x16S: {
      __ I16x8DotI8x16S(i.OutputSimd128Register(), i.InputSimd128Register(0),
                        i.InputSimd128Register(1), kScratchDoubleReg);
      break;
    }
    case kS390_I32x4DotI8x16AddS: {
      __ I32x4DotI8x16AddS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                           i.InputSimd128Register(1), i.InputSimd128Register(2),
                           kScratchDoubleReg, i.TempSimd128Register(0));
      break;
    }
    case kS390_I16x8Q15MulRSatS: {
      __ I16x8Q15MulRSatS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                          i.InputSimd128Register(1), kScratchDoubleReg,
                          i.ToSimd128Register(instr->TempAt(0)),
                          i.ToSimd128Register(instr->TempAt(1)));
      break;
    }
    case kS390_I8x16Popcnt: {
      __ I8x16Popcnt(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kS390_F64x2ConvertLowI32x4S: {
      __ F64x2ConvertLowI32x4S(i.OutputSimd128Register(),
                               i.InputSimd128Register(0));
      break;
    }
    case kS390_F64x2ConvertLowI32x4U: {
      __ F64x2ConvertLowI32x4U(i.OutputSimd128Register(),
                               i.InputSimd128Register(0));
      break;
    }
    case kS390_F64x2PromoteLowF32x4: {
      __ F64x2PromoteLowF32x4(i.OutputSimd128Register(),
                              i.InputSimd128Register(0), kScratchDoubleReg, r0,
                              r1, ip);
      break;
    }
    case kS390_F32x4DemoteF64x2Zero: {
      __ F32x4DemoteF64x2Zero(i.OutputSimd128Register(),
                              i.InputSimd128Register(0), kScratchDoubleReg, r0,
                              r1, ip);
      break;
    }
    case kS390_I32x4TruncSatF64x2SZero: {
      __ I32x4TruncSatF64x2SZero(i.OutputSimd128Register(),
                                 i.InputSimd128Register(0), kScratchDoubleReg);
      break;
    }
    case kS390_I32x4TruncSatF64x2UZero: {
      __ I32x4TruncSatF64x2UZero(i.OutputSimd128Register(),
                                 i.InputSimd128Register(0), kScratchDoubleReg);
      break;
    }
#define LOAD_SPLAT(type)                           \
  AddressingMode mode = kMode_None;                \
  MemOperand operand = i.MemoryOperand(&mode);     \
  Simd128Register dst = i.OutputSimd128Register(); \
  __ LoadAndSplat##type##LE(dst, operand, kScratchReg);
    case kS390_S128Load64Splat: {
      LOAD_SPLAT(64x2);
      break;
    }
    case kS390_S128Load32Splat: {
      LOAD_SPLAT(32x4);
      break;
    }
    case kS390_S128Load16Splat: {
      LOAD_SPLAT(16x8);
      break;
    }
    case kS390_S128Load8Splat: {
      LOAD_SPLAT(8x16);
      break;
    }
#undef LOAD_SPLAT
#define LOAD_EXTEND(type)                          \
  AddressingMode mode = kMode_None;                \
  MemOperand operand = i.MemoryOperand(&mode);     \
  Simd128Register dst = i.OutputSimd128Register(); \
  __ LoadAndExtend##type##LE(dst, operand, kScratchReg);
    case kS390_S128Load32x2U: {
      LOAD_EXTEND(32x2U);
      break;
    }
    case kS390_S128Load32x2S: {
      LOAD_EXTEND(32x2S);
      break;
    }
    case kS390_S128Load16x4U: {
      LOAD_EXTEND(16x4U);
      break;
    }
    case kS390_S128Load16x4S: {
      LOAD_EXTEND(16x4S);
      break;
    }
    case kS390_S128Load8x8U: {
      LOAD_EXTEND(8x8U);
      break;
    }
    case kS390_S128Load8x8S: {
      LOAD_EXTEND(8x8S);
      break;
    }
#undef LOAD_EXTEND
#define LOAD_AND_ZERO(type)                        \
  AddressingMode mode = kMode_None;                \
  MemOperand operand = i.MemoryOperand(&mode);     \
  Simd128Register dst = i.OutputSimd128Register(); \
  __ LoadV##type##ZeroLE(dst, operand, kScratchReg);
    case kS390_S128Load32Zero: {
      LOAD_AND_ZERO(32);
      break;
    }
    case kS390_S128Load64Zero: {
      LOAD_AND_ZERO(64);
      break;
    }
#undef LOAD_AND_ZERO
#undef LOAD_EXTEND
#define LOAD_LANE(type, lane)                          \
  AddressingMode mode = kMode_None;                    \
  size_t index = 2;                                    \
  MemOperand operand = i.MemoryOperand(&mode, &index); \
  Simd128Register dst = i.OutputSimd128Register();     \
  DCHECK_EQ(dst, i.InputSimd128Register(0));           \
  __ LoadLane##type##LE(dst, operand, lane, kScratchReg);
    case kS390_S128Load8Lane: {
      LOAD_LANE(8, 15 - i.InputUint8(1));
      break;
    }
    case kS390_S128Load16Lane: {
      LOAD_LANE(16, 7 - i.InputUint8(1));
      break;
    }
    case kS390_S128Load32Lane: {
      LOAD_LANE(32, 3 - i.InputUint8(1));
      break;
    }
    case kS390_S128Load64Lane: {
      LOAD_LANE(64, 1 - i.InputUint8(1));
      break;
    }
#undef LOAD_LANE
#define STORE_LANE(type, lane)                         \
  AddressingMode mode = kMode_None;                    \
  size_t index = 2;                                    \
  MemOperand operand = i.MemoryOperand(&mode, &index); \
  Simd128Register src = i.InputSimd128Register(0);     \
  __ StoreLane##type##LE(src, operand, lane, kScratchReg);
    case kS390_S128Store8Lane: {
      STORE_LANE(8, 15 - i.InputUint8(1));
      break;
    }
    case kS390_S128Store16Lane: {
      STORE_LANE(16, 7 - i.InputUint8(1));
      break;
    }
    case kS390_S128Store32Lane: {
      STORE_LANE(32, 3 - i.InputUint8(1));
      break;
    }
    case kS390_S128Store64Lane: {
      STORE_LANE(64, 1 - i.InputUint8(1));
      break;
    }
#undef STORE_LANE
    case kS390_StoreCompressTagged: {
      CHECK(!instr->HasOutput());
      size_t index = 0;
      AddressingMode mode = kMode_None;
      MemOperand operand = i.MemoryOperand(&mode, &index);
      Register value = i.InputRegister(index);
      __ StoreTaggedField(value, operand, r1);
      break;
    }
    case kS390_LoadDecompressTaggedSigned: {
      CHECK(instr->HasOutput());
      __ DecompressTaggedSigned(i.OutputRegister(), i.MemoryOperand());
      break;
    }
    case kS390_LoadDecompressTagged: {
      CHECK(instr->HasOutput());
      __ DecompressTagged(i.OutputRegister(), i.MemoryOperand());
      break;
    }
    default:
      UNREACHABLE();
  }
  return kSuccess;
}

// Assembles branches after an instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  S390OperandConverter i(this, instr);
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;
  ArchOpcode op = instr->arch_opcode();
  FlagsCondition condition = branch->condition;

  Condition cond = FlagsConditionToCondition(condition, op);
  if (op == kS390_CmpFloat || op == kS390_CmpDouble) {
    // check for unordered if necessary
    // Branching to flabel/tlabel according to what's expected by tests
    if (cond == le || cond == eq || cond == lt) {
      __ bunordered(flabel);
    } else if (cond == gt || cond == ne || cond == ge) {
      __ bunordered(tlabel);
    }
  }
  __ b(cond, tlabel);
  if (!branch->fallthru) __ b(flabel);  // no fallthru to flabel.
}

void CodeGenerator::AssembleArchDeoptBranch(Instruction* instr,
                                            BranchInfo* branch) {
  AssembleArchBranch(instr, branch);
}

void CodeGenerator::AssembleArchJumpRegardlessOfAssemblyOrder(
    RpoNumber target) {
  __ b(GetLabel(target));
}

#if V8_ENABLE_WEBASSEMBLY
void CodeGenerator::AssembleArchTrap(Instruction* instr,
                                     FlagsCondition condition) {
  class OutOfLineTrap final : public OutOfLineCode {
   public:
    OutOfLineTrap(CodeGenerator* gen, Instruction* instr)
        : OutOfLineCode(gen), instr_(instr), gen_(gen) {}

    void Generate() final {
      S390OperandConverter i(gen_, instr_);
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
  Label end;

  ArchOpcode op = instr->arch_opcode();
  Condition cond = FlagsConditionToCondition(condition, op);
  if (op == kS390_CmpFloat || op == kS390_CmpDouble) {
    // check for unordered if necessary
    if (cond == le || cond == eq || cond == lt) {
      __ bunordered(&end);
    } else if (cond == gt || cond == ne || cond == ge) {
      __ bunordered(tlabel);
    }
  }
  __ b(cond, tlabel);
  __ bind(&end);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  S390OperandConverter i(this, instr);
  ArchOpcode op = instr->arch_opcode();
  bool check_unordered = (op == kS390_CmpDouble || op == kS390_CmpFloat);

  // Overflow checked for add/sub only.
  DCHECK((condition != kOverflow && condition != kNotOverflow) ||
         (op == kS390_Add32 || op == kS390_Add64 || op == kS390_Sub32 ||
          op == kS390_Sub64 || op == kS390_Mul32 ||
          op == kS390_Mul64WithOverflow));

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Register reg = i.OutputRegister(instr->OutputCount() - 1);
  Condition cond = FlagsConditionToCondition(condition, op);
  Label done;
  if (check_unordered) {
    __ mov(reg, (cond == eq || cond == le || cond == lt) ? Operand::Zero()
                                                         : Operand(1));
    __ bunordered(&done);
  }

  // TODO(john.yan): use load imm high on condition here
  __ mov(reg, Operand::Zero());
  __ mov(kScratchReg, Operand(1));
  // locr is sufficient since reg's upper 32 is guarrantee to be 0
  __ locr(cond, reg, kScratchReg);
  __ bind(&done);
}

void CodeGenerator::AssembleArchConditionalBoolean(Instruction* instr) {
  UNREACHABLE();
}

void CodeGenerator::AssembleArchConditionalBranch(Instruction* instr,
                                                  BranchInfo* branch) {
  UNREACHABLE();
}

void CodeGenerator::AssembleArchBinarySearchSwitch(Instruction* instr) {
  S390OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }
  AssembleArchBinarySearchSwitchRange(input, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  S390OperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  int32_t const case_count = static_cast<int32_t>(instr->InputCount() - 2);
  base::Vector<Label*> cases = zone()->AllocateVector<Label*>(case_count);
  for (int32_t index = 0; index < case_count; ++index) {
    cases[index] = GetLabel(i.InputRpo(index + 2));
  }
  Label* const table = AddJumpTable(cases);
  __ CmpU64(input, Operand(case_count));
  __ bge(GetLabel(i.InputRpo(1)));
  __ larl(kScratchReg, table);
  __ ShiftLeftU64(r1, input, Operand(kSystemPointerSizeLog2));
  __ LoadU64(kScratchReg, MemOperand(kScratchReg, r1));
  __ Jump(kScratchReg);
}

void CodeGenerator::AssembleArchSelect(Instruction* instr,
                                       FlagsCondition condition) {
  UNIMPLEMENTED();
}

void CodeGenerator::FinishFrame(Frame* frame) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();
  const DoubleRegList double_saves = call_descriptor->CalleeSavedFPRegisters();

  // Save callee-saved Double registers.
  if (!double_saves.is_empty()) {
    frame->AlignSavedCalleeRegisterSlots();
    DCHECK_EQ(kNumCalleeSavedDoubles, double_saves.Count());
    frame->AllocateSavedCalleeRegisterSlots(kNumCalleeSavedDoubles *
                                            (kDoubleSize / kSystemPointerSize));
  }
  // Save callee-saved registers.
  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {
    // register save area does not include the fp or constant pool pointer.
    const int num_saves = kNumCalleeSaved - 1;
    frame->AllocateSavedCalleeRegisterSlots(num_saves);
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
        __ lay(sp, MemOperand(sp, -kSystemPointerSize));
#else
      // For balance.
      if (false) {
#endif  // V8_ENABLE_WEBASSEMBLY
      } else {
        __ Push(r14, fp);
        __ mov(fp, sp);
      }
    } else if (call_descriptor->IsJSFunctionCall()) {
      __ Prologue(ip);
    } else {
      StackFrame::Type type = info()->GetOutputStackFrameType();
      // TODO(mbrandy): Detect cases where ip is the entrypoint (for
      // efficient initialization of the constant pool pointer register).
      __ StubPrologue(type);
#if V8_ENABLE_WEBASSEMBLY
      if (call_descriptor->IsAnyWasmFunctionCall() ||
          call_descriptor->IsWasmImportWrapper() ||
          call_descriptor->IsWasmCapiFunction()) {
        // For import wrappers and C-API functions, this stack slot is only used
        // for printing stack traces in V8. Also, it holds a WasmImportData
        // instead of the trusted instance data, which is taken care of in the
        // frames accessors.
        __ Push(kWasmImplicitArgRegister);
      }
      if (call_descriptor->IsWasmCapiFunction()) {
        // Reserve space for saving the PC later.
        __ lay(sp, MemOperand(sp, -kSystemPointerSize));
      }
#endif  // V8_ENABLE_WEBASSEMBLY
    }
    unwinding_info_writer_.MarkFrameConstructed(__ pc_offset());
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

  const DoubleRegList saves_fp = call_descriptor->CalleeSavedFPRegisters();
  const RegList saves = call_descriptor->CalleeSavedRegisters();

  if (required_slots > 0) {
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
        Register stack_limit = r1;
        __ LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit);
        __ AddS64(stack_limit, stack_limit,
                  Operand(required_slots * kSystemPointerSize));
        __ CmpU64(sp, stack_limit);
        __ bge(&done);
      }

      if (v8_flags.experimental_wasm_growable_stacks) {
        RegList regs_to_save;
        regs_to_save.set(WasmHandleStackOverflowDescriptor::GapRegister());
        regs_to_save.set(
            WasmHandleStackOverflowDescriptor::FrameBaseRegister());
        for (auto reg : wasm::kGpParamRegisters) regs_to_save.set(reg);
        __ MultiPush(regs_to_save);
        DoubleRegList fp_regs_to_save;
        for (auto reg : wasm::kFpParamRegisters) fp_regs_to_save.set(reg);
        __ MultiPushF64OrV128(fp_regs_to_save, r1);
        __ mov(WasmHandleStackOverflowDescriptor::GapRegister(),
               Operand(required_slots * kSystemPointerSize));
        __ AddS64(
            WasmHandleStackOverflowDescriptor::FrameBaseRegister(), fp,
            Operand(call_descriptor->ParameterSlotCount() * kSystemPointerSize +
                    CommonFrameConstants::kFixedFrameSizeAboveFp));
        __ CallBuiltin(Builtin::kWasmHandleStackOverflow);
        __ MultiPopF64OrV128(fp_regs_to_save, r1);
        __ MultiPop(regs_to_save);
      } else {
        __ Call(static_cast<intptr_t>(Builtin::kWasmStackOverflow),
                RelocInfo::WASM_STUB_CALL);
        // The call does not return, hence we can ignore any references and just
        // define an empty safepoint.
        ReferenceMap* reference_map = zone()->New<ReferenceMap>(zone());
        RecordSafepoint(reference_map);
        if (v8_flags.debug_code) __ stop();
      }

      __ bind(&done);
    }
#endif  // V8_ENABLE_WEBASSEMBLY

    // Skip callee-saved and return slots, which are pushed below.
    required_slots -= saves.Count();
    required_slots -= frame()->GetReturnSlotCount();
    required_slots -= (kDoubleSize / kSystemPointerSize) * saves_fp.Count();
    __ lay(sp, MemOperand(sp, -required_slots * kSystemPointerSize));
  }

  // Save callee-saved Double registers.
  if (!saves_fp.is_empty()) {
    __ MultiPushDoubles(saves_fp);
    DCHECK_EQ(kNumCalleeSavedDoubles, saves_fp.Count());
  }

  // Save callee-saved registers.
  if (!saves.is_empty()) {
    __ MultiPush(saves);
    // register save area does not include the fp or constant pool pointer.
  }

  const int returns = frame()->GetReturnSlotCount();
  // Create space for returns.
  __ AllocateStackSpace(returns * kSystemPointerSize);

  if (!frame()->tagged_slots().IsEmpty()) {
    __ mov(kScratchReg, Operand(0));
    for (int spill_slot : frame()->tagged_slots()) {
      FrameOffset offset = frame_access_state()->GetFrameOffset(spill_slot);
      DCHECK(offset.from_frame_pointer());
      __ StoreU64(kScratchReg, MemOperand(fp, offset.offset()));
    }
  }
}

void CodeGenerator::AssembleReturn(InstructionOperand* additional_pop_count) {
  auto call_descriptor = linkage()->GetIncomingDescriptor();

  const int returns = frame()->GetReturnSlotCount();
  if (returns != 0) {
    // Create space for returns.
    __ lay(sp, MemOperand(sp, returns * kSystemPointerSize));
  }

  // Restore registers.
  const RegList saves = call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {
    __ MultiPop(saves);
  }

  // Restore double registers.
  const DoubleRegList double_saves = call_descriptor->CalleeSavedFPRegisters();
  if (!double_saves.is_empty()) {
    __ MultiPopDoubles(double_saves);
  }

  unwinding_info_writer_.MarkBlockWillExit();

  S390OperandConverter g(this, nullptr);
  const int parameter_slots =
      static_cast<int>(call_descriptor->ParameterSlotCount());

  // {aditional_pop_count} is only greater than zero if {parameter_slots = 0}.
  // Check RawMachineAssembler::PopAndReturn.
  if (parameter_slots != 0) {
    if (additional_pop_count->IsImmediate()) {
      DCHECK_EQ(g.ToConstant(additional_pop_count).ToInt32(), 0);
    } else if (v8_flags.debug_code) {
      __ CmpS64(g.ToRegister(additional_pop_count), Operand(0));
      __ Assert(eq, AbortReason::kUnexpectedAdditionalPopValue);
    }
  }

#if V8_ENABLE_WEBASSEMBLY
  if (call_descriptor->IsAnyWasmFunctionCall() &&
      v8_flags.experimental_wasm_growable_stacks) {
    {
      UseScratchRegisterScope temps{masm()};
      Register scratch = temps.Acquire();
      __ LoadU64(scratch,
                 MemOperand(fp, TypedFrameConstants::kFrameTypeOffset));
      __ CmpU64(
          scratch,
          Operand(StackFrame::TypeToMarker(StackFrame::WASM_SEGMENT_START)));
    }
    Label done;
    __ bne(&done);
    RegList regs_to_save;
    for (auto reg : wasm::kGpReturnRegisters) regs_to_save.set(reg);
    __ MultiPush(regs_to_save);
    DoubleRegList fp_regs_to_save;
    for (auto reg : wasm::kFpParamRegisters) fp_regs_to_save.set(reg);
    __ MultiPushF64OrV128(fp_regs_to_save, r1);
    __ Move(kCArgRegs[0], ExternalReference::isolate_address());
    __ PrepareCallCFunction(1, r0);
    __ CallCFunction(ExternalReference::wasm_shrink_stack(), 1);
    // Restore old FP. We don't need to restore old SP explicitly, because
    // it will be restored from FP in LeaveFrame before return.
    __ mov(fp, kReturnRegister0);
    __ MultiPopF64OrV128(fp_regs_to_save, r1);
    __ MultiPop(regs_to_save);
    __ bind(&done);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  Register argc_reg = r5;
  // Functions with JS linkage have at least one parameter (the receiver).
  // If {parameter_slots} == 0, it means it is a builtin with
  // kDontAdaptArgumentsSentinel, which takes care of JS arguments popping
  // itself.
  const bool drop_jsargs = parameter_slots != 0 &&
                           frame_access_state()->has_frame() &&
                           call_descriptor->IsJSFunctionCall();

  if (call_descriptor->IsCFunctionCall()) {
    AssembleDeconstructFrame();
  } else if (frame_access_state()->has_frame()) {
    // Canonicalize JSFunction return sites for now unless they have an variable
    // number of stack slot pops
    if (additional_pop_count->IsImmediate() &&
        g.ToConstant(additional_pop_count).ToInt32() == 0) {
      if (return_label_.is_bound()) {
        __ b(&return_label_);
        return;
      } else {
        __ bind(&return_label_);
      }
    }
    if (drop_jsargs) {
      // Get the actual argument count.
      DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
      __ LoadU64(argc_reg, MemOperand(fp, StandardFrameConstants::kArgCOffset));
    }
    AssembleDeconstructFrame();
  }

  if (drop_jsargs) {
    // We must pop all arguments from the stack (including the receiver).
    // The number of arguments without the receiver is
    // max(argc_reg, parameter_slots-1), and the receiver is added in
    // DropArguments().
    DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
    if (parameter_slots > 1) {
      Label skip;
      __ CmpS64(argc_reg, Operand(parameter_slots));
      __ bgt(&skip);
      __ mov(argc_reg, Operand(parameter_slots));
      __ bind(&skip);
    }
    __ DropArguments(argc_reg);
  } else if (additional_pop_count->IsImmediate()) {
    int additional_count = g.ToConstant(additional_pop_count).ToInt32();
    __ Drop(parameter_slots + additional_count);
  } else if (parameter_slots == 0) {
    __ Drop(g.ToRegister(additional_pop_count));
  } else {
    // {additional_pop_count} is guaranteed to be zero if {parameter_slots !=
    // 0}. Check RawMachineAssembler::PopAndReturn.
    __ Drop(parameter_slots);
  }
  __ Ret();
}

void CodeGenerator::FinishCode() {}

void CodeGenerator::PrepareForDeoptimizationExits(
    ZoneDeque<DeoptimizationExit*>* exits) {}

AllocatedOperand CodeGenerator::Push(InstructionOperand* source) {
  auto rep = LocationOperand::cast(source)->representation();
  int new_slots = ElementSizeInPointers(rep);
  S390OperandConverter g(this, nullptr);
  int last_frame_slot_id =
      frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
  int sp_delta = frame_access_state_->sp_delta();
  int slot_id = last_frame_slot_id + sp_delta + new_slots;
  AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
  if (source->IsFloatStackSlot() || source->IsDoubleStackSlot()) {
    __ LoadU64(r1, g.ToMemOperand(source));
    __ Push(r1);
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else {
    // Bump the stack pointer and assemble the move.
    __ lay(sp, MemOperand(sp, -(new_slots * kSystemPointerSize)));
    frame_access_state()->IncreaseSPDelta(new_slots);
    AssembleMove(source, &stack_slot);
  }
  temp_slots_ += new_slots;
  return stack_slot;
}

void CodeGenerator::Pop(InstructionOperand* dest, MachineRepresentation rep) {
  int dropped_slots = ElementSizeInPointers(rep);
  S390OperandConverter g(this, nullptr);
  if (dest->IsFloatStackSlot() || dest->IsDoubleStackSlot()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ Pop(r1);
    __ StoreU64(r1, g.ToMemOperand(dest));
  } else {
    int last_frame_slot_id =
        frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
    int sp_delta = frame_access_state_->sp_delta();
    int slot_id = last_frame_slot_id + sp_delta;
    AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
    AssembleMove(&stack_slot, dest);
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ lay(sp, MemOperand(sp, dropped_slots * kSystemPointerSize));
  }
  temp_slots_ -= dropped_slots;
}

void CodeGenerator::PopTempStackSlots() {
  if (temp_slots_ > 0) {
    frame_access_state()->IncreaseSPDelta(-temp_slots_);
    __ lay(sp, MemOperand(sp, temp_slots_ * kSystemPointerSize));
    temp_slots_ = 0;
  }
}

void CodeGenerator::MoveToTempLocation(InstructionOperand* source,
                                       MachineRepresentation rep) {
  // Must be kept in sync with {MoveTempLocationTo}.
  if (!IsFloatingPoint(rep) ||
      ((IsFloatingPoint(rep) &&
        !move_cycle_.pending_double_scratch_register_use))) {
    // The scratch register for this rep is available.
    int scratch_reg_code =
        !IsFloatingPoint(rep) ? kScratchReg.code() : kScratchDoubleReg.code();
    AllocatedOperand scratch(LocationOperand::REGISTER, rep, scratch_reg_code);
    DCHECK(!AreAliased(kScratchReg, r0, r1));
    AssembleMove(source, &scratch);
  } else {
    // The scratch register is blocked by pending moves. Use the stack instead.
    Push(source);
  }
}

void CodeGenerator::MoveTempLocationTo(InstructionOperand* dest,
                                       MachineRepresentation rep) {
  if (!IsFloatingPoint(rep) ||
      ((IsFloatingPoint(rep) &&
        !move_cycle_.pending_double_scratch_register_use))) {
    int scratch_reg_code =
        !IsFloatingPoint(rep) ? kScratchReg.code() : kScratchDoubleReg.code();
    AllocatedOperand scratch(LocationOperand::REGISTER, rep, scratch_reg_code);
    DCHECK(!AreAliased(kScratchReg, r0, r1));
    AssembleMove(&scratch, dest);
  } else {
    Pop(dest, rep);
  }
  move_cycle_ = MoveCycleState();
}

void CodeGenerator::SetPendingMove(MoveOperands* move) {
  if ((move->source().IsConstant() || move->source().IsFPStackSlot()) &&
      !move->destination().IsFPRegister()) {
    move_cycle_.pending_double_scratch_register_use = true;
  }
}

void CodeGenerator::AssembleMove(InstructionOperand* source,
                                 InstructionOperand* destination) {
  S390OperandConverter g(this, nullptr);
  // Dispatch on the source and destination operand kinds.  Not all
  // combinations are possible.
  // If a move type needs the scratch register, this also needs to be recorded
  // in {SetPendingMove} to avoid conflicts with the gap resolver.
  if (source->IsRegister()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      __ Move(g.ToRegister(destination), src);
    } else {
      __ StoreU64(src, g.ToMemOperand(destination));
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsRegister()) {
      __ LoadU64(g.ToRegister(destination), src);
    } else {
      Register temp = r1;
      __ LoadU64(temp, src, r0);
      __ StoreU64(temp, g.ToMemOperand(destination));
    }
  } else if (source->IsConstant()) {
    Constant src = g.ToConstant(source);
    if (destination->IsRegister() || destination->IsStackSlot()) {
      Register dst = destination->IsRegister() ? g.ToRegister(destination) : r1;
      switch (src.type()) {
        case Constant::kInt32:
          __ mov(dst, Operand(src.ToInt32(), src.rmode()));
          break;
        case Constant::kInt64:
          __ mov(dst, Operand(src.ToInt64(), src.rmode()));
          break;
        case Constant::kFloat32:
          __ mov(dst, Operand::EmbeddedNumber(src.ToFloat32()));
          break;
        case Constant::kFloat64:
          __ mov(dst, Operand::EmbeddedNumber(src.ToFloat64().value()));
          break;
        case Constant::kExternalReference:
          __ Move(dst, src.ToExternalReference());
          break;
        case Constant::kHeapObject: {
          Handle<HeapObject> src_object = src.ToHeapObject();
          RootIndex index;
          if (IsMaterializableFromRoot(src_object, &index)) {
            __ LoadRoot(dst, index);
          } else {
            __ Move(dst, src_object);
          }
          break;
        }
        case Constant::kCompressedHeapObject: {
          Handle<HeapObject> src_object = src.ToHeapObject();
          RootIndex index;
          if (IsMaterializableFromRoot(src_object, &index)) {
            __ LoadTaggedRoot(dst, index);
          } else {
            __ Move(dst, src_object, RelocInfo::COMPRESSED_EMBEDDED_OBJECT);
          }
          break;
        }
        case Constant::kRpoNumber:
          UNREACHABLE();  // TODO(dcarney): loading RPO constants on S390.
      }
      if (destination->IsStackSlot()) {
        __ StoreU64(dst, g.ToMemOperand(destination), r0);
      }
    } else {
      DoubleRegister dst = destination->IsFPRegister()
                               ? g.ToDoubleRegister(destination)
                               : kScratchDoubleReg;
      double value = (src.type() == Constant::kFloat32)
                         ? src.ToFloat32()
                         : src.ToFloat64().value();
      if (src.type() == Constant::kFloat32) {
        __ LoadF32<float>(dst, src.ToFloat32(), r1);
      } else {
        __ LoadF64<double>(dst, value, r1);
      }

      if (destination->IsFloatStackSlot()) {
        __ StoreF32(dst, g.ToMemOperand(destination));
      } else if (destination->IsDoubleStackSlot()) {
        __ StoreF64(dst, g.ToMemOperand(destination));
      }
    }
  } else if (source->IsFPRegister()) {
    MachineRepresentation rep = LocationOperand::cast(source)->representation();
    if (rep == MachineRepresentation::kSimd128) {
      if (destination->IsSimd128Register()) {
        __ vlr(g.ToSimd128Register(destination), g.ToSimd128Register(source),
               Condition(0), Condition(0), Condition(0));
      } else {
        DCHECK(destination->IsSimd128StackSlot());
        __ StoreV128(g.ToSimd128Register(source), g.ToMemOperand(destination),
                     r1);
      }
    } else {
      DoubleRegister src = g.ToDoubleRegister(source);
      if (destination->IsFPRegister()) {
        DoubleRegister dst = g.ToDoubleRegister(destination);
        __ Move(dst, src);
      } else {
        DCHECK(destination->IsFPStackSlot());
        LocationOperand* op = LocationOperand::cast(source);
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ StoreF64(src, g.ToMemOperand(destination));
        } else {
          __ StoreF32(src, g.ToMemOperand(destination));
        }
      }
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPRegister() || destination->IsFPStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsFPRegister()) {
      LocationOperand* op = LocationOperand::cast(source);
      if (op->representation() == MachineRepresentation::kFloat64) {
        __ LoadF64(g.ToDoubleRegister(destination), src);
      } else if (op->representation() == MachineRepresentation::kFloat32) {
        __ LoadF32(g.ToDoubleRegister(destination), src);
      } else {
        DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
        __ LoadV128(g.ToSimd128Register(destination), g.ToMemOperand(source),
                    r1);
      }
    } else {
      LocationOperand* op = LocationOperand::cast(source);
      DoubleRegister temp = kScratchDoubleReg;
      if (op->representation() == MachineRepresentation::kFloat64) {
        __ LoadF64(temp, src);
        __ StoreF64(temp, g.ToMemOperand(destination));
      } else if (op->representation() == MachineRepresentation::kFloat32) {
        __ LoadF32(temp, src);
        __ StoreF32(temp, g.ToMemOperand(destination));
      } else {
        DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
        __ LoadV128(kScratchDoubleReg, g.ToMemOperand(source), r1);
        __ StoreV128(kScratchDoubleReg, g.ToMemOperand(destination), r1);
      }
    }
  } else {
    UNREACHABLE();
  }
}

// Swaping contents in source and destination.
// source and destination could be:
//   Register,
//   FloatRegister,
//   DoubleRegister,
//   StackSlot,
//   FloatStackSlot,
//   or DoubleStackSlot
void CodeGenerator::AssembleSwap(InstructionOperand* source,
                                 InstructionOperand* destination) {
  S390OperandConverter g(this, nullptr);
  if (source->IsRegister()) {
    Register src = g.ToRegister(source);
    if (destination->IsRegister()) {
      __ SwapP(src, g.ToRegister(destination), kScratchReg);
    } else {
      DCHECK(destination->IsStackSlot());
      __ SwapP(src, g.ToMemOperand(destination), kScratchReg);
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsStackSlot());
    __ SwapP(g.ToMemOperand(source), g.ToMemOperand(destination), kScratchReg,
             r0);
  } else if (source->IsFloatRegister()) {
    DoubleRegister src = g.ToDoubleRegister(source);
    if (destination->IsFloatRegister()) {
      __ SwapFloat32(src, g.ToDoubleRegister(destination), kScratchDoubleReg);
    } else {
      DCHECK(destination->IsFloatStackSlot());
      __ SwapFloat32(src, g.ToMemOperand(destination), kScratchDoubleReg);
    }
  } else if (source->IsDoubleRegister()) {
    DoubleRegister src = g.ToDoubleRegister(source);
    if (destination->IsDoubleRegister()) {
      __ SwapDouble(src, g.ToDoubleRegister(destination), kScratchDoubleReg);
    } else {
      DCHECK(destination->IsDoubleStackSlot());
      __ SwapDouble(src, g.ToMemOperand(destination), kScratchDoubleReg);
    }
  } else if (source->IsFloatStackSlot()) {
    DCHECK(destination->IsFloatStackSlot());
    __ SwapFloat32(g.ToMemOperand(source), g.ToMemOperand(destination),
                   kScratchDoubleReg);
  } else if (source->IsDoubleStackSlot()) {
    DCHECK(destination->IsDoubleStackSlot());
    __ SwapDouble(g.ToMemOperand(source), g.ToMemOperand(destination),
                  kScratchDoubleReg);
  } else if (source->IsSimd128Register()) {
    Simd128Register src = g.ToSimd128Register(source);
    if (destination->IsSimd128Register()) {
      __ SwapSimd128(src, g.ToSimd128Register(destination), kScratchDoubleReg);
    } else {
      DCHECK(destination->IsSimd128StackSlot());
      __ SwapSimd128(src, g.ToMemOperand(destination), kScratchDoubleReg);
    }
  } else if (source->IsSimd128StackSlot()) {
    DCHECK(destination->IsSimd128StackSlot());
    __ SwapSimd128(g.ToMemOperand(source), g.ToMemOperand(destination),
                   kScratchDoubleReg);
  } else {
    UNREACHABLE();
  }
}

void CodeGenerator::AssembleJumpTable(base::Vector<Label*> targets) {
  for (auto target : targets) {
    __ emit_label_addr(target);
  }
}

#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
