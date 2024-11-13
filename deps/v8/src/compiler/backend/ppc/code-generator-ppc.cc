// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/numbers/double.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/callable.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/code-generator-impl.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/gap-resolver.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/osr.h"
#include "src/heap/mutable-page-metadata.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

#define __ masm()->

#define kScratchReg r11

// Adds PPC-specific methods to convert InstructionOperands.
class PPCOperandConverter final : public InstructionOperandConverter {
 public:
  PPCOperandConverter(CodeGenerator* gen, Instruction* instr)
      : InstructionOperandConverter(gen, instr) {}

  size_t OutputCount() { return instr_->OutputCount(); }

  RCBit OutputRCBit() const {
    switch (instr_->flags_mode()) {
      case kFlags_branch:
      case kFlags_conditional_branch:
      case kFlags_deoptimize:
      case kFlags_set:
      case kFlags_conditional_set:
      case kFlags_trap:
      case kFlags_select:
        return SetRC;
      case kFlags_none:
        return LeaveRC;
    }
    UNREACHABLE();
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
#if V8_TARGET_ARCH_PPC64
        return Operand(constant.ToInt64());
#endif
      case Constant::kExternalReference:
        return Operand(constant.ToExternalReference());
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
      case Constant::kHeapObject:
      case Constant::kRpoNumber:
        break;
    }
    UNREACHABLE();
  }

  MemOperand MemoryOperand(AddressingMode* mode, size_t* first_index) {
    const size_t index = *first_index;
    AddressingMode addr_mode = AddressingModeField::decode(instr_->opcode());
    if (mode) *mode = addr_mode;
    switch (addr_mode) {
      case kMode_None:
        break;
      case kMode_MRI:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputInt64(index + 1));
      case kMode_MRR:
        *first_index += 2;
        return MemOperand(InputRegister(index + 0), InputRegister(index + 1));
      case kMode_Root:
        *first_index += 1;
        return MemOperand(kRootRegister, InputRegister(index));
    }
    UNREACHABLE();
  }

  MemOperand MemoryOperand(AddressingMode* mode = NULL,
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
};

static inline bool HasRegisterInput(Instruction* instr, size_t index) {
  return instr->InputAt(index)->IsRegister();
}

namespace {

class OutOfLineRecordWrite final : public OutOfLineCode {
 public:
  OutOfLineRecordWrite(
      CodeGenerator* gen, Register object, Register offset, Register value,
      Register scratch0, Register scratch1, RecordWriteMode mode,
      StubCallMode stub_mode, UnwindingInfoWriter* unwinding_info_writer,
      IndirectPointerTag indirect_pointer_tag = kIndirectPointerNullTag)
      : OutOfLineCode(gen),
        object_(object),
        offset_(offset),
        offset_immediate_(0),
        value_(value),
        scratch0_(scratch0),
        scratch1_(scratch1),
        mode_(mode),
#if V8_ENABLE_WEBASSEMBLY
        stub_mode_(stub_mode),
#endif  // V8_ENABLE_WEBASSEMBLY
        must_save_lr_(!gen->frame_access_state()->has_frame()),
        unwinding_info_writer_(unwinding_info_writer),
        zone_(gen->zone()),
        indirect_pointer_tag_(indirect_pointer_tag) {
    DCHECK(!AreAliased(object, offset, scratch0, scratch1));
    DCHECK(!AreAliased(value, offset, scratch0, scratch1));
  }

  OutOfLineRecordWrite(
      CodeGenerator* gen, Register object, int32_t offset, Register value,
      Register scratch0, Register scratch1, RecordWriteMode mode,
      StubCallMode stub_mode, UnwindingInfoWriter* unwinding_info_writer,
      IndirectPointerTag indirect_pointer_tag = kIndirectPointerNullTag)
      : OutOfLineCode(gen),
        object_(object),
        offset_(no_reg),
        offset_immediate_(offset),
        value_(value),
        scratch0_(scratch0),
        scratch1_(scratch1),
        mode_(mode),
#if V8_ENABLE_WEBASSEMBLY
        stub_mode_(stub_mode),
#endif  // V8_ENABLE_WEBASSEMBLY
        must_save_lr_(!gen->frame_access_state()->has_frame()),
        unwinding_info_writer_(unwinding_info_writer),
        zone_(gen->zone()),
        indirect_pointer_tag_(indirect_pointer_tag) {
  }

  void Generate() final {
    ConstantPoolUnavailableScope constant_pool_unavailable(masm());
    // When storing an indirect pointer, the value will always be a
    // full/decompressed pointer.
    if (COMPRESS_POINTERS_BOOL &&
        mode_ != RecordWriteMode::kValueIsIndirectPointer) {
      __ DecompressTagged(value_, value_);
    }
    __ CheckPageFlag(value_, scratch0_,
                     MemoryChunk::kPointersToHereAreInterestingMask, eq,
                     exit());
    if (offset_ == no_reg) {
      __ addi(scratch1_, object_, Operand(offset_immediate_));
    } else {
      DCHECK_EQ(0, offset_immediate_);
      __ add(scratch1_, object_, offset_);
    }
    SaveFPRegsMode const save_fp_mode = frame()->DidAllocateDoubleRegisters()
                                            ? SaveFPRegsMode::kSave
                                            : SaveFPRegsMode::kIgnore;
    if (must_save_lr_) {
      // We need to save and restore lr if the frame was elided.
      __ mflr(scratch0_);
      __ Push(scratch0_);
      unwinding_info_writer_->MarkLinkRegisterOnTopOfStack(__ pc_offset());
    }
    if (mode_ == RecordWriteMode::kValueIsEphemeronKey) {
      __ CallEphemeronKeyBarrier(object_, scratch1_, save_fp_mode);
    } else if (mode_ == RecordWriteMode::kValueIsIndirectPointer) {
      DCHECK(IsValidIndirectPointerTag(indirect_pointer_tag_));
      __ CallIndirectPointerBarrier(object_, scratch1_, save_fp_mode,
                                    indirect_pointer_tag_);
#if V8_ENABLE_WEBASSEMBLY
    } else if (stub_mode_ == StubCallMode::kCallWasmRuntimeStub) {
      __ CallRecordWriteStubSaveRegisters(object_, scratch1_, save_fp_mode,
                                          StubCallMode::kCallWasmRuntimeStub);
#endif  // V8_ENABLE_WEBASSEMBLY
    } else {
      __ CallRecordWriteStubSaveRegisters(object_, scratch1_, save_fp_mode);
    }
    if (must_save_lr_) {
      // We need to save and restore lr if the frame was elided.
      __ Pop(scratch0_);
      __ mtlr(scratch0_);
      unwinding_info_writer_->MarkPopLinkRegisterFromTopOfStack(__ pc_offset());
    }
  }

 private:
  Register const object_;
  Register const offset_;
  int32_t const offset_immediate_;  // Valid if offset_ == no_reg.
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
  IndirectPointerTag indirect_pointer_tag_;
};

Condition FlagsConditionToCondition(FlagsCondition condition, ArchOpcode op) {
  switch (condition) {
    case kEqual:
      return eq;
    case kNotEqual:
      return ne;
    case kSignedLessThan:
    case kUnsignedLessThan:
      return lt;
    case kSignedGreaterThanOrEqual:
    case kUnsignedGreaterThanOrEqual:
      return ge;
    case kSignedLessThanOrEqual:
    case kUnsignedLessThanOrEqual:
      return le;
    case kSignedGreaterThan:
    case kUnsignedGreaterThan:
      return gt;
    case kOverflow:
      // Overflow checked for add/sub only.
      switch (op) {
#if V8_TARGET_ARCH_PPC64
        case kPPC_Add32:
        case kPPC_Add64:
        case kPPC_Sub:
#endif
        case kPPC_AddWithOverflow32:
        case kPPC_SubWithOverflow32:
          return lt;
        default:
          break;
      }
      break;
    case kNotOverflow:
      switch (op) {
#if V8_TARGET_ARCH_PPC64
        case kPPC_Add32:
        case kPPC_Add64:
        case kPPC_Sub:
#endif
        case kPPC_AddWithOverflow32:
        case kPPC_SubWithOverflow32:
          return ge;
        default:
          break;
      }
      break;
    default:
      break;
  }
  UNREACHABLE();
}

}  // namespace

#define ASSEMBLE_FLOAT_UNOP_RC(asm_instr, round)                     \
  do {                                                               \
    __ asm_instr(i.OutputDoubleRegister(), i.InputDoubleRegister(0), \
                 i.OutputRCBit());                                   \
    if (round) {                                                     \
      __ frsp(i.OutputDoubleRegister(), i.OutputDoubleRegister());   \
    }                                                                \
  } while (0)

#define ASSEMBLE_FLOAT_BINOP_RC(asm_instr, round)                    \
  do {                                                               \
    __ asm_instr(i.OutputDoubleRegister(), i.InputDoubleRegister(0), \
                 i.InputDoubleRegister(1), i.OutputRCBit());         \
    if (round) {                                                     \
      __ frsp(i.OutputDoubleRegister(), i.OutputDoubleRegister());   \
    }                                                                \
  } while (0)

#define ASSEMBLE_BINOP(asm_instr_reg, asm_instr_imm)           \
  do {                                                         \
    if (HasRegisterInput(instr, 1)) {                          \
      __ asm_instr_reg(i.OutputRegister(), i.InputRegister(0), \
                       i.InputRegister(1));                    \
    } else {                                                   \
      __ asm_instr_imm(i.OutputRegister(), i.InputRegister(0), \
                       i.InputImmediate(1));                   \
    }                                                          \
  } while (0)

#define ASSEMBLE_BINOP_RC(asm_instr_reg, asm_instr_imm)        \
  do {                                                         \
    if (HasRegisterInput(instr, 1)) {                          \
      __ asm_instr_reg(i.OutputRegister(), i.InputRegister(0), \
                       i.InputRegister(1), i.OutputRCBit());   \
    } else {                                                   \
      __ asm_instr_imm(i.OutputRegister(), i.InputRegister(0), \
                       i.InputImmediate(1), i.OutputRCBit());  \
    }                                                          \
  } while (0)

#define ASSEMBLE_BINOP_INT_RC(asm_instr_reg, asm_instr_imm)    \
  do {                                                         \
    if (HasRegisterInput(instr, 1)) {                          \
      __ asm_instr_reg(i.OutputRegister(), i.InputRegister(0), \
                       i.InputRegister(1), i.OutputRCBit());   \
    } else {                                                   \
      __ asm_instr_imm(i.OutputRegister(), i.InputRegister(0), \
                       i.InputImmediate(1), i.OutputRCBit());  \
    }                                                          \
  } while (0)

#define ASSEMBLE_ADD_WITH_OVERFLOW()                                    \
  do {                                                                  \
    if (HasRegisterInput(instr, 1)) {                                   \
      __ AddAndCheckForOverflow(i.OutputRegister(), i.InputRegister(0), \
                                i.InputRegister(1), kScratchReg, r0);   \
    } else {                                                            \
      __ AddAndCheckForOverflow(i.OutputRegister(), i.InputRegister(0), \
                                i.InputInt32(1), kScratchReg, r0);      \
    }                                                                   \
  } while (0)

#define ASSEMBLE_SUB_WITH_OVERFLOW()                                    \
  do {                                                                  \
    if (HasRegisterInput(instr, 1)) {                                   \
      __ SubAndCheckForOverflow(i.OutputRegister(), i.InputRegister(0), \
                                i.InputRegister(1), kScratchReg, r0);   \
    } else {                                                            \
      __ AddAndCheckForOverflow(i.OutputRegister(), i.InputRegister(0), \
                                -i.InputInt32(1), kScratchReg, r0);     \
    }                                                                   \
  } while (0)

#if V8_TARGET_ARCH_PPC64
#define ASSEMBLE_ADD_WITH_OVERFLOW32()         \
  do {                                         \
    ASSEMBLE_ADD_WITH_OVERFLOW();              \
    __ extsw(kScratchReg, kScratchReg, SetRC); \
  } while (0)

#define ASSEMBLE_SUB_WITH_OVERFLOW32()         \
  do {                                         \
    ASSEMBLE_SUB_WITH_OVERFLOW();              \
    __ extsw(kScratchReg, kScratchReg, SetRC); \
  } while (0)
#else
#define ASSEMBLE_ADD_WITH_OVERFLOW32 ASSEMBLE_ADD_WITH_OVERFLOW
#define ASSEMBLE_SUB_WITH_OVERFLOW32 ASSEMBLE_SUB_WITH_OVERFLOW
#endif

#define ASSEMBLE_COMPARE(cmp_instr, cmpl_instr)                        \
  do {                                                                 \
    const CRegister cr = cr0;                                          \
    if (HasRegisterInput(instr, 1)) {                                  \
      if (i.CompareLogical()) {                                        \
        __ cmpl_instr(i.InputRegister(0), i.InputRegister(1), cr);     \
      } else {                                                         \
        __ cmp_instr(i.InputRegister(0), i.InputRegister(1), cr);      \
      }                                                                \
    } else {                                                           \
      if (i.CompareLogical()) {                                        \
        __ cmpl_instr##i(i.InputRegister(0), i.InputImmediate(1), cr); \
      } else {                                                         \
        __ cmp_instr##i(i.InputRegister(0), i.InputImmediate(1), cr);  \
      }                                                                \
    }                                                                  \
    DCHECK_EQ(SetRC, i.OutputRCBit());                                 \
  } while (0)

#define ASSEMBLE_FLOAT_COMPARE(cmp_instr)                                 \
  do {                                                                    \
    const CRegister cr = cr0;                                             \
    __ cmp_instr(i.InputDoubleRegister(0), i.InputDoubleRegister(1), cr); \
    DCHECK_EQ(SetRC, i.OutputRCBit());                                    \
  } while (0)

#define ASSEMBLE_MODULO(div_instr, mul_instr)                        \
  do {                                                               \
    const Register scratch = kScratchReg;                            \
    __ div_instr(scratch, i.InputRegister(0), i.InputRegister(1));   \
    __ mul_instr(scratch, scratch, i.InputRegister(1));              \
    __ sub(i.OutputRegister(), i.InputRegister(0), scratch, LeaveOE, \
           i.OutputRCBit());                                         \
  } while (0)

#define ASSEMBLE_FLOAT_MODULO()                                             \
  do {                                                                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                           \
    __ PrepareCallCFunction(0, 2, kScratchReg);                             \
    __ MovToFloatParameters(i.InputDoubleRegister(0),                       \
                            i.InputDoubleRegister(1));                      \
    __ CallCFunction(ExternalReference::mod_two_doubles_operation(), 0, 2); \
    __ MovFromFloatResult(i.OutputDoubleRegister());                        \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                                    \
  } while (0)

#define ASSEMBLE_IEEE754_UNOP(name)                                            \
  do {                                                                         \
    /* TODO(bmeurer): We should really get rid of this special instruction, */ \
    /* and generate a CallAddress instruction instead. */                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                              \
    __ PrepareCallCFunction(0, 1, kScratchReg);                                \
    __ MovToFloatParameter(i.InputDoubleRegister(0));                          \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 1);    \
    /* Move the result in the double result register. */                       \
    __ MovFromFloatResult(i.OutputDoubleRegister());                           \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                                       \
  } while (0)

