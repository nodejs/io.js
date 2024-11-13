// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-array-gen.h"

#include <optional>

#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins-iterator-gen.h"
#include "src/builtins/builtins-string-gen.h"
#include "src/builtins/builtins-typed-array-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/tnode.h"
#include "src/execution/frame-constants.h"
#include "src/heap/factory-inl.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/arguments-inl.h"
#include "src/objects/elements-kind.h"
#include "src/objects/property-cell.h"

namespace v8 {
namespace internal {

ArrayBuiltinsAssembler::ArrayBuiltinsAssembler(
    compiler::CodeAssemblerState* state)
    : CodeStubAssembler(state),
      k_(this),
      a_(this),
      fully_spec_compliant_(this, {&k_, &a_}) {}

void ArrayBuiltinsAssembler::TypedArrayMapResultGenerator() {
  // 6. Let A be ? TypedArraySpeciesCreate(O, len).
  TNode<JSTypedArray> original_array = CAST(o());
  const char* method_name = "%TypedArray%.prototype.map";

  TNode<JSTypedArray> a = TypedArraySpeciesCreateByLength(
      context(), method_name, original_array, len());
  // In the Spec and our current implementation, the length check is already
  // performed in TypedArraySpeciesCreate.
#ifdef DEBUG
  Label detached_or_out_of_bounds(this), done(this);
  CSA_DCHECK(this, UintPtrLessThanOrEqual(
                       len(), LoadJSTypedArrayLengthAndCheckDetached(
                                  a, &detached_or_out_of_bounds)));
  Goto(&done);
  BIND(&detached_or_out_of_bounds);
  Unreachable();
  BIND(&done);
#endif  // DEBUG

  // TODO(v8:11111): Make storing fast when the elements kinds only differ
  // because of their RAB/GSABness.
  fast_typed_array_target_ =
      Word32Equal(LoadElementsKind(original_array), LoadElementsKind(a));
  a_ = a;
}

// See tc39.github.io/ecma262/#sec-%typedarray%.prototype.map.
TNode<Object> ArrayBuiltinsAssembler::TypedArrayMapProcessor(
    TNode<Object> k_value, TNode<UintPtrT> k) {
  // 7c. Let mapped_value be ? Call(callbackfn, T, « kValue, k, O »).
  TNode<Number> k_number = ChangeUintPtrToTagged(k);
  TNode<Object> mapped_value =
      Call(context(), callbackfn(), this_arg(), k_value, k_number, o());
  Label fast(this), slow(this), done(this), detached(this, Label::kDeferred);

  // 7d. Perform ? Set(A, Pk, mapped_value, true).
  // Since we know that A is a TypedArray, this always ends up in
  // #sec-integer-indexed-exotic-objects-set-p-v-receiver and then
  // tc39.github.io/ecma262/#sec-integerindexedelementset .
  Branch(fast_typed_array_target_, &fast, &slow);

  BIND(&fast);
  // #sec-integerindexedelementset
  // 2. If arrayTypeName is "BigUint64Array" or "BigInt64Array", let
  // numValue be ? ToBigInt(v).
  // 3. Otherwise, let numValue be ? ToNumber(value).
  TNode<Object> num_value;
  if (IsBigIntTypedArrayElementsKind(source_elements_kind_)) {
    num_value = ToBigInt(context(), mapped_value);
  } else {
    num_value = ToNumber_Inline(context(), mapped_value);
  }

  // The only way how this can bailout is because of a detached or out of bounds
  // buffer.
  // TODO(v8:4153): Consider checking IsDetachedBuffer() and calling
  // TypedArrayBuiltinsAssembler::StoreJSTypedArrayElementFromNumeric() here
  // instead to avoid converting k_number back to UintPtrT.

  // Using source_elements_kind_ (not "target elements kind") is correct here,
  // because the fast branch is taken only when the source and the target
  // elements kinds match.
  EmitElementStore(CAST(a()), k_number, num_value, source_elements_kind_,
                   KeyedAccessStoreMode::kInBounds, &detached, context());
  Goto(&done);

  BIND(&slow);
  {
    SetPropertyStrict(context(), a(), k_number, mapped_value);
    Goto(&done);
  }

  BIND(&detached);
  // tc39.github.io/ecma262/#sec-integerindexedelementset
  // 8. If IsDetachedBuffer(buffer) is true, throw a TypeError exception.
  ThrowTypeError(context_, MessageTemplate::kDetachedOperation, name_);

  BIND(&done);
  return a();
}

void ArrayBuiltinsAssembler::ReturnFromBuiltin(TNode<Object> value) {
  if (argc_ == nullptr) {
    Return(value);
  } else {
    CodeStubArguments args(this, argc());
    PopAndReturn(args.GetLengthWithReceiver(), value);
  }
}

void ArrayBuiltinsAssembler::InitIteratingArrayBuiltinBody(
    TNode<Context> context, TNode<Object> receiver, TNode<Object> callbackfn,
    TNode<Object> this_arg, TNode<IntPtrT> argc) {
  context_ = context;
  receiver_ = receiver;
  callbackfn_ = callbackfn;
  this_arg_ = this_arg;
  argc_ = argc;
}

void ArrayBuiltinsAssembler::GenerateIteratingTypedArrayBuiltinBody(
    const char* name, const BuiltinResultGenerator& generator,
    const CallResultProcessor& processor, ForEachDirection direction) {
  name_ = name;

  // ValidateTypedArray: tc39.github.io/ecma262/#sec-validatetypedarray

  Label throw_not_typed_array(this, Label::kDeferred);

  GotoIf(TaggedIsSmi(receiver_), &throw_not_typed_array);
  TNode<Map> typed_array_map = LoadMap(CAST(receiver_));
  GotoIfNot(IsJSTypedArrayMap(typed_array_map), &throw_not_typed_array);

  TNode<JSTypedArray> typed_array = CAST(receiver_);
  o_ = typed_array;

  Label throw_detached(this, Label::kDeferred);
  len_ = LoadJSTypedArrayLengthAndCheckDetached(typed_array, &throw_detached);

  Label throw_not_callable(this, Label::kDeferred);
  Label distinguish_types(this);
  GotoIf(TaggedIsSmi(callbackfn_), &throw_not_callable);
  Branch(IsCallableMap(LoadMap(CAST(callbackfn_))), &distinguish_types,
         &throw_not_callable);

  BIND(&throw_not_typed_array);
  ThrowTypeError(context_, MessageTemplate::kNotTypedArray);

  BIND(&throw_not_callable);
  ThrowTypeError(context_, MessageTemplate::kCalledNonCallable, callbackfn_);

  BIND(&throw_detached);
  ThrowTypeError(context_, MessageTemplate::kDetachedOperation, name_);

  Label unexpected_instance_type(this);
  BIND(&unexpected_instance_type);
  Unreachable();

  std::vector<int32_t> elements_kinds = {
#define ELEMENTS_KIND(Type, type, TYPE, ctype) TYPE##_ELEMENTS,
      TYPED_ARRAYS(ELEMENTS_KIND) RAB_GSAB_TYPED_ARRAYS(ELEMENTS_KIND)
#undef ELEMENTS_KIND
  };
  std::list<Label> labels;
  for (size_t i = 0; i < elements_kinds.size(); ++i) {
    labels.emplace_back(this);
  }
  std::vector<Label*> label_ptrs;
  for (Label& label : labels) {
    label_ptrs.push_back(&label);
  }

  BIND(&distinguish_types);

  generator(this);

  TNode<JSArrayBuffer> array_buffer = LoadJSArrayBufferViewBuffer(typed_array);
  TNode<Int32T> elements_kind = LoadMapElementsKind(typed_array_map);
  Switch(elements_kind, &unexpected_instance_type, elements_kinds.data(),
         label_ptrs.data(), labels.size());

  size_t i = 0;
  for (auto it = labels.begin(); it != labels.end(); ++i, ++it) {
    BIND(&*it);
    source_elements_kind_ = static_cast<ElementsKind>(elements_kinds[i]);
    VisitAllTypedArrayElements(array_buffer, processor, direction, typed_array);
    ReturnFromBuiltin(a_.value());
  }
}

void ArrayBuiltinsAssembler::VisitAllTypedArrayElements(
    TNode<JSArrayBuffer> array_buffer, const CallResultProcessor& processor,
    ForEachDirection direction, TNode<JSTypedArray> typed_array) {
  VariableList list({&a_, &k_}, zone());

  TNode<UintPtrT> start = UintPtrConstant(0);
  TNode<UintPtrT> end = len_;
  IndexAdvanceMode advance_mode = IndexAdvanceMode::kPost;
  int incr = 1;
  if (direction == ForEachDirection::kReverse) {
    std::swap(start, end);
    advance_mode = IndexAdvanceMode::kPre;
    incr = -1;
  }
  k_ = start;

  // TODO(v8:11111): Only RAB-backed TAs need special handling here since the
  // backing store can shrink mid-iteration. This implementation has an
  // overzealous check for GSAB-backed length-tracking TAs. Then again, the
  // non-RAB/GSAB code also has an overzealous detached check for SABs.
  ElementsKind effective_elements_kind = source_elements_kind_;
  bool is_rab_gsab = IsRabGsabTypedArrayElementsKind(effective_elements_kind);
  if (is_rab_gsab) {
    effective_elements_kind =
        GetCorrespondingNonRabGsabElementsKind(effective_elements_kind);
  }
  BuildFastLoop<UintPtrT>(
      list, start, end,
      [&](TNode<UintPtrT> index) {
        TVARIABLE(Object, value);
        Label detached(this, Label::kDeferred);
        Label process(this);
        if (is_rab_gsab) {
          // If `index` is out of bounds, Get returns undefined.
          CheckJSTypedArrayIndex(typed_array, index, &detached);
        } else {
          GotoIf(IsDetachedBuffer(array_buffer), &detached);
        }
        {
          TNode<RawPtrT> data_ptr = LoadJSTypedArrayDataPtr(typed_array);
          value = LoadFixedTypedArrayElementAsTagged(data_ptr, index,
                                                     effective_elements_kind);
          Goto(&process);
        }

        BIND(&detached);
        {
          value = UndefinedConstant();
          Goto(&process);
        }

        BIND(&process);
        {
          k_ = index;
          a_ = processor(this, value.value(), index);
        }
      },
      incr, LoopUnrollingMode::kNo, advance_mode);
}

TF_BUILTIN(ArrayPrototypePop, CodeStubAssembler) {
  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  auto context = Parameter<Context>(Descriptor::kContext);
  CSA_DCHECK(this, IsUndefined(Parameter<Object>(Descriptor::kJSNewTarget)));

  CodeStubArguments args(this, argc);
  TNode<Object> receiver = args.GetReceiver();

  Label runtime(this, Label::kDeferred);
  Label fast(this);

  // Only pop in this stub if
  // 1) the array has fast elements
  // 2) the length is writable,
  // 3) the elements backing store isn't copy-on-write,
  // 4) we aren't supposed to shrink the backing store.

  // 1) Check that the array has fast elements.
  BranchIfFastJSArray(receiver, context, &fast, &runtime);

  BIND(&fast);
  {
    TNode<JSArray> array_receiver = CAST(receiver);
    CSA_DCHECK(this, TaggedIsPositiveSmi(LoadJSArrayLength(array_receiver)));
    TNode<Int32T> length =
        LoadAndUntagToWord32ObjectField(array_receiver, JSArray::kLengthOffset);
    Label return_undefined(this), fast_elements(this);

    // 2) Ensure that the length is writable.
    EnsureArrayLengthWritable(context, LoadMap(array_receiver), &runtime);

    GotoIf(Word32Equal(length, Int32Constant(0)), &return_undefined);

    // 3) Check that the elements backing store isn't copy-on-write.
    TNode<FixedArrayBase> elements = LoadElements(array_receiver);
    GotoIf(TaggedEqual(LoadMap(elements), FixedCOWArrayMapConstant()),
           &runtime);

    TNode<Int32T> new_length = Int32Sub(length, Int32Constant(1));

    // 4) Check that we're not supposed to shrink the backing store, as
    //    implemented in elements.cc:ElementsAccessorBase::SetLengthImpl.
    TNode<Int32T> capacity = SmiToInt32(LoadFixedArrayBaseLength(elements));
    GotoIf(Int32LessThan(
               Int32Add(Int32Add(new_length, new_length),
                        Int32Constant(JSObject::kMinAddedElementsCapacity)),
               capacity),
           &runtime);

    TNode<IntPtrT> new_length_intptr = ChangePositiveInt32ToIntPtr(new_length);
    StoreObjectFieldNoWriteBarrier(array_receiver, JSArray::kLengthOffset,
                                   SmiTag(new_length_intptr));

    TNode<Int32T> elements_kind = LoadElementsKind(array_receiver);
    GotoIf(Int32LessThanOrEqual(elements_kind,
                                Int32Constant(TERMINAL_FAST_ELEMENTS_KIND)),
           &fast_elements);

    {
      TNode<FixedDoubleArray> elements_known_double_array =
          ReinterpretCast<FixedDoubleArray>(elements);
      TNode<Float64T> value = LoadFixedDoubleArrayElement(
          elements_known_double_array, new_length_intptr, &return_undefined);

      StoreFixedDoubleArrayHole(elements_known_double_array, new_length_intptr);
      args.PopAndReturn(AllocateHeapNumberWithValue(value));
    }

    BIND(&fast_elements);
    {
      TNode<FixedArray> elements_known_fixed_array = CAST(elements);
      TNode<Object> value =
          LoadFixedArrayElement(elements_known_fixed_array, new_length_intptr);
      StoreFixedArrayElement(elements_known_fixed_array, new_length_intptr,
                             TheHoleConstant());
      GotoIf(TaggedEqual(value, TheHoleConstant()), &return_undefined);
      args.PopAndReturn(value);
    }

    BIND(&return_undefined);
    { args.PopAndReturn(UndefinedConstant()); }
  }

  BIND(&runtime);
  {
    // We are not using Parameter(Descriptor::kJSTarget) and loading the value
    // from the current frame here in order to reduce register pressure on the
    // fast path.
    TNode<JSFunction> target = LoadTargetFromFrame();
    TailCallBuiltin(Builtin::kArrayPop, context, target, UndefinedConstant(),
                    argc);
  }
}

TF_BUILTIN(ArrayPrototypePush, CodeStubAssembler) {
  TVARIABLE(IntPtrT, arg_index);
  Label default_label(this, &arg_index);
  Label smi_transition(this);
  Label object_push_pre(this);
  Label object_push(this, &arg_index);
  Label double_push(this, &arg_index);
  Label double_transition(this);
  Label runtime(this, Label::kDeferred);

  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  auto context = Parameter<Context>(Descriptor::kContext);
  CSA_DCHECK(this, IsUndefined(Parameter<Object>(Descriptor::kJSNewTarget)));

  CodeStubArguments args(this, argc);
  TNode<Object> receiver = args.GetReceiver();
  TNode<JSArray> array_receiver;
  TNode<Int32T> kind;

  Label fast(this);
  BranchIfFastJSArray(receiver, context, &fast, &runtime);

  BIND(&fast);
  {
    array_receiver = CAST(receiver);
    arg_index = IntPtrConstant(0);
    kind = EnsureArrayPushable(context, LoadMap(array_receiver), &runtime);
    GotoIf(IsElementsKindGreaterThan(kind, HOLEY_SMI_ELEMENTS),
           &object_push_pre);

    TNode<Smi> new_length =
        BuildAppendJSArray(PACKED_SMI_ELEMENTS, array_receiver, &args,
                           &arg_index, &smi_transition);
    args.PopAndReturn(new_length);
  }

  // If the argument is not a smi, then use a heavyweight SetProperty to
  // transition the array for only the single next element. If the argument is
  // a smi, the failure is due to some other reason and we should fall back on
  // the most generic implementation for the rest of the array.
  BIND(&smi_transition);
  {
    TNode<Object> arg = args.AtIndex(arg_index.value());
    GotoIf(TaggedIsSmi(arg), &default_label);
    TNode<Number> length = LoadJSArrayLength(array_receiver);
    // TODO(danno): Use the KeyedStoreGeneric stub here when possible,
    // calling into the runtime to do the elements transition is overkill.
    SetPropertyStrict(context, array_receiver, length, arg);
    Increment(&arg_index);
    // The runtime SetProperty call could have converted the array to dictionary
    // mode, which must be detected to abort the fast-path.
    TNode<Int32T> elements_kind = LoadElementsKind(array_receiver);
    GotoIf(Word32Equal(elements_kind, Int32Constant(DICTIONARY_ELEMENTS)),
           &default_label);

    GotoIfNotNumber(arg, &object_push);
    Goto(&double_push);
  }

  BIND(&object_push_pre);
  {
    Branch(IsElementsKindGreaterThan(kind, HOLEY_ELEMENTS), &double_push,
           &object_push);
  }

  BIND(&object_push);
  {
    TNode<Smi> new_length = BuildAppendJSArray(
        PACKED_ELEMENTS, array_receiver, &args, &arg_index, &default_label);
    args.PopAndReturn(new_length);
  }

  BIND(&double_push);
  {
    TNode<Smi> new_length =
        BuildAppendJSArray(PACKED_DOUBLE_ELEMENTS, array_receiver, &args,
                           &arg_index, &double_transition);
    args.PopAndReturn(new_length);
  }

  // If the argument is not a double, then use a heavyweight SetProperty to
  // transition the array for only the single next element. If the argument is
  // a double, the failure is due to some other reason and we should fall back
  // on the most generic implementation for the rest of the array.
  BIND(&double_transition);
  {
    TNode<Object> arg = args.AtIndex(arg_index.value());
    GotoIfNumber(arg, &default_label);
    TNode<Number> length = LoadJSArrayLength(array_receiver);
    // TODO(danno): Use the KeyedStoreGeneric stub here when possible,
    // calling into the runtime to do the elements transition is overkill.
    SetPropertyStrict(context, array_receiver, length, arg);
    Increment(&arg_index);
    // The runtime SetProperty call could have converted the array to dictionary
    // mode, which must be detected to abort the fast-path.
    TNode<Int32T> elements_kind = LoadElementsKind(array_receiver);
    GotoIf(Word32Equal(elements_kind, Int32Constant(DICTIONARY_ELEMENTS)),
           &default_label);
    Goto(&object_push);
  }

  // Fallback that stores un-processed arguments using the full, heavyweight
  // SetProperty machinery.
  BIND(&default_label);
  {
    args.ForEach(
        [=, this](TNode<Object> arg) {
          TNode<Number> length = LoadJSArrayLength(array_receiver);
          SetPropertyStrict(context, array_receiver, length, arg);
        },
        arg_index.value());
    args.PopAndReturn(LoadJSArrayLength(array_receiver));
  }

  BIND(&runtime);
  {
    // We are not using Parameter(Descriptor::kJSTarget) and loading the value
    // from the current frame here in order to reduce register pressure on the
    // fast path.
    TNode<JSFunction> target = LoadTargetFromFrame();
    TailCallBuiltin(Builtin::kArrayPush, context, target, UndefinedConstant(),
                    argc);
  }
}

TF_BUILTIN(ExtractFastJSArray, ArrayBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto array = Parameter<JSArray>(Descriptor::kSource);
  TNode<BInt> begin = SmiToBInt(Parameter<Smi>(Descriptor::kBegin));
  TNode<BInt> count = SmiToBInt(Parameter<Smi>(Descriptor::kCount));

  CSA_DCHECK(this, Word32BinaryNot(IsNoElementsProtectorCellInvalid()));

  Return(ExtractFastJSArray(context, array, begin, count));
}

TF_BUILTIN(CloneFastJSArray, ArrayBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto array = Parameter<JSArray>(Descriptor::kSource);

  CSA_DCHECK(this,
             Word32Or(Word32BinaryNot(IsHoleyFastElementsKindForRead(
                          LoadElementsKind(array))),
                      Word32BinaryNot(IsNoElementsProtectorCellInvalid())));

  Return(CloneFastJSArray(context, array));
}

// This builtin copies the backing store of fast arrays, while converting any
// holes to undefined.
// - If there are no holes in the source, its ElementsKind will be preserved. In
// that case, this builtin should perform as fast as CloneFastJSArray. (In fact,
// for fast packed arrays, the behavior is equivalent to CloneFastJSArray.)
// - If there are holes in the source, the ElementsKind of the "copy" will be
// PACKED_ELEMENTS (such that undefined can be stored).
TF_BUILTIN(CloneFastJSArrayFillingHoles, ArrayBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto array = Parameter<JSArray>(Descriptor::kSource);

  CSA_DCHECK(this,
             Word32Or(Word32BinaryNot(IsHoleyFastElementsKindForRead(
                          LoadElementsKind(array))),
                      Word32BinaryNot(IsNoElementsProtectorCellInvalid())));

  Return(CloneFastJSArray(context, array, std::nullopt,
                          HoleConversionMode::kConvertToUndefined));
}

class ArrayPopulatorAssembler : public CodeStubAssembler {
 public:
  explicit ArrayPopulatorAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  TNode<Object> ConstructArrayLike(TNode<Context> context,
                                   TNode<Object> receiver) {
    TVARIABLE(Object, array);
    Label is_constructor(this), is_not_constructor(this), done(this);
    GotoIf(TaggedIsSmi(receiver), &is_not_constructor);
    Branch(IsConstructor(CAST(receiver)), &is_constructor, &is_not_constructor);

    BIND(&is_constructor);
    {
      array = Construct(context, CAST(receiver));
      Goto(&done);
    }

    BIND(&is_not_constructor);
    {
      Label allocate_js_array(this);

      TNode<Map> array_map = CAST(LoadContextElement(
          context, Context::JS_ARRAY_PACKED_SMI_ELEMENTS_MAP_INDEX));

      TNode<IntPtrT> capacity = IntPtrConstant(0);
      TNode<Smi> length = SmiConstant(0);
      array = AllocateJSArray(PACKED_SMI_ELEMENTS, array_map, capacity, length);
      Goto(&done);
    }

    BIND(&done);
    return array.value();
  }

