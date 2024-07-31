// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/value-mirror.h"

#include <algorithm>
#include <cmath>

#include "include/v8-container.h"
#include "include/v8-date.h"
#include "include/v8-function.h"
#include "include/v8-microtask-queue.h"
#include "include/v8-primitive-object.h"
#include "include/v8-proxy.h"
#include "include/v8-regexp.h"
#include "include/v8-typed-array.h"
#include "include/v8-wasm.h"
#include "src/base/optional.h"
#include "src/debug/debug-interface.h"
#include "src/inspector/v8-debugger.h"
#include "src/inspector/v8-deep-serializer.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/inspector/v8-serialization-duplicate-tracker.h"

namespace v8_inspector {

using protocol::Response;
using protocol::Runtime::EntryPreview;
using protocol::Runtime::ObjectPreview;
using protocol::Runtime::PropertyPreview;
using protocol::Runtime::RemoteObject;

#if defined(V8_USE_ADDRESS_SANITIZER) && V8_OS_DARWIN
// For whatever reason, ASan on MacOS has bigger stack frames.
static const int kMaxProtocolDepth = 900;
#else
static const int kMaxProtocolDepth = 1000;
#endif

Response toProtocolValue(v8::Local<v8::Context> context,
                         v8::Local<v8::Value> value, int maxDepth,
                         std::unique_ptr<protocol::Value>* result);

Response arrayToProtocolValue(v8::Local<v8::Context> context,
                              v8::Local<v8::Array> array, int maxDepth,
                              std::unique_ptr<protocol::ListValue>* result) {
  std::unique_ptr<protocol::ListValue> inspectorArray =
      protocol::ListValue::create();
  uint32_t length = array->Length();
  for (uint32_t i = 0; i < length; i++) {
    v8::Local<v8::Value> value;
    if (!array->Get(context, i).ToLocal(&value))
      return Response::InternalError();
    std::unique_ptr<protocol::Value> element;
    Response response = toProtocolValue(context, value, maxDepth - 1, &element);
    if (!response.IsSuccess()) return response;
    inspectorArray->pushValue(std::move(element));
  }
  *result = std::move(inspectorArray);
  return Response::Success();
}

Response objectToProtocolValue(
    v8::Local<v8::Context> context, v8::Local<v8::Object> object, int maxDepth,
    std::unique_ptr<protocol::DictionaryValue>* result) {
  std::unique_ptr<protocol::DictionaryValue> jsonObject =
      protocol::DictionaryValue::create();
  v8::Local<v8::Array> propertyNames;
  if (!object->GetOwnPropertyNames(context).ToLocal(&propertyNames))
    return Response::InternalError();
  uint32_t length = propertyNames->Length();
  for (uint32_t i = 0; i < length; i++) {
    v8::Local<v8::Value> name;
    if (!propertyNames->Get(context, i).ToLocal(&name))
      return Response::InternalError();
    if (name->IsString()) {
      v8::Maybe<bool> hasRealNamedProperty =
          object->HasRealNamedProperty(context, name.As<v8::String>());
      // Don't access properties with interceptors.
      if (hasRealNamedProperty.IsNothing() || !hasRealNamedProperty.FromJust())
        continue;
    }
    v8::Local<v8::String> propertyName;
    if (!name->ToString(context).ToLocal(&propertyName)) continue;
    v8::Local<v8::Value> property;
    if (!object->Get(context, name).ToLocal(&property))
      return Response::InternalError();
    if (property->IsUndefined()) continue;
    std::unique_ptr<protocol::Value> propertyValue;
    Response response =
        toProtocolValue(context, property, maxDepth - 1, &propertyValue);
    if (!response.IsSuccess()) return response;
    jsonObject->setValue(toProtocolString(context->GetIsolate(), propertyName),
                         std::move(propertyValue));
  }
  *result = std::move(jsonObject);
  return Response::Success();
}

std::unique_ptr<protocol::FundamentalValue> toProtocolValue(
    double doubleValue) {
  if (doubleValue >= std::numeric_limits<int>::min() &&
      doubleValue <= std::numeric_limits<int>::max() &&
      v8::base::bit_cast<int64_t>(doubleValue) !=
          v8::base::bit_cast<int64_t>(-0.0)) {
    int intValue = static_cast<int>(doubleValue);
    if (intValue == doubleValue) {
      return protocol::FundamentalValue::create(intValue);
    }
  }
  return protocol::FundamentalValue::create(doubleValue);
}

Response toProtocolValue(v8::Local<v8::Context> context,
                         v8::Local<v8::Value> value, int maxDepth,
                         std::unique_ptr<protocol::Value>* result) {
  if (maxDepth <= 0)
    return Response::ServerError("Object reference chain is too long");

  if (value->IsNull() || value->IsUndefined()) {
    *result = protocol::Value::null();
    return Response::Success();
  }
  if (value->IsBoolean()) {
    *result =
        protocol::FundamentalValue::create(value.As<v8::Boolean>()->Value());
    return Response::Success();
  }
  if (value->IsNumber()) {
    double doubleValue = value.As<v8::Number>()->Value();
    *result = toProtocolValue(doubleValue);
    return Response::Success();
  }
  if (value->IsString()) {
    *result = protocol::StringValue::create(
        toProtocolString(context->GetIsolate(), value.As<v8::String>()));
    return Response::Success();
  }
  if (value->IsArray()) {
    v8::Local<v8::Array> array = value.As<v8::Array>();
    std::unique_ptr<protocol::ListValue> list_result;
    auto response =
        arrayToProtocolValue(context, array, maxDepth, &list_result);
    *result = std::move(list_result);
    return response;
  }
  if (value->IsObject()) {
    v8::Local<v8::Object> object = value.As<v8::Object>();
    std::unique_ptr<protocol::DictionaryValue> dict_result;
    auto response =
        objectToProtocolValue(context, object, maxDepth, &dict_result);
    *result = std::move(dict_result);
    return response;
  }

  return Response::ServerError("Object couldn't be returned by value");
}

Response toProtocolValue(v8::Local<v8::Context> context,
                         v8::Local<v8::Value> value,
                         std::unique_ptr<protocol::Value>* result) {
  if (value->IsUndefined()) return Response::Success();
  return toProtocolValue(context, value, kMaxProtocolDepth, result);
}

namespace {

// WebAssembly memory is organized in pages of size 64KiB.
const size_t kWasmPageSize = 64 * 1024;

V8InspectorClient* clientFor(v8::Local<v8::Context> context) {
  return static_cast<V8InspectorImpl*>(
             v8::debug::GetInspector(context->GetIsolate()))
      ->client();
}

V8InternalValueType v8InternalValueTypeFrom(v8::Local<v8::Context> context,
                                            v8::Local<v8::Value> value) {
  if (!value->IsObject()) return V8InternalValueType::kNone;
  V8InspectorImpl* inspector = static_cast<V8InspectorImpl*>(
      v8::debug::GetInspector(context->GetIsolate()));
  int contextId = InspectedContext::contextId(context);
  InspectedContext* inspectedContext = inspector->getContext(contextId);
  if (!inspectedContext) return V8InternalValueType::kNone;
  return inspectedContext->getInternalType(value.As<v8::Object>());
}

enum AbbreviateMode { kMiddle, kEnd };

String16 abbreviateString(const String16& value, AbbreviateMode mode) {
  const size_t maxLength = 100;
  if (value.length() <= maxLength) return value;
  UChar ellipsis = static_cast<UChar>(0x2026);
  if (mode == kMiddle) {
    return String16::concat(
        value.substring(0, maxLength / 2), String16(&ellipsis, 1),
        value.substring(value.length() - maxLength / 2 + 1));
  }
  return String16::concat(value.substring(0, maxLength - 1), ellipsis);
}

String16 descriptionForSymbol(v8::Local<v8::Context> context,
                              v8::Local<v8::Symbol> symbol) {
  v8::Isolate* isolate = context->GetIsolate();
  return String16::concat(
      "Symbol(",
      toProtocolStringWithTypeCheck(isolate, symbol->Description(isolate)),
      ")");
}

String16 descriptionForBigInt(v8::Local<v8::Context> context,
                              v8::Local<v8::BigInt> value) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> description =
      v8::debug::GetBigIntDescription(isolate, value);
  return toProtocolString(isolate, description);
}

String16 descriptionForPrimitiveType(v8::Local<v8::Context> context,
                                     v8::Local<v8::Value> value) {
  if (value->IsUndefined()) return RemoteObject::TypeEnum::Undefined;
  if (value->IsNull()) return RemoteObject::SubtypeEnum::Null;
  if (value->IsBoolean()) {
    return value.As<v8::Boolean>()->Value() ? "true" : "false";
  }
  if (value->IsString()) {
    return toProtocolString(context->GetIsolate(), value.As<v8::String>());
  }
  UNREACHABLE();
}

String16 descriptionForRegExp(v8::Isolate* isolate,
                              v8::Local<v8::RegExp> value) {
  String16Builder description;
  description.append('/');
  description.append(toProtocolString(isolate, value->GetSource()));
  description.append('/');
  v8::RegExp::Flags flags = value->GetFlags();
  if (flags & v8::RegExp::Flags::kHasIndices) description.append('d');
  if (flags & v8::RegExp::Flags::kGlobal) description.append('g');
  if (flags & v8::RegExp::Flags::kIgnoreCase) description.append('i');
  if (flags & v8::RegExp::Flags::kLinear) description.append('l');
  if (flags & v8::RegExp::Flags::kMultiline) description.append('m');
  if (flags & v8::RegExp::Flags::kDotAll) description.append('s');
  if (flags & v8::RegExp::Flags::kUnicode) description.append('u');
  if (flags & v8::RegExp::Flags::kUnicodeSets) description.append('v');
  if (flags & v8::RegExp::Flags::kSticky) description.append('y');
  return description.toString();
}

enum class ErrorType { kNative, kClient };

// Build a description from an exception using the following rules:
//   * Usually return the stack trace found in the {stack} property.
//   * If the stack trace does not start with the class name of the passed
//     exception, try to build a description from the class name, the
//     {message} property and the rest of the stack trace.
//     (The stack trace is only used if {message} was also found in
//     said stack trace).
String16 descriptionForError(v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object, ErrorType type) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::TryCatch tryCatch(isolate);
  String16 className = toProtocolString(isolate, object->GetConstructorName());

