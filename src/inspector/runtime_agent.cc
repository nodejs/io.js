#include "runtime_agent.h"

#include "env-inl.h"
#include "inspector_agent.h"

namespace node {
namespace inspector {
namespace protocol {

RuntimeAgent::RuntimeAgent()
    : notify_when_waiting_for_disconnect_(false),
      enabled_(false),
      is_waiting_for_debugger_(false) {}

void RuntimeAgent::Wire(UberDispatcher* dispatcher) {
  frontend_ = std::make_unique<NodeRuntime::Frontend>(dispatcher->channel());
  NodeRuntime::Dispatcher::wire(dispatcher, this);
}

DispatchResponse RuntimeAgent::notifyWhenWaitingForDisconnect(bool enabled) {
  notify_when_waiting_for_disconnect_ = enabled;
  return DispatchResponse::Success();
}

DispatchResponse RuntimeAgent::enable() {
  enabled_ = true;
  if (is_waiting_for_debugger_) {
    frontend_->waitingForDebugger();
  }
  return DispatchResponse::Success();
}

DispatchResponse RuntimeAgent::disable() {
  enabled_ = false;
  return DispatchResponse::Success();
}

void RuntimeAgent::setWaitingForDebugger() {
  is_waiting_for_debugger_ = true;
  if (enabled_) {
    frontend_->waitingForDebugger();
  }
}

void RuntimeAgent::unsetWaitingForDebugger() {
  is_waiting_for_debugger_ = false;
}

bool RuntimeAgent::notifyWaitingForDisconnect() {
  if (notify_when_waiting_for_disconnect_) {
    frontend_->waitingForDisconnect();
    return true;
  }
  return false;
}
}  // namespace protocol
}  // namespace inspector
}  // namespace node
