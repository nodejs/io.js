// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-deep-serializer.h"

#include <memory>

#include "include/v8-container.h"
#include "include/v8-context.h"
#include "include/v8-date.h"
#include "include/v8-exception.h"
#include "include/v8-regexp.h"
#include "src/inspector/protocol/Runtime.h"
#include "src/inspector/v8-serialization-duplicate-tracker.h"
#include "src/inspector/value-mirror.h"

namespace v8_inspector {

namespace {
using protocol::Response;
std::unique_ptr<protocol::Value> DescriptionForDate(
    v8::Local<v8::Context> context, v8::Local<v8::Date> date) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::TryCatch tryCatch(isolate);

  v8::Local<v8::String> dateISOString = date->ToISOString();
  return protocol::StringValue::create(
      toProtocolString(isolate, dateISOString));
}

String16 DescriptionForRegExpFlags(v8::Local<v8::RegExp> value) {
  String16Builder resultStringBuilder;
  v8::RegExp::Flags flags = value->GetFlags();
  if (flags & v8::RegExp::Flags::kHasIndices) resultStringBuilder.append('d');
  if (flags & v8::RegExp::Flags::kGlobal) resultStringBuilder.append('g');
  if (flags & v8::RegExp::Flags::kIgnoreCase) resultStringBuilder.append('i');
  if (flags & v8::RegExp::Flags::kLinear) resultStringBuilder.append('l');
  if (flags & v8::RegExp::Flags::kMultiline) resultStringBuilder.append('m');
  if (flags & v8::RegExp::Flags::kDotAll) resultStringBuilder.append('s');
  if (flags & v8::RegExp::Flags::kUnicode) resultStringBuilder.append('u');
  if (flags & v8::RegExp::Flags::kUnicodeSets) {
    resultStringBuilder.append('v');
  }
  if (flags & v8::RegExp::Flags::kSticky) resultStringBuilder.append('y');
  return resultStringBuilder.toString();
}

Response SerializeRegexp(v8::Local<v8::RegExp> value,
                         v8::Local<v8::Context> context,
                         V8SerializationDuplicateTracker& duplicateTracker,
                         protocol::DictionaryValue& result) {
  result.setString("type",
                   protocol::Runtime::DeepSerializedValue::TypeEnum::Regexp);

  std::unique_ptr<protocol::DictionaryValue> resultValue =
      protocol::DictionaryValue::create();

  resultValue->setValue(protocol::String("pattern"),
                        protocol::StringValue::create(toProtocolString(
                            context->GetIsolate(), value->GetSource())));

  String16 flags = DescriptionForRegExpFlags(value);
  if (!flags.isEmpty()) {
    resultValue->setValue(protocol::String("flags"),
                          protocol::StringValue::create(flags));
  }

  result.setValue("value", std::move(resultValue));
  return Response::Success();
}

Response SerializeDate(v8::Local<v8::Date> value,
                       v8::Local<v8::Context> context,
                       V8SerializationDuplicateTracker& duplicateTracker,
                       protocol::DictionaryValue& result) {
  result.setString("type",
                   protocol::Runtime::DeepSerializedValue::TypeEnum::Date);
  std::unique_ptr<protocol::Value> dateDescription =
      DescriptionForDate(context, value.As<v8::Date>());

  result.setValue("value", std::move(dateDescription));
  return Response::Success();
}

Response SerializeArrayValue(v8::Local<v8::Array> value,
                             v8::Local<v8::Context> context, int maxDepth,
                             v8::Local<v8::Object> additionalParameters,
                             V8SerializationDuplicateTracker& duplicateTracker,
                             std::unique_ptr<protocol::ListValue>* result) {
  std::unique_ptr<protocol::ListValue> serializedValue =
      protocol::ListValue::create();
  uint32_t length = value->Length();
  serializedValue->reserve(length);
  for (uint32_t i = 0; i < length; i++) {
    v8::Local<v8::Value> elementValue;
    bool success = value->Get(context, i).ToLocal(&elementValue);
    CHECK(success);
    USE(success);

    std::unique_ptr<protocol::DictionaryValue> elementProtocolValue;
    Response response = ValueMirror::create(context, elementValue)
                            ->buildDeepSerializedValue(
                                context, maxDepth - 1, additionalParameters,
                                duplicateTracker, &elementProtocolValue);
    if (!response.IsSuccess()) return response;
    serializedValue->pushValue(std::move(elementProtocolValue));
  }
  *result = std::move(serializedValue);
  return Response::Success();
}

Response SerializeArray(v8::Local<v8::Array> value,
                        v8::Local<v8::Context> context, int maxDepth,
                        v8::Local<v8::Object> additionalParameters,
                        V8SerializationDuplicateTracker& duplicateTracker,
                        protocol::DictionaryValue& result) {
  result.setString("type",
                   protocol::Runtime::DeepSerializedValue::TypeEnum::Array);

