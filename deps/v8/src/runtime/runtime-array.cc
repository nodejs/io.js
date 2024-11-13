// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/protectors-inl.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.
#include "src/objects/allocation-site-inl.h"
#include "src/objects/elements.h"
#include "src/objects/js-array-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_TransitionElementsKind) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<JSObject> object = args.at<JSObject>(0);
  Handle<Map> to_map = args.at<Map>(1);
  ElementsKind to_kind = to_map->elements_kind();
  if (ElementsAccessor::ForKind(to_kind)
          ->TransitionElementsKind(object, to_map)
          .IsNothing()) {
    // TODO(victorgomes): EffectControlLinearizer::LowerTransitionElementsKind
    // does not handle exceptions.
    FATAL(
        "Fatal JavaScript invalid size error when transitioning elements kind");
    UNREACHABLE();
  }
  return *object;
}

RUNTIME_FUNCTION(Runtime_TransitionElementsKindWithKind) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<JSObject> object = args.at<JSObject>(0);
  ElementsKind to_kind = static_cast<ElementsKind>(args.smi_value_at(1));
  JSObject::TransitionElementsKind(object, to_kind);
  return *object;
}

RUNTIME_FUNCTION(Runtime_NewArray) {
  HandleScope scope(isolate);
  DCHECK_LE(3, args.length());
  int const argc = args.length() - 3;
  // argv points to the arguments constructed by the JavaScript call.
  JavaScriptArguments argv(argc, args.address_of_arg_at(0));
  Handle<JSFunction> constructor = args.at<JSFunction>(argc);
  Handle<JSReceiver> new_target = args.at<JSReceiver>(argc + 1);
  Handle<HeapObject> type_info = args.at<HeapObject>(argc + 2);
  // TODO(bmeurer): Use MaybeHandle to pass around the AllocationSite.
  Handle<AllocationSite> site = IsAllocationSite(*type_info)
                                    ? Cast<AllocationSite>(type_info)
                                    : Handle<AllocationSite>::null();

  Factory* factory = isolate->factory();

  // If called through new, new.target can be:
  // - a subclass of constructor,
  // - a proxy wrapper around constructor, or
  // - the constructor itself.
  // If called through Reflect.construct, it's guaranteed to be a constructor by
  // REFLECT_CONSTRUCT_PREPARE.
  DCHECK(IsConstructor(*new_target));

  bool holey = false;
  bool can_use_type_feedback = !site.is_null();
  bool can_inline_array_constructor = true;
  if (argv.length() == 1) {
    DirectHandle<Object> argument_one = argv.at<Object>(0);
    if (IsSmi(*argument_one)) {
      int value = Cast<Smi>(*argument_one).value();
      if (value < 0 ||
          JSArray::SetLengthWouldNormalize(isolate->heap(), value)) {
        // the array is a dictionary in this case.
        can_use_type_feedback = false;
      } else if (value != 0) {
        holey = true;
        if (value >= JSArray::kInitialMaxFastElementArray) {
          can_inline_array_constructor = false;
        }
      }
    } else {
      // Non-smi length argument produces a dictionary
      can_use_type_feedback = false;
    }
  }

  Handle<Map> initial_map;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, initial_map,
      JSFunction::GetDerivedMap(isolate, constructor, new_target));

  ElementsKind to_kind = can_use_type_feedback ? site->GetElementsKind()
                                               : initial_map->elements_kind();
  if (holey && !IsHoleyElementsKind(to_kind)) {
    to_kind = GetHoleyElementsKind(to_kind);
    // Update the allocation site info to reflect the advice alteration.
    if (!site.is_null()) site->SetElementsKind(to_kind);
  }

  // We should allocate with an initial map that reflects the allocation site
  // advice. Therefore we use AllocateJSObjectFromMap instead of passing
  // the constructor.
  initial_map = Map::AsElementsKind(isolate, initial_map, to_kind);

  // If we don't care to track arrays of to_kind ElementsKind, then
  // don't emit a memento for them.
  DirectHandle<AllocationSite> allocation_site;
  if (AllocationSite::ShouldTrack(to_kind)) {
    allocation_site = site;
  }

  Handle<JSArray> array = Cast<JSArray>(factory->NewJSObjectFromMap(
      initial_map, AllocationType::kYoung, allocation_site));

  factory->NewJSArrayStorage(
      array, 0, 0, ArrayStorageAllocationMode::DONT_INITIALIZE_ARRAY_ELEMENTS);

  ElementsKind old_kind = array->GetElementsKind();
  RETURN_FAILURE_ON_EXCEPTION(isolate,
                              ArrayConstructInitializeElements(array, &argv));
  if (!site.is_null()) {
    if ((old_kind != array->GetElementsKind() || !can_use_type_feedback ||
         !can_inline_array_constructor)) {
      // The arguments passed in caused a transition. This kind of complexity
      // can't be dealt with in the inlined optimized array constructor case.
      // We must mark the allocationsite as un-inlinable.
      site->SetDoNotInlineCall();
    }
  } else {
    if (old_kind != array->GetElementsKind() || !can_inline_array_constructor) {
      // We don't have an AllocationSite for this Array constructor invocation,
      // i.e. it might a call from Array#map or from an Array subclass, so we
      // just flip the bit on the global protector cell instead.
      // TODO(bmeurer): Find a better way to mark this. Global protectors
      // tend to back-fire over time...
      if (Protectors::IsArrayConstructorIntact(isolate)) {
        Protectors::InvalidateArrayConstructor(isolate);
      }
    }
  }

  return *array;
}

