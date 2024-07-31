// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_
#define V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_

#include <algorithm>
#include <type_traits>

#include "src/base/iterator.h"
#include "src/base/template-utils.h"
#include "src/codegen/machine-type.h"
#include "src/maglev/maglev-assembler.h"

#ifdef V8_TARGET_ARCH_ARM
#include "src/maglev/arm/maglev-assembler-arm-inl.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/maglev/arm64/maglev-assembler-arm64-inl.h"
#elif V8_TARGET_ARCH_X64
#include "src/maglev/x64/maglev-assembler-x64-inl.h"
#else
#error "Maglev does not supported this architecture."
#endif

namespace v8 {
namespace internal {
namespace maglev {

namespace detail {

// Base case provides an error.
template <typename T, typename Enable = void>
struct CopyForDeferredHelper {
  template <typename U>
  struct No_Copy_Helper_Implemented_For_Type;
  static void Copy(MaglevCompilationInfo* compilation_info,
                   No_Copy_Helper_Implemented_For_Type<T>);
};

// Helper for copies by value.
template <typename T, typename Enable = void>
struct CopyForDeferredByValue {
  static T Copy(MaglevCompilationInfo* compilation_info, T node) {
    return node;
  }
};

// Node pointers are copied by value.
template <typename T>
struct CopyForDeferredHelper<
    T*, typename std::enable_if<std::is_base_of<NodeBase, T>::value>::type>
    : public CopyForDeferredByValue<T*> {};
// Arithmetic values and enums are copied by value.
template <typename T>
struct CopyForDeferredHelper<
    T, typename std::enable_if<std::is_arithmetic<T>::value>::type>
    : public CopyForDeferredByValue<T> {};
template <typename T>
struct CopyForDeferredHelper<
    T, typename std::enable_if<std::is_enum<T>::value>::type>
    : public CopyForDeferredByValue<T> {};
// MaglevCompilationInfos are copied by value.
template <>
struct CopyForDeferredHelper<MaglevCompilationInfo*>
    : public CopyForDeferredByValue<MaglevCompilationInfo*> {};
// Machine registers are copied by value.
template <>
struct CopyForDeferredHelper<Register>
    : public CopyForDeferredByValue<Register> {};
template <>
struct CopyForDeferredHelper<DoubleRegister>
    : public CopyForDeferredByValue<DoubleRegister> {};
// Bytecode offsets are copied by value.
template <>
struct CopyForDeferredHelper<BytecodeOffset>
    : public CopyForDeferredByValue<BytecodeOffset> {};
// EagerDeoptInfo pointers are copied by value.
template <>
struct CopyForDeferredHelper<EagerDeoptInfo*>
    : public CopyForDeferredByValue<EagerDeoptInfo*> {};
// LazyDeoptInfo pointers are copied by value.
template <>
struct CopyForDeferredHelper<LazyDeoptInfo*>
    : public CopyForDeferredByValue<LazyDeoptInfo*> {};
// ZoneLabelRef is copied by value.
template <>
struct CopyForDeferredHelper<ZoneLabelRef>
    : public CopyForDeferredByValue<ZoneLabelRef> {};
// MapCompare is copied by value.
template <>
struct CopyForDeferredHelper<MapCompare>
    : public CopyForDeferredByValue<MapCompare> {};
// RegList are copied by value.
template <>
struct CopyForDeferredHelper<RegList> : public CopyForDeferredByValue<RegList> {
};
// Register snapshots are copied by value.
template <>
struct CopyForDeferredHelper<RegisterSnapshot>
    : public CopyForDeferredByValue<RegisterSnapshot> {};
// Feedback slots are copied by value.
template <>
struct CopyForDeferredHelper<FeedbackSlot>
    : public CopyForDeferredByValue<FeedbackSlot> {};
// Heap Refs are copied by value.
template <typename T>
struct CopyForDeferredHelper<T, typename std::enable_if<std::is_base_of<
                                    compiler::ObjectRef, T>::value>::type>
    : public CopyForDeferredByValue<T> {};

template <typename T>
T CopyForDeferred(MaglevCompilationInfo* compilation_info, T&& value) {
  return CopyForDeferredHelper<T>::Copy(compilation_info,
                                        std::forward<T>(value));
}

template <typename T>
T CopyForDeferred(MaglevCompilationInfo* compilation_info, T& value) {
  return CopyForDeferredHelper<T>::Copy(compilation_info, value);
}

template <typename T>
T CopyForDeferred(MaglevCompilationInfo* compilation_info, const T& value) {
  return CopyForDeferredHelper<T>::Copy(compilation_info, value);
}

template <typename Function>
struct FunctionArgumentsTupleHelper
    : public FunctionArgumentsTupleHelper<decltype(&Function::operator())> {};

template <typename C, typename R, typename... A>
struct FunctionArgumentsTupleHelper<R (C::*)(A...) const> {
  using FunctionPointer = R (*)(A...);
  using Tuple = std::tuple<A...>;
  static constexpr size_t kSize = sizeof...(A);
};

template <typename R, typename... A>
struct FunctionArgumentsTupleHelper<R (&)(A...)> {
  using FunctionPointer = R (*)(A...);
  using Tuple = std::tuple<A...>;
  static constexpr size_t kSize = sizeof...(A);
};

template <typename T>
struct StripFirstTupleArg;

template <typename T1, typename... T>
struct StripFirstTupleArg<std::tuple<T1, T...>> {
  using Stripped = std::tuple<T...>;
};

template <typename Function>
class DeferredCodeInfoImpl final : public DeferredCodeInfo {
 public:
  using FunctionPointer =
      typename FunctionArgumentsTupleHelper<Function>::FunctionPointer;
  using Tuple = typename StripFirstTupleArg<
      typename FunctionArgumentsTupleHelper<Function>::Tuple>::Stripped;