  TNode<Object> ConstructArrayLike(TNode<Context> context,
                                   TNode<Object> receiver,
                                   TNode<Number> length) {
    TVARIABLE(Object, array);
    Label is_constructor(this), is_not_constructor(this), done(this);
    CSA_DCHECK(this, IsNumberNormalized(length));
    GotoIf(TaggedIsSmi(receiver), &is_not_constructor);
    Branch(IsConstructor(CAST(receiver)), &is_constructor, &is_not_constructor);

    BIND(&is_constructor);
    {
      array = Construct(context, CAST(receiver), length);
      Goto(&done);
    }

    BIND(&is_not_constructor);
    {
      array = ArrayCreate(context, length);
      Goto(&done);
    }

    BIND(&done);
    return array.value();
  }
};

TF_BUILTIN(TypedArrayPrototypeMap, ArrayBuiltinsAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  CodeStubArguments args(this, argc);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<Object> receiver = args.GetReceiver();
  TNode<Object> callbackfn = args.GetOptionalArgumentValue(0);
  TNode<Object> this_arg = args.GetOptionalArgumentValue(1);

  InitIteratingArrayBuiltinBody(context, receiver, callbackfn, this_arg, argc);

  GenerateIteratingTypedArrayBuiltinBody(
      "%TypedArray%.prototype.map",
      &ArrayBuiltinsAssembler::TypedArrayMapResultGenerator,
      &ArrayBuiltinsAssembler::TypedArrayMapProcessor);
}

class ArrayIncludesIndexofAssembler : public CodeStubAssembler {
 public:
  explicit ArrayIncludesIndexofAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  enum SearchVariant { kIncludes, kIndexOf };

