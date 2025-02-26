// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_BUILTIN_CALL_DESCRIPTORS_H_
#define V8_COMPILER_TURBOSHAFT_BUILTIN_CALL_DESCRIPTORS_H_

#include "src/builtins/builtins.h"
#include "src/codegen/callable.h"
#include "src/codegen/interface-descriptors.h"
#include "src/compiler/frame.h"
#include "src/compiler/globals.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/objects/js-function.h"

namespace v8::internal::compiler::turboshaft {

struct BuiltinCallDescriptor {
 private:
  template <typename Derived>
  struct Descriptor {
    static const TSCallDescriptor* Create(
        StubCallMode call_mode, Zone* zone,
        LazyDeoptOnThrow lazy_deopt_on_throw = LazyDeoptOnThrow::kNo) {
      CallInterfaceDescriptor interface_descriptor =
          Builtins::CallInterfaceDescriptorFor(Derived::kFunction);
      auto descriptor = Linkage::GetStubCallDescriptor(
          zone, interface_descriptor,
          interface_descriptor.GetStackParameterCount(),
          Derived::kNeedsFrameState ? CallDescriptor::kNeedsFrameState
                                    : CallDescriptor::kNoFlags,
          Derived::kProperties, call_mode);
#ifdef DEBUG
      Derived::Verify(descriptor);
#endif  // DEBUG
      bool can_throw = !(Derived::kProperties & Operator::kNoThrow);
      return TSCallDescriptor::Create(
          descriptor, can_throw ? CanThrow::kYes : CanThrow::kNo,
          lazy_deopt_on_throw, zone);
    }

#ifdef DEBUG
    static void Verify(const CallDescriptor* desc) {
      using results_t = typename Derived::results_t;
      using arguments_t = typename Derived::arguments_t;
      DCHECK_EQ(desc->ReturnCount(), std::tuple_size_v<results_t>);
      if constexpr (std::tuple_size_v<results_t> >= 1) {
        using result0_t = std::tuple_element_t<0, results_t>;
        DCHECK(AllowsRepresentation<result0_t>(
            RegisterRepresentation::FromMachineRepresentation(
                desc->GetReturnType(0).representation())));
      }
      if constexpr (std::tuple_size_v<results_t> >= 2) {
        using result1_t = std::tuple_element_t<1, results_t>;
        DCHECK(AllowsRepresentation<result1_t>(
            RegisterRepresentation::FromMachineRepresentation(
                desc->GetReturnType(1).representation())));
      }
      DCHECK_EQ(desc->NeedsFrameState(), Derived::kNeedsFrameState);
      DCHECK_EQ(desc->properties(), Derived::kProperties);
      DCHECK_EQ(desc->ParameterCount(), std::tuple_size_v<arguments_t> +
                                            (Derived::kNeedsContext ? 1 : 0));
      DCHECK(VerifyArguments<arguments_t>(desc));
    }

    template <typename Arguments>
    static bool VerifyArguments(const CallDescriptor* desc) {
      return VerifyArgumentsImpl<Arguments>(
          desc, std::make_index_sequence<std::tuple_size_v<Arguments>>());
    }

   private:
    template <typename T>
    static bool AllowsRepresentation(RegisterRepresentation rep) {
      if constexpr (std::is_same_v<T, OpIndex>) {
        return true;
      } else {
        // T is V<...>
        return T::allows_representation(rep);
      }
    }
    template <typename Arguments, size_t... Indices>
    static bool VerifyArgumentsImpl(const CallDescriptor* desc,
                                    std::index_sequence<Indices...>) {
      return (AllowsRepresentation<std::tuple_element_t<Indices, Arguments>>(
                  RegisterRepresentation::FromMachineRepresentation(
                      desc->GetParameterType(Indices).representation())) &&
              ...);
    }
#endif  // DEBUG
  };

  static constexpr OpEffects base_effects = OpEffects().CanDependOnChecks();
  // TODO(nicohartmann@): Unfortunately, we cannot define builtins with
  // void/never return types properly (e.g. in Torque), but they typically have
  // a JSAny dummy return type. Use Void/Never sentinels to express that in
  // Turboshaft's descriptors. We should find a better way to model this.
  using Void = std::tuple<OpIndex>;
  using Never = std::tuple<OpIndex>;

