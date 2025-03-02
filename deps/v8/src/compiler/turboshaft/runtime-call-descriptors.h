// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_RUNTIME_CALL_DESCRIPTORS_H_
#define V8_COMPILER_TURBOSHAFT_RUNTIME_CALL_DESCRIPTORS_H_

#include "src/compiler/globals.h"
#include "src/compiler/operator.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/runtime/runtime.h"

namespace v8::internal::compiler::turboshaft {

struct RuntimeCallDescriptor {
 private:
  template <typename Derived>
  struct Descriptor {
    static const TSCallDescriptor* Create(
        Zone* zone, LazyDeoptOnThrow lazy_deopt_on_throw) {
      DCHECK_IMPLIES(lazy_deopt_on_throw == LazyDeoptOnThrow::kYes,
                     Derived::kNeedsFrameState);
      auto descriptor = Linkage::GetRuntimeCallDescriptor(
          zone, Derived::kFunction,
          std::tuple_size_v<typename Derived::arguments_t>,
          Derived::kProperties,
          Derived::kNeedsFrameState ? CallDescriptor::kNeedsFrameState
                                    : CallDescriptor::kNoFlags);
#ifdef DEBUG
      Derived::Verify(descriptor);
#endif  // DEBUG
      CanThrow can_throw = (Derived::kProperties & Operator::kNoThrow)
                               ? CanThrow::kNo
                               : CanThrow::kYes;
      return TSCallDescriptor::Create(descriptor, can_throw,
                                      lazy_deopt_on_throw, zone);
    }

#ifdef DEBUG
    static void Verify(const CallDescriptor* desc) {
      using result_t = typename Derived::result_t;
      using arguments_t = typename Derived::arguments_t;
      if constexpr (std::is_same_v<result_t, void>) {
        DCHECK_EQ(desc->ReturnCount(), 0);
      } else {
        DCHECK_EQ(desc->ReturnCount(), 1);
        DCHECK(result_t::allows_representation(
            RegisterRepresentation::FromMachineRepresentation(
                desc->GetReturnType(0).representation())));
      }
      DCHECK_EQ(desc->NeedsFrameState(), Derived::kNeedsFrameState);
      DCHECK_EQ(desc->properties(), Derived::kProperties);
      constexpr int additional_stub_arguments =
          3;  // function id, argument count, context (or NoContextConstant)
      DCHECK_EQ(desc->ParameterCount(),
                std::tuple_size_v<arguments_t> + additional_stub_arguments);
      DCHECK(VerifyArguments<arguments_t>(desc));
    }

    template <typename Arguments>
    static bool VerifyArguments(const CallDescriptor* desc) {
      return VerifyArgumentsImpl<Arguments>(
          desc, std::make_index_sequence<std::tuple_size_v<Arguments>>());
    }

   private:
    template <typename Arguments, size_t... Indices>
    static bool VerifyArgumentsImpl(const CallDescriptor* desc,
                                    std::index_sequence<Indices...>) {
      return (std::tuple_element_t<Indices, Arguments>::allows_representation(
                  RegisterRepresentation::FromMachineRepresentation(
                      desc->GetParameterType(Indices).representation())) &&
              ...);
    }
#endif  // DEBUG
  };

  // TODO(nicohartmann@): Unfortunately, we cannot define builtins with
  // void/never return types properly (e.g. in Torque), but they typically have
  // a JSAny dummy return type. Use Void/Never sentinels to express that in
  // Turboshaft's descriptors. We should find a better way to model this.
  using Void = V<Any>;
  using Never = V<Any>;