  enum class SimpleElementKind { kSmiOrHole, kAny };

  void Generate(SearchVariant variant, TNode<IntPtrT> argc,
                TNode<Context> context);
  void GenerateSmiOrObject(SearchVariant variant, TNode<Context> context,
                           TNode<FixedArray> elements,
                           TNode<Object> search_element,
                           TNode<Smi> array_length, TNode<Smi> from_index,
                           SimpleElementKind array_kind);
  void GeneratePackedDoubles(SearchVariant variant,
                             TNode<FixedDoubleArray> elements,
                             TNode<Object> search_element,
                             TNode<Smi> array_length, TNode<Smi> from_index);
  void GenerateHoleyDoubles(SearchVariant variant,
                            TNode<FixedDoubleArray> elements,
                            TNode<Object> search_element,
                            TNode<Smi> array_length, TNode<Smi> from_index);

  void ReturnIfEmpty(TNode<Smi> length, TNode<Object> value) {
    Label done(this);
    GotoIf(SmiGreaterThan(length, SmiConstant(0)), &done);
    Return(value);
    BIND(&done);
  }

 private:
  // Use SIMD code for arrays larger than kSIMDThreshold (in builtins that have
  // SIMD implementations).
  const int kSIMDThreshold = 48;

  // For now, we can vectorize if:
  //   - SSE3/AVX are present (x86/x64). Note that if __AVX__ is defined, then
  //     __SSE3__ will be as well, so we just check __SSE3__.
  //   - Neon is present and the architecture is 64-bit (because Neon on 32-bit
  //     architecture lacks some instructions).
#if defined(__SSE3__) || defined(V8_HOST_ARCH_ARM64)
  const bool kCanVectorize = true;
#else
  const bool kCanVectorize = false;
#endif
};

void ArrayIncludesIndexofAssembler::Generate(SearchVariant variant,
                                             TNode<IntPtrT> argc,
                                             TNode<Context> context) {
  const int kSearchElementArg = 0;
  const int kFromIndexArg = 1;

  CodeStubArguments args(this, argc);

  TNode<Object> receiver = args.GetReceiver();
  TNode<Object> search_element =
      args.GetOptionalArgumentValue(kSearchElementArg);

  TNode<IntPtrT> intptr_zero = IntPtrConstant(0);

  Label init_index(this), return_not_found(this), call_runtime(this);

  // Take slow path if not a JSArray, if retrieving elements requires
  // traversing prototype, or if access checks are required.
  BranchIfFastJSArrayForRead(receiver, context, &init_index, &call_runtime);

  BIND(&init_index);
  TVARIABLE(IntPtrT, index_var, intptr_zero);
  TNode<JSArray> array = CAST(receiver);

  // JSArray length is always a positive Smi for fast arrays.
  CSA_DCHECK(this, TaggedIsPositiveSmi(LoadJSArrayLength(array)));
  TNode<Smi> array_length = LoadFastJSArrayLength(array);
  TNode<IntPtrT> array_length_untagged = PositiveSmiUntag(array_length);

  {
    // Initialize fromIndex.
    Label is_smi(this), is_nonsmi(this), done(this);

    // If no fromIndex was passed, default to 0.
    GotoIf(IntPtrLessThanOrEqual(args.GetLengthWithoutReceiver(),
                                 IntPtrConstant(kFromIndexArg)),
           &done);

    TNode<Object> start_from = args.AtIndex(kFromIndexArg);
    // Handle Smis and undefined here and everything else in runtime.
    // We must be very careful with side effects from the ToInteger conversion,
    // as the side effects might render previously checked assumptions about
    // the receiver being a fast JSArray and its length invalid.
    Branch(TaggedIsSmi(start_from), &is_smi, &is_nonsmi);

    BIND(&is_nonsmi);
    {
      GotoIfNot(IsUndefined(start_from), &call_runtime);
      Goto(&done);
    }
    BIND(&is_smi);
    {
      TNode<IntPtrT> intptr_start_from = SmiUntag(CAST(start_from));
      index_var = intptr_start_from;

      GotoIf(IntPtrGreaterThanOrEqual(index_var.value(), intptr_zero), &done);
      // The fromIndex is negative: add it to the array's length.
      index_var = IntPtrAdd(array_length_untagged, index_var.value());
      // Clamp negative results at zero.
      GotoIf(IntPtrGreaterThanOrEqual(index_var.value(), intptr_zero), &done);
      index_var = intptr_zero;
      Goto(&done);
    }
    BIND(&done);
  }

  // Fail early if startIndex >= array.length.
  GotoIf(IntPtrGreaterThanOrEqual(index_var.value(), array_length_untagged),
         &return_not_found);

  Label if_smi(this), if_smiorobjects(this), if_packed_doubles(this),
      if_holey_doubles(this);

  TNode<Int32T> elements_kind = LoadElementsKind(array);
  TNode<FixedArrayBase> elements = LoadElements(array);
  static_assert(PACKED_SMI_ELEMENTS == 0);
  static_assert(HOLEY_SMI_ELEMENTS == 1);
  static_assert(PACKED_ELEMENTS == 2);
  static_assert(HOLEY_ELEMENTS == 3);
  GotoIf(IsElementsKindLessThanOrEqual(elements_kind, HOLEY_SMI_ELEMENTS),
         &if_smi);
  GotoIf(IsElementsKindLessThanOrEqual(elements_kind, HOLEY_ELEMENTS),
         &if_smiorobjects);
  GotoIf(
      ElementsKindEqual(elements_kind, Int32Constant(PACKED_DOUBLE_ELEMENTS)),
      &if_packed_doubles);
  GotoIf(ElementsKindEqual(elements_kind, Int32Constant(HOLEY_DOUBLE_ELEMENTS)),
         &if_holey_doubles);
  GotoIf(IsElementsKindLessThanOrEqual(elements_kind,
                                       LAST_ANY_NONEXTENSIBLE_ELEMENTS_KIND),
         &if_smiorobjects);
  Goto(&return_not_found);

  BIND(&if_smi);
  {
    Builtin builtin = (variant == kIncludes) ? Builtin::kArrayIncludesSmi
                                             : Builtin::kArrayIndexOfSmi;
    TNode<Object> result =
        CallBuiltin(builtin, context, elements, search_element, array_length,
                    SmiTag(index_var.value()));
    args.PopAndReturn(result);
  }

  BIND(&if_smiorobjects);
  {
    Builtin builtin = (variant == kIncludes)
                          ? Builtin::kArrayIncludesSmiOrObject
                          : Builtin::kArrayIndexOfSmiOrObject;
    TNode<Object> result =
        CallBuiltin(builtin, context, elements, search_element, array_length,
                    SmiTag(index_var.value()));
    args.PopAndReturn(result);
  }

  BIND(&if_packed_doubles);
  {
    Builtin builtin = (variant == kIncludes)
                          ? Builtin::kArrayIncludesPackedDoubles
                          : Builtin::kArrayIndexOfPackedDoubles;
    TNode<Object> result =
        CallBuiltin(builtin, context, elements, search_element, array_length,
                    SmiTag(index_var.value()));
    args.PopAndReturn(result);
  }

  BIND(&if_holey_doubles);
  {
    Builtin builtin = (variant == kIncludes)
                          ? Builtin::kArrayIncludesHoleyDoubles
                          : Builtin::kArrayIndexOfHoleyDoubles;
    TNode<Object> result =
        CallBuiltin(builtin, context, elements, search_element, array_length,
                    SmiTag(index_var.value()));
    args.PopAndReturn(result);
  }

  BIND(&return_not_found);
  if (variant == kIncludes) {
    args.PopAndReturn(FalseConstant());
  } else {
    args.PopAndReturn(NumberConstant(-1));
  }

  BIND(&call_runtime);
  {
    TNode<Object> start_from = args.GetOptionalArgumentValue(kFromIndexArg);
    Runtime::FunctionId function = variant == kIncludes
                                       ? Runtime::kArrayIncludes_Slow
                                       : Runtime::kArrayIndexOf;
    args.PopAndReturn(
        CallRuntime(function, context, array, search_element, start_from));
  }
}

void ArrayIncludesIndexofAssembler::GenerateSmiOrObject(
    SearchVariant variant, TNode<Context> context, TNode<FixedArray> elements,
    TNode<Object> search_element, TNode<Smi> array_length,
    TNode<Smi> from_index, SimpleElementKind array_kind) {
  TVARIABLE(IntPtrT, index_var, SmiUntag(from_index));
  TVARIABLE(Float64T, search_num);
  TNode<IntPtrT> array_length_untagged = PositiveSmiUntag(array_length);

  Label ident_loop(this, &index_var), heap_num_loop(this, &search_num),
      string_loop(this), bigint_loop(this, &index_var),
      undef_loop(this, &index_var), not_smi(this), not_heap_num(this),
      return_found(this), return_not_found(this);

  GotoIfNot(TaggedIsSmi(search_element), &not_smi);
  search_num = SmiToFloat64(CAST(search_element));
  Goto(&heap_num_loop);

  BIND(&not_smi);
  if (variant == kIncludes) {
    GotoIf(IsUndefined(search_element), &undef_loop);
  }
  TNode<Map> map = LoadMap(CAST(search_element));
  GotoIfNot(IsHeapNumberMap(map), &not_heap_num);
  search_num = LoadHeapNumberValue(CAST(search_element));
  Goto(&heap_num_loop);

  BIND(&not_heap_num);
  TNode<Uint16T> search_type = LoadMapInstanceType(map);
  GotoIf(IsStringInstanceType(search_type), &string_loop);
  GotoIf(IsBigIntInstanceType(search_type), &bigint_loop);

  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  GotoIfNot(UniqueInt32Constant(kCanVectorize), &ident_loop);
  {
    Label simd_call(this);
    Branch(
        UintPtrLessThan(array_length_untagged, IntPtrConstant(kSIMDThreshold)),
        &ident_loop, &simd_call);
    BIND(&simd_call);
    TNode<ExternalReference> simd_function = ExternalConstant(
        ExternalReference::array_indexof_includes_smi_or_object());
    TNode<IntPtrT> result = UncheckedCast<IntPtrT>(CallCFunction(
        simd_function, MachineType::UintPtr(),
        std::make_pair(MachineType::TaggedPointer(), elements),
        std::make_pair(MachineType::UintPtr(), array_length_untagged),
        std::make_pair(MachineType::UintPtr(), index_var.value()),
        std::make_pair(MachineType::TaggedPointer(), search_element)));
    index_var = ReinterpretCast<IntPtrT>(result);
    Branch(IntPtrLessThan(index_var.value(), IntPtrConstant(0)),
           &return_not_found, &return_found);
  }

  BIND(&ident_loop);
  {
    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);
    TNode<Object> element_k =
        UnsafeLoadFixedArrayElement(elements, index_var.value());
    GotoIf(TaggedEqual(element_k, search_element), &return_found);

    Increment(&index_var);
    Goto(&ident_loop);
  }