  if (maxDepth > 0) {
    std::unique_ptr<protocol::ListValue> serializedValue;
    Response response =
        SerializeArrayValue(value, context, maxDepth, additionalParameters,
                            duplicateTracker, &serializedValue);
    if (!response.IsSuccess()) return response;

    result.setValue("value", std::move(serializedValue));
  }

  return Response::Success();
}

Response SerializeMap(v8::Local<v8::Map> value, v8::Local<v8::Context> context,
                      int maxDepth, v8::Local<v8::Object> additionalParameters,
                      V8SerializationDuplicateTracker& duplicateTracker,
                      protocol::DictionaryValue& result) {
  result.setString("type",
                   protocol::Runtime::DeepSerializedValue::TypeEnum::Map);

  if (maxDepth > 0) {
    std::unique_ptr<protocol::ListValue> serializedValue =
        protocol::ListValue::create();

    v8::Local<v8::Array> propertiesAndValues = value->AsArray();

    uint32_t length = propertiesAndValues->Length();
    serializedValue->reserve(length);
    for (uint32_t i = 0; i < length; i += 2) {
      v8::Local<v8::Value> keyV8Value, propertyV8Value;
      std::unique_ptr<protocol::Value> keyProtocolValue;
      std::unique_ptr<protocol::DictionaryValue> propertyProtocolValue;

      bool success = propertiesAndValues->Get(context, i).ToLocal(&keyV8Value);
      CHECK(success);
      success =
          propertiesAndValues->Get(context, i + 1).ToLocal(&propertyV8Value);
      CHECK(success);
      USE(success);

      if (keyV8Value->IsString()) {
        keyProtocolValue = protocol::StringValue::create(toProtocolString(
            context->GetIsolate(), keyV8Value.As<v8::String>()));
      } else {
        std::unique_ptr<protocol::DictionaryValue> keyDictionaryProtocolValue;
        Response response =
            ValueMirror::create(context, keyV8Value)
                ->buildDeepSerializedValue(
                    context, maxDepth - 1, additionalParameters,
                    duplicateTracker, &keyDictionaryProtocolValue);
        if (!response.IsSuccess()) return response;
        keyProtocolValue = std::move(keyDictionaryProtocolValue);
      }

      Response response = ValueMirror::create(context, propertyV8Value)
                              ->buildDeepSerializedValue(
                                  context, maxDepth - 1, additionalParameters,
                                  duplicateTracker, &propertyProtocolValue);
      if (!response.IsSuccess()) return response;

      std::unique_ptr<protocol::ListValue> keyValueList =
          protocol::ListValue::create();

      keyValueList->pushValue(std::move(keyProtocolValue));
      keyValueList->pushValue(std::move(propertyProtocolValue));

      serializedValue->pushValue(std::move(keyValueList));
    }
    result.setValue("value", std::move(serializedValue));
  }

  return Response::Success();
}

Response SerializeSet(v8::Local<v8::Set> value, v8::Local<v8::Context> context,
                      int maxDepth, v8::Local<v8::Object> additionalParameters,
                      V8SerializationDuplicateTracker& duplicateTracker,
                      protocol::DictionaryValue& result) {
  result.setString("type",
                   protocol::Runtime::DeepSerializedValue::TypeEnum::Set);