  v8::base::Optional<String16> stack;
  {
    v8::Local<v8::Value> stackValue;
    if (object->Get(context, toV8String(isolate, "stack"))
            .ToLocal(&stackValue) &&
        stackValue->IsString()) {
      stack = toProtocolString(isolate, stackValue.As<v8::String>());
    }
  }

  if (type == ErrorType::kNative && stack) return *stack;

  if (stack && stack->substring(0, className.length()) == className) {
    return *stack;
  }

  v8::base::Optional<String16> message;
  {
    v8::Local<v8::Value> messageValue;
    if (object->Get(context, toV8String(isolate, "message"))
            .ToLocal(&messageValue) &&
        messageValue->IsString()) {
      String16 msg = toProtocolStringWithTypeCheck(isolate, messageValue);
      if (!msg.isEmpty()) message = msg;
    }
  }

  if (!message) return stack ? *stack : className;

  String16 description = className + ": " + *message;
  if (!stack) return description;

  DCHECK(stack && message);
  size_t index = stack->find(*message);
  String16 stackWithoutMessage =
      index != String16::kNotFound ? stack->substring(index + message->length())
                                   : String16();
  return description + stackWithoutMessage;
}

String16 descriptionForObject(v8::Isolate* isolate,
                              v8::Local<v8::Object> object) {
  return toProtocolString(isolate, object->GetConstructorName());
}

String16 descriptionForProxy(v8::Isolate* isolate, v8::Local<v8::Proxy> proxy) {
  v8::Local<v8::Value> target = proxy->GetTarget();
  if (target->IsObject()) {
    return String16::concat(
        "Proxy(", descriptionForObject(isolate, target.As<v8::Object>()), ")");
  }
  return String16("Proxy");
}

String16 descriptionForDate(v8::Local<v8::Context> context,
                            v8::Local<v8::Date> date) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> description = v8::debug::GetDateDescription(date);
  return toProtocolString(isolate, description);
}

String16 descriptionForScopeList(v8::Local<v8::Array> list) {
  return String16::concat(
      "Scopes[", String16::fromInteger(static_cast<size_t>(list->Length())),
      ']');
}

String16 descriptionForScope(v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::Value> value;
  if (!object->GetRealNamedProperty(context, toV8String(isolate, "description"))
           .ToLocal(&value)) {
    return String16();
  }
  return toProtocolStringWithTypeCheck(isolate, value);
}

String16 descriptionForCollection(v8::Isolate* isolate,
                                  v8::Local<v8::Object> object, size_t length) {
  String16 className = toProtocolString(isolate, object->GetConstructorName());
  return String16::concat(className, '(', String16::fromInteger(length), ')');
}

#if V8_ENABLE_WEBASSEMBLY
String16 descriptionForWasmValueObject(
    v8::Local<v8::Context> context,
    v8::Local<v8::debug::WasmValueObject> object) {
  v8::Isolate* isolate = context->GetIsolate();
  return toProtocolString(isolate, object->type());
}
#endif  // V8_ENABLE_WEBASSEMBLY

String16 descriptionForEntry(v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object) {
  v8::Isolate* isolate = context->GetIsolate();
  String16 key;
  v8::Local<v8::Value> tmp;
  if (object->GetRealNamedProperty(context, toV8String(isolate, "key"))
          .ToLocal(&tmp)) {
    auto wrapper = ValueMirror::create(context, tmp);
    if (wrapper) {
      std::unique_ptr<ObjectPreview> preview;
      int limit = 5;
      wrapper->buildEntryPreview(context, &limit, &limit, &preview);
      if (preview) {
        key = preview->getDescription(String16());
        if (preview->getType() == RemoteObject::TypeEnum::String) {
          key = String16::concat('\"', key, '\"');
        }
      }
    }
  }

  String16 value;
  if (object->GetRealNamedProperty(context, toV8String(isolate, "value"))
          .ToLocal(&tmp)) {
    auto wrapper = ValueMirror::create(context, tmp);
    if (wrapper) {
      std::unique_ptr<ObjectPreview> preview;
      int limit = 5;
      wrapper->buildEntryPreview(context, &limit, &limit, &preview);
      if (preview) {
        value = preview->getDescription(String16());
        if (preview->getType() == RemoteObject::TypeEnum::String) {
          value = String16::concat('\"', value, '\"');
        }
      }
    }
  }

  return key.length() ? ("{" + key + " => " + value + "}") : value;
}

String16 descriptionForFunction(v8::Local<v8::Function> value) {
  v8::Isolate* isolate = value->GetIsolate();
  v8::Local<v8::String> description = v8::debug::GetFunctionDescription(value);
  return toProtocolString(isolate, description);
}

String16 descriptionForPrivateMethodList(v8::Local<v8::Array> list) {
  return String16::concat(
      "PrivateMethods[",
      String16::fromInteger(static_cast<size_t>(list->Length())), ']');
}

String16 descriptionForPrivateMethod(v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> object) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::Value> value;
  if (!object->GetRealNamedProperty(context, toV8String(isolate, "value"))
           .ToLocal(&value)) {
    return String16();
  }
  DCHECK(value->IsFunction());
  return descriptionForFunction(value.As<v8::Function>());
}

String16 descriptionForNumber(v8::Local<v8::Number> value,
                              bool* unserializable) {
  *unserializable = true;
  double rawValue = value->Value();
  if (std::isnan(rawValue)) return "NaN";
  if (rawValue == 0.0 && std::signbit(rawValue)) return "-0";
  if (std::isinf(rawValue)) {
    return std::signbit(rawValue) ? "-Infinity" : "Infinity";
  }
  *unserializable = false;
  return String16::fromDouble(rawValue);
}

class ValueMirrorBase : public ValueMirror {
 public:
  ValueMirrorBase(v8::Isolate* isolate, v8::Local<v8::Value> value)
      : m_value(isolate, value) {}

  v8::Local<v8::Value> v8Value(v8::Isolate* isolate) const final {
    return m_value.Get(isolate);
  }

 private:
  v8::Global<v8::Value> m_value;
};

class PrimitiveValueMirror final : public ValueMirrorBase {
 public:
  PrimitiveValueMirror(v8::Isolate* isolate, v8::Local<v8::Primitive> value,
                       const String16& type)
      : ValueMirrorBase(isolate, value), m_type(type) {}

  Response buildRemoteObject(
      v8::Local<v8::Context> context, const WrapOptions& wrapOptions,
      std::unique_ptr<RemoteObject>* result) const override {
    std::unique_ptr<protocol::Value> protocolValue;
    v8::Local<v8::Value> value = v8Value(context->GetIsolate());
    toProtocolValue(context, value, &protocolValue);
    *result = RemoteObject::create()
                  .setType(m_type)
                  .setValue(std::move(protocolValue))
                  .build();
    if (value->IsNull()) (*result)->setSubtype(RemoteObject::SubtypeEnum::Null);
    return Response::Success();
  }

  void buildEntryPreview(
      v8::Local<v8::Context> context, int* nameLimit, int* indexLimit,
      std::unique_ptr<ObjectPreview>* preview) const override {
    v8::Local<v8::Value> value = v8Value(context->GetIsolate());
    *preview =
        ObjectPreview::create()
            .setType(m_type)
            .setDescription(descriptionForPrimitiveType(context, value))
            .setOverflow(false)
            .setProperties(std::make_unique<protocol::Array<PropertyPreview>>())
            .build();
    if (value->IsNull())
      (*preview)->setSubtype(RemoteObject::SubtypeEnum::Null);
  }

  void buildPropertyPreview(
      v8::Local<v8::Context> context, const String16& name,
      std::unique_ptr<PropertyPreview>* preview) const override {
    v8::Local<v8::Value> value = v8Value(context->GetIsolate());
    *preview = PropertyPreview::create()
                   .setName(name)
                   .setValue(abbreviateString(
                       descriptionForPrimitiveType(context, value), kMiddle))
                   .setType(m_type)
                   .build();
    if (value->IsNull())
      (*preview)->setSubtype(RemoteObject::SubtypeEnum::Null);
  }

