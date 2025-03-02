// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline-compiler.h"

#include <algorithm>
#include <optional>
#include <type_traits>

#include "src/base/bits.h"
#include "src/baseline/baseline-assembler-inl.h"
#include "src/baseline/baseline-assembler.h"
#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins-descriptors.h"
#include "src/builtins/builtins.h"
#include "src/codegen/assembler.h"
#include "src/codegen/compiler.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/heap/local-factory-inl.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/code.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/shared-function-info-inl.h"
#include "src/roots/roots.h"

#if V8_TARGET_ARCH_X64
#include "src/baseline/x64/baseline-compiler-x64-inl.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/baseline/arm64/baseline-compiler-arm64-inl.h"
#elif V8_TARGET_ARCH_IA32
#include "src/baseline/ia32/baseline-compiler-ia32-inl.h"
#elif V8_TARGET_ARCH_ARM
#include "src/baseline/arm/baseline-compiler-arm-inl.h"
#elif V8_TARGET_ARCH_PPC64
#include "src/baseline/ppc/baseline-compiler-ppc-inl.h"
#elif V8_TARGET_ARCH_S390X
#include "src/baseline/s390/baseline-compiler-s390-inl.h"
#elif V8_TARGET_ARCH_RISCV64
#include "src/baseline/riscv/baseline-compiler-riscv-inl.h"
#elif V8_TARGET_ARCH_RISCV32
#include "src/baseline/riscv/baseline-compiler-riscv-inl.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/baseline/mips64/baseline-compiler-mips64-inl.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/baseline/loong64/baseline-compiler-loong64-inl.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {
namespace baseline {

#define __ basm_.

#define RCS_BASELINE_SCOPE(rcs)                               \
  RCS_SCOPE(stats_,                                           \
            local_isolate_->is_main_thread()                  \
                ? RuntimeCallCounterId::kCompileBaseline##rcs \
                : RuntimeCallCounterId::kCompileBackgroundBaseline##rcs)

template <typename IsolateT>
Handle<TrustedByteArray> BytecodeOffsetTableBuilder::ToBytecodeOffsetTable(
    IsolateT* isolate) {
  if (bytes_.empty()) return isolate->factory()->empty_trusted_byte_array();
  Handle<TrustedByteArray> table =
      isolate->factory()->NewTrustedByteArray(static_cast<int>(bytes_.size()));
  MemCopy(table->begin(), bytes_.data(), bytes_.size());
  return table;
}

namespace detail {

#ifdef DEBUG
bool Clobbers(Register target, Register reg) { return target == reg; }
bool Clobbers(Register target, DirectHandle<Object> handle) { return false; }
bool Clobbers(Register target, Tagged<Smi> smi) { return false; }
bool Clobbers(Register target, Tagged<TaggedIndex> index) { return false; }
bool Clobbers(Register target, int32_t imm) { return false; }
bool Clobbers(Register target, RootIndex index) { return false; }
bool Clobbers(Register target, interpreter::Register reg) { return false; }
bool Clobbers(Register target, interpreter::RegisterList list) { return false; }

// We don't know what's inside machine registers or operands, so assume they
// match.
bool MachineTypeMatches(MachineType type, Register reg) { return true; }
bool MachineTypeMatches(MachineType type, MemOperand reg) { return true; }
bool MachineTypeMatches(MachineType type, DirectHandle<HeapObject> handle) {
  return type.IsTagged() && !type.IsTaggedSigned();
}
bool MachineTypeMatches(MachineType type, Tagged<Smi> handle) {
  return type.IsTagged() && !type.IsTaggedPointer();
}
bool MachineTypeMatches(MachineType type, Tagged<TaggedIndex> handle) {
  // Tagged<TaggedIndex> doesn't have a separate type, so check for the same
  // type as for Smis.
  return type.IsTagged() && !type.IsTaggedPointer();
}
bool MachineTypeMatches(MachineType type, int32_t imm) {
  // 32-bit immediates can be used for 64-bit params -- they'll be
  // zero-extended.
  return type.representation() == MachineRepresentation::kWord32 ||
         type.representation() == MachineRepresentation::kWord64;
}
bool MachineTypeMatches(MachineType type, RootIndex index) {
  return type.IsTagged() && !type.IsTaggedSigned();
}
bool MachineTypeMatches(MachineType type, interpreter::Register reg) {
  return type.IsTagged();
}

template <typename Descriptor, typename... Args>
struct CheckArgsHelper;

template <typename Descriptor>
struct CheckArgsHelper<Descriptor> {
  static void Check(BaselineAssembler* masm, int i) {
    if (Descriptor::AllowVarArgs()) {
      CHECK_GE(i, Descriptor::GetParameterCount());
    } else {
      CHECK_EQ(i, Descriptor::GetParameterCount());
    }
  }
};

template <typename Descriptor, typename Arg, typename... Args>
struct CheckArgsHelper<Descriptor, Arg, Args...> {
  static void Check(BaselineAssembler* masm, int i, Arg arg, Args... args) {
    if (i >= Descriptor::GetParameterCount()) {
      CHECK(Descriptor::AllowVarArgs());
      return;
    }
    CHECK(MachineTypeMatches(Descriptor().GetParameterType(i), arg));
    CheckArgsHelper<Descriptor, Args...>::Check(masm, i + 1, args...);
  }
};

template <typename Descriptor, typename... Args>
struct CheckArgsHelper<Descriptor, interpreter::RegisterList, Args...> {
  static void Check(BaselineAssembler* masm, int i,
                    interpreter::RegisterList list, Args... args) {
    for (int reg_index = 0; reg_index < list.register_count();
         ++reg_index, ++i) {
      if (i >= Descriptor::GetParameterCount()) {
        CHECK(Descriptor::AllowVarArgs());
        return;
      }
      CHECK(MachineTypeMatches(Descriptor().GetParameterType(i),
                               list[reg_index]));
    }
    CheckArgsHelper<Descriptor, Args...>::Check(masm, i, args...);
  }
};

template <typename Descriptor, typename... Args>
void CheckArgs(BaselineAssembler* masm, Args... args) {
  CheckArgsHelper<Descriptor, Args...>::Check(masm, 0, args...);
}

void CheckSettingDoesntClobber(Register target) {}
template <typename Arg, typename... Args>
void CheckSettingDoesntClobber(Register target, Arg arg, Args... args) {
  DCHECK(!Clobbers(target, arg));
  CheckSettingDoesntClobber(target, args...);
}

#else  // DEBUG

template <typename Descriptor, typename... Args>
void CheckArgs(Args... args) {}

template <typename... Args>
void CheckSettingDoesntClobber(Register target, Args... args) {}

#endif  // DEBUG

template <typename Descriptor, int ArgIndex, bool kIsRegister, typename... Args>
struct ArgumentSettingHelper;

template <typename Descriptor, int ArgIndex, bool kIsRegister>
struct ArgumentSettingHelper<Descriptor, ArgIndex, kIsRegister> {
  static void Set(BaselineAssembler* masm) {
    // Should only ever be called for the end of register arguments.
    static_assert(ArgIndex == Descriptor::GetRegisterParameterCount());
  }
};

template <typename Descriptor, int ArgIndex, typename Arg, typename... Args>
struct ArgumentSettingHelper<Descriptor, ArgIndex, true, Arg, Args...> {
  static void Set(BaselineAssembler* masm, Arg arg, Args... args) {
    static_assert(ArgIndex < Descriptor::GetRegisterParameterCount());
    Register target = Descriptor::GetRegisterParameter(ArgIndex);
    CheckSettingDoesntClobber(target, args...);
    masm->Move(target, arg);
    ArgumentSettingHelper<Descriptor, ArgIndex + 1,
                          (ArgIndex + 1 <
                           Descriptor::GetRegisterParameterCount()),
                          Args...>::Set(masm, args...);
  }
};

template <typename Descriptor, int ArgIndex>
struct ArgumentSettingHelper<Descriptor, ArgIndex, true,
                             interpreter::RegisterList> {
  static void Set(BaselineAssembler* masm, interpreter::RegisterList list) {
    static_assert(ArgIndex < Descriptor::GetRegisterParameterCount());
    DCHECK_EQ(ArgIndex + list.register_count(),
              Descriptor::GetRegisterParameterCount());
    for (int i = 0; ArgIndex + i < Descriptor::GetRegisterParameterCount();
         ++i) {
      Register target = Descriptor::GetRegisterParameter(ArgIndex + i);
      masm->Move(target, masm->RegisterFrameOperand(list[i]));
    }
  }
};

template <typename Descriptor, int ArgIndex, typename Arg, typename... Args>
struct ArgumentSettingHelper<Descriptor, ArgIndex, false, Arg, Args...> {
  static void Set(BaselineAssembler* masm, Arg arg, Args... args) {
    if (Descriptor::kStackArgumentOrder == StackArgumentOrder::kDefault) {
      masm->Push(arg, args...);
    } else {
      masm->PushReverse(arg, args...);
    }
  }
};

template <Builtin kBuiltin, typename... Args>
void MoveArgumentsForBuiltin(BaselineAssembler* masm, Args... args) {
  using Descriptor = typename CallInterfaceDescriptorFor<kBuiltin>::type;
  CheckArgs<Descriptor>(masm, args...);
  ArgumentSettingHelper<Descriptor, 0,
                        (0 < Descriptor::GetRegisterParameterCount()),
                        Args...>::Set(masm, args...);
  if (Descriptor::HasContextParameter()) {
    masm->LoadContext(Descriptor::ContextRegister());
  }
}

}  // namespace detail

namespace {

AssemblerOptions BaselineAssemblerOptions(Isolate* isolate) {
  AssemblerOptions options = AssemblerOptions::Default(isolate);
  options.builtin_call_jump_mode =
      isolate->is_short_builtin_calls_enabled()
          ? BuiltinCallJumpMode::kPCRelative
          : kFallbackBuiltinCallJumpModeForBaseline;
  return options;
}

// Rough upper-bound estimate. Copying the data is most likely more expensive
// than pre-allocating a large enough buffer.
#ifdef V8_TARGET_ARCH_IA32
const int kAverageBytecodeToInstructionRatio = 5;
#else
const int kAverageBytecodeToInstructionRatio = 7;
#endif
std::unique_ptr<AssemblerBuffer> AllocateBuffer(
    DirectHandle<BytecodeArray> bytecodes) {
  int estimated_size;
  {
    DisallowHeapAllocation no_gc;
    estimated_size = BaselineCompiler::EstimateInstructionSize(*bytecodes);
  }
  return NewAssemblerBuffer(RoundUp(estimated_size, 4 * KB));
}
}  // namespace

BaselineCompiler::BaselineCompiler(
    LocalIsolate* local_isolate,
    Handle<SharedFunctionInfo> shared_function_info,
    Handle<BytecodeArray> bytecode)
    : local_isolate_(local_isolate),
      stats_(local_isolate->runtime_call_stats()),
      shared_function_info_(shared_function_info),
      bytecode_(bytecode),
      zone_(local_isolate->allocator(), ZONE_NAME),
      masm_(
          local_isolate->GetMainThreadIsolateUnsafe(), &zone_,
          BaselineAssemblerOptions(local_isolate->GetMainThreadIsolateUnsafe()),
          CodeObjectRequired::kNo, AllocateBuffer(bytecode)),
      basm_(&masm_),
      iterator_(bytecode_),
      labels_(zone_.AllocateArray<Label>(bytecode_->length())),
      label_tags_(2 * bytecode_->length(), &zone_) {
  // Empirically determined expected size of the offset table at the 95th %ile,
  // based on the size of the bytecode, to be:
  //
  //   16 + (bytecode size) / 4
  bytecode_offset_table_builder_.Reserve(
      base::bits::RoundUpToPowerOfTwo(16 + bytecode_->Size() / 4));
}

void BaselineCompiler::GenerateCode() {
  {
    RCS_BASELINE_SCOPE(PreVisit);
    // Mark exception handlers as valid indirect jump targets. This is required
    // when CFI is enabled, to allow indirect jumps into baseline code.
    HandlerTable table(*bytecode_);
    for (int i = 0; i < table.NumberOfRangeEntries(); ++i) {
      MarkIndirectJumpTarget(table.GetRangeHandler(i));
    }
    for (; !iterator_.done(); iterator_.Advance()) {
      PreVisitSingleBytecode();
    }
    iterator_.Reset();
  }

  // No code generated yet.
  DCHECK_EQ(__ pc_offset(), 0);
  __ CodeEntry();

  {
    RCS_BASELINE_SCOPE(Visit);
    Prologue();
    AddPosition();
    for (; !iterator_.done(); iterator_.Advance()) {
      VisitSingleBytecode();
      AddPosition();
    }
  }
}

MaybeHandle<Code> BaselineCompiler::Build() {
  RCS_BASELINE_SCOPE(Build);
  CodeDesc desc;
  __ GetCode(local_isolate_, &desc);

  // Allocate the bytecode offset table.
  Handle<TrustedByteArray> bytecode_offset_table =
      bytecode_offset_table_builder_.ToBytecodeOffsetTable(local_isolate_);

  Factory::CodeBuilder code_builder(local_isolate_, desc, CodeKind::BASELINE);
  code_builder.set_bytecode_offset_table(bytecode_offset_table);
  if (shared_function_info_->HasInterpreterData(local_isolate_)) {
    code_builder.set_interpreter_data(
        handle(shared_function_info_->interpreter_data(local_isolate_),
               local_isolate_));
  } else {
    code_builder.set_interpreter_data(bytecode_);
  }
  code_builder.set_parameter_count(bytecode_->parameter_count());
  return code_builder.TryBuild();
}

int BaselineCompiler::EstimateInstructionSize(Tagged<BytecodeArray> bytecode) {
  return bytecode->length() * kAverageBytecodeToInstructionRatio;
}

interpreter::Register BaselineCompiler::RegisterOperand(int operand_index) {
  return iterator().GetRegisterOperand(operand_index);
}

void BaselineCompiler::LoadRegister(Register output, int operand_index) {
  __ LoadRegister(output, RegisterOperand(operand_index));
}

void BaselineCompiler::StoreRegister(int operand_index, Register value) {
#ifdef DEBUG
  effect_state_.CheckEffect();
#endif
  __ Move(RegisterOperand(operand_index), value);
}

void BaselineCompiler::StoreRegisterPair(int operand_index, Register val0,
                                         Register val1) {
#ifdef DEBUG
  effect_state_.CheckEffect();
#endif
  interpreter::Register reg0, reg1;
  std::tie(reg0, reg1) = iterator().GetRegisterPairOperand(operand_index);
  __ StoreRegister(reg0, val0);
  __ StoreRegister(reg1, val1);
}
template <typename Type>
Handle<Type> BaselineCompiler::Constant(int operand_index) {
  return Cast<Type>(
      iterator().GetConstantForIndexOperand(operand_index, local_isolate_));
}
Tagged<Smi> BaselineCompiler::ConstantSmi(int operand_index) {
  return iterator().GetConstantAtIndexAsSmi(operand_index);
}
template <typename Type>
void BaselineCompiler::LoadConstant(Register output, int operand_index) {
  __ Move(output, Constant<Type>(operand_index));
}
uint32_t BaselineCompiler::Uint(int operand_index) {
  return iterator().GetUnsignedImmediateOperand(operand_index);
}
int32_t BaselineCompiler::Int(int operand_index) {
  return iterator().GetImmediateOperand(operand_index);
}
uint32_t BaselineCompiler::Index(int operand_index) {
  return iterator().GetIndexOperand(operand_index);
}
uint32_t BaselineCompiler::Flag8(int operand_index) {
  return iterator().GetFlag8Operand(operand_index);
}
uint32_t BaselineCompiler::Flag16(int operand_index) {
  return iterator().GetFlag16Operand(operand_index);
}
uint32_t BaselineCompiler::RegisterCount(int operand_index) {
  return iterator().GetRegisterCountOperand(operand_index);
}
Tagged<TaggedIndex> BaselineCompiler::IndexAsTagged(int operand_index) {
  return TaggedIndex::FromIntptr(Index(operand_index));
}
Tagged<TaggedIndex> BaselineCompiler::UintAsTagged(int operand_index) {
  return TaggedIndex::FromIntptr(Uint(operand_index));
}
Tagged<Smi> BaselineCompiler::IndexAsSmi(int operand_index) {
  return Smi::FromInt(Index(operand_index));
}
Tagged<Smi> BaselineCompiler::IntAsSmi(int operand_index) {
  return Smi::FromInt(Int(operand_index));
}
Tagged<Smi> BaselineCompiler::UintAsSmi(int operand_index) {
  return Smi::FromInt(Uint(operand_index));
}
Tagged<Smi> BaselineCompiler::Flag8AsSmi(int operand_index) {
  return Smi::FromInt(Flag8(operand_index));
}
Tagged<Smi> BaselineCompiler::Flag16AsSmi(int operand_index) {
  return Smi::FromInt(Flag16(operand_index));
}

MemOperand BaselineCompiler::FeedbackVector() {
  return __ FeedbackVectorOperand();
}

void BaselineCompiler::LoadFeedbackVector(Register output) {
  ASM_CODE_COMMENT(&masm_);
  __ Move(output, __ FeedbackVectorOperand());
}

void BaselineCompiler::LoadClosureFeedbackArray(Register output) {
  LoadFeedbackVector(output);
  __ LoadTaggedField(output, output,
                     FeedbackVector::kClosureFeedbackCellArrayOffset);
}

void BaselineCompiler::SelectBooleanConstant(
    Register output, std::function<void(Label*, Label::Distance)> jump_func) {
  Label done, set_true;
  jump_func(&set_true, Label::kNear);
  __ LoadRoot(output, RootIndex::kFalseValue);
  __ Jump(&done, Label::kNear);
  __ Bind(&set_true);
  __ LoadRoot(output, RootIndex::kTrueValue);
  __ Bind(&done);
}

void BaselineCompiler::AddPosition() {
  bytecode_offset_table_builder_.AddPosition(__ pc_offset());
}

void BaselineCompiler::PreVisitSingleBytecode() {
  switch (iterator().current_bytecode()) {
    case interpreter::Bytecode::kJumpLoop:
      EnsureLabel(iterator().GetJumpTargetOffset(),
                  MarkAsIndirectJumpTarget::kYes);
      break;
    default:
      break;
  }
}

void BaselineCompiler::VisitSingleBytecode() {
#ifdef DEBUG
  effect_state_.clear();
#endif
  int offset = iterator().current_offset();
  if (IsJumpTarget(offset)) __ Bind(&labels_[offset]);
  // This is required when CFI is enabled.
  if (IsIndirectJumpTarget(offset)) {
    __ JumpTarget();
  }

  ASM_CODE_COMMENT_STRING(&masm_, [&]() {
    std::ostringstream str;
    iterator().PrintTo(str);
    return str.str();
  });

  VerifyFrame();

#ifdef V8_TRACE_UNOPTIMIZED
  TraceBytecode(Runtime::kTraceUnoptimizedBytecodeEntry);
#endif

  {
    interpreter::Bytecode bytecode = iterator().current_bytecode();

#ifdef DEBUG
    std::optional<EnsureAccumulatorPreservedScope> accumulator_preserved_scope;
    // We should make sure to preserve the accumulator whenever the bytecode
    // isn't registered as writing to it. We can't do this for jumps or switches
    // though, since the control flow would not match the control flow of this
    // scope.
    if (v8_flags.slow_debug_code &&
        !interpreter::Bytecodes::WritesOrClobbersAccumulator(bytecode) &&
        !interpreter::Bytecodes::IsJump(bytecode) &&
        !interpreter::Bytecodes::IsSwitch(bytecode)) {
      accumulator_preserved_scope.emplace(&basm_);
    }
#endif  // DEBUG

    switch (bytecode) {
#define BYTECODE_CASE(name, ...)       \
  case interpreter::Bytecode::k##name: \
    Visit##name();                     \
    break;
      BYTECODE_LIST(BYTECODE_CASE, BYTECODE_CASE)
#undef BYTECODE_CASE
    }
  }

#ifdef V8_TRACE_UNOPTIMIZED
  TraceBytecode(Runtime::kTraceUnoptimizedBytecodeExit);
#endif
}

void BaselineCompiler::VerifyFrame() {
  if (v8_flags.slow_debug_code) {
    ASM_CODE_COMMENT(&masm_);
    __ RecordComment(" -- Verify frame size");
    VerifyFrameSize();

    __ RecordComment(" -- Verify feedback vector");
    {
      BaselineAssembler::ScratchRegisterScope temps(&basm_);
      Register scratch = temps.AcquireScratch();
      __ Move(scratch, __ FeedbackVectorOperand());
      Label is_smi, is_ok;
      __ JumpIfSmi(scratch, &is_smi);
      __ JumpIfObjectTypeFast(kEqual, scratch, FEEDBACK_VECTOR_TYPE, &is_ok);
      __ Bind(&is_smi);
      __ masm()->Abort(AbortReason::kExpectedFeedbackVector);
      __ Bind(&is_ok);
    }

    // TODO(leszeks): More verification.
  }
}

#ifdef V8_TRACE_UNOPTIMIZED
void BaselineCompiler::TraceBytecode(Runtime::FunctionId function_id) {
  if (!v8_flags.trace_baseline_exec) return;
  ASM_CODE_COMMENT_STRING(&masm_,
                          function_id == Runtime::kTraceUnoptimizedBytecodeEntry
                              ? "Trace bytecode entry"
                              : "Trace bytecode exit");
  SaveAccumulatorScope accumulator_scope(this, &basm_);
  CallRuntime(function_id, bytecode_,
              Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag +
                           iterator().current_offset()),
              kInterpreterAccumulatorRegister);
}
#endif

#define DECLARE_VISITOR(name, ...) void Visit##name();
BYTECODE_LIST(DECLARE_VISITOR, DECLARE_VISITOR)
#undef DECLARE_VISITOR

#define DECLARE_VISITOR(name, ...) \
  void VisitIntrinsic##name(interpreter::RegisterList args);
INTRINSICS_LIST(DECLARE_VISITOR)
#undef DECLARE_VISITOR

void BaselineCompiler::UpdateInterruptBudgetAndJumpToLabel(
    int weight, Label* label, Label* skip_interrupt_label,
    StackCheckBehavior stack_check_behavior) {
  if (weight != 0) {
    ASM_CODE_COMMENT(&masm_);
    __ AddToInterruptBudgetAndJumpIfNotExceeded(weight, skip_interrupt_label);

    DCHECK_LT(weight, 0);
    CallRuntime(stack_check_behavior == kEnableStackCheck
                    ? Runtime::kBytecodeBudgetInterruptWithStackCheck_Sparkplug
                    : Runtime::kBytecodeBudgetInterrupt_Sparkplug,
                __ FunctionOperand());
  }
  if (label) __ Jump(label);
}

void BaselineCompiler::JumpIfRoot(RootIndex root) {
  Label dont_jump;
  __ JumpIfNotRoot(kInterpreterAccumulatorRegister, root, &dont_jump,
                   Label::kNear);
  __ Jump(BuildForwardJumpLabel());
  __ Bind(&dont_jump);
}

void BaselineCompiler::JumpIfNotRoot(RootIndex root) {
  Label dont_jump;
  __ JumpIfRoot(kInterpreterAccumulatorRegister, root, &dont_jump,
                Label::kNear);
  __ Jump(BuildForwardJumpLabel());
  __ Bind(&dont_jump);
}

Label* BaselineCompiler::BuildForwardJumpLabel() {
  int target_offset = iterator().GetJumpTargetOffset();
  return EnsureLabel(target_offset);
}

#if defined(DEBUG) || defined(V8_ENABLE_CET_SHADOW_STACK)
// Allowlist to mark builtin calls during which it is impossible that the
// sparkplug frame would have to be deoptimized. Either because they don't
// execute any user code, or because they would anyway replace the current
// frame, e.g., due to OSR.
constexpr static bool BuiltinMayDeopt(Builtin id) {
  switch (id) {
    case Builtin::kSuspendGeneratorBaseline:
    case Builtin::kBaselineOutOfLinePrologue:
    case Builtin::kIncBlockCounter:
    case Builtin::kToObject:
    case Builtin::kStoreScriptContextSlotBaseline:
    case Builtin::kStoreCurrentScriptContextSlotBaseline:
    // This one explicitly skips the construct if the debugger is enabled.
    case Builtin::kFindNonDefaultConstructorOrConstruct:
      return false;
    default:
      return true;
  }
}
#endif  // DEBUG || V8_ENABLE_CET_SHADOW_STACK

template <Builtin kBuiltin, typename... Args>
void BaselineCompiler::CallBuiltin(Args... args) {
#ifdef DEBUG
  effect_state_.CheckEffect();
  if (BuiltinMayDeopt(kBuiltin)) {
    effect_state_.MayDeopt();
  }
#endif
  ASM_CODE_COMMENT(&masm_);
  detail::MoveArgumentsForBuiltin<kBuiltin>(&basm_, args...);
  __ CallBuiltin(kBuiltin);
#ifdef V8_ENABLE_CET_SHADOW_STACK
  if (BuiltinMayDeopt(kBuiltin)) {
    __ MaybeEmitPlaceHolderForDeopt();
  }
#endif  // V8_ENABLE_CET_SHADOW_STACK
}

template <Builtin kBuiltin, typename... Args>
void BaselineCompiler::TailCallBuiltin(Args... args) {
#ifdef DEBUG
  effect_state_.CheckEffect();
#endif
  detail::MoveArgumentsForBuiltin<kBuiltin>(&basm_, args...);
  __ TailCallBuiltin(kBuiltin);
}

template <typename... Args>
void BaselineCompiler::CallRuntime(Runtime::FunctionId function, Args... args) {
#ifdef DEBUG
  effect_state_.CheckEffect();
  effect_state_.MayDeopt();
#endif
  __ LoadContext(kContextRegister);
  int nargs = __ Push(args...);
  __ CallRuntime(function, nargs);
#ifdef V8_ENABLE_CET_SHADOW_STACK
  __ MaybeEmitPlaceHolderForDeopt();
#endif  // V8_ENABLE_CET_SHADOW_STACK
}

// Returns into kInterpreterAccumulatorRegister
void BaselineCompiler::JumpIfToBoolean(bool do_jump_if_true, Label* label,
                                       Label::Distance distance) {
  CallBuiltin<Builtin::kToBooleanForBaselineJump>(
      kInterpreterAccumulatorRegister);
  // ToBooleanForBaselineJump returns the ToBoolean value into return reg 1, and
  // the original value into kInterpreterAccumulatorRegister, so we don't have
  // to worry about it getting clobbered.
  static_assert(kReturnRegister0 == kInterpreterAccumulatorRegister);
  __ JumpIfSmi(do_jump_if_true ? kNotEqual : kEqual, kReturnRegister1,
               Smi::FromInt(0), label, distance);
}

void BaselineCompiler::VisitLdaZero() {
  __ Move(kInterpreterAccumulatorRegister, Smi::FromInt(0));
}

void BaselineCompiler::VisitLdaSmi() {
  Tagged<Smi> constant = Smi::FromInt(iterator().GetImmediateOperand(0));
  __ Move(kInterpreterAccumulatorRegister, constant);
}

void BaselineCompiler::VisitLdaUndefined() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
}