#define ASSEMBLE_IEEE754_BINOP(name)                                           \
  do {                                                                         \
    /* TODO(bmeurer): We should really get rid of this special instruction, */ \
    /* and generate a CallAddress instruction instead. */                      \
    FrameScope scope(masm(), StackFrame::MANUAL);                              \
    __ PrepareCallCFunction(0, 2, kScratchReg);                                \
    __ MovToFloatParameters(i.InputDoubleRegister(0),                          \
                            i.InputDoubleRegister(1));                         \
    __ CallCFunction(ExternalReference::ieee754_##name##_function(), 0, 2);    \
    /* Move the result in the double result register. */                       \
    __ MovFromFloatResult(i.OutputDoubleRegister());                           \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                                       \
  } while (0)

#define ASSEMBLE_LOAD_FLOAT(asm_instr, asm_instrp, asm_instrx) \
  do {                                                         \
    DoubleRegister result = i.OutputDoubleRegister();          \
    size_t index = 0;                                          \
    AddressingMode mode = kMode_None;                          \
    MemOperand operand = i.MemoryOperand(&mode, &index);       \
    bool is_atomic = i.InputInt32(index);                      \
    if (mode == kMode_MRI) {                                   \
      intptr_t offset = operand.offset();                      \
      if (is_int16(offset)) {                                  \
        __ asm_instr(result, operand);                         \
      } else {                                                 \
        CHECK(CpuFeatures::IsSupported(PPC_10_PLUS));          \
        __ asm_instrp(result, operand);                        \
      }                                                        \
    } else {                                                   \
      __ asm_instrx(result, operand);                          \
    }                                                          \
    if (is_atomic) __ lwsync();                                \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                       \
  } while (0)

#define ASSEMBLE_LOAD_INTEGER(asm_instr, asm_instrp, asm_instrx,   \
                              must_be_aligned)                     \
  do {                                                             \
    Register result = i.OutputRegister();                          \
    size_t index = 0;                                              \
    AddressingMode mode = kMode_None;                              \
    MemOperand operand = i.MemoryOperand(&mode, &index);           \
    bool is_atomic = i.InputInt32(index);                          \
    if (mode == kMode_MRI) {                                       \
      intptr_t offset = operand.offset();                          \
      bool misaligned = offset & 3;                                \
      if (is_int16(offset) && (!must_be_aligned || !misaligned)) { \
        __ asm_instr(result, operand);                             \
      } else {                                                     \
        CHECK(CpuFeatures::IsSupported(PPC_10_PLUS));              \
        __ asm_instrp(result, operand);                            \
      }                                                            \
    } else {                                                       \
      __ asm_instrx(result, operand);                              \
    }                                                              \
    if (is_atomic) __ lwsync();                                    \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                           \
  } while (0)

#define ASSEMBLE_LOAD_INTEGER_RR(asm_instr)              \
  do {                                                   \
    Register result = i.OutputRegister();                \
    size_t index = 0;                                    \
    AddressingMode mode = kMode_None;                    \
    MemOperand operand = i.MemoryOperand(&mode, &index); \
    DCHECK_EQ(mode, kMode_MRR);                          \
    bool is_atomic = i.InputInt32(index);                \
    __ asm_instr(result, operand);                       \
    if (is_atomic) __ lwsync();                          \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                 \
  } while (0)

#define ASSEMBLE_STORE_FLOAT(asm_instr, asm_instrp, asm_instrx) \
  do {                                                          \
    size_t index = 0;                                           \
    AddressingMode mode = kMode_None;                           \
    MemOperand operand = i.MemoryOperand(&mode, &index);        \
    DoubleRegister value = i.InputDoubleRegister(index);        \
    bool is_atomic = i.InputInt32(3);                           \
    if (is_atomic) __ lwsync();                                 \
    /* removed frsp as instruction-selector checked */          \
    /* value to be kFloat32 */                                  \
    if (mode == kMode_MRI) {                                    \
      intptr_t offset = operand.offset();                       \
      if (is_int16(offset)) {                                   \
        __ asm_instr(value, operand);                           \
      } else {                                                  \
        CHECK(CpuFeatures::IsSupported(PPC_10_PLUS));           \
        __ asm_instrp(value, operand);                          \
      }                                                         \
    } else {                                                    \
      __ asm_instrx(value, operand);                            \
    }                                                           \
    if (is_atomic) __ sync();                                   \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                        \
  } while (0)

#define ASSEMBLE_STORE_INTEGER(asm_instr, asm_instrp, asm_instrx,  \
                               must_be_aligned)                    \
  do {                                                             \
    size_t index = 0;                                              \
    AddressingMode mode = kMode_None;                              \
    MemOperand operand = i.MemoryOperand(&mode, &index);           \
    Register value = i.InputRegister(index);                       \
    bool is_atomic = i.InputInt32(index + 1);                      \
    if (is_atomic) __ lwsync();                                    \
    if (mode == kMode_MRI) {                                       \
      intptr_t offset = operand.offset();                          \
      bool misaligned = offset & 3;                                \
      if (is_int16(offset) && (!must_be_aligned || !misaligned)) { \
        __ asm_instr(value, operand);                              \
      } else {                                                     \
        CHECK(CpuFeatures::IsSupported(PPC_10_PLUS));              \
        __ asm_instrp(value, operand);                             \
      }                                                            \
    } else {                                                       \
      __ asm_instrx(value, operand);                               \
    }                                                              \
    if (is_atomic) __ sync();                                      \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                           \
  } while (0)

#define ASSEMBLE_STORE_INTEGER_RR(asm_instr)             \
  do {                                                   \
    size_t index = 0;                                    \
    AddressingMode mode = kMode_None;                    \
    MemOperand operand = i.MemoryOperand(&mode, &index); \
    DCHECK_EQ(mode, kMode_MRR);                          \
    Register value = i.InputRegister(index);             \
    bool is_atomic = i.InputInt32(index + 1);            \
    if (is_atomic) __ lwsync();                          \
    __ asm_instr(value, operand);                        \
    if (is_atomic) __ sync();                            \
    DCHECK_EQ(LeaveRC, i.OutputRCBit());                 \
  } while (0)

#if V8_TARGET_ARCH_PPC64
// TODO(mbrandy): fix paths that produce garbage in offset's upper 32-bits.
#define CleanUInt32(x) __ ClearLeftImm(x, x, Operand(32))
#else
#define CleanUInt32(x)
#endif

#if V8_ENABLE_WEBASSEMBLY
static inline bool is_wasm_on_be(bool IsWasm) {
#if V8_TARGET_BIG_ENDIAN
  return IsWasm;
#else
  return false;
#endif
}
#endif

#if V8_ENABLE_WEBASSEMBLY
#define MAYBE_REVERSE_IF_WASM(dst, src, op, scratch, reset) \
  if (is_wasm_on_be(info()->IsWasm())) {                    \
    __ op(dst, src, scratch);                               \
    if (reset) src = dst;                                   \
  }
#else
#define MAYBE_REVERSE_IF_WASM(dst, src, op, scratch, reset)
#endif

#define ASSEMBLE_ATOMIC_EXCHANGE(_type, reverse_op)                    \
  do {                                                                 \
    Register val = i.InputRegister(2);                                 \
    Register dst = i.OutputRegister();                                 \
    MAYBE_REVERSE_IF_WASM(ip, val, reverse_op, kScratchReg, true);     \
    __ AtomicExchange<_type>(                                          \
        MemOperand(i.InputRegister(0), i.InputRegister(1)), val, dst); \
    MAYBE_REVERSE_IF_WASM(dst, dst, reverse_op, kScratchReg, false);   \
  } while (false)

#define ASSEMBLE_ATOMIC_COMPARE_EXCHANGE(_type, reverse_op)                 \
  do {                                                                      \
    Register expected_val = i.InputRegister(2);                             \
    Register new_val = i.InputRegister(3);                                  \
    Register dst = i.OutputRegister();                                      \
    MAYBE_REVERSE_IF_WASM(ip, expected_val, reverse_op, kScratchReg, true); \
    MAYBE_REVERSE_IF_WASM(r0, new_val, reverse_op, kScratchReg, true);      \
    __ AtomicCompareExchange<_type>(                                        \
        MemOperand(i.InputRegister(0), i.InputRegister(1)), expected_val,   \
        new_val, dst, kScratchReg);                                         \
    MAYBE_REVERSE_IF_WASM(dst, dst, reverse_op, kScratchReg, false);        \
  } while (false)

#define ASSEMBLE_ATOMIC_BINOP_BYTE(bin_inst, _type)                          \
  do {                                                                       \
    auto bin_op = [&](Register dst, Register lhs, Register rhs) {            \
      if (std::is_signed<_type>::value) {                                    \
        __ extsb(dst, lhs);                                                  \
        __ bin_inst(dst, dst, rhs);                                          \
      } else {                                                               \
        __ bin_inst(dst, lhs, rhs);                                          \
      }                                                                      \
    };                                                                       \
    MemOperand dst_operand =                                                 \
        MemOperand(i.InputRegister(0), i.InputRegister(1));                  \
    __ AtomicOps<_type>(dst_operand, i.InputRegister(2), i.OutputRegister(), \
                        kScratchReg, bin_op);                                \
    break;                                                                   \
  } while (false)

#define ASSEMBLE_ATOMIC_BINOP(bin_inst, _type, reverse_op, scratch)           \
  do {                                                                        \
    auto bin_op = [&](Register dst, Register lhs, Register rhs) {             \
      Register _lhs = lhs;                                                    \
      MAYBE_REVERSE_IF_WASM(dst, _lhs, reverse_op, scratch, true);            \
      if (std::is_signed<_type>::value) {                                     \
        switch (sizeof(_type)) {                                              \
          case 1:                                                             \
            UNREACHABLE();                                                    \
            break;                                                            \
          case 2:                                                             \
            __ extsh(dst, _lhs);                                              \
            break;                                                            \
          case 4:                                                             \
            __ extsw(dst, _lhs);                                              \
            break;                                                            \
          case 8:                                                             \
            break;                                                            \
          default:                                                            \
            UNREACHABLE();                                                    \
        }                                                                     \
      }                                                                       \
      __ bin_inst(dst, _lhs, rhs);                                            \
      MAYBE_REVERSE_IF_WASM(dst, dst, reverse_op, scratch, false);            \
    };                                                                        \
    MemOperand dst_operand =                                                  \
        MemOperand(i.InputRegister(0), i.InputRegister(1));                   \
    __ AtomicOps<_type>(dst_operand, i.InputRegister(2), i.OutputRegister(),  \
                        kScratchReg, bin_op);                                 \
    MAYBE_REVERSE_IF_WASM(i.OutputRegister(), i.OutputRegister(), reverse_op, \
                          scratch, false);                                    \
    break;                                                                    \
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
    masm->AddS64(sp, sp, Operand(-stack_slot_delta * kSystemPointerSize), r0);
    state->IncreaseSPDelta(stack_slot_delta);
  } else if (allow_shrinkage && stack_slot_delta < 0) {
    if (pending_pushes != nullptr) {
      FlushPendingPushRegisters(masm, state, pending_pushes);
    }
    masm->AddS64(sp, sp, Operand(-stack_slot_delta * kSystemPointerSize), r0);
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
    PPCOperandConverter g(this, instr);
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
  Register scratch = kScratchReg;
  __ ComputeCodeStartAddress(scratch);
  __ CmpS64(scratch, kJavaScriptCallCodeStartRegister);
  __ Assert(eq, AbortReason::kWrongFunctionCodeStart);
}

void CodeGenerator::BailoutIfDeoptimized() { __ BailoutIfDeoptimized(); }

