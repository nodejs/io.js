// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-iterator-gen.h"

#include "src/builtins/builtins-collections-gen.h"
#include "src/builtins/builtins-string-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/builtins/growable-fixed-array-gen.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/compiler/code-assembler.h"
#include "src/heap/factory-inl.h"

namespace v8 {
namespace internal {

using IteratorRecord = TorqueStructIteratorRecord;

TNode<Object> IteratorBuiltinsAssembler::GetIteratorMethod(
    TNode<Context> context, TNode<Object> object) {
  return GetProperty(context, object, factory()->iterator_symbol());
}

IteratorRecord IteratorBuiltinsAssembler::GetIterator(TNode<Context> context,
                                                      TNode<Object> object) {
  TNode<Object> method = GetIteratorMethod(context, object);
  return GetIterator(context, object, method);
}

IteratorRecord IteratorBuiltinsAssembler::GetIterator(TNode<Context> context,
                                                      TNode<Object> object,
                                                      TNode<Object> method) {
  Label if_not_callable(this, Label::kDeferred), if_callable(this);
  GotoIf(TaggedIsSmi(method), &if_not_callable);
  Branch(IsCallable(CAST(method)), &if_callable, &if_not_callable);

  BIND(&if_not_callable);
  CallRuntime(Runtime::kThrowIteratorError, context, object);
  Unreachable();

  BIND(&if_callable);
  {
    TNode<Object> iterator = Call(context, method, object);

    Label get_next(this), if_notobject(this, Label::kDeferred);
    GotoIf(TaggedIsSmi(iterator), &if_notobject);
    Branch(JSAnyIsNotPrimitive(CAST(iterator)), &get_next, &if_notobject);

    BIND(&if_notobject);
    CallRuntime(Runtime::kThrowSymbolIteratorInvalid, context);
    Unreachable();

    BIND(&get_next);
    TNode<Object> next =
        GetProperty(context, iterator, factory()->next_string());
    return IteratorRecord{TNode<JSReceiver>::UncheckedCast(iterator), next};
  }
}

TNode<JSReceiver> IteratorBuiltinsAssembler::IteratorStep(
    TNode<Context> context, const IteratorRecord& iterator, Label* if_done,
    base::Optional<TNode<Map>> fast_iterator_result_map) {
  DCHECK_NOT_NULL(if_done);
  // 1. a. Let result be ? Invoke(iterator, "next", « »).
  TNode<Object> result = Call(context, iterator.next, iterator.object);

  // 3. If Type(result) is not Object, throw a TypeError exception.
  Label if_notobject(this, Label::kDeferred), return_result(this);
  GotoIf(TaggedIsSmi(result), &if_notobject);
  TNode<HeapObject> heap_object_result = CAST(result);
  TNode<Map> result_map = LoadMap(heap_object_result);
  GotoIfNot(JSAnyIsNotPrimitiveMap(result_map), &if_notobject);

  // IteratorComplete
  // 2. Return ToBoolean(? Get(iterResult, "done")).
  IteratorComplete(context, heap_object_result, if_done,
                   fast_iterator_result_map);
  Goto(&return_result);

  BIND(&if_notobject);
  CallRuntime(Runtime::kThrowIteratorResultNotAnObject, context, result);
  Unreachable();

  BIND(&return_result);
  return CAST(heap_object_result);
}

void IteratorBuiltinsAssembler::IteratorComplete(
    TNode<Context> context, const TNode<HeapObject> iterator, Label* if_done,
    base::Optional<TNode<Map>> fast_iterator_result_map) {
  DCHECK_NOT_NULL(if_done);

  Label return_result(this);

  TNode<Map> result_map = LoadMap(iterator);

  if (fast_iterator_result_map) {
    // Fast iterator result case:
    Label if_generic(this);

    // 4. Return result.
    GotoIfNot(TaggedEqual(result_map, *fast_iterator_result_map), &if_generic);

    // 2. Return ToBoolean(? Get(iterResult, "done")).
    TNode<Object> done =
        LoadObjectField(iterator, JSIteratorResult::kDoneOffset);
    BranchIfToBooleanIsTrue(done, if_done, &return_result);

    BIND(&if_generic);
  }

  // Generic iterator result case:
  {
    // 2. Return ToBoolean(? Get(iterResult, "done")).
    TNode<Object> done =
        GetProperty(context, iterator, factory()->done_string());
    BranchIfToBooleanIsTrue(done, if_done, &return_result);
  }

  BIND(&return_result);
}

TNode<Object> IteratorBuiltinsAssembler::IteratorValue(
    TNode<Context> context, TNode<JSReceiver> result,
    base::Optional<TNode<Map>> fast_iterator_result_map) {
  Label exit(this);
  TVARIABLE(Object, var_value);
  if (fast_iterator_result_map) {
    // Fast iterator result case:
    Label if_generic(this);
    TNode<Map> map = LoadMap(result);
    GotoIfNot(TaggedEqual(map, *fast_iterator_result_map), &if_generic);
    var_value = LoadObjectField(result, JSIteratorResult::kValueOffset);
    Goto(&exit);

    BIND(&if_generic);
  }

  // Generic iterator result case:
  var_value = GetProperty(context, result, factory()->value_string());
  Goto(&exit);

  BIND(&exit);
  return var_value.value();
}

void IteratorBuiltinsAssembler::Iterate(
    TNode<Context> context, TNode<Object> iterable,
    std::function<void(TNode<Object>)> func,
    std::initializer_list<compiler::CodeAssemblerVariable*> merged_variables) {
  Iterate(context, iterable, GetIteratorMethod(context, iterable), func,
          merged_variables);
}

void IteratorBuiltinsAssembler::Iterate(
    TNode<Context> context, TNode<Object> iterable, TNode<Object> iterable_fn,
    std::function<void(TNode<Object>)> func,
    std::initializer_list<compiler::CodeAssemblerVariable*> merged_variables) {
  Label done(this);

  IteratorRecord iterator_record = GetIterator(context, iterable, iterable_fn);

  Label if_exception(this, Label::kDeferred);
  TVARIABLE(Object, var_exception);

  Label loop_start(this, merged_variables);
  Goto(&loop_start);

  BIND(&loop_start);
  {
    TNode<JSReceiver> next = IteratorStep(context, iterator_record, &done);
    TNode<Object> next_value = IteratorValue(context, next);

    {
      compiler::ScopedExceptionHandler handler(this, &if_exception,
                                               &var_exception);
      func(next_value);
    }

    Goto(&loop_start);
  }

  BIND(&if_exception);
  {
    TNode<HeapObject> message = GetPendingMessage();
    SetPendingMessage(TheHoleConstant());
    IteratorCloseOnException(context, iterator_record);
    CallRuntime(Runtime::kReThrowWithMessage, context, var_exception.value(),
                message);
    Unreachable();
  }

  BIND(&done);
}

TNode<JSArray> IteratorBuiltinsAssembler::IterableToList(
    TNode<Context> context, TNode<Object> iterable, TNode<Object> iterator_fn) {
  GrowableFixedArray values(state());
  FillFixedArrayFromIterable(context, iterable, iterator_fn, &values);
  return values.ToJSArray(context);
}

TNode<FixedArray> IteratorBuiltinsAssembler::IterableToFixedArray(
    TNode<Context> context, TNode<Object> iterable, TNode<Object> iterator_fn) {
  GrowableFixedArray values(state());
  FillFixedArrayFromIterable(context, iterable, iterator_fn, &values);
  TNode<FixedArray> new_array = values.ToFixedArray();
  return new_array;
}

void IteratorBuiltinsAssembler::FillFixedArrayFromIterable(
    TNode<Context> context, TNode<Object> iterable, TNode<Object> iterator_fn,
    GrowableFixedArray* values) {
  // 1. Let iteratorRecord be ? GetIterator(items, method) (handled by Iterate).

  // 2. Let values be a new empty List.

  // The GrowableFixedArray has already been created. It's ok if we do this step
  // out of order, since creating an empty List is not observable.

  // 3. Let next be true. (handled by Iterate)
  // 4. Repeat, while next is not false (handled by Iterate)
  Iterate(context, iterable, iterator_fn,
          [&values](TNode<Object> value) {
            // Handled by Iterate:
            //  a. Set next to ? IteratorStep(iteratorRecord).
            //  b. If next is not false, then
            //   i. Let nextValue be ? IteratorValue(next).

            //   ii. Append nextValue to the end of the List values.
            values->Push(value);
          },
          {values->var_array(), values->var_capacity(), values->var_length()});
}

TF_BUILTIN(IterableToList, IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);
  auto iterator_fn = Parameter<Object>(Descriptor::kIteratorFn);

