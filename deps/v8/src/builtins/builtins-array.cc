// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/logging.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-factory.h"
#include "src/common/assert-scope.h"
#include "src/debug/debug.h"
#include "src/execution/isolate.h"
#include "src/execution/protectors-inl.h"
#include "src/handles/global-handles-inl.h"
#include "src/logging/counters.h"
#include "src/objects/contexts.h"
#include "src/objects/elements-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/js-shared-array-inl.h"
#include "src/objects/lookup.h"
#include "src/objects/objects-inl.h"
#include "src/objects/prototype.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

namespace {

inline bool IsJSArrayFastElementMovingAllowed(Isolate* isolate,
                                              Tagged<JSArray> receiver) {
  return JSObject::PrototypeHasNoElements(isolate, receiver);
}

inline bool HasSimpleElements(Tagged<JSObject> current) {
  return !IsCustomElementsReceiverMap(current->map()) &&
         !current->GetElementsAccessor()->HasAccessors(current);
}

inline bool HasOnlySimpleReceiverElements(Isolate* isolate,
                                          Tagged<JSObject> receiver) {
  // Check that we have no accessors on the receiver's elements.
  if (!HasSimpleElements(receiver)) return false;
  return JSObject::PrototypeHasNoElements(isolate, receiver);
}

inline bool HasOnlySimpleElements(Isolate* isolate,
                                  Tagged<JSReceiver> receiver) {
  DisallowGarbageCollection no_gc;
  PrototypeIterator iter(isolate, receiver, kStartAtReceiver);
  for (; !iter.IsAtEnd(); iter.Advance()) {
    if (!IsJSObject(iter.GetCurrent())) return false;
    Tagged<JSObject> current = iter.GetCurrent<JSObject>();
    if (!HasSimpleElements(current)) return false;
  }
  return true;
}

// This method may transition the elements kind of the JSArray once, to make
// sure that all elements provided as arguments in the specified range can be
// added without further elements kinds transitions.
void MatchArrayElementsKindToArguments(Isolate* isolate, Handle<JSArray> array,
                                       BuiltinArguments* args,
                                       int first_arg_index, int num_arguments) {
  int args_length = args->length();
  if (first_arg_index >= args_length) return;

  ElementsKind origin_kind = array->GetElementsKind();

  // We do not need to transition for PACKED/HOLEY_ELEMENTS.
  if (IsObjectElementsKind(origin_kind)) return;

  ElementsKind target_kind = origin_kind;
  {
    DisallowGarbageCollection no_gc;
    int last_arg_index = std::min(first_arg_index + num_arguments, args_length);
    for (int i = first_arg_index; i < last_arg_index; i++) {
      Tagged<Object> arg = (*args)[i];
      if (IsHeapObject(arg)) {
        if (IsHeapNumber(arg)) {
          target_kind = PACKED_DOUBLE_ELEMENTS;
        } else {
          target_kind = PACKED_ELEMENTS;
          break;
        }
      }
    }
  }
  if (target_kind != origin_kind) {
    // Use a short-lived HandleScope to avoid creating several copies of the
    // elements handle which would cause issues when left-trimming later-on.
    HandleScope scope(isolate);
    JSObject::TransitionElementsKind(array, target_kind);
  }
}

// Returns |false| if not applicable.
// TODO(szuend): Refactor this function because it is getting hard to
//               understand what each call-site actually checks.
V8_WARN_UNUSED_RESULT
inline bool EnsureJSArrayWithWritableFastElements(Isolate* isolate,
                                                  Handle<Object> receiver,
                                                  BuiltinArguments* args,
                                                  int first_arg_index,
                                                  int num_arguments) {
  if (!IsJSArray(*receiver)) return false;
  Handle<JSArray> array = Cast<JSArray>(receiver);
  ElementsKind origin_kind = array->GetElementsKind();
  if (IsDictionaryElementsKind(origin_kind)) return false;
  if (!array->map()->is_extensible()) return false;
  if (args == nullptr) return true;

  // If there may be elements accessors in the prototype chain, the fast path
  // cannot be used if there arguments to add to the array.
  if (!IsJSArrayFastElementMovingAllowed(isolate, *array)) return false;

  // Adding elements to the array prototype would break code that makes sure
  // it has no elements. Handle that elsewhere.
  if (isolate->IsAnyInitialArrayPrototype(*array)) return false;

  // Need to ensure that the arguments passed in args can be contained in
  // the array.
  MatchArrayElementsKindToArguments(isolate, array, args, first_arg_index,
                                    num_arguments);
  return true;
}

// If |index| is Undefined, returns init_if_undefined.
// If |index| is negative, returns length + index.
// If |index| is positive, returns index.
// Returned value is guaranteed to be in the interval of [0, length].
V8_WARN_UNUSED_RESULT Maybe<double> GetRelativeIndex(Isolate* isolate,
                                                     double length,
                                                     Handle<Object> index,
                                                     double init_if_undefined) {
  double relative_index = init_if_undefined;
  if (!IsUndefined(*index)) {
    Handle<Object> relative_index_obj;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, relative_index_obj,
                                     Object::ToInteger(isolate, index),
                                     Nothing<double>());
    relative_index = Object::NumberValue(*relative_index_obj);
  }

  if (relative_index < 0) {
    return Just(std::max(length + relative_index, 0.0));
  }

  return Just(std::min(relative_index, length));
}

// Returns "length", has "fast-path" for JSArrays.
V8_WARN_UNUSED_RESULT Maybe<double> GetLengthProperty(
    Isolate* isolate, Handle<JSReceiver> receiver) {
  if (IsJSArray(*receiver)) {
    auto array = Cast<JSArray>(receiver);
    double length = Object::NumberValue(array->length());
    DCHECK(0 <= length && length <= kMaxSafeInteger);

    return Just(length);
  }

  Handle<Object> raw_length_number;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, raw_length_number,
      Object::GetLengthFromArrayLike(isolate, receiver), Nothing<double>());
  return Just(Object::NumberValue(*raw_length_number));
}

// Set "length" property, has "fast-path" for JSArrays.
// Returns Nothing if something went wrong.
V8_WARN_UNUSED_RESULT MaybeHandle<Object> SetLengthProperty(
    Isolate* isolate, Handle<JSReceiver> receiver, double length) {
  if (IsJSArray(*receiver)) {
    Handle<JSArray> array = Cast<JSArray>(receiver);
    if (!JSArray::HasReadOnlyLength(array)) {
      DCHECK_LE(length, kMaxUInt32);
      MAYBE_RETURN_NULL(
          JSArray::SetLength(array, static_cast<uint32_t>(length)));
      return receiver;
    }
  }

  return Object::SetProperty(
      isolate, receiver, isolate->factory()->length_string(),
      isolate->factory()->NewNumber(length), StoreOrigin::kMaybeKeyed,
      Just(ShouldThrow::kThrowOnError));
}

V8_WARN_UNUSED_RESULT Tagged<Object> GenericArrayFill(
    Isolate* isolate, Handle<JSReceiver> receiver, Handle<Object> value,
    double start, double end) {
  // 7. Repeat, while k < final.
  while (start < end) {
    // a. Let Pk be ! ToString(k).
    Handle<String> index = isolate->factory()->NumberToString(
        isolate->factory()->NewNumber(start));

    // b. Perform ? Set(O, Pk, value, true).
    RETURN_FAILURE_ON_EXCEPTION(isolate, Object::SetPropertyOrElement(
                                             isolate, receiver, index, value,
                                             Just(ShouldThrow::kThrowOnError)));

    // c. Increase k by 1.
    ++start;
  }

  // 8. Return O.
  return *receiver;
}