  template <typename... InArgs>
  explicit DeferredCodeInfoImpl(MaglevCompilationInfo* compilation_info,
                                RegList general_temporaries,
                                DoubleRegList double_temporaries,
                                FunctionPointer function, InArgs&&... args)
      : function(function),
        args(CopyForDeferred(compilation_info, std::forward<InArgs>(args))...),
        general_temporaries_(general_temporaries),
        double_temporaries_(double_temporaries) {}

  DeferredCodeInfoImpl(DeferredCodeInfoImpl&&) = delete;
  DeferredCodeInfoImpl(const DeferredCodeInfoImpl&) = delete;

  void Generate(MaglevAssembler* masm) override {
    MaglevAssembler::ScratchRegisterScope scratch_scope(masm);
    scratch_scope.SetAvailable(general_temporaries_);
    scratch_scope.SetAvailableDouble(double_temporaries_);
#ifdef DEBUG
    masm->set_allow_call(allow_call_);
    masm->set_allow_deferred_call(allow_call_);
    masm->set_allow_allocate(allow_allocate_);
#endif  // DEBUG
    std::apply(function,
               std::tuple_cat(std::make_tuple(masm), std::move(args)));
#ifdef DEBUG
    masm->set_allow_call(false);
    masm->set_allow_deferred_call(false);
    masm->set_allow_allocate(false);
#endif  // DEBUG
  }

#ifdef DEBUG
  void set_allow_call(bool value) { allow_call_ = value; }
  void set_allow_allocate(bool value) { allow_allocate_ = value; }
#endif  // DEBUG