  Return(IterableToList(context, iterable, iterator_fn));
}

TF_BUILTIN(IterableToFixedArray, IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);
  auto iterator_fn = Parameter<Object>(Descriptor::kIteratorFn);

  Return(IterableToFixedArray(context, iterable, iterator_fn));
}

#if V8_ENABLE_WEBASSEMBLY
TF_BUILTIN(IterableToFixedArrayForWasm, IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);
  auto expected_length = Parameter<Smi>(Descriptor::kExpectedLength);

  TNode<Object> iterator_fn = GetIteratorMethod(context, iterable);
  GrowableFixedArray values(state());

  Label done(this);

  FillFixedArrayFromIterable(context, iterable, iterator_fn, &values);

  GotoIf(WordEqual(PositiveSmiUntag(expected_length),
                   values.var_length()->value()),
         &done);
  Return(CallRuntime(
      Runtime::kThrowTypeError, context,
      SmiConstant(MessageTemplate::kWasmTrapMultiReturnLengthMismatch)));

  BIND(&done);
  Return(values.var_array()->value());
}
#endif  // V8_ENABLE_WEBASSEMBLY

TNode<FixedArray> IteratorBuiltinsAssembler::StringListFromIterable(
    TNode<Context> context, TNode<Object> iterable) {
  Label done(this);
  GrowableFixedArray list(state());
  // 1. If iterable is undefined, then
  //   a. Return a new empty List.
  GotoIf(IsUndefined(iterable), &done);

  // 2. Let iteratorRecord be ? GetIterator(items) (handled by Iterate).

  // 3. Let list be a new empty List.

  // 4. Let next be true (handled by Iterate).
  // 5. Repeat, while next is not false (handled by Iterate).
  Iterate(
      context, iterable,
      [&](TNode<Object> next_value) {
        // Handled by Iterate:
        //  a. Set next to ? IteratorStep(iteratorRecord).
        //  b. If next is not false, then
        //   i. Let nextValue be ? IteratorValue(next).

        //   ii. If Type(nextValue) is not String, then
        Label if_isnotstringtype(this, Label::kDeferred), loop_body_end(this);
        GotoIf(TaggedIsSmi(next_value), &if_isnotstringtype);
        TNode<Uint16T> next_value_type = LoadInstanceType(CAST(next_value));
        GotoIfNot(IsStringInstanceType(next_value_type), &if_isnotstringtype);

        //   iii. Append nextValue to the end of the List list.
        list.Push(next_value);

        Goto(&loop_body_end);

        // 5.b.ii
        BIND(&if_isnotstringtype);
        {
          // 1. Let error be ThrowCompletion(a newly created TypeError object).

          CallRuntime(Runtime::kThrowTypeError, context,
                      SmiConstant(MessageTemplate::kIterableYieldedNonString),
                      next_value);
          // 2. Return ? IteratorClose(iteratorRecord, error). (handled by
          // Iterate).
          Unreachable();
        }

        BIND(&loop_body_end);
      },
      {list.var_array(), list.var_length(), list.var_capacity()});
  Goto(&done);

  BIND(&done);
  // 6. Return list.
  return list.ToFixedArray();
}