V8_WARN_UNUSED_RESULT Maybe<bool> TryFastArrayFill(
    Isolate* isolate, BuiltinArguments* args, Handle<JSReceiver> receiver,
    Handle<Object> value, double start_index, double end_index) {
  // If indices are too large, use generic path since they are stored as
  // properties, not in the element backing store.
  if (end_index > kMaxUInt32) return Just(false);
  if (!IsJSObject(*receiver)) return Just(false);

  if (!EnsureJSArrayWithWritableFastElements(isolate, receiver, args, 1, 1)) {
    return Just(false);
  }

  Handle<JSArray> array = Cast<JSArray>(receiver);

  // If no argument was provided, we fill the array with 'undefined'.
  // EnsureJSArrayWith... does not handle that case so we do it here.
  // TODO(szuend): Pass target elements kind to EnsureJSArrayWith... when
  //               it gets refactored.
  if (args->length() == 1 && array->GetElementsKind() != PACKED_ELEMENTS) {
    // Use a short-lived HandleScope to avoid creating several copies of the
    // elements handle which would cause issues when left-trimming later-on.
    HandleScope scope(isolate);
    JSObject::TransitionElementsKind(array, PACKED_ELEMENTS);
  }

  DCHECK_LE(start_index, kMaxUInt32);
  DCHECK_LE(end_index, kMaxUInt32);

  uint32_t start, end;
  CHECK(DoubleToUint32IfEqualToSelf(start_index, &start));
  CHECK(DoubleToUint32IfEqualToSelf(end_index, &end));

  ElementsAccessor* accessor = array->GetElementsAccessor();
  RETURN_ON_EXCEPTION_VALUE(isolate, accessor->Fill(array, value, start, end),
                            Nothing<bool>());

  // It's possible the JSArray's 'length' property was assigned to after the
  // length was loaded due to user code during argument coercion of the start
  // and end parameters. The spec algorithm does a Set, meaning the length would
  // grow as needed during the fill.
  //
  // ElementAccessor::Fill is able to grow the backing store as needed, but we
  // need to ensure the JSArray's length is correctly set in case the user
  // assigned a smaller value.
  if (Object::NumberValue(array->length()) < end) {
    CHECK(accessor->SetLength(array, end).FromJust());
  }

  return Just(true);
}
}  // namespace

BUILTIN(ArrayPrototypeFill) {
  HandleScope scope(isolate);

  // 1. Let O be ? ToObject(this value).
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, receiver, Object::ToObject(isolate, args.receiver()));

  // 2. Let len be ? ToLength(? Get(O, "length")).
  double length;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, length, GetLengthProperty(isolate, receiver));

  // 3. Let relativeStart be ? ToInteger(start).
  // 4. If relativeStart < 0, let k be max((len + relativeStart), 0);
  //    else let k be min(relativeStart, len).
  Handle<Object> start = args.atOrUndefined(isolate, 2);

  double start_index;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, start_index, GetRelativeIndex(isolate, length, start, 0));

  // 5. If end is undefined, let relativeEnd be len;
  //    else let relativeEnd be ? ToInteger(end).
  // 6. If relativeEnd < 0, let final be max((len + relativeEnd), 0);
  //    else let final be min(relativeEnd, len).
  Handle<Object> end = args.atOrUndefined(isolate, 3);

  double end_index;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, end_index, GetRelativeIndex(isolate, length, end, length));

  if (start_index >= end_index) return *receiver;

  // Ensure indexes are within array bounds
  DCHECK_LE(0, start_index);
  DCHECK_LE(start_index, end_index);
  DCHECK_LE(end_index, length);

  Handle<Object> value = args.atOrUndefined(isolate, 1);

  bool success;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, success,
      TryFastArrayFill(isolate, &args, receiver, value, start_index,
                       end_index));
  if (success) return *receiver;
  return GenericArrayFill(isolate, receiver, value, start_index, end_index);
}

namespace {
V8_WARN_UNUSED_RESULT Tagged<Object> GenericArrayPush(Isolate* isolate,
                                                      BuiltinArguments* args) {
  // 1. Let O be ? ToObject(this value).
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, receiver, Object::ToObject(isolate, args->receiver()));

  // 2. Let len be ? ToLength(? Get(O, "length")).
  Handle<Object> raw_length_number;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, raw_length_number,
      Object::GetLengthFromArrayLike(isolate, receiver));

  // 3. Let args be a List whose elements are, in left to right order,
  //    the arguments that were passed to this function invocation.
  // 4. Let arg_count be the number of elements in args.
  int arg_count = args->length() - 1;

  // 5. If len + arg_count > 2^53-1, throw a TypeError exception.
  double length = Object::NumberValue(*raw_length_number);
  if (arg_count > kMaxSafeInteger - length) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kPushPastSafeLength,
                              isolate->factory()->NewNumberFromInt(arg_count),
                              raw_length_number));
  }

  // 6. Repeat, while args is not empty.
  for (int i = 0; i < arg_count; ++i) {
    // a. Remove the first element from args and let E be the value of the
    //    element.
    Handle<Object> element = args->at(i + 1);

    // b. Perform ? Set(O, ! ToString(len), E, true).
    if (length <= JSObject::kMaxElementIndex) {
      RETURN_FAILURE_ON_EXCEPTION(
          isolate, Object::SetElement(isolate, receiver, length, element,
                                      ShouldThrow::kThrowOnError));
    } else {
      PropertyKey key(isolate, length);
      LookupIterator it(isolate, receiver, key);
      MAYBE_RETURN(Object::SetProperty(&it, element, StoreOrigin::kMaybeKeyed,
                                       Just(ShouldThrow::kThrowOnError)),
                   ReadOnlyRoots(isolate).exception());
    }

    // c. Let len be len+1.
    ++length;
  }

  // 7. Perform ? Set(O, "length", len, true).
  Handle<Object> final_length = isolate->factory()->NewNumber(length);
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, Object::SetProperty(isolate, receiver,
                                   isolate->factory()->length_string(),
                                   final_length, StoreOrigin::kMaybeKeyed,
                                   Just(ShouldThrow::kThrowOnError)));

  // 8. Return len.
  return *final_length;
}
}  // namespace

BUILTIN(ArrayPush) {
  HandleScope scope(isolate);
  Handle<Object> receiver = args.receiver();
  if (!EnsureJSArrayWithWritableFastElements(isolate, receiver, &args, 1,
                                             args.length() - 1)) {
    return GenericArrayPush(isolate, &args);
  }

  Handle<JSArray> array = Cast<JSArray>(receiver);
  bool has_read_only_length = JSArray::HasReadOnlyLength(array);

  if (has_read_only_length) {
    return GenericArrayPush(isolate, &args);
  }

  // Fast Elements Path
  int to_add = args.length() - 1;
  uint32_t len = static_cast<uint32_t>(Object::NumberValue(array->length()));
  if (to_add == 0) return *isolate->factory()->NewNumberFromUint(len);

  // Currently fixed arrays cannot grow too big, so we should never hit this.
  DCHECK_LE(to_add, Smi::kMaxValue - Smi::ToInt(array->length()));

  ElementsAccessor* accessor = array->GetElementsAccessor();
  uint32_t new_length;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, new_length, accessor->Push(array, &args, to_add));
  return *isolate->factory()->NewNumberFromUint((new_length));
}

namespace {

V8_WARN_UNUSED_RESULT Tagged<Object> GenericArrayPop(Isolate* isolate,
                                                     BuiltinArguments* args) {
  // 1. Let O be ? ToObject(this value).
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, receiver, Object::ToObject(isolate, args->receiver()));