// Assembles an instruction after register allocation, producing machine code.
CodeGenerator::CodeGenResult CodeGenerator::AssembleArchInstruction(
    Instruction* instr) {
  PPCOperandConverter i(this, instr);
  ArchOpcode opcode = ArchOpcodeField::decode(instr->opcode());

  switch (opcode) {
    case kArchCallCodeObject: {
      v8::internal::Assembler::BlockTrampolinePoolScope block_trampoline_pool(
          masm());
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
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
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
      // We must not share code targets for calls to builtins for wasm code, as
      // they might need to be patched individually.
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
#ifdef V8_TARGET_ARCH_PPC64
        Address wasm_code = static_cast<Address>(constant.ToInt64());
#else
        Address wasm_code = static_cast<Address>(constant.ToInt32());
#endif
        __ Call(wasm_code, constant.rmode());
      } else {
        __ Call(i.InputRegister(0));
      }
      RecordCallPosition(instr);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      frame_access_state()->ClearSPDelta();
      break;
    }
    case kArchTailCallWasm: {
      // We must not share code targets for calls to builtins for wasm code, as
      // they might need to be patched individually.
      if (instr->InputAt(0)->IsImmediate()) {
        Constant constant = i.ToConstant(instr->InputAt(0));
#ifdef V8_TARGET_ARCH_PPC64
        Address wasm_code = static_cast<Address>(constant.ToInt64());
#else
        Address wasm_code = static_cast<Address>(constant.ToInt32());
#endif
        __ Jump(wasm_code, constant.rmode());
      } else {
        __ Jump(i.InputRegister(0));
      }
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
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
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
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
      v8::internal::Assembler::BlockTrampolinePoolScope block_trampoline_pool(
          masm());
      Register func = i.InputRegister(0);
      if (v8_flags.debug_code) {
        // Check the function's context matches the context argument.
        __ LoadTaggedField(
            kScratchReg, FieldMemOperand(func, JSFunction::kContextOffset), r0);
        __ CmpS64(cp, kScratchReg);
        __ Assert(eq, AbortReason::kWrongFunctionContext);
      }
      __ CallJSFunction(func, kScratchReg);
      RecordCallPosition(instr);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
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
      int bytes = __ PushCallerSaved(fp_mode_, ip, r0, kReturnRegister0);
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
      int bytes = __ PopCallerSaved(fp_mode_, ip, r0, kReturnRegister0);
      frame_access_state()->IncreaseSPDelta(-(bytes / kSystemPointerSize));
      DCHECK_EQ(0, frame_access_state()->sp_delta());
      DCHECK(caller_registers_saved_);
      caller_registers_saved_ = false;
      break;
    }
    case kArchPrepareTailCall:
      AssemblePrepareTailCall();
      break;
    case kArchComment:
#ifdef V8_TARGET_ARCH_PPC64
      __ RecordComment(reinterpret_cast<const char*>(i.InputInt64(0)),
                       SourceLocation());
#else
      __ RecordComment(reinterpret_cast<const char*>(i.InputInt32(0)),
                       SourceLocation());
#endif
      break;
    case kArchCallCFunctionWithFrameState:
    case kArchCallCFunction: {
      int const num_gp_parameters = ParamField::decode(instr->opcode());
      int const fp_param_field = FPParamField::decode(instr->opcode());
      int num_fp_parameters = fp_param_field;
      bool has_function_descriptor = false;
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes;
#if ABI_USES_FUNCTION_DESCRIPTORS
      // AIX/PPC64BE Linux uses a function descriptor
      int kNumFPParametersMask = kHasFunctionDescriptorBitMask - 1;
      num_fp_parameters = kNumFPParametersMask & fp_param_field;
      has_function_descriptor =
          (fp_param_field & kHasFunctionDescriptorBitMask) != 0;
#endif
#if V8_ENABLE_WEBASSEMBLY
      Label start_call;
      int start_pc_offset = 0;
      bool isWasmCapiFunction =
          linkage()->GetIncomingDescriptor()->IsWasmCapiFunction();
      if (isWasmCapiFunction) {
        __ mflr(r0);
        __ LoadPC(kScratchReg);
        __ bind(&start_call);
        start_pc_offset = __ pc_offset();
        // We are going to patch this instruction after emitting
        // CallCFunction, using a zero offset here as placeholder for now.
        // patch_pc_address assumes `addi` is used here to
        // add the offset to pc.
        __ addi(kScratchReg, kScratchReg, Operand::Zero());
        __ StoreU64(kScratchReg,
                    MemOperand(fp, WasmExitFrameConstants::kCallingPCOffset));
        __ mtlr(r0);
        set_isolate_data_slots = SetIsolateDataSlots::kNo;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      int pc_offset;
      if (instr->InputAt(0)->IsImmediate()) {
        ExternalReference ref = i.InputExternalReference(0);
        pc_offset =
            __ CallCFunction(ref, num_gp_parameters, num_fp_parameters,
                             set_isolate_data_slots, has_function_descriptor);
      } else {
        Register func = i.InputRegister(0);
        pc_offset =
            __ CallCFunction(func, num_gp_parameters, num_fp_parameters,
                             set_isolate_data_slots, has_function_descriptor);
      }
#if V8_ENABLE_WEBASSEMBLY
      if (isWasmCapiFunction) {
        int offset_since_start_call = pc_offset - start_pc_offset;
        // Here we are going to patch the `addi` instruction above to use the
        // correct offset.
        // LoadPC emits two instructions and pc is the address of its second
        // emitted instruction. Add one more to the offset to point to after the
        // Call.
        offset_since_start_call += kInstrSize;
        __ patch_pc_address(kScratchReg, start_pc_offset,
                            offset_since_start_call);
      }
#endif  // V8_ENABLE_WEBASSEMBLY
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
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchBinarySearchSwitch:
      AssembleArchBinarySearchSwitch(instr);
      break;
    case kArchTableSwitch:
      AssembleArchTableSwitch(instr);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchAbortCSADcheck:
      DCHECK(i.InputRegister(0) == r4);
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
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchDeoptimize: {
      DeoptimizationExit* exit =
          BuildTranslation(instr, -1, 0, 0, OutputFrameStateCombine::Ignore());
      __ b(exit->label());
      break;
    }
    case kArchRet:
      AssembleReturn(instr->InputAt(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchFramePointer:
      __ mr(i.OutputRegister(), fp);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchParentFramePointer:
      if (frame_access_state()->has_frame()) {
        __ LoadU64(i.OutputRegister(), MemOperand(fp, 0));
      } else {
        __ mr(i.OutputRegister(), fp);
      }
      break;
#if V8_ENABLE_WEBASSEMBLY
    case kArchStackPointer:
      __ mr(i.OutputRegister(), sp);
      break;
    case kArchSetStackPointer: {
      DCHECK(instr->InputAt(0)->IsRegister());
      __ mr(sp, i.InputRegister(0));
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
        __ SubS64(lhs_register, sp, Operand(offset), kScratchReg);
      }

      constexpr size_t kValueIndex = 0;
      DCHECK(instr->InputAt(kValueIndex)->IsRegister());
      __ CmpU64(lhs_register, i.InputRegister(kValueIndex), cr0);
      break;
    }
    case kArchStackCheckOffset:
      __ LoadSmiLiteral(i.OutputRegister(),
                        Smi::FromInt(GetStackCheckOffset()));
      break;
    case kArchTruncateDoubleToI:
      __ TruncateDoubleToI(isolate(), zone(), i.OutputRegister(),
                           i.InputDoubleRegister(0), DetermineStubCallMode());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kArchStoreWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      // Indirect pointer writes must use a different opcode.
      DCHECK_NE(mode, RecordWriteMode::kValueIsIndirectPointer);
      Register object = i.InputRegister(0);
      Register value = i.InputRegister(2);
      Register scratch0 = i.TempRegister(0);
      Register scratch1 = i.TempRegister(1);
      OutOfLineRecordWrite* ool;

      if (v8_flags.debug_code) {
        // Checking that |value| is not a cleared weakref: our write barrier
        // does not support that for now.
        __ CmpS64(value, Operand(kClearedWeakHeapObjectLower32), kScratchReg);
        __ Check(ne, AbortReason::kOperandIsCleared);
      }

      AddressingMode addressing_mode =
          AddressingModeField::decode(instr->opcode());
      if (addressing_mode == kMode_MRI) {
        int32_t offset = i.InputInt32(1);
        ool = zone()->New<OutOfLineRecordWrite>(
            this, object, offset, value, scratch0, scratch1, mode,
            DetermineStubCallMode(), &unwinding_info_writer_);
        __ StoreTaggedField(value, MemOperand(object, offset), r0);
      } else {
        DCHECK_EQ(kMode_MRR, addressing_mode);
        Register offset(i.InputRegister(1));
        ool = zone()->New<OutOfLineRecordWrite>(
            this, object, offset, value, scratch0, scratch1, mode,
            DetermineStubCallMode(), &unwinding_info_writer_);
        __ StoreTaggedField(value, MemOperand(object, offset), r0);
      }
      // Skip the write barrier if the value is a Smi. However, this is only
      // valid if the value isn't an indirect pointer. Otherwise the value will
      // be a pointer table index, which will always look like a Smi (but
      // actually reference a pointer in the pointer table).
      if (mode > RecordWriteMode::kValueIsIndirectPointer) {
        __ JumpIfSmi(value, ool->exit());
      }
      __ CheckPageFlag(object, scratch0,
                       MemoryChunk::kPointersFromHereAreInterestingMask, ne,
                       ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchStoreIndirectWithWriteBarrier: {
      RecordWriteMode mode = RecordWriteModeField::decode(instr->opcode());
      Register scratch0 = i.TempRegister(0);
      Register scratch1 = i.TempRegister(1);
      OutOfLineRecordWrite* ool;
      DCHECK_EQ(mode, RecordWriteMode::kValueIsIndirectPointer);
      AddressingMode addressing_mode =
          AddressingModeField::decode(instr->opcode());
      Register object = i.InputRegister(0);
      Register value = i.InputRegister(2);
      IndirectPointerTag tag = static_cast<IndirectPointerTag>(i.InputInt64(3));
      DCHECK(IsValidIndirectPointerTag(tag));
      if (addressing_mode == kMode_MRI) {
        uint64_t offset = i.InputInt64(1);
        ool = zone()->New<OutOfLineRecordWrite>(
            this, object, offset, value, scratch0, scratch1, mode,
            DetermineStubCallMode(), &unwinding_info_writer_, tag);
        __ StoreIndirectPointerField(value, MemOperand(object, offset), r0);
      } else {
        DCHECK_EQ(addressing_mode, kMode_MRR);
        Register offset(i.InputRegister(1));
        ool = zone()->New<OutOfLineRecordWrite>(
            this, object, offset, value, scratch0, scratch1, mode,
            DetermineStubCallMode(), &unwinding_info_writer_, tag);
        __ StoreIndirectPointerField(value, MemOperand(object, offset), r0);
      }
      __ CheckPageFlag(object, scratch0,
                       MemoryChunk::kPointersFromHereAreInterestingMask, ne,
                       ool->entry());
      __ bind(ool->exit());
      break;
    }
    case kArchStackSlot: {
      FrameOffset offset =
          frame_access_state()->GetFrameOffset(i.InputInt32(0));
      __ AddS64(i.OutputRegister(), offset.from_stack_pointer() ? sp : fp,
                Operand(offset.offset()), r0);
      break;
    }
    case kPPC_Peek: {
      int reverse_slot = i.InputInt32(0);
      int offset =
          FrameSlotToFPOffset(frame()->GetTotalFrameSlotCount() - reverse_slot);
      if (instr->OutputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->OutputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ LoadF64(i.OutputDoubleRegister(), MemOperand(fp, offset), r0);
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ LoadF32(i.OutputFloatRegister(), MemOperand(fp, offset), r0);
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          __ LoadSimd128(i.OutputSimd128Register(), MemOperand(fp, offset),
                         kScratchReg);
        }
      } else {
        __ LoadU64(i.OutputRegister(), MemOperand(fp, offset), r0);
      }
      break;
    }
    case kPPC_Sync: {
      __ sync();
      break;
    }
    case kPPC_And:
      if (HasRegisterInput(instr, 1)) {
        __ and_(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.OutputRCBit());
      } else {
        __ andi(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1));
      }
      break;
    case kPPC_AndComplement:
      __ andc(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
              i.OutputRCBit());
      break;
    case kPPC_Or:
      if (HasRegisterInput(instr, 1)) {
        __ orx(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      } else {
        __ ori(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1));
        DCHECK_EQ(LeaveRC, i.OutputRCBit());
      }
      break;
    case kPPC_OrComplement:
      __ orc(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
             i.OutputRCBit());
      break;
    case kPPC_Xor:
      if (HasRegisterInput(instr, 1)) {
        __ xor_(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.OutputRCBit());
      } else {
        __ xori(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1));
        DCHECK_EQ(LeaveRC, i.OutputRCBit());
      }
      break;
    case kPPC_ShiftLeft32:
      ASSEMBLE_BINOP_RC(ShiftLeftU32, ShiftLeftU32);
      break;
    case kPPC_ShiftLeft64:
      ASSEMBLE_BINOP_RC(ShiftLeftU64, ShiftLeftU64);
      break;
    case kPPC_ShiftRight32:
      ASSEMBLE_BINOP_RC(ShiftRightU32, ShiftRightU32);
      break;
    case kPPC_ShiftRight64:
      ASSEMBLE_BINOP_RC(ShiftRightU64, ShiftRightU64);
      break;
    case kPPC_ShiftRightAlg32:
      ASSEMBLE_BINOP_INT_RC(ShiftRightS32, ShiftRightS32);
      break;
    case kPPC_ShiftRightAlg64:
      ASSEMBLE_BINOP_INT_RC(ShiftRightS64, ShiftRightS64);
      break;
#if !V8_TARGET_ARCH_PPC64
    case kPPC_AddPair:
      // i.InputRegister(0) ... left low word.
      // i.InputRegister(1) ... left high word.
      // i.InputRegister(2) ... right low word.
      // i.InputRegister(3) ... right high word.
      __ addc(i.OutputRegister(0), i.InputRegister(0), i.InputRegister(2));
      __ adde(i.OutputRegister(1), i.InputRegister(1), i.InputRegister(3));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_SubPair:
      // i.InputRegister(0) ... left low word.
      // i.InputRegister(1) ... left high word.
      // i.InputRegister(2) ... right low word.
      // i.InputRegister(3) ... right high word.
      __ subc(i.OutputRegister(0), i.InputRegister(0), i.InputRegister(2));
      __ sube(i.OutputRegister(1), i.InputRegister(1), i.InputRegister(3));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_MulPair:
      // i.InputRegister(0) ... left low word.
      // i.InputRegister(1) ... left high word.
      // i.InputRegister(2) ... right low word.
      // i.InputRegister(3) ... right high word.
      __ mullw(i.TempRegister(0), i.InputRegister(0), i.InputRegister(3));
      __ mullw(i.TempRegister(1), i.InputRegister(2), i.InputRegister(1));
      __ add(i.TempRegister(0), i.TempRegister(0), i.TempRegister(1));
      __ mullw(i.OutputRegister(0), i.InputRegister(0), i.InputRegister(2));
      __ mulhwu(i.OutputRegister(1), i.InputRegister(0), i.InputRegister(2));
      __ add(i.OutputRegister(1), i.OutputRegister(1), i.TempRegister(0));
      break;
    case kPPC_ShiftLeftPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsImmediate()) {
        __ ShiftLeftPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                         i.InputRegister(1), i.InputInt32(2));
      } else {
        __ ShiftLeftPair(i.OutputRegister(0), second_output, i.InputRegister(0),
                         i.InputRegister(1), kScratchReg, i.InputRegister(2));
      }
      break;
    }
    case kPPC_ShiftRightPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsImmediate()) {
        __ ShiftRightPair(i.OutputRegister(0), second_output,
                          i.InputRegister(0), i.InputRegister(1),
                          i.InputInt32(2));
      } else {
        __ ShiftRightPair(i.OutputRegister(0), second_output,
                          i.InputRegister(0), i.InputRegister(1), kScratchReg,
                          i.InputRegister(2));
      }
      break;
    }
    case kPPC_ShiftRightAlgPair: {
      Register second_output =
          instr->OutputCount() >= 2 ? i.OutputRegister(1) : i.TempRegister(0);
      if (instr->InputAt(2)->IsImmediate()) {
        __ ShiftRightAlgPair(i.OutputRegister(0), second_output,
                             i.InputRegister(0), i.InputRegister(1),
                             i.InputInt32(2));
      } else {
        __ ShiftRightAlgPair(i.OutputRegister(0), second_output,
                             i.InputRegister(0), i.InputRegister(1),
                             kScratchReg, i.InputRegister(2));
      }
      break;
    }
#endif
    case kPPC_RotRight32:
      if (HasRegisterInput(instr, 1)) {
        __ subfic(kScratchReg, i.InputRegister(1), Operand(32));
        __ rotlw(i.OutputRegister(), i.InputRegister(0), kScratchReg,
                 i.OutputRCBit());
      } else {
        int sh = i.InputInt32(1);
        __ rotrwi(i.OutputRegister(), i.InputRegister(0), sh, i.OutputRCBit());
      }
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_RotRight64:
      if (HasRegisterInput(instr, 1)) {
        __ subfic(kScratchReg, i.InputRegister(1), Operand(64));
        __ rotld(i.OutputRegister(), i.InputRegister(0), kScratchReg,
                 i.OutputRCBit());
      } else {
        int sh = i.InputInt32(1);
        __ rotrdi(i.OutputRegister(), i.InputRegister(0), sh, i.OutputRCBit());
      }
      break;
#endif
    case kPPC_Not:
      __ notx(i.OutputRegister(), i.InputRegister(0), i.OutputRCBit());
      break;
    case kPPC_RotLeftAndMask32:
      __ rlwinm(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1),
                31 - i.InputInt32(2), 31 - i.InputInt32(3), i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_RotLeftAndClear64:
      __ rldic(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1),
               63 - i.InputInt32(2), i.OutputRCBit());
      break;
    case kPPC_RotLeftAndClearLeft64:
      __ rldicl(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1),
                63 - i.InputInt32(2), i.OutputRCBit());
      break;
    case kPPC_RotLeftAndClearRight64:
      __ rldicr(i.OutputRegister(), i.InputRegister(0), i.InputInt32(1),
                63 - i.InputInt32(2), i.OutputRCBit());
      break;