TF_BUILTIN(StringListFromIterable, IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);

  Return(StringListFromIterable(context, iterable));
}

TF_BUILTIN(StringFixedArrayFromIterable, IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);

  Return(StringListFromIterable(context, iterable));
}

// This builtin always returns a new JSArray and is thus safe to use even in the
// presence of code that may call back into user-JS. This builtin will take the
// fast path if the iterable is a fast array and the Array prototype and the
// Symbol.iterator is untouched. The fast path skips the iterator and copies the
// backing store to the new array. Note that if the array has holes, the holes
// will be copied to the new array, which is inconsistent with the behavior of
// an actual iteration, where holes should be replaced with undefined (if the
// prototype has no elements). To maintain the correct behavior for holey
// arrays, use the builtins IterableToList or IterableToListWithSymbolLookup or
// IterableToListConvertHoles.
TF_BUILTIN(IterableToListMayPreserveHoles, IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);
  auto iterator_fn = Parameter<Object>(Descriptor::kIteratorFn);

  Label slow_path(this);

  GotoIfNot(IsFastJSArrayWithNoCustomIteration(context, iterable), &slow_path);

  // The fast path will copy holes to the new array.
  TailCallBuiltin(Builtin::kCloneFastJSArray, context, iterable);

  BIND(&slow_path);
  TailCallBuiltin(Builtin::kIterableToList, context, iterable, iterator_fn);
}