 public:
  struct Abort : public Descriptor<Abort> {
    static constexpr auto kFunction = Runtime::kAbort;
    using arguments_t = std::tuple<V<Smi>>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct BigIntUnaryOp : public Descriptor<BigIntUnaryOp> {
    static constexpr auto kFunction = Runtime::kBigIntUnaryOp;
    using arguments_t = std::tuple<V<BigInt>, V<Smi>>;
    using result_t = V<BigInt>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct DateCurrentTime : public Descriptor<DateCurrentTime> {
    static constexpr auto kFunction = Runtime::kDateCurrentTime;
    using arguments_t = std::tuple<>;
    using result_t = V<Number>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct DebugPrint : public Descriptor<DebugPrint> {
    static constexpr auto kFunction = Runtime::kDebugPrint;
    using arguments_t = std::tuple<V<Object>>;
    using result_t = Void;  // No actual result

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct StackGuard : public Descriptor<StackGuard> {
    static constexpr auto kFunction = Runtime::kStackGuard;
    using arguments_t = std::tuple<>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = false;
    // TODO(nicohartmann@): Verify this.
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct StackGuardWithGap : public Descriptor<StackGuardWithGap> {
    static constexpr auto kFunction = Runtime::kStackGuardWithGap;
    using arguments_t = std::tuple<V<Smi>>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = true;
    // TODO(nicohartmann@): Verify this.
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct HandleNoHeapWritesInterrupts
      : public Descriptor<HandleNoHeapWritesInterrupts> {
    static constexpr auto kFunction = Runtime::kHandleNoHeapWritesInterrupts;
    using arguments_t = std::tuple<>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoWrite;
  };

  struct PropagateException : public Descriptor<PropagateException> {
    static constexpr auto kFunction = Runtime::kPropagateException;
    using arguments_t = std::tuple<>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct ReThrow : public Descriptor<ReThrow> {
    static constexpr auto kFunction = Runtime::kReThrow;
    using arguments_t = std::tuple<V<Object>>;
    using result_t = Never;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties = Operator::kNoThrow;
  };

  struct StringCharCodeAt : public Descriptor<StringCharCodeAt> {
    static constexpr auto kFunction = Runtime::kStringCharCodeAt;
    using arguments_t = std::tuple<V<String>, V<Number>>;
    using result_t = V<Smi>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

#ifdef V8_INTL_SUPPORT
  struct StringToUpperCaseIntl : public Descriptor<StringToUpperCaseIntl> {
    static constexpr auto kFunction = Runtime::kStringToUpperCaseIntl;
    using arguments_t = std::tuple<V<String>>;
    using result_t = V<String>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };
#endif  // V8_INTL_SUPPORT

  struct SymbolDescriptiveString : public Descriptor<SymbolDescriptiveString> {
    static constexpr auto kFunction = Runtime::kSymbolDescriptiveString;
    using arguments_t = std::tuple<V<Symbol>>;
    using result_t = V<String>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoDeopt;
  };

  struct TerminateExecution : public Descriptor<TerminateExecution> {
    static constexpr auto kFunction = Runtime::kTerminateExecution;
    using arguments_t = std::tuple<>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoDeopt;
  };

  struct TransitionElementsKind : public Descriptor<TransitionElementsKind> {
    static constexpr auto kFunction = Runtime::kTransitionElementsKind;
    using arguments_t = std::tuple<V<HeapObject>, V<Map>>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct TryMigrateInstance : public Descriptor<TryMigrateInstance> {
    static constexpr auto kFunction = Runtime::kTryMigrateInstance;
    using arguments_t = std::tuple<V<HeapObject>>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct TryMigrateInstanceAndMarkMapAsMigrationTarget
      : public Descriptor<TryMigrateInstanceAndMarkMapAsMigrationTarget> {
    static constexpr auto kFunction =
        Runtime::kTryMigrateInstanceAndMarkMapAsMigrationTarget;
    using arguments_t = std::tuple<V<HeapObject>>;
    using result_t = V<Object>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct ThrowAccessedUninitializedVariable
      : public Descriptor<ThrowAccessedUninitializedVariable> {
    static constexpr auto kFunction =
        Runtime::kThrowAccessedUninitializedVariable;
    using arguments_t = std::tuple<V<Object>>;
    // Doesn't actually return something, but the actual runtime call descriptor
    // (returned by Linkage::GetRuntimeCallDescriptor) returns 1 instead of 0.
    using result_t = Never;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct ThrowConstructorReturnedNonObject
      : public Descriptor<ThrowConstructorReturnedNonObject> {
    static constexpr auto kFunction =
        Runtime::kThrowConstructorReturnedNonObject;
    using arguments_t = std::tuple<>;
    // Doesn't actually return something, but the actual runtime call descriptor
    // (returned by Linkage::GetRuntimeCallDescriptor) returns 1 instead of 0.
    using result_t = Never;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct ThrowNotSuperConstructor
      : public Descriptor<ThrowNotSuperConstructor> {
    static constexpr auto kFunction = Runtime::kThrowNotSuperConstructor;
    using arguments_t = std::tuple<V<Object>, V<Object>>;
    // Doesn't actually return something, but the actual runtime call descriptor
    // (returned by Linkage::GetRuntimeCallDescriptor) returns 1 instead of 0.
    using result_t = Never;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct ThrowSuperAlreadyCalledError
      : public Descriptor<ThrowSuperAlreadyCalledError> {
    static constexpr auto kFunction = Runtime::kThrowSuperAlreadyCalledError;
    using arguments_t = std::tuple<>;
    // Doesn't actually return something, but the actual runtime call descriptor
    // (returned by Linkage::GetRuntimeCallDescriptor) returns 1 instead of 0.
    using result_t = Never;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct ThrowSuperNotCalled : public Descriptor<ThrowSuperNotCalled> {
    static constexpr auto kFunction = Runtime::kThrowSuperNotCalled;
    using arguments_t = std::tuple<>;
    // Doesn't actually return something, but the actual runtime call descriptor
    // (returned by Linkage::GetRuntimeCallDescriptor) returns 1 instead of 0.
    using result_t = Never;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct ThrowCalledNonCallable : public Descriptor<ThrowCalledNonCallable> {
    static constexpr auto kFunction = Runtime::kThrowCalledNonCallable;
    using arguments_t = std::tuple<V<Object>>;
    // Doesn't actually return something, but the actual runtime call descriptor
    // (returned by Linkage::GetRuntimeCallDescriptor) returns 1 instead of 0.
    using result_t = Never;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct ThrowInvalidStringLength
      : public Descriptor<ThrowInvalidStringLength> {
    static constexpr auto kFunction = Runtime::kThrowInvalidStringLength;
    using arguments_t = std::tuple<>;
    // Doesn't actually return something, but the actual runtime call descriptor
    // (returned by Linkage::GetRuntimeCallDescriptor) returns 1 instead of 0.
    using result_t = Never;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };

  struct NewClosure : public Descriptor<NewClosure> {
    static constexpr auto kFunction = Runtime::kNewClosure;
    using arguments_t = std::tuple<V<SharedFunctionInfo>, V<FeedbackCell>>;
    using result_t = V<JSFunction>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties = Operator::kNoThrow;
  };

  struct NewClosure_Tenured : public Descriptor<NewClosure_Tenured> {
    static constexpr auto kFunction = Runtime::kNewClosure_Tenured;
    using arguments_t = std::tuple<V<SharedFunctionInfo>, V<FeedbackCell>>;
    using result_t = V<JSFunction>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr Operator::Properties kProperties = Operator::kNoThrow;
  };

  struct HasInPrototypeChain : public Descriptor<HasInPrototypeChain> {
    static constexpr auto kFunction = Runtime::kHasInPrototypeChain;
    using arguments_t = std::tuple<V<Object>, V<HeapObject>>;
    using result_t = V<Boolean>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
  };
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_RUNTIME_CALL_DESCRIPTORS_H_
