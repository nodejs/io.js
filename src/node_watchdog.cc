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

#include <algorithm>

#include "async_wrap-inl.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node_errors.h"
#include "node_internals.h"
#include "node_watchdog.h"
#include "util-inl.h"

namespace node {

using v8::Context;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Value;

Watchdog::Watchdog(v8::Isolate* isolate, uint64_t ms, bool* timed_out)
    : isolate_(isolate), timed_out_(timed_out) {

  int rc;
  rc = uv_loop_init(&loop_);
  if (rc != 0) {
    UNREACHABLE("Failed to initialize uv loop.");
  }

  rc = uv_async_init(&loop_, &async_, [](uv_async_t* signal) {
    Watchdog* w = ContainerOf(&Watchdog::async_, signal);
    uv_stop(&w->loop_);
  });

  CHECK_EQ(0, rc);

  rc = uv_timer_init(&loop_, &timer_);
  CHECK_EQ(0, rc);

  rc = uv_timer_start(&timer_, &Watchdog::Timer, ms, 0);
  CHECK_EQ(0, rc);

  rc = uv_thread_create(&thread_, &Watchdog::Run, this);
  CHECK_EQ(0, rc);
}


Watchdog::~Watchdog() {
  uv_async_send(&async_);
  uv_thread_join(&thread_);

  uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);

  // UV_RUN_DEFAULT so that libuv has a chance to clean up.
  uv_run(&loop_, UV_RUN_DEFAULT);

  CheckedUvLoopClose(&loop_);
}


void Watchdog::Run(void* arg) {
  Watchdog* wd = static_cast<Watchdog*>(arg);

  // UV_RUN_DEFAULT the loop will be stopped either by the async or the
  // timer handle.
  uv_run(&wd->loop_, UV_RUN_DEFAULT);

  // Loop ref count reaches zero when both handles are closed.
  // Close the timer handle on this side and let ~Watchdog() close async_
  uv_close(reinterpret_cast<uv_handle_t*>(&wd->timer_), nullptr);
}

void Watchdog::Timer(uv_timer_t* timer) {
  Watchdog* w = ContainerOf(&Watchdog::timer_, timer);
  *w->timed_out_ = true;
  w->isolate()->TerminateExecution();
  uv_stop(&w->loop_);
}


SigintWatchdog::SigintWatchdog(
  v8::Isolate* isolate, bool* received_signal)
    : isolate_(isolate), received_signal_(received_signal) {
  Mutex::ScopedLock lock(SigintWatchdogHelper::GetInstanceActionMutex());
  // Register this watchdog with the global SIGINT/Ctrl+C listener.
  SigintWatchdogHelper::GetInstance()->Register(this);
  // Start the helper thread, if that has not already happened.
  SigintWatchdogHelper::GetInstance()->Start();
}


SigintWatchdog::~SigintWatchdog() {
  Mutex::ScopedLock lock(SigintWatchdogHelper::GetInstanceActionMutex());
  SigintWatchdogHelper::GetInstance()->Unregister(this);
  SigintWatchdogHelper::GetInstance()->Stop();
}

SignalPropagation SigintWatchdog::HandleSigint() {
  *received_signal_ = true;
  isolate_->TerminateExecution();
  return SignalPropagation::kStopPropagation;
}

void TraceSigintWatchdog::Init(Environment* env, Local<Object> target) {
  Isolate* isolate = env->isolate();
  Local<FunctionTemplate> constructor = NewFunctionTemplate(isolate, New);
  constructor->InstanceTemplate()->SetInternalFieldCount(
      TraceSigintWatchdog::kInternalFieldCount);
  constructor->Inherit(HandleWrap::GetConstructorTemplate(env));

  SetProtoMethod(isolate, constructor, "start", Start);
  SetProtoMethod(isolate, constructor, "stop", Stop);

  SetConstructorFunction(
      env->context(), target, "TraceSigintWatchdog", constructor);
}

void TraceSigintWatchdog::New(const FunctionCallbackInfo<Value>& args) {
  // This constructor should not be exposed to public javascript.
  // Therefore we assert that we are not trying to call this as a
  // normal function.
  CHECK(args.IsConstructCall());
  Environment* env = Environment::GetCurrent(args);
  new TraceSigintWatchdog(env, args.This());
}