// This builtin always returns a new JSArray and is thus safe to use even in the
// presence of code that may call back into user-JS. This builtin will take the
// fast path if the iterable is a fast array and the Array prototype and the
// Symbol.iterator is untouched. The fast path skips the iterator and copies the
// backing store to the new array. Note that if the array has holes, the holes
// will be converted to undefined values in the new array (unlike
// IterableToListMayPreserveHoles builtin).
TF_BUILTIN(IterableToListConvertHoles, IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);
  auto iterator_fn = Parameter<Object>(Descriptor::kIteratorFn);

  Label slow_path(this);

  GotoIfNot(IsFastJSArrayWithNoCustomIteration(context, iterable), &slow_path);

  // The fast path will convert holes to undefined values in the new array.
  TailCallBuiltin(Builtin::kCloneFastJSArrayFillingHoles, context, iterable);

  BIND(&slow_path);
  TailCallBuiltin(Builtin::kIterableToList, context, iterable, iterator_fn);
}

void IteratorBuiltinsAssembler::FastIterableToList(
    TNode<Context> context, TNode<Object> iterable,
    TVariable<JSArray>* var_result, Label* slow) {
  Label done(this), check_string(this), check_map(this), check_set(this);

  // Always call the `next()` builtins when the debugger is
  // active, to ensure we capture side-effects correctly.
  GotoIf(IsDebugActive(), slow);

  GotoIfNot(
      Word32Or(IsFastJSArrayWithNoCustomIteration(context, iterable),
               IsFastJSArrayForReadWithNoCustomIteration(context, iterable)),
      &check_string);

  // Fast path for fast JSArray.
  *var_result = CAST(
      CallBuiltin(Builtin::kCloneFastJSArrayFillingHoles, context, iterable));
  Goto(&done);

  BIND(&check_string);
  {
    Label string_maybe_fast_call(this);
    StringBuiltinsAssembler string_assembler(state());
    string_assembler.BranchIfStringPrimitiveWithNoCustomIteration(
        iterable, context, &string_maybe_fast_call, &check_map);

    BIND(&string_maybe_fast_call);
    const TNode<Uint32T> length = LoadStringLengthAsWord32(CAST(iterable));
    // Use string length as conservative approximation of number of codepoints.
    GotoIf(
        Uint32GreaterThan(length, Uint32Constant(JSArray::kMaxFastArrayLength)),
        slow);
    *var_result = CAST(CallBuiltin(Builtin::kStringToList, context, iterable));
    Goto(&done);
  }

  BIND(&check_map);
  {
    Label map_fast_call(this);
    BranchIfIterableWithOriginalKeyOrValueMapIterator(
        state(), iterable, context, &map_fast_call, &check_set);

    BIND(&map_fast_call);
    *var_result =
        CAST(CallBuiltin(Builtin::kMapIteratorToList, context, iterable));
    Goto(&done);
  }

  BIND(&check_set);
  {
    Label set_fast_call(this);
    BranchIfIterableWithOriginalValueSetIterator(state(), iterable, context,
                                                 &set_fast_call, slow);

    BIND(&set_fast_call);
    *var_result =
        CAST(CallBuiltin(Builtin::kSetOrSetIteratorToList, context, iterable));
    Goto(&done);
  }

  BIND(&done);
}