  // 2. Let len be ? ToLength(? Get(O, "length")).
  Handle<Object> raw_length_number;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, raw_length_number,
      Object::GetLengthFromArrayLike(isolate, receiver));
  double length = Object::NumberValue(*raw_length_number);

  // 3. If len is zero, then.
  if (length == 0) {
    // a. Perform ? Set(O, "length", 0, true).
    RETURN_FAILURE_ON_EXCEPTION(
        isolate, Object::SetProperty(isolate, receiver,
                                     isolate->factory()->length_string(),
                                     Handle<Smi>(Smi::zero(), isolate),
                                     StoreOrigin::kMaybeKeyed,
                                     Just(ShouldThrow::kThrowOnError)));

    // b. Return undefined.
    return ReadOnlyRoots(isolate).undefined_value();
  }

  // 4. Else len > 0.
  // a. Let new_len be len-1.
  Handle<Object> new_length = isolate->factory()->NewNumber(length - 1);

  // b. Let index be ! ToString(newLen).
  Handle<String> index = isolate->factory()->NumberToString(new_length);

  // c. Let element be ? Get(O, index).
  Handle<Object> element;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, element, Object::GetPropertyOrElement(isolate, receiver, index));

  // d. Perform ? DeletePropertyOrThrow(O, index).
  MAYBE_RETURN(JSReceiver::DeletePropertyOrElement(receiver, index,
                                                   LanguageMode::kStrict),
               ReadOnlyRoots(isolate).exception());

  // e. Perform ? Set(O, "length", newLen, true).
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, Object::SetProperty(isolate, receiver,
                                   isolate->factory()->length_string(),
                                   new_length, StoreOrigin::kMaybeKeyed,
                                   Just(ShouldThrow::kThrowOnError)));

  // f. Return element.
  return *element;
}

}  // namespace

BUILTIN(ArrayPop) {
  HandleScope scope(isolate);
  Handle<Object> receiver = args.receiver();
  if (!EnsureJSArrayWithWritableFastElements(isolate, receiver, nullptr, 0,
                                             0)) {
    return GenericArrayPop(isolate, &args);
  }
  Handle<JSArray> array = Cast<JSArray>(receiver);

  uint32_t len = static_cast<uint32_t>(Object::NumberValue(array->length()));

  if (JSArray::HasReadOnlyLength(array)) {
    return GenericArrayPop(isolate, &args);
  }
  if (len == 0) return ReadOnlyRoots(isolate).undefined_value();

  Handle<Object> result;
  if (IsJSArrayFastElementMovingAllowed(isolate, Cast<JSArray>(*receiver))) {
    // Fast Elements Path
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result, array->GetElementsAccessor()->Pop(array));
  } else {
    // Use Slow Lookup otherwise
    uint32_t new_length = len - 1;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result, JSReceiver::GetElement(isolate, array, new_length));

    // The length could have become read-only during the last GetElement() call,
    // so check again.
    if (JSArray::HasReadOnlyLength(array)) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kStrictReadOnlyProperty,
                                isolate->factory()->length_string(),
                                Object::TypeOf(isolate, array), array));
    }
    bool set_len_ok;
    MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, set_len_ok, JSArray::SetLength(array, new_length));
  }

  return *result;
}

namespace {

// Returns true, iff we can use ElementsAccessor for shifting.
V8_WARN_UNUSED_RESULT bool CanUseFastArrayShift(Isolate* isolate,
                                                Handle<JSReceiver> receiver) {
  if (V8_COMPRESS_POINTERS_8GB_BOOL) return false;

  if (!EnsureJSArrayWithWritableFastElements(isolate, receiver, nullptr, 0,
                                             0) ||
      !IsJSArrayFastElementMovingAllowed(isolate, Cast<JSArray>(*receiver))) {
    return false;
  }

  Handle<JSArray> array = Cast<JSArray>(receiver);
  return !JSArray::HasReadOnlyLength(array);
}

V8_WARN_UNUSED_RESULT Tagged<Object> GenericArrayShift(
    Isolate* isolate, Handle<JSReceiver> receiver, double length) {
  // 4. Let first be ? Get(O, "0").
  Handle<Object> first;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, first,
                                     Object::GetElement(isolate, receiver, 0));

  // 5. Let k be 1.
  double k = 1;

  // 6. Repeat, while k < len.
  while (k < length) {
    // a. Let from be ! ToString(k).
    Handle<String> from =
        isolate->factory()->NumberToString(isolate->factory()->NewNumber(k));

    // b. Let to be ! ToString(k-1).
    Handle<String> to = isolate->factory()->NumberToString(
        isolate->factory()->NewNumber(k - 1));

    // c. Let fromPresent be ? HasProperty(O, from).
    bool from_present;
    MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, from_present,
        JSReceiver::HasProperty(isolate, receiver, from));

    // d. If fromPresent is true, then.
    if (from_present) {
      // i. Let fromVal be ? Get(O, from).
      Handle<Object> from_val;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, from_val,
          Object::GetPropertyOrElement(isolate, receiver, from));

      // ii. Perform ? Set(O, to, fromVal, true).
      RETURN_FAILURE_ON_EXCEPTION(
          isolate,
          Object::SetPropertyOrElement(isolate, receiver, to, from_val,
                                       Just(ShouldThrow::kThrowOnError)));
    } else {  // e. Else fromPresent is false,
      // i. Perform ? DeletePropertyOrThrow(O, to).
      MAYBE_RETURN(JSReceiver::DeletePropertyOrElement(receiver, to,
                                                       LanguageMode::kStrict),
                   ReadOnlyRoots(isolate).exception());
    }

    // f. Increase k by 1.
    ++k;
  }

  // 7. Perform ? DeletePropertyOrThrow(O, ! ToString(len-1)).
  Handle<String> new_length = isolate->factory()->NumberToString(
      isolate->factory()->NewNumber(length - 1));
  MAYBE_RETURN(JSReceiver::DeletePropertyOrElement(receiver, new_length,
                                                   LanguageMode::kStrict),
               ReadOnlyRoots(isolate).exception());

  // 8. Perform ? Set(O, "length", len-1, true).
  RETURN_FAILURE_ON_EXCEPTION(isolate,
                              SetLengthProperty(isolate, receiver, length - 1));

  // 9. Return first.
  return *first;
}
}  // namespace

BUILTIN(ArrayShift) {
  HandleScope scope(isolate);

  // 1. Let O be ? ToObject(this value).
  Handle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, receiver, Object::ToObject(isolate, args.receiver()));

  // 2. Let len be ? ToLength(? Get(O, "length")).
  double length;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, length, GetLengthProperty(isolate, receiver));

  // 3. If len is zero, then.
  if (length == 0) {
    // a. Perform ? Set(O, "length", 0, true).
    RETURN_FAILURE_ON_EXCEPTION(isolate,
                                SetLengthProperty(isolate, receiver, length));

    // b. Return undefined.
    return ReadOnlyRoots(isolate).undefined_value();
  }

  if (CanUseFastArrayShift(isolate, receiver)) {
    Handle<JSArray> array = Cast<JSArray>(receiver);
    RETURN_RESULT_OR_FAILURE(isolate,
                             array->GetElementsAccessor()->Shift(array));
  }

  return GenericArrayShift(isolate, receiver, length);
}