void TraceSigintWatchdog::Start(const FunctionCallbackInfo<Value>& args) {
  TraceSigintWatchdog* watchdog;
  ASSIGN_OR_RETURN_UNWRAP(&watchdog, args.This());
  Mutex::ScopedLock lock(SigintWatchdogHelper::GetInstanceActionMutex());
  // Register this watchdog with the global SIGINT/Ctrl+C listener.
  SigintWatchdogHelper::GetInstance()->Register(watchdog);
  // Start the helper thread, if that has not already happened.
  int r = SigintWatchdogHelper::GetInstance()->Start();
  CHECK_EQ(r, 0);
}

void TraceSigintWatchdog::Stop(const FunctionCallbackInfo<Value>& args) {
  TraceSigintWatchdog* watchdog;
  ASSIGN_OR_RETURN_UNWRAP(&watchdog, args.This());
  Mutex::ScopedLock lock(SigintWatchdogHelper::GetInstanceActionMutex());
  SigintWatchdogHelper::GetInstance()->Unregister(watchdog);
  SigintWatchdogHelper::GetInstance()->Stop();
}

TraceSigintWatchdog::TraceSigintWatchdog(Environment* env, Local<Object> object)
    : HandleWrap(env,
                 object,
                 reinterpret_cast<uv_handle_t*>(&handle_),
                 AsyncWrap::PROVIDER_SIGINTWATCHDOG) {
  int r = uv_async_init(env->event_loop(), &handle_, [](uv_async_t* handle) {
    TraceSigintWatchdog* watchdog =
        ContainerOf(&TraceSigintWatchdog::handle_, handle);
    watchdog->signal_flag_ = SignalFlags::FromIdle;
    watchdog->HandleInterrupt();
  });
  CHECK_EQ(r, 0);
  uv_unref(reinterpret_cast<uv_handle_t*>(&handle_));
}

SignalPropagation TraceSigintWatchdog::HandleSigint() {
  /**
   * In case of uv loop polling, i.e. no JS currently running, activate the
   * loop to run a piece of JS code to trigger interruption.
   */
  CHECK_EQ(uv_async_send(&handle_), 0);
  env()->isolate()->RequestInterrupt(
      [](v8::Isolate* isolate, void* data) {
        TraceSigintWatchdog* self = static_cast<TraceSigintWatchdog*>(data);
        if (self->signal_flag_ == SignalFlags::None) {
          self->signal_flag_ = SignalFlags::FromInterrupt;
        }
        self->HandleInterrupt();
      },
      this);
  return SignalPropagation::kContinuePropagation;
}

void TraceSigintWatchdog::HandleInterrupt() {
  // Do not nest interrupts.
  if (interrupting) {
    return;
  }
  interrupting = true;
  if (signal_flag_ == SignalFlags::None) {
    return;
  }
  Environment* env_ = env();
  // FIXME: Before
  // https://github.com/nodejs/node/pull/29207#issuecomment-527667993 get
  // fixed, additional JavaScript code evaluation shall be prevented from
  // running during interruption.
  FPrintF(stderr,
      "KEYBOARD_INTERRUPT: Script execution was interrupted by `SIGINT`\n");
  if (signal_flag_ == SignalFlags::FromInterrupt) {
    PrintStackTrace(env_->isolate(),
                    v8::StackTrace::CurrentStackTrace(
                        env_->isolate(), 10, v8::StackTrace::kDetailed));
  }
  signal_flag_ = SignalFlags::None;
  interrupting = false;

  Mutex::ScopedLock lock(SigintWatchdogHelper::GetInstanceActionMutex());
  SigintWatchdogHelper::GetInstance()->Unregister(this);
  SigintWatchdogHelper::GetInstance()->Stop();
  raise(SIGINT);
}

#ifdef __POSIX__
void* SigintWatchdogHelper::RunSigintWatchdog(void* arg) {
  // Inside the helper thread.
  bool is_stopping;

  do {
    uv_sem_wait(&instance.sem_);
    is_stopping = InformWatchdogsAboutSignal();
  } while (!is_stopping);

  return nullptr;
}

void SigintWatchdogHelper::HandleSignal(int signum,
                                        siginfo_t* info,
                                        void* ucontext) {
  uv_sem_post(&instance.sem_);
}

#else

// Windows starts a separate thread for executing the handler, so no extra
// helper thread is required.
BOOL WINAPI SigintWatchdogHelper::WinCtrlCHandlerRoutine(DWORD dwCtrlType) {
  if (!instance.watchdog_disabled_ &&
      (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT)) {
    InformWatchdogsAboutSignal();

    // Return true because the signal has been handled.
    return TRUE;
  } else {
    return FALSE;
  }
}
#endif