void BaselineCompiler::VisitLdaNull() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue);
}

void BaselineCompiler::VisitLdaTheHole() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue);
}

void BaselineCompiler::VisitLdaTrue() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
}

void BaselineCompiler::VisitLdaFalse() {
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
}

void BaselineCompiler::VisitLdaConstant() {
  LoadConstant<HeapObject>(kInterpreterAccumulatorRegister, 0);
}

void BaselineCompiler::VisitLdaGlobal() {
  CallBuiltin<Builtin::kLoadGlobalICBaseline>(Constant<Name>(0),  // name
                                              IndexAsTagged(1));  // slot
}

void BaselineCompiler::VisitLdaGlobalInsideTypeof() {
  CallBuiltin<Builtin::kLoadGlobalICInsideTypeofBaseline>(
      Constant<Name>(0),  // name
      IndexAsTagged(1));  // slot
}

void BaselineCompiler::VisitStaGlobal() {
  CallBuiltin<Builtin::kStoreGlobalICBaseline>(
      Constant<Name>(0),                // name
      kInterpreterAccumulatorRegister,  // value
      IndexAsTagged(1));                // slot
}

void BaselineCompiler::VisitPushContext() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register context = scratch_scope.AcquireScratch();
  __ LoadContext(context);
  __ StoreContext(kInterpreterAccumulatorRegister);
  StoreRegister(0, context);
}

