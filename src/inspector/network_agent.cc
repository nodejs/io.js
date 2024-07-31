#include "network_agent.h"
#include "network_inspector.h"

namespace node {
namespace inspector {
namespace protocol {

std::unique_ptr<Network::Request> Request(const String& url,
                                          const String& method) {
  return Network::Request::create().setUrl(url).setMethod(method).build();
}

NetworkAgent::NetworkAgent(NetworkInspector* inspector)
    : inspector_(inspector) {
  event_notifier_map_["requestWillBeSent"] = &NetworkAgent::requestWillBeSent;
  event_notifier_map_["responseReceived"] = &NetworkAgent::responseReceived;
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

  frontend_->requestWillBeSent(
      request_id, Request(url, method), timestamp, wall_time);
}

void NetworkAgent::responseReceived(
    std::unique_ptr<protocol::DictionaryValue> params) {
  String request_id;
  params->getString("requestId", &request_id);
  double timestamp;
  params->getDouble("timestamp", &timestamp);

  frontend_->responseReceived(request_id, timestamp);
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