  Response buildDeepSerializedValue(
      v8::Local<v8::Context> context, int maxDepth,
      v8::Local<v8::Object> additionalParameters,
      V8SerializationDuplicateTracker& duplicateTracker,
      std::unique_ptr<protocol::DictionaryValue>* result) const override {
    v8::Local<v8::Value> value = v8Value(context->GetIsolate());
    if (value->IsUndefined()) {
      *result = protocol::DictionaryValue::create();
      (*result)->setString(
          "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Undefined);
      return Response::Success();
    }
    if (value->IsNull()) {
      *result = protocol::DictionaryValue::create();
      (*result)->setString(
          "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Null);
      return Response::Success();
    }
    if (value->IsString()) {
      *result = protocol::DictionaryValue::create();
      (*result)->setString(
          "type", protocol::Runtime::DeepSerializedValue::TypeEnum::String);
      (*result)->setString("value", toProtocolString(context->GetIsolate(),
                                                     value.As<v8::String>()));
      return Response::Success();
    }
    if (value->IsBoolean()) {
      *result = protocol::DictionaryValue::create();
      (*result)->setString(
          "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Boolean);
      (*result)->setBoolean("value", value.As<v8::Boolean>()->Value());
      return Response::Success();
    }

    // Fallback in case of unexpected type.
    bool isKnown;
    *result = duplicateTracker.LinkExistingOrCreate(value, &isKnown);
    if (isKnown) {
      return Response::Success();
    }

    (*result)->setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Object);
    return Response::Success();
  }

 private:
  String16 m_type;
  String16 m_subtype;
};

class NumberMirror final : public ValueMirrorBase {
 public:
  NumberMirror(v8::Isolate* isolate, v8::Local<v8::Number> value)
      : ValueMirrorBase(isolate, value) {}

  Response buildRemoteObject(
      v8::Local<v8::Context> context, const WrapOptions& wrapOptions,
      std::unique_ptr<RemoteObject>* result) const override {
    v8::Local<v8::Number> value =
        v8Value(context->GetIsolate()).As<v8::Number>();
    bool unserializable = false;
    String16 descriptionValue = descriptionForNumber(value, &unserializable);
    *result = RemoteObject::create()
                  .setType(RemoteObject::TypeEnum::Number)
                  .setDescription(descriptionValue)
                  .build();
    if (unserializable) {
      (*result)->setUnserializableValue(descriptionValue);
    } else {
      (*result)->setValue(protocol::FundamentalValue::create(value->Value()));
    }
    return Response::Success();
  }
  void buildPropertyPreview(
      v8::Local<v8::Context> context, const String16& name,
      std::unique_ptr<PropertyPreview>* result) const override {
    v8::Local<v8::Number> value =
        v8Value(context->GetIsolate()).As<v8::Number>();
    bool unserializable = false;
    *result = PropertyPreview::create()
                  .setName(name)
                  .setType(RemoteObject::TypeEnum::Number)
                  .setValue(descriptionForNumber(value, &unserializable))
                  .build();
  }
  void buildEntryPreview(
      v8::Local<v8::Context> context, int* nameLimit, int* indexLimit,
      std::unique_ptr<ObjectPreview>* preview) const override {
    v8::Local<v8::Number> value =
        v8Value(context->GetIsolate()).As<v8::Number>();
    bool unserializable = false;
    *preview =
        ObjectPreview::create()
            .setType(RemoteObject::TypeEnum::Number)
            .setDescription(descriptionForNumber(value, &unserializable))
            .setOverflow(false)
            .setProperties(std::make_unique<protocol::Array<PropertyPreview>>())
            .build();
  }

  Response buildDeepSerializedValue(
      v8::Local<v8::Context> context, int maxDepth,
      v8::Local<v8::Object> additionalParameters,
      V8SerializationDuplicateTracker& duplicateTracker,
      std::unique_ptr<protocol::DictionaryValue>* result) const override {
    *result = protocol::DictionaryValue::create();
    (*result)->setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Number);

    v8::Local<v8::Number> value =
        v8Value(context->GetIsolate()).As<v8::Number>();
    bool unserializable = false;
    String16 descriptionValue = descriptionForNumber(value, &unserializable);
    if (unserializable) {
      (*result)->setValue("value",
                          protocol::StringValue::create(descriptionValue));
    } else {
      (*result)->setValue("value", toProtocolValue(value->Value()));
    }
    return Response::Success();
  }
};

class BigIntMirror final : public ValueMirrorBase {
 public:
  BigIntMirror(v8::Isolate* isolate, v8::Local<v8::BigInt> value)
      : ValueMirrorBase(isolate, value) {}

  Response buildRemoteObject(
      v8::Local<v8::Context> context, const WrapOptions& wrapOptions,
      std::unique_ptr<RemoteObject>* result) const override {
    v8::Local<v8::BigInt> value =
        v8Value(context->GetIsolate()).As<v8::BigInt>();
    String16 description = descriptionForBigInt(context, value);
    *result = RemoteObject::create()
                  .setType(RemoteObject::TypeEnum::Bigint)
                  .setUnserializableValue(description)
                  .setDescription(abbreviateString(description, kMiddle))
                  .build();
    return Response::Success();
  }

  void buildPropertyPreview(v8::Local<v8::Context> context,
                            const String16& name,
                            std::unique_ptr<protocol::Runtime::PropertyPreview>*
                                preview) const override {
    v8::Local<v8::BigInt> value =
        v8Value(context->GetIsolate()).As<v8::BigInt>();
    *preview = PropertyPreview::create()
                   .setName(name)
                   .setType(RemoteObject::TypeEnum::Bigint)
                   .setValue(abbreviateString(
                       descriptionForBigInt(context, value), kMiddle))
                   .build();
  }

  void buildEntryPreview(v8::Local<v8::Context> context, int* nameLimit,
                         int* indexLimit,
                         std::unique_ptr<protocol::Runtime::ObjectPreview>*
                             preview) const override {
    v8::Local<v8::BigInt> value =
        v8Value(context->GetIsolate()).As<v8::BigInt>();
    *preview =
        ObjectPreview::create()
            .setType(RemoteObject::TypeEnum::Bigint)
            .setDescription(
                abbreviateString(descriptionForBigInt(context, value), kMiddle))
            .setOverflow(false)
            .setProperties(std::make_unique<protocol::Array<PropertyPreview>>())
            .build();
  }

  Response buildDeepSerializedValue(
      v8::Local<v8::Context> context, int maxDepth,
      v8::Local<v8::Object> additionalParameters,
      V8SerializationDuplicateTracker& duplicateTracker,
      std::unique_ptr<protocol::DictionaryValue>* result) const override {
    v8::Local<v8::BigInt> value =
        v8Value(context->GetIsolate()).As<v8::BigInt>();
    v8::Local<v8::String> stringValue =
        v8::debug::GetBigIntStringValue(context->GetIsolate(), value);

    *result = protocol::DictionaryValue::create();
    (*result)->setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Bigint);

    (*result)->setValue("value", protocol::StringValue::create(toProtocolString(
                                     context->GetIsolate(), stringValue)));
    return Response::Success();
  }
};

class SymbolMirror final : public ValueMirrorBase {
 public:
  SymbolMirror(v8::Isolate* isolate, v8::Local<v8::Symbol> value)
      : ValueMirrorBase(isolate, value) {}

  Response buildRemoteObject(
      v8::Local<v8::Context> context, const WrapOptions& wrapOptions,
      std::unique_ptr<RemoteObject>* result) const override {
    if (wrapOptions.mode == WrapMode::kJson) {
      return Response::ServerError("Object couldn't be returned by value");
    }
    v8::Local<v8::Symbol> value =
        v8Value(context->GetIsolate()).As<v8::Symbol>();
    *result = RemoteObject::create()
                  .setType(RemoteObject::TypeEnum::Symbol)
                  .setDescription(descriptionForSymbol(context, value))
                  .build();
    return Response::Success();
  }

  void buildPropertyPreview(v8::Local<v8::Context> context,
                            const String16& name,
                            std::unique_ptr<protocol::Runtime::PropertyPreview>*
                                preview) const override {
    v8::Local<v8::Symbol> value =
        v8Value(context->GetIsolate()).As<v8::Symbol>();
    *preview = PropertyPreview::create()
                   .setName(name)
                   .setType(RemoteObject::TypeEnum::Symbol)
                   .setValue(abbreviateString(
                       descriptionForSymbol(context, value), kEnd))
                   .build();
  }

  void buildEntryPreview(
      v8::Local<v8::Context> context, int* nameLimit, int* indexLimit,
      std::unique_ptr<ObjectPreview>* preview) const override {
    v8::Local<v8::Symbol> value =
        v8Value(context->GetIsolate()).As<v8::Symbol>();
    *preview =
        ObjectPreview::create()
            .setType(RemoteObject::TypeEnum::Symbol)
            .setDescription(descriptionForSymbol(context, value))
            .setOverflow(false)
            .setProperties(std::make_unique<protocol::Array<PropertyPreview>>())
            .build();
  }

  Response buildDeepSerializedValue(
      v8::Local<v8::Context> context, int maxDepth,
      v8::Local<v8::Object> additionalParameters,
      V8SerializationDuplicateTracker& duplicateTracker,
      std::unique_ptr<protocol::DictionaryValue>* result) const override {
    v8::Local<v8::Value> value = v8Value(context->GetIsolate());
    bool isKnown;
    *result = duplicateTracker.LinkExistingOrCreate(value, &isKnown);
    if (isKnown) {
      return Response::Success();
    }

    (*result)->setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Symbol);
    return Response::Success();
  }
};