void BaselineCompiler::VisitPopContext() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register context = scratch_scope.AcquireScratch();
  LoadRegister(context, 0);
  __ StoreContext(context);
}

void BaselineCompiler::VisitLdaContextSlot() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register context = scratch_scope.AcquireScratch();
  LoadRegister(context, 0);
  uint32_t index = Index(1);
  uint32_t depth = Uint(2);
  __ LdaContextSlot(context, index, depth);
}

void BaselineCompiler::VisitLdaScriptContextSlot() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register context = scratch_scope.AcquireScratch();
  Label done;
  LoadRegister(context, 0);
  uint32_t index = Index(1);
  uint32_t depth = Uint(2);
  __ LdaContextSlot(context, index, depth,
                    BaselineAssembler::CompressionMode::kForceDecompression);
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &done);
  __ JumpIfObjectTypeFast(kNotEqual, kInterpreterAccumulatorRegister,
                          HEAP_NUMBER_TYPE, &done, Label::kNear);
  CallBuiltin<Builtin::kAllocateIfMutableHeapNumberScriptContextSlot>(
      kInterpreterAccumulatorRegister,  // heap number
      context,                          // context
      Smi::FromInt(index));             // slot
  __ Bind(&done);
}

void BaselineCompiler::VisitLdaImmutableContextSlot() { VisitLdaContextSlot(); }

void BaselineCompiler::VisitLdaCurrentContextSlot() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register context = scratch_scope.AcquireScratch();
  __ LoadContext(context);
  __ LoadTaggedField(kInterpreterAccumulatorRegister, context,
                     Context::OffsetOfElementAt(Index(0)));
}

void BaselineCompiler::VisitLdaCurrentScriptContextSlot() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register context = scratch_scope.AcquireScratch();
  Label done;
  uint32_t index = Index(0);
  __ LoadContext(context);
  __ LoadTaggedField(kInterpreterAccumulatorRegister, context,
                     Context::OffsetOfElementAt(index));
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &done);
  __ JumpIfObjectTypeFast(kNotEqual, kInterpreterAccumulatorRegister,
                          HEAP_NUMBER_TYPE, &done, Label::kNear);
  CallBuiltin<Builtin::kAllocateIfMutableHeapNumberScriptContextSlot>(
      kInterpreterAccumulatorRegister,  // heap number
      context,                          // context
      Smi::FromInt(index));             // slot
  __ Bind(&done);
}

void BaselineCompiler::VisitLdaImmutableCurrentContextSlot() {
  VisitLdaCurrentContextSlot();
}

void BaselineCompiler::VisitStaContextSlot() {
  Register value = WriteBarrierDescriptor::ValueRegister();
  Register context = WriteBarrierDescriptor::ObjectRegister();
  DCHECK(!AreAliased(value, context, kInterpreterAccumulatorRegister));
  __ Move(value, kInterpreterAccumulatorRegister);
  LoadRegister(context, 0);
  uint32_t index = Index(1);
  uint32_t depth = Uint(2);
  __ StaContextSlot(context, value, index, depth);
}

void BaselineCompiler::VisitStaCurrentContextSlot() {
  Register value = WriteBarrierDescriptor::ValueRegister();
  Register context = WriteBarrierDescriptor::ObjectRegister();
  DCHECK(!AreAliased(value, context, kInterpreterAccumulatorRegister));
  __ Move(value, kInterpreterAccumulatorRegister);
  __ LoadContext(context);
  __ StoreTaggedFieldWithWriteBarrier(
      context, Context::OffsetOfElementAt(Index(0)), value);
}

void BaselineCompiler::VisitStaScriptContextSlot() {
  Register value = WriteBarrierDescriptor::ValueRegister();
  Register context = WriteBarrierDescriptor::ObjectRegister();
  DCHECK(!AreAliased(value, context, kInterpreterAccumulatorRegister));
  __ Move(value, kInterpreterAccumulatorRegister);
  LoadRegister(context, 0);
  SaveAccumulatorScope accumulator_scope(this, &basm_);
  CallBuiltin<Builtin::kStoreScriptContextSlotBaseline>(
      context,           // context
      value,             // value
      IndexAsSmi(1),     // slot
      UintAsTagged(2));  // depth
}

void BaselineCompiler::VisitStaCurrentScriptContextSlot() {
  Register value = WriteBarrierDescriptor::ValueRegister();
  DCHECK(!AreAliased(value, kInterpreterAccumulatorRegister));
  SaveAccumulatorScope accumulator_scope(this, &basm_);
  __ Move(value, kInterpreterAccumulatorRegister);
  CallBuiltin<Builtin::kStoreCurrentScriptContextSlotBaseline>(
      value,           // value
      IndexAsSmi(0));  // slot
}

void BaselineCompiler::VisitLdaLookupSlot() {
  CallRuntime(Runtime::kLoadLookupSlot, Constant<Name>(0));
}

void BaselineCompiler::VisitLdaLookupContextSlot() {
  CallBuiltin<Builtin::kLookupContextBaseline>(
      Constant<Name>(0), UintAsTagged(2), IndexAsTagged(1));
}

void BaselineCompiler::VisitLdaLookupScriptContextSlot() {
  CallBuiltin<Builtin::kLookupScriptContextBaseline>(
      Constant<Name>(0), UintAsTagged(2), IndexAsTagged(1));
}

void BaselineCompiler::VisitLdaLookupGlobalSlot() {
  CallBuiltin<Builtin::kLookupGlobalICBaseline>(
      Constant<Name>(0), UintAsTagged(2), IndexAsTagged(1));
}

void BaselineCompiler::VisitLdaLookupSlotInsideTypeof() {
  CallRuntime(Runtime::kLoadLookupSlotInsideTypeof, Constant<Name>(0));
}

void BaselineCompiler::VisitLdaLookupContextSlotInsideTypeof() {
  CallBuiltin<Builtin::kLookupContextInsideTypeofBaseline>(
      Constant<Name>(0), UintAsTagged(2), IndexAsTagged(1));
}

void BaselineCompiler::VisitLdaLookupScriptContextSlotInsideTypeof() {
  CallBuiltin<Builtin::kLookupScriptContextInsideTypeofBaseline>(
      Constant<Name>(0), UintAsTagged(2), IndexAsTagged(1));
}

void BaselineCompiler::VisitLdaLookupGlobalSlotInsideTypeof() {
  CallBuiltin<Builtin::kLookupGlobalICInsideTypeofBaseline>(
      Constant<Name>(0), UintAsTagged(2), IndexAsTagged(1));
}

