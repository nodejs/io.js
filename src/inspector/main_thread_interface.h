#ifndef SRC_INSPECTOR_MAIN_THREAD_INTERFACE_H_
#define SRC_INSPECTOR_MAIN_THREAD_INTERFACE_H_

#if !HAVE_INSPECTOR
#error("This header can only be used when inspector is enabled")
#endif

#include "inspector_agent.h"
#include "node_mutex.h"

#include <atomic>
#include <deque>
#include <memory>
#include <unordered_map>

namespace v8_inspector {
class StringBuffer;
class StringView;
}  // namespace v8_inspector

namespace node {
namespace inspector {
class MainThreadInterface;

class Request {
 public:
  virtual void Call(MainThreadInterface*) = 0;
  virtual ~Request() = default;
};

class Deletable {
 public:
  virtual ~Deletable() = default;
};

std::unique_ptr<v8_inspector::StringBuffer> Utf8ToStringView(
    const std::string_view message);

using MessageQueue = std::deque<std::unique_ptr<Request>>;

class MainThreadHandle : public std::enable_shared_from_this<MainThreadHandle> {
 public:
  explicit MainThreadHandle(MainThreadInterface* main_thread)
                            : main_thread_(main_thread) {
  }
  ~MainThreadHandle() {
    Mutex::ScopedLock scoped_lock(block_lock_);
    CHECK_NULL(main_thread_);  // main_thread_ should have called Reset
  }
  std::unique_ptr<InspectorSession> Connect(
      std::unique_ptr<InspectorSessionDelegate> delegate,
      bool prevent_shutdown);
  int newObjectId() {
    return ++next_object_id_;
  }
  bool Post(std::unique_ptr<Request> request);
  std::unique_ptr<InspectorSessionDelegate> MakeDelegateThreadSafe(
      std::unique_ptr<InspectorSessionDelegate> delegate);
  bool Expired();
  void SetTargetSessionId(int target_session_id) {
    target_session_id_ = target_session_id;
  }
  std::optional<int> GetTargetSessionId() { return target_session_id_; }

 private:
  void Reset();

  MainThreadInterface* main_thread_;
  Mutex block_lock_;
  int next_session_id_ = 0;
  std::atomic_int next_object_id_ = {1};
  std::optional<int> target_session_id_ = std::nullopt;

  friend class MainThreadInterface;
};

class MainThreadInterface :
    public std::enable_shared_from_this<MainThreadInterface> {
 public:
  explicit MainThreadInterface(Agent* agent);
  ~MainThreadInterface();

  void DispatchMessages();
  void Post(std::unique_ptr<Request> request);
  bool WaitForFrontendEvent();
  void StopWaitingForFrontendEvent();
  std::shared_ptr<MainThreadHandle> GetHandle();
  Agent* inspector_agent() {
    return agent_;
  }
  void AddObject(int handle, std::unique_ptr<Deletable> object);
  Deletable* GetObject(int id);
  Deletable* GetObjectIfExists(int id);
  void RemoveObject(int handle);

 private:
  MessageQueue requests_;
  Mutex requests_lock_;   // requests_ live across threads
  // This queue is to maintain the order of the messages for the cases
  // when we reenter the DispatchMessages function.
  MessageQueue dispatching_message_queue_;
  bool dispatching_messages_ = false;
  // This flag indicates an internal request to exit the loop in
  // WaitForFrontendEvent(). It's set to true by calling
  // StopWaitingForFrontendEvent().
  bool stop_waiting_for_frontend_event_requested_ = false;
  ConditionVariable incoming_message_cond_;
  // Used from any thread
  Agent* const agent_;
  std::shared_ptr<MainThreadHandle> handle_;
  std::unordered_map<int, std::unique_ptr<Deletable>> managed_objects_;
};

}  // namespace inspector
}  // namespace node
#endif  // SRC_INSPECTOR_MAIN_THREAD_INTERFACE_H_
