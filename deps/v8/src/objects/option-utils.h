// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OPTION_UTILS_H_
#define V8_OBJECTS_OPTION_UTILS_H_

#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/objects/js-objects.h"
#include "src/objects/string.h"

namespace v8 {
namespace internal {

// ecma402/#sec-getoptionsobject and temporal/#sec-getoptionsobject
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSReceiver> GetOptionsObject(
    Isolate* isolate, DirectHandle<Object> options, const char* method_name);

// ecma402/#sec-coerceoptionstoobject
V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSReceiver> CoerceOptionsToObject(
    Isolate* isolate, DirectHandle<Object> options, const char* method_name);

// ECMA402 9.2.10. GetOption( options, property, type, values, fallback)
// ecma402/#sec-getoption and temporal/#sec-getoption
//
// This is specialized for the case when type is string.
//
// Instead of passing undefined for the values argument as the spec
// defines, pass in an empty vector.
//
// Returns true if options object has the property and stores the
// result in value. Returns false if the value is not found. The
// caller is required to use fallback value appropriately in this
// case.
//
// method_name is a string denoting the method the call from; used when
// printing the error message.
V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Maybe<bool> GetStringOption(
    Isolate* isolate, DirectHandle<JSReceiver> options, const char* property,
    const std::vector<const char*>& values, const char* method_name,
    std::unique_ptr<char[]>* result);

// A helper template to get string from option into a enum.
// The enum in the enum_values is the corresponding value to the strings
// in the str_values. If the option does not contains name,
// default_value will be return.
template <typename T>
V8_WARN_UNUSED_RESULT static Maybe<T> GetStringOption(
    Isolate* isolate, DirectHandle<JSReceiver> options, const char* name,
    const char* method_name, const std::vector<const char*>& str_values,
    const std::vector<T>& enum_values, T default_value) {
  DCHECK_EQ(str_values.size(), enum_values.size());
  std::unique_ptr<char[]> cstr;
  Maybe<bool> found =
      GetStringOption(isolate, options, name, str_values, method_name, &cstr);
  MAYBE_RETURN(found, Nothing<T>());
  if (found.FromJust()) {
    DCHECK_NOT_NULL(cstr.get());
    for (size_t i = 0; i < str_values.size(); i++) {
      if (strcmp(cstr.get(), str_values[i]) == 0) {
        return Just(enum_values[i]);
      }
    }
    UNREACHABLE();
  }
  return Just(default_value);
}

// A helper template to get string from option into a enum.
// The enum in the enum_values is the corresponding value to the strings
// in the str_values. If the option does not contains name,
// default_value will be return.
template <typename T>
V8_WARN_UNUSED_RESULT static Maybe<T> GetStringOrBooleanOption(
    Isolate* isolate, DirectHandle<JSReceiver> options, const char* property,
    const char* method, const std::vector<const char*>& str_values,
    const std::vector<T>& enum_values, T true_value, T false_value,
    T fallback_value) {
  DCHECK_EQ(str_values.size(), enum_values.size());
  Factory* factory = isolate->factory();
  DirectHandle<String> property_str =
      factory->NewStringFromAsciiChecked(property);

  // 1. Let value be ? Get(options, property).
  DirectHandle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value,
      Object::GetPropertyOrElement(isolate, options, property_str),
      Nothing<T>());
  // 2. If value is undefined, then return fallback.
  if (IsUndefined(*value, isolate)) {
    return Just(fallback_value);
  }
  // 3. If value is true, then return trueValue.
  if (IsTrue(*value, isolate)) {
    return Just(true_value);
  }
  // 4. Let valueBoolean be ToBoolean(value).
  bool valueBoolean = Object::BooleanValue(*value, isolate);
  // 5. If valueBoolean is false, then return valueBoolean.
  if (!valueBoolean) {
    return Just(false_value);
  }

  DirectHandle<String> value_str;
  // 6. Let value be ? ToString(value).
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value_str, Object::ToString(isolate, value), Nothing<T>());
  // 7. If value is *"true"* or *"false"*, return _fallback_.
  if (String::Equals(isolate, value_str, factory->true_string()) ||
      String::Equals(isolate, value_str, factory->false_string())) {
    return Just(fallback_value);
  }
  // 8. If values does not contain an element equal to _value_, throw a
  // *RangeError* exception.
  // 9. Return value.
  value_str = String::Flatten(isolate, value_str);
  {
    DisallowGarbageCollection no_gc;
    const String::FlatContent& flat = value_str->GetFlatContent(no_gc);
    int32_t length = value_str->length();
    for (size_t i = 0; i < str_values.size(); i++) {
      if (static_cast<int32_t>(strlen(str_values.at(i))) == length) {
        if (flat.IsOneByte()) {
          if (CompareCharsEqual(str_values.at(i),
                                flat.ToOneByteVector().begin(), length)) {
            return Just(enum_values[i]);
          }
        } else {
          if (CompareCharsEqual(str_values.at(i), flat.ToUC16Vector().begin(),
                                length)) {
            return Just(enum_values[i]);
          }
        }
      }
    }
  }  // end of no_gc
  THROW_NEW_ERROR_RETURN_VALUE(
      isolate,
      NewRangeError(MessageTemplate::kValueOutOfRange, value,
                    factory->NewStringFromAsciiChecked(method), property_str),
      Nothing<T>());
}

// ECMA402 9.2.10. GetOption( options, property, type, values, fallback)
// ecma402/#sec-getoption
//
// This is specialized for the case when type is boolean.
//
// Returns true if options object has the property and stores the
// result in value. Returns false if the value is not found. The
// caller is required to use fallback value appropriately in this
// case.
//
// method_name is a string denoting the method it called from; used when
// printing the error message.
V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Maybe<bool> GetBoolOption(
    Isolate* isolate, DirectHandle<JSReceiver> options, const char* property,
    const char* method_name, bool* result);

V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Maybe<int> GetNumberOption(
    Isolate* isolate, DirectHandle<JSReceiver> options,
    DirectHandle<String> property, int min, int max, int fallback);

// #sec-getoption while type is "number"
V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Maybe<double> GetNumberOptionAsDouble(
    Isolate* isolate, DirectHandle<JSReceiver> options,
    DirectHandle<String> property, double default_value);

// ecma402/#sec-defaultnumberoption
V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT Maybe<int> DefaultNumberOption(
    Isolate* isolate, DirectHandle<Object> value, int min, int max,
    int fallback, DirectHandle<String> property);

}  // namespace internal
}  // namespace v8
#endif  // V8_OBJECTS_OPTION_UTILS_H_