void BaselineCompiler::VisitStaLookupSlot() {
  uint32_t flags = Flag8(1);
  Runtime::FunctionId function_id;
  if (flags & interpreter::StoreLookupSlotFlags::LanguageModeBit::kMask) {
    function_id = Runtime::kStoreLookupSlot_Strict;
  } else if (flags &
             interpreter::StoreLookupSlotFlags::LookupHoistingModeBit::kMask) {
    function_id = Runtime::kStoreLookupSlot_SloppyHoisting;
  } else {
    function_id = Runtime::kStoreLookupSlot_Sloppy;
  }
  CallRuntime(function_id, Constant<Name>(0),    // name
              kInterpreterAccumulatorRegister);  // value
}

void BaselineCompiler::VisitLdar() {
  LoadRegister(kInterpreterAccumulatorRegister, 0);
}

void BaselineCompiler::VisitStar() {
  StoreRegister(0, kInterpreterAccumulatorRegister);
}

#define SHORT_STAR_VISITOR(Name, ...)                                         \
  void BaselineCompiler::Visit##Name() {                                      \
    __ StoreRegister(                                                         \
        interpreter::Register::FromShortStar(interpreter::Bytecode::k##Name), \
        kInterpreterAccumulatorRegister);                                     \
  }
SHORT_STAR_BYTECODE_LIST(SHORT_STAR_VISITOR)
#undef SHORT_STAR_VISITOR

void BaselineCompiler::VisitMov() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register scratch = scratch_scope.AcquireScratch();
  LoadRegister(scratch, 0);
  StoreRegister(1, scratch);
}

void BaselineCompiler::VisitGetNamedProperty() {
  CallBuiltin<Builtin::kLoadICBaseline>(RegisterOperand(0),  // object
                                        Constant<Name>(1),   // name
                                        IndexAsTagged(2));   // slot
}

void BaselineCompiler::VisitGetNamedPropertyFromSuper() {
  __ LoadPrototype(
      LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister(),
      kInterpreterAccumulatorRegister);

  CallBuiltin<Builtin::kLoadSuperICBaseline>(
      RegisterOperand(0),  // object
      LoadWithReceiverAndVectorDescriptor::
          LookupStartObjectRegister(),  // lookup start
      Constant<Name>(1),                // name
      IndexAsTagged(2));                // slot
}

void BaselineCompiler::VisitGetKeyedProperty() {
  CallBuiltin<Builtin::kKeyedLoadICBaseline>(
      RegisterOperand(0),               // object
      kInterpreterAccumulatorRegister,  // key
      IndexAsTagged(1));                // slot
}

void BaselineCompiler::VisitGetEnumeratedKeyedProperty() {
  DCHECK(v8_flags.enable_enumerated_keyed_access_bytecode);
  CallBuiltin<Builtin::kEnumeratedKeyedLoadICBaseline>(
      RegisterOperand(0),               // object
      kInterpreterAccumulatorRegister,  // key
      RegisterOperand(1),               // enum index
      RegisterOperand(2),               // cache type
      IndexAsTagged(3));                // slot
}

void BaselineCompiler::VisitLdaModuleVariable() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register scratch = scratch_scope.AcquireScratch();
  __ LoadContext(scratch);
  int cell_index = Int(0);
  int depth = Uint(1);
  __ LdaModuleVariable(scratch, cell_index, depth);
}

void BaselineCompiler::VisitStaModuleVariable() {
  int cell_index = Int(0);
  if (V8_UNLIKELY(cell_index < 0)) {
    // Not supported (probably never).
    CallRuntime(Runtime::kAbort,
                Smi::FromInt(static_cast<int>(
                    AbortReason::kUnsupportedModuleOperation)));
    __ Trap();
  }
  Register value = WriteBarrierDescriptor::ValueRegister();
  Register scratch = WriteBarrierDescriptor::ObjectRegister();
  DCHECK(!AreAliased(value, scratch, kInterpreterAccumulatorRegister));
  __ Move(value, kInterpreterAccumulatorRegister);
  __ LoadContext(scratch);
  int depth = Uint(1);
  __ StaModuleVariable(scratch, value, cell_index, depth);
}

void BaselineCompiler::VisitSetNamedProperty() {
  // StoreIC is currently a base class for multiple property store operations
  // and contains mixed logic for named and keyed, set and define operations,
  // the paths are controlled by feedback.
  // TODO(v8:12548): refactor SetNamedIC as a subclass of StoreIC, which can be
  // called here.
  CallBuiltin<Builtin::kStoreICBaseline>(
      RegisterOperand(0),               // object
      Constant<Name>(1),                // name
      kInterpreterAccumulatorRegister,  // value
      IndexAsTagged(2));                // slot
}

void BaselineCompiler::VisitDefineNamedOwnProperty() {
  CallBuiltin<Builtin::kDefineNamedOwnICBaseline>(
      RegisterOperand(0),               // object
      Constant<Name>(1),                // name
      kInterpreterAccumulatorRegister,  // value
      IndexAsTagged(2));                // slot
}

void BaselineCompiler::VisitSetKeyedProperty() {
  // KeyedStoreIC is currently a base class for multiple keyed property store
  // operations and contains mixed logic for set and define operations,
  // the paths are controlled by feedback.
  // TODO(v8:12548): refactor SetKeyedIC as a subclass of KeyedStoreIC, which
  // can be called here.
  CallBuiltin<Builtin::kKeyedStoreICBaseline>(
      RegisterOperand(0),               // object
      RegisterOperand(1),               // key
      kInterpreterAccumulatorRegister,  // value
      IndexAsTagged(2));                // slot
}

void BaselineCompiler::VisitDefineKeyedOwnProperty() {
  CallBuiltin<Builtin::kDefineKeyedOwnICBaseline>(
      RegisterOperand(0),               // object
      RegisterOperand(1),               // key
      kInterpreterAccumulatorRegister,  // value
      Flag8AsSmi(2),                    // flags
      IndexAsTagged(3));                // slot
}

void BaselineCompiler::VisitStaInArrayLiteral() {
  CallBuiltin<Builtin::kStoreInArrayLiteralICBaseline>(
      RegisterOperand(0),               // object
      RegisterOperand(1),               // name
      kInterpreterAccumulatorRegister,  // value
      IndexAsTagged(2));                // slot
}

void BaselineCompiler::VisitDefineKeyedOwnPropertyInLiteral() {
  // Here we should save the accumulator, since
  // DefineKeyedOwnPropertyInLiteral doesn't write the accumulator, but
  // Runtime::kDefineKeyedOwnPropertyInLiteral returns the value that we got
  // from the accumulator so this still works.
  CallRuntime(Runtime::kDefineKeyedOwnPropertyInLiteral,
              RegisterOperand(0),               // object
              RegisterOperand(1),               // name
              kInterpreterAccumulatorRegister,  // value
              Flag8AsSmi(2),                    // flags
              FeedbackVector(),                 // feedback vector
              IndexAsTagged(3));                // slot
}