  if (variant == kIncludes) {
    BIND(&undef_loop);

    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);
    TNode<Object> element_k =
        UnsafeLoadFixedArrayElement(elements, index_var.value());
    GotoIf(IsUndefined(element_k), &return_found);
    GotoIf(IsTheHole(element_k), &return_found);

    Increment(&index_var);
    Goto(&undef_loop);
  }

  BIND(&heap_num_loop);
  {
    Label nan_loop(this, &index_var), not_nan_loop(this, &index_var);
    Label* nan_handling = variant == kIncludes ? &nan_loop : &return_not_found;
    GotoIfNot(Float64Equal(search_num.value(), search_num.value()),
              nan_handling);

    // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
    // that the graph structure does not depend on the value of the predicate
    // (BoolConstant uses cached nodes).
    GotoIfNot(UniqueInt32Constant(kCanVectorize &&
                                  array_kind == SimpleElementKind::kSmiOrHole),
              &not_nan_loop);
    {
      Label smi_check(this), simd_call(this);
      Branch(UintPtrLessThan(array_length_untagged,
                             IntPtrConstant(kSIMDThreshold)),
             &not_nan_loop, &smi_check);
      BIND(&smi_check);
      Branch(TaggedIsSmi(search_element), &simd_call, &not_nan_loop);
      BIND(&simd_call);
      TNode<ExternalReference> simd_function = ExternalConstant(
          ExternalReference::array_indexof_includes_smi_or_object());
      TNode<IntPtrT> result = UncheckedCast<IntPtrT>(CallCFunction(
          simd_function, MachineType::UintPtr(),
          std::make_pair(MachineType::TaggedPointer(), elements),
          std::make_pair(MachineType::UintPtr(), array_length_untagged),
          std::make_pair(MachineType::UintPtr(), index_var.value()),
          std::make_pair(MachineType::TaggedPointer(), search_element)));
      index_var = ReinterpretCast<IntPtrT>(result);
      Branch(IntPtrLessThan(index_var.value(), IntPtrConstant(0)),
             &return_not_found, &return_found);
    }

    BIND(&not_nan_loop);
    {
      Label continue_loop(this), element_k_not_smi(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
                &return_not_found);
      TNode<Object> element_k =
          UnsafeLoadFixedArrayElement(elements, index_var.value());
      GotoIfNot(TaggedIsSmi(element_k), &element_k_not_smi);
      Branch(Float64Equal(search_num.value(), SmiToFloat64(CAST(element_k))),
             &return_found, &continue_loop);

      BIND(&element_k_not_smi);
      GotoIfNot(IsHeapNumber(CAST(element_k)), &continue_loop);
      Branch(Float64Equal(search_num.value(),
                          LoadHeapNumberValue(CAST(element_k))),
             &return_found, &continue_loop);

      BIND(&continue_loop);
      Increment(&index_var);
      Goto(&not_nan_loop);
    }

    // Array.p.includes uses SameValueZero comparisons, where NaN == NaN.
    if (variant == kIncludes) {
      BIND(&nan_loop);
      Label continue_loop(this);
      GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
                &return_not_found);
      TNode<Object> element_k =
          UnsafeLoadFixedArrayElement(elements, index_var.value());
      GotoIf(TaggedIsSmi(element_k), &continue_loop);
      GotoIfNot(IsHeapNumber(CAST(element_k)), &continue_loop);
      BranchIfFloat64IsNaN(LoadHeapNumberValue(CAST(element_k)), &return_found,
                           &continue_loop);

      BIND(&continue_loop);
      Increment(&index_var);
      Goto(&nan_loop);
    }
  }

  BIND(&string_loop);
  {
    TNode<String> search_element_string = CAST(search_element);
    Label continue_loop(this), next_iteration(this, &index_var),
        slow_compare(this), runtime(this, Label::kDeferred);
    TNode<IntPtrT> search_length =
        LoadStringLengthAsWord(search_element_string);
    Goto(&next_iteration);
    BIND(&next_iteration);
    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);
    TNode<Object> element_k =
        UnsafeLoadFixedArrayElement(elements, index_var.value());
    GotoIf(TaggedIsSmi(element_k), &continue_loop);
    GotoIf(TaggedEqual(search_element_string, element_k), &return_found);
    TNode<Uint16T> element_k_type = LoadInstanceType(CAST(element_k));
    GotoIfNot(IsStringInstanceType(element_k_type), &continue_loop);
    Branch(IntPtrEqual(search_length, LoadStringLengthAsWord(CAST(element_k))),
           &slow_compare, &continue_loop);

    BIND(&slow_compare);
    StringBuiltinsAssembler string_asm(state());
    string_asm.StringEqual_Core(search_element_string, search_type,
                                CAST(element_k), element_k_type, search_length,
                                &return_found, &continue_loop, &runtime);
    BIND(&runtime);
    TNode<Object> result = CallRuntime(Runtime::kStringEqual, context,
                                       search_element_string, element_k);
    Branch(TaggedEqual(result, TrueConstant()), &return_found, &continue_loop);

    BIND(&continue_loop);
    Increment(&index_var);
    Goto(&next_iteration);
  }

  BIND(&bigint_loop);
  {
    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);

    TNode<Object> element_k =
        UnsafeLoadFixedArrayElement(elements, index_var.value());
    Label continue_loop(this);
    GotoIf(TaggedIsSmi(element_k), &continue_loop);
    GotoIfNot(IsBigInt(CAST(element_k)), &continue_loop);
    TNode<Object> result = CallRuntime(Runtime::kBigIntEqualToBigInt, context,
                                       search_element, element_k);
    Branch(TaggedEqual(result, TrueConstant()), &return_found, &continue_loop);

    BIND(&continue_loop);
    Increment(&index_var);
    Goto(&bigint_loop);
  }
  BIND(&return_found);
  if (variant == kIncludes) {
    Return(TrueConstant());
  } else {
    Return(SmiTag(index_var.value()));
  }

  BIND(&return_not_found);
  if (variant == kIncludes) {
    Return(FalseConstant());
  } else {
    Return(NumberConstant(-1));
  }
}

void ArrayIncludesIndexofAssembler::GeneratePackedDoubles(
    SearchVariant variant, TNode<FixedDoubleArray> elements,
    TNode<Object> search_element, TNode<Smi> array_length,
    TNode<Smi> from_index) {
  TVARIABLE(IntPtrT, index_var, SmiUntag(from_index));
  TNode<IntPtrT> array_length_untagged = PositiveSmiUntag(array_length);

  Label nan_loop(this, &index_var), not_nan_case(this),
      not_nan_loop(this, &index_var), hole_loop(this, &index_var),
      search_notnan(this), return_found(this), return_not_found(this);
  TVARIABLE(Float64T, search_num);
  search_num = Float64Constant(0);

  GotoIfNot(TaggedIsSmi(search_element), &search_notnan);
  search_num = SmiToFloat64(CAST(search_element));
  Goto(&not_nan_case);

  BIND(&search_notnan);
  GotoIfNot(IsHeapNumber(CAST(search_element)), &return_not_found);

  search_num = LoadHeapNumberValue(CAST(search_element));

  Label* nan_handling = variant == kIncludes ? &nan_loop : &return_not_found;
  BranchIfFloat64IsNaN(search_num.value(), nan_handling, &not_nan_case);

  BIND(&not_nan_case);
  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  GotoIfNot(UniqueInt32Constant(kCanVectorize), &not_nan_loop);
  {
    Label simd_call(this);
    Branch(
        UintPtrLessThan(array_length_untagged, IntPtrConstant(kSIMDThreshold)),
        &not_nan_loop, &simd_call);
    BIND(&simd_call);
    TNode<ExternalReference> simd_function =
        ExternalConstant(ExternalReference::array_indexof_includes_double());
    TNode<IntPtrT> result = UncheckedCast<IntPtrT>(CallCFunction(
        simd_function, MachineType::UintPtr(),
        std::make_pair(MachineType::TaggedPointer(), elements),
        std::make_pair(MachineType::UintPtr(), array_length_untagged),
        std::make_pair(MachineType::UintPtr(), index_var.value()),
        std::make_pair(MachineType::TaggedPointer(), search_element)));
    index_var = ReinterpretCast<IntPtrT>(result);
    Branch(IntPtrLessThan(index_var.value(), IntPtrConstant(0)),
           &return_not_found, &return_found);
  }

  BIND(&not_nan_loop);
  {
    Label continue_loop(this);
    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);
    TNode<Float64T> element_k =
        LoadFixedDoubleArrayElement(elements, index_var.value());
    Branch(Float64Equal(element_k, search_num.value()), &return_found,
           &continue_loop);
    BIND(&continue_loop);
    Increment(&index_var);
    Goto(&not_nan_loop);
  }

  // Array.p.includes uses SameValueZero comparisons, where NaN == NaN.
  if (variant == kIncludes) {
    BIND(&nan_loop);
    Label continue_loop(this);
    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);
    TNode<Float64T> element_k =
        LoadFixedDoubleArrayElement(elements, index_var.value());
    BranchIfFloat64IsNaN(element_k, &return_found, &continue_loop);
    BIND(&continue_loop);
    Increment(&index_var);
    Goto(&nan_loop);
  }

  BIND(&return_found);
  if (variant == kIncludes) {
    Return(TrueConstant());
  } else {
    Return(SmiTag(index_var.value()));
  }

  BIND(&return_not_found);
  if (variant == kIncludes) {
    Return(FalseConstant());
  } else {
    Return(NumberConstant(-1));
  }
}