#endif
    case kPPC_Add32:
#if V8_TARGET_ARCH_PPC64
      if (FlagsModeField::decode(instr->opcode()) != kFlags_none) {
        ASSEMBLE_ADD_WITH_OVERFLOW();
      } else {
#endif
        if (HasRegisterInput(instr, 1)) {
          __ add(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                 LeaveOE, i.OutputRCBit());
        } else {
          __ AddS64(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1),
                    r0, LeaveOE, i.OutputRCBit());
        }
        __ extsw(i.OutputRegister(), i.OutputRegister());
#if V8_TARGET_ARCH_PPC64
      }
#endif
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Add64:
      if (FlagsModeField::decode(instr->opcode()) != kFlags_none) {
        ASSEMBLE_ADD_WITH_OVERFLOW();
      } else {
        if (HasRegisterInput(instr, 1)) {
          __ add(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                 LeaveOE, i.OutputRCBit());
        } else {
          __ AddS64(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1),
                    r0, LeaveOE, i.OutputRCBit());
        }
      }
      break;
#endif
    case kPPC_AddWithOverflow32:
      ASSEMBLE_ADD_WITH_OVERFLOW32();
      break;
    case kPPC_AddDouble:
      ASSEMBLE_FLOAT_BINOP_RC(fadd, MiscField::decode(instr->opcode()));
      break;
    case kPPC_Sub:
#if V8_TARGET_ARCH_PPC64
      if (FlagsModeField::decode(instr->opcode()) != kFlags_none) {
        ASSEMBLE_SUB_WITH_OVERFLOW();
      } else {
#endif
        if (HasRegisterInput(instr, 1)) {
          __ sub(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                 LeaveOE, i.OutputRCBit());
        } else {
          __ SubS64(i.OutputRegister(), i.InputRegister(0), i.InputImmediate(1),
                    r0, LeaveOE, i.OutputRCBit());
        }
#if V8_TARGET_ARCH_PPC64
      }
#endif
      break;
    case kPPC_SubWithOverflow32:
      ASSEMBLE_SUB_WITH_OVERFLOW32();
      break;
    case kPPC_SubDouble:
      ASSEMBLE_FLOAT_BINOP_RC(fsub, MiscField::decode(instr->opcode()));
      break;
    case kPPC_Mul32:
      __ mullw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               LeaveOE, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Mul64:
      __ mulld(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               LeaveOE, i.OutputRCBit());
      break;
