#include "main_thread_interface.h"

#include "env-inl.h"
#include "simdutf.h"
#include "v8-inspector.h"

#include <functional>
#include <memory>

namespace node {
namespace inspector {
namespace {

using v8_inspector::StringBuffer;
using v8_inspector::StringView;

template <typename T>
class DeletableWrapper : public Deletable {
 public:
  explicit DeletableWrapper(std::unique_ptr<T> object)
                        : object_(std::move(object)) {}
  ~DeletableWrapper() override = default;

  static T* get(MainThreadInterface* thread, int id) {
    return
        static_cast<DeletableWrapper<T>*>(thread->GetObject(id))->object_.get();
  }

 private:
  std::unique_ptr<T> object_;
};

template <typename T>
std::unique_ptr<Deletable> WrapInDeletable(std::unique_ptr<T> object) {
  return std::unique_ptr<DeletableWrapper<T>>(
      new DeletableWrapper<T>(std::move(object)));
}

template <typename Factory>
class CreateObjectRequest : public Request {
 public:
  CreateObjectRequest(int object_id, Factory factory)
                      : object_id_(object_id), factory_(std::move(factory)) {}

  void Call(MainThreadInterface* thread) override {
    thread->AddObject(object_id_, WrapInDeletable(factory_(thread)));
  }

 private:
  int object_id_;
  Factory factory_;
};

template <typename Factory>
std::unique_ptr<Request> NewCreateRequest(int object_id, Factory factory) {
  return std::unique_ptr<Request>(
      new CreateObjectRequest<Factory>(object_id, std::move(factory)));
}

class DeleteRequest : public Request {
 public:
  explicit DeleteRequest(int object_id) : object_id_(object_id) {}

  void Call(MainThreadInterface* thread) override {
    thread->RemoveObject(object_id_);
  }

 private:
  int object_id_;
};

template <typename Target, typename Fn>
class CallRequest : public Request {
 public:
  CallRequest(int id, Fn fn) : id_(id), fn_(std::move(fn)) {}

  void Call(MainThreadInterface* thread) override {
    fn_(DeletableWrapper<Target>::get(thread, id_));
  }

 private:
  int id_;
  Fn fn_;
};

template <typename T>
class AnotherThreadObjectReference {
 public:
  AnotherThreadObjectReference(
      std::shared_ptr<MainThreadHandle> thread, int object_id)
      : thread_(thread), object_id_(object_id) {}

  template <typename Factory>
  AnotherThreadObjectReference(
      std::shared_ptr<MainThreadHandle> thread, Factory factory)
      : AnotherThreadObjectReference(thread, thread->newObjectId()) {
    thread_->Post(NewCreateRequest(object_id_, std::move(factory)));
  }
  AnotherThreadObjectReference(AnotherThreadObjectReference&) = delete;

  ~AnotherThreadObjectReference() {
    // Disappearing thread may cause a memory leak
    thread_->Post(std::make_unique<DeleteRequest>(object_id_));
  }

  template <typename Fn>
  void Call(Fn fn) const {
    using Request = CallRequest<T, Fn>;
    thread_->Post(std::unique_ptr<Request>(
        new Request(object_id_, std::move(fn))));
  }

  template <typename Arg>
  void Call(void (T::*fn)(Arg), Arg argument) const {
    Call(std::bind(Apply<Arg>, std::placeholders::_1, fn, std::move(argument)));
  }

 private:
  // This has to use non-const reference to support std::bind with non-copyable
  // types
  template <typename Argument>
  static void Apply(T* target, void (T::*fn)(Argument),
    /* NOLINT (runtime/references) */ Argument& argument) {
    (target->*fn)(std::move(argument));
  }

  std::shared_ptr<MainThreadHandle> thread_;
  const int object_id_;
};

class MainThreadSessionState {
 public:
  MainThreadSessionState(MainThreadInterface* thread, bool prevent_shutdown)
                         : thread_(thread),
                           prevent_shutdown_(prevent_shutdown) {}

  static std::unique_ptr<MainThreadSessionState> Create(
      MainThreadInterface* thread, bool prevent_shutdown) {
    return std::make_unique<MainThreadSessionState>(thread, prevent_shutdown);
  }

  void Connect(std::unique_ptr<InspectorSessionDelegate> delegate) {
    Agent* agent = thread_->inspector_agent();
    if (agent != nullptr)
      session_ = agent->Connect(std::move(delegate), prevent_shutdown_);
  }

  void Dispatch(std::unique_ptr<StringBuffer> message) {
    session_->Dispatch(message->string());
  }

 private:
  MainThreadInterface* thread_;
  bool prevent_shutdown_;
  std::unique_ptr<InspectorSession> session_;
};

class CrossThreadInspectorSession : public InspectorSession {
 public:
  CrossThreadInspectorSession(
      int id,
      std::shared_ptr<MainThreadHandle> thread,
      std::unique_ptr<InspectorSessionDelegate> delegate,
      bool prevent_shutdown)
      : state_(thread, std::bind(MainThreadSessionState::Create,
                                 std::placeholders::_1,
                                 prevent_shutdown)) {
    state_.Call(&MainThreadSessionState::Connect, std::move(delegate));
  }

  void Dispatch(const StringView& message) override {
    state_.Call(&MainThreadSessionState::Dispatch,
                StringBuffer::create(message));
  }

 private:
  AnotherThreadObjectReference<MainThreadSessionState> state_;
};

class ThreadSafeDelegate : public InspectorSessionDelegate {
 public:
  ThreadSafeDelegate(std::shared_ptr<MainThreadHandle> thread, int object_id)
                     : thread_(thread), delegate_(thread, object_id) {}