void ArrayIncludesIndexofAssembler::GenerateHoleyDoubles(
    SearchVariant variant, TNode<FixedDoubleArray> elements,
    TNode<Object> search_element, TNode<Smi> array_length,
    TNode<Smi> from_index) {
  TVARIABLE(IntPtrT, index_var, SmiUntag(from_index));
  TNode<IntPtrT> array_length_untagged = PositiveSmiUntag(array_length);

  Label nan_loop(this, &index_var), not_nan_case(this),
      not_nan_loop(this, &index_var), hole_loop(this, &index_var),
      search_notnan(this), return_found(this), return_not_found(this);
  TVARIABLE(Float64T, search_num);
  search_num = Float64Constant(0);

  GotoIfNot(TaggedIsSmi(search_element), &search_notnan);
  search_num = SmiToFloat64(CAST(search_element));
  Goto(&not_nan_case);

  BIND(&search_notnan);
  if (variant == kIncludes) {
    GotoIf(IsUndefined(search_element), &hole_loop);
  }
  GotoIfNot(IsHeapNumber(CAST(search_element)), &return_not_found);

  search_num = LoadHeapNumberValue(CAST(search_element));

  Label* nan_handling = variant == kIncludes ? &nan_loop : &return_not_found;
  BranchIfFloat64IsNaN(search_num.value(), nan_handling, &not_nan_case);

  BIND(&not_nan_case);
  // Use UniqueInt32Constant instead of BoolConstant here in order to ensure
  // that the graph structure does not depend on the value of the predicate
  // (BoolConstant uses cached nodes).
  GotoIfNot(UniqueInt32Constant(kCanVectorize), &not_nan_loop);
  {
    Label simd_call(this);
    Branch(
        UintPtrLessThan(array_length_untagged, IntPtrConstant(kSIMDThreshold)),
        &not_nan_loop, &simd_call);
    BIND(&simd_call);
    TNode<ExternalReference> simd_function =
        ExternalConstant(ExternalReference::array_indexof_includes_double());
    TNode<IntPtrT> result = UncheckedCast<IntPtrT>(CallCFunction(
        simd_function, MachineType::UintPtr(),
        std::make_pair(MachineType::TaggedPointer(), elements),
        std::make_pair(MachineType::UintPtr(), array_length_untagged),
        std::make_pair(MachineType::UintPtr(), index_var.value()),
        std::make_pair(MachineType::TaggedPointer(), search_element)));
    index_var = ReinterpretCast<IntPtrT>(result);
    Branch(IntPtrLessThan(index_var.value(), IntPtrConstant(0)),
           &return_not_found, &return_found);
  }

  BIND(&not_nan_loop);
  {
    Label continue_loop(this);
    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);

    // No need for hole checking here; the following Float64Equal will
    // return 'not equal' for holes anyway.
    TNode<Float64T> element_k =
        LoadFixedDoubleArrayElement(elements, index_var.value());

    Branch(Float64Equal(element_k, search_num.value()), &return_found,
           &continue_loop);
    BIND(&continue_loop);
    Increment(&index_var);
    Goto(&not_nan_loop);
  }

  // Array.p.includes uses SameValueZero comparisons, where NaN == NaN.
  if (variant == kIncludes) {
    BIND(&nan_loop);
    Label continue_loop(this);
    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);

    // Load double value or continue if it's the hole NaN.
    TNode<Float64T> element_k = LoadFixedDoubleArrayElement(
        elements, index_var.value(), &continue_loop);

    BranchIfFloat64IsNaN(element_k, &return_found, &continue_loop);
    BIND(&continue_loop);
    Increment(&index_var);
    Goto(&nan_loop);
  }

  // Array.p.includes treats the hole as undefined.
  if (variant == kIncludes) {
    BIND(&hole_loop);
    GotoIfNot(UintPtrLessThan(index_var.value(), array_length_untagged),
              &return_not_found);

    // Check if the element is a double hole, but don't load it.
    LoadFixedDoubleArrayElement(elements, index_var.value(), &return_found,
                                MachineType::None());

    Increment(&index_var);
    Goto(&hole_loop);
  }

  BIND(&return_found);
  if (variant == kIncludes) {
    Return(TrueConstant());
  } else {
    Return(SmiTag(index_var.value()));
  }

  BIND(&return_not_found);
  if (variant == kIncludes) {
    Return(FalseConstant());
  } else {
    Return(NumberConstant(-1));
  }
}

TF_BUILTIN(ArrayIncludes, ArrayIncludesIndexofAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  auto context = Parameter<Context>(Descriptor::kContext);

  Generate(kIncludes, argc, context);
}

TF_BUILTIN(ArrayIncludesSmi, ArrayIncludesIndexofAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto elements = Parameter<FixedArray>(Descriptor::kElements);
  auto search_element = Parameter<Object>(Descriptor::kSearchElement);
  auto array_length = Parameter<Smi>(Descriptor::kLength);
  auto from_index = Parameter<Smi>(Descriptor::kFromIndex);

  GenerateSmiOrObject(kIncludes, context, elements, search_element,
                      array_length, from_index, SimpleElementKind::kSmiOrHole);
}

TF_BUILTIN(ArrayIncludesSmiOrObject, ArrayIncludesIndexofAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto elements = Parameter<FixedArray>(Descriptor::kElements);
  auto search_element = Parameter<Object>(Descriptor::kSearchElement);
  auto array_length = Parameter<Smi>(Descriptor::kLength);
  auto from_index = Parameter<Smi>(Descriptor::kFromIndex);

  GenerateSmiOrObject(kIncludes, context, elements, search_element,
                      array_length, from_index, SimpleElementKind::kAny);
}

TF_BUILTIN(ArrayIncludesPackedDoubles, ArrayIncludesIndexofAssembler) {
  auto elements = Parameter<FixedArrayBase>(Descriptor::kElements);
  auto search_element = Parameter<Object>(Descriptor::kSearchElement);
  auto array_length = Parameter<Smi>(Descriptor::kLength);
  auto from_index = Parameter<Smi>(Descriptor::kFromIndex);

  ReturnIfEmpty(array_length, FalseConstant());
  GeneratePackedDoubles(kIncludes, CAST(elements), search_element, array_length,
                        from_index);
}

TF_BUILTIN(ArrayIncludesHoleyDoubles, ArrayIncludesIndexofAssembler) {
  auto elements = Parameter<FixedArrayBase>(Descriptor::kElements);
  auto search_element = Parameter<Object>(Descriptor::kSearchElement);
  auto array_length = Parameter<Smi>(Descriptor::kLength);
  auto from_index = Parameter<Smi>(Descriptor::kFromIndex);

  ReturnIfEmpty(array_length, FalseConstant());
  GenerateHoleyDoubles(kIncludes, CAST(elements), search_element, array_length,
                       from_index);
}

TF_BUILTIN(ArrayIndexOf, ArrayIncludesIndexofAssembler) {
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  auto context = Parameter<Context>(Descriptor::kContext);

  Generate(kIndexOf, argc, context);
}

TF_BUILTIN(ArrayIndexOfSmi, ArrayIncludesIndexofAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto elements = Parameter<FixedArray>(Descriptor::kElements);
  auto search_element = Parameter<Object>(Descriptor::kSearchElement);
  auto array_length = Parameter<Smi>(Descriptor::kLength);
  auto from_index = Parameter<Smi>(Descriptor::kFromIndex);

  GenerateSmiOrObject(kIndexOf, context, elements, search_element, array_length,
                      from_index, SimpleElementKind::kSmiOrHole);
}

TF_BUILTIN(ArrayIndexOfSmiOrObject, ArrayIncludesIndexofAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto elements = Parameter<FixedArray>(Descriptor::kElements);
  auto search_element = Parameter<Object>(Descriptor::kSearchElement);
  auto array_length = Parameter<Smi>(Descriptor::kLength);
  auto from_index = Parameter<Smi>(Descriptor::kFromIndex);

  GenerateSmiOrObject(kIndexOf, context, elements, search_element, array_length,
                      from_index, SimpleElementKind::kAny);
}

TF_BUILTIN(ArrayIndexOfPackedDoubles, ArrayIncludesIndexofAssembler) {
  auto elements = Parameter<FixedArrayBase>(Descriptor::kElements);
  auto search_element = Parameter<Object>(Descriptor::kSearchElement);
  auto array_length = Parameter<Smi>(Descriptor::kLength);
  auto from_index = Parameter<Smi>(Descriptor::kFromIndex);

  ReturnIfEmpty(array_length, NumberConstant(-1));
  GeneratePackedDoubles(kIndexOf, CAST(elements), search_element, array_length,
                        from_index);
}

TF_BUILTIN(ArrayIndexOfHoleyDoubles, ArrayIncludesIndexofAssembler) {
  auto elements = Parameter<FixedArrayBase>(Descriptor::kElements);
  auto search_element = Parameter<Object>(Descriptor::kSearchElement);
  auto array_length = Parameter<Smi>(Descriptor::kLength);
  auto from_index = Parameter<Smi>(Descriptor::kFromIndex);

  ReturnIfEmpty(array_length, NumberConstant(-1));
  GenerateHoleyDoubles(kIndexOf, CAST(elements), search_element, array_length,
                       from_index);
}

// ES #sec-array.prototype.values
TF_BUILTIN(ArrayPrototypeValues, CodeStubAssembler) {
  auto context = Parameter<NativeContext>(Descriptor::kContext);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  Return(CreateArrayIterator(context, ToObject_Inline(context, receiver),
                             IterationKind::kValues));
}

// ES #sec-array.prototype.entries
TF_BUILTIN(ArrayPrototypeEntries, CodeStubAssembler) {
  auto context = Parameter<NativeContext>(Descriptor::kContext);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  Return(CreateArrayIterator(context, ToObject_Inline(context, receiver),
                             IterationKind::kEntries));
}

// ES #sec-array.prototype.keys
TF_BUILTIN(ArrayPrototypeKeys, CodeStubAssembler) {
  auto context = Parameter<NativeContext>(Descriptor::kContext);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  Return(CreateArrayIterator(context, ToObject_Inline(context, receiver),
                             IterationKind::kKeys));
}