RUNTIME_FUNCTION(Runtime_NormalizeElements) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSObject> array = args.at<JSObject>(0);
  CHECK(!array->HasTypedArrayOrRabGsabTypedArrayElements());
  CHECK(!IsJSGlobalProxy(*array));
  JSObject::NormalizeElements(array);
  return *array;
}

// GrowArrayElements grows fast kind elements and returns a sentinel Smi if the
// object was normalized or if the key is negative.
RUNTIME_FUNCTION(Runtime_GrowArrayElements) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<JSObject> object = args.at<JSObject>(0);
  DirectHandle<Object> key = args.at(1);
  ElementsKind kind = object->GetElementsKind();
  CHECK(IsFastElementsKind(kind));
  uint32_t index;
  if (IsSmi(*key)) {
    int value = Smi::ToInt(*key);
    if (value < 0) return Smi::zero();
    index = static_cast<uint32_t>(value);
  } else {
    CHECK(IsHeapNumber(*key));
    double value = Cast<HeapNumber>(*key)->value();
    if (value < 0 || value > std::numeric_limits<uint32_t>::max()) {
      return Smi::zero();
    }
    index = static_cast<uint32_t>(value);
  }

  uint32_t capacity = static_cast<uint32_t>(object->elements()->length());

  if (index >= capacity) {
    bool has_grown;
    MAYBE_ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, has_grown,
        object->GetElementsAccessor()->GrowCapacity(object, index));
    if (!has_grown) {
      return Smi::zero();
    }
  }

  return object->elements();
}

// ES6 22.1.2.2 Array.isArray
RUNTIME_FUNCTION(Runtime_ArrayIsArray) {
  HandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  Maybe<bool> result = Object::IsArray(object);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return isolate->heap()->ToBoolean(result.FromJust());
}

RUNTIME_FUNCTION(Runtime_IsArray) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(1, args.length());
  Tagged<Object> obj = args[0];
  return isolate->heap()->ToBoolean(IsJSArray(obj));
}

RUNTIME_FUNCTION(Runtime_ArraySpeciesConstructor) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> original_array = args.at(0);
  RETURN_RESULT_OR_FAILURE(
      isolate, Object::ArraySpeciesConstructor(isolate, original_array));
}

// ES7 22.1.3.11 Array.prototype.includes
RUNTIME_FUNCTION(Runtime_ArrayIncludes_Slow) {
  HandleScope shs(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> search_element = args.at(1);
  Handle<Object> from_index = args.at(2);

  // Let O be ? ToObject(this value).
  Handle<JSReceiver> object;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, object,
      Object::ToObject(isolate, Handle<Object>(args[0], isolate)));

  // Let len be ? ToLength(? Get(O, "length")).
  int64_t len;
  {
    if (object->map()->instance_type() == JS_ARRAY_TYPE) {
      uint32_t len32 = 0;
      bool success =
          Object::ToArrayLength(Cast<JSArray>(*object)->length(), &len32);
      DCHECK(success);
      USE(success);
      len = len32;
    } else {
      Handle<Object> len_;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, len_,
          Object::GetProperty(isolate, object,
                              isolate->factory()->length_string()));

      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, len_,
                                         Object::ToLength(isolate, len_));
      len = static_cast<int64_t>(Object::NumberValue(*len_));
      DCHECK_EQ(len, Object::NumberValue(*len_));
    }
  }

  if (len == 0) return ReadOnlyRoots(isolate).false_value();

  // Let n be ? ToInteger(fromIndex). (If fromIndex is undefined, this step
  // produces the value 0.)
  int64_t index = 0;
  if (!IsUndefined(*from_index, isolate)) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, from_index,
                                       Object::ToInteger(isolate, from_index));

    if (V8_LIKELY(IsSmi(*from_index))) {
      int start_from = Smi::ToInt(*from_index);
      if (start_from < 0) {
        index = std::max<int64_t>(len + start_from, 0);
      } else {
        index = start_from;
      }
    } else {
      DCHECK(IsHeapNumber(*from_index));
      double start_from = Object::NumberValue(*from_index);
      if (start_from >= len) return ReadOnlyRoots(isolate).false_value();
      if (V8_LIKELY(std::isfinite(start_from))) {
        if (start_from < 0) {
          index = static_cast<int64_t>(std::max<double>(start_from + len, 0));
        } else {
          index = start_from;
        }
      }
    }

    DCHECK_GE(index, 0);
  }

  // If the receiver is not a special receiver type, and the length is a valid
  // element index, perform fast operation tailored to specific ElementsKinds.
  if (!IsSpecialReceiverMap(object->map()) &&
      len <= JSObject::kMaxElementCount &&
      JSObject::PrototypeHasNoElements(isolate, Cast<JSObject>(*object))) {
    Handle<JSObject> obj = Cast<JSObject>(object);
    ElementsAccessor* elements = obj->GetElementsAccessor();
    Maybe<bool> result =
        elements->IncludesValue(isolate, obj, search_element, index, len);
    MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
    return *isolate->factory()->ToBoolean(result.FromJust());
  }

  // Otherwise, perform slow lookups for special receiver types.
  for (; index < len; ++index) {
    HandleScope iteration_hs(isolate);

    // Let elementK be the result of ? Get(O, ! ToString(k)).
    Handle<Object> element_k;
    {
      PropertyKey key(isolate, static_cast<double>(index));
      LookupIterator it(isolate, object, key);
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, element_k,
                                         Object::GetProperty(&it));
    }

    // If SameValueZero(searchElement, elementK) is true, return true.
    if (Object::SameValueZero(*search_element, *element_k)) {
      return ReadOnlyRoots(isolate).true_value();
    }
  }
  return ReadOnlyRoots(isolate).false_value();
}