BUILTIN(ArrayUnshift) {
  HandleScope scope(isolate);
  DCHECK(IsJSArray(*args.receiver()));
  Handle<JSArray> array = Cast<JSArray>(args.receiver());

  // These are checked in the Torque builtin.
  DCHECK(array->map()->is_extensible());
  DCHECK(!IsDictionaryElementsKind(array->GetElementsKind()));
  DCHECK(IsJSArrayFastElementMovingAllowed(isolate, *array));
  DCHECK(!isolate->IsAnyInitialArrayPrototype(*array));

  MatchArrayElementsKindToArguments(isolate, array, &args, 1,
                                    args.length() - 1);

  int to_add = args.length() - 1;
  if (to_add == 0) return array->length();

  // Currently fixed arrays cannot grow too big, so we should never hit this.
  DCHECK_LE(to_add, Smi::kMaxValue - Smi::ToInt(array->length()));
  DCHECK(!JSArray::HasReadOnlyLength(array));

  ElementsAccessor* accessor = array->GetElementsAccessor();
  uint32_t new_length;
  MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, new_length, accessor->Unshift(array, &args, to_add));
  return Smi::FromInt(new_length);
}

// Array Concat -------------------------------------------------------------

namespace {

/**
 * A simple visitor visits every element of Array's.
 * The backend storage can be a fixed array for fast elements case,
 * or a dictionary for sparse array. Since Dictionary is a subtype
 * of FixedArray, the class can be used by both fast and slow cases.
 * The second parameter of the constructor, fast_elements, specifies
 * whether the storage is a FixedArray or Dictionary.
 *
 * An index limit is used to deal with the situation that a result array
 * length overflows 32-bit non-negative integer.
 */
class ArrayConcatVisitor {
 public:
  ArrayConcatVisitor(Isolate* isolate, DirectHandle<HeapObject> storage,
                     bool fast_elements)
      : isolate_(isolate),
        storage_(isolate->global_handles()->Create(*storage)),
        index_offset_(0u),
        bit_field_(FastElementsField::encode(fast_elements) |
                   ExceedsLimitField::encode(false) |
                   IsFixedArrayField::encode(IsFixedArray(*storage, isolate)) |
                   HasSimpleElementsField::encode(
                       IsFixedArray(*storage, isolate) ||
                       // Don't take fast path for storages that might have
                       // side effects when storing to them.
                       (!IsCustomElementsReceiverMap(storage->map(isolate)) &&
                        !IsJSTypedArray(*storage, isolate)))) {
    DCHECK_IMPLIES(this->fast_elements(), is_fixed_array());
  }

  ~ArrayConcatVisitor() { clear_storage(); }

  V8_WARN_UNUSED_RESULT bool visit(uint32_t i, Handle<Object> elm) {
    uint32_t index = index_offset_ + i;

    // Note we use >=kMaxArrayLength instead of the more appropriate
    // >kMaxArrayIndex here due to overflowing arithmetic and
    // increase_index_offset.
    if (i >= JSArray::kMaxArrayLength - index_offset_) {
      set_exceeds_array_limit(true);
      // Exception hasn't been thrown at this point. Return true to
      // break out, and caller will throw. !visit would imply that
      // there is already a exception.
      return true;
    }

    if (!is_fixed_array()) {
      MAYBE_RETURN(JSReceiver::CreateDataProperty(isolate_, storage_,
                                                  PropertyKey(isolate_, index),
                                                  elm, Just(kThrowOnError)),
                   false);
      return true;
    }

    if (fast_elements()) {
      if (index < static_cast<uint32_t>(storage_fixed_array()->length())) {
        storage_fixed_array()->set(index, *elm);
        return true;
      }
      // Our initial estimate of length was foiled, possibly by
      // getters on the arrays increasing the length of later arrays
      // during iteration.
      // This shouldn't happen in anything but pathological cases.
      SetDictionaryMode();
      // Fall-through to dictionary mode.
    }
    DCHECK(!fast_elements());
    Handle<NumberDictionary> dict(Cast<NumberDictionary>(*storage_), isolate_);
    // The object holding this backing store has just been allocated, so
    // it cannot yet be used as a prototype.
    Handle<JSObject> not_a_prototype_holder;
    Handle<NumberDictionary> result = NumberDictionary::Set(
        isolate_, dict, index, elm, not_a_prototype_holder);
    if (!result.is_identical_to(dict)) {
      // Dictionary needed to grow.
      clear_storage();
      set_storage(*result);
    }
    return true;
  }

  uint32_t index_offset() const { return index_offset_; }

  void increase_index_offset(uint32_t delta) {
    if (JSArray::kMaxArrayLength - index_offset_ < delta) {
      index_offset_ = JSArray::kMaxArrayLength;
    } else {
      index_offset_ += delta;
    }
    // If the initial length estimate was off (see special case in visit()),
    // but the array blowing the limit didn't contain elements beyond the
    // provided-for index range, go to dictionary mode now.
    if (fast_elements() &&
        index_offset_ >
            static_cast<uint32_t>(Cast<FixedArrayBase>(*storage_)->length())) {
      SetDictionaryMode();
    }
  }

  bool exceeds_array_limit() const {
    return ExceedsLimitField::decode(bit_field_);
  }

  Handle<JSArray> ToArray() {
    DCHECK(is_fixed_array());
    Handle<JSArray> array = isolate_->factory()->NewJSArray(0);
    DirectHandle<Number> length =
        isolate_->factory()->NewNumber(static_cast<double>(index_offset_));
    DirectHandle<Map> map = JSObject::GetElementsTransitionMap(
        array, fast_elements() ? HOLEY_ELEMENTS : DICTIONARY_ELEMENTS);
    {
      DisallowGarbageCollection no_gc;
      Tagged<JSArray> raw = *array;
      raw->set_length(*length);
      raw->set_elements(*storage_fixed_array());
      raw->set_map(*map, kReleaseStore);
    }
    return array;
  }

  V8_WARN_UNUSED_RESULT MaybeHandle<JSReceiver> ToJSReceiver() {
    DCHECK(!is_fixed_array());
    Handle<JSReceiver> result = Cast<JSReceiver>(storage_);
    Handle<Object> length =
        isolate_->factory()->NewNumber(static_cast<double>(index_offset_));
    RETURN_ON_EXCEPTION(
        isolate_, Object::SetProperty(isolate_, result,
                                      isolate_->factory()->length_string(),
                                      length, StoreOrigin::kMaybeKeyed,
                                      Just(ShouldThrow::kThrowOnError)));
    return result;
  }
  bool has_simple_elements() const {
    return HasSimpleElementsField::decode(bit_field_);
  }

 private:
  // Convert storage to dictionary mode.
  void SetDictionaryMode() {
    DCHECK(fast_elements() && is_fixed_array());
    DirectHandle<FixedArray> current_storage = storage_fixed_array();
    Handle<NumberDictionary> slow_storage(
        NumberDictionary::New(isolate_, current_storage->length()));
    uint32_t current_length = static_cast<uint32_t>(current_storage->length());
    FOR_WITH_HANDLE_SCOPE(
        isolate_, uint32_t, i = 0, i, i < current_length, i++, {
          Handle<Object> element(current_storage->get(i), isolate_);
          if (!IsTheHole(*element, isolate_)) {
            // The object holding this backing store has just been allocated, so
            // it cannot yet be used as a prototype.
            Handle<JSObject> not_a_prototype_holder;
            Handle<NumberDictionary> new_storage = NumberDictionary::Set(
                isolate_, slow_storage, i, element, not_a_prototype_holder);
            if (!new_storage.is_identical_to(slow_storage)) {
              slow_storage = loop_scope.CloseAndEscape(new_storage);
            }
          }
        });
    clear_storage();
    set_storage(*slow_storage);
    set_fast_elements(false);
  }

  inline void clear_storage() { GlobalHandles::Destroy(storage_.location()); }