// ES #sec-%arrayiteratorprototype%.next
TF_BUILTIN(ArrayIteratorPrototypeNext, CodeStubAssembler) {
  const char* method_name = "Array Iterator.prototype.next";

  auto context = Parameter<Context>(Descriptor::kContext);
  auto maybe_iterator = Parameter<Object>(Descriptor::kReceiver);

  TVARIABLE(Boolean, var_done, TrueConstant());
  TVARIABLE(Object, var_value, UndefinedConstant());

  Label allocate_entry_if_needed(this);
  Label allocate_iterator_result(this);
  Label if_typedarray(this), if_other(this, Label::kDeferred), if_array(this),
      if_generic(this, Label::kDeferred);
  Label set_done(this, Label::kDeferred);

  // If O does not have all of the internal slots of an Array Iterator Instance
  // (22.1.5.3), throw a TypeError exception
  ThrowIfNotInstanceType(context, maybe_iterator, JS_ARRAY_ITERATOR_TYPE,
                         method_name);

  TNode<JSArrayIterator> iterator = CAST(maybe_iterator);

  // Let a be O.[[IteratedObject]].
  TNode<JSReceiver> array = LoadJSArrayIteratorIteratedObject(iterator);

  // Let index be O.[[ArrayIteratorNextIndex]].
  TNode<Number> index = LoadJSArrayIteratorNextIndex(iterator);
  CSA_DCHECK(this, IsNumberNonNegativeSafeInteger(index));

  // Dispatch based on the type of the {array}.
  TNode<Map> array_map = LoadMap(array);
  TNode<Uint16T> array_type = LoadMapInstanceType(array_map);
  GotoIf(InstanceTypeEqual(array_type, JS_ARRAY_TYPE), &if_array);
  Branch(InstanceTypeEqual(array_type, JS_TYPED_ARRAY_TYPE), &if_typedarray,
         &if_other);

  BIND(&if_array);
  {
    // If {array} is a JSArray, then the {index} must be in Unsigned32 range.
    CSA_DCHECK(this, IsNumberArrayIndex(index));

    // Check that the {index} is within range for the {array}. We handle all
    // kinds of JSArray's here, so we do the computation on Uint32.
    TNode<Uint32T> index32 = ChangeNonNegativeNumberToUint32(index);
    TNode<Uint32T> length32 =
        ChangeNonNegativeNumberToUint32(LoadJSArrayLength(CAST(array)));
    GotoIfNot(Uint32LessThan(index32, length32), &set_done);
    StoreJSArrayIteratorNextIndex(
        iterator, ChangeUint32ToTagged(Uint32Add(index32, Uint32Constant(1))));

    var_done = FalseConstant();
    var_value = index;

    GotoIf(Word32Equal(LoadAndUntagToWord32ObjectField(
                           iterator, JSArrayIterator::kKindOffset),
                       Int32Constant(static_cast<int>(IterationKind::kKeys))),
           &allocate_iterator_result);

    Label if_hole(this, Label::kDeferred);
    TNode<Int32T> elements_kind = LoadMapElementsKind(array_map);
    TNode<FixedArrayBase> elements = LoadElements(CAST(array));
    GotoIfForceSlowPath(&if_generic);
    var_value = LoadFixedArrayBaseElementAsTagged(
        elements, Signed(ChangeUint32ToWord(index32)), elements_kind,
        &if_generic, &if_hole);
    Goto(&allocate_entry_if_needed);

    BIND(&if_hole);
    {
      GotoIf(IsNoElementsProtectorCellInvalid(), &if_generic);
      GotoIfNot(IsPrototypeInitialArrayPrototype(context, array_map),
                &if_generic);
      var_value = UndefinedConstant();
      Goto(&allocate_entry_if_needed);
    }
  }

  BIND(&if_other);
  {
    // We cannot enter here with either JSArray's or JSTypedArray's.
    CSA_DCHECK(this, Word32BinaryNot(IsJSArray(array)));
    CSA_DCHECK(this, Word32BinaryNot(IsJSTypedArray(array)));

    // Check that the {index} is within the bounds of the {array}s "length".
    TNode<Number> length = CAST(
        CallBuiltin(Builtin::kToLength, context,
                    GetProperty(context, array, factory()->length_string())));
    GotoIfNumberGreaterThanOrEqual(index, length, &set_done);
    StoreJSArrayIteratorNextIndex(iterator, NumberInc(index));

    var_done = FalseConstant();
    var_value = index;

    Branch(Word32Equal(LoadAndUntagToWord32ObjectField(
                           iterator, JSArrayIterator::kKindOffset),
                       Int32Constant(static_cast<int>(IterationKind::kKeys))),
           &allocate_iterator_result, &if_generic);
  }

  BIND(&set_done);
  {
    // Change the [[ArrayIteratorNextIndex]] such that the {iterator} will
    // never produce values anymore, because it will always fail the bounds
    // check. Note that this is different from what the specification does,
    // which is changing the [[IteratedObject]] to undefined, because leaving
    // [[IteratedObject]] alone helps TurboFan to generate better code with
    // the inlining in JSCallReducer::ReduceArrayIteratorPrototypeNext().
    //
    // The terminal value we chose here depends on the type of the {array},
    // for JSArray's we use kMaxUInt32 so that TurboFan can always use
    // Word32 representation for fast-path indices (and this is safe since
    // the "length" of JSArray's is limited to Unsigned32 range). For other
    // JSReceiver's we have to use kMaxSafeInteger, since the "length" can
    // be any arbitrary value in the safe integer range.
    //
    // Note specifically that JSTypedArray's will never take this path, so
    // we don't need to worry about their maximum value.
    CSA_DCHECK(this, Word32BinaryNot(IsJSTypedArray(array)));
    TNode<Number> max_length =
        SelectConstant(IsJSArray(array), NumberConstant(kMaxUInt32),
                       NumberConstant(kMaxSafeInteger));
    StoreJSArrayIteratorNextIndex(iterator, max_length);
    Goto(&allocate_iterator_result);
  }

  BIND(&if_generic);
  {
    var_value = GetProperty(context, array, index);
    Goto(&allocate_entry_if_needed);
  }

  BIND(&if_typedarray);
  {
    // Overflowing uintptr range also means end of iteration.
    TNode<UintPtrT> index_uintptr =
        ChangeSafeIntegerNumberToUintPtr(index, &allocate_iterator_result);

    // If we go outside of the {length}, we don't need to update the
    // [[ArrayIteratorNextIndex]] anymore, since a JSTypedArray's
    // length cannot change anymore, so this {iterator} will never
    // produce values again anyways.
    Label detached(this);
    TNode<UintPtrT> length =
        LoadJSTypedArrayLengthAndCheckDetached(CAST(array), &detached);
    GotoIfNot(UintPtrLessThan(index_uintptr, length),
              &allocate_iterator_result);
    // TODO(v8:4153): Consider storing next index as uintptr. Update this and
    // the relevant TurboFan code.
    StoreJSArrayIteratorNextIndex(
        iterator,
        ChangeUintPtrToTagged(UintPtrAdd(index_uintptr, UintPtrConstant(1))));

    var_done = FalseConstant();
    var_value = index;

    GotoIf(Word32Equal(LoadAndUntagToWord32ObjectField(
                           iterator, JSArrayIterator::kKindOffset),
                       Int32Constant(static_cast<int>(IterationKind::kKeys))),
           &allocate_iterator_result);

    TNode<Int32T> elements_kind = LoadMapElementsKind(array_map);
    TNode<RawPtrT> data_ptr = LoadJSTypedArrayDataPtr(CAST(array));
    var_value = LoadFixedTypedArrayElementAsTagged(data_ptr, index_uintptr,
                                                   elements_kind);
    Goto(&allocate_entry_if_needed);

    BIND(&detached);
    ThrowTypeError(context, MessageTemplate::kDetachedOperation, method_name);
  }

  BIND(&allocate_entry_if_needed);
  {
    GotoIf(Word32Equal(LoadAndUntagToWord32ObjectField(
                           iterator, JSArrayIterator::kKindOffset),
                       Int32Constant(static_cast<int>(IterationKind::kValues))),
           &allocate_iterator_result);

    TNode<JSObject> result =
        AllocateJSIteratorResultForEntry(context, index, var_value.value());
    Return(result);
  }

  BIND(&allocate_iterator_result);
  {
    TNode<JSObject> result =
        AllocateJSIteratorResult(context, var_value.value(), var_done.value());
    Return(result);
  }
}

TF_BUILTIN(ArrayConstructor, ArrayBuiltinsAssembler) {
  // This is a trampoline to ArrayConstructorImpl which just adds
  // allocation_site parameter value and sets new_target if necessary.
  auto context = Parameter<Context>(Descriptor::kContext);
  auto function = Parameter<JSFunction>(Descriptor::kTarget);
  auto new_target = Parameter<Object>(Descriptor::kNewTarget);
  auto argc = UncheckedParameter<Int32T>(Descriptor::kActualArgumentsCount);

  // If new_target is undefined, then this is the 'Call' case, so set new_target
  // to function.
  new_target =
      SelectConstant<Object>(IsUndefined(new_target), function, new_target);

  // Run the native code for the Array function called as a normal function.
  TNode<Oddball> no_gc_site = UndefinedConstant();
  TailCallBuiltin(Builtin::kArrayConstructorImpl, context, function, new_target,
                  argc, no_gc_site);
}

void ArrayBuiltinsAssembler::TailCallArrayConstructorStub(
    const Callable& callable, TNode<Context> context, TNode<JSFunction> target,
    TNode<HeapObject> allocation_site_or_undefined, TNode<Int32T> argc) {
  TNode<Code> code = HeapConstantNoHole(callable.code());

  // We are going to call here ArrayNoArgumentsConstructor or
  // ArraySingleArgumentsConstructor which in addition to the register arguments
  // also expect some number of arguments on the expression stack.
  // Since
  // 1) incoming JS arguments are still on the stack,
  // 2) the ArrayNoArgumentsConstructor, ArraySingleArgumentsConstructor and
  //    ArrayNArgumentsConstructor are defined so that the register arguments
  //    are passed on the same registers,
  // in order to be able to generate a tail call to those builtins we do the
  // following trick here: we tail call to the constructor builtin using
  // ArrayNArgumentsConstructorDescriptor, so the tail call instruction
  // pops the current frame but leaves all the incoming JS arguments on the
  // expression stack so that the target builtin can still find them where it
  // expects.
  TailCallStub(ArrayNArgumentsConstructorDescriptor{}, code, context, target,
               allocation_site_or_undefined, argc);
}

void ArrayBuiltinsAssembler::CreateArrayDispatchNoArgument(
    TNode<Context> context, TNode<JSFunction> target, TNode<Int32T> argc,
    AllocationSiteOverrideMode mode,
    std::optional<TNode<AllocationSite>> allocation_site) {
  if (mode == DISABLE_ALLOCATION_SITES) {
    Callable callable = CodeFactory::ArrayNoArgumentConstructor(
        isolate(), GetInitialFastElementsKind(), mode);

    TailCallArrayConstructorStub(callable, context, target, UndefinedConstant(),
                                 argc);
  } else {
    DCHECK_EQ(mode, DONT_OVERRIDE);
    DCHECK(allocation_site);
    TNode<Int32T> elements_kind = LoadElementsKind(*allocation_site);

    // TODO(ishell): Compute the builtin index dynamically instead of
    // iterating over all expected elements kinds.
    int last_index =
        GetSequenceIndexFromFastElementsKind(TERMINAL_FAST_ELEMENTS_KIND);
    for (int i = 0; i <= last_index; ++i) {
      Label next(this);
      ElementsKind kind = GetFastElementsKindFromSequenceIndex(i);
      GotoIfNot(Word32Equal(elements_kind, Int32Constant(kind)), &next);

      Callable callable =
          CodeFactory::ArrayNoArgumentConstructor(isolate(), kind, mode);

      TailCallArrayConstructorStub(callable, context, target, *allocation_site,
                                   argc);

      BIND(&next);
    }

    // If we reached this point there is a problem.
    Abort(AbortReason::kUnexpectedElementsKindInArrayConstructor);
  }
}

void ArrayBuiltinsAssembler::CreateArrayDispatchSingleArgument(
    TNode<Context> context, TNode<JSFunction> target, TNode<Int32T> argc,
    AllocationSiteOverrideMode mode,
    std::optional<TNode<AllocationSite>> allocation_site) {
  if (mode == DISABLE_ALLOCATION_SITES) {
    ElementsKind initial = GetInitialFastElementsKind();
    ElementsKind holey_initial = GetHoleyElementsKind(initial);
    Callable callable = CodeFactory::ArraySingleArgumentConstructor(
        isolate(), holey_initial, mode);

    TailCallArrayConstructorStub(callable, context, target, UndefinedConstant(),
                                 argc);
  } else {
    DCHECK_EQ(mode, DONT_OVERRIDE);
    DCHECK(allocation_site);
    TNode<Smi> transition_info = LoadTransitionInfo(*allocation_site);

    // Least significant bit in fast array elements kind means holeyness.
    static_assert(PACKED_SMI_ELEMENTS == 0);
    static_assert(HOLEY_SMI_ELEMENTS == 1);
    static_assert(PACKED_ELEMENTS == 2);
    static_assert(HOLEY_ELEMENTS == 3);
    static_assert(PACKED_DOUBLE_ELEMENTS == 4);
    static_assert(HOLEY_DOUBLE_ELEMENTS == 5);

    Label normal_sequence(this);
    TVARIABLE(Int32T, var_elements_kind,
              Signed(DecodeWord32<AllocationSite::ElementsKindBits>(
                  SmiToInt32(transition_info))));
    // Is the low bit set? If so, we are holey and that is good.
    int fast_elements_kind_holey_mask =
        AllocationSite::ElementsKindBits::encode(static_cast<ElementsKind>(1));
    GotoIf(IsSetSmi(transition_info, fast_elements_kind_holey_mask),
           &normal_sequence);
    {
      // Make elements kind holey and update elements kind in the type info.
      var_elements_kind = Word32Or(var_elements_kind.value(), Int32Constant(1));
      StoreObjectFieldNoWriteBarrier(
          *allocation_site, AllocationSite::kTransitionInfoOrBoilerplateOffset,
          SmiOr(transition_info, SmiConstant(fast_elements_kind_holey_mask)));
      Goto(&normal_sequence);
    }
    BIND(&normal_sequence);

    // TODO(ishell): Compute the builtin index dynamically instead of
    // iterating over all expected elements kinds.
    // TODO(ishell): Given that the code above ensures that the elements kind
    // is holey we can skip checking with non-holey elements kinds.
    int last_index =
        GetSequenceIndexFromFastElementsKind(TERMINAL_FAST_ELEMENTS_KIND);
    for (int i = 0; i <= last_index; ++i) {
      Label next(this);
      ElementsKind kind = GetFastElementsKindFromSequenceIndex(i);
      GotoIfNot(Word32Equal(var_elements_kind.value(), Int32Constant(kind)),
                &next);

      Callable callable =
          CodeFactory::ArraySingleArgumentConstructor(isolate(), kind, mode);

      TailCallArrayConstructorStub(callable, context, target, *allocation_site,
                                   argc);

      BIND(&next);
    }

    // If we reached this point there is a problem.
    Abort(AbortReason::kUnexpectedElementsKindInArrayConstructor);
  }
}

