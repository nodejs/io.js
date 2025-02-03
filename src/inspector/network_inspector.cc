#include "network_inspector.h"

namespace node {
namespace inspector {

NetworkInspector::NetworkInspector(Environment* env)
    : enabled_(false), env_(env) {
  network_agent_ = std::make_unique<protocol::NetworkAgent>(this);
}
NetworkInspector::~NetworkInspector() {
  network_agent_.reset();
}

void NetworkInspector::Wire(protocol::UberDispatcher* dispatcher) {
  network_agent_->Wire(dispatcher);
}

bool NetworkInspector::canEmit(const std::string& domain) {
  return domain == "Network";
}

void NetworkInspector::emitNotification(
    const std::string& domain,
    const std::string& method,
    std::unique_ptr<protocol::DictionaryValue> params) {
  if (domain == "Network") {
    network_agent_->emitNotification(method, std::move(params));
  } else {
    UNREACHABLE("Unknown domain");
  }
}

void NetworkInspector::Enable() {
  if (auto agent = env_->inspector_agent()) {
    agent->EnableNetworkTracking();
  }
  enabled_ = true;
}

void NetworkInspector::Disable() {
  if (auto agent = env_->inspector_agent()) {
    agent->DisableNetworkTracking();
  }
  enabled_ = false;
}

}  // namespace inspector
}  // namespace node