  if (maxDepth > 0) {
    std::unique_ptr<protocol::ListValue> serializedValue;
    Response response = SerializeArrayValue(value->AsArray(), context, maxDepth,
                                            additionalParameters,

                                            duplicateTracker, &serializedValue);
    result.setValue("value", std::move(serializedValue));
  }
  return Response::Success();
}

Response SerializeObjectValue(v8::Local<v8::Object> value,
                              v8::Local<v8::Context> context, int maxDepth,
                              v8::Local<v8::Object> additionalParameters,
                              V8SerializationDuplicateTracker& duplicateTracker,
                              std::unique_ptr<protocol::ListValue>* result) {
  std::unique_ptr<protocol::ListValue> serializedValue =
      protocol::ListValue::create();
  // Iterate through object's enumerable properties ignoring symbols.
  v8::Local<v8::Array> propertyNames;
  bool success =
      value
          ->GetOwnPropertyNames(context,
                                static_cast<v8::PropertyFilter>(
                                    v8::PropertyFilter::ONLY_ENUMERABLE |
                                    v8::PropertyFilter::SKIP_SYMBOLS),
                                v8::KeyConversionMode::kConvertToString)
          .ToLocal(&propertyNames);
  CHECK(success);

  uint32_t length = propertyNames->Length();
  serializedValue->reserve(length);
  for (uint32_t i = 0; i < length; i++) {
    v8::Local<v8::Value> keyV8Value, propertyV8Value;
    std::unique_ptr<protocol::Value> keyProtocolValue;
    std::unique_ptr<protocol::DictionaryValue> propertyProtocolValue;

    success = propertyNames->Get(context, i).ToLocal(&keyV8Value);
    CHECK(success);
    CHECK(keyV8Value->IsString());

    v8::Maybe<bool> hasRealNamedProperty =
        value->HasRealNamedProperty(context, keyV8Value.As<v8::String>());
    // Don't access properties with interceptors.
    if (hasRealNamedProperty.IsNothing() || !hasRealNamedProperty.FromJust()) {
      continue;
    }
    keyProtocolValue = protocol::StringValue::create(
        toProtocolString(context->GetIsolate(), keyV8Value.As<v8::String>()));

    success = value->Get(context, keyV8Value).ToLocal(&propertyV8Value);
    CHECK(success);
    USE(success);

    Response response = ValueMirror::create(context, propertyV8Value)
                            ->buildDeepSerializedValue(
                                context, maxDepth - 1, additionalParameters,
                                duplicateTracker, &propertyProtocolValue);
    if (!response.IsSuccess()) return response;

    std::unique_ptr<protocol::ListValue> keyValueList =
        protocol::ListValue::create();

    keyValueList->pushValue(std::move(keyProtocolValue));
    keyValueList->pushValue(std::move(propertyProtocolValue));

    serializedValue->pushValue(std::move(keyValueList));
  }
  *result = std::move(serializedValue);
  return Response::Success();
}

Response SerializeObject(v8::Local<v8::Object> value,
                         v8::Local<v8::Context> context, int maxDepth,
                         v8::Local<v8::Object> additionalParameters,
                         V8SerializationDuplicateTracker& duplicateTracker,
                         protocol::DictionaryValue& result) {
  result.setString("type",
                   protocol::Runtime::DeepSerializedValue::TypeEnum::Object);

  if (maxDepth > 0) {
    std::unique_ptr<protocol::ListValue> serializedValue;
    Response response = SerializeObjectValue(
        value.As<v8::Object>(), context, maxDepth, additionalParameters,
        duplicateTracker, &serializedValue);
    if (!response.IsSuccess()) return response;
    result.setValue("value", std::move(serializedValue));
  }
  return Response::Success();
}
}  // namespace

Response V8DeepSerializer::serializeV8Value(
    v8::Local<v8::Object> value, v8::Local<v8::Context> context, int maxDepth,
    v8::Local<v8::Object> additionalParameters,
    V8SerializationDuplicateTracker& duplicateTracker,
    protocol::DictionaryValue& result) {
  if (value->IsArray()) {
    return SerializeArray(value.As<v8::Array>(), context, maxDepth,
                          additionalParameters, duplicateTracker, result);
  }
  if (value->IsRegExp()) {
    return SerializeRegexp(value.As<v8::RegExp>(), context, duplicateTracker,
                           result);
  }
  if (value->IsDate()) {
    return SerializeDate(value.As<v8::Date>(), context, duplicateTracker,
                         result);
  }
  if (value->IsMap()) {
    return SerializeMap(value.As<v8::Map>(), context, maxDepth,
                        additionalParameters, duplicateTracker, result);
  }
  if (value->IsSet()) {
    return SerializeSet(value.As<v8::Set>(), context, maxDepth,
                        additionalParameters, duplicateTracker, result);
  }
  if (value->IsWeakMap()) {
    result.setString("type",
                     protocol::Runtime::DeepSerializedValue::TypeEnum::Weakmap);
    return Response::Success();
  }
  if (value->IsWeakSet()) {
    result.setString("type",
                     protocol::Runtime::DeepSerializedValue::TypeEnum::Weakset);
    return Response::Success();
  }
  if (value->IsNativeError()) {
    result.setString("type",
                     protocol::Runtime::DeepSerializedValue::TypeEnum::Error);
    return Response::Success();
  }
  if (value->IsProxy()) {
    result.setString("type",
                     protocol::Runtime::DeepSerializedValue::TypeEnum::Proxy);
    return Response::Success();
  }
  if (value->IsPromise()) {
    result.setString("type",
                     protocol::Runtime::DeepSerializedValue::TypeEnum::Promise);
    return Response::Success();
  }
  if (value->IsTypedArray()) {
    result.setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Typedarray);
    return Response::Success();
  }
  if (value->IsArrayBuffer()) {
    result.setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Arraybuffer);
    return Response::Success();
  }
  if (value->IsFunction()) {
    result.setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Function);
    return Response::Success();
  }
  if (value->IsGeneratorObject()) {
    result.setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Generator);
    return Response::Success();
  }

  // Serialize as an Object.
  return SerializeObject(value.As<v8::Object>(), context, maxDepth,
                         additionalParameters, duplicateTracker, result);
}

}  // namespace v8_inspector