void BaselineCompiler::VisitAdd() {
  CallBuiltin<Builtin::kAdd_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitSub() {
  CallBuiltin<Builtin::kSubtract_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitMul() {
  CallBuiltin<Builtin::kMultiply_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitDiv() {
  CallBuiltin<Builtin::kDivide_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitMod() {
  CallBuiltin<Builtin::kModulus_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitExp() {
  CallBuiltin<Builtin::kExponentiate_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitBitwiseOr() {
  CallBuiltin<Builtin::kBitwiseOr_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitBitwiseXor() {
  CallBuiltin<Builtin::kBitwiseXor_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitBitwiseAnd() {
  CallBuiltin<Builtin::kBitwiseAnd_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitShiftLeft() {
  CallBuiltin<Builtin::kShiftLeft_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitShiftRight() {
  CallBuiltin<Builtin::kShiftRight_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitShiftRightLogical() {
  CallBuiltin<Builtin::kShiftRightLogical_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitAddSmi() {
  CallBuiltin<Builtin::kAddSmi_Baseline>(kInterpreterAccumulatorRegister,
                                         IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitSubSmi() {
  CallBuiltin<Builtin::kSubtractSmi_Baseline>(kInterpreterAccumulatorRegister,
                                              IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitMulSmi() {
  CallBuiltin<Builtin::kMultiplySmi_Baseline>(kInterpreterAccumulatorRegister,
                                              IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitDivSmi() {
  CallBuiltin<Builtin::kDivideSmi_Baseline>(kInterpreterAccumulatorRegister,
                                            IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitModSmi() {
  CallBuiltin<Builtin::kModulusSmi_Baseline>(kInterpreterAccumulatorRegister,
                                             IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitExpSmi() {
  CallBuiltin<Builtin::kExponentiateSmi_Baseline>(
      kInterpreterAccumulatorRegister, IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitBitwiseOrSmi() {
  CallBuiltin<Builtin::kBitwiseOrSmi_Baseline>(kInterpreterAccumulatorRegister,
                                               IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitBitwiseXorSmi() {
  CallBuiltin<Builtin::kBitwiseXorSmi_Baseline>(kInterpreterAccumulatorRegister,
                                                IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitBitwiseAndSmi() {
  CallBuiltin<Builtin::kBitwiseAndSmi_Baseline>(kInterpreterAccumulatorRegister,
                                                IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitShiftLeftSmi() {
  CallBuiltin<Builtin::kShiftLeftSmi_Baseline>(kInterpreterAccumulatorRegister,
                                               IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitShiftRightSmi() {
  CallBuiltin<Builtin::kShiftRightSmi_Baseline>(kInterpreterAccumulatorRegister,
                                                IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitShiftRightLogicalSmi() {
  CallBuiltin<Builtin::kShiftRightLogicalSmi_Baseline>(
      kInterpreterAccumulatorRegister, IntAsSmi(0), Index(1));
}

void BaselineCompiler::VisitInc() {
  CallBuiltin<Builtin::kIncrement_Baseline>(kInterpreterAccumulatorRegister,
                                            Index(0));
}

void BaselineCompiler::VisitDec() {
  CallBuiltin<Builtin::kDecrement_Baseline>(kInterpreterAccumulatorRegister,
                                            Index(0));
}

void BaselineCompiler::VisitNegate() {
  CallBuiltin<Builtin::kNegate_Baseline>(kInterpreterAccumulatorRegister,
                                         Index(0));
}

void BaselineCompiler::VisitBitwiseNot() {
  CallBuiltin<Builtin::kBitwiseNot_Baseline>(kInterpreterAccumulatorRegister,
                                             Index(0));
}

void BaselineCompiler::VisitToBooleanLogicalNot() {
  SelectBooleanConstant(kInterpreterAccumulatorRegister,
                        [&](Label* if_true, Label::Distance distance) {
                          JumpIfToBoolean(false, if_true, distance);
                        });
}

void BaselineCompiler::VisitLogicalNot() {
  SelectBooleanConstant(kInterpreterAccumulatorRegister,
                        [&](Label* if_true, Label::Distance distance) {
                          __ JumpIfRoot(kInterpreterAccumulatorRegister,
                                        RootIndex::kFalseValue, if_true,
                                        distance);
                        });
}

void BaselineCompiler::VisitTypeOf() {
  CallBuiltin<Builtin::kTypeof_Baseline>(kInterpreterAccumulatorRegister,
                                         Index(0));
}

void BaselineCompiler::VisitDeletePropertyStrict() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register scratch = scratch_scope.AcquireScratch();
  __ Move(scratch, kInterpreterAccumulatorRegister);
  CallBuiltin<Builtin::kDeleteProperty>(RegisterOperand(0), scratch,
                                        Smi::FromEnum(LanguageMode::kStrict));
}

void BaselineCompiler::VisitDeletePropertySloppy() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register scratch = scratch_scope.AcquireScratch();
  __ Move(scratch, kInterpreterAccumulatorRegister);
  CallBuiltin<Builtin::kDeleteProperty>(RegisterOperand(0), scratch,
                                        Smi::FromEnum(LanguageMode::kSloppy));
}

void BaselineCompiler::VisitGetSuperConstructor() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register prototype = scratch_scope.AcquireScratch();
  __ LoadPrototype(prototype, kInterpreterAccumulatorRegister);
  StoreRegister(0, prototype);
}

void BaselineCompiler::VisitFindNonDefaultConstructorOrConstruct() {
  SaveAccumulatorScope accumulator_scope(this, &basm_);
  CallBuiltin<Builtin::kFindNonDefaultConstructorOrConstruct>(
      RegisterOperand(0), RegisterOperand(1));
  StoreRegisterPair(2, kReturnRegister0, kReturnRegister1);
}

namespace {
constexpr Builtin ConvertReceiverModeToCompactBuiltin(
    ConvertReceiverMode mode) {
  switch (mode) {
    case ConvertReceiverMode::kAny:
      return Builtin::kCall_ReceiverIsAny_Baseline_Compact;
    case ConvertReceiverMode::kNullOrUndefined:
      return Builtin::kCall_ReceiverIsNullOrUndefined_Baseline_Compact;
    case ConvertReceiverMode::kNotNullOrUndefined:
      return Builtin::kCall_ReceiverIsNotNullOrUndefined_Baseline_Compact;
  }
}
constexpr Builtin ConvertReceiverModeToBuiltin(ConvertReceiverMode mode) {
  switch (mode) {
    case ConvertReceiverMode::kAny:
      return Builtin::kCall_ReceiverIsAny_Baseline;
    case ConvertReceiverMode::kNullOrUndefined:
      return Builtin::kCall_ReceiverIsNullOrUndefined_Baseline;
    case ConvertReceiverMode::kNotNullOrUndefined:
      return Builtin::kCall_ReceiverIsNotNullOrUndefined_Baseline;
  }
}
}  // namespace

template <ConvertReceiverMode kMode, typename... Args>
void BaselineCompiler::BuildCall(uint32_t slot, uint32_t arg_count,
                                 Args... args) {
  uint32_t bitfield;
  if (CallTrampoline_Baseline_CompactDescriptor::EncodeBitField(arg_count, slot,
                                                                &bitfield)) {
    CallBuiltin<ConvertReceiverModeToCompactBuiltin(kMode)>(
        RegisterOperand(0),  // kFunction
        bitfield,            // kActualArgumentsCount | kSlot
        args...);            // Arguments
  } else {
    CallBuiltin<ConvertReceiverModeToBuiltin(kMode)>(
        RegisterOperand(0),  // kFunction
        arg_count,           // kActualArgumentsCount
        slot,                // kSlot
        args...);            // Arguments
  }
}

void BaselineCompiler::VisitCallAnyReceiver() {
  interpreter::RegisterList args = iterator().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();
  BuildCall<ConvertReceiverMode::kAny>(Index(3), arg_count, args);
}

void BaselineCompiler::VisitCallProperty() {
  interpreter::RegisterList args = iterator().GetRegisterListOperand(1);
  uint32_t arg_count = args.register_count();
  BuildCall<ConvertReceiverMode::kNotNullOrUndefined>(Index(3), arg_count,
                                                      args);
}

void BaselineCompiler::VisitCallProperty0() {
  BuildCall<ConvertReceiverMode::kNotNullOrUndefined>(
      Index(2), JSParameterCount(0), RegisterOperand(1));
}

void BaselineCompiler::VisitCallProperty1() {
  BuildCall<ConvertReceiverMode::kNotNullOrUndefined>(
      Index(3), JSParameterCount(1), RegisterOperand(1), RegisterOperand(2));
}

void BaselineCompiler::VisitCallProperty2() {
  BuildCall<ConvertReceiverMode::kNotNullOrUndefined>(
      Index(4), JSParameterCount(2), RegisterOperand(1), RegisterOperand(2),
      RegisterOperand(3));
}

void BaselineCompiler::VisitCallUndefinedReceiver() {
  interpreter::RegisterList args = iterator().GetRegisterListOperand(1);
  uint32_t arg_count = JSParameterCount(args.register_count());
  BuildCall<ConvertReceiverMode::kNullOrUndefined>(
      Index(3), arg_count, RootIndex::kUndefinedValue, args);
}

void BaselineCompiler::VisitCallUndefinedReceiver0() {
  BuildCall<ConvertReceiverMode::kNullOrUndefined>(
      Index(1), JSParameterCount(0), RootIndex::kUndefinedValue);
}

void BaselineCompiler::VisitCallUndefinedReceiver1() {
  BuildCall<ConvertReceiverMode::kNullOrUndefined>(
      Index(2), JSParameterCount(1), RootIndex::kUndefinedValue,
      RegisterOperand(1));
}

void BaselineCompiler::VisitCallUndefinedReceiver2() {
  BuildCall<ConvertReceiverMode::kNullOrUndefined>(
      Index(3), JSParameterCount(2), RootIndex::kUndefinedValue,
      RegisterOperand(1), RegisterOperand(2));
}

void BaselineCompiler::VisitCallWithSpread() {
  interpreter::RegisterList args = iterator().GetRegisterListOperand(1);

  // Do not push the spread argument
  interpreter::Register spread_register = args.last_register();
  args = args.Truncate(args.register_count() - 1);

  uint32_t arg_count = args.register_count();

  CallBuiltin<Builtin::kCallWithSpread_Baseline>(
      RegisterOperand(0),  // kFunction
      arg_count,           // kActualArgumentsCount
      spread_register,     // kSpread
      Index(3),            // kSlot
      args);
}

void BaselineCompiler::VisitCallRuntime() {
  CallRuntime(iterator().GetRuntimeIdOperand(0),
              iterator().GetRegisterListOperand(1));
}

void BaselineCompiler::VisitCallRuntimeForPair() {
  auto builtin = iterator().GetRuntimeIdOperand(0);
  switch (builtin) {
    case Runtime::kLoadLookupSlotForCall: {
      // TODO(olivf) Once we have more builtins to support here we should find
      // out how to do this generically.
      auto in = iterator().GetRegisterListOperand(1);
      auto out = iterator().GetRegisterPairOperand(3);
      BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
      Register out_reg = scratch_scope.AcquireScratch();
      __ RegisterFrameAddress(out.first, out_reg);
      DCHECK_EQ(in.register_count(), 1);
      CallRuntime(Runtime::kLoadLookupSlotForCall_Baseline, in.first_register(),
                  out_reg);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void BaselineCompiler::VisitCallJSRuntime() {
  interpreter::RegisterList args = iterator().GetRegisterListOperand(1);
  uint32_t arg_count = JSParameterCount(args.register_count());

  // Load context for LoadNativeContextSlot.
  __ LoadContext(kContextRegister);
  __ LoadNativeContextSlot(kJavaScriptCallTargetRegister,
                           iterator().GetNativeContextIndexOperand(0));
  CallBuiltin<Builtin::kCall_ReceiverIsNullOrUndefined>(
      kJavaScriptCallTargetRegister,  // kFunction
      arg_count,                      // kActualArgumentsCount
      RootIndex::kUndefinedValue,     // kReceiver
      args);
}

void BaselineCompiler::VisitInvokeIntrinsic() {
  Runtime::FunctionId intrinsic_id = iterator().GetIntrinsicIdOperand(0);
  interpreter::RegisterList args = iterator().GetRegisterListOperand(1);
  switch (intrinsic_id) {
#define CASE(Name, ...)         \
  case Runtime::kInline##Name:  \
    VisitIntrinsic##Name(args); \
    break;
    INTRINSICS_LIST(CASE)
#undef CASE

    default:
      UNREACHABLE();
  }
}

void BaselineCompiler::VisitIntrinsicCopyDataProperties(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kCopyDataProperties>(args);
}

void BaselineCompiler::
    VisitIntrinsicCopyDataPropertiesWithExcludedPropertiesOnStack(
        interpreter::RegisterList args) {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register rscratch = scratch_scope.AcquireScratch();
  // Use an offset from args[0] instead of args[1] to pass a valid "end of"
  // pointer in the case where args.register_count() == 1.
  basm_.RegisterFrameAddress(interpreter::Register(args[0].index() + 1),
                             rscratch);
  CallBuiltin<Builtin::kCopyDataPropertiesWithExcludedPropertiesOnStack>(
      args[0], args.register_count() - 1, rscratch);
}

void BaselineCompiler::VisitIntrinsicCreateIterResultObject(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kCreateIterResultObject>(args);
}

void BaselineCompiler::VisitIntrinsicCreateAsyncFromSyncIterator(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kCreateAsyncFromSyncIteratorBaseline>(args[0]);
}

void BaselineCompiler::VisitIntrinsicCreateJSGeneratorObject(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kCreateGeneratorObject>(args);
}

void BaselineCompiler::VisitIntrinsicGeneratorGetResumeMode(
    interpreter::RegisterList args) {
  __ LoadRegister(kInterpreterAccumulatorRegister, args[0]);
  __ LoadTaggedField(kInterpreterAccumulatorRegister,
                     kInterpreterAccumulatorRegister,
                     JSGeneratorObject::kResumeModeOffset);
}

void BaselineCompiler::VisitIntrinsicGeneratorClose(
    interpreter::RegisterList args) {
  __ LoadRegister(kInterpreterAccumulatorRegister, args[0]);
  __ StoreTaggedSignedField(kInterpreterAccumulatorRegister,
                            JSGeneratorObject::kContinuationOffset,
                            Smi::FromInt(JSGeneratorObject::kGeneratorClosed));
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
}

void BaselineCompiler::VisitIntrinsicGetImportMetaObject(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kGetImportMetaObjectBaseline>();
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionAwait(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kAsyncFunctionAwait>(args);
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionEnter(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kAsyncFunctionEnter>(args);
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionReject(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kAsyncFunctionReject>(args);
}

void BaselineCompiler::VisitIntrinsicAsyncFunctionResolve(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kAsyncFunctionResolve>(args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorAwait(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kAsyncGeneratorAwait>(args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorReject(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kAsyncGeneratorReject>(args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorResolve(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kAsyncGeneratorResolve>(args);
}

void BaselineCompiler::VisitIntrinsicAsyncGeneratorYieldWithAwait(
    interpreter::RegisterList args) {
  CallBuiltin<Builtin::kAsyncGeneratorYieldWithAwait>(args);
}

void BaselineCompiler::VisitConstruct() {
  interpreter::RegisterList args = iterator().GetRegisterListOperand(1);
  uint32_t arg_count = JSParameterCount(args.register_count());
  CallBuiltin<Builtin::kConstruct_Baseline>(
      RegisterOperand(0),               // kFunction
      kInterpreterAccumulatorRegister,  // kNewTarget
      arg_count,                        // kActualArgumentsCount
      Index(3),                         // kSlot
      RootIndex::kUndefinedValue,       // kReceiver
      args);
}

void BaselineCompiler::VisitConstructWithSpread() {
  interpreter::RegisterList args = iterator().GetRegisterListOperand(1);

  // Do not push the spread argument
  interpreter::Register spread_register = args.last_register();
  args = args.Truncate(args.register_count() - 1);

  uint32_t arg_count = JSParameterCount(args.register_count());

  using Descriptor =
      CallInterfaceDescriptorFor<Builtin::kConstructWithSpread_Baseline>::type;
  Register new_target =
      Descriptor::GetRegisterParameter(Descriptor::kNewTarget);
  __ Move(new_target, kInterpreterAccumulatorRegister);

  CallBuiltin<Builtin::kConstructWithSpread_Baseline>(
      RegisterOperand(0),          // kFunction
      new_target,                  // kNewTarget
      arg_count,                   // kActualArgumentsCount
      spread_register,             // kSpread
      IndexAsTagged(3),            // kSlot
      RootIndex::kUndefinedValue,  // kReceiver
      args);
}

void BaselineCompiler::VisitConstructForwardAllArgs() {
  using Descriptor = CallInterfaceDescriptorFor<
      Builtin::kConstructForwardAllArgs_Baseline>::type;
  Register new_target =
      Descriptor::GetRegisterParameter(Descriptor::kNewTarget);
  __ Move(new_target, kInterpreterAccumulatorRegister);

  CallBuiltin<Builtin::kConstructForwardAllArgs_Baseline>(
      RegisterOperand(0),  // kFunction
      new_target,          // kNewTarget
      IndexAsTagged(1));   // kSlot
}

void BaselineCompiler::VisitTestEqual() {
  CallBuiltin<Builtin::kEqual_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitTestEqualStrict() {
  CallBuiltin<Builtin::kStrictEqual_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitTestLessThan() {
  CallBuiltin<Builtin::kLessThan_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitTestGreaterThan() {
  CallBuiltin<Builtin::kGreaterThan_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitTestLessThanOrEqual() {
  CallBuiltin<Builtin::kLessThanOrEqual_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitTestGreaterThanOrEqual() {
  CallBuiltin<Builtin::kGreaterThanOrEqual_Baseline>(
      RegisterOperand(0), kInterpreterAccumulatorRegister, Index(1));
}

void BaselineCompiler::VisitTestReferenceEqual() {
  SelectBooleanConstant(
      kInterpreterAccumulatorRegister,
      [&](Label* is_true, Label::Distance distance) {
        __ JumpIfTagged(kEqual, __ RegisterFrameOperand(RegisterOperand(0)),
                        kInterpreterAccumulatorRegister, is_true, distance);
      });
}

void BaselineCompiler::VisitTestInstanceOf() {
  using Descriptor =
      CallInterfaceDescriptorFor<Builtin::kInstanceOf_Baseline>::type;
  Register callable = Descriptor::GetRegisterParameter(Descriptor::kRight);
  __ Move(callable, kInterpreterAccumulatorRegister);

  CallBuiltin<Builtin::kInstanceOf_Baseline>(RegisterOperand(0),  // object
                                             callable,            // callable
                                             Index(1));           // slot
}

void BaselineCompiler::VisitTestIn() {
  CallBuiltin<Builtin::kKeyedHasICBaseline>(
      kInterpreterAccumulatorRegister,  // object
      RegisterOperand(0),               // name
      IndexAsTagged(1));                // slot
}

void BaselineCompiler::VisitTestUndetectable() {
  Label done, is_smi, not_undetectable;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

  Register map_bit_field = kInterpreterAccumulatorRegister;
  __ LoadMap(map_bit_field, kInterpreterAccumulatorRegister);
  __ LoadWord8Field(map_bit_field, map_bit_field, Map::kBitFieldOffset);
  __ TestAndBranch(map_bit_field, Map::Bits1::IsUndetectableBit::kMask, kZero,
                   &not_undetectable, Label::kNear);

  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
  __ Jump(&done, Label::kNear);

  __ Bind(&is_smi);
  __ Bind(&not_undetectable);
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
  __ Bind(&done);
}

void BaselineCompiler::VisitTestNull() {
  SelectBooleanConstant(kInterpreterAccumulatorRegister,
                        [&](Label* is_true, Label::Distance distance) {
                          __ JumpIfRoot(kInterpreterAccumulatorRegister,
                                        RootIndex::kNullValue, is_true,
                                        distance);
                        });
}

void BaselineCompiler::VisitTestUndefined() {
  SelectBooleanConstant(kInterpreterAccumulatorRegister,
                        [&](Label* is_true, Label::Distance distance) {
                          __ JumpIfRoot(kInterpreterAccumulatorRegister,
                                        RootIndex::kUndefinedValue, is_true,
                                        distance);
                        });
}

void BaselineCompiler::VisitTestTypeOf() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);

  auto literal_flag =
      static_cast<interpreter::TestTypeOfFlags::LiteralFlag>(Flag8(0));

  Label done;
  switch (literal_flag) {
    case interpreter::TestTypeOfFlags::LiteralFlag::kNumber: {
      Label is_smi, is_heap_number;
      __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);
      __ JumpIfObjectTypeFast(kEqual, kInterpreterAccumulatorRegister,
                              HEAP_NUMBER_TYPE, &is_heap_number, Label::kNear);

      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
      __ Jump(&done, Label::kNear);

      __ Bind(&is_smi);
      __ Bind(&is_heap_number);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
      break;
    }
    case interpreter::TestTypeOfFlags::LiteralFlag::kString: {
      Label is_smi, bad_instance_type;
      __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);
      static_assert(INTERNALIZED_TWO_BYTE_STRING_TYPE == FIRST_TYPE);
      __ JumpIfObjectType(kGreaterThanEqual, kInterpreterAccumulatorRegister,
                          FIRST_NONSTRING_TYPE, scratch_scope.AcquireScratch(),
                          &bad_instance_type, Label::kNear);

      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
      __ Jump(&done, Label::kNear);

      __ Bind(&is_smi);
      __ Bind(&bad_instance_type);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
      break;
    }
    case interpreter::TestTypeOfFlags::LiteralFlag::kSymbol: {
      Label is_smi, bad_instance_type;
      __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);
      __ JumpIfObjectTypeFast(kNotEqual, kInterpreterAccumulatorRegister,
                              SYMBOL_TYPE, &bad_instance_type, Label::kNear);

      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
      __ Jump(&done, Label::kNear);

      __ Bind(&is_smi);
      __ Bind(&bad_instance_type);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
      break;
    }
    case interpreter::TestTypeOfFlags::LiteralFlag::kBoolean: {
      Label is_true, is_false;
      __ JumpIfRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue,
                    &is_true, Label::kNear);
      __ JumpIfRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue,
                    &is_false, Label::kNear);

      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
      __ Jump(&done, Label::kNear);

      __ Bind(&is_true);
      __ Bind(&is_false);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
      break;
    }
    case interpreter::TestTypeOfFlags::LiteralFlag::kBigInt: {
      Label is_smi, bad_instance_type;
      __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);
      __ JumpIfObjectTypeFast(kNotEqual, kInterpreterAccumulatorRegister,
                              BIGINT_TYPE, &bad_instance_type, Label::kNear);

      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
      __ Jump(&done, Label::kNear);

      __ Bind(&is_smi);
      __ Bind(&bad_instance_type);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
      break;
    }
    case interpreter::TestTypeOfFlags::LiteralFlag::kUndefined: {
      Label is_smi, is_null, not_undetectable;
      __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

      // null is undetectable, so test it explicitly, and return false.
      __ JumpIfRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue,
                    &is_null, Label::kNear);

      // All other undetectable maps are typeof undefined.
      Register map_bit_field = kInterpreterAccumulatorRegister;
      __ LoadMap(map_bit_field, kInterpreterAccumulatorRegister);
      __ LoadWord8Field(map_bit_field, map_bit_field, Map::kBitFieldOffset);
      __ TestAndBranch(map_bit_field, Map::Bits1::IsUndetectableBit::kMask,
                       kZero, &not_undetectable, Label::kNear);

      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
      __ Jump(&done, Label::kNear);

      __ Bind(&is_smi);
      __ Bind(&is_null);
      __ Bind(&not_undetectable);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
      break;
    }
    case interpreter::TestTypeOfFlags::LiteralFlag::kFunction: {
      Label is_smi, not_callable, undetectable;
      __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

      // Check if the map is callable but not undetectable.
      Register map_bit_field = kInterpreterAccumulatorRegister;
      __ LoadMap(map_bit_field, kInterpreterAccumulatorRegister);
      __ LoadWord8Field(map_bit_field, map_bit_field, Map::kBitFieldOffset);
      __ TestAndBranch(map_bit_field, Map::Bits1::IsCallableBit::kMask, kZero,
                       &not_callable, Label::kNear);
      __ TestAndBranch(map_bit_field, Map::Bits1::IsUndetectableBit::kMask,
                       kNotZero, &undetectable, Label::kNear);

      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
      __ Jump(&done, Label::kNear);

      __ Bind(&is_smi);
      __ Bind(&not_callable);
      __ Bind(&undetectable);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
      break;
    }
    case interpreter::TestTypeOfFlags::LiteralFlag::kObject: {
      Label is_smi, is_null, bad_instance_type, undetectable_or_callable;
      __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

      // If the object is null, return true.
      __ JumpIfRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue,
                    &is_null, Label::kNear);

      // If the object's instance type isn't within the range, return false.
      static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
      Register map = scratch_scope.AcquireScratch();
      __ JumpIfObjectType(kLessThan, kInterpreterAccumulatorRegister,
                          FIRST_JS_RECEIVER_TYPE, map, &bad_instance_type,
                          Label::kNear);

      // If the map is undetectable or callable, return false.
      Register map_bit_field = kInterpreterAccumulatorRegister;
      __ LoadWord8Field(map_bit_field, map, Map::kBitFieldOffset);
      __ TestAndBranch(map_bit_field,
                       Map::Bits1::IsUndetectableBit::kMask |
                           Map::Bits1::IsCallableBit::kMask,
                       kNotZero, &undetectable_or_callable, Label::kNear);

      __ Bind(&is_null);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kTrueValue);
      __ Jump(&done, Label::kNear);

      __ Bind(&is_smi);
      __ Bind(&bad_instance_type);
      __ Bind(&undetectable_or_callable);
      __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kFalseValue);
      break;
    }
    case interpreter::TestTypeOfFlags::LiteralFlag::kOther:
    default:
      UNREACHABLE();
  }
  __ Bind(&done);
}

void BaselineCompiler::VisitToName() {
  CallBuiltin<Builtin::kToName>(kInterpreterAccumulatorRegister);
}

void BaselineCompiler::VisitToNumber() {
  CallBuiltin<Builtin::kToNumber_Baseline>(kInterpreterAccumulatorRegister,
                                           Index(0));
}

void BaselineCompiler::VisitToNumeric() {
  CallBuiltin<Builtin::kToNumeric_Baseline>(kInterpreterAccumulatorRegister,
                                            Index(0));
}

void BaselineCompiler::VisitToObject() {
  SaveAccumulatorScope save_accumulator(this, &basm_);
  CallBuiltin<Builtin::kToObject>(kInterpreterAccumulatorRegister);
  StoreRegister(0, kInterpreterAccumulatorRegister);
}

void BaselineCompiler::VisitToString() {
  CallBuiltin<Builtin::kToString>(kInterpreterAccumulatorRegister);
}

void BaselineCompiler::VisitToBoolean() {
  CallBuiltin<Builtin::kToBoolean>(kInterpreterAccumulatorRegister);
}

void BaselineCompiler::VisitCreateRegExpLiteral() {
  CallBuiltin<Builtin::kCreateRegExpLiteral>(
      FeedbackVector(),         // feedback vector
      IndexAsTagged(1),         // slot
      Constant<HeapObject>(0),  // pattern
      Flag16AsSmi(2));          // flags
}

void BaselineCompiler::VisitCreateArrayLiteral() {
  uint32_t flags = Flag8(2);
  int32_t flags_raw = static_cast<int32_t>(
      interpreter::CreateArrayLiteralFlags::FlagsBits::decode(flags));
  if (flags &
      interpreter::CreateArrayLiteralFlags::FastCloneSupportedBit::kMask) {
    CallBuiltin<Builtin::kCreateShallowArrayLiteral>(
        FeedbackVector(),          // feedback vector
        IndexAsTagged(1),          // slot
        Constant<HeapObject>(0),   // constant elements
        Smi::FromInt(flags_raw));  // flags
  } else {
    CallBuiltin<Builtin::kCreateArrayFromSlowBoilerplate>(
        FeedbackVector(),          // feedback vector
        IndexAsTagged(1),          // slot
        Constant<HeapObject>(0),   // constant elements
        Smi::FromInt(flags_raw));  // flags
  }
}

void BaselineCompiler::VisitCreateArrayFromIterable() {
  CallBuiltin<Builtin::kIterableToListWithSymbolLookup>(
      kInterpreterAccumulatorRegister);  // iterable
}

void BaselineCompiler::VisitCreateEmptyArrayLiteral() {
  CallBuiltin<Builtin::kCreateEmptyArrayLiteral>(FeedbackVector(),
                                                 IndexAsTagged(0));
}

void BaselineCompiler::VisitCreateObjectLiteral() {
  uint32_t flags = Flag8(2);
  int32_t flags_raw = static_cast<int32_t>(
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(flags));
  if (flags &
      interpreter::CreateObjectLiteralFlags::FastCloneSupportedBit::kMask) {
    CallBuiltin<Builtin::kCreateShallowObjectLiteral>(
        FeedbackVector(),                           // feedback vector
        IndexAsTagged(1),                           // slot
        Constant<ObjectBoilerplateDescription>(0),  // boilerplate
        Smi::FromInt(flags_raw));                   // flags
  } else {
    CallBuiltin<Builtin::kCreateObjectFromSlowBoilerplate>(
        FeedbackVector(),                           // feedback vector
        IndexAsTagged(1),                           // slot
        Constant<ObjectBoilerplateDescription>(0),  // boilerplate
        Smi::FromInt(flags_raw));                   // flags
  }
}

void BaselineCompiler::VisitCreateEmptyObjectLiteral() {
  CallBuiltin<Builtin::kCreateEmptyLiteralObject>();
}

void BaselineCompiler::VisitCloneObject() {
  uint32_t flags = Flag8(1);
  int32_t raw_flags =
      interpreter::CreateObjectLiteralFlags::FlagsBits::decode(flags);
  CallBuiltin<Builtin::kCloneObjectICBaseline>(
      RegisterOperand(0),       // source
      Smi::FromInt(raw_flags),  // flags
      IndexAsTagged(2));        // slot
}

void BaselineCompiler::VisitGetTemplateObject() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  CallBuiltin<Builtin::kGetTemplateObject>(
      shared_function_info_,    // shared function info
      Constant<HeapObject>(0),  // description
      Index(1),                 // slot
      FeedbackVector());        // feedback_vector
}

void BaselineCompiler::VisitCreateClosure() {
  Register feedback_cell =
      FastNewClosureBaselineDescriptor::GetRegisterParameter(
          FastNewClosureBaselineDescriptor::kFeedbackCell);
  LoadClosureFeedbackArray(feedback_cell);
  __ LoadFixedArrayElement(feedback_cell, feedback_cell, Index(1));

  uint32_t flags = Flag8(2);
  if (interpreter::CreateClosureFlags::FastNewClosureBit::decode(flags)) {
    CallBuiltin<Builtin::kFastNewClosureBaseline>(
        Constant<SharedFunctionInfo>(0), feedback_cell);
  } else {
    Runtime::FunctionId function_id =
        interpreter::CreateClosureFlags::PretenuredBit::decode(flags)
            ? Runtime::kNewClosure_Tenured
            : Runtime::kNewClosure;
    CallRuntime(function_id, Constant<SharedFunctionInfo>(0), feedback_cell);
  }
}

void BaselineCompiler::VisitCreateBlockContext() {
  CallRuntime(Runtime::kPushBlockContext, Constant<ScopeInfo>(0));
}

void BaselineCompiler::VisitCreateCatchContext() {
  CallRuntime(Runtime::kPushCatchContext,
              RegisterOperand(0),  // exception
              Constant<ScopeInfo>(1));
}

void BaselineCompiler::VisitCreateFunctionContext() {
  Handle<ScopeInfo> info = Constant<ScopeInfo>(0);
  uint32_t slot_count = Uint(1);
  DCHECK_LE(slot_count, ConstructorBuiltins::MaximumFunctionContextSlots());
  DCHECK_EQ(info->scope_type(), ScopeType::FUNCTION_SCOPE);
  CallBuiltin<Builtin::kFastNewFunctionContextFunction>(info, slot_count);
}

void BaselineCompiler::VisitCreateEvalContext() {
  Handle<ScopeInfo> info = Constant<ScopeInfo>(0);
  uint32_t slot_count = Uint(1);
  if (slot_count < static_cast<uint32_t>(
                       ConstructorBuiltins::MaximumFunctionContextSlots())) {
    DCHECK_EQ(info->scope_type(), ScopeType::EVAL_SCOPE);
    CallBuiltin<Builtin::kFastNewFunctionContextEval>(info, slot_count);
  } else {
    CallRuntime(Runtime::kNewFunctionContext, Constant<ScopeInfo>(0));
  }
}

void BaselineCompiler::VisitCreateWithContext() {
  CallRuntime(Runtime::kPushWithContext,
              RegisterOperand(0),  // object
              Constant<ScopeInfo>(1));
}

void BaselineCompiler::VisitCreateMappedArguments() {
  if (shared_function_info_->has_duplicate_parameters()) {
    CallRuntime(Runtime::kNewSloppyArguments, __ FunctionOperand());
  } else {
    CallBuiltin<Builtin::kFastNewSloppyArguments>(__ FunctionOperand());
  }
}

void BaselineCompiler::VisitCreateUnmappedArguments() {
  CallBuiltin<Builtin::kFastNewStrictArguments>(__ FunctionOperand());
}

void BaselineCompiler::VisitCreateRestParameter() {
  CallBuiltin<Builtin::kFastNewRestArguments>(__ FunctionOperand());
}

void BaselineCompiler::VisitJumpLoop() {
#ifndef V8_JITLESS
  Label osr_armed, osr_not_armed;
  using D = OnStackReplacementDescriptor;
  Register feedback_vector = Register::no_reg();
  Register osr_state = Register::no_reg();
  const int loop_depth = iterator().GetImmediateOperand(1);
  {
    ASM_CODE_COMMENT_STRING(&masm_, "OSR Check Armed");
    BaselineAssembler::ScratchRegisterScope temps(&basm_);
    feedback_vector = temps.AcquireScratch();
    osr_state = temps.AcquireScratch();
    LoadFeedbackVector(feedback_vector);
    __ LoadWord8Field(osr_state, feedback_vector,
                      FeedbackVector::kOsrStateOffset);
    static_assert(FeedbackVector::MaybeHasMaglevOsrCodeBit::encode(true) >
                  FeedbackVector::kMaxOsrUrgency);
    static_assert(FeedbackVector::MaybeHasTurbofanOsrCodeBit::encode(true) >
                  FeedbackVector::kMaxOsrUrgency);
    __ JumpIfByte(kUnsignedGreaterThan, osr_state, loop_depth, &osr_armed,
                  Label::kNear);
  }

  __ Bind(&osr_not_armed);
#endif  // !V8_JITLESS
  Label* label = &labels_[iterator().GetJumpTargetOffset()];
  int weight = iterator().GetRelativeJumpTargetOffset() -
               iterator().current_bytecode_size_without_prefix();
  // We can pass in the same label twice since it's a back edge and thus already
  // bound.
  DCHECK(label->is_bound());
  UpdateInterruptBudgetAndJumpToLabel(weight, label, label, kEnableStackCheck);

#ifndef V8_JITLESS
  {
    // In case we deopt during the above interrupt check then this part of the
    // jump loop is skipped. This is not a problem as nothing observable happens
    // here.
#ifdef DEBUG
    effect_state_.safe_to_skip = true;
#endif

    ASM_CODE_COMMENT_STRING(&masm_, "OSR Handle Armed");
    __ Bind(&osr_armed);
    Register maybe_target_code = D::MaybeTargetCodeRegister();
    Label osr;
    {
      BaselineAssembler::ScratchRegisterScope temps(&basm_);
      Register scratch0 = temps.AcquireScratch();
      Register scratch1 = temps.AcquireScratch();
      DCHECK_EQ(scratch0, feedback_vector);
      DCHECK_EQ(scratch1, osr_state);
      DCHECK(!AreAliased(maybe_target_code, scratch0, scratch1));
      __ TryLoadOptimizedOsrCode(maybe_target_code, scratch0,
                                 iterator().GetSlotOperand(2), &osr,
                                 Label::kNear);
      __ DecodeField<FeedbackVector::OsrUrgencyBits>(scratch1);
      __ JumpIfByte(kUnsignedLessThanEqual, scratch1, loop_depth,
                    &osr_not_armed, Label::kNear);
    }

    __ Bind(&osr);
    Label do_osr;
    weight = bytecode_->length() * v8_flags.osr_to_tierup;
    __ Push(maybe_target_code);
    UpdateInterruptBudgetAndJumpToLabel(-weight, nullptr, &do_osr,
                                        kDisableStackCheck);
    __ Bind(&do_osr);
    Register expected_param_count = D::ExpectedParameterCountRegister();
    __ Move(expected_param_count, Smi::FromInt(bytecode_->parameter_count()));
    __ Pop(maybe_target_code);
    CallBuiltin<Builtin::kBaselineOnStackReplacement>(maybe_target_code,
                                                      expected_param_count);
    __ AddToInterruptBudgetAndJumpIfNotExceeded(weight, nullptr);
    __ Jump(&osr_not_armed, Label::kNear);

#ifdef DEBUG
    effect_state_.safe_to_skip = false;
#endif
  }
#endif  // !V8_JITLESS
}

void BaselineCompiler::VisitJump() { __ Jump(BuildForwardJumpLabel()); }

void BaselineCompiler::VisitJumpConstant() { VisitJump(); }

void BaselineCompiler::VisitJumpIfNullConstant() { VisitJumpIfNull(); }

void BaselineCompiler::VisitJumpIfNotNullConstant() { VisitJumpIfNotNull(); }

void BaselineCompiler::VisitJumpIfUndefinedConstant() {
  VisitJumpIfUndefined();
}

void BaselineCompiler::VisitJumpIfNotUndefinedConstant() {
  VisitJumpIfNotUndefined();
}

void BaselineCompiler::VisitJumpIfUndefinedOrNullConstant() {
  VisitJumpIfUndefinedOrNull();
}

void BaselineCompiler::VisitJumpIfTrueConstant() { VisitJumpIfTrue(); }

void BaselineCompiler::VisitJumpIfFalseConstant() { VisitJumpIfFalse(); }

void BaselineCompiler::VisitJumpIfJSReceiverConstant() {
  VisitJumpIfJSReceiver();
}

void BaselineCompiler::VisitJumpIfForInDoneConstant() {
  VisitJumpIfForInDone();
}

void BaselineCompiler::VisitJumpIfToBooleanTrueConstant() {
  VisitJumpIfToBooleanTrue();
}

void BaselineCompiler::VisitJumpIfToBooleanFalseConstant() {
  VisitJumpIfToBooleanFalse();
}

void BaselineCompiler::VisitJumpIfToBooleanTrue() {
  Label dont_jump;
  JumpIfToBoolean(false, &dont_jump, Label::kNear);
  __ Jump(BuildForwardJumpLabel());
  __ Bind(&dont_jump);
}

void BaselineCompiler::VisitJumpIfToBooleanFalse() {
  Label dont_jump;
  JumpIfToBoolean(true, &dont_jump, Label::kNear);
  __ Jump(BuildForwardJumpLabel());
  __ Bind(&dont_jump);
}

void BaselineCompiler::VisitJumpIfTrue() { JumpIfRoot(RootIndex::kTrueValue); }

void BaselineCompiler::VisitJumpIfFalse() {
  JumpIfRoot(RootIndex::kFalseValue);
}

void BaselineCompiler::VisitJumpIfNull() { JumpIfRoot(RootIndex::kNullValue); }

void BaselineCompiler::VisitJumpIfNotNull() {
  JumpIfNotRoot(RootIndex::kNullValue);
}

void BaselineCompiler::VisitJumpIfUndefined() {
  JumpIfRoot(RootIndex::kUndefinedValue);
}

void BaselineCompiler::VisitJumpIfNotUndefined() {
  JumpIfNotRoot(RootIndex::kUndefinedValue);
}

void BaselineCompiler::VisitJumpIfUndefinedOrNull() {
  Label do_jump, dont_jump;
  __ JumpIfRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue,
                &do_jump);
  __ JumpIfNotRoot(kInterpreterAccumulatorRegister, RootIndex::kNullValue,
                   &dont_jump, Label::kNear);
  __ Bind(&do_jump);
  __ Jump(BuildForwardJumpLabel());
  __ Bind(&dont_jump);
}

void BaselineCompiler::VisitJumpIfJSReceiver() {
  Label is_smi, dont_jump;
  __ JumpIfSmi(kInterpreterAccumulatorRegister, &is_smi, Label::kNear);

#if V8_STATIC_ROOTS_BOOL
  __ JumpIfJSAnyIsPrimitive(kInterpreterAccumulatorRegister, &dont_jump,
                            Label::Distance::kNear);
#else
  __ JumpIfObjectTypeFast(kLessThan, kInterpreterAccumulatorRegister,
                          FIRST_JS_RECEIVER_TYPE, &dont_jump);
#endif
  __ Jump(BuildForwardJumpLabel());

  __ Bind(&is_smi);
  __ Bind(&dont_jump);
}

void BaselineCompiler::VisitJumpIfForInDone() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register index = scratch_scope.AcquireScratch();
  LoadRegister(index, 1);
  __ JumpIfTagged(kEqual, index, __ RegisterFrameOperand(RegisterOperand(2)),
                  BuildForwardJumpLabel());
}

void BaselineCompiler::VisitSwitchOnSmiNoFeedback() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  interpreter::JumpTableTargetOffsets offsets =
      iterator().GetJumpTableTargetOffsets();

  if (offsets.size() == 0) return;

  int case_value_base = (*offsets.begin()).case_value;

  std::unique_ptr<Label*[]> labels = std::make_unique<Label*[]>(offsets.size());
  for (interpreter::JumpTableTargetOffset offset : offsets) {
    labels[offset.case_value - case_value_base] =
        EnsureLabel(offset.target_offset);
  }
  Register case_value = scratch_scope.AcquireScratch();
  __ SmiUntag(case_value, kInterpreterAccumulatorRegister);
  __ Switch(case_value, case_value_base, labels.get(), offsets.size());
}

void BaselineCompiler::VisitForInEnumerate() {
  CallBuiltin<Builtin::kForInEnumerate>(RegisterOperand(0));
}

void BaselineCompiler::VisitForInPrepare() {
  StoreRegister(0, kInterpreterAccumulatorRegister);
  CallBuiltin<Builtin::kForInPrepare>(kInterpreterAccumulatorRegister,
                                      IndexAsTagged(1), FeedbackVector());
  interpreter::Register first = iterator().GetRegisterOperand(0);
  interpreter::Register second(first.index() + 1);
  interpreter::Register third(first.index() + 2);
  __ StoreRegister(second, kReturnRegister0);
  __ StoreRegister(third, kReturnRegister1);
}

void BaselineCompiler::VisitForInNext() {
  interpreter::Register cache_type, cache_array;
  std::tie(cache_type, cache_array) = iterator().GetRegisterPairOperand(2);
  CallBuiltin<Builtin::kForInNext>(Index(3),            // vector slot
                                   RegisterOperand(0),  // object
                                   cache_array,         // cache array
                                   cache_type,          // cache type
                                   RegisterOperand(1),  // index
                                   FeedbackVector());   // feedback vector
}

void BaselineCompiler::VisitForInStep() {
  __ IncrementSmi(__ RegisterFrameOperand(RegisterOperand(0)));
}

void BaselineCompiler::VisitSetPendingMessage() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register pending_message = scratch_scope.AcquireScratch();
  __ Move(pending_message,
          ExternalReference::address_of_pending_message(local_isolate_));
  Register tmp = scratch_scope.AcquireScratch();
  __ Move(tmp, kInterpreterAccumulatorRegister);
  __ Move(kInterpreterAccumulatorRegister, MemOperand(pending_message, 0));
  __ Move(MemOperand(pending_message, 0), tmp);
}

void BaselineCompiler::VisitThrow() {
  CallRuntime(Runtime::kThrow, kInterpreterAccumulatorRegister);
  __ Trap();
}

void BaselineCompiler::VisitReThrow() {
  CallRuntime(Runtime::kReThrow, kInterpreterAccumulatorRegister);
  __ Trap();
}

void BaselineCompiler::VisitReturn() {
  ASM_CODE_COMMENT_STRING(&masm_, "Return");
  int profiling_weight = iterator().current_offset() +
                         iterator().current_bytecode_size_without_prefix();
  int parameter_count = bytecode_->parameter_count();

  TailCallBuiltin<Builtin::kBaselineLeaveFrame>(parameter_count,
                                                -profiling_weight);
}

void BaselineCompiler::VisitThrowReferenceErrorIfHole() {
  Label done;
  __ JumpIfNotRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue,
                   &done);
  CallRuntime(Runtime::kThrowAccessedUninitializedVariable, Constant<Name>(0));
  // Unreachable.
  __ Trap();
  __ Bind(&done);
}

void BaselineCompiler::VisitThrowSuperNotCalledIfHole() {
  Label done;
  __ JumpIfNotRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue,
                   &done);
  CallRuntime(Runtime::kThrowSuperNotCalled);
  // Unreachable.
  __ Trap();
  __ Bind(&done);
}

void BaselineCompiler::VisitThrowSuperAlreadyCalledIfNotHole() {
  Label done;
  __ JumpIfRoot(kInterpreterAccumulatorRegister, RootIndex::kTheHoleValue,
                &done);
  CallRuntime(Runtime::kThrowSuperAlreadyCalledError);
  // Unreachable.
  __ Trap();
  __ Bind(&done);
}

void BaselineCompiler::VisitThrowIfNotSuperConstructor() {
  Label done;

  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register reg = scratch_scope.AcquireScratch();
  LoadRegister(reg, 0);
  Register map_bit_field = scratch_scope.AcquireScratch();
  __ LoadMap(map_bit_field, reg);
  __ LoadWord8Field(map_bit_field, map_bit_field, Map::kBitFieldOffset);
  __ TestAndBranch(map_bit_field, Map::Bits1::IsConstructorBit::kMask, kNotZero,
                   &done, Label::kNear);

  CallRuntime(Runtime::kThrowNotSuperConstructor, reg, __ FunctionOperand());

  __ Bind(&done);
}

void BaselineCompiler::VisitSwitchOnGeneratorState() {
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);

  Label fallthrough;

  Register generator_object = scratch_scope.AcquireScratch();
  LoadRegister(generator_object, 0);
  __ JumpIfRoot(generator_object, RootIndex::kUndefinedValue, &fallthrough);

  Register continuation = scratch_scope.AcquireScratch();
  __ LoadTaggedSignedFieldAndUntag(continuation, generator_object,
                                   JSGeneratorObject::kContinuationOffset);
  __ StoreTaggedSignedField(
      generator_object, JSGeneratorObject::kContinuationOffset,
      Smi::FromInt(JSGeneratorObject::kGeneratorExecuting));

  Register context = scratch_scope.AcquireScratch();
  __ LoadTaggedField(context, generator_object,
                     JSGeneratorObject::kContextOffset);
  __ StoreContext(context);

  interpreter::JumpTableTargetOffsets offsets =
      iterator().GetJumpTableTargetOffsets();

  if (0 < offsets.size()) {
    DCHECK_EQ(0, (*offsets.begin()).case_value);

    std::unique_ptr<Label*[]> labels =
        std::make_unique<Label*[]>(offsets.size());
    for (interpreter::JumpTableTargetOffset offset : offsets) {
      labels[offset.case_value] = EnsureLabel(offset.target_offset);
    }
    __ Switch(continuation, 0, labels.get(), offsets.size());
    // We should never fall through this switch.
    // TODO(v8:11429,leszeks): Maybe remove the fallthrough check in the Switch?
    __ Trap();
  }

  __ Bind(&fallthrough);
}

void BaselineCompiler::VisitSuspendGenerator() {
  DCHECK_EQ(iterator().GetRegisterOperand(1), interpreter::Register(0));
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register generator_object = scratch_scope.AcquireScratch();
  LoadRegister(generator_object, 0);
  {
    SaveAccumulatorScope accumulator_scope(this, &basm_);

    int bytecode_offset =
        BytecodeArray::kHeaderSize + iterator().current_offset();
    CallBuiltin<Builtin::kSuspendGeneratorBaseline>(
        generator_object,
        static_cast<int>(Uint(3)),  // suspend_id
        bytecode_offset,
        static_cast<int>(RegisterCount(2)));  // register_count
  }
  int parameter_count = bytecode_->parameter_count();

  TailCallBuiltin<Builtin::kBaselineLeaveFrame>(parameter_count, 0);
}

void BaselineCompiler::VisitResumeGenerator() {
  DCHECK_EQ(iterator().GetRegisterOperand(1), interpreter::Register(0));
  BaselineAssembler::ScratchRegisterScope scratch_scope(&basm_);
  Register generator_object = scratch_scope.AcquireScratch();
  LoadRegister(generator_object, 0);
  CallBuiltin<Builtin::kResumeGeneratorBaseline>(
      generator_object,
      static_cast<int>(RegisterCount(2)));  // register_count
}

void BaselineCompiler::VisitGetIterator() {
  CallBuiltin<Builtin::kGetIteratorBaseline>(RegisterOperand(0),  // receiver
                                             IndexAsTagged(1),    // load_slot
                                             IndexAsTagged(2));   // call_slot
}

void BaselineCompiler::VisitDebugger() {
  CallRuntime(Runtime::kHandleDebuggerStatement);
}

void BaselineCompiler::VisitIncBlockCounter() {
  SaveAccumulatorScope accumulator_scope(this, &basm_);
  CallBuiltin<Builtin::kIncBlockCounter>(__ FunctionOperand(),
                                         IndexAsSmi(0));  // coverage array slot
}

void BaselineCompiler::VisitAbort() {
  CallRuntime(Runtime::kAbort, Smi::FromInt(Index(0)));
  __ Trap();
}

void BaselineCompiler::VisitWide() {
  // Consumed by the BytecodeArrayIterator.
  UNREACHABLE();
}

void BaselineCompiler::VisitExtraWide() {
  // Consumed by the BytecodeArrayIterator.
  UNREACHABLE();
}

void BaselineCompiler::VisitIllegal() {
  // Not emitted in valid bytecode.
  UNREACHABLE();
}
#define DEBUG_BREAK(Name, ...) \
  void BaselineCompiler::Visit##Name() { UNREACHABLE(); }
DEBUG_BREAK_BYTECODE_LIST(DEBUG_BREAK)
#undef DEBUG_BREAK

SaveAccumulatorScope::SaveAccumulatorScope(BaselineCompiler* compiler,
                                           BaselineAssembler* assembler)
    :
#ifdef DEBUG
      compiler_(compiler),
#endif
      assembler_(assembler) {
#ifdef DEBUG
  DCHECK(!compiler_->effect_state_.accumulator_on_stack);
  compiler_->effect_state_.accumulator_on_stack = true;
#endif  // DEBUG
  ASM_CODE_COMMENT(assembler_->masm());
  assembler_->Push(kInterpreterAccumulatorRegister);
}

SaveAccumulatorScope::~SaveAccumulatorScope() {
#ifdef DEBUG
  DCHECK(compiler_->effect_state_.accumulator_on_stack);
  compiler_->effect_state_.accumulator_on_stack = false;
#endif  // DEBUG
  ASM_CODE_COMMENT(assembler_->masm());
  assembler_->Pop(kInterpreterAccumulatorRegister);
}

#undef RCS_BASELINE_SCOPE
#undef __

}  // namespace baseline
}  // namespace internal
}  // namespace v8