class LocationMirror final : public ValueMirrorBase {
 public:
  static std::unique_ptr<LocationMirror> create(
      v8::Local<v8::Function> function) {
    return create(function, function->ScriptId(),
                  function->GetScriptLineNumber(),
                  function->GetScriptColumnNumber());
  }
  static std::unique_ptr<LocationMirror> createForGenerator(
      v8::Local<v8::Object> value) {
    v8::Local<v8::debug::GeneratorObject> generatorObject =
        v8::debug::GeneratorObject::Cast(value);
    if (!generatorObject->IsSuspended()) {
      return create(generatorObject->Function());
    }
    v8::Local<v8::debug::Script> script;
    if (!generatorObject->Script().ToLocal(&script)) return nullptr;
    v8::debug::Location suspendedLocation =
        generatorObject->SuspendedLocation();
    return create(value, script->Id(), suspendedLocation.GetLineNumber(),
                  suspendedLocation.GetColumnNumber());
  }

  Response buildRemoteObject(
      v8::Local<v8::Context> context, const WrapOptions& wrapOptions,
      std::unique_ptr<RemoteObject>* result) const override {
    auto location = protocol::DictionaryValue::create();
    location->setString("scriptId", String16::fromInteger(m_scriptId));
    location->setInteger("lineNumber", m_lineNumber);
    location->setInteger("columnNumber", m_columnNumber);
    *result = RemoteObject::create()
                  .setType(RemoteObject::TypeEnum::Object)
                  .setSubtype("internal#location")
                  .setDescription("Object")
                  .setValue(std::move(location))
                  .build();
    return Response::Success();
  }

  Response buildDeepSerializedValue(
      v8::Local<v8::Context> context, int maxDepth,
      v8::Local<v8::Object> additionalParameters,
      V8SerializationDuplicateTracker& duplicateTracker,
      std::unique_ptr<protocol::DictionaryValue>* result) const override {
    bool isKnown;
    v8::Local<v8::Value> value = v8Value(context->GetIsolate());
    *result = duplicateTracker.LinkExistingOrCreate(value, &isKnown);
    if (isKnown) {
      return Response::Success();
    }

    (*result)->setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Object);
    return Response::Success();
  }

 private:
  static std::unique_ptr<LocationMirror> create(v8::Local<v8::Object> value,
                                                int scriptId, int lineNumber,
                                                int columnNumber) {
    if (scriptId == v8::UnboundScript::kNoScriptId) return nullptr;
    if (lineNumber == v8::Function::kLineOffsetNotFound ||
        columnNumber == v8::Function::kLineOffsetNotFound) {
      return nullptr;
    }
    return std::unique_ptr<LocationMirror>(
        new LocationMirror(value, scriptId, lineNumber, columnNumber));
  }

  LocationMirror(v8::Local<v8::Object> value, int scriptId, int lineNumber,
                 int columnNumber)
      : ValueMirrorBase(value->GetIsolate(), value),
        m_scriptId(scriptId),
        m_lineNumber(lineNumber),
        m_columnNumber(columnNumber) {}

  int m_scriptId;
  int m_lineNumber;
  int m_columnNumber;
};

class FunctionMirror final : public ValueMirrorBase {
 public:
  explicit FunctionMirror(v8::Local<v8::Function> value)
      : ValueMirrorBase(value->GetIsolate(), value) {}

  Response buildRemoteObject(
      v8::Local<v8::Context> context, const WrapOptions& wrapOptions,
      std::unique_ptr<RemoteObject>* result) const override {
    v8::Local<v8::Function> value =
        v8Value(context->GetIsolate()).As<v8::Function>();
    // TODO(alph): drop this functionality.
    if (wrapOptions.mode == WrapMode::kJson) {
      std::unique_ptr<protocol::Value> protocolValue;
      Response response = toProtocolValue(context, value, &protocolValue);
      if (!response.IsSuccess()) return response;
      *result = RemoteObject::create()
                    .setType(RemoteObject::TypeEnum::Function)
                    .setValue(std::move(protocolValue))
                    .build();
    } else {
      *result = RemoteObject::create()
                    .setType(RemoteObject::TypeEnum::Function)
                    .setClassName(toProtocolStringWithTypeCheck(
                        context->GetIsolate(), value->GetConstructorName()))
                    .setDescription(descriptionForFunction(value))
                    .build();
    }
    return Response::Success();
  }

  void buildPropertyPreview(
      v8::Local<v8::Context> context, const String16& name,
      std::unique_ptr<PropertyPreview>* result) const override {
    *result = PropertyPreview::create()
                  .setName(name)
                  .setType(RemoteObject::TypeEnum::Function)
                  .setValue(String16())
                  .build();
  }
  void buildEntryPreview(
      v8::Local<v8::Context> context, int* nameLimit, int* indexLimit,
      std::unique_ptr<ObjectPreview>* preview) const override {
    v8::Local<v8::Function> value =
        v8Value(context->GetIsolate()).As<v8::Function>();
    *preview =
        ObjectPreview::create()
            .setType(RemoteObject::TypeEnum::Function)
            .setDescription(descriptionForFunction(value))
            .setOverflow(false)
            .setProperties(std::make_unique<protocol::Array<PropertyPreview>>())
            .build();
  }

  Response buildDeepSerializedValue(
      v8::Local<v8::Context> context, int maxDepth,
      v8::Local<v8::Object> additionalParameters,
      V8SerializationDuplicateTracker& duplicateTracker,
      std::unique_ptr<protocol::DictionaryValue>* result) const override {
    bool isKnown;
    v8::Local<v8::Value> value = v8Value(context->GetIsolate());
    *result = duplicateTracker.LinkExistingOrCreate(value, &isKnown);
    if (isKnown) {
      return Response::Success();
    }

    (*result)->setString(
        "type", protocol::Runtime::DeepSerializedValue::TypeEnum::Function);
    return Response::Success();
  }
};

bool isArrayLike(v8::Local<v8::Context> context, v8::Local<v8::Object> object,
                 size_t* length) {
  if (object->IsArray()) {
    *length = object.As<v8::Array>()->Length();
    return true;
  }
  if (object->IsArgumentsObject()) {
    v8::Isolate* isolate = context->GetIsolate();
    v8::TryCatch tryCatch(isolate);
    v8::MicrotasksScope microtasksScope(
        context, v8::MicrotasksScope::kDoNotRunMicrotasks);
    v8::Local<v8::Value> lengthDescriptor;
    if (!object
             ->GetOwnPropertyDescriptor(context, toV8String(isolate, "length"))
             .ToLocal(&lengthDescriptor)) {
      return false;
    }
    v8::Local<v8::Value> lengthValue;
    if (!lengthDescriptor->IsObject() ||
        !lengthDescriptor.As<v8::Object>()
             ->Get(context, toV8String(isolate, "value"))
             .ToLocal(&lengthValue) ||
        !lengthValue->IsUint32()) {
      return false;
    }
    *length = lengthValue.As<v8::Uint32>()->Value();
    return true;
  }
  return false;
}

struct EntryMirror {
  std::unique_ptr<ValueMirror> key;
  std::unique_ptr<ValueMirror> value;

  static bool getEntries(v8::Local<v8::Context> context,
                         v8::Local<v8::Object> object, size_t limit,
                         bool* overflow, std::vector<EntryMirror>* mirrors) {
    bool isKeyValue = false;
    v8::Local<v8::Array> entries;
    if (!object->PreviewEntries(&isKeyValue).ToLocal(&entries)) return false;
    for (uint32_t i = 0; i < entries->Length(); i += isKeyValue ? 2 : 1) {
      v8::Local<v8::Value> tmp;

      std::unique_ptr<ValueMirror> keyMirror;
      if (isKeyValue && entries->Get(context, i).ToLocal(&tmp)) {
        keyMirror = ValueMirror::create(context, tmp);
      }
      std::unique_ptr<ValueMirror> valueMirror;
      if (entries->Get(context, isKeyValue ? i + 1 : i).ToLocal(&tmp)) {
        valueMirror = ValueMirror::create(context, tmp);
      } else {
        continue;
      }
      if (mirrors->size() == limit) {
        *overflow = true;
        return true;
      }
      mirrors->emplace_back(
          EntryMirror{std::move(keyMirror), std::move(valueMirror)});
    }
    return !mirrors->empty();
  }
};

class PreviewPropertyAccumulator : public ValueMirror::PropertyAccumulator {
 public:
  PreviewPropertyAccumulator(v8::Isolate* isolate,
                             const std::vector<String16>& blocklist,
                             int skipIndex, int* nameLimit, int* indexLimit,
                             bool* overflow,
                             std::vector<PropertyMirror>* mirrors)
      : m_isolate(isolate),
        m_blocklist(blocklist),
        m_skipIndex(skipIndex),
        m_nameLimit(nameLimit),
        m_indexLimit(indexLimit),
        m_overflow(overflow),
        m_mirrors(mirrors) {}

