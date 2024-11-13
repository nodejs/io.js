// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-factory.h"
#include "src/logging/counters.h"
#include "src/numbers/conversions.h"
#include "src/objects/objects-inl.h"
#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#endif

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 20.1 Number Objects

// ES6 section 20.1.3.2 Number.prototype.toExponential ( fractionDigits )
BUILTIN(NumberPrototypeToExponential) {
  HandleScope scope(isolate);
  Handle<Object> value = args.at(0);
  Handle<Object> fraction_digits = args.atOrUndefined(isolate, 1);

  // Unwrap the receiver {value}.
  if (IsJSPrimitiveWrapper(*value)) {
    value = handle(Cast<JSPrimitiveWrapper>(value)->value(), isolate);
  }
  if (!IsNumber(*value)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotGeneric,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Number.prototype.toExponential"),
                              isolate->factory()->Number_string()));
  }
  double const value_number = Object::NumberValue(*value);

  // Convert the {fraction_digits} to an integer first.
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, fraction_digits, Object::ToInteger(isolate, fraction_digits));
  double const fraction_digits_number = Object::NumberValue(*fraction_digits);

  if (std::isnan(value_number)) return ReadOnlyRoots(isolate).NaN_string();
  if (std::isinf(value_number)) {
    return (value_number < 0.0) ? ReadOnlyRoots(isolate).minus_Infinity_string()
                                : ReadOnlyRoots(isolate).Infinity_string();
  }
  if (fraction_digits_number < 0.0 ||
      fraction_digits_number > kMaxFractionDigits) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kNumberFormatRange,
                               isolate->factory()->NewStringFromAsciiChecked(
                                   "toExponential()")));
  }
  int const f = IsUndefined(*args.atOrUndefined(isolate, 1), isolate)
                    ? -1
                    : static_cast<int>(fraction_digits_number);
  char* const str = DoubleToExponentialCString(value_number, f);
  DirectHandle<String> result =
      isolate->factory()->NewStringFromAsciiChecked(str);
  DeleteArray(str);
  return *result;
}

// ES6 section 20.1.3.3 Number.prototype.toFixed ( fractionDigits )
BUILTIN(NumberPrototypeToFixed) {
  HandleScope scope(isolate);
  Handle<Object> value = args.at(0);
  Handle<Object> fraction_digits = args.atOrUndefined(isolate, 1);

  // Unwrap the receiver {value}.
  if (IsJSPrimitiveWrapper(*value)) {
    value = handle(Cast<JSPrimitiveWrapper>(value)->value(), isolate);
  }
  if (!IsNumber(*value)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotGeneric,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Number.prototype.toFixed"),
                              isolate->factory()->Number_string()));
  }
  double const value_number = Object::NumberValue(*value);

  // Convert the {fraction_digits} to an integer first.
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, fraction_digits, Object::ToInteger(isolate, fraction_digits));
  double const fraction_digits_number = Object::NumberValue(*fraction_digits);

  // Check if the {fraction_digits} are in the supported range.
  if (fraction_digits_number < 0.0 ||
      fraction_digits_number > kMaxFractionDigits) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kNumberFormatRange,
                               isolate->factory()->NewStringFromAsciiChecked(
                                   "toFixed() digits")));
  }

  if (std::isnan(value_number)) return ReadOnlyRoots(isolate).NaN_string();
  if (std::isinf(value_number)) {
    return (value_number < 0.0) ? ReadOnlyRoots(isolate).minus_Infinity_string()
                                : ReadOnlyRoots(isolate).Infinity_string();
  }
  char* const str = DoubleToFixedCString(
      value_number, static_cast<int>(fraction_digits_number));
  DirectHandle<String> result =
      isolate->factory()->NewStringFromAsciiChecked(str);
  DeleteArray(str);
  return *result;
}

// ES6 section 20.1.3.4 Number.prototype.toLocaleString ( [ r1 [ , r2 ] ] )
BUILTIN(NumberPrototypeToLocaleString) {
  HandleScope scope(isolate);
  const char* method_name = "Number.prototype.toLocaleString";

  isolate->CountUsage(v8::Isolate::UseCounterFeature::kNumberToLocaleString);

  Handle<Object> value = args.at(0);

  // Unwrap the receiver {value}.
  if (IsJSPrimitiveWrapper(*value)) {
    value = handle(Cast<JSPrimitiveWrapper>(value)->value(), isolate);
  }
  // 1. Let x be ? thisNumberValue(this value)
  if (!IsNumber(*value)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kNotGeneric,
                     isolate->factory()->NewStringFromAsciiChecked(method_name),
                     isolate->factory()->Number_string()));
  }

#ifdef V8_INTL_SUPPORT
  RETURN_RESULT_OR_FAILURE(
      isolate,
      Intl::NumberToLocaleString(isolate, value, args.atOrUndefined(isolate, 1),
                                 args.atOrUndefined(isolate, 2), method_name));
#else
  // Turn the {value} into a String.
  return *isolate->factory()->NumberToString(value);
#endif  // V8_INTL_SUPPORT
}

// ES6 section 20.1.3.5 Number.prototype.toPrecision ( precision )
BUILTIN(NumberPrototypeToPrecision) {
  HandleScope scope(isolate);
  Handle<Object> value = args.at(0);
  Handle<Object> precision = args.atOrUndefined(isolate, 1);

  // Unwrap the receiver {value}.
  if (IsJSPrimitiveWrapper(*value)) {
    value = handle(Cast<JSPrimitiveWrapper>(value)->value(), isolate);
  }
  if (!IsNumber(*value)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotGeneric,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Number.prototype.toPrecision"),
                              isolate->factory()->Number_string()));
  }
  double const value_number = Object::NumberValue(*value);

  // If no {precision} was specified, just return ToString of {value}.
  if (IsUndefined(*precision, isolate)) {
    return *isolate->factory()->NumberToString(value);
  }

  // Convert the {precision} to an integer first.
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, precision,
                                     Object::ToInteger(isolate, precision));
  double const precision_number = Object::NumberValue(*precision);

  if (std::isnan(value_number)) return ReadOnlyRoots(isolate).NaN_string();
  if (std::isinf(value_number)) {
    return (value_number < 0.0) ? ReadOnlyRoots(isolate).minus_Infinity_string()
                                : ReadOnlyRoots(isolate).Infinity_string();
  }
  if (precision_number < 1.0 || precision_number > kMaxFractionDigits) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kToPrecisionFormatRange));
  }
  char* const str = DoubleToPrecisionCString(
      value_number, static_cast<int>(precision_number));
  DirectHandle<String> result =
      isolate->factory()->NewStringFromAsciiChecked(str);
  DeleteArray(str);
  return *result;
}

}  // namespace internal
}  // namespace v8