  inline void set_storage(Tagged<FixedArray> storage) {
    DCHECK(is_fixed_array());
    DCHECK(has_simple_elements());
    storage_ = isolate_->global_handles()->Create(storage);
  }

  using FastElementsField = base::BitField<bool, 0, 1>;
  using ExceedsLimitField = base::BitField<bool, 1, 1>;
  using IsFixedArrayField = base::BitField<bool, 2, 1>;
  using HasSimpleElementsField = base::BitField<bool, 3, 1>;

  bool fast_elements() const { return FastElementsField::decode(bit_field_); }
  void set_fast_elements(bool fast) {
    bit_field_ = FastElementsField::update(bit_field_, fast);
  }
  void set_exceeds_array_limit(bool exceeds) {
    bit_field_ = ExceedsLimitField::update(bit_field_, exceeds);
  }
  bool is_fixed_array() const { return IsFixedArrayField::decode(bit_field_); }
  Handle<FixedArray> storage_fixed_array() {
    DCHECK(is_fixed_array());
    DCHECK(has_simple_elements());
    return Cast<FixedArray>(storage_);
  }

  Isolate* isolate_;
  Handle<Object> storage_;  // Always a global handle.
  // Index after last seen index. Always less than or equal to
  // JSArray::kMaxArrayLength.
  uint32_t index_offset_;
  uint32_t bit_field_;
};

uint32_t EstimateElementCount(Isolate* isolate, DirectHandle<JSArray> array) {
  DisallowGarbageCollection no_gc;
  uint32_t length = static_cast<uint32_t>(Object::NumberValue(array->length()));
  int element_count = 0;
  switch (array->GetElementsKind()) {
    case PACKED_SMI_ELEMENTS:
    case HOLEY_SMI_ELEMENTS:
    case PACKED_ELEMENTS:
    case PACKED_FROZEN_ELEMENTS:
    case PACKED_SEALED_ELEMENTS:
    case PACKED_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_FROZEN_ELEMENTS:
    case HOLEY_SEALED_ELEMENTS:
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_ELEMENTS: {
      // Fast elements can't have lengths that are not representable by
      // a 32-bit signed integer.
      DCHECK_GE(static_cast<int32_t>(FixedArray::kMaxLength), 0);
      int fast_length = static_cast<int>(length);
      Tagged<FixedArray> elements = Cast<FixedArray>(array->elements());
      for (int i = 0; i < fast_length; i++) {
        if (!IsTheHole(elements->get(i), isolate)) element_count++;
      }
      break;
    }
    case PACKED_DOUBLE_ELEMENTS:
    case HOLEY_DOUBLE_ELEMENTS: {
      // Fast elements can't have lengths that are not representable by
      // a 32-bit signed integer.
      DCHECK_GE(static_cast<int32_t>(FixedDoubleArray::kMaxLength), 0);
      int fast_length = static_cast<int>(length);
      if (IsFixedArray(array->elements())) {
        DCHECK_EQ(Cast<FixedArray>(array->elements())->length(), 0);
        break;
      }
      Tagged<FixedDoubleArray> elements =
          Cast<FixedDoubleArray>(array->elements());
      for (int i = 0; i < fast_length; i++) {
        if (!elements->is_the_hole(i)) element_count++;
      }
      break;
    }
    case DICTIONARY_ELEMENTS: {
      Tagged<NumberDictionary> dictionary =
          Cast<NumberDictionary>(array->elements());
      ReadOnlyRoots roots(isolate);
      for (InternalIndex i : dictionary->IterateEntries()) {
        Tagged<Object> key = dictionary->KeyAt(i);
        if (dictionary->IsKey(roots, key)) {
          element_count++;
        }
      }
      break;
    }
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) case TYPE##_ELEMENTS:

      TYPED_ARRAYS(TYPED_ARRAY_CASE)
      RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
      // External arrays are always dense.
      return length;

#undef TYPED_ARRAY_CASE
    case NO_ELEMENTS:
      return 0;
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS:
    case FAST_STRING_WRAPPER_ELEMENTS:
    case SLOW_STRING_WRAPPER_ELEMENTS:
    case WASM_ARRAY_ELEMENTS:
    case SHARED_ARRAY_ELEMENTS:
      UNREACHABLE();
  }
  // As an estimate, we assume that the prototype doesn't contain any
  // inherited elements.
  return element_count;
}

void CollectElementIndices(Isolate* isolate, Handle<JSObject> object,
                           uint32_t range, std::vector<uint32_t>* indices) {
  ElementsKind kind = object->GetElementsKind();
  switch (kind) {
    case PACKED_SMI_ELEMENTS:
    case PACKED_ELEMENTS:
    case PACKED_FROZEN_ELEMENTS:
    case PACKED_SEALED_ELEMENTS:
    case PACKED_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_SMI_ELEMENTS:
    case HOLEY_FROZEN_ELEMENTS:
    case HOLEY_SEALED_ELEMENTS:
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_ELEMENTS: {
      DisallowGarbageCollection no_gc;
      Tagged<FixedArray> elements = Cast<FixedArray>(object->elements());
      uint32_t length = static_cast<uint32_t>(elements->length());
      if (range < length) length = range;
      for (uint32_t i = 0; i < length; i++) {
        if (!IsTheHole(elements->get(i), isolate)) {
          indices->push_back(i);
        }
      }
      break;
    }
    case HOLEY_DOUBLE_ELEMENTS:
    case PACKED_DOUBLE_ELEMENTS: {
      if (IsFixedArray(object->elements())) {
        DCHECK_EQ(object->elements()->length(), 0);
        break;
      }
      DirectHandle<FixedDoubleArray> elements(
          Cast<FixedDoubleArray>(object->elements()), isolate);
      uint32_t length = static_cast<uint32_t>(elements->length());
      if (range < length) length = range;
      for (uint32_t i = 0; i < length; i++) {
        if (!elements->is_the_hole(i)) {
          indices->push_back(i);
        }
      }
      break;
    }
    case DICTIONARY_ELEMENTS: {
      DisallowGarbageCollection no_gc;
      Tagged<NumberDictionary> dict =
          Cast<NumberDictionary>(object->elements());
      uint32_t capacity = dict->Capacity();
      ReadOnlyRoots roots(isolate);
      FOR_WITH_HANDLE_SCOPE(isolate, uint32_t, j = 0, j, j < capacity, j++, {
        Tagged<Object> k = dict->KeyAt(InternalIndex(j));
        if (!dict->IsKey(roots, k)) continue;
        DCHECK(IsNumber(k));
        uint32_t index = static_cast<uint32_t>(Object::NumberValue(k));
        if (index < range) {
          indices->push_back(index);
        }
      });
      break;
    }
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) case TYPE##_ELEMENTS:

      TYPED_ARRAYS(TYPED_ARRAY_CASE) RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE) {
        size_t length = Cast<JSTypedArray>(object)->GetLength();
        if (range <= length) {
          length = range;
          // We will add all indices, so we might as well clear it first
          // and avoid duplicates.
          indices->clear();
        }
        // {range} puts a cap on {length}.
        DCHECK_LE(length, std::numeric_limits<uint32_t>::max());
        for (uint32_t i = 0; i < length; i++) {
          indices->push_back(i);
        }
        if (length == range) return;  // All indices accounted for already.
        break;
      }