RUNTIME_FUNCTION(Runtime_ArrayIndexOf) {
  HandleScope hs(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> search_element = args.at(1);
  Handle<Object> from_index = args.at(2);

  // Let O be ? ToObject(this value).
  Handle<JSReceiver> object;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, object,
      Object::ToObject(isolate, args.at(0), "Array.prototype.indexOf"));

  // Let len be ? ToLength(? Get(O, "length")).
  int64_t len;
  {
    if (IsJSArray(*object)) {
      uint32_t len32 = 0;
      bool success =
          Object::ToArrayLength(Cast<JSArray>(*object)->length(), &len32);
      DCHECK(success);
      USE(success);
      len = len32;
    } else {
      Handle<Object> len_;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, len_,
          Object::GetProperty(isolate, object,
                              isolate->factory()->length_string()));

      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, len_,
                                         Object::ToLength(isolate, len_));
      len = static_cast<int64_t>(Object::NumberValue(*len_));
      DCHECK_EQ(len, Object::NumberValue(*len_));
    }
  }

  if (len == 0) return Smi::FromInt(-1);

  // Let n be ? ToInteger(fromIndex). (If fromIndex is undefined, this step
  // produces the value 0.)
  int64_t start_from;
  {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, from_index,
                                       Object::ToInteger(isolate, from_index));
    double fp = Object::NumberValue(*from_index);
    if (fp > len) return Smi::FromInt(-1);
    if (V8_LIKELY(fp >=
                  static_cast<double>(std::numeric_limits<int64_t>::min()))) {
      DCHECK(fp < static_cast<double>(std::numeric_limits<int64_t>::max()));
      start_from = static_cast<int64_t>(fp);
    } else {
      start_from = std::numeric_limits<int64_t>::min();
    }
  }

  int64_t index;
  if (start_from >= 0) {
    index = start_from;
  } else {
    index = len + start_from;
    if (index < 0) {
      index = 0;
    }
  }

  // If the receiver is not a special receiver type, and the length fits
  // uint32_t, perform fast operation tailored to specific ElementsKinds.
  if (!IsSpecialReceiverMap(object->map()) && len <= kMaxUInt32 &&
      JSObject::PrototypeHasNoElements(isolate, Cast<JSObject>(*object))) {
    Handle<JSObject> obj = Cast<JSObject>(object);
    ElementsAccessor* elements = obj->GetElementsAccessor();
    Maybe<int64_t> result = elements->IndexOfValue(isolate, obj, search_element,
                                                   static_cast<uint32_t>(index),
                                                   static_cast<uint32_t>(len));
    MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
    return *isolate->factory()->NewNumberFromInt64(result.FromJust());
  }

  // Otherwise, perform slow lookups for special receiver types.
  for (; index < len; ++index) {
    HandleScope iteration_hs(isolate);
    // Let elementK be the result of ? Get(O, ! ToString(k)).
    Handle<Object> element_k;
    {
      PropertyKey key(isolate, static_cast<double>(index));
      LookupIterator it(isolate, object, key);
      Maybe<bool> present = JSReceiver::HasProperty(&it);
      MAYBE_RETURN(present, ReadOnlyRoots(isolate).exception());
      if (!present.FromJust()) continue;
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, element_k,
                                         Object::GetProperty(&it));
      if (Object::StrictEquals(*search_element, *element_k)) {
        return *isolate->factory()->NewNumberFromInt64(index);
      }
    }
  }
  return Smi::FromInt(-1);
}

}  // namespace internal
}  // namespace v8
