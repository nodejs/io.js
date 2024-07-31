#ifndef SRC_INSPECTOR_RUNTIME_AGENT_H_
#define SRC_INSPECTOR_RUNTIME_AGENT_H_

#include "node/inspector/protocol/NodeRuntime.h"
#include "v8.h"

namespace node {
class Environment;

namespace inspector {
namespace protocol {

class RuntimeAgent : public NodeRuntime::Backend {
 public:
  RuntimeAgent();

  void Wire(UberDispatcher* dispatcher);

  DispatchResponse notifyWhenWaitingForDisconnect(bool enabled) override;

  DispatchResponse enable() override;

  DispatchResponse disable() override;

  bool notifyWaitingForDisconnect();

  void setWaitingForDebugger();

  void unsetWaitingForDebugger();

 private:
  std::shared_ptr<NodeRuntime::Frontend> frontend_;
  bool notify_when_waiting_for_disconnect_;
  bool enabled_;
  bool is_waiting_for_debugger_;
};
}  // namespace protocol
}  // namespace inspector
}  // namespace node

#endif  // SRC_INSPECTOR_RUNTIME_AGENT_H_