#undef TYPED_ARRAY_CASE
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS: {
      DisallowGarbageCollection no_gc;
      DisableGCMole no_gc_mole;
      Tagged<FixedArrayBase> elements = object->elements();
      Tagged<JSObject> raw_object = *object;
      ElementsAccessor* accessor = object->GetElementsAccessor();
      for (uint32_t i = 0; i < range; i++) {
        if (accessor->HasElement(raw_object, i, elements)) {
          indices->push_back(i);
        }
      }
      break;
    }
    case FAST_STRING_WRAPPER_ELEMENTS:
    case SLOW_STRING_WRAPPER_ELEMENTS: {
      DCHECK(IsJSPrimitiveWrapper(*object));
      auto js_value = Cast<JSPrimitiveWrapper>(object);
      DCHECK(IsString(js_value->value()));
      DirectHandle<String> string(Cast<String>(js_value->value()), isolate);
      uint32_t length = static_cast<uint32_t>(string->length());
      uint32_t i = 0;
      uint32_t limit = std::min(length, range);
      for (; i < limit; i++) {
        indices->push_back(i);
      }
      ElementsAccessor* accessor = object->GetElementsAccessor();
      for (; i < range; i++) {
        if (accessor->HasElement(*object, i)) {
          indices->push_back(i);
        }
      }
      break;
    }
    case WASM_ARRAY_ELEMENTS:
      // TODO(ishell): implement
      UNIMPLEMENTED();
    case SHARED_ARRAY_ELEMENTS: {
      uint32_t length = Cast<JSSharedArray>(object)->elements()->length();
      if (range <= length) {
        length = range;
        indices->clear();
      }
      for (uint32_t i = 0; i < length; i++) {
        // JSSharedArrays are created non-resizable and do not have holes.
        SLOW_DCHECK(object->GetElementsAccessor()->HasElement(
            *object, i, object->elements()));
        indices->push_back(i);
      }
      if (length == range) return;
      break;
    }
    case NO_ELEMENTS:
      break;
  }

  PrototypeIterator iter(isolate, object);
  if (!iter.IsAtEnd()) {
    // The prototype will usually have no inherited element indices,
    // but we have to check.
    // Casting to JSObject is safe because we ran {HasOnlySimpleElements} on
    // the receiver before, which checks the prototype chain.
    CollectElementIndices(
        isolate, PrototypeIterator::GetCurrent<JSObject>(iter), range, indices);
  }
}

bool IterateElementsSlow(Isolate* isolate, Handle<JSReceiver> receiver,
                         uint32_t length, ArrayConcatVisitor* visitor) {
  FOR_WITH_HANDLE_SCOPE(isolate, uint32_t, i = 0, i, i < length, ++i, {
    Maybe<bool> maybe = JSReceiver::HasElement(isolate, receiver, i);
    if (maybe.IsNothing()) return false;
    if (maybe.FromJust()) {
      Handle<Object> element_value;
      ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, element_value, JSReceiver::GetElement(isolate, receiver, i),
          false);
      if (!visitor->visit(i, element_value)) return false;
    }
  });
  visitor->increase_index_offset(length);
  return true;
}
/**
 * A helper function that visits "array" elements of a JSReceiver in numerical
 * order.
 *
 * The visitor argument called for each existing element in the array
 * with the element index and the element's value.
 * Afterwards it increments the base-index of the visitor by the array
 * length.
 * Returns false if any access threw an exception, otherwise true.
 */
bool IterateElements(Isolate* isolate, Handle<JSReceiver> receiver,
                     ArrayConcatVisitor* visitor) {
  uint32_t length = 0;

  if (IsJSArray(*receiver)) {
    auto array = Cast<JSArray>(receiver);
    length = static_cast<uint32_t>(Object::NumberValue(array->length()));
  } else {
    Handle<Object> val;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, val, Object::GetLengthFromArrayLike(isolate, receiver), false);
    if (visitor->index_offset() + Object::NumberValue(*val) > kMaxSafeInteger) {
      isolate->Throw(*isolate->factory()->NewTypeError(
          MessageTemplate::kInvalidArrayLength));
      return false;
    }
    // TODO(caitp): Support larger element indexes (up to 2^53-1).
    if (!Object::ToUint32(*val, &length)) {
      length = 0;
    }
    // TODO(cbruni): handle other element kind as well
    return IterateElementsSlow(isolate, receiver, length, visitor);
  }

  if (!visitor->has_simple_elements() ||
      !HasOnlySimpleElements(isolate, *receiver)) {
    return IterateElementsSlow(isolate, receiver, length, visitor);
  }
  Handle<JSArray> array = Cast<JSArray>(receiver);

  switch (array->GetElementsKind()) {
    case PACKED_SMI_ELEMENTS:
    case PACKED_ELEMENTS:
    case PACKED_FROZEN_ELEMENTS:
    case PACKED_SEALED_ELEMENTS:
    case PACKED_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_SMI_ELEMENTS:
    case HOLEY_FROZEN_ELEMENTS:
    case HOLEY_SEALED_ELEMENTS:
    case HOLEY_NONEXTENSIBLE_ELEMENTS:
    case HOLEY_ELEMENTS: {
      // Disallow execution so the cached elements won't change mid execution.
      DisallowJavascriptExecution no_js(isolate);

      // Run through the elements FixedArray and use HasElement and GetElement
      // to check the prototype for missing elements.
      DirectHandle<FixedArray> elements(Cast<FixedArray>(array->elements()),
                                        isolate);
      int fast_length = static_cast<int>(length);
      DCHECK(fast_length <= elements->length());
      FOR_WITH_HANDLE_SCOPE(isolate, int, j = 0, j, j < fast_length, j++, {
        Handle<Object> element_value(elements->get(j), isolate);
        if (!IsTheHole(*element_value, isolate)) {
          if (!visitor->visit(j, element_value)) return false;
        } else {
          Maybe<bool> maybe = JSReceiver::HasElement(isolate, array, j);
          if (maybe.IsNothing()) return false;
          if (maybe.FromJust()) {
            // Call GetElement on array, not its prototype, or getters won't
            // have the correct receiver.
            ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                isolate, element_value,
                JSReceiver::GetElement(isolate, array, j), false);
            if (!visitor->visit(j, element_value)) return false;
          }
        }
      });
      break;
    }
    case HOLEY_DOUBLE_ELEMENTS:
    case PACKED_DOUBLE_ELEMENTS: {
      // Disallow execution so the cached elements won't change mid execution.
      DisallowJavascriptExecution no_js(isolate);

      // Empty array is FixedArray but not FixedDoubleArray.
      if (length == 0) break;
      // Run through the elements FixedArray and use HasElement and GetElement
      // to check the prototype for missing elements.
      if (IsFixedArray(array->elements())) {
        DCHECK_EQ(array->elements()->length(), 0);
        break;
      }
      DirectHandle<FixedDoubleArray> elements(
          Cast<FixedDoubleArray>(array->elements()), isolate);
      int fast_length = static_cast<int>(length);
      DCHECK(fast_length <= elements->length());
      FOR_WITH_HANDLE_SCOPE(isolate, int, j = 0, j, j < fast_length, j++, {
        if (!elements->is_the_hole(j)) {
          double double_value = elements->get_scalar(j);
          Handle<Object> element_value =
              isolate->factory()->NewNumber(double_value);
          if (!visitor->visit(j, element_value)) return false;
        } else {
          Maybe<bool> maybe = JSReceiver::HasElement(isolate, array, j);
          if (maybe.IsNothing()) return false;
          if (maybe.FromJust()) {
            // Call GetElement on array, not its prototype, or getters won't
            // have the correct receiver.
            Handle<Object> element_value;
            ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                isolate, element_value,
                JSReceiver::GetElement(isolate, array, j), false);
            if (!visitor->visit(j, element_value)) return false;
          }
        }
      });
      break;
    }

    case DICTIONARY_ELEMENTS: {
      // Disallow execution so the cached dictionary won't change mid execution.
      DisallowJavascriptExecution no_js(isolate);

      DirectHandle<NumberDictionary> dict(array->element_dictionary(), isolate);
      std::vector<uint32_t> indices;
      indices.reserve(dict->Capacity() / 2);

      // Collect all indices in the object and the prototypes less
      // than length. This might introduce duplicates in the indices list.
      CollectElementIndices(isolate, array, length, &indices);
      std::sort(indices.begin(), indices.end());
      size_t n = indices.size();
      FOR_WITH_HANDLE_SCOPE(isolate, size_t, j = 0, j, j < n, (void)0, {
        uint32_t index = indices[j];
        Handle<Object> element;
        ASSIGN_RETURN_ON_EXCEPTION_VALUE(
            isolate, element, JSReceiver::GetElement(isolate, array, index),
            false);
        if (!visitor->visit(index, element)) return false;
        // Skip to next different index (i.e., omit duplicates).
        do {
          j++;
        } while (j < n && indices[j] == index);
      });
      break;
    }
    case FAST_SLOPPY_ARGUMENTS_ELEMENTS:
    case SLOW_SLOPPY_ARGUMENTS_ELEMENTS: {
      FOR_WITH_HANDLE_SCOPE(
          isolate, uint32_t, index = 0, index, index < length, index++, {
            Handle<Object> element;
            ASSIGN_RETURN_ON_EXCEPTION_VALUE(
                isolate, element, JSReceiver::GetElement(isolate, array, index),
                false);
            if (!visitor->visit(index, element)) return false;
          });
      break;
    }
    case WASM_ARRAY_ELEMENTS:
      // TODO(ishell): implement
      UNIMPLEMENTED();
    case NO_ELEMENTS:
      break;
      // JSArrays cannot have the following elements kinds:
