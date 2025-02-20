#include "network_agent.h"
#include "inspector/protocol_helper.h"
#include "network_inspector.h"
#include "util-inl.h"
#include "v8.h"

namespace node {
namespace inspector {

using v8::EscapableHandleScope;
using v8::HandleScope;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::Value;

// Get a protocol string property from the object.
Maybe<protocol::String> ObjectGetProtocolString(v8::Local<v8::Context> context,
                                                Local<Object> object,
                                                Local<v8::String> property) {
  HandleScope handle_scope(context->GetIsolate());
  Local<Value> value;
  if (!object->Get(context, property).ToLocal(&value) || !value->IsString()) {
    return Nothing<protocol::String>();
  }
  Local<v8::String> str = value.As<v8::String>();
  return Just(ToProtocolString(context->GetIsolate(), str));
}

// Get a protocol string property from the object.
Maybe<protocol::String> ObjectGetProtocolString(v8::Local<v8::Context> context,
                                                Local<Object> object,
                                                const char* property) {
  HandleScope handle_scope(context->GetIsolate());
  return ObjectGetProtocolString(
      context, object, OneByteString(context->GetIsolate(), property));
}

// Get a protocol double property from the object.
Maybe<double> ObjectGetDouble(v8::Local<v8::Context> context,
                              Local<Object> object,
                              const char* property) {
  HandleScope handle_scope(context->GetIsolate());
  Local<Value> value;
  if (!object->Get(context, OneByteString(context->GetIsolate(), property))
           .ToLocal(&value) ||
      !value->IsNumber()) {
    return Nothing<double>();
  }
  return Just(value.As<v8::Number>()->Value());
}

// Get a protocol int property from the object.
Maybe<int> ObjectGetInt(v8::Local<v8::Context> context,
                        Local<Object> object,
                        const char* property) {
  HandleScope handle_scope(context->GetIsolate());
  Local<Value> value;
  if (!object->Get(context, OneByteString(context->GetIsolate(), property))
           .ToLocal(&value) ||
      !value->IsInt32()) {
    return Nothing<int>();
  }
  return Just(value.As<v8::Int32>()->Value());
}

// Get an object property from the object.
MaybeLocal<v8::Object> ObjectGetObject(v8::Local<v8::Context> context,
                                       Local<Object> object,
                                       const char* property) {
  EscapableHandleScope handle_scope(context->GetIsolate());
  Local<Value> value;
  if (!object->Get(context, OneByteString(context->GetIsolate(), property))
           .ToLocal(&value) ||
      !value->IsObject()) {
    return {};
  }
  return handle_scope.Escape(value.As<v8::Object>());
}

// Create a protocol::Network::Headers from the v8 object.
std::unique_ptr<protocol::Network::Headers> createHeadersFromObject(
    v8::Local<v8::Context> context, Local<Object> headers_obj) {
  HandleScope handle_scope(context->GetIsolate());

  std::unique_ptr<protocol::DictionaryValue> dict =
      protocol::DictionaryValue::create();
  Local<v8::Array> property_names;
  if (!headers_obj->GetOwnPropertyNames(context).ToLocal(&property_names)) {
    return {};
  }

  for (size_t idx = 0; idx < property_names->Length(); idx++) {
    Local<v8::Value> property_name_val;
    if (!property_names->Get(context, idx).ToLocal(&property_name_val) ||
        !property_name_val->IsString()) {
      return {};
    }
    Local<v8::String> property_name = property_name_val.As<v8::String>();
    protocol::String property_value;
    if (!ObjectGetProtocolString(context, headers_obj, property_name)
             .To(&property_value)) {
      return {};
    }
    dict->setString(ToProtocolString(context->GetIsolate(), property_name),
                    property_value);
  }

  return std::make_unique<protocol::Network::Headers>(std::move(dict));
}

// Create a protocol::Network::Request from the v8 object.
std::unique_ptr<protocol::Network::Request> createRequestFromObject(
    v8::Local<v8::Context> context, Local<Object> request) {
  HandleScope handle_scope(context->GetIsolate());
  protocol::String url;
  if (!ObjectGetProtocolString(context, request, "url").To(&url)) {
    return {};
  }
  protocol::String method;
  if (!ObjectGetProtocolString(context, request, "method").To(&method)) {
    return {};
  }
  Local<Object> headers_obj;
  if (!ObjectGetObject(context, request, "headers").ToLocal(&headers_obj)) {
    return {};
  }
  std::unique_ptr<protocol::Network::Headers> headers =
      createHeadersFromObject(context, headers_obj);
  if (!headers) {
    return {};
  }

  return protocol::Network::Request::create()
      .setUrl(url)
      .setMethod(method)
      .setHeaders(std::move(headers))
      .build();
}

// Create a protocol::Network::Response from the v8 object.
std::unique_ptr<protocol::Network::Response> createResponseFromObject(
    v8::Local<v8::Context> context, Local<Object> response) {
  HandleScope handle_scope(context->GetIsolate());
  protocol::String url;
  if (!ObjectGetProtocolString(context, response, "url").To(&url)) {
    return {};
  }
  int status;
  if (!ObjectGetInt(context, response, "status").To(&status)) {
    return {};
  }
  protocol::String statusText;
  if (!ObjectGetProtocolString(context, response, "statusText")
           .To(&statusText)) {
    return {};
  }
  Local<Object> headers_obj;
  if (!ObjectGetObject(context, response, "headers").ToLocal(&headers_obj)) {
    return {};
  }
  std::unique_ptr<protocol::Network::Headers> headers =
      createHeadersFromObject(context, headers_obj);
  if (!headers) {
    return {};
  }

  return protocol::Network::Response::create()
      .setUrl(url)
      .setStatus(status)
      .setStatusText(statusText)
      .setHeaders(std::move(headers))
      .build();
}

NetworkAgent::NetworkAgent(NetworkInspector* inspector,
                           v8_inspector::V8Inspector* v8_inspector)
    : inspector_(inspector), v8_inspector_(v8_inspector) {
  event_notifier_map_["requestWillBeSent"] = &NetworkAgent::requestWillBeSent;
  event_notifier_map_["responseReceived"] = &NetworkAgent::responseReceived;
  event_notifier_map_["loadingFailed"] = &NetworkAgent::loadingFailed;
  event_notifier_map_["loadingFinished"] = &NetworkAgent::loadingFinished;
}

void NetworkAgent::emitNotification(v8::Local<v8::Context> context,
                                    const protocol::String& event,
                                    v8::Local<v8::Object> params) {
  if (!inspector_->IsEnabled()) return;
  auto it = event_notifier_map_.find(event);
  if (it != event_notifier_map_.end()) {
    (this->*(it->second))(context, params);
  }
}

void NetworkAgent::Wire(protocol::UberDispatcher* dispatcher) {
  frontend_ =
      std::make_unique<protocol::Network::Frontend>(dispatcher->channel());
  protocol::Network::Dispatcher::wire(dispatcher, this);
}

protocol::DispatchResponse NetworkAgent::enable() {
  inspector_->Enable();
  return protocol::DispatchResponse::Success();
}

protocol::DispatchResponse NetworkAgent::disable() {
  inspector_->Disable();
  return protocol::DispatchResponse::Success();
}

void NetworkAgent::requestWillBeSent(v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> params) {
  protocol::String request_id;
  if (!ObjectGetProtocolString(context, params, "requestId").To(&request_id)) {
    return;
  }
  double timestamp;
  if (!ObjectGetDouble(context, params, "timestamp").To(&timestamp)) {
    return;
  }
  double wall_time;
  if (!ObjectGetDouble(context, params, "wallTime").To(&wall_time)) {
    return;
  }
  Local<v8::Object> request_obj;
  if (!ObjectGetObject(context, params, "request").ToLocal(&request_obj)) {
    return;
  }
  std::unique_ptr<protocol::Network::Request> request =
      createRequestFromObject(context, request_obj);
  if (!request) {
    return;
  }

  std::unique_ptr<protocol::Network::Initiator> initiator =
      protocol::Network::Initiator::create()
          .setType(protocol::Network::Initiator::TypeEnum::Script)
          .setStack(
              v8_inspector_->captureStackTrace(true)->buildInspectorObject(0))
          .build();

  frontend_->requestWillBeSent(request_id,
                               std::move(request),
                               std::move(initiator),
                               timestamp,
                               wall_time);
}

void NetworkAgent::responseReceived(v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> params) {
  protocol::String request_id;
  if (!ObjectGetProtocolString(context, params, "requestId").To(&request_id)) {
    return;
  }
  double timestamp;
  if (!ObjectGetDouble(context, params, "timestamp").To(&timestamp)) {
    return;
  }
  protocol::String type;
  if (!ObjectGetProtocolString(context, params, "type").To(&type)) {
    return;
  }
  Local<Object> response_obj;
  if (!ObjectGetObject(context, params, "response").ToLocal(&response_obj)) {
    return;
  }
  auto response = createResponseFromObject(context, response_obj);
  if (!response) {
    return;
  }

  frontend_->responseReceived(request_id, timestamp, type, std::move(response));
}

void NetworkAgent::loadingFailed(v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> params) {
  protocol::String request_id;
  if (!ObjectGetProtocolString(context, params, "requestId").To(&request_id)) {
    return;
  }
  double timestamp;
  if (!ObjectGetDouble(context, params, "timestamp").To(&timestamp)) {
    return;
  }
  protocol::String type;
  if (!ObjectGetProtocolString(context, params, "type").To(&type)) {
    return;
  }
  protocol::String error_text;
  if (!ObjectGetProtocolString(context, params, "errorText").To(&error_text)) {
    return;
  }

  frontend_->loadingFailed(request_id, timestamp, type, error_text);
}

void NetworkAgent::loadingFinished(v8::Local<v8::Context> context,
                                   Local<v8::Object> params) {
  protocol::String request_id;
  if (!ObjectGetProtocolString(context, params, "requestId").To(&request_id)) {
    return;
  }
  double timestamp;
  if (!ObjectGetDouble(context, params, "timestamp").To(&timestamp)) {
    return;
  }

  frontend_->loadingFinished(request_id, timestamp);
}

}  // namespace inspector
}  // namespace node