  bool Add(PropertyMirror mirror) override {
    if (mirror.exception) return true;
    if ((!mirror.getter || !mirror.getter->v8Value(m_isolate)->IsFunction()) &&
        !mirror.value) {
      return true;
    }
    if (!mirror.isOwn && !mirror.isSynthetic) return true;
    if (std::find(m_blocklist.begin(), m_blocklist.end(), mirror.name) !=
        m_blocklist.end()) {
      return true;
    }
    if (mirror.isIndex && m_skipIndex > 0) {
      --m_skipIndex;
      if (m_skipIndex > 0) return true;
    }
    int* limit = mirror.isIndex ? m_indexLimit : m_nameLimit;
    if (!*limit) {
      *m_overflow = true;
      return false;
    }
    --*limit;
    m_mirrors->push_back(std::move(mirror));
    return true;
  }

 private:
  v8::Isolate* m_isolate;
  std::vector<String16> m_blocklist;
  int m_skipIndex;
  int* m_nameLimit;
  int* m_indexLimit;
  bool* m_overflow;
  std::vector<PropertyMirror>* m_mirrors;
};

bool getPropertiesForPreview(v8::Local<v8::Context> context,
                             v8::Local<v8::Object> object, int* nameLimit,
                             int* indexLimit, bool* overflow,
                             std::vector<PropertyMirror>* properties) {
  std::vector<String16> blocklist;
  size_t length = 0;
  if (isArrayLike(context, object, &length) || object->IsStringObject()) {
    blocklist.push_back("length");
#if V8_ENABLE_WEBASSEMBLY
  } else if (v8::debug::WasmValueObject::IsWasmValueObject(object)) {
    blocklist.push_back("type");
#endif  // V8_ENABLE_WEBASSEMBLY
  } else {
    auto clientSubtype = clientFor(context)->valueSubtype(object);
    if (clientSubtype && toString16(clientSubtype->string()) == "array") {
      blocklist.push_back("length");
    }
  }
  if (object->IsArrayBuffer() || object->IsSharedArrayBuffer()) {
    blocklist.push_back("[[Int8Array]]");
    blocklist.push_back("[[Uint8Array]]");
    blocklist.push_back("[[Int16Array]]");
    blocklist.push_back("[[Int32Array]]");
  }
  blocklist.push_back("constructor");
  int skipIndex = object->IsStringObject()
                      ? object.As<v8::StringObject>()->ValueOf()->Length() + 1
                      : -1;
  PreviewPropertyAccumulator accumulator(context->GetIsolate(), blocklist,
                                         skipIndex, nameLimit, indexLimit,
                                         overflow, properties);
  return ValueMirror::getProperties(context, object, false, false, false,
                                    &accumulator);
}

void getInternalPropertiesForPreview(
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    int* nameLimit, bool* overflow,
    std::vector<InternalPropertyMirror>* properties) {
  std::vector<InternalPropertyMirror> mirrors;
  ValueMirror::getInternalProperties(context, object, &mirrors);
  std::vector<String16> allowlist;
  if (object->IsBooleanObject() || object->IsNumberObject() ||
      object->IsStringObject() || object->IsSymbolObject() ||
      object->IsBigIntObject()) {
    allowlist.emplace_back("[[PrimitiveValue]]");
  } else if (object->IsPromise()) {
    allowlist.emplace_back("[[PromiseState]]");
    allowlist.emplace_back("[[PromiseResult]]");
  } else if (object->IsGeneratorObject()) {
    allowlist.emplace_back("[[GeneratorState]]");
  } else if (object->IsWeakRef()) {
    allowlist.emplace_back("[[WeakRefTarget]]");
  }
  for (auto& mirror : mirrors) {
    if (std::find(allowlist.begin(), allowlist.end(), mirror.name) ==
        allowlist.end()) {
      continue;
    }
    if (!*nameLimit) {
      *overflow = true;
      return;
    }
    --*nameLimit;
    properties->push_back(std::move(mirror));
  }
}

void getPrivatePropertiesForPreview(
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    int* nameLimit, bool* overflow,
    protocol::Array<PropertyPreview>* privateProperties) {
  std::vector<PrivatePropertyMirror> mirrors =
      ValueMirror::getPrivateProperties(context, object,
                                        /* accessPropertiesOnly */ false);
  for (auto& mirror : mirrors) {
    std::unique_ptr<PropertyPreview> propertyPreview;
    if (mirror.value) {
      mirror.value->buildPropertyPreview(context, mirror.name,
                                         &propertyPreview);
    } else {
      propertyPreview = PropertyPreview::create()
                            .setName(mirror.name)
                            .setType(PropertyPreview::TypeEnum::Accessor)
                            .build();
    }
    if (!propertyPreview) continue;
    if (!*nameLimit) {
      *overflow = true;
      return;
    }
    --*nameLimit;
    privateProperties->emplace_back(std::move(propertyPreview));
  }
}

class ObjectMirror final : public ValueMirrorBase {
 public:
  ObjectMirror(v8::Local<v8::Object> value, const String16& description)
      : ValueMirrorBase(value->GetIsolate(), value),
        m_description(description),
        m_hasSubtype(false) {}
  ObjectMirror(v8::Local<v8::Object> value, const String16& subtype,
               const String16& description)
      : ValueMirrorBase(value->GetIsolate(), value),
        m_description(description),
        m_hasSubtype(true),
        m_subtype(subtype) {}

  Response buildRemoteObject(
      v8::Local<v8::Context> context, const WrapOptions& wrapOptions,
      std::unique_ptr<RemoteObject>* result) const override {
    v8::Isolate* isolate = context->GetIsolate();
    v8::Local<v8::Object> value = v8Value(isolate).As<v8::Object>();
    if (wrapOptions.mode == WrapMode::kJson) {
      std::unique_ptr<protocol::Value> protocolValue;
      Response response = toProtocolValue(context, value, &protocolValue);
      if (!response.IsSuccess()) return response;
      *result = RemoteObject::create()
                    .setType(RemoteObject::TypeEnum::Object)
                    .setValue(std::move(protocolValue))
                    .build();
    } else {
      *result = RemoteObject::create()
                    .setType(RemoteObject::TypeEnum::Object)
                    .setClassName(
                        toProtocolString(isolate, value->GetConstructorName()))
                    .setDescription(m_description)
                    .build();
      if (m_hasSubtype) (*result)->setSubtype(m_subtype);
      if (wrapOptions.mode == WrapMode::kPreview) {
        std::unique_ptr<ObjectPreview> previewValue;
        int nameLimit = 5;
        int indexLimit = 100;
        buildObjectPreview(context, false, &nameLimit, &indexLimit,
                           &previewValue);
        (*result)->setPreview(std::move(previewValue));
      }
    }
    return Response::Success();
  }

  void buildObjectPreview(
      v8::Local<v8::Context> context, bool generatePreviewForTable,
      int* nameLimit, int* indexLimit,
      std::unique_ptr<ObjectPreview>* result) const override {
    buildObjectPreviewInternal(context, false /* forEntry */,
                               generatePreviewForTable, nameLimit, indexLimit,
                               result);
  }

  void buildEntryPreview(
      v8::Local<v8::Context> context, int* nameLimit, int* indexLimit,
      std::unique_ptr<ObjectPreview>* result) const override {
    buildObjectPreviewInternal(context, true /* forEntry */,
                               false /* generatePreviewForTable */, nameLimit,
                               indexLimit, result);
  }

  void buildPropertyPreview(
      v8::Local<v8::Context> context, const String16& name,
      std::unique_ptr<PropertyPreview>* result) const override {
    *result = PropertyPreview::create()
                  .setName(name)
                  .setType(RemoteObject::TypeEnum::Object)
                  .setValue(abbreviateString(
                      m_description,
                      m_subtype == RemoteObject::SubtypeEnum::Regexp ? kMiddle
                                                                     : kEnd))
                  .build();
    if (m_hasSubtype) (*result)->setSubtype(m_subtype);
  }

  Response buildDeepSerializedValue(
      v8::Local<v8::Context> context, int maxDepth,
      v8::Local<v8::Object> additionalParameters,
      V8SerializationDuplicateTracker& duplicateTracker,
      std::unique_ptr<protocol::DictionaryValue>* result) const override {
    v8::Local<v8::Object> value =
        v8Value(context->GetIsolate()).As<v8::Object>();
    maxDepth = std::min(kMaxProtocolDepth, maxDepth);
    bool isKnown;
    *result = duplicateTracker.LinkExistingOrCreate(value, &isKnown);
    if (isKnown) {
      return Response::Success();
    }

    // Check if embedder implemented custom serialization.
    std::unique_ptr<v8_inspector::DeepSerializationResult>
        embedderDeepSerializedResult = clientFor(context)->deepSerialize(
            value, maxDepth, additionalParameters);
    if (embedderDeepSerializedResult) {
      // Embedder-implemented serialization.

      if (!embedderDeepSerializedResult->isSuccess)
        return Response::ServerError(
            toString16(embedderDeepSerializedResult->errorMessage->string())
                .utf8());

      (*result)->setString(
          "type",
          toString16(
              embedderDeepSerializedResult->serializedValue->type->string()));
      v8::Local<v8::Value> v8Value;
      if (embedderDeepSerializedResult->serializedValue->value.ToLocal(
              &v8Value)) {
        // Embedder-implemented serialization has value.
        std::unique_ptr<protocol::Value> protocolValue;
        Response response = toProtocolValue(context, v8Value, &protocolValue);
        if (!response.IsSuccess()) return response;
        (*result)->setValue("value", std::move(protocolValue));
      }
      return Response::Success();
    }

    // No embedder-implemented serialization. Serialize as V8 Object.
    return V8DeepSerializer::serializeV8Value(value, context, maxDepth,
                                              additionalParameters,
                                              duplicateTracker, *(*result));
  }