#define TYPED_ARRAY_CASE(Type, type, TYPE, ctype) case TYPE##_ELEMENTS:
      TYPED_ARRAYS(TYPED_ARRAY_CASE)
      RAB_GSAB_TYPED_ARRAYS(TYPED_ARRAY_CASE)
#undef TYPED_ARRAY_CASE
    case FAST_STRING_WRAPPER_ELEMENTS:
    case SLOW_STRING_WRAPPER_ELEMENTS:
    case SHARED_ARRAY_ELEMENTS:
      UNREACHABLE();
  }
  visitor->increase_index_offset(length);
  return true;
}

static Maybe<bool> IsConcatSpreadable(Isolate* isolate, Handle<Object> obj) {
  HandleScope handle_scope(isolate);
  if (!IsJSReceiver(*obj)) return Just(false);
  if (!Protectors::IsIsConcatSpreadableLookupChainIntact(isolate) ||
      Cast<JSReceiver>(*obj)->HasProxyInPrototype(isolate)) {
    // Slow path if @@isConcatSpreadable has been used.
    Handle<Symbol> key(isolate->factory()->is_concat_spreadable_symbol());
    Handle<Object> value;
    MaybeHandle<Object> maybeValue =
        i::Runtime::GetObjectProperty(isolate, obj, key);
    if (!maybeValue.ToHandle(&value)) return Nothing<bool>();
    if (!IsUndefined(*value, isolate))
      return Just(Object::BooleanValue(*value, isolate));
  }
  return Object::IsArray(obj);
}

Tagged<Object> Slow_ArrayConcat(BuiltinArguments* args, Handle<Object> species,
                                Isolate* isolate) {
  int argument_count = args->length();

  bool is_array_species = *species == isolate->context()->array_function();

  // Pass 1: estimate the length and number of elements of the result.
  // The actual length can be larger if any of the arguments have getters
  // that mutate other arguments (but will otherwise be precise).
  // The number of elements is precise if there are no inherited elements.

  ElementsKind kind = PACKED_SMI_ELEMENTS;

  uint32_t estimate_result_length = 0;
  uint32_t estimate_nof = 0;
  FOR_WITH_HANDLE_SCOPE(isolate, int, i = 0, i, i < argument_count, i++, {
    Handle<Object> obj = args->at(i);
    uint32_t length_estimate;
    uint32_t element_estimate;
    if (IsJSArray(*obj)) {
      auto array = Cast<JSArray>(obj);
      length_estimate =
          static_cast<uint32_t>(Object::NumberValue(array->length()));
      if (length_estimate != 0) {
        ElementsKind array_kind =
            GetPackedElementsKind(array->GetElementsKind());
        if (IsAnyNonextensibleElementsKind(array_kind)) {
          array_kind = PACKED_ELEMENTS;
        }
        kind = GetMoreGeneralElementsKind(kind, array_kind);
      }
      element_estimate = EstimateElementCount(isolate, array);
    } else {
      if (IsHeapObject(*obj)) {
        kind = GetMoreGeneralElementsKind(
            kind, IsNumber(*obj) ? PACKED_DOUBLE_ELEMENTS : PACKED_ELEMENTS);
      }
      length_estimate = 1;
      element_estimate = 1;
    }
    // Avoid overflows by capping at kMaxArrayLength.
    if (JSArray::kMaxArrayLength - estimate_result_length < length_estimate) {
      estimate_result_length = JSArray::kMaxArrayLength;
    } else {
      estimate_result_length += length_estimate;
    }
    if (JSArray::kMaxArrayLength - estimate_nof < element_estimate) {
      estimate_nof = JSArray::kMaxArrayLength;
    } else {
      estimate_nof += element_estimate;
    }
  });

  // If estimated number of elements is more than half of length, a
  // fixed array (fast case) is more time and space-efficient than a
  // dictionary.
  bool fast_case = is_array_species &&
                   (estimate_nof * 2) >= estimate_result_length &&
                   Protectors::IsIsConcatSpreadableLookupChainIntact(isolate);

  if (fast_case && kind == PACKED_DOUBLE_ELEMENTS) {
    Handle<FixedArrayBase> storage =
        isolate->factory()->NewFixedDoubleArray(estimate_result_length);
    int j = 0;
    bool failure = false;
    if (estimate_result_length > 0) {
      auto double_storage = Cast<FixedDoubleArray>(storage);
      for (int i = 0; i < argument_count; i++) {
        DirectHandle<Object> obj = args->at(i);
        if (IsSmi(*obj)) {
          double_storage->set(j, Smi::ToInt(*obj));
          j++;
        } else if (IsNumber(*obj)) {
          double_storage->set(j, Object::NumberValue(*obj));
          j++;
        } else {
          DisallowGarbageCollection no_gc;
          Tagged<JSArray> array = Cast<JSArray>(*obj);
          uint32_t length =
              static_cast<uint32_t>(Object::NumberValue(array->length()));
          switch (array->GetElementsKind()) {
            case HOLEY_DOUBLE_ELEMENTS:
            case PACKED_DOUBLE_ELEMENTS: {
              // Empty array is FixedArray but not FixedDoubleArray.
              if (length == 0) break;
              Tagged<FixedDoubleArray> elements =
                  Cast<FixedDoubleArray>(array->elements());
              for (uint32_t k = 0; k < length; k++) {
                if (elements->is_the_hole(k)) {
                  // TODO(jkummerow/verwaest): We could be a bit more clever
                  // here: Check if there are no elements/getters on the
                  // prototype chain, and if so, allow creation of a holey
                  // result array.
                  // Same thing below (holey smi case).
                  failure = true;
                  break;
                }
                double double_value = elements->get_scalar(k);
                double_storage->set(j, double_value);
                j++;
              }
              break;
            }
            case HOLEY_SMI_ELEMENTS:
            case PACKED_SMI_ELEMENTS: {
              Tagged<Object> the_hole = ReadOnlyRoots(isolate).the_hole_value();
              Tagged<FixedArray> elements(Cast<FixedArray>(array->elements()));
              for (uint32_t k = 0; k < length; k++) {
                Tagged<Object> element = elements->get(k);
                if (element == the_hole) {
                  failure = true;
                  break;
                }
                int32_t int_value = Smi::ToInt(element);
                double_storage->set(j, int_value);
                j++;
              }
              break;
            }
            case HOLEY_ELEMENTS:
            case HOLEY_FROZEN_ELEMENTS:
            case HOLEY_SEALED_ELEMENTS:
            case HOLEY_NONEXTENSIBLE_ELEMENTS:
            case PACKED_ELEMENTS:
            case PACKED_FROZEN_ELEMENTS:
            case PACKED_SEALED_ELEMENTS:
            case PACKED_NONEXTENSIBLE_ELEMENTS:
            case DICTIONARY_ELEMENTS:
            case NO_ELEMENTS:
              DCHECK_EQ(0u, length);
              break;
            default:
              UNREACHABLE();
          }
        }
        if (failure) {
#ifdef VERIFY_HEAP
          // The allocated storage may contain uninitialized values which will
          // cause FixedDoubleArray::FixedDoubleArrayVerify to fail, when the
          // heap is verified (see: crbug.com/1415071). To prevent this, we
          // initialize the array with holes.
          if (v8_flags.verify_heap) {
            double_storage->FillWithHoles(0, estimate_result_length);
          }
#endif  // VERIFY_HEAP
          break;
        }
      }
    }
    if (!failure) {
      return *isolate->factory()->NewJSArrayWithElements(storage, kind, j);
    }
    // In case of failure, fall through.
  }

  DirectHandle<HeapObject> storage;
  if (fast_case) {
    // The backing storage array must have non-existing elements to preserve
    // holes across concat operations.
    storage =
        isolate->factory()->NewFixedArrayWithHoles(estimate_result_length);
  } else if (is_array_species) {
    storage = NumberDictionary::New(isolate, estimate_nof);
  } else {
    DCHECK(IsConstructor(*species));
    Handle<Object> length(Smi::zero(), isolate);
    Handle<Object> storage_object;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, storage_object,
        Execution::New(isolate, species, species, 1, &length));
    storage = Cast<HeapObject>(storage_object);
  }

  ArrayConcatVisitor visitor(isolate, storage, fast_case);

  for (int i = 0; i < argument_count; i++) {
    Handle<Object> obj = args->at(i);
    Maybe<bool> spreadable = IsConcatSpreadable(isolate, obj);
    MAYBE_RETURN(spreadable, ReadOnlyRoots(isolate).exception());
    if (spreadable.FromJust()) {
      Handle<JSReceiver> object = Cast<JSReceiver>(obj);
      if (!IterateElements(isolate, object, &visitor)) {
        return ReadOnlyRoots(isolate).exception();
      }
    } else {
      if (!visitor.visit(0, obj)) return ReadOnlyRoots(isolate).exception();
      visitor.increase_index_offset(1);
    }
  }

  if (visitor.exceeds_array_limit()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kInvalidArrayLength));
  }

  if (is_array_species) {
    return *visitor.ToArray();
  } else {
    RETURN_RESULT_OR_FAILURE(isolate, visitor.ToJSReceiver());
  }
}