 public:
  struct CheckTurbofanType : public Descriptor<CheckTurbofanType> {
    static constexpr auto kFunction = Builtin::kCheckTurbofanType;
    using arguments_t = std::tuple<V<Object>, V<TurbofanType>, V<Smi>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties =
        Operator::kNoThrow | Operator::kNoDeopt;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().RequiredWhenUnused();
  };

#define DECL_GENERIC_BINOP(Name)                                          \
  struct Name : public Descriptor<Name> {                                 \
    static constexpr auto kFunction = Builtin::k##Name;                   \
    using arguments_t = std::tuple<V<Object>, V<Object>>;                 \
    using results_t = std::tuple<V<Object>>;                              \
                                                                          \
    static constexpr bool kNeedsFrameState = true;                        \
    static constexpr bool kNeedsContext = true;                           \
    static constexpr Operator::Properties kProperties =                   \
        Operator::kNoProperties;                                          \
    static constexpr OpEffects kEffects = base_effects.CanCallAnything(); \
  };
  GENERIC_BINOP_LIST(DECL_GENERIC_BINOP)
#undef DECL_GENERIC_BINOP

#define DECL_GENERIC_UNOP(Name)                                           \
  struct Name : public Descriptor<Name> {                                 \
    static constexpr auto kFunction = Builtin::k##Name;                   \
    using arguments_t = std::tuple<V<Object>>;                            \
    using results_t = std::tuple<V<Object>>;                              \
                                                                          \
    static constexpr bool kNeedsFrameState = true;                        \
    static constexpr bool kNeedsContext = true;                           \
    static constexpr Operator::Properties kProperties =                   \
        Operator::kNoProperties;                                          \
    static constexpr OpEffects kEffects = base_effects.CanCallAnything(); \
  };
  GENERIC_UNOP_LIST(DECL_GENERIC_UNOP)
#undef DECL_GENERIC_UNOP

  struct ToNumber : public Descriptor<ToNumber> {
    static constexpr auto kFunction = Builtin::kToNumber;
    using arguments_t = std::tuple<V<Object>>;
    using results_t = std::tuple<V<Number>>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

  struct NonNumberToNumber : public Descriptor<NonNumberToNumber> {
    static constexpr auto kFunction = Builtin::kNonNumberToNumber;
    using arguments_t = std::tuple<V<JSAnyNotNumber>>;
    using results_t = std::tuple<V<Number>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

  struct ToNumeric : public Descriptor<ToNumeric> {
    static constexpr auto kFunction = Builtin::kToNumeric;
    using arguments_t = std::tuple<V<Object>>;
    using results_t = std::tuple<V<Numeric>>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

  struct NonNumberToNumeric : public Descriptor<NonNumberToNumeric> {
    static constexpr auto kFunction = Builtin::kNonNumberToNumeric;
    using arguments_t = std::tuple<V<JSAnyNotNumber>>;
    using results_t = std::tuple<V<Numeric>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

  struct CopyFastSmiOrObjectElements
      : public Descriptor<CopyFastSmiOrObjectElements> {
    static constexpr auto kFunction = Builtin::kCopyFastSmiOrObjectElements;
    using arguments_t = std::tuple<V<Object>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanWriteMemory().CanReadMemory().CanAllocate();
  };

  template <Builtin B, typename Input>
  struct DebugPrint : public Descriptor<DebugPrint<B, Input>> {
    static constexpr auto kFunction = B;
    using arguments_t = std::tuple<V<Input>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties =
        Operator::kNoThrow | Operator::kNoDeopt;
    static constexpr OpEffects kEffects = base_effects.RequiredWhenUnused();
  };
  using DebugPrintFloat64 = DebugPrint<Builtin::kDebugPrintFloat64, Float64>;
  using DebugPrintWordPtr = DebugPrint<Builtin::kDebugPrintWordPtr, WordPtr>;

  template <Builtin B>
  struct FindOrderedHashEntry : public Descriptor<FindOrderedHashEntry<B>> {
    static constexpr auto kFunction = B;
    using arguments_t = std::tuple<V<Object>, V<Smi>>;
    using results_t = std::tuple<V<Smi>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.AssumesConsistentHeap().CanReadMemory().CanAllocate();
  };
  using FindOrderedHashMapEntry =
      FindOrderedHashEntry<Builtin::kFindOrderedHashMapEntry>;
  using FindOrderedHashSetEntry =
      FindOrderedHashEntry<Builtin::kFindOrderedHashSetEntry>;

  template <Builtin B>
  struct GrowFastElements : public Descriptor<GrowFastElements<B>> {
    static constexpr auto kFunction = B;
    using arguments_t = std::tuple<V<Object>, V<Smi>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanWriteMemory().CanReadMemory().CanAllocate();
  };
  using GrowFastDoubleElements =
      GrowFastElements<Builtin::kGrowFastDoubleElements>;
  using GrowFastSmiOrObjectElements =
      GrowFastElements<Builtin::kGrowFastSmiOrObjectElements>;

