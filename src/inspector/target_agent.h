#ifndef SRC_INSPECTOR_TARGET_AGENT_H_
#define SRC_INSPECTOR_TARGET_AGENT_H_

#include <unordered_map>
#include <vector>
#include "inspector/worker_inspector.h"
#include "node/inspector/protocol/Target.h"

namespace node {

namespace inspector {
class TargetInspector;

namespace protocol {

struct TargetInfo {
  std::string target_id;
  std::string type;
  std::string title;
  std::string url;
  std::shared_ptr<MainThreadHandle> worker;
  bool attached;
};

class TargetAgent : public Target::Backend,
                    public std::enable_shared_from_this<TargetAgent> {
 public:
  void Wire(UberDispatcher* dispatcher);

  void createAndAttachIfNecessary(std::shared_ptr<MainThreadHandle> worker,
                                  const std::string& title,
                                  const std::string& url);

  DispatchResponse setAutoAttach(bool auto_attach,
                                 bool wait_for_debugger_on_start) override;

  void listenWorker(std::weak_ptr<WorkerManager> worker_manager);
  void reset();
  static std::unordered_map<int, std::shared_ptr<MainThreadHandle>>
      target_session_id_worker_map_;

  bool isThisThread(MainThreadHandle* worker) { return worker == main_thread_; }

 private:
  int getNextTargetId();
  int getNextSessionId();
  void targetCreated(const std::string& target_id,
                     const std::string& type,
                     const std::string& title,
                     const std::string& url);
  void attachedToTarget(std::shared_ptr<MainThreadHandle> worker,
                        const std::string& target_id,
                        const std::string& type,
                        const std::string& title,
                        const std::string& url);
  std::shared_ptr<Target::Frontend> frontend_;
  std::weak_ptr<WorkerManager> worker_manager_;
  static int next_session_id_;
  int next_target_id_ = 1;
  std::unique_ptr<WorkerManagerEventHandle> worker_event_handle_ = nullptr;
  bool auto_attach_ = false;
  // TODO(islandryu): If false, implement it so that each thread does not wait
  // for the worker to execute.
  bool wait_for_debugger_on_start_ = true;
  std::vector<TargetInfo> targets_;
  MainThreadHandle* main_thread_;
};

}  // namespace protocol
}  // namespace inspector
}  // namespace node

#endif  // SRC_INSPECTOR_TARGET_AGENT_H_