bool IsSimpleArray(Isolate* isolate, DirectHandle<JSArray> obj) {
  DisallowGarbageCollection no_gc;
  Tagged<Map> map = obj->map();
  // If there is only the 'length' property we are fine.
  if (map->prototype() ==
          isolate->native_context()->initial_array_prototype() &&
      map->NumberOfOwnDescriptors() == 1) {
    return true;
  }
  // TODO(cbruni): slower lookup for array subclasses and support slow
  // @@IsConcatSpreadable lookup.
  return false;
}

MaybeHandle<JSArray> Fast_ArrayConcat(Isolate* isolate,
                                      BuiltinArguments* args) {
  if (!Protectors::IsIsConcatSpreadableLookupChainIntact(isolate)) {
    return MaybeHandle<JSArray>();
  }
  // We shouldn't overflow when adding another len.
  const int kHalfOfMaxInt = 1 << (kBitsPerInt - 2);
  static_assert(FixedArray::kMaxLength < kHalfOfMaxInt);
  static_assert(FixedDoubleArray::kMaxLength < kHalfOfMaxInt);
  USE(kHalfOfMaxInt);

  int n_arguments = args->length();
  int result_len = 0;
  {
    DisallowGarbageCollection no_gc;
    // Iterate through all the arguments performing checks
    // and calculating total length.
    for (int i = 0; i < n_arguments; i++) {
      Tagged<Object> arg = (*args)[i];
      if (!IsJSArray(arg)) return MaybeHandle<JSArray>();
      if (!HasOnlySimpleReceiverElements(isolate, Cast<JSObject>(arg))) {
        return MaybeHandle<JSArray>();
      }
      // TODO(cbruni): support fast concatenation of DICTIONARY_ELEMENTS.
      if (!Cast<JSObject>(arg)->HasFastElements()) {
        return MaybeHandle<JSArray>();
      }
      DirectHandle<JSArray> array(Cast<JSArray>(arg), isolate);
      if (!IsSimpleArray(isolate, array)) {
        return MaybeHandle<JSArray>();
      }
      // The Array length is guaranted to be <= kHalfOfMaxInt thus we won't
      // overflow.
      result_len += Smi::ToInt(array->length());
      DCHECK_GE(result_len, 0);
      // Throw an Error if we overflow the FixedArray limits
      if (FixedDoubleArray::kMaxLength < result_len ||
          FixedArray::kMaxLength < result_len) {
        AllowGarbageCollection gc;
        THROW_NEW_ERROR(isolate,
                        NewRangeError(MessageTemplate::kInvalidArrayLength));
      }
    }
  }
  return ElementsAccessor::Concat(isolate, args, n_arguments, result_len);
}

}  // namespace

// ES6 22.1.3.1 Array.prototype.concat
BUILTIN(ArrayConcat) {
  HandleScope scope(isolate);

  Handle<Object> receiver = args.receiver();
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, receiver,
      Object::ToObject(isolate, args.receiver(), "Array.prototype.concat"));
  BuiltinArguments::ChangeValueScope set_receiver_value_scope(
      isolate, &args, BuiltinArguments::kReceiverOffset, *receiver);

  Handle<JSArray> result_array;

  // Avoid a real species read to avoid extra lookups to the array constructor
  if (V8_LIKELY(IsJSArray(*receiver) &&
                Cast<JSArray>(receiver)->HasArrayPrototype(isolate) &&
                Protectors::IsArraySpeciesLookupChainIntact(isolate))) {
    if (Fast_ArrayConcat(isolate, &args).ToHandle(&result_array)) {
      return *result_array;
    }
    if (isolate->has_exception()) return ReadOnlyRoots(isolate).exception();
  }
  // Reading @@species happens before anything else with a side effect, so
  // we can do it here to determine whether to take the fast path.
  Handle<Object> species;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, species, Object::ArraySpeciesConstructor(isolate, receiver));
  if (*species == *isolate->array_function()) {
    if (Fast_ArrayConcat(isolate, &args).ToHandle(&result_array)) {
      return *result_array;
    }
    if (isolate->has_exception()) return ReadOnlyRoots(isolate).exception();
  }
  return Slow_ArrayConcat(&args, species, isolate);
}

}  // namespace internal
}  // namespace v8
