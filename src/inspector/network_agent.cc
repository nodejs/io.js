#include "network_agent.h"
#include "network_inspector.h"

namespace node {
namespace inspector {
namespace protocol {

std::unique_ptr<Network::Request> createRequest(
    const String& url,
    const String& method,
    std::unique_ptr<Network::Headers> headers) {
  return Network::Request::create()
      .setUrl(url)
      .setMethod(method)
      .setHeaders(std::move(headers))
      .build();
}

std::unique_ptr<Network::Response> createResponse(
    const String& url,
    int status,
    const String& statusText,
    std::unique_ptr<Network::Headers> headers) {
  return Network::Response::create()
      .setUrl(url)
      .setStatus(status)
      .setStatusText(statusText)
      .setHeaders(std::move(headers))
      .build();
}

NetworkAgent::NetworkAgent(NetworkInspector* inspector)
    : inspector_(inspector) {
  event_notifier_map_["requestWillBeSent"] = &NetworkAgent::requestWillBeSent;
  event_notifier_map_["responseReceived"] = &NetworkAgent::responseReceived;
  event_notifier_map_["loadingFailed"] = &NetworkAgent::loadingFailed;
  event_notifier_map_["loadingFinished"] = &NetworkAgent::loadingFinished;
}

void NetworkAgent::emitNotification(
    const String& event, std::unique_ptr<protocol::DictionaryValue> params) {
  if (!inspector_->IsEnabled()) return;
  auto it = event_notifier_map_.find(event);
  if (it != event_notifier_map_.end()) {
    (this->*(it->second))(std::move(params));
  }
}

void NetworkAgent::Wire(UberDispatcher* dispatcher) {
  frontend_ = std::make_unique<Network::Frontend>(dispatcher->channel());
  Network::Dispatcher::wire(dispatcher, this);
}

DispatchResponse NetworkAgent::enable() {
  inspector_->Enable();
  return DispatchResponse::OK();
}

DispatchResponse NetworkAgent::disable() {
  inspector_->Disable();
  return DispatchResponse::OK();
}

void NetworkAgent::requestWillBeSent(
    std::unique_ptr<protocol::DictionaryValue> params) {
  String request_id;
  params->getString("requestId", &request_id);
  double timestamp;
  params->getDouble("timestamp", &timestamp);
  double wall_time;
  params->getDouble("wallTime", &wall_time);
  auto request = params->getObject("request");
  String url;
  request->getString("url", &url);
  String method;
  request->getString("method", &method);

  ErrorSupport errors;
  auto headers =
      Network::Headers::fromValue(request->getObject("headers"), &errors);
  if (errors.hasErrors()) {
    headers = std::make_unique<Network::Headers>(DictionaryValue::create());
  }

  frontend_->requestWillBeSent(request_id,
                               createRequest(url, method, std::move(headers)),
                               timestamp,
                               wall_time);
}

void NetworkAgent::responseReceived(
    std::unique_ptr<protocol::DictionaryValue> params) {
  String request_id;
  params->getString("requestId", &request_id);
  double timestamp;
  params->getDouble("timestamp", &timestamp);
  String type;
  params->getString("type", &type);
  auto response = params->getObject("response");
  String url;
  response->getString("url", &url);
  int status;
  response->getInteger("status", &status);
  String statusText;
  response->getString("statusText", &statusText);

  ErrorSupport errors;
  auto headers =
      Network::Headers::fromValue(response->getObject("headers"), &errors);
  if (errors.hasErrors()) {
    headers = std::make_unique<Network::Headers>(DictionaryValue::create());
  }

  frontend_->responseReceived(
      request_id,
      timestamp,
      type,
      createResponse(url, status, statusText, std::move(headers)));
}

void NetworkAgent::loadingFailed(
    std::unique_ptr<protocol::DictionaryValue> params) {
  String request_id;
  params->getString("requestId", &request_id);
  double timestamp;
  params->getDouble("timestamp", &timestamp);
  String type;
  params->getString("type", &type);
  String error_text;
  params->getString("errorText", &error_text);

  frontend_->loadingFailed(request_id, timestamp, type, error_text);
}

void NetworkAgent::loadingFinished(
    std::unique_ptr<protocol::DictionaryValue> params) {
  String request_id;
  params->getString("requestId", &request_id);
  double timestamp;
  params->getDouble("timestamp", &timestamp);

  frontend_->loadingFinished(request_id, timestamp);
}

}  // namespace protocol
}  // namespace inspector
}  // namespace node
