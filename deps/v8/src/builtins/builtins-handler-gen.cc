// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/optional.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/ic/ic.h"
#include "src/ic/keyed-store-generic.h"
#include "src/objects/objects-inl.h"
#include "torque-generated/exported-macros-assembler.h"

namespace v8 {
namespace internal {

class HandlerBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit HandlerBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  void Generate_KeyedStoreIC_SloppyArguments();

  // Essentially turns runtime elements kinds (TNode<Int32T>) into
  // compile-time types (int) by dispatching over the runtime type and
  // emitting a specialized copy of the given case function for each elements
  // kind. Use with caution. This produces a *lot* of code.
  using ElementsKindSwitchCase = std::function<void(ElementsKind)>;
  void DispatchByElementsKind(TNode<Int32T> elements_kind,
                              const ElementsKindSwitchCase& case_function,
                              bool handle_typed_elements_kind);

  // Dispatches over all possible combinations of {from,to} elements kinds.
  using ElementsKindTransitionSwitchCase =
      std::function<void(ElementsKind, ElementsKind)>;
  void DispatchForElementsKindTransition(
      TNode<Int32T> from_kind, TNode<Int32T> to_kind,
      const ElementsKindTransitionSwitchCase& case_function);

  void Generate_ElementsTransitionAndStore(KeyedAccessStoreMode store_mode);
  void Generate_StoreFastElementIC(KeyedAccessStoreMode store_mode);
};

TF_BUILTIN(LoadIC_StringLength, CodeStubAssembler) {
  auto string = Parameter<String>(Descriptor::kReceiver);
  Return(LoadStringLengthAsSmi(string));
}

TF_BUILTIN(LoadIC_StringWrapperLength, CodeStubAssembler) {
  auto value = Parameter<JSPrimitiveWrapper>(Descriptor::kReceiver);
  TNode<String> string = CAST(LoadJSPrimitiveWrapperValue(value));
  Return(LoadStringLengthAsSmi(string));
}

void Builtins::Generate_KeyedStoreIC_Megamorphic(
    compiler::CodeAssemblerState* state) {
  KeyedStoreMegamorphicGenerator::Generate(state);
}

void Builtins::Generate_DefineKeyedOwnIC_Megamorphic(
    compiler::CodeAssemblerState* state) {
  DefineKeyedOwnGenericGenerator::Generate(state);
}

void Builtins::Generate_StoreIC_NoFeedback(
    compiler::CodeAssemblerState* state) {
  StoreICNoFeedbackGenerator::Generate(state);
}

void Builtins::Generate_DefineNamedOwnIC_NoFeedback(
    compiler::CodeAssemblerState* state) {
  DefineNamedOwnICNoFeedbackGenerator::Generate(state);
}

// All possible fast-to-fast transitions. Transitions to dictionary mode are not
// handled by ElementsTransitionAndStore builtins.
#define ELEMENTS_KIND_TRANSITIONS(V)               \
  V(PACKED_SMI_ELEMENTS, HOLEY_SMI_ELEMENTS)       \
  V(PACKED_SMI_ELEMENTS, PACKED_DOUBLE_ELEMENTS)   \
  V(PACKED_SMI_ELEMENTS, HOLEY_DOUBLE_ELEMENTS)    \
  V(PACKED_SMI_ELEMENTS, PACKED_ELEMENTS)          \
  V(PACKED_SMI_ELEMENTS, HOLEY_ELEMENTS)           \
  V(HOLEY_SMI_ELEMENTS, HOLEY_DOUBLE_ELEMENTS)     \
  V(HOLEY_SMI_ELEMENTS, HOLEY_ELEMENTS)            \
  V(PACKED_DOUBLE_ELEMENTS, HOLEY_DOUBLE_ELEMENTS) \
  V(PACKED_DOUBLE_ELEMENTS, PACKED_ELEMENTS)       \
  V(PACKED_DOUBLE_ELEMENTS, HOLEY_ELEMENTS)        \
  V(HOLEY_DOUBLE_ELEMENTS, HOLEY_ELEMENTS)         \
  V(PACKED_ELEMENTS, HOLEY_ELEMENTS)

void HandlerBuiltinsAssembler::DispatchForElementsKindTransition(
    TNode<Int32T> from_kind, TNode<Int32T> to_kind,
    const ElementsKindTransitionSwitchCase& case_function) {
  static_assert(sizeof(ElementsKind) == sizeof(uint8_t));

  Label next(this), if_unknown_type(this, Label::kDeferred);

  int32_t combined_elements_kinds[] = {
#define ELEMENTS_KINDS_CASE(FROM, TO) (FROM << kBitsPerByte) | TO,
      ELEMENTS_KIND_TRANSITIONS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE
  };

#define ELEMENTS_KINDS_CASE(FROM, TO) Label if_##FROM##_##TO(this);
  ELEMENTS_KIND_TRANSITIONS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE

  Label* elements_kind_labels[] = {
#define ELEMENTS_KINDS_CASE(FROM, TO) &if_##FROM##_##TO,
      ELEMENTS_KIND_TRANSITIONS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE
  };
  static_assert(arraysize(combined_elements_kinds) ==
                arraysize(elements_kind_labels));

  TNode<Int32T> combined_elements_kind =
      Word32Or(Word32Shl(from_kind, Int32Constant(kBitsPerByte)), to_kind);

  Switch(combined_elements_kind, &if_unknown_type, combined_elements_kinds,
         elements_kind_labels, arraysize(combined_elements_kinds));

#define ELEMENTS_KINDS_CASE(FROM, TO) \
  BIND(&if_##FROM##_##TO);            \
  {                                   \
    case_function(FROM, TO);          \
    Goto(&next);                      \
  }
  ELEMENTS_KIND_TRANSITIONS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE

  BIND(&if_unknown_type);
  Unreachable();

  BIND(&next);
}

#undef ELEMENTS_KIND_TRANSITIONS

void HandlerBuiltinsAssembler::Generate_ElementsTransitionAndStore(
    KeyedAccessStoreMode store_mode) {
  using Descriptor = StoreTransitionDescriptor;
  auto receiver = Parameter<JSObject>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto map = Parameter<Map>(Descriptor::kMap);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<FeedbackVector>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  Comment("ElementsTransitionAndStore: store_mode=", store_mode);

  Label miss(this);

  if (v8_flags.trace_elements_transitions) {
    // Tracing elements transitions is the job of the runtime.
    Goto(&miss);
  } else {
    // TODO(v8:8481): Pass from_kind and to_kind in feedback vector slots.
    DispatchForElementsKindTransition(
        LoadElementsKind(receiver), LoadMapElementsKind(map),
        [=, &miss](ElementsKind from_kind, ElementsKind to_kind) {
          TransitionElementsKind(receiver, map, from_kind, to_kind, &miss);
          EmitElementStore(receiver, key, value, to_kind, store_mode, &miss,
                           context, nullptr);
        });
    Return(value);
  }

  BIND(&miss);
  TailCallRuntime(Runtime::kElementsTransitionAndStoreIC_Miss, context,
                  receiver, key, value, map, slot, vector);
}

TF_BUILTIN(ElementsTransitionAndStore_InBounds, HandlerBuiltinsAssembler) {
  Generate_ElementsTransitionAndStore(KeyedAccessStoreMode::kInBounds);
}

TF_BUILTIN(ElementsTransitionAndStore_NoTransitionGrowAndHandleCOW,
           HandlerBuiltinsAssembler) {
  Generate_ElementsTransitionAndStore(KeyedAccessStoreMode::kGrowAndHandleCOW);
}

TF_BUILTIN(ElementsTransitionAndStore_NoTransitionIgnoreTypedArrayOOB,
           HandlerBuiltinsAssembler) {
  Generate_ElementsTransitionAndStore(
      KeyedAccessStoreMode::kIgnoreTypedArrayOOB);
}

TF_BUILTIN(ElementsTransitionAndStore_NoTransitionHandleCOW,
           HandlerBuiltinsAssembler) {
  Generate_ElementsTransitionAndStore(KeyedAccessStoreMode::kHandleCOW);
}

// All elements kinds handled by EmitElementStore. Specifically, this includes
// fast elements and fixed typed array elements.
#define ELEMENTS_KINDS(V)            \
  V(PACKED_SMI_ELEMENTS)             \
  V(HOLEY_SMI_ELEMENTS)              \
  V(PACKED_ELEMENTS)                 \
  V(PACKED_NONEXTENSIBLE_ELEMENTS)   \
  V(PACKED_SEALED_ELEMENTS)          \
  V(SHARED_ARRAY_ELEMENTS)           \
  V(HOLEY_ELEMENTS)                  \
  V(HOLEY_NONEXTENSIBLE_ELEMENTS)    \
  V(HOLEY_SEALED_ELEMENTS)           \
  V(PACKED_DOUBLE_ELEMENTS)          \
  V(HOLEY_DOUBLE_ELEMENTS)           \
  V(UINT8_ELEMENTS)                  \
  V(INT8_ELEMENTS)                   \
  V(UINT16_ELEMENTS)                 \
  V(INT16_ELEMENTS)                  \
  V(UINT32_ELEMENTS)                 \
  V(INT32_ELEMENTS)                  \
  V(FLOAT16_ELEMENTS)                \
  V(FLOAT32_ELEMENTS)                \
  V(FLOAT64_ELEMENTS)                \
  V(UINT8_CLAMPED_ELEMENTS)          \
  V(BIGUINT64_ELEMENTS)              \
  V(BIGINT64_ELEMENTS)               \
  V(RAB_GSAB_UINT8_ELEMENTS)         \
  V(RAB_GSAB_INT8_ELEMENTS)          \
  V(RAB_GSAB_UINT16_ELEMENTS)        \
  V(RAB_GSAB_INT16_ELEMENTS)         \
  V(RAB_GSAB_UINT32_ELEMENTS)        \
  V(RAB_GSAB_INT32_ELEMENTS)         \
  V(RAB_GSAB_FLOAT16_ELEMENTS)       \
  V(RAB_GSAB_FLOAT32_ELEMENTS)       \
  V(RAB_GSAB_FLOAT64_ELEMENTS)       \
  V(RAB_GSAB_UINT8_CLAMPED_ELEMENTS) \
  V(RAB_GSAB_BIGUINT64_ELEMENTS)     \
  V(RAB_GSAB_BIGINT64_ELEMENTS)

void HandlerBuiltinsAssembler::DispatchByElementsKind(
    TNode<Int32T> elements_kind, const ElementsKindSwitchCase& case_function,
    bool handle_typed_elements_kind) {
  Label next(this), if_unknown_type(this, Label::kDeferred);

  int32_t elements_kinds[] = {
#define ELEMENTS_KINDS_CASE(KIND) KIND,
      ELEMENTS_KINDS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE
  };

#define ELEMENTS_KINDS_CASE(KIND) Label if_##KIND(this);
  ELEMENTS_KINDS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE

  Label* elements_kind_labels[] = {
#define ELEMENTS_KINDS_CASE(KIND) &if_##KIND,
      ELEMENTS_KINDS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE
  };
  static_assert(arraysize(elements_kinds) == arraysize(elements_kind_labels));

  // TODO(mythria): Do not emit cases for typed elements kind when
  // handle_typed_elements is false to decrease the size of the jump table.
  Switch(elements_kind, &if_unknown_type, elements_kinds, elements_kind_labels,
         arraysize(elements_kinds));

#define ELEMENTS_KINDS_CASE(KIND)                                   \
  BIND(&if_##KIND);                                                 \
  {                                                                 \
    if (!v8_flags.enable_sealed_frozen_elements_kind &&             \
        IsAnyNonextensibleElementsKindUnchecked(KIND)) {            \
      /* Disable support for frozen or sealed elements kinds. */    \
      Unreachable();                                                \
    } else if (!handle_typed_elements_kind &&                       \
               IsTypedArrayOrRabGsabTypedArrayElementsKind(KIND)) { \
      Unreachable();                                                \
    } else {                                                        \
      case_function(KIND);                                          \
      Goto(&next);                                                  \
    }                                                               \
  }
  ELEMENTS_KINDS(ELEMENTS_KINDS_CASE)
#undef ELEMENTS_KINDS_CASE

  BIND(&if_unknown_type);
  Unreachable();

  BIND(&next);
}

#undef ELEMENTS_KINDS

void HandlerBuiltinsAssembler::Generate_StoreFastElementIC(
    KeyedAccessStoreMode store_mode) {
  using Descriptor = StoreWithVectorDescriptor;
  auto receiver = Parameter<JSObject>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  Comment("StoreFastElementStub: store_mode=", store_mode);

  Label miss(this);

  // For typed arrays maybe_converted_value contains the value obtained after
  // calling ToNumber. We should pass the converted value to the runtime to
  // avoid doing the user visible conversion again.
  TVARIABLE(Object, maybe_converted_value, value);
  // TODO(v8:8481): Pass elements_kind in feedback vector slots.
  DispatchByElementsKind(
      LoadElementsKind(receiver),
      [=, &miss, &maybe_converted_value](ElementsKind elements_kind) {
        EmitElementStore(receiver, key, value, elements_kind, store_mode, &miss,
                         context, &maybe_converted_value);
      },
      StoreModeSupportsTypeArray(store_mode));
  Return(value);

  BIND(&miss);
  TailCallRuntime(Runtime::kKeyedStoreIC_Miss, context,
                  maybe_converted_value.value(), slot, vector, receiver, key);
}

TF_BUILTIN(StoreFastElementIC_InBounds, HandlerBuiltinsAssembler) {
  Generate_StoreFastElementIC(KeyedAccessStoreMode::kInBounds);
}

TF_BUILTIN(StoreFastElementIC_NoTransitionGrowAndHandleCOW,
           HandlerBuiltinsAssembler) {
  Generate_StoreFastElementIC(KeyedAccessStoreMode::kGrowAndHandleCOW);
}

TF_BUILTIN(StoreFastElementIC_NoTransitionIgnoreTypedArrayOOB,
           HandlerBuiltinsAssembler) {
  Generate_StoreFastElementIC(KeyedAccessStoreMode::kIgnoreTypedArrayOOB);
}

TF_BUILTIN(StoreFastElementIC_NoTransitionHandleCOW, HandlerBuiltinsAssembler) {
  Generate_StoreFastElementIC(KeyedAccessStoreMode::kHandleCOW);
}

TF_BUILTIN(LoadIC_FunctionPrototype, CodeStubAssembler) {
  auto receiver = Parameter<JSFunction>(Descriptor::kReceiver);
  auto name = Parameter<Name>(Descriptor::kName);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<FeedbackVector>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  Label miss(this, Label::kDeferred);
  Return(LoadJSFunctionPrototype(receiver, &miss));

  BIND(&miss);
  TailCallRuntime(Runtime::kLoadIC_Miss, context, receiver, name, slot, vector);
}

TF_BUILTIN(StoreGlobalIC_Slow, CodeStubAssembler) {
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Name>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<FeedbackVector>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  // The slow case calls into the runtime to complete the store without causing
  // an IC miss that would otherwise cause a transition to the generic stub.
  TailCallRuntime(Runtime::kStoreGlobalIC_Slow, context, value, slot, vector,
                  receiver, name);
}

TF_BUILTIN(KeyedLoadIC_SloppyArguments, HandlerBuiltinsAssembler) {
  auto receiver = Parameter<JSObject>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  Label miss(this);

  TNode<Object> result = SloppyArgumentsLoad(receiver, key, &miss);
  Return(result);

  BIND(&miss);
  {
    Comment("Miss");
    TailCallRuntime(Runtime::kKeyedLoadIC_Miss, context, receiver, key, slot,
                    vector);
  }
}

void HandlerBuiltinsAssembler::Generate_KeyedStoreIC_SloppyArguments() {
  using Descriptor = StoreWithVectorDescriptor;
  auto receiver = Parameter<JSObject>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  Label miss(this);

  SloppyArgumentsStore(receiver, key, value, &miss);
  Return(value);

  BIND(&miss);
  TailCallRuntime(Runtime::kKeyedStoreIC_Miss, context, value, slot, vector,
                  receiver, key);
}

TF_BUILTIN(KeyedStoreIC_SloppyArguments_InBounds, HandlerBuiltinsAssembler) {
  Generate_KeyedStoreIC_SloppyArguments();
}

TF_BUILTIN(KeyedStoreIC_SloppyArguments_NoTransitionGrowAndHandleCOW,
           HandlerBuiltinsAssembler) {
  Generate_KeyedStoreIC_SloppyArguments();
}

TF_BUILTIN(KeyedStoreIC_SloppyArguments_NoTransitionIgnoreTypedArrayOOB,
           HandlerBuiltinsAssembler) {
  Generate_KeyedStoreIC_SloppyArguments();
}

TF_BUILTIN(KeyedStoreIC_SloppyArguments_NoTransitionHandleCOW,
           HandlerBuiltinsAssembler) {
  Generate_KeyedStoreIC_SloppyArguments();
}

TF_BUILTIN(LoadIndexedInterceptorIC, CodeStubAssembler) {
  auto receiver = Parameter<JSObject>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  Label if_keyispositivesmi(this), if_keyisinvalid(this);
  Branch(TaggedIsPositiveSmi(key), &if_keyispositivesmi, &if_keyisinvalid);
  BIND(&if_keyispositivesmi);
  TailCallRuntime(Runtime::kLoadElementWithInterceptor, context, receiver, key);

  BIND(&if_keyisinvalid);
  TailCallRuntime(Runtime::kKeyedLoadIC_Miss, context, receiver, key, slot,
                  vector);
}

TF_BUILTIN(KeyedHasIC_SloppyArguments, HandlerBuiltinsAssembler) {
  auto receiver = Parameter<JSObject>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  Label miss(this);

  TNode<Object> result = SloppyArgumentsHas(receiver, key, &miss);
  Return(result);

  BIND(&miss);
  {
    Comment("Miss");
    TailCallRuntime(Runtime::kKeyedHasIC_Miss, context, receiver, key, slot,
                    vector);
  }
}

TF_BUILTIN(HasIndexedInterceptorIC, CodeStubAssembler) {
  auto receiver = Parameter<JSObject>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<Smi>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  Label if_keyispositivesmi(this), if_keyisinvalid(this);
  Branch(TaggedIsPositiveSmi(key), &if_keyispositivesmi, &if_keyisinvalid);
  BIND(&if_keyispositivesmi);
  TailCallRuntime(Runtime::kHasElementWithInterceptor, context, receiver, key);

  BIND(&if_keyisinvalid);
  TailCallRuntime(Runtime::kKeyedHasIC_Miss, context, receiver, key, slot,
                  vector);
}

}  // namespace internal
}  // namespace v8
