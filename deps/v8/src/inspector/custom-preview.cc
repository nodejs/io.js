// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/custom-preview.h"

#include "../../third_party/inspector_protocol/crdtp/json.h"
#include "include/v8-container.h"
#include "include/v8-context.h"
#include "include/v8-function.h"
#include "include/v8-json.h"
#include "include/v8-microtask-queue.h"
#include "src/debug/debug-interface.h"
#include "src/inspector/injected-script.h"
#include "src/inspector/inspected-context.h"
#include "src/inspector/string-util.h"
#include "src/inspector/v8-console-message.h"
#include "src/inspector/v8-debugger.h"
#include "src/inspector/v8-inspector-impl.h"
#include "src/inspector/v8-stack-trace-impl.h"

namespace v8_inspector {

using protocol::Runtime::CustomPreview;

namespace {
void reportError(v8::Local<v8::Context> context, const v8::TryCatch& tryCatch) {
  DCHECK(tryCatch.HasCaught());
  v8::Isolate* isolate = context->GetIsolate();
  V8InspectorImpl* inspector =
      static_cast<V8InspectorImpl*>(v8::debug::GetInspector(isolate));
  int contextId = InspectedContext::contextId(context);
  int groupId = inspector->contextGroupId(contextId);
  v8::Local<v8::String> message = tryCatch.Message()->Get();
  v8::Local<v8::String> prefix =
      toV8String(isolate, "Custom Formatter Failed: ");
  message = v8::String::Concat(isolate, prefix, message);
  v8::LocalVector<v8::Value> arguments(isolate);
  arguments.push_back(message);
  V8ConsoleMessageStorage* storage =
      inspector->ensureConsoleMessageStorage(groupId);
  if (!storage) return;
  storage->addMessage(V8ConsoleMessage::createForConsoleAPI(
      context, contextId, groupId, inspector,
      inspector->client()->currentTimeMS(), ConsoleAPIType::kError,
      {arguments.begin(), arguments.end()}, String16(), nullptr));
}

void reportError(v8::Local<v8::Context> context, const v8::TryCatch& tryCatch,
                 const String16& message) {
  v8::Isolate* isolate = context->GetIsolate();
  isolate->ThrowException(toV8String(isolate, message));
  reportError(context, tryCatch);
}

InjectedScript* getInjectedScript(v8::Local<v8::Context> context,
                                  int sessionId) {
  v8::Isolate* isolate = context->GetIsolate();
  V8InspectorImpl* inspector =
      static_cast<V8InspectorImpl*>(v8::debug::GetInspector(isolate));
  InspectedContext* inspectedContext =
      inspector->getContext(InspectedContext::contextId(context));
  if (!inspectedContext) return nullptr;
  return inspectedContext->getInjectedScript(sessionId);
}

bool substituteObjectTags(int sessionId, const String16& groupName,
                          v8::Local<v8::Context> context,
                          v8::Local<v8::Array> jsonML, int maxDepth) {
  if (!jsonML->Length()) return true;
  v8::Isolate* isolate = context->GetIsolate();
  v8::TryCatch tryCatch(isolate);

  if (maxDepth <= 0) {
    reportError(context, tryCatch,
                "Too deep hierarchy of inlined custom previews");
    return false;
  }

  v8::Local<v8::Value> firstValue;
  if (!jsonML->Get(context, 0).ToLocal(&firstValue)) {
    reportError(context, tryCatch);
    return false;
  }
  v8::Local<v8::String> objectLiteral = toV8String(isolate, "object");
  if (jsonML->Length() == 2 && firstValue->IsString() &&
      firstValue.As<v8::String>()->StringEquals(objectLiteral)) {
    v8::Local<v8::Value> attributesValue;
    if (!jsonML->Get(context, 1).ToLocal(&attributesValue)) {
      reportError(context, tryCatch);
      return false;
    }
    if (!attributesValue->IsObject()) {
      reportError(context, tryCatch, "attributes should be an Object");
      return false;
    }
    v8::Local<v8::Object> attributes = attributesValue.As<v8::Object>();
    v8::Local<v8::Value> originValue;
    if (!attributes->Get(context, objectLiteral).ToLocal(&originValue)) {
      reportError(context, tryCatch);
      return false;
    }
    if (originValue->IsUndefined()) {
      reportError(context, tryCatch,
                  "obligatory attribute \"object\" isn't specified");
      return false;
    }

    v8::Local<v8::Value> configValue;
    if (!attributes->Get(context, toV8String(isolate, "config"))
             .ToLocal(&configValue)) {
      reportError(context, tryCatch);
      return false;
    }

    InjectedScript* injectedScript = getInjectedScript(context, sessionId);
    if (!injectedScript) {
      reportError(context, tryCatch, "cannot find context with specified id");
      return false;
    }
    std::unique_ptr<protocol::Runtime::RemoteObject> wrapper;
    protocol::Response response = injectedScript->wrapObject(
        originValue, groupName, WrapOptions({WrapMode::kIdOnly}), configValue,
        maxDepth - 1, &wrapper);
    if (!response.IsSuccess() || !wrapper) {
      reportError(context, tryCatch, "cannot wrap value");
      return false;
    }
    std::vector<uint8_t> json;
    v8_crdtp::json::ConvertCBORToJSON(v8_crdtp::SpanFrom(wrapper->Serialize()),
                                      &json);
    v8::Local<v8::Value> jsonWrapper;
    v8_inspector::StringView serialized(json.data(), json.size());
    if (!v8::JSON::Parse(context, toV8String(isolate, serialized))
             .ToLocal(&jsonWrapper)) {
      reportError(context, tryCatch, "cannot wrap value");
      return false;
    }
    if (jsonML->Set(context, 1, jsonWrapper).IsNothing()) {
      reportError(context, tryCatch);
      return false;
    }
  } else {
    for (uint32_t i = 0; i < jsonML->Length(); ++i) {
      v8::Local<v8::Value> value;
      if (!jsonML->Get(context, i).ToLocal(&value)) {
        reportError(context, tryCatch);
        return false;
      }
      if (value->IsArray() && value.As<v8::Array>()->Length() > 0 &&
          !substituteObjectTags(sessionId, groupName, context,
                                value.As<v8::Array>(), maxDepth - 1)) {
        return false;
      }
    }
  }
  return true;
}

void bodyCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::TryCatch tryCatch(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Object> bodyConfig = info.Data().As<v8::Object>();

  v8::Local<v8::Value> objectValue;
  if (!bodyConfig->Get(context, toV8String(isolate, "object"))
           .ToLocal(&objectValue)) {
    reportError(context, tryCatch);
    return;
  }
  if (!objectValue->IsObject()) {
    reportError(context, tryCatch, "object should be an Object");
    return;
  }
  v8::Local<v8::Object> object = objectValue.As<v8::Object>();

  v8::Local<v8::Value> formatterValue;
  if (!bodyConfig->Get(context, toV8String(isolate, "formatter"))
           .ToLocal(&formatterValue)) {
    reportError(context, tryCatch);
    return;
  }
  if (!formatterValue->IsObject()) {
    reportError(context, tryCatch, "formatter should be an Object");
    return;
  }
  v8::Local<v8::Object> formatter = formatterValue.As<v8::Object>();

  v8::Local<v8::Value> bodyValue;
  if (!formatter->Get(context, toV8String(isolate, "body"))
           .ToLocal(&bodyValue)) {
    reportError(context, tryCatch);
    return;
  }
  if (!bodyValue->IsFunction()) {
    reportError(context, tryCatch, "body should be a Function");
    return;
  }
  v8::Local<v8::Function> bodyFunction = bodyValue.As<v8::Function>();

  v8::Local<v8::Value> configValue;
  if (!bodyConfig->Get(context, toV8String(isolate, "config"))
           .ToLocal(&configValue)) {
    reportError(context, tryCatch);
    return;
  }

  v8::Local<v8::Value> sessionIdValue;
  if (!bodyConfig->Get(context, toV8String(isolate, "sessionId"))
           .ToLocal(&sessionIdValue)) {
    reportError(context, tryCatch);
    return;
  }
  if (!sessionIdValue->IsInt32()) {
    reportError(context, tryCatch, "sessionId should be an Int32");
    return;
  }

  v8::Local<v8::Value> groupNameValue;
  if (!bodyConfig->Get(context, toV8String(isolate, "groupName"))
           .ToLocal(&groupNameValue)) {
    reportError(context, tryCatch);
    return;
  }
  if (!groupNameValue->IsString()) {
    reportError(context, tryCatch, "groupName should be a string");
    return;
  }

  v8::Local<v8::Value> formattedValue;
  v8::Local<v8::Value> args[] = {object, configValue};
  if (!bodyFunction->Call(context, formatter, 2, args)
           .ToLocal(&formattedValue)) {
    reportError(context, tryCatch);
    return;
  }
  if (!formattedValue->IsArray()) {
    reportError(context, tryCatch, "body should return an Array");
    return;
  }
  v8::Local<v8::Array> jsonML = formattedValue.As<v8::Array>();
  if (jsonML->Length() &&
      !substituteObjectTags(
          sessionIdValue.As<v8::Int32>()->Value(),
          toProtocolString(isolate, groupNameValue.As<v8::String>()), context,
          jsonML, kMaxCustomPreviewDepth)) {
    return;
  }
  info.GetReturnValue().Set(jsonML);
}
}  // anonymous namespace

void generateCustomPreview(int sessionId, const String16& groupName,
                           v8::Local<v8::Object> object,
                           v8::MaybeLocal<v8::Value> maybeConfig, int maxDepth,
                           std::unique_ptr<CustomPreview>* preview) {
  v8::Local<v8::Context> context;
  if (!object->GetCreationContext().ToLocal(&context)) {
    return;
  }

  v8::Isolate* isolate = context->GetIsolate();
  v8::MicrotasksScope microtasksScope(context,
                                      v8::MicrotasksScope::kDoNotRunMicrotasks);
  v8::TryCatch tryCatch(isolate);

  v8::Local<v8::Value> configValue;
  if (!maybeConfig.ToLocal(&configValue)) configValue = v8::Undefined(isolate);

  v8::Local<v8::Object> global = context->Global();
  v8::Local<v8::Value> formattersValue;
  if (!global->Get(context, toV8String(isolate, "devtoolsFormatters"))
           .ToLocal(&formattersValue)) {
    reportError(context, tryCatch);
    return;
  }
  if (!formattersValue->IsArray()) return;
  v8::Local<v8::Array> formatters = formattersValue.As<v8::Array>();
  v8::Local<v8::String> headerLiteral = toV8String(isolate, "header");
  v8::Local<v8::String> hasBodyLiteral = toV8String(isolate, "hasBody");
  for (uint32_t i = 0; i < formatters->Length(); ++i) {
    v8::Local<v8::Value> formatterValue;
    if (!formatters->Get(context, i).ToLocal(&formatterValue)) {
      reportError(context, tryCatch);
      return;
    }
    if (!formatterValue->IsObject()) {
      reportError(context, tryCatch, "formatter should be an Object");
      return;
    }
    v8::Local<v8::Object> formatter = formatterValue.As<v8::Object>();

    v8::Local<v8::Value> headerValue;
    if (!formatter->Get(context, headerLiteral).ToLocal(&headerValue)) {
      reportError(context, tryCatch);
      return;
    }
    if (!headerValue->IsFunction()) {
      reportError(context, tryCatch, "header should be a Function");
      return;
    }
    v8::Local<v8::Function> headerFunction = headerValue.As<v8::Function>();

    v8::Local<v8::Value> formattedValue;
    v8::Local<v8::Value> args[] = {object, configValue};
    if (!headerFunction->Call(context, formatter, 2, args)
             .ToLocal(&formattedValue)) {
      reportError(context, tryCatch);
      return;
    }
    if (!formattedValue->IsArray()) continue;
    v8::Local<v8::Array> jsonML = formattedValue.As<v8::Array>();

    v8::Local<v8::Value> hasBodyFunctionValue;
    if (!formatter->Get(context, hasBodyLiteral)
             .ToLocal(&hasBodyFunctionValue)) {
      reportError(context, tryCatch);
      return;
    }
    if (!hasBodyFunctionValue->IsFunction()) continue;
    v8::Local<v8::Function> hasBodyFunction =
        hasBodyFunctionValue.As<v8::Function>();
    v8::Local<v8::Value> hasBodyValue;
    if (!hasBodyFunction->Call(context, formatter, 2, args)
             .ToLocal(&hasBodyValue)) {
      reportError(context, tryCatch);
      return;
    }
    bool hasBody = hasBodyValue->ToBoolean(isolate)->Value();

    if (jsonML->Length() && !substituteObjectTags(sessionId, groupName, context,
                                                  jsonML, maxDepth)) {
      return;
    }

    v8::Local<v8::String> header;
    if (!v8::JSON::Stringify(context, jsonML).ToLocal(&header)) {
      reportError(context, tryCatch);
      return;
    }

    v8::Local<v8::Function> bodyFunction;
    if (hasBody) {
      v8::Local<v8::Object> bodyConfig = v8::Object::New(isolate);
      if (bodyConfig
              ->CreateDataProperty(context, toV8String(isolate, "sessionId"),
                                   v8::Integer::New(isolate, sessionId))
              .IsNothing()) {
        reportError(context, tryCatch);
        return;
      }
      if (bodyConfig
              ->CreateDataProperty(context, toV8String(isolate, "formatter"),
                                   formatter)
              .IsNothing()) {
        reportError(context, tryCatch);
        return;
      }
      if (bodyConfig
              ->CreateDataProperty(context, toV8String(isolate, "groupName"),
                                   toV8String(isolate, groupName))
              .IsNothing()) {
        reportError(context, tryCatch);
        return;
      }
      if (bodyConfig
              ->CreateDataProperty(context, toV8String(isolate, "config"),
                                   configValue)
              .IsNothing()) {
        reportError(context, tryCatch);
        return;
      }
      if (bodyConfig
              ->CreateDataProperty(context, toV8String(isolate, "object"),
                                   object)
              .IsNothing()) {
        reportError(context, tryCatch);
        return;
      }
      if (!v8::Function::New(context, bodyCallback, bodyConfig)
               .ToLocal(&bodyFunction)) {
        reportError(context, tryCatch);
        return;
      }
    }
    *preview = CustomPreview::create()
                   .setHeader(toProtocolString(isolate, header))
                   .build();
    if (!bodyFunction.IsEmpty()) {
      InjectedScript* injectedScript = getInjectedScript(context, sessionId);
      if (!injectedScript) {
        reportError(context, tryCatch, "cannot find context with specified id");
        return;
      }
      (*preview)->setBodyGetterId(
          injectedScript->bindObject(bodyFunction, groupName));
    }
    return;
  }
}
}  // namespace v8_inspector