TNode<JSArray> IteratorBuiltinsAssembler::FastIterableToList(
    TNode<Context> context, TNode<Object> iterable, Label* slow) {
  TVARIABLE(JSArray, var_fast_result);
  FastIterableToList(context, iterable, &var_fast_result, slow);
  return var_fast_result.value();
}

// This builtin loads the property Symbol.iterator as the iterator, and has fast
// paths for fast arrays, for primitive strings, for sets and set iterators, and
// for map iterators. These fast paths will only be taken if Symbol.iterator and
// the Iterator prototype are not modified in a way that changes the original
// iteration behavior.
// * In case of fast holey arrays, holes will be converted to undefined to
//   reflect iteration semantics. Note that replacement by undefined is only
//   correct when the NoElements protector is valid.
// * In case of map/set iterators, there is an additional requirement that the
//   iterator is not partially consumed. To be spec-compliant, after spreading
//   the iterator is set to be exhausted.
TF_BUILTIN(IterableToListWithSymbolLookup, IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);

  Label slow_path(this);

  GotoIfForceSlowPath(&slow_path);

  TVARIABLE(JSArray, var_result);
  FastIterableToList(context, iterable, &var_result, &slow_path);
  Return(var_result.value());

  BIND(&slow_path);
  {
    TNode<Object> iterator_fn = GetIteratorMethod(context, iterable);
    TailCallBuiltin(Builtin::kIterableToList, context, iterable, iterator_fn);
  }
}

TF_BUILTIN(GetIteratorWithFeedbackLazyDeoptContinuation,
           IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  // TODO(v8:10047): Use TaggedIndex here once TurboFan supports it.
  auto call_slot_smi = Parameter<Smi>(Descriptor::kCallSlot);
  auto feedback = Parameter<FeedbackVector>(Descriptor::kFeedback);
  auto iterator_method = Parameter<Object>(Descriptor::kResult);

  // Note, that the builtin also expects the call_slot as a Smi.
  TNode<Object> result =
      CallBuiltin(Builtin::kCallIteratorWithFeedback, context, receiver,
                  iterator_method, call_slot_smi, feedback);
  Return(result);
}

TF_BUILTIN(CallIteratorWithFeedbackLazyDeoptContinuation,
           IteratorBuiltinsAssembler) {
  TNode<Context> context = Parameter<Context>(Descriptor::kContext);
  TNode<Object> iterator = Parameter<Object>(Descriptor::kArgument);

  ThrowIfNotJSReceiver(context, iterator,
                       MessageTemplate::kSymbolIteratorInvalid, "");
  Return(iterator);
}

// This builtin creates a FixedArray based on an Iterable and doesn't have a
// fast path for anything.
TF_BUILTIN(IterableToFixedArrayWithSymbolLookupSlow,
           IteratorBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);

  TNode<Object> iterator_fn = GetIteratorMethod(context, iterable);
  TailCallBuiltin(Builtin::kIterableToFixedArray, context, iterable,
                  iterator_fn);
}

}  // namespace internal
}  // namespace v8
