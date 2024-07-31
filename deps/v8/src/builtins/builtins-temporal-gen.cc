// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-iterator-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/growable-fixed-array-gen.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/objects/js-temporal-objects-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class TemporalBuiltinsAssembler : public IteratorBuiltinsAssembler {
 public:
  explicit TemporalBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : IteratorBuiltinsAssembler(state) {}

  // Step 3 and later of #sec-temporal.calendar.prototype.fields
  TNode<JSArray> CalendarFieldsArrayFromIterable(
      TNode<Context> context, TNode<JSTemporalCalendar> calendar,
      TNode<Object> iterable);

  // For the use inside Temporal GetPossibleInstantFor
  TNode<FixedArray> TemporalInstantFixedArrayFromIterable(
      TNode<Context> context, TNode<Object> iterable);
};

// Step 3 and later of
// #sec-temporal.calendar.prototype.fields
TNode<JSArray> TemporalBuiltinsAssembler::CalendarFieldsArrayFromIterable(
    TNode<Context> context, TNode<JSTemporalCalendar> calendar,
    TNode<Object> iterable) {
  Label done(this), add_fields(this, Label::kDeferred);
  // 4. Let iteratorRecord be ? GetIterator(items).

  // 5. Let fieldNames be a new empty List.
  GrowableFixedArray field_names(state());

  // 6. Repeat, while next is not false,
  Iterate(
      context, iterable,
      [&](TNode<Object> next_value) {
        // Handled by Iterate:
        //  a. Set next to ? IteratorStep(iteratorRecord).
        //  b. If next is not false, then
        //   i. Let nextValue be ? IteratorValue(next).

        //   ii. If Type(nextValue) is not String, then
        Label if_isnotstringtype(this, Label::kDeferred),
            if_rangeerror(this, Label::kDeferred), loop_body_end(this);
        GotoIf(TaggedIsSmi(next_value), &if_isnotstringtype);
        TNode<Uint16T> next_value_type = LoadInstanceType(CAST(next_value));
        GotoIfNot(IsStringInstanceType(next_value_type), &if_isnotstringtype);

        // Step iii and iv see IsInvalidTemporalCalendarField
        // TODO(ftang) Optimize this and remove the runtime call by keeping a
        // bitfield of "fields seen so far" and doing the string comparisons +
        // bitfield access directly here.
        GotoIf(IsTrue(CallRuntime(Runtime::kIsInvalidTemporalCalendarField,
                                  context, next_value,
                                  field_names.ToFixedArray())),
               &if_rangeerror);

        //   v. Append nextValue to the end of the List fieldNames.
        field_names.Push(next_value);

        Goto(&loop_body_end);

        // 6.b.ii
        BIND(&if_isnotstringtype);
        {
          // 1. Let completion be ThrowCompletion(a newly created TypeError
          // object).

          CallRuntime(Runtime::kThrowTypeError, context,
                      SmiConstant(MessageTemplate::kIterableYieldedNonString),
                      next_value);
          // 2. Return ? IteratorClose(iteratorRecord, completion). (handled by
          // Iterate).
          Unreachable();
        }

        // 6.b.ii
        BIND(&if_rangeerror);
        {
          // 1. Let completion be ThrowCompletion(a newly created RangeError
          // object).

          CallRuntime(Runtime::kThrowRangeError, context,
                      SmiConstant(MessageTemplate::kInvalidTimeValue),
                      next_value);
          // 2. Return ? IteratorClose(iteratorRecord, completion). (handled by
          // Iterate).
          Unreachable();
        }
        BIND(&loop_body_end);
      },
      {field_names.var_array(), field_names.var_length(),
       field_names.var_capacity()});
  {
    // Step 7 and 8 of
    // of #sup-temporal.calendar.prototype.fields.
    // Notice this spec text is in the Chapter 15 of the #sup part not #sec
    // part.
    // 7. If calendar.[[Identifier]] is "iso8601", then
    const TNode<Int32T> flags = LoadAndUntagToWord32ObjectField(
        calendar, JSTemporalCalendar::kFlagsOffset);
    // calendar is "iso8601" while the index of calendar is 0
    const TNode<IntPtrT> index = Signed(
        DecodeWordFromWord32<JSTemporalCalendar::CalendarIndexBits>(flags));
    Branch(IntPtrEqual(index, IntPtrConstant(0)), &done, &add_fields);
    BIND(&add_fields);
    {
      // Step 8.a. Let result be the result of implementation-defined processing
      // of fieldNames and calendar.[[Identifier]]. We just always add "era" and
      // "eraYear" for other calendar.

      TNode<String> era_string = StringConstant("era");
      field_names.Push(era_string);
      TNode<String> eraYear_string = StringConstant("eraYear");
      field_names.Push(eraYear_string);
    }
    Goto(&done);
  }
  BIND(&done);
  return field_names.ToJSArray(context);
}