  void SendMessageToFrontend(const v8_inspector::StringView& message) override {
    delegate_.Call(
        [m = StringBuffer::create(message)]
        (InspectorSessionDelegate* delegate) {
      delegate->SendMessageToFrontend(m->string());
    });
  }

 private:
  std::shared_ptr<MainThreadHandle> thread_;
  AnotherThreadObjectReference<InspectorSessionDelegate> delegate_;
};
}  // namespace


MainThreadInterface::MainThreadInterface(Agent* agent) : agent_(agent) {}

MainThreadInterface::~MainThreadInterface() {
  if (handle_)
    handle_->Reset();
}

void MainThreadInterface::Post(std::unique_ptr<Request> request) {
  CHECK_NOT_NULL(agent_);
  Mutex::ScopedLock scoped_lock(requests_lock_);
  bool needs_notify = requests_.empty();
  requests_.push_back(std::move(request));
  if (needs_notify) {
    std::weak_ptr<MainThreadInterface> weak_self {shared_from_this()};
    agent_->env()->RequestInterrupt([weak_self](Environment*) {
      if (auto iface = weak_self.lock()) iface->DispatchMessages();
    });
  }
  incoming_message_cond_.Broadcast(scoped_lock);
}

bool MainThreadInterface::WaitForFrontendEvent() {
  // We allow DispatchMessages reentry as we enter the pause. This is important
  // to support debugging the code invoked by an inspector call, such
  // as Runtime.evaluate
  dispatching_messages_ = false;
  if (dispatching_message_queue_.empty()) {
    Mutex::ScopedLock scoped_lock(requests_lock_);
    while (!stop_waiting_for_frontend_event_requested_ && requests_.empty()) {
      incoming_message_cond_.Wait(scoped_lock);
    }
    stop_waiting_for_frontend_event_requested_ = false;
  }
  return true;
}

void MainThreadInterface::StopWaitingForFrontendEvent() {
  Mutex::ScopedLock scoped_lock(requests_lock_);
  stop_waiting_for_frontend_event_requested_ = true;
  incoming_message_cond_.Broadcast(scoped_lock);
}

void MainThreadInterface::DispatchMessages() {
  if (dispatching_messages_)
    return;
  dispatching_messages_ = true;
  bool had_messages = false;
  do {
    if (dispatching_message_queue_.empty()) {
      Mutex::ScopedLock scoped_lock(requests_lock_);
      requests_.swap(dispatching_message_queue_);
    }
    had_messages = !dispatching_message_queue_.empty();
    while (!dispatching_message_queue_.empty()) {
      MessageQueue::value_type task;
      std::swap(dispatching_message_queue_.front(), task);
      dispatching_message_queue_.pop_front();

      v8::SealHandleScope seal_handle_scope(agent_->env()->isolate());
      task->Call(this);
    }
  } while (had_messages);
  dispatching_messages_ = false;
}

std::shared_ptr<MainThreadHandle> MainThreadInterface::GetHandle() {
  if (handle_ == nullptr)
    handle_ = std::make_shared<MainThreadHandle>(this);
  return handle_;
}

void MainThreadInterface::AddObject(int id,
                                    std::unique_ptr<Deletable> object) {
  CHECK_NOT_NULL(object);
  managed_objects_[id] = std::move(object);
}

void MainThreadInterface::RemoveObject(int id) {
  CHECK_EQ(1, managed_objects_.erase(id));
}

Deletable* MainThreadInterface::GetObject(int id) {
  Deletable* pointer = GetObjectIfExists(id);
  // This would mean the object is requested after it was disposed, which is
  // a coding error.
  CHECK_NOT_NULL(pointer);
  return pointer;
}

Deletable* MainThreadInterface::GetObjectIfExists(int id) {
  auto iterator = managed_objects_.find(id);
  if (iterator == managed_objects_.end()) {
    return nullptr;
  }
  return iterator->second.get();
}

std::unique_ptr<StringBuffer> Utf8ToStringView(const std::string_view message) {
  size_t expected_u16_length =
      simdutf::utf16_length_from_utf8(message.data(), message.length());
  MaybeStackBuffer<char16_t> buffer(expected_u16_length);
  size_t utf16_length = simdutf::convert_utf8_to_utf16(
      message.data(), message.length(), buffer.out());
  StringView view(reinterpret_cast<uint16_t*>(buffer.out()), utf16_length);
  return StringBuffer::create(view);
}

std::unique_ptr<InspectorSession> MainThreadHandle::Connect(
    std::unique_ptr<InspectorSessionDelegate> delegate,
    bool prevent_shutdown) {
  return std::unique_ptr<InspectorSession>(
      new CrossThreadInspectorSession(++next_session_id_,
                                      shared_from_this(),
                                      std::move(delegate),
                                      prevent_shutdown));
}

bool MainThreadHandle::Post(std::unique_ptr<Request> request) {
  Mutex::ScopedLock scoped_lock(block_lock_);
  if (!main_thread_)
    return false;
  main_thread_->Post(std::move(request));
  return true;
}

void MainThreadHandle::Reset() {
  Mutex::ScopedLock scoped_lock(block_lock_);
  main_thread_ = nullptr;
}

std::unique_ptr<InspectorSessionDelegate>
MainThreadHandle::MakeDelegateThreadSafe(
    std::unique_ptr<InspectorSessionDelegate> delegate) {
  int id = newObjectId();
  main_thread_->AddObject(id, WrapInDeletable(std::move(delegate)));
  return std::unique_ptr<InspectorSessionDelegate>(
      new ThreadSafeDelegate(shared_from_this(), id));
}

bool MainThreadHandle::Expired() {
  Mutex::ScopedLock scoped_lock(block_lock_);
  return main_thread_ == nullptr;
}
}  // namespace inspector
}  // namespace node