  template <Builtin B>
  struct NewArgumentsElements : public Descriptor<NewArgumentsElements<B>> {
    static constexpr auto kFunction = B;
    // TODO(nicohartmann@): First argument should be replaced by a proper
    // RawPtr.
    using arguments_t = std::tuple<V<WordPtr>, V<WordPtr>, V<Smi>>;
    using results_t = std::tuple<V<FixedArray>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanAllocate();
  };
  using NewSloppyArgumentsElements =
      NewArgumentsElements<Builtin::kNewSloppyArgumentsElements>;
  using NewStrictArgumentsElements =
      NewArgumentsElements<Builtin::kNewStrictArgumentsElements>;
  using NewRestArgumentsElements =
      NewArgumentsElements<Builtin::kNewRestArgumentsElements>;

  struct NumberToString : public Descriptor<NumberToString> {
    static constexpr auto kFunction = Builtin::kNumberToString;
    using arguments_t = std::tuple<V<Number>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct ToString : public Descriptor<ToString> {
    static constexpr auto kFunction = Builtin::kToString;
    using arguments_t = std::tuple<V<Object>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

  struct PlainPrimitiveToNumber : public Descriptor<PlainPrimitiveToNumber> {
    static constexpr auto kFunction = Builtin::kPlainPrimitiveToNumber;
    using arguments_t = std::tuple<V<PlainPrimitive>>;
    using results_t = std::tuple<V<Number>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct SameValue : public Descriptor<SameValue> {
    static constexpr auto kFunction = Builtin::kSameValue;
    using arguments_t = std::tuple<V<Object>, V<Object>>;
    using results_t = std::tuple<V<Boolean>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocate();
  };

  struct SameValueNumbersOnly : public Descriptor<SameValueNumbersOnly> {
    static constexpr auto kFunction = Builtin::kSameValueNumbersOnly;
    using arguments_t = std::tuple<V<Object>, V<Object>>;
    using results_t = std::tuple<V<Boolean>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct StringAdd_CheckNone : public Descriptor<StringAdd_CheckNone> {
    static constexpr auto kFunction = Builtin::kStringAdd_CheckNone;
    using arguments_t = std::tuple<V<String>, V<String>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoWrite;
    // This will only write in a fresh object, so the writes are not visible
    // from Turboshaft, and CanAllocate is enough.
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct StringEqual : public Descriptor<StringEqual> {
    static constexpr auto kFunction = Builtin::kStringEqual;
    using arguments_t = std::tuple<V<String>, V<String>, V<WordPtr>>;
    using results_t = std::tuple<V<Boolean>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    // If the strings aren't flat, StringEqual could flatten them, which will
    // allocate new strings.
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct StringFromCodePointAt : public Descriptor<StringFromCodePointAt> {
    static constexpr auto kFunction = Builtin::kStringFromCodePointAt;
    using arguments_t = std::tuple<V<String>, V<WordPtr>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct StringIndexOf : public Descriptor<StringIndexOf> {
    static constexpr auto kFunction = Builtin::kStringIndexOf;
    using arguments_t = std::tuple<V<String>, V<String>, V<Smi>>;
    using results_t = std::tuple<V<Smi>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    // StringIndexOf does a ToString on the receiver, which can allocate a new
    // string.
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct StringCompare : public Descriptor<StringCompare> {
    static constexpr auto kFunction = Builtin::kStringCompare;
    using arguments_t = std::tuple<V<String>, V<String>>;
    using results_t = std::tuple<V<Smi>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  template <Builtin B>
  struct StringComparison : public Descriptor<StringComparison<B>> {
    static constexpr auto kFunction = B;
    using arguments_t = std::tuple<V<String>, V<String>>;
    using results_t = std::tuple<V<Boolean>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };
  using StringLessThan = StringComparison<Builtin::kStringLessThan>;
  using StringLessThanOrEqual =
      StringComparison<Builtin::kStringLessThanOrEqual>;

  struct StringSubstring : public Descriptor<StringSubstring> {
    static constexpr auto kFunction = Builtin::kStringSubstring;
    using arguments_t = std::tuple<V<String>, V<WordPtr>, V<WordPtr>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

#ifdef V8_INTL_SUPPORT
  struct StringToLowerCaseIntl : public Descriptor<StringToLowerCaseIntl> {
    static constexpr auto kFunction = Builtin::kStringToLowerCaseIntl;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };
#endif  // V8_INTL_SUPPORT

  struct StringToNumber : public Descriptor<StringToNumber> {
    static constexpr auto kFunction = Builtin::kStringToNumber;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<Number>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct ToBoolean : public Descriptor<ToBoolean> {
    static constexpr auto kFunction = Builtin::kToBoolean;
    using arguments_t = std::tuple<V<Object>>;
    using results_t = std::tuple<V<Boolean>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct ToObject : public Descriptor<ToObject> {
    static constexpr auto kFunction = Builtin::kToObject;
    using arguments_t = std::tuple<V<JSPrimitive>>;
    using results_t = std::tuple<V<JSReceiver>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocate();
  };

  template <Builtin B>
  struct CreateFunctionContext : public Descriptor<CreateFunctionContext<B>> {
    static constexpr auto kFunction = B;
    using arguments_t = std::tuple<V<ScopeInfo>, V<Word32>>;
    using results_t = std::tuple<V<Context>>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocate();
  };

  using FastNewFunctionContextFunction =
      CreateFunctionContext<Builtin::kFastNewFunctionContextFunction>;
  using FastNewFunctionContextEval =
      CreateFunctionContext<Builtin::kFastNewFunctionContextEval>;

  struct FastNewClosure : public Descriptor<FastNewClosure> {
    static constexpr auto kFunction = Builtin::kFastNewClosure;
    using arguments_t = std::tuple<V<SharedFunctionInfo>, V<FeedbackCell>>;
    using results_t = std::tuple<V<JSFunction>>;

    static constexpr bool kNeedsFrameState = true;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties =
        Operator::kEliminatable | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory().CanAllocate();
  };

  struct Typeof : public Descriptor<Typeof> {
    static constexpr auto kFunction = Builtin::kTypeof;
    using arguments_t = std::tuple<V<Object>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct CheckTurboshaftWord32Type
      : public Descriptor<CheckTurboshaftWord32Type> {
    static constexpr auto kFunction = Builtin::kCheckTurboshaftWord32Type;
    using arguments_t = std::tuple<V<Word32>, V<TurboshaftWord32Type>, V<Smi>>;
    using results_t = std::tuple<V<Oddball>>;
    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct CheckTurboshaftWord64Type
      : public Descriptor<CheckTurboshaftWord64Type> {
    static constexpr auto kFunction = Builtin::kCheckTurboshaftWord64Type;
    using arguments_t =
        std::tuple<V<Word32>, V<Word32>, V<TurboshaftWord64Type>, V<Smi>>;
    using results_t = std::tuple<V<Oddball>>;
    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct CheckTurboshaftFloat32Type
      : public Descriptor<CheckTurboshaftFloat32Type> {
    static constexpr auto kFunction = Builtin::kCheckTurboshaftFloat32Type;
    using arguments_t =
        std::tuple<V<Float32>, V<TurboshaftFloat64Type>, V<Smi>>;
    using results_t = std::tuple<V<Oddball>>;
    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

  struct CheckTurboshaftFloat64Type
      : public Descriptor<CheckTurboshaftFloat64Type> {
    static constexpr auto kFunction = Builtin::kCheckTurboshaftFloat64Type;
    using arguments_t =
        std::tuple<V<Float64>, V<TurboshaftFloat64Type>, V<Smi>>;
    using results_t = std::tuple<V<Oddball>>;
    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
  };

#ifdef V8_ENABLE_WEBASSEMBLY

  struct WasmStringAsWtf8 : public Descriptor<WasmStringAsWtf8> {
    static constexpr auto kFunction = Builtin::kWasmStringAsWtf8;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<ByteArray>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct WasmStringAsWtf16 : public Descriptor<WasmStringAsWtf16> {
    static constexpr auto kFunction = Builtin::kWasmStringAsWtf16;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct WasmInt32ToHeapNumber : public Descriptor<WasmInt32ToHeapNumber> {
    static constexpr auto kFunction = Builtin::kWasmInt32ToHeapNumber;
    using arguments_t = std::tuple<V<Word32>>;
    using results_t = std::tuple<V<HeapNumber>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kPure;
    static constexpr OpEffects kEffects =
        base_effects.CanAllocateWithoutIdentity();
  };

  struct WasmRefFunc : public Descriptor<WasmRefFunc> {
    static constexpr auto kFunction = Builtin::kWasmRefFunc;
    using arguments_t = std::tuple<V<Word32>, V<Word32>>;
    using results_t = std::tuple<V<WasmFuncRef>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoThrow;
    // TODO(nicohartmann@): Use more precise effects.
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

  struct WasmGetOwnProperty : public Descriptor<WasmGetOwnProperty> {
    static constexpr auto kFunction = Builtin::kWasmGetOwnProperty;
    using arguments_t = std::tuple<V<Object>, V<Symbol>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kNoThrow;
    static constexpr OpEffects kEffects = base_effects.CanReadHeapMemory();
  };

  struct WasmRethrow : public Descriptor<WasmRethrow> {
    static constexpr auto kFunction = Builtin::kWasmRethrow;
    using arguments_t = std::tuple<V<Object>>;
    using results_t = std::tuple<OpIndex>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanChangeControlFlow();
  };

  struct WasmThrowRef : public Descriptor<WasmThrowRef> {
    static constexpr auto kFunction = Builtin::kWasmThrowRef;
    using arguments_t = std::tuple<V<Object>>;
    using results_t = std::tuple<OpIndex>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanChangeControlFlow();
  };

  struct WasmMemoryGrow : public Descriptor<WasmMemoryGrow> {
    static constexpr auto kFunction = Builtin::kWasmMemoryGrow;
    using arguments_t = std::tuple<V<Word32>, V<Word32>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory();
  };

  struct WasmStringFromCodePoint : public Descriptor<WasmStringFromCodePoint> {
    static constexpr auto kFunction = Builtin::kWasmStringFromCodePoint;
    using arguments_t = std::tuple<V<Word32>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoWrite;
    static constexpr OpEffects kEffects =
        base_effects.CanAllocateWithoutIdentity().CanLeaveCurrentFunction();
  };

  struct WasmStringNewWtf8Array : public Descriptor<WasmStringNewWtf8Array> {
    static constexpr auto kFunction = Builtin::kWasmStringNewWtf8Array;
    using arguments_t = std::tuple<V<Word32>, V<Word32>, V<WasmArray>, V<Smi>>;
    using results_t = std::tuple<V<WasmStringRefNullable>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects = base_effects.CanReadHeapMemory()
                                              .CanAllocateWithoutIdentity()
                                              .CanLeaveCurrentFunction();
  };

  struct WasmStringNewWtf16Array : public Descriptor<WasmStringNewWtf16Array> {
    static constexpr auto kFunction = Builtin::kWasmStringNewWtf16Array;
    using arguments_t = std::tuple<V<WasmArray>, V<Word32>, V<Word32>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects = base_effects.CanReadHeapMemory()
                                              .CanAllocateWithoutIdentity()
                                              .CanLeaveCurrentFunction();
  };

  struct WasmStringViewWtf8Slice : public Descriptor<WasmStringViewWtf8Slice> {
    static constexpr auto kFunction = Builtin::kWasmStringViewWtf8Slice;
    using arguments_t = std::tuple<V<ByteArray>, V<Word32>, V<Word32>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct WasmStringViewWtf16Slice
      : public Descriptor<WasmStringViewWtf16Slice> {
    static constexpr auto kFunction = Builtin::kWasmStringViewWtf16Slice;
    using arguments_t = std::tuple<V<String>, V<Word32>, V<Word32>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct WasmStringEncodeWtf8Array
      : public Descriptor<WasmStringEncodeWtf8Array> {
    static constexpr auto kFunction = Builtin::kWasmStringEncodeWtf8Array;
    using arguments_t = std::tuple<V<String>, V<WasmArray>, V<Word32>, V<Smi>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteHeapMemory();
  };

  struct WasmStringToUtf8Array : public Descriptor<WasmStringToUtf8Array> {
    static constexpr auto kFunction = Builtin::kWasmStringToUtf8Array;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<WasmArray>>;
    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocate();
  };

  struct WasmStringEncodeWtf16Array
      : public Descriptor<WasmStringEncodeWtf16Array> {
    static constexpr auto kFunction = Builtin::kWasmStringEncodeWtf16Array;
    using arguments_t = std::tuple<V<String>, V<WasmArray>, V<Word32>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory()
                                              .CanWriteHeapMemory()
                                              .CanLeaveCurrentFunction();
  };

  struct WasmFloat64ToString : public Descriptor<WasmFloat64ToString> {
    static constexpr auto kFunction = Builtin::kWasmFloat64ToString;
    using arguments_t = std::tuple<V<Float64>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanAllocateWithoutIdentity();
  };

  struct WasmIntToString : public Descriptor<WasmIntToString> {
    static constexpr auto kFunction = Builtin::kWasmIntToString;
    using arguments_t = std::tuple<V<Word32>, V<Word32>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoDeopt;
    static constexpr OpEffects kEffects =
        base_effects.CanAllocateWithoutIdentity();
  };

  struct WasmStringToDouble : public Descriptor<WasmStringToDouble> {
    static constexpr auto kFunction = Builtin::kWasmStringToDouble;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<Float64>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct WasmAllocateFixedArray : public Descriptor<WasmAllocateFixedArray> {
    static constexpr auto kFunction = Builtin::kWasmAllocateFixedArray;
    using arguments_t = std::tuple<V<WordPtr>>;
    using results_t = std::tuple<V<FixedArray>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanAllocate();
  };

  struct WasmThrow : public Descriptor<WasmThrow> {
    static constexpr auto kFunction = Builtin::kWasmThrow;
    using arguments_t = std::tuple<V<Object>, V<FixedArray>>;
    using results_t = std::tuple<OpIndex>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects =
        base_effects.CanReadHeapMemory().CanChangeControlFlow();
  };

  struct WasmI32AtomicWait : public Descriptor<WasmI32AtomicWait> {
    static constexpr auto kFunction = Builtin::kWasmI32AtomicWait;
    using arguments_t = std::tuple<V<Word32>, V<WordPtr>, V<Word32>, V<BigInt>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

  struct WasmI64AtomicWait : public Descriptor<WasmI64AtomicWait> {
    static constexpr auto kFunction = Builtin::kWasmI64AtomicWait;
    using arguments_t = std::tuple<V<Word32>, V<WordPtr>, V<BigInt>, V<BigInt>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

  struct WasmFunctionTableGet : public Descriptor<WasmFunctionTableGet> {
    static constexpr auto kFunction = Builtin::kWasmFunctionTableGet;
    using arguments_t = std::tuple<V<WordPtr>, V<WordPtr>, V<Word32>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory().CanAllocate();
  };

  struct WasmTableSetFuncRef : public Descriptor<WasmTableSetFuncRef> {
    static constexpr auto kFunction = Builtin::kWasmTableSetFuncRef;
    using arguments_t =
        std::tuple<V<WordPtr>, V<Word32>, V<WordPtr>, V<WasmFuncRef>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanWriteMemory();
  };

  struct WasmTableSet : public Descriptor<WasmTableSet> {
    static constexpr auto kFunction = Builtin::kWasmTableSet;
    using arguments_t =
        std::tuple<V<WordPtr>, V<Word32>, V<WordPtr>, V<Object>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanWriteMemory();
  };

  struct WasmTableInit : public Descriptor<WasmTableInit> {
    static constexpr auto kFunction = Builtin::kWasmTableInit;
    using arguments_t =
        std::tuple<V<WordPtr>, V<Word32>, V<Word32>, V<Smi>, V<Smi>, V<Smi>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanWriteMemory();
  };

  struct WasmTableCopy : public Descriptor<WasmTableCopy> {
    static constexpr auto kFunction = Builtin::kWasmTableCopy;
    using arguments_t =
        std::tuple<V<WordPtr>, V<WordPtr>, V<WordPtr>, V<Smi>, V<Smi>, V<Smi>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory();
  };

  struct WasmTableGrow : public Descriptor<WasmTableGrow> {
    static constexpr auto kFunction = Builtin::kWasmTableGrow;
    using arguments_t = std::tuple<V<Smi>, V<WordPtr>, V<Word32>, V<Object>>;
    using results_t = std::tuple<V<Smi>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory().CanAllocate();
  };

  struct WasmTableFill : public Descriptor<WasmTableFill> {
    static constexpr auto kFunction = Builtin::kWasmTableFill;
    using arguments_t =
        std::tuple<V<WordPtr>, V<WordPtr>, V<Word32>, V<Smi>, V<Object>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanWriteMemory();
  };

  struct WasmArrayNewSegment : public Descriptor<WasmArrayNewSegment> {
    static constexpr auto kFunction = Builtin::kWasmArrayNewSegment;
    using arguments_t =
        std::tuple<V<Word32>, V<Word32>, V<Word32>, V<Smi>, V<Smi>, V<Map>>;
    using results_t = std::tuple<V<WasmArray>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects =
        base_effects.CanReadHeapMemory().CanAllocate();
  };

  struct WasmArrayInitSegment : public Descriptor<WasmArrayInitSegment> {
    static constexpr auto kFunction = Builtin::kWasmArrayInitSegment;
    using arguments_t = std::tuple<V<Word32>, V<Word32>, V<Word32>, V<Smi>,
                                   V<Smi>, V<Smi>, V<HeapObject>>;
    using results_t = std::tuple<V<Object>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects =
        base_effects.CanWriteHeapMemory().CanReadHeapMemory();
  };

  struct WasmStringNewWtf8 : public Descriptor<WasmStringNewWtf8> {
    static constexpr auto kFunction = Builtin::kWasmStringNewWtf8;
    using arguments_t = std::tuple<V<WordPtr>, V<Word32>, V<Word32>, V<Smi>>;
    using results_t = std::tuple<V<WasmStringRefNullable>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory()
                                              .CanAllocateWithoutIdentity()
                                              .CanLeaveCurrentFunction();
  };

  struct WasmStringNewWtf16 : public Descriptor<WasmStringNewWtf16> {
    static constexpr auto kFunction = Builtin::kWasmStringNewWtf16;
    using arguments_t = std::tuple<V<Word32>, V<WordPtr>, V<Word32>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects = base_effects.CanReadHeapMemory()
                                              .CanAllocateWithoutIdentity()
                                              .CanLeaveCurrentFunction();
  };

  struct WasmStringFromDataSegment
      : public Descriptor<WasmStringFromDataSegment> {
    static constexpr auto kFunction = Builtin::kWasmStringFromDataSegment;
    using arguments_t =
        std::tuple<V<Word32>, V<Word32>, V<Word32>, V<Smi>, V<Smi>, V<Smi>>;
    using results_t = std::tuple<V<WasmStringRefNullable>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoDeopt;
    // No "CanReadMemory" because data segments are immutable.
    static constexpr OpEffects kEffects =
        base_effects.CanAllocateWithoutIdentity().RequiredWhenUnused();
  };

  struct WasmStringConst : public Descriptor<WasmStringConst> {
    static constexpr auto kFunction = Builtin::kWasmStringConst;
    using arguments_t = std::tuple<V<Word32>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadHeapMemory().CanAllocateWithoutIdentity();
  };

  struct WasmStringMeasureUtf8 : public Descriptor<WasmStringMeasureUtf8> {
    static constexpr auto kFunction = Builtin::kWasmStringMeasureUtf8;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct WasmStringMeasureWtf8 : public Descriptor<WasmStringMeasureWtf8> {
    static constexpr auto kFunction = Builtin::kWasmStringMeasureWtf8;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct WasmStringEncodeWtf8 : public Descriptor<WasmStringEncodeWtf8> {
    static constexpr auto kFunction = Builtin::kWasmStringEncodeWtf8;
    using arguments_t = std::tuple<V<WordPtr>, V<Word32>, V<Word32>, V<String>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory();
  };

  struct WasmStringEncodeWtf16 : public Descriptor<WasmStringEncodeWtf16> {
    static constexpr auto kFunction = Builtin::kWasmStringEncodeWtf16;
    using arguments_t = std::tuple<V<String>, V<WordPtr>, V<Word32>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory().CanLeaveCurrentFunction();
  };

  struct WasmStringEqual : public Descriptor<WasmStringEqual> {
    static constexpr auto kFunction = Builtin::kWasmStringEqual;
    using arguments_t = std::tuple<V<String>, V<String>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct WasmStringIsUSVSequence : public Descriptor<WasmStringIsUSVSequence> {
    static constexpr auto kFunction = Builtin::kWasmStringIsUSVSequence;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct WasmStringViewWtf8Advance
      : public Descriptor<WasmStringViewWtf8Advance> {
    static constexpr auto kFunction = Builtin::kWasmStringViewWtf8Advance;
    using arguments_t = std::tuple<V<ByteArray>, V<Word32>, V<Word32>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct WasmStringViewWtf8Encode
      : public Descriptor<WasmStringViewWtf8Encode> {
    static constexpr auto kFunction = Builtin::kWasmStringViewWtf8Encode;
    using arguments_t = std::tuple<V<WordPtr>, V<Word32>, V<Word32>,
                                   V<ByteArray>, V<Smi>, V<Smi>>;
    using results_t = std::tuple<V<Word32>, V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory().CanLeaveCurrentFunction();
  };

  struct WasmStringViewWtf16Encode
      : public Descriptor<WasmStringViewWtf16Encode> {
    static constexpr auto kFunction = Builtin::kWasmStringViewWtf16Encode;
    using arguments_t =
        std::tuple<V<WordPtr>, V<Word32>, V<Word32>, V<String>, V<Smi>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties =
        Operator::kNoDeopt | Operator::kNoThrow;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteMemory();
  };

  struct WasmStringViewWtf16GetCodeUnit
      : public Descriptor<WasmStringViewWtf16GetCodeUnit> {
    static constexpr auto kFunction = Builtin::kWasmStringViewWtf16GetCodeUnit;
    using arguments_t = std::tuple<V<String>, V<Word32>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct WasmStringCodePointAt : public Descriptor<WasmStringCodePointAt> {
    static constexpr auto kFunction = Builtin::kWasmStringCodePointAt;
    using arguments_t = std::tuple<V<String>, V<Word32>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct WasmStringAsIter : public Descriptor<WasmStringAsIter> {
    static constexpr auto kFunction = Builtin::kWasmStringAsIter;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<WasmStringViewIter>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanAllocate();
  };

  struct WasmStringViewIterNext : public Descriptor<WasmStringViewIterNext> {
    static constexpr auto kFunction = Builtin::kWasmStringViewIterNext;
    using arguments_t = std::tuple<V<WasmStringViewIter>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteHeapMemory();
  };

  struct WasmStringViewIterAdvance
      : public Descriptor<WasmStringViewIterAdvance> {
    static constexpr auto kFunction = Builtin::kWasmStringViewIterAdvance;
    using arguments_t = std::tuple<V<WasmStringViewIter>, V<Word32>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteHeapMemory();
  };

  struct WasmStringViewIterRewind
      : public Descriptor<WasmStringViewIterRewind> {
    static constexpr auto kFunction = Builtin::kWasmStringViewIterRewind;
    using arguments_t = std::tuple<V<WasmStringViewIter>, V<Word32>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanWriteHeapMemory();
  };

  struct WasmStringViewIterSlice : public Descriptor<WasmStringViewIterSlice> {
    static constexpr auto kFunction = Builtin::kWasmStringViewIterSlice;
    using arguments_t = std::tuple<V<WasmStringViewIter>, V<Word32>>;
    using results_t = std::tuple<V<String>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects =
        base_effects.CanReadMemory().CanAllocateWithoutIdentity();
  };

  struct WasmStringHash : public Descriptor<WasmStringHash> {
    static constexpr auto kFunction = Builtin::kWasmStringHash;
    using arguments_t = std::tuple<V<String>>;
    using results_t = std::tuple<V<Word32>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kEliminatable;
    static constexpr OpEffects kEffects = base_effects.CanReadMemory();
  };

  struct ThrowDataViewDetachedError
      : public Descriptor<ThrowDataViewDetachedError> {
    static constexpr auto kFunction = Builtin::kThrowDataViewDetachedError;
    using arguments_t = std::tuple<>;
    using results_t = std::tuple<OpIndex>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanChangeControlFlow();
  };

  struct ThrowDataViewOutOfBounds
      : public Descriptor<ThrowDataViewOutOfBounds> {
    static constexpr auto kFunction = Builtin::kThrowDataViewOutOfBounds;
    using arguments_t = std::tuple<>;
    using results_t = Never;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanChangeControlFlow();
  };

  struct ThrowDataViewTypeError : public Descriptor<ThrowDataViewTypeError> {
    static constexpr auto kFunction = Builtin::kThrowDataViewTypeError;
    using arguments_t = std::tuple<V<JSDataView>>;
    using results_t = Never;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects =
        base_effects.CanReadHeapMemory().CanChangeControlFlow();
  };

  struct ThrowIndexOfCalledOnNull
      : public Descriptor<ThrowIndexOfCalledOnNull> {
    static constexpr auto kFunction = Builtin::kThrowIndexOfCalledOnNull;
    using arguments_t = std::tuple<>;
    using results_t = Never;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoWrite;
    static constexpr OpEffects kEffects = base_effects.CanChangeControlFlow();
  };

  struct ThrowToLowerCaseCalledOnNull
      : public Descriptor<ThrowToLowerCaseCalledOnNull> {
    static constexpr auto kFunction = Builtin::kThrowToLowerCaseCalledOnNull;
    using arguments_t = std::tuple<>;
    using results_t = Never;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoWrite;
    static constexpr OpEffects kEffects = base_effects.CanChangeControlFlow();
  };

  struct WasmFastApiCallTypeCheckAndUpdateIC
      : public Descriptor<WasmFastApiCallTypeCheckAndUpdateIC> {
    static constexpr auto kFunction =
        Builtin::kWasmFastApiCallTypeCheckAndUpdateIC;
    using arguments_t = std::tuple<V<Object>, V<Object>>;
    using results_t = std::tuple<V<Smi>>;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = true;
    static constexpr Operator::Properties kProperties = Operator::kNoWrite;
    static constexpr OpEffects kEffects =
        base_effects.CanLeaveCurrentFunction();
  };

  struct WasmPropagateException : public Descriptor<WasmPropagateException> {
    static constexpr auto kFunction = Builtin::kWasmPropagateException;
    using arguments_t = std::tuple<>;
    using results_t = Never;

    static constexpr bool kNeedsFrameState = false;
    static constexpr bool kNeedsContext = false;
    static constexpr Operator::Properties kProperties = Operator::kNoProperties;
    static constexpr OpEffects kEffects = base_effects.CanCallAnything();
  };

#endif  // V8_ENABLE_WEBASSEMBLY
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_BUILTIN_CALL_DESCRIPTORS_H_
