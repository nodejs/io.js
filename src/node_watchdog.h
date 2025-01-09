// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_NODE_WATCHDOG_H_
#define SRC_NODE_WATCHDOG_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <vector>
#include "handle_wrap.h"
#include "memory_tracker-inl.h"
#include "node_mutex.h"
#include "uv.h"
#include "v8.h"

#ifdef __POSIX__
#include <pthread.h>
#endif

namespace node {

enum class SignalPropagation {
  kContinuePropagation,
  kStopPropagation,
};

struct WatchdogTimer {
  v8::Isolate* isolate;
  uint64_t expires_at_hrtime;
  bool* timed_out;
};

class WatchdogService {
 public:
  ~WatchdogService();
  void SetTimer(std::shared_ptr<WatchdogTimer> watchdog_timer);
  void ClearTimer(std::shared_ptr<WatchdogTimer> watchdog_timer);

 private:
  std::multimap<uint64_t, std::shared_ptr<WatchdogTimer>> watchdog_timers_;
  uint64_t sleep_until_hrtime_;
  uv_mutex_t mutex_;
  uv_cond_t cond_;
  uv_thread_t thread_;
  bool initialized_;
  bool should_destroy_;
  void Init();
  static void Run(void* arg);
};

class Watchdog {
 public:
  explicit Watchdog(v8::Isolate* isolate,
                    uint64_t ms,
                    bool* timed_out = nullptr);
  ~Watchdog();
  v8::Isolate* isolate() { return watchdog_timer_->isolate; }

 private:
  std::shared_ptr<WatchdogTimer> watchdog_timer_;
};

class SigintWatchdogBase {
 public:
  virtual ~SigintWatchdogBase() = default;
  virtual SignalPropagation HandleSigint() = 0;
};

class SigintWatchdog : public SigintWatchdogBase {
 public:
  explicit SigintWatchdog(v8::Isolate* isolate,
                          bool* received_signal = nullptr);
  ~SigintWatchdog();
  v8::Isolate* isolate() { return isolate_; }
  SignalPropagation HandleSigint() override;

 private:
  v8::Isolate* isolate_;
  bool* received_signal_;
};

class TraceSigintWatchdog : public HandleWrap, public SigintWatchdogBase {
 public:
  static void Init(Environment* env, v8::Local<v8::Object> target);
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Start(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Stop(const v8::FunctionCallbackInfo<v8::Value>& args);

  SignalPropagation HandleSigint() override;

  inline void MemoryInfo(node::MemoryTracker* tracker) const override {
    tracker->TrackInlineField("handle_", handle_);
  }
  SET_MEMORY_INFO_NAME(TraceSigintWatchdog)
  SET_SELF_SIZE(TraceSigintWatchdog)

 private:
  enum class SignalFlags { None, FromIdle, FromInterrupt };

  TraceSigintWatchdog(Environment* env, v8::Local<v8::Object> object);
  void HandleInterrupt();

  bool interrupting = false;
  uv_async_t handle_;
  SignalFlags signal_flag_ = SignalFlags::None;
};

class SigintWatchdogHelper {
 public:
  static SigintWatchdogHelper* GetInstance() { return &instance; }
  static Mutex& GetInstanceActionMutex() { return instance_action_mutex_; }
  void Register(SigintWatchdogBase* watchdog);
  void Unregister(SigintWatchdogBase* watchdog);
  bool HasPendingSignal();

  int Start();
  bool Stop();

 private:
  SigintWatchdogHelper();
  ~SigintWatchdogHelper();

  static bool InformWatchdogsAboutSignal();
  static SigintWatchdogHelper instance;
  static Mutex instance_action_mutex_;

  int start_stop_count_;

  Mutex mutex_;
  Mutex list_mutex_;
  std::vector<SigintWatchdogBase*> watchdogs_;
  bool has_pending_signal_;

#ifdef __POSIX__
  pthread_t thread_;
  uv_sem_t sem_;
  bool has_running_thread_;
  bool stopping_;

  static void* RunSigintWatchdog(void* arg);
  static void HandleSignal(int signum, siginfo_t* info, void* ucontext);
#else
  bool watchdog_disabled_;
  static BOOL WINAPI WinCtrlCHandlerRoutine(DWORD dwCtrlType);
#endif
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_WATCHDOG_H_