 private:
  void buildObjectPreviewInternal(
      v8::Local<v8::Context> context, bool forEntry,
      bool generatePreviewForTable, int* nameLimit, int* indexLimit,
      std::unique_ptr<ObjectPreview>* result) const {
    auto properties = std::make_unique<protocol::Array<PropertyPreview>>();
    std::unique_ptr<protocol::Array<EntryPreview>> entriesPreview;
    bool overflow = false;

    v8::Local<v8::Value> value = v8Value(context->GetIsolate());
    while (value->IsProxy()) value = value.As<v8::Proxy>()->GetTarget();

    if (value->IsObject() && !value->IsProxy()) {
      v8::Local<v8::Object> objectForPreview = value.As<v8::Object>();
      std::vector<InternalPropertyMirror> internalProperties;
      getInternalPropertiesForPreview(context, objectForPreview, nameLimit,
                                      &overflow, &internalProperties);
      for (size_t i = 0; i < internalProperties.size(); ++i) {
        std::unique_ptr<PropertyPreview> propertyPreview;
        internalProperties[i].value->buildPropertyPreview(
            context, internalProperties[i].name, &propertyPreview);
        if (propertyPreview) {
          properties->emplace_back(std::move(propertyPreview));
        }
      }

      getPrivatePropertiesForPreview(context, objectForPreview, nameLimit,
                                     &overflow, properties.get());

      std::vector<PropertyMirror> mirrors;
      if (getPropertiesForPreview(context, objectForPreview, nameLimit,
                                  indexLimit, &overflow, &mirrors)) {
        for (size_t i = 0; i < mirrors.size(); ++i) {
          std::unique_ptr<PropertyPreview> preview;
          std::unique_ptr<ObjectPreview> valuePreview;
          if (mirrors[i].value) {
            mirrors[i].value->buildPropertyPreview(context, mirrors[i].name,
                                                   &preview);
            if (generatePreviewForTable) {
              int tableLimit = 1000;
              mirrors[i].value->buildObjectPreview(context, false, &tableLimit,
                                                   &tableLimit, &valuePreview);
            }
          } else {
            preview = PropertyPreview::create()
                          .setName(mirrors[i].name)
                          .setType(PropertyPreview::TypeEnum::Accessor)
                          .build();
          }
          if (valuePreview) {
            preview->setValuePreview(std::move(valuePreview));
          }
          properties->emplace_back(std::move(preview));
        }
      }

      std::vector<EntryMirror> entries;
      if (EntryMirror::getEntries(context, objectForPreview, 5, &overflow,
                                  &entries)) {
        if (forEntry) {
          overflow = true;
        } else {
          entriesPreview = std::make_unique<protocol::Array<EntryPreview>>();
          for (const auto& entry : entries) {
            std::unique_ptr<ObjectPreview> valuePreview;
            entry.value->buildEntryPreview(context, nameLimit, indexLimit,
                                           &valuePreview);
            if (!valuePreview) continue;
            std::unique_ptr<ObjectPreview> keyPreview;
            if (entry.key) {
              entry.key->buildEntryPreview(context, nameLimit, indexLimit,
                                           &keyPreview);
              if (!keyPreview) continue;
            }
            std::unique_ptr<EntryPreview> entryPreview =
                EntryPreview::create()
                    .setValue(std::move(valuePreview))
                    .build();
            if (keyPreview) entryPreview->setKey(std::move(keyPreview));
            entriesPreview->emplace_back(std::move(entryPreview));
          }
        }
      }
    }
    *result = ObjectPreview::create()
                  .setType(RemoteObject::TypeEnum::Object)
                  .setDescription(m_description)
                  .setOverflow(overflow)
                  .setProperties(std::move(properties))
                  .build();
    if (m_hasSubtype) (*result)->setSubtype(m_subtype);
    if (entriesPreview) (*result)->setEntries(std::move(entriesPreview));
  }

  String16 m_description;
  bool m_hasSubtype;
  String16 m_subtype;
};

void nativeGetterCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Local<v8::Object> data = info.Data().As<v8::Object>();
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Value> name;
  if (!data->GetRealNamedProperty(context, toV8String(isolate, "name"))
           .ToLocal(&name)) {
    return;
  }
  v8::Local<v8::Value> object;
  if (!data->GetRealNamedProperty(context, toV8String(isolate, "object"))
           .ToLocal(&object) ||
      !object->IsObject()) {
    return;
  }
  v8::Local<v8::Value> value;
  if (!object.As<v8::Object>()->Get(context, name).ToLocal(&value)) return;
  info.GetReturnValue().Set(value);
}

std::unique_ptr<ValueMirror> createNativeGetter(v8::Local<v8::Context> context,
                                                v8::Local<v8::Value> object,
                                                v8::Local<v8::Name> name) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::TryCatch tryCatch(isolate);

  v8::Local<v8::Object> data = v8::Object::New(isolate);
  if (data->Set(context, toV8String(isolate, "name"), name).IsNothing()) {
    return nullptr;
  }
  if (data->Set(context, toV8String(isolate, "object"), object).IsNothing()) {
    return nullptr;
  }

  v8::Local<v8::Function> function;
  if (!v8::Function::New(context, nativeGetterCallback, data, 0,
                         v8::ConstructorBehavior::kThrow)
           .ToLocal(&function)) {
    return nullptr;
  }
  return ValueMirror::create(context, function);
}

void nativeSetterCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  if (info.Length() < 1) return;
  v8::Local<v8::Object> data = info.Data().As<v8::Object>();
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Value> name;
  if (!data->GetRealNamedProperty(context, toV8String(isolate, "name"))
           .ToLocal(&name)) {
    return;
  }
  v8::Local<v8::Value> object;
  if (!data->GetRealNamedProperty(context, toV8String(isolate, "object"))
           .ToLocal(&object) ||
      !object->IsObject()) {
    return;
  }
  if (!object.As<v8::Object>()->Set(context, name, info[0]).IsNothing()) return;
}

std::unique_ptr<ValueMirror> createNativeSetter(v8::Local<v8::Context> context,
                                                v8::Local<v8::Value> object,
                                                v8::Local<v8::Name> name) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::TryCatch tryCatch(isolate);

  v8::Local<v8::Object> data = v8::Object::New(isolate);
  if (data->Set(context, toV8String(isolate, "name"), name).IsNothing()) {
    return nullptr;
  }
  if (data->Set(context, toV8String(isolate, "object"), object).IsNothing()) {
    return nullptr;
  }

  v8::Local<v8::Function> function;
  if (!v8::Function::New(context, nativeSetterCallback, data, 1,
                         v8::ConstructorBehavior::kThrow)
           .ToLocal(&function)) {
    return nullptr;
  }
  return ValueMirror::create(context, function);
}

bool doesAttributeHaveObservableSideEffectOnGet(v8::Local<v8::Context> context,
                                                v8::Local<v8::Object> object,
                                                v8::Local<v8::Name> name) {
  // TODO(dgozman): we should remove this, annotate more embedder properties as
  // side-effect free, and call all getters which do not produce side effects.
  if (!name->IsString()) return false;
  v8::Isolate* isolate = context->GetIsolate();
  if (!name.As<v8::String>()->StringEquals(toV8String(isolate, "body"))) {
    return false;
  }

  v8::TryCatch tryCatch(isolate);
  v8::Local<v8::Value> request;
  if (context->Global()
          ->GetRealNamedProperty(context, toV8String(isolate, "Request"))
          .ToLocal(&request)) {
    if (request->IsObject() &&
        object->InstanceOf(context, request.As<v8::Object>())
            .FromMaybe(false)) {
      return true;
    }
  }
  if (tryCatch.HasCaught()) tryCatch.Reset();

  v8::Local<v8::Value> response;
  if (context->Global()
          ->GetRealNamedProperty(context, toV8String(isolate, "Response"))
          .ToLocal(&response)) {
    if (response->IsObject() &&
        object->InstanceOf(context, response.As<v8::Object>())
            .FromMaybe(false)) {
      return true;
    }
  }
  return false;
}

}  // anonymous namespace

ValueMirror::~ValueMirror() = default;