#endif

    case kPPC_Mul32WithHigh32:
      if (i.OutputRegister(0) == i.InputRegister(0) ||
          i.OutputRegister(0) == i.InputRegister(1) ||
          i.OutputRegister(1) == i.InputRegister(0) ||
          i.OutputRegister(1) == i.InputRegister(1)) {
        __ mullw(kScratchReg, i.InputRegister(0), i.InputRegister(1));  // low
        __ mulhw(i.OutputRegister(1), i.InputRegister(0),
                 i.InputRegister(1));  // high
        __ mr(i.OutputRegister(0), kScratchReg);
      } else {
        __ mullw(i.OutputRegister(0), i.InputRegister(0),
                 i.InputRegister(1));  // low
        __ mulhw(i.OutputRegister(1), i.InputRegister(0),
                 i.InputRegister(1));  // high
      }
      break;
    case kPPC_MulHighS64:
      __ mulhd(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      break;
    case kPPC_MulHighU64:
      __ mulhdu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.OutputRCBit());
      break;
    case kPPC_MulHigh32:
      __ mulhw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
               i.OutputRCBit());
      // High 32 bits are undefined and need to be cleared.
      CleanUInt32(i.OutputRegister());
      break;
    case kPPC_MulHighU32:
      __ mulhwu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1),
                i.OutputRCBit());
      // High 32 bits are undefined and need to be cleared.
      CleanUInt32(i.OutputRegister());
      break;
    case kPPC_MulDouble:
      ASSEMBLE_FLOAT_BINOP_RC(fmul, MiscField::decode(instr->opcode()));
      break;
    case kPPC_Div32:
      __ divw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Div64:
      __ divd(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#endif
    case kPPC_DivU32:
      __ divwu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_DivU64:
      __ divdu(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#endif
    case kPPC_DivDouble:
      ASSEMBLE_FLOAT_BINOP_RC(fdiv, MiscField::decode(instr->opcode()));
      break;
    case kPPC_Mod32:
      if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
        __ modsw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        ASSEMBLE_MODULO(divw, mullw);
      }
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Mod64:
      if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
        __ modsd(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        ASSEMBLE_MODULO(divd, mulld);
      }
      break;
#endif
    case kPPC_ModU32:
      if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
        __ moduw(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        ASSEMBLE_MODULO(divwu, mullw);
      }
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_ModU64:
      if (CpuFeatures::IsSupported(PPC_9_PLUS)) {
        __ modud(i.OutputRegister(), i.InputRegister(0), i.InputRegister(1));
      } else {
        ASSEMBLE_MODULO(divdu, mulld);
      }
      break;
#endif
    case kPPC_ModDouble:
      // TODO(bmeurer): We should really get rid of this special instruction,
      // and generate a CallAddress instruction instead.
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
    case kIeee754Float64Atan:
      ASSEMBLE_IEEE754_UNOP(atan);
      break;
    case kIeee754Float64Atan2:
      ASSEMBLE_IEEE754_BINOP(atan2);
      break;
    case kIeee754Float64Atanh:
      ASSEMBLE_IEEE754_UNOP(atanh);
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
    case kPPC_Neg:
      __ neg(i.OutputRegister(), i.InputRegister(0), LeaveOE, i.OutputRCBit());
      break;
    case kPPC_MaxDouble:
      __ MaxF64(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1), kScratchDoubleReg);
      break;
    case kPPC_MinDouble:
      __ MinF64(i.OutputDoubleRegister(), i.InputDoubleRegister(0),
                i.InputDoubleRegister(1), kScratchDoubleReg);
      break;
    case kPPC_AbsDouble:
      ASSEMBLE_FLOAT_UNOP_RC(fabs, 0);
      break;
    case kPPC_SqrtDouble:
      ASSEMBLE_FLOAT_UNOP_RC(fsqrt, MiscField::decode(instr->opcode()));
      break;
    case kPPC_FloorDouble:
      ASSEMBLE_FLOAT_UNOP_RC(frim, MiscField::decode(instr->opcode()));
      break;
    case kPPC_CeilDouble:
      ASSEMBLE_FLOAT_UNOP_RC(frip, MiscField::decode(instr->opcode()));
      break;
    case kPPC_TruncateDouble:
      ASSEMBLE_FLOAT_UNOP_RC(friz, MiscField::decode(instr->opcode()));
      break;
    case kPPC_RoundDouble:
      ASSEMBLE_FLOAT_UNOP_RC(frin, MiscField::decode(instr->opcode()));
      break;
    case kPPC_NegDouble:
      ASSEMBLE_FLOAT_UNOP_RC(fneg, 0);
      break;
    case kPPC_Cntlz32:
      __ cntlzw(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Cntlz64:
      __ cntlzd(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#endif
    case kPPC_Popcnt32:
      __ Popcnt32(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Popcnt64:
      __ Popcnt64(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#endif
    case kPPC_Cmp32:
      ASSEMBLE_COMPARE(cmpw, cmplw);
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Cmp64:
      ASSEMBLE_COMPARE(cmp, cmpl);
      break;
#endif
    case kPPC_CmpDouble:
      ASSEMBLE_FLOAT_COMPARE(fcmpu);
      break;
    case kPPC_Tst32:
      if (HasRegisterInput(instr, 1)) {
        __ and_(r0, i.InputRegister(0), i.InputRegister(1), i.OutputRCBit());
      } else {
        __ andi(r0, i.InputRegister(0), i.InputImmediate(1));
      }
#if V8_TARGET_ARCH_PPC64
      __ extsw(r0, r0, i.OutputRCBit());
#endif
      DCHECK_EQ(SetRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_Tst64:
      if (HasRegisterInput(instr, 1)) {
        __ and_(r0, i.InputRegister(0), i.InputRegister(1), i.OutputRCBit());
      } else {
        __ andi(r0, i.InputRegister(0), i.InputImmediate(1));
      }
      DCHECK_EQ(SetRC, i.OutputRCBit());
      break;
#endif
    case kPPC_Float64SilenceNaN: {
      DoubleRegister value = i.InputDoubleRegister(0);
      DoubleRegister result = i.OutputDoubleRegister();
      __ CanonicalizeNaN(result, value);
      break;
    }
    case kPPC_Push: {
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
          __ StoreF32WithUpdate(i.InputDoubleRegister(1),
                                MemOperand(sp, -kSystemPointerSize), r0);
          break;
        case MachineRepresentation::kFloat64:
          __ StoreF64WithUpdate(i.InputDoubleRegister(1),
                                MemOperand(sp, -kDoubleSize), r0);
          break;
        case MachineRepresentation::kSimd128:
          __ addi(sp, sp, Operand(-kSimd128Size));
          __ StoreSimd128(i.InputSimd128Register(1), MemOperand(r0, sp),
                          kScratchReg);
          break;
        default:
          __ StoreU64WithUpdate(i.InputRegister(1),
                                MemOperand(sp, -kSystemPointerSize), r0);
          break;
      }
      frame_access_state()->IncreaseSPDelta(slots);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kPPC_PushFrame: {
      int num_slots = i.InputInt32(1);
      if (instr->InputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->InputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ StoreF64WithUpdate(i.InputDoubleRegister(0),
                                MemOperand(sp, -num_slots * kSystemPointerSize),
                                r0);
        } else {
          DCHECK_EQ(MachineRepresentation::kFloat32, op->representation());
          __ StoreF32WithUpdate(i.InputDoubleRegister(0),
                                MemOperand(sp, -num_slots * kSystemPointerSize),
                                r0);
        }
      } else {
        __ StoreU64WithUpdate(i.InputRegister(0),
                              MemOperand(sp, -num_slots * kSystemPointerSize),
                              r0);
      }
      break;
    }
    case kPPC_StoreToStackSlot: {
      int slot = i.InputInt32(1);
      if (instr->InputAt(0)->IsFPRegister()) {
        LocationOperand* op = LocationOperand::cast(instr->InputAt(0));
        if (op->representation() == MachineRepresentation::kFloat64) {
          __ StoreF64(i.InputDoubleRegister(0),
                      MemOperand(sp, slot * kSystemPointerSize), r0);
        } else if (op->representation() == MachineRepresentation::kFloat32) {
          __ StoreF32(i.InputDoubleRegister(0),
                      MemOperand(sp, slot * kSystemPointerSize), r0);
        } else {
          DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
          __ StoreSimd128(i.InputSimd128Register(0),
                          MemOperand(sp, slot * kSystemPointerSize),
                          kScratchReg);
        }
      } else {
        __ StoreU64(i.InputRegister(0),
                    MemOperand(sp, slot * kSystemPointerSize), r0);
      }
      break;
    }
    case kPPC_ExtendSignWord8:
      __ extsb(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_ExtendSignWord16:
      __ extsh(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_ExtendSignWord32:
      __ extsw(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Uint32ToUint64:
      // Zero extend
      __ clrldi(i.OutputRegister(), i.InputRegister(0), Operand(32));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Int64ToInt32:
      __ extsw(i.OutputRegister(), i.InputRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Int64ToFloat32:
      __ ConvertInt64ToFloat(i.InputRegister(0), i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Int64ToDouble:
      __ ConvertInt64ToDouble(i.InputRegister(0), i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Uint64ToFloat32:
      __ ConvertUnsignedInt64ToFloat(i.InputRegister(0),
                                     i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Uint64ToDouble:
      __ ConvertUnsignedInt64ToDouble(i.InputRegister(0),
                                      i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
#endif
    case kPPC_Int32ToFloat32:
      __ ConvertIntToFloat(i.InputRegister(0), i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Int32ToDouble:
      __ ConvertIntToDouble(i.InputRegister(0), i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Uint32ToFloat32:
      __ ConvertUnsignedIntToFloat(i.InputRegister(0),
                                   i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Uint32ToDouble:
      __ ConvertUnsignedIntToDouble(i.InputRegister(0),
                                    i.OutputDoubleRegister());
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_Float32ToInt32: {
      bool set_overflow_to_min_i32 = MiscField::decode(instr->opcode());
      if (set_overflow_to_min_i32) {
        __ mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      }
      __ fctiwz(kScratchDoubleReg, i.InputDoubleRegister(0));
      __ MovDoubleLowToInt(i.OutputRegister(), kScratchDoubleReg);
      if (set_overflow_to_min_i32) {
        // Avoid INT32_MAX as an overflow indicator and use INT32_MIN instead,
        // because INT32_MIN allows easier out-of-bounds detection.
        CRegister cr = cr7;
        int crbit = v8::internal::Assembler::encode_crbit(
            cr, static_cast<CRBit>(VXCVI % CRWIDTH));
        __ mcrfs(cr, VXCVI);  // extract FPSCR field containing VXCVI into cr7
        __ li(kScratchReg, Operand(1));
        __ ShiftLeftU64(kScratchReg, kScratchReg,
                        Operand(31));  // generate INT32_MIN.
        __ isel(i.OutputRegister(0), kScratchReg, i.OutputRegister(0), crbit);
      }
      break;
    }
    case kPPC_Float32ToUint32: {
      bool set_overflow_to_min_u32 = MiscField::decode(instr->opcode());
      if (set_overflow_to_min_u32) {
        __ mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      }
      __ fctiwuz(kScratchDoubleReg, i.InputDoubleRegister(0));
      __ MovDoubleLowToInt(i.OutputRegister(), kScratchDoubleReg);
      if (set_overflow_to_min_u32) {
        // Avoid UINT32_MAX as an overflow indicator and use 0 instead,
        // because 0 allows easier out-of-bounds detection.
        CRegister cr = cr7;
        int crbit = v8::internal::Assembler::encode_crbit(
            cr, static_cast<CRBit>(VXCVI % CRWIDTH));
        __ mcrfs(cr, VXCVI);  // extract FPSCR field containing VXCVI into cr7
        __ li(kScratchReg, Operand::Zero());
        __ isel(i.OutputRegister(0), kScratchReg, i.OutputRegister(0), crbit);
      }
      break;
    }
#define DOUBLE_TO_INT32(op)                                                \
  bool check_conversion = i.OutputCount() > 1;                             \
  CRegister cr = cr7;                                                      \
  FPSCRBit fps_bit = VXCVI;                                                \
  int cr_bit = v8::internal::Assembler::encode_crbit(                      \
      cr, static_cast<CRBit>(fps_bit % CRWIDTH));                          \
  __ mtfsb0(fps_bit); /* clear FPSCR:VXCVI bit */                          \
  __ op(kScratchDoubleReg, i.InputDoubleRegister(0));                      \
  __ MovDoubleLowToInt(i.OutputRegister(0), kScratchDoubleReg);            \
  __ mcrfs(cr, VXCVI); /* extract FPSCR field containing VXCVI into cr7 */ \
  if (check_conversion) {                                                  \
    __ li(i.OutputRegister(1), Operand(1));                                \
    __ isel(i.OutputRegister(1), r0, i.OutputRegister(1), cr_bit);         \
  } else {                                                                 \
    __ isel(i.OutputRegister(0), r0, i.OutputRegister(0), cr_bit);         \
  }
    case kPPC_DoubleToInt32: {
      DOUBLE_TO_INT32(fctiwz)
      break;
    }
    case kPPC_DoubleToUint32: {
      DOUBLE_TO_INT32(fctiwuz)
      break;
    }
#undef DOUBLE_TO_INT32
    case kPPC_DoubleToInt64: {
#if V8_TARGET_ARCH_PPC64
      bool check_conversion = i.OutputCount() > 1;
      __ mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
#endif
      __ ConvertDoubleToInt64(i.InputDoubleRegister(0),
#if !V8_TARGET_ARCH_PPC64
                              kScratchReg,
#endif
                              i.OutputRegister(0), kScratchDoubleReg);
#if V8_TARGET_ARCH_PPC64
        CRegister cr = cr7;
        int crbit = v8::internal::Assembler::encode_crbit(
            cr, static_cast<CRBit>(VXCVI % CRWIDTH));
        __ mcrfs(cr, VXCVI);  // extract FPSCR field containing VXCVI into cr7
        // Handle conversion failures (such as overflow).
        if (CpuFeatures::IsSupported(PPC_7_PLUS)) {
          if (check_conversion) {
            __ li(i.OutputRegister(1), Operand(1));
            __ isel(i.OutputRegister(1), r0, i.OutputRegister(1), crbit);
          } else {
            __ isel(i.OutputRegister(0), r0, i.OutputRegister(0), crbit);
          }
        } else {
          if (check_conversion) {
            __ li(i.OutputRegister(1), Operand::Zero());
            __ bc(v8::internal::kInstrSize * 2, BT, crbit);
            __ li(i.OutputRegister(1), Operand(1));
          } else {
            __ mr(ip, i.OutputRegister(0));
            __ li(i.OutputRegister(0), Operand::Zero());
            __ bc(v8::internal::kInstrSize * 2, BT, crbit);
            __ mr(i.OutputRegister(0), ip);
          }
        }
#endif
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
#if V8_TARGET_ARCH_PPC64
    case kPPC_DoubleToUint64: {
      bool check_conversion = (i.OutputCount() > 1);
      if (check_conversion) {
        __ mtfsb0(VXCVI);  // clear FPSCR:VXCVI bit
      }
      __ ConvertDoubleToUnsignedInt64(i.InputDoubleRegister(0),
                                      i.OutputRegister(0), kScratchDoubleReg);
      if (check_conversion) {
        // Set 2nd output to zero if conversion fails.
        CRegister cr = cr7;
        int crbit = v8::internal::Assembler::encode_crbit(
            cr, static_cast<CRBit>(VXCVI % CRWIDTH));
        __ mcrfs(cr, VXCVI);  // extract FPSCR field containing VXCVI into cr7
        if (CpuFeatures::IsSupported(PPC_7_PLUS)) {
          __ li(i.OutputRegister(1), Operand(1));
          __ isel(i.OutputRegister(1), r0, i.OutputRegister(1), crbit);
        } else {
          __ li(i.OutputRegister(1), Operand::Zero());
          __ bc(v8::internal::kInstrSize * 2, BT, crbit);
          __ li(i.OutputRegister(1), Operand(1));
        }
      }
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
#endif
    case kPPC_DoubleToFloat32:
      ASSEMBLE_FLOAT_UNOP_RC(frsp, 0);
      break;
    case kPPC_Float32ToDouble:
      // Nothing to do.
      __ Move(i.OutputDoubleRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_DoubleExtractLowWord32:
      __ MovDoubleLowToInt(i.OutputRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_DoubleExtractHighWord32:
      __ MovDoubleHighToInt(i.OutputRegister(), i.InputDoubleRegister(0));
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_DoubleFromWord32Pair:
      __ clrldi(kScratchReg, i.InputRegister(1), Operand(32));
      __ ShiftLeftU64(i.TempRegister(0), i.InputRegister(0), Operand(32));
      __ OrU64(i.TempRegister(0), i.TempRegister(0), kScratchReg);
      __ MovInt64ToDouble(i.OutputDoubleRegister(), i.TempRegister(0));
      break;
    case kPPC_DoubleInsertLowWord32:
      __ InsertDoubleLow(i.OutputDoubleRegister(), i.InputRegister(1), r0);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_DoubleInsertHighWord32:
      __ InsertDoubleHigh(i.OutputDoubleRegister(), i.InputRegister(1), r0);
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_DoubleConstruct:
#if V8_TARGET_ARCH_PPC64
      __ MovInt64ComponentsToDouble(i.OutputDoubleRegister(),
                                    i.InputRegister(0), i.InputRegister(1), r0);
#else
      __ MovInt64ToDouble(i.OutputDoubleRegister(), i.InputRegister(0),
                          i.InputRegister(1));
#endif
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    case kPPC_BitcastFloat32ToInt32:
      __ MovFloatToInt(i.OutputRegister(), i.InputDoubleRegister(0),
                       kScratchDoubleReg);
      break;
    case kPPC_BitcastInt32ToFloat32:
      __ MovIntToFloat(i.OutputDoubleRegister(), i.InputRegister(0), ip);
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_BitcastDoubleToInt64:
      __ MovDoubleToInt64(i.OutputRegister(), i.InputDoubleRegister(0));
      break;
    case kPPC_BitcastInt64ToDouble:
      __ MovInt64ToDouble(i.OutputDoubleRegister(), i.InputRegister(0));
      break;
#endif
    case kPPC_LoadWordU8:
      ASSEMBLE_LOAD_INTEGER(lbz, plbz, lbzx, false);
      break;
    case kPPC_LoadWordS8:
      ASSEMBLE_LOAD_INTEGER(lbz, plbz, lbzx, false);
      __ extsb(i.OutputRegister(), i.OutputRegister());
      break;
    case kPPC_LoadWordU16:
      ASSEMBLE_LOAD_INTEGER(lhz, plhz, lhzx, false);
      break;
    case kPPC_LoadWordS16:
      ASSEMBLE_LOAD_INTEGER(lha, plha, lhax, false);
      break;
    case kPPC_LoadWordU32:
      ASSEMBLE_LOAD_INTEGER(lwz, plwz, lwzx, false);
      break;
    case kPPC_LoadWordS32:
      ASSEMBLE_LOAD_INTEGER(lwa, plwa, lwax, true);
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_LoadWord64:
      ASSEMBLE_LOAD_INTEGER(ld, pld, ldx, true);
      break;
#endif
    case kPPC_LoadFloat32:
      ASSEMBLE_LOAD_FLOAT(lfs, plfs, lfsx);
      break;
    case kPPC_LoadDouble:
      ASSEMBLE_LOAD_FLOAT(lfd, plfd, lfdx);
      break;
    case kPPC_LoadSimd128: {
      Simd128Register result = i.OutputSimd128Register();
      AddressingMode mode = kMode_None;
      MemOperand operand = i.MemoryOperand(&mode);
      bool is_atomic = i.InputInt32(2);
      DCHECK_EQ(mode, kMode_MRR);
      __ LoadSimd128(result, operand, kScratchReg);
      if (is_atomic) __ lwsync();
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kPPC_LoadReverseSimd128RR: {
      __ xxbrq(i.OutputSimd128Register(), i.InputSimd128Register(0));
      break;
    }
    case kPPC_StoreWord8:
      ASSEMBLE_STORE_INTEGER(stb, pstb, stbx, false);
      break;
    case kPPC_StoreWord16:
      ASSEMBLE_STORE_INTEGER(sth, psth, sthx, false);
      break;
    case kPPC_StoreWord32:
      ASSEMBLE_STORE_INTEGER(stw, pstw, stwx, false);
      break;
#if V8_TARGET_ARCH_PPC64
    case kPPC_StoreWord64:
      ASSEMBLE_STORE_INTEGER(std, pstd, stdx, true);
      break;
#endif
    case kPPC_StoreFloat32:
      ASSEMBLE_STORE_FLOAT(stfs, pstfs, stfsx);
      break;
    case kPPC_StoreDouble:
      ASSEMBLE_STORE_FLOAT(stfd, pstfd, stfdx);
      break;
    case kPPC_StoreSimd128: {
      size_t index = 0;
      AddressingMode mode = kMode_None;
      MemOperand operand = i.MemoryOperand(&mode, &index);
      Simd128Register value = i.InputSimd128Register(index);
      bool is_atomic = i.InputInt32(3);
      if (is_atomic) __ lwsync();
      DCHECK_EQ(mode, kMode_MRR);
      __ StoreSimd128(value, operand, kScratchReg);
      if (is_atomic) __ sync();
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kAtomicLoadInt8:
    case kAtomicLoadInt16:
      UNREACHABLE();
    case kAtomicExchangeInt8:
      __ AtomicExchange<int8_t>(
          MemOperand(i.InputRegister(0), i.InputRegister(1)),
          i.InputRegister(2), i.OutputRegister());
      break;
    case kPPC_AtomicExchangeUint8:
      __ AtomicExchange<uint8_t>(
          MemOperand(i.InputRegister(0), i.InputRegister(1)),
          i.InputRegister(2), i.OutputRegister());
      break;
    case kAtomicExchangeInt16: {
      ASSEMBLE_ATOMIC_EXCHANGE(int16_t, ByteReverseU16);
      __ extsh(i.OutputRegister(), i.OutputRegister());
      break;
    }
    case kPPC_AtomicExchangeUint16: {
      ASSEMBLE_ATOMIC_EXCHANGE(uint16_t, ByteReverseU16);
      break;
    }
    case kPPC_AtomicExchangeWord32: {
      ASSEMBLE_ATOMIC_EXCHANGE(uint32_t, ByteReverseU32);
      break;
    }
    case kPPC_AtomicExchangeWord64: {
      ASSEMBLE_ATOMIC_EXCHANGE(uint64_t, ByteReverseU64);
      break;
    }
    case kAtomicCompareExchangeInt8:
      __ AtomicCompareExchange<int8_t>(
          MemOperand(i.InputRegister(0), i.InputRegister(1)),
          i.InputRegister(2), i.InputRegister(3), i.OutputRegister(),
          kScratchReg);
      break;
    case kPPC_AtomicCompareExchangeUint8:
      __ AtomicCompareExchange<uint8_t>(
          MemOperand(i.InputRegister(0), i.InputRegister(1)),
          i.InputRegister(2), i.InputRegister(3), i.OutputRegister(),
          kScratchReg);
      break;
    case kAtomicCompareExchangeInt16: {
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE(int16_t, ByteReverseU16);
      __ extsh(i.OutputRegister(), i.OutputRegister());
      break;
    }
    case kPPC_AtomicCompareExchangeUint16: {
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE(uint16_t, ByteReverseU16);
      break;
    }
    case kPPC_AtomicCompareExchangeWord32: {
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE(uint32_t, ByteReverseU32);
      break;
    }
    case kPPC_AtomicCompareExchangeWord64: {
      ASSEMBLE_ATOMIC_COMPARE_EXCHANGE(uint64_t, ByteReverseU64);
    } break;

#define ATOMIC_BINOP_CASE(op, inst)                            \
  case kPPC_Atomic##op##Int8:                                  \
    ASSEMBLE_ATOMIC_BINOP_BYTE(inst, int8_t);                  \
    __ extsb(i.OutputRegister(), i.OutputRegister());          \
    break;                                                     \
  case kPPC_Atomic##op##Uint8:                                 \
    ASSEMBLE_ATOMIC_BINOP_BYTE(inst, uint8_t);                 \
    break;                                                     \
  case kPPC_Atomic##op##Int16:                                 \
    ASSEMBLE_ATOMIC_BINOP(inst, int16_t, ByteReverseU16, r0);  \
    __ extsh(i.OutputRegister(), i.OutputRegister());          \
    break;                                                     \
  case kPPC_Atomic##op##Uint16:                                \
    ASSEMBLE_ATOMIC_BINOP(inst, uint16_t, ByteReverseU16, r0); \
    break;                                                     \
  case kPPC_Atomic##op##Int32:                                 \
    ASSEMBLE_ATOMIC_BINOP(inst, int32_t, ByteReverseU32, r0);  \
    __ extsw(i.OutputRegister(), i.OutputRegister());          \
    break;                                                     \
  case kPPC_Atomic##op##Uint32:                                \
    ASSEMBLE_ATOMIC_BINOP(inst, uint32_t, ByteReverseU32, r0); \
    break;                                                     \
  case kPPC_Atomic##op##Int64:                                 \
  case kPPC_Atomic##op##Uint64:                                \
    ASSEMBLE_ATOMIC_BINOP(inst, uint64_t, ByteReverseU64, r0); \
    break;
      ATOMIC_BINOP_CASE(Add, add)
      ATOMIC_BINOP_CASE(Sub, sub)
      ATOMIC_BINOP_CASE(And, and_)
      ATOMIC_BINOP_CASE(Or, orx)
      ATOMIC_BINOP_CASE(Xor, xor_)
#undef ATOMIC_BINOP_CASE

    case kPPC_ByteRev32: {
      Register input = i.InputRegister(0);
      Register output = i.OutputRegister();
      Register temp1 = r0;
      if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
        __ brw(output, input);
        __ extsw(output, output);
        break;
      }
      __ rotlwi(temp1, input, 8);
      __ rlwimi(temp1, input, 24, 0, 7);
      __ rlwimi(temp1, input, 24, 16, 23);
      __ extsw(output, temp1);
      break;
    }
    case kPPC_LoadByteRev32: {
      ASSEMBLE_LOAD_INTEGER_RR(lwbrx);
      break;
    }
    case kPPC_StoreByteRev32: {
      ASSEMBLE_STORE_INTEGER_RR(stwbrx);
      break;
    }
    case kPPC_ByteRev64: {
      Register input = i.InputRegister(0);
      Register output = i.OutputRegister();
      Register temp1 = r0;
      Register temp2 = kScratchReg;
      Register temp3 = i.TempRegister(0);
      if (CpuFeatures::IsSupported(PPC_10_PLUS)) {
        __ brd(output, input);
        break;
      }
      __ rldicl(temp1, input, 32, 32);
      __ rotlwi(temp2, input, 8);
      __ rlwimi(temp2, input, 24, 0, 7);
      __ rotlwi(temp3, temp1, 8);
      __ rlwimi(temp2, input, 24, 16, 23);
      __ rlwimi(temp3, temp1, 24, 0, 7);
      __ rlwimi(temp3, temp1, 24, 16, 23);
      __ rldicr(temp2, temp2, 32, 31);
      __ orx(output, temp2, temp3);
      break;
    }
    case kPPC_LoadByteRev64: {
      ASSEMBLE_LOAD_INTEGER_RR(ldbrx);
      break;
    }
    case kPPC_StoreByteRev64: {
      ASSEMBLE_STORE_INTEGER_RR(stdbrx);
      break;
    }
// Simd Support.
#define SIMD_BINOP_LIST(V) \
  V(F64x2Add)              \
  V(F64x2Sub)              \
  V(F64x2Mul)              \
  V(F64x2Div)              \
  V(F64x2Eq)               \
  V(F64x2Lt)               \
  V(F64x2Le)               \
  V(F32x4Add)              \
  V(F32x4Sub)              \
  V(F32x4Mul)              \
  V(F32x4Div)              \
  V(F32x4Min)              \
  V(F32x4Max)              \
  V(F32x4Eq)               \
  V(F32x4Lt)               \
  V(F32x4Le)               \
  V(I64x2Add)              \
  V(I64x2Sub)              \
  V(I64x2Eq)               \
  V(I64x2GtS)              \
  V(I32x4Add)              \
  V(I32x4Sub)              \
  V(I32x4Mul)              \
  V(I32x4MinS)             \
  V(I32x4MinU)             \
  V(I32x4MaxS)             \
  V(I32x4MaxU)             \
  V(I32x4Eq)               \
  V(I32x4GtS)              \
  V(I32x4GtU)              \
  V(I32x4DotI16x8S)        \
  V(I16x8Add)              \
  V(I16x8Sub)              \
  V(I16x8Mul)              \
  V(I16x8MinS)             \
  V(I16x8MinU)             \
  V(I16x8MaxS)             \
  V(I16x8MaxU)             \
  V(I16x8Eq)               \
  V(I16x8GtS)              \
  V(I16x8GtU)              \
  V(I16x8AddSatS)          \
  V(I16x8SubSatS)          \
  V(I16x8AddSatU)          \
  V(I16x8SubSatU)          \
  V(I16x8SConvertI32x4)    \
  V(I16x8UConvertI32x4)    \
  V(I16x8RoundingAverageU) \
  V(I16x8Q15MulRSatS)      \
  V(I8x16Add)              \
  V(I8x16Sub)              \
  V(I8x16MinS)             \
  V(I8x16MinU)             \
  V(I8x16MaxS)             \
  V(I8x16MaxU)             \
  V(I8x16Eq)               \
  V(I8x16GtS)              \
  V(I8x16GtU)              \
  V(I8x16AddSatS)          \
  V(I8x16SubSatS)          \
  V(I8x16AddSatU)          \
  V(I8x16SubSatU)          \
  V(I8x16SConvertI16x8)    \
  V(I8x16UConvertI16x8)    \
  V(I8x16RoundingAverageU) \
  V(S128And)               \
  V(S128Or)                \
  V(S128Xor)               \
  V(S128AndNot)

#define EMIT_SIMD_BINOP(name)                                     \
  case kPPC_##name: {                                             \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.InputSimd128Register(1));                           \
    break;                                                        \
  }
      SIMD_BINOP_LIST(EMIT_SIMD_BINOP)
#undef EMIT_SIMD_BINOP
#undef SIMD_BINOP_LIST

#define SIMD_BINOP_WITH_SCRATCH_LIST(V) \
  V(F64x2Ne)                            \
  V(F64x2Pmin)                          \
  V(F64x2Pmax)                          \
  V(F32x4Ne)                            \
  V(F32x4Pmin)                          \
  V(F32x4Pmax)                          \
  V(I64x2Ne)                            \
  V(I64x2GeS)                           \
  V(I64x2ExtMulLowI32x4S)               \
  V(I64x2ExtMulHighI32x4S)              \
  V(I64x2ExtMulLowI32x4U)               \
  V(I64x2ExtMulHighI32x4U)              \
  V(I32x4Ne)                            \
  V(I32x4GeS)                           \
  V(I32x4GeU)                           \
  V(I32x4ExtMulLowI16x8S)               \
  V(I32x4ExtMulHighI16x8S)              \
  V(I32x4ExtMulLowI16x8U)               \
  V(I32x4ExtMulHighI16x8U)              \
  V(I16x8Ne)                            \
  V(I16x8GeS)                           \
  V(I16x8GeU)                           \
  V(I16x8ExtMulLowI8x16S)               \
  V(I16x8ExtMulHighI8x16S)              \
  V(I16x8ExtMulLowI8x16U)               \
  V(I16x8ExtMulHighI8x16U)              \
  V(I16x8DotI8x16S)                     \
  V(I8x16Ne)                            \
  V(I8x16GeS)                           \
  V(I8x16GeU)                           \
  V(I8x16Swizzle)

#define EMIT_SIMD_BINOP_WITH_SCRATCH(name)                        \
  case kPPC_##name: {                                             \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.InputSimd128Register(1), kScratchSimd128Reg);       \
    break;                                                        \
  }
      SIMD_BINOP_WITH_SCRATCH_LIST(EMIT_SIMD_BINOP_WITH_SCRATCH)
#undef EMIT_SIMD_BINOP_WITH_SCRATCH
#undef SIMD_BINOP_WITH_SCRATCH_LIST

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
  case kPPC_##name: {                                             \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.InputRegister(1), kScratchSimd128Reg);              \
    break;                                                        \
  }
      SIMD_SHIFT_LIST(EMIT_SIMD_SHIFT)
#undef EMIT_SIMD_SHIFT
#undef SIMD_SHIFT_LIST

#define SIMD_UNOP_LIST(V)   \
  V(F64x2Abs)               \
  V(F64x2Neg)               \
  V(F64x2Sqrt)              \
  V(F64x2Ceil)              \
  V(F64x2Floor)             \
  V(F64x2Trunc)             \
  V(F64x2PromoteLowF32x4)   \
  V(F32x4Abs)               \
  V(F32x4Neg)               \
  V(F32x4SConvertI32x4)     \
  V(F32x4UConvertI32x4)     \
  V(I64x2Neg)               \
  V(I32x4Neg)               \
  V(F32x4Sqrt)              \
  V(F32x4Ceil)              \
  V(F32x4Floor)             \
  V(F32x4Trunc)             \
  V(F64x2ConvertLowI32x4S)  \
  V(I64x2SConvertI32x4Low)  \
  V(I64x2SConvertI32x4High) \
  V(I32x4SConvertI16x8Low)  \
  V(I32x4SConvertI16x8High) \
  V(I32x4UConvertF32x4)     \
  V(I16x8SConvertI8x16Low)  \
  V(I16x8SConvertI8x16High) \
  V(I8x16Popcnt)            \
  V(S128Not)

#define EMIT_SIMD_UNOP(name)                                       \
  case kPPC_##name: {                                              \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0)); \
    break;                                                         \
  }
      SIMD_UNOP_LIST(EMIT_SIMD_UNOP)
#undef EMIT_SIMD_UNOP
#undef SIMD_UNOP_LIST

#define SIMD_UNOP_WITH_SCRATCH_LIST(V) \
  V(F32x4DemoteF64x2Zero)              \
  V(I64x2Abs)                          \
  V(I32x4Abs)                          \
  V(I32x4SConvertF32x4)                \
  V(I32x4TruncSatF64x2SZero)           \
  V(I32x4TruncSatF64x2UZero)           \
  V(I16x8Abs)                          \
  V(I16x8Neg)                          \
  V(I8x16Abs)                          \
  V(I8x16Neg)

#define EMIT_SIMD_UNOP_WITH_SCRATCH(name)                         \
  case kPPC_##name: {                                             \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            kScratchSimd128Reg);                                  \
    break;                                                        \
  }
      SIMD_UNOP_WITH_SCRATCH_LIST(EMIT_SIMD_UNOP_WITH_SCRATCH)
#undef EMIT_SIMD_UNOP_WITH_SCRATCH
#undef SIMD_UNOP_WITH_SCRATCH_LIST

#define SIMD_ALL_TRUE_LIST(V) \
  V(I64x2AllTrue)             \
  V(I32x4AllTrue)             \
  V(I16x8AllTrue)             \
  V(I8x16AllTrue)
#define EMIT_SIMD_ALL_TRUE(name)                                   \
  case kPPC_##name: {                                              \
    __ name(i.OutputRegister(), i.InputSimd128Register(0), r0, ip, \
            kScratchSimd128Reg);                                   \
    break;                                                         \
  }
      SIMD_ALL_TRUE_LIST(EMIT_SIMD_ALL_TRUE)
#undef EMIT_SIMD_ALL_TRUE
#undef SIMD_ALL_TRUE_LIST

#define SIMD_QFM_LIST(V) \
  V(F64x2Qfma)           \
  V(F64x2Qfms)           \
  V(F32x4Qfma)           \
  V(F32x4Qfms)
#define EMIT_SIMD_QFM(name)                                       \
  case kPPC_##name: {                                             \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            i.InputSimd128Register(1), i.InputSimd128Register(2), \
            kScratchSimd128Reg);                                  \
    break;                                                        \
  }
      SIMD_QFM_LIST(EMIT_SIMD_QFM)
#undef EMIT_SIMD_QFM
#undef SIMD_QFM_LIST

#define SIMD_EXT_ADD_PAIRWISE_LIST(V) \
  V(I32x4ExtAddPairwiseI16x8S)        \
  V(I32x4ExtAddPairwiseI16x8U)        \
  V(I16x8ExtAddPairwiseI8x16S)        \
  V(I16x8ExtAddPairwiseI8x16U)
#define EMIT_SIMD_EXT_ADD_PAIRWISE(name)                          \
  case kPPC_##name: {                                             \
    __ name(i.OutputSimd128Register(), i.InputSimd128Register(0), \
            kScratchSimd128Reg, kScratchSimd128Reg2);             \
    break;                                                        \
  }
      SIMD_EXT_ADD_PAIRWISE_LIST(EMIT_SIMD_EXT_ADD_PAIRWISE)
#undef EMIT_SIMD_EXT_ADD_PAIRWISE
#undef SIMD_EXT_ADD_PAIRWISE_LIST

#define SIMD_LOAD_LANE_LIST(V)    \
  V(S128Load64Lane, LoadLane64LE) \
  V(S128Load32Lane, LoadLane32LE) \
  V(S128Load16Lane, LoadLane16LE) \
  V(S128Load8Lane, LoadLane8LE)

#define EMIT_SIMD_LOAD_LANE(name, op)                                      \
  case kPPC_##name: {                                                      \
    Simd128Register dst = i.OutputSimd128Register();                       \
    DCHECK_EQ(dst, i.InputSimd128Register(0));                             \
    AddressingMode mode = kMode_None;                                      \
    size_t index = 1;                                                      \
    MemOperand operand = i.MemoryOperand(&mode, &index);                   \
    DCHECK_EQ(mode, kMode_MRR);                                            \
    __ op(dst, operand, i.InputUint8(3), kScratchReg, kScratchSimd128Reg); \
    break;                                                                 \
  }
      SIMD_LOAD_LANE_LIST(EMIT_SIMD_LOAD_LANE)
#undef EMIT_SIMD_LOAD_LANE
#undef SIMD_LOAD_LANE_LIST

#define SIMD_STORE_LANE_LIST(V)     \
  V(S128Store64Lane, StoreLane64LE) \
  V(S128Store32Lane, StoreLane32LE) \
  V(S128Store16Lane, StoreLane16LE) \
  V(S128Store8Lane, StoreLane8LE)

#define EMIT_SIMD_STORE_LANE(name, op)                                      \
  case kPPC_##name: {                                                       \
    AddressingMode mode = kMode_None;                                       \
    size_t index = 1;                                                       \
    MemOperand operand = i.MemoryOperand(&mode, &index);                    \
    DCHECK_EQ(mode, kMode_MRR);                                             \
    __ op(i.InputSimd128Register(0), operand, i.InputUint8(3), kScratchReg, \
          kScratchSimd128Reg);                                              \
    break;                                                                  \
  }
      SIMD_STORE_LANE_LIST(EMIT_SIMD_STORE_LANE)
#undef EMIT_SIMD_STORE_LANE
#undef SIMD_STORE_LANE_LIST

#define SIMD_LOAD_SPLAT(V)               \
  V(S128Load64Splat, LoadAndSplat64x2LE) \
  V(S128Load32Splat, LoadAndSplat32x4LE) \
  V(S128Load16Splat, LoadAndSplat16x8LE) \
  V(S128Load8Splat, LoadAndSplat8x16LE)

#define EMIT_SIMD_LOAD_SPLAT(name, op)                      \
  case kPPC_##name: {                                       \
    AddressingMode mode = kMode_None;                       \
    MemOperand operand = i.MemoryOperand(&mode);            \
    DCHECK_EQ(mode, kMode_MRR);                             \
    __ op(i.OutputSimd128Register(), operand, kScratchReg); \
    break;                                                  \
  }
      SIMD_LOAD_SPLAT(EMIT_SIMD_LOAD_SPLAT)
#undef EMIT_SIMD_LOAD_SPLAT
#undef SIMD_LOAD_SPLAT

    case kPPC_F64x2Splat: {
      __ F64x2Splat(i.OutputSimd128Register(), i.InputDoubleRegister(0),
                    kScratchReg);
      break;
    }
    case kPPC_F32x4Splat: {
      __ F32x4Splat(i.OutputSimd128Register(), i.InputDoubleRegister(0),
                    kScratchDoubleReg, kScratchReg);
      break;
    }
    case kPPC_I64x2Splat: {
      __ I64x2Splat(i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kPPC_I32x4Splat: {
      __ I32x4Splat(i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kPPC_I16x8Splat: {
      __ I16x8Splat(i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kPPC_I8x16Splat: {
      __ I8x16Splat(i.OutputSimd128Register(), i.InputRegister(0));
      break;
    }
    case kPPC_FExtractLane: {
      int lane_size = LaneSizeField::decode(instr->opcode());
      switch (lane_size) {
        case 32: {
          __ F32x4ExtractLane(i.OutputDoubleRegister(),
                              i.InputSimd128Register(0), i.InputInt8(1),
                              kScratchSimd128Reg, kScratchReg, ip);
          break;
        }
        case 64: {
          __ F64x2ExtractLane(i.OutputDoubleRegister(),
                              i.InputSimd128Register(0), i.InputInt8(1),
                              kScratchSimd128Reg, kScratchReg);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }
    case kPPC_IExtractLane: {
      int lane_size = LaneSizeField::decode(instr->opcode());
      switch (lane_size) {
        case 32: {
          __ I32x4ExtractLane(i.OutputRegister(), i.InputSimd128Register(0),
                              i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        case 64: {
          __ I64x2ExtractLane(i.OutputRegister(), i.InputSimd128Register(0),
                              i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }
    case kPPC_IExtractLaneU: {
      int lane_size = LaneSizeField::decode(instr->opcode());
      switch (lane_size) {
        case 8: {
          __ I8x16ExtractLaneU(i.OutputRegister(), i.InputSimd128Register(0),
                               i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        case 16: {
          __ I16x8ExtractLaneU(i.OutputRegister(), i.InputSimd128Register(0),
                               i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }
    case kPPC_IExtractLaneS: {
      int lane_size = LaneSizeField::decode(instr->opcode());
      switch (lane_size) {
        case 8: {
          __ I8x16ExtractLaneS(i.OutputRegister(), i.InputSimd128Register(0),
                               i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        case 16: {
          __ I16x8ExtractLaneS(i.OutputRegister(), i.InputSimd128Register(0),
                               i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }
    case kPPC_FReplaceLane: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      int lane_size = LaneSizeField::decode(instr->opcode());
      switch (lane_size) {
        case 32: {
          __ F32x4ReplaceLane(
              i.OutputSimd128Register(), i.InputSimd128Register(0),
              i.InputDoubleRegister(2), i.InputInt8(1), kScratchReg,
              kScratchDoubleReg, kScratchSimd128Reg);
          break;
        }
        case 64: {
          __ F64x2ReplaceLane(i.OutputSimd128Register(),
                              i.InputSimd128Register(0),
                              i.InputDoubleRegister(2), i.InputInt8(1),
                              kScratchReg, kScratchSimd128Reg);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }
    case kPPC_IReplaceLane: {
      DCHECK_EQ(i.OutputSimd128Register(), i.InputSimd128Register(0));
      int lane_size = LaneSizeField::decode(instr->opcode());
      switch (lane_size) {
        case 8: {
          __ I8x16ReplaceLane(i.OutputSimd128Register(),
                              i.InputSimd128Register(0), i.InputRegister(2),
                              i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        case 16: {
          __ I16x8ReplaceLane(i.OutputSimd128Register(),
                              i.InputSimd128Register(0), i.InputRegister(2),
                              i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        case 32: {
          __ I32x4ReplaceLane(i.OutputSimd128Register(),
                              i.InputSimd128Register(0), i.InputRegister(2),
                              i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        case 64: {
          __ I64x2ReplaceLane(i.OutputSimd128Register(),
                              i.InputSimd128Register(0), i.InputRegister(2),
                              i.InputInt8(1), kScratchSimd128Reg);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }
    case kPPC_I64x2Mul: {
      __ I64x2Mul(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), ip, r0,
                  i.ToRegister(instr->TempAt(0)), kScratchSimd128Reg);
      break;
    }
    case kPPC_F64x2Min: {
      __ F64x2Min(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchSimd128Reg,
                  kScratchSimd128Reg2);
      break;
    }
    case kPPC_F64x2Max: {
      __ F64x2Max(i.OutputSimd128Register(), i.InputSimd128Register(0),
                  i.InputSimd128Register(1), kScratchSimd128Reg,
                  kScratchSimd128Reg2);
      break;
    }
    case kPPC_S128Const: {
      uint64_t low = make_uint64(i.InputUint32(1), i.InputUint32(0));
      uint64_t high = make_uint64(i.InputUint32(3), i.InputUint32(2));
      __ S128Const(i.OutputSimd128Register(), high, low, r0, ip);
      break;
    }
    case kPPC_S128Zero: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vxor(dst, dst, dst);
      break;
    }
    case kPPC_S128AllOnes: {
      Simd128Register dst = i.OutputSimd128Register();
      __ vcmpequb(dst, dst, dst);
      break;
    }
    case kPPC_S128Select: {
      Simd128Register dst = i.OutputSimd128Register();
      Simd128Register mask = i.InputSimd128Register(0);
      Simd128Register src1 = i.InputSimd128Register(1);
      Simd128Register src2 = i.InputSimd128Register(2);
      __ S128Select(dst, src1, src2, mask);
      break;
    }
    case kPPC_V128AnyTrue: {
      __ V128AnyTrue(i.OutputRegister(), i.InputSimd128Register(0), r0, ip,
                     kScratchSimd128Reg);
      break;
    }
    case kPPC_F64x2ConvertLowI32x4U: {
      __ F64x2ConvertLowI32x4U(i.OutputSimd128Register(),
                               i.InputSimd128Register(0), kScratchReg,
                               kScratchSimd128Reg);
      break;
    }
    case kPPC_I64x2UConvertI32x4Low: {
      __ I64x2UConvertI32x4Low(i.OutputSimd128Register(),
                               i.InputSimd128Register(0), kScratchReg,
                               kScratchSimd128Reg);
      break;
    }
    case kPPC_I64x2UConvertI32x4High: {
      __ I64x2UConvertI32x4High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0), kScratchReg,
                                kScratchSimd128Reg);
      break;
    }
    case kPPC_I32x4UConvertI16x8Low: {
      __ I32x4UConvertI16x8Low(i.OutputSimd128Register(),
                               i.InputSimd128Register(0), kScratchReg,
                               kScratchSimd128Reg);
      break;
    }
    case kPPC_I32x4UConvertI16x8High: {
      __ I32x4UConvertI16x8High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0), kScratchReg,
                                kScratchSimd128Reg);
      break;
    }
    case kPPC_I16x8UConvertI8x16Low: {
      __ I16x8UConvertI8x16Low(i.OutputSimd128Register(),
                               i.InputSimd128Register(0), kScratchReg,
                               kScratchSimd128Reg);
      break;
    }
    case kPPC_I16x8UConvertI8x16High: {
      __ I16x8UConvertI8x16High(i.OutputSimd128Register(),
                                i.InputSimd128Register(0), kScratchReg,
                                kScratchSimd128Reg);
      break;
    }
    case kPPC_I8x16Shuffle: {
      uint64_t low = make_uint64(i.InputUint32(3), i.InputUint32(2));
      uint64_t high = make_uint64(i.InputUint32(5), i.InputUint32(4));
      __ I8x16Shuffle(i.OutputSimd128Register(), i.InputSimd128Register(0),
                      i.InputSimd128Register(1), high, low, r0, ip,
                      kScratchSimd128Reg);
      break;
    }
    case kPPC_I64x2BitMask: {
      __ I64x2BitMask(i.OutputRegister(), i.InputSimd128Register(0),
                      kScratchReg, kScratchSimd128Reg);
      break;
    }
    case kPPC_I32x4BitMask: {
      __ I32x4BitMask(i.OutputRegister(), i.InputSimd128Register(0),
                      kScratchReg, kScratchSimd128Reg);
      break;
    }
    case kPPC_I16x8BitMask: {
      __ I16x8BitMask(i.OutputRegister(), i.InputSimd128Register(0),
                      kScratchReg, kScratchSimd128Reg);
      break;
    }
    case kPPC_I8x16BitMask: {
      __ I8x16BitMask(i.OutputRegister(), i.InputSimd128Register(0), r0, ip,
                      kScratchSimd128Reg);
      break;
    }
    case kPPC_I32x4DotI8x16AddS: {
      __ I32x4DotI8x16AddS(i.OutputSimd128Register(), i.InputSimd128Register(0),
                           i.InputSimd128Register(1),
                           i.InputSimd128Register(2));
      break;
    }
#define PREP_LOAD_EXTEND()                     \
  AddressingMode mode = kMode_None;            \
  MemOperand operand = i.MemoryOperand(&mode); \
  DCHECK_EQ(mode, kMode_MRR);
    case kPPC_S128Load8x8S: {
      PREP_LOAD_EXTEND()
      __ LoadAndExtend8x8SLE(i.OutputSimd128Register(), operand, kScratchReg);
      break;
    }
    case kPPC_S128Load8x8U: {
      PREP_LOAD_EXTEND()
      __ LoadAndExtend8x8ULE(i.OutputSimd128Register(), operand, kScratchReg,
                             kScratchSimd128Reg);
      break;
    }
    case kPPC_S128Load16x4S: {
      PREP_LOAD_EXTEND()
      __ LoadAndExtend16x4SLE(i.OutputSimd128Register(), operand, kScratchReg);
      break;
    }
    case kPPC_S128Load16x4U: {
      PREP_LOAD_EXTEND()
      __ LoadAndExtend16x4ULE(i.OutputSimd128Register(), operand, kScratchReg,
                              kScratchSimd128Reg);
      break;
    }
    case kPPC_S128Load32x2S: {
      PREP_LOAD_EXTEND()
      __ LoadAndExtend32x2SLE(i.OutputSimd128Register(), operand, kScratchReg);
      break;
    }
    case kPPC_S128Load32x2U: {
      PREP_LOAD_EXTEND()
      __ LoadAndExtend32x2ULE(i.OutputSimd128Register(), operand, kScratchReg,
                              kScratchSimd128Reg);
      break;
    }
    case kPPC_S128Load32Zero: {
      PREP_LOAD_EXTEND()
      __ LoadV32ZeroLE(i.OutputSimd128Register(), operand, kScratchReg,
                       kScratchSimd128Reg);
      break;
    }
    case kPPC_S128Load64Zero: {
      PREP_LOAD_EXTEND()
      __ LoadV64ZeroLE(i.OutputSimd128Register(), operand, kScratchReg,
                       kScratchSimd128Reg);
      break;
    }
#undef PREP_LOAD_EXTEND
    case kPPC_StoreCompressTagged: {
      size_t index = 0;
      AddressingMode mode = kMode_None;
      MemOperand operand = i.MemoryOperand(&mode, &index);
      Register value = i.InputRegister(index);
      bool is_atomic = i.InputInt32(index + 1);
      if (is_atomic) __ lwsync();
      __ StoreTaggedField(value, operand, r0);
      if (is_atomic) __ sync();
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kPPC_StoreIndirectPointer: {
      size_t index = 0;
      AddressingMode mode = kMode_None;
      MemOperand mem = i.MemoryOperand(&mode, &index);
      Register value = i.InputRegister(index);
      bool is_atomic = i.InputInt32(index + 1);
      if (is_atomic) __ lwsync();
      __ StoreIndirectPointerField(value, mem, kScratchReg);
      if (is_atomic) __ sync();
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kPPC_LoadDecodeSandboxedPointer: {
      size_t index = 0;
      AddressingMode mode = kMode_None;
      MemOperand mem = i.MemoryOperand(&mode, &index);
      bool is_atomic = i.InputInt32(index);
      __ LoadSandboxedPointerField(i.OutputRegister(), mem, kScratchReg);
      if (is_atomic) __ lwsync();
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kPPC_StoreEncodeSandboxedPointer: {
      size_t index = 0;
      AddressingMode mode = kMode_None;
      MemOperand mem = i.MemoryOperand(&mode, &index);
      Register value = i.InputRegister(index);
      bool is_atomic = i.InputInt32(index + 1);
      if (is_atomic) __ lwsync();
      __ StoreSandboxedPointerField(value, mem, kScratchReg);
      if (is_atomic) __ sync();
      DCHECK_EQ(LeaveRC, i.OutputRCBit());
      break;
    }
    case kPPC_LoadDecompressTaggedSigned: {
      CHECK(instr->HasOutput());
      ASSEMBLE_LOAD_INTEGER(lwz, plwz, lwzx, false);
      break;
    }
    case kPPC_LoadDecompressTagged: {
      CHECK(instr->HasOutput());
      ASSEMBLE_LOAD_INTEGER(lwz, plwz, lwzx, false);
      __ add(i.OutputRegister(), i.OutputRegister(), kPtrComprCageBaseRegister);
      break;
    }
    default:
      UNREACHABLE();
  }
  return kSuccess;
}

// Assembles branches after an instruction.
void CodeGenerator::AssembleArchBranch(Instruction* instr, BranchInfo* branch) {
  PPCOperandConverter i(this, instr);
  Label* tlabel = branch->true_label;
  Label* flabel = branch->false_label;
  ArchOpcode op = instr->arch_opcode();
  FlagsCondition condition = branch->condition;
  CRegister cr = cr0;

  Condition cond = FlagsConditionToCondition(condition, op);
  if (op == kPPC_CmpDouble) {
    // check for unordered if necessary
    if (cond == le) {
      __ bunordered(flabel, cr);
      // Unnecessary for eq/lt since only FU bit will be set.
    } else if (cond == gt) {
      __ bunordered(tlabel, cr);
      // Unnecessary for ne/ge since only FU bit will be set.
    }
  }
  __ b(cond, tlabel, cr);
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
      PPCOperandConverter i(gen_, instr_);
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
  CRegister cr = cr0;
  Condition cond = FlagsConditionToCondition(condition, op);
  if (op == kPPC_CmpDouble) {
    // check for unordered if necessary
    if (cond == le) {
      __ bunordered(&end, cr);
      // Unnecessary for eq/lt since only FU bit will be set.
    } else if (cond == gt) {
      __ bunordered(tlabel, cr);
      // Unnecessary for ne/ge since only FU bit will be set.
    }
  }
  __ b(cond, tlabel, cr);
  __ bind(&end);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Assembles boolean materializations after an instruction.
void CodeGenerator::AssembleArchBoolean(Instruction* instr,
                                        FlagsCondition condition) {
  PPCOperandConverter i(this, instr);
  Label done;
  ArchOpcode op = instr->arch_opcode();
  CRegister cr = cr0;
  int reg_value = -1;

  // Materialize a full 32-bit 1 or 0 value. The result register is always the
  // last output of the instruction.
  DCHECK_NE(0u, instr->OutputCount());
  Register reg = i.OutputRegister(instr->OutputCount() - 1);

  Condition cond = FlagsConditionToCondition(condition, op);
  if (op == kPPC_CmpDouble) {
    // check for unordered if necessary
    if (cond == le) {
      reg_value = 0;
      __ li(reg, Operand::Zero());
      __ bunordered(&done, cr);
    } else if (cond == gt) {
      reg_value = 1;
      __ li(reg, Operand(1));
      __ bunordered(&done, cr);
    }
    // Unnecessary for eq/lt & ne/ge since only FU bit will be set.
  }

  if (CpuFeatures::IsSupported(PPC_7_PLUS)) {
    switch (cond) {
      case eq:
      case lt:
      case gt:
        if (reg_value != 1) __ li(reg, Operand(1));
        __ li(kScratchReg, Operand::Zero());
        __ isel(cond, reg, reg, kScratchReg, cr);
        break;
      case ne:
      case ge:
      case le:
        if (reg_value != 1) __ li(reg, Operand(1));
        // r0 implies logical zero in this form
        __ isel(NegateCondition(cond), reg, r0, reg, cr);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    if (reg_value != 0) __ li(reg, Operand::Zero());
    __ b(NegateCondition(cond), &done, cr);
    __ li(reg, Operand(1));
  }
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
  PPCOperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  std::vector<std::pair<int32_t, Label*>> cases;
  for (size_t index = 2; index < instr->InputCount(); index += 2) {
    cases.push_back({i.InputInt32(index + 0), GetLabel(i.InputRpo(index + 1))});
  }
  AssembleArchBinarySearchSwitchRange(input, i.InputRpo(1), cases.data(),
                                      cases.data() + cases.size());
}

void CodeGenerator::AssembleArchTableSwitch(Instruction* instr) {
  PPCOperandConverter i(this, instr);
  Register input = i.InputRegister(0);
  int32_t const case_count = static_cast<int32_t>(instr->InputCount() - 2);
  Label** cases = zone()->AllocateArray<Label*>(case_count);
  for (int32_t index = 0; index < case_count; ++index) {
    cases[index] = GetLabel(i.InputRpo(index + 2));
  }
  Label* const table = AddJumpTable(cases, case_count);
  __ CmpU64(input, Operand(case_count), r0);
  __ bge(GetLabel(i.InputRpo(1)));
  __ mov_label_addr(kScratchReg, table);
  __ ShiftLeftU64(r0, input, Operand(kSystemPointerSizeLog2));
  __ LoadU64(kScratchReg, MemOperand(kScratchReg, r0));
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
  const RegList saves =
      V8_EMBEDDED_CONSTANT_POOL_BOOL
          ? call_descriptor->CalleeSavedRegisters() - kConstantPoolRegister
          : call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {
    // register save area does not include the fp or constant pool pointer.
    const int num_saves =
        kNumCalleeSaved - 1 - (V8_EMBEDDED_CONSTANT_POOL_BOOL ? 1 : 0);
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
        __ addi(sp, sp, Operand(-kSystemPointerSize));
#else
      // For balance.
      if (false) {
#endif  // V8_ENABLE_WEBASSEMBLY
      } else {
        __ mflr(r0);
        if (V8_EMBEDDED_CONSTANT_POOL_BOOL) {
          __ Push(r0, fp, kConstantPoolRegister);
          // Adjust FP to point to saved FP.
          __ SubS64(fp, sp,
                    Operand(StandardFrameConstants::kConstantPoolOffset), r0);
        } else {
          __ Push(r0, fp);
          __ mr(fp, sp);
        }
      }
    } else if (call_descriptor->IsJSFunctionCall()) {
      __ Prologue();
    } else {
      StackFrame::Type type = info()->GetOutputStackFrameType();
      // TODO(mbrandy): Detect cases where ip is the entrypoint (for
      // efficient initialization of the constant pool pointer register).
      __ StubPrologue(type);
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
#if V8_EMBEDDED_CONSTANT_POOL_BOOL
        static_assert(WasmImportWrapperFrameConstants::kCentralStackSPOffset ==
                      -32);
        static_assert(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset == -40);
#else
        static_assert(WasmImportWrapperFrameConstants::kCentralStackSPOffset ==
                      -24);
        static_assert(
            WasmImportWrapperFrameConstants::kSecondaryStackLimitOffset == -32);
#endif
        __ mov(r0, Operand(0));
        __ push(r0);
        __ push(r0);
      } else if (call_descriptor->IsWasmCapiFunction()) {
        // Reserve space for saving the PC later.
        __ addi(sp, sp, Operand(-kSystemPointerSize));
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
  const RegList saves =
      V8_EMBEDDED_CONSTANT_POOL_BOOL
          ? call_descriptor->CalleeSavedRegisters() - kConstantPoolRegister
          : call_descriptor->CalleeSavedRegisters();

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
        Register stack_limit = ip;
        __ LoadStackLimit(stack_limit, StackLimitKind::kRealStackLimit, r0);
        __ AddS64(stack_limit, stack_limit,
                  Operand(required_slots * kSystemPointerSize), r0);
        __ CmpU64(sp, stack_limit);
        __ bge(&done);
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

    // Skip callee-saved and return slots, which are pushed below.
    required_slots -= saves.Count();
    required_slots -= frame()->GetReturnSlotCount();
    required_slots -= (kDoubleSize / kSystemPointerSize) * saves_fp.Count();
    __ AddS64(sp, sp, Operand(-required_slots * kSystemPointerSize), r0);
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
    __ AddS64(sp, sp, Operand(returns * kSystemPointerSize), r0);
  }

  // Restore registers.
  const RegList saves =
      V8_EMBEDDED_CONSTANT_POOL_BOOL
          ? call_descriptor->CalleeSavedRegisters() - kConstantPoolRegister
          : call_descriptor->CalleeSavedRegisters();
  if (!saves.is_empty()) {
    __ MultiPop(saves);
  }

  // Restore double registers.
  const DoubleRegList double_saves = call_descriptor->CalleeSavedFPRegisters();
  if (!double_saves.is_empty()) {
    __ MultiPopDoubles(double_saves);
  }

  unwinding_info_writer_.MarkBlockWillExit();

  PPCOperandConverter g(this, nullptr);
  const int parameter_slots =
      static_cast<int>(call_descriptor->ParameterSlotCount());

  // {aditional_pop_count} is only greater than zero if {parameter_slots = 0}.
  // Check RawMachineAssembler::PopAndReturn.
  if (parameter_slots != 0) {
    if (additional_pop_count->IsImmediate()) {
      DCHECK_EQ(g.ToConstant(additional_pop_count).ToInt32(), 0);
    } else if (v8_flags.debug_code) {
      __ cmpi(g.ToRegister(additional_pop_count), Operand(0));
      __ Assert(eq, AbortReason::kUnexpectedAdditionalPopValue);
    }
  }

  Register argc_reg = r6;
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
  // Constant pool is unavailable since the frame has been destructed
  ConstantPoolUnavailableScope constant_pool_unavailable(masm());
  if (drop_jsargs) {
    // We must pop all arguments from the stack (including the receiver).
    // The number of arguments without the receiver is
    // max(argc_reg, parameter_slots-1), and the receiver is added in
    // DropArguments().
    DCHECK(!call_descriptor->CalleeSavedRegisters().has(argc_reg));
    if (parameter_slots > 1) {
      Label skip;
      __ CmpS64(argc_reg, Operand(parameter_slots), r0);
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
    ZoneDeque<DeoptimizationExit*>* exits) {
  int total_size = 0;
  for (DeoptimizationExit* exit : deoptimization_exits_) {
    total_size += (exit->kind() == DeoptimizeKind::kLazy)
                      ? Deoptimizer::kLazyDeoptExitSize
                      : Deoptimizer::kEagerDeoptExitSize;
  }

  __ CheckTrampolinePoolQuick(total_size);
}

AllocatedOperand CodeGenerator::Push(InstructionOperand* source) {
  auto rep = LocationOperand::cast(source)->representation();
  int new_slots = ElementSizeInPointers(rep);
  PPCOperandConverter g(this, nullptr);
  int last_frame_slot_id =
      frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
  int sp_delta = frame_access_state_->sp_delta();
  int slot_id = last_frame_slot_id + sp_delta + new_slots;
  AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
  if (source->IsFloatStackSlot() || source->IsDoubleStackSlot()) {
    __ LoadU64(r0, g.ToMemOperand(source), r0);
    __ Push(r0);
    frame_access_state()->IncreaseSPDelta(new_slots);
  } else {
    // Bump the stack pointer and assemble the move.
    __ addi(sp, sp, Operand(-(new_slots * kSystemPointerSize)));
    frame_access_state()->IncreaseSPDelta(new_slots);
    AssembleMove(source, &stack_slot);
  }
  temp_slots_ += new_slots;
  return stack_slot;
}

void CodeGenerator::Pop(InstructionOperand* dest, MachineRepresentation rep) {
  int dropped_slots = ElementSizeInPointers(rep);
  PPCOperandConverter g(this, nullptr);
  if (dest->IsFloatStackSlot() || dest->IsDoubleStackSlot()) {
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    UseScratchRegisterScope temps(masm());
    Register scratch = temps.Acquire();
    __ Pop(scratch);
    __ StoreU64(scratch, g.ToMemOperand(dest), r0);
  } else {
    int last_frame_slot_id =
        frame_access_state_->frame()->GetTotalFrameSlotCount() - 1;
    int sp_delta = frame_access_state_->sp_delta();
    int slot_id = last_frame_slot_id + sp_delta;
    AllocatedOperand stack_slot(LocationOperand::STACK_SLOT, rep, slot_id);
    AssembleMove(&stack_slot, dest);
    frame_access_state()->IncreaseSPDelta(-dropped_slots);
    __ addi(sp, sp, Operand(dropped_slots * kSystemPointerSize));
  }
  temp_slots_ -= dropped_slots;
}

void CodeGenerator::PopTempStackSlots() {
  if (temp_slots_ > 0) {
    frame_access_state()->IncreaseSPDelta(-temp_slots_);
    __ addi(sp, sp, Operand(temp_slots_ * kSystemPointerSize));
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
    int scratch_reg_code;
    if (IsSimd128(rep)) {
      scratch_reg_code = kScratchSimd128Reg.code();
    } else if (IsFloatingPoint(rep)) {
      scratch_reg_code = kScratchDoubleReg.code();
    } else {
      scratch_reg_code = kScratchReg.code();
    }
    AllocatedOperand scratch(LocationOperand::REGISTER, rep, scratch_reg_code);
    DCHECK(!AreAliased(kScratchReg, r0, ip));
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
    DCHECK(!AreAliased(kScratchReg, r0, ip));
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
  PPCOperandConverter g(this, nullptr);
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
      __ StoreU64(src, g.ToMemOperand(destination), r0);
    }
  } else if (source->IsStackSlot()) {
    DCHECK(destination->IsRegister() || destination->IsStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsRegister()) {
      __ LoadU64(g.ToRegister(destination), src, r0);
    } else {
      Register temp = ip;
      __ LoadU64(temp, src, r0);
      __ StoreU64(temp, g.ToMemOperand(destination), r0);
    }
  } else if (source->IsConstant()) {
    Constant src = g.ToConstant(source);
    if (destination->IsRegister() || destination->IsStackSlot()) {
      Register dst = destination->IsRegister() ? g.ToRegister(destination) : ip;
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
            // TODO(v8:7703, jyan@ca.ibm.com): Turn into a
            // COMPRESSED_EMBEDDED_OBJECT when the constant pool entry size is
            // tagged size.
            __ Move(dst, src_object, RelocInfo::FULL_EMBEDDED_OBJECT);
          }
          break;
        }
        case Constant::kRpoNumber:
          UNREACHABLE();  // TODO(dcarney): loading RPO constants on PPC.
      }
      if (destination->IsStackSlot()) {
        __ StoreU64(dst, g.ToMemOperand(destination), r0);
      }
    } else {
      DoubleRegister dst = destination->IsFPRegister()
                               ? g.ToDoubleRegister(destination)
                               : kScratchDoubleReg;
      base::Double value;
#if V8_HOST_ARCH_IA32 || V8_HOST_ARCH_X64
      // casting double precision snan to single precision
      // converts it to qnan on ia32/x64
      if (src.type() == Constant::kFloat32) {
        uint32_t val = src.ToFloat32AsInt();
        if ((val & 0x7F800000) == 0x7F800000) {
          uint64_t dval = static_cast<uint64_t>(val);
          dval = ((dval & 0xC0000000) << 32) | ((dval & 0x40000000) << 31) |
                 ((dval & 0x40000000) << 30) | ((dval & 0x7FFFFFFF) << 29);
          value = base::Double(dval);
        } else {
          value = base::Double(static_cast<double>(src.ToFloat32()));
        }
      } else {
        value = base::Double(src.ToFloat64());
      }
#else
      value = src.type() == Constant::kFloat32
                  ? base::Double(static_cast<double>(src.ToFloat32()))
                  : base::Double(src.ToFloat64());
#endif
      __ LoadDoubleLiteral(dst, value, r0);
      if (destination->IsDoubleStackSlot()) {
        __ StoreF64(dst, g.ToMemOperand(destination), r0);
      } else if (destination->IsFloatStackSlot()) {
        __ StoreF32(dst, g.ToMemOperand(destination), r0);
      }
    }
  } else if (source->IsFPRegister()) {
    MachineRepresentation rep = LocationOperand::cast(source)->representation();
    if (rep == MachineRepresentation::kSimd128) {
      if (destination->IsSimd128Register()) {
        __ vor(g.ToSimd128Register(destination), g.ToSimd128Register(source),
               g.ToSimd128Register(source));
      } else {
        DCHECK(destination->IsSimd128StackSlot());
        MemOperand dst = g.ToMemOperand(destination);
        __ StoreSimd128(g.ToSimd128Register(source), dst, r0);
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
          __ StoreF64(src, g.ToMemOperand(destination), r0);
        } else {
          __ StoreF32(src, g.ToMemOperand(destination), r0);
        }
      }
    }
  } else if (source->IsFPStackSlot()) {
    DCHECK(destination->IsFPRegister() || destination->IsFPStackSlot());
    MemOperand src = g.ToMemOperand(source);
    if (destination->IsFPRegister()) {
      LocationOperand* op = LocationOperand::cast(source);
      if (op->representation() == MachineRepresentation::kFloat64) {
        __ LoadF64(g.ToDoubleRegister(destination), src, r0);
      } else if (op->representation() == MachineRepresentation::kFloat32) {
        __ LoadF32(g.ToDoubleRegister(destination), src, r0);
      } else {
        DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
        MemOperand src = g.ToMemOperand(source);
        __ LoadSimd128(g.ToSimd128Register(destination), src, r0);
      }
    } else {
      LocationOperand* op = LocationOperand::cast(source);
      DoubleRegister temp = kScratchDoubleReg;
      if (op->representation() == MachineRepresentation::kFloat64) {
        __ LoadF64(temp, src, r0);
        __ StoreF64(temp, g.ToMemOperand(destination), r0);
      } else if (op->representation() == MachineRepresentation::kFloat32) {
        __ LoadF32(temp, src, r0);
        __ StoreF32(temp, g.ToMemOperand(destination), r0);
      } else {
        DCHECK_EQ(MachineRepresentation::kSimd128, op->representation());
        MemOperand src = g.ToMemOperand(source);
        MemOperand dst = g.ToMemOperand(destination);
        __ LoadSimd128(kScratchSimd128Reg, src, r0);
        __ StoreSimd128(kScratchSimd128Reg, dst, r0);
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
  PPCOperandConverter g(this, nullptr);
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
                   kScratchDoubleReg, d0);
  } else if (source->IsDoubleStackSlot()) {
    DCHECK(destination->IsDoubleStackSlot());
    __ SwapDouble(g.ToMemOperand(source), g.ToMemOperand(destination),
                  kScratchDoubleReg, d0);

  } else if (source->IsSimd128Register()) {
    Simd128Register src = g.ToSimd128Register(source);
    if (destination->IsSimd128Register()) {
      __ SwapSimd128(src, g.ToSimd128Register(destination), kScratchSimd128Reg);
    } else {
      DCHECK(destination->IsSimd128StackSlot());
      __ SwapSimd128(src, g.ToMemOperand(destination), kScratchSimd128Reg,
                     kScratchReg);
    }
  } else if (source->IsSimd128StackSlot()) {
    DCHECK(destination->IsSimd128StackSlot());
    __ SwapSimd128(g.ToMemOperand(source), g.ToMemOperand(destination),
                   kScratchSimd128Reg, kScratchSimd128Reg2, kScratchReg);

  } else {
    UNREACHABLE();
  }

  return;
}

void CodeGenerator::AssembleJumpTable(Label** targets, size_t target_count) {
  for (size_t index = 0; index < target_count; ++index) {
    __ emit_label_addr(targets[index]);
  }
}

#undef __

}  // namespace compiler
}  // namespace internal
}  // namespace v8