void ArrayBuiltinsAssembler::GenerateDispatchToArrayStub(
    TNode<Context> context, TNode<JSFunction> target, TNode<Int32T> argc,
    AllocationSiteOverrideMode mode,
    std::optional<TNode<AllocationSite>> allocation_site) {
  CodeStubArguments args(this, argc);
  Label check_one_case(this), fallthrough(this);
  GotoIfNot(IntPtrEqual(args.GetLengthWithoutReceiver(), IntPtrConstant(0)),
            &check_one_case);
  CreateArrayDispatchNoArgument(context, target, argc, mode, allocation_site);

  BIND(&check_one_case);
  GotoIfNot(IntPtrEqual(args.GetLengthWithoutReceiver(), IntPtrConstant(1)),
            &fallthrough);
  CreateArrayDispatchSingleArgument(context, target, argc, mode,
                                    allocation_site);

  BIND(&fallthrough);
}

TF_BUILTIN(ArrayConstructorImpl, ArrayBuiltinsAssembler) {
  auto target = Parameter<JSFunction>(Descriptor::kTarget);
  auto new_target = Parameter<Object>(Descriptor::kNewTarget);
  auto argc = UncheckedParameter<Int32T>(Descriptor::kActualArgumentsCount);
  auto maybe_allocation_site =
      Parameter<HeapObject>(Descriptor::kAllocationSite);

  // Initial map for the builtin Array functions should be Map.
  CSA_DCHECK(this, IsMap(CAST(LoadObjectField(
                       target, JSFunction::kPrototypeOrInitialMapOffset))));

  // We should either have undefined or a valid AllocationSite
  CSA_DCHECK(this, Word32Or(IsUndefined(maybe_allocation_site),
                            IsAllocationSite(maybe_allocation_site)));

  // "Enter" the context of the Array function.
  TNode<Context> context =
      CAST(LoadObjectField(target, JSFunction::kContextOffset));

  Label runtime(this, Label::kDeferred);
  GotoIf(TaggedNotEqual(target, new_target), &runtime);

  Label no_info(this);
  // If the feedback vector is the undefined value call an array constructor
  // that doesn't use AllocationSites.
  GotoIf(IsUndefined(maybe_allocation_site), &no_info);

  GenerateDispatchToArrayStub(context, target, argc, DONT_OVERRIDE,
                              CAST(maybe_allocation_site));
  Goto(&runtime);

  BIND(&no_info);
  GenerateDispatchToArrayStub(context, target, argc, DISABLE_ALLOCATION_SITES);
  Goto(&runtime);

  BIND(&runtime);
  GenerateArrayNArgumentsConstructor(context, target, new_target, argc,
                                     maybe_allocation_site);
}

void ArrayBuiltinsAssembler::GenerateConstructor(
    TNode<Context> context, TNode<HeapObject> array_function,
    TNode<Map> array_map, TNode<Object> array_size,
    TNode<HeapObject> allocation_site, ElementsKind elements_kind,
    AllocationSiteMode mode) {
  Label ok(this);
  Label smi_size(this);
  Label small_smi_size(this);
  Label call_runtime(this, Label::kDeferred);

  Branch(TaggedIsSmi(array_size), &smi_size, &call_runtime);

  BIND(&smi_size);
  {
    TNode<Smi> array_size_smi = CAST(array_size);

    if (IsFastPackedElementsKind(elements_kind)) {
      Label abort(this, Label::kDeferred);
      Branch(SmiEqual(array_size_smi, SmiConstant(0)), &small_smi_size, &abort);

      BIND(&abort);
      TNode<Smi> reason =
          SmiConstant(AbortReason::kAllocatingNonEmptyPackedArray);
      TailCallRuntime(Runtime::kAbort, context, reason);
    } else {
      Branch(SmiAboveOrEqual(array_size_smi,
                             SmiConstant(JSArray::kInitialMaxFastElementArray)),
             &call_runtime, &small_smi_size);
    }

    BIND(&small_smi_size);
    {
      TNode<JSArray> array = AllocateJSArray(
          elements_kind, array_map, array_size_smi, array_size_smi,
          mode == DONT_TRACK_ALLOCATION_SITE
              ? std::optional<TNode<AllocationSite>>(std::nullopt)
              : CAST(allocation_site));
      Return(array);
    }
  }

  BIND(&call_runtime);
  {
    TailCallRuntimeNewArray(context, array_function, array_size, array_function,
                            allocation_site);
  }
}

void ArrayBuiltinsAssembler::GenerateArrayNoArgumentConstructor(
    ElementsKind kind, AllocationSiteOverrideMode mode) {
  using Descriptor = ArrayNoArgumentConstructorDescriptor;
  TNode<NativeContext> native_context = LoadObjectField<NativeContext>(
      Parameter<HeapObject>(Descriptor::kFunction), JSFunction::kContextOffset);
  bool track_allocation_site =
      AllocationSite::ShouldTrack(kind) && mode != DISABLE_ALLOCATION_SITES;
  std::optional<TNode<AllocationSite>> allocation_site =
      track_allocation_site
          ? Parameter<AllocationSite>(Descriptor::kAllocationSite)
          : std::optional<TNode<AllocationSite>>(std::nullopt);
  TNode<Map> array_map = LoadJSArrayElementsMap(kind, native_context);
  TNode<JSArray> array = AllocateJSArray(
      kind, array_map, IntPtrConstant(JSArray::kPreallocatedArrayElements),
      SmiConstant(0), allocation_site);
  Return(array);
}

void ArrayBuiltinsAssembler::GenerateArraySingleArgumentConstructor(
    ElementsKind kind, AllocationSiteOverrideMode mode) {
  using Descriptor = ArraySingleArgumentConstructorDescriptor;
  auto context = Parameter<Context>(Descriptor::kContext);
  auto function = Parameter<HeapObject>(Descriptor::kFunction);
  TNode<NativeContext> native_context =
      CAST(LoadObjectField(function, JSFunction::kContextOffset));
  TNode<Map> array_map = LoadJSArrayElementsMap(kind, native_context);

  AllocationSiteMode allocation_site_mode = DONT_TRACK_ALLOCATION_SITE;
  if (mode == DONT_OVERRIDE) {
    allocation_site_mode = AllocationSite::ShouldTrack(kind)
                               ? TRACK_ALLOCATION_SITE
                               : DONT_TRACK_ALLOCATION_SITE;
  }

  auto array_size = Parameter<Object>(Descriptor::kArraySizeSmiParameter);
  // allocation_site can be Undefined or an AllocationSite
  auto allocation_site = Parameter<HeapObject>(Descriptor::kAllocationSite);

  GenerateConstructor(context, function, array_map, array_size, allocation_site,
                      kind, allocation_site_mode);
}

void ArrayBuiltinsAssembler::GenerateArrayNArgumentsConstructor(
    TNode<Context> context, TNode<JSFunction> target, TNode<Object> new_target,
    TNode<Int32T> argc, TNode<HeapObject> maybe_allocation_site) {
  // Replace incoming JS receiver argument with the target.
  // TODO(ishell): Avoid replacing the target on the stack and just add it
  // as another additional parameter for Runtime::kNewArray.
  CodeStubArguments args(this, argc);
  args.SetReceiver(target);

  // Adjust arguments count for the runtime call:
  // +2 for new_target and maybe_allocation_site.
  argc = Int32Add(TruncateIntPtrToInt32(args.GetLengthWithReceiver()),
                  Int32Constant(2));
  TailCallRuntime(Runtime::kNewArray, argc, context, new_target,
                  maybe_allocation_site);
}

TF_BUILTIN(ArrayNArgumentsConstructor, ArrayBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto target = Parameter<JSFunction>(Descriptor::kFunction);
  auto argc = UncheckedParameter<Int32T>(Descriptor::kActualArgumentsCount);
  auto maybe_allocation_site =
      Parameter<HeapObject>(Descriptor::kAllocationSite);

  GenerateArrayNArgumentsConstructor(context, target, target, argc,
                                     maybe_allocation_site);
}

#define GENERATE_ARRAY_CTOR(name, kind_camel, kind_caps, mode_camel, \
                            mode_caps)                               \
  TF_BUILTIN(Array##name##Constructor_##kind_camel##_##mode_camel,   \
             ArrayBuiltinsAssembler) {                               \
    GenerateArray##name##Constructor(kind_caps, mode_caps);          \
  }

// The ArrayNoArgumentConstructor builtin family.
GENERATE_ARRAY_CTOR(NoArgument, PackedSmi, PACKED_SMI_ELEMENTS, DontOverride,
                    DONT_OVERRIDE)
GENERATE_ARRAY_CTOR(NoArgument, HoleySmi, HOLEY_SMI_ELEMENTS, DontOverride,
                    DONT_OVERRIDE)