// static
bool ValueMirror::getProperties(v8::Local<v8::Context> context,
                                v8::Local<v8::Object> object,
                                bool ownProperties, bool accessorPropertiesOnly,
                                bool nonIndexedPropertiesOnly,
                                PropertyAccumulator* accumulator) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::TryCatch tryCatch(isolate);
  v8::Local<v8::Set> set = v8::Set::New(isolate);

  v8::MicrotasksScope microtasksScope(context,
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);
  V8InternalValueType internalType = v8InternalValueTypeFrom(context, object);
  if (internalType == V8InternalValueType::kScope) {
    v8::Local<v8::Value> value;
    if (!object->Get(context, toV8String(isolate, "object")).ToLocal(&value) ||
        !value->IsObject()) {
      return false;
    } else {
      object = value.As<v8::Object>();
    }
  }
  if (internalType == V8InternalValueType::kScopeList ||
      internalType == V8InternalValueType::kPrivateMethodList) {
    if (!set->Add(context, toV8String(isolate, "length")).ToLocal(&set)) {
      return false;
    }
  }

  auto iterator = v8::debug::PropertyIterator::Create(context, object,
                                                      nonIndexedPropertiesOnly);
  if (!iterator) {
    CHECK(tryCatch.HasCaught());
    return false;
  }
  while (!iterator->Done()) {
    bool isOwn = iterator->is_own();
    if (!isOwn && ownProperties) break;
    v8::Local<v8::Name> v8Name = iterator->name();
    v8::Maybe<bool> result = set->Has(context, v8Name);
    if (result.IsNothing()) return false;
    if (result.FromJust()) {
      if (!iterator->Advance().FromMaybe(false)) {
        CHECK(tryCatch.HasCaught());
        return false;
      }
      continue;
    }
    if (!set->Add(context, v8Name).ToLocal(&set)) return false;

    String16 name;
    std::unique_ptr<ValueMirror> symbolMirror;
    if (v8Name->IsString()) {
      name = toProtocolString(isolate, v8Name.As<v8::String>());
    } else {
      v8::Local<v8::Symbol> symbol = v8Name.As<v8::Symbol>();
      name = descriptionForSymbol(context, symbol);
      symbolMirror = ValueMirror::create(context, symbol);
    }

    v8::PropertyAttribute attributes;
    std::unique_ptr<ValueMirror> valueMirror;
    std::unique_ptr<ValueMirror> getterMirror;
    std::unique_ptr<ValueMirror> setterMirror;
    std::unique_ptr<ValueMirror> exceptionMirror;
    bool writable = false;
    bool enumerable = false;
    bool configurable = false;

    bool isAccessorProperty = false;
    v8::TryCatch tryCatchAttributes(isolate);
    if (!iterator->attributes().To(&attributes)) {
      exceptionMirror =
          ValueMirror::create(context, tryCatchAttributes.Exception());
    } else {
      if (iterator->is_native_accessor()) {
        if (iterator->has_native_getter()) {
          getterMirror = createNativeGetter(context, object, v8Name);
        }
        if (iterator->has_native_setter()) {
          setterMirror = createNativeSetter(context, object, v8Name);
        }
        writable = !(attributes & v8::PropertyAttribute::ReadOnly);
        enumerable = !(attributes & v8::PropertyAttribute::DontEnum);
        configurable = !(attributes & v8::PropertyAttribute::DontDelete);
        isAccessorProperty = getterMirror || setterMirror;
      } else {
        v8::TryCatch tryCatchDescriptor(isolate);
        v8::debug::PropertyDescriptor descriptor;
        if (!iterator->descriptor().To(&descriptor)) {
          exceptionMirror =
              ValueMirror::create(context, tryCatchDescriptor.Exception());
        } else {
          writable = descriptor.has_writable ? descriptor.writable : false;
          enumerable =
              descriptor.has_enumerable ? descriptor.enumerable : false;
          configurable =
              descriptor.has_configurable ? descriptor.configurable : false;
          if (!descriptor.value.IsEmpty()) {
            valueMirror = ValueMirror::create(context, descriptor.value);
          }
          v8::Local<v8::Function> getterFunction;
          if (!descriptor.get.IsEmpty()) {
            v8::Local<v8::Value> get = descriptor.get;
            getterMirror = ValueMirror::create(context, get);
            if (get->IsFunction()) getterFunction = get.As<v8::Function>();
          }
          if (!descriptor.set.IsEmpty()) {
            setterMirror = ValueMirror::create(context, descriptor.set);
          }
          isAccessorProperty = getterMirror || setterMirror;
          if (name != "__proto__" && !getterFunction.IsEmpty() &&
              getterFunction->ScriptId() == v8::UnboundScript::kNoScriptId &&
              !doesAttributeHaveObservableSideEffectOnGet(context, object,
                                                          v8Name)) {
            v8::TryCatch tryCatchFunction(isolate);
            v8::Local<v8::Value> value;
            if (object->Get(context, v8Name).ToLocal(&value)) {
              if (value->IsPromise() &&
                  value.As<v8::Promise>()->State() == v8::Promise::kRejected) {
                value.As<v8::Promise>()->MarkAsHandled();
              } else {
                valueMirror = ValueMirror::create(context, value);
                setterMirror = nullptr;
                getterMirror = nullptr;
              }
            }
          }
        }
      }
    }
    if (accessorPropertiesOnly && !isAccessorProperty) continue;
    auto mirror = PropertyMirror{name,
                                 writable,
                                 configurable,
                                 enumerable,
                                 isOwn,
                                 iterator->is_array_index(),
                                 isAccessorProperty && valueMirror,
                                 std::move(valueMirror),
                                 std::move(getterMirror),
                                 std::move(setterMirror),
                                 std::move(symbolMirror),
                                 std::move(exceptionMirror)};
    if (!accumulator->Add(std::move(mirror))) return true;

    if (!iterator->Advance().FromMaybe(false)) {
      CHECK(tryCatchAttributes.HasCaught());
      return false;
    }
  }
  return true;
}

// static
void ValueMirror::getInternalProperties(
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    std::vector<InternalPropertyMirror>* mirrors) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::MicrotasksScope microtasksScope(context,
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::TryCatch tryCatch(isolate);
  if (object->IsFunction()) {
    v8::Local<v8::Function> function = object.As<v8::Function>();
    auto location = LocationMirror::create(function);
    if (location) {
      mirrors->emplace_back(InternalPropertyMirror{
          String16("[[FunctionLocation]]"), std::move(location)});
    }
    if (function->IsGeneratorFunction()) {
      mirrors->emplace_back(InternalPropertyMirror{
          String16("[[IsGenerator]]"),
          ValueMirror::create(context, v8::True(context->GetIsolate()))});
    }
  }
  if (object->IsGeneratorObject()) {
    auto location = LocationMirror::createForGenerator(object);
    if (location) {
      mirrors->emplace_back(InternalPropertyMirror{
          String16("[[GeneratorLocation]]"), std::move(location)});
    }
  }
  V8Debugger* debugger =
      static_cast<V8InspectorImpl*>(v8::debug::GetInspector(isolate))
          ->debugger();
  v8::Local<v8::Array> properties;
  if (debugger->internalProperties(context, object).ToLocal(&properties)) {
    for (uint32_t i = 0; i < properties->Length(); i += 2) {
      v8::Local<v8::Value> name;
      if (!properties->Get(context, i).ToLocal(&name) || !name->IsString()) {
        tryCatch.Reset();
        continue;
      }
      v8::Local<v8::Value> value;
      if (!properties->Get(context, i + 1).ToLocal(&value)) {
        tryCatch.Reset();
        continue;
      }
      auto wrapper = ValueMirror::create(context, value);
      if (wrapper) {
        mirrors->emplace_back(InternalPropertyMirror{
            toProtocolStringWithTypeCheck(context->GetIsolate(), name),
            std::move(wrapper)});
      }
    }
  }
}

// static
std::vector<PrivatePropertyMirror> ValueMirror::getPrivateProperties(
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    bool accessorPropertiesOnly) {
  std::vector<PrivatePropertyMirror> mirrors;
  v8::Isolate* isolate = context->GetIsolate();
  v8::MicrotasksScope microtasksScope(context,
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::TryCatch tryCatch(isolate);

  v8::LocalVector<v8::Value> names(isolate);
  v8::LocalVector<v8::Value> values(isolate);
  int filter =
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateAccessors) |
      static_cast<int>(v8::debug::PrivateMemberFilter::kPrivateFields);
  if (!v8::debug::GetPrivateMembers(context, object, filter, &names, &values))
    return mirrors;

  size_t len = values.size();
  for (size_t i = 0; i < len; i++) {
    v8::Local<v8::Value> name = names[i];
    DCHECK(name->IsString());
    v8::Local<v8::Value> value = values[i];

    std::unique_ptr<ValueMirror> valueMirror;
    std::unique_ptr<ValueMirror> getterMirror;
    std::unique_ptr<ValueMirror> setterMirror;
    if (v8::debug::AccessorPair::IsAccessorPair(value)) {
      v8::Local<v8::debug::AccessorPair> accessors =
          value.As<v8::debug::AccessorPair>();
      v8::Local<v8::Value> getter = accessors->getter();
      v8::Local<v8::Value> setter = accessors->setter();
      if (!getter->IsNull()) {
        getterMirror = ValueMirror::create(context, getter);
      }
      if (!setter->IsNull()) {
        setterMirror = ValueMirror::create(context, setter);
      }
    } else if (accessorPropertiesOnly) {
      continue;
    } else {
      valueMirror = ValueMirror::create(context, value);
    }

    mirrors.emplace_back(PrivatePropertyMirror{
        toProtocolStringWithTypeCheck(context->GetIsolate(), name),
        std::move(valueMirror), std::move(getterMirror),
        std::move(setterMirror)});
  }
  return mirrors;
}