// #sec-iterabletolistoftype
TNode<FixedArray>
TemporalBuiltinsAssembler::TemporalInstantFixedArrayFromIterable(
    TNode<Context> context, TNode<Object> iterable) {
  GrowableFixedArray list(state());
  Label done(this);
  // 1. If iterable is undefined, then
  //   a. Return a new empty List.
  GotoIf(IsUndefined(iterable), &done);

  // 2. Let iteratorRecord be ? GetIterator(items) (handled by Iterate).

  // 3. Let list be a new empty List.

  // 3. Let next be true. (handled by Iterate).
  // 4. Repeat, while next is not false (handled by Iterate).
  Iterate(context, iterable,
          [&](TNode<Object> next_value) {
            // Handled by Iterate:
            //  a. Set next to ? IteratorStep(iteratorRecord).
            //  b. If next is not false, then
            //   i. Let nextValue be ? IteratorValue(next).

            //   ii. If Type(nextValue) is not Object or nextValue does not have
            //   an [[InitializedTemporalInstant]] internal slot
            Label if_isnottemporalinstant(this, Label::kDeferred),
                loop_body_end(this);
            GotoIf(TaggedIsSmi(next_value), &if_isnottemporalinstant);
            TNode<Uint16T> next_value_type = LoadInstanceType(CAST(next_value));
            GotoIfNot(IsTemporalInstantInstanceType(next_value_type),
                      &if_isnottemporalinstant);

            //   iii. Append nextValue to the end of the List list.
            list.Push(next_value);
            Goto(&loop_body_end);

            // 5.b.ii
            BIND(&if_isnottemporalinstant);
            {
              // 1. Let error be ThrowCompletion(a newly created TypeError
              // object).
              CallRuntime(
                  Runtime::kThrowTypeError, context,
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
  return list.ToFixedArray();
}

TF_BUILTIN(TemporalInstantFixedArrayFromIterable, TemporalBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterable = Parameter<Object>(Descriptor::kIterable);

  Return(TemporalInstantFixedArrayFromIterable(context, iterable));
}

// #sec-temporal.calendar.prototype.fields
TF_BUILTIN(TemporalCalendarPrototypeFields, TemporalBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);

  CodeStubArguments args(this, argc);

  // 1. Let calendar be this value.
  TNode<Object> receiver = args.GetReceiver();

  // 2. Perform ? RequireInternalSlot(calendar,
  // [[InitializedTemporalCalendar]]).
  ThrowIfNotInstanceType(context, receiver, JS_TEMPORAL_CALENDAR_TYPE,
                         "Temporal.Calendar.prototype.fields");
  TNode<JSTemporalCalendar> calendar = CAST(receiver);

  // Step 3 and later is inside CalendarFieldsArrayFromIterable
  TNode<Object> iterable = args.GetOptionalArgumentValue(0);
  Return(CalendarFieldsArrayFromIterable(context, calendar, iterable));
}

}  // namespace internal
}  // namespace v8
