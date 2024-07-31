// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/v8-serialization-duplicate-tracker.h"

#include "include/v8-context.h"
#include "include/v8-external.h"
#include "src/base/logging.h"

namespace v8_inspector {

std::unique_ptr<protocol::DictionaryValue>
V8SerializationDuplicateTracker::LinkExistingOrCreate(
    v8::Local<v8::Value> v8Value, bool* isKnown) {
  std::unique_ptr<protocol::DictionaryValue> result =
      protocol::DictionaryValue::create();

  protocol::DictionaryValue* maybeKnownSerializedValue =
      FindKnownSerializedValue(v8Value);

  if (maybeKnownSerializedValue == nullptr) {
    *isKnown = false;
    // Keep reference to the serialized value, so that
    // `weakLocalObjectReference` can be set later.
    SetKnownSerializedValue(v8Value, result.get());
  } else {
    *isKnown = true;

    String16 type;
    maybeKnownSerializedValue->getString("type", &type);
    result->setString("type", type);

    int weakLocalObjectReference;
    // If `maybeKnownSerializedValue` has no `weakLocalObjectReference` yet,
    // it's need to be set.
    if (!maybeKnownSerializedValue->getInteger("weakLocalObjectReference",
                                               &weakLocalObjectReference)) {
      weakLocalObjectReference = m_counter++;
      maybeKnownSerializedValue->setInteger("weakLocalObjectReference",
                                            weakLocalObjectReference);
    }
    result->setInteger("weakLocalObjectReference", weakLocalObjectReference);
  }

  return result;
}

void V8SerializationDuplicateTracker::SetKnownSerializedValue(
    v8::Local<v8::Value> v8Value, protocol::DictionaryValue* serializedValue) {
  m_v8ObjectToSerializedDictionary =
      m_v8ObjectToSerializedDictionary
          ->Set(m_context, v8Value,
                v8::External::New(m_context->GetIsolate(), serializedValue))
          .ToLocalChecked();
}

protocol::DictionaryValue*
V8SerializationDuplicateTracker::FindKnownSerializedValue(
    v8::Local<v8::Value> v8Value) {
  v8::Local<v8::Value> knownValue;
  if (!m_v8ObjectToSerializedDictionary->Get(m_context, v8Value)
           .ToLocal(&knownValue) ||
      knownValue->IsUndefined()) {
    return nullptr;
  }

  return static_cast<protocol::DictionaryValue*>(
      knownValue.As<v8::External>()->Value());
}

V8SerializationDuplicateTracker::V8SerializationDuplicateTracker(
    v8::Local<v8::Context> context)
    : m_context(context),
      m_counter(1),
      m_v8ObjectToSerializedDictionary(v8::Map::New(context->GetIsolate())) {}
}  // namespace v8_inspector