 private:
  FunctionPointer function;
  Tuple args;
  RegList general_temporaries_;
  DoubleRegList double_temporaries_;

#ifdef DEBUG
  bool allow_call_ = false;
  bool allow_allocate_ = false;
#endif  // DEBUG
};

}  // namespace detail

template <typename Function, typename... Args>
inline Label* MaglevAssembler::MakeDeferredCode(Function&& deferred_code_gen,
                                                Args&&... args) {
  using FunctionPointer =
      typename detail::FunctionArgumentsTupleHelper<Function>::FunctionPointer;
  static_assert(
      std::is_invocable_v<FunctionPointer, MaglevAssembler*,
                          decltype(detail::CopyForDeferred(
                              std::declval<MaglevCompilationInfo*>(),
                              std::declval<Args>()))...>,
      "Parameters of deferred_code_gen function should match arguments into "
      "MakeDeferredCode");

  ScratchRegisterScope scratch_scope(this);
  using DeferredCodeInfoT = detail::DeferredCodeInfoImpl<Function>;
  DeferredCodeInfoT* deferred_code =
      compilation_info()->zone()->New<DeferredCodeInfoT>(
          compilation_info(), scratch_scope.Available(),
          scratch_scope.AvailableDouble(), deferred_code_gen,
          std::forward<Args>(args)...);

#ifdef DEBUG
  deferred_code->set_allow_call(allow_deferred_call_);
  deferred_code->set_allow_allocate(allow_allocate_);
#endif  // DEBUG

  code_gen_state()->PushDeferredCode(deferred_code);
  return &deferred_code->deferred_code_label;
}

// Note this doesn't take capturing lambdas by design, since state may
// change until `deferred_code_gen` is actually executed. Use either a
// non-capturing lambda, or a plain function pointer.
template <typename Function, typename... Args>
inline void MaglevAssembler::JumpToDeferredIf(Condition cond,
                                              Function&& deferred_code_gen,
                                              Args&&... args) {
  if (v8_flags.code_comments) {
    RecordComment("-- Jump to deferred code");
  }
  JumpIf(cond, MakeDeferredCode<Function, Args...>(
                   std::forward<Function>(deferred_code_gen),
                   std::forward<Args>(args)...));
}

inline void MaglevAssembler::SmiToDouble(DoubleRegister result, Register smi) {
  AssertSmi(smi);
  SmiUntag(smi);
  Int32ToDouble(result, smi);
}

inline void MaglevAssembler::CompareInt32AndBranch(Register r1, int32_t value,
                                                   Condition cond,
                                                   BasicBlock* if_true,
                                                   BasicBlock* if_false,
                                                   BasicBlock* next_block) {
  CompareInt32AndBranch(r1, value, cond, if_true->label(), Label::kFar,
                        if_true == next_block, if_false->label(), Label::kFar,
                        if_false == next_block);
}

inline void MaglevAssembler::CompareInt32AndBranch(Register r1, Register r2,
                                                   Condition cond,
                                                   BasicBlock* if_true,
                                                   BasicBlock* if_false,
                                                   BasicBlock* next_block) {
  CompareInt32AndBranch(r1, r2, cond, if_true->label(), Label::kFar,
                        if_true == next_block, if_false->label(), Label::kFar,
                        if_false == next_block);
}

inline void MaglevAssembler::Branch(Condition condition, BasicBlock* if_true,
                                    BasicBlock* if_false,
                                    BasicBlock* next_block) {
  Branch(condition, if_true->label(), Label::kFar, if_true == next_block,
         if_false->label(), Label::kFar, if_false == next_block);
}

inline void MaglevAssembler::Branch(Condition condition, Label* if_true,
                                    Label::Distance true_distance,
                                    bool fallthrough_when_true, Label* if_false,
                                    Label::Distance false_distance,
                                    bool fallthrough_when_false) {
  if (fallthrough_when_false) {
    if (fallthrough_when_true) {
      // If both paths are a fallthrough, do nothing.
      DCHECK_EQ(if_true, if_false);
      return;
    }
    // Jump over the false block if true, otherwise fall through into it.
    JumpIf(condition, if_true, true_distance);
  } else {
    // Jump to the false block if true.
    JumpIf(NegateCondition(condition), if_false, false_distance);
    // Jump to the true block if it's not the next block.
    if (!fallthrough_when_true) {
      Jump(if_true, true_distance);
    }
  }
}

inline void MaglevAssembler::LoadTaggedField(Register result,
                                             MemOperand operand) {
  MacroAssembler::LoadTaggedField(result, operand);
}

inline void MaglevAssembler::LoadTaggedField(Register result, Register object,
                                             int offset) {
  MacroAssembler::LoadTaggedField(result, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::LoadTaggedFieldWithoutDecompressing(
    Register result, Register object, int offset) {
  MacroAssembler::LoadTaggedFieldWithoutDecompressing(
      result, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::LoadTaggedSignedField(Register result,
                                                   MemOperand operand) {
  MacroAssembler::LoadTaggedField(result, operand);
}

inline void MaglevAssembler::LoadTaggedSignedField(Register result,
                                                   Register object,
                                                   int offset) {
  MacroAssembler::LoadTaggedField(result, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::LoadAndUntagTaggedSignedField(Register result,
                                                           Register object,
                                                           int offset) {
  MacroAssembler::SmiUntagField(result, FieldMemOperand(object, offset));
}

inline void MaglevAssembler::LoadHeapNumberOrOddballValue(DoubleRegister result,
                                                          Register object) {
  static_assert(offsetof(HeapNumber, value_) ==
                offsetof(Oddball, to_number_raw_));
  LoadHeapNumberValue(result, object);
}

namespace detail {

#ifdef DEBUG
inline bool ClobberedBy(RegList written_registers, Register reg) {
  return written_registers.has(reg);
}
inline bool ClobberedBy(RegList written_registers, DoubleRegister reg) {
  return false;
}
inline bool ClobberedBy(RegList written_registers, Handle<Object> handle) {
  return false;
}
inline bool ClobberedBy(RegList written_registers, Tagged<Smi> smi) {
  return false;
}
inline bool ClobberedBy(RegList written_registers, Tagged<TaggedIndex> index) {
  return false;
}
inline bool ClobberedBy(RegList written_registers, int32_t imm) {
  return false;
}
inline bool ClobberedBy(RegList written_registers, RootIndex index) {
  return false;
}
inline bool ClobberedBy(RegList written_registers, const Input& input) {
  if (!input.IsGeneralRegister()) return false;
  return ClobberedBy(written_registers, input.AssignedGeneralRegister());
}

inline bool ClobberedBy(DoubleRegList written_registers, Register reg) {
  return false;
}
inline bool ClobberedBy(DoubleRegList written_registers, DoubleRegister reg) {
  return written_registers.has(reg);
}
inline bool ClobberedBy(DoubleRegList written_registers,
                        Handle<Object> handle) {
  return false;
}
inline bool ClobberedBy(DoubleRegList written_registers, Tagged<Smi> smi) {
  return false;
}
inline bool ClobberedBy(DoubleRegList written_registers,
                        Tagged<TaggedIndex> index) {
  return false;
}
inline bool ClobberedBy(DoubleRegList written_registers, int32_t imm) {
  return false;
}
inline bool ClobberedBy(DoubleRegList written_registers, RootIndex index) {
  return false;
}
inline bool ClobberedBy(DoubleRegList written_registers, const Input& input) {
  if (!input.IsDoubleRegister()) return false;
  return ClobberedBy(written_registers, input.AssignedDoubleRegister());
}

// We don't know what's inside machine registers or operands, so assume they
// match.
inline bool MachineTypeMatches(MachineType type, Register reg) {
  return !IsFloatingPoint(type.representation());
}
inline bool MachineTypeMatches(MachineType type, DoubleRegister reg) {
  return IsFloatingPoint(type.representation());
}
inline bool MachineTypeMatches(MachineType type, MemOperand reg) {
  return true;
}
inline bool MachineTypeMatches(MachineType type, Handle<HeapObject> handle) {
  return type.IsTagged() && !type.IsTaggedSigned();
}
inline bool MachineTypeMatches(MachineType type, Tagged<Smi> smi) {
  return type.IsTagged() && !type.IsTaggedPointer();
}
inline bool MachineTypeMatches(MachineType type, Tagged<TaggedIndex> index) {
  // TaggedIndex doesn't have a separate type, so check for the same type as for
  // Smis.
  return type.IsTagged() && !type.IsTaggedPointer();
}
inline bool MachineTypeMatches(MachineType type, int32_t imm) {
  // 32-bit immediates can be used for 64-bit params -- they'll be
  // zero-extended.
  return type.representation() == MachineRepresentation::kWord32 ||
         type.representation() == MachineRepresentation::kWord64;
}
inline bool MachineTypeMatches(MachineType type, RootIndex index) {
  return type.IsTagged() && !type.IsTaggedSigned();
}
inline bool MachineTypeMatches(MachineType type, const Input& input) {
  if (type.representation() == input.node()->GetMachineRepresentation()) {
    return true;
  }
  if (type.IsTagged()) {
    return input.node()->is_tagged();
  }
  return false;
}

template <typename Descriptor, typename Arg>
void CheckArg(MaglevAssembler* masm, Arg& arg, int& i) {
  if (i >= Descriptor::GetParameterCount()) {
    CHECK(Descriptor::AllowVarArgs());
  }
  CHECK(MachineTypeMatches(Descriptor::GetParameterType(i), arg));
  ++i;
}

template <typename Descriptor, typename Iterator>
void CheckArg(MaglevAssembler* masm,
              const base::iterator_range<Iterator>& range, int& i) {
  for (auto it = range.begin(), end = range.end(); it != end; ++it, ++i) {
    if (i >= Descriptor::GetParameterCount()) {
      CHECK(Descriptor::AllowVarArgs());
    }
    CHECK(MachineTypeMatches(Descriptor::GetParameterType(i), *it));
  }
}

template <typename Descriptor, typename... Args>
void CheckArgs(MaglevAssembler* masm, const std::tuple<Args...>& args) {
  int i = 0;
  base::tuple_for_each(args,
                       [&](auto&& arg) { CheckArg<Descriptor>(masm, arg, i); });
  if (Descriptor::AllowVarArgs()) {
    CHECK_GE(i, Descriptor::GetParameterCount());
  } else {
    CHECK_EQ(i, Descriptor::GetParameterCount());
  }
}

#else  // DEBUG

template <typename Descriptor, typename... Args>
void CheckArgs(Args&&... args) {}

#endif  // DEBUG

template <typename Descriptor, typename... Args>
void PushArgumentsForBuiltin(MaglevAssembler* masm, std::tuple<Args...> args) {
  std::apply(
      [&](auto&&... stack_args) {
        if (Descriptor::kStackArgumentOrder == StackArgumentOrder::kDefault) {
          masm->Push(std::forward<decltype(stack_args)>(stack_args)...);
        } else {
          masm->PushReverse(std::forward<decltype(stack_args)>(stack_args)...);
        }
      },
      args);
}

template <typename Descriptor>
void PushArgumentsForBuiltin(MaglevAssembler* masm, std::tuple<> empty_args) {}

template <Builtin kBuiltin, typename... Args>
void MoveArgumentsForBuiltin(MaglevAssembler* masm, Args&&... args) {
  using Descriptor = typename CallInterfaceDescriptorFor<kBuiltin>::type;

  // Put the args into a tuple for easier manipulation.
  std::tuple<Args&&...> args_tuple{std::forward<Args>(args)...};

  // If there is a context, the first argument is the context parameter. Use
  // the remaining args as the actual arguments. We pass the context first
  // instead of last to avoid ambiguity around dealing with on-stack
  // arguments.
  constexpr size_t context_args = Descriptor::HasContextParameter() ? 1 : 0;
  static_assert(context_args <= std::tuple_size_v<decltype(args_tuple)>,
                "Not enough arguments passed in to builtin (are you missing a "
                "context argument?)");
  auto args_tuple_without_context = base::tuple_drop<context_args>(args_tuple);
  CheckArgs<Descriptor>(masm, args_tuple_without_context);

  // Split args into register and stack args.
  static_assert(Descriptor::GetRegisterParameterCount() <=
                    std::tuple_size_v<decltype(args_tuple_without_context)>,
                "Not enough arguments passed in to builtin (are you missing a "
                "context argument?)");
  auto register_args =
      base::tuple_head<Descriptor::GetRegisterParameterCount()>(
          args_tuple_without_context);
  auto stack_args = base::tuple_drop<Descriptor::GetRegisterParameterCount()>(
      args_tuple_without_context);

  // Split stack args into fixed and variable.
  static_assert(
      Descriptor::GetStackParameterCount() <=
          std::tuple_size_v<decltype(stack_args)>,
      "Not enough stack arguments passed in to builtin (are you missing a "
      "context argument?)");
  auto fixed_stack_args =
      base::tuple_head<Descriptor::GetStackParameterCount()>(stack_args);
  auto vararg_stack_args =
      base::tuple_drop<Descriptor::GetStackParameterCount()>(stack_args);

  if constexpr (!Descriptor::AllowVarArgs()) {
    static_assert(std::tuple_size_v<decltype(vararg_stack_args)> == 0,
                  "Too many arguments passed in to builtin that expects no "
                  "vararg stack arguments");
  }

  // First push stack arguments (if any), since some of these may be in
  // registers and we don't want to clobber them. This supports any thing
  // `masm->Push` supports, including iterator ranges, so the tuple size may be
  // smaller than the number of arguments actually pushed. We push fixed and
  // vararg stack arguments separately, so that there's an appropriate amount
  // of padding between them.
  if (Descriptor::kStackArgumentOrder == StackArgumentOrder::kDefault) {
    PushArgumentsForBuiltin<Descriptor>(
        masm, std::forward<decltype(fixed_stack_args)>(fixed_stack_args));
    PushArgumentsForBuiltin<Descriptor>(
        masm, std::forward<decltype(vararg_stack_args)>(vararg_stack_args));
  } else {
    PushArgumentsForBuiltin<Descriptor>(
        masm, std::forward<decltype(vararg_stack_args)>(vararg_stack_args));
    PushArgumentsForBuiltin<Descriptor>(
        masm, std::forward<decltype(fixed_stack_args)>(fixed_stack_args));
  }

// Then, set register arguments.
// TODO(leszeks): Use the parallel move helper to do register moves, instead
// of detecting clobbering.
#ifdef DEBUG
  RegList written_registers = {};
  DoubleRegList written_double_registers = {};
#endif  // DEBUG

  base::tuple_for_each_with_index(register_args, [&](auto&& arg, auto index) {
    using Arg = decltype(arg);
    static_assert(index < Descriptor::GetRegisterParameterCount());

    // Make sure the argument wasn't clobbered by any previous write.
    DCHECK(!ClobberedBy(written_registers, arg));
    DCHECK(!ClobberedBy(written_double_registers, arg));

    static constexpr bool use_double_register =
        IsFloatingPoint(Descriptor::GetParameterType(index).representation());
    if constexpr (use_double_register) {
      DoubleRegister target = Descriptor::GetDoubleRegisterParameter(index);
      if constexpr (std::is_same_v<Input, std::decay_t<Arg>>) {
        DCHECK_EQ(target, arg.AssignedDoubleRegister());
        USE(target);
      } else {
        masm->Move(target, std::forward<Arg>(arg));
      }
#ifdef DEBUG
      written_double_registers.set(target);
#endif  // DEBUG
    } else {
      Register target = Descriptor::GetRegisterParameter(index);
      if constexpr (std::is_same_v<Input, std::decay_t<Arg>>) {
        DCHECK_EQ(target, arg.AssignedGeneralRegister());
        USE(target);
      } else {
        masm->Move(target, std::forward<Arg>(arg));
      }
#ifdef DEBUG
      written_registers.set(target);
#endif  // DEBUG
    }

    // TODO(leszeks): Support iterator range for register args.
  });

  // Set the context last (to avoid clobbering).
  if constexpr (Descriptor::HasContextParameter()) {
    auto&& context = std::get<0>(args_tuple);
    DCHECK(!ClobberedBy(written_registers, context));
    DCHECK(!ClobberedBy(written_double_registers, context));
    DCHECK(MachineTypeMatches(MachineType::AnyTagged(), context));

    if constexpr (std::is_same_v<Input, std::decay_t<decltype(context)>>) {
      DCHECK_EQ(Descriptor::ContextRegister(),
                context.AssignedGeneralRegister());
    } else {
      // Don't allow raw Register here, force materialisation from a constant.
      // This is because setting parameters could have clobbered the register.
      // TODO(leszeks): Include the context register in the parallel moves
      // described above.
      static_assert(!std::is_same_v<Register, std::decay_t<decltype(context)>>);
      masm->Move(Descriptor::ContextRegister(), context);
    }
  }
}

}  // namespace detail

inline void MaglevAssembler::CallBuiltin(Builtin builtin) {
  // Special case allowing calls to DoubleToI, which takes care to preserve all
  // registers and therefore doesn't require special spill handling.
  DCHECK(allow_call() || builtin == Builtin::kDoubleToI);

  // Temporaries have to be reset before calling CallBuiltin, in case it uses
  // temporaries that alias register parameters.
  ScratchRegisterScope reset_temps(this);
  reset_temps.ResetToDefault();

  // Make sure that none of the register parameters alias the default
  // temporaries.
#ifdef DEBUG
  CallInterfaceDescriptor descriptor =
      Builtins::CallInterfaceDescriptorFor(builtin);
  for (int i = 0; i < descriptor.GetRegisterParameterCount(); ++i) {
    DCHECK(!reset_temps.Available().has(descriptor.GetRegisterParameter(i)));
  }
#endif

  MacroAssembler::CallBuiltin(builtin);
}

template <Builtin kBuiltin, typename... Args>
inline void MaglevAssembler::CallBuiltin(Args&&... args) {
  ASM_CODE_COMMENT(this);
  detail::MoveArgumentsForBuiltin<kBuiltin>(this, std::forward<Args>(args)...);
  CallBuiltin(kBuiltin);
}

inline void MaglevAssembler::CallRuntime(Runtime::FunctionId fid) {
  DCHECK(allow_call());
  // Temporaries have to be reset before calling CallRuntime, in case it uses
  // temporaries that alias register parameters.
  ScratchRegisterScope reset_temps(this);
  reset_temps.ResetToDefault();
  MacroAssembler::CallRuntime(fid);
}

inline void MaglevAssembler::CallRuntime(Runtime::FunctionId fid,
                                         int num_args) {
  DCHECK(allow_call());
  // Temporaries have to be reset before calling CallRuntime, in case it uses
  // temporaries that alias register parameters.
  ScratchRegisterScope reset_temps(this);
  reset_temps.ResetToDefault();
  MacroAssembler::CallRuntime(fid, num_args);
}

inline void MaglevAssembler::SetMapAsRoot(Register object, RootIndex map) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.GetDefaultScratchRegister();
  LoadTaggedRoot(scratch, map);
  StoreTaggedFieldNoWriteBarrier(object, HeapObject::kMapOffset, scratch);
}

inline void MaglevAssembler::SmiTagInt32AndJumpIfFail(
    Register dst, Register src, Label* fail, Label::Distance distance) {
  SmiTagInt32AndSetFlags(dst, src);
  if (!SmiValuesAre32Bits()) {
    JumpIf(kOverflow, fail, distance);
  }
}

inline void MaglevAssembler::SmiTagInt32AndJumpIfFail(
    Register reg, Label* fail, Label::Distance distance) {
  SmiTagInt32AndJumpIfFail(reg, reg, fail, distance);
}

inline void MaglevAssembler::SmiTagInt32AndJumpIfSuccess(
    Register dst, Register src, Label* success, Label::Distance distance) {
  SmiTagInt32AndSetFlags(dst, src);
  if (!SmiValuesAre32Bits()) {
    JumpIf(kNoOverflow, success, distance);
  } else {
    jmp(success);
  }
}

inline void MaglevAssembler::SmiTagInt32AndJumpIfSuccess(
    Register reg, Label* success, Label::Distance distance) {
  SmiTagInt32AndJumpIfSuccess(reg, reg, success, distance);
}

inline void MaglevAssembler::UncheckedSmiTagInt32(Register dst, Register src) {
  SmiTagInt32AndSetFlags(dst, src);
  Assert(kNoOverflow, AbortReason::kInputDoesNotFitSmi);
}

inline void MaglevAssembler::UncheckedSmiTagInt32(Register reg) {
  UncheckedSmiTagInt32(reg, reg);
}

inline void MaglevAssembler::SmiTagUint32AndJumpIfFail(
    Register dst, Register src, Label* fail, Label::Distance distance) {
  // Perform an unsigned comparison against Smi::kMaxValue.
  CompareInt32AndJumpIf(src, Smi::kMaxValue, kUnsignedGreaterThan, fail,
                        distance);
  SmiTagInt32AndSetFlags(dst, src);
  Assert(kNoOverflow, AbortReason::kInputDoesNotFitSmi);
}

inline void MaglevAssembler::SmiTagUint32AndJumpIfFail(
    Register reg, Label* fail, Label::Distance distance) {
  SmiTagUint32AndJumpIfFail(reg, reg, fail, distance);
}

inline void MaglevAssembler::SmiTagUint32AndJumpIfSuccess(
    Register dst, Register src, Label* success, Label::Distance distance) {
  Label fail;
  SmiTagUint32AndJumpIfFail(dst, src, &fail, Label::Distance::kNear);
  Jump(success, distance);
  bind(&fail);
}

inline void MaglevAssembler::SmiTagUint32AndJumpIfSuccess(
    Register reg, Label* success, Label::Distance distance) {
  SmiTagUint32AndJumpIfSuccess(reg, reg, success, distance);
}

inline void MaglevAssembler::UncheckedSmiTagUint32(Register dst, Register src) {
  if (v8_flags.debug_code) {
    // Perform an unsigned comparison against Smi::kMaxValue.
    CompareInt32AndAssert(src, Smi::kMaxValue, kUnsignedLessThanEqual,
                          AbortReason::kInputDoesNotFitSmi);
  }
  SmiTagInt32AndSetFlags(dst, src);
  Assert(kNoOverflow, AbortReason::kInputDoesNotFitSmi);
}

inline void MaglevAssembler::UncheckedSmiTagUint32(Register reg) {
  UncheckedSmiTagUint32(reg, reg);
}

inline void MaglevAssembler::SmiAddConstant(Register reg, int value,
                                            Label* fail,
                                            Label::Distance distance) {
  return SmiAddConstant(reg, reg, value, fail, distance);
}

inline void MaglevAssembler::SmiSubConstant(Register reg, int value,
                                            Label* fail,
                                            Label::Distance distance) {
  return SmiSubConstant(reg, reg, value, fail, distance);
}

inline void MaglevAssembler::JumpIfStringMap(Register map, Label* target,
                                             Label::Distance distance,
                                             bool jump_if_true) {
#if V8_STATIC_ROOTS_BOOL
  // All string maps are allocated at the start of the read only heap. Thus,
  // non-strings must have maps with larger (compressed) addresses.
  CompareInt32AndJumpIf(
      map, InstanceTypeChecker::kLastStringMap,
      jump_if_true ? kUnsignedLessThanEqual : kUnsignedGreaterThan, target,
      distance);
#else
#ifdef V8_COMPRESS_POINTERS
  DecompressTagged(map, map);
#endif
  static_assert(FIRST_STRING_TYPE == FIRST_TYPE);
  CompareInstanceType(map, LAST_STRING_TYPE);
  JumpIf(jump_if_true ? kUnsignedLessThanEqual : kUnsignedGreaterThan, target,
         distance);
#endif
}

inline void MaglevAssembler::JumpIfString(Register heap_object, Label* target,
                                          Label::Distance distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.GetDefaultScratchRegister();
#ifdef V8_COMPRESS_POINTERS
  LoadCompressedMap(scratch, heap_object);
#else
  LoadMap(scratch, heap_object);
#endif
  JumpIfStringMap(scratch, target, distance, true);
}

inline void MaglevAssembler::JumpIfNotString(Register heap_object,
                                             Label* target,
                                             Label::Distance distance) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.GetDefaultScratchRegister();
#ifdef V8_COMPRESS_POINTERS
  LoadCompressedMap(scratch, heap_object);
#else
  LoadMap(scratch, heap_object);
#endif
  JumpIfStringMap(scratch, target, distance, false);
}

inline void MaglevAssembler::CheckJSAnyIsStringAndBranch(
    Register heap_object, Label* if_true, Label::Distance true_distance,
    bool fallthrough_when_true, Label* if_false, Label::Distance false_distance,
    bool fallthrough_when_false) {
#if V8_STATIC_ROOTS_BOOL
  ScratchRegisterScope temps(this);
  Register scratch = temps.GetDefaultScratchRegister();
  // All string maps are allocated at the start of the read only heap. Thus,
  // non-strings must have maps with larger (compressed) addresses.
#ifdef V8_COMPRESS_POINTERS
  LoadCompressedMap(scratch, heap_object);
#else
  LoadMap(scratch, heap_object);
#endif
  CompareInt32AndBranch(scratch, InstanceTypeChecker::kLastStringMap,
                        kUnsignedLessThanEqual, if_true, true_distance,
                        fallthrough_when_true, if_false, false_distance,
                        fallthrough_when_false);
#else
  static_assert(FIRST_STRING_TYPE == FIRST_TYPE);
  CompareObjectTypeAndBranch(heap_object, LAST_STRING_TYPE,
                             kUnsignedLessThanEqual, if_true, true_distance,
                             fallthrough_when_true, if_false, false_distance,
                             fallthrough_when_false);
#endif
}

inline void MaglevAssembler::StringLength(Register result, Register string) {
  if (v8_flags.debug_code) {
    // Check if {string} is a string.
    ScratchRegisterScope temps(this);
    Register scratch = temps.GetDefaultScratchRegister();
    AssertNotSmi(string);
    LoadMap(scratch, string);
    CompareInstanceTypeRange(scratch, scratch, FIRST_STRING_TYPE,
                             LAST_STRING_TYPE);
    Check(kUnsignedLessThanEqual, AbortReason::kUnexpectedValue);
  }
  LoadSignedField(result, FieldMemOperand(string, offsetof(String, length_)),
                  sizeof(int32_t));
}

void MaglevAssembler::LoadMapForCompare(Register dst, Register obj) {
#ifdef V8_COMPRESS_POINTERS
  MacroAssembler::LoadCompressedMap(dst, obj);
#else
  MacroAssembler::LoadMap(dst, obj);
#endif
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_ASSEMBLER_INL_H_