std::unique_ptr<ValueMirror> clientMirror(v8::Local<v8::Context> context,
                                          v8::Local<v8::Object> value,
                                          const String16& subtype) {
  auto descriptionForValueSubtype =
      clientFor(context)->descriptionForValueSubtype(context, value);
  if (descriptionForValueSubtype) {
    return std::make_unique<ObjectMirror>(
        value, subtype, toString16(descriptionForValueSubtype->string()));
  }
  if (subtype == "error") {
    return std::make_unique<ObjectMirror>(
        value, RemoteObject::SubtypeEnum::Error,
        descriptionForError(context, value, ErrorType::kClient));
  }
  if (subtype == "array" && value->IsObject()) {
    v8::Isolate* isolate = context->GetIsolate();
    v8::TryCatch tryCatch(isolate);
    v8::Local<v8::Value> lengthValue;
    if (value->Get(context, toV8String(isolate, "length"))
            .ToLocal(&lengthValue)) {
      if (lengthValue->IsInt32()) {
        return std::make_unique<ObjectMirror>(
            value, RemoteObject::SubtypeEnum::Array,
            descriptionForCollection(isolate, value,
                                     lengthValue.As<v8::Int32>()->Value()));
      }
    }
  }
  return std::make_unique<ObjectMirror>(
      value, descriptionForObject(context->GetIsolate(), value));
}

std::unique_ptr<ValueMirror> ValueMirror::create(v8::Local<v8::Context> context,
                                                 v8::Local<v8::Value> value) {
  v8::Isolate* isolate = context->GetIsolate();
  if (value->IsNull()) {
    return std::make_unique<PrimitiveValueMirror>(
        isolate, value.As<v8::Primitive>(), RemoteObject::TypeEnum::Object);
  }
  if (value->IsBoolean()) {
    return std::make_unique<PrimitiveValueMirror>(
        isolate, value.As<v8::Primitive>(), RemoteObject::TypeEnum::Boolean);
  }
  if (value->IsNumber()) {
    return std::make_unique<NumberMirror>(isolate, value.As<v8::Number>());
  }
  if (value->IsString()) {
    return std::make_unique<PrimitiveValueMirror>(
        isolate, value.As<v8::Primitive>(), RemoteObject::TypeEnum::String);
  }
  if (value->IsBigInt()) {
    return std::make_unique<BigIntMirror>(isolate, value.As<v8::BigInt>());
  }
  if (value->IsSymbol()) {
    return std::make_unique<SymbolMirror>(isolate, value.As<v8::Symbol>());
  }
  if (value->IsUndefined()) {
    return std::make_unique<PrimitiveValueMirror>(
        isolate, value.As<v8::Primitive>(), RemoteObject::TypeEnum::Undefined);
  }
  if (!value->IsObject()) {
    return nullptr;
  }
  v8::Local<v8::Object> object = value.As<v8::Object>();
  auto clientSubtype = clientFor(context)->valueSubtype(object);
  if (clientSubtype) {
    String16 subtype = toString16(clientSubtype->string());
    return clientMirror(context, object, subtype);
  }
  if (object->IsRegExp()) {
    v8::Local<v8::RegExp> regexp = object.As<v8::RegExp>();
    return std::make_unique<ObjectMirror>(
        regexp, RemoteObject::SubtypeEnum::Regexp,
        descriptionForRegExp(isolate, regexp));
  }
  if (object->IsProxy()) {
    v8::Local<v8::Proxy> proxy = object.As<v8::Proxy>();
    return std::make_unique<ObjectMirror>(proxy,
                                          RemoteObject::SubtypeEnum::Proxy,
                                          descriptionForProxy(isolate, proxy));
  }
  if (object->IsFunction()) {
    v8::Local<v8::Function> function = object.As<v8::Function>();
    return std::make_unique<FunctionMirror>(function);
  }
  if (object->IsDate()) {
    v8::Local<v8::Date> date = object.As<v8::Date>();
    return std::make_unique<ObjectMirror>(date, RemoteObject::SubtypeEnum::Date,
                                          descriptionForDate(context, date));
  }
  if (object->IsPromise()) {
    v8::Local<v8::Promise> promise = object.As<v8::Promise>();
    return std::make_unique<ObjectMirror>(
        promise, RemoteObject::SubtypeEnum::Promise,
        descriptionForObject(isolate, promise));
  }
  if (object->IsNativeError()) {
    return std::make_unique<ObjectMirror>(
        object, RemoteObject::SubtypeEnum::Error,
        descriptionForError(context, object, ErrorType::kNative));
  }
  if (object->IsMap()) {
    v8::Local<v8::Map> map = object.As<v8::Map>();
    return std::make_unique<ObjectMirror>(
        map, RemoteObject::SubtypeEnum::Map,
        descriptionForCollection(isolate, map, map->Size()));
  }
  if (object->IsSet()) {
    v8::Local<v8::Set> set = object.As<v8::Set>();
    return std::make_unique<ObjectMirror>(
        set, RemoteObject::SubtypeEnum::Set,
        descriptionForCollection(isolate, set, set->Size()));
  }
  if (object->IsWeakMap()) {
    return std::make_unique<ObjectMirror>(
        object, RemoteObject::SubtypeEnum::Weakmap,
        descriptionForObject(isolate, object));
  }
  if (object->IsWeakSet()) {
    return std::make_unique<ObjectMirror>(
        object, RemoteObject::SubtypeEnum::Weakset,
        descriptionForObject(isolate, object));
  }
  if (object->IsMapIterator() || object->IsSetIterator()) {
    return std::make_unique<ObjectMirror>(
        object, RemoteObject::SubtypeEnum::Iterator,
        descriptionForObject(isolate, object));
  }
  if (object->IsGeneratorObject()) {
    return std::make_unique<ObjectMirror>(
        object, RemoteObject::SubtypeEnum::Generator,
        descriptionForObject(isolate, object));
  }
  if (object->IsTypedArray()) {
    v8::Local<v8::TypedArray> array = object.As<v8::TypedArray>();
    return std::make_unique<ObjectMirror>(
        array, RemoteObject::SubtypeEnum::Typedarray,
        descriptionForCollection(isolate, array, array->Length()));
  }
  if (object->IsArrayBuffer()) {
    v8::Local<v8::ArrayBuffer> buffer = object.As<v8::ArrayBuffer>();
    return std::make_unique<ObjectMirror>(
        buffer, RemoteObject::SubtypeEnum::Arraybuffer,
        descriptionForCollection(isolate, buffer, buffer->ByteLength()));
  }
  if (object->IsSharedArrayBuffer()) {
    v8::Local<v8::SharedArrayBuffer> buffer =
        object.As<v8::SharedArrayBuffer>();
    return std::make_unique<ObjectMirror>(
        buffer, RemoteObject::SubtypeEnum::Arraybuffer,
        descriptionForCollection(isolate, buffer, buffer->ByteLength()));
  }
  if (object->IsDataView()) {
    v8::Local<v8::DataView> view = object.As<v8::DataView>();
    return std::make_unique<ObjectMirror>(
        view, RemoteObject::SubtypeEnum::Dataview,
        descriptionForCollection(isolate, view, view->ByteLength()));
  }
  if (object->IsWasmMemoryObject()) {
    v8::Local<v8::WasmMemoryObject> memory = object.As<v8::WasmMemoryObject>();
    return std::make_unique<ObjectMirror>(
        memory, RemoteObject::SubtypeEnum::Webassemblymemory,
        descriptionForCollection(
            isolate, memory, memory->Buffer()->ByteLength() / kWasmPageSize));
  }
#if V8_ENABLE_WEBASSEMBLY
  if (v8::debug::WasmValueObject::IsWasmValueObject(object)) {
    v8::Local<v8::debug::WasmValueObject> value_object =
        object.As<v8::debug::WasmValueObject>();
    return std::make_unique<ObjectMirror>(
        value_object, RemoteObject::SubtypeEnum::Wasmvalue,
        descriptionForWasmValueObject(context, value_object));
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  if (!value->IsObject()) {
    return nullptr;
  }
  V8InternalValueType internalType = v8InternalValueTypeFrom(context, object);
  if (internalType == V8InternalValueType::kScopeList) {
    v8::Local<v8::Array> array = value.As<v8::Array>();
    return std::make_unique<ObjectMirror>(array, "internal#scopeList",
                                          descriptionForScopeList(array));
  }
  if (internalType == V8InternalValueType::kPrivateMethodList) {
    v8::Local<v8::Array> array = object.As<v8::Array>();
    return std::make_unique<ObjectMirror>(
        array, "internal#privateMethodList",
        descriptionForPrivateMethodList(array));
  }
  if (internalType == V8InternalValueType::kEntry) {
    return std::make_unique<ObjectMirror>(object, "internal#entry",
                                          descriptionForEntry(context, object));
  }
  if (internalType == V8InternalValueType::kScope) {
    return std::make_unique<ObjectMirror>(object, "internal#scope",
                                          descriptionForScope(context, object));
  }
  if (internalType == V8InternalValueType::kPrivateMethod) {
    return std::make_unique<ObjectMirror>(
        object, "internal#privateMethod",
        descriptionForPrivateMethod(context, object));
  }
  size_t length = 0;
  if (isArrayLike(context, object, &length)) {
    return std::make_unique<ObjectMirror>(
        object, RemoteObject::SubtypeEnum::Array,
        descriptionForCollection(isolate, object, length));
  }
  return std::make_unique<ObjectMirror>(object,
                                        descriptionForObject(isolate, object));
}

}  // namespace v8_inspector