bool SigintWatchdogHelper::InformWatchdogsAboutSignal() {
  Mutex::ScopedLock list_lock(instance.list_mutex_);

  bool is_stopping = false;
#ifdef __POSIX__
  is_stopping = instance.stopping_;
#endif

  // If there are no listeners and the helper thread has been awoken by a signal
  // (= not when stopping it), indicate that by setting has_pending_signal_.
  if (instance.watchdogs_.empty() && !is_stopping) {
    instance.has_pending_signal_ = true;
  }

  for (auto it = instance.watchdogs_.rbegin(); it != instance.watchdogs_.rend();
       it++) {
    SignalPropagation wp = (*it)->HandleSigint();
    if (wp == SignalPropagation::kStopPropagation) {
      break;
    }
  }

  return is_stopping;
}


int SigintWatchdogHelper::Start() {
  Mutex::ScopedLock lock(mutex_);

  if (start_stop_count_++ > 0) {
    return 0;
  }

#ifdef __POSIX__
  CHECK_EQ(has_running_thread_, false);
  has_pending_signal_ = false;
  stopping_ = false;

  sigset_t sigmask;
  sigfillset(&sigmask);
  sigset_t savemask;
  CHECK_EQ(0, pthread_sigmask(SIG_SETMASK, &sigmask, &savemask));
  sigmask = savemask;
  int ret = pthread_create(&thread_, nullptr, RunSigintWatchdog, nullptr);
  CHECK_EQ(0, pthread_sigmask(SIG_SETMASK, &sigmask, nullptr));
  if (ret != 0) {
    return ret;
  }
  has_running_thread_ = true;

  RegisterSignalHandler(SIGINT, HandleSignal);
#else
  if (watchdog_disabled_) {
    watchdog_disabled_ = false;
  } else {
    SetConsoleCtrlHandler(WinCtrlCHandlerRoutine, TRUE);
  }
#endif

  return 0;
}


bool SigintWatchdogHelper::Stop() {
  bool had_pending_signal;
  Mutex::ScopedLock lock(mutex_);

  {
    Mutex::ScopedLock list_lock(list_mutex_);

    had_pending_signal = has_pending_signal_;

    if (--start_stop_count_ > 0) {
      has_pending_signal_ = false;
      return had_pending_signal;
    }

#ifdef __POSIX__
    // Set stopping now because it's only protected by list_mutex_.
    stopping_ = true;
#endif

    watchdogs_.clear();
  }

#ifdef __POSIX__
  if (!has_running_thread_) {
    has_pending_signal_ = false;
    return had_pending_signal;
  }

  // Wake up the helper thread.
  uv_sem_post(&sem_);

  // Wait for the helper thread to finish.
  CHECK_EQ(0, pthread_join(thread_, nullptr));
  has_running_thread_ = false;

  RegisterSignalHandler(SIGINT, SignalExit, true);
#else
  watchdog_disabled_ = true;
#endif

  had_pending_signal = has_pending_signal_;
  has_pending_signal_ = false;

  return had_pending_signal;
}


bool SigintWatchdogHelper::HasPendingSignal() {
  Mutex::ScopedLock lock(list_mutex_);

  return has_pending_signal_;
}

void SigintWatchdogHelper::Register(SigintWatchdogBase* wd) {
  Mutex::ScopedLock lock(list_mutex_);

  watchdogs_.push_back(wd);
}

void SigintWatchdogHelper::Unregister(SigintWatchdogBase* wd) {
  Mutex::ScopedLock lock(list_mutex_);

  auto it = std::find(watchdogs_.begin(), watchdogs_.end(), wd);

  CHECK_NE(it, watchdogs_.end());
  watchdogs_.erase(it);
}


SigintWatchdogHelper::SigintWatchdogHelper()
    : start_stop_count_(0),
      has_pending_signal_(false) {
#ifdef __POSIX__
  has_running_thread_ = false;
  stopping_ = false;
  CHECK_EQ(0, uv_sem_init(&sem_, 0));
#else
  watchdog_disabled_ = false;
#endif
}


SigintWatchdogHelper::~SigintWatchdogHelper() {
  start_stop_count_ = 0;
  Stop();

#ifdef __POSIX__
  CHECK_EQ(has_running_thread_, false);
  uv_sem_destroy(&sem_);
#endif
}

SigintWatchdogHelper SigintWatchdogHelper::instance;
Mutex SigintWatchdogHelper::instance_action_mutex_;

namespace watchdog {
static void Initialize(Local<Object> target,
                       Local<Value> unused,
                       Local<Context> context,
                       void* priv) {
  Environment* env = Environment::GetCurrent(context);
  TraceSigintWatchdog::Init(env, target);
}
}  // namespace watchdog

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(watchdog, node::watchdog::Initialize)
