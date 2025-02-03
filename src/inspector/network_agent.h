#ifndef SRC_INSPECTOR_NETWORK_AGENT_H_
#define SRC_INSPECTOR_NETWORK_AGENT_H_

#include "node/inspector/protocol/Network.h"

#include <unordered_map>

namespace node {

namespace inspector {
class NetworkInspector;

namespace protocol {

class NetworkAgent : public Network::Backend {
 public:
  explicit NetworkAgent(NetworkInspector* inspector);

  void Wire(UberDispatcher* dispatcher);

  DispatchResponse enable() override;

  DispatchResponse disable() override;

  void emitNotification(const String& event,
                        std::unique_ptr<protocol::DictionaryValue> params);

  void requestWillBeSent(std::unique_ptr<protocol::DictionaryValue> params);

  void responseReceived(std::unique_ptr<protocol::DictionaryValue> params);

  void loadingFailed(std::unique_ptr<protocol::DictionaryValue> params);

  void loadingFinished(std::unique_ptr<protocol::DictionaryValue> params);

 private:
  NetworkInspector* inspector_;
  std::shared_ptr<Network::Frontend> frontend_;
  using EventNotifier =
      void (NetworkAgent::*)(std::unique_ptr<protocol::DictionaryValue>);
  std::unordered_map<String, EventNotifier> event_notifier_map_;
};

}  // namespace protocol
}  // namespace inspector
}  // namespace node

#endif  // SRC_INSPECTOR_NETWORK_AGENT_H_