GENERATE_ARRAY_CTOR(NoArgument, PackedSmi, PACKED_SMI_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(NoArgument, HoleySmi, HOLEY_SMI_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(NoArgument, Packed, PACKED_ELEMENTS, DisableAllocationSites,
                    DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(NoArgument, Holey, HOLEY_ELEMENTS, DisableAllocationSites,
                    DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(NoArgument, PackedDouble, PACKED_DOUBLE_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(NoArgument, HoleyDouble, HOLEY_DOUBLE_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)

// The ArraySingleArgumentConstructor builtin family.
GENERATE_ARRAY_CTOR(SingleArgument, PackedSmi, PACKED_SMI_ELEMENTS,
                    DontOverride, DONT_OVERRIDE)
GENERATE_ARRAY_CTOR(SingleArgument, HoleySmi, HOLEY_SMI_ELEMENTS, DontOverride,
                    DONT_OVERRIDE)
GENERATE_ARRAY_CTOR(SingleArgument, PackedSmi, PACKED_SMI_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(SingleArgument, HoleySmi, HOLEY_SMI_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(SingleArgument, Packed, PACKED_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(SingleArgument, Holey, HOLEY_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(SingleArgument, PackedDouble, PACKED_DOUBLE_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)
GENERATE_ARRAY_CTOR(SingleArgument, HoleyDouble, HOLEY_DOUBLE_ELEMENTS,
                    DisableAllocationSites, DISABLE_ALLOCATION_SITES)

#undef GENERATE_ARRAY_CTOR

class SlowBoilerplateCloneAssembler : public CodeStubAssembler {
 public:
  explicit SlowBoilerplateCloneAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  // If `item` is an object or an array, deep-clone it and jump to `cloned`.
  void CloneIfObjectOrArray(TNode<Object> item, TVariable<Object>& clone,
                            TVariable<Object>& current_allocation_site,
                            TNode<Context> context, Label* cloned,
                            Label* not_cloned, Label* bailout) {
    Label is_object(this, &current_allocation_site),
        is_array(this, &current_allocation_site);

    GotoIf(TaggedIsSmi(item), not_cloned);
    GotoIf(IsJSArray(CAST(item)), &is_array);
    GotoIf(IsJSObject(CAST(item)), &is_object);
    Goto(not_cloned);

    BIND(&is_array);
    {
      // Consume the next AllocationSite. All objects inside this array, as well
      // as all sibling objects (until a new array is encountered) will use this
      // AllocationSite. E.g., in [1, 2, {a: 3}, [4, 5], {b: 6}], the object {a:
      // 3} uses the topmost AllocationSite, and the object {b: 6} uses the
      // AllocationSite of [4, 5].
      if (V8_ALLOCATION_SITE_TRACKING_BOOL) {
        current_allocation_site =
            LoadNestedAllocationSite(CAST(current_allocation_site.value()));

        // Ensure we're consuming the AllocationSites in the correct order.
        CSA_DCHECK(
            this,
            TaggedEqual(LoadBoilerplate(CAST(current_allocation_site.value())),
                        item));
      }

      auto clone_and_next_allocation_site = CallBuiltin<PairT<Object, Object>>(
          Builtin::kCreateArrayFromSlowBoilerplateHelper, context,
          current_allocation_site.value(), item);

      clone = Projection<0>(clone_and_next_allocation_site);
      GotoIf(IsUndefined(clone.value()), bailout);
      current_allocation_site = Projection<1>(clone_and_next_allocation_site);
      Goto(cloned);
    }

    BIND(&is_object);
    {
      auto clone_and_next_allocation_site = CallBuiltin<PairT<Object, Object>>(
          Builtin::kCreateObjectFromSlowBoilerplateHelper, context,
          current_allocation_site.value(), item);
      clone = Projection<0>(clone_and_next_allocation_site);
      GotoIf(IsUndefined(clone.value()), bailout);
      current_allocation_site = Projection<1>(clone_and_next_allocation_site);
      Goto(cloned);
    }
  }

  void CloneElementsOfFixedArray(TNode<FixedArrayBase> elements,
                                 TNode<Smi> length, TNode<Int32T> elements_kind,
                                 TVariable<Object>& current_allocation_site,
                                 TNode<Context> context, Label* done,
                                 Label* bailout) {
    CSA_DCHECK(this, SmiNotEqual(length, SmiConstant(0)));

    auto loop_body = [&](TNode<IntPtrT> index) {
      TVARIABLE(Object, clone);
      Label cloned(this, &clone),
          done_with_element(this, &current_allocation_site);

      TNode<Object> element = LoadFixedArrayElement(CAST(elements), index);
      CloneIfObjectOrArray(element, clone, current_allocation_site, context,
                           &cloned, &done_with_element, bailout);

      BIND(&cloned);
      {
        StoreFixedArrayElement(CAST(elements), index, clone.value());
        Goto(&done_with_element);
      }

      BIND(&done_with_element);
    };
    VariableList loop_vars({&current_allocation_site}, zone());
    BuildFastLoop<IntPtrT>(loop_vars, IntPtrConstant(0),
                           PositiveSmiUntag(length), loop_body, 1,
                           LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
    Goto(done);
  }
};

TF_BUILTIN(CreateArrayFromSlowBoilerplate, SlowBoilerplateCloneAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto feedback_vector = Parameter<FeedbackVector>(Descriptor::kFeedbackVector);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);

  Label call_runtime(this);

  TNode<Object> maybe_allocation_site =
      CAST(LoadFeedbackVectorSlot(feedback_vector, slot));
  GotoIfNot(HasBoilerplate(maybe_allocation_site), &call_runtime);

  TNode<AllocationSite> allocation_site = CAST(maybe_allocation_site);
  TNode<JSArray> boilerplate = CAST(LoadBoilerplate(allocation_site));

  {
    auto clone_and_next_allocation_site = CallBuiltin<PairT<Object, Object>>(
        Builtin::kCreateArrayFromSlowBoilerplateHelper, context,
        allocation_site, boilerplate);
    TNode<Object> result = Projection<0>(clone_and_next_allocation_site);

    GotoIf(IsUndefined(result), &call_runtime);
    Return(result);
  }

  BIND(&call_runtime);
  {
    auto boilerplate_descriptor = Parameter<ArrayBoilerplateDescription>(
        Descriptor::kBoilerplateDescriptor);
    auto flags = Parameter<Smi>(Descriptor::kFlags);
    TNode<Object> result =
        CallRuntime(Runtime::kCreateArrayLiteral, context, feedback_vector,
                    slot, boilerplate_descriptor, flags);
    Return(result);
  }
}

TF_BUILTIN(CreateObjectFromSlowBoilerplate, SlowBoilerplateCloneAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto feedback_vector = Parameter<FeedbackVector>(Descriptor::kFeedbackVector);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);

  Label call_runtime(this);

  TNode<Object> maybe_allocation_site =
      CAST(LoadFeedbackVectorSlot(feedback_vector, slot));
  GotoIfNot(HasBoilerplate(maybe_allocation_site), &call_runtime);

  TNode<AllocationSite> allocation_site = CAST(maybe_allocation_site);
  TNode<JSObject> boilerplate = LoadBoilerplate(allocation_site);

  {
    auto clone_and_next_allocation_site = CallBuiltin<PairT<Object, Object>>(
        Builtin::kCreateObjectFromSlowBoilerplateHelper, context,
        allocation_site, boilerplate);
    TNode<Object> result = Projection<0>(clone_and_next_allocation_site);

    GotoIf(IsUndefined(result), &call_runtime);
    Return(result);
  }

  BIND(&call_runtime);
  {
    auto boilerplate_descriptor = Parameter<ObjectBoilerplateDescription>(
        Descriptor::kBoilerplateDescriptor);
    auto flags = Parameter<Smi>(Descriptor::kFlags);
    TNode<Object> result =
        CallRuntime(Runtime::kCreateObjectLiteral, context, feedback_vector,
                    slot, boilerplate_descriptor, flags);
    Return(result);
  }
}

TF_BUILTIN(CreateArrayFromSlowBoilerplateHelper,
           SlowBoilerplateCloneAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto allocation_site = Parameter<AllocationSite>(Descriptor::kAllocationSite);
  auto boilerplate = Parameter<JSArray>(Descriptor::kBoilerplate);

  PerformStackCheck(context);

  TNode<FixedArrayBase> boilerplate_elements = LoadElements(boilerplate);
  TNode<Smi> length = LoadFixedArrayBaseLength(boilerplate_elements);

  // If the array contains other arrays (either directly or inside objects),
  // the AllocationSite tree is stored as a list (AllocationSite::nested_site)
  // in pre-order. See AllocationSiteUsageContext.
  TVARIABLE(Object, current_allocation_site);
  current_allocation_site = allocation_site;

  Label done(this, &current_allocation_site),
      bailout(this, &current_allocation_site, Label::kDeferred);

  // Keep in sync with ArrayLiteralBoilerplateBuilder::IsFastCloningSupported.
  // TODO(42204675): Detect this in advance when constructing the boilerplate.
  GotoIf(
      SmiAboveOrEqual(
          length,
          SmiConstant(ConstructorBuiltins::kMaximumClonedShallowArrayElements)),
      &bailout);

  // First clone the array as if was a simple, shallow array:
  TNode<JSArray> array;
  if (V8_ALLOCATION_SITE_TRACKING_BOOL) {
    array = CloneFastJSArray(context, boilerplate, allocation_site);
  } else {
    array = CloneFastJSArray(context, boilerplate);
  }

  // Then fix up each element by cloning it (if it's an object or an array).
  TNode<FixedArrayBase> elements = LoadElements(array);

  // If the boilerplate array is COW, it won't contain objects or arrays.
  GotoIf(TaggedEqual(LoadMap(elements), FixedCOWArrayMapConstant()), &done);

  // If the elements kind is not between PACKED_ELEMENTS and HOLEY_ELEMENTS, it
  // cannot contain objects or arrays.
  TNode<Int32T> elements_kind = LoadElementsKind(boilerplate);
  GotoIf(Uint32GreaterThan(
             Unsigned(Int32Sub(elements_kind, Int32Constant(PACKED_ELEMENTS))),
             Uint32Constant(HOLEY_ELEMENTS - PACKED_ELEMENTS)),
         &done);

  GotoIf(SmiEqual(length, SmiConstant(0)), &done);
  CloneElementsOfFixedArray(elements, length, elements_kind,
                            current_allocation_site, context, &done, &bailout);
  BIND(&done);
  { Return(array, current_allocation_site.value()); }

  BIND(&bailout);
  { Return(UndefinedConstant(), UndefinedConstant()); }
}

TF_BUILTIN(CreateObjectFromSlowBoilerplateHelper,
           SlowBoilerplateCloneAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto allocation_site = Parameter<AllocationSite>(Descriptor::kAllocationSite);
  auto boilerplate = Parameter<JSObject>(Descriptor::kBoilerplate);

  PerformStackCheck(context);

  TVARIABLE(Object, current_allocation_site);
  current_allocation_site = allocation_site;

  Label bailout(this, &current_allocation_site);

  // Keep in sync with ObjectLiteralBoilerplateBuilder::IsFastCloningSupported.
  // The property count needs to be below
  // ConstructorBuiltins::kMaximumClonedShallowObjectProperties.
  // CreateShallowObjectLiteral already bails out if all properties don't fit
  // in-object, so we don't need to check the property count here.
  // TODO(42204675): Detect this in advance when constructing the boilerplate.
  TNode<Int32T> elements_kind = LoadElementsKind(boilerplate);
  GotoIf(
      Int32GreaterThan(elements_kind, Int32Constant(LAST_FAST_ELEMENTS_KIND)),
      &bailout);

  constexpr bool kBailoutIfDictionaryPropertiesTrue = true;
  ConstructorBuiltinsAssembler constructor_assembler(state());
  TNode<JSObject> object =
      CAST(constructor_assembler.CreateShallowObjectLiteral(
          allocation_site, boilerplate, &bailout,
          kBailoutIfDictionaryPropertiesTrue));

  // Fix up the object properties and elements and consume the correct amount of
  // AllocationSites. To iterate the AllocationSites in the correct order, we
  // need to first iterate the in-object properties and then the elements.

  // Assert that there aren't any out of object properties (if there are, we
  // must have bailed out already):
  CSA_DCHECK(this, IsEmptyFixedArray(LoadFastProperties(boilerplate)));

  // In-object properties:
  {
    auto loop_body = [&](TNode<IntPtrT> offset) {
      TVARIABLE(Object, clone);
      Label cloned(this, &clone),
          done_with_field(this, &current_allocation_site);

      TNode<Object> field = LoadObjectField(object, offset);
      CloneIfObjectOrArray(field, clone, current_allocation_site, context,
                           &cloned, &done_with_field, &bailout);

      BIND(&cloned);
      {
        StoreObjectField(object, offset, clone.value());
        Goto(&done_with_field);
      }

      BIND(&done_with_field);
    };

    TNode<Map> boilerplate_map = LoadMap(boilerplate);
    TNode<IntPtrT> instance_size =
        TimesTaggedSize(LoadMapInstanceSizeInWords(boilerplate_map));
    VariableList loop_vars({&current_allocation_site}, zone());
    BuildFastLoop<IntPtrT>(loop_vars, IntPtrConstant(JSObject::kHeaderSize),
                           instance_size, loop_body, kTaggedSize,
                           LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
  }

  // Elements:
  {
    Label done_with_elements(this);
    TNode<FixedArrayBase> elements = LoadElements(object);
    GotoIf(IsEmptyFixedArray(elements), &done_with_elements);

    TNode<Int32T> elements_kind = LoadElementsKind(object);
    // Object elements are never COW and never SMI_ELEMENTS etc.
    CloneElementsOfFixedArray(elements, LoadFixedArrayBaseLength(elements),
                              elements_kind, current_allocation_site, context,
                              &done_with_elements, &bailout);
    BIND(&done_with_elements);
  }

  Return(object, current_allocation_site.value());

  BIND(&bailout);
  {
    // We can't solve this case by calling into Runtime_CreateObjectLiteral,
    // since it's currently not suitable for creating a nested objects (e.g.,
    // doesn't return the next AllocationSite).
    Return(UndefinedConstant(), UndefinedConstant());
  }
}

}  // namespace internal
}  // namespace v8
